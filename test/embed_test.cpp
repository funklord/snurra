/*
 * A tile in a program of its own (project.md sec 1: BeerSSH's tile,
 * fuzzypickles' player), made the way a consumer makes one --
 * snurra_tile::embedded, an engine of the consumer's, bytes from its
 * transport -- and seen from outside, through the tile's own state: it
 * plays, holds where the bytes stop and goes on when they come, and closing
 * it leaves no deck behind in the engine, whichever goes first.
 */
#include <QDial>
#include <QMimeData>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QElapsedTimer>
#include <QAbstractItemModel>
#include <QGuiApplication>
#include <QStyleHints>
#include <QSlider>
#include <QTreeView>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QToolButton>

#include <cmath>
#include <QtTest>

#include <memory>

#include "snurra/clock.h"
#include "snurra/engine.h"
#include "snurra/embed.h"
#include "snurra/node.h"
#include "snurra/beats.h"
#include "snurra/library.h"
#include "snurra/source.h"
#include "snurra/theme.h"
#include "snurra/tile.h"
#include "test_bytes.h"

class embed_test : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void an_embedded_tile_plays_its_bytes_and_holds_where_they_stop();
	void a_closed_embedded_tile_retires_its_deck();
	void an_engine_gone_first_leaves_the_tile_safe_to_close();
	void an_envelope_is_drawn_on_the_waveform();
	void a_handover_carries_the_envelope();
	void a_drawer_adds_room_and_leaves_every_waveform_as_it_was();
	void the_look_is_drawers_or_square_as_asked();
	void a_jump_is_the_tracks_step();
	void a_jump_on_a_beat_is_whole_bars();
	void a_nudge_moves_one_lane_and_stays();
	void a_lane_at_a_tempo_keeps_the_groups_time();
	void sync_matches_a_beat_to_another();
	void files_dropped_on_a_window_join_it();
	void a_folder_or_a_link_dropped_joins_too();
	void a_trains_crossfade_is_drawn_on_its_tiles();
	void a_lane_dragged_later_starts_later_on_the_beat();
	void loop_plays_again_from_the_start();
	void the_library_lists_finds_and_hands_out_tunes();
	void a_file_dropped_on_a_tile_takes_its_place();
	void a_tile_dropped_on_another_trades_places();
	void the_tempo_reaches_16_and_snaps_to_the_middle();
	void a_dark_desktop_is_found_where_it_is_written();
	void a_jump_while_playing_lands_exactly_its_step_on();
	void the_dj_player_loads_two_decks_and_crosses_them();
	void the_cue_is_set_paused_and_returned_to_playing();
	void a_beat_found_once_is_kept_and_shown();
	void the_library_finds_tempos_only_when_asked();
	void the_library_shows_each_tunes_key();
	void a_tile_says_its_beat_and_key_as_heard();
	void a_beat_loop_plays_its_bars_again_seamlessly();
	void a_timed_seek_is_reported_where_it_is_heard();
	void key_sync_moves_a_tile_into_the_leaders_key();
	void a_cue_sheet_beside_a_file_is_its_tracklist();
	void a_player_plays_a_hosts_bytes_and_says_when_empty();
	void a_player_says_what_it_cannot_play();
	void a_player_says_where_each_tile_is_and_that_it_ended();
	void a_player_deleted_at_once_never_calls_the_bytes_again();
};

static snurra_tile_state playing(const QString &name) {
	snurra_tile_state state;
	state.source = name;
	state.playing = true;
	state.sampled_at_us = snurra_clock_us();
	return state;
}

void embed_test::initTestCase() {
	qputenv("SNURRA_OUTPUT", "null");   /* paced by the clock, no device */
}

void embed_test::an_embedded_tile_plays_its_bytes_and_holds_where_they_stop() {
	auto bytes = std::make_shared<memory_bytes>(make_wav(48000 * 4));
	bytes->arrive(0, bytes->size() / 4);   /* about the first second */
	snurra_engine engine;
	std::unique_ptr<snurra_tile> tile(snurra_tile::embedded(engine, playing(QStringLiteral("song.wav")), bytes));

	/* It plays, then holds: the position stops short of the bytes' end. */
	qint64 last = -1;
	qint64 held = -1;
	for (int i = 0; i < 60 && held < 0; ++i) {
		QTest::qWait(100);
		const qint64 now = tile->state().position_now();
		if (now > 0 && now == last) held = now;
		last = now;
	}
	QVERIFY2(held > 500000 && held < 1100000,
	         qPrintable(QStringLiteral("held at %1 us, not where a quarter of the bytes ends").arg(held)));

	/* The rest comes, and it goes on from there. */
	bytes->arrive(0, bytes->size());
	QTRY_VERIFY_WITH_TIMEOUT(tile->state().position_now() > held + 500000, 5000);
}

void embed_test::a_closed_embedded_tile_retires_its_deck() {
	auto bytes = std::make_shared<memory_bytes>(make_wav(48000 * 2));
	bytes->arrive(0, bytes->size());
	snurra_engine engine;
	snurra_tile *tile = snurra_tile::embedded(engine, playing(QStringLiteral("song.wav")), bytes);
	QTest::qWait(200);
	QVERIFY(!engine.busy());
	QSignalSpy idle(&engine, &snurra_engine::idle);
	delete tile;
	QVERIFY2(engine.busy(), "closing the tile retired nothing");
	QVERIFY2(idle.wait(2000), "the deck was never deleted");
	QVERIFY(!engine.busy());
}

void embed_test::an_engine_gone_first_leaves_the_tile_safe_to_close() {
	auto bytes = std::make_shared<memory_bytes>(make_wav(48000));
	bytes->arrive(0, bytes->size());
	auto engine = std::make_unique<snurra_engine>();
	snurra_tile *tile = snurra_tile::embedded(*engine, playing(QStringLiteral("song.wav")), bytes);
	QTest::qWait(100);
	engine.reset();   /* deletes its decks; the tile must not call it */
	delete tile;
}

/*
 * Step 5 (project.md sec 3b): with the envelope control on, a press on the
 * waveform adds a point where it was pressed, a drag moves it, and a drag
 * out of the waveform takes it away; off, a press seeks as before.
 */
void embed_test::an_envelope_is_drawn_on_the_waveform() {
	auto bytes = std::make_shared<memory_bytes>(make_wav(48000 * 4));
	bytes->arrive(0, bytes->size());
	snurra_engine engine;
	snurra_tile_state state = playing(QStringLiteral("song.wav"));
	state.playing = false;
	std::unique_ptr<snurra_tile> tile(snurra_tile::embedded(engine, state, bytes));
	tile->resize(400, 200);
	tile->show();
	QVERIFY(QTest::qWaitForWindowExposed(tile.get()));
	QTRY_COMPARE_WITH_TIMEOUT(tile->state().duration_us, qint64(4000000), 3000);
	QWidget *waveform = tile->findChild<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
	for (QWidget *child : tile->findChildren<QWidget *>())
		if (child->accessibleName() == QLatin1String("Waveform")) waveform = child;
	QToolButton *shape = tile->findChild<QToolButton *>(QStringLiteral("snurra-envelope"));
	QVERIFY(waveform && shape);

	/* Off: a press seeks, and makes no point. */
	QTest::mouseClick(waveform, Qt::LeftButton, {}, QPoint(waveform->width() / 2, waveform->height() / 2));
	QTRY_VERIFY_WITH_TIMEOUT(qAbs(tile->state().position_us - 2000000) < 50000, 2000);
	QVERIFY(tile->envelope().isEmpty());

	/* On: a press at a quarter across and a quarter up is a point there. */
	shape->setChecked(true);
	const int w = waveform->width();
	const int h = waveform->height();
	const QPoint at(w / 4, (h - 1) * 3 / 4);
	QTest::mouseClick(waveform, Qt::LeftButton, {}, at);
	QCOMPARE(tile->envelope().size(), 1);
	QVERIFY2(qAbs(tile->envelope().at(0).first - 1000000) < 4000000 / w + 1, "the point is not a quarter across");
	QVERIFY2(qAbs(tile->envelope().at(0).second - 0.25f) < 2.0f / h, "the point is not a quarter up");

	/* Dragged down to the bottom: the same point, at silence. */
	QTest::mousePress(waveform, Qt::LeftButton, {}, at);
	QMouseEvent move(QEvent::MouseMove, QPointF(w / 4, h - 1), waveform->mapToGlobal(QPointF(w / 4, h - 1)),
	                 Qt::NoButton, Qt::LeftButton, {});
	QCoreApplication::sendEvent(waveform, &move);
	QTest::mouseRelease(waveform, Qt::LeftButton, {}, QPoint(w / 4, h - 1));
	QCOMPARE(tile->envelope().size(), 1);
	QCOMPARE(tile->envelope().at(0).second, 0.0f);

	/* Dragged out of the waveform, far below: gone. */
	QTest::mousePress(waveform, Qt::LeftButton, {}, QPoint(w / 4, h - 1));
	QMouseEvent out(QEvent::MouseMove, QPointF(w / 4, h + 200), waveform->mapToGlobal(QPointF(w / 4, h + 200)),
	                Qt::NoButton, Qt::LeftButton, {});
	QCoreApplication::sendEvent(waveform, &out);
	QTest::mouseRelease(waveform, Qt::LeftButton, {}, QPoint(w / 4, h + 200));
	QVERIFY(tile->envelope().isEmpty());
}

void embed_test::a_handover_carries_the_envelope() {
	snurra_tile_state state = playing(QStringLiteral("song.wav"));
	state.envelope = { { 0, 1.0f }, { 1500000, 0.25f } };
	QByteArray bytes;
	{
		QDataStream out(&bytes, QIODevice::WriteOnly);
		out << state;
	}
	snurra_tile_state back;
	QDataStream in(bytes);
	in >> back;
	QCOMPARE(back.envelope, state.envelope);
	QCOMPARE(back.source, state.source);
}

static QWidget *waveform_of(snurra_tile *tile) {
	for (QWidget *child : tile->findChildren<QWidget *>())
		if (child->accessibleName() == QLatin1String("Waveform")) return child;
	return nullptr;
}

static snurra_tile_state placed(const QString &name, int row, int column) {
	snurra_tile_state state;
	state.source = name;
	state.duration_us = 4000000;
	state.controls = false;
	state.row = row;
	state.column = column;
	return state;
}

/*
 * The drawers (project.md sec 3b): A and B side by side, C below. A's
 * controls drawer opened, every waveform keeps its height, A's and B's
 * stay on one line, the drawer sits right under A at A's width, and C --
 * and the window -- move down by just the drawer. Closed, all as before.
 */
void embed_test::a_drawer_adds_room_and_leaves_every_waveform_as_it_was() {
	snurra_node node;
	snurra_tile *a = node.add_tile(placed(QStringLiteral("a"), 0, 0));
	snurra_tile *b = node.add_tile(placed(QStringLiteral("b"), 0, 1));
	snurra_tile *c = node.add_tile(placed(QStringLiteral("c"), 1, 0));
	node.show();
	QVERIFY(QTest::qWaitForWindowExposed(&node));
	QTest::qWait(50);
	auto top = [&node](snurra_tile *tile) { return waveform_of(tile)->mapTo(&node, QPoint(0, 0)).y(); };
	const int height = waveform_of(a)->height();
	const int c_top = top(c);
	const int window = node.height();

	a->set_controls(true);
	QTRY_VERIFY_WITH_TIMEOUT(node.height() > window, 2000);
	QWidget *drawer = a->drawer_below();
	QVERIFY(drawer->isVisible());
	for (snurra_tile *tile : { a, b, c }) QCOMPARE(waveform_of(tile)->height(), height);
	QCOMPARE(top(b), top(a));
	QCOMPARE(drawer->geometry().top(), a->geometry().bottom() + 1);
	QCOMPARE(drawer->geometry().left(), a->geometry().left());
	QCOMPARE(drawer->width(), a->width());
	QCOMPARE(top(c) - c_top, drawer->height());
	QCOMPARE(node.height() - window, drawer->height());

	a->set_controls(false);
	QTRY_COMPARE_WITH_TIMEOUT(node.height(), window, 2000);
	QCOMPARE(top(c), c_top);
	for (snurra_tile *tile : { a, b, c }) QCOMPARE(waveform_of(tile)->height(), height);
}

