/*
 * How each codec keeps time (project.md sec 5, the holder 2026-11-12):
 * every file is to sound at the same frame for the same moment, whatever
 * it was encoded with and at whatever rate, and land on the same frame
 * after a seek as when played through -- a handover's taker seeks while
 * its giver played on, and a train lines files up end to end.
 *
 * A reference is written with chirps at known times, encoded with the
 * ffmpeg program into each codec at 48 and 44.1 kHz, and decoded through
 * snurra's own source at 48 kHz. Each chirp is found by correlation, to a
 * fraction of a frame; a chirp rather than a tone because its correlation
 * has one peak, where a tone's has one per period and a lossy codec can
 * tip it onto the wrong one. What is checked, per file:
 *
 *  - played from the start, every chirp lands where it was written:
 *    encoder delay and padding (MP3's, AAC's priming, Opus's pre-skip)
 *    and resampler delay all show here, as a constant offset;
 *  - after a seek, the same chirps land on the same frames as played
 *    through;
 *  - the length the source reports is the length it plays.
 *
 * snurra is a DJ program, so the bar is the sample (the holder,
 * 2026-11-12: "our seeking api needs to be very precise"): a quarter of a
 * frame, which is the correlation's own noise on a lossy codec, not a
 * tolerance for timing. A raw MP2 or AC3 stream carries no word of its
 * encoder's delay, so it plays that much late in every player; there the
 * offset is recorded, and must be the same at every chirp -- no drift --
 * while seeking must still agree with playing through to the sample.
 */
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>
#include <memory>
#include <vector>

#include "../src/source.h"

namespace {

constexpr int RATE = 48000;           /* the engine's */
constexpr double SECONDS = 6.0;
constexpr double FIRST_CHIRP = 0.1;   /* seconds */
constexpr double CHIRP_EVERY = 0.25;
constexpr double CHIRP_LONG = 0.01;
constexpr double CHIRP_FROM_HZ = 500;
constexpr double CHIRP_TO_HZ = 4000;

/* A Hann-windowed linear chirp starting at t = 0, at time `t` seconds. */
double chirp(double t) {
	if (t < 0 || t >= CHIRP_LONG) return 0;
	const double window = 0.5 - 0.5 * std::cos(2 * M_PI * t / CHIRP_LONG);
	const double sweep = (CHIRP_TO_HZ - CHIRP_FROM_HZ) / CHIRP_LONG;
	return window * std::sin(2 * M_PI * (CHIRP_FROM_HZ * t + 0.5 * sweep * t * t));
}

int chirp_count() {
	return int((SECONDS - FIRST_CHIRP - 0.2) / CHIRP_EVERY) + 1;
}

double chirp_at(int k) {
	return FIRST_CHIRP + k * CHIRP_EVERY;
}

/* The reference, as a float WAV at `rate`: so nothing is lost before the
 * codec under test. */
bool write_reference(const QString &path, int rate) {
	const int frames = int(SECONDS * rate);
	std::vector<float> samples(2 * size_t(frames));
	for (int i = 0; i < frames; ++i) {
		const double t = double(i) / rate;
		double v = 0;
		for (int k = 0; k < chirp_count(); ++k) v += chirp(t - chirp_at(k));
		samples[2 * size_t(i)] = samples[2 * size_t(i) + 1] = float(0.5 * v);
	}
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) return false;
	const quint32 bytes = quint32(samples.size() * 4);
	auto u32 = [&file](quint32 v) { file.write(reinterpret_cast<const char *>(&v), 4); };
	auto u16 = [&file](quint16 v) { file.write(reinterpret_cast<const char *>(&v), 2); };
	file.write("RIFF");
	u32(36 + bytes);
	file.write("WAVEfmt ");
	u32(16);
	u16(3);   /* IEEE float */
	u16(2);
	u32(quint32(rate));
	u32(quint32(rate) * 8);
	u16(8);
	u16(32);
	file.write("data");
	u32(bytes);
	file.write(reinterpret_cast<const char *>(samples.data()), bytes);
	return true;
}

/* The left channel, decoded through snurra's source from `from`. */
std::vector<float> decode(snurra_source &source, int64_t from, int64_t frames) {
	source.seek(from);
	std::vector<float> left;
	std::vector<float> block(2 * 4096);
	while (int64_t(left.size()) < frames) {
		const int n = source.read(block.data(), 4096);
		if (n <= 0) break;
		for (int i = 0; i < n; ++i) left.push_back(block[2 * size_t(i)]);
	}
	if (int64_t(left.size()) > frames) left.resize(size_t(frames));
	return left;
}

