#include "snurra/output.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>
#include <QThread>

#include <algorithm>

#include "snurra/clock.h"

namespace {

/*
 * Qt Multimedia's QAudioSink (project.md sec 3a): the plain fallback, for
 * a program that wants nothing beyond Qt and does not need low latency --
 * a media tile playing a file, not a mix -- and what Android has until
 * AAudio is written. Pull mode: the sink reads from a device that renders
 * the mix as it is asked, on a thread of the sink's own, since a pull sink
 * is driven from its thread's event loop.
 *
 * When a pull is heard is what Qt still holds ahead of it -- its buffer,
 * less what is free -- filtered as every output's is (snurra_heard_clock).
 * What the platform holds below Qt (Android's mixer, PulseAudio) is not
 * Qt's to report, so this output plays late by that; that is the trade
 * sec 3a names for it.
 */
class pull : public QIODevice {
public:
	pull(snurra_output::render_t render, int rate) : m_render(std::move(render)), m_rate(rate) {}

	QAudioSink *sink = nullptr;

	bool isSequential() const override { return true; }
	qint64 bytesAvailable() const override { return std::numeric_limits<qint32>::max(); }

protected:
	qint64 readData(char *data, qint64 bytes) override {
		const int frames = int(bytes / 8);
		if (frames <= 0) return 0;
		const qint64 held = sink ? qMax<qint64>(0, sink->bufferSize() - sink->bytesFree()) : 0;
		const int64_t heard = m_clock.next(snurra_clock_us() + snurra_frames_to_us(held / 8, m_rate), frames, m_rate);
		float *out = reinterpret_cast<float *>(data);
		std::fill(out, out + 2 * frames, 0.0f);
		m_render(out, frames, heard);
		return qint64(frames) * 8;
	}

	qint64 writeData(const char *, qint64) override { return -1; }

private:
	snurra_output::render_t m_render;
	int m_rate;
	snurra_heard_clock m_clock;
};

class qt_output : public snurra_output {
public:
	explicit qt_output(const QString &device) : m_wanted(device) {}
	~qt_output() override { stop(); }

	bool open() {
		m_device = QMediaDevices::defaultAudioOutput();
		for (const QAudioDevice &device : QMediaDevices::audioOutputs())
			if (!m_wanted.isEmpty() && device.description() == m_wanted) m_device = device;
		if (m_device.isNull()) return false;
		m_format.setChannelCount(2);
		m_format.setSampleFormat(QAudioFormat::Float);
		m_format.setSampleRate(48000);
		if (!m_device.isFormatSupported(m_format)) m_format.setSampleRate(m_device.preferredFormat().sampleRate());
		return m_device.isFormatSupported(m_format);
	}

	bool start(render_t render) override {
		m_thread.start();
		m_pull = new pull(std::move(render), m_format.sampleRate());
		m_pull->moveToThread(&m_thread);
		bool started = false;
		QMetaObject::invokeMethod(
		        m_pull,
		        [this, &started] {
			        QAudioSink *sink = new QAudioSink(m_device, m_format);
			        sink->setBufferSize(m_format.bytesForDuration(40000));   /* 40 ms, asked for */
			        m_pull->sink = sink;
			        m_pull->open(QIODevice::ReadOnly);
			        sink->start(m_pull);
			        started = sink->error() == QAudio::NoError;
			        m_buffer = int(sink->bufferSize() / 8);
		        },
		        Qt::BlockingQueuedConnection);
		return started;
	}

	void stop() override {
		if (!m_thread.isRunning()) return;
		QMetaObject::invokeMethod(
		        m_pull,
		        [this] {
			        if (m_pull->sink) {
				        m_pull->sink->stop();
				        delete m_pull->sink;
				        m_pull->sink = nullptr;
			        }
			        delete m_pull;
		        },
		        Qt::BlockingQueuedConnection);
		m_pull = nullptr;
		m_thread.quit();
		m_thread.wait();
	}

	int rate() const override { return m_format.sampleRate(); }
	int period() const override { return std::max(1, m_buffer / 4); }
	int64_t latency_us() const override { return snurra_frames_to_us(m_buffer, rate()); }
	QString name() const override { return QStringLiteral("qt:") + m_device.description(); }

private:
	QString m_wanted;
	QAudioDevice m_device;
	QAudioFormat m_format;
	QThread m_thread;
	pull *m_pull = nullptr;
	int m_buffer = 0;
};

}

std::unique_ptr<snurra_output> snurra_open_output_qt(const QString &device) {
	auto output = std::make_unique<qt_output>(device);
	if (!output->open()) return nullptr;
	return output;
}
