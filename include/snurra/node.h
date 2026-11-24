#ifndef SNURRA_NODE_H
#define SNURRA_NODE_H

#include <QHash>
#include <QList>
#include <QPointer>
#include <QWidget>

#include "snurra/mixer.h"
#include "snurra/tile_state.h"
#include "snurra/timeline.h"
#include "snurra/wire.h"

class QGridLayout;
class QImage;
class QLabel;
class QToolButton;
class snurra_ruler;
class QLocalServer;
class QLocalSocket;
class QTimer;
class snurra_engine;
class snurra_tile;
class snurra_marker;
struct snurra_beat_grid;

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
/* What the window's right-click menu says under "About snurra" (rich
 * text): the player sets it, with its version and copyright; a host
 * embedding the library sets none and has no such entry -- its own About
 * is its own. */
void snurra_set_about(const QString &rich_text);

class snurra_node : public QWidget {
	Q_OBJECT

public:
	explicit snurra_node(QWidget *parent = nullptr);
	~snurra_node() override;

	/* Listens on this process's socket; false when it cannot, and
	 * listen_error() says why -- the socket's path and the system's word,
	 * since a path past the 107 bytes a socket's name may take fails
	 * with nothing else to show for it. */
	bool listen();
	QString listen_error() const { return m_listen_error; }
	/* Controllers for this window as the player `player` (project.md sec
	 * 3f): an emergency mix, say, its own process, its play on a button
	 * of its own. Its controls are `play` (the group's, both ways) and
	 * `stop`, bound as `<player>:play`; Alt held on a tile's play button
	 * learns one. False without MIDI. */
	bool take_controllers(const QString &player);
	QString socket_path() const { return m_path; }

	/* `held`: its deck silent until a crossfade brings it in. */
	snurra_tile *add_tile(const snurra_tile_state &state, bool held = false);

	/* Hands every tile -- or the one at `index` -- to the process at
	 * `peer`, to its right or below; or, where `peer` is a deck view
	 * (project.md sec 3f), onto its deck `deck`, or the deck under `at`. */
	void merge_into(const QString &peer, snurra_wire::side_t side, int index = -1, int deck = -1,
	                const QPoint &at = QPoint());

	/* Pulls one tile out into a new process, placed at `at`. */
	void release(int index, const QPoint &at);

	/* Started as the taker of a tile another process releases. */
	void adopt(const QString &from, const QString &token, int index, const QPoint &at);

	/* This player into the deck view (project.md sec 3f): a new process
	 * opens the decks and takes it whole onto deck A, unfolded across
	 * them, gapless; this window then goes. Empty, the decks just open. */
	void open_decks();

