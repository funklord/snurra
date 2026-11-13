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
#include <QLibrary>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QStandardPaths>

#include <cmath>
#include <cstring>
#include <QTemporaryDir>
#include <QtTest>

#include "snurra/snap.h"
#include "snurra/source.h"
#include "snurra/wire.h"

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
	void a_tile_dragged_onto_another_window_joins_it();
	void a_tile_opens_at_the_pointer_clamped_to_the_screen();
	void mplayers_keys_drive_the_tile();
	void mplayers_options_say_where_and_how_it_plays();
	void a_film_playing_keeps_the_screen_awake();
	void a_train_plays_its_tiles_in_sequence_crossed_at_equal_power();
	void a_stack_plays_its_lanes_at_once_from_one_time();
	void a_stack_finds_its_time_in_a_lane_that_is_a_train();

private:
	QString start_x_server();
	static QString xdotool(const QString &display, const QStringList &args);
	QProcess *start(const QStringList &args = {}, const QString &output = QStringLiteral("null"),
	                const QProcessEnvironment &extra = {});
	QString capture_dir() const { return m_dir->filePath(QStringLiteral("capture")); }
	void check_crossover(qint64 giver, qint64 taker, const snurra_wire::message &status);
	QString socket_of(QProcess *process) const;
	QProcess *start_process_for_options(const QString &time);
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
	/*
	 * Last started first, so a client goes before its server; and asked
	 * to stop before being made to: snurra closes its output on SIGTERM
	 * (a dmix's shared memory goes with it), and Xvfb removes its socket
	 * and lock -- both left behind by a SIGKILL.
	 */
	for (auto it = m_processes.rbegin(); it != m_processes.rend(); ++it) {
		QProcess *process = *it;
		if (process->state() != QProcess::NotRunning) {
			process->terminate();
			if (!process->waitForFinished(3000)) {
				process->kill();
				process->waitForFinished(3000);
			}
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
	/* $SNURRA_BIN runs these against another build of the player:
	 * `make check-mk`'s, from snurra.mk alone. */
	process->start(qEnvironmentVariable("SNURRA_BIN", QStringLiteral(SNURRA_BIN)), args);
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
/* Shared memory in this test's key range (0x5e00....) with nothing
 * attached, by key and id, from /proc -- no parsing of a tool's output. */
static QStringList dmix_segments() {
	QStringList found;
	QFile file(QStringLiteral("/proc/sysvipc/shm"));
	if (!file.open(QIODevice::ReadOnly)) return found;
	const QList<QByteArray> lines = file.readAll().split('\n');
	for (qsizetype i = 1; i < lines.size(); ++i) {
		const QList<QByteArray> fields = lines.at(i).simplified().split(' ');
		if (fields.size() < 7) continue;
		const qint64 key = fields.at(0).toLongLong();
		const qint64 attached = fields.at(6).toLongLong();
		if ((key >> 16) == 0x5e00 && attached == 0) found << QStringLiteral("%1/%2").arg(key, 0, 16).arg(QString::fromLatin1(fields.at(1)));
	}
	return found;
}

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
	const QStringList dmix_segments_before = dmix_segments();
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
	b->terminate();
	QVERIFY2(b->waitForFinished(3000), "the taker did not end on SIGTERM");

	/* $SNURRA_KEEP_RECORDING: a copy, to look at a run by hand. */
	if (qEnvironmentVariableIsSet("SNURRA_KEEP_RECORDING"))
		QFile::copy(recorded, qEnvironmentVariable("SNURRA_KEEP_RECORDING"));
	QFile file(recorded);
	QVERIFY(file.open(QIODevice::ReadOnly));
	/* In the dmix's own format: snd-aloop takes the format of whichever
	 * end opens first, and a dmix needs an integer one. The wrapped ramp
	 * plays at half scale, so read back at double. */
	const QByteArray bytes = file.readAll();
	const qsizetype frames = bytes.size() / 8;
	QList<float> samples(2 * frames);
	for (qsizetype i = 0; i < 2 * frames; ++i) {
		qint32 v = 0;
		memcpy(&v, bytes.constData() + 4 * i, 4);
		samples[i] = float(double(v) / 1073741824.0);
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
	/* A quarter frame. Measured 2026-11-12 over 24 runs, both builds,
	 * with the half-scale ramp and the median-filtered clock: 0.05 at
	 * worst, and the taker on the giver's very frame every time. */
	/*
	 * A cleanup is proved by counting what it left: a dmix frees its shared
	 * memory when its last client closes, and only then. Counted as what
	 * is new in this test's key range since it began, rather than by the
	 * key configured -- alsa-lib does not use that key as given (one run
	 * configured ...6640 and left ...6d40), and a check for the wrong key
	 * passed over a leak.
	 */
	const QStringList left_behind = dmix_segments();
	QVERIFY2(QFile::exists(QStringLiteral("/proc/sysvipc/shm")), "cannot list shared memory");
	QStringList new_ones;
	for (const QString &segment : left_behind)
		if (!dmix_segments_before.contains(segment)) new_ones << segment;
	QVERIFY2(new_ones.isEmpty(),
	         qPrintable(QStringLiteral("the dmix's shared memory was left behind: %1").arg(new_ones.join(QLatin1Char(' ')))));
	QVERIFY2(highest - lowest <= 0.25,
	         qPrintable(QStringLiteral("the frame heard moved by %1 frames across the handover").arg(highest - lowest)));
}

/*
 * Wayland's gesture (project.md sec 3b): a tile's title dragged onto
 * another snurra's window, as a drag and drop, joins it. Wayland itself is
 * not here, so the same drag and drop runs on X11 -- $SNURRA_DRAG=dnd -- in
 * a private Xvfb, driven by xdotool through XTEST: a real button pressed,
 * moved and let go, the way a hand does it. What is not shown is that a
 * compositor carries it the same way.
 */
void handover_test::a_tile_dragged_onto_another_window_joins_it() {
	const QString name = start_x_server();
	if (name.isEmpty()) QSKIP("no Xvfb, no xdotool, or no free display number");
	QProcessEnvironment x11;
	x11.insert(QStringLiteral("DISPLAY"), name);
	x11.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("xcb"));
	x11.insert(QStringLiteral("SNURRA_DRAG"), QStringLiteral("dnd"));
	auto run = [&](const QStringList &args) { return xdotool(name, args); };
	auto window_of = [&](QProcess *p) {
		return run({ QStringLiteral("search"), QStringLiteral("--sync"), QStringLiteral("--onlyvisible"),
		             QStringLiteral("--pid"), QString::number(p->processId()) })
		        .section(QLatin1Char('\n'), 0, 0);
	};

	QProcess *a = start({ QStringLiteral("a.flac") }, QStringLiteral("null"), x11);
	QVERIFY(a->waitForStarted() && wait_for_socket(socket_of(a)));
	QProcess *b = start({ QStringLiteral("b.flac") }, QStringLiteral("null"), x11);
	QVERIFY(b->waitForStarted() && wait_for_socket(socket_of(b)));
	/* Both open at the pointer; set apart. */
	run({ QStringLiteral("windowmove"), window_of(a), QStringLiteral("100"), QStringLiteral("100") });
	run({ QStringLiteral("windowmove"), window_of(b), QStringLiteral("600"), QStringLiteral("300") });
	QTest::qWait(300);

	snurra_wire::message where;
	where.type = snurra_wire::WHERE;
	const QRect from = ask(socket_of(a), where).frame;
	const QRect onto = ask(socket_of(b), where).frame;
	QVERIFY2(from.isValid() && onto.isValid() && !from.intersects(onto),
	         qPrintable(QStringLiteral("windows at %1,%2 and %3,%4").arg(from.x()).arg(from.y()).arg(onto.x()).arg(onto.y())));

	/* Pressed on A's title, moved in steps, let go near B's right edge. */
	{
		const QPoint grab = from.topLeft() + QPoint(24, 12);
		const QPoint drop(onto.right() - 8, onto.center().y());
		run({ QStringLiteral("mousemove"), QString::number(grab.x()), QString::number(grab.y()) });
		run({ QStringLiteral("mousedown"), QStringLiteral("1") });
		for (int step = 1; step <= 20; ++step) {
			const QPoint at = grab + (drop - grab) * step / 20;
			run({ QStringLiteral("mousemove"), QString::number(at.x()), QString::number(at.y()) });
			QTest::qWait(30);
		}
		QTest::qWait(200);
		run({ QStringLiteral("mouseup"), QStringLiteral("1") });
	}

	QVERIFY2(a->waitForFinished(8000), "the dragged tile's process did not hand it over and go");
	const snurra_wire::message joined = ask(socket_of(b), status_request());
	QCOMPARE(joined.states.size(), 2);
	snurra_tile_state moved;
	for (const snurra_tile_state &state : joined.states)
		if (state.source.endsWith(QLatin1String("a.flac"))) moved = state;
	QVERIFY2(moved.source.endsWith(QLatin1String("a.flac")), "the dragged tile is not there");
	QCOMPARE(moved.column, 1);   /* dropped by the right edge: next in the train */

	/* A drag along the screen, pressed at `grab` and let go at `drop`. */
	auto drag = [&](const QPoint &grab, const QPoint &drop) {
		run({ QStringLiteral("mousemove"), QString::number(grab.x()), QString::number(grab.y()) });
		run({ QStringLiteral("mousedown"), QStringLiteral("1") });
		for (int step = 1; step <= 20; ++step) {
			const QPoint at = grab + (drop - grab) * step / 20;
			run({ QStringLiteral("mousemove"), QString::number(at.x()), QString::number(at.y()) });
			QTest::qWait(30);
		}
		QTest::qWait(200);
		run({ QStringLiteral("mouseup"), QStringLiteral("1") });
	};

	/* Dropped on nothing, a tile of a group is pulled out into a process
	 * of its own -- the same gesture, with no window to land in. */
	QTest::qWait(300);
	const QRect group = ask(socket_of(b), where).frame;
	const QPoint second_title(group.x() + group.width() / 2 + 24, group.y() + 12);
	drag(second_title, QPoint(200, 650));
	QString pulled;
	QElapsedTimer waited;
	waited.start();
	while (pulled.isEmpty() && waited.elapsed() < 10000) {
		for (const QString &path : sockets())
			if (path != socket_of(b) && ask(path, status_request(), 500).states.size() == 1) pulled = path;
		QTest::qWait(100);
	}
	QVERIFY2(!pulled.isEmpty(), "a tile dropped on nothing was not pulled out");
	QTRY_COMPARE_WITH_TIMEOUT(ask(socket_of(b), status_request()).states.size(), 1, 5000);
	QVERIFY(ask(pulled, status_request()).states.value(0).source.endsWith(QLatin1String("a.flac")));

	/* And dropped by the bottom edge, it becomes a lane below. */
	QTest::qWait(300);
	const QRect lone = ask(pulled, where).frame;
	const QRect target = ask(socket_of(b), where).frame;
	QVERIFY2(lone.isValid() && !lone.intersects(target), "the pulled-out window lies over the group");
	drag(lone.topLeft() + QPoint(24, 12), QPoint(target.center().x(), target.bottom() - 6));
	QTRY_COMPARE_WITH_TIMEOUT(ask(socket_of(b), status_request()).states.size(), 2, 8000);
	snurra_tile_state below;
	for (const snurra_tile_state &state : ask(socket_of(b), status_request()).states)
		if (state.source.endsWith(QLatin1String("a.flac"))) below = state;
	QCOMPARE(below.row, 1);
	QCOMPARE(below.column, 0);
}

/* A WAV of `seconds` at 48 kHz, its left at `left` and right at `right`,
 * 16-bit, every frame the same: a constant a capture reads as a gain. */
static bool write_constant_wav(const QString &path, int seconds, qint16 left, qint16 right) {
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) return false;
	const quint32 bytes = quint32(seconds) * 48000 * 4;
	auto u32 = [&file](quint32 v) { file.write(reinterpret_cast<const char *>(&v), 4); };
	auto u16 = [&file](quint16 v) { file.write(reinterpret_cast<const char *>(&v), 2); };
	file.write("RIFF");
	u32(36 + bytes);
	file.write("WAVEfmt ");
	u32(16);
	u16(1);
	u16(2);
	u32(48000);
	u32(48000 * 4);
	u16(4);
	u16(16);
	file.write("data");
	u32(bytes);
	QByteArray frame(4, Qt::Uninitialized);
	std::memcpy(frame.data(), &left, 2);
	std::memcpy(frame.data() + 2, &right, 2);
	file.write(frame.repeated(int(bytes / 4)));
	return true;
}

