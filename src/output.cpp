#include "snurra/output.h"

#include <QCoreApplication>
#include <QDir>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <iterator>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

#include "snurra/clock.h"

namespace {

/*
 * An output with no device: a thread that renders a period each time the
 * clock reaches it, as a sound card's interrupt would. With a capture file
 * it writes each block -- in the render thread, which a real device must
 * never do; this is a test's instrument, not a device.
 */
class clocked_output : public snurra_output {
public:
	explicit clocked_output(const QString &capture) : m_capture(capture) {}
	~clocked_output() override { stop(); }

	bool start(render_t render) override {
		if (!m_capture.isEmpty()) {
			QDir().mkpath(m_capture);
			const QString path = QDir(m_capture).filePath(
			        QStringLiteral("%1.capture").arg(QCoreApplication::applicationPid()));
			m_file = std::fopen(QFile::encodeName(path).constData(), "wb");
			if (!m_file) return false;
		}
		m_running.store(true);
		m_thread = std::thread([this, render] { run(render); });
		return true;
	}

	void stop() override {
		m_running.store(false);
		if (m_thread.joinable()) m_thread.join();
		if (m_file) std::fclose(m_file);
		m_file = nullptr;
	}

	int rate() const override { return RATE; }
	int period() const override { return PERIOD; }
	int64_t latency_us() const override { return int64_t(PERIOD) * 1000000 / RATE; }
	QString name() const override { return m_capture.isEmpty() ? QStringLiteral("null") : QStringLiteral("capture"); }

private:
	static constexpr int RATE = 48000;
	static constexpr int PERIOD = 480;   /* 10 ms */
	static constexpr long CAPTURE_LIMIT = 32L * 1024 * 1024;

	void run(const render_t &render) {
		std::vector<float> buffer(2 * PERIOD);
		/*
		 * Each period's time is counted from the start, not measured, so
		 * the frames' times are exactly what a device at this rate would
		 * play them at; a late wake-up is caught up, not drifted.
		 */
		const int64_t origin = snurra_clock_us();
		for (int64_t n = 0; m_running.load(std::memory_order_relaxed); ++n) {
			const int64_t due = origin + n * PERIOD * 1000000 / RATE;
			const int64_t wait = due - snurra_clock_us();
			if (wait > 0) std::this_thread::sleep_for(std::chrono::microseconds(wait));
			std::fill(buffer.begin(), buffer.end(), 0.0f);
			const int64_t present = due + latency_us();
			render(buffer.data(), PERIOD, present);
			if (m_file && m_written < CAPTURE_LIMIT) {
				const int32_t frames = PERIOD;
				std::fwrite(&present, sizeof present, 1, m_file);
				std::fwrite(&frames, sizeof frames, 1, m_file);
				std::fwrite(buffer.data(), sizeof(float), buffer.size(), m_file);
				std::fflush(m_file);   /* a test reads it while this runs */
				m_written += long(sizeof present + sizeof frames + sizeof(float) * buffer.size());
			}
		}
	}

	QString m_capture;
	std::FILE *m_file = nullptr;
	long m_written = 0;
	std::atomic<bool> m_running{ false };
	std::thread m_thread;
};

}

int64_t snurra_heard_clock::next(int64_t measured, int frames, int rate) {
	const double period_us = double(frames) * 1e6 / rate;
	if (m_predicted > 0) {
		const double error = double(measured) - m_predicted;
		if (std::abs(error) < 2 * period_us) {
			m_errors[m_next] = error;
			m_next = (m_next + 1) % ERRORS;
			m_count = std::min(m_count + 1, ERRORS);
			double sorted[ERRORS];
			std::copy(m_errors, m_errors + m_count, sorted);
			std::nth_element(sorted, sorted + m_count / 2, sorted + m_count);
			m_predicted += sorted[m_count / 2] / 8;
			m_worst = std::max(m_worst, std::abs(error));
		} else {
			m_predicted = double(measured);
			m_count = 0;
		}
	} else {
		m_predicted = double(measured);
		m_count = 0;
	}
	const int64_t heard = int64_t(std::llround(m_predicted));
	m_predicted += period_us;   /* the frames now being written */
	return heard;
}

std::unique_ptr<snurra_output> snurra_open_output(const QString &spec) {
	if (spec == QLatin1String("null")) return std::make_unique<clocked_output>(QString());
	if (spec.startsWith(QLatin1String("capture:"))) return std::make_unique<clocked_output>(spec.mid(8));
#if SNURRA_ALSA
	if (spec.startsWith(QLatin1String("alsa:"))) return snurra_open_output_alsa(spec.mid(5));
#endif
#if SNURRA_AAUDIO
	if (spec == QLatin1String("aaudio")) return snurra_open_output_aaudio(false);
	if (spec == QLatin1String("aaudio:exclusive")) return snurra_open_output_aaudio(true);
	if (spec.startsWith(QLatin1String("aaudio:drop="))) return snurra_open_output_aaudio(false, spec.mid(12).toInt());
#endif
#if SNURRA_QT_AUDIO
	if (spec == QLatin1String("qt")) return snurra_open_output_qt(QString());
	if (spec.startsWith(QLatin1String("qt:"))) return snurra_open_output_qt(spec.mid(3));
#endif
	return nullptr;
}
