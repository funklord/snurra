#ifndef SNURRA_DECK_H
#define SNURRA_DECK_H

#include <QString>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include "snurra/spsc.h"
#include "snurra/stretch.h"

class snurra_bytes;
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
		QString source;                       /* a name; or, with bytes, a hint at their kind */
		std::shared_ptr<snurra_bytes> bytes;  /* from a transport, still arriving or not */
		int rate = 48000;
		int64_t fallback_length = 0;   /* frames, for a source that says none */
		int64_t anchor_frame = 0;      /* heard at anchor_us */
		int64_t anchor_us = 0;
		bool playing = false;
		float level = 1;
		bool held = false;             /* silent until a fade brings it in */
		bool anchored = false;         /* in step with the anchor's clock: a handover's taker */
		int64_t lead_us = 0;           /* how far ahead the first block is needed */
	};

	explicit snurra_deck(const start_t &start);
	~snurra_deck();

	/* ---- any thread but the callback ---- */

	/* Released, and read by the callback before the seek generation: a
	 * start_at() made before set_playing(true) is then always seen with
	 * it, and the deck never sounds a block from where it was. */
	void set_playing(bool playing) { m_playing.store(playing, std::memory_order_release); }
	void set_level(float level) { m_level.store(level, std::memory_order_relaxed); }

	/* Track frames a frame out (1: as recorded), and whether the pitch
	 * stays as the tempo moves -- key lock -- or moves with it, as a
	 * turntable's (stretch.h). Heard from the next period on. */
	void set_tempo(float tempo) { m_tempo.store(std::clamp(tempo, 0.25f, 4.0f), std::memory_order_relaxed); }
	float tempo() const { return m_tempo.load(std::memory_order_relaxed); }
	void set_key_lock(bool on) { m_key_lock.store(on, std::memory_order_relaxed); }

	/* A nudge (project.md sec 4), as a turntable's: `frames` more of the
	 * track (fewer, negative) played over the next moments, at most a
	 * tenth faster or slower -- 10 ms in 100 ms -- with no gap. Bends add
	 * up. */
	void bend(int64_t frames) { m_bend_asked.fetch_add(frames, std::memory_order_relaxed); }
	bool key_lock() const { return m_key_lock.load(std::memory_order_relaxed); }
	void seek(int64_t frame);

	/* Plays `frame` at `us` on the shared clock and on from there, silent
	 * until then: how a train's next tile starts on the very frame the
	 * one before it reaches its overlap. The deck must be playing. */
	void start_at(int64_t frame, int64_t us);

	/*
	 * The gain goes from `from` to `to` over [start_us, start_us + length_us]
	 * of the shared clock. In a straight line by default: the two sides of
	 * a handover play the same samples in step, so straight lines sum to
	 * one. With `power`, along a quarter circle -- cos falling, sin rising,
	 * for fades between 0 and 1 -- so that two DIFFERENT signals, a train's
	 * two tracks, keep their summed power, where straight lines dip 3 dB
	 * in the middle.
	 */
	void fade(int64_t start_us, int64_t length_us, float from, float to, bool power = false);

	/*
	 * The envelope (project.md sec 3b, step 5): a level curve over the
	 * deck's own frames, so it moves with the track wherever it is played
	 * from. Points are (frame, gain), in order; between two the gain is a
	 * straight line in amplitude, as Audacity's envelope; before the first
	 * and after the last it holds. None: 1 throughout. At most
	 * ENVELOPE_POINTS are kept. It multiplies the level and any fade.
	 */
	static constexpr int ENVELOPE_POINTS = 64;
	using envelope_t = std::vector<std::pair<int64_t, float>>;
	void set_envelope(const envelope_t &points);

	/* The frame heard at `us`, from what the callback last rendered. */
	int64_t position_at(int64_t us) const;

	/* What the callback last rendered, exactly: `frame` is heard at `us`,
	 * and `advanced` frames from it went out (0: paused or starved). A
	 * handover passes the pair on as it is, since any other moment would be
	 * rounded to a frame once here and again by the taker. */
	void last_rendered(int64_t &frame, int64_t &us, int64_t &advanced) const;
	int64_t length() const { return m_length.load(std::memory_order_relaxed); }
	int rate() const { return m_rate; }

	/* Playing in step with its anchor (or never anchored). */
	bool locked() const { return m_locked.load(std::memory_order_acquire); }

	/* Played to the end of its source. */
	bool ended() const { return m_finished.load(std::memory_order_relaxed); }

	/* What it was given could not be opened: it plays silence instead. */
	bool failed() const { return m_failed.load(std::memory_order_acquire); }

	/* Its reader stopped and joined: nothing reads its source or its bytes
	 * after this returns, and the callback finds no more blocks -- silence.
	 * For a host that must know its bytes are no longer called (embed.h). */
	void stop_reading();

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
		bool power;
	};

	struct envelope_snapshot {
		int count = 0;
		int64_t frame[ENVELOPE_POINTS];
		float gain[ENVELOPE_POINTS];
	};

	void read_loop();
	const envelope_snapshot &envelope_now();
	static float envelope_gain(const envelope_snapshot &envelope, int &segment, int64_t frame);
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
	std::atomic<float> m_tempo{ 1 };
	std::atomic<bool> m_key_lock{ false };
	std::atomic<int64_t> m_bend_asked{ 0 };
	std::atomic<uint64_t> m_generation{ 1 };   /* bumped by every seek */
	std::atomic<int64_t> m_seek_target{ 0 };
	std::atomic<uint32_t> m_fade_sequence{ 0 };   /* a seqlock over the fade */
	std::atomic<int64_t> m_fade_start{ 0 };
	std::atomic<int64_t> m_fade_length{ 0 };
	std::atomic<float> m_fade_from{ 1 };
	std::atomic<float> m_fade_to{ 1 };
	std::atomic<bool> m_fade_power{ false };
	std::atomic<int64_t> m_seek_anchor_us{ INT64_MIN };   /* start_at's time; MIN for a plain seek */
	std::atomic<uint32_t> m_envelope_sequence{ 0 };   /* a seqlock over the envelope */
	std::atomic<int> m_envelope_count{ 0 };
	std::atomic<int64_t> m_envelope_frame[ENVELOPE_POINTS];
	std::atomic<float> m_envelope_gain[ENVELOPE_POINTS];

	/* From the reader. */
	std::atomic<int64_t> m_length{ 0 };
	std::atomic<int64_t> m_ended_generation{ -1 };
	uint64_t m_reader_generation = 0;   /* the reader's own */

	/* From the callback: where it was, published under a seqlock. */
	std::atomic<uint32_t> m_published_sequence{ 0 };
	std::atomic<int64_t> m_published_frame{ 0 };
	std::atomic<uint64_t> m_published_generation{ 1 };   /* the last seek rendered */
	std::atomic<float> m_published_tempo{ 1 };
	std::atomic<int64_t> m_published_us{ 0 };
	std::atomic<int64_t> m_published_advanced{ 0 };
	std::atomic<bool> m_locked{ false };
	std::atomic<bool> m_finished{ false };
	std::atomic<bool> m_failed{ false };

	/* The callback's own. */
	int64_t m_position = 0;
	uint64_t m_seen_generation = 1;
	int64_t m_anchor_frame = 0;
	int64_t m_anchor_us = 0;
	bool m_anchored = false;
	block *m_current = nullptr;
	int m_offset = 0;
	float m_slew = 0;   /* play/pause and level, smoothed so neither clicks */
	/* Off tempo 1, or key-locked: frames come through the stretch, and
	 * m_position is where it reads the track, ahead of what is heard. */
	bool m_speed = false;
	double m_bend_left = 0;   /* frames of a bend still to play, the callback's */
	/* A seek for a moment ahead, made while playing: the old place plays
	 * on until then, and the new one's first block waits here. */
	bool m_pending = false;
	/* The last period played the track: what a jump keeps playing until
	 * its moment. Not a deck merely marked playing -- a train's next tile
	 * asked to start ahead has nothing to keep (ThreadSanitizer's timing
	 * let one sound its first blocks early). */
	bool m_sounded = false;
	uint64_t m_pending_generation = 0;
	int64_t m_pending_frame = 0;
	int64_t m_pending_us = 0;
	block *m_held = nullptr;
	void switch_to_pending(int64_t us, int i);
	/* A cut's tail: what the old place had next, faded out over 5 ms
	 * while the new one slews in, so a seek heard is not a click. */
	static constexpr int TAIL = 240;
	float m_tail[2 * TAIL] = {};
	int m_tail_len = 0;
	int m_tail_at = 0;
	int m_tail_from = 0;   /* the frame of this period it starts on */
	void keep_tail(int from);
	snurra_stretch m_stretch;
	fade_t m_fade_last{ 0, 0, 1, 1, false };
	envelope_snapshot m_envelope_last;
};

#endif