/*
 * A train (project.md sec 3b): B snapped to the right of a playing A
 * follows it, B starting on the very moment A reaches its length less the
 * overlap, the two crossed at equal power. A holds half scale on the left
 * only and B on the right only, so A's process's capture reads each one's
 * gain directly: the overlap is exactly as long as asked, the summed
 * power through it is one, A is whole before it and B after, B plays all
 * of itself -- so it began at its first frame -- and the controls end on
 * B, with A stopped.
 */
void handover_test::a_train_plays_its_tiles_in_sequence_crossed_at_equal_power() {
	const QString a_file = m_dir->filePath(QStringLiteral("a.wav"));
	const QString b_file = m_dir->filePath(QStringLiteral("b.wav"));
	QVERIFY(write_constant_wav(a_file, 3, 16384, 0));
	QVERIFY(write_constant_wav(b_file, 3, 0, 16384));
	QProcessEnvironment train;
	train.insert(QStringLiteral("SNURRA_TRAIN_OVERLAP_MS"), QStringLiteral("1000"));
	const QString output = QStringLiteral("capture:") + capture_dir();

	QProcess *a = start({ a_file }, output, train);
	QVERIFY(a->waitForStarted() && wait_for_socket(socket_of(a)));
	QProcess *b = start({ b_file }, output, train);
	QVERIFY(b->waitForStarted() && wait_for_socket(socket_of(b)));
	snurra_wire::message merge;
	merge.type = snurra_wire::MERGE_INTO;
	merge.path = socket_of(a);
	merge.side = snurra_wire::RIGHT;
	tell(socket_of(b), merge);
	QVERIFY2(b->waitForFinished(5000), "B did not join A");
	QTRY_VERIFY_WITH_TIMEOUT(!ask(socket_of(a), status_request()).states.value(1).playing
	                                 && ask(socket_of(a), status_request()).states.size() == 2
	                                 && QFileInfo(capture_dir() + QStringLiteral("/%1.capture").arg(a->processId())).size()
	                                            > 8 * 48000 * 6,
	                         12000);
	QTest::qWait(500);

	const capture_t heard = read_capture(QDir(capture_dir()).filePath(QStringLiteral("%1.capture").arg(a->processId())));
	auto gain_a = [&](int i) { return double(heard.left.at(i)) / 0.5; };
	auto gain_b = [&](int i) { return double(heard.right.at(i)) / 0.5; };
	int b_first = -1;
	int b_last = -1;
	int a_last = -1;
	/* Any sample above zero sounds: an equal-power fade begins at sin 0,
	 * so a threshold of 1e-4 cut three frames off each end of it. */
	for (int i = 0; i < heard.us.size(); ++i) {
		if (gain_b(i) > 0) {
			if (b_first < 0) b_first = i;
			b_last = i;
		}
		if (gain_a(i) > 0) a_last = i;
	}
	QVERIFY2(b_first > 0 && a_last > b_first, "B never followed A, or A stopped first");
	const int overlap = a_last - b_first + 1;
	double worst_power = 0;
	for (int i = b_first; i <= a_last; ++i)
		worst_power = qMax(worst_power, qAbs(gain_a(i) * gain_a(i) + gain_b(i) * gain_b(i) - 1));
	double worst_before = 0;
	for (int i = b_first - 24000; i < b_first; ++i) worst_before = qMax(worst_before, qAbs(gain_a(i) - 1));
	double worst_after = 0;
	for (int i = a_last + 1; i <= b_last; ++i) worst_after = qMax(worst_after, qAbs(gain_b(i) - 1));
	qInfo("train: overlap %d frames, summed power off by %.4f at worst, A before %.4f, B after %.4f, B played %d frames",
	      overlap, worst_power, worst_before, worst_after, b_last - b_first + 1);
	QVERIFY2(qAbs(overlap - 48000) <= 2, qPrintable(QStringLiteral("an overlap of %1 frames").arg(overlap)));
	QVERIFY2(worst_power < 0.01, qPrintable(QStringLiteral("the summed power is off by %1").arg(worst_power)));
	QVERIFY2(worst_before < 1e-4 && worst_after < 1e-4, "a side was not whole outside the overlap");
	QVERIFY2(qAbs(b_last - b_first + 1 - 3 * 48000) <= 2,
	         qPrintable(QStringLiteral("B played %1 frames of %2").arg(b_last - b_first + 1).arg(3 * 48000)));

	const QList<snurra_tile_state> states = ask(socket_of(a), status_request()).states;
	for (const snurra_tile_state &state : states)
		QCOMPARE(state.controls, state.source == b_file);
}

