#include "snurra/node.h"

#include <QCoreApplication>
#include <QCursor>
#include <QDrag>
#include <QDragEnterEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QUrl>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPixmap>
#include <QGridLayout>
#include <QGuiApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMap>
#include <QProcess>
#include <QSizeGrip>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>

#if SNURRA_XCB
#include <xcb/xcb.h>

#include <cstdlib>
#endif

#include "snurra/clock.h"
#include "snurra/deck.h"
#include "snurra/engine.h"
#include "snurra/marker.h"
#include "snurra/snap.h"
#include "snurra/source.h"
#include "snurra/tile.h"

namespace {

/* How long a giver waits for READY: a peer already running answers in
 * milliseconds; a process started to adopt a tile needs to start first. */
constexpr int READY_WAIT_MS = 2000;
constexpr int ADOPT_WAIT_MS = 8000;

/*
 * The crossfade a handover makes. Both sides play the same samples in
 * step, so it is not heard as a fade at all if they are; if they are a
 * little apart, a short one keeps the comb filtering brief.
 */
constexpr qint64 CROSSFADE_US = 50000;

/* Slack for a COMMIT to cross the socket before its moment arrives. */
constexpr qint64 COMMIT_MARGIN_US = 20000;

/* How long a taker waits to fall in step before saying READY anyway, and
 * after READY for a COMMIT before fading in on its own. */
constexpr int IN_STEP_WAIT_MS = 500;
constexpr int COMMIT_WAIT_MS = 2000;

QString new_token() {
	return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

/* The MIME type of a tile in a drag: its process's socket, a newline,
 * and its index there. */
const char *const TILE_MIME = "application/x-snurra-tile";

bool can_place_windows() {
	/* X11 lets a program move its own windows and read others'; Wayland
	 * does neither, so there a drag is the compositor's (project.md 3b). */
	return QGuiApplication::platformName() == QLatin1String("xcb")
	       || QGuiApplication::platformName() == QLatin1String("offscreen");
}

/*
 * Tiles join by dragging one onto another -- a drag and drop, which the
 * compositor carries between programs -- where a program can neither
 * place its windows nor learn where the others are: Wayland (sec 3b).
 * $SNURRA_DRAG=dnd chooses it anywhere, which is how it is tested, on X11.
 */
bool tiles_join_by_drop() {
	return !can_place_windows() || qgetenv("SNURRA_DRAG") == "dnd";
}

}

/*
 * The look (project.md sec 3b): the normal one, drawers sticking out of a
 * window that is transparent around them, wherever the window system can
 * draw a window that is not a rectangle -- Wayland always, X11 when a
 * compositor holds its selection -- and otherwise the square one, the
 * same layout on an opaque window. $SNURRA_LAYOUT (--layout) decides it
 * outright: `drawers` or `square`.
 */
/* Whether the window can be seen through: a compositor draws what is
 * behind it -- Wayland always, X11 when one holds its selection. */
static bool see_through() {
	if (qgetenv("SNURRA_LAYOUT") == "drawers") return true;
	const QString platform = QGuiApplication::platformName();
	if (platform == QLatin1String("wayland")) return true;
#if SNURRA_XCB
	if (platform == QLatin1String("xcb")) {
		auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
		xcb_connection_t *connection = x11 ? x11->connection() : nullptr;
		if (!connection) return false;
		static const char name[] = "_NET_WM_CM_S0";
		xcb_intern_atom_reply_t *atom
		        = xcb_intern_atom_reply(connection, xcb_intern_atom(connection, 1, sizeof name - 1, name), nullptr);
		if (!atom) return false;
		xcb_get_selection_owner_reply_t *owner
		        = xcb_get_selection_owner_reply(connection, xcb_get_selection_owner(connection, atom->atom), nullptr);
		const bool composited = owner && owner->owner != XCB_NONE;
		free(owner);
		free(atom);
		return composited;
	}
#endif
	return false;
}

/*
 * The normal look, everywhere a window can have a shape: seen through
 * where a compositor draws behind it, and cut to the tiles and their open
 * drawers (reshape) either way -- X11's shape needs no compositor. Square
 * only when asked, or where nothing can say (offscreen). The holder,
 * 2026-11-13: "we should not have a large square window around the
 * players", on X11 with no compositor, where it had been square.
 */
static bool look_with_drawers() {
	const QByteArray asked = qgetenv("SNURRA_LAYOUT");
	if (asked == "square") return false;
	if (asked == "drawers") return true;
	const QString platform = QGuiApplication::platformName();
	return platform == QLatin1String("wayland") || platform == QLatin1String("xcb");
}

snurra_node::snurra_node(QWidget *parent) : QWidget(parent) {
	m_drawers_look = look_with_drawers();
	if (m_drawers_look && see_through()) setAttribute(Qt::WA_TranslucentBackground);
	qInfo("snurra: the %s look", m_drawers_look ? "drawers" : "square");
	m_engine = new snurra_engine(this);
	connect(m_engine, &snurra_engine::idle, this, &snurra_node::leave_if_empty);
	setWindowFlag(Qt::FramelessWindowHint);
	setWindowTitle(QStringLiteral("snurra"));
	QVBoxLayout *outer = new QVBoxLayout(this);
	outer->setContentsMargins(2, 2, 2, 2);
	outer->setSpacing(0);
	m_grid = new QGridLayout;
	m_grid->setHorizontalSpacing(2);
	m_grid->setVerticalSpacing(0);   /* drawers meet their tiles */
	outer->addLayout(m_grid, 1);
	/* Resizeable, though frameless: the grip in the corner. */
	/* Over the bottom right corner rather than in a row of its own: a row
	 * the tiles do not fill was a strip of nothing under them, cut away
	 * by the shape, where a drop by the bottom edge then landed. */
	QSizeGrip *grip = new QSizeGrip(this);
	grip->resize(grip->sizeHint());

	setFocusPolicy(Qt::StrongFocus);
	m_trains = new QTimer(this);
	m_trains->setInterval(100);
	connect(m_trains, &QTimer::timeout, this, &snurra_node::run_trains);
	m_trains->start();

	setAcceptDrops(true);
	/* Where a tile dropped here would go: a strip along that edge, over the
	 * tiles, taking no input so the drag goes on through it. */
	m_drop_hint = new QWidget(this);
	m_drop_hint->setAttribute(Qt::WA_TransparentForMouseEvents);
	m_drop_hint->setAttribute(Qt::WA_StyledBackground);
	m_drop_hint->setStyleSheet(QStringLiteral("background: palette(highlight);"));
	m_drop_hint->hide();
}

snurra_node::~snurra_node() {
	/* First: the sockets are children, deleted by QWidget's destructor after
	 * this one has destroyed m_taking -- and a socket going says
	 * disconnected, whose handler here read m_taking (ThreadSanitizer: a
	 * use after free, at every exit with a take open). */
	for (QLocalSocket *socket : findChildren<QLocalSocket *>()) socket->disconnect(this);
	delete m_marker;
	if (m_server) {
		m_server->close();
		QLocalServer::removeServer(m_path);
	}
}

bool snurra_node::listen() {
	m_path = snurra_wire::runtime_directory() + QStringLiteral("/%1.sock").arg(QCoreApplication::applicationPid());
	QLocalServer::removeServer(m_path);
	m_server = new QLocalServer(this);
	m_server->setSocketOptions(QLocalServer::UserAccessOption);
	if (!m_server->listen(m_path)) return false;
	connect(m_server, &QLocalServer::newConnection, this, &snurra_node::accept);
	return true;
}

void snurra_node::accept() {
	while (QLocalSocket *socket = m_server->nextPendingConnection()) watch(socket);
}

void snurra_node::watch(QLocalSocket *socket) {
	auto buffer = std::make_shared<QByteArray>();
	connect(socket, &QLocalSocket::readyRead, this, [this, socket, buffer] {
		buffer->append(socket->readAll());
		for (const snurra_wire::message &m : snurra_wire::decode(*buffer)) handle(socket, m);
	});
	connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
}

void snurra_node::send(QLocalSocket *socket, const snurra_wire::message &m) {
	if (socket && socket->state() == QLocalSocket::ConnectedState) socket->write(snurra_wire::encode(m));
}

snurra_tile *snurra_node::add_tile(const snurra_tile_state &state, bool held) {
	/*
	 * A stem file is a stack of its own (project.md sec 3e, step 1): a lane
	 * a stem, the mix left out, from the start of the timeline -- below
	 * what is here, since a lane cannot yet start later than the
	 * timeline does (sec 3b). Played or paused together, as lanes are;
	 * the first holds the controls. The first stem's tile is returned.
	 */
	const QStringList stems = state.audio_track < 0 ? snurra_stem_names(state.source) : QStringList();
	if (!stems.isEmpty()) {
		int row = state.row;
		if (!m_tiles.isEmpty()) {
			row = 0;
			for (snurra_tile *tile : m_tiles) row = qMax(row, tile->row() + 1);
		}
		snurra_tile *first = nullptr;
		for (int i = 0; i < stems.size(); ++i) {
			snurra_tile_state stem = state;
			stem.audio_track = i + 1;
			stem.row = row + i;
			stem.column = 0;
			stem.controls = state.controls && i == 0;
			snurra_tile *tile = add_tile(stem, held);
			if (!first) first = tile;
		}
		return first;
	}
	snurra_deck::start_t start;
	start.source = state.source;
	start.rate = m_engine->rate();
	start.fallback_length = snurra_us_to_frames(state.duration_us, start.rate);
	start.anchor_frame = snurra_us_to_frames(state.position_now(state.sampled_at_us), start.rate);
	start.anchor_us = state.sampled_at_us;
	start.playing = state.playing;
	start.level = float(state.level) / 100;
	start.held = held;
	start.anchored = held;   /* in step with a giver: only a handover's taker */
	start.audio_track = state.audio_track;
	snurra_tile *tile = new snurra_tile(state, m_engine->add(start), this);
	tile->take_drawers_out();
	connect(tile, &snurra_tile::handle_moved, this, &snurra_node::handle_moved);
	connect(tile, &snurra_tile::handle_released, this, &snurra_node::handle_released);
	connect(tile, &snurra_tile::touched, this, [this](snurra_tile *touched) { m_current = touched; });
	tile->set_grouped(true);
	connect(tile, &snurra_tile::play_requested, this, &snurra_node::play_group);
	connect(tile, &snurra_tile::drawers_changed, this, &snurra_node::fit_to_drawers);
	connect(tile, &snurra_tile::nudge_requested, this, &snurra_node::nudge_lane);
	connect(tile, &snurra_tile::sync_requested, this, [this](snurra_tile *asking) { sync(asking); });
	connect(tile, &snurra_tile::key_sync_requested, this, [this](snurra_tile *asking) { sync_key(asking); });
	connect(tile, &snurra_tile::seek_requested, this, &snurra_node::seek_group);
	connect(tile, &snurra_tile::shift_requested, this, &snurra_node::shift_lane);
	/* Dropped on the full-screen picture, where nothing else of the window
	 * shows: the train goes on. */
	connect(tile, &snurra_tile::files_dropped, this,
	        [this](const QStringList &files) { add_files(files, snurra_wire::RIGHT); });
	connect(tile, &snurra_tile::controls_changed, this, [this] {
		/* What plays is what can be stopped (sec 3c): with no controls
		 * out anywhere, the group stops. */
		fit_to_drawers();
		for (snurra_tile *other : m_tiles)
			if (other->controls()) return;
		for (snurra_tile *other : m_tiles)
			if (other->playing()) other->set_playing(false);
	});
	connect(tile, &snurra_tile::close_requested, this, [this](snurra_tile *closing) {
		remove_tile(closing);
		leave_if_empty();
	});
	m_tiles.append(tile);
	place_tiles();
	return tile;
}

void snurra_node::place_tiles() {
	/*
	 * Rows and columns renumbered to close the gaps a tile leaving makes,
	 * in their order, and the grid laid out again in one go with updates
	 * held, so a merge is one repaint rather than one per tile.
	 */
	QMap<int, int> rows;
	QMap<int, int> columns;
	for (snurra_tile *tile : m_tiles) {
		rows.insert(tile->row(), 0);
		columns.insert(tile->column(), 0);
	}
	int n = 0;
	for (auto it = rows.begin(); it != rows.end(); ++it) it.value() = n++;
	n = 0;
	for (auto it = columns.begin(); it != columns.end(); ++it) it.value() = n++;

	/* Each lane three rows of the grid: its upward drawers, its tiles,
	 * its downward drawers (project.md sec 3b) -- so every waveform of a
	 * lane is on one line whatever is open, and a row of drawers all
	 * closed takes no room. Spare height goes below the last lane. */
	setUpdatesEnabled(false);
	for (snurra_tile *tile : m_tiles) {
		m_grid->removeWidget(tile);
		m_grid->removeWidget(tile->drawer_above());
		m_grid->removeWidget(tile->drawer_below());
	}
	for (int row = 0; row < m_grid->rowCount(); ++row) m_grid->setRowStretch(row, 0);
	for (snurra_tile *tile : m_tiles) {
		tile->set_place(rows.value(tile->row()), columns.value(tile->column()));
		const int lane = 3 * tile->row();
		m_grid->addWidget(tile->drawer_above(), lane, tile->column());
		m_grid->addWidget(tile, lane + 1, tile->column());
		m_grid->addWidget(tile->drawer_below(), lane + 2, tile->column());
		tile->show();
	}
	m_grid->setRowStretch(3 * int(rows.size()), 1);
	setUpdatesEnabled(true);
	adjustSize();

	/* Titled by what it plays -- the first tile's file, and how many
	 * more -- so a taskbar or a window switcher tells windows apart. */
	snurra_tile *first = nullptr;
	for (snurra_tile *tile : m_tiles)
		if (!first || tile->row() < first->row() || (tile->row() == first->row() && tile->column() < first->column()))
			first = tile;
	QString title = QStringLiteral("snurra");
	if (first) {
		title = snurra_title_of(first->state().source);
		if (m_tiles.size() > 1) title += QStringLiteral(" +%1").arg(m_tiles.size() - 1);
	}
	setWindowTitle(title);
	reshape();
}

/* The window as tall as what is open: a drawer opened adds its height, a
 * drawer closed gives it back; the width, which a person may have set,
 * stays. And the tiles stay where they are on the screen: a drawer that
 * opens upward grows the window up, not the tiles down. After the event
 * loop, once the drawer's own show or hide is in the layout. */
void snurra_node::fit_to_drawers() {
	if (m_fitting) return;
	m_fitting = true;
	/* Where the first tile is now, before the layout has heard of the
	 * drawer: Qt lays out on an event already queued ahead of this one. */
	snurra_tile *first = m_tiles.isEmpty() ? nullptr : m_tiles.first();
	const QPoint before = first ? first->mapTo(this, QPoint()) : QPoint();
	QTimer::singleShot(0, this, [this, before, anchor = QPointer<snurra_tile>(first)] {
		m_fitting = false;
		layout()->activate();
		resize(qMax(width(), minimumSizeHint().width()), sizeHint().height());
		layout()->activate();
		if (anchor && isWindow() && isVisible()) move(pos() - (anchor->mapTo(this, QPoint()) - before));
		reshape();
	});
}

void snurra_node::resizeEvent(QResizeEvent *event) {
	QWidget::resizeEvent(event);
	if (QSizeGrip *grip = findChild<QSizeGrip *>(QString(), Qt::FindDirectChildrenOnly)) {
		grip->move(width() - grip->width(), height() - grip->height());
		grip->raise();
	}
	reshape();
}

/* The tiles placed again without the window's size changing -- shown, or
 * laid out -- is a new shape too: one cut before the layout had placed
 * them missed a drop on the window under xcb. */
bool snurra_node::event(QEvent *event) {
	if (event->type() == QEvent::LayoutRequest || event->type() == QEvent::Show) reshape();
	return QWidget::event(event);
}

/*
 * The window cut to what it holds (the normal look): each tile and its
 * open drawers, with the 2 pixels round them -- so the frame between two
 * tiles is still there to move the window by -- the corner's size grip,
 * and a drop's hint while one shows. After the layout has settled.
 */
void snurra_node::reshape() {
	if (!m_drawers_look || m_shape_queued) return;
	m_shape_queued = true;
	QTimer::singleShot(0, this, [this] {
		m_shape_queued = false;
		if (m_tiles.isEmpty()) {
			clearMask();
			return;
		}
		QRegion shape;
		auto add = [&shape](const QWidget *part) {
			if (part && part->isVisible()) shape += part->geometry().adjusted(-2, -2, 2, 2);
		};
		for (snurra_tile *tile : m_tiles) {
			add(tile);
			add(tile->drawer_above());
			add(tile->drawer_below());
		}
		add(findChild<QSizeGrip *>());
		add(m_drop_hint);
		setMask(shape);
	});
}

void snurra_node::remove_tile(snurra_tile *tile) {
	if (!m_tiles.contains(tile)) return;
	m_following.remove(tile);
	m_engine->retire(tile->deck());
	drop_tile(tile);
}

/* Out of the window, its deck left to whoever retires it. */
void snurra_node::drop_tile(snurra_tile *tile) {
	m_following.remove(tile);
	if (tile->playing()) m_stopped = true;   /* gone, not ended */
	/* A lane's first going: the lane keeps its begin, and its train closes
	 * up behind it as from the timeline's start -- the next tile takes it. */
	const QList<snurra_tile *> tiles = lane(tile->row());
	if (tiles.size() > 1 && tiles.first() == tile) tiles.at(1)->set_lane_start_us(tile->lane_start_us());
	if (!m_tiles.removeOne(tile)) return;
	m_grid->removeWidget(tile);
	m_grid->removeWidget(tile->drawer_above());
	m_grid->removeWidget(tile->drawer_below());
	tile->hide();
	if (tile->drawer_above()) tile->drawer_above()->hide();
	if (tile->drawer_below()) tile->drawer_below()->hide();
	tile->deleteLater();
	place_tiles();
	/* Narrowed to what is left: where the tile was is nothing now, cut
	 * away by the shape, and a window wider than its tiles is a target
	 * for drops that miss them. */
	QTimer::singleShot(0, this, [this] {
		if (m_tiles.isEmpty()) return;
		layout()->activate();
		if (width() > sizeHint().width()) resize(sizeHint().width(), height());
	});
	fit_to_drawers();
}

/*
 * Trains (project.md sec 3b): tiles side by side in a row play as one
 * sequence. When a playing tile comes within reach of its end, the next in
 * its row is started at frame 0 on the exact moment the playing one
 * reaches its length less the overlap, and the two cross over that
 * overlap along a quarter circle -- two different tracks, so equal power,
 * where a handover's identical signals cross in straight lines. The
 * controls follow what plays: out on the next tile as it is started, in on
 * the one before once it has ended. A tile stopped before its next began
 * takes the next back.
 *
 * The overlap is 5 s, or $SNURRA_TRAIN_OVERLAP_MS, and never more than
 * half of either tile -- a decision taken from the aims (a DJ mix's
 * crossfade). Where both tracks have a beat (step 6) it is whole bars of
 * the first, moved by less than a beat so the next one's first beat falls
 * on a beat of the first: a DJ brings the next record in on the beat.
 * The tempos are not matched here -- sync is the DJ's own choice (sec 4)
 * -- so two tempos apart drift through the overlap from in phase.
 */
int64_t snurra_train_overlap(int64_t length, int64_t next_length, int rate, double tempo, double next_tempo,
                             const snurra_beat_grid *grid, const snurra_beat_grid *next_grid) {
	const QByteArray wanted = qgetenv("SNURRA_TRAIN_OVERLAP_MS");
	const qint64 overlap_us = wanted.isEmpty() ? 5000000 : qint64(wanted.toLongLong()) * 1000;
	const int64_t room = std::min(length / 2, next_length / 2);
	const int64_t plain = std::min(snurra_us_to_frames(overlap_us, rate), room);
	if (plain <= 0 || !grid || !next_grid || !grid->valid() || !next_grid->valid() || tempo <= 0 || next_tempo <= 0)
		return plain;
	/* In frames out: a beat of the first, and the next one's first beat
	 * after it starts. */
	const double beat = grid->period / tempo;
	const double bar = 4 * beat;
	const double whole = std::max(1.0, std::round(double(plain) / bar)) * bar;
	const double next_first = next_grid->first / next_tempo;
	/* The overlap is length + next_first - (the first's beat k, out): the
	 * k that comes nearest the whole bars. */
	const double k = std::round(((double(length) + next_first - whole) * tempo - grid->first) / grid->period);
	double snapped = double(length) + next_first - (grid->first + k * grid->period) / tempo;
	while (snapped > double(room)) snapped -= beat;
	while (snapped <= 0) snapped += beat;
	if (snapped > double(room)) return plain;   /* no beat of it fits */
	return std::llround(snapped);
}

/* The overlap between `tile` and `next` on their lane's timeline: the
 * one the train plays and every seek counts with. */
int64_t snurra_node::overlap(snurra_tile *tile, snurra_tile *next) const {
	if (!next) return 0;
	const snurra_beat_grid grid = tile->beats();
	const snurra_beat_grid next_grid = next->beats();
	return snurra_train_overlap(timeline_length(tile), timeline_length(next), m_engine->rate(), tile->deck()->tempo(),
	                            next->deck()->tempo(), &grid, &next_grid);
}

void snurra_node::run_trains() {
	const qint64 now = snurra_clock_us();
	constexpr qint64 LEAD_US = 1500000;   /* for the next one's reader to be ahead */

	/* What was started: taken back if its tile stopped before it began;
	 * finished with once that tile has ended. */
	for (auto it = m_following.begin(); it != m_following.end();) {
		snurra_tile *before = it.key();
		if (!m_tiles.contains(before)) {
			it = m_following.erase(it);
			continue;
		}
		if (!before->playing()) {
			if (now < it->at_us && it->next) {
				it->next->set_playing(false);
				it->next->deck()->fade(0, 0, 1, 1);
			} else {
				before->set_controls(false);   /* ended: what plays is the next */
			}
			/* Its fade out is done with: played again, it is heard. */
			before->deck()->fade(0, 0, 1, 1);
			it = m_following.erase(it);
			continue;
		}
		++it;
	}

	for (snurra_tile *tile : m_tiles) {
		if (!tile->playing() || m_following.contains(tile)) continue;
		snurra_tile *next = nullptr;
		for (snurra_tile *other : m_tiles)
			if (other->row() == tile->row() && other->column() > tile->column()
			    && (!next || other->column() < next->column()))
				next = other;
		if (!next || next->playing()) continue;
		snurra_deck *deck = tile->deck();
		const int rate = deck->rate();
		const int64_t length = deck->length();
		const int64_t next_length = next->deck()->length();
		if (length <= 0 || next_length <= 0) continue;
		/* In frames out: a tile at a tempo plays its length in length /
		 * tempo of them, and the overlap is counted out, where both sound. */
		const double tempo = deck->tempo();
		int64_t frame = 0;
		int64_t at = 0;
		int64_t advanced = 0;
		deck->last_rendered(frame, at, advanced);
		if (advanced <= 0) continue;   /* starved or paused: no moment to aim at */
		const int64_t overlap = this->overlap(tile, next);
		const qint64 start = at + snurra_frames_to_us(int64_t(std::llround((double(length) - double(overlap) * tempo - double(frame)) / tempo)), rate);
		if (start - now > LEAD_US || start < now) continue;
		const qint64 fade_us = snurra_frames_to_us(overlap, rate);
		next->deck()->start_at(0, start);
		next->deck()->fade(start, fade_us, 0, 1, true);
		next->set_playing(true);
		next->set_controls(true);
		deck->fade(start, fade_us, 1, 0, true);
		m_following.insert(tile, following{ next, start });
	}
	show_crossfades();

	/* An end, not a pause: the group plays again from the start while
	 * -loop has plays left. */
	bool sounding = false;
	for (snurra_tile *tile : m_tiles) sounding = sounding || tile->playing();
	if (m_sounding && !sounding && !m_stopped && m_loops_left != 0 && !m_tiles.isEmpty()) {
		if (m_loops_left > 0) --m_loops_left;
		start_group_at(0);
		sounding = true;
	}
	m_sounding = sounding;
}

/*
 * A train's crossfades drawn on its tiles (project.md sec 3b): each tile's
 * overlap with the one before as its fade in, with the one after as its
 * fade out -- the same overlap the train plays -- as fractions of the
 * track. On every tick of the trains, so a length found, a tempo moved or
 * a tile come or gone shows within a tenth of a second.
 */
void snurra_node::show_crossfades() {
	for (int row : rows()) {
		const QList<snurra_tile *> tiles = lane(row);
		for (int i = 0; i < tiles.size(); ++i) {
			snurra_tile *tile = tiles.at(i);
			const double length = double(timeline_length(tile));
			if (length <= 0) {
				tile->set_crossfade(0, 0);
				continue;
			}
			const double in = i > 0 ? double(overlap(tiles.at(i - 1), tile)) / length : 0;
			const double out = i + 1 < tiles.size() ? double(overlap(tile, tiles.at(i + 1))) / length : 0;
			tile->set_crossfade(in, out);
		}
	}
}

/*
 * The stack (project.md sec 3b, step 4): rows are lanes, played at once on
 * one timeline, each lane a train along it. So the transport is the
 * group's -- play, pause or seek on any tile moves every lane -- and a
 * time on that timeline is a lane's own: its tiles one after another, each
 * next one starting its overlap before the one before ends, as a train
 * plays them. Every lane is started on one moment of the shared clock,
 * anchored (deck.h), so the lanes are in step to the frame however their
 * readers fare; the lead only keeps a slow reader from losing a lane's
 * first frames.
 */
QList<snurra_tile *> snurra_node::lane(int row) const {
	QList<snurra_tile *> tiles;
	for (snurra_tile *tile : m_tiles)
		if (tile->row() == row) tiles << tile;
	std::sort(tiles.begin(), tiles.end(), [](snurra_tile *a, snurra_tile *b) { return a->column() < b->column(); });
	return tiles;
}

int64_t snurra_node::lane_begin(int row) const {
	const QList<snurra_tile *> tiles = lane(row);
	return tiles.isEmpty() ? 0 : snurra_us_to_frames(tiles.first()->lane_start_us(), m_engine->rate());
}

int64_t snurra_node::lane_start(snurra_tile *tile) const {
	const QList<snurra_tile *> tiles = lane(tile->row());
	int64_t start = lane_begin(tile->row());
	for (int i = 0; i < tiles.size() && tiles.at(i) != tile; ++i) {
		const int64_t length = timeline_length(tiles.at(i));
		start += length - overlap(tiles.at(i), i + 1 < tiles.size() ? tiles.at(i + 1) : nullptr);
	}
	return start;
}

/* A tile's length on its lane's timeline, in frames out: its track's
 * length at its tempo. */
int64_t snurra_node::timeline_length(snurra_tile *tile) const {
	return int64_t(double(tile->length_frames()) / tile->deck()->tempo());
}

void snurra_node::play_group(snurra_tile *from, bool playing) {
	if (!playing) {
		m_stopped = true;
		for (snurra_tile *tile : m_tiles)
			if (tile->playing()) tile->set_playing(false);
		return;
	}
	/* Played at its end -- every lane past its last tile -- it plays
	 * again from the start, as Winamp's play after the end does; else
	 * nothing at all would sound. */
	int64_t time = group_time(from);
	bool any = false;
	for (int row : rows()) {
		int64_t frame = 0;
		any = any || lane_at(row, time, frame) != nullptr;
	}
	start_group_at(any ? time : 0);
}

/*
 * The group's time, from `from` where it says it: a late lane not yet
 * begun stands at its first frame, which says only that the time is not
 * past its begin -- then another lane's tile, one that has begun, says it.
 */
int64_t snurra_node::group_time(snurra_tile *from) const {
	auto begun = [this](snurra_tile *tile) {
		const QList<snurra_tile *> tiles = lane(tile->row());
		return lane_begin(tile->row()) == 0 || tiles.isEmpty() || tiles.first() != tile
		       || tile->frame_now() > snurra_us_to_frames(tile->offset_us(), m_engine->rate());
	};
	if (begun(from)) return time_of(from);
	for (snurra_tile *tile : m_tiles)
		if (tile->playing() && begun(tile)) return time_of(tile);
	for (snurra_tile *tile : m_tiles)
		if (begun(tile)) return std::min(time_of(tile), time_of(from));
	return time_of(from);
}

QList<int> snurra_node::rows() const {
	QList<int> rows;
	for (snurra_tile *tile : m_tiles)
		if (!rows.contains(tile->row())) rows << tile->row();
	return rows;
}

snurra_tile *snurra_node::lane_at(int row, int64_t time, int64_t &frame, int64_t *wait) const {
	const QList<snurra_tile *> tiles = lane(row);
	int64_t start = lane_begin(row);
	if (wait) *wait = 0;
	/* Before a late lane begins: its first tile, from its start, after the
	 * wait -- a start on the shared clock still to come (deck.h). */
	if (!tiles.isEmpty() && time < start) {
		frame = std::max<int64_t>(0, snurra_us_to_frames(tiles.first()->offset_us(), m_engine->rate()));
		if (wait) *wait = start - time;
		return tiles.first();
	}
	for (int i = 0; i < tiles.size(); ++i) {
		const int64_t length = timeline_length(tiles.at(i));
		const int64_t own = length - overlap(tiles.at(i), i + 1 < tiles.size() ? tiles.at(i + 1) : nullptr);
		/* Inside an overlap belongs to the next: a seek into a crossfade
		 * comes in on the track it goes to. A length not known yet is
		 * taken as long enough. */
		if (length <= 0 || time < start + own) {
			const double tempo = tiles.at(i)->deck()->tempo();
			frame = std::max<int64_t>(0, std::llround(double(time - start) * tempo)
			                                     + snurra_us_to_frames(tiles.at(i)->offset_us(), m_engine->rate()));
			return tiles.at(i);
		}
		start += own;
	}
	return nullptr;
}

void snurra_node::seek_group(snurra_tile *from, qint64 position_us) {
	const int64_t time = lane_start(from) + std::llround(double(snurra_us_to_frames(position_us - from->offset_us(), m_engine->rate()))
	                                                    / from->deck()->tempo());
	for (snurra_tile *tile : m_tiles) {
		if (tile->playing()) {
			start_group_at(time, true);
			return;
		}
	}
	/* Paused: each lane held where the time falls in it. */
	for (int row : rows()) {
		int64_t frame = 0;
		if (snurra_tile *tile = lane_at(row, time, frame)) tile->hold_at(frame);
	}
}

/*
 * mplayer's < and >: the playlist's previous and next file, which here is
 * the train's -- the lane's previous or next tile, from its start, the
 * group with it. The one now is the lane's playing tile, else the one the
 * keys are on; before the first is the first's start.
 */
void snurra_node::step_train(snurra_tile *tile, int direction) {
	const QList<snurra_tile *> tiles = lane(tile->row());
	snurra_tile *now = tile;
	for (snurra_tile *candidate : tiles)
		if (candidate->playing()) now = candidate;
	const int to = qBound(0, int(tiles.indexOf(now)) + direction, int(tiles.size()) - 1);
	if (direction > 0 && to == tiles.indexOf(now)) return;   /* after the last: nothing */
	snurra_tile *next = tiles.at(to);
	seek_group(next, next->offset_us());
}

void snurra_node::start_group_at(int64_t time, bool moving) {
	m_stopped = false;
	const qint64 now = snurra_clock_us();
	const qint64 at = now + m_engine->latency_us() + 50000;
	/* Playing, `time` is the group's time now, and the switch is heard at
	 * `at`: the old place plays on until then, so the new one is taken as
	 * far on. Else a jump of 4 bars landed short by the output's latency
	 * and 50 ms, off the beat -- heard as a skip (the holder, 2026-11-13:
	 * "the jump 4 bars isn't transparent"). */
	if (moving) time += snurra_us_to_frames(at - now, m_engine->rate());
	m_following.clear();   /* every lane starts afresh: no train is mid-way */
	for (int row : rows()) {
		int64_t frame = 0;
		int64_t wait = 0;
		snurra_tile *chosen = lane_at(row, time, frame, &wait);
		for (snurra_tile *tile : lane(row)) {
			if (tile == chosen) tile->play_from(frame, at + snurra_frames_to_us(wait, m_engine->rate()));
			else if (tile->playing()) tile->set_playing(false);
		}
	}
}

/*
 * A lane moved along the timeline (project.md sec 3b): the waveform of any
 * of its tiles dragged with Ctrl, the lane's begin following the pointer
 * -- never before the timeline's start -- and said on its first tile. The
 * begin snaps so the lane's first beat falls on a beat of another lane
 * playing there (snapped_begin); Shift held, it goes where it is put.
 * Released while the group plays, the group is started again where it
 * was, so the moved lane is heard where it now is.
 */
void snurra_node::shift_lane(snurra_tile *tile, qint64 delta_us, bool done, bool snap) {
	const QList<snurra_tile *> tiles = lane(tile->row());
	if (tiles.isEmpty()) return;
	if (m_shift_row != tile->row()) {
		m_shift_row = tile->row();
		m_shift_from_us = tiles.first()->lane_start_us();
	}
	int64_t begin = std::max<int64_t>(0, snurra_us_to_frames(m_shift_from_us + delta_us, m_engine->rate()));
	if (snap) begin = snapped_begin(tile->row(), begin);
	tiles.first()->set_lane_start_us(qint64(snurra_frames_to_us(begin, m_engine->rate())));
	if (!done) return;
	m_shift_row = -1;
	/* The time now, from a tile sounding in another lane if one is: the
	 * moved lane's own tiles no longer say it. */
	snurra_tile *clock = nullptr;
	for (snurra_tile *other : m_tiles)
		if (other->playing() && (!clock || clock->row() == tile->row())) clock = other;
	if (clock) start_group_at(group_time(clock), true);
}

/*
 * Where lane `row` begins if it is to come in on the beat: its first
 * tile's first beat, at its tempo and offset, moved onto the nearest beat
 * of the tile another lane plays at that moment -- the first such lane
 * with a beat. By less than half a beat; never before the timeline's
 * start. Unchanged where either has no beat.
 */
int64_t snurra_node::snapped_begin(int row, int64_t begin) const {
	const QList<snurra_tile *> tiles = lane(row);
	if (tiles.isEmpty()) return begin;
	snurra_tile *mine = tiles.first();
	const snurra_beat_grid grid = mine->beats();
	if (!grid.valid()) return begin;
	const double tempo = mine->deck()->tempo();
	const double offset = double(snurra_us_to_frames(mine->offset_us(), m_engine->rate()));
	const double beat = double(begin) + (grid.first - offset) / tempo;   /* the first beat, on the timeline */
	for (int other : rows()) {
		if (other == row) continue;
		int64_t frame = 0;
		int64_t wait = 0;
		snurra_tile *theirs = lane_at(other, int64_t(beat), frame, &wait);
		if (!theirs || wait > 0 || !theirs->beats().valid()) continue;
		const snurra_beat_grid their = theirs->beats();
		const double their_tempo = theirs->deck()->tempo();
		const double their_offset = double(snurra_us_to_frames(theirs->offset_us(), m_engine->rate()));
		/* Their beats on the timeline: lane_start + (track frame - offset) / tempo. */
		const double origin = double(lane_start(theirs)) + (their.first - their_offset) / their_tempo;
		const double period = their.period / their_tempo;
		const double nearest = origin + std::round((beat - origin) / period) * period;
		double snapped = double(begin) + (nearest - beat);
		while (snapped < 0) snapped += period;
		return std::llround(snapped);
	}
	return begin;
}

/* The group's time where `tile` is now: its place in its lane, less how
 * far a nudge put its track ahead of the lane. */
int64_t snurra_node::time_of(snurra_tile *tile) const {
	return lane_start(tile)
	       + std::llround(double(tile->frame_now() - snurra_us_to_frames(tile->offset_us(), m_engine->rate())) / tile->deck()->tempo());
}

/*
 * A nudge (project.md sec 4): this tile's track moved by `delta_us` against
 * its lane's time and every other lane, and remembered as its offset, so a
 * group play or seek keeps the lanes as lined up by ear. Playing, it is a
 * bend, a turntable's: the track a tenth faster or slower until it has
 * moved, with no gap -- a seek, as it first was, left a silence the length
 * of the output's latency at every press.
 */
void snurra_node::nudge_lane(snurra_tile *tile, qint64 delta_us) {
	tile->set_offset_us(tile->offset_us() + delta_us);
	const int64_t frame = std::max<int64_t>(0, tile->frame_now() + snurra_us_to_frames(delta_us, m_engine->rate()));
	if (tile->playing()) tile->deck()->bend(snurra_us_to_frames(delta_us, m_engine->rate()));
	else tile->hold_at(frame);
}

bool snurra_node::sync(snurra_tile *tile) {
	const snurra_beat_grid mine = tile->beats();
	if (!mine.valid()) return false;
	snurra_tile *leader = nullptr;
	for (snurra_tile *other : m_tiles)
		if (other != tile && other->playing() && other->beats().valid()) {
			leader = other;
			break;
		}
	if (!leader) return false;
	/* The phase bent out as a nudge, kept as the lane's offset too. */
	const qint64 delta_us = tile->match_beat_of(leader);
	if (delta_us != 0) nudge_lane(tile, delta_us);
	return true;
}

bool snurra_node::sync_key(snurra_tile *tile) {
	for (snurra_tile *other : m_tiles)
		if (other != tile && other->playing() && other->key_heard() >= 0) return tile->match_key_of(other);
	return false;
}

void snurra_node::leave_if_empty() {
	/* Not while a given tile's fade is still to be heard. */
	if (!m_tiles.isEmpty() || !m_giving.isEmpty() || !m_adopt_token.isEmpty() || m_engine->busy()) return;
	hide();
	if (m_stays_open) return;
	/* After the event loop has written what is queued: a COMMIT must
	 * reach the taker before this process goes. */
	QTimer::singleShot(0, this, [] { QCoreApplication::quit(); });
}

/* ---- giving ------------------------------------------------------------ */

void snurra_node::offer(QLocalSocket *socket, const QString &token, snurra_wire::side_t side,
                        const QList<snurra_tile *> &tiles) {
	snurra_wire::message m;
	m.type = snurra_wire::OFFER;
	m.token = token;
	m.side = side;
	pending_give give;
	give.socket = socket;
	for (snurra_tile *tile : tiles) {
		m.states << tile->state();
		give.tiles << tile;
	}
	give.expiry = new QTimer(this);
	give.expiry->setSingleShot(true);
	connect(give.expiry, &QTimer::timeout, this, [this, token] { abort_give(token); });
	give.expiry->start(READY_WAIT_MS);
	m_giving.insert(token, give);
	send(socket, m);
}

void snurra_node::merge_into(const QString &peer, snurra_wire::side_t side, int index) {
	if (m_tiles.isEmpty() || index >= m_tiles.size()) return;
	QLocalSocket *socket = new QLocalSocket(this);
	watch(socket);
	const QString token = new_token();
	const QList<snurra_tile *> tiles = index < 0 ? m_tiles : QList<snurra_tile *>{ m_tiles.at(index) };
	connect(socket, &QLocalSocket::connected, this, [this, socket, token, side, tiles] {
		offer(socket, token, side, tiles);
	});
	connect(socket, &QLocalSocket::errorOccurred, this, [socket] { socket->deleteLater(); });
	socket->connectToServer(peer);
}

void snurra_node::complete_give(QLocalSocket *socket, const snurra_wire::message &ready) {
	const auto it = m_giving.find(ready.token);
	if (it == m_giving.end()) return;
	const pending_give give = it.value();
	m_giving.erase(it);
	if (give.expiry) give.expiry->deleteLater();

	/*
	 * The taker is playing, silently, in step. The crossfade starts at the
	 * later of when it can fade in and when this side can still change
	 * what it plays -- what is rendered now is heard a latency from now --
	 * with slack for the COMMIT to arrive first. The tiles leave the window
	 * at once; their decks sound on through the fade, and then retire.
	 */
	const qint64 period_us = qint64(m_engine->period()) * 1000000 / m_engine->rate();
	const qint64 start = qMax(ready.time, snurra_clock_us() + m_engine->latency_us() + period_us + COMMIT_MARGIN_US);
	for (const QPointer<snurra_tile> &tile : give.tiles) {
		if (!tile) continue;
		if (tile->playing()) {
			tile->deck()->fade(start, CROSSFADE_US, 1, 0);
			m_engine->retire(tile->deck(), start + CROSSFADE_US + period_us);
		} else {
			m_engine->retire(tile->deck());
		}
		drop_tile(tile);
	}
	snurra_wire::message commit;
	commit.type = snurra_wire::COMMIT;
	commit.token = ready.token;
	commit.time = start;
	commit.fade = CROSSFADE_US;
	send(socket, commit);
	if (socket) socket->flush();
	leave_if_empty();
}

void snurra_node::abort_give(const QString &token) {
	const auto it = m_giving.find(token);
	if (it == m_giving.end()) return;
	const pending_give give = it.value();
	m_giving.erase(it);
	if (give.expiry) give.expiry->deleteLater();
	snurra_wire::message abort;
	abort.type = snurra_wire::ABORT;
	abort.token = token;
	send(give.socket, abort);
	/* Nothing else: the tiles never stopped. */
}

void snurra_node::release(int index, const QPoint &at) {
	if (index < 0 || index >= m_tiles.size() || m_tiles.size() < 2) return;
	/*
	 * A new process takes the tile: it starts, connects back and asks for
	 * it by the token (ADOPT); the tile plays on here until it says READY.
	 * Starting a process takes a moment, which is why the wait is longer.
	 */
	const QString token = new_token();
	pending_give placeholder;
	placeholder.tiles << m_tiles.at(index);
	placeholder.expiry = new QTimer(this);
	placeholder.expiry->setSingleShot(true);
	connect(placeholder.expiry, &QTimer::timeout, this, [this, token] { abort_give(token); });
	placeholder.expiry->start(ADOPT_WAIT_MS);
	m_giving.insert(token, placeholder);
	QProcess::startDetached(QCoreApplication::applicationFilePath(),
	                        { QStringLiteral("--adopt"), m_path, QStringLiteral("--token"), token,
	                          QStringLiteral("--index"), QString::number(index),
	                          QStringLiteral("--at"), QStringLiteral("%1,%2").arg(at.x()).arg(at.y()) });
}

/* ---- taking ------------------------------------------------------------ */

void snurra_node::adopt(const QString &from, const QString &token, int index, const QPoint &at) {
	m_adopt_token = token;
	m_adopt_at = at;
	QLocalSocket *socket = new QLocalSocket(this);
	watch(socket);
	connect(socket, &QLocalSocket::connected, this, [this, socket, token, index] {
		snurra_wire::message m;
		m.type = snurra_wire::ADOPT;
		m.token = token;
		m.index = index;
		send(socket, m);
	});
	connect(socket, &QLocalSocket::errorOccurred, this, [] { QCoreApplication::quit(); });
	socket->connectToServer(from);
	/* Never a process with nothing in it for long: a giver that has gone
	 * or refused leaves this one to end. */
	QTimer::singleShot(ADOPT_WAIT_MS + 2000, this, [this] {
		if (m_tiles.isEmpty()) QCoreApplication::quit();
	});
}

void snurra_node::take(QLocalSocket *socket, const snurra_wire::message &m) {
	/*
	 * Placed after what is here: a train to the right of the last column,
	 * a stack below the last row, keeping the giver's own arrangement. A
	 * process with nothing yet takes them as they were.
	 */
	int last_row = -1;
	int last_column = -1;
	for (snurra_tile *tile : m_tiles) {
		last_row = qMax(last_row, tile->row());
		last_column = qMax(last_column, tile->column());
	}
	const qint64 now = snurra_clock_us();
	pending_take pending;
	pending.socket = socket;
	pending.since = now;
	setUpdatesEnabled(false);
	for (snurra_tile_state state : m.states) {
		/* Anchored on the giver's own sample, as it came: re-sampled here
		 * it would be rounded to a frame once more. A playing tile's deck
		 * is held silent until the crossfade. */
		if (!m_tiles.isEmpty()) {
			if (m.side == snurra_wire::RIGHT) state.column += last_column + 1;
			else state.row += last_row + 1;
		}
		pending.tiles << add_tile(state, state.playing);
	}
	setUpdatesEnabled(true);

	if (!m_adopt_token.isEmpty() && m.token == m_adopt_token) {
		m_adopt_token.clear();
		if (!m_adopt_at.isNull() && can_place_windows()) move(m_adopt_at);
	}
	show();

	/* READY once every deck plays in step with the giver, which takes a
	 * decode or two; checked often, so the giver waits no longer. */
	const QString token = m.token;
	pending.timer = new QTimer(this);
	pending.timer->setInterval(2);
	connect(pending.timer, &QTimer::timeout, this, [this, token] { check_ready(token); });
	pending.timer->start();
	m_taking.insert(token, pending);
	/* A giver gone without a word: what was taken fades in on its own. */
	if (socket)
		connect(socket, &QLocalSocket::disconnected, this,
		        [this, token] { fade_in(token, snurra_clock_us(), CROSSFADE_US); });
	check_ready(token);
}

void snurra_node::check_ready(const QString &token) {
	auto it = m_taking.find(token);
	if (it == m_taking.end() || it->ready) return;
	bool in_step = true;
	for (const QPointer<snurra_tile> &tile : it->tiles)
		if (tile && !tile->deck()->locked()) in_step = false;
	if (!in_step && snurra_clock_us() - it->since < qint64(IN_STEP_WAIT_MS) * 1000) return;

	it->ready = true;
	/* Now a wait for the COMMIT, which a giver sends at once. */
	it->timer->stop();
	disconnect(it->timer, nullptr, this, nullptr);
	connect(it->timer, &QTimer::timeout, this, [this, token] { fade_in(token, snurra_clock_us(), CROSSFADE_US); });
	it->timer->setSingleShot(true);
	it->timer->start(COMMIT_WAIT_MS);

	/* The earliest this side can begin to sound: what it renders now is
	 * heard a latency from now. */
	const qint64 period_us = qint64(m_engine->period()) * 1000000 / m_engine->rate();
	snurra_wire::message ready;
	ready.type = snurra_wire::READY;
	ready.token = token;
	ready.time = snurra_clock_us() + m_engine->latency_us() + period_us + COMMIT_MARGIN_US;
	send(it->socket, ready);
}

void snurra_node::fade_in(const QString &token, qint64 start_us, qint64 length_us) {
	const auto it = m_taking.constFind(token);
	if (it == m_taking.constEnd()) return;
	for (const QPointer<snurra_tile> &tile : it->tiles)
		if (tile) tile->deck()->fade(start_us, length_us, 0, 1);
	m_last_started = start_us;
	m_last_stopped = start_us + length_us;
	end_take(token);
}

void snurra_node::end_take(const QString &token) {
	const pending_take pending = m_taking.take(token);
	if (pending.timer) pending.timer->deleteLater();
}

/* ---- the socket -------------------------------------------------------- */

void snurra_node::handle(QLocalSocket *socket, const snurra_wire::message &m) {
	switch (m.type) {
	case snurra_wire::WHERE: {
		snurra_wire::message here;
		here.type = snurra_wire::HERE;
		here.frame = isVisible() ? frameGeometry() : QRect();
		here.path = m_path;
		send(socket, here);
		break;
	}
	case snurra_wire::HERE:
		if (!m.path.isEmpty()) m_peer_frames.insert(m.path, m.frame);
		socket->disconnectFromServer();
		break;
	case snurra_wire::OFFER:
		take(socket, m);
		break;
	case snurra_wire::READY:
		complete_give(socket, m);
		break;
	case snurra_wire::COMMIT:
		fade_in(m.token, m.time, m.fade);
		break;
	case snurra_wire::ABORT: {
		/* The giver plays on: what was taken goes, never having sounded. */
		const QList<QPointer<snurra_tile>> taken = m_taking.value(m.token).tiles;
		end_take(m.token);
		for (const QPointer<snurra_tile> &tile : taken)
			if (tile) remove_tile(tile);
		leave_if_empty();
		break;
	}
	case snurra_wire::ADOPT: {
		const auto it = m_giving.find(m.token);
		if (it == m_giving.end() || it->tiles.isEmpty() || !it->tiles.first()) break;
		/* The placeholder release() left becomes the real offer. */
		snurra_tile *tile = it->tiles.first();
		if (it->expiry) it->expiry->deleteLater();
		m_giving.erase(it);
		offer(socket, m.token, snurra_wire::RIGHT, { tile });
		break;
	}
	case snurra_wire::MERGE_INTO:
		merge_into(m.path, m.side, m.index);
		break;
	case snurra_wire::RELEASE:
		release(m.index, m.frame.topLeft());
		break;
	case snurra_wire::SHOW_CONTROLS:
		if (m.index >= 0 && m.index < m_tiles.size()) m_tiles.at(m.index)->set_controls(m.time != 0);
		break;
	case snurra_wire::PLAY:
		if (m.index >= 0 && m.index < m_tiles.size()) m_tiles.at(m.index)->request_playing(m.time != 0);
		break;
	case snurra_wire::SEEK:
		if (m.index >= 0 && m.index < m_tiles.size()) seek_group(m_tiles.at(m.index), m.time);
		break;
	case snurra_wire::STATUS: {
		snurra_wire::message status;
		status.type = snurra_wire::STATUS_IS;
		for (snurra_tile *tile : m_tiles) status.states << tile->state();
		status.started = m_last_started;
		status.stopped = m_last_stopped;
		status.path = m_path;
		send(socket, status);
		break;
	}
	default:
		break;
	}
}

/* ---- dragging ---------------------------------------------------------- */

void snurra_node::ask_where() {
	/* Asked as the drag starts, answered while it goes on: by the release
	 * every peer's frame is here, and nothing waited for it. */
	m_peer_frames.clear();
	for (const QString &peer : snurra_wire::peer_sockets(m_path)) {
		QLocalSocket *socket = new QLocalSocket(this);
		watch(socket);
		connect(socket, &QLocalSocket::connected, this, [this, socket] {
			snurra_wire::message where;
			where.type = snurra_wire::WHERE;
			send(socket, where);
		});
		connect(socket, &QLocalSocket::errorOccurred, socket, &QObject::deleteLater);
		socket->connectToServer(peer);
	}
}

void snurra_node::mark(const QRect &landing, const QRect &shape) {
	/* Only where a program may place a window of its own; on Wayland the
	 * compositor's drag shows the window itself and nothing more. */
	if (!can_place_windows()) return;
	if (!m_marker && landing.isEmpty()) return;
	if (!m_marker) m_marker = new snurra_marker;
	m_marker->show_at(landing, shape);
}

void snurra_node::handle_moved(snurra_tile *tile, const QPoint &global) {
	if (tiles_join_by_drop()) {
		drag_out(tile);
		return;
	}
	if (m_drag_from.isNull()) {
		m_drag_from = global;
		m_window_from = pos();
		m_system_move_started = false;
		ask_where();
		return;
	}
	/*
	 * In a group a tile is being pulled out: outside the window, the
	 * marker is where its new window opens -- release() places it there.
	 */
	if (m_tiles.size() > 1) {
		if (frameGeometry().contains(global)) {
			mark(QRect(), QRect());
			snurra_tile *over = tile_at(mapFromGlobal(global), false);
			hint_drop(over && over != tile, snurra_wire::RIGHT, over);
		} else {
			hint_drop(false);
			mark(QRect(global, tile->size()), QRect());
		}
		return;
	}
	/* A lone tile: the window moves with the hand, and the marker says
	 * where it would join, and the shape the group would take. */
	if (can_place_windows()) {
		move(m_window_from + (global - m_drag_from));
		const snurra_snap snap = snurra_find_snap(frameGeometry(), m_peer_frames);
		mark(snap.landing, snap.shape);
	} else if (!m_system_move_started && windowHandle()) {
		m_system_move_started = windowHandle()->startSystemMove();
	}
}

/*
 * The tile as a drag and drop: dropped on another snurra, that one asks
 * this one to hand it over (dropEvent below), so the handover is the same
 * as a snap's. Dropped on nothing, a tile in a group is pulled out -- where
 * the pointer is on X11, wherever the compositor puts it on Wayland -- and
 * a lone tile stays: its window moves by its frame.
 */
void snurra_node::drag_out(snurra_tile *tile) {
	const int index = int(m_tiles.indexOf(tile));
	if (index < 0) return;
	QMimeData *mime = new QMimeData;
	mime->setData(QLatin1String(TILE_MIME), (m_path + QLatin1Char('\n') + QString::number(index)).toUtf8());
	QDrag *drag = new QDrag(tile);
	drag->setMimeData(mime);
	const QPixmap look = tile->grab();
	drag->setPixmap(look.scaledToWidth(qMin(look.width(), 160)));
	const Qt::DropAction done = drag->exec(Qt::MoveAction);
	if (done == Qt::IgnoreAction && m_tiles.size() > 1 && m_tiles.contains(tile))
		release(int(m_tiles.indexOf(tile)), can_place_windows() ? QCursor::pos() : QPoint());
}

snurra_wire::side_t snurra_node::side_at(const QPoint &local) const {
	/* The nearer of the right edge and the bottom, as a share of each. */
	const double right = double(width() - local.x()) / qMax(1, width());
	const double below = double(height() - local.y()) / qMax(1, height());
	return right <= below ? snurra_wire::RIGHT : snurra_wire::BELOW;
}

void snurra_node::hint_drop(bool show, snurra_wire::side_t side, snurra_tile *over) {
	if (!show) {
		m_drop_hint->hide();
		reshape();
		return;
	}
	/* Over a tile, an outline round it: that one is what goes. */
	if (over) {
		m_drop_hint->setStyleSheet(QStringLiteral("background: transparent; border: 3px solid palette(highlight);"));
		m_drop_hint->setGeometry(over->geometry());
		m_drop_hint->raise();
		m_drop_hint->show();
		reshape();
		return;
	}
	m_drop_hint->setStyleSheet(QStringLiteral("background: palette(highlight);"));
	const int thick = qMax(4, fontMetrics().height() / 2);
	m_drop_hint->setGeometry(side == snurra_wire::RIGHT ? QRect(width() - thick, 0, thick, height())
	                                                    : QRect(0, height() - thick, width(), thick));
	m_drop_hint->raise();
	m_drop_hint->show();
	reshape();
}

/* The index of one of this process's own tiles in a drag of it, or -1. */
static int own_tile(const QMimeData *mime, const QString &own) {
	if (!mime || !mime->hasFormat(QLatin1String(TILE_MIME))) return -1;
	const QString data = QString::fromUtf8(mime->data(QLatin1String(TILE_MIME)));
	return data.section(QLatin1Char('\n'), 0, 0) == own ? data.section(QLatin1Char('\n'), 1, 1).toInt() : -1;
}

/* A drag of one of this process's own tiles is not a join. */
static bool from_a_peer(const QMimeData *mime, const QString &own) {
	if (!mime || !mime->hasFormat(QLatin1String(TILE_MIME))) return false;
	const QString path = QString::fromUtf8(mime->data(QLatin1String(TILE_MIME))).section(QLatin1Char('\n'), 0, 0);
	return !path.isEmpty() && path != own;
}

/*
 * Where a drop lands (the holder, 2026-11-13): along the right edge or the
 * bottom, a strip a fifth of the window deep (at least two lines), it adds
 * -- a train, a stack; anywhere else on a tile, it is that tile's: files
 * replace it.
 */
snurra_tile *snurra_node::tile_at(const QPoint &local, bool strips) const {
	const int strip_x = qMax(fontMetrics().height() * 2, width() / 5);
	const int strip_y = qMax(fontMetrics().height() * 2, height() / 5);
	if (strips && (local.x() >= width() - strip_x || local.y() >= height() - strip_y)) return nullptr;
	for (snurra_tile *tile : m_tiles)
		if (tile->isVisible() && tile->geometry().contains(local)) return tile;
	return nullptr;
}

void snurra_node::dragEnterEvent(QDragEnterEvent *event) {
	if (own_tile(event->mimeData(), m_path) >= 0) {
		event->acceptProposedAction();
		hint_drop(false);
		return;
	}
	if (!from_a_peer(event->mimeData(), m_path) && !snurra_drop_offers(event->mimeData())) return;
	event->acceptProposedAction();
	const QPoint at = event->position().toPoint();
	hint_drop(true, side_at(at), from_a_peer(event->mimeData(), m_path) ? nullptr : tile_at(at));
}

void snurra_node::dragMoveEvent(QDragMoveEvent *event) {
	/* One of this window's own tiles: over another, the two trade places. */
	const int own = own_tile(event->mimeData(), m_path);
	if (own >= 0) {
		snurra_tile *over = tile_at(event->position().toPoint(), false);
		if (over && own < m_tiles.size() && over != m_tiles.at(own)) {
			event->acceptProposedAction();
			hint_drop(true, snurra_wire::RIGHT, over);
		} else {
			event->ignore();
			hint_drop(false);
		}
		return;
	}
	if (!from_a_peer(event->mimeData(), m_path) && !snurra_drop_offers(event->mimeData())) return;
	event->acceptProposedAction();
	const QPoint at = event->position().toPoint();
	hint_drop(true, side_at(at), from_a_peer(event->mimeData(), m_path) ? nullptr : tile_at(at));
}

void snurra_node::dragLeaveEvent(QDragLeaveEvent *) {
	hint_drop(false);
}

void snurra_node::dropEvent(QDropEvent *event) {
	hint_drop(false);
	const int own = own_tile(event->mimeData(), m_path);
	if (own >= 0) {
		snurra_tile *over = tile_at(event->position().toPoint(), false);
		if (!over || own >= m_tiles.size() || over == m_tiles.at(own)) return;
		swap_tiles(m_tiles.at(own), over);
		event->acceptProposedAction();
		return;
	}
	if (!from_a_peer(event->mimeData(), m_path)) {
		QStringList files = snurra_dropped(event->mimeData());
		if (files.isEmpty()) return;
		if (snurra_tile *over = tile_at(event->position().toPoint())) {
			/* The first in its place; any more after the train. */
			replace_tile(over, files.takeFirst());
			if (!files.isEmpty()) add_files(files, snurra_wire::RIGHT);
		} else {
			add_files(files, side_at(event->position().toPoint()));
		}
		event->acceptProposedAction();
		return;
	}
	const QString data = QString::fromUtf8(event->mimeData()->data(QLatin1String(TILE_MIME)));
	snurra_wire::message merge;
	merge.type = snurra_wire::MERGE_INTO;
	merge.path = m_path;
	merge.side = side_at(event->position().toPoint());
	merge.index = data.section(QLatin1Char('\n'), 1, 1).toInt();
	/* The giver is asked, as a snap asks it: the handover is its to make. */
	QLocalSocket *socket = new QLocalSocket(this);
	connect(socket, &QLocalSocket::connected, this, [this, socket, merge] {
		send(socket, merge);
		socket->disconnectFromServer();
	});
	connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
	connect(socket, &QLocalSocket::errorOccurred, socket, &QObject::deleteLater);
	socket->connectToServer(data.section(QLatin1Char('\n'), 0, 0));
	event->acceptProposedAction();
}

/*
 * Files dropped from a file manager (project.md sec 3b): a tile each, where
 * the drop's side says -- to the right, the first lane's train goes on;
 * below, a new lane, a stack. Paused and their controls in, as a later
 * file of a launch opens (sec 3c). A link stays a URL.
 */
void snurra_node::add_files(const QStringList &files, snurra_wire::side_t side) {
	int last_row = -1;
	int last_column = -1;
	for (snurra_tile *tile : m_tiles) {
		last_row = qMax(last_row, tile->row());
		if (tile->row() == 0) last_column = qMax(last_column, tile->column());
	}
	int row = side == snurra_wire::BELOW ? last_row + 1 : 0;
	int column = side == snurra_wire::BELOW ? 0 : last_column + 1;
	for (const QString &file : files) {
		snurra_tile_state state;
		state.source = snurra_source_of(file);
		state.duration_us = 180000000;
		state.sampled_at_us = snurra_clock_us();
		state.controls = false;
		state.playing = false;
		state.row = row;
		state.column = column++;
		add_tile(state);
	}
}

/*
 * A file dropped on a tile takes its place: the same row and column, level,
 * controls and lane begin; its own track from the start, at tempo 1, with
 * no envelope, nudge or sound track of the last one's; playing if that
 * one was.
 */
void snurra_node::replace_tile(snurra_tile *tile, const QString &file) {
	snurra_tile_state state = tile->state();
	state.source = snurra_source_of(file);
	state.duration_us = 180000000;
	state.position_us = 0;
	state.sampled_at_us = snurra_clock_us();
	state.envelope.clear();
	state.offset_us = 0;
	state.tempo = 1;
	state.audio_track = -1;
	const bool current = m_current == tile;
	/* In first, then the old one out: the other way the grid closes up
	 * the gap and renumbers before the new one takes its place. */
	snurra_tile *taken = add_tile(state);
	remove_tile(tile);
	if (current) m_current = taken;
}

/*
 * Two tiles trade places (the holder, 2026-11-13: one dragged directly on
 * top of another "they should switch places"). A lane's begin stays with
 * the lane: the places' begins are traded back, so a late lane still
 * starts where it did with whatever now heads it.
 */
void snurra_node::swap_tiles(snurra_tile *a, snurra_tile *b) {
	const int a_row = a->row();
	const int a_column = a->column();
	const qint64 a_begin = a->lane_start_us();
	a->set_place(b->row(), b->column());
	a->set_lane_start_us(b->lane_start_us());
	b->set_place(a_row, a_column);
	b->set_lane_start_us(a_begin);
	place_tiles();
}

void snurra_node::play_file(const QString &path) {
	if (m_tiles.size() > 1) {
		add_files({ path }, snurra_wire::RIGHT);
		return;
	}
	for (snurra_tile *tile : QList<snurra_tile *>(m_tiles)) remove_tile(tile);
	snurra_tile_state state;
	state.source = path;
	state.duration_us = 180000000;
	state.sampled_at_us = snurra_clock_us();
	state.controls = true;
	state.playing = true;
	add_tile(state);
}

void snurra_node::set_stays_open(bool stays) {
	m_stays_open = stays;
	if (!stays) leave_if_empty();
}

void snurra_node::mousePressEvent(QMouseEvent *event) {
	/* The bare frame beside the tiles moves the window -- on Wayland the
	 * only way to, the tiles' titles being drags of their own there. */
	if (event->button() == Qt::LeftButton && windowHandle()) windowHandle()->startSystemMove();
}

/*
 * mplayer's keys, since snurra is to replace it (project.md sec 1): space
 * or p plays and pauses, the arrows jump -- left and right the track's
 * small step, down and up its large one (sec 4), Ctrl a nudge -- page up
 * and down ten minutes, 9 and 0 (and / and *) take the level down and up by
 * mplayer's 3, m mutes, ] [ } { and backspace set the tempo as
 * mplayer's speed, f the picture full screen, . a frame on, paused, + and
 * - the sound's delay against the picture, < and > the train's previous
 * and next tile, ! and @ the previous and next chapter, o the time over the picture, q and escape quit. On the tile last touched, or the first with its controls
 * out, or the first. A tile played from the keyboard has its controls
 * pulled out first: what plays is what can be stopped (sec 3c).
 */
void snurra_node::keyPressEvent(QKeyEvent *event) {
	snurra_tile *tile = m_current && m_tiles.contains(m_current) ? m_current.data() : nullptr;
	for (snurra_tile *candidate : m_tiles)
		if (!tile && candidate->controls()) tile = candidate;
	if (!tile && !m_tiles.isEmpty()) tile = m_tiles.first();
	if (!tile) {
		QWidget::keyPressEvent(event);
		return;
	}
	constexpr qint64 SECOND = 1000000;
	switch (event->key()) {
	case Qt::Key_Space:
	case Qt::Key_P:
		if (!tile->playing() && !tile->controls()) tile->set_controls(true);
		tile->request_playing(!tile->playing());
		break;
	/* mplayer's two levels, at the track's step (sec 4): the arrows left
	 * and right the small, down and up the large; with Ctrl, a nudge. */
	case Qt::Key_Left:
	case Qt::Key_Right: {
		const int direction = event->key() == Qt::Key_Right ? 1 : -1;
		if (event->modifiers() & Qt::ControlModifier) tile->nudge(direction);
		else tile->jump(direction, false);
		break;
	}
	case Qt::Key_Down: tile->jump(-1, true); break;
	case Qt::Key_Up: tile->jump(1, true); break;
	case Qt::Key_PageDown: tile->seek_by(-600 * SECOND); break;
	case Qt::Key_PageUp: tile->seek_by(600 * SECOND); break;
	case Qt::Key_9:
	case Qt::Key_Slash: tile->set_level(tile->level() - 3); break;
	case Qt::Key_0:
	case Qt::Key_Asterisk: tile->set_level(tile->level() + 3); break;
	case Qt::Key_M: tile->toggle_mute(); break;
	/* mplayer's speed keys, on the tempo (sec 4): ] and [ by its 10 %,
	 * one the other's inverse; } and { double and halve; backspace back
	 * to as recorded. */
	case Qt::Key_BracketRight: tile->set_tempo(tile->tempo() * 1.1f); break;
	case Qt::Key_BracketLeft: tile->set_tempo(tile->tempo() / 1.1f); break;
	case Qt::Key_BraceRight: tile->set_tempo(tile->tempo() * 2); break;
	case Qt::Key_BraceLeft: tile->set_tempo(tile->tempo() / 2); break;
	case Qt::Key_Backspace: tile->set_tempo(1); break;
	/* mplayer's f: the picture full screen, or back. */
	case Qt::Key_F: tile->toggle_full_screen(); break;
	case Qt::Key_V: tile->toggle_subtitles(); break;
	case Qt::Key_J: tile->next_subtitles(); break;
	case Qt::Key_Z: tile->delay_subtitles(-100000); break;
	case Qt::Key_X: tile->delay_subtitles(100000); break;
	case Qt::Key_NumberSign: tile->next_audio_track(); break;
	case Qt::Key_Period: tile->step_frame(); break;
	case Qt::Key_O: tile->toggle_clock(); break;
	case Qt::Key_Exclam: tile->step_chapter(-1); break;
	case Qt::Key_At: tile->step_chapter(1); break;
	case Qt::Key_Less: step_train(tile, -1); break;
	case Qt::Key_Greater: step_train(tile, 1); break;
	case Qt::Key_Plus: tile->delay_sound(100000); break;
	case Qt::Key_Minus: tile->delay_sound(-100000); break;
	case Qt::Key_Q:
	case Qt::Key_Escape: {
		const QList<snurra_tile *> all = m_tiles;
		for (snurra_tile *closing : all) remove_tile(closing);
		leave_if_empty();
		break;
	}
	default:
		QWidget::keyPressEvent(event);
		return;
	}
	event->accept();
}

void snurra_node::handle_released(snurra_tile *tile, const QPoint &global) {
	const QPoint from = m_drag_from;
	m_drag_from = QPoint();
	mark(QRect(), QRect());
	hint_drop(false);
	if (from.isNull()) return;

	/* In a group, a tile dropped outside the window is pulled out there;
	 * on another of its tiles, the two trade places. */
	if (m_tiles.size() > 1) {
		if (!frameGeometry().contains(global)) release(int(m_tiles.indexOf(tile)), global);
		else if (snurra_tile *over = tile_at(mapFromGlobal(global), false); over && over != tile) swap_tiles(tile, over);
		return;
	}

	/* A lone tile joins where the marker showed: the same answer. */
	const snurra_snap snap = snurra_find_snap(frameGeometry(), m_peer_frames);
	if (!snap.peer.isEmpty()) merge_into(snap.peer, snap.side);
}
