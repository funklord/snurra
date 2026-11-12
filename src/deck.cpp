#include "deck.h"

#include <algorithm>
#include <chrono>

#include "clock.h"
#include "source.h"

snurra_deck::snurra_deck(const start_t &start)
    : m_rate(start.rate), m_blocks(new block[BLOCKS]), m_playing(start.playing), m_level(start.level) {
	for (int i = 0; i < BLOCKS; ++i) m_free.push(&m_blocks[i]);

	/*
	 * Anchored -- a handover's taker, which must sound in step with the
	 * giver -- the first block is read from where the anchor will be when
	 * it is needed, and the callback places it by the clock. Any other deck
	 * starts where it was asked: anchoring every playing deck made a newly
	 * opened file skip what passed while it opened, its first 80 ms on
	 * ALSA, and a file still arriving would skip all it waited for.
	 */
	m_anchored = start.playing && start.anchored;
	m_anchor_frame = start.anchor_frame;
	m_anchor_us = start.anchor_us;
	int64_t first = start.anchor_frame;
	if (m_anchored) {
		const int64_t ahead = snurra_clock_us() + start.lead_us - start.anchor_us;
		first += snurra_us_to_frames(ahead, m_rate);
	}
	first = std::max<int64_t>(0, first);
	m_seek_target.store(first);
	m_position = m_anchored ? 0 : first;
	m_published_frame.store(start.anchor_frame);
	m_published_us.store(start.anchor_us);
	m_locked.store(!m_anchored, std::memory_order_release);
	m_slew = start.playing ? start.level : 0;
	if (start.held) fade(INT64_MAX, 0, 0, 0);

	/* The source is opened on the reader's thread: opening may block --
	 * bytes still arriving block until the format can be known. */
	const QString name = start.source;
	const int64_t fallback = start.fallback_length;
	std::shared_ptr<snurra_bytes> bytes = start.bytes;
	m_reader = std::thread([this, name, fallback, bytes] {
		/* A wait for bytes is given up for a seek elsewhere or a close. */
		auto interrupted = [this] {
			return m_stop.load(std::memory_order_relaxed)
			       || m_generation.load(std::memory_order_acquire) != m_reader_generation;
		};
		m_reader_generation = m_generation.load(std::memory_order_acquire);
		if (bytes) m_source = snurra_open_source(bytes, name, m_rate, interrupted);
		if (!m_source) m_source = snurra_open_source(bytes ? QString() : name, m_rate, fallback);
		m_length.store(m_source->length(), std::memory_order_relaxed);
		read_loop();
	});
}

snurra_deck::~snurra_deck() {
	m_stop.store(true);
	if (m_reader.joinable()) m_reader.join();
}

void snurra_deck::seek(int64_t frame) {
	m_seek_target.store(std::max<int64_t>(0, frame), std::memory_order_relaxed);
	m_generation.fetch_add(1, std::memory_order_release);
}

void snurra_deck::fade(int64_t start_us, int64_t length_us, float from, float to) {
	m_fade_sequence.fetch_add(1, std::memory_order_acq_rel);
	m_fade_start.store(start_us, std::memory_order_relaxed);
	m_fade_length.store(length_us, std::memory_order_relaxed);
	m_fade_from.store(from, std::memory_order_relaxed);
	m_fade_to.store(to, std::memory_order_relaxed);
	m_fade_sequence.fetch_add(1, std::memory_order_release);
}

snurra_deck::fade_t snurra_deck::fade_snapshot() {
	/* A few tries; a writer is a handful of stores away from done, and if
	 * it is not, the last whole fade serves for one more period. */
	for (int attempt = 0; attempt < 4; ++attempt) {
		const uint32_t before = m_fade_sequence.load(std::memory_order_acquire);
		if (before & 1) continue;
		fade_t fade{ m_fade_start.load(std::memory_order_relaxed), m_fade_length.load(std::memory_order_relaxed),
			     m_fade_from.load(std::memory_order_relaxed), m_fade_to.load(std::memory_order_relaxed) };
		std::atomic_thread_fence(std::memory_order_acquire);
		if (m_fade_sequence.load(std::memory_order_relaxed) == before) {
			m_fade_last = fade;
			break;
		}
	}
	return m_fade_last;
}

float snurra_deck::fade_gain(const fade_t &fade, int64_t us) const {
	if (us < fade.start_us) return fade.from;
	if (fade.length_us <= 0 || us >= fade.start_us + fade.length_us) return fade.to;
	const float t = float(double(us - fade.start_us) / double(fade.length_us));
	return fade.from + (fade.to - fade.from) * t;
}

void snurra_deck::last_rendered(int64_t &frame, int64_t &us, int64_t &advanced) const {
	for (;;) {
		const uint32_t before = m_published_sequence.load(std::memory_order_acquire);
		if (before & 1) continue;
		frame = m_published_frame.load(std::memory_order_relaxed);
		us = m_published_us.load(std::memory_order_relaxed);
		advanced = m_published_advanced.load(std::memory_order_relaxed);
		std::atomic_thread_fence(std::memory_order_acquire);
		if (m_published_sequence.load(std::memory_order_relaxed) == before) break;
	}
}

