/*
 * Subtitles (the holder: "yes, subtitles too, embedded and .srt"): a
 * SubRip file read as the files people actually have are written -- CRLF,
 * a byte order mark, Windows-1252, numbers missing, tags -- the text
 * FFmpeg's decoders give for an embedded stream, the cues of a track by
 * time, an embedded stream read as the picture passes it, and in a tile:
 * the words of the time heard on screen, v hiding them and j taking the
 * next track.
 */
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include "snurra/clock.h"
#include "snurra/deck.h"
#include "snurra/embed.h"
#include "snurra/node.h"
#include "snurra/subtitles.h"
#include "snurra/tile.h"
#include "snurra/video.h"
#include "test_bytes.h"

namespace {

constexpr int64_t SECOND = 1000000;

QString words(const snurra_cue &cue) {
	QStringList lines;
	for (const auto &line : cue.lines) {
		QString text;
		for (const snurra_subtitle_span &span : line) text += span.text;
		lines << text;
	}
	return lines.join(QLatin1Char('|'));
}

QString words_at(const snurra_subtitles &track, int64_t us) {
	QStringList all;
	for (const snurra_cue &cue : track.at(us)) all << words(cue);
	return all.join(QLatin1Char('+'));
}

/* Two cues, over a second and a half each: "one" from 0.5 s, "two" from
 * 2 s. */
const char *const SRT = "1\n00:00:00,500 --> 00:00:01,500\none\n\n2\n00:00:02,000 --> 00:00:02,800\ntwo\n";

}

class subtitles_test : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void a_srt_reads_as_people_write_them();
	void a_srt_not_in_utf8_is_windows_1252();
	void a_srt_keeps_italic_and_bold_and_drops_other_tags();
	void ass_text_is_its_words_and_styles();
	void a_track_shows_the_cues_of_a_time();
	void a_cue_without_an_end_ends_at_the_next();
	void a_cue_read_again_is_not_doubled();
	void the_srt_beside_a_video_is_found();
	void an_embedded_stream_is_read_as_the_picture_passes();
	void a_blu_ray_picture_subtitle_is_shown_where_it_was_placed();
	void a_seek_into_a_line_shows_that_line();
	void a_tile_shows_the_words_heard_and_v_and_j_change_them();
	void a_hosts_subtitle_file_comes_first_for_its_bytes();

private:
	bool make_video(const QString &name, const QStringList &more);
	QTemporaryDir m_dir;
	QString m_ffmpeg;
};

void subtitles_test::initTestCase() {
	QVERIFY(m_dir.isValid());
	m_ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
	qputenv("SNURRA_OUTPUT", "null");
}

void subtitles_test::a_srt_reads_as_people_write_them() {
	/* A byte order mark, CRLF, a cue with no number, one with a period
	 * for the comma and a position after its end, a missing hour, and two
	 * lines of text. Out of order in the file, in order out. */
	const QByteArray data = "\xef\xbb\xbf"
	                        "1\r\n00:00:01,000 --> 00:00:02,500\r\nfirst line\r\nsecond line\r\n\r\n"
	                        "00:00:05.250 --> 00:00:06.000 X1:10 X2:20 Y1:5 Y2:9\r\nno number\r\n\r\n"
	                        "3\r\n00:03,000 --> 00:04,000\r\nno hour\r\n\r\n"
	                        "4\r\n01:00:00,000 --> 01:00:01,000\r\nan hour in\r\n";
	const std::vector<snurra_cue> cues = snurra_read_srt(data);
	QCOMPARE(cues.size(), size_t(4));
	QCOMPARE(cues[0].start_us, SECOND);
	QCOMPARE(cues[0].end_us, 2500000);
	QCOMPARE(words(cues[0]), QStringLiteral("first line|second line"));
	QCOMPARE(cues[1].start_us, 3 * SECOND);
	QCOMPARE(words(cues[1]), QStringLiteral("no hour"));
	QCOMPARE(cues[2].start_us, 5250000);
	QCOMPARE(cues[2].end_us, 6 * SECOND);
	QCOMPARE(words(cues[2]), QStringLiteral("no number"));
	QCOMPARE(cues[3].start_us, 3600 * SECOND);
}

