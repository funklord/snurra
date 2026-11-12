#include "tile.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QRandomGenerator>
#include <QSlider>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "deck.h"

/*
 * A stand-in for the waveform until there is audio to draw: bars from the
 * source's name, so the same file always looks the same, and the playhead
 * where the position is. A click seeks there.
 */
class snurra_waveform : public QWidget {
public:
	snurra_waveform(const QString &source, QWidget *parent) : QWidget(parent) {
		QRandomGenerator bars(qHash(source));
		for (int i = 0; i < 96; ++i) m_bars << 0.15 + 0.85 * bars.generateDouble();
		setMinimumHeight(fontMetrics().height() * 2);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
		setCursor(Qt::IBeamCursor);
	}

	void set_fraction(double fraction) {
		if (qAbs(fraction - m_fraction) * width() < 0.5) return;   /* no repaint for no pixel */
		m_fraction = fraction;
		update();
	}

	std::function<void(double)> on_seek;

protected:
	void paintEvent(QPaintEvent *) override {
		QPainter painter(this);
		const int h = height();
		const double step = double(width()) / m_bars.size();
		const QColor played = palette().color(QPalette::Highlight);
		const QColor ahead = palette().color(QPalette::Mid);
		for (int i = 0; i < m_bars.size(); ++i) {
			const double x = i * step;
			const double bar = m_bars.at(i) * (h - 2);
			painter.fillRect(QRectF(x, (h - bar) / 2, qMax(1.0, step - 1), bar),
			                 (i + 0.5) / m_bars.size() <= m_fraction ? played : ahead);
		}
		painter.setPen(palette().color(QPalette::Text));
		const int at = int(m_fraction * width());
		painter.drawLine(at, 0, at, h);
	}

	void mousePressEvent(QMouseEvent *event) override {
		if (on_seek && width() > 0) on_seek(qBound(0.0, event->position().x() / width(), 1.0));
	}

private:
	QList<double> m_bars;
	double m_fraction = 0;
};

snurra_tile::snurra_tile(const snurra_tile_state &state, snurra_deck *deck, QWidget *parent)
    : QWidget(parent), m_state(state), m_deck(deck) {
	setObjectName(QStringLiteral("snurra-tile"));
	setAttribute(Qt::WA_StyledBackground);
	setStyleSheet(QStringLiteral("#snurra-tile { border: 1px solid palette(mid); border-radius: 3px; }"));

	QVBoxLayout *column = new QVBoxLayout(this);
	column->setContentsMargins(4, 2, 4, 4);
	column->setSpacing(2);

	QHBoxLayout *top = new QHBoxLayout;
	const QString name = QFileInfo(state.source).fileName();
	m_title = new QLabel(name.isEmpty() ? state.source : name, this);
	m_title->setObjectName(QStringLiteral("snurra-handle"));
	m_title->setCursor(Qt::OpenHandCursor);
	m_title->setToolTip(state.source);
	m_title->setMinimumWidth(1);
	m_title->installEventFilter(this);
	top->addWidget(m_title, 1);
	/* The controls' handle: pulled out, this tile plays on its own. */
	m_pull = new QToolButton(this);
	m_pull->setObjectName(QStringLiteral("snurra-pull"));
	m_pull->setAutoRaise(true);
	m_pull->setCheckable(true);
	top->addWidget(m_pull);
	QToolButton *close = new QToolButton(this);
	close->setAutoRaise(true);
	close->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
	close->setAccessibleName(tr("Close"));
	top->addWidget(close);
	column->addLayout(top);

	m_waveform = new snurra_waveform(state.source, this);
	m_waveform->setAccessibleName(tr("Waveform"));
	m_waveform->on_seek = [this](double fraction) { seek(qint64(fraction * m_state.duration_us)); };
	column->addWidget(m_waveform, 1);

	m_controls = new QWidget(this);
	m_controls->setObjectName(QStringLiteral("snurra-controls"));
	QVBoxLayout *strip = new QVBoxLayout(m_controls);
	strip->setContentsMargins(0, 0, 0, 0);
	strip->setSpacing(2);
	QHBoxLayout *transport = new QHBoxLayout;
	m_play = new QToolButton(m_controls);
	m_play->setObjectName(QStringLiteral("snurra-play"));
	m_play->setAutoRaise(true);
	m_play->setCheckable(true);
	transport->addWidget(m_play);
	m_position = new QSlider(Qt::Horizontal, m_controls);
	m_position->setAccessibleName(tr("Position"));
	m_position->setRange(0, int(qMax<qint64>(1, state.duration_us / 100000)));
	transport->addWidget(m_position, 1);
	strip->addLayout(transport);
	m_level = new QSlider(Qt::Horizontal, m_controls);
	m_level->setAccessibleName(tr("Level"));
	m_level->setRange(0, 100);
	m_level->setValue(state.level);
	strip->addWidget(m_level);
	column->addWidget(m_controls);

	connect(close, &QToolButton::clicked, this, [this] { emit close_requested(this); });
	connect(m_pull, &QToolButton::toggled, this, &snurra_tile::set_controls);
	connect(m_play, &QToolButton::toggled, this, &snurra_tile::set_playing);
	connect(m_position, &QSlider::sliderMoved, this, [this](int tenths) { seek(qint64(tenths) * 100000); });
	connect(m_level, &QSlider::valueChanged, this, [this](int level) {
		m_state.level = level;
		m_deck->set_level(float(level) / 100);
	});

	/* A tenth of a second is fine enough for a slider and a playhead, and
	 * cheap; the waveform repaints only when its playhead moves a pixel. */
	QTimer *ticker = new QTimer(this);
	ticker->setInterval(100);
	connect(ticker, &QTimer::timeout, this, &snurra_tile::tick);
	ticker->start();

	set_controls(state.controls);
	set_playing(state.playing);
	tick();
}