/*
 * A stack (project.md sec 3b, step 4): B snapped below A is a second lane,
 * and the transport is the group's. Paused from A, sought to one second
 * from B and played from B, both lanes start on the same frame of the
 * shared clock and each plays what is left of it after that second --
 * A, three seconds long, two more; B, two seconds long, one more. A is
 * on the left only and B the right only, at a quarter scale, so the
 * capture reads each lane apart.
 */
void handover_test::a_stack_plays_its_lanes_at_once_from_one_time() {
	const QString a_file = m_dir->filePath(QStringLiteral("a.wav"));
	const QString b_file = m_dir->filePath(QStringLiteral("b.wav"));
	QVERIFY(write_constant_wav(a_file, 3, 8192, 0));
	QVERIFY(write_constant_wav(b_file, 2, 0, 8192));
	const QString output = QStringLiteral("capture:") + capture_dir();

	QProcess *a = start({ a_file }, output);
	QVERIFY(a->waitForStarted() && wait_for_socket(socket_of(a)));
	QProcess *b = start({ b_file }, output);
	QVERIFY(b->waitForStarted() && wait_for_socket(socket_of(b)));
	snurra_wire::message merge;
	merge.type = snurra_wire::MERGE_INTO;
	merge.path = socket_of(a);
	merge.side = snurra_wire::BELOW;
	tell(socket_of(b), merge);
	QVERIFY2(b->waitForFinished(5000), "B did not join A");
	QTRY_COMPARE_WITH_TIMEOUT(ask(socket_of(a), status_request()).states.size(), 2, 5000);
	QList<snurra_tile_state> states = ask(socket_of(a), status_request()).states;
	QCOMPARE(states.at(1).row, 1);

	auto control = [&](snurra_wire::type_t type, int index, qint64 time) {
		snurra_wire::message m;
		m.type = type;
		m.index = index;
		m.time = time;
		tell(socket_of(a), m);
	};
	control(snurra_wire::PLAY, 0, 0);
	QTRY_VERIFY_WITH_TIMEOUT(!ask(socket_of(a), status_request()).states.value(0).playing, 2000);
	control(snurra_wire::SEEK, 1, 1000000);
	QTest::qWait(300);

	states = ask(socket_of(a), status_request()).states;
	QCOMPARE(states.at(0).position_us, qint64(1000000));
	QCOMPARE(states.at(1).position_us, qint64(1000000));

	const double from = double(snurra_clock_us());
	control(snurra_wire::PLAY, 1, 1);
	QTRY_VERIFY_WITH_TIMEOUT(ask(socket_of(a), status_request()).states.value(0).playing, 2000);
	QTRY_VERIFY_WITH_TIMEOUT(!ask(socket_of(a), status_request()).states.value(0).playing, 5000);
	QTest::qWait(300);

	const capture_t heard = read_capture(QDir(capture_dir()).filePath(QStringLiteral("%1.capture").arg(a->processId())));
	int a_first = -1, a_last = -1, b_first = -1, b_last = -1;
	double worst = 0;
	for (int i = 0; i < heard.us.size(); ++i) {
		if (heard.us.at(i) < from) continue;
		if (heard.left.at(i) > 0) {
			if (a_first < 0) a_first = i;
			a_last = i;
			worst = qMax(worst, qAbs(heard.left.at(i) - 0.25));
		}
		if (heard.right.at(i) > 0) {
			if (b_first < 0) b_first = i;
			b_last = i;
			worst = qMax(worst, qAbs(heard.right.at(i) - 0.25));
		}
	}
	qInfo("stack: lanes began %d frames apart; A played %d frames, B %d; level off by %.5f at worst",
	      b_first - a_first, a_last - a_first + 1, b_last - b_first + 1, worst);
	QVERIFY2(a_first > 0 && b_first > 0, "a lane never sounded after the play");
	QCOMPARE(b_first, a_first);
	QVERIFY2(qAbs(a_last - a_first + 1 - 2 * 48000) <= 1,
	         qPrintable(QStringLiteral("A played %1 frames, not two seconds").arg(a_last - a_first + 1)));
	QVERIFY2(qAbs(b_last - b_first + 1 - 48000) <= 1,
	         qPrintable(QStringLiteral("B played %1 frames, not one second").arg(b_last - b_first + 1)));
	QVERIFY2(worst < 1e-4, "a lane was not at its level throughout");
}

