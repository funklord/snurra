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
/* A cut's crossover, the deck's TAIL: 5 ms. */
constexpr size_t deck_test_tail() { return 240; }
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

/* Each block where the last left off, and what it played is the file. After
 * a seek made while sounding, the first `crossover` frames out are a cut's
 * crossover -- the old place's tail falling, the new place rising -- so
 * their samples are not the file's, and may sound while the new place
 * waits for its bytes; where each block begins is checked all the same. */
QString check_continuous(const std::vector<rendered_t> &blocks, const std::vector<float> &reference, int64_t from,
                         int64_t crossover = 0) {
	int64_t next = from;
	int64_t out = 0;   /* frames out so far */
	for (const rendered_t &block : blocks) {
		if (block.advanced == 0) {
			for (size_t i = 0; i < block.samples.size(); ++i)
				if (block.samples[i] != 0 && out + int64_t(i / 2) >= crossover)
					return QStringLiteral("sound while not moving, at frame %1").arg(block.first);
			if (block.first != next) return QStringLiteral("held at %1, not %2").arg(block.first).arg(next);
			out += int64_t(block.samples.size() / 2);
			continue;
		}
		if (block.first != next) return QStringLiteral("went on at frame %1, not %2").arg(block.first).arg(next);
		for (int64_t i = 0; i < block.advanced; ++i) {
			const size_t frame = size_t(block.first + i);
			if (2 * frame + 1 >= reference.size()) return QStringLiteral("played past the end");
			if (out + i < crossover) continue;
			if (block.samples[2 * size_t(i)] != reference[2 * frame]
			    || block.samples[2 * size_t(i) + 1] != reference[2 * frame + 1])
				return QStringLiteral("frame %1 is not the file's").arg(frame);
		}
		out += int64_t(block.samples.size() / 2);
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
	void a_seek_just_after_a_pause_is_heard_as_nothing();
	void a_tempo_without_key_lock_reads_between_the_frames();
	void key_lock_keeps_the_pitch_as_the_tempo_moves();
	void the_eq_cuts_its_band_and_leaves_the_rest();
	void a_nudge_bends_without_a_gap();
	void a_jump_while_playing_has_no_gap();
	void an_anchored_deck_keeps_step_at_its_tempo();
	void an_envelope_is_heard_on_the_tracks_own_frames();
	void an_equal_power_fade_keeps_its_quarter_circle();
	void the_waveform_shows_where_it_is_loud();
	void a_waveform_closed_while_reading_stops_at_once();
	void a_waveform_from_bytes_fills_as_far_as_they_are_here();

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
	const QString fault = check_continuous(after, reference, target, int64_t(deck_test_tail()));
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
	const QString fault = check_continuous(after, reference, target, int64_t(deck_test_tail()));
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

void deck_test::a_seek_just_after_a_pause_is_heard_as_nothing() {
	/*
	 * A stack's lanes are paused and then sought together, in less than a
	 * period (project.md sec 3b, step 4). The 5 ms slew down to silence
	 * belongs to the old position: the new one must neither sound nor move
	 * while paused, or each lane holds a different frame.
	 */
	snurra_deck::start_t start = start_for(nullptr, QStringLiteral("snurra-test:ramp"));
	start.fallback_length = RATE * 10;
	snurra_deck deck(start);
	int64_t present = snurra_clock_us();
	play(deck, present, 2000, [](const std::vector<rendered_t> &b) { return advanced_by(b) > RATE / 10; });
	deck.set_playing(false);
	deck.seek(RATE);
	const std::vector<rendered_t> blocks = play(deck, present, 50, [](const std::vector<rendered_t> &) { return false; });
	float loudest = 0;
	for (const rendered_t &block : blocks)
		for (float sample : block.samples) loudest = std::max(loudest, std::abs(sample));
	QCOMPARE(loudest, 0.0f);
	QCOMPARE(blocks.back().first, int64_t(RATE));
}

void deck_test::an_envelope_is_heard_on_the_tracks_own_frames() {
	/*
	 * An envelope (project.md sec 3b, step 5) down from 1 at frame 0 to 0
	 * at frame 48000, then up to 1 at 96000. The ramp's right channel is 1,
	 * so the capture reads the gain on each frame the left names: every
	 * one on the line, from the start and again after a seek into the
	 * middle -- the curve goes with the track, not with the clock.
	 */
	snurra_deck::start_t start = start_for(nullptr, QStringLiteral("snurra-test:ramp"));
	start.fallback_length = RATE * 10;
	snurra_deck deck(start);
	deck.set_envelope({ { 0, 1.0f }, { RATE, 0.0f }, { 2 * RATE, 1.0f } });
	auto expected = [](double frame) {
		if (frame <= RATE) return 1 - frame / RATE;
		if (frame <= 2 * RATE) return (frame - RATE) / RATE;
		return 1.0;
	};
	int64_t present = snurra_clock_us();
	double worst = 0;
	int64_t checked = 0;
	auto check = [&](const std::vector<rendered_t> &blocks) {
		for (const rendered_t &block : blocks)
			for (int64_t i = 0; i < block.advanced; ++i) {
				const double gain = block.samples[size_t(2 * i + 1)];
				if (gain < 1e-3) continue;   /* the frame cannot be read back where it is silent */
				const double frame = double(block.samples[size_t(2 * i)]) / gain * SNURRA_RAMP_SCALE;
				worst = qMax(worst, qAbs(gain - expected(frame)));
				++checked;
			}
	};
	check(play(deck, present, 4000, [](const std::vector<rendered_t> &b) { return advanced_by(b) > RATE * 3 / 2; }));
	deck.seek(RATE / 2);
	const std::vector<rendered_t> after
	        = play(deck, present, 4000, [](const std::vector<rendered_t> &b) { return advanced_by(b) > RATE / 4; });
	check(after);
	int64_t first = -1;
	for (const rendered_t &block : after)
		if (block.advanced > 0 && first < 0) first = block.first;
	qInfo("envelope: %lld frames checked, off the line by %.2g at worst", (long long)checked, worst);
	QCOMPARE(first, int64_t(RATE / 2));
	QVERIFY2(checked > RATE * 3 / 2, "too few frames were read back");
	QVERIFY2(worst < 1e-5, qPrintable(QStringLiteral("a gain off its line by %1").arg(worst)));
}

/*
 * Varispeed (project.md sec 4): on the ramp, whose left channel names its
 * frame, every frame out at tempo 1.05 is 1.05 frames on from the last --
 * a line read between frames exactly -- and a change of tempo is heard in
 * the very next period.
 */
void deck_test::a_tempo_without_key_lock_reads_between_the_frames() {
	snurra_deck::start_t start = start_for(nullptr, QStringLiteral("snurra-test:ramp"));
	start.fallback_length = RATE * 10;
	snurra_deck deck(start);
	int64_t present = snurra_clock_us();
	play(deck, present, 2000, [](const std::vector<rendered_t> &b) { return advanced_by(b) > RATE / 10; });
	deck.set_tempo(1.05f);
	std::vector<double> frames;
	auto collect = [&frames](const std::vector<rendered_t> &blocks) {
		for (const rendered_t &block : blocks)
			for (int64_t i = 0; i < block.advanced; ++i)
				frames.push_back(double(block.samples[size_t(2 * i)]) / block.samples[size_t(2 * i + 1)] * SNURRA_RAMP_SCALE);
	};
	collect(play(deck, present, 2000, [](const std::vector<rendered_t> &b) { return advanced_by(b) > RATE; }));
	const size_t at_105 = frames.size();
	deck.set_tempo(0.9f);
	collect({ render(deck, present) });
	double worst = 0;
	for (size_t i = 1; i < frames.size(); ++i) {
		const double step = i < at_105 ? 1.05 : 0.9;
		if (i == at_105) continue;   /* the change itself, between the two periods */
		worst = std::max(worst, std::abs(frames[i] - frames[i - 1] - step));
	}
	qInfo("varispeed: %zu frames out, each the tempo on from the last to %.2g", frames.size(), worst);
	QVERIFY2(frames.size() > size_t(RATE), "too few frames out");
	/* The ramp's own resolution: near frame 50000 its sample, a float about
	 * 0.003, is resolved to 2^-32, which is 0.004 of a frame. */
	QVERIFY2(worst < 5e-3, qPrintable(QStringLiteral("a step off the tempo by %1 frames").arg(worst)));
}

/* Frequency from upward zero crossings, between the first and the last. */
static double frequency(const std::vector<float> &mono) {
	int64_t first = -1;
	int64_t last = -1;
	int crossings = 0;
	for (size_t i = 1; i < mono.size(); ++i)
		if (mono[i - 1] < 0 && mono[i] >= 0) {
			const double at = double(i - 1) + mono[i - 1] / double(mono[i - 1] - mono[i]);
			if (first < 0) first = int64_t(at);
			last = int64_t(at);
			++crossings;
		}
	return crossings > 1 ? (crossings - 1) * double(RATE) / double(last - first) : 0;
}

/*
 * A DJ's EQ (the holder's simplified Traktor): a tone's level through a
 * deck, in dB against the tone's own, for each band at -24 dB. Untouched,
 * the level is the tone's; a band cut takes its own frequencies down and
 * leaves the far ones as they were.
 */
void deck_test::the_eq_cuts_its_band_and_leaves_the_rest() {
	auto level = [this](int hz, float low, float mid, float high) {
		snurra_deck::start_t start = start_for(nullptr, QStringLiteral("tone:%1").arg(hz));
		start.fallback_length = RATE * 20;
		snurra_deck deck(start);
		deck.set_eq(low, mid, high);
		int64_t present = snurra_clock_us();
		play(deck, present, 2000, [](const std::vector<rendered_t> &b) { return advanced_by(b) > RATE / 5; });
		const std::vector<rendered_t> blocks = play(deck, present, 4000, [](const std::vector<rendered_t> &b) { return advanced_by(b) > RATE / 2; });
		double sum = 0;
		int64_t n = 0;
		for (const rendered_t &block : blocks)
			for (int64_t i = 0; i < block.advanced; ++i, ++n) sum += double(block.samples[size_t(2 * i)]) * block.samples[size_t(2 * i)];
		return 10 * std::log10(sum / double(n) / (0.2 * 0.2 / 2));
	};
	for (int hz : { 60, 1000, 8000 }) {
		const double flat = level(hz, 0, 0, 0);
		QVERIFY2(std::abs(flat) < 0.1, qPrintable(QStringLiteral("%1 Hz untouched at %2 dB").arg(hz).arg(flat)));
	}
	const double low_cut_low = level(60, -24, 0, 0);
	const double low_cut_high = level(8000, -24, 0, 0);
	const double high_cut_high = level(8000, 0, 0, -24);
	const double high_cut_low = level(60, 0, 0, -24);
	const double mid_cut_mid = level(1000, 0, -24, 0);
	qInfo("low cut: 60 Hz %.1f dB, 8 kHz %.1f dB; high cut: 8 kHz %.1f dB, 60 Hz %.1f dB; mid cut: 1 kHz %.1f dB",
	      low_cut_low, low_cut_high, high_cut_high, high_cut_low, mid_cut_mid);
	QVERIFY(low_cut_low < -18);
	QVERIFY(std::abs(low_cut_high) < 1);
	QVERIFY(high_cut_high < -18);
	QVERIFY(std::abs(high_cut_low) < 1);
	QVERIFY(std::abs(mid_cut_mid + 24) < 1);
}

/*
 * Key lock (project.md sec 4): a 440 Hz tone at tempo 1.2 stays 440 Hz --
 * varispeed, the control, makes it 528 -- while the track goes 1.2 times as
 * fast, and its level holds through every join of segments: no dip, no
 * doubling.
 */
void deck_test::key_lock_keeps_the_pitch_as_the_tempo_moves() {
	auto run = [this](bool key_lock, double &hz, double &speed, double &level_low, double &level_high) {
		snurra_deck::start_t start = start_for(nullptr, QStringLiteral("tone:440"));
		start.fallback_length = RATE * 20;
		snurra_deck deck(start);
		int64_t present = snurra_clock_us();
		play(deck, present, 2000, [](const std::vector<rendered_t> &b) { return advanced_by(b) > RATE / 10; });
		deck.set_key_lock(key_lock);
		deck.set_tempo(1.2f);
		render(deck, present);   /* in */
		int64_t from_frame = 0, from_us = 0, ignored = 0;
		deck.last_rendered(from_frame, from_us, ignored);
		std::vector<float> mono;
		const std::vector<rendered_t> blocks = play(deck, present, 4000, [](const std::vector<rendered_t> &b) { return advanced_by(b) > 2 * RATE; });
		for (const rendered_t &block : blocks)
			for (int64_t i = 0; i < block.advanced; ++i) mono.push_back(block.samples[size_t(2 * i)]);
		int64_t to_frame = 0, to_us = 0;
		deck.last_rendered(to_frame, to_us, ignored);
		hz = frequency(mono);
		speed = double(to_frame - from_frame) / double(mono.size() - size_t(blocks.back().advanced));
		/* The level in 10 ms windows, against the tone's own: 0.2 peak. */
		level_low = 1e9;
		level_high = 0;
		const size_t window = RATE / 100;
		for (size_t w = 0; w + window <= mono.size(); w += window) {
			double sum = 0;
			for (size_t i = w; i < w + window; ++i) sum += double(mono[i]) * mono[i];
			const double db = 10 * std::log10(sum / double(window) / (0.2 * 0.2 / 2));
			level_low = std::min(level_low, db);
			level_high = std::max(level_high, db);
		}
	};
	double hz = 0, speed = 0, low = 0, high = 0;
	run(false, hz, speed, low, high);
	qInfo("varispeed 1.2: %.2f Hz, the track at %.4f, level %.2f to %.2f dB", hz, speed, low, high);
	QVERIFY2(std::abs(hz - 528) < 1, qPrintable(QStringLiteral("varispeed at %1 Hz, not 528").arg(hz)));
	run(true, hz, speed, low, high);
	qInfo("key lock 1.2: %.2f Hz, the track at %.4f, level %.2f to %.2f dB", hz, speed, low, high);
	QVERIFY2(std::abs(hz - 440) < 1, qPrintable(QStringLiteral("key lock at %1 Hz, not 440").arg(hz)));
	QVERIFY2(std::abs(speed - 1.2) < 0.01, qPrintable(QStringLiteral("the track went at %1, not 1.2").arg(speed)));
	QVERIFY2(low > -1 && high < 1, qPrintable(QStringLiteral("the level went from %1 to %2 dB").arg(low).arg(high)));
}

/*
 * A nudge as a bend (project.md sec 4): 10 ms gained, playing, on the
 * ramp -- every frame out sounds (no gap), none steps more than a tenth
 * faster, and the track ends exactly 480 frames further on than the
 * frames out.
 */
void deck_test::a_nudge_bends_without_a_gap() {
	snurra_deck::start_t start = start_for(nullptr, QStringLiteral("snurra-test:ramp"));
	start.fallback_length = RATE * 10;
	snurra_deck deck(start);
	int64_t present = snurra_clock_us();
	play(deck, present, 2000, [](const std::vector<rendered_t> &b) { return advanced_by(b) > RATE / 10; });
	deck.bend(480);
	std::vector<double> frames;
	int silent = 0;
	const std::vector<rendered_t> blocks = play(deck, present, 2000, [](const std::vector<rendered_t> &b) { return advanced_by(b) > RATE / 2; });
	for (const rendered_t &block : blocks)
		for (int64_t i = 0; i < block.advanced; ++i) {
			const float gain = block.samples[size_t(2 * i + 1)];
			if (gain < 0.999f) {
				++silent;
				continue;
			}
			frames.push_back(double(block.samples[size_t(2 * i)]) / gain * SNURRA_RAMP_SCALE);
		}
	double fastest = 0;
	for (size_t i = 1; i < frames.size(); ++i) fastest = std::max(fastest, frames[i] - frames[i - 1]);
	const double gained = (frames.back() - frames.front()) - double(frames.size() - 1);
	qInfo("bend: %zu frames out, %d silent, fastest step %.4f, %.3f frames gained", frames.size(), silent, fastest, gained);
	QCOMPARE(silent, 0);
	QVERIFY2(fastest < 1.1 + 5e-3, qPrintable(QStringLiteral("a step of %1").arg(fastest)));
	QVERIFY2(std::abs(gained - 480) < 0.01, qPrintable(QStringLiteral("%1 frames gained, not 480").arg(gained)));
}

/*
 * A jump while playing (project.md sec 4), as a window makes one: the new
 * place from a moment 100 ms ahead. Until then the old place plays on,
 * frame after frame; from then, the new one, on its frames -- crossed over
 * 5 ms, the old place's next frames falling as the new rise, so the cut
 * is not a click: halfway, half of each. The ramp's right channel, the
 * summed gain, stays whole throughout: never a silent frame, never a dip.
 */
void deck_test::a_jump_while_playing_has_no_gap() {
	snurra_deck::start_t start = start_for(nullptr, QStringLiteral("snurra-test:ramp"));
	start.fallback_length = RATE * 10;
	snurra_deck deck(start);
	int64_t present = snurra_clock_us();
	play(deck, present, 2000, [](const std::vector<rendered_t> &b) { return advanced_by(b) > RATE / 10; });
	const int64_t target = RATE * 5;
	const int64_t at = present + 100000 + 7;
	deck.start_at(target, at);
	int64_t block_present = present;
	std::vector<double> left;
	std::vector<double> gain;
	std::vector<double> times;
	const std::vector<rendered_t> blocks = play(deck, present, 2000, [](const std::vector<rendered_t> &b) { return advanced_by(b) > RATE / 2; });
	for (const rendered_t &block : blocks) {
		for (int i = 0; i < PERIOD; ++i) {
			left.push_back(double(block.samples[size_t(2 * i)]) * SNURRA_RAMP_SCALE);
			gain.push_back(block.samples[size_t(2 * i + 1)]);
			times.push_back(double(block_present) + i * 1e6 / RATE);
		}
		block_present += snurra_frames_to_us(PERIOD, RATE);
	}
	/* The cut: read back from the new place's own frames, past any
	 * crossover -- frame target + k sounds k frames after it. */
	size_t settled = left.size() - 1;
	const size_t cut = settled - size_t(left[settled] - double(target));
	QVERIFY2(cut > 0 && cut < left.size(), "no cut");
	double worst_before = 0;
	for (size_t i = 1; i < cut; ++i) worst_before = std::max(worst_before, std::abs(left[i] - left[i - 1] - 1));
	const double old_next = left[cut - 1] + 1;
	double worst_gain = 0;
	for (double g : gain) worst_gain = std::max(worst_gain, std::abs(g - 1));
	/* The crossover's length is the old block's rest, 5 ms at most: find
	 * where the line is the new place's alone. */
	size_t alone = cut;
	while (alone < left.size() && std::abs(left[alone] - double(target + int64_t(alone - cut))) > 0.5) ++alone;
	const size_t length = alone - cut;
	double worst_after = 0;
	for (size_t i = alone; i < left.size(); ++i)
		worst_after = std::max(worst_after, std::abs(left[i] - double(target + int64_t(i - cut))));
	const size_t half = cut + length / 2;
	const double w = length > 0 ? double(half - cut) / double(length) : 1;
	const double expected_half = (1 - w) * (old_next + double(half - cut)) + w * double(target + int64_t(half - cut));
	qInfo("jump: cut %.1f us from its moment, a crossover of %zu frames, gain off by %.4f at worst, halfway %.1f against %.1f",
	      times[cut] - double(at), length, worst_gain, left[half], expected_half);
	QVERIFY2(worst_before < 1e-6, "the old place did not play on to the cut");
	QVERIFY2(std::abs(times[cut] - double(at)) <= 0.5e6 / RATE, "cut away from its moment");
	QVERIFY2(length > 0 && length <= deck_test_tail(), "no crossover, or one longer than 5 ms");
	QVERIFY2(worst_gain < 1e-3, "the summed gain dipped: a gap");
	QVERIFY2(worst_after < 1e-6, "after the crossover, not the new place's frames");
	QVERIFY2(length < 4 || std::abs(left[half] - expected_half) < 2, "no crossover: halfway is not half of each");
}

/*
 * A handover's taker of a tile at +5 % (project.md sec 4): anchored to the
 * giver's frame and moment, it must be where the giver is -- which moves
 * 1.05 frames a frame out -- not where a tempo of 1 would put it. On the
 * ramp: once sounding, the frame heard against the anchor's line, within
 * a frame.
 */
void deck_test::an_anchored_deck_keeps_step_at_its_tempo() {
	snurra_deck::start_t start = start_for(nullptr, QStringLiteral("snurra-test:ramp"));
	start.fallback_length = RATE * 20;
	int64_t present = snurra_clock_us();
	start.anchor_frame = RATE * 3;
	start.anchor_us = present;
	start.playing = true;
	start.anchored = true;
	start.lead_us = 100000;
	snurra_deck deck(start);
	deck.set_tempo(1.05f);
	const std::vector<rendered_t> blocks = play(deck, present, 4000, [](const std::vector<rendered_t> &b) { return advanced_by(b) > RATE / 2; });
	int64_t block_present = present - int64_t(blocks.size()) * snurra_frames_to_us(PERIOD, RATE);
	double worst = 0;
	int heard = 0;
	for (const rendered_t &block : blocks) {
		for (int i = 0; i < PERIOD; ++i) {
			const float gain = block.samples[size_t(2 * i + 1)];
			if (gain < 0.999f) continue;
			const double frame = double(block.samples[size_t(2 * i)]) / gain * SNURRA_RAMP_SCALE;
			const double when = double(block_present) + i * 1e6 / RATE;
			const double line = RATE * 3 + (when - double(start.anchor_us)) * RATE / 1e6 * 1.05;
			worst = std::max(worst, std::abs(frame - line));
			++heard;
		}
		block_present += snurra_frames_to_us(PERIOD, RATE);
	}
	qInfo("anchored at 1.05: %d frames heard, off the giver's line by %.2f frames at worst", heard, worst);
	QVERIFY2(heard > RATE / 4, "too little was heard");
	QVERIFY2(worst < 2, qPrintable(QStringLiteral("off the giver's line by %1 frames").arg(worst)));
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
	std::atomic<int> columns{ 0 };
	QElapsedTimer timer;
	{
		snurra_peaks reading(path, RATE, 1024,
		                     [&columns](int, const std::vector<float> &values) { columns += int(values.size()); });
		QTRY_VERIFY_WITH_TIMEOUT(columns.load() > 0, 5000);
		timer.start();
	}
	const qint64 took = timer.elapsed();
	QVERIFY2(took < 500, qPrintable(QStringLiteral("closing took %1 ms").arg(took)));
	QVERIFY2(columns.load() < 1024, "it read the whole file before stopping");
}

void deck_test::a_waveform_from_bytes_fills_as_far_as_they_are_here() {
	/*
	 * fuzzypickles' waveform (inbox 20261112T0430Z): from a second
	 * snurra_bytes of the consumer's. The same four seconds as above, eight
	 * columns, with the first half here: the first three columns come --
	 * the fourth ends only with the first frame not here -- and the rest
	 * wait; when the rest arrives, all eight, exactly a file's.
	 * Closed while waiting, it stops at once.
	 */
	const QByteArray wav = wav_of(RATE * 4, [](int i) {
		const int second = i / RATE;
		const int sign = (i / 50) % 2 ? 1 : -1;
		return second == 1 ? sign * 16384 : second == 2 ? sign * 8192 : 0;
	});
	auto bytes = std::make_shared<memory_bytes>(wav);
	bytes->arrive(0, 44 + int64_t(RATE) * 2 * 4);
	std::mutex lock;
	std::vector<float> peaks(8, -1.0f);
	auto piece = [&](int first, const std::vector<float> &values) {
		std::lock_guard<std::mutex> hold(lock);
		for (size_t i = 0; i < values.size(); ++i) peaks[size_t(first) + i] = values[i];
	};
	auto read = [&] {
		std::lock_guard<std::mutex> hold(lock);
		return peaks;
	};
	{
		snurra_peaks reading(bytes, QStringLiteral("loud.wav"), RATE, 8, piece);
		QTRY_VERIFY_WITH_TIMEOUT(read()[2] >= 0, 5000);
		QTest::qWait(300);
		const std::vector<float> half = read();
		QCOMPARE(std::vector<float>(half.begin(), half.begin() + 3), (std::vector<float>{ 0, 0, 0.5f }));
		QVERIFY2(half[3] < 0 && half[4] < 0, "a column was read past the bytes that are here");
		bytes->arrive(0, wav.size());
		QTRY_VERIFY_WITH_TIMEOUT(read()[7] >= 0, 5000);
		QCOMPARE(read(), (std::vector<float>{ 0, 0, 0.5f, 0.5f, 0.25f, 0.25f, 0, 0 }));
	}

	auto stalled = std::make_shared<memory_bytes>(wav);
	stalled->arrive(0, 44 + int64_t(RATE) * 4);
	QElapsedTimer timer;
	{
		snurra_peaks waiting(stalled, QStringLiteral("loud.wav"), RATE, 8, [](int, const std::vector<float> &) {});
		QTest::qWait(300);
		timer.start();
	}
	QVERIFY2(timer.elapsed() < 500, qPrintable(QStringLiteral("closing took %1 ms").arg(timer.elapsed())));
}

int run_deck_tests(int argc, char **argv) {
	deck_test test;
	return QTest::qExec(&test, argc, argv);
}

#include "deck_test.moc"
