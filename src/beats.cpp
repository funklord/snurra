#include "snurra/beats.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

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
/* The correlation with a key's profile under which no key is given. */
constexpr double KEY_FIT = 0.5;

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
	double notes[12] = {};   /* chroma_t's, C first */
};

/*
 * The key's evidence, read in the same pass: how strongly each of the
 * twelve notes sounds over the track, a chromagram. Blocks of KEY_WINDOW
 * frames end to end, each bin from G2 to C7 counted to the note nearest
 * it, a block weighed by its own loudest note -- so a loud chorus does
 * not outvote a quiet verse -- and silence left out.
 */
constexpr int KEY_WINDOW = 8192;   /* 5.9 Hz a bin at 48 kHz: notes a bin apart from about 100 Hz */

class chroma_t {
public:
	/* Notes up to C7 need no more than 12 kHz: at 32 kHz and over every
	 * fourth frame is kept, after a low pass at 3 kHz, so the FFT is a
	 * quarter the size for the same bins. Whole, it doubled the time a
	 * tune's analysis took -- five one-minute signals, 3.0 to 4.0 s without
	 * the chromagram, 6.3 to 7.0 s with it; so, 5.0 s. */
	explicit chroma_t(int rate) : m_every(rate >= 32000 ? 4 : rate >= 16000 ? 2 : 1), m_size(KEY_WINDOW / m_every) {
		m_spectrum.resize(size_t(m_size));
		m_window.resize(size_t(m_size));
		for (int i = 0; i < m_size; ++i) m_window[size_t(i)] = float(0.5 - 0.5 * std::cos(2 * M_PI * i / m_size));
		const double kept_rate = double(rate) / m_every;
		for (int k = 1; k < m_size / 2; ++k) {
			const double hz = double(k) * kept_rate / m_size;
			/* G2 to C7: a kick's 40 to 60 Hz, leaking into C2's bins, made a
			 * bare drum loop read as Db minor. */
			if (hz < 98 || hz > 2093) continue;
			const long midi = std::lround(69 + 12 * std::log2(hz / 440));
			m_bins.push_back({ k, int(midi % 12) });
		}
		/* A fourth-order Butterworth, two sections, at 3 kHz: what would
		 * fold onto the notes, from 9.9 kHz up at 48 kHz, is 40 dB down. */
		if (m_every > 1) {
			const double q[2] = { 1 / (2 * std::cos(M_PI / 8)), 1 / (2 * std::cos(3 * M_PI / 8)) };
			for (int i = 0; i < 2; ++i) {
				const double w = 2 * M_PI * 3000 / rate;
				const double alpha = std::sin(w) / (2 * q[i]);
				const double a0 = 1 + alpha;
				m_low[i] = { float((1 - std::cos(w)) / 2 / a0), float((1 - std::cos(w)) / a0), float((1 - std::cos(w)) / 2 / a0),
					     float(-2 * std::cos(w) / a0), float((1 - alpha) / a0) };
			}
		}
		m_block.reserve(size_t(m_size));
	}
	void add(float frame) {
		if (m_every > 1) {
			for (section_t &low : m_low) frame = low.run(frame);
			if (++m_skipped < m_every) return;
			m_skipped = 0;
		}
		m_block.push_back(frame);
		if (int(m_block.size()) < m_size) return;
		for (int i = 0; i < m_size; ++i) m_spectrum[size_t(i)] = std::complex<float>(m_block[size_t(i)] * m_window[size_t(i)], 0);
		m_block.clear();
		fft(m_spectrum);
		double block[12] = {};
		for (const bin_t &bin : m_bins) block[bin.note] += std::abs(m_spectrum[size_t(bin.k)]);
		const double loudest = *std::max_element(block, block + 12);
		if (loudest < 1e-3 * m_size / KEY_WINDOW) return;   /* silence, or near it */
		for (int note = 0; note < 12; ++note) notes[note] += block[note] / loudest;
	}
	double notes[12] = {};

private:
	struct bin_t {
		int k;
		int note;
	};
	struct section_t {
		float b0, b1, b2, a1, a2;
		float z1 = 0, z2 = 0;
		float run(float x) {
			const float y = b0 * x + z1;
			z1 = b1 * x - a1 * y + z2;
			z2 = b2 * x - a2 * y;
			return y;
		}
	};
	const int m_every;
	const int m_size;
	int m_skipped = 0;
	section_t m_low[2] = {};
	std::vector<bin_t> m_bins;
	std::vector<float> m_block;
	std::vector<std::complex<float>> m_spectrum;
	std::vector<float> m_window;
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
	chroma_t chroma(rate);
	source.seek(0);
	bool first = true;
	for (;;) {
		if (stop && stop()) return {};
		const int n = source.read(block.data(), 4096);
		if (n <= 0) break;
		for (int i = 0; i < n; ++i) {
			mono.push_back(0.5f * (block[2 * size_t(i)] + block[2 * size_t(i) + 1]));
			chroma.add(mono.back());
		}
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
	onsets_t found{ standing_out(flux), standing_out(body) };
	std::copy(chroma.notes, chroma.notes + 12, found.notes);
	return found;
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

/* The grid one run of onsets holds, at one tempo and phase; none with its
 * confidence where it is not clear. In hops: period and phase. */
static snurra_beat_grid grid_of(const onsets_t &found, int rate, const std::function<bool()> &stop) {
	snurra_beat_grid grid;
	grid.rate = rate;
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
	/* A beat or a dotted one: the autocorrelation can take a pulse every
	 * beat and a half for the beat -- 88 for 132, 92 for 138 in the
	 * holder's tracks measured against Mixxx (2026-11-13). Folded at two
	 * thirds of the period, a true beat puts a kick on its line every
	 * cycle, where at the period the kicks fall on three lines in turn,
	 * one each other cycle; so the stronger a cycle, in the body, is the
	 * beat. */
	if (60 * hops_a_second / (2 * period / 3) <= FASTEST) {
		auto per_cycle = [&found](double at) {
			const fold_t folded = fold_at(found.body, at, fold(found.body, at).phase);
			return folded.peak / (double(found.body.size()) / at);
		};
		const double faster = 2 * period / 3;
		if (per_cycle(faster) > per_cycle(period)) {
			period = faster;
			double span_fast = 0.005 * faster;
			chosen = fold(strength, period);
			for (int pass = 0; pass < 3; ++pass) {
				constexpr int STEPS = 80;
				const double from = period - span_fast;
				for (int i = 0; i <= STEPS; ++i) {
					const double candidate = from + 2 * span_fast * i / STEPS;
					const fold_t tried = fold(strength, candidate);
					if (tried.peak > chosen.peak) {
						chosen = tried;
						period = candidate;
					}
				}
				span_fast = 4 * span_fast / STEPS;
				chosen = fold(strength, period);
			}
		}
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

/*
 * A DJ set is one tempo with its phase moving at each track's join: the
 * whole of it folded smears to nothing (the holder's 42 minute set,
 * 2026-11-13: confidence 3.7 where 6 is clear, while each of its minutes
 * stood at 7.6 to 10.9, all 129 BPM). So a long track whose whole is not
 * clear is judged a minute at a time, at up to twelve places across it,
 * and where half of those minutes -- three at least -- agree on the tempo
 * within 1 %, the grid is that tempo, the middle of theirs, in the phase
 * of the first of them: Mixxx's constant grid for a set, right in tempo
 * throughout and in phase from that minute on until the next join moves
 * it.
 */
/*
 * The key whose profile the notes match best: Krumhansl and Kessler's
 * probe-tone ratings, how well each note of the scale is heard to fit a
 * major and a minor key, correlated with the notes at all 24 rotations.
 * None where the best fit is weak -- noise, a drum loop -- since a key
 * shown is a key mixed by.
 */
static int key_of(const double notes[12], double &fit) {
	static const double MAJOR[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
	static const double MINOR[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
	auto correlation = [](const double *profile, const double *heard, int tonic) {
		double profile_mean = 0, heard_mean = 0;
		for (int i = 0; i < 12; ++i) {
			profile_mean += profile[i] / 12;
			heard_mean += heard[i] / 12;
		}
		double both = 0, profile_spread = 0, heard_spread = 0;
		for (int i = 0; i < 12; ++i) {
			const double p = profile[i] - profile_mean;
			const double h = heard[(i + tonic) % 12] - heard_mean;
			both += p * h;
			profile_spread += p * p;
			heard_spread += h * h;
		}
		return heard_spread > 0 ? both / std::sqrt(profile_spread * heard_spread) : 0.0;
	};
	int best = -1;
	fit = 0;
	for (int key = 0; key < 24; ++key) {
		const double r = correlation(key < 12 ? MAJOR : MINOR, notes, key % 12);
		if (r > fit) {
			fit = r;
			best = key;
		}
	}
	return fit >= KEY_FIT ? best : -1;
}

static snurra_beat_grid find_grid(const onsets_t &found, int rate, const std::function<bool()> &stop);

snurra_beat_grid snurra_find_beats(snurra_source &source, int rate, const std::function<bool()> &stop) {
	const onsets_t found = onsets(source, rate, stop);
	double fit = 0;
	const int key = key_of(found.notes, fit);
	snurra_beat_grid grid = find_grid(found, rate, stop);
	grid.key = key;
	grid.key_fit = fit;
	return grid;
}

static snurra_beat_grid find_grid(const onsets_t &found, int rate, const std::function<bool()> &stop) {
	const snurra_beat_grid whole = grid_of(found, rate, stop);
	const double hops_a_second = double(rate) / HOP;
	const size_t minute = size_t(hops_a_second * 60);
	constexpr size_t LONG_MINUTES = 8;
	constexpr size_t PLACES = 12;
	if (whole.valid() || found.all.size() < minute * LONG_MINUTES) return whole;
	const size_t step = std::max(minute, (found.all.size() - minute) / (PLACES - 1));
	struct place_t {
		size_t from;
		snurra_beat_grid grid;
	};
	std::vector<place_t> clear;
	size_t looked = 0;
	for (size_t from = 0; from + minute <= found.all.size(); from += step) {
		if (stop && stop()) return snurra_beat_grid{};
		onsets_t part;
		part.all.assign(found.all.begin() + long(from), found.all.begin() + long(from + minute));
		part.body.assign(found.body.begin() + long(from), found.body.begin() + long(from + minute));
		++looked;
		const snurra_beat_grid grid = grid_of(part, rate, stop);
		if (grid.valid()) clear.push_back({ from, grid });
	}
	if (clear.size() < 3 || clear.size() * 2 < looked) return whole;
	std::vector<double> periods;
	for (const place_t &place : clear) periods.push_back(place.grid.period);
	std::sort(periods.begin(), periods.end());
	const double middle = periods[periods.size() / 2];
	std::vector<const place_t *> agreeing;
	std::vector<double> confidences;
	for (const place_t &place : clear)
		if (std::abs(place.grid.period / middle - 1) <= 0.01) {
			agreeing.push_back(&place);
			confidences.push_back(place.grid.confidence);
		}
	if (agreeing.size() < 3 || agreeing.size() * 2 < looked) return whole;
	std::sort(confidences.begin(), confidences.end());
	snurra_beat_grid grid;
	grid.rate = rate;
	grid.period = middle;
	grid.confidence = confidences[confidences.size() / 2];
	const double first = double(agreeing.front()->from) * HOP + agreeing.front()->grid.first;
	grid.first = std::fmod(first, grid.period);
	return grid;
}

namespace {

QString beats_file() {
	QString dir = qEnvironmentVariable("SNURRA_CACHE");
	if (dir.isEmpty()) dir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/snurra");
	QDir().mkpath(dir);
	return dir + QStringLiteral("/beats.ini");
}

/* The key: a hash of the path and the track, so any path is a plain key. */
QString beats_key(const QString &path, int audio_track) {
	const QByteArray what = (QFileInfo(path).absoluteFilePath() + QLatin1Char('|') + QString::number(audio_track)).toUtf8();
	return QString::fromLatin1(QCryptographicHash::hash(what, QCryptographicHash::Sha1).toHex());
}

/* The file as it is now: its size and time, which a kept grid must match. */
QString beats_stamp(const QString &path) {
	const QFileInfo info(path);
	return QStringLiteral("%1 %2").arg(info.size()).arg(info.lastModified().toMSecsSinceEpoch());
}

}   // namespace

int snurra_key_shifted(int key, int semitones) {
	if (key < 0 || key > 23) return -1;
	return (key / 12) * 12 + ((key % 12 + semitones) % 12 + 12) % 12;
}

/* A major key's place on the Camelot wheel: C is 8, each fifth up one
 * more; a minor key's is its relative major's, three semitones up. */
static int camelot_number(int key) {
	const int major = key < 12 ? key : (key + 3) % 12;
	return (major * 7 + 7) % 12 + 1;
}

int snurra_key_order(int key) {
	if (key < 0 || key > 23) return 1000;
	return camelot_number(key) * 2 + (key < 12 ? 1 : 0);
}

QString snurra_key_name(int key, int semitones) {
	key = snurra_key_shifted(key, semitones);
	if (key < 0) return QString();
	static const char *const NOTES[12] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
	return QStringLiteral("%1%2 %3%4")
	        .arg(camelot_number(key))
	        .arg(key < 12 ? QLatin1Char('B') : QLatin1Char('A'))
	        .arg(QLatin1String(NOTES[key % 12]))
	        .arg(key < 12 ? QString() : QStringLiteral("m"));
}

bool snurra_cached_beats(const QString &path, int audio_track, int rate, snurra_beat_grid &grid) {
	if (!QFileInfo(path).isFile()) return false;
	QSettings kept(beats_file(), QSettings::IniFormat);
	const QStringList value = kept.value(beats_key(path, audio_track)).toString().split(QLatin1Char(' '));
	/* size time period first confidence rate key fit */
	if (value.size() != 8 || value.at(0) + QLatin1Char(' ') + value.at(1) != beats_stamp(path)) return false;
	const int found_at = value.at(5).toInt();
	if (found_at <= 0) return false;
	const double scale = double(rate) / found_at;
	grid.period = value.at(2).toDouble() * scale;
	grid.first = value.at(3).toDouble() * scale;
	grid.confidence = value.at(4).toDouble();
	grid.rate = rate;
	grid.key = std::clamp(value.at(6).toInt(), -1, 23);
	grid.key_fit = value.at(7).toDouble();
	return true;
}

void snurra_cache_beats(const QString &path, int audio_track, const snurra_beat_grid &grid) {
	if (!QFileInfo(path).isFile() || grid.rate <= 0) return;
	QSettings kept(beats_file(), QSettings::IniFormat);
	kept.setValue(beats_key(path, audio_track), QStringLiteral("%1 %2 %3 %4 %5 %6 %7")
	                                                     .arg(beats_stamp(path))
	                                                     .arg(grid.period, 0, 'g', 17)
	                                                     .arg(grid.first, 0, 'g', 17)
	                                                     .arg(grid.confidence, 0, 'g', 6)
	                                                     .arg(grid.rate)
	                                                     .arg(grid.key)
	                                                     .arg(grid.key_fit, 0, 'g', 4));
}

snurra_beats::snurra_beats(const QString &source, int rate, std::function<void(const snurra_beat_grid &)> done,
                           int audio_track) {
	m_thread = std::thread([this, source, rate, done, audio_track] {
		/* Kept from before: at once, where a set took 27 s to find. */
		snurra_beat_grid kept;
		if (snurra_cached_beats(source, audio_track, rate, kept)) {
			if (!m_stop.load()) done(kept);
			return;
		}
		std::unique_ptr<snurra_source> reader = snurra_open_source(source, rate, 0);
		if (!reader || reader->length() <= 0) return;
		if (audio_track >= 0 && reader->choose_audio_track(audio_track)) reader->seek(0);
		snurra_beat_grid grid = snurra_find_beats(*reader, rate, [this] { return m_stop.load(std::memory_order_relaxed); });
		if (m_stop.load()) return;
		grid.rate = rate;
		snurra_cache_beats(source, audio_track, grid);
		done(grid);
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