/*
 * A lane that is itself a train (project.md sec 3b, step 4): A and B side
 * by side, two seconds each with a one-second overlap, so B begins at one
 * second on the timeline; C, three seconds, below them. Sought to 2.5 s
 * from C, the upper lane is B at 1.5 s -- not A -- and the lower C at
 * 2.5 s: each plays half a second, from one frame. A is at half scale on
 * the left, B a quarter on the left, C a quarter on the right.
 */
void handover_test::a_stack_finds_its_time_in_a_lane_that_is_a_train() {
	const QString a_file = m_dir->filePath(QStringLiteral("a.wav"));
	const QString b_file = m_dir->filePath(QStringLiteral("b.wav"));
	const QString c_file = m_dir->filePath(QStringLiteral("c.wav"));
	QVERIFY(write_constant_wav(a_file, 2, 16384, 0));
	QVERIFY(write_constant_wav(b_file, 2, 8192, 0));
	QVERIFY(write_constant_wav(c_file, 3, 0, 8192));
	QProcessEnvironment train;
	train.insert(QStringLiteral("SNURRA_TRAIN_OVERLAP_MS"), QStringLiteral("1000"));
	const QString output = QStringLiteral("capture:") + capture_dir();

	QProcess *a = start({ a_file }, output, train);
	QVERIFY(a->waitForStarted() && wait_for_socket(socket_of(a)));
	auto join = [&](const QString &file, snurra_wire::side_t side) {
		QProcess *p = start({ file }, output, train);
		if (!p->waitForStarted() || !wait_for_socket(socket_of(p))) return false;
		snurra_wire::message merge;
		merge.type = snurra_wire::MERGE_INTO;
		merge.path = socket_of(a);
		merge.side = side;
		tell(socket_of(p), merge);
		return p->waitForFinished(5000);
	};
	auto control = [&](snurra_wire::type_t type, int index, qint64 time) {
		snurra_wire::message m;
		m.type = type;
		m.index = index;
		m.time = time;
		tell(socket_of(a), m);
	};
	/* Paused before B joins, so no train runs on its own meanwhile. */
	control(snurra_wire::PLAY, 0, 0);
	QVERIFY2(join(b_file, snurra_wire::RIGHT), "B did not join A");
	QVERIFY2(join(c_file, snurra_wire::BELOW), "C did not join below");
	QTRY_COMPARE_WITH_TIMEOUT(ask(socket_of(a), status_request()).states.size(), 3, 5000);
	const QList<snurra_tile_state> states = ask(socket_of(a), status_request()).states;
	int c_index = -1;
	for (int i = 0; i < states.size(); ++i) {
		QVERIFY2(!states.at(i).playing, "a tile was playing before the play");
		if (states.at(i).source == c_file) c_index = i;
	}
	QVERIFY(c_index >= 0);
	QCOMPARE(states.at(c_index).row, 1);

	control(snurra_wire::SEEK, c_index, 2500000);
	QTest::qWait(300);
	const double from = double(snurra_clock_us());
	control(snurra_wire::PLAY, c_index, 1);
	QTest::qWait(1500);

	const capture_t heard = read_capture(QDir(capture_dir()).filePath(QStringLiteral("%1.capture").arg(a->processId())));
	int up_first = -1, up_last = -1, down_first = -1, down_last = -1;
	float up_loudest = 0;
	for (int i = 0; i < heard.us.size(); ++i) {
		if (heard.us.at(i) < from) continue;
		if (heard.left.at(i) > 0) {
			if (up_first < 0) up_first = i;
			up_last = i;
			up_loudest = qMax(up_loudest, heard.left.at(i));
		}
		if (heard.right.at(i) > 0) {
			if (down_first < 0) down_first = i;
			down_last = i;
		}
	}
	qInfo("stack with a train: lanes began %d frames apart; upper %d frames at %.4f, lower %d frames",
	      down_first - up_first, up_last - up_first + 1, up_loudest, down_last - down_first + 1);
	QVERIFY2(up_first > 0 && down_first > 0, "a lane never sounded after the play");
	QCOMPARE(down_first, up_first);
	QVERIFY2(qAbs(up_loudest - 0.25f) < 1e-4, "the upper lane played A, not B");
	QVERIFY2(qAbs(up_last - up_first + 1 - 24000) <= 1, "the upper lane did not play B's last half second");
	QVERIFY2(qAbs(down_last - down_first + 1 - 24000) <= 1, "the lower lane did not play C's last half second");
}

