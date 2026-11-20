#include "snurra/embed.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QTimer>
#include <QUuid>
#include <QHash>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QList>
#include <QMimeData>
#include <QPointer>
#include <QSet>
#include <QSlider>
#include <QVBoxLayout>

#include <cmath>

#include "snurra/clock.h"
#include "snurra/deck.h"
#include "snurra/engine.h"
#include "snurra/library.h"
#include "snurra/theme.h"
#include "snurra/subtitles.h"
#include "snurra/tile.h"
#include "snurra/wire.h"
#include "snurra/timeline.h"

namespace {

/* The process's one engine, made on first use and kept while any player
 * holds it (embed.h: the engine is inside). */
std::shared_ptr<snurra_engine> shared_engine() {
	static std::weak_ptr<snurra_engine> one;
	std::shared_ptr<snurra_engine> engine = one.lock();
	if (!engine) {
		engine = std::make_shared<snurra_engine>();
		one = engine;
	}
	return engine;
}

}

class snurra_player_private {
public:
	std::shared_ptr<snurra_engine> engine;
	QHBoxLayout *row = nullptr;
	QList<snurra_tile *> tiles;
};

snurra_player::snurra_player(QWidget *parent) : QWidget(parent), d(std::make_unique<snurra_player_private>()) {
	d->row = new QHBoxLayout(this);
	d->row->setContentsMargins(0, 0, 0, 0);
	d->row->setSpacing(2);
}

/*
 * Every reader of the host's bytes is stopped and joined before the tiles
 * go -- the waveform's by the tile itself -- so nothing calls them once
 * this returns; the decks are retired to the engine, which outlives this
 * only while another player holds it.
 */
snurra_player::~snurra_player() {
	const QList<snurra_tile *> tiles = d->tiles;
	d->tiles.clear();
	for (snurra_tile *tile : tiles) {
		tile->disconnect(this);
		tile->deck()->stop_reading();
		delete tile;
	}
}

bool snurra_player::can_play(const QString &name) {
	static const QSet<QString> kinds{
		/* sound */ QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("ogg"), QStringLiteral("oga"),
		QStringLiteral("opus"), QStringLiteral("wav"), QStringLiteral("aac"), QStringLiteral("m4a"),
		QStringLiteral("ac3"), QStringLiteral("mp2"), QStringLiteral("aif"), QStringLiteral("aiff"),
		QStringLiteral("ape"), QStringLiteral("wv"), QStringLiteral("wma"), QStringLiteral("alac"),
		/* pictures with sound */ QStringLiteral("mp4"), QStringLiteral("m4v"), QStringLiteral("mkv"),
		QStringLiteral("mka"), QStringLiteral("webm"), QStringLiteral("avi"), QStringLiteral("mov"),
		QStringLiteral("mpg"), QStringLiteral("mpeg"), QStringLiteral("ts"), QStringLiteral("flv"),
		QStringLiteral("wmv"), QStringLiteral("ogv"), QStringLiteral("3gp")
	};
	return kinds.contains(QFileInfo(name).suffix().toLower());
}

