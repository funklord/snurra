/*
 * The estate's music (app/estate.h, project.md sec 3b) against a real
 * fuzznetd -- built from the vendored fuzznet by `make test` and named by
 * $SNURRA_FUZZNETD -- in a store of its own: a file stored and catalogued
 * through fuzznet's typed calls, listed by the player and played from the
 * node's bytes, read on and sought. No stand-in for the node: the holder
 * (2026-11-14), these projects become stable by being used rigorously.
 */
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>
#include <csignal>
#include <sys/prctl.h>

#include "estate.h"
#include "snurra/source.h"

namespace {

constexpr int RATE = 48000;

/* A mono 16-bit WAV of `frames` frames, each `sample(i)`. */
QByteArray wav_of(int frames, const std::function<int(int)> &sample) {
	QByteArray data;
	for (int i = 0; i < frames; ++i) {
		const qint16 v = qint16(sample(i));
		data.append(reinterpret_cast<const char *>(&v), 2);
	}
	auto le32 = [](quint32 v) { return QByteArray(reinterpret_cast<const char *>(&v), 4); };
	auto le16 = [](quint16 v) { return QByteArray(reinterpret_cast<const char *>(&v), 2); };
	return "RIFF" + le32(quint32(36 + data.size())) + "WAVEfmt " + le32(16) + le16(1) + le16(1) + le32(RATE)
	       + le32(RATE * 2) + le16(2) + le16(16) + "data" + le32(quint32(data.size())) + data;
}

/* Crossings up through zero a second, in the left channel. */
double frequency(const std::vector<float> &left) {
	int crossings = 0;
	for (size_t i = 1; i < left.size(); ++i)
		if (left[i - 1] < 0 && left[i] >= 0) ++crossings;
	return left.empty() ? 0 : crossings * double(RATE) / double(left.size());
}

}   // namespace

class estate_test : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void cleanupTestCase();
	void a_catalogued_file_is_listed_and_played_from_the_node();
	void no_node_says_so_and_lists_nothing();

private:
	QTemporaryDir m_dir{ QStringLiteral("/tmp/snurra-estate.XXXXXX") };   /* short: a socket path is */
	QProcess m_node;
	QString m_socket;
};

void estate_test::initTestCase() {
	const QString daemon = qEnvironmentVariable("SNURRA_FUZZNETD");
	if (daemon.isEmpty() || !QFileInfo(daemon).isExecutable())
		QSKIP("no fuzznetd: `make test` builds one from the vendored fuzznet and names it in $SNURRA_FUZZNETD");
	QVERIFY(m_dir.isValid());
	const QString store = m_dir.filePath(QStringLiteral("store"));
	QVERIFY(QDir().mkpath(store));
	const QStringList capability{ QStringLiteral("--fuzznet-service=1"), QStringLiteral("--fuzznet-product=1") };
	QProcess root;
	root.start(daemon, QStringList{ QStringLiteral("--fuzznet-dir=") + store, QStringLiteral("--new-root") } + capability);
	QVERIFY(root.waitForFinished(20000));
	QVERIFY2(root.exitCode() == 0, root.readAllStandardError().constData());
	m_socket = m_dir.filePath(QStringLiteral("s"));
	const QString port = QString::number(30000 + QCoreApplication::applicationPid() % 20000);
	/* Gone with this process, however it ends: a daemon a crashed test
	 * left would hold its port and store for ever. */
	m_node.setChildProcessModifier([] { ::prctl(PR_SET_PDEATHSIG, SIGKILL); });
	m_node.setProcessChannelMode(QProcess::ForwardedErrorChannel);
	m_node.setStandardOutputFile(QProcess::nullDevice());
	m_node.start(daemon, QStringList{ QStringLiteral("--socket=") + m_socket, QStringLiteral("--fuzznet-dir=") + store,
	                                  QStringLiteral("--log-dir=") + m_dir.filePath(QStringLiteral("log")),
	                                  QStringLiteral("--udp-port=") + port }
	                         + capability);
	QVERIFY(m_node.waitForStarted(5000));
	qputenv("SNURRA_FUZZNET_SOCKET", m_socket.toLocal8Bit());
	/* Up once it answers: an empty catalogue, not "not running". */
	QString error = QStringLiteral("not asked");
	QElapsedTimer waited;
	waited.start();
	while (waited.elapsed() < 15000) {
		error.clear();
		snurra_estate_list(&error);
		if (error.isEmpty()) break;
		QTest::qWait(100);
	}
	QVERIFY2(error.isEmpty(), qPrintable(error));
}

