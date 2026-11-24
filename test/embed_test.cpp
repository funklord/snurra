/*
 * A tile in a program of its own (project.md sec 1: BeerSSH's tile,
 * fuzzypickles' player), made the way a consumer makes one --
 * snurra_tile::embedded, an engine of the consumer's, bytes from its
 * transport -- and seen from outside, through the tile's own state: it
 * plays, holds where the bytes stop and goes on when they come, and closing
 * it leaves no deck behind in the engine, whichever goes first.
 */
#include <QMenu>
#include <QMessageBox>
#include <QTextDocument>
#include <QToolTip>
#include <QTimer>
#include <QApplication>
#include <QDial>
#include <QMimeData>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QElapsedTimer>
#include <QAbstractItemModel>
#include <QGuiApplication>
#include <QStyleHints>
#include <QSlider>
#include <QTreeView>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QToolButton>

#include <cmath>
#include <QLocalSocket>
#include <QScopeGuard>
#include <QDir>
#include <QCoreApplication>
#include <cstring>
#include <QComboBox>
#include <QGridLayout>
#include <QProgressBar>
#include <QFileInfo>
#include <QtTest>

#include <alsa/asoundlib.h>

#include <memory>

#include "snurra/clock.h"
#include "snurra/engine.h"
#include "snurra/embed.h"
#include "snurra/deck_waves.h"
#include "snurra/beats.h"
#include "snurra/midi.h"
#include "snurra/tile_state.h"
#include "snurra/timeline.h"
#include "snurra/wire.h"
#include "snurra/node.h"
#include "snurra/beats.h"
#include "snurra/library.h"
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
	void a_tile_dragged_below_another_is_a_lane_of_its_own();
	void a_ruler_marks_the_bars_on_the_lanes_scale();
	void a_tracks_menu_removes_it_from_its_window();
	void a_window_has_one_bar_for_its_tiles();
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
	void the_waveform_to_come_shows_on_a_hosts_dark_palette();
	void about_is_in_the_right_click_menu();
	void a_lane_dragged_later_starts_later_on_the_beat();
	void tiles_are_as_wide_as_their_time_on_the_timeline();
	void a_width_the_person_set_is_kept();
	void a_trains_next_tile_dragged_comes_in_on_the_beat();
	void loop_plays_again_from_the_start();
	void the_library_lists_finds_and_hands_out_tunes();
	void a_file_dropped_on_a_tile_takes_its_place();
	void a_tile_dropped_on_another_trades_places();
	void the_tempo_reaches_16_and_snaps_to_the_middle();
	void a_jump_while_playing_lands_exactly_its_step_on();
	void the_control_moved_is_told_from_the_noise();
	void a_controller_learned_by_holding_moves_the_deck();
	void a_nested_track_is_brought_to_the_level_its_deck_would_give();
	void a_player_in_a_tile_beats_with_the_track_it_sounds();
	void a_browse_knob_and_load_buttons_fill_the_decks();
	void an_encoder_about_64_steps_both_ways();
	void a_mixxx_mappings_plain_bindings_move_the_decks();
	void a_mappings_lights_show_the_deck_playing();
	void each_player_takes_the_bindings_named_for_it();
	void each_deck_has_its_side_of_the_crossfader();
	void the_decks_sit_as_the_person_arranged_them();
	void a_listener_hears_the_deck_its_plug_is_in();
	void a_listener_hears_one_track_of_a_decks_player();
	void the_decks_waveforms_scroll_stacked_above_them();
	void a_listener_plugs_into_a_windows_tile();
	void the_decks_show_the_player_they_play();
	void a_window_follows_the_player_on_the_decks();
	void a_player_unfolds_across_the_decks();
	void a_fader_ridden_is_written_into_the_track();
	void a_deck_started_by_hand_is_where_its_track_comes_in();
	void a_folder_chosen_while_another_lists_shows_only_its_own();
	void the_dj_player_loads_two_decks_and_crosses_them();
	void the_cue_is_set_paused_and_returned_to_playing();
	void the_eq_line_draws_its_slopes_and_splits();
	void decks_share_a_phones_width();
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
/*
 * One bar for the window (the holder, 2026-11-21): the tiles' own title
 * rows are hidden, each named on its waveform instead; the bar names the
 * tile last touched, its controls' handle pulls the controls out and in,
 * and its close takes every tile. A click on a waveform seeks when let
 * go -- a press alone does not, being maybe the start of a drag.
 */
/*
 * A tile dragged by its waveform to the bottom of another of its window
 * (the holder, 2026-11-21: "placed above or below in separate tracks,
 * the preview should show what is going to happen before the drop"):
 * while held, a faint copy of it shows where the new lane would be, and
 * nothing has moved yet; let go, it is a lane of its own below, coming in
 * when it came in before.
 */