bool snurra_player::open(const snurra_media &media) {
	if (!media.bytes && media.path.isEmpty()) return false;
	if (!d->engine) d->engine = shared_engine();
	snurra_tile_state state;
	state.source = media.bytes ? media.name : media.path;
	state.playing = true;
	state.controls = true;
	state.sampled_at_us = snurra_clock_us();
	/* The host's bytes serve the waveform too: embed.h has hosts allow
	 * reads from more than one thread. */
	snurra_tile *tile = snurra_tile::embedded(*d->engine, state, media.bytes, this, media.bytes);
	/* The host's subtitle files first; then, where the bytes are a local
	 * file's, the .srt beside that file -- the tile looks beside its
	 * source, which for bytes is only their name (BeerSSH, inbox
	 * 20261113T0834Z: none found on a phone with film.srt beside film). */
	QStringList subtitles = media.subtitles;
	if (media.bytes && !media.path.isEmpty())
		for (const QString &beside : snurra_srt_beside(media.path))
			if (!subtitles.contains(beside)) subtitles << beside;
	tile->set_subtitle_files(subtitles);
	d->tiles.append(tile);
	d->row->addWidget(tile);
	tile->show();
	const QString name = media.name.isEmpty() ? snurra_title_of(media.path) : media.name;
	connect(tile, &snurra_tile::failed, this,
	        [this, name] { emit failed(name, tr("snurra cannot play %1: it is not sound or video it can read.").arg(name)); });
	connect(tile, &snurra_tile::ended, this, [this, name] { emit ended(name); });
	connect(tile, &snurra_tile::close_requested, this, [this](snurra_tile *closing) {
		if (!d->tiles.removeOne(closing)) return;
		closing->deck()->stop_reading();
		closing->hide();
		closing->deleteLater();
		if (d->tiles.isEmpty()) emit emptied();
	});
	return true;
}

int snurra_player::count() const {
	return int(d->tiles.size());
}

qint64 snurra_player::position_ms(int tile) const {
	if (tile < 0 || tile >= d->tiles.size()) return -1;
	const snurra_deck *deck = d->tiles.at(tile)->deck();
	return qint64(snurra_frames_to_us(deck->position_at(snurra_clock_us()), deck->rate()) / 1000);
}

qint64 snurra_player::length_ms(int tile) const {
	if (tile < 0 || tile >= d->tiles.size()) return -1;
	const snurra_deck *deck = d->tiles.at(tile)->deck();
	return deck->length() > 0 ? qint64(snurra_frames_to_us(deck->length(), deck->rate()) / 1000) : -1;
}

snurra_player::play_state snurra_player::state_of(int tile) const {
	if (tile < 0 || tile >= d->tiles.size()) return play_state::none;
	const snurra_tile *shown = d->tiles.at(tile);
	const snurra_deck *deck = shown->deck();
	if (deck->ended()) return play_state::ended;
	if (!shown->playing()) return play_state::paused;
	/* Asked to play, and its last period put out nothing: the bytes stop
	 * there, or it has not begun to sound yet. */
	int64_t frame = 0, at = 0, advanced = 0;
	deck->last_rendered(frame, at, advanced);
	return advanced > 0 ? play_state::playing : play_state::waiting;
}

void snurra_player::close_all() {
	const QList<snurra_tile *> tiles = d->tiles;
	d->tiles.clear();
	for (snurra_tile *tile : tiles) {
		tile->disconnect(this);
		tile->deck()->stop_reading();
		tile->hide();
		tile->deleteLater();
	}
	if (!tiles.isEmpty()) emit emptied();
}

/* ---- the DJ player (embed.h 3) --------------------------------------- */

namespace {

/* A deck's place: what it plays, or a word saying how to give it some;
 * a tune dropped on it loads there. */
class dj_deck : public QFrame {
public:
	dj_deck(const QString &name, QWidget *parent) : QFrame(parent) {
		setFrameShape(QFrame::StyledPanel);
		setAcceptDrops(true);
		setMinimumHeight(fontMetrics().height() * 8);
		column = new QVBoxLayout(this);
		column->setContentsMargins(2, 2, 2, 2);
		empty = new QLabel(QObject::tr("Deck %1 -- double-click a tune below, or drop one here").arg(name), this);
		empty->setAlignment(Qt::AlignCenter);
		empty->setWordWrap(true);
		snurra_mute(empty);
		column->addWidget(empty);
	}
	std::function<void(const QString &path)> on_drop;
	/* A tile of a window dropped here, as the drag carries it: its
	 * process's socket and its index there (snurra_wire::TILE_MIME). */
	std::function<void(const QString &giver, int index)> on_tile_drop;
	QVBoxLayout *column;
	QLabel *empty;
	QPointer<snurra_tile> tile;
	QString name;

protected:
	void dragEnterEvent(QDragEnterEvent *event) override {
		if (snurra_drop_offers(event->mimeData())
		    || (on_tile_drop && event->mimeData()->hasFormat(QLatin1String(snurra_wire::TILE_MIME))))
			event->acceptProposedAction();
	}
	void dropEvent(QDropEvent *event) override {
		if (on_tile_drop && event->mimeData()->hasFormat(QLatin1String(snurra_wire::TILE_MIME))) {
			const QString data = QString::fromUtf8(event->mimeData()->data(QLatin1String(snurra_wire::TILE_MIME)));
			event->acceptProposedAction();
			on_tile_drop(data.section(QLatin1Char('\n'), 0, 0), data.section(QLatin1Char('\n'), 1, 1).toInt());
			return;
		}
		const QStringList files = snurra_dropped(event->mimeData());
		if (files.isEmpty() || !on_drop) return;
		event->acceptProposedAction();
		on_drop(files.first());
	}
};

}

