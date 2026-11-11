#ifndef SNURRA_NODE_H
#define SNURRA_NODE_H

#include <QHash>
#include <QList>
#include <QPointer>
#include <QWidget>

#include "tile_state.h"
#include "wire.h"

class QGridLayout;
class QLocalServer;
class QLocalSocket;
class QTimer;
class snurra_tile;

/*
 * One snurra process: its window, the tiles in it, and its side of every
 * handover (project.md sec 3b). Tiles sit in a grid -- a row is a lane of
 * a stack, a column a place in a train.
 *
 * Giving: OFFER the tiles' states, keep playing; on READY stop them, send
 * COMMIT, and go when nothing is left. Taking: on OFFER build the tiles,
 * start them at the extrapolated position, send READY -- so the taker is
 * sounding before the giver stops. A giver that hears no READY in time
 * sends ABORT and keeps its tiles: a failed handover costs nothing.
 */
class snurra_node : public QWidget {
	Q_OBJECT

public:
	explicit snurra_node(QWidget *parent = nullptr);
	~snurra_node() override;

	/* Listens on this process's socket; false when it cannot. */
	bool listen();
	QString socket_path() const { return m_path; }

	void add_tile(const snurra_tile_state &state);

	/* Hands every tile to the process at `peer`, to its right or below. */
	void merge_into(const QString &peer, snurra_wire::side_t side);

	/* Pulls one tile out into a new process, placed at `at`. */
	void release(int index, const QPoint &at);

	/* Started as the taker of a tile another process releases. */
	void adopt(const QString &from, const QString &token, int index, const QPoint &at);

	int tile_count() const { return int(m_tiles.size()); }

private:
	struct pending_give {
		QList<QPointer<snurra_tile>> tiles;
		QPointer<QLocalSocket> socket;
		QTimer *expiry = nullptr;
	};

	void accept();
	void watch(QLocalSocket *socket);
	void handle(QLocalSocket *socket, const snurra_wire::message &m);
	void send(QLocalSocket *socket, const snurra_wire::message &m);
	void offer(QLocalSocket *socket, const QString &token, snurra_wire::side_t side,
	           const QList<snurra_tile *> &tiles);
	void take(QLocalSocket *socket, const snurra_wire::message &m);
	void complete_give(QLocalSocket *socket, const QString &token);
	void abort_give(const QString &token);
	void remove_tile(snurra_tile *tile);
	void place_tiles();
	void ask_where();
	void handle_moved(snurra_tile *tile, const QPoint &global);
	void handle_released(snurra_tile *tile, const QPoint &global);
	void leave_if_empty();

	QString m_path;
	QLocalServer *m_server = nullptr;
	QGridLayout *m_grid = nullptr;
	QList<snurra_tile *> m_tiles;
	QHash<QString, pending_give> m_giving;
	QHash<QString, QList<QPointer<snurra_tile>>> m_taking;
	QHash<QString, QRect> m_peer_frames;
	QString m_adopt_token;
	QPoint m_adopt_at;
	QPoint m_drag_from;
	QPoint m_window_from;
	bool m_system_move_started = false;
	qint64 m_last_started = 0;   /* the last handover, as taker */
	qint64 m_last_stopped = 0;
};

#endif
