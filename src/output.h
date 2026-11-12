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
 *   null              nothing, paced by the clock as a device would be
 *   capture:<dir>     as null, and what was played written to
 *                     <dir>/<pid>.capture, for the tests
 *
 * $SNURRA_OUTPUT names it, alsa:default when unset; one that will not open
 * falls back to null, said on stderr, so a machine with no sound still
 * plays -- silently, with the positions moving as they would.
 *
 * A capture is a run of records, each one block as rendered:
 *   int64 present_us, int32 frames, then frames * 2 floats (L R),
 * native byte order, and it stops growing at 32 MB.
 */
std::unique_ptr<snurra_output> snurra_open_output(const QString &spec);
std::unique_ptr<snurra_output> snurra_open_output_alsa(const QString &device);

#endif