class snurra_dj_private {
public:
	snurra_dj *q = nullptr;
	std::shared_ptr<snurra_engine> engine;

	/* ---- on the wire, as a deck view (project.md sec 3f, step 3) ---- */
	QLocalServer *server = nullptr;
	QString path;
	/* A handover being taken onto a deck: its tile silent and in step
	 * with the giver until COMMIT names the moment to fade in. */
	struct taking_t {
		QPointer<QLocalSocket> socket;
		QPointer<snurra_tile> tile;
		qint64 since = 0;
		QTimer *timer = nullptr;
		bool ready = false;
	};
	QHash<QString, taking_t> taking;
	/* A deck being given to a window: it plays on until READY. */
	struct giving_t {
		QPointer<QLocalSocket> socket;
		int deck = -1;
		QTimer *expiry = nullptr;
	};
	QHash<QString, giving_t> giving;
	qint64 last_started = 0;   /* the last crossfade taken: its start */
	qint64 last_stopped = 0;   /* and its end */

	void install(int deck, snurra_tile *tile, const QString &name);
	void watch(QLocalSocket *socket);
	void send(QLocalSocket *socket, const snurra_wire::message &m);
	void handle(QLocalSocket *socket, const snurra_wire::message &m);
	int deck_at(const QPoint &global) const;
	void take(QLocalSocket *socket, const snurra_wire::message &m);
	void check_ready(const QString &token);
	void fade_in(const QString &token, qint64 start_us, qint64 length_us);
	void end_take(const QString &token);
	void release(int deck, const QPoint &at);
	void offer(QLocalSocket *socket, const QString &token, int deck);
	void complete_give(QLocalSocket *socket, const snurra_wire::message &ready);
	void abort_give(const QString &token);
	/* The deck's tile gone from it, its deck left to the engine's retire. */
	void detach(int deck);

	QList<dj_deck *> decks;   /* A, B and, with four, C and D */
	QGridLayout *grid = nullptr;
	QSlider *crossfader = nullptr;
	QLabel *left_side = nullptr, *right_side = nullptr;   /* which decks each end sends */
	snurra_library *browser = nullptr;
	double position = 0;

