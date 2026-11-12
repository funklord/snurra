#ifndef SNURRA_DECK_H
#define SNURRA_DECK_H

#include <QString>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

#include "spsc.h"

class snurra_source;

/*
 * One stream in the engine (project.md sec 3a, Mixxx's EngineDeck and
 * CachingReader in one): a reader thread decodes ahead into blocks, and the
 * output's callback takes them. The two meet only through lock-free queues
 * and atomics, so the callback never waits on a decoder, a disk or a
 * network -- a block that is not there yet is silence, and the position
 * holds until it comes (fuzzypickles' stall contract).
 *
 * A deck can start *anchored*: told that frame F was heard at time T, it
 * plays whatever frame belongs to each moment, from wherever its data
 * starts, so a handover's taker sounds in step with the giver. And it
 * carries a *fade*, a gain ramp over a stretch of the shared clock, which
 * is what a handover's two sides schedule to cross over in the same
 * moment.
 */
class snurra_deck {
public:
	struct start_t {
		QString source;
		int rate = 48000;
		int64_t fallback_length = 0;   /* frames, for a source that says none */
		int64_t anchor_frame = 0;      /* heard at anchor_us */
		int64_t anchor_us = 0;
		bool playing = false;
		float level = 1;
		bool held = false;             /* silent until a fade brings it in */
		int64_t lead_us = 0;           /* how far ahead the first block is needed */
	};

	explicit snurra_deck(const start_t &start);
	~snurra_deck();

	/* ---- any thread but the callback ---- */

	void set_playing(bool playing) { m_playing.store(playing, std::memory_order_relaxed); }
	void set_level(float level) { m_level.store(level, std::memory_order_relaxed); }
	void seek(int64_t frame);

	/* The gain goes from `from` to `to` over [start_us, start_us + length_us]
	 * of the shared clock, in a straight line: the two sides of a handover
	 * play the same samples in step, so straight lines sum to one. */
	void fade(int64_t start_us, int64_t length_us, float from, float to);

	/* The frame heard at `us`, from what the callback last rendered. */
	int64_t position_at(int64_t us) const;

	/* What the callback last rendered, exactly: `frame` is heard at `us`,
	 * and it was moving (playing, and not starved). A handover passes this
	 * pair on as it is, since any other moment would be rounded to a frame
	 * once here and again by the taker. */
	void last_rendered(int64_t &frame, int64_t &us, bool &moving) const;
	int64_t length() const { return m_length.load(std::memory_order_relaxed); }
	int rate() const { return m_rate; }

	/* Playing in step with its anchor (or never anchored). */
	bool locked() const { return m_locked.load(std::memory_order_acquire); }

	/* Played to the end of its source. */
	bool ended() const { return m_finished.load(std::memory_order_relaxed); }

	/* ---- the output's callback ---- */

	/* Adds this deck into `out` (interleaved stereo), the first frame of
	 * which is heard at `present_us`. */
	void render_add(float *out, int frames, int64_t present_us);

private:
	static constexpr int BLOCK_FRAMES = 1024;
	static constexpr int BLOCKS = 24;

	struct block {
		uint64_t generation = 0;
		int64_t start = 0;
		int frames = 0;
		float data[2 * BLOCK_FRAMES];
	};

	struct fade_t {
		int64_t start_us;
		int64_t length_us;
		float from;
		float to;
	};

	void read_loop();
	bool next_frame(float &left, float &right);
	fade_t fade_snapshot();
	float fade_gain(const fade_t &fade, int64_t us) const;

	const int m_rate;
	std::unique_ptr<snurra_source> m_source;
	std::unique_ptr<block[]> m_blocks;
	snurra_spsc<block *, BLOCKS + 1> m_filled;   /* reader -> callback */
	snurra_spsc<block *, BLOCKS + 1> m_free;     /* callback -> reader */
	std::thread m_reader;
	std::atomic<bool> m_stop{ false };

	/* Controls. */
	std::atomic<bool> m_playing;
	std::atomic<float> m_level;
	std::atomic<uint64_t> m_generation{ 1 };   /* bumped by every seek */
	std::atomic<int64_t> m_seek_target{ 0 };
	std::atomic<uint32_t> m_fade_sequence{ 0 };   /* a seqlock over the fade */
	std::atomic<int64_t> m_fade_start{ 0 };
	std::atomic<int64_t> m_fade_length{ 0 };
	std::atomic<float> m_fade_from{ 1 };
	std::atomic<float> m_fade_to{ 1 };

	/* From the reader. */
	std::atomic<int64_t> m_length{ 0 };
	std::atomic<int64_t> m_ended_generation{ -1 };

	/* From the callback: where it was, published under a seqlock. */
	std::atomic<uint32_t> m_published_sequence{ 0 };
	std::atomic<int64_t> m_published_frame{ 0 };
	std::atomic<int64_t> m_published_us{ 0 };
	std::atomic<int64_t> m_published_advanced{ 0 };
	std::atomic<bool> m_locked{ false };
	std::atomic<bool> m_finished{ false };

	/* The callback's own. */
	int64_t m_position = 0;
	uint64_t m_seen_generation = 1;
	int64_t m_anchor_frame = 0;
	int64_t m_anchor_us = 0;
	bool m_anchored = false;
	block *m_current = nullptr;
	int m_offset = 0;
	float m_slew = 0;   /* play/pause and level, smoothed so neither clicks */
	fade_t m_fade_last{ 0, 0, 1, 1 };
};

#endif
