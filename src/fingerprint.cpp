#include "snurra/fingerprint.h"

#include "snurra/source.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

namespace {

constexpr int RATE = 44100;   /* Chromaprint takes it to 11025 itself */

/* One of FFmpeg's chromaprint muxers, writing into `out`. */
class chromaprint_muxer {
public:
	chromaprint_muxer(const char *format, QByteArray *out) : m_out(out) {
		if (avformat_alloc_output_context2(&m_context, nullptr, "chromaprint", nullptr) < 0 || !m_context) return;
		constexpr int BUFFER = 4096;
		auto *buffer = static_cast<unsigned char *>(av_malloc(BUFFER));
		m_context->pb = avio_alloc_context(buffer, BUFFER, 1, this, nullptr, &chromaprint_muxer::write, nullptr);
		m_context->flags |= AVFMT_FLAG_CUSTOM_IO;
		AVStream *stream = avformat_new_stream(m_context, nullptr);
		if (!m_context->pb || !stream) return;
		stream->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
		stream->codecpar->codec_id = AV_CODEC_ID_PCM_S16LE;
		stream->codecpar->sample_rate = RATE;
		av_channel_layout_default(&stream->codecpar->ch_layout, 2);
		stream->time_base = AVRational{ 1, RATE };
		AVDictionary *options = nullptr;
		av_dict_set(&options, "fp_format", format, 0);
		m_open = avformat_write_header(m_context, &options) >= 0;
		av_dict_free(&options);
	}
	~chromaprint_muxer() {
		if (!m_context) return;
		if (m_context->pb) {
			av_freep(&m_context->pb->buffer);
			avio_context_free(&m_context->pb);
		}
		avformat_free_context(m_context);
	}
	bool open() const { return m_open; }
	void add(const int16_t *samples, int frames, int64_t at) {
		if (!m_open) return;
		AVPacket *packet = av_packet_alloc();
		if (av_new_packet(packet, frames * 4) == 0) {
			memcpy(packet->data, samples, size_t(frames) * 4);
			packet->pts = packet->dts = at;
			packet->duration = frames;
			packet->stream_index = 0;
			if (av_write_frame(m_context, packet) < 0) m_open = false;
		}
		av_packet_free(&packet);
	}
	void finish() {
		if (!m_open) return;
		av_write_trailer(m_context);
		avio_flush(m_context->pb);
		m_open = false;
	}

private:
	static int write(void *self, const uint8_t *data, int size) {
		static_cast<chromaprint_muxer *>(self)->m_out->append(reinterpret_cast<const char *>(data), size);
		return size;
	}
	AVFormatContext *m_context = nullptr;
	QByteArray *m_out;
	bool m_open = false;
};

}   // namespace

bool snurra_fingerprints_available() {
	return av_guess_format("chromaprint", nullptr, nullptr) != nullptr;
}

snurra_fingerprint snurra_fingerprint_of(const QString &name, double seconds, const std::function<bool()> &stop) {
	snurra_fingerprint print;
	if (!snurra_fingerprints_available()) return print;
	std::unique_ptr<snurra_source> source = snurra_open_source(name, RATE, 0);
	if (!source || source->length() <= 0 || source->stand_in()) return print;
	QByteArray raw;
	/* Both forms in one reading: the same packets to two muxers. */
	chromaprint_muxer raw_out("raw", &raw);
	chromaprint_muxer text_out("base64", &print.base64);
	if (!raw_out.open() || !text_out.open()) {
		print.base64.clear();
		return print;
	}
	const int64_t most = int64_t(seconds * RATE);
	std::vector<float> block(2 * 4096);
	std::vector<int16_t> pcm(2 * 4096);
	int64_t read = 0;
	while (read < most) {
		if (stop && stop()) return snurra_fingerprint{};
		const int n = source->read(block.data(), int(std::min<int64_t>(4096, most - read)));
		if (n <= 0) break;
		for (int i = 0; i < 2 * n; ++i) pcm[size_t(i)] = int16_t(std::lround(std::clamp(block[size_t(i)], -1.0f, 1.0f) * 32767.0f));
		raw_out.add(pcm.data(), n, read);
		text_out.add(pcm.data(), n, read);
		read += n;
	}
	raw_out.finish();
	text_out.finish();
	print.raw.resize(size_t(raw.size()) / 4);
	memcpy(print.raw.data(), raw.constData(), print.raw.size() * 4);
	print.base64 = print.base64.trimmed();
	print.seconds = double(read) / RATE;
	if (print.raw.empty()) print.base64.clear();
	return print;
}

double snurra_fingerprint_distance(const std::vector<uint32_t> &a, const std::vector<uint32_t> &b, int shift, int least) {
	double best = 1;
	for (int offset = -shift; offset <= shift; ++offset) {
		/* b moved by `offset` against a. */
		const int64_t from = std::max<int64_t>(0, -offset);
		const int64_t to = std::min<int64_t>(int64_t(a.size()), int64_t(b.size()) - offset);
		if (to - from < least) continue;
		int64_t differ = 0;
		for (int64_t i = from; i < to; ++i) differ += __builtin_popcount(a[size_t(i)] ^ b[size_t(i + offset)]);
		best = std::min(best, double(differ) / double(32 * (to - from)));
	}
	return best;
}
