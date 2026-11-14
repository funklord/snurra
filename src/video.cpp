#include "snurra/video.h"
#include "snurra/source.h"

#include "snurra/bytes_input.h"
#include "snurra/subtitles.h"

#include <QFile>
#include <QTransform>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <thread>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/display.h>
#include <libavutil/pixdesc.h>
}

namespace {

constexpr size_t AHEAD = 6;                 /* frames decoded ahead of the target */
constexpr int64_t JUMP_US = 1000000;        /* further ahead than this: seek, not decode through */
constexpr int WIDEST = 1280;                /* until told how wide it is shown: set_shown_width */
const AVRational MICROSECONDS{ 1, 1000000 };

/*
 * A decoded picture as RGB, at most WIDEST wide, sampled straight to that
 * size so the work is bounded by what is shown. Without libswscale, which
 * this machine's FFmpeg has no headers for and a tiny player need not
 * link: the YUV layouts decoders give -- planar 4:2:0, 4:2:2 and 4:4:4,
 * NV12/NV21, and their 10-bit kin (planar 10-bit, P010), which HEVC and
 * AV1 releases are mostly in -- in the matrix (BT.601, 709 or 2020) and
 * range (limited or full) the stream says. A 10-bit sample is read in the
 * 8-bit scale the matrix works in. Any other layout gives no picture.
 */
/* The picture's size here: at most `widest` wide, its shape kept. */
QSize shown_size(const AVFrame *from, int widest) {
	int width = from->width;
	int height = from->height;
	/* Pixels that are not square -- a DVD's 720 wide for 4:3 or 16:9,
	 * anamorphic releases -- shown at the shape they are meant. */
	const AVRational aspect = from->sample_aspect_ratio;
	if (aspect.num > 0 && aspect.den > 0 && aspect.num != aspect.den)
		width = int(std::lround(double(width) * aspect.num / aspect.den));
	if (width > widest) {
		height = int(int64_t(height) * widest / width);
		width = widest;
	}
	return QSize(width, height);
}

/*
 * Pictures already RGB, or near it: packed RGB in any byte order (screen
 * recordings, PNG, QuickTime Animation), a palette (GIF) and grey. Null for
 * any other layout, which the YUV path below then takes.
 */
QImage packed_to_rgb(const AVFrame *from, int widest) {
	const AVPixelFormat format = AVPixelFormat(from->format);
	int size = 0;          /* bytes a pixel */
	int r = 0, g = 0, b = 0;
	bool palette = false;
	bool grey = false;
	switch (format) {
	case AV_PIX_FMT_RGB24: size = 3; r = 0; g = 1; b = 2; break;
	case AV_PIX_FMT_BGR24: size = 3; r = 2; g = 1; b = 0; break;
	case AV_PIX_FMT_RGBA: case AV_PIX_FMT_RGB0: size = 4; r = 0; g = 1; b = 2; break;
	case AV_PIX_FMT_BGRA: case AV_PIX_FMT_BGR0: size = 4; r = 2; g = 1; b = 0; break;
	case AV_PIX_FMT_ARGB: case AV_PIX_FMT_0RGB: size = 4; r = 1; g = 2; b = 3; break;
	case AV_PIX_FMT_ABGR: case AV_PIX_FMT_0BGR: size = 4; r = 3; g = 2; b = 1; break;
	case AV_PIX_FMT_PAL8: size = 1; palette = true; break;
	case AV_PIX_FMT_GRAY8: size = 1; grey = true; break;
	default: return QImage();
	}
	const QSize shown = shown_size(from, widest);
	if (shown.isEmpty()) return QImage();
	QImage image(shown, QImage::Format_RGB32);
	/* Which source byte each column shown starts at, once for the frame. */
	std::vector<int> columns(static_cast<size_t>(shown.width()));
	for (int x = 0; x < shown.width(); ++x) columns[size_t(x)] = int(int64_t(x) * from->width / shown.width()) * size;
	for (int y = 0; y < shown.height(); ++y) {
		const int sy = int(int64_t(y) * from->height / shown.height());
		const uint8_t *row = from->data[0] + int64_t(sy) * from->linesize[0];
		QRgb *out = reinterpret_cast<QRgb *>(image.scanLine(y));
		for (int x = 0; x < shown.width(); ++x) {
			const uint8_t *p = row + columns[size_t(x)];
			if (palette) {
				uint32_t entry = 0;   /* 0xAARRGGBB, native order (FFmpeg's PAL8) */
				std::memcpy(&entry, from->data[1] + 4 * p[0], 4);
				out[x] = qRgb(int(entry >> 16 & 0xff), int(entry >> 8 & 0xff), int(entry & 0xff));
			} else if (grey) {
				out[x] = qRgb(p[0], p[0], p[0]);
			} else {
				out[x] = qRgb(p[r], p[g], p[b]);
			}
		}
	}
	return image;
}

/*
 * Rows [0, height) converted in bands, one a thread, at most four and no
 * more than half the cores, each band at least 64 rows: a frame's rows
 * are each the image's own, so they need no lock. One thread converted a
 * 4K film at 60 to 13 pictures a second, even with its helpers inlined
 * (2026-11-14); a phone has the cores too.
 */
template <typename rows_t> void in_bands(int height, const rows_t &rows) {
	const int cores = int(std::max(1u, std::thread::hardware_concurrency()));
	const int bands = std::clamp(std::min(4, cores / 2), 1, std::max(1, height / 64));
	if (bands <= 1) {
		rows(0, height);
		return;
	}
	std::vector<std::thread> others;
	for (int band = 1; band < bands; ++band)
		others.emplace_back([&rows, band, bands, height] { rows(height * band / bands, height * (band + 1) / bands); });
	rows(0, height / bands);
	for (std::thread &other : others) other.join();
}

QImage to_rgb(const AVFrame *from, int widest) {
	if (QImage packed = packed_to_rgb(from, widest); !packed.isNull()) return packed;
	const AVPixelFormat format = AVPixelFormat(from->format);
	int x_shift = 0;
	int y_shift = 0;
	bool interleaved = false;   /* NV12: one plane of Cb Cr pairs */
	bool swapped = false;       /* NV21: Cr Cb */
	bool full = from->color_range == AVCOL_RANGE_JPEG;
	bool wide = false;      /* 16-bit samples */
	double divisor = 1;     /* to the 8-bit scale: 10 bits low, or high (P010) */
	switch (format) {
	case AV_PIX_FMT_YUV420P10LE: wide = true; divisor = 4; x_shift = y_shift = 1; break;
	case AV_PIX_FMT_YUV422P10LE: wide = true; divisor = 4; x_shift = 1; break;
	case AV_PIX_FMT_YUV444P10LE: wide = true; divisor = 4; break;
	case AV_PIX_FMT_P010LE: wide = true; divisor = 256; x_shift = y_shift = 1; interleaved = true; break;
	case AV_PIX_FMT_YUVJ420P: full = true; [[fallthrough]];
	case AV_PIX_FMT_YUV420P:
	case AV_PIX_FMT_YUVA420P: x_shift = y_shift = 1; break;   /* the alpha plane, a fourth, unread */
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

	const QSize shown = shown_size(from, widest);
	const int width = shown.width();
	const int height = shown.height();
	if (width <= 0 || height <= 0) return QImage();
	QImage image(width, height, QImage::Format_RGB32);

	/*
	 * In whole numbers, from tables made once a frame: a sample's part in
	 * each of R, G and B, at 16 bits of fraction. The same arithmetic as
	 * the matrix written out -- r = l + 2(1 - kr) v, b = l + 2(1 - kb) u,
	 * g = (l - kr r - kb b) / kg -- to within one step of 255, where the
	 * same in doubles, a 64-bit divide and a rounding on every pixel,
	 * kept a 1080p film to 13 frames a second (measured 2026-11-13).
	 * Samples of more than 8 bits are taken to 8 first: the picture is
	 * shown at 8 bits a colour either way.
	 */
	constexpr int ONE = 1 << 16;
	int luma_part[256];
	int r_from_v[256], g_from_u[256], g_from_v[256], b_from_u[256];
	for (int i = 0; i < 256; ++i) {
		const double l = (i - y_zero) * y_scale;
		const double c = (i - 128) * c_scale;
		luma_part[i] = int(std::lround(l * 255 * ONE));
		r_from_v[i] = int(std::lround(2 * (1 - kr) * c * 255 * ONE));
		b_from_u[i] = int(std::lround(2 * (1 - kb) * c * 255 * ONE));
		/* g = l - (kr * 2(1-kr) v + kb * 2(1-kb) u) / kg */
		g_from_v[i] = int(std::lround(-kr * 2 * (1 - kr) * c / kg * 255 * ONE));
		g_from_u[i] = int(std::lround(-kb * 2 * (1 - kb) * c / kg * 255 * ONE));
	}
	/* Inlined by force: this runs for every sample of every frame, and the
	 * player is built -Os, which made each a call -- a 4K film at 60 fell
	 * from 44 pictures a second to 6 (2026-11-14). */
	auto to_byte = [](int v) __attribute__((always_inline)) {
		const int byte = (v + ONE / 2) >> 16;
		return byte < 0 ? 0 : byte > 255 ? 255 : byte;
	};
	const int shift = divisor == 256 ? 8 : divisor == 4 ? 2 : 0;
	auto sample = [wide, shift](const uint8_t *row, int i) __attribute__((always_inline)) -> int {
		if (!wide) return row[i];
		uint16_t value = 0;
		std::memcpy(&value, row + 2 * i, 2);
		return std::min(255, value >> shift);
	};
	/* Which source column each column shown is from, once for the frame. */
	std::vector<int> columns(static_cast<size_t>(width) + 1);
	for (int x = 0; x <= width; ++x) columns[size_t(x)] = int(int64_t(x) * from->width / width);

	/*
	 * Shown at half the film's width or less -- a 1080p film in a drawer
	 * 200 wide -- each pixel is the mean of the samples it covers, luma
	 * and chroma alike, before the matrix: one picked of every five or
	 * ten shimmered on fine detail (the holder, 2026-11-13: "could do with
	 * some anti-aliasing"), and stripes finer than the step came out one
	 * colour. Every sample of the film is read, so it costs what a
	 * picture the film's size would, less the matrix.
	 */
	if (from->width >= 2 * width && from->height >= 2 * height) {
		const int chroma_width = (from->width + (1 << x_shift) - 1) >> x_shift;
		/* Each column's span, luma and chroma, once: a divide a pixel --
		 * three, luma and both chroma -- kept a 4K film shown 1280 wide to
		 * 16 frames a second of 25 (2026-11-13). */
		std::vector<int> luma_from(static_cast<size_t>(width));
		std::vector<int> luma_to(static_cast<size_t>(width));
		std::vector<int> chroma_from(static_cast<size_t>(width));
		std::vector<int> chroma_to(static_cast<size_t>(width));
		for (int x = 0; x < width; ++x) {
			const int x0 = columns[size_t(x)];
			const int x1 = std::max(x0 + 1, columns[size_t(x) + 1]);
			luma_from[size_t(x)] = x0;
			luma_to[size_t(x)] = x1;
			chroma_from[size_t(x)] = x0 >> x_shift;
			chroma_to[size_t(x)] = std::min(chroma_width, std::max((x0 >> x_shift) + 1, ((x1 - 1) >> x_shift) + 1));
		}
		in_bands(height, [&](int band_from, int band_to) {
			std::vector<int> luma_sum(static_cast<size_t>(from->width));
			std::vector<int> cb_sum(static_cast<size_t>(chroma_width));
			std::vector<int> cr_sum(static_cast<size_t>(chroma_width));
			for (int y = band_from; y < band_to; ++y) {
				const int y0 = int(int64_t(y) * from->height / height);
				const int y1 = std::max(y0 + 1, int(int64_t(y + 1) * from->height / height));
				std::fill(luma_sum.begin(), luma_sum.end(), 0);
				std::fill(cb_sum.begin(), cb_sum.end(), 0);
				std::fill(cr_sum.begin(), cr_sum.end(), 0);
				for (int sy = y0; sy < y1; ++sy) {
					const uint8_t *luma = from->data[0] + int64_t(sy) * from->linesize[0];
					for (int sx = 0; sx < from->width; ++sx) luma_sum[size_t(sx)] += sample(luma, sx);
				}
				const int c0 = y0 >> y_shift;
				const int c1 = std::max(c0 + 1, ((y1 - 1) >> y_shift) + 1);
				for (int cy = c0; cy < c1; ++cy) {
					const uint8_t *cb_row = from->data[1] + int64_t(cy) * from->linesize[1];
					const uint8_t *cr_row = interleaved ? cb_row : from->data[2] + int64_t(cy) * from->linesize[2];
					for (int cx = 0; cx < chroma_width; ++cx) {
						int cb = interleaved ? sample(cb_row, 2 * cx) : sample(cb_row, cx);
						int cr = interleaved ? sample(cr_row, 2 * cx + 1) : sample(cr_row, cx);
						if (swapped) std::swap(cb, cr);
						cb_sum[size_t(cx)] += cb;
						cr_sum[size_t(cx)] += cr;
					}
				}
				QRgb *out = reinterpret_cast<QRgb *>(image.scanLine(y));
				const float luma_rows = 1.0f / float(y1 - y0);
				const float chroma_rows = 1.0f / float(c1 - c0);
				for (int x = 0; x < width; ++x) {
					const int x0 = luma_from[size_t(x)];
					const int x1 = luma_to[size_t(x)];
					int l = 0;
					for (int sx = x0; sx < x1; ++sx) l += luma_sum[size_t(sx)];
					const int cx0 = chroma_from[size_t(x)];
					const int cx1 = chroma_to[size_t(x)];
					int cb = 0;
					int cr = 0;
					for (int cx = cx0; cx < cx1; ++cx) {
						cb += cb_sum[size_t(cx)];
						cr += cr_sum[size_t(cx)];
					}
					const float luma_each = luma_rows / float(x1 - x0);
					const float chroma_each = chroma_rows / float(cx1 - cx0);
					const int lv = luma_part[std::min(255, int(float(l) * luma_each + 0.5f))];
					const int cbv = std::min(255, int(float(cb) * chroma_each + 0.5f));
					const int crv = std::min(255, int(float(cr) * chroma_each + 0.5f));
					out[x] = qRgb(to_byte(lv + r_from_v[crv]), to_byte(lv + g_from_u[cbv] + g_from_v[crv]), to_byte(lv + b_from_u[cbv]));
				}
			}
		});
		return image;
	}
	in_bands(height, [&](int band_from, int band_to) {
		for (int y = band_from; y < band_to; ++y) {
			const int sy = int(int64_t(y) * from->height / height);
			const uint8_t *luma = from->data[0] + int64_t(sy) * from->linesize[0];
			const int cy = sy >> y_shift;
			const uint8_t *cb_row = from->data[1] + int64_t(cy) * from->linesize[1];
			const uint8_t *cr_row = interleaved ? cb_row : from->data[2] + int64_t(cy) * from->linesize[2];
			QRgb *out = reinterpret_cast<QRgb *>(image.scanLine(y));
			for (int x = 0; x < width; ++x) {
				const int sx = columns[size_t(x)];
				const int cx = sx >> x_shift;
				int cb = interleaved ? sample(cb_row, 2 * cx) : sample(cb_row, cx);
				int cr = interleaved ? sample(cr_row, 2 * cx + 1) : sample(cr_row, cx);
				if (swapped) std::swap(cb, cr);
				const int l = luma_part[sample(luma, sx)];
				out[x] = qRgb(to_byte(l + r_from_v[cr]), to_byte(l + g_from_u[cb] + g_from_v[cr]), to_byte(l + b_from_u[cb]));
			}
		}
	});
	return image;
}

/* A subtitle stream's decoder, and the track its cues go to. */
struct subtitle_reader {
	int index = -1;
	AVCodecContext *codec = nullptr;
	std::shared_ptr<snurra_subtitles> track;
};

/* A bitmap subtitle's picture: FFmpeg's palette of 0xAARRGGBB entries
 * indexed by each pixel. */
QImage subtitle_image(const AVSubtitleRect *rect) {
	if (rect->w <= 0 || rect->h <= 0 || !rect->data[0] || !rect->data[1]) return QImage();
	QImage image(rect->w, rect->h, QImage::Format_ARGB32);
	const uint32_t *palette = reinterpret_cast<const uint32_t *>(rect->data[1]);
	const int colours = rect->nb_colors > 0 ? std::min(rect->nb_colors, 256) : 256;
	for (int y = 0; y < rect->h; ++y) {
		const uint8_t *row = rect->data[0] + int64_t(y) * rect->linesize[0];
		QRgb *out = reinterpret_cast<QRgb *>(image.scanLine(y));
		for (int x = 0; x < rect->w; ++x) out[x] = row[x] < colours ? palette[row[x]] : 0;
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

std::vector<std::shared_ptr<snurra_subtitles>> snurra_video::subtitle_tracks() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_subtitles;
}

int snurra_video::subtitle_default() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_subtitle_default;
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

int64_t snurra_video::next_frame_after(int64_t us) const {
	std::lock_guard<std::mutex> lock(m_mutex);
	for (const snurra_video_frame &frame : m_frames)
		if (frame.pts_us > us) return frame.pts_us;
	return us + m_frame_us;
}

/* Under the lock. */
bool snurra_video::want_seek(int64_t target) const {
	if (target < m_floor) return true;
	/* Forward while on the way only for a jump: a seek decodes from the
	 * key frame before its target, and one far apart (250 frames, x264's
	 * default) took longer than the target moved on -- each new seek began
	 * again from the key frame, and the picture stalled for seconds. The
	 * target moving as the clock does is followed (run, 983da1b); moved
	 * further than that by more than JUMP_US, it is a new seek -- else a
	 * jump made while the reader was still on its way, or a film started
	 * partway in, was decoded from where it had been: 7 s of 4K frames
	 * (the holder, 2026-11-13). */
	if (m_seeking) {
		const int64_t elapsed = int64_t(std::chrono::duration_cast<std::chrono::microseconds>(
		                                        std::chrono::steady_clock::now() - m_seek_began).count());
		return target > m_seek_asked + elapsed + JUMP_US;
	}
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
	std::vector<subtitle_reader> subtitles;
	int rotation = 0;   /* degrees clockwise the picture is turned to be shown */

	auto opened = [this](bool has_video) {
		std::function<void(bool)> on_open;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			on_open = m_on_open;
		}
		m_has_video.store(has_video, std::memory_order_release);
		m_opened.store(true, std::memory_order_release);
		if (on_open) on_open(has_video);
	};

	/* Every subtitle stream FFmpeg can decode, named by its language and
	 * title; bitmap and text alike. */
	auto open_subtitles = [&] {
		std::vector<std::shared_ptr<snurra_subtitles>> tracks;
		int marked = -1;
		for (unsigned i = 0; i < format->nb_streams; ++i) {
			AVStream *in = format->streams[i];
			if (in->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) continue;
			const AVCodec *decoder = avcodec_find_decoder(in->codecpar->codec_id);
			if (!decoder) continue;
			AVCodecContext *context = avcodec_alloc_context3(decoder);
			if (!context) continue;
			context->pkt_timebase = in->time_base;
			if (avcodec_parameters_to_context(context, in->codecpar) < 0 || avcodec_open2(context, decoder, nullptr) < 0) {
				avcodec_free_context(&context);
				continue;
			}
			QStringList said;
			if (const AVDictionaryEntry *language = av_dict_get(in->metadata, "language", nullptr, 0))
				said << QString::fromUtf8(language->value);
			if (const AVDictionaryEntry *title = av_dict_get(in->metadata, "title", nullptr, 0))
				said << QString::fromUtf8(title->value);
			if (said.isEmpty()) said << QStringLiteral("track %1").arg(tracks.size() + 1);
			auto track = std::make_shared<snurra_subtitles>(said.join(QStringLiteral(", ")));
			if (marked < 0 && (in->disposition & (AV_DISPOSITION_DEFAULT | AV_DISPOSITION_FORCED))) marked = int(tracks.size());
			tracks.push_back(track);
			subtitles.push_back({ int(i), context, track });
		}
		std::lock_guard<std::mutex> lock(m_mutex);
		m_subtitles = std::move(tracks);
		m_subtitle_default = marked;
	};

	auto open = [&]() -> bool {
		if (bytes) {
			format = input.open(bytes, path, [this] {
				std::lock_guard<std::mutex> lock(m_mutex);
				return m_stop;
			});
			if (!format) return false;
		} else {
			/* A URL's waits -- the opening, then each read -- are given
			 * up for a close. */
			format = avformat_alloc_context();
			if (!format) return false;
			format->interrupt_callback.opaque = this;
			format->interrupt_callback.callback = [](void *opaque) -> int {
				snurra_video *self = static_cast<snurra_video *>(opaque);
				std::lock_guard<std::mutex> lock(self->m_mutex);
				return self->m_stop ? 1 : 0;
			};
			if (snurra_avformat_open(&format, path) < 0) return false;
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
		/* A phone's video, recorded upright, is stored sideways with a
		 * matrix saying how to turn it; turned here as ffmpeg's own
		 * player does, by the quarter turn nearest. */
		if (const AVPacketSideData *matrix = av_packet_side_data_get(stream->codecpar->coded_side_data,
		                                                              stream->codecpar->nb_coded_side_data,
		                                                              AV_PKT_DATA_DISPLAYMATRIX);
		    matrix && matrix->size >= 9 * sizeof(int32_t)) {
			const double turn = -av_display_rotation_get(reinterpret_cast<const int32_t *>(matrix->data));
			if (!std::isnan(turn)) rotation = ((int(std::lround(turn / 90)) % 4 + 4) % 4) * 90;
		}
		/* The picture's length: its stream's, else the file's. */
		int64_t length = 0;
		if (stream->duration != AV_NOPTS_VALUE && stream->duration > 0)
			length = av_rescale_q(stream->duration, stream->time_base, MICROSECONDS);
		else if (format->duration != AV_NOPTS_VALUE && format->duration > 0)
			length = av_rescale_q(format->duration, AV_TIME_BASE_Q, MICROSECONDS);
		m_length_us.store(length, std::memory_order_release);
		open_subtitles();
		const AVRational rate = av_guess_frame_rate(format, stream, nullptr);
		std::lock_guard<std::mutex> lock(m_mutex);
		if (rate.num > 0 && rate.den > 0) m_frame_us = av_rescale_q(1, av_inv_q(rate), MICROSECONDS);
		/* The shape shown: square pixels, turned as the file says. */
		QSize shown(codec->width, codec->height);
		const AVRational aspect = av_guess_sample_aspect_ratio(format, stream, nullptr);
		if (aspect.num > 0 && aspect.den > 0 && aspect.num != aspect.den)
			shown.setWidth(int(std::lround(double(codec->width) * aspect.num / aspect.den)));
		if (rotation == 90 || rotation == 270) shown.transpose();
		m_size = shown;
		return codec->width > 0 && codec->height > 0;
	};

	auto read_subtitle = [&](subtitle_reader &reader, AVPacket *from) {
		AVSubtitle subtitle{};
		int got = 0;
		if (avcodec_decode_subtitle2(reader.codec, &subtitle, &got, from) < 0 || !got) return;
		const AVStream *in = format->streams[reader.index];
		int64_t at = subtitle.pts != AV_NOPTS_VALUE ? subtitle.pts
		             : from->pts != AV_NOPTS_VALUE ? av_rescale_q(from->pts, in->time_base, MICROSECONDS) : AV_NOPTS_VALUE;
		if (at == AV_NOPTS_VALUE) {
			avsubtitle_free(&subtitle);
			return;
		}
		snurra_cue cue;
		cue.start_us = at + int64_t(subtitle.start_display_time) * 1000 - origin_us;
		/* The end: the subtitle's own, else the packet's length, else the
		 * next cue's start (Blu-ray's, which clear with an empty one). */
		if (subtitle.end_display_time > subtitle.start_display_time && subtitle.end_display_time != UINT32_MAX)
			cue.end_us = at + int64_t(subtitle.end_display_time) * 1000 - origin_us;
		else if (from->duration > 0)
			cue.end_us = cue.start_us + av_rescale_q(from->duration, in->time_base, MICROSECONDS);
		QSize frame;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			frame = m_size;
		}
		if (reader.codec->width > 0 && reader.codec->height > 0) frame = QSize(reader.codec->width, reader.codec->height);
		for (unsigned i = 0; i < subtitle.num_rects; ++i) {
			const AVSubtitleRect *rect = subtitle.rects[i];
			if (rect->type == SUBTITLE_BITMAP && cue.image.isNull() && !frame.isEmpty()) {
				cue.image = subtitle_image(rect);
				cue.place = QRectF(double(rect->x) / frame.width(), double(rect->y) / frame.height(),
				                   double(rect->w) / frame.width(), double(rect->h) / frame.height());
			} else if (rect->type == SUBTITLE_ASS && rect->ass) {
				for (auto &line : snurra_ass_text(QString::fromUtf8(rect->ass), &cue.top)) cue.lines.push_back(std::move(line));
			} else if (rect->type == SUBTITLE_TEXT && rect->text) {
				for (const QString &line : QString::fromUtf8(rect->text).split(QLatin1Char('\n')))
					cue.lines.push_back({ snurra_subtitle_span{ line } });
			}
		}
		avsubtitle_free(&subtitle);
		reader.track->add(std::move(cue));
	};

	/*
	 * A seek reads on from the key frame before its target, so a cue that
	 * began before that -- the line on screen at the target -- would not
	 * be seen until read again. So after a seek, a second reader of the
	 * file, every stream but the subtitles discarded, reads their packets
	 * from PREROLL before the target up to it, or up to where the
	 * picture's reader stands in the file, whichever comes first: sparse
	 * subtitles would otherwise have it read on through the film for the
	 * next one. A file only: a host's bytes may not be here yet.
	 */
	AVFormatContext *preroll = nullptr;
	std::vector<subtitle_reader> preroll_readers;
	bool preroll_failed = false;
	/* Inside one read too: with the other streams discarded, a single
	 * av_read_frame reads on to the next subtitle packet, however far. */
	struct preroll_bound {
		AVFormatContext *format = nullptr;
		int64_t stand = INT64_MAX;
		snurra_video *video = nullptr;
	} bound;
	bound.video = this;
	auto preroll_subtitles = [&](int64_t target) {
		constexpr int64_t PREROLL = 10 * int64_t(1000000);
		if (subtitles.empty() || bytes || preroll_failed || snurra_is_url(path)) return;
		if (!preroll) {
			preroll_failed = true;
			preroll = avformat_alloc_context();
			if (!preroll) return;
			bound.format = preroll;
			preroll->interrupt_callback.opaque = &bound;
			preroll->interrupt_callback.callback = [](void *opaque) -> int {
				const preroll_bound *at = static_cast<const preroll_bound *>(opaque);
				if (at->format->pb && avio_tell(at->format->pb) > at->stand) return 1;
				std::lock_guard<std::mutex> lock(at->video->m_mutex);
				return at->video->m_stop ? 1 : 0;
			};
			if (snurra_avformat_open(&preroll, path) < 0) return;
			if (avformat_find_stream_info(preroll, nullptr) < 0 || preroll->nb_streams != format->nb_streams) return;
			for (unsigned i = 0; i < preroll->nb_streams; ++i) preroll->streams[i]->discard = AVDISCARD_ALL;
			for (const subtitle_reader &reader : subtitles) {
				AVStream *in = preroll->streams[reader.index];
				in->discard = AVDISCARD_DEFAULT;
				const AVCodec *decoder = avcodec_find_decoder(in->codecpar->codec_id);
				AVCodecContext *context = decoder ? avcodec_alloc_context3(decoder) : nullptr;
				if (!context) return;
				context->pkt_timebase = in->time_base;
				preroll_readers.push_back({ reader.index, context, reader.track });
				if (avcodec_parameters_to_context(context, in->codecpar) < 0 || avcodec_open2(context, decoder, nullptr) < 0) return;
			}
			preroll_failed = false;
		}
		for (subtitle_reader &reader : preroll_readers) avcodec_flush_buffers(reader.codec);
		const int64_t from = std::max<int64_t>(0, target + origin_us - PREROLL);
		if (av_seek_frame(preroll, -1, av_rescale_q(from, MICROSECONDS, AV_TIME_BASE_Q), AVSEEK_FLAG_BACKWARD) < 0) return;
		bound.stand = avio_tell(format->pb) + (1 << 20);
		AVPacket *read = av_packet_alloc();
		while (read && av_read_frame(preroll, read) >= 0) {
			const AVStream *in = preroll->streams[read->stream_index];
			const bool past = read->pts != AV_NOPTS_VALUE
			                  && av_rescale_q(read->pts, in->time_base, MICROSECONDS) > target + origin_us;
			if (!past)
				for (subtitle_reader &reader : preroll_readers)
					if (read->stream_index == reader.index) read_subtitle(reader, read);
			av_packet_unref(read);
			if (past) break;
		}
		av_packet_free(&read);
		bound.stand = INT64_MAX;
	};

	auto push = [&](const AVFrame *from, int64_t pts_us) {
		/*
		 * At the width shown (to_rgb averages where that is far smaller).
		 */
		snurra_video_frame next{ to_rgb(from, m_widest.load(std::memory_order_relaxed)), pts_us };
		if (rotation != 0 && !next.image.isNull()) next.image = next.image.transformed(QTransform().rotate(rotation));
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
		std::deque<AVPacket *> queued;   /* read ahead to a seek's key frame, not yet decoded */
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
					m_seek_asked = seek_target;
					m_seek_began = std::chrono::steady_clock::now();
					draining = false;
					lock.unlock();
					av_frame_free(&pending);
					const int64_t ts = av_rescale_q(seek_target + origin_us, MICROSECONDS, stream->time_base);
					for (AVPacket *stale : queued) av_packet_free(&stale);
					queued.clear();
					av_seek_frame(format, stream_index, ts, AVSEEK_FLAG_BACKWARD);
					avcodec_flush_buffers(codec);
					for (subtitle_reader &reader : subtitles) avcodec_flush_buffers(reader.codec);
					preroll_subtitles(seek_target);
					/*
					 * A seek lands on an indexed key frame, and a file may index
					 * few: YouTube's 4K WebM landed 30 s before a target 1.8 s
					 * past a key frame, and decoding all of it took 7.4 s of a
					 * jump (the holder, 2026-11-13: "jumping takes a long time
					 * to load video"). So read on to the target without
					 * decoding -- packets are cheap -- keeping those from the
					 * last key frame on, and decode from there.
					 */
					std::vector<AVPacket *> from_key;
					auto drop_kept = [&from_key] {
						for (AVPacket *kept : from_key) av_packet_free(&kept);
						from_key.clear();
					};
					for (;;) {
						if (m_stop) break;
						if (av_read_frame(format, packet) < 0) break;
						if (packet->stream_index != stream_index) {
							for (subtitle_reader &reader : subtitles)
								if (packet->stream_index == reader.index) read_subtitle(reader, packet);
							av_packet_unref(packet);
							continue;
						}
						const bool key = packet->flags & AV_PKT_FLAG_KEY;
						const int64_t at = packet->pts != AV_NOPTS_VALUE ? packet->pts : packet->dts;
						const bool past = at != AV_NOPTS_VALUE
						                  && av_rescale_q(at, stream->time_base, MICROSECONDS) - origin_us > seek_target;
						if (key && !past) drop_kept();
						from_key.push_back(av_packet_clone(packet));
						av_packet_unref(packet);
						if (past) break;
					}
					/* Fed one at a time, as from the file: a decoder takes no
					 * more until its frames are taken. */
					for (AVPacket *kept : from_key) queued.push_back(kept);
					from_key.clear();
				}
			}

			/* One packet in, every frame it gives out: those read ahead to a
			 * seek's key frame first. */
			if (!draining && !queued.empty()) {
				AVPacket *next = queued.front();
				queued.pop_front();
				avcodec_send_packet(codec, next);
				av_packet_free(&next);
			} else if (!draining) {
				const int read = av_read_frame(format, packet);
				if (read < 0) {
					draining = true;
					avcodec_send_packet(codec, nullptr);
				} else {
					if (packet->stream_index == stream_index) avcodec_send_packet(codec, packet);
					else
						for (subtitle_reader &reader : subtitles)
							if (packet->stream_index == reader.index) read_subtitle(reader, packet);
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
				m_decoded.fetch_add(1, std::memory_order_relaxed);
				if (frame->best_effort_timestamp == AV_NOPTS_VALUE) {
					av_frame_unref(frame);
					continue;
				}
				const int64_t pts_us = av_rescale_q(frame->best_effort_timestamp, stream->time_base, MICROSECONDS) - origin_us;
				bool seeking = false;
				int64_t now = seek_target;
				{
					std::lock_guard<std::mutex> lock(m_mutex);
					seeking = m_seeking;
					now = std::max(seek_target, m_target);
				}
				if (seeking) {
					/* Up to the target, only the last frame is kept: the one
					 * a screen shows at the target -- the target as it is now,
					 * not as it was asked. Playing, it moves on while the
					 * reader decodes from the key frame, and stopping at the
					 * old one showed a frame already behind the sound and
					 * then every frame since, as fast as they decoded: a
					 * jitter at each skip (the holder, 2026-11-13). */
					if (pts_us <= now) {
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
		for (AVPacket *left : queued) av_packet_free(&left);
	}

	av_frame_free(&pending);
	av_frame_free(&frame);
	av_packet_free(&packet);
	avcodec_free_context(&codec);
	for (subtitle_reader &reader : subtitles) avcodec_free_context(&reader.codec);
	for (subtitle_reader &reader : preroll_readers) avcodec_free_context(&reader.codec);
	avformat_close_input(&preroll);
	avformat_close_input(&format);
}