	/* A view of the player the deck view at `decks` plays (project.md sec
	 * 3f: both views open at once): its tracks as this window's tiles,
	 * silent -- the decks sound it -- and standing where the decks are,
	 * following what changes there. When the decks hold it no longer,
	 * the window goes. */
	void follow(const QString &decks);
	bool follows_decks() const { return !m_follow.isEmpty(); }

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
	void changeEvent(QEvent *event) override;
	/* A tile dragged here from another snurra joins this one (Wayland's
	 * gesture, sec 3b), and the bare frame moves the window. */
	void dragEnterEvent(QDragEnterEvent *event) override;
	void dragMoveEvent(QDragMoveEvent *event) override;
	void dragLeaveEvent(QDragLeaveEvent *event) override;
	void dropEvent(QDropEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	bool event(QEvent *event) override;
	/* mplayer's keys, on the tile last touched (src/node.cpp). */
	void keyPressEvent(QKeyEvent *event) override;
	bool eventFilter(QObject *watched, QEvent *event) override;
	void contextMenuEvent(QContextMenuEvent *event) override;

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
	           const QList<snurra_tile *> &tiles, int deck = -1, const QPoint &at = QPoint());
	void take(QLocalSocket *socket, const snurra_wire::message &m);
	void complete_give(QLocalSocket *socket, const snurra_wire::message &ready);
	void check_ready(const QString &token);
	void fade_in(const QString &token, qint64 start_us, qint64 length_us);
	void end_take(const QString &token);
	void abort_give(const QString &token);
	void remove_tile(snurra_tile *tile);
	void drop_tile(snurra_tile *tile);
	void place_tiles();
	/* The grid's columns as spans of the timeline (place_tiles): every
	 * tile as wide as its time, the lanes on one scale. */
	void lay_out_timeline();
	/* The width at which every tile has room at the timeline's one scale,
	 * no wider than the screen; the window grows to it, never shrinks. */
	void widen_for_timeline(bool may_narrow = false);
	/* A width the person set -- with the grip, or the window manager --
	 * is theirs: no layout changes it after (the holder, 2026-11-18: "keep
	 * my width if I set it"). Told from the window's own resizes, which
	 * are made with m_resizing_self set. */
	bool m_width_chosen = false;
	bool m_resizing_self = false;
	int m_timeline_wanted = 0;   /* lay_out_timeline's: that width, for the tiles */
	void keep_on_screen();
	void timing_changed();
	void fit_to_drawers();
	void reshape();
	void ask_where();
	void handle_moved(snurra_tile *tile, const QPoint &global);
	void handle_released(snurra_tile *tile, const QPoint &global);
	void drag_out(snurra_tile *tile);
	snurra_wire::side_t side_at(const QPoint &local) const;
	void hint_drop(bool show, snurra_wire::side_t side = snurra_wire::RIGHT, snurra_tile *over = nullptr);
	/* The tile a drop at `local` lands on, to replace or swap with; none
	 * in the strip along the right and bottom edges, which add. */
	snurra_tile *tile_at(const QPoint &local, bool strips = true) const;
	/* Two tiles of this window trade places (and their lanes' begins). */
	void swap_tiles(snurra_tile *a, snurra_tile *b);
	void replace_tile(snurra_tile *tile, const QString &file);
	void leave_if_empty();
	/* The window's one bar (sec 3b). */
	QWidget *m_bar = nullptr;
	QLabel *m_bar_title = nullptr;
	QToolButton *m_bar_controls = nullptr;
	bool m_bar_held = false;
	bool m_moving_panel = false;   /* the panel being moved between tiles */
	/* Following a deck view's player (follow). */
	QString m_follow;
	QPointer<QLocalSocket> m_follow_socket;
	QTimer *m_follow_timer = nullptr;
	QList<snurra_tile_state> m_followed;   /* as last told, to see what changed */
	QList<snurra_tile_state> m_follow_base;   /* each tile as it stood after that, to see what the hand changed here */
	int64_t m_follow_held = -1;            /* paused, the time the lanes were held at */
	int m_follow_misses = 0;               /* asks gone unanswered in a row */
	int m_follow_empty = 0;
	int m_raise_seen = 0;                  /* the decks' "player" presses seen */                /* answers with no tracks before any had any */
	QString m_decks_token;                 /* the player given by open_decks(), followed once given */
	void followed(const snurra_wire::message &m);
	/* One picture to a window (sec 3b). */
	QPointer<snurra_tile> m_picture;
	bool m_picture_open = true;
	bool m_picture_heard = false;   /* m_picture shown because it played */
	QToolButton *m_bar_picture = nullptr;
	snurra_tile *picture_tile() const;
	void show_picture();
	double mix_for(snurra_tile *lead, QImage &other);
	/* The beats and bars along the top (sec 3b). */
	QWidget *m_ruler = nullptr;
	void fill_ruler(snurra_ruler &made) const;
	/* A tile dragged into a lane of its own (sec 3b): where, and its
	 * preview. */
	int new_lane_at(const QPoint &local, snurra_tile *moving, snurra_tile **over) const;
	void show_ghost(snurra_tile *moving, int row);
	void move_to_new_lane(snurra_tile *tile, int row);
	QLabel *m_ghost = nullptr;
	QWidget *m_ghost_line = nullptr;
	int m_ghost_row = -1;
	snurra_tile *m_ghost_tile = nullptr;
	QPoint m_bar_press;
	QPoint m_bar_window_from;
	void show_bar();
	void close_all();
	snurra_tile *panel_tile() const;
	void run_trains();
	void show_crossfades();
	/* The group's transport (project.md sec 3b, step 4): every lane at
	 * once, at a time on the group's timeline, in frames. */
	void play_group(snurra_tile *from, bool playing);
	void seek_group(snurra_tile *from, qint64 position_us);
	/* `moving`: the group is playing, and `time` is its time now. */
	void start_group_at(int64_t time, bool moving = false);
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
	/* The group's timeline as data (project.md sec 3f), from the tiles as
	 * they are now -- what every place on it is counted from. */
	snurra_timeline timeline() const;
	/* The tiles in the timeline's order -- row by row, each lane in order
	 * -- so a tile's index there is its track's. */
	QList<snurra_tile *> ordered() const;
	void nudge_lane(snurra_tile *tile, qint64 delta_us);
public:
	/* Sync (project.md sec 4): `tile`'s tempo set so its beat runs at the
	 * leader's -- the first other tile playing with a grid -- and its
	 * phase bent onto the leader's, as a nudge. False with no leader. */
	bool sync(snurra_tile *tile);
	/* Key sync: `tile`'s key moved to the leader's -- the first other tile
	 * playing with a key. False with no leader. */
	bool sync_key(snurra_tile *tile);
private:

	bool m_drawers_look = false;
	bool m_fitting = false;   /* a re-fit is queued */
	bool m_timing_queued = false;   /* a lay_out_timeline is */
	bool m_shape_queued = false;
	QString m_path;
	QString m_listen_error;
	QLocalServer *m_server = nullptr;
	QGridLayout *m_grid = nullptr;
	QList<snurra_tile *> m_tiles;
	QHash<QString, pending_give> m_giving;
	QHash<QString, pending_take> m_taking;
	QHash<QString, QRect> m_peer_frames;
	QHash<QString, int> m_peer_decks;   /* a deck view's decks, by its socket */
	QHash<QString, int> m_peer_listeners;   /* and its listeners */
	QTimer *m_listener_poll = nullptr;
	void show_sockets();
	void listen_at(snurra_tile *tile, bool on);
	class snurra_controller *m_controller = nullptr;
	void name_controls(snurra_tile *tile);
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
	/* The master section's window, opened from the bar (sec 3f, e). */
	QWidget *m_master_window = nullptr;   /* the master apart, a window of its own */
	QWidget *m_master_drawer = nullptr;   /* under the panel, or in the window */
	snurra_mixer::master_strip m_master_strip;
	bool m_master_open = false;
	bool m_master_apart = false;
	void show_master(bool open);
	void place_master();
	/* Quiet while nothing of it is seen (update_quiet). */
	bool m_quiet = false;
	bool m_behind_decks = false;
	void update_quiet();
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
	snurra_tile *m_shift_tile = nullptr;   /* or a train's later tile, */
	int64_t m_shift_from_overlap = 0;      /* and its overlap before it */
	qint64 m_shift_from_us = 0;    /* and where it began before it */
};

#endif
