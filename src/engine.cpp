#include "snurra/engine.h"

#include <QTimer>

#include <cstdio>

#include "snurra/clock.h"
#include "snurra/output.h"

snurra_engine::snurra_engine(QObject *parent) : snurra_engine(QString::fromLocal8Bit(qgetenv("SNURRA_OUTPUT")), parent) {}

snurra_engine::snurra_engine(const QString &output, QObject *parent) : QObject(parent) {
	const QByteArray wanted = output.toLocal8Bit();
#ifdef Q_OS_ANDROID
	/* No ALSA for an app there: AAudio, and QAudioSink if it will not. */
	const QString fallback = QStringLiteral("aaudio");
	const QString second = QStringLiteral("qt");
#else
	const QString fallback = QStringLiteral("alsa:default");
	const QString second;
#endif
	const QString spec = wanted.isEmpty() ? fallback : QString::fromLocal8Bit(wanted);
	m_output = snurra_open_output(spec);
	if (!m_output && wanted.isEmpty() && !second.isEmpty()) {
		qWarning("snurra: no output from %s; trying %s", qPrintable(spec), qPrintable(second));
		m_output = snurra_open_output(second);
	}
	if (!m_output) {
		qWarning("snurra: no output from %s; playing to none", qPrintable(spec));
		m_output = snurra_open_output(QStringLiteral("null"));
	}
	m_running = m_output->start(
	        [this](float *out, int frames, int64_t present_us) { render(out, frames, present_us); });
	if (!m_running) {
		qWarning("snurra: %s would not start; playing to none", qPrintable(m_output->name()));
		m_output = snurra_open_output(QStringLiteral("null"));
		m_running = m_output->start(
		        [this](float *out, int frames, int64_t present_us) { render(out, frames, present_us); });
	}

	m_sweeper = new QTimer(this);
	m_sweeper->setInterval(10);
	connect(m_sweeper, &QTimer::timeout, this, &snurra_engine::sweep);
}

snurra_engine::~snurra_engine() {
	if (m_output) m_output->stop();
	for (std::atomic<snurra_deck *> &slot : m_slots) delete slot.exchange(nullptr);
	for (snurra_deck *deck : m_unplaced) delete deck;
	for (const buried_t &buried : m_graveyard) delete buried.deck;
}

int snurra_engine::rate() const {
	return m_output->rate();
}

int snurra_engine::period() const {
	return m_output->period();
}

int64_t snurra_engine::latency_us() const {
	return m_output->latency_us();
}

QString snurra_engine::output_name() const {
	return m_output->name();
}

snurra_deck *snurra_engine::add(snurra_deck::start_t start) {
	start.rate = rate();
	/* The first block is wanted by the time a period rendered now is
	 * heard, and a little after, for the reader to get there. */
	start.lead_us = latency_us() + int64_t(period()) * 1000000 / rate() + 20000;
	snurra_deck *deck = new snurra_deck(start);
	for (std::atomic<snurra_deck *> &slot : m_slots) {
		snurra_deck *empty = nullptr;
		if (slot.compare_exchange_strong(empty, deck, std::memory_order_release)) return deck;
	}
	m_unplaced << deck;
	return deck;
}

void snurra_engine::retire(snurra_deck *deck, int64_t at_us) {
	if (!deck) return;
	m_retiring << retiring_t{ deck, at_us };
	sweep();
	if (busy()) m_sweeper->start();
}

void snurra_engine::render(float *out, int frames, int64_t present_us) {
	for (std::atomic<snurra_deck *> &slot : m_slots)
		if (snurra_deck *deck = slot.load(std::memory_order_acquire)) deck->render_add(out, frames, present_us);
	master(out, frames);
	m_cycles.fetch_add(1, std::memory_order_release);
}