/* A private Xvfb, 1280x800, ended in cleanup() with the rest: its
 * display name, or empty where there is no Xvfb, no xdotool to drive it,
 * or no free display number. */
QString handover_test::start_x_server() {
	const QString xvfb = QStandardPaths::findExecutable(QStringLiteral("Xvfb"));
	if (xvfb.isEmpty() || QStandardPaths::findExecutable(QStringLiteral("xdotool")).isEmpty()) return QString();
	int display = -1;
	for (int n = 90; n < 130 && display < 0; ++n)
		if (!QFile::exists(QStringLiteral("/tmp/.X11-unix/X%1").arg(n)) && !QFile::exists(QStringLiteral("/tmp/.X%1-lock").arg(n)))
			display = n;
	if (display < 0) return QString();
	const QString name = QStringLiteral(":%1").arg(display);
	QProcess *server = new QProcess;
	m_processes << server;
	server->start(xvfb, { name, QStringLiteral("-screen"), QStringLiteral("0"), QStringLiteral("1280x800x24"),
	                      QStringLiteral("-nolisten"), QStringLiteral("tcp") });
	if (!server->waitForStarted()) return QString();
	QElapsedTimer waited;
	waited.start();
	/* Answering, not merely listening: its socket appears before it
	 * serves, and a pointer moved then stays where it was. */
	while (!xdotool(name, { QStringLiteral("getmouselocation") }).startsWith(QLatin1String("x:")) && waited.elapsed() < 5000)
		QTest::qWait(50);
	return waited.elapsed() < 5000 ? name : QString();
}