void subtitles_test::a_srt_not_in_utf8_is_windows_1252() {
	/* "cafe" with an e acute as 0xe9 and a right quote as 0x92: not UTF-8. */
	const QByteArray latin = "1\n00:00:01,000 --> 00:00:02,000\ncaf\xe9 isn\x92t\n";
	const std::vector<snurra_cue> cues = snurra_read_srt(latin);
	QCOMPARE(cues.size(), size_t(1));
	QCOMPARE(words(cues[0]), QStringLiteral("café isn’t"));
	/* The same words in UTF-8 are read as UTF-8. */
	const std::vector<snurra_cue> utf8 = snurra_read_srt(QByteArray("1\n00:00:01,000 --> 00:00:02,000\ncaf\xc3\xa9\n"));
	QCOMPARE(words(utf8[0]), QStringLiteral("café"));
}

void subtitles_test::a_srt_keeps_italic_and_bold_and_drops_other_tags() {
	const QByteArray data = "1\n00:00:01,000 --> 00:00:02,000\n{\\an8}<i>Inside</i> <font color=\"#ffff00\">yellow</font> "
	                        "<b>loud</b> &amp; <u>under</u>\n";
	const std::vector<snurra_cue> cues = snurra_read_srt(data);
	QCOMPARE(cues.size(), size_t(1));
	QVERIFY(cues[0].top);
	QCOMPARE(words(cues[0]), QStringLiteral("Inside yellow loud & under"));
	const auto &line = cues[0].lines[0];
	QCOMPARE(line.front().text, QStringLiteral("Inside"));
	QVERIFY(line.front().italic);
	bool bold = false;
	for (const snurra_subtitle_span &span : line)
		if (span.text == QLatin1String("loud")) bold = span.bold && !span.italic;
	QVERIFY(bold);
}

void subtitles_test::ass_text_is_its_words_and_styles() {
	bool top = false;
	const auto lines = snurra_ass_text(QStringLiteral("0,0,Default,,0,0,0,,Hello, {\\i1}world{\\i0}\\Nnext {\\b1}line"), &top);
	QVERIFY(!top);
	QCOMPARE(lines.size(), size_t(2));
	QCOMPARE(lines[0].size(), size_t(2));
	QCOMPARE(lines[0][0].text, QStringLiteral("Hello, "));
	QCOMPARE(lines[0][1].text, QStringLiteral("world"));
	QVERIFY(lines[0][1].italic);
	QCOMPARE(lines[1][1].text, QStringLiteral("line"));
	QVERIFY(lines[1][1].bold);
	/* A drawing is not words; a position at the top is read. */
	const auto drawn = snurra_ass_text(QStringLiteral("1,0,Sign,,0,0,0,,{\\an8\\p1}m 0 0 l 10 0 10 10{\\p0}Sign"), &top);
	QVERIFY(top);
	QCOMPARE(drawn.size(), size_t(1));
	QCOMPARE(drawn[0][0].text, QStringLiteral("Sign"));
}

void subtitles_test::a_track_shows_the_cues_of_a_time() {
	snurra_subtitles track(QStringLiteral("test"));
	for (snurra_cue &cue : snurra_read_srt(SRT)) track.add(std::move(cue));
	QCOMPARE(words_at(track, 0), QString());
	QCOMPARE(words_at(track, 499999), QString());
	QCOMPARE(words_at(track, 500000), QStringLiteral("one"));
	QCOMPARE(words_at(track, 1499999), QStringLiteral("one"));
	QCOMPARE(words_at(track, 1500000), QString());
	QCOMPARE(words_at(track, 2400000), QStringLiteral("two"));
	QCOMPARE(words_at(track, 3 * SECOND), QString());
	/* Overlapping cues both show, earliest first. */
	snurra_cue over;
	over.start_us = 1 * SECOND;
	over.end_us = 2500000;
	over.lines = { { snurra_subtitle_span{ QStringLiteral("over") } } };
	track.add(over);
	QCOMPARE(words_at(track, 1200000), QStringLiteral("one+over"));
}

