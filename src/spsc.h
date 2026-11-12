#ifndef SNURRA_SPSC_H
#define SNURRA_SPSC_H

#include <atomic>
#include <cstddef>

/*
 * A queue for one writer thread and one reader thread, of fixed size, that
 * neither locks nor allocates: what the audio callback may use (project.md
 * sec 3a). Holds up to N - 1 items.
 */
template <typename T, size_t N>
class snurra_spsc {
public:
	bool push(const T &item) {
		const size_t tail = m_tail.load(std::memory_order_relaxed);
		const size_t next = (tail + 1) % N;
		if (next == m_head.load(std::memory_order_acquire)) return false;
		m_items[tail] = item;
		m_tail.store(next, std::memory_order_release);
		return true;
	}

	bool pop(T &item) {
		const size_t head = m_head.load(std::memory_order_relaxed);
		if (head == m_tail.load(std::memory_order_acquire)) return false;
		item = m_items[head];
		m_head.store((head + 1) % N, std::memory_order_release);
		return true;
	}

	/* Reader side only: the next item without taking it. */
	bool peek(T &item) const {
		const size_t head = m_head.load(std::memory_order_relaxed);
		if (head == m_tail.load(std::memory_order_acquire)) return false;
		item = m_items[head];
		return true;
	}

private:
	T m_items[N];
	std::atomic<size_t> m_head{ 0 };
	std::atomic<size_t> m_tail{ 0 };
};

#endif