int64_t snurra_deck::position_at(int64_t us) const {
	int64_t frame = 0;
	int64_t at = 0;
	int64_t advanced = 0;
	last_rendered(frame, at, advanced);
	/* Carried along the clock, but never past what was rendered moving: a
	 * paused or starved deck stays put. */
	const int64_t along = advanced > 0 ? snurra_us_to_frames(us - at, m_rate) : 0;
	return std::max<int64_t>(0, frame + along);
}

/* ---- the reader thread ----------------------------------------------- */

void snurra_deck::read_loop() {
	uint64_t generation = 0;
	block *spare = nullptr;
	bool ended = false;
	while (!m_stop.load(std::memory_order_relaxed)) {
		const uint64_t now = m_generation.load(std::memory_order_acquire);
		if (now != generation) {
			generation = now;
			m_reader_generation = now;
			m_source->seek(m_seek_target.load(std::memory_order_relaxed));
			ended = false;
		}
		if (ended) {
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			continue;
		}
		if (!spare && !m_free.pop(spare)) {
			/* Full: the lead is read. A block lasts 21 ms at 48 kHz. */
			std::this_thread::sleep_for(std::chrono::milliseconds(4));
			continue;
		}
		spare->generation = generation;
		spare->start = m_source->position();
		spare->frames = m_source->read(spare->data, BLOCK_FRAMES);
		if (spare->frames > 0) {
			m_filled.push(spare);
			spare = nullptr;
		} else if (m_source->at_end()) {
			ended = true;
			m_ended_generation.store(int64_t(generation), std::memory_order_release);
		} else {
			/* Not here yet: a source still arriving. */
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
	}
}

/* ---- the callback ----------------------------------------------------- */

bool snurra_deck::next_frame(float &left, float &right) {
	for (;;) {
		if (!m_current) {
			if (!m_filled.pop(m_current)) return false;
			m_offset = 0;
			if (m_current->generation != m_seen_generation) {
				m_free.push(m_current);
				m_current = nullptr;
				continue;
			}
		}
		const int64_t here = m_current->start + m_offset;
		if (here < m_position) {
			/* Behind the position: skipped, as a seek or an anchor asks. */
			m_offset += int(std::min<int64_t>(m_position - here, m_current->frames - m_offset));
		} else if (here > m_position) {
			/* Data starts later than this frame: silence until it does. */
			left = right = 0;
			return true;
		} else {
			left = m_current->data[2 * m_offset];
			right = m_current->data[2 * m_offset + 1];
			++m_offset;
		}
		if (m_offset >= m_current->frames) {
			m_free.push(m_current);
			m_current = nullptr;
		}
		if (here == m_position) return true;
	}
}

void snurra_deck::render_add(float *out, int frames, int64_t present_us) {
	const uint64_t generation = m_generation.load(std::memory_order_acquire);
	if (generation != m_seen_generation) {
		/* A seek: what was read for before it is stale. */
		m_seen_generation = generation;
		m_position = m_seek_target.load(std::memory_order_relaxed);
		m_anchored = false;
		m_locked.store(true, std::memory_order_release);
		m_finished.store(false, std::memory_order_relaxed);
		if (m_current) {
			m_free.push(m_current);
			m_current = nullptr;
		}
	}
	/* Stale blocks go back even while paused, or the reader waits on them. */
	block *head = nullptr;
	while (!m_current && m_filled.peek(head) && head->generation != m_seen_generation) {
		m_filled.pop(head);
		m_free.push(head);
	}

	if (m_anchored) m_position = m_anchor_frame + snurra_us_to_frames(present_us - m_anchor_us, m_rate);

	const fade_t fade = fade_snapshot();
	const bool playing = m_playing.load(std::memory_order_relaxed);
	const float level = m_level.load(std::memory_order_relaxed);
	const float target = playing ? level : 0.0f;
	const float step = 1.0f / (0.005f * float(m_rate));   /* 5 ms from silence to full */
	const bool ended = m_ended_generation.load(std::memory_order_acquire) == int64_t(m_seen_generation);

	const int64_t first = m_position;
	int64_t advanced = 0;
	for (int i = 0; i < frames; ++i) {
		if (m_slew < target) m_slew = std::min(target, m_slew + step);
		else if (m_slew > target) m_slew = std::max(target, m_slew - step);
		if (m_slew <= 0 && !playing) break;   /* paused: the position holds */

		float left = 0;
		float right = 0;
		const bool have = next_frame(left, right);
		if (!have) {
			if (m_anchored) {
				/* Not yet in step: the clock moves on regardless. */
				++m_position;
				++advanced;
				continue;
			}
			if (ended) m_finished.store(true, std::memory_order_relaxed);
			break;   /* starved: hold, and sound nothing until data comes */
		}
		if (m_anchored) {
			m_anchored = false;
			m_locked.store(true, std::memory_order_release);
		}
		const int64_t us = present_us + int64_t(i) * 1000000 / m_rate;
		const float gain = m_slew * fade_gain(fade, us);
		out[2 * i] += gain * left;
		out[2 * i + 1] += gain * right;
		++m_position;
		++advanced;
	}

	m_published_sequence.fetch_add(1, std::memory_order_acq_rel);
	m_published_frame.store(first, std::memory_order_relaxed);
	m_published_us.store(present_us, std::memory_order_relaxed);
	m_published_advanced.store(advanced, std::memory_order_relaxed);
	m_published_sequence.fetch_add(1, std::memory_order_release);
}
