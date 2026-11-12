/*
 * Handovers between real snurra processes (project.md sec 3b), driven over
 * their sockets the way a peer would drive them. No audio yet, so what is
 * checked is the protocol's promise: the taker is playing before the giver
 * stops, the position carries on without a jump, and a handover that
 * fails costs the giver nothing.
 */
#include <QDir>
#include <QElapsedTimer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

#include "../src/snap.h"
#include "../src/wire.h"

class handover_test : public QObject {
	Q_OBJECT

private slots:
	void init();
	void cleanup();
	void the_marker_and_the_drop_agree_on_where_a_tile_lands();
	void only_the_first_opens_with_controls_and_plays();
	void a_merge_is_taken_before_it_is_let_go();
	void a_tile_pulled_out_plays_on_in_a_new_process();
	void a_taker_that_never_answers_leaves_the_giver_playing();

private:
	QProcess *start(const QStringList &args = {});
	QString socket_of(QProcess *process) const;
	QStringList sockets() const;
	bool wait_for_socket(const QString &path, int ms = 10000);
	snurra_wire::message ask(const QString &path, const snurra_wire::message &m, int ms = 3000);
	void tell(const QString &path, const snurra_wire::message &m);

	QTemporaryDir *m_dir = nullptr;
	QList<QProcess *> m_processes;
};

void handover_test::init() {
	m_dir = new QTemporaryDir;
	QVERIFY(m_dir->isValid());
}

void handover_test::cleanup() {
	for (QProcess *process : m_processes) {
		if (process->state() != QProcess::NotRunning) {
			process->kill();
			process->waitForFinished(3000);
		}
		delete process;
	}
	m_processes.clear();
	/*
	 * A process that adopted a tile is no child of this test, so it is
	 * ended by the pid its socket is named for -- only sockets in this
	 * test's own directory, so nothing else of the user's is touched.
	 */
	for (const QString &path : sockets()) {
		const qint64 pid = QFileInfo(path).baseName().toLongLong();
		if (pid > 0) QProcess::execute(QStringLiteral("kill"), { QString::number(pid) });
	}
	delete m_dir;
	m_dir = nullptr;
}

QProcess *handover_test::start(const QStringList &args) {
	QProcess *process = new QProcess;
	QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
	env.insert(QStringLiteral("SNURRA_RUNTIME_DIR"), m_dir->path());
	env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
	process->setProcessEnvironment(env);
	process->setProcessChannelMode(QProcess::ForwardedErrorChannel);
	process->start(QStringLiteral(SNURRA_BIN), args);
	m_processes << process;
	return process;
}

QString handover_test::socket_of(QProcess *process) const {
	return m_dir->filePath(QStringLiteral("%1.sock").arg(process->processId()));
}

QStringList handover_test::sockets() const {
	QStringList found;
	const QDir dir(m_dir->path());
	for (const QString &name : dir.entryList({ QStringLiteral("*.sock") }, QDir::System | QDir::Files))
		found << dir.filePath(name);
	return found;
}

bool handover_test::wait_for_socket(const QString &path, int ms) {
	QElapsedTimer waited;
	waited.start();
	while (waited.elapsed() < ms) {
		QLocalSocket probe;
		probe.connectToServer(path);
		if (probe.waitForConnected(200)) return true;
		QTest::qWait(50);
	}
	return false;
}

snurra_wire::message handover_test::ask(const QString &path, const snurra_wire::message &m, int ms) {
	QLocalSocket socket;
	socket.connectToServer(path);
	if (!socket.waitForConnected(ms)) return {};
	socket.write(snurra_wire::encode(m));
	socket.flush();
	QByteArray buffer;
	QElapsedTimer waited;
	waited.start();
	while (waited.elapsed() < ms) {
		if (socket.waitForReadyRead(100)) buffer += socket.readAll();
		const QList<snurra_wire::message> got = snurra_wire::decode(buffer);
		if (!got.isEmpty()) return got.first();
	}
	return {};
}

void handover_test::tell(const QString &path, const snurra_wire::message &m) {
	QLocalSocket socket;
	socket.connectToServer(path);
	QVERIFY(socket.waitForConnected(3000));
	socket.write(snurra_wire::encode(m));
	QVERIFY(socket.waitForBytesWritten(3000));
	socket.disconnectFromServer();
}

static snurra_wire::message status_request() {
	snurra_wire::message m;
	m.type = snurra_wire::STATUS;
	return m;
}