void embed_test::the_look_is_drawers_or_square_as_asked() {
	qputenv("SNURRA_LAYOUT", "drawers");
	{
		snurra_node node;
		QVERIFY(node.drawers_look());
		QVERIFY(node.testAttribute(Qt::WA_TranslucentBackground));
	}
	/* Cut to what it holds (the holder, 2026-11-13: no large square
	 * window): A with its controls drawer open, B beside it with none --
	 * the corner under B, beside A's drawer, is not the window's; the
	 * frame between A and B is, to move the window by. */
	{
		snurra_node node;
		snurra_tile_state a = placed(QStringLiteral("a"), 0, 0);
		a.controls = true;
		snurra_tile *first = node.add_tile(a);
		snurra_tile *second = node.add_tile(placed(QStringLiteral("b"), 0, 1));
		node.show();
		QVERIFY(QTest::qWaitForWindowExposed(&node));
		QWidget *drawer = first->drawer_below();
		QVERIFY(drawer && drawer->isVisible());
		QTRY_VERIFY_WITH_TIMEOUT(!node.mask().isEmpty(), 2000);
		QTest::qWait(50);
		const QRegion shape = node.mask();
		QVERIFY(shape.contains(first->geometry().center()));
		QVERIFY(shape.contains(second->geometry().center()));
		QVERIFY(shape.contains(drawer->geometry().center()));
		const QPoint between((first->geometry().right() + second->geometry().left()) / 2, first->geometry().center().y());
		QVERIFY2(shape.contains(between), "the frame between the tiles is cut away");
		const QPoint corner(second->geometry().center().x(), drawer->geometry().center().y());
		QVERIFY2(!shape.contains(corner), "the empty corner under B is still the window's");
	}
	qputenv("SNURRA_LAYOUT", "square");
	{
		snurra_node node;
		QVERIFY(!node.drawers_look());
		QVERIFY(!node.testAttribute(Qt::WA_TranslucentBackground));
	}
	/* Offscreen, nothing can say the window may have a shape: square. */
	qunsetenv("SNURRA_LAYOUT");
	snurra_node node;
	QVERIFY(!node.drawers_look());
}

/* Paused, a tile's position after a jump; the window's transport moves it. */
static qint64 jumped(snurra_tile *tile, int direction, bool large) {
	tile->jump(direction, large);
	QTest::qWait(100);
	return tile->state().position_us;
}

/* The holder's steps (2026-11-12): under ten minutes 10 s and 60 s, from
 * ten minutes 5 % and 20 %, each said on the buttons. */
void embed_test::a_jump_is_the_tracks_step() {
	snurra_node node;
	snurra_tile_state song_state = placed(QStringLiteral("song"), 0, 0);
	song_state.duration_us = 3 * 60 * qint64(1000000);
	snurra_tile *song = node.add_tile(song_state);
	snurra_tile_state long_one = placed(QStringLiteral("film"), 1, 0);
	long_one.duration_us = 20 * 60 * qint64(1000000);
	snurra_tile *film = node.add_tile(long_one);
	QCOMPARE(song->jump_step(false), QStringLiteral("10 s"));
	QCOMPARE(song->jump_step(true), QStringLiteral("60 s"));
	QCOMPARE(film->jump_step(false), QStringLiteral("5 %"));
	QCOMPARE(film->jump_step(true), QStringLiteral("20 %"));
	QCOMPARE(jumped(song, 1, false), qint64(10000000));
	QCOMPARE(jumped(song, 1, true), qint64(70000000));
	QCOMPARE(jumped(song, -1, false), qint64(60000000));
	QCOMPARE(jumped(film, 1, false), qint64(60000000 + 60000000));   /* the group moved it to 60 s; 5 % is a minute */
	/* In a window the controls drawer is the window's to place: look there. */
	QToolButton *forward = song->drawer_below()->findChild<QToolButton *>(QStringLiteral("snurra-forward"));
	QVERIFY(forward);
	QVERIFY(forward->text().contains(QStringLiteral("10 s")));
}

/* A kick at 120 BPM: once its grid is found, the step is bars, and a jump
 * is exactly 16 beats -- 8 s, 384000 frames. */
void embed_test::a_jump_on_a_beat_is_whole_bars() {
	QTemporaryDir dir;
	const QString path = dir.filePath(QStringLiteral("kick.wav"));
	QFile file(path);
	QVERIFY(file.open(QIODevice::WriteOnly));
	file.write(make_wav_of(48000 * 30, [](int i) {
		const int in_beat = i % 24000;
		const double t = in_beat / 48000.0;
		return int(20000 * std::sin(2 * M_PI * 55 * t) * std::exp(-t / 0.09));
	}));
	file.close();
	snurra_node node;
	snurra_tile_state state = placed(path, 0, 0);
	state.duration_us = 30000000;
	snurra_tile *tile = node.add_tile(state);
	QTRY_VERIFY_WITH_TIMEOUT(tile->beats().valid(), 10000);
	qInfo("found %.3f BPM", tile->beats().bpm());
	QCOMPARE(tile->jump_step(false), QStringLiteral("4 bars"));
	QCOMPARE(tile->jump_step(true), QStringLiteral("16 bars"));
	QVERIFY2(qAbs(tile->beats().bpm() - 120) < 0.01, "not the tempo it was made at");
	const qint64 from = tile->state().position_us;
	const qint64 to = jumped(tile, 1, false);
	const double beats = 16 * tile->beats().period / 48000 * 1e6;
	QVERIFY2(qAbs(double(to - from) - beats) <= 21,
	         qPrintable(QStringLiteral("jumped %1 us, not 16 of its beats, %2").arg(to - from).arg(beats)));
}

/* Two lanes held at one time; the lower nudged forward goes 10 ms ahead
 * of the upper, and a seek of the group keeps it so. */
void embed_test::a_nudge_moves_one_lane_and_stays() {
	snurra_node node;
	snurra_tile_state a = placed(QStringLiteral("a"), 0, 0);
	snurra_tile_state b = placed(QStringLiteral("b"), 1, 0);
	a.duration_us = b.duration_us = 60000000;
	snurra_tile *upper = node.add_tile(a);
	snurra_tile *lower = node.add_tile(b);
	lower->nudge(1);
	QTest::qWait(100);
	QCOMPARE(lower->state().position_us - upper->state().position_us, qint64(10000));
	upper->jump(1, false);
	QTest::qWait(100);
	QCOMPARE(upper->state().position_us, qint64(10000000));
	QCOMPARE(lower->state().position_us, qint64(10010000));
}

/* A kick every beat at `bpm`, the first at `first` seconds, 20 seconds. */
static QString kick_file(QTemporaryDir &dir, const QString &name, double bpm, double first) {
	const QString path = dir.filePath(name);
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) return QString();
	const double period = 60.0 * 48000 / bpm;
	file.write(make_wav_of(48000 * 20, [period, first](int i) {
		const double since = double(i) - first * 48000;
		if (since < 0) return 0;
		const double t = std::fmod(since, period) / 48000;
		return int(20000 * std::sin(2 * M_PI * 55 * t) * std::exp(-t / 0.09));
	}));
	return path;
}

/* Files a file manager drags onto a window (project.md sec 3b): dropped
 * at its right edge, they go on the first lane's train; at its bottom, a
 * new lane. Paused, controls in, as a launch's later files. */
void embed_test::files_dropped_on_a_window_join_it() {
	QTemporaryDir dir;
	QStringList paths;
	for (const QString &name : { QStringLiteral("one.wav"), QStringLiteral("two.wav"), QStringLiteral("three.wav") }) {
		QFile file(dir.filePath(name));
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write(make_wav(48000));
		paths << file.fileName();
	}
	snurra_node node;
	node.add_tile(placed(QStringLiteral("first"), 0, 0));
	node.resize(400, 200);
	node.show();
	QVERIFY(QTest::qWaitForWindowExposed(&node));
	auto drop = [&node](const QStringList &files, const QPoint &at) {
		QMimeData mime;
		QList<QUrl> urls;
		for (const QString &file : files) urls << QUrl::fromLocalFile(file);
		mime.setUrls(urls);
		/* As a drag arrives: enter first -- Qt delivers a drop only to the
		 * widget that took the enter. */
		QDragEnterEvent enter(at, Qt::CopyAction, &mime, Qt::LeftButton, {});
		QCoreApplication::sendEvent(&node, &enter);
		if (!enter.isAccepted()) return false;
		QDropEvent event(QPointF(at), Qt::CopyAction, &mime, Qt::LeftButton, {});
		QCoreApplication::sendEvent(&node, &event);
		return event.isAccepted();
	};
	QVERIFY(drop({ paths[0], paths[1] }, QPoint(node.width() - 2, node.height() / 2)));
	QTRY_COMPARE_WITH_TIMEOUT(node.tile_count(), 3, 2000);
	QVERIFY(drop({ paths[2] }, QPoint(node.width() / 2, node.height() - 2)));
	QTRY_COMPARE_WITH_TIMEOUT(node.tile_count(), 4, 2000);
	QList<QPair<int, int>> places;
	for (snurra_tile *tile : node.findChildren<snurra_tile *>()) {
		if (tile->state().source.startsWith(dir.path())) {
			places << qMakePair(tile->row(), tile->column());
			QVERIFY(!tile->playing());
			QVERIFY(!tile->controls());
		}
	}
	std::sort(places.begin(), places.end());
	QCOMPARE(places, (QList<QPair<int, int>>{ { 0, 1 }, { 0, 2 }, { 1, 0 } }));
	/* Titled by what it plays: the first tile's name, and how many more. */
	QCOMPARE(node.windowTitle(), QStringLiteral("first +3"));
}

/*
 * A folder dropped is its tracks (project.md sec 3b): under it, in a
 * folder of its own too, 2.wav, 10.wav and a film, beside a cover, a
 * cue sheet and three playlists (.m3u, .pls and .asx, which
 * shared-mime-info types as audio/). They join as 2, 10, the film --
 * counted, not spelled -- and the rest make no tile. A link from a browser stays a
 * URL, after them. A drag still moving never walks the folder.
 */
void embed_test::a_folder_or_a_link_dropped_joins_too() {
	QTemporaryDir dir;
	QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("album/cd")));
	const QStringList names{ QStringLiteral("album/2.wav"), QStringLiteral("album/10.wav"), QStringLiteral("album/cd/film.mkv"),
		                 QStringLiteral("album/cover.jpg"), QStringLiteral("album/album.cue"),
		                 QStringLiteral("album/album.m3u"), QStringLiteral("album/radio.pls"), QStringLiteral("album/list.asx") };
	for (const QString &name : names) {
		QFile file(dir.filePath(name));
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write(make_wav(4800));
	}
	const QString link = QStringLiteral("http://127.0.0.1:9/radio");
	QMimeData mime;
	mime.setUrls({ QUrl::fromLocalFile(dir.filePath(QStringLiteral("album"))), QUrl(link) });
	QCOMPARE(snurra_dropped(&mime),
	         (QStringList{ dir.filePath(names[0]), dir.filePath(names[1]), dir.filePath(names[2]), link }));
	QCOMPARE(snurra_dropped(&mime, 1), (QStringList{ dir.filePath(names[0]), link }));
	QMimeData text;
	text.setText(link + QLatin1Char('\n'));
	QVERIFY2(snurra_drop_offers(&text), "a link dragged as text is refused");
	QCOMPARE(snurra_dropped(&text), QStringList{ link });

	snurra_node node;
	node.add_tile(placed(QStringLiteral("first"), 0, 0));
	node.resize(400, 200);
	node.show();
	QVERIFY(QTest::qWaitForWindowExposed(&node));
	const QPoint at(node.width() - 2, node.height() / 2);
	QDragEnterEvent enter(at, Qt::CopyAction, &mime, Qt::LeftButton, {});
	QCoreApplication::sendEvent(&node, &enter);
	QVERIFY(enter.isAccepted());
	QDropEvent drop(QPointF(at), Qt::CopyAction, &mime, Qt::LeftButton, {});
	QCoreApplication::sendEvent(&node, &drop);
	QVERIFY(drop.isAccepted());
	QTRY_COMPARE_WITH_TIMEOUT(node.tile_count(), 5, 2000);
	QMap<int, QString> train;
	for (snurra_tile *tile : node.findChildren<snurra_tile *>()) train.insert(tile->column(), tile->state().source);
	QCOMPARE(train.values(), (QStringList{ QStringLiteral("first"), dir.filePath(names[0]), dir.filePath(names[1]),
	                                       dir.filePath(names[2]), link }));
}

