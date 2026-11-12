/*
 * A deck over bytes that may still be arriving (project.md sec 1, the
 * library's centre), driven the way an output drives it -- a block
 * rendered, then which frames went out read back -- over a transport whose
 * arrived ranges the test sets. fuzzypickles' stall contract, held to the
 * sample: where the bytes stop, the deck is silent and holds its place;
 * when they come it goes on from the very next frame; a seek into what is
 * not here waits there and asks for it; and what began before any bytes
 * starts at the beginning, not where the clock had got to.
 */
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include "snurra/clock.h"
#include "snurra/deck.h"
#include "snurra/peaks.h"
#include "snurra/source.h"
#include "test_bytes.h"

namespace {

constexpr int RATE = 48000;
constexpr int PERIOD = 480;


/* What one rendered block sent out: which frames, and the samples. */
struct rendered_t {
	int64_t first = 0;
	int64_t advanced = 0;
	std::vector<float> samples;
};

/* Plays `deck` a period, as an output would. */
rendered_t render(snurra_deck &deck, int64_t &present) {
	rendered_t block;
	block.samples.assign(2 * PERIOD, 0.0f);
	deck.render_add(block.samples.data(), PERIOD, present);
	int64_t at = 0;
	deck.last_rendered(block.first, at, block.advanced);
	present += snurra_frames_to_us(PERIOD, RATE);
	return block;
}


/* Every frame of `path`, decoded through snurra's own source. */
std::vector<float> decode_all(const QString &path) {
	std::vector<float> all;
	std::unique_ptr<snurra_source> source = snurra_open_source(path, RATE, 0);
	std::vector<float> block(2 * 4096);
	for (;;) {
		const int n = source->read(block.data(), 4096);
		if (n <= 0) break;
		all.insert(all.end(), block.begin(), block.begin() + 2 * n);
	}
	return all;
}

/* Renders until `done` says so, at most `limit` blocks, giving the reader
 * a moment between; returns what went out. */
template <typename stop_t>
std::vector<rendered_t> play(snurra_deck &deck, int64_t &present, int limit, stop_t done) {
	std::vector<rendered_t> blocks;
	for (int i = 0; i < limit; ++i) {
		blocks.push_back(render(deck, present));
		if (done(blocks)) break;
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	return blocks;
}

/* Each block where the last left off, and what it played is the file. */
QString check_continuous(const std::vector<rendered_t> &blocks, const std::vector<float> &reference, int64_t from) {
	int64_t next = from;
	for (const rendered_t &block : blocks) {
		if (block.advanced == 0) {
			for (float sample : block.samples)
				if (sample != 0) return QStringLiteral("sound while not moving, at frame %1").arg(block.first);
			if (block.first != next) return QStringLiteral("held at %1, not %2").arg(block.first).arg(next);
			continue;
		}
		if (block.first != next) return QStringLiteral("went on at frame %1, not %2").arg(block.first).arg(next);
		for (int64_t i = 0; i < block.advanced; ++i) {
			const size_t frame = size_t(block.first + i);
			if (2 * frame + 1 >= reference.size()) return QStringLiteral("played past the end");
			if (block.samples[2 * size_t(i)] != reference[2 * frame]
			    || block.samples[2 * size_t(i) + 1] != reference[2 * frame + 1])
				return QStringLiteral("frame %1 is not the file's").arg(frame);
		}
		next = block.first + block.advanced;
	}
	return QString();
}

int64_t advanced_by(const std::vector<rendered_t> &blocks) {
	int64_t total = 0;
	for (const rendered_t &block : blocks) total += block.advanced;
	return total;
}

}

class deck_test : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void a_new_deck_starts_where_it_was_asked();
	void formats_data();
	void where_the_bytes_stop_the_deck_holds_and_goes_on_from_the_next_frame_data() { formats_data(); }
	void where_the_bytes_stop_the_deck_holds_and_goes_on_from_the_next_frame();
	void a_seek_into_bytes_not_here_waits_there_and_asks_for_them_data() { formats_data(); }
	void a_seek_into_bytes_not_here_waits_there_and_asks_for_them();
	void a_seek_in_bytes_all_here_lands_as_in_a_file_data() { formats_data(); }
	void a_seek_in_bytes_all_here_lands_as_in_a_file();
	void a_play_before_any_bytes_begins_at_the_beginning_data() { formats_data(); }
	void a_play_before_any_bytes_begins_at_the_beginning();
	void a_deck_started_at_a_time_sounds_its_frame_then();
	void an_equal_power_fade_keeps_its_quarter_circle();
	void the_waveform_shows_where_it_is_loud();
	void a_waveform_closed_while_reading_stops_at_once();

private:
	snurra_deck::start_t start_for(std::shared_ptr<snurra_bytes> bytes, const QString &name) const;

