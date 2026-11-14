#include "snurra/source.h"

#include "snurra/bytes_input.h"

#include <QCollator>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QMimeData>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>
#include <chrono>
#include <mutex>
#include <thread>
#include <numeric>
#include <cmath>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/intreadwrite.h>
#include <libswresample/swresample.h>
}

namespace {

/* What every generated source shares: a length, a position, and a sample
 * for each frame. */
class generated_source : public snurra_source {
public:
	explicit generated_source(int64_t length) : m_length(length) {}

	int read(float *out, int frames) override {
		const int n = int(std::min<int64_t>(frames, m_length - m_position));
		for (int i = 0; i < n; ++i) sample(m_position + i, out + 2 * i);
		m_position += n;
		return std::max(0, n);
	}
	bool at_end() const override { return m_position >= m_length; }
	void seek(int64_t frame) override { m_position = std::clamp<int64_t>(frame, 0, m_length); }
	int64_t position() const override { return m_position; }
	int64_t length() const override { return m_length; }
	void set_length(int64_t frames) override {
		m_length = std::max<int64_t>(0, frames);
		m_position = std::min(m_position, m_length);
	}

protected:
	virtual void sample(int64_t frame, float *lr) const = 0;

private:
	int64_t m_length;
	int64_t m_position = 0;
};

class silence : public generated_source {
public:
	using generated_source::generated_source;
	bool stand_in() const override { return true; }

protected:
	void sample(int64_t, float *lr) const override { lr[0] = lr[1] = 0; }
};

class tone : public generated_source {
public:
	tone(double hz, int rate, int64_t length) : generated_source(length), m_step(2 * M_PI * hz / rate) {}

protected:
	void sample(int64_t frame, float *lr) const override {
		/* The phase from the frame itself, so a seek lands in phase. */
		lr[0] = lr[1] = float(0.2 * std::sin(m_step * double(frame)));
	}

private:
	double m_step;
};

class wrapped_ramp : public generated_source {
public:
	using generated_source::generated_source;

protected:
	void sample(int64_t frame, float *lr) const override {
		/* At half scale: two at full scale summed in a dmix clip. */
		lr[0] = float(0.5 * double(frame % int64_t(SNURRA_WRAPPED_RAMP_SCALE)) / SNURRA_WRAPPED_RAMP_SCALE);
		lr[1] = 0.5f;
	}
};

class ramp : public generated_source {
public:
	using generated_source::generated_source;

protected:
	void sample(int64_t frame, float *lr) const override {
		lr[0] = float(double(frame) / SNURRA_RAMP_SCALE);
		lr[1] = 1.0f;
	}
};

/*
 * A file, through FFmpeg: demuxed, decoded and resampled to stereo float
 * at the engine's rate. A seek goes to the key frame before the target and
 * decodes forward, dropping what comes before it, so the next read starts
 * at the very frame asked for -- which a handover's taker relies on to
 * sound in step with the giver.
 */
class ffmpeg_source : public snurra_source {
public:
	ffmpeg_source(int rate) : m_rate(rate) {}

	~ffmpeg_source() override {
		swr_free(&m_swr);
		av_channel_layout_uninit(&m_swr_layout);
		avcodec_free_context(&m_codec);
		avformat_close_input(&m_format);
		av_packet_free(&m_packet);
		av_frame_free(&m_frame);
	}

	/* A file, or a URL; `interrupted` gives up a network wait -- in the
	 * opening as in every read after it -- for a close or a seek. */
	bool open(const QString &path, std::function<bool()> interrupted = nullptr) {
		m_interrupted = std::move(interrupted);
		m_format = avformat_alloc_context();
		if (!m_format) return false;
		m_format->interrupt_callback.opaque = this;
		m_format->interrupt_callback.callback = [](void *opaque) -> int {
			const ffmpeg_source *self = static_cast<const ffmpeg_source *>(opaque);
			return self->m_interrupted && self->m_interrupted() ? 1 : 0;
		};
		if (avformat_open_input(&m_format, QFile::encodeName(path).constData(), nullptr, nullptr) < 0) return false;
		return open_streams(true);
	}

	/*
	 * Over bytes that may still be arriving: FFmpeg reads through
	 * read_bytes() below, which waits for what is missing, so this blocks
	 * until the format is known -- the deferred start -- and the decoding
	 * after it stalls where the bytes stop.
	 */
	bool open(std::shared_ptr<snurra_bytes> bytes, const QString &name, std::function<bool()> interrupted) {
		m_interrupted = interrupted;
		m_format = m_input.open(std::move(bytes), name, std::move(interrupted));
		return m_format && open_streams(false);
	}

	void set_interrupt(std::function<bool()> interrupted) override { m_interrupted = std::move(interrupted); }