/*
 * A lane may start later than the timeline (project.md sec 3b): A and B
 * stacked, 4 s each. B's waveform dragged with Ctrl by half its width
 * moves B's lane to begin at 2 s. Paused, a seek to 1 s holds B at its
 * start and a seek to 3 s holds it 1 s in; played from 0, B waits silent
 * and comes in at 2 s. Then with beats -- A's every 0.5 s from 0, B's
 * first at 0.1 s -- a drag to 1.3 s snaps to 1.4 s, so B's first beat
 * lands on A's at 1.5 s; Shift held, it stays at 1.3 s.
 */
void embed_test::a_lane_dragged_later_starts_later_on_the_beat() {
	snurra_node node;
	snurra_tile *a = node.add_tile(placed(QStringLiteral("a"), 0, 0));
	snurra_tile *b = node.add_tile(placed(QStringLiteral("b"), 1, 0));
	node.resize(400, 300);
	node.show();
	QVERIFY(QTest::qWaitForWindowExposed(&node));
	QWidget *wave_a = waveform_of(a);
	QWidget *wave_b = waveform_of(b);
	QVERIFY(wave_a && wave_b);
	const int w = wave_b->width();
	auto drag = [&](double fraction, Qt::KeyboardModifiers held) {
		const QPointF from(w / 4.0, wave_b->height() / 2.0);
		const QPointF to = from + QPointF(fraction * w, 0);
		QMouseEvent press(QEvent::MouseButtonPress, from, wave_b->mapToGlobal(from), Qt::LeftButton, Qt::LeftButton, held);
		QCoreApplication::sendEvent(wave_b, &press);
		QMouseEvent move(QEvent::MouseMove, to, wave_b->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, held);
		QCoreApplication::sendEvent(wave_b, &move);
		QMouseEvent release(QEvent::MouseButtonRelease, to, wave_b->mapToGlobal(to), Qt::LeftButton, Qt::NoButton, held);
		QCoreApplication::sendEvent(wave_b, &release);
	};
	const qint64 pixel_us = 4000000 / w + 1;
	drag(0.5, Qt::ControlModifier);
	QVERIFY2(qAbs(b->lane_start_us() - 2000000) <= pixel_us, qPrintable(QString::number(b->lane_start_us())));
	QCOMPARE(a->lane_start_us(), qint64(0));

	auto seek_a = [&](double fraction) {
		QTest::mouseClick(wave_a, Qt::LeftButton, {}, QPoint(int(fraction * wave_a->width()), wave_a->height() / 2));
	};
	seek_a(0.25);
	QTRY_VERIFY_WITH_TIMEOUT(qAbs(a->state().position_us - 1000000) < 50000, 2000);
	QCOMPARE(b->state().position_us, qint64(0));
	seek_a(0.75);
	QTRY_VERIFY_WITH_TIMEOUT(qAbs(b->state().position_us - 1000000) < 50000, 2000);

	seek_a(0);
	QTRY_VERIFY_WITH_TIMEOUT(b->state().position_us == 0 && a->state().position_us < 50000, 2000);
	a->request_playing(true);
	QTest::qWait(1000);
	QVERIFY2(b->playing(), "the late lane is not on its way");
	/* Said as how long before its start: a second, of the two. */
	QVERIFY2(qAbs(b->state().position_us + 1000000) < 150000, qPrintable(QString::number(b->state().position_us)));
	QTest::qWait(1600);
	QVERIFY2(qAbs(b->state().position_us - 600000) < 250000, qPrintable(QString::number(b->state().position_us)));
	a->request_playing(false);

	const int rate = b->deck()->rate();
	a->set_beats(snurra_beat_grid{ rate * 0.5, 0, 2, rate });
	b->set_beats(snurra_beat_grid{ rate * 0.5, rate * 0.1, 2, rate });
	b->set_lane_start_us(0);
	drag(0.325, Qt::ControlModifier);
	QVERIFY2(qAbs(b->lane_start_us() - 1400000) <= 25, qPrintable(QString::number(b->lane_start_us())));
	b->set_lane_start_us(0);
	drag(0.325, Qt::ControlModifier | Qt::ShiftModifier);
	QVERIFY2(qAbs(b->lane_start_us() - 1300000) <= pixel_us, qPrintable(QString::number(b->lane_start_us())));

	/* A handover keeps it. */
	QByteArray bytes;
	QDataStream out(&bytes, QIODevice::WriteOnly);
	out << b->state();
	snurra_tile_state back;
	QDataStream in(bytes);
	in >> back;
	QCOMPARE(back.lane_start_us, b->lane_start_us());

	/* The lane's first closed: the next keeps the lane's begin. */
	snurra_tile_state after = placed(QStringLiteral("c"), 1, 1);
	snurra_tile *c = node.add_tile(after);
	const qint64 begin = b->lane_start_us();
	QVERIFY(begin > 0);
	emit b->close_requested(b);
	QTRY_COMPARE_WITH_TIMEOUT(node.tile_count(), 2, 2000);
	QCOMPARE(c->lane_start_us(), begin);
}

/*
 * mplayer's -loop: a 1 s tile, looped twice, plays again from its start
 * as it ends and then stops; for ever (0), it is still playing after
 * three of its lengths; paused by hand, it stays paused.
 */
void embed_test::loop_plays_again_from_the_start() {
	snurra_node node;
	snurra_tile_state state = placed(QStringLiteral("a"), 0, 0);
	state.duration_us = 1000000;
	snurra_tile *a = node.add_tile(state);
	node.show();
	QVERIFY(QTest::qWaitForWindowExposed(&node));
	node.set_loop(2);
	a->request_playing(true);
	QTest::qWait(1500);
	QVERIFY2(a->playing(), "the second play did not begin");
	QVERIFY2(a->state().position_us < 700000, qPrintable(QString::number(a->state().position_us)));
	QTRY_VERIFY_WITH_TIMEOUT(!a->playing(), 2000);
	QTest::qWait(600);
	QVERIFY2(!a->playing(), "played a third time");
	/* Played at its end, it plays from the start. */
	node.set_loop(1);
	a->request_playing(true);
	QTest::qWait(400);
	QVERIFY2(a->playing() && a->state().position_us < 500000, "a play at the end did not start again");
	QTRY_VERIFY_WITH_TIMEOUT(!a->playing(), 2000);

	node.set_loop(0);
	QTest::mouseClick(waveform_of(a), Qt::LeftButton, {}, QPoint(0, 5));
	QTRY_COMPARE_WITH_TIMEOUT(a->state().position_us, qint64(0), 2000);
	a->request_playing(true);
	QTest::qWait(3300);
	QVERIFY2(a->playing(), "for ever ended");
	a->request_playing(false);
	QTest::qWait(1500);
	QVERIFY2(!a->playing(), "a pause was taken for an end");
}

/*
 * The library (project.md sec 3b), what snurra opens with no file: under
 * a folder, a/2.wav, a/10.wav and b/song.flac are listed -- 2 before 10 --
 * and a cover and a playlist are not. Every word typed must be found. A
 * double-click asks to play; a drag carries the snurra files type, which
 * a window takes as a drop and a desktop cannot copy; let go over nothing
 * it asks for a tile there. The window then plays a tune in place of its
 * one tile, and with several takes it at its train's end, paused.
 */
void embed_test::the_library_lists_finds_and_hands_out_tunes() {
	QTemporaryDir dir;
	const QStringList names{ QStringLiteral("a/2.wav"), QStringLiteral("a/10.wav"), QStringLiteral("b/song.flac"),
		                 QStringLiteral("a/cover.jpg"), QStringLiteral("a/list.m3u") };
	for (const QString &name : names) {
		QVERIFY(QDir(dir.path()).mkpath(QFileInfo(name).path()));
		QFile file(dir.filePath(name));
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write(make_wav(4800));
	}
	const QStringList tunes{ dir.filePath(names[0]), dir.filePath(names[1]), dir.filePath(names[2]) };
	snurra_library library(dir.path());
	library.show();
	QVERIFY(QTest::qWaitForWindowExposed(&library));
	QTRY_VERIFY_WITH_TIMEOUT(!library.scanning(), 5000);
	QCOMPARE(library.shown(), tunes);
	library.set_search(QStringLiteral("A 10"));
	QCOMPARE(library.shown(), QStringList{ tunes[1] });
	library.set_search(QStringLiteral("song"));
	QCOMPARE(library.shown(), QStringList{ tunes[2] });
	library.set_search(QString());

	QString asked;
	connect(&library, &snurra_library::play_requested, this, [&asked](const QString &path) { asked = path; });
	QTreeView *view = library.findChild<QTreeView *>();
	QVERIFY(view);
	const QRect row = view->visualRect(view->model()->index(1, 0));
	QTest::mouseClick(view->viewport(), Qt::LeftButton, {}, row.center());
	QVERIFY2(asked.isEmpty(), "one click played");
	QTest::mouseDClick(view->viewport(), Qt::LeftButton, {}, row.center());
	QCOMPARE(asked, tunes[1]);
	asked.clear();
	QTest::keyClick(view, Qt::Key_Return);
	QCOMPARE(asked, tunes[1]);

	std::unique_ptr<QMimeData> mime(snurra_library::mime_for({ tunes[0], tunes[2] }));
	QVERIFY(!mime->hasUrls());
	QVERIFY(snurra_drop_offers(mime.get()));
	QCOMPARE(snurra_dropped(mime.get()), (QStringList{ tunes[0], tunes[2] }));
	QStringList placed_out;
	library.on_dropped_nowhere = [&placed_out](const QStringList &paths, const QPoint &) { placed_out = paths; };
	library.dragged_out({ tunes[0] }, true, QPoint(-10000, -10000));
	QVERIFY2(placed_out.isEmpty(), "a drag a window took was placed as well");
	library.dragged_out({ tunes[0] }, false, library.mapToGlobal(library.rect().center()));
	QVERIFY2(placed_out.isEmpty(), "a drag let go over the library was placed");
	library.dragged_out({ tunes[0] }, false, QPoint(-10000, -10000));
	QCOMPARE(placed_out, QStringList{ tunes[0] });

	snurra_node node;
	node.play_file(tunes[0]);
	QCOMPARE(node.tile_count(), 1);
	node.play_file(tunes[1]);
	QCOMPARE(node.tile_count(), 1);
	snurra_tile *only = node.findChildren<snurra_tile *>().first();
	QTRY_COMPARE_WITH_TIMEOUT(node.findChildren<snurra_tile *>().size(), 1, 2000);
	only = node.findChildren<snurra_tile *>().first();
	QCOMPARE(only->state().source, tunes[1]);
	QVERIFY(only->playing());
	node.add_files({ tunes[0] }, snurra_wire::BELOW);
	node.play_file(tunes[2]);
	QCOMPARE(node.tile_count(), 3);
	for (snurra_tile *tile : node.findChildren<snurra_tile *>())
		if (tile->state().source == tunes[2]) {
			QCOMPARE(qMakePair(tile->row(), tile->column()), qMakePair(0, 1));
			QVERIFY(!tile->playing());
		}
}