	QTemporaryDir m_dir;
	bool m_ffmpeg = false;
};

void deck_test::initTestCase() {
	QVERIFY(m_dir.isValid());
	QFile wav(m_dir.filePath(QStringLiteral("deck.wav")));
	QVERIFY(wav.open(QIODevice::WriteOnly));
	wav.write(make_wav(RATE * 6));
	wav.close();
	const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
	if (ffmpeg.isEmpty()) return;
	m_ffmpeg = true;
	for (const QStringList &codec : { QStringList{ "flac", "flac" }, QStringList{ "mp3", "libmp3lame" } }) {
		QProcess encode;
		encode.start(ffmpeg, { "-nostdin", "-hide_banner", "-loglevel", "error", "-y", "-i", wav.fileName(), "-c:a",
		                       codec.at(1), m_dir.filePath(QStringLiteral("deck.") + codec.at(0)) });
		QVERIFY(encode.waitForFinished(30000) && encode.exitCode() == 0);
	}
}

void deck_test::formats_data() {
	QTest::addColumn<QString>("extension");
	QTest::newRow("wav") << QStringLiteral("wav");
	QTest::newRow("flac") << QStringLiteral("flac");
	QTest::newRow("mp3") << QStringLiteral("mp3");
}

snurra_deck::start_t deck_test::start_for(std::shared_ptr<snurra_bytes> bytes, const QString &name) const {
	snurra_deck::start_t start;
	start.source = name;
	start.bytes = std::move(bytes);
	start.rate = RATE;
	start.anchor_frame = 0;
	start.anchor_us = snurra_clock_us();
	start.playing = true;
	start.lead_us = 50000;
	return start;
}

void deck_test::a_new_deck_starts_where_it_was_asked() {
	/*
	 * A lone tile opened to play: its first frame heard is frame 0. Every
	 * playing deck was once anchored to the clock, so it skipped what
	 * passed while it opened -- the first lead's worth, 80 ms on ALSA.
	 */
	snurra_deck::start_t start = start_for(nullptr, QStringLiteral("snurra-test:ramp"));
	start.fallback_length = RATE * 10;
	snurra_deck deck(start);
	int64_t present = snurra_clock_us();
	const std::vector<rendered_t> blocks
	        = play(deck, present, 2000, [](const std::vector<rendered_t> &b) { return b.back().advanced > 0; });
	QVERIFY2(blocks.back().advanced > 0, "the deck never played");
	QCOMPARE(blocks.back().first, int64_t(0));
	QCOMPARE(double(blocks.back().samples[0]) * SNURRA_RAMP_SCALE, 0.0);
}

void deck_test::where_the_bytes_stop_the_deck_holds_and_goes_on_from_the_next_frame() {
	QFETCH(QString, extension);
	const QString path = m_dir.filePath(QStringLiteral("deck.") + extension);
	if (!QFile::exists(path)) QSKIP("no ffmpeg to make this format with");
	QFile file(path);
	QVERIFY(file.open(QIODevice::ReadOnly));
	auto bytes = std::make_shared<memory_bytes>(file.readAll());
	const std::vector<float> reference = decode_all(path);
	bytes->arrive(0, bytes->size() * 4 / 10);

	snurra_deck deck(start_for(bytes, QStringLiteral("deck.") + extension));
	int64_t present = snurra_clock_us();
	/* Until it has been still for 30 blocks: the bytes ran out. */
	std::vector<rendered_t> blocks = play(deck, present, 4000, [](const std::vector<rendered_t> &b) {
		if (b.size() < 30) return false;
		for (size_t i = b.size() - 30; i < b.size(); ++i)
			if (b[i].advanced != 0) return false;
		return advanced_by(b) > 0;
	});
	const int64_t held = blocks.back().first;
	QVERIFY2(held > 0 && 2 * size_t(held) < reference.size(),
	         qPrintable(QStringLiteral("held at frame %1 of %2").arg(held).arg(reference.size() / 2)));
	QVERIFY2(!deck.ended(), "a stall taken for the end");

	bytes->arrive(0, bytes->size());
	const int64_t before = advanced_by(blocks);
	std::vector<rendered_t> more = play(deck, present, 4000, [before](const std::vector<rendered_t> &b) {
		return advanced_by(b) > RATE * 2 || (before + advanced_by(b) > 0 && b.back().advanced == 0 && b.size() > 1000);
	});
	blocks.insert(blocks.end(), more.begin(), more.end());
	QVERIFY2(advanced_by(more) > RATE, "it did not go on once the bytes came");
	const QString fault = check_continuous(blocks, reference, 0);
	QVERIFY2(fault.isEmpty(), qPrintable(fault));
	qInfo("%s: held at frame %lld until the rest came, then went on from the next", qPrintable(extension),
	      (long long)held);
}

