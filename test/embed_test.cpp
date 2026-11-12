/*
 * A tile in a program of its own (project.md sec 1: BeerSSH's tile,
 * fuzzypickles' player), made the way a consumer makes one --
 * snurra_tile::embedded, an engine of the consumer's, bytes from its
 * transport -- and seen from outside, through the tile's own state: it
 * plays, holds where the bytes stop and goes on when they come, and closing
 * it leaves no deck behind in the engine, whichever goes first.
 */
#include <QSignalSpy>
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

int run_embed_tests(int argc, char **argv) {
	embed_test test;
	return QTest::qExec(&test, argc, argv);
}

#include "embed_test.moc"
