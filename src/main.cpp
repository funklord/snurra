#include <QApplication>

#include <algorithm>
#include <cstdio>
#include <random>

#ifndef SNURRA_VERSION_TEXT
#define SNURRA_VERSION_TEXT "unknown"   /* built without VERSION's word */
#endif
#include <QCommandLineParser>
#include <QCursor>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QScreen>
#include <QSocketNotifier>

#include <csignal>
#include <fcntl.h>
#include <unistd.h>

#ifdef Q_OS_ANDROID
#include <QtCore/private/qandroidextras_p.h>
#endif

#include "snurra/clock.h"
#include "snurra/embed.h"
#include "snurra/library.h"
#include "snurra/midi.h"
#include "snurra/node.h"
#include "snurra/source.h"
#include "snurra/theme.h"
#include "snurra/tile.h"
#include "snurra/wire.h"

#include "estate.h"   /* the player's, app/: the estate's music through fuzznet's node */

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
	/* exit(), not quit(), which a window refusing to close drops (node.cpp,
	 * leave_if_empty): asked to end, it ends. */
	QObject::connect(notifier, &QSocketNotifier::activated, &app, [] { QCoreApplication::exit(0); });
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
	/* Its own icon, the record make install puts in the icon theme, on
	 * every window; the generic player's where it is not installed. And
	 * the desktop entry named, which is where Wayland finds the icon. */
	QGuiApplication::setDesktopFileName(QStringLiteral("snurra"));
	QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("snurra"), QIcon::fromTheme(QStringLiteral("multimedia-audio-player"))));
	quit_on_signals(app);
	/* "fuzznet:ROOT" opened from the user's node, in this process and in
	 * every tile process it starts, each running this same main. */
	snurra_estate_install();

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
	const QCommandLineOption dj(QStringLiteral("dj"), QStringLiteral("Open the DJ player: two decks, a crossfader and the music under them."));
	const QCommandLineOption listen(QStringLiteral("listen"),
	                                QStringLiteral("Headphones for the DJ player, on this output (as --output): each deck's \"listen\" plugs them in there."),
	                                QStringLiteral("spec"));
	const QCommandLineOption player_name(QStringLiteral("name"),
	                                     QStringLiteral("This player's name for controllers, as an emergency mix's: its play and stop on buttons of their own."),
	                                     QStringLiteral("player"));
	const QCommandLineOption decks(QStringLiteral("decks"), QStringLiteral("The DJ player's decks: 2 or 4."), QStringLiteral("count"));
	const QCommandLineOption loop(QStringLiteral("loop"), QStringLiteral("Play it all this many times; 0 for ever."), QStringLiteral("times"));
	const QCommandLineOption audio_file(QStringList{ QStringLiteral("audiofile"), QStringLiteral("audio-file") },
	                                    QStringLiteral("The sound, from a file or URL of its own beside the picture's."),
	                                    QStringLiteral("file"));
	const QCommandLineOption chapters_file(QStringLiteral("chapters-file"),
	                                       QStringLiteral("A tracklist for the first file, in FFmpeg's metadata format, as chapters."),
	                                       QStringLiteral("file"));
	const QCommandLineOption shuffle(QStringLiteral("shuffle"), QStringLiteral("Play the files in a random order."));
	const QCommandLineOption import_mapping(QStringLiteral("import-mapping"),
	                                        QStringLiteral("Bind a controller as a Mixxx mapping (.midi.xml) does, its plain bindings only."),
	                                        QStringLiteral("file"));
	parser.addOptions({ adopt, token, index, at, paused, output, layout, version, start_at, full_screen, subtitles, volume, speed,
	                    playlist, loop, shuffle, audio_file, chapters_file, dj, decks, listen, player_name, import_mapping });
	parser.process(app);
	/* The first line keeps its shape for whatever reads it; the copyright
	 * holder on a line of their own. */
	if (parser.isSet(version)) {
		std::printf("snurra %s\nCopyright (C) 2026 Nabeel Sowan <nabeel@vibes.se>\n", SNURRA_VERSION_TEXT);
		return 0;
	}
	/* A Mixxx mapping's plain bindings into the settings, where a running
	 * deck view reads them within a second; its scripts are not run (the
	 * holder, 2026-11-21). What was not taken is said, a line each. */
	if (parser.isSet(import_mapping)) {
		const QString path = parser.value(import_mapping);
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly)) {
			std::fprintf(stderr, "snurra: cannot read %s\n", qPrintable(path));
			return 1;
		}
		snurra_bindings bindings;
		bindings.load();
		QStringList skipped;
		const int bound = bindings.import_mixxx(file.readAll(), &skipped);
		if (bound < 0) {
			std::fprintf(stderr, "snurra: %s is not a Mixxx controller mapping\n", qPrintable(path));
			return 1;
		}
		bindings.save();
		std::printf("snurra: %d controls bound from %s, from whichever port sends them\n", bound, qPrintable(path));
		for (const QString &line : skipped) std::printf("  not taken: %s\n", qPrintable(line));
		return 0;
	}
	if (parser.isSet(layout)) qputenv("SNURRA_LAYOUT", parser.value(layout).toLocal8Bit());
	/* As $SNURRA_OUTPUT, which the engine reads -- and which an app on
	 * Android cannot be given, its arguments being all it has. A process
	 * this one starts to adopt a tile inherits it. */
	if (parser.isSet(output)) qputenv("SNURRA_OUTPUT", parser.value(output).toLocal8Bit());

	/* The DJ player (embed.h 3), as a window of its own; with files, the
	 * first two on its decks. On a phone it is the app (the holder,
	 * 2026-11-13: one static screen, the browser under the decks). */
	bool as_dj = parser.isSet(dj) || parser.isSet(decks) || parser.isSet(listen);
