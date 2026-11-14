#ifndef SNURRA_SOURCE_H
#define SNURRA_SOURCE_H

#include <QString>
#include <QStringList>

class QMimeData;

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

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
 *  - a file FFmpeg can decode, or a URL it can open (snurra_is_url:
 *    http, https, rtsp ...), as mplayer plays one -- `interrupted` gives
 *    up a network wait, for a close or a seek;
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
 *  - a name the host's resolver knows (snurra_set_bytes_resolver), as its
 *    bytes;
 *  - anything else: silence, `fallback_length` frames long, so a tile for
 *    something unplayable still behaves as a tile.
 */
std::unique_ptr<snurra_source> snurra_open_source(const QString &name, int rate, int64_t fallback_length,
                                                  std::function<bool()> interrupted = nullptr);

/*
 * Names a host opens as bytes of its own: asked before anything above but
 * the tests' names, with the name a tile was given; an empty pointer for a
 * name it does not know. The player's estate (app/estate.h) opens
 * "fuzznet:ROOT" this way, so the library itself knows nothing of where
 * those bytes come from (project.md sec 1). Set once, at start, before any
 * source is opened; every process the player starts sets its own.
 */
using snurra_bytes_resolver = std::function<std::shared_ptr<snurra_bytes>(const QString &name)>;
void snurra_set_bytes_resolver(snurra_bytes_resolver resolver);

/*
 * What to call `name` in a title: the host's own word for it where it has
 * one (snurra_set_title_resolver -- the estate's catalogued name for a
 * "fuzznet:ROOT"), else the file's name, else the name itself. Set once,
 * at start, as the bytes resolver is.
 */
using snurra_title_resolver = std::function<QString(const QString &name)>;
void snurra_set_title_resolver(snurra_title_resolver resolver);
QString snurra_title_of(const QString &name);

/*
 * Names whose content never changes -- named by its hash, as an estate's
 * "fuzznet:ROOT" is -- which a cache may keep by the name alone: the beat
 * cache keys a file by its size and time, which such a name has none of.
 * A host says which (snurra_set_lasting_names); none by default.
 */
void snurra_set_lasting_names(std::function<bool(const QString &name)> lasting);
bool snurra_name_lasts(const QString &name);

/* What a tile is given for `name`: a path made absolute, so another
 * process finds it; a URL, or a name with a scheme of its own --
 * "fuzznet:ROOT", "tone:440" -- as it is, which a path made absolute
 * would have turned into a file under the current folder. */
QString snurra_source_of(const QString &name);

/* Whether `name` is a URL (a scheme and "://"), which goes to FFmpeg as
 * it is, rather than a path. */
bool snurra_is_url(const QString &name);

/*
 * What a drag brings that a tile could play (project.md sec 3b), in order:
 * a file as it is; a folder as the audio and video under it, sorted by
 * path as people count (2 before 10), at most `folder_most` of them; a
 * link (a browser's) as its URL. Empty when it brings none of these.
 */
QStringList snurra_dropped(const QMimeData *mime, int folder_most = 64);
/*
 * A stem file's stems (project.md sec 3e): Native Instruments' .stem.mp4
 * (or .stem.m4a) holds the mix as its first sound track and four stems
 * after it, in this order -- drums, bass, other, vocals. Empty for any
 * other name. By the name, as the format's own programs know one.
 */
QStringList snurra_stem_names(const QString &path);

/*
 * A playlist's entries (mplayer's -playlist): .m3u and .m3u8 a line each,
 * comments (#) skipped; .pls its FileN= lines in N's order. A path is
 * taken from the playlist's folder, a file:// URL as its path, another URL
 * as it is. Another playlist in it is left out -- no loops -- and so is
 * all past `most`. Empty for a name that is not a playlist's.
 */
bool snurra_is_playlist(const QString &path);
QStringList snurra_playlist(const QString &path, int most = 1000);

/* A drag of tunes from snurra's library (library.h): paths, a line each.
 * Its own type, not a URI list, so a desktop does not take it as a copy. */
#define SNURRA_FILES_MIME "application/x-snurra-files"

/* Whether a folder's file is one to play, by the name's type: audio or
 * video, and no playlist or cue sheet. */
bool snurra_playable_by_name(const QString &path);

/*
 * A file's own title and artist, as its tags say (ID3, Vorbis comments,
 * MP4 and Matroska tags, through FFmpeg): the container's, else its first
 * stream's. Empty where it has none. Opens the file, so off the UI thread.
 */
struct snurra_tags {
	QString title;
	QString artist;
	double bpm = 0;   /* not tags: the library's, from beats.h's cache */
	int key = -1;
};
snurra_tags snurra_read_tags(const QString &path);

/*
 * A file's chapters -- a mix's tracklist, as hydra's saved mixes carry it
 * in Matroska and MP4 -- each from its start, in microseconds from the
 * sound's zero (the deck's), with its title. In order; none for a file
 * without.
 */
struct snurra_chapter {
	qint64 start_us = 0;
	QString title;
};
std::vector<snurra_chapter> snurra_read_chapters(const QString &path);

/* A cue sheet's tracks for the file `audio` names, as chapters: INDEX 01
 * (minutes:seconds:frames, 75 a second), titled "PERFORMER - TITLE" (or
 * the one there is). snurra_read_chapters takes the sheet beside a file
 * -- its name with .cue -- where the file has no chapters of its own, as
 * hydra writes one beside a saved mix. */
std::vector<snurra_chapter> snurra_read_cue(const QString &cue, const QString &audio);

/* Whether a drag might bring any, cheaply -- a folder not looked into --
 * for a drag still moving. */
bool snurra_drop_offers(const QMimeData *mime);

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
