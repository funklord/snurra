#include "snurra/peaks.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

#include "snurra/source.h"
#include "snurra/beats.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

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

namespace {
/*
 * A file's waveform, kept beside its beat grid (beats.h): reading it
 * decodes the whole of the sound, and a three hour DJ set -- the holder's
 * own use, a downloaded .webm opened bare -- took some 100 s of a core
 * each time it was opened (2026-11-24), where its beats came back from
 * the cache at once. A file of its own a track, under the cache's
 * peaks/: a line with the file's stamp and the column count, then the
 * columns as 32-bit floats. Only for a file, or a lasting name, whose
 * stamp says when it has changed.
 */
QString kept_peaks_path(const QString &source, int audio_track) {
	return QFileInfo(snurra_kept_file()).absolutePath() + QStringLiteral("/peaks/") + snurra_kept_key(source, audio_track);
}

QByteArray kept_peaks_head(const QString &source, int columns) {
	return (snurra_kept_stamp(source) + QStringLiteral(" %1\n").arg(columns)).toUtf8();
}

bool keeps_peaks(const QString &source) {
	return QFileInfo(source).isFile() || snurra_name_lasts(source);
}

bool kept_peaks(const QString &source, int audio_track, int columns, std::vector<float> &peaks) {
	if (!keeps_peaks(source)) return false;
	QFile file(kept_peaks_path(source, audio_track));
	if (!file.open(QIODevice::ReadOnly)) return false;
	if (file.readLine(256) != kept_peaks_head(source, columns)) return false;
	const QByteArray body = file.readAll();
	if (body.size() != qsizetype(columns) * qsizetype(sizeof(float))) return false;
	peaks.resize(size_t(columns));
	std::memcpy(peaks.data(), body.constData(), size_t(body.size()));
	return true;
}

void keep_peaks(const QString &source, int audio_track, const std::vector<float> &peaks) {
	if (!keeps_peaks(source) || peaks.empty()) return;
	const QString path = kept_peaks_path(source, audio_track);
	QDir().mkpath(QFileInfo(path).absolutePath());
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly)) return;
	file.write(kept_peaks_head(source, int(peaks.size())));
	file.write(reinterpret_cast<const char *>(peaks.data()), qint64(peaks.size() * sizeof(float)));
	file.commit();
}
}

snurra_peaks::snurra_peaks(const QString &source, int rate, int columns,
                           std::function<void(int first, const std::vector<float> &values)> piece, int audio_track) {
	m_thread = std::thread([this, source, rate, columns, piece, audio_track] {
		std::vector<float> kept;
		if (kept_peaks(source, audio_track, columns, kept)) {
			if (!m_stop.load(std::memory_order_relaxed)) piece(0, kept);
			return;
		}
		std::unique_ptr<snurra_source> reader = snurra_open_source(source, rate, 0);
		if (!reader || reader->length() <= 0) return;
		if (audio_track >= 0 && reader->choose_audio_track(audio_track)) reader->seek(0);
		const std::vector<float> peaks = snurra_peaks_of(*reader, reader->length(), columns, [this, &piece](int first, const std::vector<float> &values) {
			if (m_stop.load(std::memory_order_relaxed)) return false;
			piece(first, values);
			return !m_stop.load(std::memory_order_relaxed);
		});
		/* Kept only whole: a read stopped, or given up waiting, leaves
		 * columns short, and is not the file's waveform. */
		if (int(peaks.size()) == columns && !m_stop.load(std::memory_order_relaxed))
			keep_peaks(source, audio_track, peaks);
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

snurra_peaks::snurra_peaks(std::function<std::unique_ptr<snurra_source>(std::function<bool()> stopped)> open, int columns,
                           std::function<void(int first, const std::vector<float> &values)> piece) {
	m_thread = std::thread([this, open, columns, piece] {
		auto stopped = [this] { return m_stop.load(std::memory_order_relaxed); };
		std::unique_ptr<snurra_source> reader = open(stopped);
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
