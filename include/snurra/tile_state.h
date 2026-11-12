#ifndef SNURRA_TILE_STATE_H
#define SNURRA_TILE_STATE_H

#include <QDataStream>
#include <QList>
#include <QPair>
#include <QString>

#include "snurra/clock.h"

/*
 * What a tile is playing, as it moves between processes (project.md
 * sec 3b): enough for the process taking it to go on from the same
 * place. The position is a sample, stamped with the time on the shared
 * clock (clock.h) at which that position is heard, so the taker
 * extrapolates across the handover rather than restarting from where the
 * giver was when it began to send. Microseconds, because a taker that
 * should sound in step with the giver to within a frame needs better
 * than a millisecond.
 */
struct snurra_tile_state {
	QString source;            /* a path, or a name source.h knows */
	qint64 duration_us = 0;
	qint64 position_us = 0;    /* heard at sampled_at_us */
	qint64 sampled_at_us = 0;  /* snurra_clock_us() */
	bool playing = false;
	int level = 100;           /* per cent */
	int row = 0;               /* the place in a group: row is a lane (stack), */
	int column = 0;            /* column the sequence (train) */

	/*
	 * Whether its controls are out (project.md sec 3c): the first tile
	 * opens with them and plays; the rest open as a waveform alone,
	 * paused, until their controls are pulled out.
	 */
	bool controls = true;

	/*
	 * Its envelope (project.md sec 3b, step 5): points of (microseconds
	 * into the track, gain 0 to 1), in order; none is 1 throughout. In the
	 * track's own time, so it goes with the track wherever it plays.
	 */
	QList<QPair<qint64, float>> envelope;

	/* How far the track is ahead of its lane's time, in microseconds: a
	 * nudge's, so a lane lined up by ear stays lined up (sec 4). */
	qint64 offset_us = 0;

	/* Where it is at `now`, carried along from the sample -- either way,
	 * since a sample's time can be just ahead, when it is heard. */
	qint64 position_now(qint64 now = snurra_clock_us()) const {
		if (!playing) return position_us;
		return qBound<qint64>(0, position_us + (now - sampled_at_us), qMax<qint64>(0, duration_us));
	}
};

inline QDataStream &operator<<(QDataStream &out, const snurra_tile_state &state) {
	return out << state.source << state.duration_us << state.position_us << state.sampled_at_us << state.playing
	           << qint32(state.level) << qint32(state.row) << qint32(state.column) << state.controls << state.envelope << state.offset_us;
}

inline QDataStream &operator>>(QDataStream &in, snurra_tile_state &state) {
	qint32 level = 0;
	qint32 row = 0;
	qint32 column = 0;
	in >> state.source >> state.duration_us >> state.position_us >> state.sampled_at_us >> state.playing >> level
	   >> row >> column >> state.controls >> state.envelope >> state.offset_us;
	state.level = level;
	state.row = row;
	state.column = column;
	return in;
}

#endif
