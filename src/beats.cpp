#include "snurra/beats.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <memory>
#include <vector>

#include "snurra/source.h"

namespace {

constexpr int WINDOW = 1024;   /* frames an analysis looks at */
constexpr int HOP = 512;       /* and how far the next one is */
constexpr double SLOWEST = 70;
constexpr double FASTEST = 180;
constexpr double LIKELIEST = 120;   /* the prior's centre, BPM */
constexpr double PRIOR_WIDTH = 0.9; /* in octaves, one standard deviation */
/* A fold's peak over its mean, for a beat to count. Measured on made
 * signals: beats 17 to 24; white noise 1.4, a steady tone 2.4, hits at
 * random times 3.3 -- the best of many periods and phases searched, so
 * chance alone climbs above 1. */
constexpr double CLEAR = 6;
/* Onset strength a beat line, on average, for a beat to count. Measured:
 * made drums 1640 to 1770; a steady tone through AAC 6.3, MP3 4.8, Opus
 * 0.7; white noise 7. In log magnitude a rise is relative, so a quiet
 * track's beats score near a loud one's. */
constexpr double AUDIBLE = 50;
/* At half the tempo, the weaker of a beat's two lines under this share of
 * the stronger: the lines alternate strong and weak, and the strong ones
 * are the beat -- kick and snare on the quarters, hats alone between. */
constexpr double ALTERNATING = 0.5;

/* In place, radix 2; `data` a power of two long. */
void fft(std::vector<std::complex<float>> &data) {
	const size_t n = data.size();
	for (size_t i = 1, j = 0; i < n; ++i) {
		size_t bit = n >> 1;
		for (; j & bit; bit >>= 1) j ^= bit;
		j ^= bit;
		if (i < j) std::swap(data[i], data[j]);
	}
	for (size_t length = 2; length <= n; length <<= 1) {
		const double angle = -2 * M_PI / double(length);
		const std::complex<float> step(float(std::cos(angle)), float(std::sin(angle)));
		for (size_t i = 0; i < n; i += length) {
			std::complex<float> turn(1, 0);
			for (size_t k = 0; k < length / 2; ++k) {
				const std::complex<float> even = data[i + k];
				const std::complex<float> odd = data[i + k + length / 2] * turn;
				data[i + k] = even + odd;
				data[i + k + length / 2] = even - odd;
				turn *= step;
			}
		}
	}
}

/* Less the local mean (a third of a second each side), never below zero:
 * what stands out. */
std::vector<float> standing_out(const std::vector<float> &flux) {
	std::vector<float> strength(flux.size(), 0.0f);
	constexpr size_t AROUND = 16;
	std::vector<double> prefix(flux.size() + 1, 0.0);
	for (size_t i = 0; i < flux.size(); ++i) prefix[i + 1] = prefix[i] + flux[i];
	for (size_t i = 0; i < flux.size(); ++i) {
		const size_t low = i >= AROUND ? i - AROUND : 0;
		const size_t high = std::min(flux.size(), i + AROUND + 1);
		const double mean = (prefix[high] - prefix[low]) / double(high - low);
		strength[i] = float(std::max(0.0, double(flux[i]) - mean));
	}
	return strength;
}

/* Onset strength a hop: the summed rise of each bin's magnitude, made to
 * stand out. Hop n is centred on frame n * HOP + WINDOW / 2. `all` takes
 * every bin's log magnitude, for where onsets are, quiet ones too; `body`
 * the plain magnitude below 4 kHz, for which of them are strong -- in log
 * magnitude a quiet hat rises nearly as far as a kick (measured: hat-only
 * lines at 0.54 of the beats'), and has little below 4 kHz. */
struct onsets_t {
	std::vector<float> all;
	std::vector<float> body;
};

onsets_t onsets(snurra_source &source, int rate, const std::function<bool()> &stop) {
	const int body_bins = int(4000.0 * WINDOW / rate);
	std::vector<float> window(WINDOW);
	for (int i = 0; i < WINDOW; ++i) window[size_t(i)] = float(0.5 - 0.5 * std::cos(2 * M_PI * i / WINDOW));
	std::vector<float> mono;   /* the frames not yet looked at, from the last window's start */
	std::vector<float> block(2 * 4096);
	std::vector<std::complex<float>> spectrum(WINDOW);
	std::vector<float> previous(WINDOW / 2 + 1, 0.0f);
	std::vector<float> previous_linear(WINDOW / 2 + 1, 0.0f);
	std::vector<float> flux;
	std::vector<float> body;
	source.seek(0);
	bool first = true;
	for (;;) {
		if (stop && stop()) return {};
		const int n = source.read(block.data(), 4096);
		if (n <= 0) break;
		for (int i = 0; i < n; ++i) mono.push_back(0.5f * (block[2 * size_t(i)] + block[2 * size_t(i) + 1]));
		size_t at = 0;
		while (mono.size() - at >= size_t(WINDOW)) {
			for (int i = 0; i < WINDOW; ++i) spectrum[size_t(i)] = std::complex<float>(mono[at + size_t(i)] * window[size_t(i)], 0);
			fft(spectrum);
			float rise = 0;
			float body_rise = 0;
			for (int k = 1; k <= WINDOW / 2; ++k) {
				const float linear = std::abs(spectrum[size_t(k)]);
				const float magnitude = std::log1p(100.0f * linear);
				if (!first) {
					rise += std::max(0.0f, magnitude - previous[size_t(k)]);
					if (k < body_bins) body_rise += std::max(0.0f, linear - previous_linear[size_t(k)]);
				}
				previous[size_t(k)] = magnitude;
				previous_linear[size_t(k)] = linear;
			}
			flux.push_back(rise);
			body.push_back(body_rise);
			first = false;
			at += HOP;
		}
		mono.erase(mono.begin(), mono.begin() + std::ptrdiff_t(at));
	}
	return { standing_out(flux), standing_out(body) };
}

/* Every onset folded at `period` hops into bins a tenth of a hop wide,
 * smoothed over about a hop: the best phase in hops, its height, and the
 * fold's mean. */
struct fold_t {
	double phase = 0;
	double peak = 0;
	double mean = 0;
};

fold_t fold_at(const std::vector<float> &strength, double period, double phase);

fold_t fold(const std::vector<float> &strength, double period) {
	const int bins = std::max(8, int(std::ceil(period / 0.1)));
	std::vector<double> histogram(size_t(bins), 0.0);
	for (size_t n = 0; n < strength.size(); ++n) {
		if (strength[n] <= 0) continue;
		const double place = std::fmod(double(n), period) / period * bins;
		histogram[size_t(std::min(bins - 1, int(place)))] += strength[n];
	}
	/* A triangle a hop either side, round the circle. */
	const int reach = std::max(1, int(std::lround(bins / period)));
	std::vector<double> smooth(size_t(bins), 0.0);
	for (int b = 0; b < bins; ++b)
		for (int d = -reach; d <= reach; ++d)
			smooth[size_t(b)] += histogram[size_t(((b + d) % bins + bins) % bins)] * double(reach + 1 - std::abs(d));
	fold_t best;
	int top = 0;
	double total = 0;
	for (int b = 0; b < bins; ++b) {
		total += smooth[size_t(b)];
		if (smooth[size_t(b)] > smooth[size_t(top)]) top = b;
	}
	best.peak = smooth[size_t(top)];
	best.mean = total / bins;
	/* Between bins, by the parabola through the top and its neighbours. */
	const double left = smooth[size_t((top + bins - 1) % bins)];
	const double right = smooth[size_t((top + 1) % bins)];
	const double curve = left - 2 * best.peak + right;
	const double shift = curve < 0 ? 0.5 * (left - right) / curve : 0;
	best.phase = (top + 0.5 + shift) / bins * period;
	return best;
}

/* The fold's height at a given phase, smoothed as above. */
fold_t fold_at(const std::vector<float> &strength, double period, double phase) {
	fold_t at;
	at.phase = phase;
	double total = 0;
	for (size_t n = 0; n < strength.size(); ++n) {
		total += strength[n];
		double distance = std::abs(std::fmod(double(n) - phase, period));
		distance = std::min(distance, period - distance);
		if (distance <= 1) at.peak += strength[n] * (1 - distance);
	}
	at.mean = total;
	return at;
}

}

