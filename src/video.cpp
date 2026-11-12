#include "snurra/video.h"

#include "snurra/bytes_input.h"

#include <QFile>

#include <algorithm>
#include <cmath>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
}

namespace {

constexpr size_t AHEAD = 6;                 /* frames decoded ahead of the target */
constexpr int64_t JUMP_US = 1000000;        /* further ahead than this: seek, not decode through */
constexpr int WIDEST = 1280;                /* a picture is scaled down to this, never up */
const AVRational MICROSECONDS{ 1, 1000000 };

/*
 * A decoded picture as RGB, at most WIDEST wide, sampled straight to that
 * size so the work is bounded by what is shown. Without libswscale, which
 * this machine's FFmpeg has no headers for and a tiny player need not
 * link: the 8-bit YUV layouts decoders give -- planar 4:2:0, 4:2:2 and
 * 4:4:4, and NV12/NV21 -- in the matrix (BT.601, 709 or 2020) and range
 * (limited or full) the stream says. Any other layout gives no picture.
 */
QImage to_rgb(const AVFrame *from) {
	const AVPixelFormat format = AVPixelFormat(from->format);
	int x_shift = 0;
	int y_shift = 0;
	bool interleaved = false;   /* NV12: one plane of Cb Cr pairs */
	bool swapped = false;       /* NV21: Cr Cb */
	bool full = from->color_range == AVCOL_RANGE_JPEG;
	switch (format) {
	case AV_PIX_FMT_YUVJ420P: full = true; [[fallthrough]];
	case AV_PIX_FMT_YUV420P: x_shift = y_shift = 1; break;
	case AV_PIX_FMT_YUVJ422P: full = true; [[fallthrough]];
	case AV_PIX_FMT_YUV422P: x_shift = 1; break;
	case AV_PIX_FMT_YUVJ444P: full = true; [[fallthrough]];
	case AV_PIX_FMT_YUV444P: break;
	case AV_PIX_FMT_NV21: swapped = true; [[fallthrough]];
	case AV_PIX_FMT_NV12: x_shift = y_shift = 1; interleaved = true; break;
	default: return QImage();
	}
	double kr = 0.299;
	double kb = 0.114;
	if (from->colorspace == AVCOL_SPC_BT709) {
		kr = 0.2126;
		kb = 0.0722;
	} else if (from->colorspace == AVCOL_SPC_BT2020_NCL || from->colorspace == AVCOL_SPC_BT2020_CL) {
		kr = 0.2627;
		kb = 0.0593;
	}
	const double kg = 1 - kr - kb;
	const double y_scale = full ? 1.0 / 255 : 1.0 / 219;
	const double y_zero = full ? 0 : 16;
	const double c_scale = full ? 1.0 / 255 : 1.0 / 224;

	int width = from->width;
	int height = from->height;
	if (width > WIDEST) {
		height = int(int64_t(height) * WIDEST / width);
		width = WIDEST;
	}
	if (width <= 0 || height <= 0) return QImage();
	QImage image(width, height, QImage::Format_RGB32);
	auto clamp = [](double v) { return int(std::lround(std::clamp(v, 0.0, 1.0) * 255)); };
	for (int y = 0; y < height; ++y) {
		const int sy = int(int64_t(y) * from->height / height);
		const uint8_t *luma = from->data[0] + int64_t(sy) * from->linesize[0];
		const int cy = sy >> y_shift;
		const uint8_t *cb_row = from->data[1] + int64_t(cy) * from->linesize[1];
		const uint8_t *cr_row = interleaved ? cb_row : from->data[2] + int64_t(cy) * from->linesize[2];
		QRgb *out = reinterpret_cast<QRgb *>(image.scanLine(y));
		for (int x = 0; x < width; ++x) {
			const int sx = int(int64_t(x) * from->width / width);
			const int cx = sx >> x_shift;
			int cb = interleaved ? cb_row[2 * cx] : cb_row[cx];
			int cr = interleaved ? cr_row[2 * cx + 1] : cr_row[cx];
			if (swapped) std::swap(cb, cr);
			const double l = (luma[sx] - y_zero) * y_scale;
			const double u = (cb - 128) * c_scale;
			const double v = (cr - 128) * c_scale;
			const double r = l + 2 * (1 - kr) * v;
			const double b = l + 2 * (1 - kb) * u;
			const double g = (l - kr * r - kb * b) / kg;
			out[x] = qRgb(clamp(r), clamp(g), clamp(b));
		}
	}
	return image;
}

}

