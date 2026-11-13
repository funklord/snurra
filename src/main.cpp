#include <QApplication>

#include <cstdio>

#ifndef SNURRA_VERSION_TEXT
#define SNURRA_VERSION_TEXT "unknown"   /* built without VERSION's word */
#endif
#include <QCommandLineParser>
#include <QCursor>
#include <QFileInfo>
#include <QGuiApplication>
#include <QScreen>
#include <QSocketNotifier>

#include <csignal>
#include <fcntl.h>
#include <unistd.h>

#include "snurra/clock.h"
#include "snurra/library.h"
#include "snurra/node.h"
#include "snurra/source.h"
#include "snurra/theme.h"
#include "snurra/tile.h"
#include "snurra/wire.h"

namespace {

int quit_pipe[2] = { -1, -1 };

void ask_to_quit(int) {
	const char byte = 0;
	(void)!write(quit_pipe[1], &byte, 1);
}

/*
 * SIGTERM and SIGINT end the event loop rather than the process, so the
 * engine closes its output: a shared ALSA device (dmix) frees its shared
 * memory only when its last client closes, and a player killed at logout
 * left two segments and a semaphore set behind each time. The handler
 * only writes a byte; the loop reads it.
 */
void quit_on_signals(QCoreApplication &app) {
	if (pipe(quit_pipe) != 0) return;
	fcntl(quit_pipe[0], F_SETFD, FD_CLOEXEC);
	fcntl(quit_pipe[1], F_SETFD, FD_CLOEXEC);
	fcntl(quit_pipe[1], F_SETFL, O_NONBLOCK);
	QSocketNotifier *notifier = new QSocketNotifier(quit_pipe[0], QSocketNotifier::Read, &app);
	QObject::connect(notifier, &QSocketNotifier::activated, &app, [] { QCoreApplication::quit(); });
	struct sigaction action {};
	action.sa_handler = ask_to_quit;
	sigemptyset(&action.sa_mask);
	action.sa_flags = SA_RESTART;
	sigaction(SIGTERM, &action, nullptr);
	sigaction(SIGINT, &action, nullptr);
}

/*
 * A time as mplayer's -ss takes one: seconds, or minutes:seconds, or
 * hours:minutes:seconds, the last part with a fraction if it likes. -1 for
 * what is none of these.
 */
qint64 parse_time_us(const QString &text) {
	const QStringList parts = text.trimmed().split(QLatin1Char(':'));
	if (parts.size() > 3) return -1;
	double seconds = 0;
	for (int i = 0; i < parts.size(); ++i) {
		bool ok = false;
		const double part = i + 1 < parts.size() ? double(parts.at(i).toUInt(&ok)) : parts.at(i).toDouble(&ok);
		if (!ok || part < 0) return -1;
		seconds = seconds * 60 + part;
	}
	return qint64(seconds * 1e6 + 0.5);
}

}

/*
 * snurra: one tile per process (project.md sec 3b). Started with a file,
 * it opens a tile for it at the pointer -- where the file was clicked in a
 * file manager, on X11. Started with --adopt, it is taking a tile another
 * snurra is letting go, and shows nothing until that tile arrives.
 */
