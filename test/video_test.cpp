/*
 * The picture's timing (project.md sec 3b: the drawer that opens upward;
 * the holder: codecs come with tests of how they keep time). Short videos
 * whose frame N is grey at a level that names N modulo 16, made with the
 * ffmpeg command-line tool in several codecs -- B-frames reordered, intra
 * only, a container that starts the picture after the sound -- read back
 * through snurra_video: at every time asked, the frame a screen shows
 * then, by its time and by its picture, and never one from the future,
 * forward, after a seek back and after a jump ahead.
 */
#include <QElapsedTimer>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <cmath>
#include <vector>

#include "snurra/clock.h"
#include "snurra/deck.h"
#include "snurra/node.h"
#include "snurra/tile.h"
#include "snurra/video.h"
#include "test_bytes.h"

namespace {

/* The index a picture says: frame n is made grey at (n % 16) * 12 + 24. */
int index_of(const QImage &image) {
	/* Limited range: RGB = (Y - 16) * 255 / 219, about. */
	const double y = qRed(image.pixel(image.width() / 2, image.height() / 2)) * 219.0 / 255.0 + 16;
	return int(std::lround((y - 24) / 12));
}

}

class video_test : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void every_codec_shows_the_frame_of_its_time_data();
	void every_codec_shows_the_frame_of_its_time();
	void a_track_without_a_picture_has_no_video();
	void the_video_drawer_opens_upward_and_shows_what_is_heard();
	void a_picture_from_bytes_is_the_files_picture();

private:
	bool make(const QString &name, const QStringList &video_args, double offset);
	std::vector<int64_t> frame_times(const QString &name);
	QTemporaryDir m_dir;
	QString m_ffmpeg;
	QString m_ffprobe;
};

void video_test::initTestCase() {
	QVERIFY(m_dir.isValid());
	m_ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
	m_ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
	qputenv("SNURRA_OUTPUT", "null");   /* paced by the clock, no device */
}

/*
 * When each picture is shown, in microseconds of the track, as a second
 * tool reads the file: ffprobe's time for every video frame, less the
 * sound's start -- the zero a deck counts from. In time order; frame n of
 * that order is the n-th made, and carries its grey.
 */