void subtitles_test::a_cue_without_an_end_ends_at_the_next() {
	snurra_subtitles track(QStringLiteral("blu-ray"));
	snurra_cue first;
	first.start_us = SECOND;
	first.lines = { { snurra_subtitle_span{ QStringLiteral("first") } } };
	track.add(first);
	QCOMPARE(words_at(track, 100 * SECOND), QStringLiteral("first"));
	snurra_cue clear;   /* an empty one: the one before ends */
	clear.start_us = 3 * SECOND;
	track.add(clear);
	QCOMPARE(words_at(track, 2 * SECOND), QStringLiteral("first"));
	QCOMPARE(words_at(track, 3 * SECOND), QString());
	QCOMPARE(track.size(), size_t(1));
	/* An open cue added before one already here ends at that one. */
	snurra_cue later;
	later.start_us = 10 * SECOND;
	later.end_us = 11 * SECOND;
	later.lines = { { snurra_subtitle_span{ QStringLiteral("later") } } };
	track.add(later);
	snurra_cue between;
	between.start_us = 8 * SECOND;
	between.lines = { { snurra_subtitle_span{ QStringLiteral("between") } } };
	track.add(between);
	QCOMPARE(words_at(track, 9 * SECOND), QStringLiteral("between"));
	QCOMPARE(words_at(track, 10 * SECOND), QStringLiteral("later"));
}

void subtitles_test::a_cue_read_again_is_not_doubled() {
	snurra_subtitles track(QStringLiteral("again"));
	for (int pass = 0; pass < 3; ++pass)
		for (snurra_cue &cue : snurra_read_srt(SRT)) track.add(std::move(cue));
	QCOMPARE(track.size(), size_t(2));
	QCOMPARE(words_at(track, SECOND), QStringLiteral("one"));
}

void subtitles_test::the_srt_beside_a_video_is_found() {
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	for (const char *name : { "film.mkv", "film.srt", "film.en.srt", "film.sv.SRT", "other.srt", "film2.srt" }) {
		QFile file(dir.filePath(QString::fromLatin1(name)));
		QVERIFY(file.open(QIODevice::WriteOnly));
	}
	const QStringList found = snurra_srt_beside(dir.filePath(QStringLiteral("film.mkv")));
	QCOMPARE(found, (QStringList{ dir.filePath(QStringLiteral("film.srt")), dir.filePath(QStringLiteral("film.en.srt")),
	                              dir.filePath(QStringLiteral("film.sv.SRT")) }));
}

/* Three seconds of picture and tone, 64x48, with `more` arguments for
 * the subtitle inputs and codecs. */
bool subtitles_test::make_video(const QString &name, const QStringList &more) {
	QStringList args{ QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-y"),
		          QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("testsrc=s=64x48:r=25:d=3"),
		          QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("sine=f=440:d=3") };
	args << more << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p") << m_dir.filePath(name);
	QProcess ffmpeg;
	ffmpeg.start(m_ffmpeg, args);
	return ffmpeg.waitForFinished(60000) && ffmpeg.exitCode() == 0;
}