	/* Equal power (project.md sec 3b): cos and sin of the quarter circle.
	 * A and C on its left, B and D on its right. */
	void apply_crossfader() {
		const double angle = (position + 1) / 2 * M_PI_2;
		const float gains[2] = { float(std::cos(angle)), float(std::sin(angle)) };
		for (int i = 0; i < decks.size(); ++i)
			if (decks[i]->tile) decks[i]->tile->deck()->set_mix(gains[i % 2]);
	}
	void name_sides() {
		if (!left_side) return;
		const bool four = decks.size() > 2;
		left_side->setText(four ? QStringLiteral("A C") : QStringLiteral("A"));
		right_side->setText(four ? QStringLiteral("B D") : QStringLiteral("B"));
	}
	/* What a deck syncs to: another deck playing, else another loaded. */
	snurra_tile *other_than(int deck) const {
		for (int pass = 0; pass < 2; ++pass)
			for (int i = 0; i < decks.size(); ++i)
				if (i != deck && decks[i]->tile && (pass == 1 || decks[i]->tile->playing())) return decks[i]->tile;
		return nullptr;
	}
	void unload(int i) {
		dj_deck *deck = decks[i];
		if (!deck->tile) return;
		snurra_tile *tile = deck->tile;
		deck->tile = nullptr;
		deck->name.clear();
		tile->disconnect();
		tile->deck()->stop_reading();
		tile->hide();
		tile->deleteLater();
		deck->empty->show();
	}
	/* A double-click's deck: an empty one, else the one not playing, else
	 * the one the crossfader has quieter. */
	int idle_deck() const {
		for (int i = 0; i < decks.size(); ++i)
			if (!decks[i]->tile) return i;
		for (int i = 0; i < decks.size(); ++i)
			if (!decks[i]->tile->playing()) return i;
		return position > 0 ? 0 : 1;
	}
};

/* ---- the deck view on the wire --------------------------------------- */

using namespace snurra_wire::timing;

void snurra_dj_private::watch(QLocalSocket *socket) {
	auto buffer = std::make_shared<QByteArray>();
	QObject::connect(socket, &QLocalSocket::readyRead, q, [this, socket, buffer] {
		buffer->append(socket->readAll());
		for (const snurra_wire::message &m : snurra_wire::decode(*buffer)) handle(socket, m);
	});
	QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
}

void snurra_dj_private::send(QLocalSocket *socket, const snurra_wire::message &m) {
	if (socket && socket->state() == QLocalSocket::ConnectedState) socket->write(snurra_wire::encode(m));
}

void snurra_dj_private::handle(QLocalSocket *socket, const snurra_wire::message &m) {
	switch (m.type) {
	case snurra_wire::WHERE: {
		/* Where it is, and that it is a deck view: how many decks. */
		snurra_wire::message here;
		here.type = snurra_wire::HERE;
		here.frame = q->window()->isVisible() ? q->window()->frameGeometry() : QRect();
		here.path = path;
		here.index = int(decks.size());
		send(socket, here);
		break;
	}
	case snurra_wire::OFFER:
		take(socket, m);
		break;
	case snurra_wire::COMMIT:
		fade_in(m.token, m.time, m.fade);
		break;
	case snurra_wire::ABORT: {
		/* The giver plays on: what was taken goes, never having sounded. */
		const QPointer<snurra_tile> tile = taking.value(m.token).tile;
		end_take(m.token);
		for (int i = 0; i < decks.size(); ++i)
			if (tile && decks[i]->tile == tile) unload(i);
		break;
	}
	case snurra_wire::READY:
		complete_give(socket, m);
		break;
	case snurra_wire::RELEASE:
		/* As a deck's title dragged out to `frame`'s corner. */
		release(m.index, m.frame.topLeft());
		break;
	case snurra_wire::ADOPT: {
		/* The window started to take a deck asks for it by its token. */
		const auto it = giving.find(m.token);
		if (it == giving.end()) break;
		const int deck = it->deck;
		if (it->expiry) it->expiry->deleteLater();
		giving.erase(it);
		offer(socket, m.token, deck);
		break;
	}
	case snurra_wire::STATUS: {
		/* Each deck's tile, its deck as its column; an empty deck none. */
		snurra_wire::message status;
		status.type = snurra_wire::STATUS_IS;
		for (int i = 0; i < decks.size(); ++i)
			if (decks[i]->tile) {
				snurra_tile_state state = decks[i]->tile->state();
				state.row = 0;
				state.column = i;
				status.states << state;
			}
		status.path = path;
		status.index = int(decks.size());
		status.started = last_started;
		status.stopped = last_stopped;
		send(socket, status);
		break;
	}
	default:
		break;
	}
}

int snurra_dj_private::deck_at(const QPoint &global) const {
	for (int i = 0; i < decks.size(); ++i)
		if (decks[i]->isVisible() && QRect(decks[i]->mapToGlobal(QPoint(0, 0)), decks[i]->size()).contains(global)) return i;
	return -1;
}

