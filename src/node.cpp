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
#include <QPainter>
#include <QVariant>
#include <QPixmap>
#include <QContextMenuEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QStyle>
#include <QToolButton>
#include <QMenu>
#include <QMessageBox>
#include <QScreen>
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
#include "snurra/midi.h"
#include "snurra/snap.h"
#include "snurra/source.h"
#include "snurra/tile.h"

using namespace snurra_wire::timing;
using snurra_wire::TILE_MIME;

static QString &about_text() {
	static QString text;
	return text;
}

void snurra_set_about(const QString &rich_text) {
	about_text() = rich_text;
}

namespace {

QString new_token() {
	return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

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

/*
 * The beats and bars along the top of a window's lanes (the holder,
 * 2026-11-21: "a timeline at the top, below the top bar with
 * beats/bars"): drawn on the lanes' own scale -- each tile's span of the
 * timeline where the tile is -- from the first track's beat at its tempo,
 * a tick a beat, a longer one and its number a bar of four; where no beat
 * is known, seconds. A mark where the window is in its timeline. What it
 * draws it is asked for at each paint (`fill`), so it is never stale.
 */
class snurra_ruler : public QWidget {
public:
	struct segment {
		int64_t from, to;    /* timeline frames */
		double left, right;  /* where, across this widget */
	};
	std::vector<segment> segments;
	double period = 0;   /* timeline frames a beat; 0: no beat */
	double first = 0;    /* a beat's timeline frame: bar 1's first */
	int rate = 48000;
	int64_t now = -1;
	std::function<void(snurra_ruler &)> fill;

	explicit snurra_ruler(QWidget *parent) : QWidget(parent) {
		setObjectName(QStringLiteral("snurra-ruler"));
		QFont small = font();
		small.setPointSizeF(small.pointSizeF() * 0.8);
		setFont(small);
		setFixedHeight(fontMetrics().height() + 4);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		setAttribute(Qt::WA_StyledBackground);
		setStyleSheet(QStringLiteral("#snurra-ruler { background: palette(window); }"));
	}
	/* Across, where timeline frame `at` is; -1 off every tile. */
	double x_of(double at) const {
		for (const segment &one : segments)
			if (at >= double(one.from) && at <= double(one.to) && one.to > one.from)
				return one.left + (at - double(one.from)) / double(one.to - one.from) * (one.right - one.left);
		return -1;
	}

protected:
	void paintEvent(QPaintEvent *event) override {
		QWidget::paintEvent(event);
		segments.clear();
		period = 0;
		now = -1;
		if (fill) fill(*this);
		if (segments.empty()) return;
		int64_t from = segments.front().from, to = segments.front().to;
		for (const segment &one : segments) {
			from = std::min(from, one.from);
			to = std::max(to, one.to);
		}
		QPainter painter(this);
		QColor ink = palette().color(QPalette::WindowText);
		QColor faint = ink;
		faint.setAlphaF(0.45f);
		const int h = height();
		const QFontMetrics metrics = fontMetrics();
		QVariantList bars;
		double free_from = -1e9;
		auto mark = [&](double x, bool long_one, const QString &said) {
			painter.setPen(long_one ? ink : faint);
			painter.drawLine(QPointF(x, long_one ? 0 : h * 0.6), QPointF(x, h));
			if (said.isEmpty() || x < free_from) return;
			painter.drawText(QPointF(x + 2, metrics.ascent() + 1), said);
			free_from = x + metrics.horizontalAdvance(said) + 6;
		};
		if (period > 0) {
			const double pixels = (x_of(double(to)) - x_of(double(from))) / std::max(1.0, double(to - from)) * period;
			const int each = pixels >= 3 ? 1 : 4;   /* beats too close: bars only */
			const int64_t k0 = int64_t(std::ceil((double(from) - first) / period));
			const int64_t k1 = int64_t(std::floor((double(to) - first) / period));
			for (int64_t k = k0; k <= k1 && k - k0 < 20000; ++k) {
				const bool bar = ((k % 4) + 4) % 4 == 0;
				if (!bar && (k % each) != 0) continue;
				const double x = x_of(first + double(k) * period);
				if (x < 0) continue;
				if (bar) bars << x;
				mark(x, bar, bar ? QString::number(k / 4 + (k >= 0 ? 1 : 0)) : QString());
			}
		} else {
			const double pixels = (x_of(double(to)) - x_of(double(from))) / std::max(1.0, double(to - from)) * rate;
			int step = 1;
			for (int s : { 1, 5, 10, 30, 60, 300 })
				if (pixels * s >= 8) {
					step = s;
					break;
				}
			const int said_each = step * (pixels * step >= metrics.horizontalAdvance(QStringLiteral("00:00")) + 6 ? 1 : 6);
			for (int64_t s = 0; s * rate <= to && s < 100000; s += step) {
				const double x = x_of(double(s * rate));
				if (x < 0) continue;
				const bool said = s % said_each == 0;
				mark(x, said, said ? QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0')) : QString());
			}
		}
		setProperty("bars", bars);
		if (now >= 0) {
			const double x = x_of(double(now));
			if (x >= 0) {
				painter.setRenderHint(QPainter::Antialiasing);
				painter.setPen(Qt::NoPen);
				painter.setBrush(palette().color(QPalette::Highlight));
				const double r = h * 0.35;
				painter.drawPolygon(QPolygonF({ QPointF(x - r, h - r * 1.6), QPointF(x + r, h - r * 1.6), QPointF(x, h) }));
			}
		}
	}
};

snurra_node::snurra_node(QWidget *parent) : QWidget(parent) {
	m_drawers_look = look_with_drawers();
	if (m_drawers_look && see_through()) setAttribute(Qt::WA_TranslucentBackground);
	qInfo("snurra: the %s look", m_drawers_look ? "drawers" : "square");
	m_engine = new snurra_engine(this);
	/* The last fade heard out: an empty window goes -- not one following
	 * the decks, empty only until the player it gave them has spread
	 * across them (open_decks). */
	connect(m_engine, &snurra_engine::idle, this, [this] {
		if (!follows_decks()) leave_if_empty();
	});
	setWindowFlag(Qt::FramelessWindowHint);
	setWindowTitle(QStringLiteral("snurra"));
	QVBoxLayout *outer = new QVBoxLayout(this);
	outer->setContentsMargins(2, 2, 2, 2);
	outer->setSpacing(0);
	/*
	 * One bar for the window (the holder, 2026-11-21: "Each player only
	 * has one control panel ... with a single top bar with all the close
	 * minimize buttons etc"): what it plays, the controls' handle, the way
	 * to the decks, minimise and close. Held, it moves the window -- a
	 * lone tile's as that tile, so it snaps to another as a title did
	 * (sec 3d); the tiles themselves are dragged by their waveforms.
	 */
	m_bar = new QWidget(this);
	m_bar->setObjectName(QStringLiteral("snurra-bar"));
	m_bar->setAttribute(Qt::WA_StyledBackground);
	m_bar->setStyleSheet(QStringLiteral(
	        "#snurra-bar { background: palette(window); border: 1px solid palette(mid); border-radius: 3px; }"));
	QHBoxLayout *bar = new QHBoxLayout(m_bar);
	bar->setContentsMargins(6, 1, 2, 1);
	bar->setSpacing(2);
	m_bar_title = new QLabel(QStringLiteral("snurra"), m_bar);
	m_bar_title->setObjectName(QStringLiteral("snurra-bar-title"));
	m_bar_title->setCursor(Qt::OpenHandCursor);
	m_bar_title->setMinimumWidth(1);
	m_bar_title->installEventFilter(this);
	bar->addWidget(m_bar_title, 1);
	m_bar_controls = new QToolButton(m_bar);
	m_bar_controls->setObjectName(QStringLiteral("snurra-bar-controls"));
	m_bar_controls->setAutoRaise(true);
	m_bar_controls->setCheckable(true);
	m_bar_controls->setArrowType(Qt::DownArrow);
	m_bar_controls->setAccessibleName(tr("Pull the controls out"));
	m_bar_controls->setToolTip(m_bar_controls->accessibleName());
	connect(m_bar_controls, &QToolButton::toggled, this, [this](bool out) {
		if (out) {
			if (snurra_tile *tile = panel_tile()) tile->set_controls(true);
		} else {
			for (snurra_tile *tile : m_tiles) tile->set_controls(false);
		}
	});
	bar->addWidget(m_bar_controls);
	m_bar_picture = new QToolButton(m_bar);
	m_bar_picture->setObjectName(QStringLiteral("snurra-bar-picture"));
	m_bar_picture->setAutoRaise(true);
	m_bar_picture->setCheckable(true);
	m_bar_picture->setText(tr("video"));
	m_bar_picture->setAccessibleName(tr("Show the video"));
	m_bar_picture->setToolTip(m_bar_picture->accessibleName());
	m_bar_picture->hide();   /* until a tile has a picture */
	connect(m_bar_picture, &QToolButton::toggled, this, [this](bool open) {
		m_picture_open = open;
		if (snurra_tile *tile = picture_tile()) tile->set_video_open(open);
	});
	bar->addWidget(m_bar_picture);
	QToolButton *decks = new QToolButton(m_bar);
	decks->setObjectName(QStringLiteral("snurra-bar-decks"));
	decks->setAutoRaise(true);
	decks->setText(tr("decks"));
	decks->setAccessibleName(tr("Open in the decks"));
	decks->setToolTip(tr("This player onto a DJ's decks, each track on its deck"));
	connect(decks, &QToolButton::clicked, this, &snurra_node::open_decks);
	bar->addWidget(decks);
	QToolButton *minimise = new QToolButton(m_bar);
	minimise->setAutoRaise(true);
	minimise->setIcon(style()->standardIcon(QStyle::SP_TitleBarMinButton));
	minimise->setAccessibleName(tr("Minimise"));
	connect(minimise, &QToolButton::clicked, this, &QWidget::showMinimized);
	bar->addWidget(minimise);
	QToolButton *close = new QToolButton(m_bar);
	close->setObjectName(QStringLiteral("snurra-bar-close"));
	close->setAutoRaise(true);
	close->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
	close->setAccessibleName(tr("Close"));
	connect(close, &QToolButton::clicked, this, &snurra_node::close_all);
	bar->addWidget(close);
	for (QWidget *child : m_bar->findChildren<QWidget *>()) child->setFocusPolicy(Qt::NoFocus);
	outer->addWidget(m_bar);
	snurra_ruler *bars = new snurra_ruler(this);
	bars->fill = [this](snurra_ruler &made) { fill_ruler(made); };
	m_ruler = bars;
	outer->addWidget(m_ruler);
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
	if (!m_server->listen(m_path)) {
		m_listen_error = QStringLiteral("cannot listen at %1: %2").arg(m_path, m_server->errorString());
		return false;
	}
	connect(m_server, &QLocalServer::newConnection, this, &snurra_node::accept);
	/* Whether a deck view has headphones a tile here could be heard in:
	 * asked now and every two seconds -- not while a drag asks for itself. */
	m_listener_poll = new QTimer(this);
	m_listener_poll->setInterval(2000);
	connect(m_listener_poll, &QTimer::timeout, this, [this] {
		if (m_drag_from.isNull()) ask_where();
	});
	m_listener_poll->start();
	QTimer::singleShot(0, this, [this] { ask_where(); });
	return true;
}

bool snurra_node::take_controllers(const QString &player) {
	if (m_controller) return true;
	m_controller = new snurra_controller(
	        player,
	        [this](const QString &control, snurra_control_kind, const snurra_midi_event &event) {
		        /* Buttons, on their press. */
		        if (event.kind == snurra_midi_event::note_off || (event.kind == snurra_midi_event::cc && event.value < 64)) return;
		        if (m_tiles.isEmpty()) return;
		        bool playing = false;
		        for (snurra_tile *tile : m_tiles) playing = playing || tile->playing();
		        if (control == QLatin1String("play")) play_group(m_tiles.first(), !playing);
		        else if (control == QLatin1String("stop") && playing) play_group(m_tiles.first(), false);
	        },
	        this);
	if (!m_controller->open()) {
		delete m_controller;
		m_controller = nullptr;
		return false;
	}
	for (snurra_tile *tile : m_tiles) name_controls(tile);
	return true;
}

void snurra_node::name_controls(snurra_tile *tile) {
	if (m_controller) m_controller->name_widget(tile->play_button(), QStringLiteral("play"));
}

void snurra_node::show_sockets() {
	for (snurra_tile *tile : m_tiles) tile->set_listen_available(!m_peer_listeners.isEmpty());
}

/* The plug into this tile (project.md sec 3f): the deck view with
 * headphones is asked to mirror it, as it mirrors a deck of its own. */
void snurra_node::listen_at(snurra_tile *tile, bool on) {
	if (m_peer_listeners.isEmpty()) return;
	for (snurra_tile *other : m_tiles)
		if (other != tile) other->set_listened(false);
	snurra_wire::message listen;
	listen.type = snurra_wire::LISTEN;
	listen.path = m_path;
	listen.index = on ? int(m_tiles.indexOf(tile)) : -1;
	QLocalSocket *socket = new QLocalSocket(this);
	connect(socket, &QLocalSocket::connected, this, [socket, listen] {
		socket->write(snurra_wire::encode(listen));
		socket->disconnectFromServer();
	});
	connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
	connect(socket, &QLocalSocket::errorOccurred, socket, &QObject::deleteLater);
	socket->connectToServer(m_peer_listeners.constBegin().key());
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
	snurra_deck::start_t start = snurra_tile::start_for(state, m_engine->rate());
	start.held = held;
	start.anchored = held;   /* in step with a giver: only a handover's taker */
	snurra_tile *tile = new snurra_tile(state, m_engine->add(start), this);
	tile->take_drawers_out();
	tile->set_bar_shown(false);
	name_controls(tile);
	connect(tile, &snurra_tile::listen_requested, this, [this](snurra_tile *asking, bool on) { listen_at(asking, on); });
	tile->set_listen_available(!m_peer_listeners.isEmpty());
	connect(tile, &snurra_tile::handle_moved, this, &snurra_node::handle_moved);
	connect(tile, &snurra_tile::handle_released, this, &snurra_node::handle_released);
	connect(tile, &snurra_tile::touched, this, [this](snurra_tile *touched) {
		m_current = touched;
		/* The panel follows the hand: out, it moves to the tile touched. */
		if (!touched->controls())
			for (snurra_tile *other : m_tiles)
				if (other->controls()) {
					touched->set_controls(true);
					break;
				}
		show_bar();
	});
	tile->set_grouped(true);
	connect(tile, &snurra_tile::play_requested, this, &snurra_node::play_group);
	connect(tile, &snurra_tile::drawers_changed, this, [this] {
		fit_to_drawers();
		/* A picture found or opened that is not the window's: closed
		 * once it has said so -- one picture to a window. */
		QTimer::singleShot(0, this, [this] { show_picture(); });
	});
	connect(tile, &snurra_tile::timing_changed, this, &snurra_node::timing_changed);
	connect(tile, &snurra_tile::nudge_requested, this, &snurra_node::nudge_lane);
	connect(tile, &snurra_tile::sync_requested, this, [this](snurra_tile *asking) { sync(asking); });
	connect(tile, &snurra_tile::key_sync_requested, this, [this](snurra_tile *asking) { sync_key(asking); });
	connect(tile, &snurra_tile::seek_requested, this, &snurra_node::seek_group);
	connect(tile, &snurra_tile::shift_requested, this, &snurra_node::shift_lane);
	/* Dropped on the full-screen picture, where nothing else of the window
	 * shows: the train goes on. */
	connect(tile, &snurra_tile::files_dropped, this,
	        [this](const QStringList &files) { add_files(files, snurra_wire::RIGHT); });
	connect(tile, &snurra_tile::controls_changed, this, [this](snurra_tile *changed) {
		if (m_moving_panel) return;
		/* One panel (the holder, 2026-11-21: "Each player only has one
		 * control panel"): out on this tile, in on every other. */
		m_moving_panel = true;
		if (changed->controls())
			for (snurra_tile *other : m_tiles)
				if (other != changed && other->controls()) other->set_controls(false);
		m_moving_panel = false;
		show_bar();
		setUpdatesEnabled(false);
		lay_out_timeline();
		setUpdatesEnabled(true);
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
	for (snurra_tile *tile : m_tiles) tile->set_place(rows.value(tile->row()), columns.value(tile->column()));
	/* One panel: tiles joined with their controls out keep them out on
	 * one -- the one last touched, else the first. */
	m_moving_panel = true;
	snurra_tile *keep = nullptr;
	for (snurra_tile *tile : ordered())
		if (tile->controls() && (!keep || tile == m_current)) keep = tile;
	for (snurra_tile *tile : m_tiles)
		if (tile != keep && tile->controls()) tile->set_controls(false);
	m_moving_panel = false;
	lay_out_timeline();
	setUpdatesEnabled(true);
	/* As wide as it was, or as the tiles ask -- a few lines a tile -- and
	 * never wider than the screen: what a long lane needs is room, and the
	 * scale gives it (the holder, 2026-11-18: proportional "without going
	 * off screen"). */
	const int kept = width();
	m_resizing_self = true;
	adjustSize();
	if (m_width_chosen) resize(kept, height());
	m_resizing_self = false;
	widen_for_timeline();

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
	show_bar();
	reshape();
}

/* The bar says what the window plays -- the tile last touched, else the
 * first -- and whether the controls are out. */
void snurra_node::show_bar() {
	snurra_tile *shown = panel_tile();
	QString said = shown ? shown->title() : QStringLiteral("snurra");
	if (m_tiles.size() > 1) said += QStringLiteral("  (%1)").arg(tr("%n tracks", nullptr, int(m_tiles.size())));
	m_bar_title->setText(said);
	m_bar_title->setToolTip(shown ? shown->state().source : QString());
	bool out = false;
	for (snurra_tile *tile : m_tiles) out = out || tile->controls();
	m_bar_controls->blockSignals(true);
	m_bar_controls->setChecked(out);
	m_bar_controls->blockSignals(false);
	m_bar_controls->setArrowType(out ? Qt::UpArrow : Qt::DownArrow);
	m_bar_controls->setAccessibleName(out ? tr("Push the controls in") : tr("Pull the controls out"));
	m_bar_controls->setToolTip(m_bar_controls->accessibleName());
}

/* The tile the window's controls act on: the one last touched, else the
 * first with its controls out, else the first. */
snurra_tile *snurra_node::panel_tile() const {
	if (m_current && m_tiles.contains(m_current)) return m_current;
	for (snurra_tile *tile : ordered())
		if (tile->controls()) return tile;
	const QList<snurra_tile *> all = ordered();
	return all.isEmpty() ? nullptr : all.first();
}

/* The bar's close: every tile, and so the window. */
void snurra_node::close_all() {
	for (snurra_tile *tile : QList<snurra_tile *>(m_tiles)) remove_tile(tile);
	leave_if_empty();
}

bool snurra_node::eventFilter(QObject *watched, QEvent *event) {
	if (watched != m_bar_title) return QWidget::eventFilter(watched, event);
	switch (event->type()) {
	case QEvent::MouseButtonPress: {
		const QMouseEvent *press = static_cast<QMouseEvent *>(event);
		if (press->button() != Qt::LeftButton) break;
		m_bar_held = true;
		m_bar_press = press->globalPosition().toPoint();
		m_bar_window_from = pos();
		m_bar_title->setCursor(Qt::ClosedHandCursor);
		/* Several tiles: the window moves as one, by the window system
		 * where it can -- on Wayland the only way. */
		if (m_tiles.size() != 1 && windowHandle() && windowHandle()->startSystemMove()) {
			m_bar_held = false;
			m_bar_title->setCursor(Qt::OpenHandCursor);
		}
		return true;
	}
	case QEvent::MouseMove: {
		if (!m_bar_held) break;
		const QMouseEvent *moved = static_cast<QMouseEvent *>(event);
		if (!(moved->buttons() & Qt::LeftButton)) {
			m_bar_held = false;
			m_bar_title->setCursor(Qt::OpenHandCursor);
			break;
		}
		const QPoint global = moved->globalPosition().toPoint();
		if (m_tiles.size() == 1) handle_moved(m_tiles.first(), global);
		else move(m_bar_window_from + (global - m_bar_press));
		return true;
	}
	case QEvent::MouseButtonRelease: {
		if (!m_bar_held) break;
		m_bar_held = false;
		m_bar_title->setCursor(Qt::OpenHandCursor);
		if (m_tiles.size() == 1) handle_released(m_tiles.first(), static_cast<QMouseEvent *>(event)->globalPosition().toPoint());
		return true;
	}
	default:
		break;
	}
	return QWidget::eventFilter(watched, event);
}

/*
 * The tiles along the timeline (the holder, 2026-11-18: "the tracklengths
 * probably need to be proportional for mixing (without going off
 * screen)"): a tile from where its lane starts it to where the next one
 * starts -- its whole length if it is last -- so every lane is on one
 * scale, a lane that begins later begins further right, and a train's
 * join is drawn where the next track comes in, on the beat the overlap
 * snapped to (snurra_train_overlap). The grid's columns are the spans
 * between every tile's start and end, each stretched by its time; a tile
 * and its drawers span its own. Each tile keeps a few letters' width, so
 * a short one beside a long one is narrow, not gone.
 */
void snurra_node::lay_out_timeline() {
	struct span_t {
		snurra_tile *tile;
		int64_t from, to;
	};
	std::vector<span_t> spans;
	std::vector<int64_t> edges;
	for (int row : rows()) {
		const QList<snurra_tile *> tiles = lane(row);
		for (int i = 0; i < tiles.size(); ++i) {
			const int64_t from = lane_start(tiles.at(i));
			const int64_t to = i + 1 < tiles.size() ? lane_start(tiles.at(i + 1)) : from + timeline_length(tiles.at(i));
			spans.push_back({ tiles.at(i), from, std::max(to, from + 1) });
			edges.push_back(from);
			edges.push_back(std::max(to, from + 1));
		}
	}
	std::sort(edges.begin(), edges.end());
	edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
	/* The scale at which the tile that asks most for its time -- a short
	 * one beside a long set -- has its least width, and so the width of
	 * the whole timeline at it. */
	double per_frame = 0;
	for (const span_t &span : spans) {
		const int least = span.tile->minimumSizeHint().width();
		per_frame = std::max(per_frame, double(least) / double(span.to - span.from));
	}
	m_timeline_wanted = edges.size() > 1 ? int(std::ceil(per_frame * double(edges.back() - edges.front()))) : 0;
	/*
	 * The window's one control panel (the holder, 2026-11-21: "Each
	 * player only has one control panel and once a tile is connected to
	 * it, it becomes a part of that player"): the controls of the tile
	 * they are out on, under every lane and the window's width -- not a
	 * drawer under that tile's own span.
	 */
	snurra_tile *panel = nullptr;
	for (const span_t &span : spans)
		if (!panel && span.tile->controls()) panel = span.tile;
	if (panel) m_timeline_wanted = std::max(m_timeline_wanted, panel->drawer_below()->minimumSizeHint().width());
	auto column_of = [&edges](int64_t at) { return int(std::lower_bound(edges.begin(), edges.end(), at) - edges.begin()); };
	for (const span_t &span : spans) {
		m_grid->removeWidget(span.tile);
		m_grid->removeWidget(span.tile->drawer_above());
		m_grid->removeWidget(span.tile->drawer_below());
	}
	for (int column = 0; column < m_grid->columnCount(); ++column) m_grid->setColumnStretch(column, 0);
	for (int row = 0; row < m_grid->rowCount(); ++row) m_grid->setRowStretch(row, 0);
	/* Stretches in tenths of a second: an int, and finer than a pixel. */
	for (size_t i = 0; i + 1 < edges.size(); ++i)
		m_grid->setColumnStretch(int(i), std::max(1, int(snurra_frames_to_us(edges[i + 1] - edges[i], m_engine->rate()) / 100000)));
	for (const span_t &span : spans) {
		const int lane = 3 * span.tile->row();
		const int column = column_of(span.from);
		const int across = std::max(1, column_of(span.to) - column);
		m_grid->addWidget(span.tile, lane + 1, column, 1, across);
		span.tile->show();
	}
	const int lanes = int(rows().size());
	if (panel) m_grid->addWidget(panel->drawer_below(), 3 * lanes, 0, 1, std::max(1, int(edges.size()) - 1));
	/*
	 * One picture to a window (the holder, 2026-11-21: "maybe video still
	 * goes on top of top bar or below it, only one video window"): the
	 * picture tile's drawer on top, over the bar, the window's width;
	 * every other tile's drawer in no layout, and closed (show_picture).
	 */
	QVBoxLayout *outer = static_cast<QVBoxLayout *>(layout());
	snurra_tile *picture = picture_tile();
	for (const span_t &span : spans)
		if (span.tile != picture) outer->removeWidget(span.tile->drawer_above());
	if (picture && outer->indexOf(picture->drawer_above()) != 0) {
		outer->removeWidget(picture->drawer_above());
		outer->insertWidget(0, picture->drawer_above());
	}
	m_grid->setRowStretch(3 * lanes + 1, 1);
	if (m_ruler) m_ruler->update();
}

/* A length known, a tempo moved, beats found, a lane's begin set: laid
 * out again once the event loop is back, as one -- a tempo slider dragged
 * says so at every step. Not while a lane is dragged along the timeline,
 * whose waveform the pointer is on; its release says so again. */
void snurra_node::timing_changed() {
	if (m_timing_queued) return;
	m_timing_queued = true;
	QTimer::singleShot(100, this, [this] {
		m_timing_queued = false;
		if (m_shift_row >= 0) {
			timing_changed();
			return;
		}
		setUpdatesEnabled(false);
		lay_out_timeline();
		setUpdatesEnabled(true);
		widen_for_timeline();
		reshape();
	});
}

/* Wide enough for the timeline's one scale (lay_out_timeline), and never
 * wider than the screen (the holder, 2026-11-18: proportional "without
 * going off screen"): grown to it, never narrowed -- a width a person set
 * that already has room stays. Where the screen is too narrow for it the
 * shortest tiles keep their least width and the scale gives. */
void snurra_node::widen_for_timeline(bool may_narrow) {
	if (!isWindow()) return;
	if (m_width_chosen) {
		keep_on_screen();
		return;
	}
	const QMargins margins = layout() ? layout()->contentsMargins() : QMargins();
	const int spacing = m_grid->horizontalSpacing() > 0 ? m_grid->horizontalSpacing() : 0;
	const int wanted = m_timeline_wanted + margins.left() + margins.right() + spacing * std::max(0, m_grid->columnCount() - 1);
	const QScreen *on = screen();
	const int most = on ? on->availableGeometry().width() : wanted;
	const int width_now = std::min(std::max(may_narrow ? std::max(minimumSizeHint().width(), wanted) : width(), wanted), most);
	if (width_now != width()) {
		m_resizing_self = true;
		resize(width_now, height());
		m_resizing_self = false;
	}
	keep_on_screen();
}

/* On the screen: grown from where it was opened -- at the pointer -- its
 * right edge went past the screen's (seen in Xvfb). */
void snurra_node::keep_on_screen() {
	const QScreen *on = screen();
	if (!on || !isWindow() || !isVisible()) return;
	const QRect room = on->availableGeometry();
	const int x = std::max(room.left(), std::min(pos().x(), room.right() + 1 - width()));
	if (x != pos().x()) move(x, pos().y());
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
		m_resizing_self = true;
		resize(qMax(width(), minimumSizeHint().width()), sizeHint().height());
		m_resizing_self = false;
		layout()->activate();
		if (anchor && isWindow() && isVisible()) move(pos() - (anchor->mapTo(this, QPoint()) - before));
		reshape();
	});
}

void snurra_node::resizeEvent(QResizeEvent *event) {
	QWidget::resizeEvent(event);
	/* Not one of its own, while shown, to another width: the person's. */
	if (!m_resizing_self && isVisible() && event->oldSize().width() > 0 && event->size().width() != event->oldSize().width())
		m_width_chosen = true;
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
		add(m_bar);
		add(m_ruler);
		for (snurra_tile *tile : m_tiles) {
			add(tile);
			add(tile->drawer_above());
			add(tile->drawer_below());
		}
		add(findChild<QSizeGrip *>());
		add(m_drop_hint);
		add(m_ghost);
		add(m_ghost_line);
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
	layout()->removeWidget(tile->drawer_above());   /* the window's picture */
	tile->hide();
	if (tile->drawer_above()) tile->drawer_above()->hide();
	if (tile->drawer_below()) tile->drawer_below()->hide();
	tile->deleteLater();
	place_tiles();
	/* Fitted to what is left, at the timeline's scale: the tiles fill the
	 * width, so where the tile was is not left as nothing. Narrowed to the
	 * size hint instead, as before the tiles went along the timeline, it
	 * was the tiles' least widths, every one squeezed. A width the person
	 * set stays, the tiles filling it. */
	QTimer::singleShot(0, this, [this] {
		if (m_tiles.isEmpty()) return;
		layout()->activate();
		widen_for_timeline(true);
	});
	fit_to_drawers();
}

/* The overlap between `tile` and `next` on their lane's timeline: the
 * one the train plays and every seek counts with -- `next`'s own where it
 * was dragged to come in. */
int64_t snurra_node::overlap(snurra_tile *tile, snurra_tile *next) const {
	if (!next) return 0;
	const QList<snurra_tile *> tiles = lane(tile->row());
	const int index = int(tiles.indexOf(tile));
	if (index < 0 || index + 1 >= tiles.size() || tiles.at(index + 1) != next) {
		/* Not neighbours on one lane: as the two would cross. */
		const snurra_beat_grid grid = tile->beats(), next_grid = next->beats();
		const qint64 chosen_us = next->state().overlap_us;
		return snurra_train_overlap(timeline_length(tile), timeline_length(next), m_engine->rate(), tile->deck()->tempo(),
		                            next->deck()->tempo(), &grid, &next_grid,
		                            chosen_us >= 0 ? snurra_us_to_frames(chosen_us, m_engine->rate()) : -1);
	}
	return timeline().overlap(int(ordered().indexOf(tile)));
}

/* The tile whose picture the window shows: the one chosen, else the
 * first in the timeline with a picture. */
snurra_tile *snurra_node::picture_tile() const {
	if (m_picture && m_tiles.contains(m_picture) && m_picture->has_video()) return m_picture;
	for (snurra_tile *tile : ordered())
		if (tile->has_video()) return tile;
	return nullptr;
}

/*
 * The picture follows what is heard: the first in the timeline that plays
 * and has one -- through a train's crossfade the one going out, the one
 * coming in mixed over it as far as the crossfade has gone (mix_for),
 * then, the first ended, the one come in. Every other picture is closed:
 * one window of video to a player. Open as the window last had it.
 */
void snurra_node::show_picture() {
	snurra_tile *lead = nullptr;
	for (snurra_tile *tile : ordered())
		if (tile->has_video() && tile->playing()) {
			lead = tile;
			break;
		}
	const bool heard = lead != nullptr;
	if (!lead) lead = picture_tile();
	bool moved = false;
	if (lead && lead != m_picture) {
		/* Open as the window last had it, where the picture it showed was
		 * one heard; one shown only for want of any playing -- the next
		 * film found before the first, closed as a tile with no controls
		 * opens -- says nothing of that, and the new one opens as it would
		 * on its own (a_window_shows_one_picture_..., one run in six). */
		m_picture_open = m_picture && m_tiles.contains(m_picture) && m_picture_heard ? m_picture->video_open() : lead->video_open();
		m_picture = lead;
		moved = true;
	}
	if (lead) m_picture_heard = heard;
	for (snurra_tile *tile : m_tiles) {
		if (tile == lead) continue;
		tile->mix_in = nullptr;
		if (tile->video_open()) {
			tile->set_video_open(false);
			moved = true;
		}
	}
	if (!lead) {
		m_bar_picture->hide();
		return;
	}
	if (!lead->mix_in) {
		const QPointer<snurra_tile> shown = lead;
		lead->mix_in = [this, shown](QImage &other) { return shown ? mix_for(shown, other) : 0.0; };
	}
	if (moved) {
		if (lead->video_open() != m_picture_open) lead->set_video_open(m_picture_open);
		setUpdatesEnabled(false);
		lay_out_timeline();
		setUpdatesEnabled(true);
		fit_to_drawers();
	}
	m_bar_picture->setVisible(true);
	m_bar_picture->blockSignals(true);
	m_bar_picture->setChecked(lead->video_open());
	m_bar_picture->blockSignals(false);
}

/* Through a crossfade from `lead` to the next of its train, the next's
 * picture and how far in it is: 0 where it comes in, 1 where the first
 * ends. None where the next has no picture or is not yet heard. */
double snurra_node::mix_for(snurra_tile *lead, QImage &other) {
	const QList<snurra_tile *> tiles = lane(lead->row());
	const int at = int(tiles.indexOf(lead));
	if (at < 0 || at + 1 >= tiles.size()) return 0;
	snurra_tile *next = tiles.at(at + 1);
	if (!next->has_video() || !next->playing()) return 0;
	const int64_t across = overlap(lead, next);
	if (across <= 0) return 0;
	const double in = double(next->deck()->position_at(snurra_clock_us()) - snurra_us_to_frames(next->offset_us(), next->deck()->rate()))
	                  / std::max(0.01, double(next->deck()->tempo()));
	if (in <= 0) return 0;
	other = next->picture_now();
	return other.isNull() ? 0 : std::clamp(in / double(across), 0.0, 1.0);
}

void snurra_node::run_trains() {
	show_picture();
	if (m_ruler)
		for (snurra_tile *tile : m_tiles)
			if (tile->playing()) {
				m_ruler->update();
				break;
			}
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
	return timeline().start(int(ordered().indexOf(tile)));
}

/* A tile's length on its lane's timeline, in frames out: its track's
 * length at its tempo. */
int64_t snurra_node::timeline_length(snurra_tile *tile) const {
	snurra_timeline_track one;
	one.length = tile->length_frames();
	one.tempo = tile->deck()->tempo();
	return snurra_timeline::length_out(one);
}

snurra_timeline snurra_node::timeline() const {
	snurra_timeline made(m_engine->rate());
	for (snurra_tile *tile : ordered()) {
		const QList<snurra_tile *> tiles = lane(tile->row());
		snurra_timeline_track one;
		one.length = tile->length_frames();
		one.tempo = tile->deck()->tempo();
		one.offset = snurra_us_to_frames(tile->offset_us(), m_engine->rate());
		one.grid = tile->beats();
		/* A window's lane is a train: its first comes in at the lane's
		 * begin, each after it follows the one before. */
		if (tiles.first() == tile) {
			one.position = snurra_us_to_frames(tile->lane_start_us(), m_engine->rate());
		} else {
			one.follows = int(made.tracks.size()) - 1;
			const qint64 chosen_us = tile->state().overlap_us;
			one.chosen_overlap = chosen_us >= 0 ? snurra_us_to_frames(chosen_us, m_engine->rate()) : -1;
		}
		made.tracks.push_back(one);
	}
	return made;
}

QList<snurra_tile *> snurra_node::ordered() const {
	QList<snurra_tile *> order;
	for (int row : rows()) order << lane(row);
	return order;
}

void snurra_node::play_group(snurra_tile *from, bool playing) {
	if (follows_decks()) {
		snurra_wire::message asked;
		asked.type = snurra_wire::PLAY;
		asked.time = playing ? 1 : 0;
		send(m_follow_socket, asked);
		return;
	}
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
	if (wait) *wait = 0;
	const QList<snurra_tile *> tiles = lane(row);
	if (tiles.isEmpty()) return nullptr;
	const QList<snurra_tile *> order = ordered();
	const snurra_timeline made = timeline();
	const snurra_timeline::place found = made.at(made.train(int(order.indexOf(tiles.first()))), time);
	if (wait) *wait = found.wait;
	if (found.track < 0) return nullptr;
	frame = found.frame;
	return order.at(found.track);
}

void snurra_node::seek_group(snurra_tile *from, qint64 position_us) {
	const int64_t time = lane_start(from) + std::llround(double(snurra_us_to_frames(position_us - from->offset_us(), m_engine->rate()))
	                                                    / from->deck()->tempo());
	if (follows_decks()) {
		snurra_wire::message asked;
		asked.type = snurra_wire::SEEK;
		asked.time = snurra_frames_to_us(std::max<int64_t>(0, time), m_engine->rate());
		send(m_follow_socket, asked);
		m_follow_held = -1;
		return;
	}
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
	/* A train's later tile: where it comes in, along the one before it --
	 * later, a shorter crossing; earlier, a longer -- its first beat on a
	 * beat of the one before (the holder, 2026-11-18: "they need to snap
	 * to beats when connecting"), Shift held where it is put. */
	const int at = int(tiles.indexOf(tile));
	if (at > 0) {
		snurra_tile *before = tiles.at(at - 1);
		if (m_shift_tile != tile) {
			m_shift_tile = tile;
			m_shift_row = tile->row();
			m_shift_from_overlap = overlap(before, tile);
		}
		const int64_t length = timeline_length(before), next_length = timeline_length(tile);
		int64_t chosen = m_shift_from_overlap - snurra_us_to_frames(delta_us, m_engine->rate());
		if (snap) {
			const snurra_beat_grid grid = before->beats(), next_grid = tile->beats();
			chosen = snurra_overlap_on_beat(chosen, length, next_length, before->deck()->tempo(), tile->deck()->tempo(), &grid,
			                                &next_grid);
		}
		chosen = snurra_train_overlap(length, next_length, m_engine->rate(), 1, 1, nullptr, nullptr, std::max<int64_t>(0, chosen));
		tile->set_overlap_us(qint64(snurra_frames_to_us(chosen, m_engine->rate())));
		if (!done) return;
		m_shift_tile = nullptr;
		m_shift_row = -1;
		snurra_tile *clock = nullptr;
		for (snurra_tile *other : m_tiles)
			if (other->playing() && (!clock || clock == tile)) clock = other;
		if (clock) start_group_at(group_time(clock), true);
		return;
	}
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
	return timeline().time_of(int(ordered().indexOf(tile)), tile->frame_now());
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
	/* Emptied by the hand, or by the decks folding: nothing more to
	 * follow, so the engine's idle may end it. */
	if (m_tiles.isEmpty() && follows_decks()) {
		m_follow_timer->stop();
		m_follow.clear();
	}
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
                        const QList<snurra_tile *> &tiles, int deck, const QPoint &at) {
	snurra_wire::message m;
	m.type = snurra_wire::OFFER;
	m.token = token;
	m.side = side;
	m.deck = deck;
	if (!at.isNull()) m.frame = QRect(at, QSize(1, 1));
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

void snurra_node::merge_into(const QString &peer, snurra_wire::side_t side, int index, int deck, const QPoint &at) {
	/* A window following the decks' player holds no tracks of its own to
	 * give: the decks hold them (follow). */
	if (follows_decks()) return;
	if (m_tiles.isEmpty() || index >= m_tiles.size()) return;
	QLocalSocket *socket = new QLocalSocket(this);
	watch(socket);
	const QString token = new_token();
	const QList<snurra_tile *> tiles = index < 0 ? m_tiles : QList<snurra_tile *>{ m_tiles.at(index) };
	connect(socket, &QLocalSocket::connected, this, [this, socket, token, side, tiles, deck, at] {
		offer(socket, token, side, tiles, deck, at);
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
	/* The whole player given to the decks (open_decks): the switch -- one
	 * player or its decks, not both (the holder, 2026-11-24, setting aside
	 * 2026-11-22's "leave the window open as a follower"), so this window
	 * goes as any emptied one does. Following stays for --follow. */
	if (ready.token == m_decks_token) m_decks_token.clear();
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
	/* A window following the decks' player holds no tracks of its own to
	 * give: the decks hold them (follow). */
	if (follows_decks()) return;
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

/*
 * The way from a window to the decks (the holder, 2026-11-21: "I don't
 * know how to switch to deck mode"): as a tile pulled out, a new process
 * -- the deck view -- connects back and asks for the player by the token
 * (ADOPT), here every tile, onto its deck A; the player plays on here
 * until it says READY, so the switch is heard as nothing. The deck view
 * unfolds what it took across its decks.
 */
void snurra_node::open_decks() {
	/* A window following the decks' player holds no tracks of its own to
	 * give: the decks hold them (follow). */
	if (follows_decks()) return;
	if (m_tiles.isEmpty()) {
		QProcess::startDetached(QCoreApplication::applicationFilePath(), { QStringLiteral("--dj") });
		return;
	}
	const QString token = new_token();
	pending_give placeholder;
	for (snurra_tile *tile : m_tiles) placeholder.tiles << tile;
	placeholder.expiry = new QTimer(this);
	placeholder.expiry->setSingleShot(true);
	connect(placeholder.expiry, &QTimer::timeout, this, [this, token] { abort_give(token); });
	placeholder.expiry->start(ADOPT_WAIT_MS);
	m_giving.insert(token, placeholder);
	m_decks_token = token;
	QProcess::startDetached(QCoreApplication::applicationFilePath(),
	                        { QStringLiteral("--dj"), QStringLiteral("--adopt"), m_path, QStringLiteral("--token"), token });
}

/*
 * Both views open at once (sec 3f, the holder, 2026-11-20: switched
 * between and/or combined): this window showing the player the decks at
 * `decks` play. Asked ten times a second (FOLLOW); what changed there --
 * a track's EQ, tempo or key turned on a deck, a ride, a crossing moved,
 * the tracks themselves -- shown here; the tiles silent, the decks being
 * what is heard, and in step with them, so the waveforms, the ruler and
 * the picture move as the decks play. Its play, pause and seek are the
 * decks' (PLAY, SEEK with no index).
 */
void snurra_node::follow(const QString &decks) {
	m_follow = decks;
	if (QWidget *to_decks = findChild<QWidget *>(QStringLiteral("snurra-bar-decks"))) to_decks->hide();
	if (!m_follow_timer) {
		m_follow_timer = new QTimer(this);
		m_follow_timer->setInterval(100);
		connect(m_follow_timer, &QTimer::timeout, this, [this] {
			/* Half a second unanswered: the decks are gone, and so is
			 * what this showed. */
			if (++m_follow_misses > 5) {
				m_follow_timer->stop();
				m_follow.clear();
				close_all();
				return;
			}
			if (!m_follow_socket) {
				m_follow_socket = new QLocalSocket(this);
				watch(m_follow_socket);
				connect(m_follow_socket, &QLocalSocket::errorOccurred, m_follow_socket, &QObject::deleteLater);
				m_follow_socket->connectToServer(m_follow);
			}
			snurra_wire::message asked;
			asked.type = snurra_wire::FOLLOW;
			send(m_follow_socket, asked);
		});
	}
	m_follow_timer->start();
}

void snurra_node::followed(const snurra_wire::message &m) {
	m_follow_misses = 0;
	if (m.states.isEmpty() && m_followed.isEmpty() && ++m_follow_empty <= 50) {
		/* Not yet unfolded: the player just given by open_decks() spreads
		 * across the decks once its crossfade is over. Five seconds. */
		return;
	}
	if (m.states.isEmpty()) {
		/* Folded, or ended by a tune loaded by hand: nothing to show. */
		m_follow_timer->stop();
		m_follow.clear();
		close_all();
		return;
	}
	bool same = m.states.size() == m_followed.size();
	for (qsizetype i = 0; same && i < m.states.size(); ++i)
		same = m.states.at(i).source == m_followed.at(i).source && m.states.at(i).audio_track == m_followed.at(i).audio_track;
	const QList<snurra_tile *> order = ordered();
	/* What the hand changed here since the last answer, to the decks: a
	 * track's EQ, ride, level, tempo or key, a crossing or a lane moved
	 * along. Each track as it is here, then as it was -- so the decks
	 * take only what changed here, and a deck turned there meanwhile
	 * keeps its own. Those fields are not taken back from the answer
	 * below, which the decks gave before they had them. */
	QList<bool> mine(order.size(), false);
	if (same && order.size() == m.states.size() && m_follow_base.size() == order.size()) {
		snurra_wire::message change;
		change.type = snurra_wire::CHANGE;
		bool any = false;
		for (qsizetype i = 0; i < order.size(); ++i) {
			const snurra_tile_state now = order.at(i)->state();
			mine[i] = snurra_wire::follow_changed(now, m_follow_base.at(i));
			any = any || mine[i];
			change.states << now;
		}
		if (any) {
			change.states << m_follow_base;
			send(m_follow_socket, change);
		}
	}
	if (!same || order.size() != m.states.size()) {
		/* Other tracks: the tiles made again, paused, the time below
		 * starting them. */
		for (snurra_tile *tile : QList<snurra_tile *>(m_tiles)) remove_tile(tile);
		for (snurra_tile_state state : m.states) {
			state.playing = false;
			add_tile(state);
		}
		m_follow_held = -1;
		place_tiles();
		timing_changed();
	} else {
		/* The same tracks: what was changed there, field by field. */
		bool timing = false, placed = false;
		for (qsizetype i = 0; i < order.size(); ++i) {
			if (mine.at(i)) continue;
			snurra_tile *tile = order.at(i);
			const snurra_tile_state &now = m.states.at(i), &was = m_followed.at(i);
			if (!(now.eq == was.eq)) tile->set_eq(now.eq);
			if (now.envelope != was.envelope) tile->set_envelope(now.envelope);
			if (now.level != was.level) tile->set_level(now.level);
			if (now.key_lock != was.key_lock) tile->set_key_lock(now.key_lock);
			if (now.key_shift != was.key_shift) tile->set_key_shift(now.key_shift);
			if (now.tempo != was.tempo) {
				tile->set_tempo(now.tempo);
				timing = true;
			}
			if (now.lane_start_us != was.lane_start_us) {
				tile->set_lane_start_us(now.lane_start_us);
				timing = true;
			}
			if (now.overlap_us != was.overlap_us) {
				tile->set_overlap_us(now.overlap_us);
				timing = true;
			}
			if (now.offset_us != was.offset_us) {
				tile->set_offset_us(now.offset_us);
				timing = true;
			}
			if (now.row != was.row || now.column != was.column) {
				tile->set_place(now.row, now.column);
				placed = timing = true;
			}
		}
		if (placed) place_tiles();
		if (timing) {
			timing_changed();
			m_follow_held = -1;
		}
	}
	m_followed = m.states;
	m_follow_base.clear();
	for (snurra_tile *tile : ordered()) m_follow_base << tile->state();
	/* Silent: the decks are what is heard. */
	for (snurra_tile *tile : m_tiles) tile->deck()->set_mix(0);

	/* Where the decks are; started again only when this has drifted from
	 * them by more than a fifth of a second -- a waveform redrawn ten
	 * times a second for nothing is not in step, it is busy. */
	const int rate = m_engine->rate();
	const bool playing = m.index != 0;
	const qint64 now = snurra_clock_us();
	const int64_t time = snurra_us_to_frames(std::max<qint64>(0, m.time + (playing ? now - m.started : 0)), rate);
	snurra_tile *sounding = nullptr;
	for (snurra_tile *tile : m_tiles)
		if (tile->playing()) sounding = tile;
	if (playing) {
		m_follow_held = -1;
		if (!sounding || std::abs(group_time(sounding) - time) > rate / 5) start_group_at(time, true);
		return;
	}
	if (sounding) {
		m_stopped = true;
		for (snurra_tile *tile : m_tiles)
			if (tile->playing()) tile->set_playing(false);
	}
	if (m_follow_held >= 0 && std::abs(m_follow_held - time) <= rate / 50) return;
	m_follow_held = time;
	for (int row : rows()) {
		int64_t frame = 0;
		if (snurra_tile *tile = lane_at(row, time, frame)) tile->hold_at(frame);
	}
}

void snurra_node::contextMenuEvent(QContextMenuEvent *event) {
	/* Remove, a window of its own, the decks: each the decks' to do. */
	if (follows_decks()) return;
	QMenu menu(this);
	/* The track under the hand: what its own title row's close and drag
	 * did, before the window's one bar took the row (sec 3b). */
	const QPointer<snurra_tile> over = tile_at(event->pos(), false);
	if (over) {
		const QString name = fontMetrics().elidedText(over->title(), Qt::ElideMiddle, fontMetrics().averageCharWidth() * 40);
		QAction *remove = menu.addAction(tr("Remove %1").arg(name));
		remove->setObjectName(QStringLiteral("snurra-remove"));
		connect(remove, &QAction::triggered, this, [this, over] {
			if (!over) return;
			remove_tile(over);
			leave_if_empty();
		});
		if (m_tiles.size() > 1) {
			QAction *out = menu.addAction(tr("Into a window of its own"));
			out->setObjectName(QStringLiteral("snurra-pull-out"));
			const QPoint beside = frameGeometry().topRight() + QPoint(fontMetrics().height(), 0);
			connect(out, &QAction::triggered, this, [this, over, beside] {
				if (over && m_tiles.size() > 1) release(int(m_tiles.indexOf(over)), can_place_windows() ? beside : QPoint());
			});
		}
		menu.addSeparator();
	}
	QAction *decks = menu.addAction(tr("Open in the decks"));
	decks->setToolTip(tr("This player onto a DJ's decks, each track on its deck"));
	connect(decks, &QAction::triggered, this, &snurra_node::open_decks);
	/* Who made it (the holder, 2026-11-23: in the right-click menu, as
	 * XMMS and gmplayer put theirs -- snurra has no menu bar). */
	if (!about_text().isEmpty()) {
		menu.addSeparator();
		QAction *about = menu.addAction(tr("About snurra"));
		about->setObjectName(QStringLiteral("snurra-about"));
		connect(about, &QAction::triggered, this, [this] { QMessageBox::about(this, tr("About snurra"), about_text()); });
	}
	menu.exec(event->globalPos());
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
	/* A whole player into an empty window -- a deck pulled out of a deck
	 * view -- is the player again, its tracks tiles once more, in step
	 * where the player stands (project.md sec 3f); into a window with
	 * tiles it stays one tile, a channel with a prepared mix. */
	QList<snurra_tile_state> states = m.states;
	if (m_tiles.isEmpty() && states.size() == 1 && !states.first().player.empty())
		states = snurra_player_tracks(states.first(), m_engine->rate());
	for (snurra_tile_state state : states) {
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
		if (!m.path.isEmpty()) {
			m_peer_frames.insert(m.path, m.frame);
			/* A deck view says how many decks it has; a window, none. */
			if (m.index > 0) m_peer_decks.insert(m.path, m.index);
			else m_peer_decks.remove(m.path);
			if (m.index > 0 && m.time > 0) m_peer_listeners.insert(m.path, int(m.time));
			else m_peer_listeners.remove(m.path);
			show_sockets();
		}
		socket->disconnectFromServer();
		break;
	case snurra_wire::OFFER:
		/* Not into a window following the decks: unanswered, the giver
		 * plays on (ABORT). */
		if (!follows_decks()) take(socket, m);
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
		/* The placeholder release() or open_decks() left becomes the real
		 * offer: one tile, or the whole player onto the deck asked. */
		QList<snurra_tile *> tiles;
		for (const QPointer<snurra_tile> &tile : it->tiles)
			if (tile) tiles << tile;
		if (it->expiry) it->expiry->deleteLater();
		m_giving.erase(it);
		offer(socket, m.token, snurra_wire::RIGHT, tiles, m.deck);
		break;
	}
	case snurra_wire::MERGE_INTO:
		merge_into(m.path, m.side, m.index, m.deck);
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
	case snurra_wire::FOLLOWING:
		if (follows_decks() && socket == m_follow_socket) followed(m);
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
		/* Asked every two seconds now: a peer that takes the question and
		 * never answers must not leave a socket open each time. */
		QTimer::singleShot(1500, socket, &QObject::deleteLater);
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
	/* Following the decks, nothing is rearranged here: a track is placed,
	 * pulled out or added on the decks, which hold the player. */
	if (follows_decks()) return;
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
			snurra_tile *over = nullptr;
			const int row = new_lane_at(mapFromGlobal(global), tile, &over);
			show_ghost(tile, row);
			hint_drop(row < 0 && over && over != tile, snurra_wire::RIGHT, over);
		} else {
			show_ghost(nullptr, -1);
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
	if (follows_decks()) {
		event->ignore();
		return;
	}
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
/*
 * Where a tile dragged by its waveform lands in its own window (the
 * holder, 2026-11-21: "we allow them to be placed above or below in
 * separate tracks, the preview should show what is going to happen
 * before the drop"): over the top or bottom third of a tile, a new lane
 * above or below that tile's -- the row it would be, the rows from there
 * down moving one down; under the last lane, a new last one. Over the
 * middle of another tile, -1 and that tile in `over`: the two trade
 * places. A tile alone in its lane gains nothing by a lane beside it.
 */
int snurra_node::new_lane_at(const QPoint &local, snurra_tile *moving, snurra_tile **over) const {
	*over = tile_at(local, false);
	const bool alone = lane(moving->row()).size() == 1;
	int row = -1;
	if (*over) {
		const QRect at = (*over)->geometry();
		const double share = double(local.y() - at.top()) / std::max(1, at.height());
		if (share < 1.0 / 3) row = (*over)->row();
		else if (share > 2.0 / 3) row = (*over)->row() + 1;
	} else {
		int bottom = -1, last = -1;
		for (snurra_tile *tile : m_tiles) {
			bottom = std::max(bottom, tile->geometry().bottom());
			last = std::max(last, tile->row());
		}
		if (local.y() > bottom) row = last + 1;
	}
	if (row >= 0 && alone && (row == moving->row() || row == moving->row() + 1)) row = -1;
	return row;
}

/* The preview of a new lane: the tile, faint, where it would sound -- at
 * the same moment, so across where it is now -- on the line between the
 * lanes it would come between, and that line drawn. */
/* What the ruler draws: each tile's span of the timeline where the tile
 * is, as lay_out_timeline spread them; the first track's beat on the
 * timeline -- its grid at its tempo, from where it comes in, less its
 * offset; and where the window is now. */
void snurra_node::fill_ruler(snurra_ruler &made) const {
	made.rate = m_engine->rate();
	for (int row : rows()) {
		const QList<snurra_tile *> tiles = lane(row);
		for (int i = 0; i < tiles.size(); ++i) {
			snurra_tile *tile = tiles.at(i);
			const int64_t from = lane_start(tile);
			const int64_t to = i + 1 < tiles.size() ? lane_start(tiles.at(i + 1)) : from + timeline_length(tile);
			const double left = tile->x() - made.x();
			made.segments.push_back({ from, std::max(to, from + 1), left, left + tile->width() });
		}
	}
	const QList<snurra_tile *> all = ordered();
	if (all.isEmpty()) return;
	snurra_tile *leader = all.first();
	const snurra_beat_grid grid = leader->beats();
	if (grid.valid() && grid.rate > 0) {
		const double scale = double(m_engine->rate()) / grid.rate;
		const double tempo = std::max(0.01, double(leader->deck()->tempo()));
		const double offset = double(snurra_us_to_frames(leader->offset_us(), m_engine->rate()));
		made.period = grid.period * scale / tempo;
		made.first = double(lane_start(leader)) + (grid.first * scale - offset) / tempo;
	}
	for (snurra_tile *tile : m_tiles)
		if (tile->playing()) {
			made.now = group_time(tile);
			break;
		}
}

void snurra_node::show_ghost(snurra_tile *moving, int row) {
	if (!moving || row < 0) {
		if (m_ghost) m_ghost->hide();
		if (m_ghost_line) m_ghost_line->hide();
		return;
	}
	if (!m_ghost) {
		m_ghost = new QLabel(this);
		m_ghost->setObjectName(QStringLiteral("snurra-ghost"));
		m_ghost->setAttribute(Qt::WA_TransparentForMouseEvents);
		m_ghost_line = new QWidget(this);
		m_ghost_line->setObjectName(QStringLiteral("snurra-ghost-line"));
		m_ghost_line->setAttribute(Qt::WA_TransparentForMouseEvents);
		m_ghost_line->setAttribute(Qt::WA_StyledBackground);
		m_ghost_line->setStyleSheet(QStringLiteral("background: palette(highlight);"));
	}
	int y = -1;
	for (snurra_tile *tile : m_tiles)
		if (tile->row() == row) y = y < 0 ? tile->geometry().top() : std::min(y, tile->geometry().top());
	if (y < 0)
		for (snurra_tile *tile : m_tiles) y = std::max(y, tile->geometry().bottom() + 1);
	if (m_ghost_row != row || m_ghost_tile != moving) {
		QPixmap faint(moving->size() * moving->devicePixelRatioF());
		faint.setDevicePixelRatio(moving->devicePixelRatioF());
		faint.fill(Qt::transparent);
		QPainter painter(&faint);
		painter.setOpacity(0.55);
		painter.drawPixmap(0, 0, moving->grab());
		painter.end();
		m_ghost->setPixmap(faint);
		m_ghost_row = row;
		m_ghost_tile = moving;
	}
	const int thick = std::max(3, fontMetrics().height() / 5);
	m_ghost_line->setGeometry(0, y - thick / 2, width(), thick);
	m_ghost->setGeometry(QRect(QPoint(moving->x(), y - moving->height() / 2), moving->size()));
	m_ghost_line->raise();
	m_ghost->raise();
	m_ghost_line->show();
	m_ghost->show();
	reshape();
}

/* A tile into a lane of its own at `row`, coming in where it comes in
 * now; the lanes from there down one further down, and the train it left
 * closing up as when a tile goes (drop_tile). */
void snurra_node::move_to_new_lane(snurra_tile *tile, int row) {
	const qint64 begin = snurra_frames_to_us(lane_start(tile), m_engine->rate());
	const QList<snurra_tile *> left = lane(tile->row());
	if (left.size() > 1 && left.first() == tile) left.at(1)->set_lane_start_us(tile->lane_start_us());
	for (snurra_tile *other : m_tiles)
		if (other != tile && other->row() >= row) other->set_place(other->row() + 1, other->column());
	tile->set_place(row, 0);
	tile->set_lane_start_us(begin);
	m_ghost_row = -1;
	place_tiles();
	timing_changed();
}

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
	/* Ctrl+L: learning without Alt, as the deck view's "learn" button --
	 * where the window manager takes Alt with the pointer (sec 3f). */
	if (m_controller && event->key() == Qt::Key_L && (event->modifiers() & Qt::ControlModifier)) {
		m_controller->set_learning(!m_controller->learning_mode());
		return;
	}
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
	if (from.isNull() || follows_decks()) return;

	/* Let go over a deck view (project.md sec 3f): onto the deck there --
	 * this tile, or with Shift the whole player, the sum of its tiles. */
	for (auto it = m_peer_decks.constBegin(); it != m_peer_decks.constEnd(); ++it) {
		if (!m_peer_frames.value(it.key()).contains(global)) continue;
		const bool whole = m_tiles.size() == 1 || (QGuiApplication::keyboardModifiers() & Qt::ShiftModifier);
		merge_into(it.key(), snurra_wire::RIGHT, whole ? -1 : int(m_tiles.indexOf(tile)), -1, global);
		return;
	}

	/* In a group, a tile dropped outside the window is pulled out there;
	 * by the top or bottom of a tile, it is a lane of its own there; on
	 * the middle of another, the two trade places. */
	show_ghost(nullptr, -1);
	if (m_tiles.size() > 1) {
		snurra_tile *over = nullptr;
		const int row = frameGeometry().contains(global) ? new_lane_at(mapFromGlobal(global), tile, &over) : -1;
		if (!frameGeometry().contains(global)) release(int(m_tiles.indexOf(tile)), global);
		else if (row >= 0) move_to_new_lane(tile, row);
		else if (over && over != tile) swap_tiles(tile, over);
		return;
	}

	/* A lone tile joins where the marker showed: the same answer. */
	const snurra_snap snap = snurra_find_snap(frameGeometry(), m_peer_frames);
	if (!snap.peer.isEmpty()) merge_into(snap.peer, snap.side);
}