/* The deck's frames as the shared clock's microseconds, and back. */
static qint64 frames_to_us(int64_t frames, int rate) {
	return qint64(frames * 1000000 / rate);
}

static int64_t us_to_frames(qint64 us, int rate) {
	return int64_t(us) * rate / 1000000;
}

snurra_tile_state snurra_tile::state() const {
	snurra_tile_state now = m_state;
	const qint64 at = snurra_clock_us();
	now.position_us = frames_to_us(m_deck->position_at(at), m_deck->rate());
	now.sampled_at_us = at;
	return now;
}

void snurra_tile::seek(qint64 position_us) {
	m_deck->seek(us_to_frames(qBound<qint64>(0, position_us, m_state.duration_us), m_deck->rate()));
	tick();
}

void snurra_tile::set_playing(bool playing) {
	m_state.playing = playing;
	m_deck->set_playing(playing);
	m_play->blockSignals(true);
	m_play->setChecked(playing);
	m_play->blockSignals(false);
	m_play->setIcon(style()->standardIcon(playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
	m_play->setAccessibleName(playing ? tr("Pause") : tr("Play"));
}

void snurra_tile::set_controls(bool out) {
	m_state.controls = out;
	m_controls->setVisible(out);
	m_pull->blockSignals(true);
	m_pull->setChecked(out);
	m_pull->blockSignals(false);
	m_pull->setArrowType(out ? Qt::UpArrow : Qt::DownArrow);
	m_pull->setAccessibleName(out ? tr("Push the controls in") : tr("Pull the controls out"));
	m_pull->setToolTip(m_pull->accessibleName());
	/* Pushed back in, a tile stops: what plays is what can be stopped. */
	if (!out && m_state.playing) set_playing(false);
}

void snurra_tile::set_place(int row, int column) {
	m_state.row = row;
	m_state.column = column;
}

void snurra_tile::tick() {
	/* The length once the source has said it; a dummy until then. */
	const int64_t length = m_deck->length();
	if (length > 0 && frames_to_us(length, m_deck->rate()) != m_state.duration_us) {
		m_state.duration_us = frames_to_us(length, m_deck->rate());
		m_position->setRange(0, int(qMax<qint64>(1, m_state.duration_us / 100000)));
	}
	if (m_state.playing && m_deck->ended()) set_playing(false);
	const qint64 now = frames_to_us(m_deck->position_at(snurra_clock_us()), m_deck->rate());
	m_state.position_us = now;
	m_state.sampled_at_us = snurra_clock_us();
	if (!m_position->isSliderDown()) m_position->setValue(int(now / 100000));
	m_waveform->set_fraction(m_state.duration_us > 0 ? double(now) / m_state.duration_us : 0);
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
