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
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include "../src/clock.h"
#include "../src/deck.h"
#include "../src/source.h"

namespace {

constexpr int RATE = 48000;
constexpr int PERIOD = 480;

/* Bytes in memory, arriving in whatever ranges the test says. */
class memory_bytes : public snurra_bytes {
public:
	explicit memory_bytes(QByteArray data) : m_data(std::move(data)) {}

	int64_t size() const override { return m_data.size(); }

	int64_t read(int64_t offset, uint8_t *out, int64_t n) override {
		if (offset >= m_data.size()) return -1;
		std::lock_guard<std::mutex> lock(m_mutex);
		for (const auto &range : m_here) {
			if (offset >= range.first && offset < range.second) {
				const int64_t count = std::min(n, range.second - offset);
				std::memcpy(out, m_data.constData() + offset, size_t(count));
				return count;
			}
		}
		return 0;
	}

	bool has(int64_t offset, int64_t n) const override {
		std::lock_guard<std::mutex> lock(m_mutex);
		for (const auto &range : m_here)
			if (offset >= range.first && offset + n <= range.second) return true;
		return false;
	}

	void want(int64_t offset) override {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_wanted.push_back(offset);
	}

	void wait(int ms) override { std::this_thread::sleep_for(std::chrono::milliseconds(std::min(ms, 2))); }

	/* [from, to) is here now. Ranges are kept apart; reads look in each. */
	void arrive(int64_t from, int64_t to) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_here.push_back({ from, std::min<int64_t>(to, m_data.size()) });
	}

	std::vector<int64_t> wanted() const {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_wanted;
	}

private:
	QByteArray m_data;
	mutable std::mutex m_mutex;
	std::vector<std::pair<int64_t, int64_t>> m_here;
	std::vector<int64_t> m_wanted;
};

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

/* A WAV whose every frame is different from its neighbours, so a sample
 * compared says which frame it is. */
QByteArray make_wav(int frames) {
	QByteArray pcm(frames * 4, Qt::Uninitialized);
	for (int64_t i = 0; i < frames; ++i) {
		/* 64-bit: in int these overflow past frame 271,000, and the
		 * optimiser's reading of that undefined loop wrote past the buffer. */
		const qint16 left = qint16((i * 7919) % 65536 - 32768);
		const qint16 right = qint16((i * 104729) % 65536 - 32768);
		std::memcpy(pcm.data() + 4 * i, &left, 2);
		std::memcpy(pcm.data() + 4 * i + 2, &right, 2);
	}
	QByteArray wav;
	auto u32 = [&wav](quint32 v) { wav.append(reinterpret_cast<const char *>(&v), 4); };
	auto u16 = [&wav](quint16 v) { wav.append(reinterpret_cast<const char *>(&v), 2); };
	wav += "RIFF";
	u32(quint32(36 + pcm.size()));
	wav += "WAVEfmt ";
	u32(16);
	u16(1);
	u16(2);
	u32(RATE);
	u32(RATE * 4);
	u16(4);
	u16(16);
	wav += "data";
	u32(quint32(pcm.size()));
	return wav + pcm;
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

int run_deck_tests(int argc, char **argv) {
	deck_test test;
	return QTest::qExec(&test, argc, argv);
}

#include "deck_test.moc"