void subtitles_test::an_embedded_stream_is_read_as_the_picture_passes() {
	if (m_ffmpeg.isEmpty()) QSKIP("no ffmpeg");
	QFile srt(m_dir.filePath(QStringLiteral("embed.srt")));
	QVERIFY(srt.open(QIODevice::WriteOnly));
	srt.write("1\n00:00:00,500 --> 00:00:01,500\n<i>one</i>\n\n2\n00:00:02,000 --> 00:00:02,800\ntwo\n");
	srt.close();
	struct kind {
		QString name;
		QStringList codec;
	};
	/* Matroska with SubRip and with ASS, MP4 with its own text. */
	const std::vector<kind> kinds{
		{ QStringLiteral("subrip.mkv"), { QStringLiteral("-c:s"), QStringLiteral("srt") } },
		{ QStringLiteral("ass.mkv"), { QStringLiteral("-c:s"), QStringLiteral("ass") } },
		{ QStringLiteral("text.mp4"), { QStringLiteral("-c:s"), QStringLiteral("mov_text") } },
	};
	for (const kind &k : kinds) {
		QVERIFY2(make_video(k.name, QStringList{ QStringLiteral("-i"), srt.fileName(), QStringLiteral("-map"), QStringLiteral("0:v"),
		                                         QStringLiteral("-map"), QStringLiteral("1:a"), QStringLiteral("-map"),
		                                         QStringLiteral("2:s"), QStringLiteral("-metadata:s:s:0"),
		                                         QStringLiteral("language=swe") }
		                                        << k.codec),
		         qPrintable(k.name));
		snurra_video video(m_dir.filePath(k.name));
		QTRY_VERIFY_WITH_TIMEOUT(video.opened(), 5000);
		QVERIFY(video.has_video());
		const auto tracks = video.subtitle_tracks();
		QCOMPARE(tracks.size(), size_t(1));
		QVERIFY2(tracks[0]->name().contains(QLatin1String("swe")), qPrintable(tracks[0]->name()));
		/* Walk the picture through the file, as a screen would. */
		for (int64_t t = 0; t <= 3 * SECOND; t += 40000) {
			video.frame_at(t);
			QTest::qWait(2);
		}
		QTRY_COMPARE_WITH_TIMEOUT(tracks[0]->size(), size_t(2), 3000);
		QCOMPARE(words_at(*tracks[0], SECOND), QStringLiteral("one"));
		QVERIFY2(tracks[0]->at(SECOND).front().lines[0][0].italic, qPrintable(k.name));
		QCOMPARE(words_at(*tracks[0], 2400000), QStringLiteral("two"));
		QCOMPARE(words_at(*tracks[0], 1700000), QString());
		QCOMPARE(words_at(*tracks[0], 2900000), QString());
	}
}

namespace {

/*
 * A Blu-ray subtitle stream (PGS, a .sup), written by hand since FFmpeg
 * cannot encode text into pictures: on a 64x48 frame, a white box 48x10
 * at (8, 30) from 1 s, cleared at 2 s. Each segment: "PG", its time at
 * 90 kHz twice, its kind, its length, then what it says.
 */
QByteArray pgs_stream() {
	QByteArray out;
	auto u8 = [](QByteArray &to, int v) { to.append(char(v & 0xff)); };
	auto u16 = [&u8](QByteArray &to, int v) {
		u8(to, v >> 8);
		u8(to, v);
	};
	auto segment = [&](uint32_t pts, int kind, const QByteArray &body) {
		out.append("PG");
		for (int shift = 24; shift >= 0; shift -= 8) u8(out, int(pts >> shift));
		for (int i = 0; i < 4; ++i) u8(out, 0);
		u8(out, kind);
		u16(out, int(body.size()));
		out.append(body);
	};
	auto composition = [&](uint32_t pts, int number, bool shown) {
		QByteArray pcs;
		u16(pcs, 64);
		u16(pcs, 48);
		u8(pcs, 0x10);                  /* frame rate */
		u16(pcs, number);
		u8(pcs, shown ? 0x80 : 0x00);   /* an epoch's start, or normal */
		u8(pcs, 0);                     /* no palette-only update */
		u8(pcs, 0);                     /* palette 0 */
		u8(pcs, shown ? 1 : 0);         /* objects shown */
		if (shown) {
			u16(pcs, 0);   /* object 0 */
			u8(pcs, 0);    /* window 0 */
			u8(pcs, 0);    /* not cropped */
			u16(pcs, 8);
			u16(pcs, 30);
		}
		segment(pts, 0x16, pcs);
		QByteArray wds;
		u8(wds, 1);
		u8(wds, 0);
		u16(wds, 8);
		u16(wds, 30);
		u16(wds, 48);
		u16(wds, 10);
		segment(pts, 0x17, wds);
	};
	constexpr uint32_t SHOW = 90000;
	constexpr uint32_t CLEAR = 180000;
	/* Nothing at 0: ffmpeg starts each input at zero, and would move the
	 * box there. */
	composition(0, 0, false);
	segment(0, 0x80, QByteArray());
	composition(SHOW, 1, true);
	QByteArray pds;
	u8(pds, 0);   /* palette 0, version 0 */
	u8(pds, 0);
	for (int v : { 1, 235, 128, 128, 255 }) u8(pds, v);   /* entry 1: white, opaque */
	segment(SHOW, 0x14, pds);
	QByteArray rle;
	for (int y = 0; y < 10; ++y) {
		/* 48 pixels of colour 1, then the line's end. */
		for (int v : { 0x00, 0x80 | 48, 0x01, 0x00, 0x00 }) u8(rle, v);
	}
	QByteArray ods;
	u16(ods, 0);    /* object 0 */
	u8(ods, 0);     /* version */
	u8(ods, 0xc0);  /* first and last of it */
	const int length = int(rle.size()) + 4;
	u8(ods, length >> 16);
	u16(ods, length);
	u16(ods, 48);
	u16(ods, 10);
	ods.append(rle);
	segment(SHOW, 0x15, ods);
	segment(SHOW, 0x80, QByteArray());
	composition(CLEAR, 2, false);
	segment(CLEAR, 0x80, QByteArray());
	return out;
}

}

