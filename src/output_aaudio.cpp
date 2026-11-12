#include "snurra/output.h"

#include <QtGlobal>

#include <aaudio/AAudio.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <thread>

#include "snurra/clock.h"

namespace {

/*
 * AAudio, Android's own audio path (project.md sec 3a): the engine is run
 * inside AAudio's data callback, on AAudio's real-time thread -- pull, as
 * ALSA's thread and every other backend here -- in low-latency mode,
 * shared by default and exclusive when asked (`aaudio:exclusive`), as sec
 * 3a has it: shared so other programs go on sounding, exclusive for the
 * lowest latency, by choice.
 *
 * Unlike QAudioSink this sees all the way down: AAudioStream_getTimestamp
 * gives the moment a known frame was presented, on CLOCK_MONOTONIC, which
 * is clock.h's clock, so the frames written after it are placed exactly
 * and through the same filter as ALSA's (snurra_heard_clock). AAudio's
 * own rules for the callback (AAudio.h): no allocation, no locks, no I/O;
 * AAudioStream_get*() is allowed, and is all this calls.
 *
 * A stream AAudio closes underneath -- headphones pulled, a route change,
 * the audio server restarting -- reports an error on another thread, which
 * may not close it either (AAudio.h); it is marked dead and the callback
 * stops calling the engine. A watcher of this output's own, outside every
 * AAudio callback, then closes it and opens a new one on whatever the
 * route now is, as Oboe does, and the engine goes on in it: the decks
 * never knew. The new stream is measured afresh (the heard clock
 * restarted), and must come at the same rate, the engine's; until one
 * opens, the watcher tries again every half second.
 */
class aaudio_output : public snurra_output {
public:
	aaudio_output(bool exclusive, int drop_ms) : m_exclusive(exclusive), m_drop_ms(drop_ms) {}
	~aaudio_output() override {
		stop();
		if (m_stream) AAudioStream_close(m_stream);
	}

	bool open() {
		AAudioStreamBuilder *builder = nullptr;
		if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK) return false;
		AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
		AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
		AAudioStreamBuilder_setChannelCount(builder, 2);
		AAudioStreamBuilder_setSampleRate(builder, 48000);
		AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
		AAudioStreamBuilder_setSharingMode(builder, m_exclusive ? AAUDIO_SHARING_MODE_EXCLUSIVE
		                                                        : AAUDIO_SHARING_MODE_SHARED);
		AAudioStreamBuilder_setDataCallback(builder, &aaudio_output::data, this);
		AAudioStreamBuilder_setErrorCallback(builder, &aaudio_output::error, this);
		const aaudio_result_t opened = AAudioStreamBuilder_openStream(builder, &m_stream);
		AAudioStreamBuilder_delete(builder);
		if (opened != AAUDIO_OK || !m_stream) {
			m_stream = nullptr;
			return false;
		}
		/* What was granted, which need not be what was asked. */
		m_rate = AAudioStream_getSampleRate(m_stream);
		if (AAudioStream_getFormat(m_stream) != AAUDIO_FORMAT_PCM_FLOAT || AAudioStream_getChannelCount(m_stream) != 2
		    || m_rate <= 0)
			return false;
		/* The lowest latency that does not glitch: two bursts, the guide's
		 * starting point; AAudio clamps it to what it can do. */
		const int32_t burst = AAudioStream_getFramesPerBurst(m_stream);
		if (burst > 0) AAudioStream_setBufferSizeInFrames(m_stream, 2 * burst);
		m_period = std::max(1, int(burst));
		m_buffer = int(AAudioStream_getBufferSizeInFrames(m_stream));
		/* What was granted, said: exclusive asked for may come back shared. */
		const bool exclusive = AAudioStream_getSharingMode(m_stream) == AAUDIO_SHARING_MODE_EXCLUSIVE;
		qInfo("snurra: aaudio %s (%s asked), %d Hz, burst %d, buffer %d frames, %s",
		      exclusive ? "exclusive" : "shared", m_exclusive ? "exclusive" : "shared", m_rate,
		      int(burst), m_buffer,
		      AAudioStream_getPerformanceMode(m_stream) == AAUDIO_PERFORMANCE_MODE_LOW_LATENCY ? "low latency"
		                                                                                       : "not low latency");
		return true;
	}

	bool start(render_t render) override {
		if (!m_stream) return false;
		m_render = std::move(render);
		if (AAudioStream_requestStart(m_stream) != AAUDIO_OK) return false;
		m_watching.store(true);
		m_watcher = std::thread([this] { watch(); });
		return true;
	}

