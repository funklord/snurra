#ifndef SNURRA_VIDEO_H
#define SNURRA_VIDEO_H

#include <QImage>
#include <QSize>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <memory>
#include <thread>

class snurra_bytes;

/* A picture and the time on the track it is shown from, in microseconds. */
struct snurra_video_frame {
	QImage image;
	int64_t pts_us = INT64_MIN;   /* none */
	bool valid() const { return pts_us != INT64_MIN; }
};

/*
 * The picture of a track (project.md sec 3b: the drawer that opens upward),
 * decoded on a thread of its own, a few frames ahead of where it is asked
 * for. Times are the track's own, from the same zero as its sound -- the
 * audio stream's start, which is what a deck counts from -- so a picture
 * asked for at the deck's heard position is the one that belongs with what
 * is heard, whatever offset the container gives the video.
 *
 * frame_at(t) is the last frame whose time is not after t: what a screen
 * shows at t. Asked for a time the frames ahead do not reach -- a seek, or
 * a jump far ahead -- the reader seeks there itself; until it arrives the
 * frame shown before is given again, never one from the future. A track
 * with no picture, or only a cover image, has no video.
 */
class snurra_video {
public:
	explicit snurra_video(const QString &path);
	/* From a host's bytes, still arriving or not (embed.h), `name` hinting
	 * at their kind: the picture waits where they stop. */
	snurra_video(std::shared_ptr<snurra_bytes> bytes, const QString &name);
	~snurra_video();

	/* Known once the reader has opened the track (on its thread); until
	 * then, and for a track without a picture, false. */
	bool has_video() const { return m_has_video.load(std::memory_order_acquire); }
	/* Whether the reader has looked, picture or none. */
	bool opened() const { return m_opened.load(std::memory_order_acquire); }
	QSize size() const;

	/* Called on the reader's thread once it has opened, with or without a
	 * picture. Set it before the reader can be done: in the constructor's
	 * caller, straight away. */
	void set_on_open(std::function<void(bool has_video)> on_open);

	snurra_video_frame frame_at(int64_t us);

private:
	void run(const QString &path, std::shared_ptr<snurra_bytes> bytes);
	bool want_seek(int64_t target) const;

	std::thread m_thread;
	mutable std::mutex m_mutex;
	std::condition_variable m_wake;
	std::deque<snurra_video_frame> m_frames;   /* in time order, ahead of the target */
	snurra_video_frame m_shown;                 /* the last given, given again while waiting */
	int64_t m_target = 0;
	int64_t m_floor = INT64_MIN;    /* every frame for a time from here on is here, or coming */
	int64_t m_newest = INT64_MIN;   /* the last frame decoded */
	bool m_ended = false;
	bool m_seeking = false;         /* decoding toward a seek's target, not there yet */
	bool m_stop = false;
	QSize m_size;
	int64_t m_frame_us = 40000;     /* one frame's length, from the stream's rate */
	std::function<void(bool)> m_on_open;
	std::atomic<bool> m_has_video{ false };
	std::atomic<bool> m_opened{ false };
};

#endif
