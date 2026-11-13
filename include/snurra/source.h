#ifndef SNURRA_SOURCE_H
#define SNURRA_SOURCE_H

#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <memory>

/*
 * Bytes that may still be arriving (project.md sec 1, the library's
 * centre): read at an offset, without blocking, saying how many are here.
 * What a consumer implements to have snurra play from its own transport --
 * BeerSSH's SFTP, fuzznet's swarm -- and nothing above it knows which.
 * Called from a deck's reader thread only.
 *
 * fuzzypickles' contract, in this shape: a read that finds nothing yet is
 * a stall, not an end; playback waits for bytes rather than failing, and
 * what has not begun is deferred until it can.
 */
class snurra_bytes {
public:
	virtual ~snurra_bytes() = default;

	/* The whole size, or -1 while not known. */
	virtual int64_t size() const = 0;

	/* Up to `n` bytes at `offset` into `out`: how many are here now, 0 if
	 * none yet (a stall), -1 at or past the end. Never blocks. */
	virtual int64_t read(int64_t offset, uint8_t *out, int64_t n) = 0;

	/* Whether all of [offset, offset + n) is here. The default says no,
	 * which costs only what needs certainty: the length is then estimated
	 * rather than read from the last packets. */
	virtual bool has(int64_t offset, int64_t n) const {
		(void)offset;
		(void)n;
		return false;
	}

	/* The reader will want bytes from `offset` next: a fetch that follows
	 * the playhead asks for these first (fuzznet). A seek is one of these. */
	virtual void want(int64_t offset) { (void)offset; }

	/* Waits up to `ms` for more to arrive, anywhere. The default sleeps;
	 * a transport that can wake its reader should. */
	virtual void wait(int ms);
};

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

	/* Asked while waiting for bytes: true gives up the wait -- a seek to
	 * somewhere else, or the deck closing. The read then returns 0 and
	 * the source is ready to seek. */
	virtual void set_interrupt(std::function<bool()> interrupted) { (void)interrupted; }

	/* The silence put in place of what could not be opened, so a tile for
	 * it still behaves as a tile: true for that alone. */
	virtual bool stand_in() const { return false; }
	/* A stand-in's length, told it from elsewhere -- a picture's, when the
	 * file has no sound. Nothing for a real source. */
	virtual void set_length(int64_t frames) { (void)frames; }

	/* The file's sound tracks -- a film's languages -- by name, and which
	 * plays; another chosen plays from where the next seek puts it. A
	 * source of one track, or none, lists none. */
	virtual QStringList audio_tracks() const { return {}; }
	virtual int audio_track() const { return -1; }
	virtual bool choose_audio_track(int track) {
		(void)track;
		return false;
	}
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
 *  - `snurra-test:wrapped-ramp`, the same with the index taken modulo
 *    2^16 over 2^16, and both channels at half scale: a frame is 2^-17
 *    of full scale rather than 2^-24, so it survives an output that mixes
 *    at 24 bits -- ALSA's dmix shifts a 32-bit sample right by 8 -- to
 *    1/64 of a frame, where the plain ramp is resolved to two frames; and
 *    two of them crossfading in a dmix do not clip, which at full scale
 *    they did, a period at a time, reading as a frame and a bit of error;
 *  - anything else: silence, `fallback_length` frames long, so a tile for
 *    something unplayable still behaves as a tile.
 */
std::unique_ptr<snurra_source> snurra_open_source(const QString &name, int rate, int64_t fallback_length);

/*
 * Opens `bytes` through FFmpeg, `name` only a hint at its kind (an
 * extension). Blocks, on the reader's thread, until enough has arrived to
 * know the format -- the deferred start -- or `interrupted` says to stop;
 * nullptr if it is not audio FFmpeg can read.
 */
std::unique_ptr<snurra_source> snurra_open_source(std::shared_ptr<snurra_bytes> bytes, const QString &name, int rate,
                                                  std::function<bool()> interrupted);

/* The ramp's scale: an index this size or more no longer fits a float. */
constexpr double SNURRA_RAMP_SCALE = 16777216.0;
constexpr double SNURRA_WRAPPED_RAMP_SCALE = 65536.0;

#endif
