#include "output.h"

#include <QFile>

#include <alsa/asoundlib.h>
#include <pthread.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

#include "clock.h"

namespace {

/*
 * ALSA, by device name (project.md sec 3a). `default` shares the card the
 * way the system is set up to -- dmix, PulseAudio or PipeWire behind it --
 * so other programs go on sounding; a `hw:` device is the card alone, for
 * the lowest latency, and refuses a second program. Either works at any
 * period the device grants: the engine is told the latency and places
 * every frame by it.
 *
 * The thread blocks in snd_pcm_writei until the device has room for a
 * period, renders that period, and writes it: the device's clock paces
 * the engine. Before each render it asks how much is queued ahead of the
 * speaker, which is when the new period will be heard -- and *when that
 * was true*: snd_pcm_htimestamp gives the queue together with the
 * monotonic time the device last moved, where snd_pcm_delay read at an
 * arbitrary moment is up to a period stale. Measured through snd-aloop and
 * a dmix, two processes placing frames by snd_pcm_delay were 48 to 110
 * frames apart; a DJ program cannot have its decks a period apart.
 */
class alsa_output : public snurra_output {
public:
	explicit alsa_output(const QString &device) : m_device(device) {}
	~alsa_output() override {
		stop();
		if (m_pcm) snd_pcm_close(m_pcm);
	}

	bool open() {
		if (snd_pcm_open(&m_pcm, QFile::encodeName(m_device).constData(), SND_PCM_STREAM_PLAYBACK, 0) < 0) {
			m_pcm = nullptr;
			return false;
		}
		/* Asked for, not promised: a shared device may grant far more. */
		const QByteArray wanted = qgetenv("SNURRA_LATENCY_MS");
		const unsigned latency = wanted.isEmpty() ? 40000 : unsigned(wanted.toUInt() * 1000);
		if (snd_pcm_set_params(m_pcm, SND_PCM_FORMAT_FLOAT, SND_PCM_ACCESS_RW_INTERLEAVED, 2, RATE, 1, latency)
		    < 0)
			return false;
		snd_pcm_uframes_t buffer = 0;
		snd_pcm_uframes_t period = 0;
		if (snd_pcm_get_params(m_pcm, &buffer, &period) < 0 || period == 0) return false;
		m_period = int(period);
		m_buffer = int(buffer);

		/* Timestamps on the shared clock (clock.h is CLOCK_MONOTONIC). A
		 * device that will not give them falls back to snd_pcm_delay. */
		snd_pcm_sw_params_t *sw = nullptr;
		snd_pcm_sw_params_alloca(&sw);
		if (snd_pcm_sw_params_current(m_pcm, sw) == 0 && snd_pcm_sw_params_set_tstamp_mode(m_pcm, sw, SND_PCM_TSTAMP_ENABLE) == 0
		    && snd_pcm_sw_params_set_tstamp_type(m_pcm, sw, SND_PCM_TSTAMP_TYPE_MONOTONIC) == 0
		    && snd_pcm_sw_params(m_pcm, sw) == 0)
			m_timestamps = true;
		return true;
	}

	bool start(render_t render) override {
		if (!m_pcm) return false;
		m_running.store(true);
		m_thread = std::thread([this, render] { run(render); });
		return true;
	}

	void stop() override {
		m_running.store(false);
		if (m_thread.joinable()) m_thread.join();
		if (m_pcm) snd_pcm_drop(m_pcm);
		/* Said, not counted silently: an underrun is a dropout heard. */
		if (const int n = m_underruns.exchange(0))
			std::fprintf(stderr, "snurra: %s: %d underrun(s)\n", qPrintable(name()), n);
	}

	int rate() const override { return RATE; }
	int period() const override { return m_period; }
	int64_t latency_us() const override { return int64_t(m_buffer) * 1000000 / RATE; }
	QString name() const override { return QStringLiteral("alsa:") + m_device; }

private:
	static constexpr unsigned RATE = 48000;

