#include "snurra/subtitles.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringDecoder>

#include <optional>

#include <algorithm>

void snurra_subtitles::add(snurra_cue cue) {
	std::lock_guard<std::mutex> lock(m_mutex);
	/* The open cue before this one ends where it begins. */
	auto after = m_cues.upper_bound(cue.start_us);
	auto before = after;
	while (before != m_cues.begin()) {
		--before;
		if (before->second.end_us == INT64_MAX && before->first < cue.start_us) {
			before->second.end_us = cue.start_us;
			break;
		}
		if (before->first < cue.start_us) break;
	}
	if (cue.empty()) return;
	auto [first, last] = m_cues.equal_range(cue.start_us);
	for (auto it = first; it != last; ++it) {
		const snurra_cue &here = it->second;
		if (here.lines == cue.lines && here.image == cue.image) {
			/* Read again: a later read may know its end. */
			if (here.end_us == INT64_MAX) it->second.end_us = cue.end_us;
			return;
		}
	}
	/* Open, and a cue after it already here: it ends there. */
	if (cue.end_us == INT64_MAX && after != m_cues.end()) cue.end_us = after->first;
	m_cues.emplace(cue.start_us, std::move(cue));
}

std::vector<snurra_cue> snurra_subtitles::at(int64_t us) const {
	std::vector<snurra_cue> showing;
	std::lock_guard<std::mutex> lock(m_mutex);
	/* Cues last seconds, not minutes: those that began in the last ten
	 * minutes are enough to look through. */
	constexpr int64_t LONGEST = 600 * int64_t(1000000);
	for (auto it = m_cues.lower_bound(us - LONGEST); it != m_cues.end() && it->first <= us; ++it)
		if (us < it->second.end_us) showing.push_back(it->second);
	return showing;
}

size_t snurra_subtitles::size() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_cues.size();
}