void snurra_dj_private::take(QLocalSocket *socket, const snurra_wire::message &m) {
	if (m.states.isEmpty()) return;
	if (!engine) engine = shared_engine();
	/* Onto the deck meant, else the one let go over, else an idle one. */
	int deck = m.deck >= 0 && m.deck < decks.size() ? int(m.deck) : -1;
	if (deck < 0 && m.frame.isValid()) deck = deck_at(m.frame.topLeft());
	if (deck < 0) deck = idle_deck();
	/* One tile as it is; several -- a whole player -- as one tile holding
	 * them, standing where the group stands (snurra_player_state). */
	snurra_tile_state state;
	if (m.states.size() == 1) {
		state = m.states.first();
	} else {
		state = snurra_player_state(std::vector<snurra_tile_state>(m.states.begin(), m.states.end()), engine->rate());
	}
	state.row = 0;
	state.column = 0;
	state.controls = true;
	unload(deck);
	snurra_tile *tile = snurra_tile::embedded(*engine, state, nullptr, decks[deck], nullptr, true);
	install(deck, tile, state.player.empty() ? snurra_title_of(state.source) : state.source);

	const QString token = m.token;
	taking_t pending;
	pending.socket = socket;
	pending.tile = tile;
	pending.since = snurra_clock_us();
	pending.timer = new QTimer(q);
	pending.timer->setInterval(2);
	QObject::connect(pending.timer, &QTimer::timeout, q, [this, token] { check_ready(token); });
	pending.timer->start();
	taking.insert(token, pending);
	/* A giver gone without a word: what was taken fades in on its own. */
	if (socket)
		QObject::connect(socket, &QLocalSocket::disconnected, q,
		                 [this, token] { fade_in(token, snurra_clock_us(), CROSSFADE_US); });
	check_ready(token);
}

void snurra_dj_private::check_ready(const QString &token) {
	auto it = taking.find(token);
	if (it == taking.end() || it->ready) return;
	const bool in_step = !it->tile || it->tile->deck()->locked();
	if (!in_step && snurra_clock_us() - it->since < qint64(IN_STEP_WAIT_MS) * 1000) return;
	it->ready = true;
	it->timer->stop();
	QObject::disconnect(it->timer, nullptr, q, nullptr);
	QObject::connect(it->timer, &QTimer::timeout, q, [this, token] { fade_in(token, snurra_clock_us(), CROSSFADE_US); });
	it->timer->setSingleShot(true);
	it->timer->start(COMMIT_WAIT_MS);
	const qint64 period_us = qint64(engine->period()) * 1000000 / engine->rate();
	snurra_wire::message ready;
	ready.type = snurra_wire::READY;
	ready.token = token;
	ready.time = snurra_clock_us() + engine->latency_us() + period_us + COMMIT_MARGIN_US;
	send(it->socket, ready);
}

void snurra_dj_private::fade_in(const QString &token, qint64 start_us, qint64 length_us) {
	const auto it = taking.constFind(token);
	if (it == taking.constEnd()) return;
	if (it->tile) it->tile->deck()->fade(start_us, length_us, 0, 1);
	last_started = start_us;
	last_stopped = start_us + length_us;
	end_take(token);
}

void snurra_dj_private::end_take(const QString &token) {
	const taking_t pending = taking.take(token);
	if (pending.timer) pending.timer->deleteLater();
}

