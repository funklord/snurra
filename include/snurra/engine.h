#ifndef SNURRA_ENGINE_H
#define SNURRA_ENGINE_H

#include <QList>
#include <QObject>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

#include "snurra/deck.h"

class QTimer;
class snurra_output;

/*
 * The mixer (project.md sec 3a, Mixxx's EngineMixer): every deck summed
 * into one output, inside the output's callback. One per process; a tile
 * is a deck in it.
 *
 * Decks join and leave from the GUI thread through a fixed table of
 * atomic slots, so the callback never takes a lock. A deck that leaves is
 * deleted only once the callback has gone round twice without it -- and
 * may be told to leave at a time to come, which is how a handover's giver
 * keeps sounding through its fade after its tile has gone from the window.
 */
class snurra_engine : public QObject {
	Q_OBJECT

public:
	/* Opens $SNURRA_OUTPUT, or alsa:default, or null failing those. */
	explicit snurra_engine(QObject *parent = nullptr);
	~snurra_engine() override;

	snurra_deck *add(snurra_deck::start_t start);

	/* Out of the mix at `at_us` on the shared clock (now, when past), and
	 * deleted once the callback cannot be holding it. */
	void retire(snurra_deck *deck, int64_t at_us = 0);

	/* Decks still to retire or delete: the process must not end yet. */
	bool busy() const { return !m_retiring.isEmpty() || !m_graveyard.isEmpty(); }

	int rate() const;
	int period() const;
	int64_t latency_us() const;
	QString output_name() const;

signals:
	void idle();

private:
	struct retiring_t {
		snurra_deck *deck;
		int64_t at_us;
	};
	struct buried_t {
		snurra_deck *deck;
		uint64_t cycle;
	};

	void render(float *out, int frames, int64_t present_us);
	void sweep();

	static constexpr int SLOTS = 64;
	std::array<std::atomic<snurra_deck *>, SLOTS> m_slots{};
	std::atomic<uint64_t> m_cycles{ 0 };
	std::unique_ptr<snurra_output> m_output;
	bool m_running = false;
	QList<snurra_deck *> m_unplaced;   /* no slot was free: silent, but a deck */
	QList<retiring_t> m_retiring;
	QList<buried_t> m_graveyard;
	QTimer *m_sweeper = nullptr;
};

#endif
