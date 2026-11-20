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

/* 1: snurra_media, snurra_player. 2: snurra_media::subtitles. 3: snurra_dj.
 * 4: snurra_player's position_ms, length_ms, state_of and ended(). */
#define SNURRA_EMBED_VERSION 5

/* One thing to play. */
struct snurra_media {
	QString name;                          /* shown as the title; its extension hints at its kind */
	QString path;                          /* a local file, when there is one */
	std::shared_ptr<snurra_bytes> bytes;   /* the bytes, still arriving or not; used over `path` when set */
	/* (2) Subtitle files the host has fetched -- a .srt beside a remote
	 * file, copied over -- as local paths, offered first; then those
	 * beside `path`, with bytes or without. Empty: those beside `path`
	 * and what the file carries. */
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

	/*
	 * (4) A tile, by its place among those open (0 the first opened still
	 * open), for a host's own words beside it (fuzzypickles, inbox
	 * 20261114T1128Z): where it is heard, in ms; its length once known,
	 * -1 until then; and whether it plays, was paused, is waiting -- asked
	 * to play and holding silent where the bytes stop, as a file still
	 * arriving does -- or played to its end. -1, and `none`, for no such
	 * tile.
	 */
	enum class play_state { none, playing, paused, waiting, ended };
	qint64 position_ms(int tile) const;
	qint64 length_ms(int tile) const;
	play_state state_of(int tile) const;

signals:
	/* The last tile closed, by the person or by close_all(). */
	void emptied();
	/* (4) `name`'s tile played to its end: its bytes may be let go, as a
	 * close would. The tile stays, paused at the end. */
	void ended(const QString &name);
	/* `name` cannot be played, and why, in words for the person. */
	void failed(const QString &name, const QString &why);

private:
	std::unique_ptr<snurra_player_private> d;
};

/*
 * (3) A simple DJ player -- a simplified Traktor (the holder, 2026-11-13):
 * two decks side by side, A and B, each a tile with its controls, its
 * jumps of bars and its sync to the other; an equal-power crossfader
 * between them; and under them a browser of the music in a folder, whose
 * tune double-clicked loads onto the deck not playing, or dragged, onto
 * the deck it is let go on. One layout, decks above and browser below,
 * for a desktop and for a phone's one static screen alike. A host that
 * browses its own (BeerSSH's remote files) hides the browser and loads.
 */
class snurra_dj_private;

class snurra_dj : public QWidget {
	Q_OBJECT

public:
	explicit snurra_dj(QWidget *parent = nullptr);
	/* Stops at once and cleanly, as snurra_player does. */
	~snurra_dj() override;

	/* `media` onto deck 0 (A) or 1 (B) -- or, with four, 2 (C) or 3 (D) --
	 * paused, in place of what it had. False when there is nothing to
	 * read, or no such deck. */
	bool load(int deck, const snurra_media &media);
	/* What each deck has, empty where nothing. */
	QString loaded(int deck) const;

	/* (5) Two decks or four (project.md sec 3f), A and B above C and D;
	 * A and C on the crossfader's left, B and D on its right. Two by
	 * default. Taking away C and D unloads them. */
	void set_deck_count(int count);
	int deck_count() const;
	/* (5) Where the decks sit: letters in order, "/" between rows ("C A B D"
	 * in a row, as many four-deck controllers lay them out); empty for the
	 * default, two to a row. Kept as the person's choice; also on a
	 * deck's right-click menu. */
	void arrange(const QString &arrangement);

	/* (5) On the wire as a deck view (project.md sec 3f): snurra's own
	 * windows of tiles hand a tile -- or with Shift a whole player -- onto
	 * the deck it is let go over, gapless, and a deck's title dragged out
	 * of this window opens it in a window of its own. The player's; a
	 * host need not. False where the socket cannot be made. */
	bool listen();
	QString socket_path() const;

	/* (5) The player deck `deck` holds -- a whole player handed onto it --
	 * unfolded across the decks (project.md sec 3f): each of its tracks on
	 * its channel's deck, started at its moment, its crossfades and
	 * envelope moving that deck, the next one cued; the decks' play and
	 * seek are the player's, and a hand on a deck's fader takes that deck
	 * from the automation while held. Two decks become four where it needs
	 * them. False where the deck holds no player, or one needing more than
	 * four decks at once. A tune loaded by hand ends it. */
	bool unfold(int deck);
	/* (5) And back: the decks of an unfolded player gathered onto deck A
	 * as one player again, as edited, where it stands. False where no
	 * player is unfolded. */
	bool fold();
	/* (5) The track cued on `deck` of an unfolded player in now, by hand
	 * -- Shift with the deck's play -- and where the train can say it,
	 * where it comes in is written into the player. False where nothing
	 * is cued there. */
	bool start_now(int deck);

	/* (5) Controllers (project.md sec 3f): MIDI in, the decks' controls
	 * named ch_1_play ... ch_4_eq_high and crossfader, bound as the
	 * person chose (the settings' `controllers/`), and a binding learned
	 * by holding Alt and the pointer on a control and moving the hardware
	 * control meant. False where there is no MIDI (not Linux, no
	 * sequencer). */
	bool take_controllers();

	/* (5) Pre-listen as a plug (project.md sec 3f): a listener is headphones
	 * on an output of their own (as --output names one), plugged into one
	 * deck at a time and hearing it before its fader and the crossfader.
	 * Its number back; each deck shows "listen", which moves the first
	 * listener's plug there. plug(listener, -1) unplugs. */
	int add_listener(const QString &output);
	void plug(int listener, int deck);
	int plugged(int listener) const;
	/* Which listener a deck's "listen" moves -- chosen on screen by the
	 * crossfader where there are two or more. */
	void choose_listener(int listener);

	/* -1, A alone; 1, B alone; 0, both at equal power (-3 dB each). */
	void set_crossfader(double position);
	double crossfader() const;

	/* The browser under the decks: shown by default, on the desktop's
	 * music folder or one chosen in it. */
	void set_browser_visible(bool visible);
	void set_browser_folder(const QString &folder);

signals:
	/* `name` cannot be played, and why, in words for the person. */
	void failed(const QString &name, const QString &why);

private:
	std::unique_ptr<snurra_dj_private> d;
};

#endif
