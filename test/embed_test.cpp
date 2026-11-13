/*
 * A tile in a program of its own (project.md sec 1: BeerSSH's tile,
 * fuzzypickles' player), made the way a consumer makes one --
 * snurra_tile::embedded, an engine of the consumer's, bytes from its
 * transport -- and seen from outside, through the tile's own state: it
 * plays, holds where the bytes stop and goes on when they come, and closing
 * it leaves no deck behind in the engine, whichever goes first.
 */
#include <QMimeData>
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
#include "snurra/source.h"
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
	void a_player_plays_a_hosts_bytes_and_says_when_empty();
	void a_player_says_what_it_cannot_play();
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
	QCOMPARE(SNURRA_EMBED_VERSION, 2);   /* 2: snurra_media::subtitles */
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
