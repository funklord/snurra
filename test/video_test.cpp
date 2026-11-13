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
#include <QTcpServer>
#include <QTcpSocket>
#include <QSignalSpy>
#include <QDataStream>
#include <QLabel>
#include <QMimeData>
#include <QAbstractItemModel>
#include <QTreeView>
#include <QToolButton>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <cmath>
#include <vector>

#include "snurra/clock.h"
#include "snurra/deck.h"
#include "snurra/embed.h"
#include "snurra/library.h"
#include "snurra/node.h"
#include "snurra/peaks.h"
#include "snurra/source.h"
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
	void full_screen_asked_before_the_picture_comes_with_it();
	void a_period_steps_one_frame_on_paused();
	void plus_and_minus_move_the_picture_against_the_sound();
	void less_and_greater_step_along_the_train();
	void the_full_screen_picture_takes_the_players_keys();
	void a_url_plays_with_its_picture();
	void a_url_that_never_answers_lets_the_window_close();
	void a_picture_from_bytes_is_the_files_picture();
	void a_picture_without_sound_is_not_a_failure();
	void a_picture_is_shown_at_its_meant_shape_and_turn();
	void a_films_other_sound_track_plays_from_where_it_is();
	void a_stem_file_is_a_stack_of_its_stems();
	void colours_are_ffmpegs_own_data();
	void colours_are_ffmpegs_own();
	void a_full_screen_button_shows_while_the_pointer_moves();
	void a_picture_is_made_as_wide_as_it_is_shown();
	void a_local_films_srt_is_found_when_its_bytes_are_given();
	void a_picture_made_small_is_averaged_not_picked();
	void a_skip_shows_the_frame_heard_not_one_behind();
	void a_far_target_while_on_the_way_is_a_new_seek();
	void the_library_shows_a_files_own_title_and_artist();
	void a_mixs_chapters_are_its_tracklist();

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
	     << QStringLiteral("sine=f=440:d=3") << video_args;
	/* 8-bit 4:2:0 unless the codec's row says otherwise (10-bit). */
	if (!video_args.contains(QStringLiteral("-pix_fmt"))) args << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p");
	args << m_dir.filePath(name);
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
	QTest::newRow("h265, 10-bit") << QStringLiteral("hevc10.mkv")
	                              << QStringList{ c, QStringLiteral("libx265"), QStringLiteral("-pix_fmt"),
	                                              QStringLiteral("yuv420p10le"), QStringLiteral("-x265-params"),
	                                              QStringLiteral("log-level=error") }
	                              << 0.0;
	QTest::newRow("av1, 10-bit") << QStringLiteral("av1.mkv")
	                             /* libaom: SVT-AV1 refuses a picture under 64 high */
	                             << QStringList{ c, QStringLiteral("libaom-av1"), QStringLiteral("-pix_fmt"),
	                                             QStringLiteral("yuv420p10le"), QStringLiteral("-cpu-used"), QStringLiteral("8") }
	                             << 0.0;
	QTest::newRow("qtrle, rgb") << QStringLiteral("qtrle.mov")
	                            << QStringList{ c, QStringLiteral("qtrle"), QStringLiteral("-pix_fmt"), QStringLiteral("rgb24") }
	                            << 0.0;
	QTest::newRow("png, rgb") << QStringLiteral("png.mkv")
	                          << QStringList{ c, QStringLiteral("png"), QStringLiteral("-pix_fmt"), QStringLiteral("rgb24") }
	                          << 0.0;
	/* FFmpeg's GIF decoder hands over BGRA, not its palette: a paletted
	 * PNG is what comes through as a palette, and a grey one as grey. */
	QTest::newRow("png, palette") << QStringLiteral("pal.mkv")
	                              << QStringList{ QStringLiteral("-vf"),
	                                              QStringLiteral("split[a][b];[a]palettegen[p];[b][p]paletteuse"), c,
	                                              QStringLiteral("png"), QStringLiteral("-pix_fmt"), QStringLiteral("pal8") }
	                              << 0.0;
	QTest::newRow("png, grey") << QStringLiteral("grey.mkv")
	                           << QStringList{ c, QStringLiteral("png"), QStringLiteral("-pix_fmt"), QStringLiteral("gray") }
	                           << 0.0;
	QTest::newRow("gif, bgra") << QStringLiteral("anim.gif")
	                              /* Its palette made from its own frames, as a GIF is: FFmpeg's
	                               * fixed one flattens the greys before snurra ever sees them. */
	                              << QStringList{ QStringLiteral("-vf"),
	                                              QStringLiteral("split[a][b];[a]palettegen[p];[b][p]paletteuse"), c,
	                                              QStringLiteral("gif"), QStringLiteral("-pix_fmt"), QStringLiteral("pal8") }
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

	/* mplayer's f, to the window, as a key press. */
	QTest::keyClick(&node, Qt::Key_F);
	QVERIFY2(tile->full_screen(), "f did not show the picture full screen");
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

/* mplayer's -fs: asked for as the tile opens, before its picture has been
 * found, and shown full screen once it is. */
void video_test::full_screen_asked_before_the_picture_comes_with_it() {
	if (m_ffmpeg.isEmpty() || m_ffprobe.isEmpty()) QSKIP("no ffmpeg and ffprobe");
	const QString name = QStringLiteral("full.mp4");
	QVERIFY(make(name, { QStringLiteral("-c:v"), QStringLiteral("libx264") }, 0));
	snurra_node node;
	snurra_tile_state state;
	state.source = m_dir.filePath(name);
	state.controls = true;
	state.sampled_at_us = snurra_clock_us();
	snurra_tile *tile = node.add_tile(state);
	tile->set_full_screen(true);
	QVERIFY(!tile->video_open());
	node.show();
	QTRY_VERIFY_WITH_TIMEOUT(tile->full_screen(), 5000);
	tile->set_full_screen(false);
	QVERIFY(!tile->full_screen());
}

/* mplayer's .: paused, the frame after the one shown, each time exactly the
 * next a second tool counts in the file -- a playing tile pauses first. */