	/* The decoder and resampler for stream `index`, the one read from here
	 * on; what came before is closed. */
	bool open_codec(int index) {
		avcodec_free_context(&m_codec);
		swr_free(&m_swr);
		if (index < 0 || index >= int(m_format->nb_streams)) return false;
		const AVCodec *codec = avcodec_find_decoder(m_format->streams[index]->codecpar->codec_id);
		if (!codec) return false;
		m_stream = index;
		const AVStream *stream = m_format->streams[m_stream];
		m_codec = avcodec_alloc_context3(codec);

		if (!m_codec || avcodec_parameters_to_context(m_codec, stream->codecpar) < 0) return false;
		if (avcodec_open2(m_codec, codec, nullptr) < 0) return false;
		if (!make_resampler()) return false;

		/*
		 * How far before a target a seek starts decoding. A lossy codec's
		 * frame overlaps the one before it (MDCT overlap-add), and MP3 also
		 * borrows bits from earlier frames, so a decoder restarted at the
		 * target gives different samples for a frame or two: the right
		 * place, the wrong sound -- a glitch on a cue, and two different
		 * signals summed in a handover's crossfade. So four codec frames
		 * before it, or the codec's own seek pre-roll where it states one
		 * (Opus: 80 ms); and a resampler's filter needs its history too.
		 * Played-through and seeked must give the same samples.
		 */
		{
			const AVCodecDescriptor *descriptor = avcodec_descriptor_get(m_codec->codec_id);
			const AVCodecParameters *parameters = stream->codecpar;
			int64_t preroll_in = 0;
			if (descriptor && (descriptor->props & AV_CODEC_PROP_LOSSY)) {
				const int64_t frame = parameters->frame_size > 0 ? parameters->frame_size : 2048;
				preroll_in = std::max<int64_t>(parameters->seek_preroll, 4 * frame);
			}
			if (m_codec->sample_rate != m_rate) preroll_in += 256;
			m_preroll = m_codec->sample_rate > 0 ? av_rescale(preroll_in, m_rate, m_codec->sample_rate) : 0;
		}
		return true;
	}

	QStringList audio_tracks() const override { return m_audio_streams.size() > 1 ? m_audio_names : QStringList(); }
	int audio_track() const override {
		for (size_t i = 0; i < m_audio_streams.size(); ++i)
			if (m_audio_streams[i] == m_stream) return int(i);
		return -1;
	}
	/* Takes effect from the next seek, which the deck makes at once. */
	bool choose_audio_track(int track) override {
		if (track < 0 || track >= int(m_audio_streams.size()) || m_audio_streams[size_t(track)] == m_stream) return false;
		const int was = m_stream;
		if (open_codec(m_audio_streams[size_t(track)])) return true;
		open_codec(was);
		return false;
	}

	bool open_streams(bool find_info) {
		if (find_info && avformat_find_stream_info(m_format, nullptr) < 0) return false;
		/* Every sound track a decoder exists for, named by its language
		 * and title; FFmpeg's best (the default-marked, else the most
		 * channels) plays first. */
		for (unsigned i = 0; i < m_format->nb_streams; ++i) {
			const AVStream *in = m_format->streams[i];
			if (in->codecpar->codec_type != AVMEDIA_TYPE_AUDIO || !avcodec_find_decoder(in->codecpar->codec_id)) continue;
			QStringList said;
			if (const AVDictionaryEntry *language = av_dict_get(in->metadata, "language", nullptr, 0))
				said << QString::fromUtf8(language->value);
			if (const AVDictionaryEntry *title = av_dict_get(in->metadata, "title", nullptr, 0))
				said << QString::fromUtf8(title->value);
			if (said.isEmpty()) said << QStringLiteral("track %1").arg(m_audio_streams.size() + 1);
			m_audio_streams.push_back(int(i));
			m_audio_names << said.join(QStringLiteral(", "));
		}
		if (!open_codec(av_find_best_stream(m_format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0))) return false;
		m_packet = av_packet_alloc();
		m_frame = av_frame_alloc();
		if (!m_packet || !m_frame) return false;
		const AVStream *stream = m_format->streams[m_stream];

		if (stream->duration > 0)
			m_length = av_rescale_q(stream->duration, stream->time_base, AVRational{ 1, m_rate });
		else if (m_format->duration > 0)
			m_length = av_rescale(m_format->duration, m_rate, AV_TIME_BASE);
		/* Only from bytes all here: read for its end, a file still
		 * arriving would wait for its last bytes before playing its first. */
		snurra_bytes *bytes = m_input.bytes();
		if (!bytes || (bytes->size() > 0 && bytes->has(std::max<int64_t>(0, bytes->size() - 262144),
		                                               std::min<int64_t>(bytes->size(), 262144))))
			find_end();
		seek(0);
		return true;
	}

	int read(float *out, int frames) override {
		m_waiting = false;
		while (pending_frames() < size_t(frames) && !m_ended && !m_waiting) decode_some();
		int n = int(std::min(pending_frames(), size_t(frames)));
		/* Not past the end the packets say: a codec's padding is not audio. */
		if (m_exact_length) n = int(std::max<int64_t>(0, std::min<int64_t>(n, m_length - m_position)));
		std::copy(m_pending.begin() + 2 * m_pending_from, m_pending.begin() + 2 * (m_pending_from + n), out);
		m_pending_from += n;
		m_position += n;
		if (m_pending_from > 65536) {
			m_pending.erase(m_pending.begin(), m_pending.begin() + 2 * m_pending_from);
			m_pending_from = 0;
		}
		return n;
	}