/* xdotool on `display`: what it printed. */
QString handover_test::xdotool(const QString &display, const QStringList &args) {
	QProcess tool;
	QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
	env.insert(QStringLiteral("DISPLAY"), display);
	tool.setProcessEnvironment(env);
	tool.start(QStringLiteral("xdotool"), args);
	tool.waitForFinished(5000);
	return QString::fromLocal8Bit(tool.readAllStandardOutput()).trimmed();
}

/*
 * project.md sec 3b: a tile opens at the pointer -- where the file was
 * clicked -- clamped to the screen. By the bottom right corner it lies
 * wholly on the screen; in the middle, it is centred on the pointer.
 */
void handover_test::a_tile_opens_at_the_pointer_clamped_to_the_screen() {
	const QString name = start_x_server();
	if (name.isEmpty()) QSKIP("no Xvfb, no xdotool, or no free display number");
	QProcessEnvironment x11;
	x11.insert(QStringLiteral("DISPLAY"), name);
	x11.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("xcb"));
	const QRect screen(0, 0, 1280, 800);
	snurra_wire::message where;
	where.type = snurra_wire::WHERE;

	/* Xvfb with no window at all reports the pointer where it started,
	 * however it is moved -- XWarpPointer and XTEST alike -- and Qt asks
	 * the same way. One window open, and it follows. */
	QProcess *placeholder = start({ QStringLiteral("z.flac"), QStringLiteral("--paused") }, QStringLiteral("null"), x11);
	QVERIFY(placeholder->waitForStarted() && wait_for_socket(socket_of(placeholder)));
	xdotool(name, { QStringLiteral("mousemove"), QStringLiteral("1275"), QStringLiteral("795") });
	QVERIFY2(xdotool(name, { QStringLiteral("getmouselocation") }).startsWith(QLatin1String("x:1275 y:795")),
	         "the pointer did not move");
	QProcess *corner = start({ QStringLiteral("a.flac") }, QStringLiteral("null"), x11);
	QVERIFY(corner->waitForStarted() && wait_for_socket(socket_of(corner)));
	QRect frame;
	QTRY_VERIFY_WITH_TIMEOUT((frame = ask(socket_of(corner), where).frame).isValid(), 5000);
	QVERIFY2(screen.contains(frame),
	         qPrintable(QStringLiteral("opened at %1,%2 %3x%4, off the screen")
	                            .arg(frame.x()).arg(frame.y()).arg(frame.width()).arg(frame.height())));
	QVERIFY2(frame.right() >= 1270 && frame.bottom() >= 790, qPrintable(QStringLiteral("at %1,%2 %3x%4: not as near the pointer as the screen allows").arg(frame.x()).arg(frame.y()).arg(frame.width()).arg(frame.height())));

	xdotool(name, { QStringLiteral("mousemove"), QStringLiteral("640"), QStringLiteral("400") });
	QProcess *middle = start({ QStringLiteral("b.flac") }, QStringLiteral("null"), x11);
	QVERIFY(middle->waitForStarted() && wait_for_socket(socket_of(middle)));
	QTRY_VERIFY_WITH_TIMEOUT((frame = ask(socket_of(middle), where).frame).isValid(), 5000);
	QVERIFY2((frame.center() - QPoint(640, 400)).manhattanLength() <= 4,
	         qPrintable(QStringLiteral("centred on %1,%2, not the pointer").arg(frame.center().x()).arg(frame.center().y())));
}

