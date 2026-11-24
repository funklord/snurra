#include "snurra/deck_head.h"

#include "snurra/beats.h"
#include "snurra/clock.h"
#include "snurra/deck.h"
#include "snurra/deck_waves.h"
#include "snurra/source.h"
#include "snurra/tile.h"

#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QStyle>
#include <QTimer>
#include <QToolButton>

#include <cmath>

snurra_deck_head::snurra_deck_head(int deck, QWidget *parent) : QWidget(parent), m_deck(deck) {
	setObjectName(QStringLiteral("snurra-deck-head"));
	QGridLayout *grid = new QGridLayout(this);
	grid->setContentsMargins(0, 0, 0, 0);
	grid->setHorizontalSpacing(8);
	grid->setVerticalSpacing(0);

	m_letter = new QLabel(QString(QChar(QLatin1Char(char('A' + deck)))), this);
	QFont big = font();
	big.setBold(true);
	big.setPointSizeF(big.pointSizeF() * 1.6);
	m_letter->setFont(big);
	const QColor colour = snurra_deck_waves::colour_of(deck);
	m_letter->setStyleSheet(QStringLiteral("color: %1;").arg(colour.name()));
	m_letter->setAccessibleName(tr("Deck %1").arg(m_letter->text()));

	m_title = new QLabel(this);
	QFont bold = font();
	bold.setBold(true);
	m_title->setFont(bold);
	m_title->setTextFormat(Qt::PlainText);
	m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	m_artist = new QLabel(this);
	m_artist->setTextFormat(Qt::PlainText);
	m_artist->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	m_artist->setForegroundRole(QPalette::PlaceholderText);

	m_bpm = new QLabel(this);
	m_bpm->setFont(big);
	m_bpm->setAccessibleName(tr("Tempo, beats a minute"));
	m_bpm->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	m_key = new QLabel(this);
	m_key->setAccessibleName(tr("Key as heard"));
	m_key->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	m_tempo = new QLabel(this);
	m_tempo->setAccessibleName(tr("Tempo moved"));
	m_tempo->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	m_left = new QLabel(this);
	QFont time = big;
	time.setBold(false);
	m_left->setFont(time);
	m_left->setAccessibleName(tr("Time left"));
	m_left->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

	auto button = [this](const QString &text, const QString &name) {
		QToolButton *one = new QToolButton(this);
		one->setText(text);
		one->setAccessibleName(name);
		one->setToolTip(name);
		one->setEnabled(false);
		return one;
	};
	m_play = button(QString(), tr("Play or pause"));
	m_play->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
	m_cue = button(tr("CUE"), tr("Cue: set it paused, back to it playing"));
	m_sync = button(tr("SYNC"), tr("Sync: this deck's beat to the other's"));
	connect(m_play, &QToolButton::clicked, this, [this] {
		if (m_tile) m_tile->set_playing(!m_tile->playing());
		refresh();
	});
	connect(m_cue, &QToolButton::clicked, this, [this] {
		if (m_tile) m_tile->cue();
		refresh();
	});
	connect(m_sync, &QToolButton::clicked, this, [this] {
		if (m_tile) m_tile->request_sync();
	});

	/* Row 0: letter, title, BPM, time left; row 1: artist, key and tempo,
	 * the buttons. As Traktor's head: what is playing left, its numbers
	 * right. */
	grid->addWidget(m_letter, 0, 0, 2, 1, Qt::AlignVCenter);
	grid->addWidget(m_title, 0, 1);
	grid->addWidget(m_artist, 1, 1);
	grid->addWidget(m_bpm, 0, 2);
	QWidget *numbers = new QWidget(this);
	QHBoxLayout *row = new QHBoxLayout(numbers);
	row->setContentsMargins(0, 0, 0, 0);
	row->addWidget(m_key);
	row->addWidget(m_tempo);
	grid->addWidget(numbers, 1, 2);
	grid->addWidget(m_left, 0, 3);
	QWidget *buttons = new QWidget(this);
	QHBoxLayout *press = new QHBoxLayout(buttons);
	press->setContentsMargins(0, 0, 0, 0);
	press->addWidget(m_cue);
	press->addWidget(m_play);
	press->addWidget(m_sync);
	grid->addWidget(buttons, 1, 3);
	grid->setColumnStretch(1, 1);

	/* Its text is what a label prefers, never what it needs: on a phone's
	 * width (412 upright) two heads share the row, and a loaded deck's
	 * longer text made it wider than an empty one beside it. */
	for (QLabel *label : { m_letter, m_bpm, m_key, m_tempo, m_left }) label->setMinimumWidth(1);

	QTimer *tick = new QTimer(this);
	tick->setInterval(100);
	connect(tick, &QTimer::timeout, this, [this] {
		if (isVisible()) refresh();
	});
	tick->start();
	refresh();
}

void snurra_deck_head::set_tile(snurra_tile *tile) {
	m_tile = tile;
	m_title->clear();
	m_artist->clear();
	if (tile) {
		const QString source = tile->state().sound_source();
		snurra_tags tags;
		/* The file's own tags, once: a local file, never a stream. */
		if (QFileInfo(source).isFile()) tags = snurra_read_tags(source);
		m_title->setText(tags.title.isEmpty() ? tile->title() : tags.title);
		m_artist->setText(tags.artist);
	}
	for (QToolButton *one : { m_play, m_cue, m_sync }) one->setEnabled(tile != nullptr);
	refresh();
}

void snurra_deck_head::refresh() {
	snurra_tile *tile = m_tile;
	if (!tile) {
		m_bpm->clear();
		m_key->clear();
		m_tempo->clear();
		m_left->clear();
		m_play->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
		return;
	}
	const snurra_beat_grid grid = tile->beats();
	m_bpm->setText(grid.valid() ? QString::number(grid.bpm() * tile->tempo(), 'f', 1) : QStringLiteral("--"));
	m_key->setText(snurra_key_name(tile->key_heard()));
	const double moved = (double(tile->tempo()) - 1) * 100;
	m_tempo->setText(std::abs(moved) < 0.05 ? QStringLiteral("0.0%") : QStringLiteral("%1%2%").arg(moved > 0 ? QStringLiteral("+") : QString()).arg(moved, 0, 'f', 1));
	const int rate = std::max(1, tile->deck()->rate());
	const int64_t length = tile->length_frames();
	if (length > 0) {
		/* Time left as it will be heard, at the deck's tempo. */
		const double seconds = std::max(0.0, double(length - tile->frame_now()) / rate / std::max(0.01f, tile->tempo()));
		const int whole = int(std::ceil(seconds));
		m_left->setText(QStringLiteral("-%1:%2").arg(whole / 60).arg(whole % 60, 2, 10, QLatin1Char('0')));
	} else {
		m_left->clear();
	}
	m_play->setIcon(style()->standardIcon(tile->playing() ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
}

QString snurra_deck_head::bpm_text() const { return m_bpm->text(); }
QString snurra_deck_head::key_text() const { return m_key->text(); }
QString snurra_deck_head::left_text() const { return m_left->text(); }
QString snurra_deck_head::artist_text() const { return m_artist->text(); }