/*
 * Where chirp `k` is in `left` (whose first sample is frame `first`), in
 * frames at the engine's rate, to a fraction: the correlation's peak
 * within 20 ms of where it should be, refined by a parabola through it
 * and its neighbours. NaN when it is not in the window.
 */
double find_chirp(const std::vector<float> &left, int64_t first, int k) {
	static std::vector<double> shape;
	if (shape.empty())
		for (int i = 0; i < int(CHIRP_LONG * RATE); ++i) shape.push_back(chirp(double(i) / RATE));
	const double expected = chirp_at(k) * RATE;
	const int64_t lo = int64_t(expected) - RATE / 50 - first;
	const int64_t hi = int64_t(expected) + RATE / 50 - first;
	if (lo < 1 || hi + int64_t(shape.size()) + 1 >= int64_t(left.size())) return NAN;
	std::vector<double> score(size_t(hi - lo + 1));
	for (int64_t at = lo; at <= hi; ++at) {
		double sum = 0;
		for (size_t i = 0; i < shape.size(); ++i) sum += shape[i] * left[size_t(at) + i];
		score[size_t(at - lo)] = sum;
	}
	size_t best = 0;
	for (size_t i = 1; i < score.size(); ++i)
		if (score[i] > score[best]) best = i;
	double fraction = 0;
	if (best > 0 && best + 1 < score.size()) {
		const double a = score[best - 1], b = score[best], c = score[best + 1];
		const double bend = a - 2 * b + c;
		if (bend != 0) fraction = 0.5 * (a - c) / bend;
	}
	return double(lo + int64_t(best) + first) + fraction;
}

struct codec_t {
	const char *name;
	const char *extension;
	QStringList arguments;
	bool timed;   /* carries its priming and padding, so plays as written */
	/* How far a sample after a seek may differ from played through: 0,
	 * except where the decoder dithers from a state a seek cannot rebuild
	 * -- AC3 by its spec, FFmpeg's MPEG audio decoder in rounding to 16
	 * bits. Two 16-bit steps, -90 dB; eight times the pre-roll changed
	 * nothing, so it is not the pre-roll. */
	double sample_tolerance = 0;
};

/* A quarter of a frame at 48 kHz: 5 microseconds. */
constexpr double PRECISION = 0.25;

}

class codec_test : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void every_codec_keeps_time_data();
	void every_codec_keeps_time();

private:
	QString m_ffmpeg;
	QTemporaryDir m_dir;
};

void codec_test::initTestCase() {
	m_ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
	if (m_ffmpeg.isEmpty()) QSKIP("no ffmpeg program to encode the fixtures with");
	QVERIFY(m_dir.isValid());
	for (int rate : { 48000, 44100 })
		QVERIFY(write_reference(m_dir.filePath(QStringLiteral("reference-%1.wav").arg(rate)), rate));
}

void codec_test::every_codec_keeps_time_data() {
	QTest::addColumn<QString>("codec");
	QTest::addColumn<QString>("extension");
	QTest::addColumn<QStringList>("arguments");
	QTest::addColumn<int>("rate");
	QTest::addColumn<bool>("timed");
	QTest::addColumn<double>("sample_tolerance");

	const QList<codec_t> codecs{
		{ "wav-s16", "wav", { "-c:a", "pcm_s16le" }, true },
		{ "flac", "flac", { "-c:a", "flac" }, true },
		{ "alac", "m4a", { "-c:a", "alac" }, true },
		{ "mp3", "mp3", { "-c:a", "libmp3lame", "-b:a", "192k" }, true },   /* the LAME tag */
		{ "aac", "m4a", { "-c:a", "aac", "-b:a", "192k" }, true },          /* the edit list */
		{ "opus", "opus", { "-c:a", "libopus", "-b:a", "128k" }, true },    /* pre-skip, granules */
		{ "vorbis", "ogg", { "-c:a", "libvorbis", "-q:a", "6" }, true },
		{ "mp2", "mp2", { "-c:a", "mp2", "-b:a", "256k" }, false, 2.0 / 32768 },   /* raw: no word of either */
		{ "ac3", "ac3", { "-c:a", "ac3", "-b:a", "256k" }, false, 2.0 / 32768 },
	};
	for (const codec_t &codec : codecs) {
		for (int rate : { 48000, 44100 }) {
			QTest::newRow(qPrintable(QStringLiteral("%1@%2").arg(QLatin1String(codec.name)).arg(rate)))
			        << QString::fromLatin1(codec.name) << QString::fromLatin1(codec.extension) << codec.arguments
			        << rate << codec.timed << codec.sample_tolerance;
		}
	}
}

