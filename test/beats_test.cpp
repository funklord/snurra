/*
 * Beat detection (project.md sec 3b, step 6): signals made here, whose every
 * beat is known to the frame, read back through snurra_find_beats -- the
 * tempo, and the grid's error at every true beat across a minute, which a
 * tempo off by a little turns into a drift that grows to the end. And
 * signals with no beat at all, which must be given none.
 */
#include <QNetworkAccessManager>
#include <QAbstractItemModel>
#include <QItemSelectionModel>
#include <QProcess>
#include <QPushButton>
#include <QTreeView>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

#include <cmath>
#include <random>
#include <vector>

#include "snurra/beats.h"
#include "snurra/deck.h"
#include "snurra/timeline.h"
#include "snurra/tile_state.h"
#include "snurra/fingerprint.h"
#include "snurra/library.h"
#include "snurra/node.h"
#include "snurra/tile.h"
#include "snurra/peaks.h"
#include "snurra/source.h"

namespace {

constexpr int RATE = 48000;

/* Stereo frames held in memory, as a source. */
class memory_source : public snurra_source {
public:
	explicit memory_source(std::vector<float> mono) : m_mono(std::move(mono)) {}
	int read(float *out, int frames) override {
		const int n = int(std::min<int64_t>(frames, int64_t(m_mono.size()) - m_at));
		for (int i = 0; i < n; ++i) out[2 * i] = out[2 * i + 1] = m_mono[size_t(m_at + i)];
		m_at += std::max(0, n);
		return std::max(0, n);
	}
	bool at_end() const override { return m_at >= int64_t(m_mono.size()); }
	void seek(int64_t frame) override { m_at = std::clamp<int64_t>(frame, 0, int64_t(m_mono.size())); }
	int64_t position() const override { return m_at; }
	int64_t length() const override { return int64_t(m_mono.size()); }

private:
	std::vector<float> m_mono;
	int64_t m_at = 0;
};

/* A drum hit at `at` frames: a decaying 55 Hz thump and a click (kick), or
 * decaying noise (snare, or a short high hat). */
enum hit_t { KICK, SNARE, HAT };

void add_hit(std::vector<float> &out, double at, hit_t hit, std::mt19937 &noise) {
	std::uniform_real_distribution<float> white(-1, 1);
	const int length = hit == HAT ? RATE / 40 : RATE / 4;
	const int start = int(std::lround(at));
	float last = 0;
	for (int i = 0; i < length && start + i < int(out.size()); ++i) {
		if (start + i < 0) continue;
		const double t = double(i) / RATE;
		float v = 0;
		if (hit == KICK) v = float(0.7 * std::sin(2 * M_PI * 55 * t) * std::exp(-t / 0.09) + (i < 96 ? 0.3 * white(noise) : 0));
		if (hit == SNARE) v = float(0.4 * white(noise) * std::exp(-t / 0.07));
		if (hit == HAT) {
			const float n = white(noise);
			v = float(0.15 * (n - last) * std::exp(-t / 0.01));   /* differenced: high */
			last = n;
		}
		out[size_t(start + i)] += v;
	}
}

struct made_t {
	std::vector<float> mono;
	std::vector<double> beats;   /* every beat's frame */
};

/* `seconds` of a beat at `bpm`, the first at `first` seconds: a bare kick
 * on every beat, or kick, snare, kick, snare with eighth-note hats and a
 * held chord under it all. */
made_t beat(double bpm, double first, bool pattern, int seconds = 60) {
	made_t made;
	made.mono.assign(size_t(seconds) * RATE, 0.0f);
	std::mt19937 noise(7);
	const double period = 60.0 * RATE / bpm;
	if (pattern)
		for (size_t i = 0; i < made.mono.size(); ++i) {
			const double t = double(i) / RATE;
			made.mono[i] += float(0.08 * (std::sin(2 * M_PI * 220 * t) + std::sin(2 * M_PI * 277.2 * t) + std::sin(2 * M_PI * 329.6 * t)));
		}
	for (int k = 0;; ++k) {
		const double at = first * RATE + k * period;
		if (at >= double(made.mono.size())) break;
		made.beats.push_back(at);
		add_hit(made.mono, at, pattern && k % 2 ? SNARE : KICK, noise);
		if (pattern) {
			add_hit(made.mono, at, HAT, noise);
			add_hit(made.mono, at + period / 2, HAT, noise);
		}
	}
	return made;
}

}

class beats_test : public QObject {
	Q_OBJECT

private slots:
	void a_timeline_places_its_tiles();
	void channels_are_seated_so_none_overlaps();
	void a_player_sounds_as_its_tiles();
	void a_nested_tracks_eq_is_the_decks();
	void a_players_waveform_is_its_mix();
	void a_players_tracks_are_analysed_in_its_tile();
	void a_beat_is_found_to_the_frame_data();
	void a_beat_is_found_to_the_frame();
	void what_has_no_beat_is_given_none_data();
	void what_has_no_beat_is_given_none();
	void an_encoded_steady_tone_has_no_beat_data();
	void an_encoded_steady_tone_has_no_beat();
	void an_encoded_beat_is_still_found();
	void a_trains_overlap_brings_the_next_in_on_the_beat_data();
	void a_trains_overlap_brings_the_next_in_on_the_beat();
	void a_set_whose_phase_moves_has_its_tempo();
	void a_dotted_rhythm_is_not_taken_for_the_beat();
	void a_fingerprint_knows_a_tune_however_it_is_encoded();
	void acoustid_is_asked_and_its_answer_read();
	void the_library_identifies_a_tune_and_keeps_its_id();
	void a_key_is_found_data();
	void a_key_is_found();
	void keys_are_named_on_the_camelot_wheel();
	void loudness_is_measured_as_ebu_r128_says_data();
	void loudness_is_measured_as_ebu_r128_says();
	void a_loud_tune_is_cut_and_a_quiet_one_boosted_to_its_peak();
};

/*
 * Step 6 (project.md sec 3b): where both tracks have a beat, the train's
 * overlap is whole bars of the first near the plain 5 s, moved so the
 * next one's first beat falls on a beat of the first -- at any tempo of
 * either, and within half of the shorter. Without both grids, 5 s.
 */
void beats_test::a_trains_overlap_brings_the_next_in_on_the_beat_data() {
	QTest::addColumn<double>("bpm");
	QTest::addColumn<double>("first");
	QTest::addColumn<double>("tempo");
	QTest::addColumn<double>("next_bpm");
	QTest::addColumn<double>("next_first");
	QTest::addColumn<double>("next_tempo");
	QTest::addColumn<double>("next_seconds");
	QTest::newRow("120 into 128") << 120.0 << 1000.0 << 1.0 << 128.0 << 7000.0 << 1.0 << 60.0;
	QTest::newRow("both at tempos") << 127.3 << 333.0 << 1.25 << 90.0 << 21000.0 << 0.8 << 60.0;
	QTest::newRow("a short next") << 174.0 << 50.0 << 1.0 << 140.0 << 400.0 << 1.0 << 5.0;
	QTest::newRow("a late first beat") << 100.0 << 9000.0 << 1.0 << 100.0 << 96000.0 << 1.0 << 60.0;
}

