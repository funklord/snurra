#include "snurra/deck.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "snurra/clock.h"
#include "snurra/source.h"

snurra_deck::snurra_deck(const start_t &start)
    : m_rate(start.rate), m_blocks(new block[BLOCKS]), m_playing(start.playing), m_level(start.level),
      m_stretch(start.rate, [this](float &left, float &right) {
	      if (!next_frame(left, right)) return false;
	      ++m_position;
	      return true;
      }) {
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
		/* Not for a close that gave up the opening: that is not a failure. */
		if (m_source->stand_in() && !m_stop.load()) m_failed.store(true, std::memory_order_release);
		m_length.store(m_source->length(), std::memory_order_relaxed);
		read_loop();
	});
}

snurra_deck::~snurra_deck() {
	stop_reading();
}

void snurra_deck::stop_reading() {
	m_stop.store(true);
	if (m_reader.joinable()) m_reader.join();
}

void snurra_deck::seek(int64_t frame) {
	m_seek_target.store(std::max<int64_t>(0, frame), std::memory_order_relaxed);
	m_seek_anchor_us.store(INT64_MIN, std::memory_order_relaxed);
	m_generation.fetch_add(1, std::memory_order_release);
}

void snurra_deck::start_at(int64_t frame, int64_t us) {
	/* A seek the callback anchors: before `us` the position is short of
	 * `frame` and the data ahead of it, so it plays silence and reaches
	 * the frame exactly then (render_add). */
	m_seek_target.store(std::max<int64_t>(0, frame), std::memory_order_relaxed);
	m_seek_anchor_us.store(us, std::memory_order_relaxed);
	m_generation.fetch_add(1, std::memory_order_release);
}

void snurra_deck::fade(int64_t start_us, int64_t length_us, float from, float to, bool power) {
	m_fade_sequence.fetch_add(1, std::memory_order_acq_rel);
	m_fade_start.store(start_us, std::memory_order_relaxed);
	m_fade_length.store(length_us, std::memory_order_relaxed);
	m_fade_from.store(from, std::memory_order_relaxed);
	m_fade_to.store(to, std::memory_order_relaxed);
	m_fade_power.store(power, std::memory_order_relaxed);
	m_fade_sequence.fetch_add(1, std::memory_order_release);
}

void snurra_deck::set_envelope(const envelope_t &points) {
	const int count = int(std::min<size_t>(points.size(), ENVELOPE_POINTS));
	m_envelope_sequence.fetch_add(1, std::memory_order_acq_rel);
	for (int i = 0; i < count; ++i) {
		m_envelope_frame[i].store(points[size_t(i)].first, std::memory_order_relaxed);
		m_envelope_gain[i].store(points[size_t(i)].second, std::memory_order_relaxed);
	}
	m_envelope_count.store(count, std::memory_order_relaxed);
	m_envelope_sequence.fetch_add(1, std::memory_order_release);
}

const snurra_deck::envelope_snapshot &snurra_deck::envelope_now() {
	/* As the fade: a few tries, and the last whole one otherwise. */
	for (int attempt = 0; attempt < 4; ++attempt) {
		const uint32_t before = m_envelope_sequence.load(std::memory_order_acquire);
		if (before & 1) continue;
		envelope_snapshot envelope;
		envelope.count = std::clamp(m_envelope_count.load(std::memory_order_relaxed), 0, int(ENVELOPE_POINTS));
		for (int i = 0; i < envelope.count; ++i) {
			envelope.frame[i] = m_envelope_frame[i].load(std::memory_order_relaxed);
			envelope.gain[i] = m_envelope_gain[i].load(std::memory_order_relaxed);
		}
		std::atomic_thread_fence(std::memory_order_acquire);
		if (m_envelope_sequence.load(std::memory_order_relaxed) == before) {
			m_envelope_last = envelope;
			break;
		}
	}
	return m_envelope_last;
}

/* `segment` is the caller's cursor, carried from frame to frame: frames
 * mostly go forward, so it moves on rather than searching again. */
float snurra_deck::envelope_gain(const envelope_snapshot &envelope, int &segment, int64_t frame) {
	if (envelope.count == 0) return 1;
	if (frame <= envelope.frame[0]) return envelope.gain[0];
	const int last = envelope.count - 1;
	if (frame >= envelope.frame[last]) return envelope.gain[last];
	if (segment < 0 || segment >= last || frame < envelope.frame[segment]) segment = 0;
	while (frame >= envelope.frame[segment + 1]) ++segment;
	const int64_t from = envelope.frame[segment];
	const int64_t span = envelope.frame[segment + 1] - from;
	const float t = span > 0 ? float(double(frame - from) / double(span)) : 1.0f;
	return envelope.gain[segment] + (envelope.gain[segment + 1] - envelope.gain[segment]) * t;
}