	bool at_end() const override {
		return (m_ended && pending_frames() == 0) || (m_exact_length && m_position >= m_length);
	}

	void seek(int64_t frame) override {
		const AVStream *stream = m_format->streams[m_stream];
		/* From a pre-roll before the target, decoded and dropped, so the
		 * target's samples are the ones played-through gives (see m_preroll). */
		int64_t ts = av_rescale_q(std::max<int64_t>(0, frame - m_preroll), AVRational{ 1, m_rate }, stream->time_base);
		if (stream->start_time != AV_NOPTS_VALUE) ts += stream->start_time;
		/* A wait given up leaves the byte reader in error; a seek starts
		 * it afresh. */
		if (m_format->pb) {
			m_format->pb->error = 0;
			m_format->pb->eof_reached = 0;
		}
		av_seek_frame(m_format, m_stream, ts, AVSEEK_FLAG_BACKWARD);
		avcodec_flush_buffers(m_codec);
		make_resampler();
		m_pending.clear();
		m_pending_from = 0;
		m_ended = false;
		m_waiting = false;
		m_draining = false;
		m_position = frame;
		m_trim_to = frame;
		m_skip = 0;
		m_front_pts = AV_NOPTS_VALUE;
		m_place_next = true;
		m_untimed = 0;
	}

	int64_t position() const override { return m_position; }
	int64_t length() const override { return m_length; }

private:
	/*
	 * The length, from the last packets rather than the stream's duration:
	 * Opus counts its pre-skip in the duration and MP3 its padding, and an
	 * AAC file in MP4 ends its last packet early -- 256 of 1024 samples --
	 * where the decoder still gives the whole frame. The end is the last
	 * packet's pts and duration, less what its side data says to discard
	 * -- unless the packet is already short by it: Ogg shortens the last
	 * packet's duration and names the discard too (Opus 312 + 648 = 960),
	 * MP3 keeps the full 1152 and names 47. So a last packet as long as the
	 * one before it still holds its padding, and a shorter one does not.
	 * The source plays to there and no further. Read from near the end, a
	 * bounded number of packets; a file that will not seek keeps the
	 * duration's guess.
	 */
	void find_end() {
		const AVStream *stream = m_format->streams[m_stream];
		const int64_t origin = stream->start_time != AV_NOPTS_VALUE ? stream->start_time : 0;
		int64_t guess = stream->duration > 0 ? stream->duration + origin
		                                     : av_rescale_q(m_format->duration, AV_TIME_BASE_Q, stream->time_base);
		if (guess <= 0) return;
		const int64_t back = av_rescale_q(10 * AV_TIME_BASE, AV_TIME_BASE_Q, stream->time_base);
		if (avformat_seek_file(m_format, m_stream, INT64_MIN, std::max<int64_t>(0, guess - back), INT64_MAX, 0) < 0)
			return;
		int64_t end = AV_NOPTS_VALUE;
		int64_t previous = 0;
		for (int packets = 0; packets < 100000 && av_read_frame(m_format, m_packet) >= 0; ++packets) {
			if (m_packet->stream_index == m_stream && m_packet->pts != AV_NOPTS_VALUE && m_packet->duration > 0) {
				int64_t here = m_packet->pts + m_packet->duration;
				size_t size = 0;
				const uint8_t *skip = av_packet_get_side_data(m_packet, AV_PKT_DATA_SKIP_SAMPLES, &size);
				const bool whole = m_packet->duration >= previous;
				previous = m_packet->duration;
				if (whole && skip && size >= 8 && m_codec->sample_rate > 0) {
					const int64_t trailing = int64_t(AV_RL32(skip + 4));
					here -= av_rescale_q(trailing, AVRational{ 1, m_codec->sample_rate }, stream->time_base);
				}
				end = std::max(end, here);
			}
			av_packet_unref(m_packet);
		}
		if (end == AV_NOPTS_VALUE || end <= origin) return;
		m_length = av_rescale_q(end - origin, stream->time_base, AVRational{ 1, m_rate });
		m_exact_length = true;
	}

	size_t pending_frames() const { return m_pending.size() / 2 - m_pending_from; }

	/*
	 * The resampler for what is decoded: the decoder's word for it at
	 * first, and again from each frame where the frame says otherwise. On
	 * FFmpeg 9.0 (Android's release kit, 2026-11-13) an Opus frame's
	 * layout was not the decoder's, a resampler made for one read the
	 * other as planes it did not have, and the deck's reader died in
	 * swr_convert on a null plane.
	 */
	bool make_resampler(const AVChannelLayout *layout = nullptr, int format = -1, int rate = 0) {
		swr_free(&m_swr);
		if (!layout) layout = &m_codec->ch_layout;
		if (format < 0) format = m_codec->sample_fmt;
		if (rate <= 0) rate = m_codec->sample_rate;
		av_channel_layout_uninit(&m_swr_layout);
		av_channel_layout_copy(&m_swr_layout, layout);
		m_swr_format = format;
		m_swr_rate = rate;
		AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
		if (swr_alloc_set_opts2(&m_swr, &stereo, AV_SAMPLE_FMT_FLT, m_rate, layout, AVSampleFormat(format), rate, 0, nullptr) < 0)
			return false;
		return swr_init(m_swr) >= 0;
	}

