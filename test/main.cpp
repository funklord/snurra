/* Every suite in one binary, so `make test` builds and runs one thing. */
#include <QApplication>
#include <QTemporaryDir>

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
	/* Widgets for the embedding tests, on no screen at all. */
	if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
	/* Beat grids are kept in the user's cache (beats.h); a run keeps its
	 * own, removed when it ends, and never touches the person's. Here, so
	 * the processes the tests start inherit it. */
	QTemporaryDir cache;
	qputenv("SNURRA_CACHE", cache.path().toLocal8Bit());
	QApplication app(argc, argv);
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