	void run(const render_t &render) {
		/* Real-time if the system allows it; on as an ordinary thread if
		 * not, with more risk of an underrun under load. */
		sched_param param{};
		param.sched_priority = sched_get_priority_min(SCHED_FIFO) + 10;
		pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);

		std::vector<float> buffer(2 * size_t(m_period));
		while (m_running.load(std::memory_order_relaxed)) {
			const int64_t present = when_next_is_heard();
			std::fill(buffer.begin(), buffer.end(), 0.0f);
			render(buffer.data(), m_period, present);
			const float *at = buffer.data();
			snd_pcm_uframes_t left = snd_pcm_uframes_t(m_period);
			while (left > 0 && m_running.load(std::memory_order_relaxed)) {
				snd_pcm_sframes_t wrote = snd_pcm_writei(m_pcm, at, left);
				if (wrote < 0) {
					/* An underrun or a suspend: recovered, and the rest of
					 * this period dropped rather than played late. */
					m_underruns.fetch_add(1, std::memory_order_relaxed);
					m_predicted = 0;   /* the device restarts: measure afresh */
					if (snd_pcm_recover(m_pcm, int(wrote), 1) < 0) m_running.store(false);
					break;
				}
				at += 2 * wrote;
				left -= snd_pcm_uframes_t(wrote);
			}
		}
	}

	/*
	 * The shared-clock time the next frame written will be heard, filtered:
	 * the device's clock is steady, so each period is heard one period after
	 * the last, and a measurement only nudges that prediction (by an eighth)
	 * rather than replacing it. A single reading jitters by tens of
	 * microseconds -- through snd-aloop, two to three frames -- and a
	 * handover takes one reading from each side; filtered, the two agree.
	 * A reading far off the prediction (a start, an underrun, a suspend)
	 * is taken as it is.
	 */
	int64_t when_next_is_heard() {
		const double measured = double(measure_next_heard());
		if (m_predicted > 0) {
			const double error = measured - m_predicted;
			const double period_us = double(m_period) * 1e6 / RATE;
			if (std::abs(error) < 2 * period_us) m_predicted += error / 8;
			else m_predicted = measured;
		} else {
			m_predicted = measured;
		}
		const int64_t present = int64_t(std::llround(m_predicted));
		m_predicted += double(m_period) * 1e6 / RATE;   /* the period now being written */
		return present;
	}

	/* One reading, unfiltered. */
	int64_t measure_next_heard() {
		snd_pcm_uframes_t avail = 0;
		snd_htimestamp_t at{};
		if (m_timestamps && snd_pcm_state(m_pcm) == SND_PCM_STATE_RUNNING
		    && snd_pcm_htimestamp(m_pcm, &avail, &at) == 0 && (at.tv_sec || at.tv_nsec) && avail <= snd_pcm_uframes_t(m_buffer)) {
			const int64_t then = int64_t(at.tv_sec) * 1000000 + at.tv_nsec / 1000;
			return then + snurra_frames_to_us(int64_t(m_buffer) - int64_t(avail), int(RATE));
		}
		snd_pcm_sframes_t queued = 0;
		if (snd_pcm_delay(m_pcm, &queued) < 0 || queued < 0) queued = 0;
		return snurra_clock_us() + snurra_frames_to_us(queued, int(RATE));
	}

	QString m_device;
	bool m_timestamps = false;
	double m_predicted = 0;   /* when the next period is heard, filtered */
	snd_pcm_t *m_pcm = nullptr;
	int m_period = 0;
	int m_buffer = 0;
	std::atomic<bool> m_running{ false };
	std::atomic<int> m_underruns{ 0 };
	std::thread m_thread;
};

}

std::unique_ptr<snurra_output> snurra_open_output_alsa(const QString &device) {
	auto output = std::make_unique<alsa_output>(device);
	if (!output->open()) return nullptr;
	return output;
}
