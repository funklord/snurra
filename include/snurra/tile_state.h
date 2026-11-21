#ifndef SNURRA_TILE_STATE_H
#define SNURRA_TILE_STATE_H

#include <QDataStream>
#include <QList>
#include <QPair>
#include <QString>

#include <vector>

#include "snurra/clock.h"
#include "snurra/eq.h"

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
/* How deep players may nest in a state read off the wire. */
constexpr int SNURRA_PLAYER_DEPTH = 16;

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

	/* Track frames a frame out, and whether the pitch stays as it moves
	 * (key lock): the deck's (sec 4). */
	float tempo = 1;
	bool key_lock = false;

	/* Which of the file's sound tracks plays, as source.h numbers them: a
	 * stem's lane (project.md sec 3e), or a film's language; -1 the file's
	 * own choice. */
	int audio_track = -1;

	/* On a lane's first tile: where the lane begins on the group's
	 * timeline, in microseconds -- a lane may start later than the
	 * timeline does (project.md sec 3b). */
	qint64 lane_start_us = 0;

	/* The cue point, a DJ's (project.md sec 4): microseconds into the
	 * track, -1 for none. */
	qint64 cue_us = -1;

	/* The EQ: its bands and each one's dB, -24 to +6 or SNURRA_EQ_KILL
	 * (eq.h); a DJ's three unless split. */
	snurra_eq eq;

	/* The key moved on its own, in semitones (deck.h). */
	int key_shift = 0;

	/* On a train's later tile: how long it crosses the one before, in
	 * microseconds out, where it was dragged to come in (project.md sec
	 * 3b); -1 the train's own, 5 s on the bars (snurra_train_overlap). */
	qint64 overlap_us = -1;

	/* Where the sound comes from, where it is not `source` itself: a film
	 * whose picture and sound are two streams, as a site serves them
	 * (mplayer's -audiofile, mpv's --audio-file). Empty: the source's. */
	QString sound;
	const QString &sound_source() const { return sound.isEmpty() ? source : sound; }

	/* A tracklist given from outside, as (microseconds in, title), in
	 * order: mpv's --chapters-file, as hydra hands a streamed set's. Held
	 * here, not as the file's name, since the file is the giver's to
	 * overwrite. Empty: the source's own chapters, where it has any. */
	QList<QPair<qint64, QString>> chapters;

	/* A player inside this tile (project.md sec 3f): where not empty, the
	 * tile plays these tiles' output, the sum of all their decks, as its
	 * track -- a channel with a prepared mix in it. A tree: a player is
	 * moved into a tile, never shared, so it cannot contain itself.
	 * std::vector, which takes the element type before it is complete. */
	std::vector<snurra_tile_state> player;

	/* Where it is at `now`, carried along from the sample -- either way,
	 * since a sample's time can be just ahead, when it is heard. */
	qint64 position_now(qint64 now = snurra_clock_us()) const {
		if (!playing) return position_us;
		return qBound<qint64>(0, position_us + (now - sampled_at_us), qMax<qint64>(0, duration_us));
	}
};

inline QDataStream &operator<<(QDataStream &out, const snurra_tile_state &state) {
	out << state.source << state.duration_us << state.position_us << state.sampled_at_us << state.playing
	           << qint32(state.level) << qint32(state.row) << qint32(state.column) << state.controls << state.envelope << state.offset_us << state.tempo << state.key_lock << qint32(state.audio_track) << state.lane_start_us << state.cue_us << state.eq.splits;
	for (float db : state.eq.db) out << db;
	out << qint32(state.key_shift) << state.overlap_us << state.sound << state.chapters << quint32(state.player.size());
	for (const snurra_tile_state &inner : state.player) out << inner;
	return out;
}

inline QDataStream &operator>>(QDataStream &in, snurra_tile_state &state) {
	qint32 level = 0;
	qint32 row = 0;
	qint32 column = 0;
	qint32 audio_track = -1;
	qint32 key_shift = 0;
	in >> state.source >> state.duration_us >> state.position_us >> state.sampled_at_us >> state.playing >> level
	   >> row >> column >> state.controls >> state.envelope >> state.offset_us >> state.tempo >> state.key_lock >> audio_track >> state.lane_start_us >> state.cue_us >> state.eq.splits;
	for (float &db : state.eq.db) in >> db;
	state.eq.splits &= snurra_eq::TWELVE;
	in >> key_shift >> state.overlap_us >> state.sound >> state.chapters;
	quint32 inner = 0;
	in >> inner;
	/* No more than a stream can hold: a count read from bytes that are not
	 * a state must not reserve a gigabyte. */
	state.player.clear();
	/* And no deeper than a person nests players: each level is a call. */
	static thread_local int depth = 0;
	if (inner > 0 && depth >= SNURRA_PLAYER_DEPTH) {
		in.setStatus(QDataStream::ReadCorruptData);
		return in;
	}
	++depth;
	for (quint32 i = 0; i < inner && in.status() == QDataStream::Ok && !in.atEnd(); ++i) {
		snurra_tile_state one;
		in >> one;
		state.player.push_back(std::move(one));
	}
	--depth;
	state.key_shift = key_shift;
	state.level = level;
	state.row = row;
	state.column = column;
	state.audio_track = audio_track;
	return in;
}

#endif
