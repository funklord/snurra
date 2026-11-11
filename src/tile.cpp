#include "tile.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QSlider>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

snurra_tile::snurra_tile(const snurra_tile_state &state, QWidget *parent)
    : QWidget(parent), m_state(state) {
	setObjectName(QStringLiteral("snurra-tile"));
	setAttribute(Qt::WA_StyledBackground);
	setStyleSheet(QStringLiteral("#snurra-tile { border: 1px solid palette(mid); border-radius: 3px; }"));

	QVBoxLayout *column = new QVBoxLayout(this);
	column->setContentsMargins(4, 2, 4, 4);
	column->setSpacing(2);

	QHBoxLayout *top = new QHBoxLayout;
	m_title = new QLabel(QFileInfo(state.source).fileName().isEmpty() ? state.source
	                                                                   : QFileInfo(state.source).fileName(), this);
	m_title->setObjectName(QStringLiteral("snurra-handle"));
	m_title->setCursor(Qt::OpenHandCursor);
	m_title->setToolTip(state.source);
	m_title->setMinimumWidth(1);
	m_title->installEventFilter(this);
	top->addWidget(m_title, 1);
	QToolButton *close = new QToolButton(this);
	close->setAutoRaise(true);
	close->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
	close->setAccessibleName(tr("Close"));
	top->addWidget(close);
	column->addLayout(top);

	QHBoxLayout *transport = new QHBoxLayout;
	m_play = new QToolButton(this);
	m_play->setObjectName(QStringLiteral("snurra-play"));
	m_play->setAutoRaise(true);
	m_play->setCheckable(true);
	transport->addWidget(m_play);
	m_position = new QSlider(Qt::Horizontal, this);
	m_position->setAccessibleName(tr("Position"));
	m_position->setRange(0, int(qMax<qint64>(1, state.duration_ms / 100)));
	transport->addWidget(m_position, 1);
	column->addLayout(transport);

	m_level = new QSlider(Qt::Horizontal, this);
	m_level->setAccessibleName(tr("Level"));
	m_level->setRange(0, 100);
	m_level->setValue(state.level);
	column->addWidget(m_level);

	connect(close, &QToolButton::clicked, this, [this] { emit close_requested(this); });
	connect(m_play, &QToolButton::toggled, this, &snurra_tile::set_playing);
	connect(m_position, &QSlider::sliderMoved, this, [this](int tenths) {
		m_state.position_ms = qint64(tenths) * 100;
		m_state.sampled_at = QDateTime::currentMSecsSinceEpoch();
	});
	connect(m_level, &QSlider::valueChanged, this, [this](int level) { m_state.level = level; });

	/* A tenth of a second is fine enough for a slider and cheap. */
	QTimer *tick = new QTimer(this);
	tick->setInterval(100);
	connect(tick, &QTimer::timeout, this, &snurra_tile::show_position);
	tick->start();

	m_play->setChecked(state.playing);
	set_playing(state.playing);
	show_position();
}

snurra_tile_state snurra_tile::state() const {
	snurra_tile_state now = m_state;
	const qint64 at = QDateTime::currentMSecsSinceEpoch();
	now.position_ms = m_state.position_now(at);
	now.sampled_at = at;
	return now;
}

void snurra_tile::set_playing(bool playing) {
	/* The sample moves to now either way, so a pause keeps its place. */
	const qint64 at = QDateTime::currentMSecsSinceEpoch();
	m_state.position_ms = m_state.position_now(at);
	m_state.sampled_at = at;
	m_state.playing = playing;
	m_play->blockSignals(true);
	m_play->setChecked(playing);
	m_play->blockSignals(false);
	m_play->setIcon(style()->standardIcon(playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
	m_play->setAccessibleName(playing ? tr("Pause") : tr("Play"));
}

void snurra_tile::set_place(int row, int column) {
	m_state.row = row;
	m_state.column = column;
}

void snurra_tile::show_position() {
	if (!m_position->isSliderDown()) m_position->setValue(int(m_state.position_now() / 100));
}

QSize snurra_tile::sizeHint() const {
	return QSize(fontMetrics().averageCharWidth() * 26, QWidget::sizeHint().height());
}

QSize snurra_tile::minimumSizeHint() const {
	return QSize(fontMetrics().averageCharWidth() * 14, QWidget::minimumSizeHint().height());
}

bool snurra_tile::eventFilter(QObject *watched, QEvent *event) {
	if (watched != m_title) return QWidget::eventFilter(watched, event);
	switch (event->type()) {
	case QEvent::MouseButtonPress: {
		const QMouseEvent *press = static_cast<QMouseEvent *>(event);
		if (press->button() != Qt::LeftButton) break;
		m_press = press->globalPosition().toPoint();
		m_dragging = true;
		m_title->setCursor(Qt::ClosedHandCursor);
		return true;
	}
	case QEvent::MouseMove:
		if (!m_dragging) break;
		emit handle_moved(this, static_cast<QMouseEvent *>(event)->globalPosition().toPoint());
		return true;
	case QEvent::MouseButtonRelease:
		if (!m_dragging) break;
		m_dragging = false;
		m_title->setCursor(Qt::OpenHandCursor);
		emit handle_released(this, static_cast<QMouseEvent *>(event)->globalPosition().toPoint());
		return true;
	default:
		break;
	}
	return QWidget::eventFilter(watched, event);
}
