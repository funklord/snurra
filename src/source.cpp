#include "source.h"

#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
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
		av_packet_free(&m_packet);
		av_frame_free(&m_frame);
	}

	bool open(const QString &path) {
		if (avformat_open_input(&m_format, QFile::encodeName(path).constData(), nullptr, nullptr) < 0) return false;
		if (avformat_find_stream_info(m_format, nullptr) < 0) return false;
		const AVCodec *codec = nullptr;
		m_stream = av_find_best_stream(m_format, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
		if (m_stream < 0 || !codec) return false;
		const AVStream *stream = m_format->streams[m_stream];
		m_codec = avcodec_alloc_context3(codec);
		if (!m_codec || avcodec_parameters_to_context(m_codec, stream->codecpar) < 0) return false;
		if (avcodec_open2(m_codec, codec, nullptr) < 0) return false;
		if (!make_resampler()) return false;
		m_packet = av_packet_alloc();
		m_frame = av_frame_alloc();
		if (!m_packet || !m_frame) return false;

		if (stream->duration > 0)
			m_length = av_rescale_q(stream->duration, stream->time_base, AVRational{ 1, m_rate });
		else if (m_format->duration > 0)
			m_length = av_rescale(m_format->duration, m_rate, AV_TIME_BASE);
		return true;
	}

	int read(float *out, int frames) override {
		while (pending_frames() < size_t(frames) && !m_ended) decode_some();
		const int n = int(std::min(pending_frames(), size_t(frames)));
		std::copy(m_pending.begin() + 2 * m_pending_from, m_pending.begin() + 2 * (m_pending_from + n), out);
		m_pending_from += n;
		m_position += n;
		if (m_pending_from > 65536) {
			m_pending.erase(m_pending.begin(), m_pending.begin() + 2 * m_pending_from);
			m_pending_from = 0;
		}
		return n;
	}

	bool at_end() const override { return m_ended && pending_frames() == 0; }

	void seek(int64_t frame) override {
		const AVStream *stream = m_format->streams[m_stream];
		int64_t ts = av_rescale_q(frame, AVRational{ 1, m_rate }, stream->time_base);
		if (stream->start_time != AV_NOPTS_VALUE) ts += stream->start_time;
		av_seek_frame(m_format, m_stream, ts, AVSEEK_FLAG_BACKWARD);
		avcodec_flush_buffers(m_codec);
		make_resampler();
		m_pending.clear();
		m_pending_from = 0;
		m_ended = false;
		m_draining = false;
		m_position = frame;
		m_trim_to = frame;
		m_skip = 0;
		m_place_next = true;
	}

	int64_t position() const override { return m_position; }
	int64_t length() const override { return m_length; }

private:
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
			if (read < 0) {
				avcodec_send_packet(m_codec, nullptr);
				m_draining = true;
				return;
			}
			const bool ours = m_packet->stream_index == m_stream;
			if (ours) avcodec_send_packet(m_codec, m_packet);
			av_packet_unref(m_packet);
			if (ours) return;
		}
	}

	void append(const AVFrame *frame) {
		const int in = frame ? frame->nb_samples : 0;
		const int room = swr_get_out_samples(m_swr, in);
		if (room <= 0) return;
		const size_t at = m_pending.size();
		m_pending.resize(at + 2 * size_t(room));
		uint8_t *out = reinterpret_cast<uint8_t *>(m_pending.data() + at);
		const int made = swr_convert(m_swr, &out, room, frame ? frame->extended_data : nullptr, in);
		m_pending.resize(at + 2 * size_t(std::max(0, made)));

		/*
		 * After a seek, the first frame decoded says where the samples
		 * really are; everything before the frame asked for is dropped,
		 * so the next read starts exactly there.
		 */
		if (m_place_next && frame) {
			m_place_next = false;
			const AVStream *stream = m_format->streams[m_stream];
			int64_t pts = frame->best_effort_timestamp;
			if (pts != AV_NOPTS_VALUE) {
				if (stream->start_time != AV_NOPTS_VALUE) pts -= stream->start_time;
				const int64_t first = av_rescale_q(pts, stream->time_base, AVRational{ 1, m_rate });
				m_skip = std::max<int64_t>(0, m_trim_to - first);
			}
		}
		/* The drop may span several decoded frames when the key frame is
		 * far before the target. */
		const size_t drop = size_t(std::min<int64_t>(m_skip, int64_t(pending_frames())));
		m_pending_from += drop;
		m_skip -= int64_t(drop);
	}

	int m_rate;
	AVFormatContext *m_format = nullptr;
	AVCodecContext *m_codec = nullptr;
	SwrContext *m_swr = nullptr;
	AVPacket *m_packet = nullptr;
	AVFrame *m_frame = nullptr;
	int m_stream = -1;
	int64_t m_length = 0;
	int64_t m_position = 0;
	std::vector<float> m_pending;
	size_t m_pending_from = 0;
	bool m_ended = false;
	bool m_draining = false;
	bool m_place_next = false;
	int64_t m_trim_to = 0;
	int64_t m_skip = 0;   /* frames still to drop to reach m_trim_to */
};

}

std::unique_ptr<snurra_source> snurra_open_source(const QString &name, int rate, int64_t fallback_length) {
	if (name == QLatin1String("snurra-test:ramp"))
		return std::make_unique<ramp>(std::min<int64_t>(fallback_length, int64_t(SNURRA_RAMP_SCALE) - 1));
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