void subtitles_test::a_blu_ray_picture_subtitle_is_shown_where_it_was_placed() {
	if (m_ffmpeg.isEmpty()) QSKIP("no ffmpeg");
	QFile sup(m_dir.filePath(QStringLiteral("box.sup")));
	QVERIFY(sup.open(QIODevice::WriteOnly));
	sup.write(pgs_stream());
	sup.close();
	QVERIFY(make_video(QStringLiteral("pgs.mkv"), { QStringLiteral("-i"), sup.fileName(), QStringLiteral("-map"), QStringLiteral("0:v"),
	                                                QStringLiteral("-map"), QStringLiteral("1:a"), QStringLiteral("-map"),
	                                                QStringLiteral("2:s"), QStringLiteral("-c:s"), QStringLiteral("copy") }));
	snurra_video video(m_dir.filePath(QStringLiteral("pgs.mkv")));
	QTRY_VERIFY_WITH_TIMEOUT(video.opened(), 5000);
	const auto tracks = video.subtitle_tracks();
	QCOMPARE(tracks.size(), size_t(1));
	for (int64_t t = 0; t <= 3 * SECOND; t += 40000) {
		video.frame_at(t);
		QTest::qWait(2);
	}
	QTRY_VERIFY_WITH_TIMEOUT(!tracks[0]->at(1500000).empty(), 3000);
	QVERIFY(tracks[0]->at(900000).empty());
	QVERIFY(tracks[0]->at(2100000).empty());
	const snurra_cue cue = tracks[0]->at(1500000).front();
	QVERIFY(cue.lines.empty());
	QCOMPARE(cue.image.size(), QSize(48, 10));
	const QRgb middle = cue.image.pixel(24, 5);
	QVERIFY2(qAlpha(middle) == 255 && qRed(middle) > 240 && qGreen(middle) > 240 && qBlue(middle) > 240,
	         qPrintable(QString::number(middle, 16)));
	QCOMPARE(cue.place.x(), 8.0 / 64);
	QCOMPARE(cue.place.y(), 30.0 / 48);
	QCOMPARE(cue.place.width(), 48.0 / 64);
	QCOMPARE(cue.place.height(), 10.0 / 48);
}

/* A key frame each second, and a line from 1.5 s to 5.5 s: a seek to
 * 4.5 s lands on the key frame at 4 s, after the line began. */
