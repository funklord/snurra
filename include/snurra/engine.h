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
	/* Opens `output` in its place: a second engine on an output of its own
	 * -- a listener's headphones (project.md sec 3f). */
	snurra_engine(const QString &output, QObject *parent);
	~snurra_engine() override;

	snurra_deck *add(snurra_deck::start_t start);

	/* Out of the mix at `at_us` on the shared clock (now, when past), and
	 * deleted once the callback cannot be holding it. */
	void retire(snurra_deck *deck, int64_t at_us = 0);

	/* Decks still to retire or delete: the process must not end yet. */
	bool busy() const { return !m_retiring.isEmpty() || !m_graveyard.isEmpty(); }

	/* ---- the master (project.md sec 3f, e; any thread) ----
	 * What every deck's sum goes through on its way out: an EQ of three
	 * parts in dB (SNURRA_EQ_KILL a kill), a level, and a limiter that
	 * holds it under full scale -- on by default, touching nothing that
	 * was not over. Flat, at full and unlimited, the sum is untouched. */
	void set_master_level(float level) { m_master_level.store(level, std::memory_order_relaxed); }
	float master_level() const { return m_master_level.load(std::memory_order_relaxed); }
	void set_master_eq(int part, float db) {
		if (part >= 0 && part < 3) m_master_db[size_t(part)].store(db, std::memory_order_relaxed);
	}
	float master_eq(int part) const { return part >= 0 && part < 3 ? m_master_db[size_t(part)].load(std::memory_order_relaxed) : 0.0f; }
	void set_limiter(bool on) { m_limiter.store(on, std::memory_order_relaxed); }
	bool limiter() const { return m_limiter.load(std::memory_order_relaxed); }
	/* The loudest sample sent out, and the most the limiter took off (a
	 * gain, 1 for nothing), since asked last: for a meter. */
	float take_master_peak() { return m_master_peak.exchange(0.0f, std::memory_order_relaxed); }
	float take_master_reduction() { return m_master_reduction.exchange(1.0f, std::memory_order_relaxed); }

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
	/* The master: asked by any thread, applied by the callback. */
	std::atomic<float> m_master_level{ 1.0f };
	std::array<std::atomic<float>, 3> m_master_db{};
	std::atomic<bool> m_limiter{ true };
	std::atomic<float> m_master_peak{ 0.0f };
	std::atomic<float> m_master_reduction{ 1.0f };
	snurra_isolator m_master_eq;           /* the callback's */
	float m_master_gain[SNURRA_EQ_BANDS] = {};
	float m_master_now = 1.0f;             /* the level, eased */
	float m_limit_gain = 1.0f;             /* the limiter's, now */
	bool m_master_made = false;
	void master(float *out, int frames);
	std::unique_ptr<snurra_output> m_output;
	bool m_running = false;
	QList<snurra_deck *> m_unplaced;   /* no slot was free: silent, but a deck */
	QList<retiring_t> m_retiring;
	QList<buried_t> m_graveyard;
	QTimer *m_sweeper = nullptr;
};

#endif
