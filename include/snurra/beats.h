#ifndef SNURRA_BEATS_H
#define SNURRA_BEATS_H

#include <QString>

#include <atomic>
#include <memory>
#include <cstdint>
#include <functional>
#include <thread>

class snurra_bytes;
class snurra_source;

/*
 * Where the beats fall (project.md sec 3b, step 6): a constant tempo and
 * the first beat, in frames of the track at its rate. Not the downbeat --
 * a bar is counted as four beats from any beat, so a jump of whole bars
 * keeps the place in the beat, which is what a DJ hears.
 */
struct snurra_beat_grid {
	double period = 0;       /* frames a beat; 0: no beat found */
	double first = 0;        /* the first beat's frame */
	double confidence = 0;   /* how far the beat stands out of the rest: 1 is none */
	int rate = 0;
	/* And the key, found in the same reading (snurra_find_beats): 0 to 11
	 * C to B major, 12 to 23 C to B minor, -1 none; and how well its
	 * profile fits, a correlation. */
	int key = -1;
	double key_fit = 0;
	/* And how loud it is, in the same reading: the integrated loudness of
	 * EBU R128 / ITU-R BS.1770-4 in LUFS -- K-weighted, over 400 ms blocks
	 * a tenth of a second apart, gated at -70 LUFS and then 10 LU under
	 * the blocks' mean -- `loudness_known` false for silence; and the
	 * highest sample, 0 to 1 or over, so a boost can stop short of it. */
	double loudness = 0;
	bool loudness_known = false;
	double peak = 0;

	bool valid() const { return period > 0; }
	double bpm() const { return valid() ? 60.0 * rate / period : 0; }
};

/*
 * snurra's own detector, no third-party code (so none of sec 3e's
 * licences): onsets as spectral flux -- how much each frequency's log
 * magnitude rose from one hop of 512 frames to the next -- then the tempo
 * from their autocorrelation, weighted toward 120 BPM to choose between a
 * tempo and its double or half, between 70 and 180 BPM; then the period and
 * phase that fit the whole track best, by folding every onset at each
 * candidate period. A track whose fold shows no clear peak has no beat.
 * `stop` returning true gives up, returning no grid.
 */
snurra_beat_grid snurra_find_beats(snurra_source &source, int rate, const std::function<bool()> &stop = {});

/*
 * The gain that brings `grid`'s tune to `reference` LUFS (snurra_tile, the
 * level match): cut as far as it takes, boosted only while the highest
 * sample stays under full scale and by 12 dB at most; 1 where its loudness
 * is not known.
 */
float snurra_loudness_gain(const snurra_beat_grid &grid, double reference);
/* Where tunes are brought to: -14 LUFS, as YouTube and Spotify bring theirs,
 * so most of what is played moves by little. */
constexpr double SNURRA_LOUDNESS_REFERENCE = -14;

/*
 * A key as a DJ reads it: the Camelot wheel's number and letter, where
 * a step round or across is a key that mixes, then the key's own name --
 * "8A Am". Empty for none. Shifted by `semitones` first, as playing
 * faster without key lock raises it.
 */
QString snurra_key_name(int key, int semitones = 0);
/* The key raised by `semitones` (or lowered), its mode kept; -1 stays. */
int snurra_key_shifted(int key, int semitones);
/* Where a key sorts: by its Camelot number, A before B; none last. */
int snurra_key_order(int key);

/*
 * Beat grids worked out before, kept in the user's cache --
 * $SNURRA_CACHE, else the desktop's (~/.cache/snurra) -- in beats.ini: by
 * the file's path and sound track, and good only while its size and time
 * are the same -- a lasting name (source.h) by the name alone -- and the
 * analysis that kept it is this one's. "No beat" is kept too, and the key
 * with the grid; one an older analysis kept is not given, so it is found
 * again. A grid is given at `rate`, from whatever rate it was found at.
 * False where none is kept.
 */
/* Where things worked out are kept (the grids here, identities in
 * fingerprint.h): the file, a name's key in it, and the stamp a kept thing
 * must match -- a file's size and time, "0 0" for a lasting name. */
QString snurra_kept_file();
QString snurra_kept_key(const QString &path, int audio_track);
QString snurra_kept_stamp(const QString &path);

bool snurra_cached_beats(const QString &path, int audio_track, int rate, snurra_beat_grid &grid);
void snurra_cache_beats(const QString &path, int audio_track, const snurra_beat_grid &grid);

/* The same on a thread of its own, over a source of its own, as
 * snurra_peaks reads the waveform; `done` is called on that thread. */
class snurra_beats {
public:
	snurra_beats(const QString &source, int rate, std::function<void(const snurra_beat_grid &)> done,
	             int audio_track = -1);   /* as snurra_peaks' */
	/* From a host's bytes (embed.h), `name` hinting at their kind. */
	snurra_beats(std::shared_ptr<snurra_bytes> bytes, const QString &name, int rate,
	             std::function<void(const snurra_beat_grid &)> done);
	~snurra_beats();

private:
	std::atomic<bool> m_stop{ false };
	std::thread m_thread;
};

#endif