/*
 * A file dropped on a tile replaces it (the holder, 2026-11-13): A, B, C
 * in a train; a file dropped on B's middle is in B's place, between A and
 * C, at B's level, and B is gone. At the right edge it still joins the
 * train after C.
 */
void embed_test::a_file_dropped_on_a_tile_takes_its_place() {
	QTemporaryDir dir;
	const QString path = dir.filePath(QStringLiteral("new.wav"));
	QFile file(path);
	QVERIFY(file.open(QIODevice::WriteOnly));
	file.write(make_wav(4800));
	file.close();
	snurra_node node;
	node.add_tile(placed(QStringLiteral("a"), 0, 0));
	snurra_tile *b = node.add_tile(placed(QStringLiteral("b"), 0, 1));
	node.add_tile(placed(QStringLiteral("c"), 0, 2));
	b->set_level(40);
	node.resize(900, 200);
	node.show();
	QVERIFY(QTest::qWaitForWindowExposed(&node));
	auto drop_at = [&node, &path](const QPoint &at) {
		QMimeData mime;
		mime.setUrls({ QUrl::fromLocalFile(path) });
		QDragEnterEvent enter(at, Qt::CopyAction, &mime, Qt::LeftButton, {});
		QCoreApplication::sendEvent(&node, &enter);
		if (!enter.isAccepted()) return false;
		QDropEvent event(QPointF(at), Qt::CopyAction, &mime, Qt::LeftButton, {});
		QCoreApplication::sendEvent(&node, &event);
		return event.isAccepted();
	};
	const QPoint middle_of_b = b->geometry().center();
	QVERIFY(drop_at(middle_of_b));
	QTRY_COMPARE_WITH_TIMEOUT(node.findChildren<snurra_tile *>().size(), 3, 2000);
	QMap<int, snurra_tile *> train;
	for (snurra_tile *tile : node.findChildren<snurra_tile *>()) train.insert(tile->column(), tile);
	QCOMPARE(train.value(0)->state().source, QStringLiteral("a"));
	QCOMPARE(train.value(1)->state().source, path);
	QCOMPARE(train.value(1)->level(), 40);
	QCOMPARE(train.value(2)->state().source, QStringLiteral("c"));

	QVERIFY(drop_at(QPoint(node.width() - 2, node.height() / 3)));
	QTRY_COMPARE_WITH_TIMEOUT(node.tile_count(), 4, 2000);
}

/*
 * A tile let go on another of its window trades places with it (the
 * holder, 2026-11-13): A and B in a train, C below beginning 1 s late.
 * A's title let go over C: A heads the lower lane, still 1 s late, and C
 * the upper. Then, as Wayland's drag and drop, A dropped on B.
 */
void embed_test::a_tile_dropped_on_another_trades_places() {
	snurra_node node;
	snurra_tile *a = node.add_tile(placed(QStringLiteral("a"), 0, 0));
	snurra_tile *b = node.add_tile(placed(QStringLiteral("b"), 0, 1));
	snurra_tile *c = node.add_tile(placed(QStringLiteral("c"), 1, 0));
	c->set_lane_start_us(1000000);
	node.resize(600, 300);
	node.show();
	QVERIFY(QTest::qWaitForWindowExposed(&node));
	auto place = [](snurra_tile *tile) { return qMakePair(tile->row(), tile->column()); };

	const QPoint on_c = node.mapToGlobal(c->geometry().center());
	emit a->handle_moved(a, node.mapToGlobal(a->geometry().center()));
	emit a->handle_moved(a, on_c);
	emit a->handle_released(a, on_c);
	QCOMPARE(place(a), qMakePair(1, 0));
	QCOMPARE(place(c), qMakePair(0, 0));
	QCOMPARE(a->lane_start_us(), qint64(1000000));
	QCOMPARE(c->lane_start_us(), qint64(0));
	QTRY_VERIFY_WITH_TIMEOUT(a->geometry().top() > c->geometry().top(), 2000);

	/* The window's own index of A, its first tile added: 0. */
	QMimeData mime;
	mime.setData(QStringLiteral("application/x-snurra-tile"), (node.socket_path() + QStringLiteral("\n0")).toUtf8());
	const QPoint on_b = b->geometry().center();
	QDragEnterEvent enter(on_b, Qt::MoveAction, &mime, Qt::LeftButton, {});
	QCoreApplication::sendEvent(&node, &enter);
	QVERIFY(enter.isAccepted());
	QDropEvent drop(QPointF(on_b), Qt::MoveAction, &mime, Qt::LeftButton, {});
	QCoreApplication::sendEvent(&node, &drop);
	QVERIFY(drop.isAccepted());
	QCOMPARE(place(a), qMakePair(0, 1));
	QCOMPARE(place(b), qMakePair(1, 0));
	QCOMPARE(b->lane_start_us(), qint64(1000000));
}

/*
 * The tempo slider (the holder, 2026-11-13): +-16 %, key lock off by
 * default so the pitch moves with it; dragged within 0.5 % of the middle
 * it is the middle, beyond that its own tenth; set by a key, any tenth.
 */
void embed_test::the_tempo_reaches_16_and_snaps_to_the_middle() {
	snurra_node node;
	snurra_tile_state state = placed(QStringLiteral("a"), 0, 0);
	state.controls = true;
	snurra_tile *tile = node.add_tile(state);
	QSlider *tempo = node.findChild<QSlider *>(QStringLiteral("snurra-tempo"));
	QVERIFY(tempo);
	QCOMPARE(tempo->maximum(), 160);
	QCOMPARE(tempo->minimum(), -160);
	QVERIFY2(!tile->key_lock(), "key lock is on by default");
	tempo->setSliderDown(true);
	tempo->setValue(4);
	QCOMPARE(tile->tempo(), 1.0f);
	tempo->setValue(-5);
	QCOMPARE(tile->tempo(), 1.0f);
	tempo->setValue(12);
	QVERIFY(qAbs(tile->tempo() - 1.012f) < 1e-5f);
	tempo->setValue(160);
	QVERIFY(qAbs(tile->tempo() - 1.16f) < 1e-5f);
	tempo->setSliderDown(false);
	tempo->setValue(3);
	QVERIFY(qAbs(tile->tempo() - 1.003f) < 1e-5f);
}

/*
 * A dark desktop with no dark status (the holder's TDE, 2026-11-13): its
 * kdeglobals, or LXQt's lxqt.conf, read as one comparison -- the window's
 * ground against its text. A file without the pair says nothing. In a TDE
 * session the file is asked before the palette, which always answers
 * (light, unintegrated); $SNURRA_THEME overrides all.
 */
void embed_test::a_dark_desktop_is_found_where_it_is_written() {
	QTemporaryDir home;
	auto write = [&home](const QString &name, const QByteArray &text) {
		QVERIFY(QDir(home.path()).mkpath(QFileInfo(home.filePath(name)).path()));
		QFile file(home.filePath(name));
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write(text);
	};
	write(QStringLiteral("dark/kdeglobals"), "[General]\nalternateBackground=237,244,249\nwindowBackground=0,42,78\n"
	                                         "windowForeground=220,220,220\n[WM]\nactiveBackground=65,101,148\n");
	write(QStringLiteral("light/kdeglobals"), "[General]\nwindowBackground=239,239,239\nwindowForeground=0,0,0\n");
	write(QStringLiteral("none/kdeglobals"), "[General]\nXftHintStyle=hintfull\n");
	write(QStringLiteral("lxqt.conf"), "[General]\ntheme=frost\n[Palette]\nwindow_color=#232323\nwindow_text_color=#d0d0d0\n");
	write(QStringLiteral("lxqt-plain.conf"), "[General]\ntheme=frost\n");
	QCOMPARE(snurra_scheme_from_kdeglobals(home.filePath(QStringLiteral("dark/kdeglobals"))), snurra_scheme::dark);
	QCOMPARE(snurra_scheme_from_kdeglobals(home.filePath(QStringLiteral("light/kdeglobals"))), snurra_scheme::light);
	QCOMPARE(snurra_scheme_from_kdeglobals(home.filePath(QStringLiteral("none/kdeglobals"))), snurra_scheme::unknown);
	QCOMPARE(snurra_scheme_from_kdeglobals(home.filePath(QStringLiteral("missing"))), snurra_scheme::unknown);
	QCOMPARE(snurra_scheme_from_lxqt(home.filePath(QStringLiteral("lxqt.conf"))), snurra_scheme::dark);
	QCOMPARE(snurra_scheme_from_lxqt(home.filePath(QStringLiteral("lxqt-plain.conf"))), snurra_scheme::unknown);

	/* A TDE session: $TDEHOME/share/config/kdeglobals. */
	write(QStringLiteral("tde/share/config/kdeglobals"), "[General]\nwindowBackground=0,42,78\nwindowForeground=220,220,220\n");
	const QByteArray was_desktop = qgetenv("XDG_CURRENT_DESKTOP");
	const QByteArray was_tde = qgetenv("TDEHOME");
	qputenv("XDG_CURRENT_DESKTOP", "TDE");
	qputenv("TDEHOME", home.filePath(QStringLiteral("tde")).toLocal8Bit());
	if (QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Unknown)
		QCOMPARE(snurra_desktop_scheme(), snurra_scheme::dark);
	qputenv("SNURRA_THEME", "light");
	QCOMPARE(snurra_desktop_scheme(), snurra_scheme::light);
	qunsetenv("SNURRA_THEME");
	qputenv("XDG_CURRENT_DESKTOP", was_desktop);
	qputenv("TDEHOME", was_tde);
	if (was_tde.isEmpty()) qunsetenv("TDEHOME");
	if (was_desktop.isEmpty()) qunsetenv("XDG_CURRENT_DESKTOP");

	/* The dark scheme reads: text against ground, the played waveform and
	 * the curves against it. */
	const QPalette dark = snurra_dark_palette();
	QCOMPARE(snurra_scheme_of(dark.color(QPalette::Window), dark.color(QPalette::WindowText)), snurra_scheme::dark);
}

/*
 * A jump while playing is seamless (the holder, 2026-11-13: "the jump 4
 * bars isn't transparent"): the track goes on from exactly its step past
 * where it would have been -- not short by the time the switch takes to
 * be heard, which put a 4 bar jump off the beat.
 */
