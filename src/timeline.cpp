#include "snurra/timeline.h"

#include <QByteArray>
#include <QtGlobal>

#include <algorithm>
#include <cmath>

#include "snurra/clock.h"

int64_t snurra_timeline::length_out(const snurra_timeline_tile &tile) {
	return int64_t(double(tile.length) / tile.tempo);
}

const snurra_timeline_tile *snurra_timeline::tile(int row, int index) const {
	const auto lane = lanes.find(row);
	if (lane == lanes.end() || index < 0 || index >= int(lane->second.tiles.size())) return nullptr;
	return &lane->second.tiles[size_t(index)];
}

int64_t snurra_timeline::overlap(int row, int index) const {
	const snurra_timeline_tile *one = tile(row, index);
	const snurra_timeline_tile *next = tile(row, index + 1);
	if (!one || !next) return 0;
	return snurra_train_overlap(length_out(*one), length_out(*next), m_rate, one->tempo, next->tempo, &one->grid, &next->grid,
	                            next->chosen_overlap);
}

int64_t snurra_timeline::start(int row, int index) const {
	const auto lane = lanes.find(row);
	if (lane == lanes.end()) return 0;
	int64_t start = lane->second.begin;
	for (int i = 0; i < index && i < int(lane->second.tiles.size()); ++i)
		start += length_out(lane->second.tiles[size_t(i)]) - overlap(row, i);
	return start;
}

snurra_timeline::place snurra_timeline::at(int row, int64_t time) const {
	place found;
	const auto lane = lanes.find(row);
	if (lane == lanes.end()) return found;
	const std::vector<snurra_timeline_tile> &tiles = lane->second.tiles;
	int64_t start = lane->second.begin;
	/* Before a late lane begins: its first tile, from its start, after the
	 * wait -- a start on the shared clock still to come (deck.h). */
	if (!tiles.empty() && time < start) {
		found.index = 0;
		found.frame = std::max<int64_t>(0, tiles.front().offset);
		found.wait = start - time;
		return found;
	}
	for (int i = 0; i < int(tiles.size()); ++i) {
		const int64_t length = length_out(tiles[size_t(i)]);
		const int64_t own = length - overlap(row, i);
		if (length <= 0 || time < start + own) {
			found.index = i;
			found.frame = std::max<int64_t>(0, std::llround(double(time - start) * tiles[size_t(i)].tempo) + tiles[size_t(i)].offset);
			return found;
		}
		start += own;
	}
	return found;
}

int64_t snurra_timeline::time_of(int row, int index, int64_t frame) const {
	const snurra_timeline_tile *one = tile(row, index);
	if (!one) return 0;
	return start(row, index) + std::llround(double(frame - one->offset) / one->tempo);
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

