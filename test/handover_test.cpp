/*
 * Handovers between real snurra processes (project.md sec 3b), driven over
 * their sockets the way a peer would drive them: the taker is playing
 * before the giver stops, the position carries on without a jump, a
 * handover that fails costs the giver nothing -- and, from what each
 * process actually rendered, the two cross over in step and sum to one
 * stream throughout.
 */
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QStandardPaths>

#include <cmath>
#include <QTemporaryDir>
#include <QtTest>

#include "../src/snap.h"
#include "../src/source.h"
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
	void a_merge_crosses_over_in_step_without_a_gap();
	void a_tile_pulled_out_crosses_over_in_step_without_a_gap();
	void a_merge_between_two_alsa_outputs_is_heard_in_step();

private:
	QProcess *start(const QStringList &args = {}, const QString &output = QStringLiteral("null"),
	                const QProcessEnvironment &extra = {});
	QString capture_dir() const { return m_dir->filePath(QStringLiteral("capture")); }
	void check_crossover(qint64 giver, qint64 taker, const snurra_wire::message &status);
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

QProcess *handover_test::start(const QStringList &args, const QString &output, const QProcessEnvironment &extra) {
	QProcess *process = new QProcess;
	QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
	env.insert(QStringLiteral("SNURRA_RUNTIME_DIR"), m_dir->path());
	env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
	/* No sound card is touched: null, or a capture the test reads. */
	env.insert(QStringLiteral("SNURRA_OUTPUT"), output);
	for (const QString &key : extra.keys()) env.insert(key, extra.value(key));
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
	const qint64 expected = given.position_now(moved.sampled_at_us);
	QVERIFY2(qAbs(moved.position_us - expected) <= 2000,
	         qPrintable(QStringLiteral("position %1 us, expected %2").arg(moved.position_us).arg(expected)));
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

/* ---- what was heard ----------------------------------------------------- */

namespace {

/* A capture (output.h), frame by frame: when it was heard, and the ramp's
 * two channels -- the frame index over 2^24 times the gain, and the gain. */
struct capture_t {
	QList<double> us;
	QList<float> left;
	QList<float> right;

	double gain(int i) const { return i >= 0 && i < right.size() ? right.at(i) : 0.0; }
	double frame(int i) const { return double(left.at(i)) / right.at(i) * SNURRA_RAMP_SCALE; }

	/* The frame being heard at exactly `when`, to a fraction: the ramp is
	 * a straight line in time, so the nearest frame says where it is. */
	double frame_at(int i, double when) const { return frame(i) + (when - us.at(i)) * 48000 / 1e6; }

	/* The frame heard nearest `at`, or -1 outside the capture. */
	int at(double when) const {
		if (us.isEmpty() || when < us.first() - 11 || when > us.last() + 11) return -1;
		const double step = 1e6 / 48000;
		return qBound(0, int((when - us.first()) / step + 0.5), int(us.size()) - 1);
	}
};

capture_t read_capture(const QString &path) {
	capture_t capture;
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) return capture;
	const QByteArray bytes = file.readAll();
	qsizetype at = 0;
	while (at + 12 <= bytes.size()) {
		qint64 present = 0;
		qint32 frames = 0;
		memcpy(&present, bytes.constData() + at, 8);
		memcpy(&frames, bytes.constData() + at + 8, 4);
		at += 12;
		if (frames <= 0 || at + qsizetype(frames) * 8 > bytes.size()) break;
		const float *samples = reinterpret_cast<const float *>(bytes.constData() + at);
		for (int i = 0; i < frames; ++i) {
			capture.us << double(present) + i * 1e6 / 48000;
			capture.left << samples[2 * i];
			capture.right << samples[2 * i + 1];
		}
		at += qsizetype(frames) * 8;
	}
	return capture;
}

}

/*
 * From the two captures, around the crossfade the taker reports: at every
 * frame the taker heard, the two gains sum to one (nothing missing,
 * nothing doubled); where both sound, they play the same frame; each
 * plays its frames one after another, without a skip or a repeat; and the
 * fade really overlaps, rather than one side cutting off.
 */
