#include "snurra/embed.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QFrame>
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
	const QString name = media.name.isEmpty() ? QFileInfo(media.path).fileName() : media.name;
	connect(tile, &snurra_tile::failed, this,
	        [this, name] { emit failed(name, tr("snurra cannot play %1: it is not sound or video it can read.").arg(name)); });
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
	QVBoxLayout *column;
	QLabel *empty;
	QPointer<snurra_tile> tile;
	QString name;

protected:
	void dragEnterEvent(QDragEnterEvent *event) override {
		if (snurra_drop_offers(event->mimeData())) event->acceptProposedAction();
	}
	void dropEvent(QDropEvent *event) override {
		const QStringList files = snurra_dropped(event->mimeData());
		if (files.isEmpty() || !on_drop) return;
		event->acceptProposedAction();
		on_drop(files.first());
	}
};

}

class snurra_dj_private {
public:
	std::shared_ptr<snurra_engine> engine;
	dj_deck *decks[2] = { nullptr, nullptr };
	QSlider *crossfader = nullptr;
	snurra_library *browser = nullptr;
	double position = 0;

	/* Equal power (project.md sec 3b): cos and sin of the quarter circle. */
	void apply_crossfader() {
		const double angle = (position + 1) / 2 * M_PI_2;
		const float gains[2] = { float(std::cos(angle)), float(std::sin(angle)) };
		for (int i = 0; i < 2; ++i)
			if (decks[i]->tile) decks[i]->tile->deck()->fade(0, 0, gains[i], gains[i]);
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
		for (int i = 0; i < 2; ++i)
			if (!decks[i]->tile) return i;
		for (int i = 0; i < 2; ++i)
			if (!decks[i]->tile->playing()) return i;
		return position > 0 ? 0 : 1;
	}
};

snurra_dj::snurra_dj(QWidget *parent) : QWidget(parent), d(std::make_unique<snurra_dj_private>()) {
	QVBoxLayout *column = new QVBoxLayout(this);
	QHBoxLayout *decks = new QHBoxLayout;
	for (int i = 0; i < 2; ++i) {
		d->decks[i] = new dj_deck(i == 0 ? QStringLiteral("A") : QStringLiteral("B"), this);
		d->decks[i]->setObjectName(i == 0 ? QStringLiteral("snurra-deck-a") : QStringLiteral("snurra-deck-b"));
		d->decks[i]->on_drop = [this, i](const QString &path) {
			snurra_media media;
			media.path = path;
			load(i, media);
		};
		decks->addWidget(d->decks[i], 1);
	}
	column->addLayout(decks);
	QHBoxLayout *fader = new QHBoxLayout;
	d->crossfader = new QSlider(Qt::Horizontal, this);
	d->crossfader->setObjectName(QStringLiteral("snurra-crossfader"));
	d->crossfader->setAccessibleName(tr("Crossfader"));
	d->crossfader->setRange(-100, 100);
	d->crossfader->setMaximumWidth(fontMetrics().horizontalAdvance(QLatin1Char('M')) * 24);
	fader->addStretch(1);
	fader->addWidget(new QLabel(QStringLiteral("A"), this));
	fader->addWidget(d->crossfader, 2);
	fader->addWidget(new QLabel(QStringLiteral("B"), this));
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
	if (deck < 0 || deck > 1 || (!media.bytes && media.path.isEmpty())) return false;
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
	tile->set_sync_available(true);
	place->tile = tile;
	place->name = media.name.isEmpty() ? QFileInfo(media.path).fileName() : media.name;
	place->empty->hide();
	place->column->addWidget(tile);
	tile->show();
	const QString name = place->name;
	connect(tile, &snurra_tile::failed, this,
	        [this, name] { emit failed(name, tr("snurra cannot play %1: it is not sound or video it can read.").arg(name)); });
	connect(tile, &snurra_tile::close_requested, this, [this, deck] { d->unload(deck); });
	/* Sync: this deck's beat onto the other's, the phase bent out. */
	connect(tile, &snurra_tile::sync_requested, this, [this, deck](snurra_tile *asking) {
		snurra_tile *other = d->decks[1 - deck]->tile;
		const qint64 delta_us = asking->match_beat_of(other);
		if (delta_us != 0) asking->deck()->bend(snurra_us_to_frames(delta_us, asking->deck()->rate()));
	});
	d->apply_crossfader();
	return true;
}

QString snurra_dj::loaded(int deck) const {
	return deck >= 0 && deck < 2 ? d->decks[deck]->name : QString();
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