void deck_test::a_seek_into_bytes_not_here_waits_there_and_asks_for_them() {
	QFETCH(QString, extension);
	const QString path = m_dir.filePath(QStringLiteral("deck.") + extension);
	if (!QFile::exists(path)) QSKIP("no ffmpeg to make this format with");
	QFile file(path);
	QVERIFY(file.open(QIODevice::ReadOnly));
	auto bytes = std::make_shared<memory_bytes>(file.readAll());
	const std::vector<float> reference = decode_all(path);
	const int64_t here = bytes->size() / 5;
	bytes->arrive(0, here);

	snurra_deck deck(start_for(bytes, QStringLiteral("deck.") + extension));
	int64_t present = snurra_clock_us();
	play(deck, present, 2000, [](const std::vector<rendered_t> &b) { return advanced_by(b) > RATE / 4; });

	const int64_t target = int64_t(reference.size() / 2) * 7 / 10 + 13;   /* between packets */
	deck.seek(target);
	std::vector<rendered_t> waiting = play(deck, present, 200, [](const std::vector<rendered_t> &) { return false; });
	for (const rendered_t &block : waiting) {
		QCOMPARE(block.advanced, int64_t(0));
		QCOMPARE(block.first, target);
	}
	/*
	 * Asked for where it now needs bytes: the last offset told to the
	 * transport is at or past the edge of what is here. Not necessarily
	 * the target -- an exact MP3 seek reads forward from the last frame it
	 * knows (a CBR stream's byte positions are not exact), so it needs, and
	 * asks for, the bytes from that edge on.
	 */
	const std::vector<int64_t> wanted = bytes->wanted();
	const bool asked = !wanted.empty() && wanted.back() >= here;
	QStringList offsets;
	for (int64_t offset : wanted) offsets << QString::number(offset);
	qInfo("%s: here to %lld of %lld; asked for %s", qPrintable(extension), (long long)here, (long long)bytes->size(),
	      qPrintable(offsets.join(QLatin1Char(' '))));
	QVERIFY2(asked, "the reader never asked for the bytes the seek needs");

	bytes->arrive(0, bytes->size());
	std::vector<rendered_t> after
	        = play(deck, present, 4000, [](const std::vector<rendered_t> &b) { return advanced_by(b) > RATE; });
	QVERIFY2(advanced_by(after) > 0, "it did not play once the bytes came");
	const QString fault = check_continuous(after, reference, target);
	QVERIFY2(fault.isEmpty(), qPrintable(fault));
}

void deck_test::a_seek_in_bytes_all_here_lands_as_in_a_file() {
	/* The transport is not the timing: bytes all here seek as the file. */
	QFETCH(QString, extension);
	const QString path = m_dir.filePath(QStringLiteral("deck.") + extension);
	if (!QFile::exists(path)) QSKIP("no ffmpeg to make this format with");
	QFile file(path);
	QVERIFY(file.open(QIODevice::ReadOnly));
	auto bytes = std::make_shared<memory_bytes>(file.readAll());
	bytes->arrive(0, bytes->size());
	const std::vector<float> reference = decode_all(path);

	snurra_deck deck(start_for(bytes, QStringLiteral("deck.") + extension));
	int64_t present = snurra_clock_us();
	play(deck, present, 2000, [](const std::vector<rendered_t> &b) { return advanced_by(b) > RATE / 4; });
	const int64_t target = int64_t(reference.size() / 2) * 7 / 10 + 13;
	deck.seek(target);
	std::vector<rendered_t> after
	        = play(deck, present, 4000, [](const std::vector<rendered_t> &b) { return advanced_by(b) > RATE; });
	const QString fault = check_continuous(after, reference, target);
	QVERIFY2(fault.isEmpty(), qPrintable(fault));
}