void subtitles_test::a_seek_into_a_line_shows_that_line() {
	if (m_ffmpeg.isEmpty()) QSKIP("no ffmpeg");
	QFile srt(m_dir.filePath(QStringLiteral("long.srt")));
	QVERIFY(srt.open(QIODevice::WriteOnly));
	srt.write("1\n00:00:01,500 --> 00:00:05,500\nthe long line\n\n2\n00:00:06,000 --> 00:00:06,500\nlast\n");
	srt.close();
	QStringList args{ QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"),
		          QStringLiteral("-i"), QStringLiteral("testsrc=s=64x48:r=25:d=7"), QStringLiteral("-f"), QStringLiteral("lavfi"),
		          QStringLiteral("-i"), QStringLiteral("sine=f=440:d=7"), QStringLiteral("-i"), srt.fileName(),
		          QStringLiteral("-map"), QStringLiteral("0:v"), QStringLiteral("-map"), QStringLiteral("1:a"), QStringLiteral("-map"),
		          QStringLiteral("2:s"), QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-g"), QStringLiteral("25"),
		          QStringLiteral("-keyint_min"), QStringLiteral("25"), QStringLiteral("-c:s"), QStringLiteral("srt"),
		          QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), m_dir.filePath(QStringLiteral("seek.mkv")) };
	QProcess ffmpeg;
	ffmpeg.start(m_ffmpeg, args);
	QVERIFY(ffmpeg.waitForFinished(60000) && ffmpeg.exitCode() == 0);

	snurra_video video(m_dir.filePath(QStringLiteral("seek.mkv")));
	QTRY_VERIFY_WITH_TIMEOUT(video.opened(), 5000);
	const auto tracks = video.subtitle_tracks();
	QCOMPARE(tracks.size(), size_t(1));
	/* Straight to 4.5 s, as a jump or a click on the position does. */
	QTRY_VERIFY_WITH_TIMEOUT(video.frame_at(4500000).pts_us >= 4400000, 5000);
	QTRY_COMPARE_WITH_TIMEOUT(words_at(*tracks[0], 4500000), QStringLiteral("the long line"), 2000);
	/* And again, back and on, the second reader used twice. */
	QTRY_VERIFY_WITH_TIMEOUT(video.frame_at(500000).pts_us >= 400000, 5000);
	QTRY_VERIFY_WITH_TIMEOUT(video.frame_at(6200000).pts_us >= 6100000, 5000);
	QTRY_COMPARE_WITH_TIMEOUT(words_at(*tracks[0], 6200000), QStringLiteral("last"), 2000);
	QCOMPARE(tracks[0]->size(), size_t(2));
}

void subtitles_test::a_tile_shows_the_words_heard_and_v_and_j_change_them() {
	if (m_ffmpeg.isEmpty()) QSKIP("no ffmpeg");
	/* An embedded track, named, and a .srt beside the file, which comes
	 * first and shows from the start. Cues long enough to be seen. */
	QFile inside(m_dir.filePath(QStringLiteral("inside.srt")));
	QVERIFY(inside.open(QIODevice::WriteOnly));
	inside.write("1\n00:00:00,000 --> 00:00:03,000\nfrom inside\n");
	inside.close();
	QVERIFY(make_video(QStringLiteral("film.mkv"),
	                   { QStringLiteral("-i"), inside.fileName(), QStringLiteral("-map"), QStringLiteral("0:v"),
	                     QStringLiteral("-map"), QStringLiteral("1:a"), QStringLiteral("-map"), QStringLiteral("2:s"),
	                     QStringLiteral("-c:s"), QStringLiteral("srt"), QStringLiteral("-metadata:s:s:0"),
	                     QStringLiteral("title=Inside") }));
	QFile beside(m_dir.filePath(QStringLiteral("film.srt")));
	QVERIFY(beside.open(QIODevice::WriteOnly));
	beside.write("1\n00:00:00,000 --> 00:00:03,000\nfrom beside\n");
	beside.close();

	snurra_node node;
	snurra_tile_state state;
	state.source = m_dir.filePath(QStringLiteral("film.mkv"));
	state.playing = true;
	state.controls = true;
	state.sampled_at_us = snurra_clock_us();
	snurra_tile *tile = node.add_tile(state);
	node.show();
	QVERIFY(QTest::qWaitForWindowExposed(&node));
	QTRY_VERIFY_WITH_TIMEOUT(tile->video_open(), 5000);
	QWidget *view = nullptr;
	for (QWidget *child : tile->drawer_above()->findChildren<QWidget *>())
		if (child->accessibleName() == QLatin1String("Video")) view = child;
	QVERIFY(view);
	QCOMPARE(tile->subtitles_name(), QStringLiteral("film.srt"));
	QTRY_COMPARE_WITH_TIMEOUT(view->property("subtitle").toString(), QStringLiteral("from beside"), 2000);

	QTest::keyClick(&node, Qt::Key_V);
	QCOMPARE(tile->subtitles_name(), QString());
	QCOMPARE(view->property("notice").toString(), QStringLiteral("Subtitles off"));
	QTRY_COMPARE_WITH_TIMEOUT(view->property("subtitle").toString(), QString(), 1000);
	QTest::keyClick(&node, Qt::Key_V);
	QCOMPARE(tile->subtitles_name(), QStringLiteral("film.srt"));

	QTest::keyClick(&node, Qt::Key_J);
	QCOMPARE(tile->subtitles_name(), QStringLiteral("Inside"));
	QCOMPARE(view->property("notice").toString(), QStringLiteral("Subtitles: Inside"));
	QTRY_COMPARE_WITH_TIMEOUT(view->property("subtitle").toString(), QStringLiteral("from inside"), 2000);
	QTest::keyClick(&node, Qt::Key_J);
	QCOMPARE(tile->subtitles_name(), QString());
	QTRY_COMPARE_WITH_TIMEOUT(view->property("subtitle").toString(), QString(), 1000);
	QTest::keyClick(&node, Qt::Key_J);
	QCOMPARE(tile->subtitles_name(), QStringLiteral("film.srt"));

	/* x: later by a tenth each. At the start, the line from 0 s now waits
	 * half a second; z takes it back. */
	tile->request_playing(false);
	tile->seek_by(-60 * SECOND);
	QTRY_VERIFY_WITH_TIMEOUT(tile->deck()->position_at(snurra_clock_us()) < 1000, 3000);
	for (int i = 0; i < 5; ++i) QTest::keyClick(&node, Qt::Key_X);
	QCOMPARE(tile->subtitle_delay(), 500000);
	QCOMPARE(view->property("notice").toString(), QStringLiteral("Subtitle delay: 0.5 s"));
	QTRY_COMPARE_WITH_TIMEOUT(view->property("subtitle").toString(), QString(), 1000);
	for (int i = 0; i < 5; ++i) QTest::keyClick(&node, Qt::Key_Z);
	QCOMPARE(tile->subtitle_delay(), 0);
	QTRY_COMPARE_WITH_TIMEOUT(view->property("subtitle").toString(), QStringLiteral("from beside"), 1000);
}

