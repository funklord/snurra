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

#endif
