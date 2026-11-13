#ifndef SNURRA_SUBTITLES_H
#define SNURRA_SUBTITLES_H

#include <QByteArray>
#include <QImage>
#include <QRectF>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <map>
#include <mutex>
#include <vector>

/* A run of subtitle text in one style. */
struct snurra_subtitle_span {
	QString text;
	bool italic = false;
	bool bold = false;
	bool operator==(const snurra_subtitle_span &other) const {
		return text == other.text && italic == other.italic && bold == other.bold;
	}
};

/*
 * One subtitle on screen from `start_us` until before `end_us`, in the
 * track's microseconds from the same zero as its sound and picture: text,
 * as lines of styled runs, or a picture (a Blu-ray's or a DVD's) placed at
 * `place`, fractions of the video's width and height.
 */
struct snurra_cue {
	int64_t start_us = 0;
	int64_t end_us = INT64_MAX;   /* INT64_MAX: until the next cue */
	std::vector<std::vector<snurra_subtitle_span>> lines;
	QImage image;
	QRectF place;
	bool top = false;   /* text asked to be at the top of the picture */

	bool empty() const { return lines.empty() && image.isNull(); }
};

/*
 * One track of subtitles: cues gathered as they are read -- all at once
 * from a file beside the video, or as the picture's reader passes them in
 * an embedded stream -- and asked for by time. Safe across threads.
 */
class snurra_subtitles {
public:
	explicit snurra_subtitles(const QString &name) : m_name(name) {}

	QString name() const { return m_name; }

	/* A cue read; one already here at the same start with the same content
	 * (read again after a seek back) is not doubled. A cue with no end
	 * ends where the next begins; an empty one only ends the one before. */
	void add(snurra_cue cue);
	/* The cues showing at `us`, earliest first. */
	std::vector<snurra_cue> at(int64_t us) const;
	size_t size() const;

private:
	QString m_name;
	mutable std::mutex m_mutex;
	std::multimap<int64_t, snurra_cue> m_cues;   /* by start */
};

/* A SubRip file's cues: UTF-8 (or UTF-16, by its byte order mark), else
 * Windows-1252, which is what older subtitle files are mostly in; <i> and
 * <b> kept, other tags dropped. */
std::vector<snurra_cue> snurra_read_srt(const QByteArray &data);

/* The text of an ASS dialogue, as FFmpeg's decoders give every text
 * subtitle (the fields before the text included): its lines, {\i1} and
 * {\b1} kept, other overrides and drawings dropped. */
std::vector<std::vector<snurra_subtitle_span>> snurra_ass_text(const QString &dialogue, bool *top = nullptr);

/* SubRip files beside a video, the one named like it first, then those
 * with a language between (film.en.srt), in name order. */
QStringList snurra_srt_beside(const QString &video);

#endif
