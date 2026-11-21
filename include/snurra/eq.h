#ifndef SNURRA_EQ_H
#define SNURRA_EQ_H

#include <QtGlobal>

#include <algorithm>
#include <cmath>

/* A band's dB for none of it: below everything, a kill (deck.h). */
constexpr float SNURRA_EQ_KILL = -1000.0f;

/* The most bands the EQ splits into: the grid's. */
constexpr int SNURRA_EQ_BANDS = 12;

/*
 * Where the EQ may split (project.md sec 4, The EQ): eleven crossovers,
 * octaves from 32 Hz to 8 kHz and then 12 and 16 kHz. Every layout splits
 * at some of them, so a layout is a set of bits and a band a run of the
 * grid's twelve narrowest; a band split in two is split at a crossover of
 * the grid inside it. The three of a DJ mixer are at 250 Hz and 4 kHz
 * (bits 3 and 7), and each count offered nests the one before.
 */
constexpr float SNURRA_EQ_CROSSOVERS[SNURRA_EQ_BANDS - 1] = { 32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 12000, 16000 };

/* The EQ's line runs 20 Hz to 20 kHz, octaves evenly: where a frequency
 * is across it, from 0 to 1, and back. A controller learned on the line
 * takes the part under the hand by the same reckoning. */
inline double snurra_eq_fraction_of(double hz) {
	return std::clamp(std::log(hz / 20.0) / std::log(1000.0), 0.0, 1.0);
}
inline double snurra_eq_hz_at(double fraction) {
	return 20.0 * std::pow(1000.0, std::clamp(fraction, 0.0, 1.0));
}

struct snurra_eq {
	static constexpr quint16 THREE = (1u << 3) | (1u << 7);
	static constexpr quint16 FIVE = THREE | (1u << 2) | (1u << 8);
	static constexpr quint16 SEVEN = FIVE | (1u << 1) | (1u << 5);
	static constexpr quint16 TWELVE = (1u << (SNURRA_EQ_BANDS - 1)) - 1;

	/* Bit i: split at SNURRA_EQ_CROSSOVERS[i]. A DJ mixer's three splits
	 * (THREE) are always among them: the parts the knobs move. */
	quint16 splits = THREE;
	/* The shape, by the grid's twelve narrowest bands, in dB, or
	 * SNURRA_EQ_KILL; a band of several holds its one value in each. What
	 * is heard is the shape moved by its part's knob (level). */
	float db[SNURRA_EQ_BANDS] = {};
	/* The three knobs -- low, mid, high, a controller's -- in dB (-24 to
	 * +6, or SNURRA_EQ_KILL): each raises or lowers every band in its
	 * part, the shape kept, so a kill and back finds it as it was (the
	 * holder, 2026-11-21). */
	float part[3] = {};