snurra_deck::fade_t snurra_deck::fade_snapshot() {
	/* A few tries; a writer is a handful of stores away from done, and if
	 * it is not, the last whole fade serves for one more period. */
	for (int attempt = 0; attempt < 4; ++attempt) {
		const uint32_t before = m_fade_sequence.load(std::memory_order_acquire);
		if (before & 1) continue;
		fade_t fade{ m_fade_start.load(std::memory_order_relaxed), m_fade_length.load(std::memory_order_relaxed),
			     m_fade_from.load(std::memory_order_relaxed), m_fade_to.load(std::memory_order_relaxed),
			     m_fade_power.load(std::memory_order_relaxed) };
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
	if (fade.power) {
		const float angle = t * float(M_PI_2);
		return fade.from * std::cos(angle) + fade.to * std::sin(angle);
	}
	return fade.from + (fade.to - fade.from) * t;
}

void snurra_deck::last_rendered(int64_t &frame, int64_t &us, int64_t &advanced) const {
	/* A seek asked and not yet rendered: where it was asked, held there --
	 * so a second jump made before the output's next period goes on from
	 * the first, not from where the deck was. */
	const uint64_t asked = m_generation.load(std::memory_order_acquire);
	if (asked != m_published_generation.load(std::memory_order_acquire)) {
		frame = m_seek_target.load(std::memory_order_relaxed);
		const int64_t anchor = m_seek_anchor_us.load(std::memory_order_relaxed);
		us = anchor != INT64_MIN ? anchor : snurra_clock_us();
		advanced = 0;
		return;
	}
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
	const double tempo = m_published_tempo.load(std::memory_order_relaxed);
	const int64_t along = advanced > 0 ? int64_t(std::llround(double(snurra_us_to_frames(us - at, m_rate)) * tempo)) : 0;
	return std::max<int64_t>(0, frame + along);
}

/* ---- the reader thread ----------------------------------------------- */

void snurra_deck::read_loop() {
	uint64_t generation = 0;
	block *spare = nullptr;
	bool ended = false;
	bool gave_any = false;   /* a frame, ever: a source ending without one could not be read */
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
			gave_any = true;
			m_filled.push(spare);
			spare = nullptr;
		} else if (m_source->at_end()) {
			ended = true;
			if (!gave_any && spare->start == 0) m_failed.store(true, std::memory_order_release);
			m_ended_generation.store(int64_t(generation), std::memory_order_release);
		} else {
			/* Not here yet: a source still arriving. */
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
	}
}

/* ---- the callback ----------------------------------------------------- */

/* The pending seek's moment: its generation seen from here, its frame
 * placed on this moment, the old place's block let go and the new one's
 * first, held till now, taken up. */
void snurra_deck::switch_to_pending(int64_t us) {
	m_pending = false;
	m_seen_generation = m_pending_generation;
	m_position = m_pending_frame;
	m_anchored = true;
	m_anchor_frame = m_pending_frame;
	m_anchor_us = us;
	m_locked.store(false, std::memory_order_release);
	m_finished.store(false, std::memory_order_relaxed);
	m_speed = false;
	if (m_current) m_free.push(m_current);
	m_current = m_held;
	m_held = nullptr;
	m_offset = 0;
}