/*
 * The master section's sound (the holder, 2026-11-25: "just make it
 * contain the controls we need"): the sum through an EQ of three parts --
 * the decks' own isolator -- a level eased over some 10 ms so a turn does
 * not click, and a limiter. The limiter's ceiling is full scale: it takes
 * off at once what would go over and gives it back over some 100 ms, and
 * never touches a sum that stays under, so with it on by default a mix
 * that does not clip is sent out as it was. Flat and at full, the EQ and
 * the level are not run at all.
 */
void snurra_engine::master(float *out, int frames) {
	const int rate = m_output ? m_output->rate() : 48000;
	if (!m_master_made) {
		m_master_eq.make(rate, snurra_eq::THREE);
		m_master_made = true;
	}
	float db[3];
	bool flat = true;
	for (int part = 0; part < 3; ++part) {
		db[part] = m_master_db[size_t(part)].load(std::memory_order_relaxed);
		flat = flat && db[part] == 0;
	}
	if (!flat) {
		for (int band = 0; band < 3; ++band) m_master_gain[band] = db[band] <= SNURRA_EQ_KILL / 2 ? 0.0f : std::pow(10.0f, db[band] / 20);
		for (int i = 0; i < frames; ++i) {
			out[2 * i] = m_master_eq.run(out[2 * i], 0, m_master_gain);
			out[2 * i + 1] = m_master_eq.run(out[2 * i + 1], 1, m_master_gain);
		}
	}
	const float level = m_master_level.load(std::memory_order_relaxed);
	if (level != 1.0f || m_master_now != 1.0f) {
		const float most = 1.0f / (0.01f * float(rate));
		for (int i = 0; i < frames; ++i) {
			m_master_now += std::clamp(level - m_master_now, -most, most);
			out[2 * i] *= m_master_now;
			out[2 * i + 1] *= m_master_now;
		}
	}
	const bool limiting = m_limiter.load(std::memory_order_relaxed);
	const float release = 1.0f / (0.1f * float(rate));
	float peak = 0, least = 1;
	for (int i = 0; i < frames; ++i) {
		const float loudest = std::max(std::abs(out[2 * i]), std::abs(out[2 * i + 1]));
		if (limiting) {
			m_limit_gain = std::min(1.0f, m_limit_gain + release);
			if (loudest * m_limit_gain > 1.0f) m_limit_gain = 1.0f / loudest;
			out[2 * i] *= m_limit_gain;
			out[2 * i + 1] *= m_limit_gain;
			least = std::min(least, m_limit_gain);
		}
		peak = std::max(peak, loudest * (limiting ? m_limit_gain : 1.0f));
	}
	if (peak > m_master_peak.load(std::memory_order_relaxed)) m_master_peak.store(peak, std::memory_order_relaxed);
	if (least < m_master_reduction.load(std::memory_order_relaxed)) m_master_reduction.store(least, std::memory_order_relaxed);
}

void snurra_engine::sweep() {
	const int64_t now = snurra_clock_us();
	const uint64_t cycle = m_cycles.load(std::memory_order_acquire);
	for (int i = int(m_retiring.size()) - 1; i >= 0; --i) {
		const retiring_t retiring = m_retiring.at(i);
		if (retiring.at_us > now) continue;
		m_retiring.removeAt(i);
		if (m_unplaced.removeOne(retiring.deck)) {
			delete retiring.deck;
			continue;
		}
		for (std::atomic<snurra_deck *> &slot : m_slots) {
			snurra_deck *expected = retiring.deck;
			if (slot.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel)) break;
		}
		m_graveyard << buried_t{ retiring.deck, cycle };
	}
	/*
	 * The callback that was running when a slot was cleared may still
	 * hold its deck; once the count has moved on by two, no callback can.
	 * A stopped output holds nothing.
	 */
	for (int i = int(m_graveyard.size()) - 1; i >= 0; --i) {
		if (m_running && cycle < m_graveyard.at(i).cycle + 2) continue;
		delete m_graveyard.at(i).deck;
		m_graveyard.removeAt(i);
	}
	if (!busy() && m_sweeper->isActive()) {
		m_sweeper->stop();
		emit idle();
	}
}
