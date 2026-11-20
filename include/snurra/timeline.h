#ifndef SNURRA_TIMELINE_H
#define SNURRA_TIMELINE_H

#include <cstdint>
#include <map>
#include <vector>

#include "snurra/beats.h"

/*
 * A player's timeline as data (project.md sec 3f): lanes of tiles, each
 * lane a train -- its tiles one after another, crossing over their
 * overlaps -- and the lanes a stack, played at once. A window draws it
 * (snurra_node), the decks will play it, and a deck will play one whole as
 * its source; so it knows nothing of widgets, processes or the clock.
 *
 * Times are frames out at `rate`, from the timeline's start; a track's own
 * frames are its tile's, at its tempo (frames of track a frame out).
 */
struct snurra_timeline_tile {
	int64_t length = 0;          /* the track's frames; 0 or less: not known yet */
	double tempo = 1;
	int64_t offset = 0;          /* track frames it is ahead of its lane (a nudge) */
	int64_t chosen_overlap = -1; /* frames out across the tile before, where dragged to; -1 the train's own */
	snurra_beat_grid grid;
};

struct snurra_timeline_lane {
	int64_t begin = 0;           /* where the lane's first tile comes in */
	std::vector<snurra_timeline_tile> tiles;
};

class snurra_timeline {
public:
	explicit snurra_timeline(int rate) : m_rate(rate) {}

	int rate() const { return m_rate; }

	/* Lanes by row: a window's rows need not run from 0 without a gap. */
	std::map<int, snurra_timeline_lane> lanes;

	/* A tile's length out: its track's at its tempo. */
	static int64_t length_out(const snurra_timeline_tile &tile);

	/* Frames out tile `index` of lane `row` crosses the next over; 0 for
	 * the last. */
	int64_t overlap(int row, int index) const;

	/* Where tile `index` of lane `row` comes in. */
	int64_t start(int row, int index) const;

	/* What lane `row` plays at `time`: which tile (-1 none: past its end),
	 * at which frame of its track, and how long until it begins where the
	 * lane has not yet. Inside an overlap is the next tile's: a seek into a
	 * crossfade comes in on the track it goes to. A length not known is
	 * taken as long enough. */
	struct place {
		int index = -1;
		int64_t frame = 0;
		int64_t wait = 0;
	};
	place at(int row, int64_t time) const;

	/* The time at which tile `index` of lane `row` is at `frame` of its
	 * track. */
	int64_t time_of(int row, int index, int64_t frame) const;

private:
	const snurra_timeline_tile *tile(int row, int index) const;
	int m_rate;
};

/*
 * A train's overlap between a tile and the next (project.md sec 3b, steps
 * 3 and 6), in frames out at `rate`: lengths in frames out, tempos as the
 * decks', grids in each track's frames. 5 s ($SNURRA_TRAIN_OVERLAP_MS),
 * never more than half either; where both have a beat, the whole bars of
 * the first nearest that, then moved by less than a beat so the next
 * one's first beat falls on one of the first's -- the two in phase as
 * they cross.
 */
int64_t snurra_train_overlap(int64_t length, int64_t next_length, int rate, double tempo = 1, double next_tempo = 1,
                             const snurra_beat_grid *grid = nullptr, const snurra_beat_grid *next_grid = nullptr,
                             int64_t chosen = -1);
/* `chosen`, an overlap dragged to, the next one's first beat moved onto the
 * nearest beat of the first -- by less than half a beat, within half the
 * shorter, at least one beat -- or as it was where either has no beat.
 * Placed so by snurra_node when a later tile of a train is dragged; then
 * passed to snurra_train_overlap as `chosen`, which keeps it. */
int64_t snurra_overlap_on_beat(int64_t chosen, int64_t length, int64_t next_length, double tempo, double next_tempo,
                               const snurra_beat_grid *grid, const snurra_beat_grid *next_grid);

#endif