void codec_test::every_codec_keeps_time() {
	QFETCH(QString, codec);
	QFETCH(QString, extension);
	QFETCH(QStringList, arguments);
	QFETCH(int, rate);
	QFETCH(bool, timed);
	QFETCH(double, sample_tolerance);

	const QString reference = m_dir.filePath(QStringLiteral("reference-%1.wav").arg(rate));
	const QString encoded = m_dir.filePath(QStringLiteral("%1-%2.%3").arg(codec).arg(rate).arg(extension));
	QProcess ffmpeg;
	ffmpeg.setProcessChannelMode(QProcess::ForwardedErrorChannel);
	ffmpeg.start(m_ffmpeg, QStringList{ "-nostdin", "-hide_banner", "-loglevel", "error", "-y", "-i", reference }
	                               + arguments + QStringList{ encoded });
	QVERIFY(ffmpeg.waitForFinished(30000));
	QCOMPARE(ffmpeg.exitCode(), 0);

	std::unique_ptr<snurra_source> source = snurra_open_source(encoded, RATE, 0);
	QVERIFY2(source->length() > 0, "the file did not open, or says no length");

	/* Played through from the start. */
	const std::vector<float> through = decode(*source, 0, int64_t((SECONDS + 1) * RATE));
	std::vector<double> offsets;
	double worst = 0;
	double lowest = 1e9;
	double highest = -1e9;
	for (int k = 0; k < chirp_count(); ++k) {
		const double found = find_chirp(through, 0, k);
		QVERIFY2(!std::isnan(found), qPrintable(QStringLiteral("chirp %1 not found played through").arg(k)));
		offsets.push_back(found - chirp_at(k) * RATE);
		if (std::abs(offsets.back()) > std::abs(worst)) worst = offsets.back();
		lowest = std::min(lowest, offsets.back());
		highest = std::max(highest, offsets.back());
	}

	/* After seeks to frames that fall between packets, chirps included --
	 * and every sample after one the same as played through: a seek that
	 * lands in the right place with the wrong sound is a glitch on a cue. */
	double worst_seek = 0;
	double worst_sample = 0;
	for (double at : { 1.013, 2.4567, 3.30001, 4.77 }) {
		const int64_t from = int64_t(at * RATE);
		const std::vector<float> after = decode(*source, from, RATE / 2);
		for (size_t i = 0; i < after.size() && size_t(from) + i < through.size(); ++i)
			worst_sample = std::max(worst_sample, double(std::abs(after[i] - through[size_t(from) + i])));
		for (int k = 0; k < chirp_count(); ++k) {
			const double found = find_chirp(after, from, k);
			if (std::isnan(found)) continue;
			const double apart = found - (chirp_at(k) * RATE + offsets[size_t(k)]);
			if (std::abs(apart) > std::abs(worst_seek)) worst_seek = apart;
		}
	}

	const double expected_length = SECONDS * RATE;
	qInfo("%-12s played through: %+.2f frames at worst; after a seek: %+.2f from played through, "
	      "samples apart by %.2g; length says %lld, plays %zu, written %.0f",
	      qPrintable(QStringLiteral("%1@%2").arg(codec).arg(rate)), worst, worst_seek, worst_sample,
	      (long long)source->length(), through.size(), expected_length);

	if (timed)
		QVERIFY2(std::abs(worst) <= PRECISION,
		         qPrintable(QStringLiteral("played through, a chirp lands %1 frames from where it was written")
		                            .arg(worst, 0, 'f', 2)));
	QVERIFY2(highest - lowest <= PRECISION,
	         qPrintable(QStringLiteral("played through, the offset drifts by %1 frames").arg(highest - lowest, 0, 'f', 2)));
	QVERIFY2(std::abs(worst_seek) <= PRECISION,
	         qPrintable(QStringLiteral("after a seek, a chirp lands %1 frames from where it plays through")
	                            .arg(worst_seek, 0, 'f', 2)));
	QVERIFY2(worst_sample <= sample_tolerance,
	         qPrintable(QStringLiteral("after a seek, a sample differs from played through by %1").arg(worst_sample)));
	QVERIFY2(qAbs(source->length() - int64_t(through.size())) <= 1,
	         qPrintable(QStringLiteral("the length says %1 frames and %2 play").arg(source->length()).arg(through.size())));
	if (timed)
		QVERIFY2(qAbs(int64_t(through.size()) - int64_t(expected_length)) <= 1,
		         qPrintable(QStringLiteral("%1 frames play of %2 written").arg(through.size()).arg(expected_length)));
}

int run_codec_tests(int argc, char **argv) {
	codec_test test;
	return QTest::qExec(&test, argc, argv);
}

#include "codec_test.moc"