int main(int argc, char *argv[]) {
	QApplication app(argc, argv);
	/* Dark where the desktop is (project.md sec 3b), in snurra's own
	 * colours; a host embedding the library keeps its own palette. */
	snurra_follow_desktop(app);
	QCoreApplication::setApplicationName(QStringLiteral("snurra"));
	quit_on_signals(app);

	QCommandLineParser parser;
	/* mplayer's options are one dash and a word (-ss, -fs), and are what
	 * the hands type; --ss works as well. */
	parser.setSingleDashWordOptionMode(QCommandLineParser::ParseAsLongOptions);
	parser.addHelpOption();
	parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("What to play."));
	const QCommandLineOption adopt(QStringLiteral("adopt"), QStringLiteral("Take a tile from this socket."),
	                               QStringLiteral("socket"));
	const QCommandLineOption token(QStringLiteral("token"), QStringLiteral("The handover's token."),
	                               QStringLiteral("token"));
	const QCommandLineOption index(QStringLiteral("index"), QStringLiteral("Which tile."), QStringLiteral("n"));
	const QCommandLineOption at(QStringLiteral("at"), QStringLiteral("Where, as x,y."), QStringLiteral("x,y"));
	const QCommandLineOption paused(QStringLiteral("paused"), QStringLiteral("Open without playing."));
	const QCommandLineOption output(QStringLiteral("output"),
	                                QStringLiteral("Where to play: alsa:<device>, aaudio[:exclusive], qt, null."),
	                                QStringLiteral("spec"));
	const QCommandLineOption layout(QStringLiteral("layout"),
	                                QStringLiteral("The look: drawers (the normal one, where the window system can), or square."),
	                                QStringLiteral("look"));
	const QCommandLineOption version(QStringLiteral("version"), QStringLiteral("Say which snurra this is, and whose."));
	/* mplayer's, by the names its users type: where to start, the picture
	 * full screen, a subtitle file, the level in per cent, the speed. */
	const QCommandLineOption start_at(QStringList{ QStringLiteral("ss"), QStringLiteral("start") },
	                                  QStringLiteral("Start there: seconds, m:ss or h:mm:ss."), QStringLiteral("time"));
	const QCommandLineOption full_screen(QStringList{ QStringLiteral("fs"), QStringLiteral("full-screen") },
	                                     QStringLiteral("Show the picture full screen."));
	const QCommandLineOption subtitles(QStringLiteral("sub"), QStringLiteral("Subtitle files, comma-separated, before those beside the file."),
	                                   QStringLiteral("file"));
	const QCommandLineOption volume(QStringLiteral("volume"), QStringLiteral("The level, 0 to 100."), QStringLiteral("per-cent"));
	const QCommandLineOption speed(QStringLiteral("speed"), QStringLiteral("The speed, 0.25 to 4."), QStringLiteral("factor"));
	const QCommandLineOption playlist(QStringLiteral("playlist"), QStringLiteral("Play the files a playlist names (.m3u, .pls), as a train."),
	                                  QStringLiteral("file"));
	const QCommandLineOption loop(QStringLiteral("loop"), QStringLiteral("Play it all this many times; 0 for ever."), QStringLiteral("times"));
	parser.addOptions({ adopt, token, index, at, paused, output, layout, version, start_at, full_screen, subtitles, volume, speed,
	                    playlist, loop });
	parser.process(app);
	/* The first line keeps its shape for whatever reads it; the copyright
	 * holder on a line of their own. */
	if (parser.isSet(version)) {
		std::printf("snurra %s\nCopyright (C) 2026 Nabeel Sowan <nabeel@vibes.se>\n", SNURRA_VERSION_TEXT);
		return 0;
	}
	if (parser.isSet(layout)) qputenv("SNURRA_LAYOUT", parser.value(layout).toLocal8Bit());
	/* As $SNURRA_OUTPUT, which the engine reads -- and which an app on
	 * Android cannot be given, its arguments being all it has. A process
	 * this one starts to adopt a tile inherits it. */
	if (parser.isSet(output)) qputenv("SNURRA_OUTPUT", parser.value(output).toLocal8Bit());

	snurra_node node;
	if (!node.listen()) return 1;

	if (parser.isSet(adopt)) {
		const QStringList xy = parser.value(at).split(QLatin1Char(','));
		const QPoint where = xy.size() == 2 ? QPoint(xy.at(0).toInt(), xy.at(1).toInt()) : QPoint();
		node.adopt(parser.value(adopt), parser.value(token), parser.value(index).toInt(), where);
		return app.exec();
	}

	/*
	 * Only the first snurra opens with controls and plays (project.md sec
	 * 3c); one opened while another runs is a waveform, paused, until its
	 * controls are pulled out. Within one launch the first file is first.
	 */
	/* A playlist, named or given with -playlist as mplayer's, is the files
	 * it names, in its place in the train. */
	QStringList files;
	for (const QString &name : parser.values(playlist)) files << snurra_playlist(name);
	for (const QString &name : parser.positionalArguments())
		files << (snurra_is_playlist(name) && QFileInfo(name).isFile() ? snurra_playlist(name) : QStringList{ name });
	qint64 start_us = 0;
	if (parser.isSet(start_at) && (start_us = parse_time_us(parser.value(start_at))) < 0) {
		std::fprintf(stderr, "snurra: -ss takes seconds, m:ss or h:mm:ss, not \"%s\"\n", qPrintable(parser.value(start_at)));
		return 2;
	}
	bool first = !snurra_wire::any_peer_alive(node.socket_path());
	int column = 0;
	for (const QString &file : files) {
		snurra_tile_state state;
		state.source = QFileInfo::exists(file) ? QFileInfo(file).absoluteFilePath() : file;
		/* Three minutes for what has no length of its own: silence, for
		 * a name nothing can play, still behaves as a tile. Past where a
		 * start is asked for, which this length would otherwise clamp. */
		state.duration_us = 180000000 + (column == 0 ? start_us : 0);
		/* The start, the full screen and the subtitles are the first
		 * file's: a launch of several is a train, started at its head. */
		if (column == 0) state.position_us = start_us;
		/* Given to the deck as it opens, so the first block is heard at
		 * them; the tile clamps both. */
		if (parser.isSet(volume)) state.level = qBound(0, parser.value(volume).toInt(), 100);
		if (parser.isSet(speed)) state.tempo = parser.value(speed).toFloat();
		state.sampled_at_us = snurra_clock_us();
		state.controls = first;
		state.playing = first && !parser.isSet(paused);
		first = false;
		state.column = column++;
		snurra_tile *tile = node.add_tile(state);
		if (state.column == 0) {
			if (parser.isSet(subtitles)) tile->set_subtitle_files(parser.value(subtitles).split(QLatin1Char(','), Qt::SkipEmptyParts));
			if (parser.isSet(full_screen)) tile->set_full_screen(true);
		}
	}
	if (parser.isSet(loop)) {
		bool number = false;
		const int times = parser.value(loop).toInt(&number);
		if (!number || times < 0) {
			std::fprintf(stderr, "snurra: -loop takes a count, 0 for ever, not \"%s\"\n", qPrintable(parser.value(loop)));
			return 2;
		}
		node.set_loop(times);
	}
	/* At the pointer -- where the file was clicked, on X11 -- and clamped
	 * to the screen it is on (project.md sec 3b), so a file by the edge
	 * does not open half off it. */
	auto show_at_pointer = [](QWidget *window) {
		window->adjustSize();
		const QPoint pointer = QCursor::pos();
		QRect place(pointer - QPoint(window->width() / 2, window->height() / 2), window->size());
		if (const QScreen *screen = QGuiApplication::screenAt(pointer)) {
			const QRect room = screen->availableGeometry();
			place.moveLeft(qBound(room.left(), place.left(), qMax(room.left(), room.right() + 1 - place.width())));
			place.moveTop(qBound(room.top(), place.top(), qMax(room.top(), room.bottom() + 1 - place.height())));
		}
		window->move(place.topLeft());
		window->show();
	};
	/*
	 * Started with nothing to play: the library (project.md sec 3b), not a
	 * tile of three minutes' silence. The window of tiles waits, hidden,
	 * for a tune double-clicked there; the process goes once both are
	 * closed.
	 */
	if (files.isEmpty()) {
		snurra_library *library = new snurra_library;
		library->setAttribute(Qt::WA_DeleteOnClose);
		node.set_stays_open(true);
		QObject::connect(library, &snurra_library::play_requested, &node, [&node, show_at_pointer](const QString &path) {
			node.play_file(path);
			if (!node.isVisible()) show_at_pointer(&node);
		});
		QObject::connect(library, &QObject::destroyed, &node, [&node] { node.set_stays_open(false); });
		library->resize(640, 420);
		library->move(QCursor::pos() - QPoint(320, 210));
		library->show();
		return app.exec();
	}
	show_at_pointer(&node);
	return app.exec();
}
