#include "snurra/mixer.h"

#include "snurra/deck_waves.h"
#include "snurra/eq.h"
#include "snurra/tile.h"

#include <QDial>
#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace {

/* An EQ knob as a DJ mixer's: 0 dB at twelve o'clock, the left half
 * down to -24 dB and the kill at its end, the right half up to +6. */
int step_of(float db) {
	if (db <= SNURRA_EQ_KILL / 2) return snurra_mixer::KILL_STEP;
	if (db >= 0) return int(std::lround(std::min(db, 6.0f) / 6 * 100));
	return std::max(snurra_mixer::KILL_STEP + 1, int(std::lround(std::max(db, -24.0f) / 24 * 99)));
}

float db_of(int step) {
	if (step <= snurra_mixer::KILL_STEP) return SNURRA_EQ_KILL;
	return step >= 0 ? float(step) / 100 * 6 : float(step) / 99 * 24;
}

}

snurra_mixer::snurra_mixer(QWidget *parent) : QWidget(parent) {
	setObjectName(QStringLiteral("snurra-mixer"));
	setAccessibleName(tr("Mixer"));
	m_row = new QHBoxLayout(this);
	m_row->setContentsMargins(4, 0, 4, 0);
	m_row->setSpacing(2);
	setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
	QTimer *tick = new QTimer(this);
	tick->setInterval(100);
	connect(tick, &QTimer::timeout, this, [this] {
		if (isVisible()) refresh();
	});
	tick->start();
	if (parent) parent->installEventFilter(this);
}

void snurra_mixer::set_deck_count(int count) {
	while (int(m_strips.size()) > count) {
		delete m_strips.back().box;
		m_strips.pop_back();
	}
	while (int(m_strips.size()) < count) {
		const int deck = int(m_strips.size());
		const QString letter(QChar(QLatin1Char(char('A' + deck))));
		strip one;
		one.box = new QWidget(this);
		one.box->setObjectName(QStringLiteral("snurra-strip-%1").arg(letter.toLower()));
		QVBoxLayout *column = new QVBoxLayout(one.box);
		column->setContentsMargins(0, 0, 0, 0);
		column->setSpacing(0);
		QLabel *name = new QLabel(letter, one.box);
		QFont bold = name->font();
		bold.setBold(true);
		name->setFont(bold);
		name->setAlignment(Qt::AlignHCenter);
		name->setStyleSheet(QStringLiteral("color: %1;").arg(snurra_deck_waves::colour_of(deck).name()));
		column->addWidget(name);
		const int size = fontMetrics().height() * 2;
		auto dial = [&](const QString &label, const QString &accessible, int from, int to) {
			QLabel *text = new QLabel(label, one.box);
			QFont small = text->font();
			small.setPointSizeF(small.pointSizeF() * 0.8);
			text->setFont(small);
			text->setAlignment(Qt::AlignHCenter);
			column->addWidget(text);
			QDial *knob = new QDial(one.box);
			knob->setRange(from, to);
			knob->setFixedSize(size, size);
			knob->setNotchesVisible(true);
			knob->setAccessibleName(tr("Deck %1 %2").arg(letter, accessible));
			column->addWidget(knob, 0, Qt::AlignHCenter);
			return knob;
		};
		one.gain = dial(tr("GAIN"), tr("gain"), -12, 12);
		one.high = dial(tr("HI"), tr("high"), KILL_STEP, 100);
		one.mid = dial(tr("MID"), tr("mid"), KILL_STEP, 100);
		one.low = dial(tr("LOW"), tr("low"), KILL_STEP, 100);
		one.fader = new QSlider(Qt::Vertical, one.box);
		one.fader->setRange(0, 100);
		one.fader->setAccessibleName(tr("Deck %1 fader").arg(letter));
		one.fader->setMinimumHeight(size * 2);
		column->addWidget(one.fader, 1, Qt::AlignHCenter);
		one.cue = new QToolButton(one.box);
		one.cue->setText(tr("CUE"));
		one.cue->setCheckable(true);
		one.cue->setAccessibleName(tr("Deck %1 cue: the headphones here").arg(letter));
		column->addWidget(one.cue, 0, Qt::AlignHCenter);
		/* By hand: to the tile, whose own state is what is read back. */
		auto eq = [this, deck] {
			snurra_tile *tile = tile_of ? tile_of(deck) : nullptr;
			if (!tile) return;
			const strip &s = m_strips[size_t(deck)];
			tile->set_eq(db_of(s.low->value()), db_of(s.mid->value()), db_of(s.high->value()));
		};
		for (QDial *knob : { one.high, one.mid, one.low }) connect(knob, &QDial::valueChanged, this, eq);
		connect(one.gain, &QDial::valueChanged, this, [this, deck](int db) {
			if (snurra_tile *tile = tile_of ? tile_of(deck) : nullptr) tile->set_gain_db(float(db));
		});
		connect(one.fader, &QSlider::valueChanged, this, [this, deck](int level) {
			if (snurra_tile *tile = tile_of ? tile_of(deck) : nullptr) tile->set_level(level);
		});
		connect(one.cue, &QToolButton::clicked, this, [this, deck](bool on) {
			if (on_cue) on_cue(deck, on);
		});
		m_row->addWidget(one.box);
		m_strips.push_back(one);
	}
	refresh();
}

