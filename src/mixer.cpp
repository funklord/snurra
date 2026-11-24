#include "snurra/mixer.h"
#include "snurra/controls.h"

#include "snurra/deck_waves.h"
#include "snurra/eq.h"
#include "snurra/tile.h"
#include "snurra/engine.h"

#include <QProgressBar>

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
			/* snurra's knob, held and moved up or down, never Qt's stock
			 * dial (the holder, 2026-11-25); a double-click to 0. */
			snurra_knob *knob = new snurra_knob(one.box);
			knob->setRange(from, to);
			knob->set_home(0);
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
		/* The drawn EQ -- every band, not three -- opened by hand: rarely
		 * wanted, so hidden until asked for, as Traktor's (2026-11-25). */
		one.eq = new QToolButton(one.box);
		one.eq->setText(tr("EQ"));
		one.eq->setCheckable(true);
		one.eq->setAutoRaise(true);
		one.eq->setAccessibleName(tr("Deck %1 EQ, drawn").arg(letter));
		one.eq->setToolTip(tr("The deck's EQ drawn as a line, every band: opened on the deck"));
		column->addWidget(one.eq, 0, Qt::AlignHCenter);
		/* A DJ fader, vertical and long, as a mixer's channel. */
		snurra_fader *fader = new snurra_fader(Qt::Vertical, one.box);
		fader->set_home(100);
		one.fader = fader;
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
		connect(one.eq, &QToolButton::clicked, this, [this, deck](bool open) {
			if (on_eq) on_eq(deck, open);
		});
		m_row->addWidget(one.box);
		m_strips.push_back(one);
	}
	refresh();
}

snurra_mixer::master_strip snurra_master_strip(QWidget *parent, snurra_engine *engine) {
	snurra_mixer::master_strip m_master;
	m_master.box = new QWidget(parent);
	m_master.box->setObjectName(QStringLiteral("snurra-master"));
	m_master.box->setAccessibleName(QObject::tr("Master"));
	QVBoxLayout *column = new QVBoxLayout(m_master.box);
	column->setContentsMargins(6, 0, 0, 0);
	column->setSpacing(0);
	QLabel *name = new QLabel(QObject::tr("MASTER"), m_master.box);
	QFont bold = name->font();
	bold.setBold(true);
	name->setFont(bold);
	name->setAlignment(Qt::AlignHCenter);
	column->addWidget(name);
	const int size = parent->fontMetrics().height() * 2;
	auto dial = [&](const QString &label, const QString &accessible, int from, int to, int home) {
		QLabel *text = new QLabel(label, m_master.box);
		QFont small = text->font();
		small.setPointSizeF(small.pointSizeF() * 0.8);
		text->setFont(small);
		text->setAlignment(Qt::AlignHCenter);
		column->addWidget(text);
		snurra_knob *knob = new snurra_knob(m_master.box);
		knob->setRange(from, to);
		knob->set_home(home);
		knob->setValue(home);
		knob->setFixedSize(size, size);
		knob->setNotchesVisible(true);
		knob->setAccessibleName(QObject::tr("Master %1").arg(accessible));
		column->addWidget(knob, 0, Qt::AlignHCenter);
		return knob;
	};
	m_master.level = dial(QObject::tr("LEVEL"), QObject::tr("level"), 0, 100, 100);
	m_master.high = dial(QObject::tr("HI"), QObject::tr("high"), snurra_mixer::KILL_STEP, 100, 0);
	m_master.mid = dial(QObject::tr("MID"), QObject::tr("mid"), snurra_mixer::KILL_STEP, 100, 0);
	m_master.low = dial(QObject::tr("LOW"), QObject::tr("low"), snurra_mixer::KILL_STEP, 100, 0);
	m_master.limit = new QToolButton(m_master.box);
	m_master.limit->setText(QObject::tr("LIMIT"));
	m_master.limit->setCheckable(true);
	m_master.limit->setChecked(true);
	m_master.limit->setAccessibleName(QObject::tr("Master limiter"));
	m_master.limit->setToolTip(QObject::tr("The limiter: what would go over full scale is held under it"));
	column->addWidget(m_master.limit, 0, Qt::AlignHCenter);
	/* What goes out, its peak in dB from -48 to 0. */
	m_master.meter = new QProgressBar(m_master.box);
	m_master.meter->setOrientation(Qt::Vertical);
	m_master.meter->setRange(-48, 0);
	m_master.meter->setValue(-48);
	m_master.meter->setTextVisible(false);
	m_master.meter->setAccessibleName(QObject::tr("Master peak"));
	m_master.meter->setFixedWidth(parent->fontMetrics().height() / 2 + 2);
	column->addWidget(m_master.meter, 1, Qt::AlignHCenter);
	m_master.reduction = new QLabel(m_master.box);
	m_master.reduction->setAccessibleName(QObject::tr("Limiter's reduction"));
	m_master.reduction->setAlignment(Qt::AlignHCenter);
	column->addWidget(m_master.reduction);
	QObject::connect(m_master.level, &QDial::valueChanged, m_master.box, [engine](int level) {
		if (engine) engine->set_master_level(float(level) / 100);
	});
	auto eq = [&m_master, engine](int part, QDial *knob) {
		QObject::connect(knob, &QDial::valueChanged, m_master.box, [engine, part](int step) {
			if (engine) engine->set_master_eq(part, db_of(step));
		});
	};
	eq(0, m_master.low);
	eq(1, m_master.mid);
	eq(2, m_master.high);
	QObject::connect(m_master.limit, &QToolButton::toggled, m_master.box, [engine](bool on) {
		if (engine) engine->set_limiter(on);
	});
	return m_master;
}