void handover_test::check_crossover(qint64 giver_pid, qint64 taker_pid, const snurra_wire::message &status) {
	const QDir dir(capture_dir());
	const capture_t giver = read_capture(dir.filePath(QStringLiteral("%1.capture").arg(giver_pid)));
	const capture_t taker = read_capture(dir.filePath(QStringLiteral("%1.capture").arg(taker_pid)));
	QVERIFY2(!giver.us.isEmpty() && !taker.us.isEmpty(), "no capture to read");
	QVERIFY2(status.started > 0 && status.stopped > status.started, "no crossfade reported");

	const double from = double(status.started) - 200000;
	const double to = double(status.stopped) + 200000;
	/* The giver from before; the taker, which may be a process started
	 * for the handover, from before its fade -- silent until then. */
	QVERIFY2(giver.us.first() < from, "the giver's capture does not reach back before the crossfade");
	QVERIFY2(taker.us.first() < double(status.started) && taker.us.last() > to,
	         "the taker's capture does not cover the crossfade");

	int checked = 0;
	int both = 0;
	double worst_sum = 1;
	double worst_at = 0;
	double worst_apart = 0;
	/* A grid of times, each from the start rather than summed, compared
	 * by nearest frame: good for gains and for where each side is, not for
	 * whether frames follow one another -- two grids half a frame apart
	 * can round onto one frame twice. That is checked below, on each
	 * capture's own frames. */
	const qint64 steps = qint64((to - from) * 48000 / 1e6);
	for (qint64 step = 0; step <= steps; ++step) {
		const double when = from + double(step) * 1e6 / 48000;
		const int t = taker.at(when);
		const int g = giver.at(when);
		const double gain_taker = taker.gain(t);
		const double gain_giver = giver.gain(g);
		const double sum = gain_taker + gain_giver;
		if (qAbs(sum - 1) > qAbs(worst_sum - 1)) {
			worst_sum = sum;
			worst_at = when - double(status.started);
		}
		++checked;
		if (gain_taker > 0.01 && gain_giver > 0.01) {
			++both;
			worst_apart = qMax(worst_apart, qAbs(giver.frame_at(g, when) - taker.frame_at(t, when)));
		}
	}
	/* Each side's own run, over the whole of its capture's sounding part:
	 * every frame the one after the last, no skip and no repeat. */
	for (const capture_t *capture : { &giver, &taker }) {
		const char *side = capture == &giver ? "giver" : "taker";
		double previous = -1;
		for (int i = 0; i < capture->us.size(); ++i) {
			if (capture->gain(i) <= 0.001) continue;
			const double frame = capture->frame(i);
			if (previous >= 0)
				QVERIFY2(qAbs(frame - previous - 1) < 0.5,
				         qPrintable(QStringLiteral("the %1 went from frame %2 to %3 at %4 ms from the fade's start")
				                            .arg(QLatin1String(side))
				                            .arg(previous)
				                            .arg(frame)
				                            .arg((capture->us.at(i) - double(status.started)) / 1000, 0, 'f', 2)));
			previous = frame;
		}
	}

	qInfo("crossover: %d frames checked, %d with both sounding, gains summed to %.4f at worst "
	      "(%.1f ms from the fade's start), %.2f frames apart at worst",
	      checked, both, worst_sum, worst_at / 1000, worst_apart);
	QVERIFY2(checked > 20000, "too little of the crossfade was checked");
	QVERIFY2(qAbs(worst_sum - 1) < 0.02,
	         qPrintable(QStringLiteral("the gains summed to %1: a gap or a doubling").arg(worst_sum)));
	QVERIFY2(both >= 48000 * (status.stopped - status.started) / 2000000,
	         qPrintable(QStringLiteral("only %1 frames with both sounding: no real overlap").arg(both)));
	/* A deck plays whole frames, so half a frame is the floor without
	 * resampling the taker; a little over it is the clock's microsecond.
	 * Measured over ten runs 2026-11-12: 0.48 at worst. */
	QVERIFY2(worst_apart <= 0.55, qPrintable(QStringLiteral("the two were %1 frames apart").arg(worst_apart)));
}

void handover_test::a_merge_crosses_over_in_step_without_a_gap() {
	const QString output = QStringLiteral("capture:") + capture_dir();
	QProcess *a = start({ QStringLiteral("snurra-test:ramp") }, output);
	QVERIFY(a->waitForStarted() && wait_for_socket(socket_of(a)));
	QProcess *b = start({ QStringLiteral("b.flac") }, output);
	QVERIFY(b->waitForStarted() && wait_for_socket(socket_of(b)));
	QTest::qWait(500);
	const qint64 giver = a->processId();

	snurra_wire::message merge;
	merge.type = snurra_wire::MERGE_INTO;
	merge.path = socket_of(b);
	tell(socket_of(a), merge);
	QVERIFY2(a->waitForFinished(5000), "the giver did not go once its fade was heard");
	QTest::qWait(500);

	const snurra_wire::message status = ask(socket_of(b), status_request());
	QCOMPARE(status.states.size(), 2);
	check_crossover(giver, b->processId(), status);
}