	/* One more frame decoded and resampled onto the pending samples, or
	 * the end noted. */
	void decode_some() {
		const int got = avcodec_receive_frame(m_codec, m_frame);
		if (got == 0) {
			append(m_frame);
			av_frame_unref(m_frame);
			return;
		}
		if (got == AVERROR_EOF) {
			append(nullptr);   /* what the resampler still holds */
			m_ended = true;
			return;
		}
		if (got != AVERROR(EAGAIN)) {
			m_ended = true;
			return;
		}
		if (m_draining) {
			m_ended = true;
			return;
		}
		/* The decoder wants input. */
		for (;;) {
			const int read = av_read_frame(m_format, m_packet);
			if (read < 0 && m_interrupted && m_interrupted()) {
				/* A wait for bytes given up: not the end, only a pause. */
				m_waiting = true;
				return;
			}
			if (read < 0) {
				avcodec_send_packet(m_codec, nullptr);
				m_draining = true;
				return;
			}
			/* A packet cut short by a wait given up comes back marked
			 * corrupt; decoded, it would put garbage in the stream. */
			if ((m_packet->flags & AV_PKT_FLAG_CORRUPT) && m_interrupted && m_interrupted()) {
				av_packet_unref(m_packet);
				m_waiting = true;
				return;
			}
			const bool ours = m_packet->stream_index == m_stream;
			if (ours && m_place_next && m_front_pts == AV_NOPTS_VALUE) {
				/* The first packet after a seek, to tell its first frame by. */
				m_front_pts = m_packet->pts;
				m_front_duration = m_packet->duration;
			}
			if (ours) avcodec_send_packet(m_codec, m_packet);
			av_packet_unref(m_packet);
			if (ours) return;
		}
	}

	void append(const AVFrame *frame) {
		if (frame
		    && (frame->format != m_swr_format || frame->sample_rate != m_swr_rate
		        || av_channel_layout_compare(&frame->ch_layout, &m_swr_layout) != 0)
		    && !make_resampler(&frame->ch_layout, frame->format, frame->sample_rate))
			return;
		if (!m_swr) return;
		int in = frame ? frame->nb_samples : 0;
		const uint8_t *planes[AV_NUM_DATA_POINTERS] = {};
		if (frame)
			for (int i = 0; i < AV_NUM_DATA_POINTERS && i < frame->ch_layout.nb_channels; ++i)
				planes[i] = frame->extended_data[i];
		/*
		 * A frame with no time cannot be placed: after an HLS seek the first
		 * AAC frame of the segment has none (2026-11-14), and placed as it
		 * was it played from the segment's start, 2 s before the target.
		 * So it is dropped, and the next frame with a time is placed --
		 * after a seek past the start only, and sixteen at most, so a
		 * stream with no times at all still plays as it did.
		 */
		if (m_place_next && frame && m_trim_to > 0 && frame->pts == AV_NOPTS_VALUE
		    && frame->best_effort_timestamp == AV_NOPTS_VALUE && m_untimed < 16) {
			++m_untimed;
			return;
		}
		if (m_place_next && frame) {
			m_place_next = false;
			const int skip = place(frame);
			if (skip > 0) {
				/* Into the frame: the resampler starts on its grid. */
				const int bytes = av_get_bytes_per_sample(AVSampleFormat(frame->format));
				const bool planar = av_sample_fmt_is_planar(AVSampleFormat(frame->format));
				const int channels = frame->ch_layout.nb_channels;
				for (int i = 0; i < (planar ? channels : 1) && i < AV_NUM_DATA_POINTERS; ++i)
					planes[i] += size_t(skip) * size_t(bytes) * size_t(planar ? 1 : channels);
				in -= skip;
			}
		}
		const int room = swr_get_out_samples(m_swr, in);
		if (room > 0) {
			const size_t at = m_pending.size();
			m_pending.resize(at + 2 * size_t(room));
			uint8_t *out = reinterpret_cast<uint8_t *>(m_pending.data() + at);
			const int made = swr_convert(m_swr, &out, room, frame ? planes : nullptr, in);
			m_pending.resize(at + 2 * size_t(std::max(0, made)));
		}
		/* The drop may span several decoded frames when the key frame is
		 * far before the target. */
		const size_t drop = size_t(std::min<int64_t>(m_skip, int64_t(pending_frames())));
		m_pending_from += drop;
		m_skip -= int64_t(drop);
	}

