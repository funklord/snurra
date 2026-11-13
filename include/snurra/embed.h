#ifndef SNURRA_EMBED_H
#define SNURRA_EMBED_H

/*
 * The whole of snurra a host relies on (project.md sec 4, BeerSSH's
 * proposal): this header and snurra_bytes in snurra/source.h. Everything
 * else in snurra is snurra's to change without notice; this is not.
 *
 * Additive only. A new member, signal or snurra_media field may come at
 * any release, with SNURRA_EMBED_VERSION raised; nothing here is ever
 * removed or renamed, nor given another meaning. A host tests
 * `SNURRA_EMBED_VERSION >= n` for what came at n.
 *
 * What a host may count on beyond the signatures:
 *
 *  - The engine is inside: every player in a process shares the one
 *    engine and output, made on first use. A host never sees it.
 *  - Bytes stall, never fail: open() returns as soon as the tile exists;
 *    where the bytes have nothing yet the tile waits, silent, and goes on
 *    from the next frame when they come (source.h's contract).
 *  - A host's snurra_bytes may be called from more than one snurra thread
 *    at once -- the sound's reader and the waveform's -- so it must allow
 *    that.
 *  - The player may be deleted at any moment, playing or not. Its
 *    destructor stops every reader of the host's bytes and waits for them:
 *    once it returns, nothing in snurra calls them again.
 *  - Placement is the host's: the widget takes the room it is given.
 *    Several open() calls make a row of tiles inside it -- snurra's layout.
 *
 * Main thread only.
 */

#include <QString>
#include <QStringList>
#include <QWidget>

#include <memory>

#include "snurra/source.h"

/* 1: snurra_media, snurra_player. 2: snurra_media::subtitles. */
#define SNURRA_EMBED_VERSION 2

/* One thing to play. */
struct snurra_media {
	QString name;                          /* shown as the title; its extension hints at its kind */
	QString path;                          /* a local file, when there is one */
	std::shared_ptr<snurra_bytes> bytes;   /* the bytes, still arriving or not; used over `path` when set */
	/* (2) Subtitle files the host has fetched -- a .srt beside a remote
	 * file, copied over -- as local paths, offered first, as a .srt beside
	 * a local file is. Empty: none but what the file carries. */
	QStringList subtitles;
	/* Later fields are added here, at the end, with defaults that keep today's meaning. */
};

class snurra_player_private;

class snurra_player : public QWidget {
	Q_OBJECT

public:
	explicit snurra_player(QWidget *parent = nullptr);
	/* Stops at once and cleanly, wherever it is: see above. */
	~snurra_player() override;

	/* Whether snurra would try `name`, by its name alone, before any byte. */
	static bool can_play(const QString &name);

	/* A tile for `media`, beside this player's others, playing. False only
	 * when it cannot even begin -- nothing to read; a file that turns out
	 * unplayable says so later, through failed(). */
	bool open(const snurra_media &media);

	/* Tiles open now. */
	int count() const;

	/* Every tile closed; emptied() follows. */
	void close_all();

signals:
	/* The last tile closed, by the person or by close_all(). */
	void emptied();
	/* `name` cannot be played, and why, in words for the person. */
	void failed(const QString &name, const QString &why);

private:
	std::unique_ptr<snurra_player_private> d;
};

#endif