void beats_test::a_trains_overlap_brings_the_next_in_on_the_beat() {
	QFETCH(double, bpm);
	QFETCH(double, first);
	QFETCH(double, tempo);
	QFETCH(double, next_bpm);
	QFETCH(double, next_first);
	QFETCH(double, next_tempo);
	QFETCH(double, next_seconds);
	constexpr int RATE = 48000;
	const int64_t length = 60 * RATE + 1234;   /* frames out */
	const int64_t next_length = int64_t(next_seconds * RATE);
	snurra_beat_grid grid;
	grid.period = 60.0 * RATE / bpm;
	grid.first = first;
	grid.rate = RATE;
	snurra_beat_grid next_grid = grid;
	next_grid.period = 60.0 * RATE / next_bpm;
	next_grid.first = next_first;

	const int64_t plain = snurra_train_overlap(length, next_length, RATE);
	QCOMPARE(plain, std::min<int64_t>(5 * RATE, next_length / 2));
	snurra_beat_grid none;
	QCOMPARE(snurra_train_overlap(length, next_length, RATE, tempo, next_tempo, &none, &next_grid), plain);

	const int64_t overlap = snurra_train_overlap(length, next_length, RATE, tempo, next_tempo, &grid, &next_grid);
	QVERIFY(overlap > 0 && overlap <= std::min(length, next_length) / 2);
	/* The next one's first beat, as a frame of the first track. */
	const double at = (double(length - overlap) + next_first / next_tempo) * tempo;
	const double beats = (at - first) / grid.period;
	QVERIFY2(std::abs(beats - std::round(beats)) * grid.period < 1.0,
	         qPrintable(QStringLiteral("the next comes in %1 of a beat off").arg(beats - std::round(beats))));
	/* Near the plain overlap's whole bars, where they fit. */
	const double beat = grid.period / tempo;
	const double whole = std::max(1.0, std::round(double(plain) / (4 * beat))) * 4 * beat;
	if (whole <= double(next_length) / 2) QVERIFY(std::abs(double(overlap) - whole) <= beat / 2 + 1);

	/* Dragged to come in elsewhere: 11.3 s chosen is moved onto a beat --
	 * the next one's first beat on one of the first's, by less than half a
	 * beat -- and kept as it is by the train once chosen. */
	const int64_t chosen = std::min<int64_t>(int64_t(11.3 * RATE), std::min(length, next_length) / 2 - 1);
	const int64_t on_beat = snurra_overlap_on_beat(chosen, length, next_length, tempo, next_tempo, &grid, &next_grid);
	const double chosen_at = (double(length - on_beat) + next_first / next_tempo) * tempo;
	const double chosen_beats = (chosen_at - first) / grid.period;
	QVERIFY2(std::abs(chosen_beats - std::round(chosen_beats)) * grid.period < 1.0,
	         qPrintable(QStringLiteral("dragged, it comes in %1 of a beat off").arg(chosen_beats - std::round(chosen_beats))));
	QVERIFY2(std::abs(double(on_beat - chosen)) <= beat / 2 + 1 || on_beat == chosen,
	         qPrintable(QStringLiteral("moved %1 frames from where it was put, a beat %2").arg(on_beat - chosen).arg(beat)));
	QCOMPARE(snurra_train_overlap(length, next_length, RATE, tempo, next_tempo, &grid, &next_grid, on_beat), on_beat);
	/* No grid, as it was put. */
	QCOMPARE(snurra_overlap_on_beat(chosen, length, next_length, tempo, next_tempo, &none, &next_grid), chosen);
}

/*
 * The timeline as data (project.md sec 3f): a train of three -- 60 s at
 * tempo 1, 30 s at 1.5 (20 s out), 40 s -- each following the one before
 * over the plain 5 s, and a free track coming in at 10 s, nudged 2 s
 * ahead. Each follower comes in where the one before less the overlap
 * ends; a time inside an overlap is the next track's; before a late
 * track, it waits; past the end, nothing; and time_of undoes at.
 */
void beats_test::a_timeline_places_its_tiles() {
	snurra_timeline timeline(RATE);
	int previous = -1;
	for (const auto &spec : { std::pair<double, double>{ 60, 1 }, { 30, 1.5 }, { 40, 1 } }) {
		snurra_timeline_track one;
		one.length = int64_t(spec.first * RATE);
		one.tempo = spec.second;
		one.follows = previous;
		previous = int(timeline.tracks.size());
		timeline.tracks.push_back(one);
	}
	snurra_timeline_track late;
	late.length = 50 * RATE;
	late.offset = 2 * RATE;
	late.position = 10 * RATE;
	timeline.tracks.push_back(late);

	QCOMPARE(timeline.train(1), (std::vector<int>{ 0, 1, 2 }));
	QCOMPARE(timeline.train(3), (std::vector<int>{ 3 }));
	QCOMPARE(timeline.overlap(0), int64_t(5 * RATE));
	QCOMPARE(timeline.overlap(2), int64_t(0));
	QCOMPARE(timeline.start(0), int64_t(0));
	QCOMPARE(timeline.start(1), int64_t(55 * RATE));
	QCOMPARE(timeline.start(2), int64_t(70 * RATE));   /* 55 + 20 out - 5 */
	const std::vector<int> train = timeline.train(0);
	/* In the first overlap: the second track's, 1 s in out, 1.5 s of it. */
	const snurra_timeline::place crossing = timeline.at(train, 56 * RATE);
	QCOMPARE(crossing.track, 1);
	QCOMPARE(crossing.frame, int64_t(1.5 * RATE));
	QCOMPARE(timeline.time_of(crossing.track, crossing.frame), int64_t(56 * RATE));
	QCOMPARE(timeline.at(train, 54 * RATE).track, 0);
	QCOMPARE(timeline.at(train, 110 * RATE).track, -1);
	/* Before the late track: from its offset, after a wait. */
	const snurra_timeline::place before = timeline.at({ 3 }, 4 * RATE);
	QCOMPARE(before.track, 3);
	QCOMPARE(before.frame, int64_t(2 * RATE));
	QCOMPARE(before.wait, int64_t(6 * RATE));
	const snurra_timeline::place inside = timeline.at({ 3 }, 15 * RATE);
	QCOMPARE(inside.frame, int64_t(7 * RATE));
	QCOMPARE(timeline.time_of(3, inside.frame), int64_t(15 * RATE));
	QCOMPARE(timeline.at({}, 0).track, -1);
}

/*
 * Channels seated, not stored (project.md sec 3f): a train alternates
 * over two, its overlaps being at most half a track; a second train
 * connected after the first changes which channel it starts on so that
 * nothing ever sounds twice on one; trains at once take as many as sound together; a channel a
 * track was on is kept where free; a pin is kept and, where it collides,
 * named.
 */
