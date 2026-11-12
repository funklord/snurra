/* Both suites in one binary, so `make test` builds and runs one thing. */
#include <QCoreApplication>

int run_handover_tests(int argc, char **argv);
int run_codec_tests(int argc, char **argv);

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	const int handover = run_handover_tests(argc, argv);
	const int codec = run_codec_tests(argc, argv);
	return handover || codec ? 1 : 0;
}
