#include "snurra/embed.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QTimer>
#include <QUuid>
#include <QHash>
#include <QComboBox>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QToolTip>
#include <QCursor>
#include <QMouseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QList>
#include <QMenu>
#include <QPainter>
#include <QMimeData>
#include <QPointer>
#include <QSet>
#include <QSettings>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

#include "snurra/beats.h"
#include "snurra/clock.h"
#include "snurra/deck.h"
#include "snurra/engine.h"
#include "snurra/library.h"
#include "snurra/midi.h"
#include "snurra/palette.h"
#include "snurra/subtitles.h"
#include "snurra/tile.h"
#include "snurra/deck_waves.h"
#include "snurra/peaks.h"
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
		/* Its side of the crossfader: left, right or thru. */
		side = new QToolButton(this);
		side->setAccessibleName(QObject::tr("Crossfader side"));
		side->setToolTip(QObject::tr("Which side of the crossfader this deck is on, or thru, out of its reach"));
		side->setAutoRaise(true);
		column->addWidget(side, 0, Qt::AlignRight);
		/* The plug's socket here: a listener's headphones hear this deck. */
		listen = new QToolButton(this);
		listen->setText(QObject::tr("listen"));
		listen->setAccessibleName(QObject::tr("Listen here"));
		listen->setToolTip(QObject::tr("Your headphones hear this deck, before its fader and the crossfader"));
		listen->setCheckable(true);
		listen->setAutoRaise(true);
		listen->hide();
		column->addWidget(listen, 0, Qt::AlignRight);
		/* A deck in a player unfolded: gather it back into one deck. */
		fold = new QToolButton(this);
		fold->setText(QObject::tr("Fold back into one deck"));
		fold->setAccessibleName(fold->text());
		fold->setAutoRaise(true);
		fold->hide();
		column->addWidget(fold, 0, Qt::AlignLeft);
		/* A deck holding a whole player: spread it over the decks. */
		unfold = new QToolButton(this);
		unfold->setText(QObject::tr("Unfold across the decks"));
		unfold->setAccessibleName(unfold->text());
		unfold->setAutoRaise(true);
		unfold->hide();
		column->addWidget(unfold, 0, Qt::AlignLeft);
		/* The way back (sec 3f): what the deck holds into a window of its
		 * own -- an unfolded player gathered first. */
		out = new QToolButton(this);
		out->setObjectName(QStringLiteral("snurra-deck-out"));
		out->setText(QObject::tr("Into a window"));
		out->setAccessibleName(out->text());
		out->setToolTip(QObject::tr("What this deck plays, into a player window of its own -- a player spread over the decks gathered first"));
		out->setAutoRaise(true);
		out->hide();
		column->addWidget(out, 0, Qt::AlignLeft);
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
	QToolButton *side;
	QToolButton *listen;
	QMenu *listen_menu = nullptr;   /* a player's tracks, while it holds one */
	QToolButton *fold;
	QToolButton *unfold;
	QToolButton *out;
	QLabel *empty;
	QPointer<snurra_tile> tile;
	QString name;
	/* Its scrolling waveform's peaks (snurra_deck_waves), read for the
	 * track in `fine_of`. */
	std::unique_ptr<snurra_peaks> fine;
	QString fine_of;

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

/*
 * The player the decks play, seen as its timeline while it is unfolded
 * (sec 3f: "both show the one timeline"): a row a channel -- a deck --
 * each track a bar from where it comes in to where it ends, named, the
 * ones the decks sound now filled, and a line where the player is. So
 * the prepared mix's shape -- what comes next, on which deck, when -- is
 * there while the decks play it. Asked what to draw at each paint.
 */
class snurra_dj_set_view : public QWidget {
public:
	struct track {
		int channel = 0;
		int64_t start = 0, end = 0;
		QString name;
		bool sounding = false;
	};
	std::function<void(std::vector<track> &, int64_t &now)> fill;
	/* Pressed: the player to there, a share of its length -- to hear a
	 * crossing again, or go on to the next. */
	std::function<void(double share)> on_seek;

