/*
 * Beat detection (project.md sec 3b, step 6): signals made here, whose every
 * beat is known to the frame, read back through snurra_find_beats -- the
 * tempo, and the grid's error at every true beat across a minute, which a
 * tempo off by a little turns into a drift that grows to the end. And
 * signals with no beat at all, which must be given none.
 */
#include <QtTest>

#include <cmath>
#include <random>
#include <vector>

#include "snurra/beats.h"
#include "snurra/source.h"

namespace {

constexpr int RATE = 48000;

/* Stereo frames held in memory, as a source. */
class memory_source : public snurra_source {
public:
	explicit memory_source(std::vector<float> mono) : m_mono(std::move(mono)) {}
	int read(float *out, int frames) override {
		const int n = int(std::min<int64_t>(frames, int64_t(m_mono.size()) - m_at));
		for (int i = 0; i < n; ++i) out[2 * i] = out[2 * i + 1] = m_mono[size_t(m_at + i)];
		m_at += std::max(0, n);
		return std::max(0, n);
	}
	bool at_end() const override { return m_at >= int64_t(m_mono.size()); }
	void seek(int64_t frame) override { m_at = std::clamp<int64_t>(frame, 0, int64_t(m_mono.size())); }
	int64_t position() const override { return m_at; }
	int64_t length() const override { return int64_t(m_mono.size()); }

private:
	std::vector<float> m_mono;
	int64_t m_at = 0;
};

/* A drum hit at `at` frames: a decaying 55 Hz thump and a click (kick), or
 * decaying noise (snare, or a short high hat). */
enum hit_t { KICK, SNARE, HAT };

void add_hit(std::vector<float> &out, double at, hit_t hit, std::mt19937 &noise) {
	std::uniform_real_distribution<float> white(-1, 1);
	const int length = hit == HAT ? RATE / 40 : RATE / 4;
	const int start = int(std::lround(at));
	float last = 0;
	for (int i = 0; i < length && start + i < int(out.size()); ++i) {
		if (start + i < 0) continue;
		const double t = double(i) / RATE;
		float v = 0;
		if (hit == KICK) v = float(0.7 * std::sin(2 * M_PI * 55 * t) * std::exp(-t / 0.09) + (i < 96 ? 0.3 * white(noise) : 0));
		if (hit == SNARE) v = float(0.4 * white(noise) * std::exp(-t / 0.07));
		if (hit == HAT) {
			const float n = white(noise);
			v = float(0.15 * (n - last) * std::exp(-t / 0.01));   /* differenced: high */
			last = n;
		}
		out[size_t(start + i)] += v;
	}
}

struct made_t {
	std::vector<float> mono;
	std::vector<double> beats;   /* every beat's frame */
};

/* `seconds` of a beat at `bpm`, the first at `first` seconds: a bare kick
 * on every beat, or kick, snare, kick, snare with eighth-note hats and a
 * held chord under it all. */
made_t beat(double bpm, double first, bool pattern, int seconds = 60) {
	made_t made;
	made.mono.assign(size_t(seconds) * RATE, 0.0f);
	std::mt19937 noise(7);
	const double period = 60.0 * RATE / bpm;
	if (pattern)
		for (size_t i = 0; i < made.mono.size(); ++i) {
			const double t = double(i) / RATE;
			made.mono[i] += float(0.08 * (std::sin(2 * M_PI * 220 * t) + std::sin(2 * M_PI * 277.2 * t) + std::sin(2 * M_PI * 329.6 * t)));
		}
	for (int k = 0;; ++k) {
		const double at = first * RATE + k * period;
		if (at >= double(made.mono.size())) break;
		made.beats.push_back(at);
		add_hit(made.mono, at, pattern && k % 2 ? SNARE : KICK, noise);
		if (pattern) {
			add_hit(made.mono, at, HAT, noise);
			add_hit(made.mono, at + period / 2, HAT, noise);
		}
	}
	return made;
}

}