	/*
	 * The first frame decoded after a seek says where its samples really
	 * are. Its own pts, not best_effort_timestamp: when the decoder drops
	 * a codec's priming (MP3's encoder delay, AAC's, Opus's pre-skip) it
	 * moves the frame's pts past what it dropped, and the guessed
	 * timestamp does not follow -- read, it drops the priming twice.
	 *
	 * From there, two corrections, so a seek lands on the frame played
	 * through would have, to a fraction:
	 *  - resampling, input sample a maps to output frame a * out / in, a
	 *    whole frame only on a grid (every 147 input samples at 44.1 kHz
	 *    into 48); a resampler restarted off the grid plays up to half a
	 *    frame early or late, so it restarts on the next grid point;
	 *  - what lies before the target is dropped, and if the data starts
	 *    after it, silence fills the gap rather than the data being moved.
	 * Returns how many input samples to skip in this frame.
	 */
	int place(const AVFrame *frame) {
		const AVStream *stream = m_format->streams[m_stream];
		int64_t pts = frame->pts != AV_NOPTS_VALUE ? frame->pts : frame->best_effort_timestamp;
		if (pts == AV_NOPTS_VALUE) return 0;
		if (stream->start_time != AV_NOPTS_VALUE) pts -= stream->start_time;
		const int in_rate = frame->sample_rate > 0 ? frame->sample_rate : m_codec->sample_rate;
		int64_t first_in = av_rescale_q(pts, stream->time_base, AVRational{ 1, in_rate });
		/*
		 * The decoder dropped the priming from the front of the first frame
		 * without moving its pts past it -- FFmpeg 7.1 does so for MP3 (told
		 * by the packet's side data) and Opus (by its header's pre-skip),
		 * and shortens the frame's own duration for one and not the other.
		 * So the packet decides: a frame at the first packet's pts that is
		 * shorter than that packet had the difference dropped from its
		 * front; a frame whose pts was moved does not match.
		 */
		const int64_t origin = stream->start_time != AV_NOPTS_VALUE ? stream->start_time : 0;
		const AVRational base = stream->time_base;
		const bool first_packet = pts + origin == m_front_pts && m_front_duration > 0;
		if (first_packet && (int64_t(frame->nb_samples) * base.den) % (int64_t(in_rate) * base.num) != 0
		    && origin == 0 && m_front_pts % m_front_duration == 0) {
			/*
			 * A raw stream in a time base too coarse for its frames: AC3 at
			 * 44.1 kHz in 1/90000, 3134.69 ticks a frame, counted as 3134.
			 * Its timestamps are whole packet durations summed, so the pts
			 * a seek lands on lags by 0.69 ticks a frame -- 50 frames by
			 * the fifth second. The frame's index is exact, though, and so
			 * is the position it gives.
			 */
			first_in = m_front_pts / m_front_duration * frame->nb_samples;
		} else if (first_packet) {
			const int64_t whole = av_rescale_q(m_front_duration, base, AVRational{ 1, in_rate });
			if (whole > frame->nb_samples) first_in += whole - frame->nb_samples;
		}

		const int64_t common = std::gcd(int64_t(in_rate), int64_t(m_rate));
		const int64_t per_in = in_rate / common;
		const int64_t per_out = m_rate / common;
		int64_t aligned_in = first_in;
		if (per_in > 1) {
			aligned_in = first_in >= 0 ? (first_in + per_in - 1) / per_in * per_in : -((-first_in) / per_in * per_in);
			if (aligned_in - first_in >= frame->nb_samples) aligned_in = first_in;   /* no grid point here */
		}
		const int skip = int(aligned_in - first_in);
		const int64_t first_out = aligned_in % per_in == 0 ? aligned_in / per_in * per_out
		                                                    : av_rescale(aligned_in, m_rate, in_rate);
		if (first_out > m_trim_to) {
			const size_t gap = size_t(first_out - m_trim_to);
			m_pending.insert(m_pending.begin() + 2 * m_pending_from, 2 * gap, 0.0f);
			m_skip = 0;
		} else {
			m_skip = m_trim_to - first_out;
		}
		return skip;
	}

	/* FFmpeg's reads, from bytes that may still be arriving: what is not
	 * here yet is waited for, and the wait given up when interrupted. */
	snurra_bytes_input m_input;   /* first, so it goes last: the format reads through it */
	std::function<bool()> m_interrupted;
	bool m_waiting = false;   /* the last read gave up a wait for bytes */

	int m_rate;
	AVFormatContext *m_format = nullptr;
	AVCodecContext *m_codec = nullptr;
	SwrContext *m_swr = nullptr;
	AVChannelLayout m_swr_layout{};   /* what m_swr was made for */
	int m_swr_format = -1;
	int m_swr_rate = 0;
	AVPacket *m_packet = nullptr;
	AVFrame *m_frame = nullptr;
	std::vector<int> m_audio_streams;   /* the sound tracks, by stream index */
	QStringList m_audio_names;
	int m_stream = -1;
	int64_t m_length = 0;
	bool m_exact_length = false;
	int64_t m_position = 0;
	std::vector<float> m_pending;
	size_t m_pending_from = 0;
	bool m_ended = false;
	bool m_draining = false;
	bool m_place_next = false;
	int m_untimed = 0;   /* frames with no time dropped before placing, since the seek */
	int64_t m_trim_to = 0;
	int64_t m_skip = 0;   /* frames still to drop to reach m_trim_to */
	int64_t m_preroll = 0;   /* frames decoded and dropped before a seek's target */
	int64_t m_front_pts = AV_NOPTS_VALUE;   /* the first packet after a seek */
	int64_t m_front_duration = 0;
};

}

