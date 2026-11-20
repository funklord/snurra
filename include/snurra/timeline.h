#ifndef SNURRA_TIMELINE_H
#define SNURRA_TIMELINE_H

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "snurra/beats.h"

/*
 * A player's timeline as data (project.md sec 3f): tracks, each placed
 * where it comes in or connected after another, and the channels they are
 * seated on. A window draws it (snurra_node), the decks will play it, and
 * a deck will play one whole as its source; so it knows nothing of
 * widgets, processes or the clock.
 *
 * A track is free -- it comes in at its `position` -- or follows another,
 * coming in where that one ends less their overlap. Tracks so connected
 * are a train. Channels are not stored on tracks but seated
 * (snurra_timeline::seat): a channel never holds two tracks at once, so
 * connecting two trains can move the second's onto other channels, and
 * nothing ever overlaps on one.
 *
 * Times are frames out at `rate`, from the timeline's start; a track's own
 * frames are its, at its tempo (frames of track a frame out). A track is
 * named by its index in `tracks`.
 */
struct snurra_timeline_track {
	int64_t length = 0;          /* the track's frames; 0 or less: not known yet */
	double tempo = 1;
	int64_t offset = 0;          /* track frames it is ahead of its place (a nudge) */
	snurra_beat_grid grid;

	int64_t position = 0;        /* where it comes in, when it follows nothing */
	int follows = -1;            /* the track it is connected after; -1 free */
	int64_t chosen_overlap = -1; /* frames out across the one it follows, where dragged to; -1 the train's own */

	int pinned = -1;             /* a channel the person put it on; -1 seated freely */
	int was = -1;                /* the channel it sat on before, kept where it can be */
};

class snurra_timeline {
public:
	explicit snurra_timeline(int rate) : m_rate(rate) {}

	int rate() const { return m_rate; }

	std::vector<snurra_timeline_track> tracks;

	/* A track's length out: its track's at its tempo. */
	static int64_t length_out(const snurra_timeline_track &track);

	/* The track connected after `track`, or -1. */
	int follower(int track) const;

	/* The train `track` is in, from its first track, in order. */
	std::vector<int> train(int track) const;

	/* Frames out `track` and its follower cross over; 0 where none. */
	int64_t overlap(int track) const;

	/* Where `track` comes in, and where it ends. */
	int64_t start(int track) const;
	int64_t end(int track) const;

	/* What `train` (as train() gives it) plays at `time`: which of its
	 * tracks (-1 none: past its end), at which frame of that track, and how
	 * long until it begins where it has not yet. Inside an overlap is the
	 * next track's: a seek into a crossfade comes in on the track it goes
	 * to. A length not known is taken as long enough. */
	struct place {
		int track = -1;
		int64_t frame = 0;
		int64_t wait = 0;
	};
	place at(const std::vector<int> &train, int64_t time) const;

	/* The time at which `track` is at `frame` of itself. */
	int64_t time_of(int track, int64_t frame) const;

	/*
	 * Every track's channel, in time order: a pinned track on its pin, else
	 * the channel it was on where that is free, else the lowest free one --
	 * free meaning nothing still playing there when it comes in. Taken in
	 * the order tracks come in, so the channels used are as few as the
	 * most tracks ever sounding at once: a train, whose overlap is never
	 * more than half of either track, alternates over two. A pin on a
	 * channel not free is kept and named in `conflicts`.
	 */
	struct seating {
		std::vector<int> channel;    /* by track */
		std::vector<int> conflicts;  /* pinned tracks sharing a channel with one sounding */
		int channels = 0;
	};
	seating seat() const;

private:
	const snurra_timeline_track *track(int index) const;
	int m_rate;
};

class snurra_source;
struct snurra_tile_state;

/*
 * A player's output as a source (project.md sec 3f, step 2): what its
 * tiles -- `states`, as a window's group hands them over -- sound
 * together, so a deck can play a whole player. Each row is a train, its
 * first tile coming in at its lane start and each after it following the
 * one before; each track read through a stretch of its own at its tempo,
 * key lock and key shift, as a deck reads it, at its level and envelope,
 * crossing over its overlaps on a quarter circle as a train does. A
 * state's own `player` makes it a timeline in turn, so players nest.
 *
 * Rendered in the timeline's own time on the deck's reader thread: a
 * position is a frame of the timeline, and a seek moves every track.
 * Not yet in a nested track: its EQ and the level match's trim, which a
 * deck applies in its callback.
 */
std::unique_ptr<snurra_source> snurra_open_timeline(const std::vector<snurra_tile_state> &states, int rate,
                                                    std::function<bool()> interrupted = nullptr);

/*
 * A train's overlap between a track and the next (project.md sec 3b, steps
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