void embed_test::a_jump_while_playing_lands_exactly_its_step_on() {
	snurra_node node;
	snurra_tile_state state = placed(QStringLiteral("a"), 0, 0);
	state.duration_us = 60000000;
	state.controls = true;
	snurra_tile *tile = node.add_tile(state);
	node.show();
	QVERIFY(QTest::qWaitForWindowExposed(&node));
	tile->request_playing(true);
	QTest::qWait(800);
	const int rate = tile->deck()->rate();
	const qint64 before_us = snurra_clock_us();
	const int64_t before = tile->frame_now();
	tile->jump(1, false);   /* 10 s, a track under ten minutes */
	QTest::qWait(800);
	const qint64 after_us = snurra_clock_us();
	const int64_t after = tile->frame_now();
	const int64_t expected = before + snurra_us_to_frames(after_us - before_us, rate) + snurra_us_to_frames(10000000, rate);
	const double off_ms = double(after - expected) * 1000 / rate;
	qInfo("after the jump: %.1f ms off where it would be", off_ms);
	QVERIFY2(std::abs(off_ms) < 5, qPrintable(QStringLiteral("%1 ms off its step").arg(off_ms)));

	/* A tile on its own, as a DJ player's deck: the same. */
	QTemporaryDir dir;
	QFile file(dir.filePath(QStringLiteral("deck.wav")));
	QVERIFY(file.open(QIODevice::WriteOnly));
	/* Steady, so no beat is found and the step is 10 s, not 4 bars:
	 * make_wav repeats each 65536 frames, which reads as one. */
	file.write(make_wav_of(48000 * 40, [](int) { return 1000; }));
	file.close();
	snurra_dj dj;
	snurra_media media;
	media.path = file.fileName();
	QVERIFY(dj.load(0, media));
	dj.show();
	snurra_tile *deck = dj.findChild<QWidget *>(QStringLiteral("snurra-deck-a"))->findChild<snurra_tile *>();
	QVERIFY(deck);
	QTRY_VERIFY_WITH_TIMEOUT(deck->deck()->length() > 0, 3000);
	deck->set_playing(true);
	QTest::qWait(800);
	const int deck_rate = deck->deck()->rate();
	const qint64 deck_before_us = snurra_clock_us();
	const int64_t deck_before = deck->frame_now();
	deck->jump(1, false);
	QTest::qWait(800);
	const int64_t deck_expected = deck_before + snurra_us_to_frames(snurra_clock_us() - deck_before_us, deck_rate)
	                              + snurra_us_to_frames(10000000, deck_rate);
	const double deck_off_ms = double(deck->frame_now() - deck_expected) * 1000 / deck_rate;
	qInfo("a deck after the jump: %.1f ms off where it would be", deck_off_ms);
	QVERIFY2(std::abs(deck_off_ms) < 5, qPrintable(QStringLiteral("a deck %1 ms off its step").arg(deck_off_ms)));
}

/*
 * The DJ player (embed.h 3, the holder's simplified Traktor): two decks
 * and a browser. Loaded by a call, by a double-click in the browser --
 * onto the deck that is empty -- and by a drop on a deck; the crossfader
 * sends A alone, both at equal power, or B alone; sync puts one deck's
 * beat on the other's; a tune let go over nothing opens no player.
 */
void embed_test::the_dj_player_loads_two_decks_and_crosses_them() {
	QTemporaryDir dir;
	QStringList paths;
	for (const QString &name : { QStringLiteral("one.wav"), QStringLiteral("two.wav"), QStringLiteral("three.wav") }) {
		QFile file(dir.filePath(name));
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write(make_wav(48000 * 4));
		paths << file.fileName();
	}
	snurra_dj dj;
	dj.set_browser_folder(dir.path());
	dj.resize(900, 600);
	dj.show();
	QVERIFY(QTest::qWaitForWindowExposed(&dj));
	QVERIFY(dj.loaded(0).isEmpty() && dj.loaded(1).isEmpty());
	snurra_media media;
	media.path = paths[0];
	QVERIFY(dj.load(0, media));
	QCOMPARE(dj.loaded(0), QStringLiteral("one.wav"));
	QVERIFY(!dj.load(2, media));

	snurra_library *browser = dj.findChild<snurra_library *>();
	QVERIFY(browser);
	QTRY_COMPARE_WITH_TIMEOUT(browser->shown().size(), 3, 5000);
	QTreeView *list = browser->findChild<QTreeView *>();
	const int row = int(browser->shown().indexOf(paths[2]));
	const QRect at = list->visualRect(list->model()->index(row, 0));
	QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, at.center());
	QTest::mouseDClick(list->viewport(), Qt::LeftButton, {}, at.center());
	QCOMPARE(dj.loaded(1), QStringLiteral("three.wav"));

	/* Dropped on deck A: in place of what it had. */
	QWidget *deck_a = dj.findChild<QWidget *>(QStringLiteral("snurra-deck-a"));
	QVERIFY(deck_a);
	std::unique_ptr<QMimeData> mime(snurra_library::mime_for({ paths[1] }));
	const QPoint middle = deck_a->rect().center();
	QDragEnterEvent enter(middle, Qt::CopyAction, mime.get(), Qt::LeftButton, {});
	QCoreApplication::sendEvent(deck_a, &enter);
	QVERIFY(enter.isAccepted());
	QDropEvent drop(QPointF(middle), Qt::CopyAction, mime.get(), Qt::LeftButton, {});
	QCoreApplication::sendEvent(deck_a, &drop);
	QCOMPARE(dj.loaded(0), QStringLiteral("two.wav"));

	/* The deck's tile shown: one replaced goes at the next turn of the loop. */
	auto shown_on = [](QWidget *deck) {
		for (snurra_tile *tile : deck->findChildren<snurra_tile *>())
			if (tile->isVisible()) return tile;
		return static_cast<snurra_tile *>(nullptr);
	};
	snurra_tile *a = shown_on(deck_a);
	snurra_tile *b = shown_on(dj.findChild<QWidget *>(QStringLiteral("snurra-deck-b")));
	QCOMPARE(a->state().source, paths[1]);
	QVERIFY(a && b);
	/* With both decks in, it still fits a phone's portrait, 412 wide:
	 * each deck half of it, every control in its row. */
	qInfo("least width with two decks: %d", dj.minimumSizeHint().width());
	/* And at that width every button of a deck has the room it asks for:
	 * the key's three beside the tempo clipped their words (seen in this
	 * test, 2026-11-13: "in key" 28 wide of 47). */
	dj.resize(412, 915);
	QTest::qWait(50);
	a->set_beats(snurra_beat_grid{ 24000, 0, 10, 48000 });   /* the jumps' widest words, "4 bars" */
	QTest::qWait(50);
	/* The browser there gives the name half or more (it had 38 of 370:
	 * Folder hidden, Artist narrowed); wide, Folder is back. */
	QHeaderView *columns = list->header();
	qInfo("browser at 412: %d wide, Name %d, Artist %d", list->viewport()->width(), columns->sectionSize(0), columns->sectionSize(1));
	QVERIFY2(columns->sectionSize(0) * 2 >= list->viewport()->width(), qPrintable(QString::number(columns->sectionSize(0))));
	QVERIFY(columns->isSectionHidden(4));
	for (QToolButton *button : deck_a->findChildren<QToolButton *>())
		if (button->isVisibleTo(&dj) && button->objectName().startsWith(QLatin1String("snurra-")))
			QVERIFY2(button->width() >= button->sizeHint().width(),
			         qPrintable(QStringLiteral("%1 %2 wide of %3").arg(button->objectName()).arg(button->width()).arg(button->sizeHint().width())));
	/* The browser's buttons too, and the search keeps room to type in. */
	for (QPushButton *button : browser->findChildren<QPushButton *>())
		if (button->isVisibleTo(&dj))
			QVERIFY2(button->width() >= button->sizeHint().width(),
			         qPrintable(QStringLiteral("%1 %2 wide of %3").arg(button->text()).arg(button->width()).arg(button->sizeHint().width())));
	QLineEdit *search = browser->findChild<QLineEdit *>();
	qInfo("browser at 412: search %d wide", search->width());
	QVERIFY2(search->width() >= 80, qPrintable(QString::number(search->width())));
	dj.resize(900, 600);
	QTest::qWait(50);
	QVERIFY(!columns->isSectionHidden(4));
	QVERIFY2(dj.minimumSizeHint().width() <= 412, qPrintable(QString::number(dj.minimumSizeHint().width())));
	dj.set_crossfader(-1);
	QCOMPARE(a->deck()->fade_to(), 1.0f);
	QVERIFY(b->deck()->fade_to() < 1e-6f);
	dj.set_crossfader(0);
	QVERIFY(qAbs(a->deck()->fade_to() - float(M_SQRT1_2)) < 1e-4f && qAbs(b->deck()->fade_to() - float(M_SQRT1_2)) < 1e-4f);
	dj.set_crossfader(1);
	QVERIFY(a->deck()->fade_to() < 1e-6f);
	QCOMPARE(b->deck()->fade_to(), 1.0f);

	/* Sync: B's 126 onto A's 120, from either deck's button. */
	a->set_beats(snurra_beat_grid{ 24000, 0, 10, 48000 });
	b->set_beats(snurra_beat_grid{ 48000 * 60.0 / 126, 0, 10, 48000 });
	QToolButton *sync = nullptr;
	for (QToolButton *button : b->findChildren<QToolButton *>())
		if (button->text() == QLatin1String("sync")) sync = button;
	QVERIFY(sync && sync->isEnabled());
	sync->click();
	QVERIFY2(qAbs(b->tempo() - 120.0f / 126) < 1e-3f, qPrintable(QString::number(b->tempo())));

	/* Let go over nothing: no new player. */
	browser->dragged_out({ paths[0] }, false, QPoint(-10000, -10000));
}

/*
 * The cue, a DJ's (the holder's simplified Traktor): paused at 2 s, the
 * cue button sets it there; played on, the button goes back to 2 s and
 * stops; a handover keeps it.
 */
void embed_test::the_cue_is_set_paused_and_returned_to_playing() {
	QTemporaryDir dir;
	QFile file(dir.filePath(QStringLiteral("cue.wav")));
	QVERIFY(file.open(QIODevice::WriteOnly));
	file.write(make_wav_of(48000 * 20, [](int) { return 1000; }));
	file.close();
	snurra_dj dj;
	snurra_media media;
	media.path = file.fileName();
	QVERIFY(dj.load(0, media));
	dj.show();
	snurra_tile *deck = dj.findChild<QWidget *>(QStringLiteral("snurra-deck-a"))->findChild<snurra_tile *>();
	QVERIFY(deck);
	QTRY_COMPARE_WITH_TIMEOUT(deck->state().duration_us, qint64(20000000), 3000);
	QToolButton *cue = deck->findChild<QToolButton *>(QStringLiteral("snurra-cue"));
	QVERIFY(cue);
	QCOMPARE(deck->cue_us(), qint64(-1));
	deck->seek_by(2000000);
	QTRY_VERIFY_WITH_TIMEOUT(qAbs(deck->state().position_us - 2000000) < 20000, 2000);
	cue->click();
	QVERIFY2(qAbs(deck->cue_us() - 2000000) < 20000, qPrintable(QString::number(deck->cue_us())));
	deck->set_playing(true);
	QTest::qWait(700);
	QVERIFY(deck->state().position_us > 2400000);
	cue->click();
	QVERIFY2(!deck->playing(), "the cue did not stop it");
	QTRY_VERIFY_WITH_TIMEOUT(qAbs(deck->state().position_us - deck->cue_us()) < 20000, 2000);

	/* The EQ's dials: the low one down to the kill, a step short of it
	 * -22 dB, kept by a handover, put back by a double-click. */
	QDial *low = deck->findChild<QDial *>(QStringLiteral("snurra-eq-low"));
	QVERIFY(low && deck->findChild<QDial *>(QStringLiteral("snurra-eq-mid")) && deck->findChild<QDial *>(QStringLiteral("snurra-eq-high")));
	QCOMPARE(low->value(), (low->minimum() + low->maximum()) / 2);   /* 0 dB at the top */
	low->setValue(low->minimum() + 1);
	QCOMPARE(deck->state().eq_db[0], -22.0f);
	low->setValue(low->minimum());
	QCOMPARE(deck->state().eq_db[0], SNURRA_EQ_KILL);
	low->setValue(low->maximum());
	QCOMPARE(deck->state().eq_db[0], 6.0f);
	low->setValue(low->minimum());

	QByteArray bytes;
	QDataStream out(&bytes, QIODevice::WriteOnly);
	out << deck->state();
	snurra_tile_state back;
	QDataStream in(bytes);
	in >> back;
	QCOMPARE(back.cue_us, deck->cue_us());
	QCOMPARE(back.eq_db[0], SNURRA_EQ_KILL);
	QTest::mouseDClick(low, Qt::LeftButton);
	QCOMPARE(deck->state().eq_db[0], 0.0f);
}

