#ifndef SNURRA_PEAKS_H
#define SNURRA_PEAKS_H

#include <QString>

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

class snurra_source;

/*
 * What a waveform draws (project.md sec 3c: a screen of tiles is a screen
 * of waveforms): the loudest sample of either channel in each of `columns`
 * equal stretches of a source, column c covering frames [c * length /
 * columns, (c + 1) * length / columns). Read from the start in order, and
 * handed to `piece` a run of columns at a time -- `first`, then the values
 * -- so a long file fills in left to right; `piece` returning false stops
 * it. Returns every column read.
 */
std::vector<float> snurra_peaks_of(snurra_source &source, int64_t length, int columns,
                                   const std::function<bool(int first, const std::vector<float> &values)> &piece);

/*
 * The same on a thread of its own, over a source of its own -- never the
 * deck's, whose reader must stay ahead of the output. `piece` is called on
 * that thread. Destroying it stops the work and waits for the thread.
 */
class snurra_peaks {
public:
	snurra_peaks(const QString &source, int rate, int columns,
	             std::function<void(int first, const std::vector<float> &values)> piece);
	~snurra_peaks();

private:
	std::atomic<bool> m_stop{ false };
	std::thread m_thread;
};

#endif