/*
 * snurra is to replace mplayer (project.md sec 1), so it answers to its
 * keys: real key presses through XTEST into the window, in Xvfb. Paused,
 * a seek moves by exactly what the key says.
 */
void handover_test::mplayers_keys_drive_the_tile() {
	const QString name = start_x_server();
	if (name.isEmpty()) QSKIP("no Xvfb, no xdotool, or no free display number");
	QProcessEnvironment x11;
	x11.insert(QStringLiteral("DISPLAY"), name);
	x11.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("xcb"));
	QProcess *a = start({ QStringLiteral("a.flac") }, QStringLiteral("null"), x11);
	QVERIFY(a->waitForStarted() && wait_for_socket(socket_of(a)));
	snurra_wire::message where;
	where.type = snurra_wire::WHERE;
	QRect frame;
	QTRY_VERIFY_WITH_TIMEOUT((frame = ask(socket_of(a), where).frame).isValid(), 5000);
	/* Xvfb's focus follows the pointer: over the waveform, below the title. */
	xdotool(name, { QStringLiteral("mousemove"), QString::number(frame.center().x()), QString::number(frame.center().y()) });
	QTest::qWait(200);
	auto key = [&](const QString &which) {
		xdotool(name, { QStringLiteral("key"), which });
		QTest::qWait(150);
	};
	auto now = [&] { return ask(socket_of(a), status_request()).states.value(0); };

	QVERIFY(now().playing);
	key(QStringLiteral("space"));
	QVERIFY2(!now().playing, "space did not pause");
	key(QStringLiteral("space"));
	QVERIFY2(now().playing, "space did not play again");
	key(QStringLiteral("p"));
	QVERIFY2(!now().playing, "p did not pause");

	const qint64 from = now().position_us;
	key(QStringLiteral("Right"));
	QCOMPARE(now().position_us - from, qint64(10000000));
	key(QStringLiteral("Up"));
	QCOMPARE(now().position_us - from, qint64(70000000));
	key(QStringLiteral("Left"));
	QCOMPARE(now().position_us - from, qint64(60000000));

	QCOMPARE(now().level, 100);
	key(QStringLiteral("9"));
	QCOMPARE(now().level, 97);
	key(QStringLiteral("0"));
	QCOMPARE(now().level, 100);
	key(QStringLiteral("m"));
	QCOMPARE(now().level, 0);
	key(QStringLiteral("m"));
	QCOMPARE(now().level, 100);

	/* mplayer's speed keys, on the tempo. */
	QCOMPARE(now().tempo, 1.0f);
	key(QStringLiteral("bracketright"));
	QVERIFY(qAbs(now().tempo - 1.1f) < 1e-4f);
	key(QStringLiteral("bracketleft"));
	QVERIFY(qAbs(now().tempo - 1.0f) < 1e-4f);
	key(QStringLiteral("braceright"));
	QVERIFY(qAbs(now().tempo - 2.0f) < 1e-4f);
	key(QStringLiteral("braceleft"));
	key(QStringLiteral("braceleft"));
	QVERIFY(qAbs(now().tempo - 0.5f) < 1e-4f);
	key(QStringLiteral("BackSpace"));
	QCOMPARE(now().tempo, 1.0f);

	/* waitForFinished() is false for a process already finished, which
	 * is what a prompt quit is by the time it is asked. */
	key(QStringLiteral("q"));
	QVERIFY2(a->state() == QProcess::NotRunning || a->waitForFinished(5000), "q did not quit");
}

