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
	qint64 time = 0;      /* READY, COMMIT: see above */
	qint64 fade = 0;      /* COMMIT: the crossfade's length */
	qint64 started = 0;   /* STATUS_IS: the last crossfade taken, its start */
	qint64 stopped = 0;   /* and its end, when the giver fell silent */
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
