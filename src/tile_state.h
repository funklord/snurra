#ifndef SNURRA_TILE_STATE_H
#define SNURRA_TILE_STATE_H

#include <QDataStream>
#include <QDateTime>
#include <QString>

/*
 * What a tile is playing, as it moves between processes (project.md
 * sec 3b): enough for the process taking it to go on from the same
 * place. The position is a sample, stamped with the wall-clock time it
 * was taken, so the taker extrapolates across the handover rather than
 * restarting from where the giver was when it began to send.
 */
struct snurra_tile_state {
	QString source;            /* a path or URL; a title for now */
	qint64 duration_ms = 0;
	qint64 position_ms = 0;    /* at sampled_at */
	qint64 sampled_at = 0;     /* ms since the epoch */
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

	/* Where it is now, carried forward from the sample. */
	qint64 position_now(qint64 now = QDateTime::currentMSecsSinceEpoch()) const {
		if (!playing) return position_ms;
		return qMin(duration_ms, position_ms + qMax<qint64>(0, now - sampled_at));
	}
};

inline QDataStream &operator<<(QDataStream &out, const snurra_tile_state &state) {
	return out << state.source << state.duration_ms << state.position_ms << state.sampled_at
	           << state.playing << qint32(state.level) << qint32(state.row) << qint32(state.column)
	           << state.controls;
}

inline QDataStream &operator>>(QDataStream &in, snurra_tile_state &state) {
	qint32 level = 0;
	qint32 row = 0;
	qint32 column = 0;
	in >> state.source >> state.duration_ms >> state.position_ms >> state.sampled_at >> state.playing
	   >> level >> row >> column >> state.controls;
	state.level = level;
	state.row = row;
	state.column = column;
	return in;
}

#endif
