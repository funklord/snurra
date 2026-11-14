/* Every suite in one binary, so `make test` builds and runs one thing. */
#include <QApplication>
#include <QDir>
#include <QTemporaryDir>

#include <algorithm>
#include <cstring>
#include <vector>

#include <sys/resource.h>

int run_handover_tests(int argc, char **argv);
int run_codec_tests(int argc, char **argv);
int run_deck_tests(int argc, char **argv);
int run_embed_tests(int argc, char **argv);
int run_video_tests(int argc, char **argv);
int run_beats_tests(int argc, char **argv);
int run_subtitles_tests(int argc, char **argv);
int run_estate_tests(int argc, char **argv);

int main(int argc, char **argv) {
	/*
	 * No debugger on a crash, set here rather than in the Makefile so it
	 * holds for whoever runs the binary directly -- which is who chases a
	 * crash. QtTest's handler attaches gdb; in this suite it re-faulted
	 * and re-attached 361 times in 281 s before an outer timeout ended it.
	 */
	qputenv("QTEST_DISABLE_STACK_DUMP", "1");
	/*
	 * No file this run or its children write -- the log its output is
	 * sent to included -- grows past 256 MB: SIGXFSZ ends it there. A
	 * sabotaged run hung in a destructor while printing, ignored the
	 * SIGTERM of the timeout around it, and wrote 4.3 GB into its log in
	 * three minutes, filling / for every session on the machine
	 * (2026-11-13). Here, so it holds for a run started by hand too.
	 */
	const struct rlimit cap { 256ul << 20, 256ul << 20 };
	setrlimit(RLIMIT_FSIZE, &cap);
	const struct rlimit no_core { 0, 0 };
	setrlimit(RLIMIT_CORE, &no_core);
	/* Widgets for the embedding tests, on no screen at all. */
	if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
	/* Beat grids are kept in the user's cache (beats.h); a run keeps its
	 * own, removed when it ends, and never touches the person's. Here, so
	 * the processes the tests start inherit it. */
	QTemporaryDir cache;
	qputenv("SNURRA_CACHE", cache.path().toLocal8Bit());
	/* And its own settings, for the same reason: a test that sets or clears
	 * one -- the AcoustID key's absence, a library folder remembered --
	 * would otherwise write the person's ~/.config/snurra/snurra.conf.
	 * Through XDG_CONFIG_HOME, which the processes the tests start take too. */
	const QString settings = cache.path() + QStringLiteral("/config");
	QDir().mkpath(settings);
	qputenv("XDG_CONFIG_HOME", settings.toLocal8Bit());
	QApplication app(argc, argv);
	/*
	 * And no crash handler at all: a crash ends the run there. A double
	 * free in a reader thread (2026-11-14) set QtTest's handler printing
	 * "Received signal 6" 2.4 million times, until the 256 MB cap above
	 * ended it -- the cap held, and the handler was what wrote. Given to
	 * every class, as QtTest takes it, unless it was given already.
	 */
	std::vector<char *> given(argv, argv + argc);
	static char no_handler[] = "-nocrashhandler";
	if (std::none_of(given.begin(), given.end(), [](const char *a) { return std::strcmp(a, no_handler) == 0; }))
		given.insert(given.begin() + 1, no_handler);
	argc = int(given.size());
	given.push_back(nullptr);
	argv = given.data();
	/* One class alone, for a script that runs one test of it
	 * (test/estate_two_hosts.py): the others would fail its name. */
	if (qEnvironmentVariable("SNURRA_TEST_CLASS") == QLatin1String("estate")) return run_estate_tests(argc, argv);
	const int handover = run_handover_tests(argc, argv);
	const int codec = run_codec_tests(argc, argv);
	const int deck = run_deck_tests(argc, argv);
	const int embed = run_embed_tests(argc, argv);
	const int video = run_video_tests(argc, argv);
	const int beats = run_beats_tests(argc, argv);
	const int subtitles = run_subtitles_tests(argc, argv);
	const int estate = run_estate_tests(argc, argv);
	return handover || codec || deck || embed || video || beats || subtitles || estate ? 1 : 0;
}