void snurra_bytes::wait(int ms) {
	std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

std::unique_ptr<snurra_source> snurra_open_source(std::shared_ptr<snurra_bytes> bytes, const QString &name, int rate,
                                                  std::function<bool()> interrupted) {
	auto source = std::make_unique<ffmpeg_source>(rate);
	if (!source->open(std::move(bytes), name, std::move(interrupted))) return nullptr;
	return source;
}

bool snurra_is_url(const QString &name) {
	static const QRegularExpression url(QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]+://"));
	return url.match(name).hasMatch();
}

/* A folder's own files are only those it plays: by the name's type, so a
 * cover or a cue sheet beside the tracks makes no tile. Nor a playlist,
 * though shared-mime-info files .m3u, .pls and .asx under audio/: the
 * first is known as text, and the other two are named, having no text
 * among their ancestors in Qt's database (measured, Qt 6.8). */
bool snurra_playable_by_name(const QString &path) {
	static const QMimeDatabase types;
	const QMimeType type = types.mimeTypeForFile(path, QMimeDatabase::MatchExtension);
	const QString name = type.name();
	if (type.inherits(QStringLiteral("text/plain")) || type.inherits(QStringLiteral("application/xml"))
	    || name == QLatin1String("audio/x-scpls") || name == QLatin1String("audio/x-ms-asx"))
		return false;
	return name.startsWith(QLatin1String("audio/")) || name.startsWith(QLatin1String("video/"))
	       || name == QLatin1String("application/ogg");
}

QStringList snurra_dropped(const QMimeData *mime, int folder_most) {
	QStringList found;
	if (!mime) return found;
	if (mime->hasFormat(QLatin1String(SNURRA_FILES_MIME)))
		return QString::fromUtf8(mime->data(QLatin1String(SNURRA_FILES_MIME))).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
	QList<QUrl> urls = mime->urls();
	/* Some programs drag a link as text alone. */
	if (urls.isEmpty() && snurra_is_url(mime->text().trimmed())) urls << QUrl(mime->text().trimmed());
	for (const QUrl &url : urls) {
		if (!url.isLocalFile()) {
			if (url.isValid() && snurra_is_url(url.toString())) found << url.toString();
			continue;
		}
		const QFileInfo info(url.toLocalFile());
		if (info.isFile() && snurra_is_playlist(info.absoluteFilePath())) {
			found << snurra_playlist(info.absoluteFilePath());
		} else if (info.isFile()) {
			found << info.absoluteFilePath();
		} else if (info.isDir()) {
			/* A folder of albums, not a home: what is looked at is capped
			 * too, so a drop of / does not walk the disk on the UI thread. */
			constexpr int LOOKED_AT_MOST = 4096;
			int looked_at = 0;
			QStringList inside;
			QDirIterator it(info.absoluteFilePath(), QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
			while (it.hasNext() && looked_at++ < LOOKED_AT_MOST) {
				const QString path = it.next();
				if (snurra_playable_by_name(path)) inside << path;
			}
			QCollator counting;
			counting.setNumericMode(true);
			std::sort(inside.begin(), inside.end(),
			          [&counting](const QString &a, const QString &b) { return counting.compare(a, b) < 0; });
			found << inside.mid(0, folder_most);
		}
	}
	return found;
}

bool snurra_is_playlist(const QString &path) {
	const QString suffix = QFileInfo(path).suffix().toLower();
	return suffix == QLatin1String("m3u") || suffix == QLatin1String("m3u8") || suffix == QLatin1String("pls");
}

QStringList snurra_playlist(const QString &path, int most) {
	QStringList entries;
	if (!snurra_is_playlist(path)) return entries;
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) return entries;
	const QDir beside = QFileInfo(path).absoluteDir();
	const QStringList lines = QString::fromUtf8(file.read(1 << 20)).split(QLatin1Char('\n'));
	const bool pls = QFileInfo(path).suffix().compare(QLatin1String("pls"), Qt::CaseInsensitive) == 0;
	QMap<int, QString> numbered;   /* pls: FileN=, in N's order */
	static const QRegularExpression file_n(QStringLiteral("^File(\\d+)=(.*)$"), QRegularExpression::CaseInsensitiveOption);
	for (QString line : lines) {
		line = line.trimmed();
		if (line.isEmpty()) continue;
		if (pls) {
			const QRegularExpressionMatch match = file_n.match(line);
			if (match.hasMatch()) numbered.insert(match.captured(1).toInt(), match.captured(2).trimmed());
		} else if (!line.startsWith(QLatin1Char('#'))) {
			numbered.insert(int(numbered.size()), line);
		}
	}
	for (const QString &entry : numbered) {
		if (int(entries.size()) >= most) break;
		QString name = entry;
		if (name.startsWith(QLatin1String("file://"), Qt::CaseInsensitive)) name = QUrl(name).toLocalFile();
		else if (!snurra_is_url(name)) name = QDir::cleanPath(beside.absoluteFilePath(name));
		if (!snurra_is_url(name) && snurra_is_playlist(name)) continue;
		/* The same file again straight after itself is one entry: hydra
		 * writes a saved mix's tracklist as the mix once a track, each
		 * with VLC's start-time (inbox 20261113T1442Z), which played as
		 * the whole mix once a track. Its tracks are its chapters, or the
		 * cue sheet hydra writes beside it. */
		if (!entries.isEmpty() && entries.last() == name) continue;
		entries << name;
	}
	return entries;
}

QStringList snurra_stem_names(const QString &path) {
	const QString name = QFileInfo(path).fileName();
	if (!name.endsWith(QLatin1String(".stem.mp4"), Qt::CaseInsensitive)
	    && !name.endsWith(QLatin1String(".stem.m4a"), Qt::CaseInsensitive))
		return {};
	return { QStringLiteral("drums"), QStringLiteral("bass"), QStringLiteral("other"), QStringLiteral("vocals") };
}

snurra_tags snurra_read_tags(const QString &path) {
	snurra_tags tags;
	AVFormatContext *format = nullptr;
	if (avformat_open_input(&format, path.toUtf8().constData(), nullptr, nullptr) < 0) return tags;
	auto take = [](const AVDictionary *from, const char *key) {
		const AVDictionaryEntry *entry = av_dict_get(from, key, nullptr, 0);
		return entry && entry->value ? QString::fromUtf8(entry->value).trimmed() : QString();
	};
	tags.title = take(format->metadata, "title");
	tags.artist = take(format->metadata, "artist");
	/* Ogg and Opus keep theirs on the stream. */
	for (unsigned i = 0; i < format->nb_streams && (tags.title.isEmpty() || tags.artist.isEmpty()); ++i) {
		if (tags.title.isEmpty()) tags.title = take(format->streams[i]->metadata, "title");
		if (tags.artist.isEmpty()) tags.artist = take(format->streams[i]->metadata, "artist");
	}
	avformat_close_input(&format);
	return tags;
}

std::vector<snurra_chapter> snurra_read_chapters(const QString &path) {
	std::vector<snurra_chapter> chapters;
	AVFormatContext *format = nullptr;
	if (avformat_open_input(&format, path.toUtf8().constData(), nullptr, nullptr) < 0) return chapters;
	/* The sound's zero, which a deck counts from. */
	int64_t origin_us = 0;
	for (unsigned i = 0; i < format->nb_streams; ++i) {
		const AVStream *stream = format->streams[i];
		if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && stream->start_time != AV_NOPTS_VALUE) {
			origin_us = av_rescale_q(stream->start_time, stream->time_base, AVRational{ 1, 1000000 });
			break;
		}
	}
	for (unsigned i = 0; i < format->nb_chapters; ++i) {
		const AVChapter *chapter = format->chapters[i];
		snurra_chapter one;
		one.start_us = std::max<int64_t>(0, av_rescale_q(chapter->start, chapter->time_base, AVRational{ 1, 1000000 }) - origin_us);
		if (const AVDictionaryEntry *title = av_dict_get(chapter->metadata, "title", nullptr, 0)) one.title = QString::fromUtf8(title->value);
		chapters.push_back(one);
	}
	avformat_close_input(&format);
	std::sort(chapters.begin(), chapters.end(), [](const snurra_chapter &a, const snurra_chapter &b) { return a.start_us < b.start_us; });
	if (chapters.empty()) {
		const QFileInfo info(path);
		const QString beside = info.dir().filePath(info.completeBaseName() + QStringLiteral(".cue"));
		if (QFileInfo(beside).isFile()) chapters = snurra_read_cue(beside, path);
	}
	return chapters;
}

