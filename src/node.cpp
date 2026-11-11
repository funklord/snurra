#include "node.h"

#include <QCoreApplication>
#include <QDateTime>
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

#include "tile.h"

namespace {

/* How long a giver waits for READY: a peer already running answers in
 * milliseconds; a process started to adopt a tile needs to start first. */
constexpr int READY_WAIT_MS = 2000;
constexpr int ADOPT_WAIT_MS = 8000;

/* How near two windows' edges must come to snap. */
constexpr int SNAP_PX = 24;

QString new_token() {
	return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool can_place_windows() {
	/* X11 lets a program move its own windows and read others'; Wayland
	 * does neither, so there a drag is the compositor's (project.md 3b). */
	return QGuiApplication::platformName() == QLatin1String("xcb")
	       || QGuiApplication::platformName() == QLatin1String("offscreen");
}

}

snurra_node::snurra_node(QWidget *parent) : QWidget(parent) {
	setWindowFlag(Qt::FramelessWindowHint);
	setWindowTitle(QStringLiteral("snurra"));
	QVBoxLayout *outer = new QVBoxLayout(this);
	outer->setContentsMargins(2, 2, 2, 2);
	outer->setSpacing(0);
	m_grid = new QGridLayout;
	m_grid->setSpacing(2);
	outer->addLayout(m_grid, 1);
	/* Resizeable, though frameless: the grip in the corner. */
	QSizeGrip *grip = new QSizeGrip(this);
	outer->addWidget(grip, 0, Qt::AlignRight | Qt::AlignBottom);
}

snurra_node::~snurra_node() {
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

void snurra_node::add_tile(const snurra_tile_state &state) {
	snurra_tile *tile = new snurra_tile(state, this);
	connect(tile, &snurra_tile::handle_moved, this, &snurra_node::handle_moved);
	connect(tile, &snurra_tile::handle_released, this, &snurra_node::handle_released);
	connect(tile, &snurra_tile::close_requested, this, [this](snurra_tile *closing) {
		remove_tile(closing);
		leave_if_empty();
	});
	m_tiles.append(tile);
	place_tiles();
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

	setUpdatesEnabled(false);
	for (snurra_tile *tile : m_tiles) m_grid->removeWidget(tile);
	for (snurra_tile *tile : m_tiles) {
		tile->set_place(rows.value(tile->row()), columns.value(tile->column()));
		m_grid->addWidget(tile, tile->row(), tile->column());
		tile->show();
	}
	setUpdatesEnabled(true);
	adjustSize();
}

void snurra_node::remove_tile(snurra_tile *tile) {
	if (!m_tiles.removeOne(tile)) return;
	m_grid->removeWidget(tile);
	tile->hide();
	tile->deleteLater();
	place_tiles();
}

void snurra_node::leave_if_empty() {
	if (!m_tiles.isEmpty() || !m_giving.isEmpty() || !m_adopt_token.isEmpty()) return;
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

void snurra_node::merge_into(const QString &peer, snurra_wire::side_t side) {
	if (m_tiles.isEmpty()) return;
	QLocalSocket *socket = new QLocalSocket(this);
	watch(socket);
	const QString token = new_token();
	const QList<snurra_tile *> tiles = m_tiles;
	connect(socket, &QLocalSocket::connected, this, [this, socket, token, side, tiles] {
		offer(socket, token, side, tiles);
	});
	connect(socket, &QLocalSocket::errorOccurred, this, [socket] { socket->deleteLater(); });
	socket->connectToServer(peer);
}

void snurra_node::complete_give(QLocalSocket *socket, const QString &token) {
	const auto it = m_giving.find(token);
	if (it == m_giving.end()) return;
	const pending_give give = it.value();
	m_giving.erase(it);
	if (give.expiry) give.expiry->deleteLater();

	/* The taker is playing: stop ours now, and say when. */
	for (const QPointer<snurra_tile> &tile : give.tiles) {
		if (!tile) continue;
		tile->set_playing(false);
		remove_tile(tile);
	}
	snurra_wire::message commit;
	commit.type = snurra_wire::COMMIT;
	commit.token = token;
	commit.time = QDateTime::currentMSecsSinceEpoch();
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
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	QList<QPointer<snurra_tile>> taken;
	setUpdatesEnabled(false);
	for (snurra_tile_state state : m.states) {
		/* Carried across the handover's own time, so nothing is replayed. */
		state.position_ms = state.position_now(now);
		state.sampled_at = now;
		if (!m_tiles.isEmpty()) {
			if (m.side == snurra_wire::RIGHT) state.column += last_column + 1;
			else state.row += last_row + 1;
		}
		add_tile(state);
		taken << m_tiles.last();
	}
	setUpdatesEnabled(true);
	m_taking.insert(m.token, taken);
	m_last_started = now;

	if (!m_adopt_token.isEmpty() && m.token == m_adopt_token) {
		m_adopt_token.clear();
		if (!m_adopt_at.isNull() && can_place_windows()) move(m_adopt_at);
	}
	show();

	snurra_wire::message ready;
	ready.type = snurra_wire::READY;
	ready.token = m.token;
	ready.time = now;
	send(socket, ready);
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
		complete_give(socket, m.token);
		break;
	case snurra_wire::COMMIT:
		m_last_stopped = m.time;
		m_taking.remove(m.token);
		break;
	case snurra_wire::ABORT: {
		/* The giver plays on: what was taken goes. */
		for (const QPointer<snurra_tile> &tile : m_taking.take(m.token))
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
		merge_into(m.path, m.side);
		break;
	case snurra_wire::RELEASE:
		release(m.index, m.frame.topLeft());
		break;
	case snurra_wire::SHOW_CONTROLS:
		if (m.index >= 0 && m.index < m_tiles.size()) m_tiles.at(m.index)->set_controls(m.time != 0);
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

void snurra_node::handle_moved(snurra_tile *tile, const QPoint &global) {
	Q_UNUSED(tile);
	if (m_drag_from.isNull()) {
		m_drag_from = global;
		m_window_from = pos();
		m_system_move_started = false;
		ask_where();
		return;
	}
	/* A lone tile, or the whole group: the window moves with the hand. */
	if (m_tiles.size() > 1) return;
	if (can_place_windows()) {
		move(m_window_from + (global - m_drag_from));
	} else if (!m_system_move_started && windowHandle()) {
		m_system_move_started = windowHandle()->startSystemMove();
	}
}

void snurra_node::handle_released(snurra_tile *tile, const QPoint &global) {
	const QPoint from = m_drag_from;
	m_drag_from = QPoint();
	if (from.isNull()) return;

	/* In a group, a tile dropped outside the window is pulled out there. */
	if (m_tiles.size() > 1) {
		if (!frameGeometry().contains(global)) release(int(m_tiles.indexOf(tile)), global);
		return;
	}

	/* A lone tile let go near another window's edge joins it: beside its
	 * right edge a train, under its bottom edge a stack. */
	const QRect mine = frameGeometry();
	for (auto it = m_peer_frames.cbegin(); it != m_peer_frames.cend(); ++it) {
		const QRect theirs = it.value();
		if (theirs.isNull()) continue;
		const bool beside = qAbs(mine.left() - theirs.right()) <= SNAP_PX
		                    && mine.bottom() > theirs.top() && mine.top() < theirs.bottom();
		const bool under = qAbs(mine.top() - theirs.bottom()) <= SNAP_PX
		                   && mine.right() > theirs.left() && mine.left() < theirs.right();
		if (beside || under) {
			merge_into(it.key(), beside ? snurra_wire::RIGHT : snurra_wire::BELOW);
			return;
		}
	}
}
