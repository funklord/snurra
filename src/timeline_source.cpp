/*
 * A player's output as a source (timeline.h, project.md sec 3f step 2):
 * its tiles summed in the timeline's own time, each through a stretch of
 * its own, so a deck can play a whole player as its track.
 */
#include "snurra/timeline.h"

#include <QList>
#include <QObject>
#include <QSettings>

#include <algorithm>
#include <cmath>

#include "snurra/beats.h"
#include "snurra/clock.h"
#include "snurra/deck.h"
#include "snurra/source.h"
#include "snurra/stretch.h"
#include "snurra/tile_state.h"

namespace {

/* The states in timeline order -- rows, each in column order -- and their
 * timeline: a window's lane is a train whose first comes in at its lane
 * start (snurra_node::timeline). */
std::vector<const snurra_tile_state *> in_order(const std::vector<snurra_tile_state> &states) {
	std::vector<const snurra_tile_state *> order;
	for (const snurra_tile_state &state : states) order.push_back(&state);
	std::stable_sort(order.begin(), order.end(), [](const snurra_tile_state *a, const snurra_tile_state *b) {
		return a->row != b->row ? a->row < b->row : a->column < b->column;
	});
	return order;
}

snurra_timeline timeline_of(const std::vector<const snurra_tile_state *> &order, int rate) {
	snurra_timeline timeline(rate);
	for (size_t i = 0; i < order.size(); ++i) {
		const snurra_tile_state &state = *order[i];
		snurra_timeline_track track;
		track.length = snurra_us_to_frames(state.duration_us, rate);
		track.tempo = std::clamp(double(state.tempo), 0.25, 4.0);
		track.offset = snurra_us_to_frames(state.offset_us, rate);
		if (i == 0 || order[i - 1]->row != state.row) {
			track.position = snurra_us_to_frames(state.lane_start_us, rate);
		} else {
			track.follows = int(i) - 1;
			track.chosen_overlap = state.overlap_us >= 0 ? snurra_us_to_frames(state.overlap_us, rate) : -1;
		}
		timeline.tracks.push_back(track);
	}
	return timeline;
}

/* One tile of the timeline: what it plays and how. */
struct part {
	QString name;                 /* the sound's: a file, a URL, a test's name */
	std::vector<snurra_tile_state> player;   /* or a player of its own */
	int audio_track = -1;
	int64_t fallback = 0;         /* frames: its length as the state says it */
	float level = 1;
	bool key_lock = false;
	double pitch = 1;             /* the key shift, as a ratio */
	std::vector<std::pair<int64_t, float>> envelope;   /* track frames, gain */
	float trim = 1;               /* the level match's, as its deck's would be */
	bool eq = false;              /* any band moved: the isolator heard */
	snurra_eq eq_asked;           /* its bands, as its deck would have them */
};

/* A part being heard: its source open, read through its stretch. */
struct voice {
	std::unique_ptr<snurra_source> source;
	std::unique_ptr<snurra_stretch> stretch;
	std::function<bool(float &, float &)> pull;   /* the track's next frame */
	int64_t frame = 0;            /* read straight, past the stretch: the next frame's */
	std::vector<float> chunk;     /* frames read ahead of the stretch, stereo */
	size_t chunk_at = 0;
	size_t chunk_len = 0;
	int64_t next_time = -1;       /* the timeline frame its next frame out is for */
	int segment = 0;              /* the envelope's, carried as the deck's is */
	snurra_isolator isolator;     /* its EQ, as a deck's (snurra_deck) */
	float eq_gain[SNURRA_EQ_BANDS] = {};
};

float envelope_gain(const std::vector<std::pair<int64_t, float>> &points, int &segment, double frame) {
	if (points.empty()) return 1;
	if (frame <= double(points.front().first)) return points.front().second;
	if (frame >= double(points.back().first)) return points.back().second;
	const int last = int(points.size()) - 1;
	if (segment < 0 || segment >= last || frame < double(points[size_t(segment)].first)) segment = 0;
	while (frame >= double(points[size_t(segment) + 1].first)) ++segment;
	const double from = double(points[size_t(segment)].first);
	const double span = double(points[size_t(segment) + 1].first) - from;
	const double t = span > 0 ? (frame - from) / span : 1;
	return float(points[size_t(segment)].second + (points[size_t(segment) + 1].second - points[size_t(segment)].second) * t);
}

class timeline_source : public snurra_source {
public:
	timeline_source(const std::vector<snurra_tile_state> &states, int rate, std::function<bool()> interrupted)
	    : m_timeline(rate), m_rate(rate), m_interrupted(std::move(interrupted)) {
		const std::vector<const snurra_tile_state *> order = in_order(states);
		m_timeline = timeline_of(order, rate);
		const bool matched = QSettings(QStringLiteral("snurra"), QStringLiteral("snurra")).value(QStringLiteral("loudness/match"), true).toBool();
		for (size_t i = 0; i < order.size(); ++i) {
			const snurra_tile_state &state = *order[i];
			const snurra_timeline_track &track = m_timeline.tracks[i];
			part made;
			made.name = state.sound_source();
			made.player = state.player;
			made.audio_track = state.audio_track;
			made.fallback = track.length;
			made.level = float(std::clamp(state.level, 0, 100)) / 100.0f;
			made.key_lock = state.key_lock;
			made.pitch = std::pow(2.0, double(state.key_shift) / 12.0);
			/* The level match, as a deck takes it (snurra_tile::set_beats):
			 * from the track's analysis where it has been kept, while the
			 * setting is on; a track not yet analysed plays as it is, as a
			 * deck's does until its analysis comes. */
			if (matched && state.player.empty()) {
				snurra_beat_grid grid;
				if (snurra_cached_beats(made.name, state.audio_track, rate, grid))
					made.trim = snurra_loudness_gain(grid, SNURRA_LOUDNESS_REFERENCE);
			}
			made.eq_asked = state.eq;
			made.eq = !state.eq.flat();
			for (const auto &point : state.envelope)
				made.envelope.emplace_back(snurra_us_to_frames(point.first, rate), std::clamp(point.second, 0.0f, 1.0f));
			m_parts.push_back(std::move(made));
		}
		m_voices.resize(m_parts.size());
		for (size_t i = 0; i < m_parts.size(); ++i) {
			m_starts.push_back(m_timeline.start(int(i)));
			m_ends.push_back(m_timeline.end(int(i)));
			const int before = m_timeline.tracks[i].follows;
			m_in.push_back(before >= 0 ? m_timeline.overlap(before) : 0);
			m_out.push_back(m_timeline.overlap(int(i)));
			m_length = std::max(m_length, m_ends.back());
		}
	}