void handover_test::a_tile_pulled_out_crosses_over_in_step_without_a_gap() {
	const QString output = QStringLiteral("capture:") + capture_dir();
	QProcess *group = start({ QStringLiteral("snurra-test:ramp"), QStringLiteral("two.flac") }, output);
	QVERIFY(group->waitForStarted());
	const QString group_socket = socket_of(group);
	QVERIFY(wait_for_socket(group_socket));
	QTest::qWait(500);

	snurra_wire::message release;
	release.type = snurra_wire::RELEASE;
	release.index = 0;
	release.frame = QRect(40, 40, 1, 1);
	tell(group_socket, release);

	QString adopted;
	snurra_wire::message status;
	QElapsedTimer waited;
	waited.start();
	while (adopted.isEmpty() && waited.elapsed() < 10000) {
		for (const QString &path : sockets()) {
			if (path == group_socket) continue;
			status = ask(path, status_request(), 500);
			if (status.states.size() == 1 && status.stopped > 0) adopted = path;
		}
		QTest::qWait(100);
	}
	QVERIFY2(!adopted.isEmpty(), "no process took the released tile");
	QTest::qWait(500);
	status = ask(adopted, status_request());
	check_crossover(group->processId(), QFileInfo(adopted).baseName().toLongLong(), status);
}

/*
 * The same merge between two real ALSA outputs, as the default setup has
 * them: two processes writing to one dmix, so the device's own clock and
 * its own idea of latency place every frame. snd-aloop carries what the
 * dmix plays to a capture, which arecord records. No clocks need lining
 * up, because the mix describes itself: from the ramp source, the right
 * channel is the two gains summed, and left over right the frame heard
 * -- in step, the same frame from both. So: the right channel stays at 1
 * from the first sound to the last (no gap, nothing doubled), and the
 * frame heard less the sample's own index is one constant throughout
 * (no skip, no repeat, and the taker where the giver was).
 *
 * Skipped where there is no loopback card, no arecord, or no subdevice
 * free -- the machine's own default may be using one.
 */
