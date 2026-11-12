/* Every suite in one binary, so `make test` builds and runs one thing. */
#include <QApplication>

int run_handover_tests(int argc, char **argv);
int run_codec_tests(int argc, char **argv);
int run_deck_tests(int argc, char **argv);
int run_embed_tests(int argc, char **argv);
int run_video_tests(int argc, char **argv);

int main(int argc, char **argv) {
	/*
	 * No debugger on a crash, set here rather than in the Makefile so it
	 * holds for whoever runs the binary directly -- which is who chases a
	 * crash. QtTest's handler attaches gdb; in this suite it re-faulted
	 * and re-attached 361 times in 281 s before an outer timeout ended it.
	 */
	qputenv("QTEST_DISABLE_STACK_DUMP", "1");
	/* Widgets for the embedding tests, on no screen at all. */
	if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
	QApplication app(argc, argv);
	const int handover = run_handover_tests(argc, argv);
	const int codec = run_codec_tests(argc, argv);
	const int deck = run_deck_tests(argc, argv);
	const int embed = run_embed_tests(argc, argv);
	const int video = run_video_tests(argc, argv);
	return handover || codec || deck || embed || video ? 1 : 0;
}
