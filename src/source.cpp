#include "snurra/source.h"

#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <chrono>
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

protected:
	virtual void sample(int64_t frame, float *lr) const = 0;

private:
	int64_t m_length;
	int64_t m_position = 0;
};

class silence : public generated_source {
public:
	using generated_source::generated_source;

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
		lr[0] = float(double(frame % int64_t(SNURRA_WRAPPED_RAMP_SCALE)) / SNURRA_WRAPPED_RAMP_SCALE);
		lr[1] = 1.0f;
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
		avcodec_free_context(&m_codec);
		avformat_close_input(&m_format);
		if (m_io) av_freep(&m_io->buffer);
		avio_context_free(&m_io);
		av_packet_free(&m_packet);
		av_frame_free(&m_frame);
	}

	bool open(const QString &path) {
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
		m_bytes = std::move(bytes);
		m_interrupted = std::move(interrupted);
		constexpr int BUFFER = 65536;
		uint8_t *buffer = static_cast<uint8_t *>(av_malloc(BUFFER));
		if (!buffer) return false;
		m_io = avio_alloc_context(buffer, BUFFER, 0, this, &ffmpeg_source::read_bytes, nullptr, &ffmpeg_source::seek_bytes);
		if (!m_io) {
			av_free(buffer);
			return false;
		}
		m_bytes->want(0);
		/*
		 * Bytes not all here are opened as a stream, unseekable: FFmpeg's WAV
		 * reader, given a seekable input, seeks past the audio to look for
		 * trailing chunks and waits there -- for the whole file, before
		 * playing any of it. A format that cannot open so (MP4 with its index
		 * at the end) is opened again seekable, and waits, which is honest:
		 * it cannot play before that index arrives. Seeking is turned on once
		 * open, for the seeks a person makes.
		 */
		const bool whole = m_bytes->size() > 0 && m_bytes->has(0, m_bytes->size());
		for (bool seekable : { whole, true }) {
			m_io->seekable = seekable ? AVIO_SEEKABLE_NORMAL : 0;
			m_io_position = 0;
			avio_seek(m_io, 0, SEEK_SET);
			m_io->eof_reached = 0;
			m_io->error = 0;
			m_format = avformat_alloc_context();
			if (!m_format) return false;
			m_format->pb = m_io;
			m_format->flags |= AVFMT_FLAG_CUSTOM_IO;
			/* Not all here: the stream analysis reads up to five seconds of
			 * audio to estimate what headers already say, and would wait
			 * for them; a fifth of a second is enough to know the codec. */
			if (!whole) {
				m_format->max_analyze_duration = AV_TIME_BASE / 5;
				m_format->probesize = 65536;
			}
			if (avformat_open_input(&m_format, QFile::encodeName(name).constData(), nullptr, nullptr) == 0
			    && avformat_find_stream_info(m_format, nullptr) >= 0) {
				m_io->seekable = AVIO_SEEKABLE_NORMAL;
				return open_streams(false);
			}
			avformat_close_input(&m_format);
			if (seekable || (m_interrupted && m_interrupted())) return false;
		}
		return false;
	}

	void set_interrupt(std::function<bool()> interrupted) override { m_interrupted = std::move(interrupted); }

	bool open_streams(bool find_info) {
		if (find_info && avformat_find_stream_info(m_format, nullptr) < 0) return false;
		const AVCodec *codec = nullptr;
		m_stream = av_find_best_stream(m_format, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
		if (m_stream < 0 || !codec) return false;
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
		m_packet = av_packet_alloc();
		m_frame = av_frame_alloc();
		if (!m_packet || !m_frame) return false;

		if (stream->duration > 0)
			m_length = av_rescale_q(stream->duration, stream->time_base, AVRational{ 1, m_rate });
		else if (m_format->duration > 0)
			m_length = av_rescale(m_format->duration, m_rate, AV_TIME_BASE);
		/* Only from bytes all here: read for its end, a file still
		 * arriving would wait for its last bytes before playing its first. */
		if (!m_bytes || (m_bytes->size() > 0 && m_bytes->has(std::max<int64_t>(0, m_bytes->size() - 262144),
		                                                     std::min<int64_t>(m_bytes->size(), 262144))))
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

	bool make_resampler() {
		swr_free(&m_swr);
		AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
		if (swr_alloc_set_opts2(&m_swr, &stereo, AV_SAMPLE_FMT_FLT, m_rate, &m_codec->ch_layout,
		                        m_codec->sample_fmt, m_codec->sample_rate, 0, nullptr)
		    < 0)
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
		int in = frame ? frame->nb_samples : 0;
		const uint8_t *planes[AV_NUM_DATA_POINTERS] = {};
		if (frame)
			for (int i = 0; i < AV_NUM_DATA_POINTERS && i < frame->ch_layout.nb_channels; ++i)
				planes[i] = frame->extended_data[i];
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
	static int read_bytes(void *opaque, uint8_t *buffer, int size) {
		ffmpeg_source *self = static_cast<ffmpeg_source *>(opaque);
		for (;;) {
			const int64_t n = self->m_bytes->read(self->m_io_position, buffer, size);
			if (n > 0) {
				self->m_io_position += n;
				return int(n);
			}
			if (n < 0) return AVERROR_EOF;
			if (self->m_interrupted && self->m_interrupted()) return AVERROR_EXIT;
			/* Waiting, say where: FFmpeg reads through a short seek rather
			 * than seeking, so a seek alone does not tell the transport. */
			if (self->m_wanted != self->m_io_position) {
				self->m_wanted = self->m_io_position;
				self->m_bytes->want(self->m_io_position);
			}
			self->m_bytes->wait(20);
		}
	}

	static int64_t seek_bytes(void *opaque, int64_t offset, int whence) {
		ffmpeg_source *self = static_cast<ffmpeg_source *>(opaque);
		const int64_t size = self->m_bytes->size();
		whence &= ~AVSEEK_FORCE;
		if (whence == AVSEEK_SIZE) return size >= 0 ? size : AVERROR(ENOSYS);
		int64_t to = offset;
		if (whence == SEEK_CUR) to = self->m_io_position + offset;
		else if (whence == SEEK_END) {
			if (size < 0) return AVERROR(ENOSYS);
			to = size + offset;
		} else if (whence != SEEK_SET) {
			return AVERROR(EINVAL);
		}
		if (to < 0) return AVERROR(EINVAL);
		self->m_io_position = to;
		self->m_wanted = to;
		self->m_bytes->want(to);
		return to;
	}

	std::shared_ptr<snurra_bytes> m_bytes;
	std::function<bool()> m_interrupted;
	AVIOContext *m_io = nullptr;
	int64_t m_io_position = 0;
	int64_t m_wanted = -1;   /* the last offset told to the transport */
	bool m_waiting = false;   /* the last read gave up a wait for bytes */

	int m_rate;
	AVFormatContext *m_format = nullptr;
	AVCodecContext *m_codec = nullptr;
	SwrContext *m_swr = nullptr;
	AVPacket *m_packet = nullptr;
	AVFrame *m_frame = nullptr;
	int m_stream = -1;
	int64_t m_length = 0;
	bool m_exact_length = false;
	int64_t m_position = 0;
	std::vector<float> m_pending;
	size_t m_pending_from = 0;
	bool m_ended = false;
	bool m_draining = false;
	bool m_place_next = false;
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

std::unique_ptr<snurra_source> snurra_open_source(const QString &name, int rate, int64_t fallback_length) {
	if (name == QLatin1String("snurra-test:ramp"))
		return std::make_unique<ramp>(std::min<int64_t>(fallback_length, int64_t(SNURRA_RAMP_SCALE) - 1));
	if (name == QLatin1String("snurra-test:wrapped-ramp")) return std::make_unique<wrapped_ramp>(fallback_length);
	if (name.startsWith(QLatin1String("tone:"))) {
		const double hz = name.mid(5).toDouble();
		if (hz > 0 && hz < rate / 2) return std::make_unique<tone>(hz, rate, fallback_length);
	}
	if (QFileInfo(name).isFile()) {
		auto file = std::make_unique<ffmpeg_source>(rate);
		if (file->open(name)) return file;
	}
	return std::make_unique<silence>(fallback_length);
}