bool snurra_deck::next_frame(float &left, float &right) {
	for (;;) {
		if (!m_current) {
			if (m_held) return false;   /* the pending seek's: not until its moment */
			if (!m_filled.pop(m_current)) return false;
			m_offset = 0;
			if (m_pending && m_current->generation == m_pending_generation) {
				m_held = m_current;
				m_current = nullptr;
				return false;
			}
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
	const bool playing = m_playing.load(std::memory_order_acquire);   /* first: see set_playing */
	const uint64_t generation = m_generation.load(std::memory_order_acquire);
	const int64_t asked_at = m_seek_anchor_us.load(std::memory_order_relaxed);
	/*
	 * A seek for a moment ahead, while sounding: the old place plays on
	 * until that moment and the switch is made on its very frame, below --
	 * where dropping it at once, as any other seek does, left a silence
	 * the length of the wait at every jump.
	 */
	if (generation != m_seen_generation && generation != m_pending_generation && playing && m_slew > 0 && !m_anchored
	    && asked_at != INT64_MIN && asked_at > present_us) {
		if (m_held) {
			m_free.push(m_held);
			m_held = nullptr;
		}
		m_pending = true;
		m_pending_generation = generation;
		m_pending_frame = m_seek_target.load(std::memory_order_relaxed);
		m_pending_us = asked_at;
	}
	if (generation != m_seen_generation && !(m_pending && generation == m_pending_generation)) {
		if (m_held) {
			m_free.push(m_held);
			m_held = nullptr;
		}
		m_pending = false;
		/* A seek: what was read for before it is stale. */
		m_seen_generation = generation;
		m_position = m_seek_target.load(std::memory_order_relaxed);
		const int64_t anchor_us = m_seek_anchor_us.load(std::memory_order_relaxed);
		m_anchored = anchor_us != INT64_MIN;
		if (m_anchored) {
			m_anchor_frame = m_position;
			m_anchor_us = anchor_us;
		}
		m_locked.store(!m_anchored, std::memory_order_release);
		m_finished.store(false, std::memory_order_relaxed);
		m_speed = false;   /* the stretch begins again from the new place */
		/* Paused, a seek is heard as nothing: the slew down to silence was
		 * for the old position, and would otherwise sound the new one's
		 * first 5 ms -- and move it -- if the pause came in this period. */
		if (!playing) m_slew = 0;
		if (m_current) {
			m_free.push(m_current);
			m_current = nullptr;
		}
	}
	/* Stale blocks go back even while paused, or the reader waits on them --
	 * not the pending seek's, which are the next to play. */
	block *head = nullptr;
	while (!m_current && m_filled.peek(head) && head->generation != m_seen_generation
	       && !(m_pending && head->generation == m_pending_generation)) {
		m_filled.pop(head);
		m_free.push(head);
	}

	if (m_anchored) m_position = m_anchor_frame + snurra_us_to_frames(present_us - m_anchor_us, m_rate);

	const fade_t fade = fade_snapshot();
	const envelope_snapshot &envelope = envelope_now();
	int segment = -1;
	const float level = m_level.load(std::memory_order_relaxed);
	const float target = playing ? level : 0.0f;
	const float step = 1.0f / (0.005f * float(m_rate));   /* 5 ms from silence to full */
	const bool ended = m_ended_generation.load(std::memory_order_acquire) == int64_t(m_seen_generation);

	const float tempo = m_tempo.load(std::memory_order_relaxed);
	const bool key_lock = m_key_lock.load(std::memory_order_relaxed);
	m_bend_left += double(m_bend_asked.exchange(0, std::memory_order_relaxed));
	if (!m_speed && !m_anchored && (tempo != 1.0f || key_lock || m_bend_left != 0)) {
		m_speed = true;
		m_stretch.reset(m_position);
	}
	const int64_t first = m_speed ? int64_t(std::floor(m_stretch.position())) : m_position;
	int64_t advanced = 0;
	for (int i = 0; i < frames; ++i) {
		if (m_slew < target) m_slew = std::min(target, m_slew + step);
		else if (m_slew > target) m_slew = std::max(target, m_slew - step);
		if (m_slew <= 0 && !playing) break;   /* paused: the position holds */

		float left = 0;
		float right = 0;
		/* On the frame nearest the moment: within half a frame of it. */
		if (m_pending && present_us + (int64_t(2 * i + 1) * 1000000) / (2 * m_rate) > m_pending_us)
			switch_to_pending(present_us + int64_t(i) * 1000000 / m_rate);
		const int64_t heard = m_speed ? int64_t(std::floor(m_stretch.position())) : m_position;
		/* A bend's share of this frame: a tenth at most, the last exact. */
		const double bent = m_speed ? std::clamp(m_bend_left, -0.1, 0.1) : 0.0;
		const bool have = m_speed ? m_stretch.next(left, right, double(tempo) + bent, key_lock) : next_frame(left, right);
		if (have) m_bend_left -= bent;
		if (!have) {
			if (m_anchored) {
				/* Not yet in step: the clock moves on regardless. */
				++m_position;
				++advanced;
				continue;
			}
			if (ended) m_finished.store(true, std::memory_order_relaxed);
			/* Nothing sounding to slew down from: paused while starved,
			 * the data that comes later must not play its first 5 ms. */
			if (!playing) m_slew = 0;
			break;   /* starved: hold, and sound nothing until data comes */
		}
		if (m_anchored) {
			m_anchored = false;
			m_locked.store(true, std::memory_order_release);
		}
		const int64_t us = present_us + int64_t(i) * 1000000 / m_rate;
		const float gain = m_slew * fade_gain(fade, us) * envelope_gain(envelope, segment, heard);
		out[2 * i] += gain * left;
		out[2 * i + 1] += gain * right;
		if (!m_speed) ++m_position;   /* the stretch moved it as it read */
		++advanced;
	}

	m_published_sequence.fetch_add(1, std::memory_order_acq_rel);
	m_published_frame.store(first, std::memory_order_relaxed);
	m_published_us.store(present_us, std::memory_order_relaxed);
	m_published_advanced.store(advanced, std::memory_order_relaxed);
	m_published_tempo.store(m_speed ? tempo : 1.0f, std::memory_order_relaxed);
	m_published_sequence.fetch_add(1, std::memory_order_release);
	m_published_generation.store(m_seen_generation, std::memory_order_release);
}