namespace {

/* Windows-1252's 0x80..0x9f; the rest is Latin-1. */
constexpr char16_t CP1252[32] = {
	0x20ac, 0xfffd, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021, 0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0xfffd, 0x017d, 0xfffd,
	0xfffd, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014, 0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0xfffd, 0x017e, 0x0178,
};

QString decode(const QByteArray &data) {
	const std::optional<QStringConverter::Encoding> marked = QStringConverter::encodingForData(data);
	if (marked && *marked != QStringConverter::Utf8) {
		QStringDecoder decoder(*marked);
		return decoder(data);
	}
	{
		QStringDecoder utf8(QStringConverter::Utf8);
		QString text = utf8(data);
		if (!utf8.hasError()) return text;
	}
	QString text;
	text.reserve(data.size());
	for (const char c : data) {
		const unsigned char byte = static_cast<unsigned char>(c);
		text.append(byte >= 0x80 && byte < 0xa0 ? QChar(CP1252[byte - 0x80]) : QChar(byte));
	}
	return text;
}

/* "01:02:03,456", "01:02:03.456" or "02:03,456": microseconds, or -1. */
int64_t srt_time(const QString &text) {
	static const QRegularExpression form(QStringLiteral("^\\s*(?:(\\d+):)?(\\d+):(\\d+)[,.](\\d{1,3})\\s*$"));
	const QRegularExpressionMatch match = form.match(text);
	if (!match.hasMatch()) return -1;
	const int64_t hours = match.captured(1).toLongLong();
	const int64_t minutes = match.captured(2).toLongLong();
	const int64_t seconds = match.captured(3).toLongLong();
	QString fraction = match.captured(4);
	while (fraction.size() < 3) fraction.append(QLatin1Char('0'));
	return (((hours * 60 + minutes) * 60 + seconds) * 1000 + fraction.toLongLong()) * 1000;
}

/* Appends `text` in the style given to the last line, joining a run of the
 * same style. */
void append(std::vector<std::vector<snurra_subtitle_span>> &lines, const QString &text, bool italic, bool bold) {
	if (text.isEmpty()) return;
	if (lines.empty()) lines.emplace_back();
	std::vector<snurra_subtitle_span> &line = lines.back();
	if (!line.empty() && line.back().italic == italic && line.back().bold == bold) line.back().text += text;
	else line.push_back({ text, italic, bold });
}

/* Lines with no text at all, at either end, are dropped; inner ones kept. */
void trim(std::vector<std::vector<snurra_subtitle_span>> &lines) {
	auto blank = [](const std::vector<snurra_subtitle_span> &line) {
		return std::all_of(line.begin(), line.end(), [](const snurra_subtitle_span &span) { return span.text.trimmed().isEmpty(); });
	};
	while (!lines.empty() && blank(lines.back())) lines.pop_back();
	while (!lines.empty() && blank(lines.front())) lines.erase(lines.begin());
}

const QString &entity(const QString &name) {
	static const QString amp = QStringLiteral("&"), lt = QStringLiteral("<"), gt = QStringLiteral(">"),
	                     quot = QStringLiteral("\""), nbsp = QStringLiteral(" "), none;
	if (name == QLatin1String("amp")) return amp;
	if (name == QLatin1String("lt")) return lt;
	if (name == QLatin1String("gt")) return gt;
	if (name == QLatin1String("quot")) return quot;
	if (name == QLatin1String("nbsp")) return nbsp;
	return none;
}

/* SubRip's text: <i> <b> kept, <u> <font> and any other tag dropped, an
 * ASS override ({\an8}) read for its place and dropped. */
std::vector<std::vector<snurra_subtitle_span>> srt_text(const QStringList &source, bool *top) {
	std::vector<std::vector<snurra_subtitle_span>> lines;
	bool italic = false;
	bool bold = false;
	for (const QString &line : source) {
		lines.emplace_back();
		QString run;
		for (qsizetype i = 0; i < line.size();) {
			const QChar c = line[i];
			if (c == QLatin1Char('<')) {
				const qsizetype close = line.indexOf(QLatin1Char('>'), i);
				if (close > i) {
					const QString tag = line.mid(i + 1, close - i - 1).trimmed().toLower();
					append(lines, run, italic, bold);
					run.clear();
					if (tag == QLatin1String("i")) italic = true;
					else if (tag == QLatin1String("/i")) italic = false;
					else if (tag == QLatin1String("b")) bold = true;
					else if (tag == QLatin1String("/b")) bold = false;
					i = close + 1;
					continue;
				}
			} else if (c == QLatin1Char('{') && line.mid(i, 2) == QLatin1String("{\\")) {
				const qsizetype close = line.indexOf(QLatin1Char('}'), i);
				if (close > i) {
					const QString tag = line.mid(i, close - i);
					if (top && (tag.contains(QLatin1String("\\an7")) || tag.contains(QLatin1String("\\an8"))
					            || tag.contains(QLatin1String("\\an9"))))
						*top = true;
					i = close + 1;
					continue;
				}
			} else if (c == QLatin1Char('&')) {
				const qsizetype close = line.indexOf(QLatin1Char(';'), i);
				if (close > i && close - i <= 6) {
					const QString &meant = entity(line.mid(i + 1, close - i - 1));
					if (!meant.isEmpty()) {
						run += meant;
						i = close + 1;
						continue;
					}
				}
			}
			run += c;
			++i;
		}
		append(lines, run, italic, bold);
	}
	trim(lines);
	return lines;
}

}

std::vector<snurra_cue> snurra_read_srt(const QByteArray &data) {
	QString text = decode(data);
	if (text.startsWith(QChar(0xfeff))) text.remove(0, 1);
	text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
	text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
	const QStringList all = text.split(QLatin1Char('\n'));

	std::vector<snurra_cue> cues;
	/* A cue is its timing line and the text after it, to a blank line; the
	 * number before the timing is not needed, and a file that leaves it
	 * out, or numbers wrongly, still reads. */
	for (qsizetype i = 0; i < all.size(); ++i) {
		const QString &line = all[i];
		const qsizetype arrow = line.indexOf(QLatin1String("-->"));
		if (arrow < 0) continue;
		const int64_t start = srt_time(line.left(arrow));
		/* Past the end time, a position (X1:...) some files carry. */
		QString end_text = line.mid(arrow + 3).trimmed();
		end_text = end_text.section(QLatin1Char(' '), 0, 0, QString::SectionSkipEmpty);
		const int64_t end = srt_time(end_text);
		if (start < 0 || end < 0) continue;
		QStringList body;
		while (i + 1 < all.size() && !all[i + 1].trimmed().isEmpty()) body << all[++i];
		snurra_cue cue;
		cue.start_us = start;
		cue.end_us = std::max(end, start);
		cue.lines = srt_text(body, &cue.top);
		if (!cue.lines.empty() && cue.end_us > cue.start_us) cues.push_back(std::move(cue));
	}
	std::stable_sort(cues.begin(), cues.end(), [](const snurra_cue &a, const snurra_cue &b) { return a.start_us < b.start_us; });
	return cues;
}

