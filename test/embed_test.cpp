/*
 * A tile in a program of its own (project.md sec 1: BeerSSH's tile,
 * fuzzypickles' player), made the way a consumer makes one --
 * snurra_tile::embedded, an engine of the consumer's, bytes from its
 * transport -- and seen from outside, through the tile's own state: it
 * plays, holds where the bytes stop and goes on when they come, and closing
 * it leaves no deck behind in the engine, whichever goes first.
 */
#include <QSignalSpy>
#include <QToolButton>
#include <QtTest>

#include <memory>

#include "snurra/clock.h"
#include "snurra/engine.h"
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

int run_embed_tests(int argc, char **argv) {
	embed_test test;
	return QTest::qExec(&test, argc, argv);
}

#include "embed_test.moc"