std::vector<snurra_chapter> snurra_read_cue(const QString &cue, const QString &audio) {
	QFile file(cue);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text) || file.size() > 1024 * 1024) return {};
	const QString audio_name = QFileInfo(audio).fileName();
	auto quoted = [](const QString &rest) {
		const QString text = rest.trimmed();
		return text.startsWith(QLatin1Char('"')) ? text.mid(1, text.lastIndexOf(QLatin1Char('"')) - 1) : text;
	};
	/* Each FILE's tracks, then the one for `audio`: the sheet's only FILE
	 * whatever it names (the file renamed since), else the one named so. */
	std::vector<std::vector<snurra_chapter>> per_file;
	int matching = -1;
	QString sheet_performer;
	QString performer;
	QString title;
	bool in_track = false;
	for (QString line : QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'))) {
		line = line.trimmed();
		const QString word = line.section(QLatin1Char(' '), 0, 0).toUpper();
		const QString rest = line.section(QLatin1Char(' '), 1);
		if (word == QLatin1String("FILE")) {
			per_file.emplace_back();
			if (QFileInfo(quoted(rest.left(rest.lastIndexOf(QLatin1Char(' '))))).fileName() == audio_name)
				matching = int(per_file.size()) - 1;
			in_track = false;
		} else if (word == QLatin1String("TRACK")) {
			in_track = true;
			performer = sheet_performer;
			title.clear();
		} else if (word == QLatin1String("PERFORMER")) {
			(in_track ? performer : sheet_performer) = quoted(rest);
		} else if (word == QLatin1String("TITLE")) {
			if (in_track) title = quoted(rest);
		} else if (word == QLatin1String("INDEX") && in_track && !per_file.empty()
		           && rest.section(QLatin1Char(' '), 0, 0) == QLatin1String("01")) {
			const QStringList at = rest.section(QLatin1Char(' '), 1, 1).split(QLatin1Char(':'));
			if (at.size() != 3) continue;
			snurra_chapter one;
			one.start_us = (int64_t(at[0].toInt()) * 60 + at[1].toInt()) * 1000000 + int64_t(at[2].toInt()) * 1000000 / 75;
			one.title = performer.isEmpty() ? title : title.isEmpty() ? performer : performer + QStringLiteral(" - ") + title;
			per_file.back().push_back(one);
		}
	}
	if (per_file.size() == 1) return per_file.front();
	return matching >= 0 ? per_file[size_t(matching)] : std::vector<snurra_chapter>{};
}