void estate_test::cleanupTestCase() {
	qunsetenv("SNURRA_FUZZNET_SOCKET");
	if (m_node.state() != QProcess::NotRunning) {
		m_node.terminate();
		if (!m_node.waitForFinished(5000)) {
			m_node.kill();
			m_node.waitForFinished(5000);
		}
	}
}

/*
 * A 440 Hz tone of 3 s stored on the node and catalogued "Tone A": listed
 * with its name, this host holding it; opened as "fuzznet:ROOT" it plays
 * 440 Hz for 3 s through the node's slices; and a second file, whose
 * samples count the frame, sought back and forth gives the very frames
 * the file gives opened by path.
 */
void estate_test::a_catalogued_file_is_listed_and_played_from_the_node() {
	const QString tone = m_dir.filePath(QStringLiteral("tone.wav"));
	{
		QFile file(tone);
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write(wav_of(3 * RATE, [](int i) { return int(12000 * std::sin(2 * M_PI * 440 * i / double(RATE))); }));
	}
	fzn_rpc_t call;
	fzn_rpc_init(&call);
	QCOMPARE(fzn_rpc_set_node_socket(&call, m_socket.toLocal8Bit().constData()), 0);
	fzn_rpc_blob_add_resp_t added{};
	const QByteArray path = tone.toLocal8Bit();
	QVERIFY2(fzn_rpc_blob_add(&call, path.constData(), size_t(path.size()), FZN_RPC_BLOB_TIER_PRIVATE, &added) == FZN_RPC_OK,
	         call.error);
	fzn_rpc_library_add_req_t entry{};
	memcpy(entry.root, added.root, sizeof entry.root);
	memcpy(entry.content_key, added.key, sizeof entry.content_key);
	entry.size = added.byte_len;
	entry.duration_ms = 3000;
	const QByteArray name("Tone A"), kind("audio");
	memcpy(entry.name, name.constData(), size_t(name.size()));
	entry.name_len = size_t(name.size());
	memcpy(entry.kind, kind.constData(), size_t(kind.size()));
	entry.kind_len = size_t(kind.size());
	QVERIFY2(fzn_rpc_library_add(&call, &entry) == FZN_RPC_OK, call.error);

	QString error;
	const std::vector<snurra_estate_file> files = snurra_estate_list(&error);
	QVERIFY2(error.isEmpty(), qPrintable(error));
	const auto found = std::find_if(files.begin(), files.end(), [](const snurra_estate_file &f) { return f.name == QLatin1String("Tone A"); });
	QVERIFY2(found != files.end(), qPrintable(QStringLiteral("%1 files listed, none Tone A").arg(files.size())));
	QCOMPARE(found->kind, QStringLiteral("audio"));
	QCOMPARE(found->root, QByteArray(reinterpret_cast<const char *>(added.root), 32));
	QVERIFY(found->here);
	QVERIFY(found->holders >= 1);

	const QString named = snurra_estate_name(added.root);
	uint8_t back[32];
	QVERIFY(snurra_estate_root(named, back) && memcmp(back, added.root, 32) == 0);
	QVERIFY(!snurra_estate_root(QStringLiteral("fuzznet:xyz"), back));

	snurra_estate_install();
	std::unique_ptr<snurra_source> source = snurra_open_source(named, RATE, RATE);
	QVERIFY(source);
	QVERIFY2(source->length() > 0, "a silent stand-in, not the node's file");
	qInfo("Tone A from the node: %lld frames", (long long)source->length());
	QVERIFY(std::abs(source->length() - 3 * RATE) < RATE / 50);
	std::vector<float> left;
	std::vector<float> block(2 * 4096);
	for (int n; (n = source->read(block.data(), 4096)) > 0;)
		for (int i = 0; i < n; ++i) left.push_back(block[size_t(2 * i)]);
	const double hz = frequency(left);
	qInfo("read %zu frames, %.1f Hz", left.size(), hz);
	QVERIFY(std::abs(double(left.size()) - 3 * RATE) < RATE / 50);
	QVERIFY2(std::abs(hz - 440) < 2, qPrintable(QString::number(hz)));

	/* Sought: a second file whose samples count the frame. */
	const QString ramp = m_dir.filePath(QStringLiteral("ramp.wav"));
	{
		QFile file(ramp);
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write(wav_of(3 * RATE, [](int i) { return (i / 2) % 30000; }));
	}
	const QByteArray ramp_path = ramp.toLocal8Bit();
	fzn_rpc_blob_add_resp_t ramp_added{};
	QVERIFY2(fzn_rpc_blob_add(&call, ramp_path.constData(), size_t(ramp_path.size()), FZN_RPC_BLOB_TIER_PRIVATE, &ramp_added) == FZN_RPC_OK,
	         call.error);
	std::unique_ptr<snurra_source> counted = snurra_open_source(snurra_estate_name(ramp_added.root), RATE, RATE);
	QVERIFY(counted && counted->length() > 0);
	/* Against the file itself, opened by path: the node's bytes must
	 * play exactly as it does (a mono file comes out 3 dB down a side,
	 * 18000 as 12728, either way). At several places, back and forth. */
	std::unique_ptr<snurra_source> local = snurra_open_source(ramp, RATE, RATE);
	QVERIFY(local && local->length() == counted->length());
	for (int64_t at : { int64_t(2 * RATE), int64_t(RATE / 3), int64_t(5 * RATE / 2), int64_t(0), int64_t(3 * RATE - 100) }) {
		counted->seek(at);
		local->seek(at);
		std::vector<float> theirs(2 * 64), ours(2 * 64);
		QCOMPARE(counted->read(theirs.data(), 64), local->read(ours.data(), 64));
		QVERIFY2(theirs == ours, qPrintable(QStringLiteral("frames from %1 differ").arg(at)));
		const int expected = int(std::lround((at / 2) % 30000 * M_SQRT1_2));
		const int heard = int(std::lround(double(theirs[0]) * 32768));
		QVERIFY2(std::abs(heard - expected) <= 1, qPrintable(QStringLiteral("at %1: %2, not %3").arg(at).arg(heard).arg(expected)));
	}
	qInfo("sought to five places, back and forth: the node's frames are the file's");
}

/* No node at the socket: nothing listed, and the reason in words. */
void estate_test::no_node_says_so_and_lists_nothing() {
	const QByteArray kept = qgetenv("SNURRA_FUZZNET_SOCKET");
	qputenv("SNURRA_FUZZNET_SOCKET", m_dir.filePath(QStringLiteral("nobody")).toLocal8Bit());
	QString error;
	const std::vector<snurra_estate_file> files = snurra_estate_list(&error);
	qputenv("SNURRA_FUZZNET_SOCKET", kept);
	QVERIFY(files.empty());
	qInfo("no node: %s", qPrintable(error));
	QVERIFY(error.contains(QLatin1String("not running")));
}

int run_estate_tests(int argc, char **argv) {
	estate_test test;
	return QTest::qExec(&test, argc, argv);
}

#include "estate_test.moc"