/* What the engine has now, onto the strip -- a knob held is left alone --
 * and the meter from what went out since the last look. */
void snurra_master_refresh(const snurra_mixer::master_strip &m_master, snurra_engine *m_engine) {
	if (!m_engine || !m_master.box) return;
	auto show = [](QAbstractSlider *control, int value) {
		if (control->isSliderDown() || control->value() == value) return;
		control->blockSignals(true);
		control->setValue(value);
		control->blockSignals(false);
	};
	show(m_master.level, int(std::lround(m_engine->master_level() * 100)));
	show(m_master.low, step_of(m_engine->master_eq(0)));
	show(m_master.mid, step_of(m_engine->master_eq(1)));
	show(m_master.high, step_of(m_engine->master_eq(2)));
	m_master.limit->blockSignals(true);
	m_master.limit->setChecked(m_engine->limiter());
	m_master.limit->blockSignals(false);
	const float peak = m_engine->take_master_peak();
	m_master.meter->setValue(peak > 0 ? std::clamp(int(std::lround(20 * std::log10(peak))), -48, 0) : -48);
	const float taken = m_engine->take_master_reduction();
	m_master.reduction->setText(taken < 0.999f ? QStringLiteral("%1").arg(20 * std::log10(taken), 0, 'f', 1) : QString());
}

void snurra_mixer::set_master(snurra_engine *engine) {
	m_engine = engine;
	if (m_master.box) return;
	m_master = snurra_master_strip(this, engine);
	m_row->addWidget(m_master.box);
	m_master.box->hide();
}



void snurra_mixer::set_master_open(bool open) {
	if (m_master.box) m_master.box->setVisible(open);
}

bool snurra_mixer::master_open() const {
	return m_master.box && !m_master.box->isHidden();
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
	if (master_open()) snurra_master_refresh(m_master, m_engine);
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
		one.eq->setEnabled(tile != nullptr);
		one.eq->blockSignals(true);
		one.eq->setChecked(eq_opened && eq_opened(int(deck)));
		one.eq->blockSignals(false);
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
		if (room != !isHidden()) {
			setVisible(room);
			if (on_shown) on_shown(room);
		}
	}
	return QWidget::eventFilter(watched, event);
}