	int read(float *out, int frames) override {
		const int n = int(std::max<int64_t>(0, std::min<int64_t>(frames, m_length - m_position)));
		std::fill(out, out + 2 * n, 0.0f);
		for (size_t i = 0; i < m_parts.size(); ++i) {
			const int64_t from = std::max(m_position, m_starts[i]);
			const int64_t to = std::min(m_position + n, m_ends[i]);
			if (from >= to) {
				if (m_position + n >= m_ends[i]) m_voices[i] = voice();   /* done with: closed */
				continue;
			}
			render(i, from, to, out + 2 * (from - m_position));
		}
		m_position += n;
		return n;
	}

	bool at_end() const override { return m_position >= m_length; }
	void seek(int64_t frame) override { m_position = std::clamp<int64_t>(frame, 0, m_length); }
	int64_t position() const override { return m_position; }
	int64_t length() const override { return m_length; }
	void set_interrupt(std::function<bool()> interrupted) override {
		m_interrupted = std::move(interrupted);
		for (voice &one : m_voices)
			if (one.source) one.source->set_interrupt(m_interrupted);
	}

private:
	/* Part `i` from timeline frame `from` to `to`, added at `out`. */
	void render(size_t i, int64_t from, int64_t to, float *out) {
		const part &one = m_parts[i];
		const snurra_timeline_track &track = m_timeline.tracks[i];
		voice &heard = m_voices[i];
		if (!heard.source || heard.next_time != from) start(i, from);
		if (!heard.stretch) return;
		/* As a deck: straight from the track at tempo 1 with no key lock
		 * or shift, through the stretch only otherwise -- whose cubic
		 * wants frames past a track's last, and so would drop them. */
		/* Key lock at the track's own tempo is the track as it is. */
		const bool straight = track.tempo == 1 && one.pitch == 1;
		const double fade_in = double(m_in[i]), fade_out = double(m_out[i]);
		for (int64_t time = from; time < to; ++time, out += 2) {
			float left = 0, right = 0;
			const double frame = straight ? double(heard.frame) : heard.stretch->position();
			const bool got = straight ? heard.pull(left, right) : heard.stretch->next(left, right, track.tempo, one.key_lock, one.pitch);
			if (straight && got) ++heard.frame;
			if (!got) {
				/* The track has no more: silence, and no stretch to ask. */
				heard.next_time = time + 1;
				continue;
			}
			if (one.eq) {
				left = heard.isolator.run(left, 0, heard.eq_gain);
				right = heard.isolator.run(right, 1, heard.eq_gain);
			}
			/* As a deck: its level, its envelope at the frame heard, and a
			 * train's crossfades on a quarter circle (snurra_deck::fade). */
			double gain = one.level * one.trim * envelope_gain(one.envelope, heard.segment, frame);
			const double into = double(time - m_starts[i]);
			if (fade_in > 0 && into < fade_in) gain *= std::sin(into / fade_in * M_PI_2);
			const double left_of = double(m_ends[i] - time);
			if (fade_out > 0 && left_of <= fade_out) gain *= std::cos((fade_out - left_of) / fade_out * M_PI_2);
			out[0] += float(gain * left);
			out[1] += float(gain * right);
		}
		heard.next_time = to;
	}