void handover_test::the_marker_and_the_drop_agree_on_where_a_tile_lands() {
	/*
	 * project.md sec 3d: one answer for the marker and the release. A
	 * window let go by another's right edge lands beside its first row,
	 * and the shape is the two together; by its bottom edge, under its
	 * first column; too far, or beside it but not level with it, nowhere;
	 * and of two edges within reach, the nearer.
	 */
	const QHash<QString, QRect> peers{ { QStringLiteral("a"), QRect(100, 100, 200, 120) } };
	const QSize tile(180, 110);

	const snurra_snap beside = snurra_find_snap(QRect(QPoint(305, 130), tile), peers);
	QCOMPARE(beside.peer, QStringLiteral("a"));
	QCOMPARE(beside.side, snurra_wire::RIGHT);
	QCOMPARE(beside.landing, QRect(QPoint(300, 100), tile));
	QCOMPARE(beside.shape, QRect(100, 100, 380, 120));

	const snurra_snap under = snurra_find_snap(QRect(QPoint(140, 228), tile), peers);
	QCOMPARE(under.side, snurra_wire::BELOW);
	QCOMPARE(under.landing, QRect(QPoint(100, 220), tile));
	QCOMPARE(under.shape, QRect(100, 100, 200, 230));

	QVERIFY(snurra_find_snap(QRect(QPoint(340, 130), tile), peers).peer.isEmpty());
	QVERIFY(snurra_find_snap(QRect(QPoint(305, 400), tile), peers).peer.isEmpty());

	/* Within reach of both a's right edge (4 px) and b's bottom (10 px). */
	const QHash<QString, QRect> two{ { QStringLiteral("a"), QRect(100, 100, 200, 120) },
	                                 { QStringLiteral("b"), QRect(304, 0, 200, 100) } };
	const snurra_snap nearer = snurra_find_snap(QRect(QPoint(304, 110), tile), two);
	QCOMPARE(nearer.peer, QStringLiteral("a"));
	QCOMPARE(nearer.side, snurra_wire::RIGHT);
}

void handover_test::only_the_first_opens_with_controls_and_plays() {
	/*
	 * project.md sec 3c: the first snurra opens with its controls out and
	 * plays; one opened beside it is a waveform, paused, so a screen of
	 * tiles is not a screen of control panels. Pulled out, its controls
	 * show; pushed back in, a tile stops. A handover carries which.
	 */
	QProcess *a = start({ QStringLiteral("a.flac") });
	QVERIFY(a->waitForStarted() && wait_for_socket(socket_of(a)));
	QProcess *b = start({ QStringLiteral("b.flac") });
	QVERIFY(b->waitForStarted() && wait_for_socket(socket_of(b)));

	const snurra_tile_state first = ask(socket_of(a), status_request()).states.value(0);
	QVERIFY2(first.controls && first.playing, "the first did not open with controls, playing");
	const snurra_tile_state second = ask(socket_of(b), status_request()).states.value(0);
	QVERIFY2(!second.controls && !second.playing, "a second opened with controls or playing");

	snurra_wire::message pull;
	pull.type = snurra_wire::SHOW_CONTROLS;
	pull.index = 0;
	pull.time = 1;
	tell(socket_of(b), pull);
	QTRY_VERIFY_WITH_TIMEOUT(ask(socket_of(b), status_request()).states.value(0).controls, 3000);

	snurra_wire::message push = pull;
	push.time = 0;
	tell(socket_of(a), push);
	QTRY_VERIFY_WITH_TIMEOUT(!ask(socket_of(a), status_request()).states.value(0).controls, 3000);
	QVERIFY2(!ask(socket_of(a), status_request()).states.value(0).playing, "controls pushed in, still playing");

	/* Moved, each keeps its controls as they were. */
	snurra_wire::message merge;
	merge.type = snurra_wire::MERGE_INTO;
	merge.path = socket_of(a);
	tell(socket_of(b), merge);
	QVERIFY(b->waitForFinished(5000));
	const QList<snurra_tile_state> both = ask(socket_of(a), status_request()).states;
	QCOMPARE(both.size(), 2);
	for (const snurra_tile_state &state : both)
		QCOMPARE(state.controls, state.source.endsWith(QLatin1String("b.flac")));
}