void handover_test::mplayers_options_say_where_and_how_it_plays() {
	/* As the hands type them, one dash and a word: -ss in each of its
	 * forms, the level and the speed; and a time that is none is refused
	 * rather than read as 0. */
	const struct {
		QString time;
		qint64 us;
	} starts[] = { { QStringLiteral("75"), 75000000 },
		       { QStringLiteral("1:02.5"), 62500000 },
		       { QStringLiteral("1:00:03"), 3603000000 } };
	for (const auto &start : starts) {
		QProcess *a = start_process_for_options(start.time);
		QVERIFY(a->waitForStarted() && wait_for_socket(socket_of(a)));
		snurra_tile_state now;
		QTRY_VERIFY_WITH_TIMEOUT((now = ask(socket_of(a), status_request()).states.value(0)).duration_us > 0, 5000);
		QCOMPARE(now.position_us, start.us);
		QVERIFY(!now.playing);
		QCOMPARE(now.level, 50);
		QVERIFY(qAbs(now.tempo - 1.5f) < 1e-4f);
		a->terminate();
		QVERIFY(a->waitForFinished(5000));
	}
	QProcess *bad = start({ QStringLiteral("-ss"), QStringLiteral("soon"), QStringLiteral("a.flac") });
	QVERIFY(bad->waitForFinished(5000));
	QCOMPARE(bad->exitCode(), 2);
}

QProcess *handover_test::start_process_for_options(const QString &time) {
	return start({ QStringLiteral("-ss"), time, QStringLiteral("-volume"), QStringLiteral("50"), QStringLiteral("-speed"),
	               QStringLiteral("1.5"), QStringLiteral("--paused"), QStringLiteral("a.flac") });
}

/*
 * How long the X server on `display` has seen no input, in milliseconds,
 * as a screen saver or locker reads it (MIT-SCREEN-SAVER); -1 where it
 * cannot be asked. libX11 and libXss are resolved here rather than linked,
 * so the suite builds where they are not.
 */
static qint64 x_idle_ms(const QString &display) {
	struct info {
		unsigned long window;
		int state;
		int kind;
		unsigned long til_or_since;
		unsigned long idle;
		unsigned long event_mask;
	};
	using open_t = void *(*)(const char *);
	using close_t = int (*)(void *);
	using root_t = unsigned long (*)(void *);
	using query_t = int (*)(void *, unsigned long, info *);
	const auto open = reinterpret_cast<open_t>(QLibrary::resolve(QStringLiteral("X11"), 6, "XOpenDisplay"));
	const auto close = reinterpret_cast<close_t>(QLibrary::resolve(QStringLiteral("X11"), 6, "XCloseDisplay"));
	const auto root = reinterpret_cast<root_t>(QLibrary::resolve(QStringLiteral("X11"), 6, "XDefaultRootWindow"));
	const auto query = reinterpret_cast<query_t>(QLibrary::resolve(QStringLiteral("Xss"), 1, "XScreenSaverQueryInfo"));
	if (!open || !close || !root || !query) return -1;
	void *x = open(display.toLocal8Bit().constData());
	if (!x) return -1;
	info got {};
	const qint64 idle = query(x, root(x), &got) ? qint64(got.idle) : -1;
	close(x);
	return idle;
}

/*
 * A film playing keeps the screen saver and the locker away, as mplayer's
 * heartbeat does: on an X server nobody touches, twelve seconds in, the
 * idle time a saver reads has been reset within the last ten.
 */
void handover_test::a_film_playing_keeps_the_screen_awake() {
	const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
	if (ffmpeg.isEmpty()) QSKIP("no ffmpeg");
	const QString name = start_x_server();
	if (name.isEmpty()) QSKIP("no Xvfb, no xdotool, or no free display number");
	if (x_idle_ms(name) < 0) QSKIP("no libX11 or libXss to ask the server's idle time");
	const QString film = m_dir->filePath(QStringLiteral("film.mp4"));
	QProcess make;
	make.start(ffmpeg, { QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-y"), QStringLiteral("-f"),
	                     QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("testsrc=s=64x48:r=10:d=20"),
	                     QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("sine=d=20"),
	                     QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), film });
	QVERIFY(make.waitForFinished(60000) && make.exitCode() == 0);

	QProcessEnvironment x11;
	x11.insert(QStringLiteral("DISPLAY"), name);
	x11.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("xcb"));
	QProcess *a = start({ QStringLiteral("-fs"), film }, QStringLiteral("null"), x11);
	QVERIFY(a->waitForStarted() && wait_for_socket(socket_of(a)));
	QTest::qWait(12000);
	const qint64 idle = x_idle_ms(name);
	qInfo("awake: the X server idle %lld ms, twelve seconds in", idle);
	QVERIFY2(idle >= 0 && idle < 10500, "the screen saver's idle time was not reset while the film played");
}

int run_handover_tests(int argc, char **argv) {
	handover_test test;
	return QTest::qExec(&test, argc, argv);
}
#include "handover_test.moc"