void handover_test::a_merge_between_two_alsa_outputs_is_heard_in_step() {
	if (!QFile::exists(QStringLiteral("/proc/asound/Loopback"))) QSKIP("no snd-aloop card");
	const QString arecord = QStandardPaths::findExecutable(QStringLiteral("arecord"));
	if (arecord.isEmpty()) QSKIP("no arecord");
	int sub = -1;
	for (int k = 7; k >= 1 && sub < 0; --k) {
		auto closed = [k](const char *side) {
			QFile status(QStringLiteral("/proc/asound/Loopback/%1/sub%2/status").arg(QLatin1String(side)).arg(k));
			return status.open(QIODevice::ReadOnly) && status.readAll().trimmed() == "closed";
		};
		if (closed("pcm0p") && closed("pcm1c") && closed("pcm1p") && closed("pcm0c")) sub = k;
	}
	if (sub < 0) QSKIP("no loopback subdevice free");

	/* The system's configuration, and a shared device of this test's own. */
	const QString config = m_dir->filePath(QStringLiteral("asound.conf"));
	{
		QFile file(config);
		QVERIFY(file.open(QIODevice::WriteOnly));
		const int key = 0x5e000000 + int(QCoreApplication::applicationPid() & 0xffff);
		file.write(QStringLiteral("</usr/share/alsa/alsa.conf>\n"
		                          "pcm.snurra_test_mix {\n"
		                          "\ttype dmix\n\tipc_key %1\n\tipc_perm 0600\n"
		                          "\tslave { pcm \"hw:Loopback,0,%2\" format S32_LE rate 48000 channels 2 "
		                          "period_size 256 buffer_size 2048 }\n}\n"
		                          "pcm.snurra_test { type plug slave.pcm \"snurra_test_mix\" }\n")
		                   .arg(key)
		                   .arg(sub)
		                   .toUtf8());
	}
	QProcessEnvironment extra;
	extra.insert(QStringLiteral("ALSA_CONFIG_PATH"), config);
	const QString output = QStringLiteral("alsa:snurra_test");

	const QString recorded = m_dir->filePath(QStringLiteral("loop.raw"));
	QProcess recorder;
	recorder.start(arecord, { QStringLiteral("-q"), QStringLiteral("-D"), QStringLiteral("plughw:Loopback,1,%1").arg(sub),
	                          QStringLiteral("-t"), QStringLiteral("raw"), QStringLiteral("-f"), QStringLiteral("S32_LE"),
	                          QStringLiteral("-c"), QStringLiteral("2"), QStringLiteral("-r"), QStringLiteral("48000"),
	                          QStringLiteral("-d"), QStringLiteral("4"), recorded });
	QVERIFY(recorder.waitForStarted());
	QTest::qWait(200);

	QProcess *a = start({ QStringLiteral("snurra-test:wrapped-ramp") }, output, extra);
	QVERIFY(a->waitForStarted() && wait_for_socket(socket_of(a)));
	QProcess *b = start({ QStringLiteral("b.flac") }, output, extra);
	QVERIFY(b->waitForStarted() && wait_for_socket(socket_of(b)));
	QTest::qWait(1000);
	snurra_wire::message merge;
	merge.type = snurra_wire::MERGE_INTO;
	merge.path = socket_of(b);
	tell(socket_of(a), merge);
	QVERIFY2(a->waitForFinished(5000), "the giver did not go once its fade was heard");
	QVERIFY2(recorder.waitForFinished(8000), "arecord did not finish");
	const snurra_wire::message status = ask(socket_of(b), status_request());
	QCOMPARE(status.states.size(), 2);
	b->kill();
	b->waitForFinished(3000);

	QFile file(recorded);
	QVERIFY(file.open(QIODevice::ReadOnly));
	/* In the dmix's own format: snd-aloop takes the format of whichever
	 * end opens first, and a dmix needs an integer one. */
	const QByteArray bytes = file.readAll();
	const qsizetype frames = bytes.size() / 8;
	QList<float> samples(2 * frames);
	for (qsizetype i = 0; i < 2 * frames; ++i) {
		qint32 v = 0;
		memcpy(&v, bytes.constData() + 4 * i, 4);
		samples[i] = float(double(v) / 2147483648.0);
	}
	/* From the first sound to a little before the recording or the taker
	 * stopped -- what is checked is the stretch both ends of which sound. */
	qsizetype first = -1;
	qsizetype last = -1;
	for (qsizetype i = 0; i < frames; ++i) {
		if (samples[2 * i + 1] > 0.5f) {
			if (first < 0) first = i;
			last = i;
		}
	}
	QVERIFY2(first >= 0 && last - first > 48000, "too little sound recorded");
	first += 2400;   /* past the first period's start */
	last -= 2400;

	/*
	 * The frame heard less the sample's index, from the wrapped ramp:
	 * modulo 2^16, so each is taken as the nearest to the first, which
	 * holds while the two sides are within half of 2^16 frames.
	 */
	double reference = std::nan("");
	auto residual = [&](qsizetype i) {
		const double wrap = SNURRA_WRAPPED_RAMP_SCALE;
		double heard = double(samples[2 * i]) / samples[2 * i + 1] * wrap - double(i);
		heard = std::fmod(heard, wrap);
		if (std::isnan(reference)) reference = heard;
		heard -= std::round((heard - reference) / wrap) * wrap;
		return heard;
	};

	double worst_gain = 1;
	double lowest = 1e18;
	double highest = -1e18;
	qsizetype lowest_at = 0;
	qsizetype highest_at = 0;
	for (qsizetype i = first; i <= last; ++i) {
		const double gain = samples[2 * i + 1];
		if (qAbs(gain - 1) > qAbs(worst_gain - 1)) worst_gain = gain;
		if (gain < 0.5) continue;
		const double heard = residual(i);
		if (heard < lowest) {
			lowest = heard;
			lowest_at = i;
		}
		if (heard > highest) {
			highest = heard;
			highest_at = i;
		}
	}
	/* Its shape: within the giver alone (the first half second), within
	 * the taker alone (the last), and the step between them. */
	auto spread = [&](qsizetype from, qsizetype to, double &low, double &high) {
		low = 1e18;
		high = -1e18;
		for (qsizetype i = from; i <= to; ++i) {
			if (samples[2 * i + 1] < 0.5f) continue;
			const double heard = residual(i);
			low = qMin(low, heard);
			high = qMax(high, heard);
		}
	};
	double giver_low, giver_high, taker_low, taker_high;
	spread(first, first + 24000, giver_low, giver_high);
	spread(last - 24000, last, taker_low, taker_high);
	qInfo("alsa through the loopback (subdevice %d): %.2f s checked, gains summed to %.4f at worst, "
	      "the frame heard moved by %.2f frames (lowest at %.3f s, highest at %.3f s); giver alone %.2f, "
	      "taker alone %.2f, the step %+.2f",
	      sub, double(last - first) / 48000, worst_gain, highest - lowest, double(lowest_at - first) / 48000,
	      double(highest_at - first) / 48000, giver_high - giver_low, taker_high - taker_low,
	      (taker_low + taker_high) / 2 - (giver_low + giver_high) / 2);
	QVERIFY2(qAbs(worst_gain - 1) < 0.02,
	         qPrintable(QStringLiteral("the gains summed to %1: a gap or a doubling").arg(worst_gain)));
	/* Whole frames on each side: one apart is the floor's rounding.
	 * Measured 2026-11-12, twelve runs: 0.02 frames every time. */
	QVERIFY2(highest - lowest <= 1.05,
	         qPrintable(QStringLiteral("the frame heard moved by %1 frames across the handover").arg(highest - lowest)));
}

int run_handover_tests(int argc, char **argv) {
	handover_test test;
	return QTest::qExec(&test, argc, argv);
}
#include "handover_test.moc"
