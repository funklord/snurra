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
static bool look_with_drawers() {
	const QByteArray asked = qgetenv("SNURRA_LAYOUT");
	if (asked == "square") return false;
	if (asked == "drawers") return true;
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

snurra_node::snurra_node(QWidget *parent) : QWidget(parent) {
	m_drawers_look = look_with_drawers();
	if (m_drawers_look) setAttribute(Qt::WA_TranslucentBackground);
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
	QSizeGrip *grip = new QSizeGrip(this);
	outer->addWidget(grip, 0, Qt::AlignRight | Qt::AlignBottom);

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
	connect(tile, &snurra_tile::seek_requested, this, &snurra_node::seek_group);
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
		const QString name = QFileInfo(first->state().source).fileName();
		title = name.isEmpty() ? first->state().source : name;
		if (m_tiles.size() > 1) title += QStringLiteral(" +%1").arg(m_tiles.size() - 1);
	}
	setWindowTitle(title);
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
	if (!m_tiles.removeOne(tile)) return;
	m_grid->removeWidget(tile);
	m_grid->removeWidget(tile->drawer_above());
	m_grid->removeWidget(tile->drawer_below());
	tile->hide();
	if (tile->drawer_above()) tile->drawer_above()->hide();
	if (tile->drawer_below()) tile->drawer_below()->hide();
	tile->deleteLater();
	place_tiles();
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
 * crossfade), to be made a tile's own when envelopes come (sec 3b, step
 * 5).
 */
static int64_t train_overlap(int64_t length, int64_t next_length, int rate) {
	const QByteArray wanted = qgetenv("SNURRA_TRAIN_OVERLAP_MS");
	const qint64 overlap_us = wanted.isEmpty() ? 5000000 : qint64(wanted.toLongLong()) * 1000;
	return std::min({ snurra_us_to_frames(overlap_us, rate), length / 2, next_length / 2 });
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
		const int64_t overlap = train_overlap(int64_t(double(length) / tempo), int64_t(double(next_length) / next->deck()->tempo()), rate);
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

int64_t snurra_node::lane_start(snurra_tile *tile) const {
	const int rate = m_engine->rate();
	const QList<snurra_tile *> tiles = lane(tile->row());
	int64_t start = 0;
	for (int i = 0; i < tiles.size() && tiles.at(i) != tile; ++i) {
		const int64_t length = timeline_length(tiles.at(i));
		const int64_t next = i + 1 < tiles.size() ? timeline_length(tiles.at(i + 1)) : 0;
		start += length - train_overlap(length, next, rate);
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
		for (snurra_tile *tile : m_tiles)
			if (tile->playing()) tile->set_playing(false);
		return;
	}
	start_group_at(time_of(from));
}

QList<int> snurra_node::rows() const {
	QList<int> rows;
	for (snurra_tile *tile : m_tiles)
		if (!rows.contains(tile->row())) rows << tile->row();
	return rows;
}

snurra_tile *snurra_node::lane_at(int row, int64_t time, int64_t &frame) const {
	const QList<snurra_tile *> tiles = lane(row);
	int64_t start = 0;
	for (int i = 0; i < tiles.size(); ++i) {
		const int64_t length = timeline_length(tiles.at(i));
		const int64_t next = i + 1 < tiles.size() ? timeline_length(tiles.at(i + 1)) : 0;
		const int64_t own = length - train_overlap(length, next, m_engine->rate());
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
			start_group_at(time);
			return;
		}
	}
	/* Paused: each lane held where the time falls in it. */
	for (int row : rows()) {
		int64_t frame = 0;
		if (snurra_tile *tile = lane_at(row, time, frame)) tile->hold_at(frame);
	}
}