void beats_test::channels_are_seated_so_none_overlaps() {
	auto add = [](snurra_timeline &timeline, int follows, int64_t position = 0) {
		snurra_timeline_track one;
		one.length = 60 * RATE;
		one.follows = follows;
		one.position = position;
		timeline.tracks.push_back(one);
		return int(timeline.tracks.size()) - 1;
	};
	auto overlapping = [](const snurra_timeline &timeline, const snurra_timeline::seating &seated) {
		for (size_t a = 0; a < timeline.tracks.size(); ++a)
			for (size_t b = a + 1; b < timeline.tracks.size(); ++b)
				if (seated.channel[a] == seated.channel[b] && timeline.start(int(a)) < timeline.end(int(b))
				    && timeline.start(int(b)) < timeline.end(int(a)))
					return true;
		return false;
	};
	/* One train of five: 0 1 0 1 0. */
	snurra_timeline one(RATE);
	int previous = -1;
	for (int i = 0; i < 5; ++i) previous = add(one, previous);
	snurra_timeline::seating seated = one.seat();
	QCOMPARE(seated.channel, (std::vector<int>{ 0, 1, 0, 1, 0 }));
	QCOMPARE(seated.channels, 2);

	/* A second train of three, on its own from 1000 s: it starts on 0.
	 * Connected after the first, whose last sits on 0, it comes in while
	 * that one still plays, so it starts on 1 and all of it flips. */
	const int second = add(one, -1, 1000 * RATE);
	add(one, second);
	add(one, second + 1);
	seated = one.seat();
	QCOMPARE(seated.channel[size_t(second)], 0);
	one.tracks[size_t(second)].follows = 4;
	seated = one.seat();
	QCOMPARE(std::vector<int>(seated.channel.begin() + second, seated.channel.end()), (std::vector<int>{ 1, 0, 1 }));
	QCOMPARE(seated.channels, 2);
	QVERIFY(!overlapping(one, seated));

	/* Two trains at once, the second 10 s behind: never more than three
	 * tracks sound together (at 65 s, the first's second and the
	 * second's first two), so three channels, none shared while sounding. */
	snurra_timeline both(RATE);
	previous = -1;
	for (int i = 0; i < 3; ++i) previous = add(both, previous);
	previous = add(both, -1, 10 * RATE);
	for (int i = 0; i < 2; ++i) previous = add(both, previous);
	seated = both.seat();
	QCOMPARE(seated.channels, 3);
	QVERIFY(!overlapping(both, seated));

	/* Where it was, kept while free -- a fourth channel taken for it rather
	 * than the lowest -- and a pin kept, its collision named. */
	both.tracks[3].was = 3;
	seated = both.seat();
	QCOMPARE(seated.channel[3], 3);
	QVERIFY(!overlapping(both, seated));
	both.tracks[1].pinned = 0;   /* on 0 while track 0 still plays there */
	seated = both.seat();
	QCOMPARE(seated.channel[1], 0);
	QCOMPARE(seated.conflicts, std::vector<int>{ 1 });
}

/*
 * A player's output as a source (project.md sec 3f, step 2), heard sample
 * by sample through the test ramp, whose left channel is the frame's own
 * index: a train of two 2 s tracks crossing over 0.5 s, the second at
 * half level and tempo 2. Before the crossfade, the first track's frames
 * as they are; in it, the two on a quarter circle; after it, every second
 * frame of the second at half. A seek lands on the very samples a read
 * through gives, and the same player inside a tile of another player
 * sounds the same.
 */
void beats_test::a_player_sounds_as_its_tiles() {
	std::vector<snurra_tile_state> player(2);
	for (int i = 0; i < 2; ++i) {
		player[size_t(i)].source = QStringLiteral("snurra-test:ramp");
		player[size_t(i)].duration_us = 2000000;
		player[size_t(i)].column = i;
	}
	player[1].overlap_us = 500000;
	player[1].level = 50;
	player[1].tempo = 2;   /* 1 s out */
	std::unique_ptr<snurra_source> source = snurra_open_timeline(player, RATE);
	const int64_t second_starts = 2 * RATE - RATE / 2;
	QCOMPARE(source->length(), second_starts + RATE);
	std::vector<float> heard(size_t(2 * source->length()));
	QCOMPARE(int64_t(source->read(heard.data(), int(source->length()))), source->length());
	QVERIFY(source->at_end());
	const double frame = 1.0 / SNURRA_RAMP_SCALE;
	for (int64_t t = 0; t < source->length(); t += 97) {
		double left = 0, right = 0;
		if (t < second_starts + RATE / 2) {   /* the first, fading out over its last 0.5 s */
			const double out = t < second_starts ? 1 : std::cos(double(t - second_starts) / (RATE / 2) * M_PI_2);
			left += out * double(t) * frame;
			right += out;
		}
		if (t >= second_starts) {   /* the second, fading in, at half and twice the speed */
			const double in = t < second_starts + RATE / 2 ? std::sin(double(t - second_starts) / (RATE / 2) * M_PI_2) : 1;
			left += 0.5 * in * double(2 * (t - second_starts)) * frame;
			right += 0.5 * in;
		}
		QVERIFY2(std::abs(heard[size_t(2 * t)] - left) < 0.5 * frame && std::abs(heard[size_t(2 * t + 1)] - right) < 1e-5,
		         qPrintable(QStringLiteral("at %1: %2 %3, meant %4 %5").arg(t).arg(heard[size_t(2 * t)] / frame)
		                            .arg(heard[size_t(2 * t + 1)]).arg(left / frame).arg(right)));
	}
	/* Sought into the crossfade: the samples a read through gave. */
	const int64_t at = second_starts + 777;
	source->seek(at);
	std::vector<float> again(2 * 2000);
	QCOMPARE(source->read(again.data(), 2000), 2000);
	for (int i = 0; i < 2 * 2000; ++i) QCOMPARE(again[size_t(i)], heard[size_t(2 * at + i)]);

	/* Nested: the player as one tile of another, as long as it is. */
	std::vector<snurra_tile_state> outer(1);
	outer[0].source = QStringLiteral("a mix");
	outer[0].duration_us = snurra_frames_to_us(source->length(), RATE);
	outer[0].player = player;
	std::unique_ptr<snurra_source> nested = snurra_open_timeline(outer, RATE);
	QCOMPARE(nested->length(), source->length());
	std::vector<float> inside(heard.size());
	QCOMPARE(int64_t(nested->read(inside.data(), int(nested->length()))), nested->length());
	for (size_t i = 0; i < heard.size(); ++i)
		QVERIFY2(std::abs(inside[i] - heard[i]) < 0.5 * frame, qPrintable(QStringLiteral("nested, at %1").arg(i / 2)));
}

/*
 * A nested track's EQ (project.md sec 3f): an 8 kHz tone in a player, its
 * high band killed, comes out as the deck's own isolator makes it of the
 * same samples, frame for frame -- and far down, the kill being one.
 */
void beats_test::a_nested_tracks_eq_is_the_decks() {
	std::vector<snurra_tile_state> player(1);
	player[0].source = QStringLiteral("tone:8000");
	player[0].duration_us = 1000000;
	player[0].eq.set_part(2, SNURRA_EQ_KILL);
	std::unique_ptr<snurra_source> source = snurra_open_timeline(player, RATE);
	std::vector<float> heard(size_t(2 * RATE));
	QCOMPARE(source->read(heard.data(), RATE), RATE);
	snurra_isolator isolator;
	isolator.make(RATE);
	const float gain[3] = { 1, 1, 0 };
	double in = 0, out = 0;
	for (int n = 0; n < RATE; ++n) {
		const float x = float(0.2 * std::sin(2 * M_PI * 8000.0 / RATE * double(n)));
		const float left = isolator.run(x, 0, gain);
		const float right = isolator.run(x, 1, gain);
		QVERIFY2(heard[size_t(2 * n)] == left && heard[size_t(2 * n + 1)] == right, qPrintable(QStringLiteral("at %1").arg(n)));
		if (n >= RATE / 10) {
			in += double(x) * x;
			out += double(left) * left;
		}
	}
	const double db = 10 * std::log10(out / in);
	QVERIFY2(db < -40, qPrintable(QStringLiteral("the killed band at %1 dB").arg(db)));
}