	/* Part `i` opened where timeline frame `time` falls in it. */
	void start(size_t i, int64_t time) {
		const part &one = m_parts[i];
		const snurra_timeline_track &track = m_timeline.tracks[i];
		voice &heard = m_voices[i];
		if (!heard.source) {
			heard.source = !one.player.empty() ? snurra_open_timeline(one.player, m_rate, m_interrupted)
			                                   : snurra_open_source(one.name, m_rate, one.fallback, m_interrupted);
			if (one.audio_track >= 0) heard.source->choose_audio_track(one.audio_track);
			heard.chunk.assign(2 * 1024, 0.0f);
			heard.isolator.make(m_rate, one.eq_asked.splits);
			heard.isolator.gains(one.eq_asked, heard.eq_gain);
			voice *self = &heard;
			heard.pull = [self](float &left, float &right) {
				if (self->chunk_at >= self->chunk_len) {
					self->chunk_len = size_t(std::max(0, self->source->read(self->chunk.data(), 1024)));
					self->chunk_at = 0;
					if (self->chunk_len == 0) return false;
				}
				left = self->chunk[2 * self->chunk_at];
				right = self->chunk[2 * self->chunk_at + 1];
				++self->chunk_at;
				return true;
			};
			heard.stretch = std::make_unique<snurra_stretch>(m_rate, [self](float &left, float &right) { return self->pull(left, right); });
		}
		const int64_t frame = std::max<int64_t>(0, std::llround(double(time - m_starts[i]) * track.tempo) + track.offset);
		heard.source->seek(frame);
		heard.chunk_at = heard.chunk_len = 0;
		heard.stretch->reset(frame);
		heard.frame = frame;
		heard.segment = 0;
		heard.isolator.rest();
		heard.next_time = time;
	}

	snurra_timeline m_timeline;
	int m_rate;
	std::function<bool()> m_interrupted;
	std::vector<part> m_parts;
	std::vector<voice> m_voices;
	std::vector<int64_t> m_starts, m_ends, m_in, m_out;
	int64_t m_length = 0;
	int64_t m_position = 0;
};

}   // namespace

snurra_timeline snurra_timeline_from(const std::vector<snurra_tile_state> &states, int rate,
                                     std::vector<snurra_tile_state> *in_timeline_order) {
	const std::vector<const snurra_tile_state *> order = in_order(states);
	if (in_timeline_order) {
		in_timeline_order->clear();
		for (const snurra_tile_state *state : order) in_timeline_order->push_back(*state);
	}
	return timeline_of(order, rate);
}

QList<snurra_tile_state> snurra_player_tracks(const snurra_tile_state &holder, int rate) {
	std::vector<snurra_tile_state> order;
	const snurra_timeline timeline = snurra_timeline_from(holder.player, rate, &order);
	const int64_t time = snurra_us_to_frames(holder.position_us, rate);
	QList<snurra_tile_state> tracks;
	for (size_t i = 0; i < order.size(); ++i) {
		snurra_tile_state track = order[i];
		const snurra_timeline_track &one = timeline.tracks[i];
		const int64_t start = timeline.start(int(i)), end = timeline.end(int(i));
		const bool sounding = start <= time && time < end;
		int64_t frame = one.offset;
		if (sounding) frame = std::max<int64_t>(0, std::llround(double(time - start) * one.tempo) + one.offset);
		else if (time >= end) frame = one.length;
		track.position_us = snurra_frames_to_us(frame, rate);
		track.sampled_at_us = holder.sampled_at_us;
		track.playing = holder.playing && sounding;
		track.controls = sounding || (i == 0 && !holder.playing);
		tracks << track;
	}
	return tracks;
}

snurra_tile_state snurra_player_state(const std::vector<snurra_tile_state> &states, int rate) {
	snurra_tile_state player;
	player.player = states;
	player.source = QObject::tr("%n tracks", nullptr, int(states.size()));
	const std::vector<const snurra_tile_state *> order = in_order(states);
	const snurra_timeline timeline = timeline_of(order, rate);
	int64_t length = 0;
	for (size_t i = 0; i < order.size(); ++i) length = std::max(length, timeline.end(int(i)));
	player.duration_us = snurra_frames_to_us(length, rate);
	/* Where the group stands: the time of a tile playing, else the first's,
	 * at that tile's own sample -- so a taker anchors on what the giver
	 * said, as it does a single tile. */
	size_t from = 0;
	for (size_t i = 0; i < order.size(); ++i)
		if (order[i]->playing) {
			from = i;
			break;
		}
	if (!order.empty()) {
		const snurra_tile_state &here = *order[from];
		player.playing = here.playing;
		player.sampled_at_us = here.sampled_at_us;
		player.position_us = snurra_frames_to_us(
		        timeline.time_of(int(from), snurra_us_to_frames(here.position_us, rate)), rate);
	}
	return player;
}

std::unique_ptr<snurra_source> snurra_open_timeline(const std::vector<snurra_tile_state> &states, int rate,
                                                    std::function<bool()> interrupted) {
	return std::make_unique<timeline_source>(states, rate, std::move(interrupted));
}