/* embed.h 2: a host playing bytes hands over the .srt it fetched from
 * beside the remote file; it is offered first, before what the file
 * carries, and shown. */
void subtitles_test::a_hosts_subtitle_file_comes_first_for_its_bytes() {
	if (m_ffmpeg.isEmpty()) QSKIP("no ffmpeg");
	QFile inside(m_dir.filePath(QStringLiteral("carried.srt")));
	QVERIFY(inside.open(QIODevice::WriteOnly));
	inside.write("1\n00:00:00,000 --> 00:00:03,000\ncarried\n");
	inside.close();
	QVERIFY(make_video(QStringLiteral("remote.mkv"),
	                   { QStringLiteral("-i"), inside.fileName(), QStringLiteral("-map"), QStringLiteral("0:v"),
	                     QStringLiteral("-map"), QStringLiteral("1:a"), QStringLiteral("-map"), QStringLiteral("2:s"),
	                     QStringLiteral("-c:s"), QStringLiteral("srt") }));
	QTemporaryDir fetched;
	QVERIFY(fetched.isValid());
	QFile srt(fetched.filePath(QStringLiteral("remote.en.srt")));
	QVERIFY(srt.open(QIODevice::WriteOnly));
	srt.write("1\n00:00:00,000 --> 00:00:03,000\nfetched\n");
	srt.close();
	QFile film(m_dir.filePath(QStringLiteral("remote.mkv")));
	QVERIFY(film.open(QIODevice::ReadOnly));
	auto bytes = std::make_shared<memory_bytes>(film.readAll());
	bytes->arrive(0, bytes->size());

	snurra_player player;
	player.resize(400, 300);
	player.show();
	snurra_media media;
	media.name = QStringLiteral("remote.mkv");
	media.bytes = bytes;
	media.subtitles = { srt.fileName() };
	QVERIFY(player.open(media));
	snurra_tile *tile = player.findChild<snurra_tile *>();
	QVERIFY(tile);
	QTRY_COMPARE_WITH_TIMEOUT(tile->subtitles_name(), QStringLiteral("remote.en.srt"), 5000);
	tile->next_subtitles();
	QCOMPARE(tile->subtitles_name(), QStringLiteral("track 1"));
}

int run_subtitles_tests(int argc, char **argv) {
	subtitles_test test;
	return QTest::qExec(&test, argc, argv);
}

#include "subtitles_test.moc"