bool snurra_drop_offers(const QMimeData *mime) {
	if (!mime) return false;
	if (mime->hasFormat(QLatin1String(SNURRA_FILES_MIME))) return true;
	if (!mime->hasUrls()) return snurra_is_url(mime->text().trimmed());
	for (const QUrl &url : mime->urls()) {
		if (!url.isLocalFile()) {
			if (url.isValid() && snurra_is_url(url.toString())) return true;
			continue;
		}
		const QFileInfo info(url.toLocalFile());
		if (info.isFile() || info.isDir()) return true;
	}
	return false;
}

namespace {
std::mutex resolver_lock;
snurra_bytes_resolver resolver_set;
}   // namespace

QString snurra_source_of(const QString &name) {
	if (snurra_is_url(name)) return name;
	/* A scheme: two letters or more, then a colon -- a drive letter's one
	 * is a path -- and not a file of that name here. */
	static const QRegularExpression scheme(QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]+:"));
	if (scheme.match(name).hasMatch() && !QFileInfo::exists(name)) return name;
	return QFileInfo(name).absoluteFilePath();
}

namespace {
snurra_title_resolver titler_set;
std::function<bool(const QString &)> lasting_set;
}   // namespace

void snurra_set_lasting_names(std::function<bool(const QString &name)> lasting) {
	std::lock_guard<std::mutex> hold(resolver_lock);
	lasting_set = std::move(lasting);
}

bool snurra_name_lasts(const QString &name) {
	std::function<bool(const QString &)> lasting;
	{
		std::lock_guard<std::mutex> hold(resolver_lock);
		lasting = lasting_set;
	}
	return lasting && lasting(name);
}

void snurra_set_title_resolver(snurra_title_resolver resolver) {
	std::lock_guard<std::mutex> hold(resolver_lock);
	titler_set = std::move(resolver);
}

QString snurra_title_of(const QString &name) {
	snurra_title_resolver titler;
	{
		std::lock_guard<std::mutex> hold(resolver_lock);
		titler = titler_set;
	}
	if (titler)
		if (const QString titled = titler(name); !titled.isEmpty()) return titled;
	const QString file = QFileInfo(name).fileName();
	return file.isEmpty() ? name : file;
}

void snurra_set_bytes_resolver(snurra_bytes_resolver resolver) {
	std::lock_guard<std::mutex> hold(resolver_lock);
	resolver_set = std::move(resolver);
}

std::unique_ptr<snurra_source> snurra_open_source(const QString &name, int rate, int64_t fallback_length,
                                                  std::function<bool()> interrupted) {
	if (name == QLatin1String("snurra-test:ramp"))
		return std::make_unique<ramp>(std::min<int64_t>(fallback_length, int64_t(SNURRA_RAMP_SCALE) - 1));
	if (name == QLatin1String("snurra-test:wrapped-ramp")) return std::make_unique<wrapped_ramp>(fallback_length);
	if (name.startsWith(QLatin1String("tone:"))) {
		const double hz = name.mid(5).toDouble();
		if (hz > 0 && hz < rate / 2) return std::make_unique<tone>(hz, rate, fallback_length);
	}
	snurra_bytes_resolver resolver;
	{
		std::lock_guard<std::mutex> hold(resolver_lock);
		resolver = resolver_set;
	}
	if (resolver && !QFileInfo(name).isFile())
		if (std::shared_ptr<snurra_bytes> bytes = resolver(name)) {
			if (std::unique_ptr<snurra_source> found = snurra_open_source(bytes, name, rate, interrupted)) return found;
			return std::make_unique<silence>(fallback_length);
		}
	if (QFileInfo(name).isFile() || snurra_is_url(name)) {
		auto file = std::make_unique<ffmpeg_source>(rate);
		/* Only a URL's waits are given up: a file's reads are not waits. */
		if (file->open(name, snurra_is_url(name) ? std::move(interrupted) : nullptr)) return file;
	}
	return std::make_unique<silence>(fallback_length);
}