void snurra_dj_private::release(int deck, const QPoint &at) {
	if (deck < 0 || deck >= decks.size() || !decks[deck]->tile) return;
	/* A new window takes it: it starts, connects back and asks for it by
	 * the token (ADOPT); the deck plays on here until it says READY. */
	const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
	giving_t placeholder;
	placeholder.deck = deck;
	placeholder.expiry = new QTimer(q);
	placeholder.expiry->setSingleShot(true);
	QObject::connect(placeholder.expiry, &QTimer::timeout, q, [this, token] { abort_give(token); });
	placeholder.expiry->start(ADOPT_WAIT_MS);
	giving.insert(token, placeholder);
	QProcess::startDetached(QCoreApplication::applicationFilePath(),
	                        { QStringLiteral("--adopt"), path, QStringLiteral("--token"), token, QStringLiteral("--index"),
	                          QString::number(deck), QStringLiteral("--at"), QStringLiteral("%1,%2").arg(at.x()).arg(at.y()) });
}

void snurra_dj_private::offer(QLocalSocket *socket, const QString &token, int deck) {
	if (deck < 0 || deck >= decks.size() || !decks[deck]->tile) return;
	snurra_wire::message m;
	m.type = snurra_wire::OFFER;
	m.token = token;
	snurra_tile_state state = decks[deck]->tile->state();
	state.row = 0;
	state.column = 0;
	m.states << state;
	giving_t give;
	give.socket = socket;
	give.deck = deck;
	give.expiry = new QTimer(q);
	give.expiry->setSingleShot(true);
	QObject::connect(give.expiry, &QTimer::timeout, q, [this, token] { abort_give(token); });
	give.expiry->start(READY_WAIT_MS);
	giving.insert(token, give);
	send(socket, m);
}

void snurra_dj_private::complete_give(QLocalSocket *socket, const snurra_wire::message &ready) {
	const auto it = giving.find(ready.token);
	if (it == giving.end()) return;
	const giving_t give = it.value();
	giving.erase(it);
	if (give.expiry) give.expiry->deleteLater();
	if (give.deck < 0 || give.deck >= decks.size() || !decks[give.deck]->tile) return;
	/* As a window gives (snurra_node::complete_give): out over the same
	 * stretch the taker comes in, the deck sounding on through it. */
	snurra_tile *tile = decks[give.deck]->tile;
	const qint64 period_us = qint64(engine->period()) * 1000000 / engine->rate();
	const qint64 start = qMax(ready.time, snurra_clock_us() + engine->latency_us() + period_us + COMMIT_MARGIN_US);
	if (tile->playing()) {
		tile->deck()->fade(start, CROSSFADE_US, 1, 0);
		tile->retire_deck_at(start + CROSSFADE_US + period_us);
	} else {
		tile->retire_deck_at(0);
	}
	detach(give.deck);
	snurra_wire::message commit;
	commit.type = snurra_wire::COMMIT;
	commit.token = ready.token;
	commit.time = start;
	commit.fade = CROSSFADE_US;
	send(socket, commit);
	if (socket) socket->flush();
}

void snurra_dj_private::abort_give(const QString &token) {
	const auto it = giving.find(token);
	if (it == giving.end()) return;
	const giving_t give = it.value();
	giving.erase(it);
	if (give.expiry) give.expiry->deleteLater();
	snurra_wire::message abort;
	abort.type = snurra_wire::ABORT;
	abort.token = token;
	send(give.socket, abort);
}

void snurra_dj_private::detach(int deck) {
	dj_deck *place = decks[deck];
	if (!place->tile) return;
	snurra_tile *tile = place->tile;
	place->tile = nullptr;
	place->name.clear();
	tile->disconnect();
	tile->hide();
	tile->deleteLater();
	place->empty->show();
}

bool snurra_dj::listen() {
	if (d->server) return true;
	d->path = snurra_wire::runtime_directory()
	          + QStringLiteral("/%1%2").arg(QCoreApplication::applicationPid()).arg(QLatin1String(snurra_wire::DECKS_SUFFIX));
	QLocalServer::removeServer(d->path);
	d->server = new QLocalServer(this);
	d->server->setSocketOptions(QLocalServer::UserAccessOption);
	if (!d->server->listen(d->path)) {
		delete d->server;
		d->server = nullptr;
		return false;
	}
	connect(d->server, &QLocalServer::newConnection, this, [this] {
		while (QLocalSocket *socket = d->server->nextPendingConnection()) d->watch(socket);
	});
	return true;
}

