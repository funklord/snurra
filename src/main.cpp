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
#include "snurra/node.h"
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

}

/*
 * snurra: one tile per process (project.md sec 3b). Started with a file,
 * it opens a tile for it at the pointer -- where the file was clicked in a
 * file manager, on X11. Started with --adopt, it is taking a tile another
 * snurra is letting go, and shows nothing until that tile arrives.
 */
int main(int argc, char *argv[]) {
	QApplication app(argc, argv);
	QCoreApplication::setApplicationName(QStringLiteral("snurra"));
	quit_on_signals(app);

	QCommandLineParser parser;
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
	parser.addOptions({ adopt, token, index, at, paused, output, layout, version });
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
	const QStringList files = parser.positionalArguments();
	bool first = !snurra_wire::any_peer_alive(node.socket_path());
	int column = 0;
	for (const QString &file : files.isEmpty() ? QStringList{ QStringLiteral("untitled") } : files) {
		snurra_tile_state state;
		state.source = QFileInfo::exists(file) ? QFileInfo(file).absoluteFilePath() : file;
		/* Three minutes for what has no length of its own: silence, for
		 * a name nothing can play, still behaves as a tile. */
		state.duration_us = 180000000;
		state.sampled_at_us = snurra_clock_us();
		state.controls = first;
		state.playing = first && !parser.isSet(paused);
		first = false;
		state.column = column++;
		node.add_tile(state);
	}
	node.adjustSize();
	/* At the pointer -- where the file was clicked, on X11 -- and clamped
	 * to the screen it is on (project.md sec 3b), so a file by the edge
	 * does not open half off it. */
	const QPoint pointer = QCursor::pos();
	QRect place(pointer - QPoint(node.width() / 2, node.height() / 2), node.size());
	if (const QScreen *screen = QGuiApplication::screenAt(pointer)) {
		const QRect room = screen->availableGeometry();
		place.moveLeft(qBound(room.left(), place.left(), qMax(room.left(), room.right() + 1 - place.width())));
		place.moveTop(qBound(room.top(), place.top(), qMax(room.top(), room.bottom() + 1 - place.height())));
	}
	node.move(place.topLeft());
	node.show();
	return app.exec();
}
