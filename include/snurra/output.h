#ifndef SNURRA_OUTPUT_H
#define SNURRA_OUTPUT_H

#include <QString>

#include <cstdint>
#include <functional>
#include <memory>

/*
 * Where the mix goes (project.md sec 3a): an interface with a backend per
 * kind of output, the engine run inside the backend's own thread or
 * callback -- pull, as Mixxx does. The callback gets the buffer to fill
 * and the time on the shared clock (clock.h) at which its first frame will
 * be heard, which is what lets two processes cross over at one moment
 * whatever each output's latency.
 *
 * Interleaved stereo float, always; the rate is the output's.
 */
class snurra_output {
public:
	using render_t = std::function<void(float *out, int frames, int64_t present_us)>;

	virtual ~snurra_output() = default;

	/* Starts calling `render`; false when the output cannot run. */
	virtual bool start(render_t render) = 0;

	/* Stops, and `render` is not called again once this returns. */
	virtual void stop() = 0;

	virtual int rate() const = 0;

	/* Frames per call, and from rendering to hearing, as the device says. */
	virtual int period() const = 0;
	virtual int64_t latency_us() const = 0;

	virtual QString name() const = 0;
};

/*
 * An output by its spec:
 *
 *   alsa:<device>     ALSA, e.g. alsa:default (shared: dmix, PulseAudio or
 *                     PipeWire behind it, so other programs go on sounding)
 *                     or alsa:hw:0,0 (the card alone, lowest latency)
 *   aaudio[:exclusive] Android's AAudio, low latency, shared unless asked;
 *                     the default there (SNURRA_AAUDIO)
 *   qt[:<device>]     Qt Multimedia's QAudioSink, by its description or
 *                     the default: the plain fallback, and Android's
 *                     output; built where Qt Multimedia is (SNURRA_QT_AUDIO)
 *   null              nothing, paced by the clock as a device would be
 *   capture:<dir>     as null, and what was played written to
 *                     <dir>/<pid>.capture, for the tests
 *
 * $SNURRA_OUTPUT names it, alsa:default when unset (aaudio, then qt, on
 * Android); one that will not open
 * falls back to null, said on stderr, so a machine with no sound still
 * plays -- silently, with the positions moving as they would.
 *
 * A capture is a run of records, each one block as rendered:
 *   int64 present_us, int32 frames, then frames * 2 floats (L R),
 * native byte order, and it stops growing at 32 MB.
 */
/*
 * When the next frame written will be heard, filtered:
 * the device's clock is steady, so each period is heard one period after
 * the last, and measurements only nudge that prediction rather than
 * replacing it. A single reading jitters by tens of microseconds --
 * through snd-aloop, two to three frames -- and a handover takes one
 * reading from each side.
 *
 * The nudge is an eighth of the MEDIAN of the last fifteen errors, not
 * of the latest. On a loaded machine one reading in a few hundred is
 * milliseconds off (measured: 1 to 5 ms, against a clock that drifts by
 * parts per million); an eighth of that moved the prediction 24 frames,
 * and a handover that sampled just after one left the taker 18 frames
 * off the giver. A median does not see a rare outlier at all, and still
 * follows real drift. A reading far off the prediction (a start, an
 * underrun, a suspend) is taken as it is.
 */
class snurra_heard_clock {
public:
	/* `measured`: one reading of when the next frame written is heard;
	 * `frames`: how many are written now, at `rate`. The filtered time. */
	int64_t next(int64_t measured, int frames, int rate);

	/* Measure afresh: the device has restarted. */
	void restart() {
		m_predicted = 0;
		m_count = 0;
	}

	double worst_error() const { return m_worst; }

private:
	double m_predicted = 0;
	double m_worst = 0;
	static constexpr size_t ERRORS = 15;   /* the median of this many */
	double m_errors[ERRORS] = {};
	size_t m_next = 0;
	size_t m_count = 0;
};

std::unique_ptr<snurra_output> snurra_open_output(const QString &spec);
std::unique_ptr<snurra_output> snurra_open_output_alsa(const QString &device);
std::unique_ptr<snurra_output> snurra_open_output_qt(const QString &device);
/* `drop_ms` > 0: a test's disconnect, that long after the start, taken
 * down the error callback's own path -- a route change cannot be made on
 * an emulator, whose legacy path restores itself even when the audio
 * server dies. */
std::unique_ptr<snurra_output> snurra_open_output_aaudio(bool exclusive, int drop_ms = 0);

#endif