std::vector<std::vector<snurra_subtitle_span>> snurra_ass_text(const QString &dialogue, bool *top) {
	/* ReadOrder, Layer, Style, Name, MarginL, MarginR, MarginV, Effect, Text:
	 * the text is past the eighth comma, and may hold commas itself. */
	qsizetype from = 0;
	for (int comma = 0; comma < 8; ++comma) {
		const qsizetype at = dialogue.indexOf(QLatin1Char(','), from);
		if (at < 0) {
			from = 0;   /* not a dialogue line: all of it is text */
			break;
		}
		from = at + 1;
	}
	const QString text = dialogue.mid(from);

	std::vector<std::vector<snurra_subtitle_span>> lines(1);
	bool italic = false;
	bool bold = false;
	bool drawing = false;   /* {\p1}: vector drawing commands, not words */
	QString run;
	auto flush = [&] {
		if (!drawing) append(lines, run, italic, bold);
		run.clear();
	};
	/* One override tag: \i1, \b700, \p1, \an8 ... */
	auto apply = [&](const QString &tag) {
		auto number = [&tag](qsizetype at) {
			bool ok = false;
			const int value = tag.mid(at).toInt(&ok);
			return ok ? value : (tag.size() == at ? 0 : -1);
		};
		if (tag.startsWith(QLatin1String("an"))) {
			const int value = number(2);
			if (top && value >= 7 && value <= 9) *top = true;
		} else if (tag.startsWith(QLatin1Char('i')) && (tag.size() == 1 || tag[1].isDigit())) {
			italic = number(1) > 0;
		} else if (tag.startsWith(QLatin1Char('b')) && (tag.size() == 1 || tag[1].isDigit())) {
			bold = number(1) > 0;
		} else if (tag.startsWith(QLatin1Char('p')) && (tag.size() == 1 || tag[1].isDigit())) {
			drawing = number(1) > 0;
		} else if (tag == QLatin1String("r")) {
			italic = bold = false;
		}
	};
	for (qsizetype i = 0; i < text.size(); ++i) {
		const QChar c = text[i];
		if (c == QLatin1Char('{')) {
			const qsizetype close = text.indexOf(QLatin1Char('}'), i);
			if (close < 0) break;
			flush();
			const QString block = text.mid(i + 1, close - i - 1);
			for (const QString &tag : block.split(QLatin1Char('\\'), Qt::SkipEmptyParts)) apply(tag.trimmed());
			i = close;
		} else if (c == QLatin1Char('\\') && i + 1 < text.size()
		           && (text[i + 1] == QLatin1Char('N') || text[i + 1] == QLatin1Char('n'))) {
			flush();
			lines.emplace_back();
			++i;
		} else if (c == QLatin1Char('\\') && i + 1 < text.size() && text[i + 1] == QLatin1Char('h')) {
			run += QChar(0x00a0);
			++i;
		} else {
			run += c;
		}
	}
	flush();
	trim(lines);
	return lines;
}

QStringList snurra_srt_beside(const QString &video) {
	const QFileInfo info(video);
	const QDir dir = info.dir();
	const QString base = info.completeBaseName();
	QStringList found;
	const QStringList all = dir.entryList(QDir::Files, QDir::Name);
	for (const QString &name : all)
		if (name.compare(base + QStringLiteral(".srt"), Qt::CaseInsensitive) == 0) found << dir.filePath(name);
	for (const QString &name : all)
		if (name.startsWith(base + QLatin1Char('.'), Qt::CaseInsensitive) && name.endsWith(QLatin1String(".srt"), Qt::CaseInsensitive)
		    && name.compare(base + QStringLiteral(".srt"), Qt::CaseInsensitive) != 0)
			found << dir.filePath(name);
	return found;
}
