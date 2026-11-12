#ifndef SNURRA_TEST_BYTES_H
#define SNURRA_TEST_BYTES_H

/* What the deck and embedding tests share: a transport whose arrived
 * ranges a test sets, and a WAV every frame of which differs. */

#include <QByteArray>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include "snurra/source.h"

namespace {

/* Bytes in memory, arriving in whatever ranges the test says. */
class memory_bytes : public snurra_bytes {
public:
	explicit memory_bytes(QByteArray data) : m_data(std::move(data)) {}

	int64_t size() const override { return m_data.size(); }

	int64_t read(int64_t offset, uint8_t *out, int64_t n) override {
		if (offset >= m_data.size()) return -1;
		std::lock_guard<std::mutex> lock(m_mutex);
		for (const auto &range : m_here) {
			if (offset >= range.first && offset < range.second) {
				const int64_t count = std::min(n, range.second - offset);
				std::memcpy(out, m_data.constData() + offset, size_t(count));
				return count;
			}
		}
		return 0;
	}

	bool has(int64_t offset, int64_t n) const override {
		std::lock_guard<std::mutex> lock(m_mutex);
		for (const auto &range : m_here)
			if (offset >= range.first && offset + n <= range.second) return true;
		return false;
	}

	void want(int64_t offset) override {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_wanted.push_back(offset);
	}

	void wait(int ms) override { std::this_thread::sleep_for(std::chrono::milliseconds(std::min(ms, 2))); }

	/* [from, to) is here now. Ranges are kept apart; reads look in each. */
	void arrive(int64_t from, int64_t to) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_here.push_back({ from, std::min<int64_t>(to, m_data.size()) });
	}

	std::vector<int64_t> wanted() const {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_wanted;
	}

private:
	QByteArray m_data;
	mutable std::mutex m_mutex;
	std::vector<std::pair<int64_t, int64_t>> m_here;
	std::vector<int64_t> m_wanted;
};

/* 48 kHz stereo 16-bit PCM in a WAV header. */
inline QByteArray wav_from(const QByteArray &pcm) {
	QByteArray wav;
	auto u32 = [&wav](quint32 v) { wav.append(reinterpret_cast<const char *>(&v), 4); };
	auto u16 = [&wav](quint16 v) { wav.append(reinterpret_cast<const char *>(&v), 2); };
	wav += "RIFF";
	u32(quint32(36 + pcm.size()));
	wav += "WAVEfmt ";
	u32(16);
	u16(1);
	u16(2);
	u32(48000);
	u32(48000 * 4);
	u16(4);
	u16(16);
	wav += "data";
	u32(quint32(pcm.size()));
	return wav + pcm;
}

/* A WAV whose every frame is different from its neighbours, so a sample
 * compared says which frame it is. */
inline QByteArray make_wav(int frames) {
	QByteArray pcm(frames * 4, Qt::Uninitialized);
	for (int64_t i = 0; i < frames; ++i) {
		/* 64-bit: in int these overflow past frame 271,000, and the
		 * optimiser's reading of that undefined loop wrote past the buffer. */
		const qint16 left = qint16((i * 7919) % 65536 - 32768);
		const qint16 right = qint16((i * 104729) % 65536 - 32768);
		std::memcpy(pcm.data() + 4 * i, &left, 2);
		std::memcpy(pcm.data() + 4 * i + 2, &right, 2);
	}
	return wav_from(pcm);
}

/* A WAV of `frames`, both channels at `level(i)` for frame i. */
template <typename level_t>
QByteArray make_wav_of(int frames, level_t level) {
	QByteArray pcm(frames * 4, Qt::Uninitialized);
	for (int i = 0; i < frames; ++i) {
		const qint16 v = qint16(std::clamp(level(i), -32768, 32767));
		std::memcpy(pcm.data() + 4 * int64_t(i), &v, 2);
		std::memcpy(pcm.data() + 4 * int64_t(i) + 2, &v, 2);
	}
	return wav_from(pcm);
}

}

#endif