/*
 * A player's waveform is its whole mix (project.md sec 3f): a tone that
 * comes in at 1 s on a player 2 s long gives silent columns before it
 * and its own height, 0.2, after -- read over the timeline, on a thread.
 */
void beats_test::a_players_waveform_is_its_mix() {
	std::vector<snurra_tile_state> player(2);
	player[0].source = QStringLiteral("snurra-test:silence-for-a-second");
	player[0].duration_us = 1000000;
	player[1].source = QStringLiteral("tone:440");
	player[1].duration_us = 1000000;
	player[1].row = 1;
	player[1].lane_start_us = 1000000;
	std::vector<float> columns(16, -1);
	std::atomic<int> filled{ 0 };
	{
		snurra_peaks peaks([&player](std::function<bool()> stopped) { return snurra_open_timeline(player, RATE, std::move(stopped)); },
		                   16, [&columns, &filled](int first, const std::vector<float> &values) {
			                   for (size_t i = 0; i < values.size(); ++i) columns[size_t(first) + i] = values[i];
			                   filled += int(values.size());
		                   });
		QTRY_COMPARE_WITH_TIMEOUT(filled.load(), 16, 5000);
	}
	for (int c = 0; c < 8; ++c) QVERIFY2(columns[size_t(c)] < 1e-6f, qPrintable(QStringLiteral("column %1: %2").arg(c).arg(columns[size_t(c)])));
	for (int c = 9; c < 16; ++c)
		QVERIFY2(qAbs(columns[size_t(c)] - 0.2f) < 0.01f, qPrintable(QStringLiteral("column %1: %2").arg(c).arg(columns[size_t(c)])));
}

/*
 * A player's tracks never analysed (project.md sec 3f): two tunes fresh
 * from a folder, 120 and 128 BPM, as a player in a tile standing in the
 * second. The tile analyses them itself, keeps what it finds as a deck
 * would, and its beat becomes the second's -- so sync follows the mix.
 */
void beats_test::a_players_tracks_are_analysed_in_its_tile() {
	QTemporaryDir dir;
	std::vector<snurra_tile_state> player(2);
	const double bpm[2] = { 120, 128 };
	for (int i = 0; i < 2; ++i) {
		const made_t made = beat(bpm[i], 0.1, false, 20);
		QByteArray pcm;
		for (float v : made.mono) {
			const qint16 sample = qint16(std::clamp(v, -1.0f, 1.0f) * 30000);
			pcm.append(reinterpret_cast<const char *>(&sample), 2);
		}
		auto le32 = [](quint32 v) { return QByteArray(reinterpret_cast<const char *>(&v), 4); };
		auto le16 = [](quint16 v) { return QByteArray(reinterpret_cast<const char *>(&v), 2); };
		const QString path = dir.filePath(QStringLiteral("%1.wav").arg(i));
		QFile file(path);
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write("RIFF" + le32(quint32(36 + pcm.size())) + "WAVEfmt " + le32(16) + le16(1) + le16(1) + le32(RATE)
		           + le32(RATE * 2) + le16(2) + le16(16) + "data" + le32(quint32(pcm.size())) + pcm);
		file.close();
		snurra_beat_grid none;
		QVERIFY(!snurra_cached_beats(path, -1, RATE, none));
		player[size_t(i)].source = path;
		player[size_t(i)].duration_us = 20000000;
		player[size_t(i)].column = i;
	}
	const snurra_timeline timeline = snurra_timeline_from(player, RATE);
	snurra_node node;
	snurra_tile_state state;
	state.source = QStringLiteral("2 tracks");
	state.player = player;
	state.duration_us = snurra_frames_to_us(timeline.end(1), RATE);
	state.position_us = snurra_frames_to_us(timeline.start(1) + 5 * RATE, RATE);
	state.sampled_at_us = snurra_clock_us();
	snurra_tile *tile = node.add_tile(state);
	const double period = 60.0 * RATE / 128;
	QTRY_VERIFY_WITH_TIMEOUT(tile->beats().valid() && qAbs(tile->beats().period - period) < 1, 30000);
	snurra_beat_grid kept;
	QVERIFY(snurra_cached_beats(player[1].source, -1, RATE, kept));
	QVERIFY(qAbs(kept.period - period) < 1);
}

void beats_test::a_beat_is_found_to_the_frame_data() {
	QTest::addColumn<double>("bpm");
	QTest::addColumn<double>("first");
	QTest::addColumn<bool>("pattern");
	QTest::newRow("kick, 120") << 120.0 << 0.0 << false;
	QTest::newRow("pattern, 128") << 128.0 << 0.137 << true;
	QTest::newRow("pattern, 87.5") << 87.5 << 0.5 << true;
	QTest::newRow("pattern, 140") << 140.0 << 0.31 << true;
	QTest::newRow("pattern, 127.3") << 127.3 << 0.05 << true;
}

void beats_test::a_beat_is_found_to_the_frame() {
	QFETCH(double, bpm);
	QFETCH(double, first);
	QFETCH(bool, pattern);
	made_t made = beat(bpm, first, pattern);
	memory_source source(made.mono);
	const snurra_beat_grid grid = snurra_find_beats(source, RATE);
	QVERIFY2(grid.valid(), qPrintable(QStringLiteral("no beat found (confidence %1)").arg(grid.confidence)));
	/* Every true beat against the nearest grid line: over a minute, a
	 * tempo off by 0.01 BPM at 128 drifts by 5 ms. */
	double worst = 0;
	double mean = 0;
	for (double at : made.beats) {
		const double k = std::round((at - grid.first) / grid.period);
		const double error = (grid.first + k * grid.period - at) / RATE * 1000;
		worst = std::max(worst, std::abs(error));
		mean += error;
	}
	mean /= double(made.beats.size());
	qInfo("%.2f BPM found as %.4f (confidence %.1f); every beat within %.2f ms, %.2f ms late on average",
	      bpm, grid.bpm(), grid.confidence, worst, mean);
	QVERIFY2(std::abs(grid.bpm() - bpm) < 0.02, qPrintable(QStringLiteral("%1 BPM, not %2").arg(grid.bpm()).arg(bpm)));
	QVERIFY2(worst < 10, qPrintable(QStringLiteral("a beat %1 ms off the grid").arg(worst)));
}

void beats_test::what_has_no_beat_is_given_none_data() {
	QTest::addColumn<int>("kind");
	QTest::newRow("white noise") << 0;
	QTest::newRow("a steady tone") << 1;
	QTest::newRow("hits at random times") << 2;
}

