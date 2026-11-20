#include "snurra/timeline.h"

#include <QByteArray>
#include <QtGlobal>

#include <algorithm>
#include <cmath>

#include "snurra/clock.h"

int64_t snurra_timeline::length_out(const snurra_timeline_track &track) {
	return int64_t(double(track.length) / track.tempo);
}

const snurra_timeline_track *snurra_timeline::track(int index) const {
	return index >= 0 && index < int(tracks.size()) ? &tracks[size_t(index)] : nullptr;
}

int snurra_timeline::follower(int index) const {
	for (int i = 0; i < int(tracks.size()); ++i)
		if (tracks[size_t(i)].follows == index && index >= 0) return i;
	return -1;
}

std::vector<int> snurra_timeline::train(int index) const {
	std::vector<int> found;
	if (!track(index)) return found;
	/* Back to its first, then on; a connection in a ring is cut where it
	 * comes round, so a mistake in the data cannot hang the player. */
	int first = index;
	for (size_t steps = 0; tracks[size_t(first)].follows >= 0 && track(tracks[size_t(first)].follows) && steps < tracks.size(); ++steps)
		first = tracks[size_t(first)].follows;
	for (int at = first; at >= 0 && found.size() < tracks.size(); at = follower(at)) {
		if (std::find(found.begin(), found.end(), at) != found.end()) break;
		found.push_back(at);
	}
	return found;
}

int64_t snurra_timeline::overlap(int index) const {
	const snurra_timeline_track *one = track(index);
	const int next_index = follower(index);
	const snurra_timeline_track *next = track(next_index);
	if (!one || !next) return 0;
	return snurra_train_overlap(length_out(*one), length_out(*next), m_rate, one->tempo, next->tempo, &one->grid, &next->grid,
	                            next->chosen_overlap);
}

int64_t snurra_timeline::start(int index) const {
	const std::vector<int> chain = train(index);
	if (chain.empty()) return 0;
	int64_t start = tracks[size_t(chain.front())].position;
	for (int at : chain) {
		if (at == index) break;
		start += length_out(tracks[size_t(at)]) - overlap(at);
	}
	return start;
}

int64_t snurra_timeline::end(int index) const {
	const snurra_timeline_track *one = track(index);
	return one ? start(index) + length_out(*one) : 0;
}

snurra_timeline::place snurra_timeline::at(const std::vector<int> &chain, int64_t time) const {
	place found;
	if (chain.empty() || !track(chain.front())) return found;
	int64_t start = this->start(chain.front());
	/* Before a late train begins: its first track, from its start, after
	 * the wait -- a start on the shared clock still to come (deck.h). */
	if (time < start) {
		found.track = chain.front();
		found.frame = std::max<int64_t>(0, tracks[size_t(chain.front())].offset);
		found.wait = start - time;
		return found;
	}
	for (int index : chain) {
		const snurra_timeline_track &one = tracks[size_t(index)];
		const int64_t length = length_out(one);
		const int64_t own = length - overlap(index);
		if (length <= 0 || time < start + own) {
			found.track = index;
			found.frame = std::max<int64_t>(0, std::llround(double(time - start) * one.tempo) + one.offset);
			return found;
		}
		start += own;
	}
	return found;
}

int64_t snurra_timeline::time_of(int index, int64_t frame) const {
	const snurra_timeline_track *one = track(index);
	if (!one) return 0;
	return start(index) + std::llround(double(frame - one->offset) / one->tempo);
}

snurra_timeline::seating snurra_timeline::seat() const {
	seating seated;
	seated.channel.assign(tracks.size(), -1);
	std::vector<int> order(tracks.size());
	std::vector<int64_t> starts(tracks.size()), ends(tracks.size());
	for (size_t i = 0; i < tracks.size(); ++i) {
		order[i] = int(i);
		starts[i] = start(int(i));
		/* A length not known yet holds its channel to the end of time. */
		ends[i] = tracks[i].length > 0 ? starts[i] + length_out(tracks[i]) : INT64_MAX;
	}
	/* By when they come in; pinned first among equals, so a free track
	 * gives way to a pin rather than taking its place. */
	std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
		if (starts[size_t(a)] != starts[size_t(b)]) return starts[size_t(a)] < starts[size_t(b)];
		return tracks[size_t(a)].pinned >= 0 && tracks[size_t(b)].pinned < 0;
	});
	std::vector<int64_t> busy_until;   /* by channel */
	auto free_at = [&](int channel, int64_t when) {
		return channel >= int(busy_until.size()) || busy_until[size_t(channel)] <= when;
	};
	auto take = [&](int channel, int index) {
		if (channel >= int(busy_until.size())) busy_until.resize(size_t(channel) + 1, INT64_MIN);
		busy_until[size_t(channel)] = std::max(busy_until[size_t(channel)], ends[size_t(index)]);
		seated.channel[size_t(index)] = channel;
	};
	for (int index : order) {
		const snurra_timeline_track &one = tracks[size_t(index)];
		const int64_t when = starts[size_t(index)];
		if (one.pinned >= 0) {
			if (!free_at(one.pinned, when)) seated.conflicts.push_back(index);
			take(one.pinned, index);
			continue;
		}
		if (one.was >= 0 && free_at(one.was, when)) {
			take(one.was, index);
			continue;
		}
		int channel = 0;
		while (!free_at(channel, when)) ++channel;
		take(channel, index);
	}
	seated.channels = int(busy_until.size());
	return seated;
}