snurra_beat_grid snurra_find_beats(snurra_source &source, int rate, const std::function<bool()> &stop) {
	snurra_beat_grid grid;
	grid.rate = rate;
	const onsets_t found = onsets(source, rate, stop);
	const std::vector<float> &strength = found.all;
	const double hops_a_second = double(rate) / HOP;
	const int shortest = int(std::floor(hops_a_second * 60 / FASTEST));
	const int longest = int(std::ceil(hops_a_second * 60 / SLOWEST));
	if (strength.size() < size_t(longest) * 8) return grid;   /* fewer than eight beats: no tempo to speak of */

	/* The tempo, roughly: the autocorrelation's best lag, weighted toward
	 * LIKELIEST so that a tempo wins over its double or half. */
	double best_lag = 0;
	double best_score = 0;
	std::vector<double> correlation(size_t(longest) + 2, 0.0);
	for (int lag = shortest - 1; lag <= longest + 1; ++lag) {
		double sum = 0;
		for (size_t n = 0; n + size_t(lag) < strength.size(); ++n) sum += double(strength[n]) * strength[n + size_t(lag)];
		correlation[size_t(lag)] = sum / double(strength.size() - size_t(lag));
	}
	for (int lag = shortest; lag <= longest; ++lag) {
		const double left = correlation[size_t(lag - 1)];
		const double here = correlation[size_t(lag)];
		const double right = correlation[size_t(lag + 1)];
		if (here < left || here < right) continue;
		const double curve = left - 2 * here + right;
		const double exact = lag + (curve < 0 ? 0.5 * (left - right) / curve : 0);
		const double bpm = 60 * hops_a_second / exact;
		const double octaves = std::log2(bpm / LIKELIEST) / PRIOR_WIDTH;
		const double score = here * std::exp(-0.5 * octaves * octaves);
		if (score > best_score) {
			best_score = score;
			best_lag = exact;
		}
	}
	if (best_lag <= 0) return grid;

	/* Then exactly: the period, within 2% of it, whose fold of the whole
	 * track stands highest -- finer each pass, so that over hundreds of
	 * beats the grid does not drift off them. */
	double period = best_lag;
	double span = 0.02 * best_lag;
	fold_t chosen = fold(strength, period);
	for (int pass = 0; pass < 3; ++pass) {
		constexpr int STEPS = 80;
		const double from = period - span;
		for (int i = 0; i <= STEPS; ++i) {
			if (stop && stop()) return snurra_beat_grid{};
			const double candidate = from + 2 * span * i / STEPS;
			const fold_t tried = fold(strength, candidate);
			if (tried.peak > chosen.peak) {
				chosen = tried;
				period = candidate;
			}
		}
		span = 4 * span / STEPS;
		chosen = fold(strength, period);
	}
	/* A tempo or its half: folded at twice the period, are the beat's two
	 * lines equal, or one strong and one weak? */
	if (60 * hops_a_second / (2 * period) >= SLOWEST) {
		const fold_t slow = fold(strength, 2 * period);
		const double other = std::fmod(slow.phase + period, 2 * period);
		const double one = fold_at(found.body, 2 * period, slow.phase).peak;
		const double two = fold_at(found.body, 2 * period, other).peak;
		const double weaker = std::min(one, two);
		const double stronger = std::max(one, two);
		if (stronger > 0 && weaker / stronger < ALTERNATING) {
			period = 2 * period;
			chosen = slow;
		}
	}
	grid.confidence = chosen.mean > 0 ? chosen.peak / chosen.mean : 0;
	/* And loud enough to be a beat: the onset strength a line, on
	 * average. A codec's frames ripple a steady tone at their own period,
	 * which a fold finds as clearly as a drum (AAC 25, MP3 19) -- but
	 * faintly. */
	const double per_beat = fold_at(strength, period, chosen.phase).peak / (double(strength.size()) / period);
	if (grid.confidence < CLEAR || per_beat < AUDIBLE) return snurra_beat_grid{ 0, 0, grid.confidence, rate };
	grid.period = period * HOP;
	grid.first = std::fmod(chosen.phase * HOP + WINDOW / 2.0, grid.period);
	return grid;
}

snurra_beats::snurra_beats(const QString &source, int rate, std::function<void(const snurra_beat_grid &)> done) {
	m_thread = std::thread([this, source, rate, done] {
		std::unique_ptr<snurra_source> reader = snurra_open_source(source, rate, 0);
		if (!reader || reader->length() <= 0) return;
		const snurra_beat_grid grid = snurra_find_beats(*reader, rate, [this] { return m_stop.load(std::memory_order_relaxed); });
		if (!m_stop.load()) done(grid);
	});
}

snurra_beats::snurra_beats(std::shared_ptr<snurra_bytes> bytes, const QString &name, int rate,
                           std::function<void(const snurra_beat_grid &)> done) {
	m_thread = std::thread([this, bytes, name, rate, done] {
		auto stopped = [this] { return m_stop.load(std::memory_order_relaxed); };
		std::unique_ptr<snurra_source> reader = snurra_open_source(bytes, name, rate, stopped);
		if (!reader || reader->length() <= 0) return;
		const snurra_beat_grid grid = snurra_find_beats(*reader, rate, stopped);
		if (!m_stop.load()) done(grid);
	});
}

snurra_beats::~snurra_beats() {
	m_stop.store(true);
	if (m_thread.joinable()) m_thread.join();
}
