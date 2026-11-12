#ifndef SNURRA_NODE_H
#define SNURRA_NODE_H

#include <QHash>
#include <QList>
#include <QPointer>
#include <QWidget>

#include "snurra/tile_state.h"
#include "snurra/wire.h"

class QGridLayout;
class QLocalServer;
class QLocalSocket;
class QTimer;
class snurra_engine;
class snurra_tile;
class snurra_marker;

/*
 * One snurra process: its window, the tiles in it, and its side of every
 * handover (project.md sec 3b). Tiles sit in a grid -- a row is a lane of
 * a stack, a column a place in a train.
 *
 * Giving: OFFER the tiles' states, keep playing; on READY choose when to
 * cross over, fade out from then, send COMMIT with the moment, and go once
 * the fade has been heard. Taking: on OFFER build the tiles with their
 * decks silent and in step with the giver's position, and once they are
 * in step send READY; on COMMIT fade in over the same moment. So the two
 * sum to one stream throughout. A giver that hears no READY in time sends
 * ABORT and keeps its tiles: a failed handover costs nothing; a taker
 * whose giver goes without a COMMIT fades in on its own.
 */
class snurra_node : public QWidget {
	Q_OBJECT

public:
	explicit snurra_node(QWidget *parent = nullptr);
	~snurra_node() override;

	/* Listens on this process's socket; false when it cannot. */
	bool listen();
	QString socket_path() const { return m_path; }

	/* `held`: its deck silent until a crossfade brings it in. */
	snurra_tile *add_tile(const snurra_tile_state &state, bool held = false);

	/* Hands every tile -- or the one at `index` -- to the process at
	 * `peer`, to its right or below. */
	void merge_into(const QString &peer, snurra_wire::side_t side, int index = -1);

	/* Pulls one tile out into a new process, placed at `at`. */
	void release(int index, const QPoint &at);

	/* Started as the taker of a tile another process releases. */
	void adopt(const QString &from, const QString &token, int index, const QPoint &at);

	int tile_count() const { return int(m_tiles.size()); }

	/* The normal look, drawers out of a window not a rectangle; or the
	 * square one (project.md sec 3b). */
	bool drawers_look() const { return m_drawers_look; }

protected:
	/* A tile dragged here from another snurra joins this one (Wayland's
	 * gesture, sec 3b), and the bare frame moves the window. */
	void dragEnterEvent(QDragEnterEvent *event) override;
	void dragMoveEvent(QDragMoveEvent *event) override;
	void dragLeaveEvent(QDragLeaveEvent *event) override;
	void dropEvent(QDropEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	/* mplayer's keys, on the tile last touched (src/node.cpp). */
	void keyPressEvent(QKeyEvent *event) override;

private:
	struct pending_give {
		QList<QPointer<snurra_tile>> tiles;
		QPointer<QLocalSocket> socket;
		QTimer *expiry = nullptr;
	};
	struct pending_take {
		QList<QPointer<snurra_tile>> tiles;
		QPointer<QLocalSocket> socket;
		QTimer *timer = nullptr;   /* until in step, then until COMMIT */
		qint64 since = 0;
		bool ready = false;
	};

	void accept();
	void watch(QLocalSocket *socket);
	void handle(QLocalSocket *socket, const snurra_wire::message &m);
	void send(QLocalSocket *socket, const snurra_wire::message &m);
	void offer(QLocalSocket *socket, const QString &token, snurra_wire::side_t side,
	           const QList<snurra_tile *> &tiles);
	void take(QLocalSocket *socket, const snurra_wire::message &m);
	void complete_give(QLocalSocket *socket, const snurra_wire::message &ready);
	void check_ready(const QString &token);
	void fade_in(const QString &token, qint64 start_us, qint64 length_us);
	void end_take(const QString &token);
	void abort_give(const QString &token);
	void remove_tile(snurra_tile *tile);
	void drop_tile(snurra_tile *tile);
	void place_tiles();
	void fit_to_drawers();
	void ask_where();
	void handle_moved(snurra_tile *tile, const QPoint &global);
	void handle_released(snurra_tile *tile, const QPoint &global);
	void drag_out(snurra_tile *tile);
	snurra_wire::side_t side_at(const QPoint &local) const;
	void hint_drop(bool show, snurra_wire::side_t side = snurra_wire::RIGHT);
	void leave_if_empty();
	void run_trains();
	/* The group's transport (project.md sec 3b, step 4): every lane at
	 * once, at a time on the group's timeline, in frames. */
	void play_group(snurra_tile *from, bool playing);
	void seek_group(snurra_tile *from, qint64 position_us);
	void start_group_at(int64_t time);
	QList<snurra_tile *> lane(int row) const;
	int64_t lane_start(snurra_tile *tile) const;
	/* The tile of lane `row` that plays at `time`, and its frame then;
	 * none past the lane's end. */
	snurra_tile *lane_at(int row, int64_t time, int64_t &frame) const;
	QList<int> rows() const;

	bool m_drawers_look = false;
	bool m_fitting = false;   /* a re-fit is queued */
	QString m_path;
	QLocalServer *m_server = nullptr;
	QGridLayout *m_grid = nullptr;
	QList<snurra_tile *> m_tiles;
	QHash<QString, pending_give> m_giving;
	QHash<QString, pending_take> m_taking;
	QHash<QString, QRect> m_peer_frames;
	QString m_adopt_token;
	QPoint m_adopt_at;
	QPoint m_drag_from;
	QPoint m_window_from;
	bool m_system_move_started = false;
	snurra_marker *m_marker = nullptr;
	QWidget *m_drop_hint = nullptr;   /* where a tile dropped here would go */
	QPointer<snurra_tile> m_current;  /* the tile the keys act on, last touched */
	void mark(const QRect &landing, const QRect &shape);
	snurra_engine *m_engine = nullptr;
	/* A train's next tile, started for the tile before it: when, so a
	 * stop before then can take it back. */
	struct following {
		QPointer<snurra_tile> next;
		qint64 at_us = 0;
	};
	QHash<snurra_tile *, following> m_following;
	QTimer *m_trains = nullptr;
	qint64 m_last_started = 0;   /* the last crossfade taken: its start */
	qint64 m_last_stopped = 0;   /* and its end */
};

#endif