/*
 * Trains (project.md sec 3b): tiles side by side in a row play as one
 * sequence. When a playing tile comes within reach of its end, the next in
 * its row is started at frame 0 on the exact moment the playing one
 * reaches its length less the overlap, and the two cross over that
 * overlap along a quarter circle -- two different tracks, so equal power,
 * where a handover's identical signals cross in straight lines. The
 * controls follow what plays: out on the next tile as it is started, in on
 * the one before once it has ended. A tile stopped before its next began
 * takes the next back.
 *
 * The overlap is 5 s, or $SNURRA_TRAIN_OVERLAP_MS, and never more than
 * half of either tile -- a decision taken from the aims (a DJ mix's
 * crossfade). Where both tracks have a beat (step 6) it is whole bars of
 * the first, moved by less than a beat so the next one's first beat falls
 * on a beat of the first: a DJ brings the next record in on the beat.
 * The tempos are not matched here -- sync is the DJ's own choice (sec 4)
 * -- so two tempos apart drift through the overlap from in phase.
 */
int64_t snurra_train_overlap(int64_t length, int64_t next_length, int rate, double tempo, double next_tempo,
                             const snurra_beat_grid *grid, const snurra_beat_grid *next_grid, int64_t chosen) {
	const QByteArray wanted = qgetenv("SNURRA_TRAIN_OVERLAP_MS");
	const qint64 overlap_us = wanted.isEmpty() ? 5000000 : qint64(wanted.toLongLong()) * 1000;
	const int64_t room = std::min(length / 2, next_length / 2);
	/* Dragged to: where it was put, snapped then or not. */
	if (chosen >= 0) return std::clamp<int64_t>(chosen, std::min<int64_t>(1, room), std::max<int64_t>(1, room));
	const int64_t plain = std::min(snurra_us_to_frames(overlap_us, rate), room);
	if (plain <= 0 || !grid || !next_grid || !grid->valid() || !next_grid->valid() || tempo <= 0 || next_tempo <= 0)
		return plain;
	/* In frames out: a beat of the first, and the next one's first beat
	 * after it starts. */
	const double beat = grid->period / tempo;
	const double bar = 4 * beat;
	const double whole = std::max(1.0, std::round(double(plain) / bar)) * bar;
	const double next_first = next_grid->first / next_tempo;
	/* The overlap is length + next_first - (the first's beat k, out): the
	 * k that comes nearest the whole bars. */
	const double k = std::round(((double(length) + next_first - whole) * tempo - grid->first) / grid->period);
	double snapped = double(length) + next_first - (grid->first + k * grid->period) / tempo;
	while (snapped > double(room)) snapped -= beat;
	while (snapped <= 0) snapped += beat;
	if (snapped > double(room)) return plain;   /* no beat of it fits */
	return std::llround(snapped);
}

int64_t snurra_overlap_on_beat(int64_t chosen, int64_t length, int64_t next_length, double tempo, double next_tempo,
                               const snurra_beat_grid *grid, const snurra_beat_grid *next_grid) {
	const int64_t room = std::min(length / 2, next_length / 2);
	chosen = std::clamp<int64_t>(chosen, std::min<int64_t>(1, room), std::max<int64_t>(1, room));
	if (!grid || !next_grid || !grid->valid() || !next_grid->valid() || tempo <= 0 || next_tempo <= 0) return chosen;
	/* As snurra_train_overlap: overlap = length + next_first - (beat k of
	 * the first, out), the k nearest what was chosen -- a beat, not bars:
	 * the hand chose the bar. */
	const double beat = grid->period / tempo;
	const double next_first = next_grid->first / next_tempo;
	const double k = std::round(((double(length) + next_first - double(chosen)) * tempo - grid->first) / grid->period);
	double snapped = double(length) + next_first - (grid->first + k * grid->period) / tempo;
	while (snapped > double(room)) snapped -= beat;
	while (snapped < beat && snapped + beat <= double(room)) snapped += beat;
	if (snapped <= 0 || snapped > double(room)) return chosen;   /* no beat of it fits */
	return std::llround(snapped);
}

