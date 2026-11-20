#ifndef SNURRA_WIRE_H
#define SNURRA_WIRE_H

#include <QByteArray>
#include <QList>
#include <QLocalSocket>
#include <QRect>
#include <QString>

#include <functional>

#include "snurra/tile_state.h"

/*
 * The messages between snurra processes (project.md sec 3b), framed as a
 * 32-bit length and a QDataStream payload on a local socket. Every
 * process listens on one socket in a directory of its own; finding the
 * others is listing that directory.
 *
 * A handover is three messages and overlaps on purpose: OFFER carries the
 * states; READY says the taker is playing them, silently and in step with
 * the giver, and from when it could fade in; COMMIT names the moment both
 * cross over and for how long -- the giver fading out as the taker fades
 * in, over the same stretch of the shared clock -- so nothing is ever
 * silent and nothing is heard twice. A giver that hears no READY in time
 * sends ABORT and plays on. Times are microseconds on clock.h's clock.
 */
namespace snurra_wire {

/* A handover's times, the same on both sides: a window's and a deck
 * view's (snurra_node, snurra_dj). */
namespace timing {

/* How long a giver waits for READY: a peer already running answers in
 * milliseconds; a process started to adopt a tile needs to start first. */
inline constexpr int READY_WAIT_MS = 2000;
inline constexpr int ADOPT_WAIT_MS = 8000;

/*
 * The crossfade a handover makes. Both sides play the same samples in
 * step, so it is not heard as a fade at all if they are; if they are a
 * little apart, a short one keeps the comb filtering brief.
 */
inline constexpr qint64 CROSSFADE_US = 50000;

/* Slack for a COMMIT to cross the socket before its moment arrives. */
inline constexpr qint64 COMMIT_MARGIN_US = 20000;

/* How long a taker waits to fall in step before saying READY anyway, and
 * after READY for a COMMIT before fading in on its own. */
inline constexpr int IN_STEP_WAIT_MS = 500;
inline constexpr int COMMIT_WAIT_MS = 2000;

}

/* The MIME type of a tile in a drag: its process's socket, a newline,
 * and its index there. */
inline constexpr const char *TILE_MIME = "application/x-snurra-tile";

/* A deck view's socket ends so (snurra_dj::listen), a window's in
 * ".sock" alone: both are peers, and only a window is a player already
 * running (any_peer_alive). */
inline constexpr const char *DECKS_SUFFIX = ".decks.sock";

enum type_t : quint8 {
	WHERE = 1,     /* -> HERE: the window's frame on screen */
	HERE,
	OFFER,         /* token, side, states */
	READY,         /* token; time: the earliest the taker can fade in */
	COMMIT,        /* token; time: the crossfade starts, fade: its length */
	ABORT,         /* token */
	ADOPT,         /* token, tile index: hand that tile to the sender */
	MERGE_INTO,    /* control: hand every tile (or the one at index) to the process at this socket */
	RELEASE,       /* control: pull this tile out into a new process */
	STATUS,        /* -> STATUS_IS: tiles and the last handover's times */
	STATUS_IS,
	SHOW_CONTROLS, /* control: pull out (index >= 0) or push in a tile's controls; time 1 or 0 */
	PLAY,          /* control: as the tile at index's button would -- play (time 1) or pause (0) */
	SEEK,          /* control: as the tile at index's waveform would -- to time, in microseconds */
	LISTEN,        /* to a deck view: plug its first listener into the tile at index of the process at path (-1: out) */
};

/* Where the giver's tiles go relative to the taker's: a train continues to
 * the right, a stack below. */
enum side_t : quint8 { RIGHT = 0, BELOW = 1 };

struct message {
	type_t type = WHERE;
	QString token;
	side_t side = RIGHT;
	QList<snurra_tile_state> states;
	QRect frame;
	QString path;
	qint32 index = -1;
	qint64 time = 0;      /* READY, COMMIT: see above; HERE from a deck view: its listeners */
	qint64 fade = 0;      /* COMMIT: the crossfade's length */
	qint64 started = 0;   /* STATUS_IS: the last crossfade taken, its start */
	qint64 stopped = 0;   /* and its end, when the giver fell silent */
	/* OFFER, MERGE_INTO: the deck of a deck view meant (project.md sec
	 * 3f), -1 none -- then an OFFER's `frame` holds the point it was let
	 * go at, which the deck view finds its deck under. */
	qint32 deck = -1;
};

QByteArray encode(const message &m);

/* Takes every whole message off the front of `buffer`. */
QList<message> decode(QByteArray &buffer);

/* The directory peers' sockets live in; $SNURRA_RUNTIME_DIR overrides. */
QString runtime_directory();

/* Every peer's socket path but `own`. */
QStringList peer_sockets(const QString &own);

/* Whether any peer answers: a socket a crashed process left does not. */
bool any_peer_alive(const QString &own);

}

#endif