void beats_test::what_has_no_beat_is_given_none() {
	QFETCH(int, kind);
	std::vector<float> mono(size_t(60) * RATE, 0.0f);
	std::mt19937 noise(11);
	std::uniform_real_distribution<float> white(-0.3f, 0.3f);
	if (kind == 0)
		for (float &v : mono) v = white(noise);
	if (kind == 1)
		for (size_t i = 0; i < mono.size(); ++i) mono[i] = float(0.3 * std::sin(2 * M_PI * 440 * double(i) / RATE));
	if (kind == 2) {
		std::exponential_distribution<double> gap(2.0);   /* two a second, at random */
		for (double at = 0; at < double(mono.size()); at += gap(noise) * RATE) add_hit(mono, at, KICK, noise);
	}
	memory_source source(mono);
	const snurra_beat_grid grid = snurra_find_beats(source, RATE);
	qInfo("confidence %.2f", grid.confidence);
	QVERIFY2(!grid.valid(), qPrintable(QStringLiteral("a beat at %1 BPM was found").arg(grid.bpm())));
}

/*
 * A steady tone through a lossy codec: the codec's frames (AAC's 1024
 * samples, 21 ms) leave a faint ripple at their own period in the onsets,
 * which a fold can find. Seen in the player on a test video's 440 Hz
 * tone, shown as 4 bars and a waveform full of beats. No beat.
 */
void beats_test::an_encoded_steady_tone_has_no_beat_data() {
	QTest::addColumn<QString>("codec");
	QTest::newRow("aac") << QStringLiteral("aac");
	QTest::newRow("mp3") << QStringLiteral("libmp3lame");
	QTest::newRow("opus") << QStringLiteral("libopus");
}

void beats_test::an_encoded_steady_tone_has_no_beat() {
	QFETCH(QString, codec);
	const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
	if (ffmpeg.isEmpty()) QSKIP("no ffmpeg");
	QTemporaryDir dir;
	const QString path = dir.filePath(QStringLiteral("tone.mka"));
	QProcess make;
	make.start(ffmpeg, { QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-y"), QStringLiteral("-f"),
	                     QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("sine=f=440:d=30"),
	                     QStringLiteral("-c:a"), codec, path });
	QVERIFY(make.waitForFinished(60000) && make.exitCode() == 0);
	std::unique_ptr<snurra_source> source = snurra_open_source(path, RATE, 0);
	const snurra_beat_grid grid = snurra_find_beats(*source, RATE);
	qInfo("%s: confidence %.2f", qPrintable(codec), grid.confidence);
	QVERIFY2(!grid.valid(), qPrintable(QStringLiteral("a beat at %1 BPM was found").arg(grid.bpm())));
}

/* And the other way: a made pattern at 128 BPM through AAC is still
 * found, at its tempo, every beat on the grid. */
void beats_test::an_encoded_beat_is_still_found() {
	const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
	if (ffmpeg.isEmpty()) QSKIP("no ffmpeg");
	made_t made = beat(128, 0.2, true, 30);
	QTemporaryDir dir;
	const QString raw = dir.filePath(QStringLiteral("beat.f32"));
	QFile file(raw);
	QVERIFY(file.open(QIODevice::WriteOnly));
	file.write(reinterpret_cast<const char *>(made.mono.data()), qint64(made.mono.size() * sizeof(float)));
	file.close();
	const QString path = dir.filePath(QStringLiteral("beat.m4a"));
	QProcess make;
	make.start(ffmpeg, { QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-y"), QStringLiteral("-f"),
	                     QStringLiteral("f32le"), QStringLiteral("-ar"), QStringLiteral("48000"), QStringLiteral("-ac"),
	                     QStringLiteral("1"), QStringLiteral("-i"), raw, QStringLiteral("-c:a"), QStringLiteral("aac"), path });
	QVERIFY(make.waitForFinished(60000) && make.exitCode() == 0);
	std::unique_ptr<snurra_source> source = snurra_open_source(path, RATE, 0);
	const snurra_beat_grid grid = snurra_find_beats(*source, RATE);
	QVERIFY2(grid.valid(), qPrintable(QStringLiteral("no beat (confidence %1)").arg(grid.confidence)));
	double worst = 0;
	for (double at : made.beats) {
		const double k = std::round((at - grid.first) / grid.period);
		worst = std::max(worst, std::abs(grid.first + k * grid.period - at) / RATE * 1000);
	}
	qInfo("through AAC: %.4f BPM, every beat within %.2f ms", grid.bpm(), worst);
	QVERIFY2(std::abs(grid.bpm() - 128) < 0.05, qPrintable(QStringLiteral("%1 BPM").arg(grid.bpm())));
	QVERIFY2(worst < 10, qPrintable(QStringLiteral("a beat %1 ms off").arg(worst)));
}

int run_beats_tests(int argc, char **argv) {
	beats_test test;
	return QTest::qExec(&test, argc, argv);
}


/*
 * A DJ set (the holder, 2026-11-13: no beat found in a 42 minute set): ten
 * minutes at 128 BPM whose beat moves 0.37 of a beat at each minute, as a
 * set's phase moves at each track's join. The whole does not fold clear;
 * its minutes do, and agree. Its grid is 128 BPM, on the first minute's
 * beats.
 */
void beats_test::a_set_whose_phase_moves_has_its_tempo() {
	std::vector<float> set;
	std::vector<double> first_minute;
	const double period = 60.0 * RATE / 128;
	for (int minute = 0; minute < 10; ++minute) {
		const double shift = std::fmod(0.37 * minute, 1.0) * period / RATE;
		made_t part = beat(128, shift, true, 60);
		if (minute == 0) first_minute = part.beats;
		set.insert(set.end(), part.mono.begin(), part.mono.end());
	}
	memory_source source(set);
	const snurra_beat_grid grid = snurra_find_beats(source, RATE);
	QVERIFY2(grid.valid(), qPrintable(QStringLiteral("no beat found (confidence %1)").arg(grid.confidence)));
	QVERIFY2(std::abs(grid.bpm() - 128) < 0.3, qPrintable(QStringLiteral("%1 BPM").arg(grid.bpm())));
	double worst = 0;
	for (double at : first_minute) {
		if (at > 30.0 * RATE) break;
		const double k = std::round((at - grid.first) / grid.period);
		worst = std::max(worst, std::abs(grid.first + k * grid.period - at));
	}
	QVERIFY2(worst < 0.02 * RATE, qPrintable(QStringLiteral("the first minute's beats are %1 ms off the grid").arg(worst * 1000 / RATE)));
}

#include "beats_test.moc"

/*
 * The key (beats.h): chords made here in a known key -- each note with
 * its harmonics, the root again an octave down as a bass -- two seconds
 * each, round and round for a minute; a major key's I IV V I, a minor
 * key's i iv v i (the natural minor, so the very notes of its relative
 * major, told apart only by where they lean). A drum loop and noise have
 * none.
 */
namespace {

std::vector<float> chords(const std::vector<std::vector<int>> &progression, bool drums) {
	std::vector<float> mono = drums ? beat(124, 0.1, false).mono : std::vector<float>(size_t(60) * RATE, 0.0f);
	const size_t each = 2 * RATE;
	for (size_t i = 0; i < mono.size(); ++i) {
		const std::vector<int> &chord = progression[(i / each) % progression.size()];
		const double t = double(i) / RATE;
		double v = 0;
		for (size_t n = 0; n < chord.size(); ++n) {
			const double hz = 440 * std::pow(2.0, (chord[n] - 69) / 12.0);
			for (int h = 1; h <= 4; ++h) v += 0.05 / h * std::sin(2 * M_PI * hz * h * t);
		}
		const double bass = 440 * std::pow(2.0, (chord[0] - 12 - 69) / 12.0);
		v += 0.08 * std::sin(2 * M_PI * bass * t);
		mono[i] += float(v);
	}
	return mono;
}

}   // namespace