snurra_video::snurra_video(const QString &path) {
	m_thread = std::thread([this, path] { run(path, nullptr); });
}

snurra_video::snurra_video(std::shared_ptr<snurra_bytes> bytes, const QString &name) {
	m_thread = std::thread([this, bytes, name] { run(name, bytes); });
}

snurra_video::~snurra_video() {
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_stop = true;
	}
	m_wake.notify_all();
	if (m_thread.joinable()) m_thread.join();
}

QSize snurra_video::size() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_size;
}

void snurra_video::set_on_open(std::function<void(bool)> on_open) {
	std::lock_guard<std::mutex> lock(m_mutex);
	m_on_open = std::move(on_open);
}

/*
 * The frame for `us`. Frames the target has passed are dropped, keeping the
 * last one not after it; the floor -- the time from which the reader has
 * every frame, or will -- follows the target forward. A target behind the
 * floor is a seek, which the reader sees and makes.
 */
snurra_video_frame snurra_video::frame_at(int64_t us) {
	std::unique_lock<std::mutex> lock(m_mutex);
	m_target = us;
	if (us >= m_floor) {
		while (m_frames.size() >= 2 && m_frames[1].pts_us <= us) m_frames.pop_front();
		if (!m_frames.empty() && m_frames.front().pts_us <= us) m_shown = m_frames.front();
		m_floor = us;
	}
	const snurra_video_frame shown = m_shown.valid() && m_shown.pts_us <= us ? m_shown : snurra_video_frame();
	lock.unlock();
	m_wake.notify_all();
	return shown;
}

/* Under the lock. */
bool snurra_video::want_seek(int64_t target) const {
	if (target < m_floor) return true;
	/* Forward only when not already on the way: a seek decodes from the
	 * key frame before its target, and one far apart (250 frames, x264's
	 * default) took longer than the target moved on -- each new seek began
	 * again from the key frame, and the picture stalled for seconds. */
	if (m_seeking) return false;
	const int64_t reached = m_newest != INT64_MIN ? m_newest : m_floor;
	return !m_ended && target > reached + JUMP_US;
}