QString snurra_dj::socket_path() const {
	return d->path;
}

snurra_dj::snurra_dj(QWidget *parent) : QWidget(parent), d(std::make_unique<snurra_dj_private>()) {
	d->q = this;
	QVBoxLayout *column = new QVBoxLayout(this);
	d->grid = new QGridLayout;
	column->addLayout(d->grid);
	set_deck_count(2);
	QHBoxLayout *fader = new QHBoxLayout;
	d->crossfader = new QSlider(Qt::Horizontal, this);
	d->crossfader->setObjectName(QStringLiteral("snurra-crossfader"));
	d->crossfader->setAccessibleName(tr("Crossfader"));
	d->crossfader->setRange(-100, 100);
	d->crossfader->setMaximumWidth(fontMetrics().horizontalAdvance(QLatin1Char('M')) * 24);
	fader->addStretch(1);
	d->left_side = new QLabel(this);
	d->right_side = new QLabel(this);
	fader->addWidget(d->left_side);
	fader->addWidget(d->crossfader, 2);
	fader->addWidget(d->right_side);
	d->name_sides();
	fader->addStretch(1);
	column->addLayout(fader);
	d->browser = new snurra_library(QString(), this);
	d->browser->setWindowFlags(Qt::Widget);
	/* Let go over nothing, a tune is not a new player here: the decks are. */
	d->browser->on_dropped_nowhere = nullptr;
	column->addWidget(d->browser, 3);
	connect(d->crossfader, &QSlider::valueChanged, this, [this](int value) {
		d->position = value / 100.0;
		d->apply_crossfader();
	});
	connect(d->browser, &snurra_library::play_requested, this, [this](const QString &path) {
		snurra_media media;
		media.path = path;
		load(d->idle_deck(), media);
	});
}

snurra_dj::~snurra_dj() {
	for (dj_deck *deck : d->decks) {
		if (!deck || !deck->tile) continue;
		snurra_tile *tile = deck->tile;
		tile->disconnect(this);
		tile->deck()->stop_reading();
		delete tile;
	}
}

bool snurra_dj::load(int deck, const snurra_media &media) {
	if (deck < 0 || deck >= d->decks.size() || (!media.bytes && media.path.isEmpty())) return false;
	if (!d->engine) d->engine = shared_engine();
	d->unload(deck);
	dj_deck *place = d->decks[deck];
	snurra_tile_state state;
	state.source = media.bytes ? media.name : media.path;
	/* The stand-in length until the track says its own, as everywhere:
	 * 0 clamped every seek to the start until then. */
	state.duration_us = 180000000;
	state.playing = false;
	state.controls = true;
	state.sampled_at_us = snurra_clock_us();
	snurra_tile *tile = snurra_tile::embedded(*d->engine, state, media.bytes, place, media.bytes);
	QStringList subtitles = media.subtitles;
	if (media.bytes && !media.path.isEmpty())
		for (const QString &beside : snurra_srt_beside(media.path))
			if (!subtitles.contains(beside)) subtitles << beside;
	tile->set_subtitle_files(subtitles);
	d->install(deck, tile, media.name.isEmpty() ? snurra_title_of(media.path) : media.name);
	return true;
}