	int bands() const {
		int count = 1;
		for (int i = 0; i < SNURRA_EQ_BANDS - 1; ++i) count += (splits >> i) & 1;
		return count;
	}
	/* The narrowest bands band `band` runs over, first and last. */
	int first_of(int band) const {
		int fine = 0;
		for (int i = 0; i < SNURRA_EQ_BANDS - 1 && band > 0; ++i)
			if ((splits >> i) & 1) {
				--band;
				fine = i + 1;
			}
		return fine;
	}
	int last_of(int band) const {
		const int next = band + 1 < bands() ? first_of(band + 1) : SNURRA_EQ_BANDS;
		return next - 1;
	}
	/* The band a narrowest one is in. */
	int band_of(int fine) const {
		int band = 0;
		for (int i = 0; i < fine && i < SNURRA_EQ_BANDS - 1; ++i) band += (splits >> i) & 1;
		return band;
	}
	/* What a narrowest band is heard at: its shape moved by its part's
	 * knob, no higher than +6 dB; killed where either is. */
	float level(int fine) const {
		const float knob = part[part_of(fine)], own = db[fine];
		if (knob <= SNURRA_EQ_KILL || own <= SNURRA_EQ_KILL) return SNURRA_EQ_KILL;
		return std::min(own + knob, 6.0f);
	}
	float band_level(int band) const { return level(std::clamp(first_of(band), 0, SNURRA_EQ_BANDS - 1)); }
	/* The shape's own value of a band (band_level is what is heard). */
	float band_db(int band) const { return db[std::clamp(first_of(band), 0, SNURRA_EQ_BANDS - 1)]; }
	void set_band(int band, float value) {
		for (int fine = first_of(band); fine <= last_of(band); ++fine) db[fine] = value;
	}
	/* A band drawn to be heard at `value`: its shape what the knob leaves
	 * to it. Drawn up in a part killed by its knob, the rest of the part
	 * stays killed and the knob lets go. Where the part's bands then all
	 * stand alike the knob takes their height -- three bands are the
	 * three knobs, as a DJ mixer's. */
	void set_band_level(int band, float value) {
		const int p = part_of(first_of(band));
		if (value > SNURRA_EQ_KILL && part[p] <= SNURRA_EQ_KILL) {
			for (int other = 0; other < bands(); ++other)
				if (other != band && part_of(first_of(other)) == p) set_band(other, SNURRA_EQ_KILL);
			part[p] = 0;
		}
		set_band(band, value <= SNURRA_EQ_KILL ? SNURRA_EQ_KILL : value - (part[p] <= SNURRA_EQ_KILL ? 0 : part[p]));
		settle(p);
	}
	/* A part whose bands all stand alike: the knob takes it, the shape
	 * flat. */
	void settle(int p) {
		float alike = 0;
		bool first = true;
		for (int fine = 0; fine < SNURRA_EQ_BANDS; ++fine) {
			if (part_of(fine) != p) continue;
			if (!first && db[fine] != alike) return;
			alike = db[fine];
			first = false;
		}
		if (alike == 0) return;
		if (alike <= SNURRA_EQ_KILL) part[p] = SNURRA_EQ_KILL;
		else if (part[p] > SNURRA_EQ_KILL) part[p] = std::clamp(part[p] + alike, -24.0f, 6.0f);
		for (int fine = 0; fine < SNURRA_EQ_BANDS; ++fine)
			if (part_of(fine) == p) db[fine] = 0;
	}
	/* Whether the split at the grid's crossover `at` may be joined: not
	 * one of the three the knobs' parts are bounded by. */
	static bool joinable(int at) { return at >= 0 && at < SNURRA_EQ_BANDS - 1 && !((THREE >> at) & 1); }
	/* Split at the grid's crossover `at`, or join there: the halves keep
	 * the band's value; joined, the lower half's goes across both. The
	 * three splits of the parts stay. */
	void split(int at, bool on) {
		if (at < 0 || at >= SNURRA_EQ_BANDS - 1) return;
		if (on) {
			splits = quint16(splits | (1u << at));
		} else if (joinable(at) && ((splits >> at) & 1)) {
			const float lower = db[first_of(band_of(at))];
			splits = quint16(splits & ~(1u << at));
			set_band(band_of(at), lower);
			settle(part_of(at));
		}
	}
	/* Neighbours heard near 0 dB and near each other, or alike, joined
	 * (the holder, 2026-11-21: bands "close to zero and near together are
	 * merged"): within 2 dB of 0 and 1 dB of each other, or within a
	 * quarter dB anywhere. The joined band takes the mean. Not across the
	 * parts' three splits. True where any joined. */
	bool merge_near() {
		bool joined = false, again = true;
		while (again) {
			again = false;
			for (int at = 0; at < SNURRA_EQ_BANDS - 1; ++at) {
				if (!joinable(at) || !((splits >> at) & 1)) continue;
				const int below = band_of(at);
				const float a = band_level(below), b = band_level(below + 1);
				const bool killed = a <= SNURRA_EQ_KILL || b <= SNURRA_EQ_KILL;
				const bool near = !killed && ((std::abs(a) <= 2 && std::abs(b) <= 2 && std::abs(a - b) <= 1) || std::abs(a - b) <= 0.25f);
				if (!near && !(a <= SNURRA_EQ_KILL && b <= SNURRA_EQ_KILL)) continue;
				const float mean = (band_db(below) + band_db(below + 1)) / 2;
				const float shape = killed ? SNURRA_EQ_KILL : mean;
				splits = quint16(splits & ~(1u << at));
				set_band(band_of(at), shape);
				settle(part_of(at));
				joined = again = true;
			}
		}
		return joined;
	}
	/* Another layout, each new band taking the value its middle had; the
	 * parts' three splits kept. */
	void set_layout(quint16 layout) {
		float was[SNURRA_EQ_BANDS];
		std::copy(db, db + SNURRA_EQ_BANDS, was);
		splits = quint16((layout | THREE) & TWELVE);
		for (int band = 0; band < bands(); ++band) set_band(band, was[(first_of(band) + last_of(band)) / 2]);
		for (int p = 0; p < 3; ++p) settle(p);
	}
	/* A DJ mixer's three, by where a band starts: low below 250 Hz, mid
	 * to 4 kHz, high above -- each layout's bands in that part moved
	 * together. 0, 1, 2. */
	static int part_of(int fine) { return fine < 4 ? 0 : fine < 8 ? 1 : 2; }
	/* The narrowest band a frequency is in. */
	static int fine_at(double hz) {
		int fine = 0;
		while (fine < SNURRA_EQ_BANDS - 1 && hz >= SNURRA_EQ_CROSSOVERS[fine]) ++fine;
		return fine;
	}
	/* A part's knob, -24 to +6 dB or SNURRA_EQ_KILL. */
	void set_part(int which, float value) { part[std::clamp(which, 0, 2)] = value; }
	float part_db(int which) const { return part[std::clamp(which, 0, 2)]; }
	bool flat() const {
		for (int fine = 0; fine < SNURRA_EQ_BANDS; ++fine)
			if (level(fine) != 0) return false;
		return true;
	}
	bool operator==(const snurra_eq &other) const {
		return splits == other.splits && std::equal(db, db + SNURRA_EQ_BANDS, other.db) && std::equal(part, part + 3, other.part);
	}
	bool operator!=(const snurra_eq &other) const { return !(*this == other); }
};

#endif
