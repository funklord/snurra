#include "snurra/engine.h"

#include <QTimer>

#include <cstdio>

#include "snurra/clock.h"
#include "snurra/output.h"

snurra_engine::snurra_engine(QObject *parent) : QObject(parent) {
	const QByteArray wanted = qgetenv("SNURRA_OUTPUT");
	const QString spec = wanted.isEmpty() ? QStringLiteral("alsa:default") : QString::fromLocal8Bit(wanted);
	m_output = snurra_open_output(spec);
	if (!m_output) {
		std::fprintf(stderr, "snurra: no output from %s; playing to none\n", qPrintable(spec));
		m_output = snurra_open_output(QStringLiteral("null"));
	}
	m_running = m_output->start(
	        [this](float *out, int frames, int64_t present_us) { render(out, frames, present_us); });
	if (!m_running) {
		std::fprintf(stderr, "snurra: %s would not start; playing to none\n", qPrintable(m_output->name()));
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
	m_cycles.fetch_add(1, std::memory_order_release);
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