/*
 * Beat grids kept (beats.h): a 120 BPM kick analysed once is kept in the
 * cache -- this run's own, $SNURRA_CACHE -- given again at once and at
 * another rate, shown as its BPM in the library, and found again only
 * when the file changes.
 */
void embed_test::a_beat_found_once_is_kept_and_shown() {
	QTemporaryDir dir;
	const QString path = kick_file(dir, QStringLiteral("kick.wav"), 120, 0.1);
	snurra_beat_grid kept;
	QVERIFY2(!snurra_cached_beats(path, -1, 48000, kept), "a grid kept before any was found");
	auto analyse = [&path](snurra_beat_grid &grid) {
		std::atomic<bool> done{ false };
		QElapsedTimer took;
		took.start();
		snurra_beats beats(path, 48000, [&](const snurra_beat_grid &found) {
			grid = found;
			done = true;
		});
		while (!done && took.elapsed() < 20000) QTest::qWait(1);
		return took.elapsed();
	};
	snurra_beat_grid first;
	const qint64 first_ms = analyse(first);
	QVERIFY(first.valid());
	QVERIFY(snurra_cached_beats(path, -1, 48000, kept));
	QCOMPARE(kept.period, first.period);
	QCOMPARE(kept.first, first.first);
	/* Kept, it is what is given, not found again: a grid kept on purpose
	 * unlike the file's own comes back as it was kept. */
	snurra_beat_grid planted = first;
	planted.period = first.period * 1.5;
	snurra_cache_beats(path, -1, planted);
	snurra_beat_grid again;
	const qint64 again_ms = analyse(again);
	qInfo("found in %lld ms, then kept in %lld ms", first_ms, again_ms);
	QVERIFY2(again.period == planted.period, "the kept grid was not used");
	snurra_cache_beats(path, -1, first);
	snurra_beat_grid slower;
	QVERIFY(snurra_cached_beats(path, -1, 44100, slower));
	QVERIFY(qAbs(slower.bpm() - first.bpm()) < 1e-6);
	/* Kept by an older analysis -- before the dotted check, which turned
	 * 88 into 132 -- it is not given, but found again. */
	{
		QSettings ini(qEnvironmentVariable("SNURRA_CACHE") + QStringLiteral("/beats.ini"), QSettings::IniFormat);
		int aged = 0;
		for (const QString &key : ini.allKeys()) {
			QString value = ini.value(key).toString();
			const QStringList fields = value.split(QLatin1Char(' '));
			if (fields.size() > 2 && fields.at(2).startsWith(QLatin1Char('a'))) {
				QStringList older = fields;
				older[2] = QStringLiteral("a%1").arg(fields.at(2).mid(1).toInt() - 1);
				ini.setValue(key, older.join(QLatin1Char(' ')));
				++aged;
			}
		}
		ini.sync();
		QVERIFY(aged > 0);
	}
	QVERIFY2(!snurra_cached_beats(path, -1, 48000, kept), "a grid an older analysis kept was given");
	snurra_cache_beats(path, -1, first);

	snurra_library library(dir.path());
	QTRY_VERIFY_WITH_TIMEOUT(!library.scanning(), 5000);
	QAbstractItemModel *model = library.findChild<QTreeView *>()->model();
	QTRY_COMPARE_WITH_TIMEOUT(model->index(0, 2).data().toString(), QString::number(first.bpm(), 'f', 1), 5000);

	/* Changed, the file is found again. */
	QFile file(path);
	QVERIFY(file.open(QIODevice::Append));
	file.write(QByteArray(4, '\0'));
	file.close();
	QVERIFY2(!snurra_cached_beats(path, -1, 48000, kept), "a changed file's old grid was given");
}

/*
 * The key (beats.h) in the library: a held C major chord and a bare kick.
 * Asked, the chord gets 8B C and the kick, with no key, none; sorted by
 * Key, the one with none is last whichever way.
 */
void embed_test::the_library_shows_each_tunes_key() {
	QTemporaryDir dir;
	const QString kick = kick_file(dir, QStringLiteral("a kick.wav"), 120, 0.1);
	const QString chord = dir.filePath(QStringLiteral("b chord.wav"));
	QFile file(chord);
	QVERIFY(file.open(QIODevice::WriteOnly));
	file.write(make_wav_of(48000 * 12, [](int i) {
		double v = 0;
		for (int midi : { 48, 60, 64, 67 })
			v += std::sin(2 * M_PI * 440 * std::pow(2.0, (midi - 69) / 12.0) * i / 48000.0);
		return int(5000 * v);
	}));
	file.close();
	snurra_library library(dir.path());
	QTRY_VERIFY_WITH_TIMEOUT(!library.scanning(), 5000);
	QTreeView *view = library.findChild<QTreeView *>();
	QAbstractItemModel *model = view->model();
	QCOMPARE(model->headerData(3, Qt::Horizontal).toString(), QStringLiteral("Key"));
	auto key_of = [&](const QString &path) { return model->index(int(library.shown().indexOf(path)), 3).data().toString(); };
	library.findChild<QPushButton *>(QStringLiteral("snurra-find-tempos"))->click();
	QTRY_VERIFY_WITH_TIMEOUT(!library.finding_tempos(), 20000);
	QCOMPARE(key_of(chord), QStringLiteral("8B C"));
	QVERIFY(key_of(kick).isEmpty());
	view->sortByColumn(3, Qt::AscendingOrder);
	QCOMPARE(library.shown(), (QStringList{ chord, kick }));
	view->sortByColumn(3, Qt::DescendingOrder);
	QCOMPARE(library.shown(), (QStringList{ chord, kick }));
}

/*
 * A tile's beat and key as heard (a DJ deck's head): 120 BPM in A minor,
 * at +6 % without key lock 127.2 BPM and a semitone up, Bb minor (3A);
 * with key lock the key stays 8A, and moves only by the key knob.
 */
void embed_test::a_tile_says_its_beat_and_key_as_heard() {
	snurra_node node;
	snurra_tile_state state = placed(QStringLiteral("a"), 0, 0);
	state.controls = true;
	snurra_tile *tile = node.add_tile(state);
	QLabel *said = node.findChild<QLabel *>(QStringLiteral("snurra-beat"));
	QVERIFY(said);
	snurra_beat_grid grid;
	grid.rate = 48000;
	grid.period = 24000;
	grid.key = 21;
	tile->set_beats(grid);
	QCOMPARE(said->text(), QStringLiteral("120.0 BPM 8A"));
	tile->set_tempo(1.06f);
	QCOMPARE(said->text(), QStringLiteral("127.2 BPM 3A"));
	QCOMPARE(said->toolTip(), QStringLiteral("Key 3A Bbm as heard"));
	tile->set_key_lock(true);
	QCOMPARE(said->text(), QStringLiteral("127.2 BPM 8A"));

	/* The key knob: two semitones up is B minor, kept by a handover, put
	 * back by a double-click. */
	QDial *knob = node.findChild<QDial *>(QStringLiteral("snurra-key-shift"));
	QVERIFY(knob);
	knob->setValue(2);
	QCOMPARE(tile->state().key_shift, 2);
	QCOMPARE(said->text(), QStringLiteral("127.2 BPM 10A"));
	QByteArray bytes;
	QDataStream out(&bytes, QIODevice::WriteOnly);
	out << tile->state();
	snurra_tile_state back;
	QDataStream in(bytes);
	in >> back;
	QCOMPARE(back.key_shift, 2);
	QCOMPARE(back.eq_db[0], 0.0f);
	QTest::mouseDClick(knob, Qt::LeftButton);
	QCOMPARE(tile->state().key_shift, 0);
	QCOMPARE(said->text(), QStringLiteral("127.2 BPM 8A"));
}

/*
 * A beat loop (a DJ's): on a deck playing at 240 BPM, 4 bars are 4 s.
 * Looped from the beat just played, it never passes the loop's end, goes
 * round at least twice in 10 s, and stands where an unbroken count round
 * the loop says -- each way back seamless, as the jumps are. Off, it
 * plays on past the end. In a window it is not offered.
 */
void embed_test::a_beat_loop_plays_its_bars_again_seamlessly() {
	QTemporaryDir dir;
	QFile file(dir.filePath(QStringLiteral("deck.wav")));
	QVERIFY(file.open(QIODevice::WriteOnly));
	file.write(make_wav_of(48000 * 40, [](int) { return 1000; }));
	file.close();
	snurra_dj dj;
	snurra_media media;
	media.path = file.fileName();
	QVERIFY(dj.load(0, media));
	dj.show();
	snurra_tile *deck = dj.findChild<QWidget *>(QStringLiteral("snurra-deck-a"))->findChild<snurra_tile *>();
	QVERIFY(deck);
	QTRY_VERIFY_WITH_TIMEOUT(deck->deck()->length() > 0, 3000);
	deck->set_playing(true);
	QTest::qWait(1500);   /* its own analysis done: no beat, which would undo ours */
	deck->set_beats(snurra_beat_grid{ 12000, 0, 10, 48000 });
	const int rate = deck->deck()->rate();
	const qint64 in_us = snurra_clock_us();
	const int64_t in = deck->deck()->position_at(in_us);
	deck->set_loop(true);
	QVERIFY(deck->looping());
	const int64_t from = in / 12000 * 12000;
	QCOMPARE(deck->loop_from(), from);
	QCOMPARE(deck->loop_to(), from + 192000);
	int64_t lowest = INT64_MAX, highest = 0;
	int rounds = 0;
	qint64 counted_at = -2000;
	int64_t last = in;
	QElapsedTimer looped;
	looped.start();
	while (looped.elapsed() < 10000) {
		QTest::qWait(20);
		const int64_t at = deck->frame_now();
		lowest = std::min(lowest, at);
		highest = std::max(highest, at);
		/* Once a pass: a seek asked shows its target until the deck takes
		 * it, then the old place until it is heard (deck.h), so the way
		 * back can read twice in a moment. */
		if (at < last - 96000 && looped.elapsed() - counted_at > 1000) {
			++rounds;
			counted_at = looped.elapsed();
		}
		last = at;
	}
	const qint64 out_us = snurra_clock_us();
	const int64_t at = deck->deck()->position_at(out_us);
	const int64_t counted = from + (in - from + snurra_us_to_frames(out_us - in_us, rate)) % 192000;
	const double off_ms = double(at - counted) * 1000 / rate;
	qInfo("looped %d times, between %lld and %lld of [%lld, %lld); %.1f ms off the count", rounds, (long long)lowest,
	      (long long)highest, (long long)from, (long long)(from + 192000), off_ms);
	QVERIFY(rounds >= 2 && rounds <= 3);
	QVERIFY2(highest < from + 192000 + 480, qPrintable(QString::number(highest - from - 192000)));
	QVERIFY(lowest >= from);
	QVERIFY2(std::abs(off_ms) < 5, qPrintable(QStringLiteral("%1 ms off").arg(off_ms)));
	deck->set_loop(false);
	QTRY_VERIFY_WITH_TIMEOUT(deck->frame_now() > from + 192000 + 4800, 6000);

	snurra_node node;
	snurra_tile *windowed = node.add_tile(placed(QStringLiteral("a"), 0, 0));
	windowed->set_loop(true);
	QVERIFY(!windowed->looping());
}

/*
 * A timed seek (start_at, as a jump and a loop's way back make one): asked
 * for a moment 400 ms on, the position reported never steps back before
 * that moment -- it is the old place, heard -- and is the target from
 * then on, while the plan is the target from the asking. It read the
 * target, then the old place, then the target, for some 100 ms before
 * every jump (2026-11-14).
 */
