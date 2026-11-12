#ifndef SNURRA_CLOCK_H
#define SNURRA_CLOCK_H

#include <chrono>
#include <cstdint>

/*
 * The one clock every snurra process shares: monotonic, in microseconds.
 * A handover's times are compared across processes on one machine, so it
 * has to be one clock for all of them (CLOCK_MONOTONIC is, on Linux and
 * Android) and must not step when the wall clock is set, which would put
 * a crossfade somewhere else in time than either side meant.
 */
inline int64_t snurra_clock_us() {
	return std::chrono::duration_cast<std::chrono::microseconds>(
	               std::chrono::steady_clock::now().time_since_epoch())
	        .count();
}

/*
 * Frames and microseconds, each way, rounded to the nearest -- never
 * truncated. A handover passes a position through several of these, and
 * truncation loses up to a frame at each, all in the same direction: two
 * decks meant to be in step came out two frames apart before this. A DJ
 * program seeks and cues to the sample, so the conversions must not be
 * where it is lost.
 */
inline int64_t snurra_round_divide(int64_t numerator, int64_t denominator) {
	return numerator >= 0 ? (numerator + denominator / 2) / denominator : -((-numerator + denominator / 2) / denominator);
}

inline int64_t snurra_us_to_frames(int64_t us, int rate) {
	return snurra_round_divide(us * rate, 1000000);
}

inline int64_t snurra_frames_to_us(int64_t frames, int rate) {
	return snurra_round_divide(frames * 1000000, rate);
}

#endif