#ifdef Q_OS_ANDROID
	as_dj = as_dj || parser.positionalArguments().isEmpty();
#endif
	if (as_dj) {
		snurra_dj *player = new snurra_dj;
		player->setAttribute(Qt::WA_DeleteOnClose);
		/* The estate's music in its browser, where a node answers. */
		if (snurra_library *browser = player->findChild<snurra_library *>()) snurra_estate_offer(browser);
		player->setWindowTitle(QStringLiteral("snurra -- DJ"));
		if (parser.isSet(decks)) player->set_deck_count(parser.value(decks).toInt());
		/* On the wire (project.md sec 3f): windows of tiles hand theirs onto
		 * its decks, and its decks out into windows. */
		player->listen();
		/* MIDI controllers, bound as the person chose; Alt held on a
		 * control learns one (project.md sec 3f). */
		player->take_controllers();
		for (const QString &spec : parser.values(listen)) player->add_listener(spec);
		/* Opened from a window's "Open in the decks": its player, asked for. */
		if (parser.isSet(adopt)) player->adopt(parser.value(adopt), parser.value(token));
		const QStringList given = parser.positionalArguments();
		for (int deck = 0; deck < player->deck_count() && deck < given.size(); ++deck) {
			snurra_media media;
			/* A name with a scheme as it is -- tone:440, fuzznet:ROOT, a URL --
			 * which made absolute was a file under the current folder, and
			 * the deck said it could not play it (seen in Xvfb, 2026-11-18). */
			media.path = snurra_source_of(given.at(deck));
			player->load(deck, media);
		}
#ifdef Q_OS_ANDROID
		player->showMaximized();
		/* The music is the phone's shared storage, readable once asked:
		 * looked through when the answer comes, not before. */
		/* One at a time: the kit exports the single form only. Audio and
		 * video from Android 13, all storage before it. */
		QtAndroidPrivate::requestPermission(QStringLiteral("android.permission.READ_MEDIA_AUDIO"))
		        .then(player, [player](QtAndroidPrivate::PermissionResult) {
			        QtAndroidPrivate::requestPermission(QStringLiteral("android.permission.READ_MEDIA_VIDEO"))
			                .then(player, [player](QtAndroidPrivate::PermissionResult) {
				                QtAndroidPrivate::requestPermission(QStringLiteral("android.permission.READ_EXTERNAL_STORAGE"))
				                        .then(player, [player](QtAndroidPrivate::PermissionResult) {
					                        player->set_browser_folder(QStringLiteral("/storage/emulated/0"));
				                        });
			                });
		        });
#else
		player->resize(1000, 700);
		player->show();
#endif
		return app.exec();
	}

	snurra_node node;
	if (!node.listen()) return 1;
	/* A named player takes controllers, its bindings its own (sec 3f). */
	if (parser.isSet(player_name)) node.take_controllers(parser.value(player_name));

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
	/* mplayer's -shuffle, as `mplayer -shuffle *.mp3` is typed: the train
	 * in a random order, the playlists' files among the rest. */
	if (parser.isSet(shuffle)) std::shuffle(files.begin(), files.end(), std::mt19937(std::random_device{}()));
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
		/* mplayer's -audiofile, mpv's --audio-file: a site's film comes as
		 * a picture and a sound, and hydra hands both over. */
		if (column == 0 && parser.isSet(audio_file)) {
			const QString sound = parser.value(audio_file);
			state.sound = QFileInfo::exists(sound) ? QFileInfo(sound).absoluteFilePath() : sound;
		}
		/* mpv's --chapters-file: a streamed set's tracklist, which hydra
		 * writes for a player that reads one. Read now; the file is
		 * hydra's to overwrite with the next. */
		if (column == 0 && parser.isSet(chapters_file))
			for (const snurra_chapter &chapter : snurra_read_chapters(parser.value(chapters_file)))
				state.chapters << qMakePair(chapter.start_us, chapter.title);
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
		snurra_estate_offer(library);
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