void embed_test::a_timed_seek_is_reported_where_it_is_heard() {
	QTemporaryDir dir;
	QFile file(dir.filePath(QStringLiteral("deck.wav")));
	QVERIFY(file.open(QIODevice::WriteOnly));
	file.write(make_wav_of(48000 * 40, [](int) { return 1000; }));
	file.close();
	snurra_dj dj;
	snurra_media media;
	media.path = file.fileName();
	QVERIFY(dj.load(0, media));
	dj.show();
	snurra_tile *tile = dj.findChild<QWidget *>(QStringLiteral("snurra-deck-a"))->findChild<snurra_tile *>();
	QVERIFY(tile);
	QTRY_VERIFY_WITH_TIMEOUT(tile->deck()->length() > 0, 3000);
	tile->set_playing(true);
	QTest::qWait(800);
	snurra_deck *deck = tile->deck();
	const qint64 asked_at = snurra_clock_us();
	const qint64 moment = asked_at + 400000;
	const int64_t target = deck->position_at(asked_at) + 20 * 48000;
	deck->start_at(target, moment);
	int64_t last = -1;
	int backward = 0;
	int planned_wrong = 0;
	double worst_after_ms = 0;
	while (snurra_clock_us() < moment + 400000) {
		QTest::qWait(5);
		const qint64 now = snurra_clock_us();
		const int64_t heard = deck->position_at(now);
		if (now < moment - 20000) {
			if (heard < last) ++backward;
			if (heard >= target) ++backward;   /* the target before its moment */
			if (deck->planned_at(now) < target) ++planned_wrong;
			last = heard;
		} else if (now > moment + 100000) {
			const int64_t expected = target + snurra_us_to_frames(now - moment, 48000);
			worst_after_ms = std::max(worst_after_ms, std::abs(double(heard - expected)) * 1000 / 48000);
		}
	}
	qInfo("before the moment: %d steps back or early; plan off %d times; after it, %.1f ms off at worst", backward, planned_wrong, worst_after_ms);
	QCOMPARE(backward, 0);
	QCOMPARE(planned_wrong, 0);
	QVERIFY2(worst_after_ms < 5, qPrintable(QString::number(worst_after_ms)));
}

/*
 * Key sync (Traktor's): the leader in A minor, 8A. D minor (7A) goes down
 * five semitones to A minor; C major (8B, its relative) stays; at +6 %
 * without key lock D minor is heard as Eb minor (2A) and goes six up.
 * The leader's own shift counts: shifted two up, to B minor (10A), D
 * minor goes three down, to B minor too.
 */
void embed_test::key_sync_moves_a_tile_into_the_leaders_key() {
	snurra_node node;
	snurra_tile_state state = placed(QStringLiteral("a"), 0, 0);
	state.controls = true;
	snurra_tile *leader = node.add_tile(state);
	snurra_tile *mine = node.add_tile(placed(QStringLiteral("b"), 0, 1));
	snurra_beat_grid grid;
	grid.rate = 48000;
	grid.period = 24000;
	grid.key = 21;
	leader->set_beats(grid);
	QToolButton *button = node.findChild<QToolButton *>(QStringLiteral("snurra-key-sync"));
	QVERIFY(button && button->isEnabled());
	auto synced = [&](int key, float tempo, bool key_lock) {
		snurra_beat_grid ours = grid;
		ours.key = key;
		mine->set_beats(ours);
		mine->set_key_lock(key_lock);
		mine->set_tempo(tempo);
		mine->set_key_shift(0);
		if (!mine->match_key_of(leader)) return 99;
		return mine->state().key_shift;
	};
	QCOMPARE(synced(14, 1.0f, true), -5);
	QCOMPARE(snurra_key_name(mine->key_heard()), QStringLiteral("8A Am"));
	QCOMPARE(synced(0, 1.0f, true), 0);
	QCOMPARE(synced(14, 1.06f, false), 6);
	QCOMPARE(snurra_key_name(mine->key_heard()), QStringLiteral("8A Am"));
	leader->set_key_shift(2);
	QCOMPARE(synced(14, 1.0f, true), -3);
	QCOMPARE(snurra_key_name(mine->key_heard()), QStringLiteral("10A Bm"));
	snurra_beat_grid none = grid;
	none.key = -1;
	mine->set_beats(none);
	QVERIFY(!mine->match_key_of(leader));
}

/*
 * Find tempos (the library's button): a folder of a 120 kick, a 126 kick
 * and a steady tone. Listed, nothing is analysed, however long it waits;
 * asked, the kicks get their BPM and keep it (beats.h), and the tone,
 * with no beat, stays blank.
 */
void embed_test::the_library_finds_tempos_only_when_asked() {
	QTemporaryDir dir;
	const QString slow = kick_file(dir, QStringLiteral("a slow.wav"), 120, 0.1);
	const QString fast = kick_file(dir, QStringLiteral("b fast.wav"), 126, 0.2);
	QFile tone(dir.filePath(QStringLiteral("c tone.wav")));
	QVERIFY(tone.open(QIODevice::WriteOnly));
	tone.write(make_wav_of(48000 * 8, [](int i) { return int(8000 * std::sin(2 * M_PI * 440 * i / 48000.0)); }));
	tone.close();
	snurra_library library(dir.path());
	QTRY_VERIFY_WITH_TIMEOUT(!library.scanning(), 5000);
	QAbstractItemModel *model = library.findChild<QTreeView *>()->model();
	auto bpm_of = [&](const QString &path) { return model->index(int(library.shown().indexOf(path)), 2).data().toString(); };
	QTest::qWait(500);
	QVERIFY2(bpm_of(slow).isEmpty() && bpm_of(fast).isEmpty(), "a tempo was found without being asked for");
	snurra_beat_grid kept;
	QVERIFY(!snurra_cached_beats(slow, -1, 48000, kept));

	QPushButton *find = library.findChild<QPushButton *>(QStringLiteral("snurra-find-tempos"));
	QVERIFY(find);
	find->click();
	QVERIFY(library.finding_tempos());
	QTRY_VERIFY_WITH_TIMEOUT(!library.finding_tempos(), 20000);
	QCOMPARE(bpm_of(slow), QStringLiteral("120.0"));
	QCOMPARE(bpm_of(fast), QStringLiteral("126.0"));
	QVERIFY(bpm_of(dir.filePath(QStringLiteral("c tone.wav"))).isEmpty());
	QVERIFY(find->isEnabled());
	QVERIFY(snurra_cached_beats(slow, -1, 48000, kept) && qAbs(kept.bpm() - 120) < 0.1);

	/* Sorted by the column clicked: BPM by number, the tone with none
	 * last whichever way; Folder, the first order, by path. */
	QTreeView *view = library.findChild<QTreeView *>();
	const QString tone_path = dir.filePath(QStringLiteral("c tone.wav"));
	view->sortByColumn(2, Qt::DescendingOrder);
	QCOMPARE(library.shown(), (QStringList{ fast, slow, tone_path }));
	view->sortByColumn(2, Qt::AscendingOrder);
	QCOMPARE(library.shown(), (QStringList{ slow, fast, tone_path }));
	view->sortByColumn(0, Qt::DescendingOrder);
	QCOMPARE(library.shown(), (QStringList{ tone_path, fast, slow }));
	view->sortByColumn(4, Qt::AscendingOrder);
	QCOMPARE(library.shown(), (QStringList{ slow, fast, tone_path }));
}

/*
 * A cue sheet beside a file, as hydra writes beside a saved mix: its
 * tracks are the file's chapters, from INDEX 01 at 75 frames a second,
 * titled "PERFORMER - TITLE" -- the sheet's performer where a track has
 * none -- INDEX 00 passed over. A sheet of two files gives each its own.
 */
void embed_test::a_cue_sheet_beside_a_file_is_its_tracklist() {
	QTemporaryDir dir;
	auto write = [&dir](const QString &name, const QByteArray &bytes) {
		QFile file(dir.filePath(name));
		return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
	};
	QVERIFY(write(QStringLiteral("mix.wav"), make_wav_of(48000 * 10, [](int) { return 0; })));
	QVERIFY(write(QStringLiteral("mix.cue"), "PERFORMER \"Sheet DJ\"\nTITLE \"The Mix\"\nFILE \"mix.wav\" WAVE\n"
	                                         "  TRACK 01 AUDIO\n    PERFORMER \"A\"\n    TITLE \"One\"\n    INDEX 01 00:00:00\n"
	                                         "  TRACK 02 AUDIO\n    TITLE \"Two\"\n    INDEX 01 00:03:00\n"
	                                         "  TRACK 03 AUDIO\n    PERFORMER \"C\"\n    TITLE \"Three\"\n"
	                                         "    INDEX 00 00:05:00\n    INDEX 01 00:06:37\n"));
	const std::vector<snurra_chapter> read = snurra_read_chapters(dir.filePath(QStringLiteral("mix.wav")));
	QCOMPARE(int(read.size()), 3);
	QCOMPARE(read[0].start_us, qint64(0));
	QCOMPARE(read[1].start_us, qint64(3000000));
	QCOMPARE(read[2].start_us, qint64(6000000 + 37 * 1000000 / 75));
	QCOMPARE(read[0].title, QStringLiteral("A - One"));
	QCOMPARE(read[1].title, QStringLiteral("Sheet DJ - Two"));
	QCOMPARE(read[2].title, QStringLiteral("C - Three"));

	QVERIFY(write(QStringLiteral("two.cue"), "FILE \"a.wav\" WAVE\n  TRACK 01 AUDIO\n    TITLE \"A1\"\n    INDEX 01 00:00:00\n"
	                                         "  TRACK 02 AUDIO\n    TITLE \"A2\"\n    INDEX 01 00:01:00\n"
	                                         "FILE \"b.wav\" WAVE\n  TRACK 03 AUDIO\n    TITLE \"B1\"\n    INDEX 01 00:00:00\n"));
	const std::vector<snurra_chapter> b = snurra_read_cue(dir.filePath(QStringLiteral("two.cue")), dir.filePath(QStringLiteral("b.wav")));
	QCOMPARE(int(b.size()), 1);
	QCOMPARE(b[0].title, QStringLiteral("B1"));
	QCOMPARE(int(snurra_read_cue(dir.filePath(QStringLiteral("two.cue")), dir.filePath(QStringLiteral("a.wav"))).size()), 2);
}

/*
 * A train's crossfade as curves (project.md sec 3b): A 10 s, B 4 s and C
 * 10 s in a lane, D 6 s alone below. The overlap is 5 s capped at half the
 * shorter, so 2 s each side of B: A fades out over its last fifth, B in
 * and out over a half each, C in over its first fifth; D, with no
 * neighbour, has none. Drawn: A's waveform is not what it is without.
 */
void embed_test::a_trains_crossfade_is_drawn_on_its_tiles() {
	snurra_node node;
	auto lasting = [](const QString &name, int row, int column, qint64 us) {
		snurra_tile_state state = placed(name, row, column);
		state.duration_us = us;
		return state;
	};
	snurra_tile *a = node.add_tile(lasting(QStringLiteral("a"), 0, 0, 10000000));
	snurra_tile *b = node.add_tile(lasting(QStringLiteral("b"), 0, 1, 4000000));
	snurra_tile *c = node.add_tile(lasting(QStringLiteral("c"), 0, 2, 10000000));
	snurra_tile *d = node.add_tile(lasting(QStringLiteral("d"), 1, 0, 6000000));
	node.resize(800, 300);
	node.show();
	QVERIFY(QTest::qWaitForWindowExposed(&node));
	auto near = [](QPair<double, double> got, double in, double out) {
		return qAbs(got.first - in) < 1e-3 && qAbs(got.second - out) < 1e-3;
	};
	QTRY_VERIFY_WITH_TIMEOUT(near(a->crossfade(), 0, 0.2), 2000);
	QVERIFY2(near(b->crossfade(), 0.5, 0.5), "B does not cross both ways over half of it");
	QVERIFY2(near(c->crossfade(), 0.2, 0), "C does not fade in over its first fifth");
	QVERIFY2(near(d->crossfade(), 0, 0), "a lone lane has a crossfade");

	QWidget *waveform = waveform_of(a);
	QVERIFY(waveform);
	const QImage with = waveform->grab().toImage();
	a->set_crossfade(0, 0);
	const QImage without = waveform->grab().toImage();
	int differ = 0;
	for (int y = 0; y < with.height(); ++y)
		for (int x = 0; x < with.width(); ++x)
			if (with.pixel(x, y) != without.pixel(x, y)) {
				QVERIFY2(x >= with.width() * 0.8 - 2, "the fade out is drawn outside A's last fifth");
				++differ;
			}
	QVERIFY2(differ > with.width() / 10, "A's fade out is not drawn");
}