	explicit snurra_dj_set_view(QWidget *parent) : QWidget(parent) {
		setObjectName(QStringLiteral("snurra-set-view"));
		setAccessibleName(QObject::tr("The player on the decks"));
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		m_tick.setInterval(100);
		QObject::connect(&m_tick, &QTimer::timeout, this, [this] { update(); });
	}
	void set_channels(int channels) {
		const int line = fontMetrics().height() + 2;
		setFixedHeight(std::max(1, channels) * line + 2);
	}

protected:
	void mousePressEvent(QMouseEvent *event) override {
		if (event->button() != Qt::LeftButton || !on_seek || width() <= 2) return;
		on_seek(std::clamp((event->position().x() - 1) / (width() - 2), 0.0, 1.0));
	}
	void showEvent(QShowEvent *) override { m_tick.start(); }
	void hideEvent(QHideEvent *) override { m_tick.stop(); }
	void paintEvent(QPaintEvent *) override {
		std::vector<track> tracks;
		int64_t now = -1;
		if (fill) fill(tracks, now);
		QPainter painter(this);
		painter.fillRect(rect(), palette().color(QPalette::Base));
		int64_t length = 1;
		for (const track &one : tracks) length = std::max(length, one.end);
		const int line = fontMetrics().height() + 2;
		auto x_of = [&](int64_t at) { return double(at) / double(length) * (width() - 2) + 1; };
		QColor ahead = palette().color(QPalette::Highlight);
		ahead.setAlphaF(0.3f);
		for (const track &one : tracks) {
			const QRectF bar(x_of(one.start), 1 + one.channel * line, std::max(2.0, x_of(one.end) - x_of(one.start)), line - 2);
			painter.fillRect(bar, one.sounding ? palette().color(QPalette::Highlight) : ahead);
			painter.setPen(palette().color(one.sounding ? QPalette::HighlightedText : QPalette::Text));
			painter.drawText(bar.adjusted(3, 0, -2, 0), Qt::AlignLeft | Qt::AlignVCenter,
			                 fontMetrics().elidedText(one.name, Qt::ElideRight, int(bar.width()) - 5));
		}
		setProperty("tracks", int(tracks.size()));
		if (now >= 0) {
			const double x = x_of(now);
			painter.setPen(QPen(palette().color(QPalette::Text), 2));
			painter.drawLine(QPointF(x, 0), QPointF(x, height()));
			setProperty("now_x", x);
		}
	}

private:
	QTimer m_tick;
};

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
	QString adopted;   /* the token of a player asked for by adopt(): unfolded when taken */
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

	/* ---- a player unfolded across the decks (sec 3f, step 4) ---- */
	struct score_t {
		std::vector<snurra_tile_state> states;   /* timeline order */
		snurra_timeline timeline{ 48000 };
		std::vector<int> channel;                /* by track: its deck */
		std::vector<int64_t> start, end, in, out;   /* frames of the player's time */
		int64_t time0 = 0;                       /* the player's time at t0_us */
		qint64 t0_us = 0;
		bool playing = false;
		std::vector<int> on_deck;                /* by deck: the track it holds, -1 */
		std::vector<bool> started;               /* by track */
		std::vector<int> stage;                  /* by track: 0 its fade in set, 1 its fade out */
		std::vector<bool> held;                  /* by deck: a hand on its fader */
		/* By deck: a fader ridden while held -- the level the hand found,
		 * and (track frame, gain) as it moved -- written into the track's
		 * envelope when let go (step 5). */
		struct ride_t {
			int track = -1;
			int level = 100;
			std::vector<std::pair<int64_t, float>> points;
		};
		std::vector<ride_t> rides;
		QTimer *timer = nullptr;
	};
	std::unique_ptr<score_t> score;
	snurra_dj_set_view *set_view = nullptr;
	/* The player also in a window, following it (sec 3f): beside the
	 * strip, where a window can be started. */
	QToolButton *set_window = nullptr;
	void show_set(bool shown) {
		set_view->setVisible(shown);
#ifdef Q_OS_ANDROID
		set_window->setVisible(false);
#else
		set_window->setVisible(shown && server != nullptr);
#endif
	}

	/* ---- listeners (sec 3f, step 7) ---- */
	/* A listener: headphones of their own -- an engine on another output
	 * -- and a plug in one deck at a time, heard there as a mirror of that
	 * deck, before its fader and the crossfader. */
	struct listener_t {
		std::unique_ptr<snurra_engine> engine;
		snurra_deck *mirror = nullptr;
		int deck = -1;
		/* Or one track of the player that deck plays (sec 3f, "a nested
		 * player inside a deck"): -1 the deck itself. */
		int track = -1;
		QPointer<snurra_tile> tapped;
		bool playing = false;
		float tempo = 1;
		/* Or a tile of another process's (a window's): followed by asking
		 * its status, its state the mirror's guide. */
		QString remote;
		int remote_index = -1;
		QPointer<QLocalSocket> socket;
		std::shared_ptr<QByteArray> buffer;
		snurra_tile_state state;
		float trim = 1;
	};
	std::vector<std::unique_ptr<listener_t>> listeners;
	int chosen_listener = 0;     /* the one a "listen here" moves */
	QTimer *follow = nullptr;
	void plug(int listener, int deck, int track = -1);
	/* Where the tapped track is, in its own frames, when its deck is
	 * heard at `us`; false when the plug is in no track. */
	bool track_frame(const listener_t &one, qint64 us, double &frame, double &tempo) const;
	void plug_remote(int listener, const QString &path, int index);
	void follow_remote(listener_t &one, const snurra_tile_state &state);
	void mirror(listener_t &one);
	void unmirror(listener_t &one);
	void anchor(listener_t &one);
	void follow_taps();
	void show_plugs();

	/* ---- controllers (sec 3f, step 6) ---- */
	snurra_controller *controller = nullptr;
	void name_controls(int deck);
	void control_widget(QWidget *widget, const QString &name) {
		if (controller) controller->name_widget(widget, name);
	}
	void apply(const QString &control, snurra_control_kind kind, const snurra_midi_event &event);
	double light_value(const QString &control) const;
	bool unfold(int deck);
	bool fold();
	void to_window(int deck, const QPoint &at);
	void end_score();
	int64_t score_time(qint64 us) const;
	qint64 score_us(int64_t time) const;
	int64_t frame_of(int track, int64_t time) const;
	void place(int64_t time, qint64 at_us);
	void seek_score(int64_t time);
	void play_score(bool playing);
	void take_change(const QList<snurra_tile_state> &states);
	void load_track(int track, int64_t frame);
	void keep_moves(int deck);
	bool start_now(int deck);
	void retime();
	void start_track(int track, qint64 at_us);
	void automate(int track, int64_t time);
	void conduct();
	void hold(int deck, bool held);

	QList<dj_deck *> decks;   /* A, B and, with four, C and D */
	QGridLayout *grid = nullptr;
	snurra_deck_waves *waves = nullptr;
	void fill_wave(int deck, snurra_deck_waves::deck &where);
	QSlider *crossfader = nullptr;
	QLabel *left_side = nullptr, *right_side = nullptr;   /* which decks each end sends */
	QComboBox *headphones = nullptr;   /* which listener a "listen" moves, with two or more */
	snurra_library *browser = nullptr;
	double position = 0;

	/* Equal power (project.md sec 3b): cos and sin of the quarter circle.
	 * A and C on its left, B and D on its right. */
	void apply_crossfader() {
		const double angle = (position + 1) / 2 * M_PI_2;
		const float gains[3] = { float(std::cos(angle)), float(std::sin(angle)), 1.0f };
		for (int i = 0; i < decks.size(); ++i)
			if (decks[i]->tile) decks[i]->tile->deck()->set_mix(gains[side_of(i)]);
	}
	/*
	 * Which side of the crossfader a deck is on (sec 3f: "which deck slot
	 * sits on which side of the crossfader" is the person's layout): left,
	 * right, or thru -- out of the crossfader's reach, as a mixer's assign
	 * switch. A, C left and B, D right by default; a side the person chose
	 * is kept (decks/<letter>/crossfader), and a deck that follows the
	 * default writes nothing, so a later default reaches it.
	 */
	enum { LEFT, RIGHT, THRU };
	static QString side_key(int deck) { return QStringLiteral("decks/%1/crossfader").arg(QChar(QLatin1Char(char('A' + deck)))); }
	static int side_default(int deck) { return deck % 2 == 0 ? LEFT : RIGHT; }
	/* Read from the settings when they change, not on each move of the
	 * crossfader: empty where the deck follows the default. */
	QStringList chosen_sides;
	void read_sides() {
		QSettings settings(QStringLiteral("snurra"), QStringLiteral("snurra"));
		chosen_sides.clear();
		for (int i = 0; i < decks.size(); ++i) chosen_sides << settings.value(side_key(i)).toString();
	}
	int side_of(int deck) const {
		const QString chosen = deck < chosen_sides.size() ? chosen_sides[deck] : QString();
		if (chosen == QLatin1String("left")) return LEFT;
		if (chosen == QLatin1String("right")) return RIGHT;
		if (chosen == QLatin1String("thru")) return THRU;
		return side_default(deck);
	}
	/* Default, then left, right, thru, and round to default again. */
	void next_side(int deck) {
		QSettings settings(QStringLiteral("snurra"), QStringLiteral("snurra"));
		const QString chosen = settings.value(side_key(deck)).toString();
		const QString next = chosen.isEmpty() ? QStringLiteral("left")
		                     : chosen == QLatin1String("left") ? QStringLiteral("right")
		                     : chosen == QLatin1String("right") ? QStringLiteral("thru") : QString();
		if (next.isEmpty()) settings.remove(side_key(deck));
		else settings.setValue(side_key(deck), next);
		read_sides();
		name_sides();
		apply_crossfader();
	}
	void name_sides() {
		if (!left_side) return;
		QStringList on[2];
		for (int i = 0; i < decks.size(); ++i) {
			const int side = side_of(i);
			if (side != THRU) on[side] << QString(QChar(QLatin1Char(char('A' + i))));
			const bool chosen = i < chosen_sides.size() && !chosen_sides[i].isEmpty();
			const QString word = side == LEFT ? snurra_dj::tr("left") : side == RIGHT ? snurra_dj::tr("right") : snurra_dj::tr("thru");
			/* The deck's letter first: a loaded deck shows its track, not
			 * its name, and decks can be arranged in any order. */
			const QString letter(QChar(QLatin1Char(char('A' + i))));
			decks[i]->side->setText(chosen ? snurra_dj::tr("%1: %2").arg(letter, word)
			                               : snurra_dj::tr("%1: %2 (default)").arg(letter, word));
		}
		left_side->setText(on[LEFT].join(QLatin1Char(' ')));
		right_side->setText(on[RIGHT].join(QLatin1Char(' ')));
	}
	/*
	 * Where the decks sit (sec 3f: the layout of the controller in front of
	 * the person): by default two to a row, A and B above C and D; or as
	 * the person arranged them -- `decks/arrangement`, letters in order, a
	 * "/" between rows: "C A B D" as many four-deck controllers lay them
	 * out. Only an arrangement chosen is written.
	 */
	void place_decks() {
		for (dj_deck *deck : decks) grid->removeWidget(deck);
		const QString chosen = QSettings(QStringLiteral("snurra"), QStringLiteral("snurra")).value(QStringLiteral("decks/arrangement")).toString();
		QList<QStringList> rows;
		for (const QString &row : chosen.split(QLatin1Char('/'), Qt::SkipEmptyParts)) rows << row.split(QLatin1Char(' '), Qt::SkipEmptyParts);
		QList<bool> placed(decks.size(), false);
		int row_at = 0, columns_used = 0;
		for (const QStringList &row : rows) {
			int column = 0;
			for (const QString &letter : row) {
				const int i = letter.isEmpty() ? -1 : letter.at(0).toUpper().unicode() - 'A';
				if (i < 0 || i >= decks.size() || placed[i]) continue;
				grid->addWidget(decks[i], row_at, column++);
				placed[i] = true;
			}
			columns_used = std::max(columns_used, column);
			if (column > 0) ++row_at;
		}
		/* Unnamed decks -- none arranged, or two decks of a four-deck
		 * arrangement -- two to a row after them. */
		int column = 0;
		for (int i = 0; i < decks.size(); ++i) {
			if (placed[i]) continue;
			grid->addWidget(decks[i], row_at, column);
			columns_used = std::max(columns_used, column + 1);
			if (++column == 2) {
				column = 0;
				++row_at;
			}
		}
		const int rows_used = row_at + (column > 0 ? 1 : 0);
		for (int row = 0; row < 4; ++row) grid->setRowStretch(row, row < rows_used ? 1 : 0);
		/* And the columns alike: unstretched, a loaded deck's tile gave way
		 * to an empty deck's text, and on a phone deck A had a quarter of
		 * the width, its EQ cut off after "lo". */
		for (int column = 0; column < 4; ++column) grid->setColumnStretch(column, column < columns_used ? 1 : 0);
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
		if (score && i < int(score->on_deck.size())) score->on_deck[size_t(i)] = -1;
		deck->unfold->hide();
		deck->out->hide();
		deck->listen->setMenu(nullptr);
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
		here.time = qint64(listeners.size());
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
	case snurra_wire::LISTEN:
		plug_remote(chosen_listener, m.path, m.index);
		break;
	case snurra_wire::RELEASE:
		/* As a deck's title dragged out to `frame`'s corner, or its
		 * "Into a window". */
		to_window(m.index, m.frame.topLeft());
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
	case snurra_wire::PLAY:
		/* From a window following the player: its play and pause. */
		if (m.index < 0) play_score(m.time != 0);
		break;
	case snurra_wire::SEEK:
		/* And its seek, to a time on the player's timeline. */
		if (m.index < 0 && score) seek_score(snurra_us_to_frames(std::max<qint64>(0, m.time), engine->rate()));
		break;
	case snurra_wire::CHANGE:
		take_change(m.states);
		break;
	case snurra_wire::FOLLOW: {
		/* The player unfolded here, for a window showing it (sec 3f):
		 * its tracks as they stand -- what a hand has changed on a deck
		 * now in the track it holds, as keep_moves would write it -- and
		 * where it is. None unfolded, no tracks. */
		snurra_wire::message is;
		is.type = snurra_wire::FOLLOWING;
		if (score) {
			std::vector<snurra_tile_state> now = score->states;
			for (int deck = 0; deck < decks.size() && deck < int(score->on_deck.size()); ++deck) {
				const int track = score->on_deck[size_t(deck)];
				if (track < 0 || !decks[deck]->tile) continue;
				const snurra_tile_state held = decks[deck]->tile->state();
				snurra_tile_state &kept = now[size_t(track)];
				kept.eq = held.eq;
				kept.tempo = held.tempo;
				kept.key_lock = held.key_lock;
				kept.key_shift = held.key_shift;
			}
			for (snurra_tile_state &one : now) {
				one.playing = false;
				is.states << one;
			}
			const qint64 at = snurra_clock_us();
			is.time = snurra_frames_to_us(score_time(at), engine->rate());
			is.started = at;
			is.index = score->playing ? 1 : 0;
		}
		send(socket, is);
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
	/* Where to find these decks: a window that gave its player whole
	 * goes on showing it from here (follow). */
	ready.path = path;
	send(it->socket, ready);
}

void snurra_dj_private::fade_in(const QString &token, qint64 start_us, qint64 length_us) {
	const auto it = taking.constFind(token);
	if (it == taking.constEnd()) return;
	if (it->tile) it->tile->deck()->fade(start_us, length_us, 0, 1);
	/* A player asked for from a window: across the decks once the fade
	 * is over, where the deck view is a view of it (sec 3f). */
	if (token == adopted && it->tile && !it->tile->state().player.empty()) {
		adopted.clear();
		const QPointer<snurra_tile> tile = it->tile;
		const int wait_ms = int(std::max<qint64>(0, start_us + length_us - snurra_clock_us()) / 1000) + 50;
		QTimer::singleShot(wait_ms, q, [this, tile] {
			for (int deck = 0; deck < decks.size(); ++deck)
				if (tile && decks[deck]->tile == tile) unfold(deck);
		});
	}
	last_started = start_us;
	last_stopped = start_us + length_us;
	end_take(token);
}

void snurra_dj_private::end_take(const QString &token) {
	const taking_t pending = taking.take(token);
	if (pending.timer) pending.timer->deleteLater();
}

/*
 * The way back from the decks to a window (the holder, 2026-11-20: the
 * views "switched between and/or combined effortlessly"): what deck
 * `deck` holds into a window of its own; where it is one deck of a player
 * unfolded, the player gathered back first (fold), so the window gets the
 * player whole -- its tracks as tiles -- not one of its tracks torn out.
 */
void snurra_dj_private::to_window(int deck, const QPoint &at) {
	if (deck >= 0 && deck < decks.size() && decks[deck]->fold->isVisible() && fold()) deck = 0;
	release(deck, at);
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

/*
 * A player unfolded across the decks (project.md sec 3f, step 4): its
 * tracks seated on channels (snurra_timeline::seat), channel c on deck c,
 * and conducted on the shared clock -- each loaded on its deck and
 * started a lead ahead of its moment, its crossfades the deck's fade, the
 * next one cued on a deck once its track there has ended. The decks'
 * play and seek are the player's (set_grouped), and a hand on a deck's
 * fader takes it out of the automation while held. The crossfader is left
 * to the hand.
 */
bool snurra_dj_private::unfold(int deck) {
	if (deck < 0 || deck >= decks.size() || !decks[deck]->tile) return false;
	snurra_tile *holder = decks[deck]->tile;
	const snurra_tile_state outer = holder->state();
	if (outer.player.empty()) return false;
	if (!engine) engine = shared_engine();
	const int rate = engine->rate();
	auto made = std::make_unique<score_t>();
	made->timeline = snurra_timeline_from(outer.player, rate, &made->states);
	const snurra_timeline::seating seated = made->timeline.seat();
	if (seated.channels > 4) return false;
	if (seated.channels > decks.size()) q->set_deck_count(4);
	end_score();
	const size_t tracks = made->states.size();
	made->channel = seated.channel;
	for (size_t i = 0; i < tracks; ++i) {
		made->start.push_back(made->timeline.start(int(i)));
		made->end.push_back(made->timeline.end(int(i)));
		const int before = made->timeline.tracks[i].follows;
		made->in.push_back(before >= 0 ? made->timeline.overlap(before) : 0);
		made->out.push_back(made->timeline.overlap(int(i)));
	}
	made->started.assign(tracks, false);
	made->stage.assign(tracks, -1);
	made->on_deck.assign(size_t(decks.size()), -1);
	made->held.assign(size_t(decks.size()), false);
	made->rides.assign(size_t(decks.size()), score_t::ride_t{});

	/* Over in step: the decks start a lead from now where the player is
	 * then, and the deck that held it fades out over the same moment. */
	const qint64 now = snurra_clock_us();
	const qint64 period_us = qint64(engine->period()) * 1000000 / rate;
	const qint64 at = now + engine->latency_us() + period_us + 100000;
	const int64_t time = snurra_us_to_frames(outer.playing ? outer.position_now(at) : outer.position_us, rate);
	if (outer.playing) {
		holder->deck()->fade(at, CROSSFADE_US, 1, 0);
		holder->retire_deck_at(at + CROSSFADE_US + period_us);
		detach(deck);
	} else {
		unload(deck);
	}
	score = std::move(made);
	score->playing = outer.playing;
	int channels = 0;
	for (int channel : score->channel) channels = std::max(channels, channel + 1);
	set_view->set_channels(channels);
	show_set(true);
	place(time, at);
	score->timer = new QTimer(q);
	score->timer->setInterval(20);
	QObject::connect(score->timer, &QTimer::timeout, q, [this] { conduct(); });
	score->timer->start();
	return true;
}

/*
 * The other way (sec 3f): the conducted decks folded back into one deck
 * holding the player -- as edited, its rides in its envelopes -- standing
 * where the player stands, crossing over on the shared clock as unfold
 * did. From there a pull out of the window takes the player whole, and
 * an empty window unpacks it into tiles again.
 */
bool snurra_dj_private::fold() {
	if (!score || score->states.empty()) return false;
	const int rate = engine->rate();
	const qint64 now = snurra_clock_us();
	const qint64 period_us = qint64(engine->period()) * 1000000 / rate;
	const qint64 at = now + engine->latency_us() + period_us + 100000;
	const bool playing = score->playing;
	const int64_t time = score_time(at);
	snurra_tile_state holder;
	holder.player = score->states;
	holder.source = QObject::tr("%n tracks", nullptr, int(score->states.size()));
	int64_t length = 0;
	for (int64_t end : score->end) length = std::max(length, end);
	holder.duration_us = snurra_frames_to_us(length, rate);
	holder.position_us = snurra_frames_to_us(time, rate);
	holder.sampled_at_us = at;
	holder.playing = playing;
	holder.controls = true;
	for (int deck = 0; deck < decks.size(); ++deck) keep_moves(deck);
	holder.player = score->states;
	/* Out over the moment the folded deck comes in. */
	for (int deck = 0; deck < decks.size(); ++deck) {
		if (deck >= int(score->on_deck.size()) || score->on_deck[size_t(deck)] < 0 || !decks[deck]->tile) continue;
		snurra_tile *tile = decks[deck]->tile;
		if (tile->playing()) {
			tile->deck()->fade(at, CROSSFADE_US, 1, 0);
			tile->retire_deck_at(at + CROSSFADE_US + period_us);
			detach(deck);
		} else {
			unload(deck);
		}
	}
	end_score();
	snurra_tile *tile = snurra_tile::embedded(*engine, holder, nullptr, decks[0], nullptr, playing);
	if (playing) tile->deck()->fade(at, CROSSFADE_US, 0, 1);
	install(0, tile, holder.source);
	return true;
}

void snurra_dj_private::end_score() {
	for (dj_deck *deck : decks) deck->fold->hide();
	if (set_view) show_set(false);
	if (!score) return;
	if (score->timer) score->timer->deleteLater();
	for (int i = 0; i < decks.size(); ++i)
		if (decks[i]->tile && i < int(score->on_deck.size()) && score->on_deck[size_t(i)] >= 0) decks[i]->tile->set_grouped(false);
	score.reset();
}

int64_t snurra_dj_private::score_time(qint64 us) const {
	if (!score->playing) return score->time0;
	return score->time0 + snurra_us_to_frames(us - score->t0_us, engine->rate());
}

qint64 snurra_dj_private::score_us(int64_t time) const {
	return score->t0_us + qint64(snurra_frames_to_us(time - score->time0, engine->rate()));
}

int64_t snurra_dj_private::frame_of(int track, int64_t time) const {
	const snurra_timeline_track &one = score->timeline.tracks[size_t(track)];
	return std::max<int64_t>(0, std::llround(double(time - score->start[size_t(track)]) * one.tempo) + one.offset);
}

/* Every channel's deck at the player's `time`, heard from `at_us`: the
 * track sounding there, else the next to come, cued. */
/* The player, unfolded, to its time `time`: every deck as place() puts
 * it, playing on from there or held there. */
void snurra_dj_private::seek_score(int64_t time) {
	if (!score) return;
	const qint64 now = snurra_clock_us();
	if (!score->playing)
		for (dj_deck *other : decks)
			if (other->tile) other->tile->hold_at(other->tile->frame_now());
	place(std::max<int64_t>(0, time), score->playing ? now + engine->latency_us() + 50000 : now);
}

void snurra_dj_private::place(int64_t time, qint64 at_us) {
	score->time0 = time;
	score->t0_us = at_us;
	for (size_t i = 0; i < score->states.size(); ++i)
		if (score->end[i] > time) {
			score->started[i] = false;
			score->stage[i] = -1;
		}
	for (int deck = 0; deck < decks.size(); ++deck) {
		int sounding = -1, next = -1;
		for (size_t i = 0; i < score->states.size(); ++i) {
			if (score->channel[i] != deck || score->end[i] <= time) continue;
			if (score->start[i] <= time) sounding = int(i);
			else if (next < 0 || score->start[i] < score->start[size_t(next)]) next = int(i);
		}
		if (sounding >= 0) {
			load_track(sounding, frame_of(sounding, time));
			if (score->playing) start_track(sounding, at_us);
		} else if (next >= 0) {
			load_track(next, score->timeline.tracks[size_t(next)].offset);
		}
	}
}

/* What the hand changed on a conducted deck -- its EQ, tempo, key lock and
 * key -- written back into the player's track as it leaves the deck, so a
 * fold keeps it (sec 3f, step 5). */
void snurra_dj_private::keep_moves(int deck) {
	if (!score || deck < 0 || deck >= decks.size() || deck >= int(score->on_deck.size())) return;
	const int track = score->on_deck[size_t(deck)];
	snurra_tile *tile = decks[deck]->tile;
	if (track < 0 || !tile) return;
	snurra_tile_state &kept = score->states[size_t(track)];
	const snurra_tile_state now = tile->state();
	kept.eq = now.eq;
	kept.tempo = now.tempo;
	kept.key_lock = now.key_lock;
	kept.key_shift = now.key_shift;
}

void snurra_dj_private::load_track(int track, int64_t frame) {
	const int deck = score->channel[size_t(track)];
	if (deck >= decks.size()) return;
	snurra_tile *tile = decks[deck]->tile;
	if (!tile || score->on_deck[size_t(deck)] != track) {
		keep_moves(deck);
		unload(deck);
		snurra_tile_state state = score->states[size_t(track)];
		state.row = 0;
		state.column = 0;
		state.controls = true;
		state.playing = false;
		state.position_us = snurra_frames_to_us(frame, engine->rate());
		state.sampled_at_us = snurra_clock_us();
		tile = snurra_tile::embedded(*engine, state, nullptr, decks[deck]);
		install(deck, tile, snurra_title_of(state.source));
		score->on_deck[size_t(deck)] = track;
		decks[deck]->fold->show();
		/* The automation seen moving under the hand's fader. */
		tile->set_show_heard(true);
		/* Its play and seek are the player's, not its own. */
		tile->set_grouped(true);
		QObject::connect(tile, &snurra_tile::play_requested, q, [this, deck](snurra_tile *, bool playing) {
			/* Shift with a cued deck's play: that deck in now, by hand. */
			if (playing && (QGuiApplication::keyboardModifiers() & Qt::ShiftModifier) && start_now(deck)) return;
			play_score(playing);
		});
		QObject::connect(tile, &snurra_tile::seek_requested, q, [this, track](snurra_tile *, qint64 position_us) {
			if (!score) return;
			const snurra_timeline_track &one = score->timeline.tracks[size_t(track)];
			seek_score(score->start[size_t(track)]
			           + std::llround(double(snurra_us_to_frames(position_us, engine->rate()) - one.offset) / one.tempo));
		});
		QObject::connect(tile, &snurra_tile::level_held, q, [this, deck](snurra_tile *, bool held) { hold(deck, held); });
	} else {
		tile->hold_at(frame);
	}
}

/* The unfolded player played or paused, as a deck's play button or a
 * window following it asks. */
void snurra_dj_private::play_score(bool playing) {
	if (!score || playing == score->playing) return;
	const qint64 now = snurra_clock_us();
	const int64_t time = score_time(now);
	score->playing = playing;
	if (!playing) {
		for (dj_deck *one : decks)
			if (one->tile) one->tile->hold_at(one->tile->frame_now());
		score->time0 = time;
		score->t0_us = now;
		return;
	}
	place(time, now + engine->latency_us() + 50000);
}

/*
 * What a window following the player changed there (CHANGE, sec 3f): each
 * track as the window has it, then as it was told it; a field differing
 * between the two is the window's change, taken into the player -- and
 * into the deck holding that track now, so it is heard -- while a field
 * the window left alone keeps what the decks have, a hand on a deck
 * meanwhile included. A crossing or a lane moved, the decks go on from
 * where the player is, on the new times.
 */
void snurra_dj_private::take_change(const QList<snurra_tile_state> &states) {
	if (!score || states.size() != 2 * qsizetype(score->states.size())) return;
	const qsizetype tracks = states.size() / 2;
	for (qsizetype i = 0; i < tracks; ++i)
		if (states.at(i).source != score->states[size_t(i)].source) return;   /* other tracks: not this player */
	bool timing = false;
	for (qsizetype i = 0; i < tracks; ++i) {
		const snurra_tile_state &now = states.at(i), &was = states.at(tracks + i);
		snurra_tile_state &kept = score->states[size_t(i)];
		snurra_tile *live = nullptr;
		for (int deck = 0; deck < decks.size() && deck < int(score->on_deck.size()); ++deck)
			if (score->on_deck[size_t(deck)] == int(i)) live = decks[deck]->tile;
		if (!(now.eq == was.eq)) {
			kept.eq = now.eq;
			if (live) live->set_eq(now.eq);
		}
		if (now.envelope != was.envelope) {
			kept.envelope = now.envelope;
			if (live) live->set_envelope(now.envelope);
		}
		if (now.level != was.level) kept.level = now.level;
		if (now.key_lock != was.key_lock) {
			kept.key_lock = now.key_lock;
			if (live) live->set_key_lock(now.key_lock);
		}
		if (now.key_shift != was.key_shift) {
			kept.key_shift = now.key_shift;
			if (live) live->set_key_shift(now.key_shift);
		}
		if (now.tempo != was.tempo) {
			kept.tempo = now.tempo;
			if (live) live->set_tempo(now.tempo);
			timing = true;
		}
		if (now.lane_start_us != was.lane_start_us) {
			kept.lane_start_us = now.lane_start_us;
			timing = true;
		}
		if (now.overlap_us != was.overlap_us) {
			kept.overlap_us = now.overlap_us;
			timing = true;
		}
		if (now.offset_us != was.offset_us) {
			kept.offset_us = now.offset_us;
			timing = true;
		}
	}
	if (!timing) return;
	const int64_t time = score_time(snurra_clock_us());
	retime();
	seek_score(time);
}

/* The score's times again from its states, after one has moved; the
 * channels as they were -- decks do not change places mid-set. */
void snurra_dj_private::retime() {
	score->timeline = snurra_timeline_from(score->states, engine->rate());
	for (size_t i = 0; i < score->states.size(); ++i) {
		score->start[i] = score->timeline.start(int(i));
		score->end[i] = score->timeline.end(int(i));
		const int before = score->timeline.tracks[i].follows;
		score->in[i] = before >= 0 ? score->timeline.overlap(before) : 0;
		score->out[i] = score->timeline.overlap(int(i));
		score->stage[i] = -1;
	}
}

/*
 * A deck started by hand (sec 3f, step 5: "a deck started becomes where
 * its tile comes in"): the track cued on `deck` comes in now, and where
 * it follows another, the overlap that brings it in now is written into
 * the player -- where the train can say it: inside the one before, at
 * most half of either. Elsewhere it starts all the same, unwritten.
 */
bool snurra_dj_private::start_now(int deck) {
	if (!score || !score->playing || deck < 0 || deck >= int(score->on_deck.size())) return false;
	const int track = score->on_deck[size_t(deck)];
	if (track < 0 || score->started[size_t(track)]) return false;
	const int rate = engine->rate();
	const qint64 at = snurra_clock_us() + engine->latency_us() + 50000;
	const int64_t time = score_time(at);
	const int before = score->timeline.tracks[size_t(track)].follows;
	if (before >= 0) {
		const int64_t overlap = score->end[size_t(before)] - time;
		const int64_t room = std::min(snurra_timeline::length_out(score->timeline.tracks[size_t(before)]),
		                              snurra_timeline::length_out(score->timeline.tracks[size_t(track)]))
		                     / 2;
		if (overlap > 0 && overlap <= room) {
			score->states[size_t(track)].overlap_us = snurra_frames_to_us(overlap, rate);
			retime();
		}
	}
	score->start[size_t(track)] = time;   /* now, written or not */
	score->end[size_t(track)] = time + snurra_timeline::length_out(score->timeline.tracks[size_t(track)]);
	start_track(track, at);
	return true;
}

void snurra_dj_private::start_track(int track, qint64 at_us) {
	const int deck = score->channel[size_t(track)];
	snurra_tile *tile = deck < decks.size() ? decks[deck]->tile : nullptr;
	if (!tile || score->on_deck[size_t(deck)] != track) return;
	tile->play_from(frame_of(track, score_time(at_us)), at_us);
	score->started[size_t(track)] = true;
	automate(track, score_time(at_us));
}

/* Its crossfades as its deck's fade: in over the overlap with the track
 * before, then out over the one after -- the quarter circle a train's
 * decks use. Not while a hand holds its fader. */
void snurra_dj_private::automate(int track, int64_t time) {
	const int deck = score->channel[size_t(track)];
	snurra_tile *tile = deck < decks.size() ? decks[deck]->tile : nullptr;
	if (!tile || score->held[size_t(deck)] || score->on_deck[size_t(deck)] != track) return;
	const size_t i = size_t(track);
	const int stage = score->in[i] > 0 && time < score->start[i] + score->in[i] ? 0 : 1;
	if (stage == score->stage[i]) return;
	score->stage[i] = stage;
	const int rate = engine->rate();
	if (stage == 0)
		tile->deck()->fade(score_us(score->start[i]), snurra_frames_to_us(score->in[i], rate), 0, 1, true);
	else if (score->out[i] > 0)
		tile->deck()->fade(score_us(score->end[i] - score->out[i]), snurra_frames_to_us(score->out[i], rate), 1, 0, true);
	else
		tile->deck()->fade(0, 0, 1, 1);
}

void snurra_dj_private::conduct() {
	if (!score || !score->playing) return;
	const qint64 now = snurra_clock_us();
	/* A ride, as it goes: a point each 50 ms or each move of the fader. */
	for (int deck = 0; deck < decks.size() && deck < int(score->rides.size()); ++deck) {
		score_t::ride_t &ride = score->rides[size_t(deck)];
		snurra_tile *tile = decks[deck]->tile;
		if (!score->held[size_t(deck)] || ride.track < 0 || !tile || score->on_deck[size_t(deck)] != ride.track) continue;
		const int64_t frame = tile->frame_now();
		const float gain = ride.level > 0 ? std::clamp(float(tile->level()) / float(ride.level), 0.0f, 1.0f) : 0.0f;
		if (ride.points.empty() || frame - ride.points.back().first >= snurra_us_to_frames(50000, engine->rate())
		    || gain != ride.points.back().second)
			if (ride.points.empty() || frame > ride.points.back().first) ride.points.emplace_back(frame, gain);
	}
	const int64_t time = score_time(now);
	const int64_t lead = snurra_us_to_frames(1500000, engine->rate());
	bool left = false;
	for (size_t i = 0; i < score->states.size(); ++i) {
		if (score->end[i] > time) left = true;
		const int deck = score->channel[i];
		if (deck >= decks.size()) continue;
		if (score->started[i]) {
			automate(int(i), time);
			continue;
		}
		if (score->end[i] <= time || score->start[i] > time + lead) continue;
		/* Its deck free -- what it held has ended -- it is loaded there
		 * and started at its moment; late, it starts where it is now. */
		const int holding = score->on_deck[size_t(deck)];
		if (holding >= 0 && holding != int(i) && score->end[size_t(holding)] > time) continue;
		if (holding != int(i)) load_track(int(i), score->timeline.tracks[i].offset);
		const qint64 moment = score_us(score->start[i]);
		start_track(int(i), std::max(moment, now + engine->latency_us() + 20000));
	}
	/* A deck whose track has ended shows the next one to come there. */
	for (int deck = 0; deck < decks.size(); ++deck) {
		const int holding = score->on_deck[size_t(deck)];
		if (holding < 0 || score->end[size_t(holding)] > time) continue;
		int next = -1;
		for (size_t i = 0; i < score->states.size(); ++i)
			if (score->channel[i] == deck && !score->started[i] && score->end[i] > time
			    && (next < 0 || score->start[i] < score->start[size_t(next)]))
				next = int(i);
		if (next >= 0) load_track(next, score->timeline.tracks[size_t(next)].offset);
	}
	if (!left) score->playing = false;
}

/* A hand on a deck's fader: the automation off while held -- its fades
 * and its envelope -- and back on when let go. */
void snurra_dj_private::hold(int deck, bool held) {
	if (!score || deck >= int(score->held.size())) return;
	score->held[size_t(deck)] = held;
	snurra_tile *tile = decks[deck]->tile;
	const int track = score->on_deck[size_t(deck)];
	if (!tile || track < 0) return;
	score_t::ride_t &ride = score->rides[size_t(deck)];
	if (held) {
		tile->deck()->fade(0, 0, 1, 1);
		tile->deck()->set_envelope({});
		ride = score_t::ride_t{};
		ride.track = track;
		ride.level = tile->level();
		return;
	}
	/* Let go: the ride is the track's envelope there, the rest as it was,
	 * and the fader back where the hand found it -- so the automation
	 * plays the move from now on (sec 3f, step 5). */
	if (ride.track == track && ride.points.size() >= 2) {
		const int rate = engine->rate();
		const QList<QPair<qint64, float>> was = score->states[size_t(track)].envelope;
		auto gain_at = [&was](qint64 us) -> float {
			if (was.isEmpty()) return 1.0f;
			if (us <= was.first().first) return was.first().second;
			if (us >= was.last().first) return was.last().second;
			for (int i = 0; i + 1 < was.size(); ++i)
				if (us < was[i + 1].first) {
					const double t = double(us - was[i].first) / double(std::max<qint64>(1, was[i + 1].first - was[i].first));
					return float(was[i].second + (was[i + 1].second - was[i].second) * t);
				}
			return was.last().second;
		};
		const qint64 first = qint64(snurra_frames_to_us(ride.points.front().first, rate));
		const qint64 last = qint64(snurra_frames_to_us(ride.points.back().first, rate));
		QList<QPair<qint64, float>> made;
		for (const auto &point : was)
			if (point.first < first) made << point;
		if (first > 0) made << qMakePair(first - 1000, gain_at(first - 1000));
		/* Room for the ride within the deck's points: every n-th, the
		 * last always. */
		const int room = std::max(2, snurra_deck::ENVELOPE_POINTS - int(made.size()) - 2
		                                     - int(std::count_if(was.begin(), was.end(), [last](const auto &p) { return p.first > last; })));
		const size_t step = std::max<size_t>(1, (ride.points.size() + size_t(room) - 1) / size_t(room));
		for (size_t i = 0; i < ride.points.size(); i += step)
			made << qMakePair(qint64(snurra_frames_to_us(ride.points[i].first, rate)), ride.points[i].second);
		if ((ride.points.size() - 1) % step != 0)
			made << qMakePair(last, ride.points.back().second);
		made << qMakePair(last + 1000, gain_at(last + 1000));
		for (const auto &point : was)
			if (point.first > last + 1000) made << point;
		score->states[size_t(track)].envelope = made;
		tile->set_level(ride.level);
		tile->set_envelope(made);
	} else {
		tile->set_envelope(tile->state().envelope);
	}
	ride = score_t::ride_t{};
	score->stage[size_t(track)] = -1;
	automate(track, score_time(snurra_clock_us()));
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
	place->unfold->hide();
	place->out->hide();
	place->listen->setMenu(nullptr);
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

bool snurra_dj::unfold(int deck) {
	return d->unfold(deck);
}

void snurra_dj::arrange(const QString &arrangement) {
	QSettings settings(QStringLiteral("snurra"), QStringLiteral("snurra"));
	if (arrangement.trimmed().isEmpty()) settings.remove(QStringLiteral("decks/arrangement"));
	else settings.setValue(QStringLiteral("decks/arrangement"), arrangement.trimmed());
	d->place_decks();
}

bool snurra_dj::start_now(int deck) {
	return d->start_now(deck);
}

bool snurra_dj::fold() {
	return d->fold();
}

void snurra_dj::adopt(const QString &from, const QString &token) {
	d->adopted = token;
	QLocalSocket *socket = new QLocalSocket(this);
	d->watch(socket);
	connect(socket, &QLocalSocket::connected, this, [this, socket, token] {
		snurra_wire::message m;
		m.type = snurra_wire::ADOPT;
		m.token = token;
		m.deck = 0;
		d->send(socket, m);
	});
	connect(socket, &QLocalSocket::errorOccurred, socket, &QObject::deleteLater);
	socket->connectToServer(from);
}

QString snurra_dj::socket_path() const {
	return d->path;
}

snurra_dj::snurra_dj(QWidget *parent) : QWidget(parent), d(std::make_unique<snurra_dj_private>()) {
	d->q = this;
	QVBoxLayout *column = new QVBoxLayout(this);
	d->set_view = new snurra_dj_set_view(this);
	d->set_view->fill = [this](std::vector<snurra_dj_set_view::track> &tracks, int64_t &now) {
		if (!d->score) return;
		for (size_t i = 0; i < d->score->states.size(); ++i) {
			snurra_dj_set_view::track one;
			one.channel = i < d->score->channel.size() ? d->score->channel[i] : 0;
			one.start = d->score->start[i];
			one.end = d->score->end[i];
			one.name = snurra_title_of(d->score->states[i].source);
			for (size_t deck = 0; deck < d->score->on_deck.size() && int(deck) < d->decks.size(); ++deck)
				one.sounding = one.sounding || (d->score->on_deck[deck] == int(i) && d->decks[int(deck)]->tile && d->decks[int(deck)]->tile->playing());
			tracks.push_back(one);
		}
		now = d->score_time(snurra_clock_us());
	};
	d->set_view->on_seek = [this](double share) {
		if (!d->score) return;
		int64_t length = 0;
		for (int64_t end : d->score->end) length = std::max(length, end);
		d->seek_score(int64_t(std::llround(share * double(length))));
	};
	d->set_window = new QToolButton(this);
	d->set_window->setObjectName(QStringLiteral("snurra-set-window"));
	d->set_window->setText(tr("In a window too"));
	d->set_window->setAccessibleName(d->set_window->text());
	d->set_window->setToolTip(tr("The player these decks play, in a window as well, following them -- what changes on either shows on both"));
	d->set_window->setAutoRaise(true);
	connect(d->set_window, &QToolButton::clicked, this, [this] {
		QProcess::startDetached(QCoreApplication::applicationFilePath(), { QStringLiteral("--follow"), d->path });
	});
	QHBoxLayout *set_row = new QHBoxLayout;
	set_row->addWidget(d->set_view, 1);
	set_row->addWidget(d->set_window, 0, Qt::AlignTop);
	d->show_set(false);
	column->addLayout(set_row);
	/* The decks' waveforms stacked full width above them, as Traktor's
	 * (the holder's trial, 2026-11-24). */
	d->waves = new snurra_deck_waves(this);
	d->waves->fill = [this](int deck, snurra_deck_waves::deck &where) { d->fill_wave(deck, where); };
	column->addWidget(d->waves, 2);
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
	d->headphones = new QComboBox(this);
	d->headphones->setAccessibleName(tr("Headphones"));
	d->headphones->setToolTip(tr("Whose headphones a deck's \"listen\" plugs in"));
	d->headphones->hide();
	fader->addWidget(d->headphones);
	connect(d->headphones, &QComboBox::currentIndexChanged, this, [this](int index) {
		if (index >= 0) {
			d->chosen_listener = index;
			d->show_plugs();
		}
	});
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
	/* A tune put on a deck by hand: the decks are the hand's again. */
	d->end_score();
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
	place->unfold->setVisible(!tile->state().player.empty());
	place->listen->setMenu(tile->state().player.empty() ? nullptr : place->listen_menu);
	place->listen->setPopupMode(tile->state().player.empty() ? QToolButton::DelayedPopup : QToolButton::MenuButtonPopup);
	/* Where another window can be started: on a phone the decks are
	 * the app, one screen, and there is no process to start. */
#ifdef Q_OS_ANDROID
	place->out->setVisible(false);
#else
	place->out->setVisible(server != nullptr);
#endif
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
	name_controls(deck);
}

/*
 * Controllers (project.md sec 3f, step 6). The deck view's controls are
 * named by their slot -- ch_1_play ... ch_4_eq_high, crossfader -- and a
 * binding moves one from a hardware control. Alt held with the pointer
 * pressed on a control listens; the one hardware control moved clearly
 * binds to it (snurra_midi_learn), and nothing binds while unsure.
 */
/*
 * Controllers (project.md sec 3f, step 6): the deck view's controls named
 * by slot -- ch_1_play ... ch_4_eq_high, crossfader -- and moved by what
 * the person bound to them (snurra_controller, midi.h).
 */
void snurra_dj_private::name_controls(int deck) {
	snurra_tile *tile = deck < decks.size() ? decks[deck]->tile : nullptr;
	if (!tile) return;
	const QString slot = QStringLiteral("ch_%1_").arg(deck + 1);
	/* By what each says it is, which names stay put however a deck looks. */
	for (QWidget *widget : tile->findChildren<QWidget *>()) {
		const QString said = widget->accessibleName();
		if (said == QLatin1String("Level")) control_widget(widget, slot + QStringLiteral("level"));
		else if (said == QLatin1String("Tempo")) control_widget(widget, slot + QStringLiteral("tempo"));
		else if (said == QLatin1String("Cue")) control_widget(widget, slot + QStringLiteral("cue"));
		else if (said == QLatin1String("Sync")) control_widget(widget, slot + QStringLiteral("sync"));
		else if (said == QLatin1String("EQ")) control_widget(widget, slot + QStringLiteral("eq"));
		else if (said == QLatin1String("Key lock")) control_widget(widget, slot + QStringLiteral("key_lock"));
		else if (said == QLatin1String("Play") || said == QLatin1String("Pause")) control_widget(widget, slot + QStringLiteral("play"));
	}
	/* The deck's own socket: a cue button on a controller is a plug with
	 * a fixed place (sec 3f). */
	control_widget(decks[deck]->listen, slot + QStringLiteral("listen"));
	/* The deck itself, where its tile leaves room: its load. */
	control_widget(decks[deck], slot + QStringLiteral("load"));
}

/* A deck's state for a light on the controller: playing, a listener's
 * plug in it, its key lock -- 1 or 0. */
double snurra_dj_private::light_value(const QString &control) const {
	if (!control.startsWith(QLatin1String("ch_"))) return 0;
	const int deck = control.section(QLatin1Char('_'), 1, 1).toInt() - 1;
	const QString what = control.section(QLatin1Char('_'), 2);
	snurra_tile *tile = deck >= 0 && deck < decks.size() ? decks[deck]->tile.data() : nullptr;
	if (what == QLatin1String("listen")) {
		for (const auto &one : listeners)
			if (one->deck == deck) return 1;
		return 0;
	}
	if (!tile) return 0;
	if (what == QLatin1String("play")) return tile->playing() ? 1 : 0;
	if (what == QLatin1String("key_lock")) return tile->key_lock() ? 1 : 0;
	return 0;
}

void snurra_dj_private::apply(const QString &control, snurra_control_kind kind, const snurra_midi_event &event) {
	/* A press, a position 0 to 1, or a step, by what the control is. */
	const bool off = event.kind == snurra_midi_event::note_off || (event.kind == snurra_midi_event::cc && event.value < 64);
	double at = event.kind == snurra_midi_event::bend ? event.value / 16383.0 : event.value / 127.0;
	int step = 0;
	if (kind == snurra_control_kind::relative) step = snurra_relative_step(event.value);
	const bool relative = kind == snurra_control_kind::relative;
	at = std::clamp(at, 0.0, 1.0);
	/* The browser: a knob steps the choice, or puts it at its place down
	 * the list; a deck's load puts the chosen tune on that deck. */
	if (control == QLatin1String("browse")) {
		if (relative) browser->step_choice(step);
		else if (event.kind == snurra_midi_event::note_on || (event.kind == snurra_midi_event::cc && kind == snurra_control_kind::button))
			return;
		else browser->choose_row(int(std::lround(at * double(std::max(0, int(browser->shown().size()) - 1)))));
		return;
	}
	/* Its buttons, a step each (a mapping's SelectNextTrack). */
	if (control == QLatin1String("browse_next") || control == QLatin1String("browse_prev")) {
		if (!off) browser->step_choice(control == QLatin1String("browse_next") ? 1 : -1);
		return;
	}
	if (control.startsWith(QLatin1String("ch_")) && control.endsWith(QLatin1String("_load"))) {
		if (off) return;
		const int deck = control.section(QLatin1Char('_'), 1, 1).toInt() - 1;
		const QStringList chosen = browser->chosen();
		if (deck < 0 || deck >= decks.size() || chosen.isEmpty()) return;
		snurra_media media;
		media.path = chosen.first();
		q->load(deck, media);
		return;
	}
	if (control == QLatin1String("crossfader")) {
		q->set_crossfader(relative ? position + step / 64.0 : at * 2 - 1);
		return;
	}
	if (!control.startsWith(QLatin1String("ch_"))) return;
	const int deck = control.section(QLatin1Char('_'), 1, 1).toInt() - 1;
	const QString what = control.section(QLatin1Char('_'), 2);
	snurra_tile *tile = deck >= 0 && deck < decks.size() ? decks[deck]->tile : nullptr;
	if (!tile) return;
	if (what == QLatin1String("listen")) {
		if (off || listeners.empty()) return;
		const int chosen = std::min(chosen_listener, int(listeners.size()) - 1);
		plug(chosen, listeners[size_t(chosen)]->deck == deck ? -1 : deck);
		return;
	}
	if (what == QLatin1String("play") || what == QLatin1String("cue") || what == QLatin1String("sync") || what == QLatin1String("key_lock")) {
		if (off) return;   /* on the press, not the release */
		if (what == QLatin1String("play")) tile->request_playing(!tile->playing());
		else if (what == QLatin1String("key_lock")) tile->set_key_lock(!tile->key_lock());
		else if (what == QLatin1String("cue")) tile->cue();
		else emit tile->sync_requested(tile);
		return;
	}
	if (what == QLatin1String("level")) {
		tile->set_level(relative ? tile->level() + step : int(std::lround(at * 100)));
	} else if (what == QLatin1String("tempo")) {
		/* The deck's own range, +-16 %, the middle 1. */
		const double range = snurra_tile::tempo_range();
		tile->set_tempo(float(relative ? tile->tempo() + step * 0.001 : 1 + (at * 2 - 1) * range));
	} else if (what.startsWith(QLatin1String("eq_"))) {
		const int band = what == QLatin1String("eq_low") ? 0 : what == QLatin1String("eq_mid") ? 1 : 2;
		float db[3] = { tile->state().eq.part_db(0), tile->state().eq.part_db(1), tile->state().eq.part_db(2) };
		if (relative) {
			db[band] = std::clamp((db[band] <= SNURRA_EQ_KILL ? -24.0f : db[band]) + 0.5f * float(step), -24.0f, 6.0f);
		} else {
			/* A DJ mixer's knob: all the way down kills, the middle is
			 * flat, all the way up +6 dB. */
			db[band] = at <= 0.0 ? SNURRA_EQ_KILL : at <= 0.5 ? float(-24.0 * (1 - 2 * at)) : float(12.0 * (at - 0.5));
		}
		tile->set_eq(db[0], db[1], db[2]);
	}
}

/*
 * Pre-listen as a plug (project.md sec 3f, the holder: "running around with
 * a plug and plugging yourself into different points"): a listener's
 * output plays a mirror of the deck it is plugged into -- the same track,
 * in step on the shared clock, its tempo, key and EQ followed -- at full
 * level, so it hears the deck before the room's fader and crossfader.
 * Moving the plug is retiring one mirror and making another.
 */
void snurra_dj_private::mirror(listener_t &one) {
	snurra_tile *tile = one.deck >= 0 && one.deck < decks.size() ? decks[one.deck]->tile.data() : nullptr;
	one.tapped = tile;
	if (!tile) return;
	/* A track gone with the player that held it: the deck itself. */
	if (one.track >= tile->player_tracks()) one.track = -1;
	snurra_deck::start_t start = snurra_tile::start_for(one.track >= 0 ? tile->player_track(one.track) : tile->state(), one.engine->rate());
	start.level = 1;
	start.playing = false;
	one.mirror = one.engine->add(start);
	one.playing = false;
	if (one.track >= 0) {
		/* As the player hears it: its own level match, before the
		 * player's envelope and the deck's fader. */
		const snurra_tile_state &state = tile->player_track(one.track);
		snurra_beat_grid grid;
		one.trim = QSettings(QStringLiteral("snurra"), QStringLiteral("snurra")).value(QStringLiteral("loudness/match"), true).toBool()
		                   && snurra_cached_beats(state.sound_source(), state.audio_track, one.engine->rate(), grid)
		           ? snurra_loudness_gain(grid, SNURRA_LOUDNESS_REFERENCE)
		           : 1.0f;
	}
	anchor(one);
}

bool snurra_dj_private::track_frame(const listener_t &one, qint64 us, double &frame, double &tempo) const {
	snurra_tile *tile = one.tapped;
	if (!tile || one.track < 0) return false;
	return tile->player_track_at(one.track, tile->deck()->position_at(us), frame, tempo);
}

void snurra_dj_private::unmirror(listener_t &one) {
	if (one.mirror) one.engine->retire(one.mirror);
	one.mirror = nullptr;
	one.tapped = nullptr;
	if (one.socket) one.socket->deleteLater();
	one.socket = nullptr;
	one.remote.clear();
	one.remote_index = -1;
}

/*
 * A plug in a window's tile (sec 3f: "plugging yourself into different
 * points"): states travel, not sound -- the window is asked for its
 * status as the plug follows, and the mirror is that tile's track,
 * anchored where its state says it is heard, at its tempo, key and EQ,
 * at the level match its analysis gives.
 */
void snurra_dj_private::plug_remote(int listener, const QString &path, int index) {
	if (listener < 0 || listener >= int(listeners.size())) return;
	listener_t &one = *listeners[size_t(listener)];
	unmirror(one);
	one.deck = -1;
	one.track = -1;
	show_plugs();
	if (index < 0 || path.isEmpty()) return;
	one.remote = path;
	one.remote_index = index;
	one.buffer = std::make_shared<QByteArray>();
	QLocalSocket *socket = new QLocalSocket(q);
	one.socket = socket;
	listener_t *self = &one;
	QObject::connect(socket, &QLocalSocket::readyRead, q, [this, self, socket] {
		self->buffer->append(socket->readAll());
		for (const snurra_wire::message &m : snurra_wire::decode(*self->buffer))
			if (m.type == snurra_wire::STATUS_IS && self->socket == socket && self->remote_index < m.states.size())
				follow_remote(*self, m.states.at(self->remote_index));
	});
	QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
	socket->connectToServer(path);
}

void snurra_dj_private::follow_remote(listener_t &one, const snurra_tile_state &state) {
	const int rate = one.engine->rate();
	const qint64 now = snurra_clock_us();
	if (!one.mirror) {
		snurra_deck::start_t start = snurra_tile::start_for(state, rate);
		start.level = 1;
		start.playing = false;
		one.mirror = one.engine->add(start);
		one.playing = false;
		one.tempo = -1;
		one.trim = 1;
		snurra_beat_grid grid;
		if (QSettings(QStringLiteral("snurra"), QStringLiteral("snurra")).value(QStringLiteral("loudness/match"), true).toBool()
		    && snurra_cached_beats(state.sound_source(), state.audio_track, rate, grid))
			one.trim = snurra_loudness_gain(grid, SNURRA_LOUDNESS_REFERENCE);
	}
	one.mirror->set_trim(one.trim);
	one.mirror->set_eq(state.eq);
	one.mirror->set_key_lock(state.key_lock);
	one.mirror->set_key_shift(state.key_shift);
	const int64_t there = snurra_us_to_frames(state.position_now(now), rate);
	const bool drifted = one.playing && std::abs(one.mirror->position_at(now) - there) > rate / 100;
	if (state.playing != one.playing || state.tempo != one.tempo || drifted) {
		one.playing = state.playing;
		one.tempo = state.tempo;
		one.mirror->set_tempo(state.tempo);
		if (!state.playing) {
			one.mirror->set_playing(false);
			one.mirror->seek(there);
		} else {
			const qint64 at = now + one.engine->latency_us() + 50000;
			one.mirror->start_at(snurra_us_to_frames(state.position_now(at), rate), at);
			one.mirror->set_playing(true);
		}
	}
}

/* In step with the deck tapped: where it will be heard a moment from now,
 * heard then here, playing as it plays, at its tempo, key and EQ. */
void snurra_dj_private::anchor(listener_t &one) {
	snurra_tile *tile = one.tapped;
	if (!tile || !one.mirror) return;
	snurra_deck *deck = tile->deck();
	one.tempo = deck->tempo();
	if (one.track >= 0) {
		/* A track of the deck's player: its own file, where the player
		 * will sound it -- silent until it comes in -- at its tempo there
		 * times the deck's, its own key and EQ. */
		const snurra_tile_state &state = tile->player_track(one.track);
		one.mirror->set_key_lock(state.key_lock);
		one.mirror->set_key_shift(state.key_shift);
		one.mirror->set_eq(state.eq);
		one.mirror->set_trim(one.trim);
		one.playing = tile->playing();
		const qint64 at = snurra_clock_us() + std::max(one.engine->latency_us(), engine->latency_us()) + 50000;
		double frame = 0, tempo = 1;
		if (!track_frame(one, at, frame, tempo)) return;
		one.mirror->set_tempo(float(tempo * deck->tempo()));
		const double begin = 0;   /* a track's own first frame */
		if (!one.playing) {
			one.mirror->set_playing(false);
			one.mirror->seek(std::max<int64_t>(0, std::llround(frame)));
			return;
		}
		if (frame >= begin) {
			one.mirror->start_at(std::llround(frame), at);
		} else {
			/* Not in yet: its first frame where the deck reaches it. */
			const double real_frames = (begin - frame) / tempo / std::max(0.01, double(deck->tempo()));
			one.mirror->start_at(std::llround(begin), at + snurra_frames_to_us(std::llround(real_frames), one.engine->rate()));
		}
		one.mirror->set_playing(true);
		return;
	}
	one.mirror->set_tempo(deck->tempo());
	one.mirror->set_key_lock(tile->state().key_lock);
	one.mirror->set_key_shift(tile->state().key_shift);
	one.mirror->set_eq(tile->state().eq);
	one.mirror->set_trim(deck->trim());
	one.playing = tile->playing();
	if (!one.playing) {
		one.mirror->set_playing(false);
		one.mirror->seek(deck->position_at(snurra_clock_us()));
		return;
	}
	const qint64 at = snurra_clock_us() + std::max(one.engine->latency_us(), engine->latency_us()) + 50000;
	one.mirror->start_at(deck->position_at(at), at);
	one.mirror->set_playing(true);
}

void snurra_dj_private::follow_taps() {
	const qint64 now = snurra_clock_us();
	for (const std::unique_ptr<listener_t> &one : listeners) {
		/* Plugged into a window's tile: its status asked each tick. */
		if (!one->remote.isEmpty()) {
			if (one->socket && one->socket->state() == QLocalSocket::ConnectedState) {
				snurra_wire::message status;
				status.type = snurra_wire::STATUS;
				one->socket->write(snurra_wire::encode(status));
			}
			continue;
		}
		if (one->deck < 0) continue;
		snurra_tile *tile = one->deck < decks.size() ? decks[one->deck]->tile.data() : nullptr;
		/* Another tile on the deck -- a tune loaded, a player's next track:
		 * the plug is in the deck, so it hears that. */
		if (tile != one->tapped || !one->mirror) {
			unmirror(*one);
			mirror(*one);
			continue;
		}
		double frame = 0, tempo = 1;
		const bool in_track = one->track >= 0 && track_frame(*one, now, frame, tempo);
		if (in_track) {
			/* Its track's own key and EQ, set at the anchor; drifted only
			 * once it is in. */
			const bool drifted = one->playing && frame >= 0 && std::abs(double(one->mirror->position_at(now)) - frame) > one->engine->rate() / 100;
			if (tile->playing() != one->playing || tile->deck()->tempo() != one->tempo || drifted) anchor(*one);
			continue;
		}
		const int64_t apart = one->mirror->position_at(now) - tile->deck()->position_at(now);
		const bool drifted = one->playing && std::abs(apart) > one->engine->rate() / 100;
		if (tile->playing() != one->playing || tile->deck()->tempo() != one->tempo || drifted) anchor(*one);
		one->mirror->set_eq(tile->state().eq);
		one->mirror->set_key_lock(tile->state().key_lock);
		one->mirror->set_key_shift(tile->state().key_shift);
		/* The level match too: a cue hears the deck after its gain, as the
		 * room does, and before its fader. */
		one->mirror->set_trim(tile->deck()->trim());
	}
}

void snurra_dj_private::plug(int listener, int deck, int track) {
	if (listener < 0 || listener >= int(listeners.size())) return;
	listener_t &one = *listeners[size_t(listener)];
	unmirror(one);
	one.deck = deck >= 0 && deck < decks.size() ? deck : -1;
	one.track = one.deck >= 0 ? track : -1;
	if (one.deck >= 0) mirror(one);
	show_plugs();
}

void snurra_dj_private::show_plugs() {
	/* With two listeners or more, which one a "listen" moves is chosen
	 * by the crossfader: several people preparing on one mix (sec 3f). */
	if (headphones) {
		headphones->blockSignals(true);
		while (headphones->count() > int(listeners.size())) headphones->removeItem(headphones->count() - 1);
		while (headphones->count() < int(listeners.size()))
			headphones->addItem(snurra_dj::tr("headphones %1").arg(headphones->count() + 1));
		headphones->setCurrentIndex(std::min(chosen_listener, std::max(0, headphones->count() - 1)));
		headphones->setVisible(listeners.size() > 1);
		headphones->blockSignals(false);
	}
	for (int i = 0; i < decks.size(); ++i) {
		decks[i]->listen->setVisible(!listeners.empty());
		const bool here = chosen_listener < int(listeners.size()) && listeners[size_t(chosen_listener)]->deck == i;
		decks[i]->listen->blockSignals(true);
		decks[i]->listen->setChecked(here);
		decks[i]->listen->blockSignals(false);
	}
}

int snurra_dj::add_listener(const QString &output) {
	auto one = std::make_unique<snurra_dj_private::listener_t>();
	one->engine = std::make_unique<snurra_engine>(output, nullptr);
	d->listeners.push_back(std::move(one));
	if (!d->follow) {
		d->follow = new QTimer(this);
		d->follow->setInterval(50);
		connect(d->follow, &QTimer::timeout, this, [this] { d->follow_taps(); });
		d->follow->start();
	}
	d->show_plugs();
	return int(d->listeners.size()) - 1;
}

void snurra_dj::plug(int listener, int deck) {
	d->plug(listener, deck);
}

void snurra_dj::choose_listener(int listener) {
	if (listener < 0 || listener >= int(d->listeners.size())) return;
	d->chosen_listener = listener;
	d->show_plugs();
}

int snurra_dj::plugged(int listener) const {
	return listener >= 0 && listener < int(d->listeners.size()) ? d->listeners[size_t(listener)]->deck : -1;
}

void snurra_dj::plug_track(int listener, int deck, int track) {
	d->plug(listener, deck, track);
}

int snurra_dj::plugged_track(int listener) const {
	return listener >= 0 && listener < int(d->listeners.size()) ? d->listeners[size_t(listener)]->track : -1;
}

bool snurra_dj::take_controllers() {
	if (d->controller) return true;
	d->controller = new snurra_controller(
	        QString(), [this](const QString &control, snurra_control_kind kind, const snurra_midi_event &event) { d->apply(control, kind, event); },
	        this);
	if (!d->controller->open()) {
		delete d->controller;
		d->controller = nullptr;
		return false;
	}
	d->controller->show_lights([this](const QString &control) { return d->light_value(control); });
	/* Learning without Alt, for where the window manager takes it: on,
	 * the next control pressed listens for the hardware moved. */
	QToolButton *learn = new QToolButton(this);
	learn->setText(tr("learn"));
	learn->setAccessibleName(tr("Learn controller bindings"));
	learn->setToolTip(tr("On: press a control, then move the hardware control meant for it"));
	learn->setCheckable(true);
	learn->setAutoRaise(true);
	if (QLayout *fader = d->crossfader->parentWidget()->layout())
		if (QBoxLayout *column = qobject_cast<QBoxLayout *>(fader))
			for (int i = 0; i < column->count(); ++i)
				if (QBoxLayout *row = qobject_cast<QBoxLayout *>(column->itemAt(i)->layout()))
					if (row->indexOf(d->crossfader) >= 0) row->addWidget(learn);
	connect(learn, &QToolButton::toggled, this, [this](bool on) {
		if (d->controller) d->controller->set_learning(on);
	});
	d->control_widget(d->crossfader, QStringLiteral("crossfader"));
	d->control_widget(d->browser->list(), QStringLiteral("browse"));
	for (int i = 0; i < d->decks.size(); ++i) d->name_controls(i);
	return true;
}

QString snurra_dj::loaded(int deck) const {
	return deck >= 0 && deck < d->decks.size() ? d->decks[deck]->name : QString();
}

void snurra_dj_private::fill_wave(int i, snurra_deck_waves::deck &where) {
	if (i < 0 || i >= decks.size()) return;
	dj_deck *deck = decks[i];
	snurra_tile *tile = deck->tile;
	/* A tune: its own file read finely; a player's mix has no one file,
	 * and is left to its deck's overview for now. */
	const QString source = tile && tile->state().player.empty() ? tile->state().sound_source() : QString();
	if (source != deck->fine_of) {
		deck->fine.reset();
		deck->fine_of = source;
		waves->clear_peaks(i);
		if (!source.isEmpty() && !snurra_is_url(source)) {
			QPointer<snurra_deck_waves> to = waves;
			deck->fine = std::make_unique<snurra_peaks>(
			        source, tile->deck()->rate(), snurra_peaks::per_second(snurra_deck_waves::COLUMNS_A_SECOND),
			        [to, i](int first, const std::vector<float> &values) {
				        QMetaObject::invokeMethod(
				                to, [to, i, first, values] {
					                if (to) to->set_peaks(i, first, values);
				                },
				                Qt::QueuedConnection);
			        },
			        tile->state().audio_track);
		}
	}
	if (!tile || source.isEmpty()) return;
	snurra_deck *sound = tile->deck();
	const int rate = std::max(1, sound->rate());
	where.loaded = true;
	where.playing = tile->playing();
	where.seconds = double(sound->position_at(snurra_clock_us())) / rate;
	const snurra_beat_grid grid = tile->beats();
	if (grid.valid() && grid.rate > 0) {
		where.beat = grid.period / grid.rate;
		where.first = grid.first / grid.rate;
	}
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
		connect(deck->unfold, &QToolButton::clicked, this, [this, i] { unfold(i); });
		connect(deck->side, &QToolButton::clicked, this, [this, i] { d->next_side(i); });
		connect(deck->fold, &QToolButton::clicked, this, [this] { fold(); });
		connect(deck->out, &QToolButton::clicked, this, [this, i] { d->to_window(i, d->decks[i]->mapToGlobal(QPoint())); });
		connect(deck->listen, &QToolButton::toggled, this, [this, i](bool on) { d->plug(d->chosen_listener, on ? i : -1); });
		/* A deck playing a player: each of its tracks a socket of its own,
		 * on the button's menu -- the deck itself on the button. */
		deck->listen_menu = new QMenu(deck->listen);
		connect(deck->listen_menu, &QMenu::aboutToShow, this, [this, i] {
			QMenu *menu = d->decks[i]->listen_menu;
			menu->clear();
			snurra_tile *tile = d->decks[i]->tile;
			const int listener = d->chosen_listener;
			const bool ours = listener < int(d->listeners.size()) && d->listeners[size_t(listener)]->deck == i;
			const int track = ours ? d->listeners[size_t(listener)]->track : -2;
			QAction *whole = menu->addAction(tr("The whole deck"), this, [this, i] { d->plug(d->chosen_listener, i); });
			whole->setCheckable(true);
			whole->setChecked(track == -1);
			for (int t = 0; tile && t < tile->player_tracks(); ++t) {
				const snurra_tile_state &state = tile->player_track(t);
				const QString name = QFileInfo(state.source).fileName();
				QAction *one = menu->addAction(name.isEmpty() ? state.source : name, this, [this, i, t] { d->plug(d->chosen_listener, i, t); });
				one->setCheckable(true);
				one->setChecked(track == t);
			}
		});
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
		d->decks << deck;
		/* The arrangements, from any deck's menu. */
		deck->setContextMenuPolicy(Qt::CustomContextMenu);
		connect(deck, &QWidget::customContextMenuRequested, this, [this, deck](const QPoint &at) {
			QMenu menu;
			const QString now = QSettings(QStringLiteral("snurra"), QStringLiteral("snurra")).value(QStringLiteral("decks/arrangement")).toString();
			const QList<QPair<QString, QString>> choices{ { tr("Default (A B above C D)"), QString() },
				                                      { tr("A B C D in a row"), QStringLiteral("A B C D") },
				                                      { tr("C A B D in a row"), QStringLiteral("C A B D") } };
			for (const auto &choice : choices) {
				QAction *action = menu.addAction(choice.first);
				action->setCheckable(true);
				action->setChecked(now == choice.second);
				connect(action, &QAction::triggered, this, [this, arrangement = choice.second] { arrange(arrangement); });
			}
			menu.exec(deck->mapToGlobal(at));
		});
	}
	d->place_decks();
	d->read_sides();
	d->name_sides();
	d->show_plugs();
	d->apply_crossfader();
	if (d->waves) d->waves->set_deck_count(count);
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
