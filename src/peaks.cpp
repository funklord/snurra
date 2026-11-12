#include "snurra/peaks.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include "snurra/source.h"

std::vector<float> snurra_peaks_of(snurra_source &source, int64_t length, int columns,
                                   const std::function<bool(int first, const std::vector<float> &values)> &piece) {
	std::vector<float> peaks;
	if (length <= 0 || columns <= 0) return peaks;
	peaks.reserve(size_t(columns));
	constexpr int BLOCK = 4096;

	std::vector<float> block(2 * BLOCK);
	source.seek(0);
	int column = 0;
	int run_from = 0;
	float loudest = 0;
	int64_t frame = 0;
	auto column_end = [&](int c) { return (int64_t(c) + 1) * length / columns; };
	while (column < columns) {
		/* A source still arriving waits inside its read; nothing read and
		 * not at the end is that wait given up -- stopped. */
		const int n = source.read(block.data(), BLOCK);
		if (n <= 0 && !source.at_end()) break;
		for (int i = 0; i < n && column < columns; ++i, ++frame) {
			while (column < columns && frame >= column_end(column)) {
				peaks.push_back(loudest);
				loudest = 0;
				++column;
			}
			loudest = std::max({ loudest, std::abs(block[2 * size_t(i)]), std::abs(block[2 * size_t(i) + 1]) });
		}
		/* At the end, what is left: the last column, and any the source
		 * was shorter than its length said. */
		if (n <= 0) {
			while (column < columns) {
				peaks.push_back(loudest);
				loudest = 0;
				++column;
			}
		}
		/* Whatever a read finished, at once: the next read may wait for
		 * bytes, and the columns before the wait are to be seen during it.
		 * At most one piece a column. */
		if (column > run_from) {
			const std::vector<float> run(peaks.begin() + run_from, peaks.begin() + column);
			if (!run.empty() && !piece(run_from, run)) break;
			run_from = column;
		}
	}
	return peaks;
}

snurra_peaks::snurra_peaks(const QString &source, int rate, int columns,
                           std::function<void(int first, const std::vector<float> &values)> piece) {
	m_thread = std::thread([this, source, rate, columns, piece] {
		std::unique_ptr<snurra_source> reader = snurra_open_source(source, rate, 0);
		if (!reader || reader->length() <= 0) return;
		snurra_peaks_of(*reader, reader->length(), columns, [this, &piece](int first, const std::vector<float> &values) {
			if (m_stop.load(std::memory_order_relaxed)) return false;
			piece(first, values);
			return !m_stop.load(std::memory_order_relaxed);
		});
	});
}

snurra_peaks::snurra_peaks(std::shared_ptr<snurra_bytes> bytes, const QString &name, int rate, int columns,
                           std::function<void(int first, const std::vector<float> &values)> piece) {
	m_thread = std::thread([this, bytes, name, rate, columns, piece] {
		auto stopped = [this] { return m_stop.load(std::memory_order_relaxed); };
		std::unique_ptr<snurra_source> reader = snurra_open_source(bytes, name, rate, stopped);
		if (!reader || reader->length() <= 0) return;
		snurra_peaks_of(*reader, reader->length(), columns, [&stopped, &piece](int first, const std::vector<float> &values) {
			if (stopped()) return false;
			piece(first, values);
			return !stopped();
		});
	});
}

snurra_peaks::~snurra_peaks() {
	m_stop.store(true);
	if (m_thread.joinable()) m_thread.join();
}