/*
 * Sync (project.md sec 4): two kicks stacked and playing, 120 BPM and 126,
 * their beats apart. Sync on the 126: its tempo becomes 120/126, and once
 * the bend has played out its beats fall on the 120's, to a millisecond.
 */
void embed_test::sync_matches_a_beat_to_another() {
	QTemporaryDir dir;
	const QString slow = kick_file(dir, QStringLiteral("slow.wav"), 120, 0.1);
	const QString fast = kick_file(dir, QStringLiteral("fast.wav"), 126, 0.33);
	snurra_node node;
	snurra_tile_state a = placed(slow, 0, 0);
	snurra_tile_state b = placed(fast, 1, 0);
	a.duration_us = b.duration_us = 20000000;
	snurra_tile *leader = node.add_tile(a);
	snurra_tile *follower = node.add_tile(b);
	QTRY_VERIFY_WITH_TIMEOUT(leader->beats().valid() && follower->beats().valid(), 15000);
	leader->request_playing(true);
	QTest::qWait(500);
	QVERIFY(leader->playing() && follower->playing());
	QVERIFY(node.sync(follower));
	QVERIFY2(std::abs(follower->tempo() * follower->beats().bpm() - leader->tempo() * leader->beats().bpm()) < 0.01,
	         qPrintable(QStringLiteral("tempo %1 makes %2 BPM against %3")
	                            .arg(follower->tempo())
	                            .arg(follower->tempo() * follower->beats().bpm())
	                            .arg(leader->beats().bpm())));
	QTest::qWait(3500);   /* half a beat at most, bent at a tenth: under 2.5 s */
	auto phase_ms = [](snurra_tile *tile) {
		const snurra_beat_grid grid = tile->beats();
		const double beats = (double(tile->frame_now()) - grid.first) / grid.period;
		return (beats - std::floor(beats)) * grid.period / 48000 / tile->tempo() * 1000;
	};
	double apart = phase_ms(follower) - phase_ms(leader);
	const double beat_ms = 60000.0 / 120;
	if (apart > beat_ms / 2) apart -= beat_ms;
	if (apart < -beat_ms / 2) apart += beat_ms;
	qInfo("sync: tempo %.5f, beats %.2f ms apart", follower->tempo(), apart);
	QVERIFY2(std::abs(apart) < 1, qPrintable(QStringLiteral("the beats %1 ms apart").arg(apart)));
}

/*
 * Tempo in a stack (project.md sec 4): the lower lane at +10 %. A seek of
 * the group to 10 s puts the upper at 10 s of its track and the lower at
 * 11 s; played on, the lower's track goes 1.1 times as fast.
 */
void embed_test::a_lane_at_a_tempo_keeps_the_groups_time() {
	snurra_node node;
	snurra_tile_state a = placed(QStringLiteral("a"), 0, 0);
	snurra_tile_state b = placed(QStringLiteral("b"), 1, 0);
	a.duration_us = b.duration_us = 60000000;
	snurra_tile *upper = node.add_tile(a);
	snurra_tile *lower = node.add_tile(b);
	lower->set_tempo(1.1f);
	upper->jump(1, false);   /* the group, paused, to 10 s */
	QTest::qWait(100);
	QCOMPARE(upper->state().position_us, qint64(10000000));
	QCOMPARE(lower->state().position_us, qint64(11000000));
	upper->request_playing(true);
	QTest::qWait(1200);
	const double up = double(upper->state().position_now() - 10000000);
	const double down = double(lower->state().position_now() - 11000000);
	qInfo("played: upper %.0f us, lower %.0f us, %.4f times", up, down, down / up);
	QVERIFY2(up > 500000, "the group did not play");
	QVERIFY2(std::abs(down / up - 1.1) < 0.01, qPrintable(QStringLiteral("the lower went %1 times as fast").arg(down / up)));
}

/*
 * The facade (embed.h), as BeerSSH uses it: a host's bytes, a quarter here,
 * play and hold where they stop; closing every tile says emptied. Names say
 * what snurra would try.
 */
void embed_test::a_player_plays_a_hosts_bytes_and_says_when_empty() {
	QCOMPARE(SNURRA_EMBED_VERSION, 4);   /* 2: subtitles; 3: snurra_dj; 4: position, state, ended */
	QVERIFY(snurra_player::can_play(QStringLiteral("set.FLAC")));
	QVERIFY(snurra_player::can_play(QStringLiteral("film.mkv")));
	QVERIFY(!snurra_player::can_play(QStringLiteral("notes.txt")));
	QVERIFY(!snurra_player::can_play(QStringLiteral("README")));

	auto bytes = std::make_shared<memory_bytes>(make_wav(48000 * 4));
	bytes->arrive(0, bytes->size() / 4);
	snurra_player player;
	QSignalSpy emptied(&player, &snurra_player::emptied);
	QVERIFY(!player.open(snurra_media{ QStringLiteral("nothing.wav"), QString(), nullptr }));
	QVERIFY(player.open(snurra_media{ QStringLiteral("song.wav"), QString(), bytes }));
	QCOMPARE(player.count(), 1);
	snurra_tile *tile = player.findChild<snurra_tile *>();
	QVERIFY(tile);
	qint64 held = -1;
	qint64 last = -2;
	for (int i = 0; i < 60 && held < 0; ++i) {
		QTest::qWait(100);
		const qint64 now = tile->state().position_now();
		if (now > 0 && now == last) held = now;
		last = now;
	}
	QVERIFY2(held > 500000 && held < 1100000, qPrintable(QStringLiteral("held at %1 us").arg(held)));
	player.close_all();
	QCOMPARE(emptied.count(), 1);
	QCOMPARE(player.count(), 0);
}

void embed_test::a_player_says_where_each_tile_is_and_that_it_ended() {
	using state = snurra_player::play_state;
	auto bytes = std::make_shared<memory_bytes>(make_wav(48000 * 2));
	bytes->arrive(0, bytes->size() / 4);   /* about half a second */
	snurra_player player;
	QSignalSpy ended(&player, &snurra_player::ended);
	QCOMPARE(player.state_of(0), state::none);
	QCOMPARE(player.position_ms(0), qint64(-1));
	QVERIFY(player.open(snurra_media{ QStringLiteral("song.wav"), QString(), bytes }));
	QCOMPARE(player.state_of(1), state::none);

	/* It plays, then waits where the bytes stop -- asked to play, silent. */
	QTRY_COMPARE_WITH_TIMEOUT(player.state_of(0), state::playing, 3000);
	QTRY_COMPARE_WITH_TIMEOUT(player.state_of(0), state::waiting, 5000);
	const qint64 held = player.position_ms(0);
	QVERIFY2(held > 250 && held < 600, qPrintable(QStringLiteral("waiting at %1 ms").arg(held)));
	QTRY_COMPARE_WITH_TIMEOUT(player.length_ms(0), qint64(2000), 3000);
	QCOMPARE(ended.count(), 0);

	/* Paused by the person is paused, not waiting. */
	snurra_tile *tile = player.findChild<snurra_tile *>();
	QVERIFY(tile);
	tile->set_playing(false);
	QCOMPARE(player.state_of(0), state::paused);
	tile->set_playing(true);

	/* The rest comes: it plays on, to the end, and says so once. */
	bytes->arrive(0, bytes->size());
	QTRY_COMPARE_WITH_TIMEOUT(player.state_of(0), state::playing, 3000);
	QVERIFY(player.position_ms(0) >= held);
	QVERIFY2(ended.wait(5000), "no ended() at the end");
	QCOMPARE(ended.count(), 1);
	QCOMPARE(ended.first().at(0).toString(), QStringLiteral("song.wav"));
	QCOMPARE(player.state_of(0), state::ended);
	QVERIFY(player.position_ms(0) > 1900);
	QTest::qWait(300);
	QCOMPARE(ended.count(), 1);
	QCOMPARE(player.count(), 1);   /* the tile stays */
}

void embed_test::a_player_says_what_it_cannot_play() {
	auto bytes = std::make_shared<memory_bytes>(QByteArray(100000, 'x'));
	bytes->arrive(0, bytes->size());
	snurra_player player;
	QSignalSpy failed(&player, &snurra_player::failed);
	QVERIFY(player.open(snurra_media{ QStringLiteral("junk.mp3"), QString(), bytes }));
	QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
	/* And the tile says so, where a person looks: across its waveform. */
	snurra_tile *tile = player.findChild<snurra_tile *>();
	QVERIFY(waveform_of(tile)->toolTip().contains(QStringLiteral("cannot play")));
	QCOMPARE(failed.first().at(0).toString(), QStringLiteral("junk.mp3"));
	QVERIFY(!failed.first().at(1).toString().isEmpty());
}

/* Bytes that count calls, and note any made once the host was told none
 * would come. */
class watched_bytes : public memory_bytes {
public:
	using memory_bytes::memory_bytes;
	int64_t read(int64_t offset, uint8_t *out, int64_t n) override {
		if (closed.load()) ++after;
		return memory_bytes::read(offset, out, n);
	}
	void wait(int ms) override {
		if (closed.load()) ++after;
		memory_bytes::wait(ms);
	}
	std::atomic<bool> closed{ false };
	std::atomic<int> after{ 0 };
};

/* Deleted while a reader waits on bytes not here: once the destructor has
 * returned, nothing calls the bytes again (embed.h). */
void embed_test::a_player_deleted_at_once_never_calls_the_bytes_again() {
	auto bytes = std::make_shared<watched_bytes>(make_wav(48000 * 4));
	bytes->arrive(0, bytes->size() / 8);
	/* Another player holds the engine, so its going does not join the
	 * readers for this one. */
	snurra_player other;
	auto held = std::make_shared<memory_bytes>(make_wav(48000));
	held->arrive(0, held->size());
	QVERIFY(other.open(snurra_media{ QStringLiteral("held.wav"), QString(), held }));
	auto player = std::make_unique<snurra_player>();
	QVERIFY(player->open(snurra_media{ QStringLiteral("song.wav"), QString(), bytes }));
	QTest::qWait(800);   /* playing, then waiting where the bytes stop */
	/* The control: a reader waiting on bytes not here does call them. */
	bytes->closed = true;
	std::this_thread::sleep_for(std::chrono::milliseconds(300));
	const int waiting = bytes->after.exchange(0);
	bytes->closed = false;
	qInfo("a waiting reader called the bytes %d times in 300 ms", waiting);
	QVERIFY2(waiting > 0, "the reader was not waiting on the bytes: nothing to prove");
	player.reset();
	bytes->closed = true;
	/* No event loop: nothing of snurra's can be swept up meanwhile, so a
	 * reader still alive would be calling. */
	std::this_thread::sleep_for(std::chrono::milliseconds(300));
	QCOMPARE(bytes->after.load(), 0);
	QTest::qWait(200);
	QCOMPARE(bytes->after.load(), 0);
}

int run_embed_tests(int argc, char **argv) {
	embed_test test;
	return QTest::qExec(&test, argc, argv);
}

#include "embed_test.moc"
