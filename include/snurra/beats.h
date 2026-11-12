#ifndef SNURRA_BEATS_H
#define SNURRA_BEATS_H

#include <QString>

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>

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

/* The same on a thread of its own, over a source of its own, as
 * snurra_peaks reads the waveform; `done` is called on that thread. */
class snurra_beats {
public:
	snurra_beats(const QString &source, int rate, std::function<void(const snurra_beat_grid &)> done);
	~snurra_beats();

private:
	std::atomic<bool> m_stop{ false };
	std::thread m_thread;
};

#endif