void snurra_mixer::set_order(const std::vector<int> &decks) {
	int at = 0;
	for (int deck : decks)
		if (deck >= 0 && deck < int(m_strips.size())) m_row->insertWidget(at++, m_strips[size_t(deck)].box);
}

int snurra_mixer::place_of(int deck) const {
	return deck >= 0 && deck < int(m_strips.size()) ? m_row->indexOf(m_strips[size_t(deck)].box) : -1;
}

void snurra_mixer::refresh() {
	for (size_t deck = 0; deck < m_strips.size(); ++deck) {
		const strip &one = m_strips[deck];
		snurra_tile *tile = tile_of ? tile_of(int(deck)) : nullptr;
		for (QWidget *control : { static_cast<QWidget *>(one.gain), static_cast<QWidget *>(one.high), static_cast<QWidget *>(one.mid),
		                          static_cast<QWidget *>(one.low), static_cast<QWidget *>(one.fader) })
			control->setEnabled(tile != nullptr);
		one.cue->setEnabled(tile != nullptr && has_headphones && has_headphones());
		auto show = [](QAbstractSlider *control, int value) {
			if (control->isSliderDown() || control->value() == value) return;
			control->blockSignals(true);
			control->setValue(value);
			control->blockSignals(false);
		};
		one.cue->blockSignals(true);
		one.cue->setChecked(cued && cued(int(deck)));
		one.cue->blockSignals(false);
		if (!tile) continue;
		const snurra_tile_state state = tile->state();
		show(one.gain, int(std::lround(tile->gain_db())));
		show(one.low, step_of(state.eq.part[0]));
		show(one.mid, step_of(state.eq.part[1]));
		show(one.high, step_of(state.eq.part[2]));
		show(one.fader, state.level);
	}
}

bool snurra_mixer::eventFilter(QObject *watched, QEvent *event) {
	/* No room -- a phone upright -- no mixer at all, not even its margins:
	 * the tiles have the same controls, and two decks need every pixel of
	 * 412 (a deck's button was a pixel short with the strips hidden). Room
	 * is the strips and two decks of 300 beside them. */
	if (watched == parentWidget() && event->type() == QEvent::Resize) {
		const int needed = int(m_strips.size()) * (fontMetrics().height() * 2 + 4) + 8 + 2 * 300;
		const bool room = static_cast<QResizeEvent *>(event)->size().width() >= needed;
		if (room != isVisibleTo(parentWidget())) setVisible(room);
	}
	return QWidget::eventFilter(watched, event);
}