void beats_test::a_key_is_found_data() {
	QTest::addColumn<int>("kind");
	QTest::addColumn<int>("key");
	/* MIDI notes: 60 is middle C. */
	QTest::newRow("C major") << 0 << 0;
	QTest::newRow("A minor") << 1 << 21;
	QTest::newRow("Gb major, with drums") << 2 << 6;
	QTest::newRow("Eb minor") << 3 << 15;
	QTest::newRow("white noise") << 4 << -1;
	QTest::newRow("drums alone") << 5 << -1;
}

void beats_test::a_key_is_found() {
	QFETCH(int, kind);
	QFETCH(int, key);
	std::vector<float> mono;
	if (kind == 0) mono = chords({ { 60, 64, 67 }, { 65, 69, 72 }, { 67, 71, 74 }, { 60, 64, 67 } }, false);
	if (kind == 1) mono = chords({ { 57, 60, 64 }, { 62, 65, 69 }, { 64, 67, 71 }, { 57, 60, 64 } }, false);
	if (kind == 2) mono = chords({ { 66, 70, 73 }, { 71, 75, 78 }, { 73, 77, 80 }, { 66, 70, 73 } }, true);
	if (kind == 3) mono = chords({ { 63, 66, 70 }, { 68, 71, 75 }, { 70, 73, 77 }, { 63, 66, 70 } }, false);
	if (kind == 4) {
		mono.assign(size_t(60) * RATE, 0.0f);
		std::mt19937 noise(5);
		std::uniform_real_distribution<float> white(-0.3f, 0.3f);
		for (float &v : mono) v = white(noise);
	}
	if (kind == 5) mono = beat(124, 0.1, false).mono;
	memory_source source(mono);
	const snurra_beat_grid grid = snurra_find_beats(source, RATE);
	qInfo("found %d (%s), fit %.3f", grid.key, qPrintable(snurra_key_name(grid.key)), grid.key_fit);
	QCOMPARE(grid.key, key);
}

using loudness_parts = QList<QPair<double, double>>;   /* dBFS, seconds */

/* EBU Tech 3341's test signals: a 1 kHz sine in both channels, a level in
 * dBFS for so many seconds, one after another; the integrated loudness
 * must come out within 0.1 LU of what the document says. */
void beats_test::loudness_is_measured_as_ebu_r128_says_data() {
	QTest::addColumn<loudness_parts>("parts");
	QTest::addColumn<double>("lufs");
	QTest::addColumn<int>("rate");
	using parts = loudness_parts;
	QTest::newRow("case 1: -23 dBFS") << parts{ { -23, 20 } } << -23.0 << 48000;
	QTest::newRow("case 2: -33 dBFS") << parts{ { -33, 20 } } << -33.0 << 48000;
	/* The relative gate: the quiet ends are 13 LU down and gated out. */
	QTest::newRow("case 3: -36, -23, -36") << parts{ { -36, 10 }, { -23, 60 }, { -36, 10 } } << -23.0 << 48000;
	/* The absolute gate: -72 is below -70 and ignored. */
	QTest::newRow("case 4: -72, -36, -23, -36, -72")
	        << parts{ { -72, 10 }, { -36, 10 }, { -23, 60 }, { -36, 10 }, { -72, 10 } } << -23.0 << 48000;
	/* And the filter designed at another rate. */
	QTest::newRow("case 1 at 44.1 kHz") << parts{ { -23, 20 } } << -23.0 << 44100;
}

void beats_test::loudness_is_measured_as_ebu_r128_says() {
	QFETCH(loudness_parts, parts);
	QFETCH(double, lufs);
	QFETCH(int, rate);
	std::vector<float> mono;
	double highest = 0;
	for (const auto &part : parts) {
		const double amplitude = std::pow(10.0, part.first / 20);
		highest = std::max(highest, amplitude);
		const size_t from = mono.size();
		for (size_t i = 0; i < size_t(part.second * rate); ++i)
			mono.push_back(float(amplitude * std::sin(2 * M_PI * 1000 * double(from + i) / rate)));
	}
	memory_source source(mono);
	const snurra_beat_grid grid = snurra_find_beats(source, rate);
	QVERIFY(grid.loudness_known);
	QVERIFY2(std::abs(grid.loudness - lufs) <= 0.1, qPrintable(QStringLiteral("%1 LUFS, not %2").arg(grid.loudness).arg(lufs)));
	QVERIFY(std::abs(grid.peak - highest) < 0.001);

	/* Silence has none. */
	memory_source quiet(std::vector<float>(size_t(rate) * 5, 0.0f));
	QVERIFY(!snurra_find_beats(quiet, rate).loudness_known);
}

void beats_test::a_loud_tune_is_cut_and_a_quiet_one_boosted_to_its_peak() {
	snurra_beat_grid loud;
	loud.loudness_known = true;
	loud.loudness = -8;
	loud.peak = 1;
	QVERIFY(std::abs(20 * std::log10(snurra_loudness_gain(loud, -14)) + 6) < 0.01);
	/* -20 LUFS with its peak at -4 dBFS: 6 dB wanted, 4 dB of room. */
	snurra_beat_grid quiet;
	quiet.loudness_known = true;
	quiet.loudness = -20;
	quiet.peak = std::pow(10.0, -4.0 / 20);
	QVERIFY(std::abs(20 * std::log10(snurra_loudness_gain(quiet, -14)) - 4) < 0.01);
	/* No more than 12 dB, however much room. */
	quiet.loudness = -40;
	quiet.peak = 0.01;
	QVERIFY(std::abs(20 * std::log10(snurra_loudness_gain(quiet, -14)) - 12) < 0.01);
	QCOMPARE(snurra_loudness_gain(snurra_beat_grid{}, -14), 1.0f);
}

void beats_test::keys_are_named_on_the_camelot_wheel() {
	QCOMPARE(snurra_key_name(0), QStringLiteral("8B C"));
	QCOMPARE(snurra_key_name(21), QStringLiteral("8A Am"));
	QCOMPARE(snurra_key_name(7), QStringLiteral("9B G"));
	QCOMPARE(snurra_key_name(5), QStringLiteral("7B F"));
	QCOMPARE(snurra_key_name(6), QStringLiteral("2B Gb"));
	QCOMPARE(snurra_key_name(15), QStringLiteral("2A Ebm"));
	QCOMPARE(snurra_key_name(-1), QString());
	/* Two semitones up, B minor; one down from C, B major. */
	QCOMPARE(snurra_key_name(21, 2), QStringLiteral("10A Bm"));
	QCOMPARE(snurra_key_name(0, -1), QStringLiteral("1B B"));
	QVERIFY(snurra_key_order(21) < snurra_key_order(0));   /* 8A, then 8B */
	QVERIFY(snurra_key_order(0) < snurra_key_order(7));    /* 8B, then 9B */
	QVERIFY(snurra_key_order(-1) > snurra_key_order(4));   /* none, last */
}