void handover_test::a_merge_is_taken_before_it_is_let_go() {
	/* The first plays; the second, a waveform, is where it goes. */
	QProcess *a = start({ QStringLiteral("a.flac") });
	QVERIFY(a->waitForStarted() && wait_for_socket(socket_of(a)));
	QProcess *b = start({ QStringLiteral("b.flac") });
	QVERIFY(b->waitForStarted() && wait_for_socket(socket_of(b)));
	QTest::qWait(400);

	const snurra_wire::message before = ask(socket_of(a), status_request());
	QCOMPARE(before.states.size(), 1);
	const snurra_tile_state given = before.states.first();
	QVERIFY(given.playing);

	snurra_wire::message merge;
	merge.type = snurra_wire::MERGE_INTO;
	merge.path = socket_of(b);
	merge.side = snurra_wire::RIGHT;
	tell(socket_of(a), merge);
	QVERIFY2(a->waitForFinished(5000), "the giver did not go once its tile was taken");

	const snurra_wire::message after = ask(socket_of(b), status_request());
	QCOMPARE(after.states.size(), 2);
	QVERIFY2(after.started > 0 && after.stopped > 0, "no handover times recorded");
	QVERIFY2(after.started <= after.stopped,
	         qPrintable(QStringLiteral("the taker started %1 ms after the giver stopped")
	                            .arg(after.started - after.stopped)));

	/* The moved tile is the train's second, and went on from where it was:
	 * no replay, no jump, within a scheduling slice either way. */
	snurra_tile_state moved;
	for (const snurra_tile_state &state : after.states)
		if (state.source.endsWith(QLatin1String("a.flac"))) moved = state;
	QVERIFY(moved.playing);
	QCOMPARE(moved.column, 1);
	const qint64 expected = given.position_now(moved.sampled_at);
	QVERIFY2(qAbs(moved.position_ms - expected) <= 150,
	         qPrintable(QStringLiteral("position %1 ms, expected %2").arg(moved.position_ms).arg(expected)));
}

void handover_test::a_tile_pulled_out_plays_on_in_a_new_process() {
	QProcess *group = start({ QStringLiteral("one.flac"), QStringLiteral("two.flac") });
	QVERIFY(group->waitForStarted());
	const QString group_socket = socket_of(group);
	QVERIFY(wait_for_socket(group_socket));
	QTest::qWait(300);
	QCOMPARE(ask(group_socket, status_request()).states.size(), 2);

	snurra_wire::message release;
	release.type = snurra_wire::RELEASE;
	release.index = 0;   /* the first, the one playing */
	release.frame = QRect(40, 40, 1, 1);
	tell(group_socket, release);

	/* A new socket appears: the process that took the tile. */
	QString adopted;
	QElapsedTimer waited;
	waited.start();
	while (adopted.isEmpty() && waited.elapsed() < 10000) {
		for (const QString &path : sockets()) {
			if (path == group_socket) continue;
			const snurra_wire::message status = ask(path, status_request(), 500);
			if (status.states.size() == 1) adopted = path;
		}
		QTest::qWait(100);
	}
	QVERIFY2(!adopted.isEmpty(), "no process took the released tile");
	QTRY_COMPARE_WITH_TIMEOUT(ask(group_socket, status_request()).states.size(), 1, 5000);

	const snurra_wire::message taken = ask(adopted, status_request());
	QCOMPARE(taken.states.first().source, QStringLiteral("one.flac"));
	QVERIFY(taken.states.first().playing);
	QVERIFY2(taken.started <= taken.stopped && taken.stopped > 0,
	         qPrintable(QStringLiteral("started %1, stopped %2").arg(taken.started).arg(taken.stopped)));
}

void handover_test::a_taker_that_never_answers_leaves_the_giver_playing() {
	/* A taker that takes the offer and says nothing. */
	QLocalServer silent;
	const QString silent_path = m_dir->filePath(QStringLiteral("silent.listen"));
	QVERIFY(silent.listen(silent_path));
	QByteArray heard;
	connect(&silent, &QLocalServer::newConnection, this, [&] {
		QLocalSocket *socket = silent.nextPendingConnection();
		connect(socket, &QLocalSocket::readyRead, this, [&heard, socket] { heard += socket->readAll(); });
	});

	QProcess *a = start({ QStringLiteral("a.flac") });
	QVERIFY(a->waitForStarted());
	QVERIFY(wait_for_socket(socket_of(a)));
	snurra_wire::message merge;
	merge.type = snurra_wire::MERGE_INTO;
	merge.path = silent_path;
	tell(socket_of(a), merge);

	QTRY_VERIFY_WITH_TIMEOUT(!heard.isEmpty(), 3000);
	QTest::qWait(2600);
	QByteArray copy = heard;
	QList<snurra_wire::type_t> types;
	for (const snurra_wire::message &m : snurra_wire::decode(copy)) types << m.type;
	QVERIFY2(types.contains(snurra_wire::OFFER), "no offer was made");
	QVERIFY2(types.contains(snurra_wire::ABORT), "the giver did not abort a handover nobody answered");
	QCOMPARE(a->state(), QProcess::Running);
	const snurra_wire::message status = ask(socket_of(a), status_request());
	QCOMPARE(status.states.size(), 1);
	QVERIFY(status.states.first().playing);
}

QTEST_GUILESS_MAIN(handover_test)
#include "handover_test.moc"