void snurra_dj_private::install(int deck, snurra_tile *tile, const QString &name) {
	dj_deck *place = decks[deck];
	tile->set_sync_available(true);
	/* A deck is its waveform and its controls, as Traktor's: a film's
	 * picture opened above them in a window of 700 squeezed the controls
	 * and cut them off at the EQ (seen in Xvfb, 2026-11-18). Its button
	 * on the title opens it. */
	tile->set_picture_closed_at_first(true);
	place->tile = tile;
	place->name = name;
	place->empty->hide();
	place->column->addWidget(tile);
	tile->show();
	snurra_dj *dj = q;
	QObject::connect(tile, &snurra_tile::failed, dj, [dj, name] {
		emit dj->failed(name, snurra_dj::tr("snurra cannot play %1: it is not sound or video it can read.").arg(name));
	});
	QObject::connect(tile, &snurra_tile::close_requested, dj, [this, deck] { unload(deck); });
	/* Sync: this deck's beat onto the other's, the phase bent out. */
	QObject::connect(tile, &snurra_tile::sync_requested, dj, [this, deck](snurra_tile *asking) {
		snurra_tile *other = other_than(deck);
		const qint64 delta_us = asking->match_beat_of(other);
		if (delta_us != 0) asking->deck()->bend(snurra_us_to_frames(delta_us, asking->deck()->rate()));
	});
	QObject::connect(tile, &snurra_tile::key_sync_requested, dj,
	                 [this, deck](snurra_tile *asking) { asking->match_key_of(other_than(deck)); });
	/* Pulled out of the window by its title: a window of its own takes it
	 * there (project.md sec 3f), where this deck view is on the wire. */
	QObject::connect(tile, &snurra_tile::handle_released, dj, [this, deck](snurra_tile *, const QPoint &global) {
		if (server && !q->window()->frameGeometry().contains(global)) release(deck, global);
	});
	apply_crossfader();
}

QString snurra_dj::loaded(int deck) const {
	return deck >= 0 && deck < d->decks.size() ? d->decks[deck]->name : QString();
}

void snurra_dj::set_deck_count(int count) {
	count = count >= 4 ? 4 : 2;
	while (d->decks.size() > count) {
		d->unload(int(d->decks.size()) - 1);
		delete d->decks.takeLast();
	}
	while (d->decks.size() < count) {
		const int i = int(d->decks.size());
		const QChar letter(QLatin1Char(char('A' + i)));
		dj_deck *deck = new dj_deck(QString(letter), this);
		deck->setObjectName(QStringLiteral("snurra-deck-%1").arg(QChar(QLatin1Char(char('a' + i)))));
		deck->on_drop = [this, i](const QString &path) {
			snurra_media media;
			media.path = path;
			load(i, media);
		};
		/* The giver is asked, as a window asks it (snurra_node::dropEvent):
		 * the handover is its to make -- the tile, or with Shift the whole
		 * player -- onto this deck. */
		deck->on_tile_drop = [this, i](const QString &giver, int index) {
			if (!d->server || giver.isEmpty()) return;
			snurra_wire::message merge;
			merge.type = snurra_wire::MERGE_INTO;
			merge.path = d->path;
			merge.index = (QGuiApplication::keyboardModifiers() & Qt::ShiftModifier) ? -1 : index;
			merge.deck = i;
			QLocalSocket *socket = new QLocalSocket(this);
			connect(socket, &QLocalSocket::connected, this, [socket, merge] {
				socket->write(snurra_wire::encode(merge));
				socket->disconnectFromServer();
			});
			connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
			connect(socket, &QLocalSocket::errorOccurred, socket, &QObject::deleteLater);
			socket->connectToServer(giver);
		};
		/* Two to a row, A and B above C and D, as Traktor's four. */
		d->grid->addWidget(deck, i / 2, i % 2);
		d->decks << deck;
	}
	for (int row = 0; row < 2; ++row) d->grid->setRowStretch(row, row < (count + 1) / 2 ? 1 : 0);
	d->name_sides();
	d->apply_crossfader();
}

int snurra_dj::deck_count() const {
	return int(d->decks.size());
}

void snurra_dj::set_crossfader(double position) {
	d->crossfader->setValue(int(std::lround(qBound(-1.0, position, 1.0) * 100)));
	d->position = qBound(-1.0, position, 1.0);
	d->apply_crossfader();
}

double snurra_dj::crossfader() const {
	return d->position;
}

void snurra_dj::set_browser_visible(bool visible) {
	d->browser->setVisible(visible);
}

void snurra_dj::set_browser_folder(const QString &folder) {
	d->browser->set_folder(folder);
}