std::vector<int64_t> video_test::frame_times(const QString &name) {
	auto probe = [this, &name](const QStringList &what) {
		QProcess ffprobe;
		ffprobe.start(m_ffprobe, QStringList{ QStringLiteral("-v"), QStringLiteral("error") } << what
		                                 << QStringLiteral("-of") << QStringLiteral("csv=p=0") << m_dir.filePath(name));
		ffprobe.waitForFinished(30000);
		return QString::fromLatin1(ffprobe.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
	};
	const QStringList audio = probe({ QStringLiteral("-select_streams"), QStringLiteral("a:0"), QStringLiteral("-show_entries"),
	                                  QStringLiteral("stream=start_time") });
	/* The first field: a frame with side data has more after a comma. */
	auto seconds = [](const QString &line) { return line.section(QLatin1Char(','), 0, 0).trimmed().toDouble(); };
	const double zero = audio.isEmpty() ? 0 : seconds(audio.first());
	std::vector<int64_t> times;
	for (const QString &line : probe({ QStringLiteral("-select_streams"), QStringLiteral("v:0"), QStringLiteral("-show_entries"),
	                                   QStringLiteral("frame=best_effort_timestamp_time") }))
		times.push_back(std::llround((seconds(line) - zero) * 1e6));
	std::sort(times.begin(), times.end());
	return times;
}

/* Three seconds, 64x48 at 25 a second, with a tone; the picture `offset`
 * seconds after the sound. */
bool video_test::make(const QString &name, const QStringList &video_args, double offset) {
	QStringList args{ QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-y") };
	if (offset > 0) args << QStringLiteral("-itsoffset") << QString::number(offset);
	args << QStringLiteral("-f") << QStringLiteral("lavfi") << QStringLiteral("-i")
	     << QStringLiteral("nullsrc=s=64x48:r=25:d=3,geq=lum='mod(N\\,16)*12+24':cb=128:cr=128")
	     << QStringLiteral("-f") << QStringLiteral("lavfi") << QStringLiteral("-i")
	     << QStringLiteral("sine=f=440:d=3") << video_args << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
	     << m_dir.filePath(name);
	QProcess ffmpeg;
	ffmpeg.start(m_ffmpeg, args);
	return ffmpeg.waitForFinished(60000) && ffmpeg.exitCode() == 0;
}

void video_test::every_codec_shows_the_frame_of_its_time_data() {
	QTest::addColumn<QString>("name");
	QTest::addColumn<QStringList>("video");
	QTest::addColumn<double>("offset");
	const QString c = QStringLiteral("-c:v");
	QTest::newRow("h264, B-frames") << QStringLiteral("h264.mp4")
	                                << QStringList{ c, QStringLiteral("libx264"), QStringLiteral("-bf"), QStringLiteral("3"),
	                                                QStringLiteral("-g"), QStringLiteral("25") }
	                                << 0.0;
	QTest::newRow("h264, picture 0.2 s late") << QStringLiteral("late.mp4")
	                                          << QStringList{ c, QStringLiteral("libx264"), QStringLiteral("-bf"),
	                                                          QStringLiteral("3"), QStringLiteral("-g"), QStringLiteral("25") }
	                                          << 0.2;
	QTest::newRow("mpeg4") << QStringLiteral("mpeg4.mkv") << QStringList{ c, QStringLiteral("mpeg4"), QStringLiteral("-q:v"), QStringLiteral("2") }
	                       << 0.0;
	QTest::newRow("vp9, opus") << QStringLiteral("vp9.webm")
	                           << QStringList{ c, QStringLiteral("libvpx-vp9"), QStringLiteral("-crf"), QStringLiteral("10"),
	                                           QStringLiteral("-b:v"), QStringLiteral("0"), QStringLiteral("-c:a"),
	                                           QStringLiteral("libopus") }
	                           << 0.0;
	QTest::newRow("mjpeg") << QStringLiteral("mjpeg.mkv") << QStringList{ c, QStringLiteral("mjpeg"), QStringLiteral("-q:v"), QStringLiteral("2") }
	                       << 0.0;
}

void video_test::every_codec_shows_the_frame_of_its_time() {
	QFETCH(QString, name);
	QFETCH(QStringList, video);
	QFETCH(double, offset);
	if (m_ffmpeg.isEmpty() || m_ffprobe.isEmpty()) QSKIP("no ffmpeg and ffprobe to make and read the videos with");
	QVERIFY2(make(name, video, offset), "ffmpeg could not make the video");
	const std::vector<int64_t> times = frame_times(name);
	QCOMPARE(int(times.size()), 75);
	/* The late one late by what it was made; the rest within a container's
	 * own small offset (Matroska rounds to a millisecond, Opus primes). */
	QVERIFY2(qAbs(times.front() - int64_t(std::lround(offset * 1e6))) <= 10000,
	         qPrintable(QStringLiteral("the first picture is at %1 us").arg(times.front())));

	snurra_video reader(m_dir.filePath(name));
	QTRY_VERIFY_WITH_TIMEOUT(reader.has_video(), 5000);
	QCOMPARE(reader.size(), QSize(64, 48));

	int checked = 0;
	int future = 0;
	QString wrong;
	/* At `t`, the frame shown is the last whose time is not after t. */
	auto check = [&](int64_t t) {
		if (!wrong.isEmpty()) return;
		const auto after = std::upper_bound(times.begin(), times.end(), t);
		const int n = int(after - times.begin()) - 1;
		const int64_t expected = n < 0 ? INT64_MIN : times[size_t(n)];
		QElapsedTimer waited;
		waited.start();
		snurra_video_frame frame;
		for (;;) {
			frame = reader.frame_at(t);
			if (frame.valid() && frame.pts_us > t) ++future;
			if (expected == INT64_MIN ? !frame.valid() : frame.pts_us == expected) break;
			if (waited.elapsed() > 3000) {
				wrong = QStringLiteral("at %1 us: shown %2, not %3").arg(t).arg(frame.pts_us).arg(expected);
				return;
			}
			QTest::qWait(1);
		}
		if (expected != INT64_MIN) {
			if (index_of(frame.image) != n % 16)
				wrong = QStringLiteral("at %1 us: the picture of frame %2 says %3").arg(t).arg(n).arg(index_of(frame.image));
		}
		++checked;
	};

	for (int64_t t = 0; t < 2950000; t += 13000) check(t);   /* forward, off the frames' grid */
	for (int64_t t : { 2500000, 1000017, 300000, 2000000, 40000, 2900000, 120000 }) check(t);   /* seeks and jumps */
	qInfo("%s: %d times checked, %d frames from the future", qPrintable(name), checked, future);
	QVERIFY2(wrong.isEmpty(), qPrintable(wrong));
	QCOMPARE(future, 0);
}

void video_test::a_track_without_a_picture_has_no_video() {
	if (m_ffmpeg.isEmpty()) QSKIP("no ffmpeg");
	const QString path = m_dir.filePath(QStringLiteral("tone.flac"));
	QProcess ffmpeg;
	ffmpeg.start(m_ffmpeg, { QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-y"), QStringLiteral("-f"),
	                         QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("sine=f=440:d=1"), path });
	QVERIFY(ffmpeg.waitForFinished(30000) && ffmpeg.exitCode() == 0);
	std::atomic<int> opened{ -1 };
	snurra_video reader(path);
	reader.set_on_open([&opened](bool has) { opened = has ? 1 : 0; });
	/* Set after the start, so the reader may already have opened. */
	QTRY_VERIFY_WITH_TIMEOUT(opened.load() == 0 || (opened.load() == -1 && !reader.has_video()), 3000);
	QTest::qWait(300);
	QVERIFY(!reader.has_video());
	QVERIFY(!reader.frame_at(500000).valid());
}

/*
 * The drawer (project.md sec 3b): a playing tile of a 4:3 video opens it
 * above its waveform, flush, as tall as the picture at the tile's width;
 * closed and opened again, the tile stays where it was on the screen --
 * the window grows up. While it plays, the frame on screen is the one for
 * what is heard: never ahead of it, never more than one frame behind (the
 * 16 ms it is looked at). Maximized, the picture is full screen; Escape
 * brings it back.
 */
void video_test::the_video_drawer_opens_upward_and_shows_what_is_heard() {
	if (m_ffmpeg.isEmpty() || m_ffprobe.isEmpty()) QSKIP("no ffmpeg and ffprobe");
	const QString name = QStringLiteral("drawer.mp4");
	QVERIFY(make(name, { QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-bf"), QStringLiteral("3") }, 0));
	const std::vector<int64_t> times = frame_times(name);

	snurra_node node;
	snurra_tile_state state;
	state.source = m_dir.filePath(name);
	state.playing = true;
	state.controls = true;
	state.sampled_at_us = snurra_clock_us();
	snurra_tile *tile = node.add_tile(state);
	node.move(100, 400);   /* room above it to grow into */
	node.show();
	QVERIFY(QTest::qWaitForWindowExposed(&node));
	QTRY_VERIFY_WITH_TIMEOUT(tile->video_open(), 5000);
	QWidget *drawer = tile->drawer_above();
	QTRY_COMPARE_WITH_TIMEOUT(drawer->height(), (drawer->width() - 4) * 48 / 64 + 4, 2000);
	QTRY_COMPARE_WITH_TIMEOUT(drawer->geometry().bottom() + 1, tile->geometry().top(), 2000);

	tile->set_video_open(false);
	QTRY_VERIFY_WITH_TIMEOUT(!drawer->isVisible(), 2000);
	QTest::qWait(100);
	const QPoint closed = tile->mapToGlobal(QPoint());
	tile->set_video_open(true);
	QTRY_VERIFY_WITH_TIMEOUT(drawer->isVisible(), 2000);
	QTest::qWait(100);
	QCOMPARE(tile->mapToGlobal(QPoint()), closed);

	QWidget *view = nullptr;
	for (QWidget *child : drawer->findChildren<QWidget *>())
		if (child->accessibleName() == QLatin1String("Video")) view = child;
	QVERIFY(view);
	int same = 0;
	int behind = 0;
	int ahead = 0;
	int worse = 0;
	for (int i = 0; i < 60; ++i) {
		QTest::qWait(23);
		const int64_t heard = snurra_frames_to_us(tile->deck()->position_at(snurra_clock_us()), tile->deck()->rate());
		const int64_t shown = view->property("pts").toLongLong();
		const auto after = std::upper_bound(times.begin(), times.end(), heard);
		const int n = int(after - times.begin()) - 1;
		if (n < 0 || heard > 2900000) continue;
		if (shown == times[size_t(n)]) ++same;
		else if (shown > heard) ++ahead;
		else if (n >= 1 && shown == times[size_t(n - 1)]) ++behind;
		else ++worse;
	}
	qInfo("drawer: the frame heard on screen %d times, one behind %d, ahead %d, further behind %d", same, behind, ahead, worse);
	QCOMPARE(ahead, 0);
	QCOMPARE(worse, 0);
	QVERIFY2(same > behind, "the picture was mostly a frame behind");

	tile->set_full_screen(true);
	QWidget *full = nullptr;
	QTRY_VERIFY_WITH_TIMEOUT(([&] {
		                         for (QWidget *top : QApplication::topLevelWidgets())
			                         if (top->accessibleName() == QLatin1String("Video") && top->isVisible()) full = top;
		                         return full != nullptr;
	                         }()),
	                         2000);
	QVERIFY(full->isFullScreen() || full->windowState() & Qt::WindowFullScreen);
	QTest::keyClick(full, Qt::Key_Escape);
	QTRY_VERIFY_WITH_TIMEOUT(([&] {
		                         for (QWidget *top : QApplication::topLevelWidgets())
			                         if (top->accessibleName() == QLatin1String("Video") && top->isVisible()) return false;
		                         return true;
	                         }()),
	                         2000);
}

/* From a host's bytes (embed.h, BeerSSH's files over SSH): every frame the
 * file gives at a time, the bytes give too -- by time and by picture. */
void video_test::a_picture_from_bytes_is_the_files_picture() {
	if (m_ffmpeg.isEmpty()) QSKIP("no ffmpeg");
	const QString name = QStringLiteral("bytes.mp4");
	QVERIFY(make(name, { QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-bf"), QStringLiteral("3") }, 0));
	QFile file(m_dir.filePath(name));
	QVERIFY(file.open(QIODevice::ReadOnly));
	auto bytes = std::make_shared<memory_bytes>(file.readAll());
	bytes->arrive(0, bytes->size());
	snurra_video from_file(m_dir.filePath(name));
	snurra_video from_bytes(bytes, name);
	QTRY_VERIFY_WITH_TIMEOUT(from_file.has_video() && from_bytes.has_video(), 5000);
	int same = 0;
	for (int64_t t = 0; t < 2950000; t += 13000) {
		snurra_video_frame a;
		snurra_video_frame b;
		QElapsedTimer waited;
		waited.start();
		for (;;) {
			a = from_file.frame_at(t);
			b = from_bytes.frame_at(t);
			if (a.valid() && b.valid() && a.pts_us == b.pts_us) break;
			QVERIFY2(waited.elapsed() < 3000, qPrintable(QStringLiteral("at %1 us: the file gave %2, the bytes %3")
			                                                     .arg(t).arg(a.pts_us).arg(b.pts_us)));
			QTest::qWait(1);
		}
		QVERIFY2(a.image == b.image, qPrintable(QStringLiteral("at %1 us the pictures differ").arg(t)));
		++same;
	}
	qInfo("bytes: %d times, the file's frame each time", same);
}

int run_video_tests(int argc, char **argv) {
	video_test test;
	return QTest::qExec(&test, argc, argv);
}

#include "video_test.moc"
