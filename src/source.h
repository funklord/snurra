#ifndef SNURRA_SOURCE_H
#define SNURRA_SOURCE_H

#include <QString>

#include <cstdint>
#include <memory>

/*
 * Decoded audio, stereo float at the engine's rate, read in order from a
 * position. Used only by a deck's reader thread, never by the callback,
 * so it may block, allocate and do I/O (project.md sec 3a).
 */
class snurra_source {
public:
	virtual ~snurra_source() = default;

	/* Up to `frames` frames into `out` (interleaved L R). Returns how many;
	 * 0 with at_end() is the end, 0 without it is "not here yet" -- a
	 * source still arriving, which the deck waits out rather than ends. */
	virtual int read(float *out, int frames) = 0;
	virtual bool at_end() const = 0;

	/* The next read starts exactly at `frame`. */
	virtual void seek(int64_t frame) = 0;

	/* The frame the next read starts at. */
	virtual int64_t position() const = 0;

	/* In frames; 0 when unknown. */
	virtual int64_t length() const = 0;
};

/*
 * Opens what a tile names, at `rate`:
 *
 *  - a file FFmpeg can decode;
 *  - `tone:<hz>`, a quiet sine, for trying an output by ear;
 *  - `snurra-test:ramp`, which the tests use: the left channel is the
 *    frame's own index over 2^24 and the right is 1, so a capture of what
 *    was played says, sample by sample, which frame was heard and at
 *    what gain;
 *  - anything else: silence, `fallback_length` frames long, so a tile for
 *    something unplayable still behaves as a tile.
 */
std::unique_ptr<snurra_source> snurra_open_source(const QString &name, int rate, int64_t fallback_length);

/* The ramp's scale: an index this size or more no longer fits a float. */
constexpr double SNURRA_RAMP_SCALE = 16777216.0;

#endif
