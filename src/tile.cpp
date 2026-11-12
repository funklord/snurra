#include "snurra/tile.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPointer>
#include <QPainter>
#include <QRandomGenerator>
#include <QSlider>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "snurra/deck.h"
#include "snurra/engine.h"
#include "snurra/peaks.h"

/*
 * The waveform: the source's peaks, read on a thread of their own and
 * filled in left to right as they come; until they do, and for what has
 * no audio to read, a stand-in of bars from the source's name, so the
 * same file always looks the same. The playhead where the position is;
 * a click seeks there.
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

	/* Columns [first, first + values) of `columns`, as they are read. */
	void set_peaks(int columns, int first, const std::vector<float> &values) {
		if (int(m_peaks.size()) != columns) m_peaks.assign(size_t(columns), -1.0f);
		for (size_t i = 0; i < values.size() && first + int(i) < columns; ++i) {
			m_peaks[size_t(first) + i] = values[i];
			m_loudest = qMax(m_loudest, values[i]);
		}
		update();
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
		const QColor played = palette().color(QPalette::Highlight);
		const QColor ahead = palette().color(QPalette::Mid);
		if (!m_peaks.empty()) {
			/* A line a pixel, the loudest of the columns under it, scaled
			 * to the loudest read so far -- a shape to see, as Mixxx's
			 * visual gain gives: a quiet master, or a tone at an eighth of
			 * full scale, was otherwise two pixels of flat line. Not yet
			 * read, a baseline. */
			const float scale = 1.0f / qMax(m_loudest, 1e-3f);
			const int w = qMax(1, width());
			const int columns = int(m_peaks.size());
			for (int x = 0; x < w; ++x) {
				const int from = int(int64_t(x) * columns / w);
				const int to = qMax(from + 1, int(int64_t(x + 1) * columns / w));
				float loudest = -1;
				for (int c = from; c < to && c < columns; ++c) loudest = qMax(loudest, m_peaks[size_t(c)]);
				painter.setPen((x + 0.5) / w <= m_fraction ? played : ahead);
				const double half = loudest < 0 ? 0.5 : qMin(1.0f, loudest * scale) * (h - 2) / 2.0 + 0.5;
				painter.drawLine(QPointF(x + 0.5, h / 2.0 - half), QPointF(x + 0.5, h / 2.0 + half));
			}
			painter.setPen(palette().color(QPalette::Text));
			const int at = int(m_fraction * width());
			painter.drawLine(at, 0, at, h);
			return;
		}
		const double step = double(width()) / m_bars.size();
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
	std::vector<float> m_peaks;   /* -1: not read yet */
	float m_loudest = 0;
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

	/*
	 * The window takes the keys (snurra_node::keyPressEvent), so nothing in
	 * a tile takes focus: space would press whichever button had it, and a
	 * slider would keep the arrows. And a press anywhere in a tile makes it
	 * the one the keys act on.
	 */
	for (QWidget *child : findChildren<QWidget *>()) {
		child->setFocusPolicy(Qt::NoFocus);
		child->installEventFilter(this);
	}

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

	/* The waveform's peaks, read from a source of their own and handed
	 * over on this thread -- to the waveform, which a queued call never
	 * reaches once it has gone. */
	constexpr int COLUMNS = 1024;
	QPointer<snurra_waveform> waveform = m_waveform;
	m_peaks = std::make_unique<snurra_peaks>(
	        state.source, m_deck->rate(), COLUMNS, [waveform](int first, const std::vector<float> &values) {
		        QMetaObject::invokeMethod(
		                waveform, [waveform, first, values] {
			                if (waveform) waveform->set_peaks(COLUMNS, first, values);
		                },
		                Qt::QueuedConnection);
	        });
}

snurra_tile::~snurra_tile() {
	m_peaks.reset();   /* before the deck can go: the waveform first */
	if (m_owner) m_owner->retire(m_deck);
}

snurra_tile *snurra_tile::embedded(snurra_engine &engine, const snurra_tile_state &state,
                                   std::shared_ptr<snurra_bytes> bytes, QWidget *parent) {
	snurra_deck::start_t start;
	start.source = state.source;
	start.bytes = std::move(bytes);
	start.rate = engine.rate();
	start.fallback_length = snurra_us_to_frames(state.duration_us, start.rate);
	start.anchor_frame = snurra_us_to_frames(state.position_us, start.rate);
	start.anchor_us = state.sampled_at_us;
	start.playing = state.playing;
	start.level = float(state.level) / 100;
	snurra_tile *tile = new snurra_tile(state, engine.add(start), parent);
	tile->m_owner = &engine;
	return tile;
}

/* The deck's frames as the shared clock's microseconds, and back. */
static qint64 frames_to_us(int64_t frames, int rate) {
	return qint64(snurra_frames_to_us(frames, rate));
}

static int64_t us_to_frames(qint64 us, int rate) {
	return snurra_us_to_frames(us, rate);
}

snurra_tile_state snurra_tile::state() const {
	/* The deck's own exact pair rather than a moment of this one's
	 * choosing, which would be rounded to a frame (deck.h). */
	snurra_tile_state now = m_state;
	int64_t frame = 0;
	int64_t at = 0;
	int64_t advanced = 0;
	m_deck->last_rendered(frame, at, advanced);
	now.position_us = frames_to_us(frame, m_deck->rate());
	now.sampled_at_us = advanced > 0 ? at : snurra_clock_us();
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

void snurra_tile::seek_by(qint64 delta_us) {
	seek(m_state.position_now() + delta_us);
}

void snurra_tile::set_level(int level) {
	m_muted_from = -1;
	m_level->setValue(qBound(0, level, 100));   /* the slider tells the deck */
}

void snurra_tile::toggle_mute() {
	if (m_muted_from >= 0) {
		const int back = m_muted_from;
		set_level(back);
		return;
	}
	const int was = m_state.level;
	m_level->setValue(0);
	m_muted_from = was;
}

void snurra_tile::mousePressEvent(QMouseEvent *event) {
	emit touched(this);
	QWidget::mousePressEvent(event);
}

bool snurra_tile::eventFilter(QObject *watched, QEvent *event) {
	if (event->type() == QEvent::MouseButtonPress) emit touched(this);
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
		/* A drag and drop swallows the release: the button is up. */
		if (!(static_cast<QMouseEvent *>(event)->buttons() & Qt::LeftButton)) {
			m_dragging = false;
			m_title->setCursor(Qt::OpenHandCursor);
			break;
		}
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
