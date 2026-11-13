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
struct snurra_beat_grid;

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
                             const snurra_beat_grid *grid = nullptr, const snurra_beat_grid *next_grid = nullptr);

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

	/* Files dropped from a file manager: a tile each, to the right a
	 * train, below a stack (project.md sec 3b). */
	void add_files(const QStringList &files, snurra_wire::side_t side);

	/* A tune double-clicked in the library: a window of one tile or none
	 * plays it in its place at once, a player's way; a window of several
	 * -- a mix -- keeps them and takes it at the end of its first lane's
	 * train, paused, as a drop at its right edge. */
	void play_file(const QString &path);

	/* Open while the library is (library.h): emptied, the window hides
	 * rather than ending the process. Cleared, an empty one ends it. */
	void set_stays_open(bool stays);

	/* The normal look, drawers out of a window not a rectangle; or the
	 * square one (project.md sec 3b). */
	bool drawers_look() const { return m_drawers_look; }

	/* mplayer's -loop: the group played `times` times in all, from the
	 * timeline's start each time it ends of itself; 0 for ever. 1, once,
	 * is the default. */
	void set_loop(int times) { m_loops_left = times <= 0 ? -1 : times - 1; }

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
	void hint_drop(bool show, snurra_wire::side_t side = snurra_wire::RIGHT, snurra_tile *over = nullptr);
	/* The tile a drop at `local` lands on, to replace or swap with; none
	 * in the strip along the right and bottom edges, which add. */
	snurra_tile *tile_at(const QPoint &local) const;
	void replace_tile(snurra_tile *tile, const QString &file);
	void leave_if_empty();
	void run_trains();
	void show_crossfades();
	/* The group's transport (project.md sec 3b, step 4): every lane at
	 * once, at a time on the group's timeline, in frames. */
	void play_group(snurra_tile *from, bool playing);
	void seek_group(snurra_tile *from, qint64 position_us);
	void start_group_at(int64_t time);
	QList<snurra_tile *> lane(int row) const;
	int64_t lane_start(snurra_tile *tile) const;
	/* The tile of lane `row` that plays at `time`, and its frame then;
	 * none past the lane's end. */
	snurra_tile *lane_at(int row, int64_t time, int64_t &frame, int64_t *wait = nullptr) const;
	/* Where lane `row` begins on the timeline, in frames: its first
	 * tile's lane start (tile_state.h). */
	int64_t lane_begin(int row) const;
	void shift_lane(snurra_tile *tile, qint64 delta_us, bool done, bool snap);
	int64_t snapped_begin(int row, int64_t begin) const;
	int64_t overlap(snurra_tile *tile, snurra_tile *next) const;
	void step_train(snurra_tile *tile, int direction);
	QList<int> rows() const;
	int64_t time_of(snurra_tile *tile) const;
	int64_t group_time(snurra_tile *from) const;
	int64_t timeline_length(snurra_tile *tile) const;
	void nudge_lane(snurra_tile *tile, qint64 delta_us);
public:
	/* Sync (project.md sec 4): `tile`'s tempo set so its beat runs at the
	 * leader's -- the first other tile playing with a grid -- and its
	 * phase bent onto the leader's, as a nudge. False with no leader. */
	bool sync(snurra_tile *tile);
private:

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
	bool m_stays_open = false;
	int m_loops_left = 0;           /* plays still to come; -1 for ever */
	bool m_sounding = false;        /* some tile played at the last tick */
	bool m_stopped = false;         /* by a pause or a tile going: not an end */
	int m_shift_row = -1;          /* a lane being moved by a drag, */
	qint64 m_shift_from_us = 0;    /* and where it began before it */
};

#endif