void snurra_node::start_group_at(int64_t time) {
	const qint64 at = snurra_clock_us() + m_engine->latency_us() + 50000;
	m_following.clear();   /* every lane starts afresh: no train is mid-way */
	for (int row : rows()) {
		int64_t frame = 0;
		snurra_tile *chosen = lane_at(row, time, frame);
		for (snurra_tile *tile : lane(row)) {
			if (tile == chosen) tile->play_from(frame, at);
			else if (tile->playing()) tile->set_playing(false);
		}
	}
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
	const snurra_beat_grid theirs = leader->beats();
	/* The beat heard, per second: a grid's BPM at its tile's tempo. Off by
	 * more than a DJ's range, the double or the half is the beat meant --
	 * 87.5 and 175 are one tempo. */
	const double target = theirs.bpm() * leader->tempo();
	double tempo = target / mine.bpm();
	for (double octave : { 2.0, 0.5 })
		if (std::abs(std::log2(tempo * octave)) < std::abs(std::log2(tempo))) tempo *= octave;
	tile->set_tempo(float(tempo));
	/* Then the phase: where each is within its beat, now, the shorter way
	 * round, bent out as a nudge -- kept as the lane's offset too. */
	auto phase = [](const snurra_beat_grid &grid, int64_t frame) {
		const double beats = (double(frame) - grid.first) / grid.period;
		return beats - std::floor(beats);
	};
	double shift = phase(theirs, leader->frame_now()) - phase(mine, tile->frame_now());
	if (shift > 0.5) shift -= 1;
	if (shift < -0.5) shift += 1;
	const qint64 delta_us = qint64(snurra_frames_to_us(std::llround(shift * mine.period), m_engine->rate()));
	if (delta_us != 0) nudge_lane(tile, delta_us);
	return true;
}

void snurra_node::leave_if_empty() {
	/* Not while a given tile's fade is still to be heard. */
	if (!m_tiles.isEmpty() || !m_giving.isEmpty() || !m_adopt_token.isEmpty() || m_engine->busy()) return;
	hide();
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
		if (frameGeometry().contains(global)) mark(QRect(), QRect());
		else mark(QRect(global, tile->size()), QRect());
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

void snurra_node::hint_drop(bool show, snurra_wire::side_t side) {
	if (!show) {
		m_drop_hint->hide();
		return;
	}
	const int thick = qMax(4, fontMetrics().height() / 2);
	m_drop_hint->setGeometry(side == snurra_wire::RIGHT ? QRect(width() - thick, 0, thick, height())
	                                                    : QRect(0, height() - thick, width(), thick));
	m_drop_hint->raise();
	m_drop_hint->show();
}

/* A drag of one of this process's own tiles is not a join. */
static bool from_a_peer(const QMimeData *mime, const QString &own) {
	if (!mime || !mime->hasFormat(QLatin1String(TILE_MIME))) return false;
	const QString path = QString::fromUtf8(mime->data(QLatin1String(TILE_MIME))).section(QLatin1Char('\n'), 0, 0);
	return !path.isEmpty() && path != own;
}

/* Files a file manager drags: the local ones snurra could open. */
static QStringList files_in(const QMimeData *mime) {
	QStringList files;
	if (!mime || !mime->hasUrls()) return files;
	for (const QUrl &url : mime->urls())
		if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isFile()) files << url.toLocalFile();
	return files;
}

void snurra_node::dragEnterEvent(QDragEnterEvent *event) {
	if (!from_a_peer(event->mimeData(), m_path) && files_in(event->mimeData()).isEmpty()) return;
	event->acceptProposedAction();
	hint_drop(true, side_at(event->position().toPoint()));
}

void snurra_node::dragMoveEvent(QDragMoveEvent *event) {
	if (!from_a_peer(event->mimeData(), m_path) && files_in(event->mimeData()).isEmpty()) return;
	event->acceptProposedAction();
	hint_drop(true, side_at(event->position().toPoint()));
}

void snurra_node::dragLeaveEvent(QDragLeaveEvent *) {
	hint_drop(false);
}

void snurra_node::dropEvent(QDropEvent *event) {
	hint_drop(false);
	const QStringList files = files_in(event->mimeData());
	if (!files.isEmpty() && !from_a_peer(event->mimeData(), m_path)) {
		add_files(files, side_at(event->position().toPoint()));
		event->acceptProposedAction();
		return;
	}
	if (!from_a_peer(event->mimeData(), m_path)) return;
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
 * file of a launch opens (sec 3c).
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
		state.source = QFileInfo(file).absoluteFilePath();
		state.duration_us = 180000000;
		state.sampled_at_us = snurra_clock_us();
		state.controls = false;
		state.playing = false;
		state.row = row;
		state.column = column++;
		add_tile(state);
	}
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
 * mplayer's speed, f the picture full screen, . a frame on, paused, q and
 * escape quit. On the tile last touched, or the first with its controls
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
	if (from.isNull()) return;

	/* In a group, a tile dropped outside the window is pulled out there. */
	if (m_tiles.size() > 1) {
		if (!frameGeometry().contains(global)) release(int(m_tiles.indexOf(tile)), global);
		return;
	}

	/* A lone tile joins where the marker showed: the same answer. */
	const snurra_snap snap = snurra_find_snap(frameGeometry(), m_peer_frames);
	if (!snap.peer.isEmpty()) merge_into(snap.peer, snap.side);
}
