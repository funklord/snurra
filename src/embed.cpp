#include "snurra/embed.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QList>
#include <QPointer>
#include <QSet>

#include "snurra/clock.h"
#include "snurra/deck.h"
#include "snurra/engine.h"
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
