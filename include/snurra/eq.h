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

	/* Bit i: split at SNURRA_EQ_CROSSOVERS[i]. */
	quint16 splits = THREE;
	/* By the grid's twelve narrowest bands, in dB (-24 to +6, or
	 * SNURRA_EQ_KILL); a band of several holds its one value in each. */
	float db[SNURRA_EQ_BANDS] = {};

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
	float band_db(int band) const { return db[std::clamp(first_of(band), 0, SNURRA_EQ_BANDS - 1)]; }
	void set_band(int band, float value) {
		for (int fine = first_of(band); fine <= last_of(band); ++fine) db[fine] = value;
	}
	/* Split at the grid's crossover `at`, or join there: the halves keep
	 * the band's value; joined, the lower half's goes across both. */
	void split(int at, bool on) {
		if (at < 0 || at >= SNURRA_EQ_BANDS - 1) return;
		if (on) {
			splits = quint16(splits | (1u << at));
		} else {
			const float lower = db[first_of(band_of(at))];
			splits = quint16(splits & ~(1u << at));
			set_band(band_of(at), lower);
		}
	}
	/* Another layout, each new band taking the value its middle had. */
	void set_layout(quint16 layout) {
		float was[SNURRA_EQ_BANDS];
		std::copy(db, db + SNURRA_EQ_BANDS, was);
		splits = quint16(layout & TWELVE);
		for (int band = 0; band < bands(); ++band) set_band(band, was[(first_of(band) + last_of(band)) / 2]);
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
	void set_part(int part, float value) {
		for (int band = 0; band < bands(); ++band)
			if (part_of(first_of(band)) == part) set_band(band, value);
	}
	float part_db(int part) const {
		for (int band = 0; band < bands(); ++band)
			if (part_of(first_of(band)) == part) return band_db(band);
		return 0;
	}
	bool flat() const {
		for (float value : db)
			if (value != 0) return false;
		return true;
	}
	bool operator==(const snurra_eq &other) const {
		return splits == other.splits && std::equal(db, db + SNURRA_EQ_BANDS, other.db);
	}
	bool operator!=(const snurra_eq &other) const { return !(*this == other); }
};

#endif
