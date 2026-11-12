#include "snurra/stretch.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr int CAPACITY = 16384;   /* track frames held: a segment, its search, and room */

}

snurra_stretch::snurra_stretch(int rate, std::function<bool(float &, float &)> pull)
    : m_seq(rate * 40 / 1000), m_overlap(rate * 8 / 1000), m_seek(rate * 15 / 1000), m_pull(std::move(pull)),
      m_in(size_t(2 * CAPACITY)), m_middle(size_t(2 * m_overlap)), m_out(size_t(2 * m_seq)) {}

void snurra_stretch::reset(int64_t frame) {
	m_in_start = frame;
	m_in_len = 0;
	m_place = double(frame);
	m_nominal = double(frame);
	m_nominal_out = double(frame);
	m_have_middle = false;
	m_out_at = m_out_len = 0;
	m_key_lock_last = false;
}

bool snurra_stretch::have(int64_t until) {
	while (m_in_start + m_in_len < until) {
		if (m_in_len >= CAPACITY) drop_before(m_in_start + m_in_len - CAPACITY / 2);
		float left = 0;
		float right = 0;
		if (!m_pull(left, right)) return false;
		m_in[size_t(2 * m_in_len)] = left;
		m_in[size_t(2 * m_in_len + 1)] = right;
		++m_in_len;
	}
	return true;
}

void snurra_stretch::drop_before(int64_t frame) {
	const int drop = int(std::clamp<int64_t>(frame - m_in_start, 0, m_in_len));
	if (drop == 0) return;
	std::memmove(m_in.data(), m_in.data() + 2 * drop, sizeof(float) * size_t(2 * (m_in_len - drop)));
	m_in_len -= drop;
	m_in_start += drop;
}

float snurra_stretch::at(int64_t frame, int channel) const {
	const int64_t i = std::clamp<int64_t>(frame - m_in_start, 0, std::max(0, m_in_len - 1));
	return m_in_len > 0 ? m_in[size_t(2 * i + channel)] : 0.0f;
}

bool snurra_stretch::next(float &left, float &right, double tempo, bool key_lock) {
	if (key_lock != m_key_lock_last) {
		/* Switched: go on from where the sound is, the other way. */
		const double from = position();
		m_key_lock_last = key_lock;
		m_place = m_nominal = m_nominal_out = from;
		m_have_middle = false;
		m_out_at = m_out_len = 0;
	}
	if (!key_lock) {
		const int64_t whole = int64_t(std::floor(m_place));
		const float t = float(m_place - double(whole));
		if (!have(whole + 3)) return false;
		for (int channel = 0; channel < 2; ++channel) {
			const float p0 = at(whole - 1, channel);
			const float p1 = at(whole, channel);
			const float p2 = at(whole + 1, channel);
			const float p3 = at(whole + 2, channel);
			/* Catmull-Rom: through p1 at t = 0 exactly, p2 at t = 1. */
			const float value = p1 + 0.5f * t * (p2 - p0 + t * (2 * p0 - 5 * p1 + 4 * p2 - p3 + t * (3 * (p1 - p2) + p3 - p0)));
			(channel ? right : left) = value;
		}
		m_place += tempo;
		drop_before(int64_t(std::floor(m_place)) - 2);
		return true;
	}
	if (m_out_at >= m_out_len && !step(tempo)) return false;
	left = m_out[size_t(2 * m_out_at)];
	right = m_out[size_t(2 * m_out_at + 1)];
	++m_out_at;
	m_nominal_out += m_out_tempo;
	return true;
}

/*
 * One segment: from the nominal place, searched forward up to m_seek frames
 * for where its first overlap best matches the last segment's end
 * (normalised cross-correlation, on the sum of the channels); then the
 * last end crossfaded into it, its body out, and its own end kept for the
 * next. m_seq - m_overlap frames out, and the nominal place moves on by
 * that times the tempo.
 */
bool snurra_stretch::step(double tempo) {
	const int64_t base = int64_t(std::floor(m_nominal));
	if (!have(base + m_seek + m_seq)) return false;
	int best = 0;
	if (m_have_middle) {
		double best_score = -1e300;
		for (int offset = 0; offset < m_seek; ++offset) {
			double cross = 0;
			double energy = 1e-9;
			for (int j = 0; j < m_overlap; ++j) {
				const float mine = m_middle[size_t(2 * j)] + m_middle[size_t(2 * j + 1)];
				const int64_t frame = base + offset + j;
				const float theirs = at(frame, 0) + at(frame, 1);
				cross += double(mine) * theirs;
				energy += double(theirs) * theirs;
			}
			const double score = cross / std::sqrt(energy);
			if (score > best_score) {
				best_score = score;
				best = offset;
			}
		}
	}
	const int64_t start = base + best;
	const int body = m_seq - m_overlap;
	for (int j = 0; j < body; ++j) {
		for (int channel = 0; channel < 2; ++channel) {
			float value = at(start + j, channel);
			if (j < m_overlap && m_have_middle) {
				const float w = float(j) / float(m_overlap);
				value = m_middle[size_t(2 * j + channel)] * (1 - w) + value * w;
			}
			m_out[size_t(2 * j + channel)] = value;
		}
	}
	for (int j = 0; j < m_overlap; ++j)
		for (int channel = 0; channel < 2; ++channel)
			m_middle[size_t(2 * j + channel)] = at(start + body + j, channel);
	m_have_middle = true;
	m_out_at = 0;
	m_out_len = body;
	m_out_tempo = tempo;
	m_nominal_out = m_nominal;
	m_nominal += double(body) * tempo;
	drop_before(int64_t(std::floor(m_nominal)) - 4);
	return true;
}