/*
 * A beat under a dotted rhythm (the holder's tracks against Mixxx,
 * 2026-11-13: 132 found as 88, 138 as 92): a kick on every beat at 132
 * and an open hat over a soft chord every beat and a half, which the
 * autocorrelation alone took for the beat (88.00, before the dotted
 * check). The beat is 132, not the hats' 88.
 */
void beats_test::a_dotted_rhythm_is_not_taken_for_the_beat() {
	std::vector<float> mono(size_t(60) * RATE, 0.0f);
	std::mt19937 noise(3);
	std::uniform_real_distribution<float> white(-1, 1);
	const double period = 60.0 * RATE / 132;
	for (int k = 0;; ++k) {
		const double at = 0.05 * RATE + k * period;
		if (at >= double(mono.size())) break;
		add_hit(mono, at, KICK, noise);
	}
	for (int k = 0;; ++k) {
		const size_t at = size_t(std::lround(0.05 * RATE + k * 1.5 * period));
		if (at >= mono.size()) break;
		float last = 0;
		for (size_t i = 0; i < size_t(RATE / 8) && at + i < mono.size(); ++i) {
			const double t = double(i) / RATE;
			const float n = white(noise);
			const double chord = std::sin(2 * M_PI * 440 * t) + std::sin(2 * M_PI * 554 * t) + std::sin(2 * M_PI * 659 * t);
			mono[at + i] += float((0.7 * (n - last) + 0.1 * chord) * std::exp(-t / 0.05));
			last = n;
		}
	}
	memory_source source(mono);
	const snurra_beat_grid grid = snurra_find_beats(source, RATE);
	qInfo("a kick at 132 under hats at 88: %.2f BPM (confidence %.1f)", grid.bpm(), grid.confidence);
	QVERIFY2(std::abs(grid.bpm() - 132) < 0.1, qPrintable(QStringLiteral("%1 BPM").arg(grid.bpm())));
}

/*
 * Fingerprints (fingerprint.h, the holder 2026-11-14): a tune made here --
 * notes of a seeded melody over a bass, half a second each -- written as a
 * WAV and as an MP3 of it, and a second tune from another seed. snurra's
 * fingerprint of the WAV is the ffmpeg program's own (its chromaprint
 * muxer, read separately), near; the MP3's is near; the other tune's is
 * as far as chance.
 */
namespace {

QByteArray melody_wav(unsigned seed, int seconds) {
	std::mt19937 notes(seed);
	std::uniform_int_distribution<int> pick(0, 11);
	QByteArray pcm;
	double phase[3] = { 0, 0, 0 };
	int note = 0;
	for (int i = 0; i < seconds * RATE; ++i) {
		if (i % (RATE / 2) == 0) note = pick(notes);
		const double hz[3] = { 220 * std::pow(2.0, note / 12.0), 440 * std::pow(2.0, (note + 4) / 12.0), 110 * std::pow(2.0, (note % 5) / 12.0) };
		double v = 0;
		for (int k = 0; k < 3; ++k) {
			phase[k] += 2 * M_PI * hz[k] / RATE;
			v += 0.2 * std::sin(phase[k]);
		}
		const qint16 sample = qint16(12000 * v);
		pcm.append(reinterpret_cast<const char *>(&sample), 2);
	}
	auto le32 = [](quint32 v) { return QByteArray(reinterpret_cast<const char *>(&v), 4); };
	auto le16 = [](quint16 v) { return QByteArray(reinterpret_cast<const char *>(&v), 2); };
	return "RIFF" + le32(quint32(36 + pcm.size())) + "WAVEfmt " + le32(16) + le16(1) + le16(1) + le32(RATE) + le32(RATE * 2)
	       + le16(2) + le16(16) + "data" + le32(quint32(pcm.size())) + pcm;
}

}   // namespace

void beats_test::a_fingerprint_knows_a_tune_however_it_is_encoded() {
	QVERIFY2(snurra_fingerprints_available(), "this FFmpeg has no chromaprint muxer");
	const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
	if (ffmpeg.isEmpty()) QSKIP("no ffmpeg program");
	QTemporaryDir dir;
	auto write = [&dir](const QString &name, const QByteArray &bytes) {
		QFile file(dir.filePath(name));
		file.open(QIODevice::WriteOnly);
		file.write(bytes);
		return file.fileName();
	};
	const QString tune = write(QStringLiteral("tune.wav"), melody_wav(1, 40));
	const QString other = write(QStringLiteral("other.wav"), melody_wav(2, 40));
	const QString mp3 = dir.filePath(QStringLiteral("tune.mp3"));
	QProcess encode;
	encode.start(ffmpeg, { QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-y"), QStringLiteral("-i"), tune,
	                       QStringLiteral("-b:a"), QStringLiteral("128k"), mp3 });
	QVERIFY(encode.waitForFinished(60000) && encode.exitCode() == 0);
	/* The ffmpeg program's own, read apart from snurra. */
	QProcess theirs;
	theirs.start(ffmpeg, { QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-i"), tune, QStringLiteral("-f"),
	                       QStringLiteral("chromaprint"), QStringLiteral("-fp_format"), QStringLiteral("raw"), QStringLiteral("-") });
	QVERIFY(theirs.waitForFinished(60000) && theirs.exitCode() == 0);
	const QByteArray their_bytes = theirs.readAllStandardOutput();
	std::vector<uint32_t> their_raw(size_t(their_bytes.size()) / 4);
	memcpy(their_raw.data(), their_bytes.constData(), their_raw.size() * 4);

	const snurra_fingerprint a = snurra_fingerprint_of(tune);
	const snurra_fingerprint b = snurra_fingerprint_of(mp3);
	const snurra_fingerprint c = snurra_fingerprint_of(other);
	QVERIFY(a.valid() && b.valid() && c.valid());
	const double program = snurra_fingerprint_distance(a.raw, their_raw);
	const double encoded = snurra_fingerprint_distance(a.raw, b.raw);
	const double unlike = snurra_fingerprint_distance(a.raw, c.raw);
	qInfo("fingerprint: %zu values over %.1f s, \"%s...\"; from the ffmpeg program %.3f, as MP3 %.3f, another tune %.3f",
	      a.raw.size(), a.seconds, a.base64.left(12).constData(), program, encoded, unlike);
	QVERIFY(a.base64.startsWith("AQ"));
	QVERIFY(std::abs(a.seconds - 40) < 0.1);
	QVERIFY2(program < 0.05, qPrintable(QString::number(program)));
	QVERIFY2(encoded < 0.2, qPrintable(QString::number(encoded)));
	QVERIFY2(unlike > 0.35, qPrintable(QString::number(unlike)));
}

/*
 * AcoustID asked (fingerprint.h), of a server of the test's own standing in
 * for api.acoustid.org: it keeps the request and answers as told. With no
 * key nothing is sent and the reason given; with one, the key, the tune's
 * whole length and the fingerprint go, and the best-scoring recording of
 * the answer is taken, its artists joined; AcoustID's own refusal comes
 * back in its words; an answer naming no recording says so.
 */