void snurra_video::run(const QString &path, std::shared_ptr<snurra_bytes> bytes) {
	snurra_bytes_input input;   /* before the format, so it goes after it */
	AVFormatContext *format = nullptr;
	AVCodecContext *codec = nullptr;
	AVPacket *packet = av_packet_alloc();
	AVFrame *frame = av_frame_alloc();
	AVFrame *pending = nullptr;   /* while seeking: the last frame not after the target */
	int stream_index = -1;
	int64_t origin_us = 0;
	AVStream *stream = nullptr;

	auto opened = [this](bool has_video) {
		std::function<void(bool)> on_open;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			on_open = m_on_open;
		}
		m_has_video.store(has_video, std::memory_order_release);
		if (on_open) on_open(has_video);
	};

	auto open = [&]() -> bool {
		if (bytes) {
			format = input.open(bytes, path, [this] {
				std::lock_guard<std::mutex> lock(m_mutex);
				return m_stop;
			});
			if (!format) return false;
		} else {
			if (avformat_open_input(&format, QFile::encodeName(path).constData(), nullptr, nullptr) < 0) return false;
			if (avformat_find_stream_info(format, nullptr) < 0) return false;
		}
		const AVCodec *decoder = nullptr;
		stream_index = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
		/* A cover image is not a picture to play. */
		if (stream_index < 0 || !decoder || (format->streams[stream_index]->disposition & AV_DISPOSITION_ATTACHED_PIC))
			return false;
		stream = format->streams[stream_index];
		/* The sound's zero, which the deck counts from; the picture's own
		 * where there is no sound. */
		const int audio = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
		const AVStream *zero = audio >= 0 && format->streams[audio]->start_time != AV_NOPTS_VALUE ? format->streams[audio]
		                                                                                          : stream;
		if (zero->start_time != AV_NOPTS_VALUE) origin_us = av_rescale_q(zero->start_time, zero->time_base, MICROSECONDS);
		codec = avcodec_alloc_context3(decoder);
		if (!codec || avcodec_parameters_to_context(codec, stream->codecpar) < 0) return false;
		codec->thread_count = 0;   /* as many as FFmpeg finds useful */
		if (avcodec_open2(codec, decoder, nullptr) < 0) return false;
		const AVRational rate = av_guess_frame_rate(format, stream, nullptr);
		std::lock_guard<std::mutex> lock(m_mutex);
		if (rate.num > 0 && rate.den > 0) m_frame_us = av_rescale_q(1, av_inv_q(rate), MICROSECONDS);
		m_size = QSize(codec->width, codec->height);
		return codec->width > 0 && codec->height > 0;
	};

	auto push = [&](const AVFrame *from, int64_t pts_us) {
		snurra_video_frame next{ to_rgb(from), pts_us };
		std::lock_guard<std::mutex> lock(m_mutex);
		m_frames.push_back(std::move(next));
		m_newest = pts_us;
	};

	if (!open()) {
		opened(false);
	} else {
		opened(true);
		int64_t seek_target = 0;
		bool draining = false;
		for (;;) {
			{
				std::unique_lock<std::mutex> lock(m_mutex);
				m_wake.wait(lock, [this] {
					return m_stop || want_seek(m_target) || (!m_ended && m_frames.size() < AHEAD);
				});
				if (m_stop) break;
				if (want_seek(m_target)) {
					seek_target = m_target;
					m_frames.clear();
					m_floor = seek_target;
					m_newest = INT64_MIN;
					m_ended = false;
					m_seeking = true;
					draining = false;
					lock.unlock();
					av_frame_free(&pending);
					const int64_t ts = av_rescale_q(seek_target + origin_us, MICROSECONDS, stream->time_base);
					av_seek_frame(format, stream_index, ts, AVSEEK_FLAG_BACKWARD);
					avcodec_flush_buffers(codec);
				}
			}

			/* One packet in, every frame it gives out. */
			if (!draining) {
				const int read = av_read_frame(format, packet);
				if (read < 0) {
					draining = true;
					avcodec_send_packet(codec, nullptr);
				} else {
					if (packet->stream_index == stream_index) avcodec_send_packet(codec, packet);
					av_packet_unref(packet);
				}
			}
			for (;;) {
				const int got = avcodec_receive_frame(codec, frame);
				if (got == AVERROR_EOF) {
					if (pending) push(pending, av_rescale_q(pending->best_effort_timestamp, stream->time_base, MICROSECONDS) - origin_us);
					av_frame_free(&pending);
					std::lock_guard<std::mutex> lock(m_mutex);
					m_ended = true;
					m_seeking = false;   /* at the end: nothing more to be on the way to */
					break;
				}
				if (got < 0) break;   /* wants more input */
				if (frame->best_effort_timestamp == AV_NOPTS_VALUE) {
					av_frame_unref(frame);
					continue;
				}
				const int64_t pts_us = av_rescale_q(frame->best_effort_timestamp, stream->time_base, MICROSECONDS) - origin_us;
				bool seeking = false;
				{
					std::lock_guard<std::mutex> lock(m_mutex);
					seeking = m_seeking;
				}
				if (seeking) {
					/* Up to the target, only the last frame is kept: the one
					 * a screen shows at the target. */
					if (pts_us <= seek_target) {
						av_frame_free(&pending);
						pending = av_frame_clone(frame);
						av_frame_unref(frame);
						continue;
					}
					if (pending) push(pending, av_rescale_q(pending->best_effort_timestamp, stream->time_base, MICROSECONDS) - origin_us);
					av_frame_free(&pending);
					std::lock_guard<std::mutex> lock(m_mutex);
					m_seeking = false;
				}
				push(frame, pts_us);
				av_frame_unref(frame);
			}
		}
	}

	av_frame_free(&pending);
	av_frame_free(&frame);
	av_packet_free(&packet);
	avcodec_free_context(&codec);
	avformat_close_input(&format);
}