void deck_test::a_play_before_any_bytes_begins_at_the_beginning() {
	QFETCH(QString, extension);
	const QString path = m_dir.filePath(QStringLiteral("deck.") + extension);
	if (!QFile::exists(path)) QSKIP("no ffmpeg to make this format with");
	QFile file(path);
	QVERIFY(file.open(QIODevice::ReadOnly));
	auto bytes = std::make_shared<memory_bytes>(file.readAll());
	const std::vector<float> reference = decode_all(path);

	snurra_deck deck(start_for(bytes, QStringLiteral("deck.") + extension));
	int64_t present = snurra_clock_us();
	std::vector<rendered_t> blocks = play(deck, present, 100, [](const std::vector<rendered_t> &) { return false; });
	QCOMPARE(advanced_by(blocks), int64_t(0));
	bytes->arrive(0, bytes->size());
	std::vector<rendered_t> more
	        = play(deck, present, 4000, [](const std::vector<rendered_t> &b) { return advanced_by(b) > RATE; });
	blocks.insert(blocks.end(), more.begin(), more.end());
	const QString fault = check_continuous(blocks, reference, 0);
	QVERIFY2(fault.isEmpty(), qPrintable(fault));
}

void deck_test::a_deck_started_at_a_time_sounds_its_frame_then() {
	/*
	 * A train's next tile starts on the very frame and moment the one
	 * before it reaches its overlap. From the ramp: silent before the
	 * moment, frame 0 at it -- to half a frame -- and every frame after.
	 */
	snurra_deck::start_t start = start_for(nullptr, QStringLiteral("snurra-test:ramp"));
	start.fallback_length = RATE * 10;
	snurra_deck deck(start);
	int64_t present = snurra_clock_us();
	const int64_t at = present + 150000 + 7;   /* not on a block, nor a frame */
	deck.start_at(0, at);
	const std::vector<rendered_t> blocks
	        = play(deck, present, 2000, [](const std::vector<rendered_t> &b) { return advanced_by(b) > RATE / 2; });
	int64_t block_present = blocks.empty() ? 0 : present - int64_t(blocks.size()) * snurra_frames_to_us(PERIOD, RATE);
	bool sounding = false;
	double expected = 0;
	for (const rendered_t &block : blocks) {
		for (int i = 0; i < PERIOD; ++i) {
			const double when = double(block_present) + i * 1e6 / RATE;
			const float gain = block.samples[2 * size_t(i) + 1];
			const double frame = double(block.samples[2 * size_t(i)]) * SNURRA_RAMP_SCALE;
			if (!sounding) {
				if (gain == 0) {
					QVERIFY2(when < double(at) + 1e6 / RATE,
					         qPrintable(QStringLiteral("silent %1 us after its moment").arg(when - double(at))));
					continue;
				}
				sounding = true;
				QVERIFY2(qAbs(when - double(at)) <= 0.5e6 / RATE,
				         qPrintable(QStringLiteral("began %1 us from its moment").arg(when - double(at))));
				QCOMPARE(frame, 0.0);
				expected = 0;
			}
			QCOMPARE(frame, expected);
			expected += 1;
		}
		block_present += snurra_frames_to_us(PERIOD, RATE);
	}
	QVERIFY2(sounding, "it never began");
}

void deck_test::an_equal_power_fade_keeps_its_quarter_circle() {
	/* Falling as cos and rising as sin, so two different tracks crossed
	 * keep their summed power: from the ramp, the gain is its right. */
	snurra_deck::start_t start = start_for(nullptr, QStringLiteral("snurra-test:ramp"));
	start.fallback_length = RATE * 10;
	snurra_deck deck(start);
	int64_t present = snurra_clock_us();
	const int64_t from = present + 100000;
	const int64_t length = 200000;
	deck.fade(from, length, 1, 0, true);
	const std::vector<rendered_t> blocks
	        = play(deck, present, 2000, [](const std::vector<rendered_t> &b) { return advanced_by(b) > RATE / 2; });
	int64_t block_present = present - int64_t(blocks.size()) * snurra_frames_to_us(PERIOD, RATE);
	double worst = 0;
	int inside = 0;
	for (const rendered_t &block : blocks) {
		for (int64_t i = 0; i < block.advanced; ++i) {
			const double when = double(block_present) + double(i) * 1e6 / RATE;
			const double t = qBound(0.0, (when - double(from)) / double(length), 1.0);
			worst = qMax(worst, qAbs(double(block.samples[2 * size_t(i) + 1]) - std::cos(t * M_PI_2)));
			if (t > 0 && t < 1) ++inside;
		}
		block_present += snurra_frames_to_us(PERIOD, RATE);
	}
	QVERIFY2(inside > RATE / 10, "too little of the fade was played");
	QVERIFY2(worst < 1e-4, qPrintable(QStringLiteral("off the quarter circle by %1").arg(worst)));
}

