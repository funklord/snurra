#include "output.h"

#include <QFile>

#include <alsa/asoundlib.h>
#include <pthread.h>

#include <atomic>
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
 * speaker (snd_pcm_delay), which is when the new period will be heard.
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
			snd_pcm_sframes_t queued = 0;
			if (snd_pcm_delay(m_pcm, &queued) < 0 || queued < 0) queued = 0;
			const int64_t present = snurra_clock_us() + int64_t(queued) * 1000000 / RATE;
			std::fill(buffer.begin(), buffer.end(), 0.0f);
			render(buffer.data(), m_period, present);
			const float *at = buffer.data();
			snd_pcm_uframes_t left = snd_pcm_uframes_t(m_period);
			while (left > 0 && m_running.load(std::memory_order_relaxed)) {
				snd_pcm_sframes_t wrote = snd_pcm_writei(m_pcm, at, left);
				if (wrote < 0) {
					/* An underrun or a suspend: recovered, and the rest of
					 * this period dropped rather than played late. */
					if (snd_pcm_recover(m_pcm, int(wrote), 1) < 0) m_running.store(false);
					break;
				}
				at += 2 * wrote;
				left -= snd_pcm_uframes_t(wrote);
			}
		}
	}

	QString m_device;
	snd_pcm_t *m_pcm = nullptr;
	int m_period = 0;
	int m_buffer = 0;
	std::atomic<bool> m_running{ false };
	std::thread m_thread;
};

}

std::unique_ptr<snurra_output> snurra_open_output_alsa(const QString &device) {
	auto output = std::make_unique<alsa_output>(device);
	if (!output->open()) return nullptr;
	return output;
}