void video_test::a_period_steps_one_frame_on_paused() {
	if (m_ffmpeg.isEmpty() || m_ffprobe.isEmpty()) QSKIP("no ffmpeg and ffprobe");
	const QString name = QStringLiteral("step.mp4");
	QVERIFY(make(name, { QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-bf"), QStringLiteral("3") }, 0));
	const std::vector<int64_t> times = frame_times(name);
	QVERIFY(times.size() > 20);

	snurra_node node;
	snurra_tile_state state;
	state.source = m_dir.filePath(name);
	state.playing = true;
	state.controls = true;
	state.position_us = times[10] + 1000;
	state.sampled_at_us = snurra_clock_us();
	snurra_tile *tile = node.add_tile(state);
	node.show();
	QVERIFY(QTest::qWaitForWindowExposed(&node));
	QTRY_VERIFY_WITH_TIMEOUT(tile->video_open(), 5000);
	QWidget *view = nullptr;
	for (QWidget *child : tile->drawer_above()->findChildren<QWidget *>())
		if (child->accessibleName() == QLatin1String("Video")) view = child;
	QVERIFY(view);
	QTest::keyClick(&node, Qt::Key_Period);
	QVERIFY2(!tile->playing(), ". did not pause");
	QTRY_VERIFY_WITH_TIMEOUT(view->property("pts").toLongLong() != INT64_MIN, 2000);
	QTest::qWait(300);   /* the pause, and the step it took, settled */
	for (int step = 0; step < 5; ++step) {
		const int64_t shown = view->property("pts").toLongLong();
		const auto at = std::find(times.begin(), times.end(), shown);
		QVERIFY2(at != times.end() && at + 1 != times.end(), "the frame shown is not one of the file's");
		QTest::keyClick(&node, Qt::Key_Period);
		QTRY_COMPARE_WITH_TIMEOUT(view->property("pts").toLongLong(), *(at + 1), 2000);
		QVERIFY(!tile->playing());
	}
}

/* mplayer's + and -: paused, five presses of + show the frame half a second
 * after what is heard, and the sound stays where it was; - brings it back. */
void video_test::plus_and_minus_move_the_picture_against_the_sound() {
	if (m_ffmpeg.isEmpty() || m_ffprobe.isEmpty()) QSKIP("no ffmpeg and ffprobe");
	const QString name = QStringLiteral("delay.mp4");
	QVERIFY(make(name, { QStringLiteral("-c:v"), QStringLiteral("libx264") }, 0));
	const std::vector<int64_t> times = frame_times(name);
	QVERIFY(times.size() > 40);

	snurra_node node;
	snurra_tile_state state;
	state.source = m_dir.filePath(name);
	state.controls = true;
	state.position_us = times[10] + 1000;
	state.sampled_at_us = snurra_clock_us();
	snurra_tile *tile = node.add_tile(state);
	tile->set_video_open(true);
	node.show();
	QVERIFY(QTest::qWaitForWindowExposed(&node));
	QTRY_VERIFY_WITH_TIMEOUT(tile->video_open(), 5000);
	QWidget *view = nullptr;
	for (QWidget *child : tile->drawer_above()->findChildren<QWidget *>())
		if (child->accessibleName() == QLatin1String("Video")) view = child;
	QVERIFY(view);
	QTRY_COMPARE_WITH_TIMEOUT(view->property("pts").toLongLong(), times[10], 3000);
	const qint64 heard = tile->state().position_us;

	/* The frame for a time: the last that begins at or before it. */
	auto frame_for = [&](int64_t us) { return *(std::upper_bound(times.begin(), times.end(), us) - 1); };
	for (int i = 0; i < 5; ++i) QTest::keyClick(&node, Qt::Key_Plus);
	QCOMPARE(tile->sound_delay(), qint64(500000));
	QTRY_COMPARE_WITH_TIMEOUT(view->property("pts").toLongLong(), frame_for(heard + 500000), 3000);
	QCOMPARE(tile->state().position_us, heard);
	for (int i = 0; i < 5; ++i) QTest::keyClick(&node, Qt::Key_Minus);
	QCOMPARE(tile->sound_delay(), qint64(0));
	QTRY_COMPARE_WITH_TIMEOUT(view->property("pts").toLongLong(), times[10], 3000);
}

/* mplayer's < and >: the playlist's previous and next file, here the train's
 * tile -- from its start, the one before stopping; nothing past the last,
 * and before the first, the first's start. */
void video_test::less_and_greater_step_along_the_train() {
	snurra_node node;
	QList<snurra_tile *> tiles;
	for (int column = 0; column < 3; ++column) {
		snurra_tile_state state;
		state.source = m_dir.filePath(QStringLiteral("nothing-%1.flac").arg(column));   /* silence, a minute */
		state.duration_us = 60000000;
		state.controls = column == 0;
		state.playing = column == 0;
		state.column = column;
		state.sampled_at_us = snurra_clock_us();
		tiles << node.add_tile(state);
	}
	node.show();
	QVERIFY(QTest::qWaitForWindowExposed(&node));
	QTRY_VERIFY_WITH_TIMEOUT(tiles[0]->playing(), 3000);
	QTest::qWait(1500);

	auto only = [&](int playing) {
		for (int i = 0; i < tiles.size(); ++i)
			if (tiles[i]->playing() != (i == playing)) return false;
		return true;
	};
	auto into = [&](int i) { return tiles[i]->state().position_now(); };
	QTest::keyClick(&node, Qt::Key_Greater);
	QTRY_VERIFY_WITH_TIMEOUT(only(1), 3000);
	QVERIFY2(into(1) < 500000, "> did not start the next tile from its start");
	QTest::keyClick(&node, Qt::Key_Greater);
	QTRY_VERIFY_WITH_TIMEOUT(only(2), 3000);
	QTest::keyClick(&node, Qt::Key_Greater);
	QTest::qWait(300);
	QVERIFY2(only(2) && into(2) > 200000, "> past the last moved");
	QTest::keyClick(&node, Qt::Key_Less);
	QTRY_VERIFY_WITH_TIMEOUT(only(1), 3000);
	QTest::keyClick(&node, Qt::Key_Less);
	QTRY_VERIFY_WITH_TIMEOUT(only(0), 3000);
	QTest::qWait(1000);
	QTest::keyClick(&node, Qt::Key_Less);
	QTest::qWait(300);
	QVERIFY(only(0));
	QVERIFY2(into(0) < 800000, "< at the first did not go back to its start");
}

/* Full screen is a window of its own, and every key the player takes must
 * work there too: space pauses, o puts the time over the picture, f comes
 * back. Only Escape did. And the pointer is not left on the film. */
void video_test::the_full_screen_picture_takes_the_players_keys() {
	if (m_ffmpeg.isEmpty() || m_ffprobe.isEmpty()) QSKIP("no ffmpeg and ffprobe");
	const QString name = QStringLiteral("keys.mp4");
	QVERIFY(make(name, { QStringLiteral("-c:v"), QStringLiteral("libx264") }, 0));
	snurra_node node;
	snurra_tile_state state;
	state.source = m_dir.filePath(name);
	state.controls = true;
	state.playing = true;
	state.sampled_at_us = snurra_clock_us();
	snurra_tile *tile = node.add_tile(state);
	tile->set_full_screen(true);
	node.show();
	QTRY_VERIFY_WITH_TIMEOUT(tile->full_screen(), 5000);
	QWidget *full = nullptr;
	for (QWidget *top : QApplication::topLevelWidgets())
		if (top->accessibleName() == QLatin1String("Video") && top->isVisible()) full = top;
	QVERIFY(full);

	/* The pointer goes when still, and comes back when moved. */
	QTRY_COMPARE_WITH_TIMEOUT(full->cursor().shape(), Qt::BlankCursor, 3000);
	/* Sent, not QTest::mouseMove: that sends nothing when the pointer is
	 * already there, as an earlier test can leave it, and the check then
	 * failed in a full run only. */
	const QPointF moved = full->rect().center() + QPointF(7, 5);
	QMouseEvent move(QEvent::MouseMove, moved, full->mapToGlobal(moved), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
	QApplication::sendEvent(full, &move);
	QTRY_VERIFY_WITH_TIMEOUT(full->cursor().shape() != Qt::BlankCursor, 1000);

	QVERIFY(tile->playing());
	QTest::keyClick(full, Qt::Key_Space);
	QVERIFY2(!tile->playing(), "space in full screen did not pause");
	/* mplayer's wheel: a notch the small jump -- here 10 s, from 2.5 s
	 * back to the start; half notches add up, and cancel. */
	auto wheel = [&](int delta) {
		QWheelEvent event(QPointF(full->rect().center()), QPointF(full->mapToGlobal(full->rect().center())), QPoint(),
		                  QPoint(0, delta), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
		QApplication::sendEvent(full, &event);
	};
	tile->hold_at(snurra_us_to_frames(2500000, tile->deck()->rate()));
	QTRY_COMPARE_WITH_TIMEOUT(tile->state().position_us, qint64(2500000), 2000);
	wheel(60);
	wheel(-60);
	wheel(-60);
	QTest::qWait(200);
	QCOMPARE(tile->state().position_us, qint64(2500000));
	wheel(-60);   /* the second half of a notch: now a whole one */
	QTRY_COMPARE_WITH_TIMEOUT(tile->state().position_us, qint64(0), 2000);

	QTest::keyClick(full, Qt::Key_O);
	QTRY_VERIFY_WITH_TIMEOUT(QRegularExpression(QStringLiteral("^0:00:0\\d / 0:00:03$")).match(full->property("clock").toString()).hasMatch(),
	                         2000);
	QTest::keyClick(full, Qt::Key_O);
	QTRY_VERIFY_WITH_TIMEOUT(full->property("clock").toString().isEmpty(), 2000);
	/* A file dropped on the picture -- all there is of the window, full
	 * screen -- goes on the train, after it. */
	QMimeData mime;
	mime.setUrls({ QUrl::fromLocalFile(m_dir.filePath(name)) });
	QDragEnterEvent enter(full->rect().center(), Qt::CopyAction, &mime, Qt::LeftButton, {});
	QApplication::sendEvent(full, &enter);
	QVERIFY2(enter.isAccepted(), "the full-screen picture refuses a file");
	QDropEvent drop(QPointF(full->rect().center()), Qt::CopyAction, &mime, Qt::LeftButton, {});
	QApplication::sendEvent(full, &drop);
	QTRY_COMPARE_WITH_TIMEOUT(node.tile_count(), 2, 2000);
	for (snurra_tile *other : node.findChildren<snurra_tile *>())
		if (other != tile) QCOMPARE(qMakePair(other->row(), other->column()), qMakePair(0, 1));
	QTest::keyClick(full, Qt::Key_F);
	QVERIFY2(!tile->full_screen(), "f in full screen did not come back");
}

/*
 * An HTTP server for one file, on this thread: each request answered with
 * the whole file and closed -- no ranges, so FFmpeg reads it as a stream,
 * the harder case. With no file, it accepts and never answers.
 */
class one_file_server : public QTcpServer {
public:
	explicit one_file_server(const QByteArray &body, bool answer = true) : m_body(body), m_answer(answer) {
		connect(this, &QTcpServer::newConnection, this, [this] {
			while (QTcpSocket *socket = nextPendingConnection()) {
				m_held << socket;
				connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
					m_request[socket] += socket->readAll();
					if (!m_answer || !m_request[socket].contains("\r\n\r\n")) return;
					socket->write("HTTP/1.1 200 OK\r\nContent-Type: video/x-matroska\r\nContent-Length: "
					              + QByteArray::number(m_body.size()) + "\r\nConnection: close\r\n\r\n");
					socket->write(m_body);
					socket->disconnectFromHost();
				});
			}
		});
	}
	QString url(const QString &name) const { return QStringLiteral("http://127.0.0.1:%1/%2").arg(serverPort()).arg(name); }

private:
	QByteArray m_body;
	bool m_answer;
	QList<QTcpSocket *> m_held;
	QHash<QTcpSocket *, QByteArray> m_request;
};

/* mplayer's http://...: the sound plays and the picture opens, from a
 * server that gives the file only as a stream. */
void video_test::a_url_plays_with_its_picture() {
	if (m_ffmpeg.isEmpty() || m_ffprobe.isEmpty()) QSKIP("no ffmpeg and ffprobe");
	const QString name = QStringLiteral("url.mkv");
	QVERIFY(make(name, { QStringLiteral("-c:v"), QStringLiteral("libx264") }, 0));
	QFile file(m_dir.filePath(name));
	QVERIFY(file.open(QIODevice::ReadOnly));
	one_file_server server(file.readAll());
	QVERIFY(server.listen(QHostAddress::LocalHost));
	QVERIFY(snurra_is_url(server.url(name)));
	QVERIFY(!snurra_is_url(m_dir.filePath(name)));
	QVERIFY(!snurra_is_url(QStringLiteral("tone:440")));

	snurra_node node;
	snurra_tile_state state;
	state.source = server.url(name);
	state.duration_us = 180000000;
	state.controls = true;
	state.playing = true;
	state.sampled_at_us = snurra_clock_us();
	snurra_tile *tile = node.add_tile(state);
	node.show();
	QTRY_VERIFY_WITH_TIMEOUT(tile->video_open(), 5000);
	QTRY_VERIFY_WITH_TIMEOUT(tile->state().duration_us < 4000000, 5000);   /* the film's own length */
	QTRY_VERIFY_WITH_TIMEOUT(tile->state().position_us > 1000000, 5000);
	QVERIFY(!tile->deck()->failed());
}

/* A server that takes the connection and says nothing: the window still
 * closes at once, its readers' network waits given up. */
void video_test::a_url_that_never_answers_lets_the_window_close() {
	one_file_server server(QByteArray(), false);
	QVERIFY(server.listen(QHostAddress::LocalHost));
	auto *node = new snurra_node;
	snurra_tile_state state;
	state.source = server.url(QStringLiteral("silent.mkv"));
	state.duration_us = 180000000;
	state.controls = true;
	state.playing = true;
	state.sampled_at_us = snurra_clock_us();
	node->add_tile(state);
	node->show();
	QTest::qWait(1000);   /* both readers waiting on the server by now */
	QElapsedTimer closing;
	closing.start();
	delete node;
	qInfo("url: closed in %lld ms with the server silent", closing.elapsed());
	QVERIFY(closing.elapsed() < 2000);
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

/* A picture with no sound (a GIF, a screen recording): the sound gives up
 * at once, but the file plays -- no failure said, the drawer open. And a
 * file with neither still says it. */
void video_test::a_picture_without_sound_is_not_a_failure() {
	if (m_ffmpeg.isEmpty()) QSKIP("no ffmpeg");
	QVERIFY(make(QStringLiteral("silent.gif"), { QStringLiteral("-c:v"), QStringLiteral("gif"), QStringLiteral("-an") }, 0));
	snurra_node node;
	snurra_tile_state state;
	state.source = m_dir.filePath(QStringLiteral("silent.gif"));
	state.playing = true;
	state.controls = true;
	state.duration_us = 180000000;   /* as the player opens anything: three minutes until known */
	snurra_tile *tile = node.add_tile(state);
	QSignalSpy failed(tile, &snurra_tile::failed);
	node.show();
	QTRY_VERIFY_WITH_TIMEOUT(tile->video_open(), 5000);
	QTest::qWait(500);
	QCOMPARE(failed.count(), 0);
	/* As long as its picture, not the stand-in silence's three minutes,
	 * and done when the picture is. */
	QTRY_VERIFY_WITH_TIMEOUT(qAbs(tile->state().duration_us - 3000000) < 100000, 3000);
	QTRY_VERIFY_WITH_TIMEOUT(!tile->playing(), 6000);

	QFile junk(m_dir.filePath(QStringLiteral("notes.mp4")));
	QVERIFY(junk.open(QIODevice::WriteOnly));
	junk.write("not a picture, not a sound\n");
	junk.close();
	snurra_tile_state other = state;
	other.source = junk.fileName();
	other.column = 1;
	snurra_tile *nothing = node.add_tile(other);
	QSignalSpy said(nothing, &snurra_tile::failed);
	QTRY_COMPARE_WITH_TIMEOUT(said.count(), 1, 5000);
}

/*
 * Pixels twice as wide as tall (anamorphic, as DVDs are) are shown twice
 * as wide; a clip stored sideways with a matrix saying turn it a quarter
 * (as phones record upright video) is shown turned -- the same way
 * ffmpeg's own output turns it, read back as the reference.
 */
void video_test::a_picture_is_shown_at_its_meant_shape_and_turn() {
	if (m_ffmpeg.isEmpty()) QSKIP("no ffmpeg");
	auto run = [this](const QStringList &args) {
		QProcess ffmpeg;
		ffmpeg.start(m_ffmpeg, QStringList{ QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-y") } << args);
		return ffmpeg.waitForFinished(60000) && ffmpeg.exitCode() == 0;
	};
	const QString left_white = QStringLiteral("nullsrc=s=64x48:r=25:d=1,geq=lum='if(lt(X\\,32)\\,235\\,16)':cb=128:cr=128");
	QVERIFY(run({ QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), left_white, QStringLiteral("-vf"),
	              QStringLiteral("setsar=2"), QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-pix_fmt"),
	              QStringLiteral("yuv420p"), m_dir.filePath(QStringLiteral("wide.mkv")) }));
	{
		snurra_video video(m_dir.filePath(QStringLiteral("wide.mkv")));
		QTRY_VERIFY_WITH_TIMEOUT(video.opened(), 5000);
		QCOMPARE(video.size(), QSize(128, 48));
		snurra_video_frame frame;
		QTRY_VERIFY_WITH_TIMEOUT((frame = video.frame_at(100000)).valid(), 5000);
		QCOMPARE(frame.image.size(), QSize(128, 48));
		QVERIFY(qRed(frame.image.pixel(60, 24)) > 200);   /* white to the middle */
		QVERIFY(qRed(frame.image.pixel(68, 24)) < 50);
	}

	QVERIFY(run({ QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), left_white, QStringLiteral("-c:v"),
	              QStringLiteral("libx264"), QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
	              m_dir.filePath(QStringLiteral("flat.mp4")) }));
	QVERIFY(run({ QStringLiteral("-display_rotation:v:0"), QStringLiteral("90"), QStringLiteral("-i"),
	              m_dir.filePath(QStringLiteral("flat.mp4")), QStringLiteral("-c"), QStringLiteral("copy"),
	              m_dir.filePath(QStringLiteral("turned.mp4")) }));
	/* ffmpeg turns it on decoding by default: its first frame, grey. */
	QVERIFY(run({ QStringLiteral("-i"), m_dir.filePath(QStringLiteral("turned.mp4")), QStringLiteral("-frames:v"),
	              QStringLiteral("1"), QStringLiteral("-f"), QStringLiteral("rawvideo"), QStringLiteral("-pix_fmt"),
	              QStringLiteral("gray"), m_dir.filePath(QStringLiteral("turned.gray")) }));
	QFile reference_file(m_dir.filePath(QStringLiteral("turned.gray")));
	QVERIFY(reference_file.open(QIODevice::ReadOnly));
	const QByteArray reference = reference_file.readAll();
	QCOMPARE(reference.size(), 48 * 64);   /* 48 wide, 64 tall: turned */

	snurra_video video(m_dir.filePath(QStringLiteral("turned.mp4")));
	QTRY_VERIFY_WITH_TIMEOUT(video.opened(), 5000);
	QCOMPARE(video.size(), QSize(48, 64));
	snurra_video_frame frame;
	QTRY_VERIFY_WITH_TIMEOUT((frame = video.frame_at(0)).valid(), 5000);
	QCOMPARE(frame.image.size(), QSize(48, 64));
	int differ = 0;
	for (int y = 4; y < 64; y += 8)
		for (int x = 4; x < 48; x += 8) {
			const bool ours = qRed(frame.image.pixel(x, y)) > 128;
			const bool theirs = static_cast<unsigned char>(reference[y * 48 + x]) > 128;
			if (ours != theirs) ++differ;
		}
	QCOMPARE(differ, 0);
}

/*
 * mplayer's #: a film with two sound tracks, English silent and Swedish a
 * tone. The source lists both by language and plays the first; the other
 * chosen, the next seek reads it, from exactly that frame. In a window, #
 * takes the next and says so.
 */
void video_test::a_films_other_sound_track_plays_from_where_it_is() {
	if (m_ffmpeg.isEmpty()) QSKIP("no ffmpeg");
	QProcess ffmpeg;
	ffmpeg.start(m_ffmpeg, { QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-y"), QStringLiteral("-f"),
	                         QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("testsrc=s=64x48:r=25:d=4"),
	                         QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
	                         QStringLiteral("anullsrc=r=48000:cl=stereo"), QStringLiteral("-f"), QStringLiteral("lavfi"),
	                         QStringLiteral("-i"), QStringLiteral("sine=f=1000:r=48000:d=4"), QStringLiteral("-map"),
	                         QStringLiteral("0:v"), QStringLiteral("-map"), QStringLiteral("1:a"), QStringLiteral("-map"),
	                         QStringLiteral("2:a"), QStringLiteral("-t"), QStringLiteral("4"), QStringLiteral("-c:a"),
	                         QStringLiteral("flac"), QStringLiteral("-metadata:s:a:0"), QStringLiteral("language=eng"),
	                         QStringLiteral("-metadata:s:a:1"), QStringLiteral("language=swe"), QStringLiteral("-pix_fmt"),
	                         QStringLiteral("yuv420p"), m_dir.filePath(QStringLiteral("two.mkv")) });
	QVERIFY(ffmpeg.waitForFinished(60000) && ffmpeg.exitCode() == 0);

	auto loudness = [](snurra_source &source) {
		std::vector<float> lr(2 * 4800);
		const int got = source.read(lr.data(), 4800);
		double sum = 0;
		for (int i = 0; i < 2 * got; ++i) sum += double(lr[size_t(i)]) * lr[size_t(i)];
		return got > 0 ? std::sqrt(sum / (2 * got)) : -1.0;
	};
	std::unique_ptr<snurra_source> source = snurra_open_source(m_dir.filePath(QStringLiteral("two.mkv")), 48000, 0);
	QVERIFY(source && !source->stand_in());
	QCOMPARE(source->audio_tracks(), (QStringList{ QStringLiteral("eng"), QStringLiteral("swe") }));
	QCOMPARE(source->audio_track(), 0);
	QVERIFY(loudness(*source) < 0.001);
	QVERIFY(source->choose_audio_track(1));
	QCOMPARE(source->audio_track(), 1);
	source->seek(48000);
	QCOMPARE(source->position(), int64_t(48000));
	const double tone = loudness(*source);
	QVERIFY2(tone > 0.05, qPrintable(QString::number(tone)));
	QCOMPARE(source->position(), int64_t(48000 + 4800));
	QVERIFY(!source->choose_audio_track(5));
	QVERIFY(source->choose_audio_track(0));
	source->seek(0);
	QVERIFY(loudness(*source) < 0.001);

	snurra_node node;
	snurra_tile_state state;
	state.source = m_dir.filePath(QStringLiteral("two.mkv"));
	state.playing = true;
	state.controls = true;
	state.sampled_at_us = snurra_clock_us();
	snurra_tile *tile = node.add_tile(state);
	node.show();
	QVERIFY(QTest::qWaitForWindowExposed(&node));
	QTRY_COMPARE_WITH_TIMEOUT(tile->deck()->audio_tracks().size(), 2, 5000);
	QCOMPARE(tile->deck()->audio_track(), 0);
	QTRY_VERIFY_WITH_TIMEOUT(tile->video_open(), 5000);
	QTest::keyClick(&node, Qt::Key_NumberSign);
	QTRY_COMPARE_WITH_TIMEOUT(tile->deck()->audio_track(), 1, 2000);
	QWidget *view = nullptr;
	for (QWidget *child : tile->drawer_above()->findChildren<QWidget *>())
		if (child->accessibleName() == QLatin1String("Video")) view = child;
	QVERIFY(view);
	QCOMPARE(view->property("notice").toString(), QStringLiteral("Sound: swe"));
	QTest::keyClick(&node, Qt::Key_NumberSign);
	QTRY_COMPARE_WITH_TIMEOUT(tile->deck()->audio_track(), 0, 2000);
}

int run_video_tests(int argc, char **argv) {
	video_test test;
	return QTest::qExec(&test, argc, argv);
}

#include "video_test.moc"

/*
 * Stems (project.md sec 3e, step 1): a .stem.mp4 of five sound tracks --
 * the mix, then drums, bass, other and vocals -- where track k is silent
 * until k * 0.4 s, so what is read says which track it is. Dropped on an
 * empty window it is four lanes, one stem each from the start, the mix
 * left out; each deck opens on its stem, the title says which, and the
 * waveform is read from the stem too. Dropped again, four more lanes
 * below. A handover carries the track.
 */
void video_test::a_stem_file_is_a_stack_of_its_stems() {
	if (m_ffmpeg.isEmpty()) QSKIP("no ffmpeg");
	const QString path = m_dir.filePath(QStringLiteral("song.stem.mp4"));
	QStringList args{ QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-y") };
	for (int k = 0; k < 5; ++k)
		args << QStringLiteral("-f") << QStringLiteral("lavfi") << QStringLiteral("-i")
		     << QStringLiteral("aevalsrc='if(gte(t,%1),0.5*sin(2*PI*440*t),0)':d=2:s=48000").arg(k * 0.4);
	for (int k = 0; k < 5; ++k) args << QStringLiteral("-map") << QString::number(k);
	args << QStringLiteral("-c:a") << QStringLiteral("aac") << path;
	QProcess ffmpeg;
	ffmpeg.start(m_ffmpeg, args);
	QVERIFY(ffmpeg.waitForFinished(60000) && ffmpeg.exitCode() == 0);
	QCOMPARE(snurra_stem_names(path).size(), 4);
	QVERIFY(snurra_stem_names(m_dir.filePath(QStringLiteral("song.mp4"))).isEmpty());

	/* Each track's own: the first loud column comes k * 0.4 s in. */
	for (int k = 0; k < 5; ++k) {
		std::vector<float> columns(100, -1);
		std::atomic<int> done{ 0 };
		{
			snurra_peaks peaks(path, 48000, 100, [&](int first, const std::vector<float> &values) {
				for (size_t i = 0; i < values.size(); ++i) columns[size_t(first) + i] = values[i];
				done += int(values.size());
			}, k);
			QTRY_COMPARE_WITH_TIMEOUT(done.load(), 100, 10000);
		}
		int loud = 0;
		while (loud < 100 && columns[size_t(loud)] < 0.1f) ++loud;
		QVERIFY2(qAbs(loud - k * 20) <= 2, qPrintable(QStringLiteral("track %1 is loud from column %2").arg(k).arg(loud)));
	}

	snurra_node node;
	node.add_files({ path }, snurra_wire::RIGHT);
	QCOMPARE(node.tile_count(), 4);
	const QStringList stems{ QStringLiteral("drums"), QStringLiteral("bass"), QStringLiteral("other"), QStringLiteral("vocals") };
	QList<snurra_tile *> lanes(4, nullptr);
	for (snurra_tile *tile : node.findChildren<snurra_tile *>()) {
		QCOMPARE(tile->column(), 0);
		QVERIFY(tile->row() >= 0 && tile->row() < 4);
		lanes[tile->row()] = tile;
	}
	for (int i = 0; i < 4; ++i) {
		QVERIFY(lanes[i]);
		QTRY_COMPARE_WITH_TIMEOUT(lanes[i]->deck()->audio_track(), i + 1, 5000);
		QCOMPARE(lanes[i]->state().audio_track, i + 1);
		QLabel *title = lanes[i]->findChild<QLabel *>(QStringLiteral("snurra-handle"));
		QVERIFY(title);
		QCOMPARE(title->text(), QStringLiteral("song.stem.mp4 -- ") + stems[i]);
	}
	node.add_files({ path }, snurra_wire::RIGHT);
	QCOMPARE(node.tile_count(), 8);
	QList<int> rows;
	for (snurra_tile *tile : node.findChildren<snurra_tile *>()) rows << tile->row();
	std::sort(rows.begin(), rows.end());
	QCOMPARE(rows, (QList<int>{ 0, 1, 2, 3, 4, 5, 6, 7 }));

	QByteArray bytes;
	QDataStream out(&bytes, QIODevice::WriteOnly);
	out << lanes[2]->state();
	snurra_tile_state back;
	QDataStream in(bytes);
	in >> back;
	QCOMPARE(back.audio_track, 3);
}

/*
 * Colour, not grey alone (2026-11-13, when the conversion went to whole
 * numbers): solid colours encoded as BT.601 and BT.709, limited range,
 * and full range, read back through snurra and through ffmpeg's own RGB,
 * the reference -- within a few steps of 255, what 4:2:0 and rounding
 * leave. A matrix taken wrong, or a channel swapped, is tens off.
 */
void video_test::colours_are_ffmpegs_own_data() {
	QTest::addColumn<QStringList>("coding");
	QTest::newRow("BT.601 limited") << QStringList{ QStringLiteral("-vf"), QStringLiteral("format=yuv420p") };
	QTest::newRow("BT.709 limited") << QStringList{ QStringLiteral("-vf"), QStringLiteral("scale=out_color_matrix=bt709,format=yuv420p"),
		                                         QStringLiteral("-colorspace"), QStringLiteral("bt709") };
	QTest::newRow("BT.601 full") << QStringList{ QStringLiteral("-vf"), QStringLiteral("scale=out_range=full,format=yuvj420p"),
		                                      QStringLiteral("-color_range"), QStringLiteral("pc") };
}

void video_test::colours_are_ffmpegs_own() {
	if (m_ffmpeg.isEmpty()) QSKIP("no ffmpeg");
	QFETCH(QStringList, coding);
	const QStringList colours{ QStringLiteral("0xe03020"), QStringLiteral("0x20c040"), QStringLiteral("0x3040e0"),
		                   QStringLiteral("0xd8a080"), QStringLiteral("0xc020c0") };
	for (const QString &colour : colours) {
		const QString path = m_dir.filePath(QStringLiteral("colour.mkv"));
		QProcess make;
		make.start(m_ffmpeg, QStringList{ QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-y"), QStringLiteral("-f"),
		                                  QStringLiteral("lavfi"), QStringLiteral("-i"),
		                                  QStringLiteral("color=c=%1:s=64x48:r=25:d=1").arg(colour) }
		                         << coding << QStringList{ QStringLiteral("-c:v"), QStringLiteral("ffv1"), path });
		QVERIFY(make.waitForFinished(60000) && make.exitCode() == 0);
		QProcess reference;
		reference.start(m_ffmpeg, { QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-i"), path, QStringLiteral("-frames:v"),
		                            QStringLiteral("1"), QStringLiteral("-f"), QStringLiteral("rawvideo"), QStringLiteral("-pix_fmt"),
		                            QStringLiteral("rgb24"), QStringLiteral("-") });
		QVERIFY(reference.waitForFinished(60000));
		const QByteArray rgb = reference.readAllStandardOutput();
		QCOMPARE(rgb.size(), 64 * 48 * 3);
		const int middle = (24 * 64 + 32) * 3;
		snurra_video video(path);
		QTRY_VERIFY_WITH_TIMEOUT(video.opened(), 5000);
		snurra_video_frame frame;
		QTRY_VERIFY_WITH_TIMEOUT((frame = video.frame_at(100000)).valid(), 5000);
		const QRgb ours = frame.image.pixel(32, 24);
		const int want[3] = { uchar(rgb[middle]), uchar(rgb[middle + 1]), uchar(rgb[middle + 2]) };
		const int got[3] = { qRed(ours), qGreen(ours), qBlue(ours) };
		for (int c = 0; c < 3; ++c)
			QVERIFY2(qAbs(want[c] - got[c]) <= 4, qPrintable(QStringLiteral("%1: ffmpeg %2,%3,%4, snurra %5,%6,%7")
			                                                         .arg(colour).arg(want[0]).arg(want[1]).arg(want[2])
			                                                         .arg(got[0]).arg(got[1]).arg(got[2])));
	}
}

/*
 * The full-screen button, as YouTube's (the holder, 2026-11-13): hidden
 * on a still picture, in its bottom right corner while the pointer moves
 * over it, gone again after; pressed, full screen, where the same button
 * -- now "Leave full screen" -- comes with a move and brings it back.
 */
void video_test::a_full_screen_button_shows_while_the_pointer_moves() {
	if (m_ffmpeg.isEmpty()) QSKIP("no ffmpeg");
	const QString name = QStringLiteral("button.mp4");
	QVERIFY(make(name, { QStringLiteral("-c:v"), QStringLiteral("libx264") }, 0));
	snurra_node node;
	snurra_tile_state state;
	state.source = m_dir.filePath(name);
	state.controls = true;
	state.playing = false;
	snurra_tile *tile = node.add_tile(state);
	node.show();
	QTRY_VERIFY_WITH_TIMEOUT(tile->video_open(), 5000);
	QWidget *small = nullptr;
	for (QWidget *w : node.findChildren<QWidget *>())
		if (w->accessibleName() == QLatin1String("Video")) small = w;
	QVERIFY(small);
	auto button_of = [](QWidget *view) {
		for (QToolButton *b : view->findChildren<QToolButton *>(QString(), Qt::FindDirectChildrenOnly)) return b;
		return static_cast<QToolButton *>(nullptr);
	};
	auto move_over = [](QWidget *view) {
		const QPointF at = view->rect().center();
		QMouseEvent move(QEvent::MouseMove, at, view->mapToGlobal(at), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
		QApplication::sendEvent(view, &move);
	};
	QToolButton *button = button_of(small);
	QVERIFY(button);
	QVERIFY2(!button->isVisible(), "the button is on a still picture");
	move_over(small);
	QVERIFY(button->isVisible());
	QVERIFY2(button->geometry().right() > small->width() * 3 / 4 && button->geometry().bottom() > small->height() * 3 / 4,
	         "the button is not in the bottom right corner");
	QTRY_VERIFY_WITH_TIMEOUT(!button->isVisible(), 3000);
	move_over(small);
	button->click();
	QTRY_VERIFY_WITH_TIMEOUT(tile->full_screen(), 3000);
	QWidget *full = nullptr;
	for (QWidget *top : QApplication::topLevelWidgets())
		if (top->accessibleName() == QLatin1String("Video") && top->isVisible()) full = top;
	QVERIFY(full);
	QToolButton *leave = button_of(full);
	QVERIFY(leave);
	move_over(full);
	QVERIFY(leave->isVisible());
	QCOMPARE(leave->accessibleName(), QStringLiteral("Leave full screen"));
	leave->click();
	QTRY_VERIFY_WITH_TIMEOUT(!tile->full_screen(), 3000);
}

/*
 * A picture is made as wide as it is shown (2026-11-13): a 640 wide film
 * asked for 200 is made 200 wide, its shape kept, and asked for 4000 is
 * made at its own 640, never up. In a tile, the drawer's width; full
 * screen, the screen's.
 */
void video_test::a_picture_is_made_as_wide_as_it_is_shown() {
	if (m_ffmpeg.isEmpty()) QSKIP("no ffmpeg");
	const QString path = m_dir.filePath(QStringLiteral("wide640.mp4"));
	QProcess ffmpeg;
	ffmpeg.start(m_ffmpeg, { QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"),
	                         QStringLiteral("-i"), QStringLiteral("testsrc2=s=640x480:r=25:d=4"), QStringLiteral("-f"), QStringLiteral("lavfi"),
	                         QStringLiteral("-i"), QStringLiteral("sine=d=4"), QStringLiteral("-c:v"), QStringLiteral("libx264"),
	                         QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), path });
	QVERIFY(ffmpeg.waitForFinished(60000) && ffmpeg.exitCode() == 0);
	{
		snurra_video video(path);
		video.set_shown_width(200);
		QTRY_VERIFY_WITH_TIMEOUT(video.opened(), 5000);
		snurra_video_frame frame;
		QTRY_VERIFY_WITH_TIMEOUT((frame = video.frame_at(0)).valid(), 5000);
		QCOMPARE(frame.image.size(), QSize(200, 150));
	}
	{
		snurra_video video(path);
		video.set_shown_width(4000);
		QTRY_VERIFY_WITH_TIMEOUT(video.opened(), 5000);
		snurra_video_frame frame;
		QTRY_VERIFY_WITH_TIMEOUT((frame = video.frame_at(0)).valid(), 5000);
		QCOMPARE(frame.image.size(), QSize(640, 480));
	}
	snurra_node node;
	snurra_tile_state state;
	state.source = path;
	state.controls = true;
	state.playing = true;
	state.sampled_at_us = snurra_clock_us();
	snurra_tile *tile = node.add_tile(state);
	node.show();
	QTRY_VERIFY_WITH_TIMEOUT(tile->video_open(), 5000);
	QWidget *small = nullptr;
	for (QWidget *w : node.findChildren<QWidget *>())
		if (w->accessibleName() == QLatin1String("Video")) small = w;
	QVERIFY(small && small->width() < 640);
	/* The picture fitted in the view, 4:3, in device pixels. */
	auto fitted = [](const QWidget *view) {
		return int(std::lround(std::min(double(view->width()), view->height() * 4.0 / 3) * view->devicePixelRatioF()));
	};
	QTRY_COMPARE_WITH_TIMEOUT(small->property("made").toSize().width(), fitted(small), 3000);
	tile->set_full_screen(true);
	QTRY_VERIFY_WITH_TIMEOUT(tile->full_screen(), 3000);
	QWidget *full = nullptr;
	for (QWidget *top : QApplication::topLevelWidgets())
		if (top->accessibleName() == QLatin1String("Video") && top->isVisible()) full = top;
	QVERIFY(full);
	QTRY_COMPARE_WITH_TIMEOUT(full->property("made").toSize().width(), qMin(640, fitted(full)), 3000);
	/* Fitted, not the view's width: a screen wider than the picture's
	 * shape (the holder's 3440 x 1440 with a 16:9 film) had it made as
	 * wide as the screen and scaled down again on every paint. */
	full->resize(1200, 300);
	QTRY_COMPARE_WITH_TIMEOUT(full->property("made").toSize().width(), 400, 3000);
	tile->set_full_screen(false);
}

/*
 * A host that gives a local film as both its bytes and its path, as
 * BeerSSH does (inbox 20261113T0834Z): the .srt beside the path is found,
 * not looked for beside the bytes' name alone.
 */
void video_test::a_local_films_srt_is_found_when_its_bytes_are_given() {
	if (m_ffmpeg.isEmpty()) QSKIP("no ffmpeg");
	QVERIFY(QDir(m_dir.path()).mkpath(QStringLiteral("host")));
	const QString name = QStringLiteral("host/film.mp4");
	QVERIFY(make(name, { QStringLiteral("-c:v"), QStringLiteral("libx264") }, 0));
	QFile srt(m_dir.filePath(QStringLiteral("host/film.srt")));
	QVERIFY(srt.open(QIODevice::WriteOnly));
	srt.write("1\n00:00:00,000 --> 00:00:03,000\nBESIDE THE FILE\n\n");
	srt.close();
	QFile file(m_dir.filePath(name));
	QVERIFY(file.open(QIODevice::ReadOnly));
	auto bytes = std::make_shared<memory_bytes>(file.readAll());
	bytes->arrive(0, bytes->size());
	snurra_media media;
	media.name = QStringLiteral("film.mp4");
	media.path = m_dir.filePath(name);
	media.bytes = bytes;
	snurra_player player;
	QVERIFY(player.open(media));
	snurra_tile *tile = player.findChild<snurra_tile *>();
	QVERIFY(tile);
	QTRY_COMPARE_WITH_TIMEOUT(tile->subtitles_name(), QStringLiteral("film.srt"), 5000);
}

/*
 * Anti-aliasing (the holder, 2026-11-13): a picture of one-pixel black and
 * white stripes, 1280 wide, shown 160 wide is grey -- each pixel the mean
 * of the eight stripes it covers -- not black or white as one stripe in
 * eight picked would make it.
 */
void video_test::a_picture_made_small_is_averaged_not_picked() {
	if (m_ffmpeg.isEmpty()) QSKIP("no ffmpeg");
	const QString path = m_dir.filePath(QStringLiteral("stripes.mkv"));
	QProcess ffmpeg;
	ffmpeg.start(m_ffmpeg, { QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"),
	                         QStringLiteral("-i"), QStringLiteral("nullsrc=s=1280x720:r=25:d=1,geq=lum='if(mod(X\\,2)\\,235\\,16)':cb=128:cr=128"),
	                         QStringLiteral("-c:v"), QStringLiteral("ffv1"), QStringLiteral("-pix_fmt"), QStringLiteral("yuv444p"), path });
	QVERIFY(ffmpeg.waitForFinished(60000) && ffmpeg.exitCode() == 0);
	snurra_video video(path);
	video.set_shown_width(160);
	QTRY_VERIFY_WITH_TIMEOUT(video.opened(), 5000);
	snurra_video_frame frame;
	QTRY_VERIFY_WITH_TIMEOUT((frame = video.frame_at(0)).valid(), 5000);
	QCOMPARE(frame.image.width(), 160);
	int darkest = 255;
	int lightest = 0;
	for (int x = 2; x < 158; ++x) {
		const int grey = qRed(frame.image.pixel(x, 45));
		darkest = std::min(darkest, grey);
		lightest = std::max(lightest, grey);
	}
	QVERIFY2(darkest > 90 && lightest < 165, qPrintable(QStringLiteral("from %1 to %2, not grey").arg(darkest).arg(lightest)));
}

/*
 * A skip while playing (the holder, 2026-11-13: "slight video jitter"):
 * a film with a key frame each 10 s, skipped 9 s on, makes the reader
 * decode some 280 frames from the key frame while the sound goes on. The
 * first picture shown after it is the frame heard then, within a few
 * frames -- not the frame the skip asked for, by then behind the sound,
 * followed by every frame since.
 */
void video_test::a_skip_shows_the_frame_heard_not_one_behind() {
	if (m_ffmpeg.isEmpty()) QSKIP("no ffmpeg");
	const QString path = m_dir.filePath(QStringLiteral("longgop.mp4"));
	QProcess ffmpeg;
	ffmpeg.start(m_ffmpeg, { QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"),
	                         QStringLiteral("-i"), QStringLiteral("testsrc2=s=1920x1080:r=30:d=20"), QStringLiteral("-f"), QStringLiteral("lavfi"),
	                         QStringLiteral("-i"), QStringLiteral("sine=d=20"), QStringLiteral("-c:v"), QStringLiteral("libx264"),
	                         QStringLiteral("-preset"), QStringLiteral("veryfast"), QStringLiteral("-g"), QStringLiteral("300"),
	                         QStringLiteral("-keyint_min"), QStringLiteral("300"), QStringLiteral("-sc_threshold"), QStringLiteral("0"),
	                         QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), path });
	QVERIFY(ffmpeg.waitForFinished(120000) && ffmpeg.exitCode() == 0);
	snurra_node node;
	snurra_tile_state state;
	state.source = path;
	state.controls = true;
	state.playing = true;
	state.sampled_at_us = snurra_clock_us();
	snurra_tile *tile = node.add_tile(state);
	node.show();
	QTRY_VERIFY_WITH_TIMEOUT(tile->video_open(), 5000);
	QWidget *view = nullptr;
	for (QWidget *w : node.findChildren<QWidget *>())
		if (w->accessibleName() == QLatin1String("Video")) view = w;
	QVERIFY(view);
	QTest::qWait(600);
	tile->seek_by(9000000);
	QElapsedTimer waited;
	waited.start();
	qint64 shown = -1;
	while (waited.elapsed() < 5000) {
		QTest::qWait(2);
		const qint64 pts = view->property("pts").toLongLong();
		if (pts >= 9000000) {
			shown = pts;
			break;
		}
	}
	QVERIFY2(shown >= 0, "no picture after the skip");
	const qint64 heard = qint64(snurra_frames_to_us(tile->frame_now(), tile->deck()->rate()));
	qInfo("after the skip: picture %lld us, sound %lld us, %lld ms behind", shown, heard, (heard - shown) / 1000);
	QVERIFY2(heard - shown < 150000, qPrintable(QStringLiteral("the first picture after the skip, %1 us, is %2 ms behind the sound at %3 us")
	                                                    .arg(shown).arg((heard - shown) / 1000).arg(heard)));
}

/*
 * A jump while the reader is still on its way somewhere (the holder,
 * 2026-11-13: "jumping takes a long time to load video"): a key frame
 * each 20 s; asked for 19.9 s, the reader decodes from the start; asked
 * for 45 s while it is on its way, it seeks to the key frame at 40 s --
 * some 150 frames more -- rather than decode on through every frame to
 * 45 s (some 1350).
 */
void video_test::a_far_target_while_on_the_way_is_a_new_seek() {
	if (m_ffmpeg.isEmpty()) QSKIP("no ffmpeg");
	const QString path = m_dir.filePath(QStringLiteral("keys20.mp4"));
	QProcess ffmpeg;
	ffmpeg.start(m_ffmpeg, { QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"),
	                         QStringLiteral("-i"), QStringLiteral("testsrc2=s=640x360:r=30:d=50"), QStringLiteral("-f"), QStringLiteral("lavfi"),
	                         QStringLiteral("-i"), QStringLiteral("sine=d=50"), QStringLiteral("-c:v"), QStringLiteral("libx264"),
	                         QStringLiteral("-preset"), QStringLiteral("ultrafast"), QStringLiteral("-g"), QStringLiteral("600"),
	                         QStringLiteral("-keyint_min"), QStringLiteral("600"), QStringLiteral("-sc_threshold"), QStringLiteral("0"),
	                         QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), path });
	QVERIFY(ffmpeg.waitForFinished(120000) && ffmpeg.exitCode() == 0);
	snurra_video video(path);
	QTRY_VERIFY_WITH_TIMEOUT(video.opened(), 5000);
	video.frame_at(0);
	QTRY_VERIFY_WITH_TIMEOUT(video.frame_at(0).valid(), 5000);
	const int64_t at_start = video.decoded();
	video.frame_at(19900000);
	QTRY_VERIFY_WITH_TIMEOUT(video.decoded() > at_start + 30, 5000);
	snurra_video_frame frame;
	QTRY_VERIFY_WITH_TIMEOUT((frame = video.frame_at(45000000)).valid() && frame.pts_us > 44900000, 15000);
	qInfo("45 s while on the way to 19.9 s: %lld pictures decoded", (long long)video.decoded());
	QVERIFY2(video.decoded() < 1000, qPrintable(QStringLiteral("%1 pictures decoded").arg(video.decoded())));
}

/*
 * The library's names and artists (2026-11-13): a FLAC and an Opus
 * tagged with title and artist show them -- the Opus's on its stream, as
 * Ogg keeps them -- and an untagged file keeps its own name, a YouTube ID
 * and all (the holder: no cleanup removes those); search finds a file by
 * its artist. No file is renamed.
 */
void video_test::the_library_shows_a_files_own_title_and_artist() {
	if (m_ffmpeg.isEmpty()) QSKIP("no ffmpeg");
	QTemporaryDir dir;
	auto make_tagged = [this, &dir](const QString &name, const QStringList &tags) {
		QProcess ffmpeg;
		ffmpeg.start(m_ffmpeg, QStringList{ QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-y"), QStringLiteral("-f"),
		                                    QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("sine=d=1") }
		                               << tags << dir.filePath(name));
		return ffmpeg.waitForFinished(30000) && ffmpeg.exitCode() == 0;
	};
	QVERIFY(make_tagged(QStringLiteral("01 track.flac"), { QStringLiteral("-metadata"), QStringLiteral("title=Night Drive"),
		                                               QStringLiteral("-metadata"), QStringLiteral("artist=Somebody") }));
	QVERIFY(make_tagged(QStringLiteral("02 track.opus"), { QStringLiteral("-c:a"), QStringLiteral("libopus"), QStringLiteral("-metadata"),
		                                               QStringLiteral("title=Morning"), QStringLiteral("-metadata"),
		                                               QStringLiteral("artist=Someone Else") }));
	const QString untagged = QStringLiteral("HI-LO - Impulse (Official Music Video)-FYLJdKxTxJ0");
	QVERIFY(make_tagged(untagged + QStringLiteral(".wav"), {}));
	const QStringList before = QDir(dir.path()).entryList(QDir::Files, QDir::Name);

	snurra_library library(dir.path());
	QTRY_VERIFY_WITH_TIMEOUT(!library.scanning(), 5000);
	QTreeView *view = library.findChild<QTreeView *>();
	QAbstractItemModel *model = view->model();
	auto row_of = [&library](const QString &file) { return int(library.shown().indexOf(file)); };
	const int flac = row_of(dir.filePath(QStringLiteral("01 track.flac")));
	const int opus = row_of(dir.filePath(QStringLiteral("02 track.opus")));
	const int wav = row_of(dir.filePath(untagged + QStringLiteral(".wav")));
	QVERIFY(flac >= 0 && opus >= 0 && wav >= 0);
	QTRY_COMPARE_WITH_TIMEOUT(model->index(flac, 0).data().toString(), QStringLiteral("Night Drive"), 5000);
	QCOMPARE(model->index(flac, 1).data().toString(), QStringLiteral("Somebody"));
	QTRY_COMPARE_WITH_TIMEOUT(model->index(opus, 0).data().toString(), QStringLiteral("Morning"), 5000);
	QCOMPARE(model->index(opus, 1).data().toString(), QStringLiteral("Someone Else"));
	QCOMPARE(model->index(wav, 0).data().toString(), untagged);
	QVERIFY(model->index(wav, 1).data().toString().isEmpty());

	library.set_search(QStringLiteral("somebody"));
	QCOMPARE(library.shown(), QStringList{ dir.filePath(QStringLiteral("01 track.flac")) });
	QCOMPARE(QDir(dir.path()).entryList(QDir::Files, QDir::Name), before);
}

/*
 * Chapters, a mix's tracklist (hydra's saved mixes, inbox 20261113T1442Z):
 * a Matroska file of three, 0, 3 and 6 s. The tile has them, says the one
 * playing after the file's name, and mplayer's @ and ! step between them.
 */
void video_test::a_mixs_chapters_are_its_tracklist() {
	if (m_ffmpeg.isEmpty()) QSKIP("no ffmpeg");
	QTemporaryDir dir;
	const QString meta = dir.filePath(QStringLiteral("chapters.txt"));
	QFile file(meta);
	QVERIFY(file.open(QIODevice::WriteOnly));
	file.write(";FFMETADATA1\n"
	           "[CHAPTER]\nTIMEBASE=1/1000\nSTART=0\nEND=3000\ntitle=One - First\n"
	           "[CHAPTER]\nTIMEBASE=1/1000\nSTART=3000\nEND=6000\ntitle=Two - Second\n"
	           "[CHAPTER]\nTIMEBASE=1/1000\nSTART=6000\nEND=9000\ntitle=Three - Third\n");
	file.close();
	const QString mix = dir.filePath(QStringLiteral("mix.mka"));
	QProcess ffmpeg;
	ffmpeg.start(m_ffmpeg, { QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"),
	                         QStringLiteral("-i"), QStringLiteral("sine=d=9"), QStringLiteral("-i"), meta, QStringLiteral("-map_metadata"),
	                         QStringLiteral("1"), QStringLiteral("-map_chapters"), QStringLiteral("1"), QStringLiteral("-c:a"),
	                         QStringLiteral("libopus"), mix });
	QVERIFY(ffmpeg.waitForFinished(30000) && ffmpeg.exitCode() == 0);
	const std::vector<snurra_chapter> read = snurra_read_chapters(mix);
	QCOMPARE(int(read.size()), 3);
	QVERIFY(qAbs(read[1].start_us - 3000000) < 30000);
	QCOMPARE(read[2].title, QStringLiteral("Three - Third"));

	snurra_node node;
	snurra_tile_state state;
	state.source = mix;
	state.controls = true;
	state.duration_us = 9000000;
	snurra_tile *tile = node.add_tile(state);
	node.show();
	QVERIFY(QTest::qWaitForWindowExposed(&node));
	QCOMPARE(tile->chapter_count(), 3);
	QTRY_VERIFY_WITH_TIMEOUT(tile->deck()->length() > 0, 3000);
	tile->hold_at(snurra_us_to_frames(4000000, tile->deck()->rate()));
	QTRY_COMPARE_WITH_TIMEOUT(tile->chapter_title(), QStringLiteral("Two - Second"), 2000);
	QLabel *title = tile->findChild<QLabel *>(QStringLiteral("snurra-handle"));
	QVERIFY(title && title->text().endsWith(QStringLiteral(" -- Two - Second")));
	QTest::keyClick(&node, Qt::Key_At);
	QTRY_VERIFY_WITH_TIMEOUT(qAbs(tile->state().position_us - read[2].start_us) < 30000, 2000);
	QTRY_COMPARE_WITH_TIMEOUT(tile->chapter_title(), QStringLiteral("Three - Third"), 2000);
	QTest::keyClick(&node, Qt::Key_Exclam);
	QTRY_VERIFY_WITH_TIMEOUT(qAbs(tile->state().position_us - read[1].start_us) < 30000, 2000);
	QTest::keyClick(&node, Qt::Key_Exclam);
	QTest::keyClick(&node, Qt::Key_Exclam);
	QTRY_VERIFY_WITH_TIMEOUT(tile->state().position_us < 30000, 2000);
}