/* A WAV whose sample at `frame` is `level(frame)`, 16-bit, both channels. */
template <typename level_t>
static QByteArray wav_of(int frames, level_t level) {
	QByteArray pcm(frames * 4, Qt::Uninitialized);
	for (int i = 0; i < frames; ++i) {
		const qint16 v = qint16(level(i));
		std::memcpy(pcm.data() + 4 * i, &v, 2);
		std::memcpy(pcm.data() + 4 * i + 2, &v, 2);
	}
	QByteArray wav = make_wav(0);
	const quint32 riff = quint32(36 + pcm.size());
	const quint32 data = quint32(pcm.size());
	std::memcpy(wav.data() + 4, &riff, 4);
	std::memcpy(wav.data() + 40, &data, 4);
	return wav + pcm;
}

void deck_test::the_waveform_shows_where_it_is_loud() {
	/*
	 * Silent, half scale, quarter scale, silent -- a second each, read as
	 * eight columns: exactly those, in order, in pieces that cover them.
	 */
	const QString path = m_dir.filePath(QStringLiteral("loud.wav"));
	QFile file(path);
	QVERIFY(file.open(QIODevice::WriteOnly));
	file.write(wav_of(RATE * 4, [](int i) {
		const int second = i / RATE;
		const int sign = (i / 50) % 2 ? 1 : -1;
		return second == 1 ? sign * 16384 : second == 2 ? sign * 8192 : 0;
	}));
	file.close();
	std::unique_ptr<snurra_source> source = snurra_open_source(path, RATE, 0);
	QCOMPARE(source->length(), int64_t(RATE * 4));
	int next = 0;
	const std::vector<float> peaks
	        = snurra_peaks_of(*source, source->length(), 8, [&next](int first, const std::vector<float> &values) {
		          if (first != next) return false;
		          next += int(values.size());
		          return true;
	          });
	QCOMPARE(next, 8);
	const std::vector<float> expected{ 0, 0, 0.5f, 0.5f, 0.25f, 0.25f, 0, 0 };
	QCOMPARE(peaks, expected);

	/* A piece that says stop stops it. */
	std::unique_ptr<snurra_source> again = snurra_open_source(path, RATE, 0);
	const std::vector<float> stopped
	        = snurra_peaks_of(*again, again->length(), 1024, [](int, const std::vector<float> &) { return false; });
	QVERIFY2(stopped.size() < 1024, "a piece's stop was not heeded");
}

void deck_test::a_waveform_closed_while_reading_stops_at_once() {
	/* A tile closed while its waveform is read must not wait for the
	 * whole file: sixty seconds, closed at once. */
	const QString path = m_dir.filePath(QStringLiteral("long.wav"));
	QFile file(path);
	QVERIFY(file.open(QIODevice::WriteOnly));
	file.write(wav_of(RATE * 60, [](int i) { return (i % 100) * 300 - 15000; }));
	file.close();
	std::atomic<int> pieces{ 0 };
	QElapsedTimer timer;
	{
		snurra_peaks reading(path, RATE, 1024, [&pieces](int, const std::vector<float> &) { ++pieces; });
		QTRY_VERIFY_WITH_TIMEOUT(pieces.load() > 0, 5000);
		timer.start();
	}
	const qint64 took = timer.elapsed();
	QVERIFY2(took < 500, qPrintable(QStringLiteral("closing took %1 ms").arg(took)));
	QVERIFY2(pieces.load() < 16, "it read the whole file before stopping");
}

int run_deck_tests(int argc, char **argv) {
	deck_test test;
	return QTest::qExec(&test, argc, argv);
}

#include "deck_test.moc"