namespace {

class acoustid_stand_in : public QTcpServer {
public:
	QByteArray answer;
	QByteArray asked;   /* the last request's body */
	int requests = 0;
	acoustid_stand_in() {
		connect(this, &QTcpServer::newConnection, this, [this] {
			while (QTcpSocket *socket = nextPendingConnection()) {
				connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
					m_in[socket] += socket->readAll();
					const QByteArray &in = m_in[socket];
					const qsizetype head = in.indexOf("\r\n\r\n");
					if (head < 0) return;
					qsizetype length = 0;
					for (const QByteArray &line : in.left(head).split('\n'))
						if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toLongLong();
					if (in.size() < head + 4 + length) return;
					asked = in.mid(head + 4, length);
					++requests;
					socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
					              + QByteArray::number(answer.size()) + "\r\nConnection: close\r\n\r\n" + answer);
					socket->disconnectFromHost();
				});
			}
		});
	}

private:
	QHash<QTcpSocket *, QByteArray> m_in;
};

}   // namespace

void beats_test::acoustid_is_asked_and_its_answer_read() {
	acoustid_stand_in service;
	QVERIFY(service.listen(QHostAddress::LocalHost));
	qputenv("SNURRA_ACOUSTID_URL", QStringLiteral("http://127.0.0.1:%1/v2/lookup").arg(service.serverPort()).toLatin1());
	snurra_fingerprint print;
	print.raw = { 1, 2, 3 };
	print.base64 = "AQAA_test-print";
	print.seconds = 40;
	QNetworkAccessManager network;
	snurra_identity got;
	QString error;
	bool told = false;
	auto ask = [&] {
		told = false;
		snurra_identify(&network, print, 214, [&](const snurra_identity &identity, const QString &why) {
			got = identity;
			error = why;
			told = true;
		});
		QTRY_VERIFY_WITH_TIMEOUT(told, 10000);
	};

	qunsetenv("SNURRA_ACOUSTID_KEY");
	QSettings(QStringLiteral("snurra"), QStringLiteral("snurra")).remove(QStringLiteral("acoustid/key"));
	ask();
	QVERIFY2(error.contains(QLatin1String("no AcoustID key")), qPrintable(error));
	QCOMPARE(service.requests, 0);

	qputenv("SNURRA_ACOUSTID_KEY", "test-key");
	service.answer = R"({"status":"ok","results":[
		{"id":"a","score":0.51,"recordings":[{"id":"r1","title":"Wrong One","artists":[{"name":"Nobody"}]}]},
		{"id":"b","score":0.93,"recordings":[{"id":"r2","title":"Night Drive","artists":[{"name":"Somebody"},{"name":"Someone Else"}]}]},
		{"id":"c","score":0.99}]})";
	ask();
	QVERIFY2(error.isEmpty(), qPrintable(error));
	QCOMPARE(got.title, QStringLiteral("Night Drive"));
	QCOMPARE(got.artist, QStringLiteral("Somebody, Someone Else"));
	QCOMPARE(got.recording, QStringLiteral("r2"));
	QCOMPARE(got.score, 0.93);
	const QUrlQuery sent(QString::fromUtf8(service.asked));
	QCOMPARE(sent.queryItemValue(QStringLiteral("client")), QStringLiteral("test-key"));
	QCOMPARE(sent.queryItemValue(QStringLiteral("duration")), QStringLiteral("214"));
	QCOMPARE(sent.queryItemValue(QStringLiteral("fingerprint"), QUrl::FullyDecoded), QStringLiteral("AQAA_test-print"));

	service.answer = R"({"status":"error","error":{"code":4,"message":"invalid API key"}})";
	ask();
	QCOMPARE(error, QStringLiteral("AcoustID: invalid API key"));
	QVERIFY(!got.found());

	service.answer = R"({"status":"ok","results":[]})";
	ask();
	QVERIFY(error.contains(QLatin1String("no recording")));
	qunsetenv("SNURRA_ACOUSTID_KEY");
	qunsetenv("SNURRA_ACOUSTID_URL");
}

/*
 * Identify in the library: a made tune named as yt-dlp names a download,
 * "...-FYLJdKxTxJ0.wav", chosen and identified through the stand-in for
 * AcoustID -- shown as "Night Drive [FYLJdKxTxJ0]" by its artists, the
 * file not renamed, AcoustID asked with the tune's whole 40 s; and a
 * library opened again shows it at once from what was kept, asking
 * nothing.
 */
void beats_test::the_library_identifies_a_tune_and_keeps_its_id() {
	QCOMPARE(snurra_shown_title(QStringLiteral("/m/Song-FYLJdKxTxJ0.opus"), QStringLiteral("Night Drive")), QStringLiteral("Night Drive [FYLJdKxTxJ0]"));
	QCOMPARE(snurra_shown_title(QStringLiteral("/m/Song [FYLJdKxTxJ0].mp3"), QStringLiteral("Night Drive")), QStringLiteral("Night Drive [FYLJdKxTxJ0]"));
	QCOMPARE(snurra_shown_title(QStringLiteral("/m/Song.mp3"), QStringLiteral("Night Drive")), QStringLiteral("Night Drive"));
	acoustid_stand_in service;
	QVERIFY(service.listen(QHostAddress::LocalHost));
	qputenv("SNURRA_ACOUSTID_URL", QStringLiteral("http://127.0.0.1:%1/v2/lookup").arg(service.serverPort()).toLatin1());
	qputenv("SNURRA_ACOUSTID_KEY", "test-key");
	service.answer = R"({"status":"ok","results":[{"id":"b","score":0.93,"recordings":[{"id":"r2","title":"Night Drive",
		"artists":[{"name":"Somebody"},{"name":"Someone Else"}]}]}]})";
	QTemporaryDir dir;
	const QString path = dir.filePath(QStringLiteral("night thing-FYLJdKxTxJ0.wav"));
	{
		QFile file(path);
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write(melody_wav(3, 40));
	}
	{
		snurra_library library(dir.path());
		QTRY_VERIFY_WITH_TIMEOUT(!library.scanning(), 5000);
		QTreeView *view = library.findChild<QTreeView *>();
		QAbstractItemModel *model = view->model();
		QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 1, 5000);
		view->selectionModel()->select(model->index(0, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
		QCOMPARE(library.chosen(), QStringList{ path });
		library.findChild<QPushButton *>(QStringLiteral("snurra-identify"))->click();
		QVERIFY(library.identifying());
		QTRY_VERIFY_WITH_TIMEOUT(!library.identifying(), 30000);
		QCOMPARE(model->index(0, 0).data().toString(), QStringLiteral("Night Drive [FYLJdKxTxJ0]"));
		QCOMPARE(model->index(0, 1).data().toString(), QStringLiteral("Somebody, Someone Else"));
		QCOMPARE(QUrlQuery(QString::fromUtf8(service.asked)).queryItemValue(QStringLiteral("duration")), QStringLiteral("40"));
		QVERIFY(QFileInfo::exists(path));
	}
	const int asked = service.requests;
	snurra_library again(dir.path());
	QTRY_COMPARE_WITH_TIMEOUT(again.findChild<QTreeView *>()->model()->index(0, 0).data().toString(),
	                          QStringLiteral("Night Drive [FYLJdKxTxJ0]"), 5000);
	QCOMPARE(service.requests, asked);
	qunsetenv("SNURRA_ACOUSTID_KEY");
	qunsetenv("SNURRA_ACOUSTID_URL");
}