class beats_test : public QObject {
	Q_OBJECT

private slots:
	void a_beat_is_found_to_the_frame_data();
	void a_beat_is_found_to_the_frame();
	void what_has_no_beat_is_given_none_data();
	void what_has_no_beat_is_given_none();
};

void beats_test::a_beat_is_found_to_the_frame_data() {
	QTest::addColumn<double>("bpm");
	QTest::addColumn<double>("first");
	QTest::addColumn<bool>("pattern");
	QTest::newRow("kick, 120") << 120.0 << 0.0 << false;
	QTest::newRow("pattern, 128") << 128.0 << 0.137 << true;
	QTest::newRow("pattern, 87.5") << 87.5 << 0.5 << true;
	QTest::newRow("pattern, 140") << 140.0 << 0.31 << true;
	QTest::newRow("pattern, 127.3") << 127.3 << 0.05 << true;
}

void beats_test::a_beat_is_found_to_the_frame() {
	QFETCH(double, bpm);
	QFETCH(double, first);
	QFETCH(bool, pattern);
	made_t made = beat(bpm, first, pattern);
	memory_source source(made.mono);
	const snurra_beat_grid grid = snurra_find_beats(source, RATE);
	QVERIFY2(grid.valid(), qPrintable(QStringLiteral("no beat found (confidence %1)").arg(grid.confidence)));
	/* Every true beat against the nearest grid line: over a minute, a
	 * tempo off by 0.01 BPM at 128 drifts by 5 ms. */
	double worst = 0;
	double mean = 0;
	for (double at : made.beats) {
		const double k = std::round((at - grid.first) / grid.period);
		const double error = (grid.first + k * grid.period - at) / RATE * 1000;
		worst = std::max(worst, std::abs(error));
		mean += error;
	}
	mean /= double(made.beats.size());
	qInfo("%.2f BPM found as %.4f (confidence %.1f); every beat within %.2f ms, %.2f ms late on average",
	      bpm, grid.bpm(), grid.confidence, worst, mean);
	QVERIFY2(std::abs(grid.bpm() - bpm) < 0.02, qPrintable(QStringLiteral("%1 BPM, not %2").arg(grid.bpm()).arg(bpm)));
	QVERIFY2(worst < 10, qPrintable(QStringLiteral("a beat %1 ms off the grid").arg(worst)));
}

void beats_test::what_has_no_beat_is_given_none_data() {
	QTest::addColumn<int>("kind");
	QTest::newRow("white noise") << 0;
	QTest::newRow("a steady tone") << 1;
	QTest::newRow("hits at random times") << 2;
}

void beats_test::what_has_no_beat_is_given_none() {
	QFETCH(int, kind);
	std::vector<float> mono(size_t(60) * RATE, 0.0f);
	std::mt19937 noise(11);
	std::uniform_real_distribution<float> white(-0.3f, 0.3f);
	if (kind == 0)
		for (float &v : mono) v = white(noise);
	if (kind == 1)
		for (size_t i = 0; i < mono.size(); ++i) mono[i] = float(0.3 * std::sin(2 * M_PI * 440 * double(i) / RATE));
	if (kind == 2) {
		std::exponential_distribution<double> gap(2.0);   /* two a second, at random */
		for (double at = 0; at < double(mono.size()); at += gap(noise) * RATE) add_hit(mono, at, KICK, noise);
	}
	memory_source source(mono);
	const snurra_beat_grid grid = snurra_find_beats(source, RATE);
	qInfo("confidence %.2f", grid.confidence);
	QVERIFY2(!grid.valid(), qPrintable(QStringLiteral("a beat at %1 BPM was found").arg(grid.bpm())));
}

int run_beats_tests(int argc, char **argv) {
	beats_test test;
	return QTest::qExec(&test, argc, argv);
}

#include "beats_test.moc"