	void stop() override {
		m_watching.store(false);
		if (m_watcher.joinable()) m_watcher.join();
		if (!m_stream) return;
		AAudioStream_requestStop(m_stream);
		/* After this the callback is not called again. */
		aaudio_stream_state_t now = AAUDIO_STREAM_STATE_UNINITIALIZED;
		AAudioStream_waitForStateChange(m_stream, AAUDIO_STREAM_STATE_STOPPING, &now, 1000000000LL);
	}

	int rate() const override { return m_rate; }
	int period() const override { return m_period; }
	int64_t latency_us() const override { return snurra_frames_to_us(m_buffer, m_rate); }
	QString name() const override {
		return m_exclusive ? QStringLiteral("aaudio:exclusive") : QStringLiteral("aaudio");
	}

private:
	static aaudio_data_callback_result_t data(AAudioStream *stream, void *user, void *audio, int32_t frames) {
		aaudio_output *self = static_cast<aaudio_output *>(user);
		float *out = static_cast<float *>(audio);
		std::fill(out, out + 2 * frames, 0.0f);
		if (self->m_dead.load(std::memory_order_relaxed)) return AAUDIO_CALLBACK_RESULT_STOP;
		self->m_render(out, frames, self->m_clock.next(self->measure(stream), frames, self->m_rate));
		return AAUDIO_CALLBACK_RESULT_CONTINUE;
	}

	static void error(AAudioStream *, void *user, aaudio_result_t error) {
		aaudio_output *self = static_cast<aaudio_output *>(user);
		self->m_error.store(error, std::memory_order_relaxed);
		self->m_dead.store(true, std::memory_order_release);
	}

	/* The watcher: a dead stream replaced, on no AAudio thread. */
	void watch() {
		bool said = false;
		const auto started = std::chrono::steady_clock::now();
		while (m_watching.load()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(m_stream ? 100 : 500));
			if (m_drop_ms > 0 && m_stream
			    && std::chrono::steady_clock::now() - started >= std::chrono::milliseconds(m_drop_ms)) {
				m_drop_ms = 0;
				qWarning("snurra: aaudio dropping the stream, as asked");
				error(m_stream, this, AAUDIO_ERROR_DISCONNECTED);
			}
			if (!m_dead.load(std::memory_order_acquire) || !m_watching.load()) continue;
			if (m_stream) {
				qWarning("snurra: aaudio stream lost (%s), reopening",
				         AAudio_convertResultToText(m_error.load(std::memory_order_relaxed)));
				AAudioStream_requestStop(m_stream);
				AAudioStream_close(m_stream);   /* no callback after this returns */
				m_stream = nullptr;
			}
			const int rate = m_rate;
			if (!open() || m_rate != rate) {
				if (m_stream) AAudioStream_close(m_stream);
				m_stream = nullptr;
				m_rate = rate;
				if (!said) qWarning("snurra: aaudio will not reopen at %d Hz yet; trying again", rate);
				said = true;
				continue;
			}
			said = false;
			m_clock.restart();
			m_dead.store(false, std::memory_order_release);
			if (AAudioStream_requestStart(m_stream) != AAUDIO_OK) m_dead.store(true, std::memory_order_release);
			else qInfo("snurra: aaudio reopened");
		}
	}

	/* When the first frame of this callback will be heard: frames written
	 * so far, against the frame AAudio last presented and when. */
	int64_t measure(AAudioStream *stream) const {
		const int64_t written = AAudioStream_getFramesWritten(stream);
		int64_t presented = 0;
		int64_t nanos = 0;
		if (AAudioStream_getTimestamp(stream, CLOCK_MONOTONIC, &presented, &nanos) == AAUDIO_OK)
			return nanos / 1000 + snurra_frames_to_us(written - presented, m_rate);
		/* Not yet started enough to say: what is queued, from now. */
		const int64_t read = AAudioStream_getFramesRead(stream);
		return snurra_clock_us() + snurra_frames_to_us(std::max<int64_t>(0, written - read), m_rate);
	}

	bool m_exclusive;
	int m_drop_ms;
	AAudioStream *m_stream = nullptr;
	render_t m_render;
	snurra_heard_clock m_clock;
	std::atomic<bool> m_dead{ false };
	std::atomic<aaudio_result_t> m_error{ AAUDIO_OK };
	std::atomic<bool> m_watching{ false };
	std::thread m_watcher;
	int m_rate = 0;
	int m_period = 1;
	int m_buffer = 0;
};

}

std::unique_ptr<snurra_output> snurra_open_output_aaudio(bool exclusive, int drop_ms) {
	auto output = std::make_unique<aaudio_output>(exclusive, drop_ms);
	if (!output->open()) return nullptr;
	return output;
}