void embed_test::a_tile_dragged_below_another_is_a_lane_of_its_own() {
	snurra_node node;
	snurra_tile *a = node.add_tile(placed(QStringLiteral("a"), 0, 0));
	snurra_tile *b = node.add_tile(placed(QStringLiteral("b"), 0, 1));
	node.show();
	QVERIFY(QTest::qWaitForWindowExposed(&node));
	QTest::qWait(100);
	QCOMPARE(b->row(), 0);
	QVERIFY(b->column() > a->column());
	const qint64 came_in = b->state().lane_start_us;
	QWidget *wave = waveform_of(b);
	auto move_to = [&](QWidget *over, QPoint at) {
		const QPoint global = over->mapToGlobal(at);
		QMouseEvent moved(QEvent::MouseMove, wave->mapFromGlobal(global), global, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
		QCoreApplication::sendEvent(wave, &moved);
	};
	QTest::mousePress(wave, Qt::LeftButton, Qt::NoModifier, QPoint(wave->width() / 2, wave->height() / 2));
	move_to(wave, QPoint(wave->width() / 2 + 20, wave->height() / 2));
	move_to(a, QPoint(a->width() / 2, a->height() - 3));
	move_to(a, QPoint(a->width() / 2, a->height() - 2));
	QWidget *ghost = node.findChild<QWidget *>(QStringLiteral("snurra-ghost"));
	QVERIFY2(ghost && ghost->isVisible(), "no preview while held");
	QVERIFY(ghost->geometry().center().y() > a->geometry().bottom() - a->height() / 2);
	QCOMPARE(b->row(), 0);   /* held: nothing moved yet */
	const QPoint let_go = a->mapToGlobal(QPoint(a->width() / 2, a->height() - 2));
	QTest::mouseRelease(wave, Qt::LeftButton, Qt::NoModifier, wave->mapFromGlobal(let_go));
	QVERIFY(!ghost->isVisible());
	QCOMPARE(a->row(), 0);
	QCOMPARE(b->row(), 1);
	QCOMPARE(b->column(), 0);
	/* It comes in where it did: after a, as the train had it. */
	QVERIFY2(b->state().lane_start_us > came_in, qPrintable(QString::number(b->state().lane_start_us)));
	QVERIFY(b->state().lane_start_us >= 2000000);
}

/*
 * The beats and bars along the top (the holder, 2026-11-21): under the
 * bar, over the lanes, on the lanes' own scale -- a 4 s track at 120 BPM
 * is two bars, so its bars lie half its width apart, the first where it
 * begins.
 */
void embed_test::a_ruler_marks_the_bars_on_the_lanes_scale() {
	snurra_node node;
	snurra_tile *a = node.add_tile(placed(QStringLiteral("a"), 0, 0));
	snurra_beat_grid grid;
	grid.rate = 48000;
	grid.period = 24000;   /* 120 BPM */
	grid.first = 0;
	a->set_beats(grid);
	node.resize(600, node.height());
	node.show();
	QVERIFY(QTest::qWaitForWindowExposed(&node));
	QWidget *ruler = node.findChild<QWidget *>(QStringLiteral("snurra-ruler"));
	QWidget *bar = node.findChild<QWidget *>(QStringLiteral("snurra-bar"));
	QVERIFY(ruler && ruler->isVisible() && bar);
	QVERIFY(ruler->geometry().top() > bar->geometry().top());
	QVERIFY(ruler->geometry().bottom() < a->geometry().top());
	ruler->repaint();
	const QVariantList bars = ruler->property("bars").toList();
	QVERIFY2(bars.size() >= 2, qPrintable(QString::number(bars.size())));
	const double left = a->x() - ruler->x();
	QVERIFY2(std::abs(bars[0].toDouble() - left) < 1.5, qPrintable(QStringLiteral("%1 against %2").arg(bars[0].toDouble()).arg(left)));
	const double apart = bars[1].toDouble() - bars[0].toDouble();
	QVERIFY2(std::abs(apart - a->width() / 2.0) < 2, qPrintable(QStringLiteral("%1 apart, tile %2 wide").arg(apart).arg(a->width())));
}

/*
 * A track taken out of a window by its menu (2026-11-21): its own title
 * row's close went with the row, so a right-click on a tile offers to
 * remove it -- that one, the rest left playing on.
 */
void embed_test::a_tracks_menu_removes_it_from_its_window() {
	snurra_node node;
	snurra_tile *a = node.add_tile(placed(QStringLiteral("a"), 0, 0));
	snurra_tile *b = node.add_tile(placed(QStringLiteral("b"), 0, 1));
	node.show();
	QVERIFY(QTest::qWaitForWindowExposed(&node));
	const QPointer<snurra_tile> kept = a;
	const QPointer<snurra_tile> gone = b;
	QTimer::singleShot(200, [] {
		QMenu *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
		if (!menu) return;
		for (QAction *action : menu->actions())
			if (action->objectName() == QLatin1String("snurra-remove")) action->trigger();
		menu->close();
	});
	const QPoint at = b->geometry().center();
	QContextMenuEvent asked(QContextMenuEvent::Mouse, at, node.mapToGlobal(at));
	QCoreApplication::sendEvent(&node, &asked);
	QCOMPARE(node.tile_count(), 1);
	QTRY_VERIFY_WITH_TIMEOUT(!gone, 2000);
	QVERIFY(kept);
}

void embed_test::a_window_has_one_bar_for_its_tiles() {
	QTemporaryDir dir;
	QStringList paths;
	for (const QString &name : { QStringLiteral("a.wav"), QStringLiteral("b.wav") }) {
		QFile file(dir.filePath(name));
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write(make_wav(48000 * 4));
		paths << file.fileName();
	}
	snurra_node node;
	snurra_tile *a = node.add_tile(placed(paths[0], 0, 0));
	snurra_tile *b = node.add_tile(placed(paths[1], 0, 1));
	node.show();
	QVERIFY(QTest::qWaitForWindowExposed(&node));
	QWidget *bar = node.findChild<QWidget *>(QStringLiteral("snurra-bar"));
	QVERIFY(bar && bar->isVisible());
	for (snurra_tile *tile : { a, b }) {
		QLabel *handle = tile->findChild<QLabel *>(QStringLiteral("snurra-handle"));
		QVERIFY(handle && !handle->isVisible());
		QVERIFY(bar->geometry().bottom() < tile->geometry().top());
	}
	QLabel *said = bar->findChild<QLabel *>(QStringLiteral("snurra-bar-title"));
	QVERIFY(said);
	QVERIFY2(said->text().startsWith(a->title()), qPrintable(said->text()));

	QWidget *wave = waveform_of(b);
	QTRY_VERIFY_WITH_TIMEOUT(b->deck()->length() > 0, 3000);
	const qint64 before = b->state().position_us;
	QTest::mousePress(wave, Qt::LeftButton, Qt::NoModifier, QPoint(wave->width() / 2, wave->height() / 2));
	QVERIFY2(said->text().startsWith(b->title()), qPrintable(said->text()));
	QCOMPARE(b->state().position_us, before);   /* pressed: not yet */
	QTest::mouseRelease(wave, Qt::LeftButton, Qt::NoModifier, QPoint(wave->width() / 2, wave->height() / 2));
	QTRY_VERIFY_WITH_TIMEOUT(b->state().position_us > before + 1000000, 2000);

	QToolButton *controls = bar->findChild<QToolButton *>(QStringLiteral("snurra-bar-controls"));
	QVERIFY(controls);
	const bool out = a->controls() || b->controls();
	QCOMPARE(controls->isChecked(), out);
	controls->click();
	QCOMPARE(a->controls() || b->controls(), !out);
	controls->click();
	QCOMPARE(a->controls() || b->controls(), out);

	QToolButton *close = bar->findChild<QToolButton *>(QStringLiteral("snurra-bar-close"));
	QVERIFY(close);
	close->click();
	QCOMPARE(node.tile_count(), 0);
}

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
	/* The window's one panel (2026-11-21): under every lane, as wide as
	 * the lanes, the lanes left where they were. */
	QCOMPARE(drawer->geometry().top(), c->geometry().bottom() + 1);
	QCOMPARE(drawer->geometry().left(), a->geometry().left());
	QCOMPARE(drawer->geometry().right(), b->geometry().right());
	QCOMPARE(top(c), c_top);
	QCOMPARE(node.height() - window, drawer->height());
	/* Touched, another tile takes the panel: one, never two. */
	QTest::mouseClick(waveform_of(c), Qt::LeftButton, Qt::NoModifier, QPoint(4, 4));
	QTRY_VERIFY_WITH_TIMEOUT(c->controls(), 2000);
	QVERIFY(!a->controls() && !b->controls());
	QTRY_VERIFY_WITH_TIMEOUT(c->drawer_below()->isVisible() && !drawer->isVisible(), 2000);
	QTRY_COMPARE_WITH_TIMEOUT(c->drawer_below()->geometry().top(), c->geometry().bottom() + 1, 2000);
	c->set_controls(false);
	a->set_controls(true);

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
	 * window): A and B in a train, C a lane below under A alone, A's
	 * controls out -- the window's one panel, under every lane -- so the
	 * corner under B, beside C, is not the window's; the frame between A
	 * and B is, to move the window by. */
	{
		snurra_node node;
		snurra_tile_state a = placed(QStringLiteral("a"), 0, 0);
		a.controls = true;
		snurra_tile *first = node.add_tile(a);
		snurra_tile *second = node.add_tile(placed(QStringLiteral("b"), 0, 1));
		snurra_tile_state c = placed(QStringLiteral("c"), 1, 0);
		c.duration_us = 1000000;   /* short: under A's start alone */
		snurra_tile *third = node.add_tile(c);
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
		const QPoint corner(second->geometry().center().x(), third->geometry().center().y());
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
/*
 * Proportional (the holder, 2026-11-18): A of 40 s then B of 20 s in one
 * lane, C of 60 s below. B comes in 5 s before A ends, so A is drawn to
 * 35 s and B from there to 55, C the whole 60 -- one scale. C's lane
 * begun 10 s later is drawn a seventh of the way along a timeline of 70.
 * The window no wider than the screen.
 */
void embed_test::tiles_are_as_wide_as_their_time_on_the_timeline() {
	snurra_node node;
	auto timed = [](const QString &name, int row, int column, qint64 seconds) {
		snurra_tile_state state = placed(name, row, column);
		state.duration_us = seconds * 1000000;
		return state;
	};
	snurra_tile *a = node.add_tile(timed(QStringLiteral("a"), 0, 0, 40));
	snurra_tile *b = node.add_tile(timed(QStringLiteral("b"), 0, 1, 20));
	snurra_tile *c = node.add_tile(timed(QStringLiteral("c"), 1, 0, 60));
	/* Laid out by itself, no wider than the screen. */
	const QScreen *screen = node.screen();
	QVERIFY2(!screen || node.width() <= screen->availableGeometry().width(),
	         qPrintable(QStringLiteral("%1 on a screen of %2").arg(node.width()).arg(screen ? screen->availableGeometry().width() : 0)));
	node.resize(1200, 300);
	node.show();
	QVERIFY(QTest::qWaitForWindowExposed(&node));
	QTRY_VERIFY_WITH_TIMEOUT(a->length_frames() > 0 && b->length_frames() > 0 && c->length_frames() > 0, 3000);
	const int spacing = std::max(0, node.layout() ? 6 : 0);
	auto near = [](double got, double want, double within) { return std::abs(got - want) <= within; };
	auto check = [&](double a_to, double b_to, double total, double c_from, double c_to) {
		const double scale = double(c->x() + c->width() - std::min(a->x(), c->x())) / (std::max(c_to, b_to) - std::min(0.0, c_from));
		const double left = std::min(a->x(), c->x());
		return near(a->x() - left, 0, 2 + spacing) && near(a->width(), a_to * scale, 8 + spacing)
		       && near(b->x() - left, a_to * scale, 8 + spacing) && near(b->width(), (b_to - a_to) * scale, 8 + spacing)
		       && near(c->x() - left, c_from * scale, 8 + spacing) && near(c->width(), (c_to - c_from) * scale, 8 + spacing)
		       && total > 0;
	};
	auto said = [&] {
		return QStringLiteral("a %1+%2, b %3+%4, c %5+%6").arg(a->x()).arg(a->width()).arg(b->x()).arg(b->width()).arg(c->x()).arg(c->width());
	};
	QTRY_VERIFY2_WITH_TIMEOUT(check(35, 55, 60, 0, 60), qPrintable(said()), 3000);
	c->set_lane_start_us(10000000);
	QTRY_VERIFY2_WITH_TIMEOUT(check(35, 55, 70, 10, 70), qPrintable(said()), 3000);
}

/*
 * Connecting on the beat (the holder, 2026-11-18): B after A in one lane,
 * both 60 s at 120 BPM, B's first beat 0.1 s in. B's waveform dragged
 * with Ctrl comes in where it is put, its first beat moved onto the
 * nearest of A's; with Shift exactly where it is put. Kept by a handover.
 */
void embed_test::a_trains_next_tile_dragged_comes_in_on_the_beat() {
	snurra_node node;
	snurra_tile_state a_state = placed(QStringLiteral("a"), 0, 0);
	snurra_tile_state b_state = placed(QStringLiteral("b"), 0, 1);
	a_state.duration_us = b_state.duration_us = 60000000;
	snurra_tile *a = node.add_tile(a_state);
	snurra_tile *b = node.add_tile(b_state);
	node.resize(1000, 300);
	node.show();
	QVERIFY(QTest::qWaitForWindowExposed(&node));
	QTRY_VERIFY_WITH_TIMEOUT(a->length_frames() > 0 && b->length_frames() > 0, 3000);
	const int rate = a->deck()->rate();
	a->set_beats(snurra_beat_grid{ rate * 0.5, 0, 2, rate });
	b->set_beats(snurra_beat_grid{ rate * 0.5, rate * 0.1, 2, rate });
	QCOMPARE(b->state().overlap_us, qint64(-1));   /* the train's own until dragged */
	QWidget *wave_b = waveform_of(b);
	QVERIFY(wave_b);
	auto drag = [&](qint64 by_us, Qt::KeyboardModifiers held) {
		/* The waveform's own scale: its track over its width. */
		const int w = wave_b->width();
		const QPointF from(w / 2.0, wave_b->height() / 2.0);
		const QPointF to = from + QPointF(double(by_us) / 60000000.0 * w, 0);
		QMouseEvent press(QEvent::MouseButtonPress, from, wave_b->mapToGlobal(from), Qt::LeftButton, Qt::LeftButton, held);
		QCoreApplication::sendEvent(wave_b, &press);
		QMouseEvent move(QEvent::MouseMove, to, wave_b->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, held);
		QCoreApplication::sendEvent(wave_b, &move);
		QMouseEvent release(QEvent::MouseButtonRelease, to, wave_b->mapToGlobal(to), Qt::LeftButton, Qt::NoButton, held);
		QCoreApplication::sendEvent(wave_b, &release);
		return qint64(60000000.0 / w) + 1;   /* a pixel's time */
	};
	/* B's first beat, as a time in A: A's length less the overlap, plus
	 * B's first beat; a beat of A is a multiple of 0.5 s. */
	auto off_beat_us = [&] {
		const double at = 60.0 - b->state().overlap_us / 1e6 + 0.1;
		return std::abs(at / 0.5 - std::round(at / 0.5)) * 500000;
	};
	/* Dragged 4.13 s earlier than it came in by itself -- not a whole
	 * number of beats from where it was, which was on one: a longer crossing. */
	drag(-4130000, Qt::ControlModifier);
	QVERIFY2(b->state().overlap_us > 5000000, qPrintable(QString::number(b->state().overlap_us)));
	QVERIFY2(off_beat_us() < 25, qPrintable(QStringLiteral("%1 us off the beat").arg(off_beat_us())));
	const qint64 snapped = b->state().overlap_us;
	/* With Shift, where it is put: 0.13 s later is not on a beat. */
	const qint64 pixel_us = drag(130000, Qt::ControlModifier | Qt::ShiftModifier);
	QVERIFY2(std::abs(b->state().overlap_us - (snapped - 130000)) <= pixel_us,
	         qPrintable(QStringLiteral("%1, not %2").arg(b->state().overlap_us).arg(snapped - 130000)));
	QVERIFY(off_beat_us() > 25);
	/* Kept by a handover. */
	QByteArray bytes;
	QDataStream out(&bytes, QIODevice::WriteOnly);
	out << b->state();
	snurra_tile_state back;
	QDataStream in(bytes);
	in >> back;
	QCOMPARE(back.overlap_us, b->state().overlap_us);
	/* And the lane, Ctrl-dragged on its first tile, still moves as a lane. */
	QWidget *wave_a = waveform_of(a);
	const int wa = wave_a->width();
	const QPointF from(wa / 2.0, wave_a->height() / 2.0), to = from + QPointF(wa / 6.0, 0);
	QMouseEvent press(QEvent::MouseButtonPress, from, wave_a->mapToGlobal(from), Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
	QCoreApplication::sendEvent(wave_a, &press);
	QMouseEvent move(QEvent::MouseMove, to, wave_a->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, Qt::ControlModifier);
	QCoreApplication::sendEvent(wave_a, &move);
	QMouseEvent release(QEvent::MouseButtonRelease, to, wave_a->mapToGlobal(to), Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
	QCoreApplication::sendEvent(wave_a, &release);
	QVERIFY2(a->lane_start_us() > 5000000, qPrintable(QString::number(a->lane_start_us())));
}

/*
 * The holder, 2026-11-18: "keep my width if I set it". A window left to
 * itself fits the timeline, a tile closed included -- not its tiles' least
 * widths. One resized from outside, as the grip or a window manager does,
 * keeps that width through a tile added, a lane moved and a tile closed.
 */
void embed_test::a_width_the_person_set_is_kept() {
	auto timed = [](const QString &name, int row, int column, qint64 seconds) {
		snurra_tile_state state = placed(name, row, column);
		state.duration_us = seconds * 1000000;
		return state;
	};
	{
		snurra_node node;
		snurra_tile *a = node.add_tile(timed(QStringLiteral("a"), 0, 0, 200));
		snurra_tile *b = node.add_tile(timed(QStringLiteral("b"), 0, 1, 20));
		node.show();
		QVERIFY(QTest::qWaitForWindowExposed(&node));
		QTRY_VERIFY_WITH_TIMEOUT(a->length_frames() > 0 && b->length_frames() > 0, 3000);
		QTest::qWait(300);
		const int fitted = node.width();
		QVERIFY2(fitted > a->minimumSizeHint().width() + b->minimumSizeHint().width() + 20, qPrintable(QString::number(fitted)));
		snurra_tile *c = node.add_tile(timed(QStringLiteral("c"), 0, 2, 20));
		QTRY_VERIFY_WITH_TIMEOUT(c->length_frames() > 0, 3000);
		QTest::qWait(300);
		emit c->close_requested(c);
		QTest::qWait(300);
		QVERIFY2(node.width() >= fitted - 2, qPrintable(QStringLiteral("%1 after a close, %2 fitted").arg(node.width()).arg(fitted)));
	}
	snurra_node node;
	snurra_tile *a = node.add_tile(timed(QStringLiteral("a"), 0, 0, 200));
	snurra_tile *b = node.add_tile(timed(QStringLiteral("b"), 0, 1, 20));
	node.show();
	QVERIFY(QTest::qWaitForWindowExposed(&node));
	QTRY_VERIFY_WITH_TIMEOUT(a->length_frames() > 0 && b->length_frames() > 0, 3000);
	QTest::qWait(300);
	const int chosen = std::max(node.minimumSizeHint().width() + 40, node.width() / 2);
	node.resize(chosen, node.height());   /* the person's */
	QCOMPARE(node.width(), chosen);
	snurra_tile *c = node.add_tile(timed(QStringLiteral("c"), 0, 2, 600));
	QTRY_VERIFY_WITH_TIMEOUT(c->length_frames() > 0, 3000);
	QTest::qWait(300);
	QVERIFY2(node.width() == chosen, qPrintable(QStringLiteral("%1 after a tile came, %2 chosen").arg(node.width()).arg(chosen)));
	b->set_tempo(0.5f);   /* twice as long on the timeline */
	QTest::qWait(300);
	QCOMPARE(node.width(), chosen);
	emit c->close_requested(c);
	QTest::qWait(300);
	QCOMPARE(node.width(), chosen);
}

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
	/* Its width as it is at each drag: a lane begun later is laid out
	 * further along the timeline, and its tile narrower. */
	const int w = wave_b->width();
	auto drag = [&](double fraction, Qt::KeyboardModifiers held) {
		const int w = wave_b->width();
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
 * Learning which control the hand moves (project.md sec 3f): a pot that
 * chatters untouched is set aside, the knob turned is bound and called
 * absolute; a note is a button; an encoder stepping about 64 relative; a
 * controller with its partner 32 above one fine control; two knobs at
 * once, or nothing, bind nothing and say so -- and only at the end.
 */
void embed_test::the_control_moved_is_told_from_the_noise() {
	qint64 now = 1000000000;
	auto event = [&now](int number, int value, snurra_midi_event::kind_t kind = snurra_midi_event::cc) {
		snurra_midi_event made;
		made.port = QStringLiteral("Controller:Port 1");
		made.number = number;
		made.value = value;
		made.kind = kind;
		made.at_us = (now += 10000);
		return made;
	};
	snurra_midi_learn learn;
	for (int i = 0; i < 30; ++i) learn.see(event(1, 40 + (i % 3) * 7));   /* a worn pot, untouched, wandering 14 */
	learn.start(now);
	QCOMPARE(learn.decide(false).state, snurra_midi_learn::verdict::waiting);
	for (int i = 0; i < 10; ++i) {
		learn.see(event(1, 40 + (i % 3) * 7));
		learn.see(event(7, i * 8));
	}
	snurra_midi_learn::verdict found = learn.decide(false);
	QCOMPARE(found.state, snurra_midi_learn::verdict::clear);
	QCOMPARE(found.control, QStringLiteral("Controller:Port 1/ch1/cc7"));
	QCOMPARE(found.kind, snurra_control_kind::absolute);
	learn.stop();

	learn.start(now);
	learn.see(event(36, 100, snurra_midi_event::note_on));
	found = learn.decide(false);
	QCOMPARE(found.kind, snurra_control_kind::button);
	QCOMPARE(found.control, QStringLiteral("Controller:Port 1/ch1/note36"));
	learn.stop();

	learn.start(now);
	for (int i = 0; i < 6; ++i) learn.see(event(20, i % 2 ? 65 : 63));
	QCOMPARE(learn.decide(false).kind, snurra_control_kind::relative);
	learn.stop();

	learn.start(now);
	for (int i = 0; i < 8; ++i) {
		learn.see(event(2, i * 15));
		learn.see(event(34, (i * 37) % 128));
	}
	found = learn.decide(false);
	QCOMPARE(found.kind, snurra_control_kind::fine);
	QCOMPARE(found.control, QStringLiteral("Controller:Port 1/ch1/cc2"));
	learn.stop();

	learn.start(now);
	for (int i = 0; i < 8; ++i) {
		learn.see(event(7, i * 15));
		learn.see(event(10, 127 - i * 15));
	}
	QCOMPARE(learn.decide(false).state, snurra_midi_learn::verdict::waiting);
	QCOMPARE(learn.decide(true).state, snurra_midi_learn::verdict::unsure);
	learn.stop();
	learn.start(now);
	QCOMPARE(learn.decide(true).state, snurra_midi_learn::verdict::unsure);
	learn.stop();

	/* The bindings as text, a name the person gave read back through it. */
	snurra_bindings bindings;
	bindings.set_alias(QStringLiteral("Controller:Port 1/ch1/cc7"), QStringLiteral("ch_a_gain"));
	bindings.bind(QStringLiteral("Controller:Port 1/ch1/cc7"), QStringLiteral("ch_1_level"), snurra_control_kind::absolute);
	bindings.bind(QStringLiteral("Controller:Port 1/ch1/note36"), QStringLiteral("ch_1_play"), snurra_control_kind::button);
	const QStringList text = bindings.lines();
	QVERIFY2(text.contains(QStringLiteral("ch_a_gain -> ch_1_level (absolute)")), qPrintable(text.join(QLatin1Char('|'))));
	snurra_bindings again;
	again.set_alias(QStringLiteral("Controller:Port 1/ch1/cc7"), QStringLiteral("ch_a_gain"));
	QVERIFY(again.read(text));
	snurra_control_kind kind = snurra_control_kind::absolute;
	QCOMPARE(again.control_for(QStringLiteral("Controller:Port 1/ch1/note36"), &kind), QStringLiteral("ch_1_play"));
	QCOMPARE(kind, snurra_control_kind::button);
	QCOMPARE(again.control_for(QStringLiteral("Controller:Port 1/ch1/cc7")), QStringLiteral("ch_1_level"));
	QVERIFY(!again.read({ QStringLiteral("not a binding") }));
	/* Both ends named (the holder: ch_a_gain -> ch_1_gain): written by the
	 * names, read back through them, kept in the settings. */
	again.set_control_name(QStringLiteral("ch_1_level"), QStringLiteral("deck_a_volume"));
	QVERIFY(again.lines().contains(QStringLiteral("ch_a_gain -> deck_a_volume (absolute)")));
	snurra_bindings named;
	named.set_alias(QStringLiteral("Controller:Port 1/ch1/cc7"), QStringLiteral("ch_a_gain"));
	named.set_control_name(QStringLiteral("ch_1_level"), QStringLiteral("deck_a_volume"));
	QVERIFY(named.read({ QStringLiteral("ch_a_gain -> deck_a_volume (absolute)") }));
	QCOMPARE(named.control_for(QStringLiteral("Controller:Port 1/ch1/cc7")), QStringLiteral("ch_1_level"));
	{
		QSettings(QStringLiteral("snurra"), QStringLiteral("snurra")).remove(QStringLiteral("controllers"));
		named.save();
		snurra_bindings loaded;
		loaded.load();
		QCOMPARE(loaded.control_for(QStringLiteral("Controller:Port 1/ch1/cc7")), QStringLiteral("ch_1_level"));
		QCOMPARE(loaded.control_name(QStringLiteral("ch_1_level")), QStringLiteral("deck_a_volume"));
		QCOMPARE(loaded.name_of(QStringLiteral("Controller:Port 1/ch1/cc7")), QStringLiteral("ch_a_gain"));
		/* Nothing chosen writes nothing. */
		snurra_bindings().save();
		QVERIFY(QSettings(QStringLiteral("snurra"), QStringLiteral("snurra")).childGroups().contains(QStringLiteral("controllers")) == false
		        || QSettings(QStringLiteral("snurra"), QStringLiteral("snurra")).value(QStringLiteral("controllers/bindings")).isNull());
	}
	/* A fine control: its two halves one value of 14 bits. */
	{
		QSettings(QStringLiteral("snurra"), QStringLiteral("snurra")).remove(QStringLiteral("controllers"));
		QList<int> given;
		snurra_controller controller(
		        QString(), [&given](const QString &control, snurra_control_kind kind, const snurra_midi_event &moved) {
			        QCOMPARE(control, QStringLiteral("ch_1_tempo"));
			        QCOMPARE(kind, snurra_control_kind::fine);
			        QCOMPARE(moved.kind, snurra_midi_event::bend);
			        given << moved.value;
		        },
		        nullptr);
		controller.bindings().bind(QStringLiteral("Controller:Port 1/ch1/cc2"), QStringLiteral("ch_1_tempo"), snurra_control_kind::fine);
		controller.bindings().save();   /* it reads them again as events come */
		controller.take(event(2, 64));
		controller.take(event(34, 0));
		controller.take(event(34, 127));
		controller.take(event(2, 65));
		QCOMPARE(given, (QList<int>{ 64 * 128, 64 * 128, 64 * 128 + 127, 65 * 128 + 127 }));
		QSettings(QStringLiteral("snurra"), QStringLiteral("snurra")).remove(QStringLiteral("controllers"));
	}
	/* Bound again elsewhere, a control leaves its old hardware control. */
	again.bind(QStringLiteral("Controller:Port 1/ch1/cc8"), QStringLiteral("ch_1_level"), snurra_control_kind::absolute);
	QVERIFY(again.control_for(QStringLiteral("Controller:Port 1/ch1/cc7")).isEmpty());
}

/*
 * The whole way, through ALSA's sequencer (skipped without one): a
 * sender of the test's own, deck A's level held with Alt while a worn pot
 * chatters and a fader moves -- the fader is bound, and then moves the
 * level; the pot moves nothing. The binding is kept in the settings.
 */
void embed_test::a_controller_learned_by_holding_moves_the_deck() {
	snd_seq_t *sender = nullptr;
	if (snd_seq_open(&sender, "default", SND_SEQ_OPEN_OUTPUT, 0) < 0) QSKIP("no ALSA sequencer");
	auto close_sender = qScopeGuard([sender] { snd_seq_close(sender); });
	snd_seq_set_client_name(sender, "snurra test controller");
	/* Not exported: snurra connects to it only as told, so no event comes
	 * twice. */
	const int out = snd_seq_create_simple_port(sender, "fader", SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_NO_EXPORT,
	                                           SND_SEQ_PORT_TYPE_MIDI_GENERIC);
	QVERIFY(out >= 0);

	QSettings(QStringLiteral("snurra"), QStringLiteral("snurra")).remove(QStringLiteral("controllers"));
	snurra_dj dj;
	dj.resize(900, 600);
	dj.show();
	QVERIFY(QTest::qWaitForWindowExposed(&dj));
	snurra_media media;
	media.path = QStringLiteral("tone:440");
	QVERIFY(dj.load(0, media));
	QVERIFY(dj.take_controllers());
	snurra_midi_in *in = dj.findChild<snurra_midi_in *>();
	QVERIFY(in);
	QVERIFY(snd_seq_connect_to(sender, out, in->client(), in->port()) >= 0);
	auto send = [sender, out](int number, int value) {
		snd_seq_event_t event;
		snd_seq_ev_clear(&event);
		snd_seq_ev_set_source(&event, out);
		snd_seq_ev_set_subs(&event);
		snd_seq_ev_set_direct(&event);
		snd_seq_ev_set_controller(&event, 0, number, value);
		snd_seq_event_output_direct(sender, &event);
		QTest::qWait(10);
	};
	QWidget *place = dj.findChild<QWidget *>(QStringLiteral("snurra-deck-a"));
	snurra_tile *tile = place->findChild<snurra_tile *>();
	QSlider *level = nullptr;
	for (QSlider *slider : place->findChildren<QSlider *>())
		if (slider->accessibleName() == QLatin1String("Level")) level = slider;
	QVERIFY(level);
	QCOMPARE(level->property("snurra_control").toString(), QStringLiteral("ch_1_level"));

	for (int i = 0; i < 20; ++i) send(1, 40 + (i % 3) * 7);   /* the worn pot, wandering 14 */
	/* Learned without Alt -- the Learn button on, as where the window
	 * manager takes Alt with the pointer -- the same as with it. */
	QToolButton *learn = nullptr;
	for (QToolButton *button : dj.findChildren<QToolButton *>())
		if (button->accessibleName() == QLatin1String("Learn controller bindings")) learn = button;
	QVERIFY(learn && learn->isVisible());
	learn->setChecked(true);
	QTest::mousePress(level, Qt::LeftButton, Qt::NoModifier, level->rect().center());
	for (int i = 0; i < 12; ++i) {
		send(1, 40 + (i % 3) * 7);
		send(7, 10 + i * 8);
	}
	QTest::mouseRelease(level, Qt::LeftButton, Qt::NoModifier, level->rect().center());
	learn->setChecked(false);
	snurra_bindings kept;
	kept.load();
	const QString fader = QStringLiteral("snurra test controller:fader/ch1/cc7");
	QVERIFY2(kept.control_for(fader) == QLatin1String("ch_1_level"), qPrintable(kept.lines().join(QLatin1Char('|'))));
	QVERIFY(kept.control_for(QStringLiteral("snurra test controller:fader/ch1/cc1")).isEmpty());

	send(7, 32);
	QTRY_COMPARE_WITH_TIMEOUT(tile->level(), 25, 2000);
	const int before = tile->level();
	send(1, 120);
	QTest::qWait(100);
	QCOMPARE(tile->level(), before);

	/* A cue button: a note learned onto deck A's socket plugs the
	 * listener in there, and out again. */
	QCOMPARE(dj.add_listener(QStringLiteral("null")), 0);
	QToolButton *socket = nullptr;
	for (QToolButton *button : place->findChildren<QToolButton *>(QString(), Qt::FindDirectChildrenOnly))
		if (button->accessibleName() == QLatin1String("Listen here")) socket = button;
	QVERIFY(socket && socket->isVisible());
	QCOMPARE(socket->property("snurra_control").toString(), QStringLiteral("ch_1_listen"));
	auto note = [sender, out](int number) {
		snd_seq_event_t event;
		snd_seq_ev_clear(&event);
		snd_seq_ev_set_source(&event, out);
		snd_seq_ev_set_subs(&event);
		snd_seq_ev_set_direct(&event);
		snd_seq_ev_set_noteon(&event, 0, number, 100);
		snd_seq_event_output_direct(sender, &event);
		snd_seq_ev_set_noteoff(&event, 0, number, 0);
		snd_seq_event_output_direct(sender, &event);
		QTest::qWait(30);
	};
	QTest::mousePress(socket, Qt::LeftButton, Qt::AltModifier, socket->rect().center());
	note(48);
	QTest::mouseRelease(socket, Qt::LeftButton, Qt::AltModifier, socket->rect().center());
	QCOMPARE(dj.plugged(0), -1);
	note(48);
	QTRY_COMPARE_WITH_TIMEOUT(dj.plugged(0), 0, 2000);
	note(48);
	QTRY_COMPARE_WITH_TIMEOUT(dj.plugged(0), -1, 2000);
	QSettings(QStringLiteral("snurra"), QStringLiteral("snurra")).remove(QStringLiteral("controllers"));
}

/*
 * The level match inside a player (project.md sec 3f): a track whose
 * analysis is kept, with the match on, plays nested at the gain its deck
 * would give it -- here a tune kept as -20 LUFS, so +6 dB toward -14 --
 * and as it is with the match off.
 */
void embed_test::a_nested_track_is_brought_to_the_level_its_deck_would_give() {
	QTemporaryDir dir;
	const QString path = dir.filePath(QStringLiteral("quiet.wav"));
	{
		QFile file(path);
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write(make_wav(48000));
	}
	snurra_beat_grid grid;
	grid.rate = 48000;
	grid.loudness = -20;
	grid.loudness_known = true;
	grid.peak = 0.1;
	snurra_cache_beats(path, -1, grid);
	snurra_beat_grid kept;
	QVERIFY(snurra_cached_beats(path, -1, 48000, kept));
	const float gain = snurra_loudness_gain(kept, SNURRA_LOUDNESS_REFERENCE);
	QVERIFY2(gain > 1.5f, qPrintable(QString::number(gain)));

	std::vector<snurra_tile_state> player(1);
	player[0].source = path;
	player[0].duration_us = 1000000;
	auto rendered = [&player] {
		std::unique_ptr<snurra_source> source = snurra_open_timeline(player, 48000);
		std::vector<float> out(2 * 48000);
		source->read(out.data(), 48000);
		return out;
	};
	QSettings settings(QStringLiteral("snurra"), QStringLiteral("snurra"));
	settings.setValue(QStringLiteral("loudness/match"), false);
	const std::vector<float> plain = rendered();
	settings.setValue(QStringLiteral("loudness/match"), true);
	const std::vector<float> matched = rendered();
	settings.setValue(QStringLiteral("loudness/match"), false);   /* as the suite keeps it */
	int compared = 0;
	for (size_t i = 0; i < plain.size(); ++i) {
		if (std::abs(plain[i]) < 1e-3f) continue;
		QVERIFY2(std::abs(matched[i] / plain[i] - gain) < 1e-3f, qPrintable(QStringLiteral("at %1: %2").arg(i).arg(matched[i] / plain[i])));
		++compared;
	}
	QVERIFY(compared > 1000);
}

/*
 * Sync with a player in a deck (project.md sec 3f: "sync follows the beat
 * of whichever track sounds at that moment"): two tunes with kept grids,
 * 120 BPM and 128 BPM played at 1.25, as a player in a tile. Standing in
 * the first, the tile's beat is the first's; in the second, the second's,
 * where it falls in the player's time.
 */
void embed_test::a_player_in_a_tile_beats_with_the_track_it_sounds() {
	QTemporaryDir dir;
	std::vector<snurra_tile_state> player(2);
	const double periods[2] = { 24000, 22500 };
	const double firsts[2] = { 1000, 3000 };
	for (int i = 0; i < 2; ++i) {
		const QString path = dir.filePath(QStringLiteral("%1.wav").arg(i));
		QFile file(path);
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write(make_wav(4 * 48000));
		file.close();
		snurra_beat_grid grid;
		grid.period = periods[i];
		grid.first = firsts[i];
		grid.confidence = 10;
		grid.rate = 48000;
		snurra_cache_beats(path, -1, grid);
		player[size_t(i)].source = path;
		player[size_t(i)].duration_us = 4000000;
		player[size_t(i)].column = i;
	}
	player[1].tempo = 1.25f;
	const snurra_timeline timeline = snurra_timeline_from(player, 48000);
	const int64_t second = timeline.start(1);
	for (int at = 0; at < 2; ++at) {
		snurra_node node;
		snurra_tile_state state;
		state.source = QStringLiteral("2 tracks");
		state.player = player;
		state.duration_us = snurra_frames_to_us(timeline.end(1), 48000);
		state.position_us = at == 0 ? 1000000 : snurra_frames_to_us(second + 48000, 48000);
		state.sampled_at_us = snurra_clock_us();
		snurra_tile *tile = node.add_tile(state);
		const double period = at == 0 ? 24000 : 22500 / 1.25;
		QTRY_VERIFY_WITH_TIMEOUT(qAbs(tile->beats().period - period) < 1e-6, 2000);
		const double first = at == 0 ? 1000 : double(second) + 3000 / 1.25;
		QVERIFY2(qAbs(tile->beats().first - first) < 1e-6, qPrintable(QStringLiteral("first beat at %1, meant %2").arg(tile->beats().first).arg(first)));
		QVERIFY(!tile->beats().loudness_known);
	}
}

/*
 * A controller's browse knob and load buttons (project.md sec 3f): a
 * relative encoder bound to browse steps the browser's choice down and
 * up the list, and deck B's load puts the chosen tune on deck B.
 */
void embed_test::a_browse_knob_and_load_buttons_fill_the_decks() {
	QTemporaryDir dir;
	for (const QString &name : { QStringLiteral("a.wav"), QStringLiteral("b.wav"), QStringLiteral("c.wav") }) {
		QFile file(dir.filePath(name));
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write(make_wav(48000));
	}
	QSettings(QStringLiteral("snurra"), QStringLiteral("snurra")).remove(QStringLiteral("controllers"));
	auto clean = qScopeGuard([] { QSettings(QStringLiteral("snurra"), QStringLiteral("snurra")).remove(QStringLiteral("controllers")); });
	{
		snurra_bindings bindings;
		bindings.bind(QStringLiteral("pads/ch1/cc20"), QStringLiteral("browse"), snurra_control_kind::relative);
		bindings.bind(QStringLiteral("pads/ch1/note40"), QStringLiteral("ch_2_load"), snurra_control_kind::button);
		bindings.save();
	}
	snurra_dj dj;
	dj.set_browser_folder(dir.path());
	dj.show();
	snurra_library *browser = dj.findChild<snurra_library *>();
	QTRY_COMPARE_WITH_TIMEOUT(browser->shown().size(), 3, 5000);
	QVERIFY(dj.take_controllers());
	snurra_controller *controller = dj.findChild<snurra_controller *>();
	QVERIFY(controller);
	qint64 at = snurra_clock_us();
	auto event = [&at](snurra_midi_event::kind_t kind, int number, int value) {
		snurra_midi_event made;
		made.port = QStringLiteral("pads");
		made.kind = kind;
		made.number = number;
		made.value = value;
		made.at_us = (at += 2000000);   /* each a second apart: the bindings read again */
		return made;
	};
	controller->take(event(snurra_midi_event::cc, 20, 1));   /* one step down: the first */
	controller->take(event(snurra_midi_event::cc, 20, 1));   /* the second */
	controller->take(event(snurra_midi_event::cc, 20, 1));   /* the third */
	controller->take(event(snurra_midi_event::cc, 20, 127)); /* one back up: the second */
	QCOMPARE(browser->chosen(), QStringList{ browser->shown().at(1) });
	controller->take(event(snurra_midi_event::note_on, 40, 100));
	QCOMPARE(dj.loaded(1), QFileInfo(browser->shown().at(1)).fileName());
	QVERIFY(dj.loaded(0).isEmpty());
}

/*
 * An encoder sending about 64 -- 65 up one, 63 down one, as many do --
 * steps the browser down three and back up one. Read as two's complement
 * only, 63 was 63 steps down, and the choice ran to the list's end.
 */
void embed_test::an_encoder_about_64_steps_both_ways() {
	QTemporaryDir dir;
	for (const QString &name : { QStringLiteral("a.wav"), QStringLiteral("b.wav"), QStringLiteral("c.wav"), QStringLiteral("d.wav") }) {
		QFile file(dir.filePath(name));
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write(make_wav(4800));
	}
	QSettings(QStringLiteral("snurra"), QStringLiteral("snurra")).remove(QStringLiteral("controllers"));
	auto clean = qScopeGuard([] { QSettings(QStringLiteral("snurra"), QStringLiteral("snurra")).remove(QStringLiteral("controllers")); });
	{
		snurra_bindings bindings;
		bindings.bind(QStringLiteral("pads/ch1/cc20"), QStringLiteral("browse"), snurra_control_kind::relative);
		bindings.save();
	}
	snurra_dj dj;
	dj.set_browser_folder(dir.path());
	dj.show();
	snurra_library *browser = dj.findChild<snurra_library *>();
	QTRY_COMPARE_WITH_TIMEOUT(browser->shown().size(), 4, 5000);
	QVERIFY(dj.take_controllers());
	snurra_controller *controller = dj.findChild<snurra_controller *>();
	QVERIFY(controller);
	qint64 at = snurra_clock_us();
	for (int value : { 65, 65, 65, 63 }) {
		snurra_midi_event made;
		made.port = QStringLiteral("pads");
		made.number = 20;
		made.value = value;
		made.at_us = (at += 2000000);
		controller->take(made);
	}
	QCOMPARE(browser->chosen(), QStringList{ browser->shown().at(1) });
}

/*
 * A Mixxx mapping imported (project.md sec 3f; the holder, 2026-11-21:
 * plain mappings, no JavaScript): its plain bindings move the decks from
 * whatever port sends them; a script's control and one snurra has not
 * are skipped, saying so; a binding learned for a port wins over the
 * import, and a second import replaces the first's.
 */
void embed_test::a_mixxx_mappings_plain_bindings_move_the_decks() {
	const QByteArray mapping = R"(<?xml version="1.0" encoding="utf-8"?>
<MixxxControllerPreset schemaVersion="1" mixxxVersion="2.5.0">
 <info><name>Test controller</name></info>
 <controller id="Test">
  <scriptfiles><file filename="Test-scripts.js" functionprefix="Test"/></scriptfiles>
  <controls>
   <control><group>[Channel1]</group><key>volume</key><status>0xB0</status><midino>0x07</midino><options><normal/></options></control>
   <control><group>[Channel2]</group><key>volume</key><status>0xB1</status><midino>0x07</midino><options><normal/></options></control>
   <control><group>[Channel2]</group><key>play</key><status>0x91</status><midino>0x3B</midino><options><normal/></options></control>
   <control><group>[Channel2]</group><key>play</key><status>0x81</status><midino>0x3B</midino><options><normal/></options></control>
   <control><group>[Channel1]</group><key>keylock</key><status>0x90</status><midino>0x40</midino><options><normal/></options></control>
   <control><group>[Master]</group><key>crossfader</key><status>0xB0</status><midino>0x08</midino><options><normal/></options></control>
   <control><group>[Channel1]</group><key>rate</key><status>0xB0</status><midino>0x09</midino><options><invert/></options></control>
   <control><group>[EqualizerRack1_[Channel1]_Effect1]</group><key>parameter1</key><status>0xB0</status><midino>16</midino></control>
   <control><group>[Channel1]</group><key>jog</key><status>0xB0</status><midino>0x20</midino><options><Script-Binding/></options></control>
   <control><group>[Channel1]</group><key>hotcue_1_activate</key><status>0x90</status><midino>0x01</midino><options><normal/></options></control>
  </controls>
  <outputs>
   <output><group>[Channel2]</group><key>play_indicator</key><status>0x91</status><midino>0x3B</midino><minimum>0.5</minimum></output>
  </outputs>
 </controller>
</MixxxControllerPreset>
)";
	QSettings(QStringLiteral("snurra"), QStringLiteral("snurra")).remove(QStringLiteral("controllers"));
	auto clean = qScopeGuard([] { QSettings(QStringLiteral("snurra"), QStringLiteral("snurra")).remove(QStringLiteral("controllers")); });
	{
		snurra_bindings bindings;
		QCOMPARE(bindings.import_mixxx("<html><body/></html>"), -1);
		/* Learned for one port: deck B's level from its cc 7. */
		bindings.bind(QStringLiteral("pads/ch2/cc7"), QStringLiteral("ch_2_tempo"), snurra_control_kind::absolute);
		bindings.bind(QStringLiteral("*/ch16/cc1"), QStringLiteral("crossfader"), snurra_control_kind::absolute);
		QStringList skipped;
		QCOMPARE(bindings.import_mixxx(mapping, &skipped), 7);
		QVERIFY(bindings.lines().contains(QStringLiteral("*/ch1/cc9 -> ch_1_tempo (absolute, inverted)")));
		QVERIFY(skipped.contains(QStringLiteral("[Channel1] jog: a script's")));
		QVERIFY(skipped.contains(QStringLiteral("[Channel1] hotcue_1_activate: snurra has no such control")));
		QCOMPARE(skipped.size(), 2);
		/* An earlier import's binding is gone, the learned one stays. */
		QVERIFY(bindings.control_for(QStringLiteral("any/ch16/cc1")).isEmpty());
		QCOMPARE(bindings.control_for(QStringLiteral("pads/ch2/cc7")), QStringLiteral("ch_2_tempo"));
		bindings.save();
	}
	snurra_dj dj;
	dj.resize(900, 600);
	dj.show();
	QVERIFY(QTest::qWaitForWindowExposed(&dj));
	snurra_media media;
	media.path = QStringLiteral("tone:440");
	QVERIFY(dj.load(0, media));
	QVERIFY(dj.load(1, media));
	QVERIFY(dj.take_controllers());
	snurra_controller *controller = dj.findChild<snurra_controller *>();
	QVERIFY(controller);
	snurra_tile *a = dj.findChild<QWidget *>(QStringLiteral("snurra-deck-a"))->findChild<snurra_tile *>();
	snurra_tile *b = dj.findChild<QWidget *>(QStringLiteral("snurra-deck-b"))->findChild<snurra_tile *>();
	QVERIFY(a && b);
	qint64 at = snurra_clock_us();
	auto send = [&](const QString &port, int channel, snurra_midi_event::kind_t kind, int number, int value) {
		snurra_midi_event made;
		made.port = port;
		made.channel = channel;
		made.kind = kind;
		made.number = number;
		made.value = value;
		made.at_us = (at += 2000000);
		controller->take(made);
	};
	send(QStringLiteral("Some Controller:MIDI 1"), 1, snurra_midi_event::cc, 7, 0);
	QCOMPARE(a->level(), 0);
	send(QStringLiteral("Some Controller:MIDI 1"), 1, snurra_midi_event::cc, 8, 127);
	QCOMPARE(dj.crossfader(), 1.0);
	send(QStringLiteral("Some Controller:MIDI 1"), 1, snurra_midi_event::cc, 16, 0);
	QVERIFY(a->state().eq.part_db(0) <= SNURRA_EQ_KILL);
	/* The tempo fader made the other way: at its top, the slowest. */
	send(QStringLiteral("Some Controller:MIDI 1"), 1, snurra_midi_event::cc, 9, 127);
	QCOMPARE(a->tempo(), float(1 - snurra_tile::tempo_range()));
	const bool playing = b->playing();
	send(QStringLiteral("Some Controller:MIDI 1"), 2, snurra_midi_event::note_on, 0x3b, 127);
	QTRY_COMPARE_WITH_TIMEOUT(b->playing(), !playing, 2000);
	/* Key lock, pressed and released: turned over once. */
	const bool locked = a->key_lock();
	send(QStringLiteral("Some Controller:MIDI 1"), 1, snurra_midi_event::note_on, 0x40, 127);
	send(QStringLiteral("Some Controller:MIDI 1"), 1, snurra_midi_event::note_off, 0x40, 0);
	QCOMPARE(a->key_lock(), !locked);
	/* The port learned for moves what it was learned to, not the import's. */
	const int level = b->level();
	send(QStringLiteral("pads"), 2, snurra_midi_event::cc, 7, 0);
	QCOMPARE(b->level(), level);
	send(QStringLiteral("elsewhere"), 2, snurra_midi_event::cc, 7, 0);
	QCOMPARE(b->level(), 0);
}

/*
 * A mapping's <outputs> (project.md sec 3f): the controller's play light
 * follows deck A -- on when it plays, off when it pauses -- sent back to
 * the controller it came from, once it has been heard from; a light snurra
 * has no state for is skipped, saying so.
 */
void embed_test::a_mappings_lights_show_the_deck_playing() {
	snd_seq_t *controller_seq = nullptr;
	if (snd_seq_open(&controller_seq, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK) < 0) QSKIP("no ALSA sequencer");
	auto close_controller = qScopeGuard([controller_seq] { snd_seq_close(controller_seq); });
	snd_seq_set_client_name(controller_seq, "snurra test lights");
	/* A controller's port: it sends and it takes, as hardware does. Not
	 * exported, so snurra connects to it only as told. */
	const int pad = snd_seq_create_simple_port(controller_seq, "pad",
	                                           SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_NO_EXPORT,
	                                           SND_SEQ_PORT_TYPE_MIDI_GENERIC);
	QVERIFY(pad >= 0);
	const QByteArray mapping = R"(<?xml version="1.0" encoding="utf-8"?>
<MixxxControllerPreset schemaVersion="1" mixxxVersion="2.5.0">
 <controller id="Test">
  <controls>
   <control><group>[Channel1]</group><key>volume</key><status>0xB0</status><midino>0x07</midino></control>
  </controls>
  <outputs>
   <output><group>[Channel1]</group><key>play_indicator</key><status>0x90</status><midino>0x3B</midino><on>0x7F</on><off>0x01</off><minimum>0.5</minimum></output>
   <output><group>[Channel1]</group><key>keylock</key><status>0x90</status><midino>0x3C</midino><off>0xFF</off><minimum>0.5</minimum></output>
   <output><group>[Channel1]</group><key>vu_meter</key><status>0xB0</status><midino>0x20</midino><minimum>0.1</minimum></output>
  </outputs>
 </controller>
</MixxxControllerPreset>
)";
	QSettings(QStringLiteral("snurra"), QStringLiteral("snurra")).remove(QStringLiteral("controllers"));
	auto clean = qScopeGuard([] { QSettings(QStringLiteral("snurra"), QStringLiteral("snurra")).remove(QStringLiteral("controllers")); });
	{
		snurra_bindings bindings;
		QStringList skipped;
		QCOMPARE(bindings.import_mixxx(mapping, &skipped), 1);
		QCOMPARE(skipped, QStringList{ QStringLiteral("[Channel1] vu_meter: a light snurra has no state for") });
		QCOMPARE(bindings.lights().size(), 2);
		bindings.save();
	}
	snurra_dj dj;
	dj.resize(900, 600);
	dj.show();
	QVERIFY(QTest::qWaitForWindowExposed(&dj));
	snurra_media media;
	media.path = QStringLiteral("tone:440");
	QVERIFY(dj.load(0, media));
	QVERIFY(dj.take_controllers());
	snurra_midi_in *in = dj.findChild<snurra_midi_in *>();
	QVERIFY(in);
	QVERIFY(snd_seq_connect_to(controller_seq, pad, in->client(), in->port()) >= 0);
	snurra_tile *a = dj.findChild<QWidget *>(QStringLiteral("snurra-deck-a"))->findChild<snurra_tile *>();
	QVERIFY(a);
	/* The last velocity the pad was sent for note 0x3B, -1 for none; and
	 * for the key lock's 0x3C, whose off sends nothing. */
	int light = -1, lock = -1;
	auto read_lights = [&] {
		snd_seq_event_t *event = nullptr;
		while (snd_seq_event_input(controller_seq, &event) >= 0 && event)
			if (event->type == SND_SEQ_EVENT_NOTEON && event->data.note.channel == 0) {
				if (event->data.note.note == 0x3b) light = event->data.note.velocity;
				if (event->data.note.note == 0x3c) lock = event->data.note.velocity;
			}
		return light;
	};
	a->request_playing(false);
	QTest::qWait(300);
	QCOMPARE(read_lights(), -1);   /* not heard from yet: nothing sent */
	snd_seq_event_t moved;
	snd_seq_ev_clear(&moved);
	snd_seq_ev_set_source(&moved, pad);
	snd_seq_ev_set_subs(&moved);
	snd_seq_ev_set_direct(&moved);
	snd_seq_ev_set_controller(&moved, 0, 7, 100);
	snd_seq_event_output_direct(controller_seq, &moved);
	QTRY_COMPARE_WITH_TIMEOUT(read_lights(), 1, 2000);   /* heard from: off, as the mapping says off */
	a->request_playing(true);
	QTRY_COMPARE_WITH_TIMEOUT(read_lights(), 0x7f, 2000);
	a->request_playing(false);
	QTRY_COMPARE_WITH_TIMEOUT(read_lights(), 1, 2000);
	QCOMPARE(lock, -1);   /* key lock off: its light was sent nothing */
	a->set_key_lock(true);
	QTRY_COMPARE_WITH_TIMEOUT((read_lights(), lock), 0x7f, 2000);
}

/*
 * An emergency player (project.md sec 3f): a window of its own, named
 * "emergency", and the deck view, both taking controllers. A binding
 * naming the player reaches only it -- `emergency:play` plays the window
 * and not deck A, `ch_1_play` deck A and not the window -- and a note
 * learned on the window's play button replaces what played it.
 */
void embed_test::each_player_takes_the_bindings_named_for_it() {
	snd_seq_t *sender = nullptr;
	if (snd_seq_open(&sender, "default", SND_SEQ_OPEN_OUTPUT, 0) < 0) QSKIP("no ALSA sequencer");
	auto close_sender = qScopeGuard([sender] { snd_seq_close(sender); });
	snd_seq_set_client_name(sender, "snurra test buttons");
	const int out = snd_seq_create_simple_port(sender, "pads", SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_NO_EXPORT,
	                                           SND_SEQ_PORT_TYPE_MIDI_GENERIC);
	QVERIFY(out >= 0);
	const QString pads = QStringLiteral("snurra test buttons:pads/ch1/");
	{
		QSettings settings(QStringLiteral("snurra"), QStringLiteral("snurra"));
		settings.remove(QStringLiteral("controllers"));
		settings.setValue(QStringLiteral("controllers/bindings"),
		                  QStringList{ pads + QStringLiteral("note50 -> emergency:play (button)"),
		                               pads + QStringLiteral("note51 -> ch_1_play (button)") });
	}
	auto clean = qScopeGuard([] { QSettings(QStringLiteral("snurra"), QStringLiteral("snurra")).remove(QStringLiteral("controllers")); });

	snurra_node emergency;
	snurra_tile_state state;
	state.source = QStringLiteral("tone:330");
	state.duration_us = 60000000;
	state.controls = true;
	state.sampled_at_us = snurra_clock_us();
	snurra_tile *mix = emergency.add_tile(state);
	emergency.show();
	QVERIFY(emergency.take_controllers(QStringLiteral("emergency")));
	/* A second named window with a play of its own: not reached. */
	snurra_node backup;
	snurra_tile *other = backup.add_tile(state);
	backup.show();
	QVERIFY(backup.take_controllers(QStringLiteral("backup")));
	snurra_dj dj;
	dj.show();
	snurra_media media;
	media.path = QStringLiteral("tone:440");
	QVERIFY(dj.load(0, media));
	QVERIFY(dj.take_controllers());
	snurra_tile *deck = dj.findChild<QWidget *>(QStringLiteral("snurra-deck-a"))->findChild<snurra_tile *>();
	for (snurra_midi_in *in : { emergency.findChild<snurra_midi_in *>(), backup.findChild<snurra_midi_in *>(), dj.findChild<snurra_midi_in *>() }) {
		QVERIFY(in);
		QVERIFY(snd_seq_connect_to(sender, out, in->client(), in->port()) >= 0);
	}
	auto note = [sender, out](int number) {
		snd_seq_event_t event;
		snd_seq_ev_clear(&event);
		snd_seq_ev_set_source(&event, out);
		snd_seq_ev_set_subs(&event);
		snd_seq_ev_set_direct(&event);
		snd_seq_ev_set_noteon(&event, 0, number, 100);
		snd_seq_event_output_direct(sender, &event);
		snd_seq_ev_set_noteoff(&event, 0, number, 0);
		snd_seq_event_output_direct(sender, &event);
		QTest::qWait(30);
	};
	note(50);
	QTRY_VERIFY_WITH_TIMEOUT(mix->playing(), 2000);
	QTest::qWait(100);
	QVERIFY(!deck->playing());
	QVERIFY(!other->playing());
	note(51);
	QTRY_VERIFY_WITH_TIMEOUT(deck->playing(), 2000);
	QVERIFY(mix->playing());
	note(50);
	QTRY_VERIFY_WITH_TIMEOUT(!mix->playing(), 2000);
	QVERIFY(deck->playing());

	/* Learned on the window's play button: note 52 plays it now. */
	QWidget *play = mix->play_button();
	QVERIFY(play);
	QCOMPARE(play->property("snurra_control").toString(), QStringLiteral("play"));
	/* Without Alt -- Ctrl+L, as where the window manager takes Alt. */
	QTest::keyClick(&emergency, Qt::Key_L, Qt::ControlModifier);
	QTest::mousePress(play, Qt::LeftButton, Qt::NoModifier, play->rect().center());
	note(52);
	QTest::mouseRelease(play, Qt::LeftButton, Qt::NoModifier, play->rect().center());
	QTest::keyClick(&emergency, Qt::Key_L, Qt::ControlModifier);
	QVERIFY(!mix->playing());
	snurra_bindings kept;
	kept.load();
	QCOMPARE(kept.control_for(pads + QStringLiteral("note52")), QStringLiteral("emergency:play"));
	QVERIFY(kept.control_for(pads + QStringLiteral("note50")).isEmpty());
	QCOMPARE(kept.control_for(pads + QStringLiteral("note51")), QStringLiteral("ch_1_play"));
	QTest::qWait(1100);   /* the window reads the bindings again at most each second */
	note(52);
	QTRY_VERIFY_WITH_TIMEOUT(mix->playing(), 2000);
}

/*
 * Each deck's side of the crossfader (project.md sec 3f, the layout's):
 * A and C left, B and D right by default, written nowhere. C clicked to
 * thru is out of the crossfader's reach -- full whichever way it goes --
 * and D clicked to left goes with A; both choices are kept and a deck view
 * made again has them; D clicked round to its default again forgets it.
 */
void embed_test::each_deck_has_its_side_of_the_crossfader() {
	QSettings(QStringLiteral("snurra"), QStringLiteral("snurra")).remove(QStringLiteral("decks"));
	auto clean = qScopeGuard([] { QSettings(QStringLiteral("snurra"), QStringLiteral("snurra")).remove(QStringLiteral("decks")); });
	auto side_button = [](snurra_dj &dj, int deck) {
		QWidget *place = dj.findChild<QWidget *>(QStringLiteral("snurra-deck-%1").arg(QChar(QLatin1Char(char('a' + deck)))));
		for (QToolButton *button : place->findChildren<QToolButton *>(QString(), Qt::FindDirectChildrenOnly))
			if (button->accessibleName() == QLatin1String("Crossfader side")) return button;
		return static_cast<QToolButton *>(nullptr);
	};
	auto mix_of = [](snurra_dj &dj, int deck) {
		QWidget *place = dj.findChild<QWidget *>(QStringLiteral("snurra-deck-%1").arg(QChar(QLatin1Char(char('a' + deck)))));
		return place->findChild<snurra_tile *>()->deck()->mix();
	};
	{
		snurra_dj dj;
		dj.set_deck_count(4);
		for (int deck = 0; deck < 4; ++deck) {
			snurra_media media;
			media.path = QStringLiteral("tone:%1").arg(220 * (deck + 1));
			QVERIFY(dj.load(deck, media));
		}
		QVERIFY(QSettings(QStringLiteral("snurra"), QStringLiteral("snurra")).childKeys().filter(QStringLiteral("decks")).isEmpty());
		QCOMPARE(side_button(dj, 2)->text(), QStringLiteral("C: left (default)"));
		for (int click = 0; click < 3; ++click) side_button(dj, 2)->click();   /* left, right, thru */
		QCOMPARE(side_button(dj, 2)->text(), QStringLiteral("C: thru"));
		side_button(dj, 3)->click();   /* left */
		dj.set_crossfader(-1);
		QCOMPARE(mix_of(dj, 0), 1.0f);
		QVERIFY(mix_of(dj, 1) < 1e-6f);
		QCOMPARE(mix_of(dj, 2), 1.0f);
		QCOMPARE(mix_of(dj, 3), 1.0f);
		dj.set_crossfader(1);
		QVERIFY(mix_of(dj, 0) < 1e-6f);
		QCOMPARE(mix_of(dj, 2), 1.0f);
		QVERIFY(mix_of(dj, 3) < 1e-6f);
	}
	QSettings settings(QStringLiteral("snurra"), QStringLiteral("snurra"));
	QCOMPARE(settings.value(QStringLiteral("decks/C/crossfader")).toString(), QStringLiteral("thru"));
	QCOMPARE(settings.value(QStringLiteral("decks/D/crossfader")).toString(), QStringLiteral("left"));
	QVERIFY(!settings.contains(QStringLiteral("decks/A/crossfader")));
	snurra_dj again;
	again.set_deck_count(4);
	QCOMPARE(side_button(again, 2)->text(), QStringLiteral("C: thru"));
	for (int click = 0; click < 3; ++click) side_button(again, 3)->click();   /* right, thru, default */
	QCOMPARE(side_button(again, 3)->text(), QStringLiteral("D: right (default)"));
	QVERIFY(!QSettings(QStringLiteral("snurra"), QStringLiteral("snurra")).contains(QStringLiteral("decks/D/crossfader")));
}

/*
 * The decks arranged as the controller in front of the person (project.md
 * sec 3f): by default A B above C D; arranged "C A B D", one row in that
 * order, kept, and a deck view made again has it; back to the default,
 * nothing is kept.
 */
void embed_test::the_decks_sit_as_the_person_arranged_them() {
	QSettings(QStringLiteral("snurra"), QStringLiteral("snurra")).remove(QStringLiteral("decks"));
	auto clean = qScopeGuard([] { QSettings(QStringLiteral("snurra"), QStringLiteral("snurra")).remove(QStringLiteral("decks")); });
	auto where = [](snurra_dj &dj, int deck) {
		QWidget *place = dj.findChild<QWidget *>(QStringLiteral("snurra-deck-%1").arg(QChar(QLatin1Char(char('a' + deck)))));
		/* The deck grid, wherever it sits in the view's column (the
		 * player's timeline strip came above it, 2026-11-21). */
		QGridLayout *grid = nullptr;
		QLayout *view = place->parentWidget()->layout();
		for (int i = 0; view && i < view->count() && !grid; ++i) grid = qobject_cast<QGridLayout *>(view->itemAt(i)->layout());
		if (!grid) return QPoint(-1, -1);
		int row = -1, column = -1, rows = 0, columns = 0;
		grid->getItemPosition(grid->indexOf(place), &row, &column, &rows, &columns);
		return QPoint(column, row);
	};
	{
		snurra_dj dj;
		dj.set_deck_count(4);
		QCOMPARE(where(dj, 0), QPoint(0, 0));
		QCOMPARE(where(dj, 1), QPoint(1, 0));
		QCOMPARE(where(dj, 2), QPoint(0, 1));
		QCOMPARE(where(dj, 3), QPoint(1, 1));
		dj.arrange(QStringLiteral("C A B D"));
		QCOMPARE(where(dj, 2), QPoint(0, 0));
		QCOMPARE(where(dj, 0), QPoint(1, 0));
		QCOMPARE(where(dj, 1), QPoint(2, 0));
		QCOMPARE(where(dj, 3), QPoint(3, 0));
	}
	QCOMPARE(QSettings(QStringLiteral("snurra"), QStringLiteral("snurra")).value(QStringLiteral("decks/arrangement")).toString(), QStringLiteral("C A B D"));
	snurra_dj again;
	again.set_deck_count(4);
	QCOMPARE(where(again, 2), QPoint(0, 0));
	/* Two decks of a four-deck arrangement: where they are named. */
	again.set_deck_count(2);
	QCOMPARE(where(again, 0), QPoint(0, 0));
	QCOMPARE(where(again, 1), QPoint(1, 0));
	again.arrange(QString());
	QVERIFY(!QSettings(QStringLiteral("snurra"), QStringLiteral("snurra")).contains(QStringLiteral("decks/arrangement")));
}

/*
 * The decks' waveforms (project.md sec 3f, the holder's trial of
 * 2026-11-24, Traktor's look): stacked full width above the decks, a row
 * a deck. A 4 s WAV on deck A is read at a hundred columns a second --
 * 400, kept under its own name beside the overview's -- and its row says
 * where the deck is as it plays; an empty deck's row is empty.
 */
void embed_test::the_decks_waveforms_scroll_stacked_above_them() {
	QTemporaryDir dir;
	const QString path = dir.filePath(QStringLiteral("four.wav"));
	{
		QFile file(path);
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write(make_wav_of(4 * 48000, [](int i) { return (i / 2400) % 2 ? 12000 : 0; }));
	}
	snurra_dj dj;
	dj.resize(1200, 900);
	dj.show();
	QVERIFY(QTest::qWaitForWindowExposed(&dj));
	snurra_deck_waves *waves = dj.findChild<snurra_deck_waves *>(QStringLiteral("snurra-deck-waves"));
	QVERIFY(waves && waves->isVisible());
	QCOMPARE(waves->deck_count(), 2);
	QVERIFY(waves->height() >= 2 * 48);
	snurra_media media;
	media.path = path;
	QVERIFY(dj.load(0, media));
	QTRY_COMPARE_WITH_TIMEOUT(waves->columns(0), 400, 5000);
	QCOMPARE(waves->columns(1), 0);
	QVERIFY(QFileInfo::exists(qEnvironmentVariable("SNURRA_CACHE") + QStringLiteral("/peaks/") + snurra_kept_key(path, -1) + QStringLiteral("-400")));
	snurra_deck_waves::deck where, empty;
	waves->fill(0, where);
	waves->fill(1, empty);
	QVERIFY(where.loaded);
	QVERIFY(!empty.loaded);
	snurra_tile *tile = dj.findChild<QWidget *>(QStringLiteral("snurra-deck-a"))->findChild<snurra_tile *>();
	QVERIFY(tile);
	tile->set_playing(true);
	QTest::qWait(600);
	waves->fill(0, where);
	QVERIFY2(where.playing && where.seconds > 0.3 && where.seconds < 1.5, qPrintable(QStringLiteral("at %1 s").arg(where.seconds)));
	dj.set_deck_count(4);
	QCOMPARE(waves->deck_count(), 4);
}

/*
 * A socket on a nested player's track (project.md sec 3f: "a nested
 * player inside a deck" is a tap): a player of two tones -- 440 Hz for
 * 3 s, then 880 Hz coming in 0.5 s before its end -- handed onto deck A,
 * whose fader is down. Plugged into the first track from the listen
 * button's menu, the headphones hear 440 Hz; moved to the second before
 * it comes in, nothing; once it is in, 880 Hz, at full level.
 */
void embed_test::a_listener_hears_one_track_of_a_decks_player() {
	QTemporaryDir runtime, captures;
	qputenv("SNURRA_RUNTIME_DIR", runtime.path().toLocal8Bit());
	auto no_runtime = qScopeGuard([] { qunsetenv("SNURRA_RUNTIME_DIR"); });
	snurra_dj dj;
	dj.resize(900, 600);
	dj.show();
	QVERIFY(QTest::qWaitForWindowExposed(&dj));
	QVERIFY(dj.listen());
	snurra_wire::message offer;
	offer.type = snurra_wire::OFFER;
	offer.token = QStringLiteral("nested");
	offer.deck = 0;
	const qint64 now = snurra_clock_us();
	for (int i = 0; i < 2; ++i) {
		snurra_tile_state tone;
		tone.source = QStringLiteral("tone:%1").arg(440 * (i + 1));
		tone.duration_us = 3000000;
		tone.column = i;
		tone.overlap_us = i > 0 ? 500000 : -1;
		tone.playing = i == 0;
		tone.sampled_at_us = now;
		offer.states << tone;
	}
	{
		QLocalSocket giver;
		giver.connectToServer(dj.socket_path());
		QVERIFY(giver.waitForConnected(2000));
		giver.write(snurra_wire::encode(offer));
		giver.flush();
		QTest::qWait(100);
	}
	QTRY_COMPARE_WITH_TIMEOUT(dj.loaded(0), QStringLiteral("2 tracks"), 3000);
	QElapsedTimer since;
	since.start();
	snurra_tile *tile = dj.findChild<QWidget *>(QStringLiteral("snurra-deck-a"))->findChild<snurra_tile *>();
	QVERIFY(tile);
	QCOMPARE(tile->player_tracks(), 2);
	tile->set_level(0);
	QVERIFY(dj.add_listener(QStringLiteral("capture:") + captures.path()) == 0);
	QToolButton *listen = nullptr;
	for (QToolButton *button : dj.findChild<QWidget *>(QStringLiteral("snurra-deck-a"))->findChildren<QToolButton *>(QString(), Qt::FindDirectChildrenOnly))
		if (button->accessibleName() == QLatin1String("Listen here")) listen = button;
	QVERIFY(listen && listen->menu());
	emit listen->menu()->aboutToShow();
	const QList<QAction *> sockets = listen->menu()->actions();
	QCOMPARE(sockets.size(), 3);   /* the whole deck, then each track */
	QCOMPARE(sockets.at(2)->text(), QStringLiteral("tone:880"));

	const QString capture = QDir(captures.path()).filePath(QStringLiteral("%1.capture").arg(QCoreApplication::applicationPid()));
	auto heard = [&capture](double &hz, double &rms) {
		QFile file(capture);
		if (!file.open(QIODevice::ReadOnly)) return false;
		const QByteArray all = file.readAll();
		std::vector<float> left;
		for (qsizetype at = 0; at + 12 <= all.size();) {
			int32_t frames = 0;
			std::memcpy(&frames, all.constData() + at + 8, 4);
			const qsizetype size = 12 + qsizetype(frames) * 8;
			if (at + size > all.size()) break;
			const float *samples = reinterpret_cast<const float *>(all.constData() + at + 12);
			for (int i = 0; i < frames; ++i) left.push_back(samples[2 * i]);
			at += size;
		}
		const size_t span = 9600;
		if (left.size() < span) return false;
		int crossings = 0;
		double sum = 0;
		for (size_t i = left.size() - span; i < left.size(); ++i) {
			sum += double(left[i]) * left[i];
			if (i > left.size() - span && (left[i - 1] < 0) != (left[i] < 0)) ++crossings;
		}
		rms = std::sqrt(sum / double(span));
		hz = crossings / 2.0 / (double(span) / 48000);
		return true;
	};
	double hz = 0, rms = 0;
	sockets.at(1)->trigger();
	QCOMPARE(dj.plugged(0), 0);
	QCOMPARE(dj.plugged_track(0), 0);
	QTest::qWait(600);
	QVERIFY(heard(hz, rms));
	QVERIFY2(rms > 0.1 && qAbs(hz - 440) < 30, qPrintable(QStringLiteral("the first track: %1 Hz at %2").arg(hz).arg(rms)));
	QVERIFY(since.elapsed() < 2000);
	dj.plug_track(0, 0, 1);
	QCOMPARE(dj.plugged_track(0), 1);
	QTest::qWait(400);
	QVERIFY(heard(hz, rms));
	QVERIFY2(since.elapsed() < 2400, "too slow to hear the second track before it comes in");
	QVERIFY2(rms < 0.01, qPrintable(QStringLiteral("the second track before it comes in: %1 Hz at %2").arg(hz).arg(rms)));
	QTest::qWait(int(std::max<qint64>(0, 3300 - since.elapsed())));
	QVERIFY(heard(hz, rms));
	QVERIFY2(rms > 0.1 && qAbs(hz - 880) < 40, qPrintable(QStringLiteral("the second track, in: %1 Hz at %2").arg(hz).arg(rms)));
}

/*
 * Pre-listen as a plug (project.md sec 3f, step 7): deck A plays 440 Hz
 * with its fader down and the crossfader all the way to B, which plays
 * 1320 Hz. A listener on an output of its own -- a capture the test reads
 * -- plugged into A hears 440 Hz all the same, before the fader and the
 * crossfader; the plug moved to B, it hears 1320; unplugged, nothing.
 */
void embed_test::a_listener_hears_the_deck_its_plug_is_in() {
	QTemporaryDir captures;
	snurra_dj dj;
	dj.resize(900, 600);
	dj.show();
	QVERIFY(QTest::qWaitForWindowExposed(&dj));
	for (int deck = 0; deck < 2; ++deck) {
		snurra_media media;
		media.path = deck == 0 ? QStringLiteral("tone:440") : QStringLiteral("tone:1320");
		QVERIFY(dj.load(deck, media));
	}
	auto tile_of = [&dj](int deck) {
		QWidget *place = dj.findChild<QWidget *>(deck == 0 ? QStringLiteral("snurra-deck-a") : QStringLiteral("snurra-deck-b"));
		return place ? place->findChild<snurra_tile *>() : nullptr;
	};
	tile_of(0)->set_level(0);
	/* The level match's trim on A, as an analysis would set it: heard in
	 * the headphones as in the room, before the fader. */
	tile_of(0)->deck()->set_trim(0.5f);
	dj.set_crossfader(1);
	tile_of(0)->set_playing(true);
	tile_of(1)->set_playing(true);
	const int listener = dj.add_listener(QStringLiteral("capture:") + captures.path());
	QCOMPARE(listener, 0);
	QToolButton *listen = nullptr;
	/* The deck's own socket, not its tile's (shown only in a window). */
	for (QToolButton *button : dj.findChild<QWidget *>(QStringLiteral("snurra-deck-a"))->findChildren<QToolButton *>(QString(), Qt::FindDirectChildrenOnly))
		if (button->accessibleName() == QLatin1String("Listen here")) listen = button;
	QVERIFY2(listen && listen->isVisible(), "no socket on deck A");

	const QString capture = QDir(captures.path()).filePath(QStringLiteral("%1.capture").arg(QCoreApplication::applicationPid()));
	/* The last quarter second heard: its loudness, and its pitch by how
	 * often it crosses zero. */
	auto heard = [&capture](double &hz, double &rms) {
		QFile file(capture);
		if (!file.open(QIODevice::ReadOnly)) return false;
		const QByteArray all = file.readAll();
		std::vector<float> left;
		for (qsizetype at = 0; at + 12 <= all.size();) {
			int32_t frames = 0;
			std::memcpy(&frames, all.constData() + at + 8, 4);
			const qsizetype size = 12 + qsizetype(frames) * 8;
			if (at + size > all.size()) break;
			const float *samples = reinterpret_cast<const float *>(all.constData() + at + 12);
			for (int i = 0; i < frames; ++i) left.push_back(samples[2 * i]);
			at += size;
		}
		const size_t span = 12000;
		if (left.size() < span) return false;
		int crossings = 0;
		double sum = 0;
		for (size_t i = left.size() - span; i < left.size(); ++i) {
			sum += double(left[i]) * left[i];
			if (i > left.size() - span && (left[i - 1] < 0) != (left[i] < 0)) ++crossings;
		}
		rms = std::sqrt(sum / double(span));
		hz = crossings / 2.0 / (double(span) / 48000);
		return true;
	};
	double hz = 0, rms = 0;
	listen->click();
	QCOMPARE(dj.plugged(0), 0);
	QTest::qWait(700);
	QVERIFY(heard(hz, rms));
	/* A tone of 0.2 is 0.141 rms; at the trim's half, 0.071. */
	QVERIFY2(qAbs(rms - 0.0707) < 0.01 && qAbs(hz - 440) < 20, qPrintable(QStringLiteral("plugged into A: %1 Hz at %2").arg(hz).arg(rms)));
	dj.plug(0, 1);
	QVERIFY(!listen->isChecked());
	QTest::qWait(700);
	QVERIFY(heard(hz, rms));
	QVERIFY2(rms > 0.1 && qAbs(hz - 1320) < 30, qPrintable(QStringLiteral("plugged into B: %1 Hz at %2").arg(hz).arg(rms)));
	dj.plug(0, -1);
	QTest::qWait(700);
	QVERIFY(heard(hz, rms));
	QVERIFY2(rms < 1e-4, qPrintable(QStringLiteral("unplugged: %1").arg(rms)));

	/* A second pair of headphones: chosen by the crossfader, the "listen"
	 * moves that one's plug and leaves the first's. */
	QComboBox *headphones = nullptr;
	for (QComboBox *box : dj.findChildren<QComboBox *>())
		if (box->accessibleName() == QLatin1String("Headphones")) headphones = box;
	QVERIFY(headphones && !headphones->isVisible());
	QCOMPARE(dj.add_listener(QStringLiteral("null")), 1);
	QVERIFY(headphones->isVisible());
	QCOMPARE(headphones->count(), 2);
	dj.plug(0, 1);
	headphones->setCurrentIndex(1);
	QVERIFY(!listen->isChecked());
	listen->click();
	QCOMPARE(dj.plugged(1), 0);
	QCOMPARE(dj.plugged(0), 1);
}

/*
 * A plug in a window's tile (project.md sec 3f): a window and a deck view
 * with headphones, each on the wire. The window finds the deck view's
 * listener and shows its tile's socket; clicked, the headphones hear the
 * tile's 440 Hz, which the window plays to its own output; paused there,
 * they fall silent; the socket clicked again, the plug is out.
 */
void embed_test::a_listener_plugs_into_a_windows_tile() {
	QTemporaryDir runtime, captures;
	qputenv("SNURRA_RUNTIME_DIR", runtime.path().toLocal8Bit());
	auto no_runtime = qScopeGuard([] { qunsetenv("SNURRA_RUNTIME_DIR"); });
	snurra_dj dj;
	dj.show();
	QVERIFY(dj.listen());
	QCOMPARE(dj.add_listener(QStringLiteral("capture:") + captures.path()), 0);
	snurra_node window;
	QVERIFY(window.listen());
	snurra_tile_state state;
	state.source = QStringLiteral("tone:440");
	state.duration_us = 60000000;
	state.controls = true;
	state.playing = true;
	state.sampled_at_us = snurra_clock_us();
	snurra_tile *tile = window.add_tile(state);
	window.show();
	QToolButton *socket = nullptr;
	for (QToolButton *button : tile->findChildren<QToolButton *>())
		if (button->accessibleName() == QLatin1String("Listen here")) socket = button;
	QVERIFY(socket);
	QTRY_VERIFY_WITH_TIMEOUT(socket->isVisible(), 4000);
	socket->click();
	const QString capture = QDir(captures.path()).filePath(QStringLiteral("%1.capture").arg(QCoreApplication::applicationPid()));
	auto heard = [&capture](double &hz, double &rms) {
		QFile file(capture);
		if (!file.open(QIODevice::ReadOnly)) return false;
		const QByteArray all = file.readAll();
		std::vector<float> left;
		for (qsizetype at = 0; at + 12 <= all.size();) {
			int32_t frames = 0;
			std::memcpy(&frames, all.constData() + at + 8, 4);
			const qsizetype size = 12 + qsizetype(frames) * 8;
			if (at + size > all.size()) break;
			const float *samples = reinterpret_cast<const float *>(all.constData() + at + 12);
			for (int i = 0; i < frames; ++i) left.push_back(samples[2 * i]);
			at += size;
		}
		const size_t span = 12000;
		if (left.size() < span) return false;
		int crossings = 0;
		double sum = 0;
		for (size_t i = left.size() - span; i < left.size(); ++i) {
			sum += double(left[i]) * left[i];
			if (i > left.size() - span && (left[i - 1] < 0) != (left[i] < 0)) ++crossings;
		}
		rms = std::sqrt(sum / double(span));
		hz = crossings / 2.0 / (double(span) / 48000);
		return true;
	};
	double hz = 0, rms = 0;
	QTRY_VERIFY_WITH_TIMEOUT(heard(hz, rms) && rms > 0.1 && qAbs(hz - 440) < 20, 3000);
	tile->set_playing(false);
	QTRY_VERIFY_WITH_TIMEOUT(heard(hz, rms) && rms < 1e-4, 3000);
	socket->click();
	QTRY_COMPARE_WITH_TIMEOUT(dj.plugged(0), -1, 2000);
}

/*
 * A deck started by hand (project.md sec 3f, step 5): two tones of 6 s,
 * the second to come in at 5 s, crossing the first by 1 s, and cued on
 * deck B until the conductor starts it a lead before. Started by hand at
 * 3 s, it comes in then and plays from its start; folded, the player has
 * it there -- crossing the first by some 3 s, not 1.
 */
void embed_test::a_deck_started_by_hand_is_where_its_track_comes_in() {
	QTemporaryDir runtime;
	qputenv("SNURRA_RUNTIME_DIR", runtime.path().toLocal8Bit());
	auto no_runtime = qScopeGuard([] { qunsetenv("SNURRA_RUNTIME_DIR"); });
	snurra_dj dj;
	dj.show();
	QVERIFY(dj.listen());
	snurra_wire::message offer;
	offer.type = snurra_wire::OFFER;
	offer.token = QStringLiteral("by hand");
	offer.deck = 0;
	const qint64 now = snurra_clock_us();
	for (int i = 0; i < 2; ++i) {
		snurra_tile_state tone;
		tone.source = QStringLiteral("tone:%1").arg(330 + 110 * i);
		tone.duration_us = 6000000;
		tone.column = i;
		tone.overlap_us = i == 1 ? 1000000 : -1;
		tone.playing = i == 0;
		tone.sampled_at_us = now;
		offer.states << tone;
	}
	{
		QLocalSocket giver;
		giver.connectToServer(dj.socket_path());
		QVERIFY(giver.waitForConnected(2000));
		giver.write(snurra_wire::encode(offer));
		giver.flush();
		QTest::qWait(100);
	}
	QTRY_COMPARE_WITH_TIMEOUT(dj.loaded(0), QStringLiteral("2 tracks"), 3000);
	QElapsedTimer since;
	since.start();
	QVERIFY(dj.unfold(0));
	QVERIFY(!dj.start_now(0));   /* deck A's track has started already */
	QTest::qWait(int(std::max<qint64>(0, 3000 - since.elapsed())));
	QWidget *place = dj.findChild<QWidget *>(QStringLiteral("snurra-deck-b"));
	snurra_tile *b = place->findChild<snurra_tile *>();
	QVERIFY(b && !b->playing());
	/* Where the player stands: deck A's track began with it. */
	snurra_tile *a = dj.findChild<QWidget *>(QStringLiteral("snurra-deck-a"))->findChild<snurra_tile *>();
	QVERIFY(a && a->playing());
	const qint64 player_at = a->state().position_now(snurra_clock_us());
	QVERIFY(dj.start_now(1));
	QVERIFY(!dj.start_now(1));   /* in now: nothing cued there */
	QTest::qWait(500);
	QVERIFY(b->playing());
	QVERIFY2(qAbs(b->state().position_us - 450000) < 250000, qPrintable(QStringLiteral("B at %1 us").arg(b->state().position_us)));
	QVERIFY(dj.fold());
	QWidget *a_place = dj.findChild<QWidget *>(QStringLiteral("snurra-deck-a"));
	QTest::qWait(300);
	snurra_tile *folded = a_place->findChild<snurra_tile *>();
	QVERIFY(folded && folded->state().player.size() == 2);
	const qint64 overlap = folded->state().player[1].overlap_us;
	/* Brought in a latency and 50 ms from the asking. */
	const qint64 meant = 6000000 - (player_at + 60000);
	QVERIFY2(qAbs(overlap - meant) < 250000, qPrintable(QStringLiteral("crossing %1 us, meant about %2").arg(overlap).arg(meant)));
}

/*
 * Moves on the decks written back (project.md sec 3f, step 5): a player
 * of two unfolded, deck A's fader held and pulled from 100 to 50 while it
 * plays. Let go, the ride is A's track's envelope -- ending at half --
 * and the fader is back at 100, so the automation plays the move.
 */
void embed_test::a_fader_ridden_is_written_into_the_track() {
	QTemporaryDir runtime;
	qputenv("SNURRA_RUNTIME_DIR", runtime.path().toLocal8Bit());
	auto no_runtime = qScopeGuard([] { qunsetenv("SNURRA_RUNTIME_DIR"); });
	snurra_dj dj;
	dj.resize(900, 600);
	dj.show();
	QVERIFY(QTest::qWaitForWindowExposed(&dj));
	QVERIFY(dj.listen());
	snurra_wire::message offer;
	offer.type = snurra_wire::OFFER;
	offer.token = QStringLiteral("ride");
	offer.deck = 0;
	const qint64 now = snurra_clock_us();
	for (int i = 0; i < 2; ++i) {
		snurra_tile_state tone;
		tone.source = QStringLiteral("tone:%1").arg(330 + 110 * i);
		tone.duration_us = 6000000;
		tone.column = i;
		tone.playing = i == 0;
		tone.sampled_at_us = now;
		offer.states << tone;
	}
	{
		QLocalSocket giver;
		giver.connectToServer(dj.socket_path());
		QVERIFY(giver.waitForConnected(2000));
		giver.write(snurra_wire::encode(offer));
		giver.flush();
		QTest::qWait(100);
	}
	QTRY_COMPARE_WITH_TIMEOUT(dj.loaded(0), QStringLiteral("2 tracks"), 3000);
	QVERIFY(dj.unfold(0));
	/* After the tile that held the player has gone, which it does in the
	 * event loop: found before, it is the one found. */
	QTest::qWait(300);
	QWidget *place = dj.findChild<QWidget *>(QStringLiteral("snurra-deck-a"));
	snurra_tile *tile = place->findChild<snurra_tile *>();
	QVERIFY(tile && tile->playing());
	QCOMPARE(dj.loaded(0), QStringLiteral("tone:330"));
	QSlider *level = nullptr;
	for (QSlider *slider : place->findChildren<QSlider *>())
		if (slider->accessibleName() == QLatin1String("Level")) level = slider;
	QVERIFY(level);
	emit level->sliderPressed();
	for (int value = 100; value >= 50; value -= 5) {
		level->setValue(value);
		QTest::qWait(60);
	}
	QTest::qWait(100);
	emit level->sliderReleased();
	const QList<QPair<qint64, float>> envelope = tile->state().envelope;
	QVERIFY2(envelope.size() >= 4, qPrintable(QString::number(envelope.size())));
	QVERIFY2(envelope.size() <= snurra_deck::ENVELOPE_POINTS, qPrintable(QString::number(envelope.size())));
	/* The ride's last point at half, then back to what was there. */
	QVERIFY2(qAbs(envelope.at(envelope.size() - 2).second - 0.5f) < 0.06f, qPrintable(QString::number(envelope.at(envelope.size() - 2).second)));
	QCOMPARE(envelope.last().second, 1.0f);
	QCOMPARE(tile->level(), 100);
	for (int i = 1; i < envelope.size(); ++i) QVERIFY(envelope[i].first > envelope[i - 1].first);

	/* Folded back: deck A holds the player again, as edited -- the ride
	 * in its first track's envelope -- where it stood, and B is empty. */
	/* A move of the EQ by hand on the conducted deck is the track's too. */
	tile->set_eq(-6, 0, SNURRA_EQ_KILL);
	const qint64 before = tile->state().position_us;
	QToolButton *fold = nullptr;
	for (QToolButton *button : place->findChildren<QToolButton *>())
		if (button->accessibleName() == QLatin1String("Fold back into one deck") && button->isVisible()) fold = button;
	QVERIFY2(fold, "no Fold on a conducted deck");
	fold->click();
	QCOMPARE(dj.loaded(0), QStringLiteral("2 tracks"));
	QVERIFY(dj.loaded(1).isEmpty());
	QTest::qWait(300);
	snurra_tile *folded = place->findChild<snurra_tile *>();
	QVERIFY(folded && folded->playing());
	QCOMPARE(int(folded->state().player.size()), 2);
	QCOMPARE(folded->state().player[0].envelope, envelope);
	QCOMPARE(folded->state().player[0].eq.part_db(0), -6.0f);
	QCOMPARE(folded->state().player[0].eq.part_db(2), SNURRA_EQ_KILL);
	QVERIFY2(folded->state().position_us > before && folded->state().position_us < before + 1500000,
	         qPrintable(QStringLiteral("folded at %1 us, was at %2").arg(folded->state().position_us).arg(before)));
	QVERIFY(!dj.fold());
}

/*
 * A player unfolded across the decks (project.md sec 3f, step 4): a train
 * of three tones, 3 s each crossing over 1 s, handed onto deck A whole
 * over the wire, then unfolded. Its tracks take decks A, B, A: the first
 * plays on A, the second waits cued on B and comes in at 2 s fading in
 * while A fades out, and once A's track has ended the third is cued there
 * and comes in at 4 s. A hand on B's fader takes B from the automation --
 * no fade on it while held -- and gives it back when let go.
 */
/*
 * Both views at once, a first step (sec 3f: "both show the one
 * timeline"): a player unfolded on the decks shows above them as its
 * timeline -- three tracks on two channels, the one sounding filled, the
 * playhead moving -- and folded back, the strip goes.
 */
void embed_test::the_decks_show_the_player_they_play() {
	QTemporaryDir runtime;
	qputenv("SNURRA_RUNTIME_DIR", runtime.path().toLocal8Bit());
	auto no_runtime = qScopeGuard([] { qunsetenv("SNURRA_RUNTIME_DIR"); });
	snurra_dj dj;
	dj.resize(900, 600);
	dj.show();
	QVERIFY(QTest::qWaitForWindowExposed(&dj));
	QVERIFY(dj.listen());
	QWidget *view = dj.findChild<QWidget *>(QStringLiteral("snurra-set-view"));
	QVERIFY(view && !view->isVisible());
	snurra_wire::message offer;
	offer.type = snurra_wire::OFFER;
	offer.token = QStringLiteral("shown");
	offer.deck = 0;
	const qint64 now = snurra_clock_us();
	for (int i = 0; i < 3; ++i) {
		snurra_tile_state tone;
		tone.source = QStringLiteral("tone:%1").arg(220 + 110 * i);
		tone.duration_us = 3000000;
		tone.column = i;
		tone.overlap_us = i > 0 ? 1000000 : -1;
		tone.playing = i == 0;
		tone.sampled_at_us = now;
		offer.states << tone;
	}
	{
		QLocalSocket giver;
		giver.connectToServer(dj.socket_path());
		QVERIFY(giver.waitForConnected(2000));
		giver.write(snurra_wire::encode(offer));
		giver.flush();
		QTest::qWait(100);
	}
	QTRY_COMPARE_WITH_TIMEOUT(dj.loaded(0), QStringLiteral("3 tracks"), 3000);
	QVERIFY(!view->isVisible());
	QVERIFY(dj.unfold(0));
	QTRY_VERIFY_WITH_TIMEOUT(view->isVisible(), 2000);
	view->repaint();
	QCOMPARE(view->property("tracks").toInt(), 3);
	QVERIFY(view->height() >= 2 * view->fontMetrics().height());   /* two channels */
	const double first = view->property("now_x").toDouble();
	QTest::qWait(600);
	view->repaint();
	QVERIFY2(view->property("now_x").toDouble() > first, "the playhead stood still");
	/* Pressed near its end, the player goes there: the playhead drawn
	 * where it was pressed. */
	const int near_end = view->width() * 9 / 10;
	QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, QPoint(near_end, 4));
	view->repaint();
	QVERIFY2(std::abs(view->property("now_x").toDouble() - near_end) < view->width() / 20.0,
	         qPrintable(QStringLiteral("playhead at %1, pressed at %2").arg(view->property("now_x").toDouble()).arg(near_end)));
	QVERIFY(dj.fold());
	QVERIFY(!view->isVisible());
}

/*
 * Both views open at once (sec 3f): a window following the player the
 * decks play holds its tracks as tiles, silent and in step with the
 * decks; an EQ turned on a deck shows on that track's tile, and one
 * turned in the window on the deck; a crossing moved in the window is the
 * decks'; a seek in the window moves the decks; folded, the window goes.
 */
void embed_test::a_window_follows_the_player_on_the_decks() {
	QTemporaryDir runtime;
	qputenv("SNURRA_RUNTIME_DIR", runtime.path().toLocal8Bit());
	auto no_runtime = qScopeGuard([] { qunsetenv("SNURRA_RUNTIME_DIR"); });
	snurra_dj dj;
	dj.resize(900, 600);
	dj.show();
	QVERIFY(QTest::qWaitForWindowExposed(&dj));
	QVERIFY(dj.listen());
	snurra_wire::message offer;
	offer.type = snurra_wire::OFFER;
	offer.token = QStringLiteral("followed");
	offer.deck = 0;
	const qint64 now = snurra_clock_us();
	/* Long enough that the last track comes only by the seek. */
	for (int i = 0; i < 3; ++i) {
		snurra_tile_state tone;
		tone.source = QStringLiteral("tone:%1").arg(220 + 110 * i);
		tone.duration_us = 10000000;
		tone.column = i;
		tone.overlap_us = i > 0 ? 1000000 : -1;
		tone.playing = i == 0;
		tone.sampled_at_us = now;
		offer.states << tone;
	}
	{
		QLocalSocket giver;
		giver.connectToServer(dj.socket_path());
		QVERIFY(giver.waitForConnected(2000));
		giver.write(snurra_wire::encode(offer));
		giver.flush();
		QTest::qWait(100);
	}
	QTRY_COMPARE_WITH_TIMEOUT(dj.loaded(0), QStringLiteral("3 tracks"), 3000);
	QVERIFY(dj.unfold(0));
	QWidget *also = dj.findChild<QWidget *>(QStringLiteral("snurra-set-window"));
	QTRY_VERIFY_WITH_TIMEOUT(also && also->isVisible(), 2000);

	snurra_node node;
	node.set_stays_open(true);
	node.show();
	QVERIFY(QTest::qWaitForWindowExposed(&node));
	node.follow(dj.socket_path());
	QTRY_COMPARE_WITH_TIMEOUT(node.tile_count(), 3, 3000);
	auto window_tile = [&](const QString &source) -> snurra_tile * {
		for (snurra_tile *tile : node.findChildren<snurra_tile *>())
			if (tile->isVisible() && tile->state().source == source) return tile;
		return nullptr;
	};
	auto deck_tile = [&](const QString &source) -> snurra_tile * {
		for (snurra_tile *tile : dj.findChildren<snurra_tile *>())
			if (tile->isVisible() && tile->state().source == source) return tile;
		return nullptr;
	};
	const QString first = QStringLiteral("tone:220"), last = QStringLiteral("tone:440");
	QTRY_VERIFY_WITH_TIMEOUT(window_tile(first) && window_tile(first)->playing(), 2000);
	/* Its tracks are the decks': no menu to remove one or take it into a
	 * window of its own (which would sound it twice), no "decks". */
	QWidget *to_decks = node.findChild<QWidget *>(QStringLiteral("snurra-bar-decks"));
	QVERIFY(to_decks && !to_decks->isVisible());
	bool menu_shown = false;
	QTimer::singleShot(200, [&menu_shown] {
		if (QWidget *menu = QApplication::activePopupWidget()) {
			menu_shown = true;
			menu->close();
		}
	});
	const QPoint over = window_tile(first)->geometry().center();
	QContextMenuEvent asked(QContextMenuEvent::Mouse, over, node.mapToGlobal(over));
	QCoreApplication::sendEvent(&node, &asked);
	QTest::qWait(300);
	QVERIFY2(!menu_shown, "a following window offered its tracks' menu");
	for (snurra_tile *tile : node.findChildren<snurra_tile *>()) QCOMPARE(tile->deck()->mix(), 0.0f);

	/* The deck's EQ, turned there, on the window's tile. */
	QTRY_VERIFY_WITH_TIMEOUT(deck_tile(first), 2000);
	snurra_eq eq = deck_tile(first)->state().eq;
	eq.set_part(0, -12);
	deck_tile(first)->set_eq(eq);
	QTRY_COMPARE_WITH_TIMEOUT(window_tile(first)->state().eq.part_db(0), -12.0f, 2000);

	/* And back (live both ways): the treble turned in the window is on
	 * the deck, the bass the deck's hand turned kept there. */
	snurra_eq mine = window_tile(first)->state().eq;
	mine.set_part(2, 4);
	window_tile(first)->set_eq(mine);
	QTRY_COMPARE_WITH_TIMEOUT(deck_tile(first)->state().eq.part_db(2), 4.0f, 2000);
	QCOMPARE(deck_tile(first)->state().eq.part_db(0), -12.0f);
	/* A crossing moved in the window is the decks' player's. */
	auto told = [&]() -> QList<snurra_tile_state> {
		QLocalSocket asking;
		asking.connectToServer(dj.socket_path());
		if (!asking.waitForConnected(1000)) return {};
		snurra_wire::message follow;
		follow.type = snurra_wire::FOLLOW;
		asking.write(snurra_wire::encode(follow));
		asking.flush();
		QByteArray buffer;
		QElapsedTimer waited;
		waited.start();
		while (waited.elapsed() < 1000) {
			QCoreApplication::processEvents();
			asking.waitForReadyRead(20);
			buffer += asking.readAll();
			const QList<snurra_wire::message> got = snurra_wire::decode(buffer);
			if (!got.isEmpty()) return got.first().states;
		}
		return {};
	};
	window_tile(QStringLiteral("tone:330"))->set_overlap_us(3000000);
	QTRY_VERIFY_WITH_TIMEOUT(told().size() == 3 && told().at(1).overlap_us == 3000000, 3000);
	QCOMPARE(window_tile(QStringLiteral("tone:330"))->state().overlap_us, qint64(3000000));

	/* The window's seek, into the last track: the decks play it, and the
	 * window with them. */
	snurra_tile *third = window_tile(last);
	QVERIFY(third);
	emit third->seek_requested(third, 1000000);
	QTRY_VERIFY_WITH_TIMEOUT(deck_tile(last) && deck_tile(last)->playing(), 3000);
	QTRY_VERIFY_WITH_TIMEOUT(window_tile(last) && window_tile(last)->playing(), 3000);
	QVERIFY(!window_tile(first)->playing());

	/* A second window following, closed by the hand: it stops asking,
	 * so it can end -- not hidden and polling on. */
	snurra_node closed;
	closed.set_stays_open(true);
	closed.show();
	QVERIFY(QTest::qWaitForWindowExposed(&closed));
	closed.follow(dj.socket_path());
	QTRY_COMPARE_WITH_TIMEOUT(closed.tile_count(), 3, 3000);
	QTest::keyClick(&closed, Qt::Key_Escape);
	QCOMPARE(closed.tile_count(), 0);
	QVERIFY2(!closed.follows_decks(), "a following window closed by the hand followed on");

	QVERIFY(dj.fold());
	QTRY_VERIFY_WITH_TIMEOUT(!node.isVisible(), 2000);
	QCOMPARE(node.tile_count(), 0);
}

void embed_test::a_player_unfolds_across_the_decks() {
	QTemporaryDir runtime;
	qputenv("SNURRA_RUNTIME_DIR", runtime.path().toLocal8Bit());
	auto no_runtime = qScopeGuard([] { qunsetenv("SNURRA_RUNTIME_DIR"); });
	snurra_dj dj;
	dj.resize(900, 600);
	dj.show();
	QVERIFY(QTest::qWaitForWindowExposed(&dj));
	QVERIFY(dj.listen());

	snurra_wire::message offer;
	offer.type = snurra_wire::OFFER;
	offer.token = QStringLiteral("unfold");
	offer.deck = 0;
	const qint64 now = snurra_clock_us();
	for (int i = 0; i < 3; ++i) {
		snurra_tile_state tone;
		tone.source = QStringLiteral("tone:%1").arg(220 + 110 * i);
		tone.duration_us = 3000000;
		tone.column = i;
		tone.overlap_us = i > 0 ? 1000000 : -1;
		tone.playing = i == 0;
		tone.sampled_at_us = now;
		offer.states << tone;
	}
	{
		/* Handed over and gone: the deck view fades it in on its own. */
		QLocalSocket giver;
		giver.connectToServer(dj.socket_path());
		QVERIFY(giver.waitForConnected(2000));
		giver.write(snurra_wire::encode(offer));
		giver.flush();
		QTest::qWait(100);
	}
	QTRY_COMPARE_WITH_TIMEOUT(dj.loaded(0), QStringLiteral("3 tracks"), 3000);
	QToolButton *unfold = nullptr;
	for (QToolButton *button : dj.findChildren<QToolButton *>())
		if (button->accessibleName() == QLatin1String("Unfold across the decks") && button->isVisible()) unfold = button;
	QVERIFY2(unfold, "no Unfold on a deck holding a player");
	QElapsedTimer since;
	since.start();
	unfold->click();
	QCOMPARE(dj.loaded(0), QStringLiteral("tone:220"));
	QCOMPARE(dj.loaded(1), QStringLiteral("tone:330"));

	auto tile_of = [&dj](int deck) {
		const QString name = deck == 0 ? QStringLiteral("snurra-deck-a") : QStringLiteral("snurra-deck-b");
		QWidget *place = dj.findChild<QWidget *>(name);
		return place ? place->findChild<snurra_tile *>() : nullptr;
	};
	QVERIFY(tile_of(0)->playing());
	QVERIFY(!tile_of(1)->playing());
	/* At about 2.5 s: B in, half a second into its track, faded in; A on
	 * its way out. */
	QTest::qWait(int(std::max<qint64>(0, 2500 - since.elapsed())));
	QVERIFY(tile_of(1)->playing());
	QVERIFY2(qAbs(tile_of(1)->state().position_us - 500000) < 250000,
	         qPrintable(QStringLiteral("B at %1 us").arg(tile_of(1)->state().position_us)));
	QCOMPARE(tile_of(1)->deck()->fade_from(), 0.0f);
	QCOMPARE(tile_of(0)->deck()->fade_to(), 0.0f);
	/* And seen: each conducted deck's meter shows what is heard of it --
	 * B part of the way in, A part of the way out. */
	auto meter = [](snurra_tile *tile) {
		for (QProgressBar *bar : tile->window()->findChildren<QProgressBar *>())
			if (bar->accessibleName() == QLatin1String("Heard") && tile->isAncestorOf(bar) && bar->isVisible()) return bar->value();
		return -1;
	};
	QVERIFY2(meter(tile_of(1)) > 5 && meter(tile_of(1)) < 95, qPrintable(QStringLiteral("B heard at %1").arg(meter(tile_of(1)))));
	QVERIFY2(meter(tile_of(0)) > 5 && meter(tile_of(0)) < 95, qPrintable(QStringLiteral("A heard at %1").arg(meter(tile_of(0)))));

	/* A hand on B's fader: no automation while held, back when let go. */
	QSlider *level = nullptr;
	for (QSlider *slider : tile_of(1)->window()->findChildren<QSlider *>())
		if (slider->accessibleName() == QLatin1String("Level") && tile_of(1)->isAncestorOf(slider)) level = slider;
	if (!level)
		for (QSlider *slider : dj.findChildren<QSlider *>())
			if (slider->accessibleName() == QLatin1String("Level") && dj.findChild<QWidget *>(QStringLiteral("snurra-deck-b"))->isAncestorOf(slider))
				level = slider;
	QVERIFY(level);
	emit level->sliderPressed();
	QCOMPARE(tile_of(1)->deck()->fade_from(), 1.0f);
	QCOMPARE(tile_of(1)->deck()->fade_to(), 1.0f);
	emit level->sliderReleased();
	QVERIFY(tile_of(1)->deck()->fade_from() == 0.0f || tile_of(1)->deck()->fade_to() == 0.0f);

	/* A's track over at 3 s: the third cued there, in at 4 s -- started a
	 * lead ahead on the shared clock, as a train's next is, so standing
	 * at its first frame until then, half a second in at 4.5 s. */
	QTRY_COMPARE_WITH_TIMEOUT(dj.loaded(0), QStringLiteral("tone:440"), 2000);
	QVERIFY2(since.elapsed() < 3900, qPrintable(QString::number(since.elapsed())));
	QTest::qWait(int(std::max<qint64>(0, 3700 - since.elapsed())));
	QVERIFY2(tile_of(0)->state().position_us < 100000,
	         qPrintable(QStringLiteral("the third at %1 us at 3.7 s").arg(tile_of(0)->state().position_us)));
	QTest::qWait(int(std::max<qint64>(0, 4500 - since.elapsed())));
	QVERIFY2(qAbs(tile_of(0)->state().position_us - 500000) < 250000,
	         qPrintable(QStringLiteral("the third at %1 us at 4.5 s").arg(tile_of(0)->state().position_us)));
}

/*
 * The DJ player (embed.h 3, the holder's simplified Traktor): two decks
 * and a browser. Loaded by a call, by a double-click in the browser --
 * onto the deck that is empty -- and by a drop on a deck; the crossfader
 * sends A alone, both at equal power, or B alone; sync puts one deck's
 * beat on the other's; a tune let go over nothing opens no player.
 */
/*
 * A folder chosen while the one before is still being listed: only the
 * new one's tunes. The first scan's pieces, handed over before it was
 * stopped and still queued, showed beside them (seen under load in
 * the_dj_player_loads_two_decks_and_crosses_them, 5 for 3).
 */
void embed_test::a_folder_chosen_while_another_lists_shows_only_its_own() {
	QTemporaryDir first, second;
	for (const QString &name : { QStringLiteral("a.wav"), QStringLiteral("b.wav") }) {
		QFile file(first.filePath(name));
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write(make_wav(4800));
	}
	for (const QString &name : { QStringLiteral("c.wav"), QStringLiteral("d.wav"), QStringLiteral("e.wav") }) {
		QFile file(second.filePath(name));
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write(make_wav(4800));
	}
	snurra_library library;
	library.set_folder(first.path());
	/* The first scan done and its list queued, not yet taken. */
	QThread::msleep(300);
	library.set_folder(second.path());
	QTRY_COMPARE_WITH_TIMEOUT(library.shown().size(), 3, 5000);
	QTest::qWait(300);
	QCOMPARE(library.shown().size(), 3);
	for (const QString &path : library.shown()) QVERIFY2(path.startsWith(second.path()), qPrintable(path));
}

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
	QCOMPARE(a->deck()->mix(), 1.0f);
	QVERIFY(b->deck()->mix() < 1e-6f);
	dj.set_crossfader(0);
	QVERIFY(qAbs(a->deck()->mix() - float(M_SQRT1_2)) < 1e-4f && qAbs(b->deck()->mix() - float(M_SQRT1_2)) < 1e-4f);
	dj.set_crossfader(1);
	QVERIFY(a->deck()->mix() < 1e-6f);
	QCOMPARE(b->deck()->mix(), 1.0f);

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
	QWidget *eq = deck->findChild<QWidget *>(QStringLiteral("snurra-eq"));
	QVERIFY(eq);
	/* The EQ line: a band pulled to the bottom is killed, to the top
	 * +6 dB; one stroke from the low band's bottom across to the high
	 * band's middle draws all three, the mid taking the height it was
	 * crossed at. */
	const int w = eq->width(), h = eq->height();
	auto at = [w, h](int band, int step) {   /* -12 the bottom, 12 the top */
		return QPoint(int((band + 0.5) * w / 3), int(std::lround(2.5 + (12 - step) / 24.0 * (h - 5))));
	};
	auto move_to = [eq](QPoint to) {
		QMouseEvent moved(QEvent::MouseMove, to, eq->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
		QCoreApplication::sendEvent(eq, &moved);
	};
	QCOMPARE(deck->state().eq.part_db(0), 0.0f);
	QTest::mousePress(eq, Qt::LeftButton, Qt::NoModifier, at(0, 12));
	QCOMPARE(deck->state().eq.part_db(0), 6.0f);
	QCOMPARE(deck->state().eq.part_db(1), 0.0f);   /* the others untouched */
	move_to(at(0, -12));
	QCOMPARE(deck->state().eq.part_db(0), SNURRA_EQ_KILL);
	move_to(at(2, 0));   /* across the mid, at the middle */
	QCOMPARE(deck->state().eq.part_db(1), 0.0f);
	QCOMPARE(deck->state().eq.part_db(2), 0.0f);
	move_to(at(2, -6));
	QCOMPARE(deck->state().eq.part_db(2), -12.0f);
	QTest::mouseRelease(eq, Qt::LeftButton, Qt::NoModifier, at(2, -6));
	move_to(at(2, 12));   /* not held: nothing */
	QCOMPARE(deck->state().eq.part_db(2), -12.0f);
	/* A stroke skips no band: low to high in one move, the mid too. */
	QTest::mousePress(eq, Qt::LeftButton, Qt::NoModifier, at(0, 6));
	move_to(at(2, 6));
	QCOMPARE(deck->state().eq.part_db(1), 3.0f);
	QCOMPARE(deck->state().eq.part_db(2), 3.0f);
	QTest::mouseRelease(eq, Qt::LeftButton, Qt::NoModifier, at(2, 6));
	/* Drawn band by band, each keeps its own height: low down, mid
	 * flat, high up -- moving on into the next does not take the last
	 * with it. */
	QTest::mousePress(eq, Qt::LeftButton, Qt::NoModifier, at(0, -4));
	move_to(at(1, 0));
	move_to(at(2, 4));
	QTest::mouseRelease(eq, Qt::LeftButton, Qt::NoModifier, at(2, 4));
	QCOMPARE(deck->state().eq.part_db(0), -8.0f);
	QCOMPARE(deck->state().eq.part_db(1), 0.0f);
	QCOMPARE(deck->state().eq.part_db(2), 2.0f);
	deck->set_eq(3, 3, 3);
	QTest::mouseDClick(eq, Qt::LeftButton, Qt::NoModifier, at(1, 6));
	QCOMPARE(deck->state().eq.part_db(1), 0.0f);
	QCOMPARE(deck->state().eq.part_db(0), 3.0f);
	QTest::mousePress(eq, Qt::LeftButton, Qt::NoModifier, at(0, -12));
	QTest::mouseRelease(eq, Qt::LeftButton, Qt::NoModifier, at(0, -12));
	QCOMPARE(deck->state().eq.part_db(0), SNURRA_EQ_KILL);
	/* And a step short of the bottom is -22 dB. */
	deck->set_eq(-22, 0, 0);
	QCOMPARE(deck->state().eq.part_db(0), -22.0f);
	deck->set_eq(SNURRA_EQ_KILL, 3, 3);

	QByteArray bytes;
	QDataStream out(&bytes, QIODevice::WriteOnly);
	out << deck->state();
	snurra_tile_state back;
	QDataStream in(bytes);
	in >> back;
	QCOMPARE(back.cue_us, deck->cue_us());
	QCOMPARE(back.eq.part_db(0), SNURRA_EQ_KILL);
	QTest::mouseDClick(eq, Qt::LeftButton, Qt::NoModifier, at(0, 0));
	QCOMPARE(deck->state().eq.part_db(0), 0.0f);
}

/*
 * The EQ line split (project.md sec 4, The EQ): five bands set on the
 * tile, the sub killed, are drawn as the isolator does them -- the line
 * at the bottom under the sub, on the middle at 1 kHz, and partway down
 * on the slope at 125 Hz where they cross and just below it, not a step; a press sets the
 * band under it alone; a double-click on a split joins the two bands
 * there, the lower's height taken; the menu splits it into twelve; and a
 * slope drawn inside one band splits it.
 */
void embed_test::the_eq_line_draws_its_slopes_and_splits() {
	snurra_dj dj;
	snurra_media media;
	media.path = QStringLiteral("tone:440");
	QVERIFY(dj.load(0, media));
	dj.resize(900, 600);
	dj.show();
	QVERIFY(QTest::qWaitForWindowExposed(&dj));
	snurra_tile *deck = dj.findChild<QWidget *>(QStringLiteral("snurra-deck-a"))->findChild<snurra_tile *>();
	QVERIFY(deck);
	QWidget *line = deck->findChild<QWidget *>(QStringLiteral("snurra-eq"));
	QVERIFY(line && line->width() > 100);
	snurra_eq five;
	five.set_layout(snurra_eq::FIVE);
	five.set_band(0, SNURRA_EQ_KILL);
	five.set_band(4, -6.0f);   /* above 8 kHz: not flat beside 4 to 8, so kept apart */
	deck->set_eq(five);
	QCOMPARE(deck->state().eq.bands(), 5);
	const int w = line->width(), h = line->height();
	auto x_of = [w](double hz) { return int(std::lround(snurra_eq_fraction_of(hz) * w)); };
	/* Where the line is drawn in a column: the rows in the highlight's
	 * colour, averaged; -1 for none. */
	const QImage drawn = line->grab().toImage();
	const QColor ink = line->palette().color(QPalette::Highlight);
	auto row_at = [&](int x) {
		double rows = 0;
		int found = 0;
		const qreal ratio = drawn.devicePixelRatio();
		for (int y = 0; y < drawn.height(); ++y) {
			const QColor at = drawn.pixelColor(int(x * ratio), y);
			if (std::abs(at.red() - ink.red()) + std::abs(at.green() - ink.green()) + std::abs(at.blue() - ink.blue()) < 60) {
				rows += y / ratio;
				++found;
			}
		}
		return found ? rows / found : -1.0;
	};
	const double sub = row_at(x_of(45)), crossing = row_at(x_of(125)), flat = row_at(x_of(1000));
	qInfo("the line at 45 Hz row %.1f, at 125 Hz %.1f, at 1 kHz %.1f, of %d", sub, crossing, flat, h);
	QVERIFY2(sub > h * 0.85, "the killed sub not drawn at the bottom");
	QVERIFY2(std::abs(flat - h / 2.0) < h * 0.1, "the flat band not drawn on the middle");
	QVERIFY2(crossing > flat + h * 0.05 && crossing < sub - h * 0.05, "no slope where the bands cross");
	/* Inside the sub, a sixth of an octave from its split: a step would
	 * be at the bottom already, the crossover's slope is not yet. */
	const double near = row_at(x_of(105));
	qInfo("at 105 Hz row %.1f", near);
	QVERIFY2(near < sub - h * 0.1, "drawn as a step, not the crossover's slope");

	/* A press at the top of the band from 125 to 250 Hz: that band +6
	 * dB, the sub still killed, the mid still flat. */
	QTest::mousePress(line, Qt::LeftButton, Qt::NoModifier, QPoint(x_of(175), 3));
	QTest::mouseRelease(line, Qt::LeftButton, Qt::NoModifier, QPoint(x_of(175), 3));
	QCOMPARE(deck->state().eq.band_level(1), 6.0f);
	QCOMPARE(deck->state().eq.band_level(0), SNURRA_EQ_KILL);
	QCOMPARE(deck->state().eq.band_level(2), 0.0f);
	/* Hovered, it says which band and how loud. */
	QHelpEvent hovered(QEvent::ToolTip, QPoint(x_of(175), h / 2), line->mapToGlobal(QPoint(x_of(175), h / 2)));
	QCoreApplication::sendEvent(line, &hovered);
	QVERIFY2(QToolTip::text().startsWith(QStringLiteral("125 Hz to 250 Hz: +6.0 dB")), qPrintable(QToolTip::text()));
	QHelpEvent below(QEvent::ToolTip, QPoint(x_of(45), h / 2), line->mapToGlobal(QPoint(x_of(45), h / 2)));
	QCoreApplication::sendEvent(line, &below);
	QVERIFY2(QToolTip::text().startsWith(QStringLiteral("below 125 Hz: killed")), qPrintable(QToolTip::text()));
	QToolTip::hideText();
	/* Joined at 125 Hz: four bands, the joined one killed as the sub was. */
	QTest::mouseDClick(line, Qt::LeftButton, Qt::NoModifier, QPoint(x_of(125), h / 2));
	QCOMPARE(deck->state().eq.bands(), 4);
	QCOMPARE(deck->state().eq.band_level(0), SNURRA_EQ_KILL);
	QCOMPARE(deck->state().eq.level(3), SNURRA_EQ_KILL);   /* up to 250 Hz */
	/* The parts' own splits are not joined: at 250 Hz, still four. */
	QTest::mouseDClick(line, Qt::LeftButton, Qt::NoModifier, QPoint(x_of(250), h / 2));
	QCOMPARE(deck->state().eq.bands(), 4);
	/* The menu: twelve bands. */
	QTimer::singleShot(200, [] {
		QMenu *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
		if (!menu) return;
		for (QAction *action : menu->actions())
			if (action->text().startsWith(QLatin1String("12"))) action->trigger();
		menu->close();
	});
	QContextMenuEvent asked(QContextMenuEvent::Mouse, QPoint(w / 2, h / 2), line->mapToGlobal(QPoint(w / 2, h / 2)));
	QCoreApplication::sendEvent(line, &asked);
	QCOMPARE(deck->state().eq.bands(), 12);
	QCOMPARE(deck->state().eq.level(0), SNURRA_EQ_KILL);   /* each band its middle's height */

	/* Drawn into more bands: three, and a stroke inside the mid from
	 * 400 Hz on the middle to 2.5 kHz at +4 dB splits it at 1 kHz, each
	 * half as drawn; on inside the upper half, from 1.2 kHz at +4 to 3.5
	 * kHz at -8, it splits again at 2 kHz. Pulled straight up, a band
	 * does not split. */
	deck->set_eq(snurra_eq{});
	QCOMPARE(deck->state().eq.bands(), 3);
	auto y_of = [h](int step) { return int(std::lround(2.5 + (12 - step) / 24.0 * (h - 5))); };
	auto move_to = [line](QPoint to) {
		QMouseEvent moved(QEvent::MouseMove, to, line->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
		QCoreApplication::sendEvent(line, &moved);
	};
	QTest::mousePress(line, Qt::LeftButton, Qt::NoModifier, QPoint(x_of(600), y_of(0)));
	move_to(QPoint(x_of(600), y_of(8)));
	QTest::mouseRelease(line, Qt::LeftButton, Qt::NoModifier, QPoint(x_of(600), y_of(8)));
	QCOMPARE(deck->state().eq.bands(), 3);
	QCOMPARE(deck->state().eq.band_level(1), 4.0f);
	QCOMPARE(deck->state().eq.part_db(1), 4.0f);   /* three bands: the knobs */
	QTest::mousePress(line, Qt::LeftButton, Qt::NoModifier, QPoint(x_of(400), y_of(0)));
	move_to(QPoint(x_of(2500), y_of(8)));
	QCOMPARE(deck->state().eq.bands(), 4);
	QCOMPARE(deck->state().eq.band_level(1), 0.0f);   /* 250 Hz to 1 kHz */
	QCOMPARE(deck->state().eq.band_level(2), 4.0f);   /* 1 to 4 kHz */
	move_to(QPoint(x_of(1200), y_of(8)));
	move_to(QPoint(x_of(3500), y_of(-4)));
	QTest::mouseRelease(line, Qt::LeftButton, Qt::NoModifier, QPoint(x_of(3500), y_of(-4)));
	QCOMPARE(deck->state().eq.bands(), 5);
	QCOMPARE(deck->state().eq.band_level(2), 4.0f);   /* 1 to 2 kHz */
	QCOMPARE(deck->state().eq.band_level(3), -8.0f);  /* 2 to 4 kHz */
	QCOMPARE(deck->state().eq.band_level(0), 0.0f);   /* the low untouched */

	/* The mid knob -- a controller's -- moves the mid's three bands
	 * whole: killed, all three; back to 0, the shape as it was. */
	deck->set_eq(0, SNURRA_EQ_KILL, 0);
	for (int band = 1; band <= 3; ++band) QCOMPARE(deck->state().eq.band_level(band), SNURRA_EQ_KILL);
	QCOMPARE(deck->state().eq.band_level(0), 0.0f);
	deck->set_eq(0, 0, 0);
	QCOMPARE(deck->state().eq.bands(), 5);
	QCOMPARE(deck->state().eq.band_level(1), -4.0f);
	QCOMPARE(deck->state().eq.band_level(2), 0.0f);
	QCOMPARE(deck->state().eq.band_level(3), -12.0f);
	deck->set_eq(0, 2, 0);   /* up 2 dB: each band up 2 */
	QCOMPARE(deck->state().eq.band_level(1), -2.0f);
	QCOMPARE(deck->state().eq.band_level(3), -10.0f);
	deck->set_eq(0, 0, 0);

	/* Drawn back near 0 dB, neighbours join when let go: 2 to 4 kHz to
	 * 0 beside 1 to 2 kHz at 0, one band; then 250 Hz to 1 kHz to +0.5,
	 * and the mid is one band again, its knob the height. */
	QTest::mousePress(line, Qt::LeftButton, Qt::NoModifier, QPoint(x_of(3000), y_of(0)));
	QCOMPARE(deck->state().eq.bands(), 5);   /* held: not yet */
	QTest::mouseRelease(line, Qt::LeftButton, Qt::NoModifier, QPoint(x_of(3000), y_of(0)));
	QCOMPARE(deck->state().eq.bands(), 4);
	QCOMPARE(deck->state().eq.band_level(1), -4.0f);
	QTest::mousePress(line, Qt::LeftButton, Qt::NoModifier, QPoint(x_of(500), y_of(1)));
	QTest::mouseRelease(line, Qt::LeftButton, Qt::NoModifier, QPoint(x_of(500), y_of(1)));
	QCOMPARE(deck->state().eq.bands(), 3);
	QVERIFY(std::abs(deck->state().eq.part_db(1) - 0.25f) < 0.01f);
}

/*
 * The decks share the width alike (project.md sec 3f): on a phone's 412
 * wide, deck A loaded beside an empty deck B gets half, not what an empty
 * deck's words leave it -- seen on the API 36 emulator, a quarter, the EQ
 * line cut off after its low band.
 */
void embed_test::decks_share_a_phones_width() {
	snurra_dj dj;
	snurra_media media;
	media.path = QStringLiteral("tone:440");
	QVERIFY(dj.load(0, media));
	dj.resize(412, 892);
	dj.show();
	QVERIFY(QTest::qWaitForWindowExposed(&dj));
	QTest::qWait(100);
	QWidget *a = dj.findChild<QWidget *>(QStringLiteral("snurra-deck-a"));
	QWidget *b = dj.findChild<QWidget *>(QStringLiteral("snurra-deck-b"));
	QVERIFY(a && b);
	qInfo("deck A %d wide, deck B %d", a->width(), b->width());
	QVERIFY2(std::abs(a->width() - b->width()) <= 2, qPrintable(QStringLiteral("A %1, B %2").arg(a->width()).arg(b->width())));
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
	QVERIFY(first.loudness_known && kept.loudness_known);
	QVERIFY(std::abs(kept.loudness - first.loudness) < 0.01);
	QVERIFY(std::abs(kept.peak - first.peak) < 0.001);
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
	QCOMPARE(tile->deck()->trim(), 1.0f);   /* no loudness known: untouched */
	/* A tune mastered at -8 LUFS is cut 6 dB to -14 -- on by default, the
	 * key absent; with the match set off (as the test main sets it for the
	 * rest), left as it is. */
	{
		snurra_beat_grid loud = grid;
		loud.loudness_known = true;
		loud.loudness = -8;
		loud.peak = 1;
		QSettings settings(QStringLiteral("snurra"), QStringLiteral("snurra"));
		const QVariant was = settings.value(QStringLiteral("loudness/match"));
		settings.remove(QStringLiteral("loudness/match"));
		tile->set_beats(loud);
		QVERIFY(std::abs(20 * std::log10(tile->deck()->trim()) + 6) < 0.01);
		settings.setValue(QStringLiteral("loudness/match"), false);
		tile->set_beats(loud);
		QCOMPARE(tile->deck()->trim(), 1.0f);
		if (was.isValid()) settings.setValue(QStringLiteral("loudness/match"), was);
		tile->set_beats(grid);
	}
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
	QCOMPARE(back.eq.part_db(0), 0.0f);
	QTest::mouseDClick(knob, Qt::LeftButton);
	QCOMPARE(tile->state().key_shift, 0);
	QCOMPARE(said->text(), QStringLiteral("127.2 BPM 8A"));

	/* Turned as a mixer's knob on a screen: held, then up or down, a step
	 * every half line -- wherever the press began. A press at its left
	 * edge, where a dial's angle would read most of the way down, moves
	 * nothing; three steps up is 3, five down from there -2; after the
	 * release a move turns nothing. */
	const int step = std::max(4, knob->fontMetrics().height() / 2);
	const QPoint left(1, knob->height() / 2);
	QTest::mousePress(knob, Qt::LeftButton, Qt::NoModifier, left);
	QCOMPARE(tile->state().key_shift, 0);
	auto move_to = [knob](QPoint at) {
		QMouseEvent moved(QEvent::MouseMove, at, knob->mapToGlobal(at), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
		QCoreApplication::sendEvent(knob, &moved);
	};
	move_to(left - QPoint(0, 3 * step + 1));
	QCOMPARE(tile->state().key_shift, 3);
	move_to(left + QPoint(0, 2 * step + 1));
	QCOMPARE(tile->state().key_shift, -2);
	QTest::mouseRelease(knob, Qt::LeftButton, Qt::NoModifier, left + QPoint(0, 2 * step + 1));
	move_to(left - QPoint(0, 10 * step));
	QCOMPARE(tile->state().key_shift, -2);
	/* And it stops at its ends. */
	QTest::mousePress(knob, Qt::LeftButton, Qt::NoModifier, left);
	move_to(left - QPoint(0, 100 * step));
	QCOMPARE(tile->state().key_shift, 12);
	QTest::mouseRelease(knob, Qt::LeftButton, Qt::NoModifier, left - QPoint(0, 100 * step));
	QTest::mouseDClick(knob, Qt::LeftButton);
	QCOMPARE(tile->state().key_shift, 0);
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
 * A host's dark palette (hydra's, qtkit's, which BeerSSH and fuzzypickles
 * take): Mid darker than the window, as a style shades frames. The part
 * of the waveform still to come stands off the ground all the same --
 * drawn in Mid, it was 0.04 of luma from it, all but unseen.
 */
void embed_test::the_waveform_to_come_shows_on_a_hosts_dark_palette() {
	snurra_node node;
	QPalette dark;
	const QColor window(0x2b, 0x2b, 0x2e);
	dark.setColor(QPalette::Window, window);
	dark.setColor(QPalette::WindowText, QColor(0xe6, 0xe6, 0xe6));
	dark.setColor(QPalette::Text, QColor(0xe6, 0xe6, 0xe6));
	dark.setColor(QPalette::Base, QColor(0x1f, 0x1f, 0x22));
	dark.setColor(QPalette::Highlight, QColor(0x3d, 0x7e, 0xb8));
	dark.setColor(QPalette::Mid, window.darker(130));
	/* As a host gives it: the whole application's. */
	const QPalette was = QApplication::palette();
	QApplication::setPalette(dark);
	auto restore = qScopeGuard([was] { QApplication::setPalette(was); });
	snurra_tile_state tone = placed(QStringLiteral("tone:220"), 0, 0);
	tone.playing = false;
	snurra_tile *tile = node.add_tile(tone);
	node.resize(600, 200);
	node.show();
	QVERIFY(QTest::qWaitForWindowExposed(&node));
	QWidget *waveform = waveform_of(tile);
	QVERIFY(waveform);
	auto luma = [](const QColor &c) { return 0.2126 * c.redF() + 0.7152 * c.greenF() + 0.0722 * c.blueF(); };
	auto seen = [&] {
		const QImage drawn = waveform->grab().toImage();
		int count = 0;
		for (int y = 0; y < drawn.height(); ++y)
			for (int x = 0; x < drawn.width(); ++x)
				if (luma(drawn.pixelColor(x, y)) - luma(window) > 0.12) ++count;
		/* More than the playhead's one column. */
		return count > 4 * drawn.height();
	};
	QTRY_VERIFY_WITH_TIMEOUT(seen(), 3000);
}

/*
 * About (the holder, 2026-11-23): in the window's right-click menu, as
 * the player sets it -- its version and copyright, the address escaped --
 * and not there in a host's window, which has its own.
 */
void embed_test::about_is_in_the_right_click_menu() {
	snurra_node node;
	snurra_tile *tile = node.add_tile(placed(QStringLiteral("tone:220"), 0, 0));
	node.resize(600, 200);
	node.show();
	QVERIFY(QTest::qWaitForWindowExposed(&node));
	auto offered = [&]() -> QString {
		QString said;
		QTimer::singleShot(200, [&said] {
			QMenu *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
			if (!menu) return;
			for (QAction *action : menu->actions())
				if (action->objectName() == QLatin1String("snurra-about")) said = action->text();
			menu->close();
		});
		const QPoint over = tile->geometry().center();
		QContextMenuEvent asked(QContextMenuEvent::Mouse, over, node.mapToGlobal(over));
		QCoreApplication::sendEvent(&node, &asked);
		QTest::qWait(300);
		return said;
	};
	QVERIFY2(offered().isEmpty(), "a host's window offers snurra's About");
	snurra_set_about(QStringLiteral("<p>snurra 9</p><p>%1</p>")
	                         .arg(QStringLiteral("Copyright (C) 2026 Nabeel Sowan <nabeel@vibes.se>").toHtmlEscaped()));
	auto unset = qScopeGuard([] { snurra_set_about(QString()); });
	QCOMPARE(offered(), QStringLiteral("About snurra"));

	/* Chosen, the box says whose, the address shown rather than dropped. */
	QString shown;
	QTimer::singleShot(200, [&shown] {
		QMenu *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
		if (!menu) return;
		for (QAction *action : menu->actions())
			if (action->objectName() == QLatin1String("snurra-about")) {
				QTimer::singleShot(300, [&shown] {
					if (QMessageBox *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
						QTextDocument text;
						text.setHtml(box->text());
						shown = text.toPlainText();
						box->accept();
					}
				});
				action->trigger();
			}
		menu->close();
	});
	const QPoint over = tile->geometry().center();
	QContextMenuEvent asked(QContextMenuEvent::Mouse, over, node.mapToGlobal(over));
	QCoreApplication::sendEvent(&node, &asked);
	QTRY_VERIFY_WITH_TIMEOUT(!shown.isEmpty(), 3000);
	QVERIFY2(shown.contains(QLatin1String("Copyright (C) 2026 Nabeel Sowan <nabeel@vibes.se>")), qPrintable(shown));
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
	QCOMPARE(SNURRA_EMBED_VERSION, 6);   /* 2: subtitles; 3: snurra_dj; 4: position, state, ended; 5: four decks, the wire; 6: a nested track's socket */
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
