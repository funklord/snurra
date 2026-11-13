#include "snurra/tile.h"
#include "snurra/source.h"

#include <QCoreApplication>
#include <QDial>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QPainterPath>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMetaMethod>
#include <QMimeData>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPointer>
#include <QPainter>
#include <QRandomGenerator>
#include <QSlider>
#include <QStyle>
#include <QLibrary>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

#include "snurra/beats.h"
#include "snurra/deck.h"
#include "snurra/engine.h"
#include "snurra/peaks.h"
#include "snurra/subtitles.h"
#include "snurra/theme.h"
#include "snurra/video.h"

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
		/* One height for every waveform (project.md sec 3b): a drawer
		 * opening adds room, it never squeezes or stretches one. */
		setFixedHeight(fontMetrics().height() * 3);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
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

	/* The beat grid, as fractions of the track: a tick a beat along the
	 * top, a longer one each fourth -- where a jump of bars lands. */
	/* Said across the waveform, in place of the bars, when there is
	 * nothing to draw and a reason why: what could not be played. */
	void set_message(const QString &message) {
		m_message = message;
		setToolTip(message);
		update();
	}

	/* The cue point, a fraction of the track; negative for none: a mark
	 * down from the top, a DJ's cue triangle. */
	/* Where chapters begin, as fractions of the track: a mark up from
	 * the bottom each, a mix's tracks. */
	void set_chapters(const QList<double> &starts) {
		m_chapter_starts = starts;
		update();
	}

	void set_cue(double fraction) {
		m_cue_at = fraction;
		update();
	}

	void set_beats(double first, double period) {
		m_beat_first = first;
		m_beat_period = period;
		update();
	}

	/*
	 * The envelope, as fractions of the track (x) and gains (y), drawn
	 * over the waveform whenever it has points. Editing, a press near a
	 * point takes it and anywhere else adds one; dragging moves it between
	 * its neighbours, and dragged out of the waveform it goes -- Audacity's
	 * envelope tool. Each change ends with on_envelope.
	 */
	void set_envelope(const QList<QPointF> &points) {
		m_envelope = points;
		update();
	}
	void set_editing(bool editing) {
		m_editing = editing;
		setCursor(editing ? Qt::CrossCursor : Qt::IBeamCursor);
		update();
	}
	std::function<void(const QList<QPointF> &)> on_envelope;

	/* Said small at the top left: where its lane begins, when late. */
	void set_note(const QString &note) {
		if (note == m_note) return;
		m_note = note;
		update();
	}

	/* A drag with Ctrl: the lane moved, by fractions of this track. */
	std::function<void(double fraction, bool done, bool snap)> on_shift;

	/* A train's crossfade, as fractions of the track: the fade in from
	 * its start, the fade out to its end; 0 for none. */
	void set_crossfade(double in, double out) {
		if (qAbs(in - m_fade_in) * width() < 0.5 && qAbs(out - m_fade_out) * width() < 0.5
		    && (in > 0) == (m_fade_in > 0) && (out > 0) == (m_fade_out > 0))
			return;   /* no repaint for no pixel */
		m_fade_in = in;
		m_fade_out = out;
		update();
	}

protected:
	void paintEvent(QPaintEvent *event) override {
		paint_waveform();
		paint_beats();
		paint_crossfade();
		paint_envelope();
		if (!m_chapter_starts.isEmpty()) {
			QPainter painter(this);
			painter.setPen(palette().color(QPalette::Text));
			const int reach = qMax(4, height() / 4);
			for (double at : m_chapter_starts) {
				const int x = int(at * width());
				if (x > 0) painter.drawLine(x, height() - reach, x, height());
			}
		}
		if (m_cue_at >= 0) {
			QPainter painter(this);
			painter.setRenderHint(QPainter::Antialiasing);
			const double x = m_cue_at * width();
			const double size = qMax(4.0, height() / 6.0);
			painter.setPen(Qt::NoPen);
			painter.setBrush(QColor(0xf0, 0xa0, 0x30));
			painter.drawPolygon(QPolygonF{ QPointF(x - size, 0), QPointF(x + size, 0), QPointF(x, size * 1.4) });
		}
		if (!m_note.isEmpty()) {
			QPainter painter(this);
			painter.setPen(palette().color(QPalette::Text));
			painter.drawText(rect().adjusted(3, 1, -3, -1), Qt::AlignLeft | Qt::AlignTop, m_note);
		}
		(void)event;
	}

	void paint_beats() {
		if (m_beat_period <= 0 || m_beat_period * width() < 3) return;   /* too dense to see */
		QPainter painter(this);
		painter.setPen(palette().color(QPalette::Text));
		int k = 0;
		for (double at = m_beat_first; at < 1; at += m_beat_period, ++k) {
			const int x = int(at * width());
			painter.drawLine(x, 0, x, k % 4 ? 2 : 5);
		}
	}

	QPointF to_widget(const QPointF &point) const {
		return QPointF(point.x() * width(), (1 - point.y()) * (height() - 1));
	}

	QPointF from_widget(const QPointF &at) const {
		return QPointF(qBound(0.0, at.x() / qMax(1, width()), 1.0),
		               qBound(0.0, 1 - at.y() / qMax(1, height() - 1), 1.0));
	}

	void paint_envelope() {
		if (m_envelope.isEmpty() && !m_editing) return;
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing);
		QPen pen(palette().color(QPalette::Link));
		pen.setWidthF(1.5);
		painter.setPen(pen);
		QPolygonF line;
		if (m_envelope.isEmpty()) {
			line << to_widget(QPointF(0, 1)) << to_widget(QPointF(1, 1));
		} else {
			line << to_widget(QPointF(0, m_envelope.first().y()));
			for (const QPointF &point : m_envelope) line << to_widget(point);
			line << to_widget(QPointF(1, m_envelope.last().y()));
		}
		painter.drawPolyline(line);
		if (!m_editing) return;
		painter.setBrush(palette().color(QPalette::Link));
		for (const QPointF &point : m_envelope) painter.drawRect(QRectF(to_widget(point) - QPointF(3, 3), QSizeF(6, 6)));
	}

	/*
	 * The crossfade as the deck plays it (project.md sec 3b): a quarter
	 * circle, sin rising over the fade in and cos falling over the fade
	 * out -- equal power -- with what it takes away shaded above it.
	 */
	void paint_crossfade() {
		if (m_fade_in <= 0 && m_fade_out <= 0) return;
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing);
		QColor shade = palette().color(QPalette::Window);
		shade.setAlpha(140);
		QPen pen(palette().color(QPalette::Link));
		pen.setWidthF(1.5);
		auto curve = [&](double from, double to, bool rising) {
			constexpr int STEPS = 32;
			QPolygonF line;
			for (int i = 0; i <= STEPS; ++i) {
				const double t = double(i) / STEPS;
				const double gain = rising ? std::sin(t * M_PI_2) : std::cos(t * M_PI_2);
				line << to_widget(QPointF(from + (to - from) * t, gain));
			}
			QPolygonF above = line;
			above << QPointF(to * width(), 0) << QPointF(from * width(), 0);
			painter.setPen(Qt::NoPen);
			painter.setBrush(shade);
			painter.drawPolygon(above);
			painter.setPen(pen);
			painter.setBrush(Qt::NoBrush);
			painter.drawPolyline(line);
		};
		if (m_fade_in > 0) curve(0, qMin(1.0, m_fade_in), true);
		if (m_fade_out > 0) curve(qMax(0.0, 1 - m_fade_out), 1, false);
	}

	void paint_waveform() {
		QPainter painter(this);
		const int h = height();
		if (!m_message.isEmpty()) {
			painter.setPen(palette().color(QPalette::PlaceholderText));
			painter.drawText(rect().adjusted(4, 0, -4, 0), Qt::AlignCenter | Qt::TextWordWrap, m_message);
			return;
		}
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
		if (!m_editing && on_shift && event->button() == Qt::LeftButton && (event->modifiers() & Qt::ControlModifier)) {
			m_shifting = true;
			m_shift_x = event->position().x();
			return;
		}
		if (!m_editing) {
			if (on_seek && width() > 0) on_seek(qBound(0.0, event->position().x() / width(), 1.0));
			return;
		}
		if (event->button() != Qt::LeftButton) return;
		const QPointF at = event->position();
		m_grabbed = -1;
		for (int i = 0; i < m_envelope.size(); ++i)
			if (QLineF(to_widget(m_envelope.at(i)), at).length() <= GRAB)
				m_grabbed = i;
		if (m_grabbed < 0) {
			const QPointF point = from_widget(at);
			m_grabbed = 0;
			while (m_grabbed < m_envelope.size() && m_envelope.at(m_grabbed).x() <= point.x()) ++m_grabbed;
			m_envelope.insert(m_grabbed, point);
		}
		update();
	}

	void mouseMoveEvent(QMouseEvent *event) override {
		if (m_shifting) {
			on_shift((event->position().x() - m_shift_x) / qMax(1, width()), false,
			         !(event->modifiers() & Qt::ShiftModifier));
			return;
		}
		if (!m_editing || m_grabbed < 0 || m_grabbed >= m_envelope.size()) return;
		const QPointF at = event->position();
		/* Out of the waveform by more than a grab's reach: it goes, shown
		 * gone, and comes back if dragged in again before the release. */
		m_outside = !rect().adjusted(-GRAB * 3, -GRAB * 3, GRAB * 3, GRAB * 3).contains(at.toPoint());
		QPointF point = from_widget(at);
		const double low = m_grabbed > 0 ? m_envelope.at(m_grabbed - 1).x() : 0.0;
		const double high = m_grabbed + 1 < m_envelope.size() ? m_envelope.at(m_grabbed + 1).x() : 1.0;
		point.setX(qBound(low, point.x(), high));
		m_envelope[m_grabbed] = point;
		update();
	}

	void mouseReleaseEvent(QMouseEvent *event) override {
		if (m_shifting) {
			m_shifting = false;
			on_shift((event->position().x() - m_shift_x) / qMax(1, width()), true,
			         !(event->modifiers() & Qt::ShiftModifier));
			return;
		}
		if (!m_editing || m_grabbed < 0) return;
		if (m_outside && m_grabbed < m_envelope.size()) m_envelope.removeAt(m_grabbed);
		m_grabbed = -1;
		m_outside = false;
		update();
		if (on_envelope) on_envelope(m_envelope);
	}

private:
	QList<double> m_bars;
	std::vector<float> m_peaks;   /* -1: not read yet */
	float m_loudest = 0;
	double m_fraction = 0;
	QString m_message;
	double m_beat_first = 0;
	double m_beat_period = 0;   /* fractions of the track; 0: no grid */
	static constexpr double GRAB = 6;   /* pixels: how near a press takes a point */
	QList<QPointF> m_envelope;
	QString m_note;
	double m_cue_at = -1;
	QList<double> m_chapter_starts;
	bool m_shifting = false;   /* a Ctrl drag moving the lane */
	double m_shift_x = 0;      /* where it began, in pixels */
	double m_fade_in = 0;    /* fractions of the track; 0: none */
	double m_fade_out = 0;
	bool m_editing = false;
	int m_grabbed = -1;
	bool m_outside = false;
};

/*
 * A picture, as large as fits and centred on black. `pts` is a property --
 * the time of the frame shown -- so what is on screen can be checked
 * against what is heard.
 */
class snurra_video_view : public QWidget {
public:
	explicit snurra_video_view(QWidget *parent = nullptr) : QWidget(parent) {
		setAccessibleName(QStringLiteral("Video"));
		setProperty("pts", qint64(INT64_MIN));
		/* Every pixel its own (black round the picture): what is behind
		 * it -- the drawer, a window that may be translucent -- is not
		 * painted again for each frame. */
		setAttribute(Qt::WA_OpaquePaintEvent);
		setMouseTracking(true);
		m_corner_shown.setSingleShot(true);
		m_corner_shown.setInterval(CORNER_MS);
		QObject::connect(&m_corner_shown, &QTimer::timeout, this, [this] {
			if (m_corner && !m_corner->underMouse()) m_corner->hide();
		});
	}
	/*
	 * The full-screen button, as YouTube's and most players' now: in the
	 * picture's bottom right corner while the pointer moves over it, gone
	 * two seconds after it stops or leaves -- the picture's, not a
	 * button always on the film. `full` says which way it goes.
	 */
	void set_corner(bool full, std::function<void()> pressed) {
		if (!m_corner) {
			m_corner = new QToolButton(this);
			m_corner->setAutoRaise(true);
			m_corner->setFocusPolicy(Qt::NoFocus);
			m_corner->setIconSize(QSize(24, 24));
			m_corner->setStyleSheet(QStringLiteral("QToolButton { background: rgba(0, 0, 0, 120); border-radius: 4px; }"));
			m_corner->hide();
		}
		m_corner->setIcon(style()->standardIcon(full ? QStyle::SP_TitleBarNormalButton : QStyle::SP_TitleBarMaxButton));
		m_corner->setAccessibleName(full ? QObject::tr("Leave full screen") : QObject::tr("Full screen"));
		m_corner->setToolTip(m_corner->accessibleName());
		m_corner->disconnect();
		QObject::connect(m_corner, &QToolButton::clicked, this, [pressed] { pressed(); });
		place_corner();
	}
	QToolButton *corner() const { return m_corner; }
	void show_frame(const snurra_video_frame &frame) {
		if (!frame.valid() || frame.pts_us == property("pts").toLongLong()) return;
		m_image = frame.image;
		setProperty("pts", qint64(frame.pts_us));
		setProperty("made", frame.image.size());   /* as made, before it is fitted */
		update();
	}
	/* The subtitles showing now; `subtitle` is their words, a line each,
	 * so a test can read what is on screen. */
	void show_cues(std::vector<snurra_cue> cues) {
		auto same = [](const snurra_cue &a, const snurra_cue &b) {
			return a.start_us == b.start_us && a.lines == b.lines && a.image.cacheKey() == b.image.cacheKey();
		};
		if (cues.size() == m_cues.size() && std::equal(cues.begin(), cues.end(), m_cues.begin(), same)) return;
		m_cues = std::move(cues);
		QStringList words;
		for (const snurra_cue &cue : m_cues)
			for (const auto &line : cue.lines) {
				QString text;
				for (const snurra_subtitle_span &span : line) text += span.text;
				words << text;
			}
		setProperty("subtitle", words.join(QLatin1Char('\n')));
		update();
	}
	/* A word on screen for two seconds: which subtitles, or none. */
	void show_notice(const QString &notice) {
		m_notice = notice;
		m_notice_age.start();
		setProperty("notice", notice);
		update();
		QTimer::singleShot(NOTICE_MS + 50, this, [this] { update(); });
	}
	/* Where it is and how long, held on screen (mplayer's o) until
	 * cleared with an empty string. */
	void show_clock(const QString &clock) {
		if (clock == m_clock) return;
		m_clock = clock;
		setProperty("clock", clock);
		update();
	}
	/* Full screen, the pointer goes after a second still, as mplayer's
	 * does, and comes back when moved: it would sit on the film. */
	void hide_pointer_when_still() {
		setMouseTracking(true);
		m_pointer.setSingleShot(true);
		m_pointer.setInterval(POINTER_MS);
		QObject::connect(&m_pointer, &QTimer::timeout, this, [this] {
			setCursor(Qt::BlankCursor);
			if (m_corner) m_corner->hide();   /* a still film is the film alone */
		});
		m_pointer.start();
	}
	std::function<void()> on_leave;   /* full screen: Escape or a double-click */
	/* Full screen, a window of its own: every other key, to the player's
	 * window, as if pressed there -- or space, f and the rest do nothing
	 * once the picture fills the screen. */
	std::function<void(QKeyEvent *)> on_key;
	/* mplayer's wheel: a notch up forward, down back. */
	std::function<void(int direction)> on_wheel;
	/* What a drag brings, dropped here; drops are taken only with it. */
	std::function<void(const QStringList &)> on_drop;

protected:
	void paintEvent(QPaintEvent *) override {
		QPainter painter(this);
		painter.fillRect(rect(), Qt::black);
		if (m_image.isNull()) return;
		const QSize fitted = m_image.size().scaled(size(), Qt::KeepAspectRatio);
		const QRect at(QPoint((width() - fitted.width()) / 2, (height() - fitted.height()) / 2), fitted);
		painter.setRenderHint(QPainter::SmoothPixmapTransform);
		painter.drawImage(at, m_image);
		paint_cues(painter, at);
		if (!m_notice.isEmpty() && m_notice_age.isValid() && m_notice_age.elapsed() < NOTICE_MS) {
			QFont font = painter.font();
			font.setPixelSize(qMax(11, at.height() / 20));
			outlined(painter, at.topLeft() + QPointF(at.height() / 30.0, at.height() / 30.0 + font.pixelSize()),
			         { { m_notice, false, false } }, font);
		}
		if (!m_clock.isEmpty()) {
			QFont font = painter.font();
			font.setPixelSize(qMax(11, at.height() / 20));
			const double margin = at.height() / 30.0;
			const double wide = QFontMetricsF(font).horizontalAdvance(m_clock);
			outlined(painter, QPointF(at.right() + 1 - margin - wide, at.top() + margin + font.pixelSize()),
			         { { m_clock, false, false } }, font);
		}
	}
	void keyPressEvent(QKeyEvent *event) override {
		if (event->key() == Qt::Key_Escape && on_leave) on_leave();
		else if (on_key) on_key(event);
		else QWidget::keyPressEvent(event);
	}
	void mouseDoubleClickEvent(QMouseEvent *) override {
		if (on_leave) on_leave();
	}
	void dragEnterEvent(QDragEnterEvent *event) override {
		if (on_drop && snurra_drop_offers(event->mimeData())) event->acceptProposedAction();
	}
	void dropEvent(QDropEvent *event) override {
		const QStringList files = on_drop ? snurra_dropped(event->mimeData()) : QStringList();
		if (files.isEmpty()) return;
		event->acceptProposedAction();
		on_drop(files);
	}
	void wheelEvent(QWheelEvent *event) override {
		if (!on_wheel) return QWidget::wheelEvent(event);
		/* Whole notches, however a touchpad divides them. */
		m_wheel += event->angleDelta().y();
		for (; m_wheel >= 120; m_wheel -= 120) on_wheel(1);
		for (; m_wheel <= -120; m_wheel += 120) on_wheel(-1);
		event->accept();
	}
	void resizeEvent(QResizeEvent *event) override {
		place_corner();
		QWidget::resizeEvent(event);
	}
	void leaveEvent(QEvent *event) override {
		if (m_corner && !m_corner->underMouse()) m_corner->hide();
		QWidget::leaveEvent(event);
	}
	void mouseMoveEvent(QMouseEvent *event) override {
		if (m_corner) {
			m_corner->show();
			m_corner->raise();
			m_corner_shown.start();
		}
		if (m_pointer.interval() == POINTER_MS) {   /* full screen: hides when still */
			unsetCursor();
			m_pointer.start();
		}
		QWidget::mouseMoveEvent(event);
	}

private:
	void place_corner() {
		if (!m_corner) return;
		const QSize size = m_corner->sizeHint();
		const int margin = qMax(4, height() / 40);
		m_corner->move(width() - size.width() - margin, height() - size.height() - margin);
	}
	static constexpr int POINTER_MS = 1000;
	static constexpr int CORNER_MS = 2000;
	QTimer m_pointer;
	QTimer m_corner_shown;
	QToolButton *m_corner = nullptr;
	int m_wheel = 0;   /* eighths of a degree not yet a notch */
	static constexpr int NOTICE_MS = 2000;

	/* A line of styled runs, white on a black outline, its baseline at
	 * `at` from its left end; the width it took. */
	static double outlined(QPainter &painter, QPointF at, const std::vector<snurra_subtitle_span> &line, const QFont &base) {
		QPainterPath path;
		double x = at.x();
		for (const snurra_subtitle_span &span : line) {
			QFont font = base;
			font.setItalic(span.italic);
			font.setBold(span.bold);
			path.addText(QPointF(x, at.y()), font, span.text);
			x += QFontMetricsF(font).horizontalAdvance(span.text);
		}
		painter.save();
		painter.setRenderHint(QPainter::Antialiasing);
		painter.strokePath(path, QPen(Qt::black, qMax(2.0, base.pixelSize() / 7.0), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		painter.fillPath(path, Qt::white);
		painter.restore();
		return x - at.x();
	}

	/* A source line's runs broken at spaces into lines no wider than
	 * `room`; a word wider than that has a line of its own. */
	static std::vector<std::vector<snurra_subtitle_span>> wrapped(const std::vector<snurra_subtitle_span> &line, const QFont &base,
	                                                             double room) {
		std::vector<std::vector<snurra_subtitle_span>> lines(1);
		double used = 0;
		for (const snurra_subtitle_span &span : line) {
			QFont font = base;
			font.setItalic(span.italic);
			font.setBold(span.bold);
			const QFontMetricsF metrics(font);
			const QStringList words = span.text.split(QLatin1Char(' '));
			for (int i = 0; i < words.size(); ++i) {
				const QString word = words[i] + (i + 1 < words.size() ? QStringLiteral(" ") : QString());
				const double width = metrics.horizontalAdvance(word.trimmed());
				if (used > 0 && used + width > room) {
					lines.emplace_back();
					used = 0;
				}
				if (used == 0 && word.trimmed().isEmpty()) continue;
				auto &into = lines.back();
				if (!into.empty() && into.back().italic == span.italic && into.back().bold == span.bold) into.back().text += word;
				else into.push_back({ word, span.italic, span.bold });
				used += metrics.horizontalAdvance(word);
			}
		}
		for (auto &out : lines) {
			if (out.empty()) continue;
			QString &last = out.back().text;
			while (last.endsWith(QLatin1Char(' '))) last.chop(1);
		}
		return lines;
	}

	/* Text centred near the bottom of the picture (or its top, where the
	 * cue asks), at a size that follows the picture's; a picture cue where
	 * it was placed. */
	void paint_cues(QPainter &painter, const QRect &at) const {
		QFont font = painter.font();
		font.setPixelSize(qMax(11, at.height() / 15));
		const double margin = at.height() * 0.04;
		const double room = at.width() * 0.92;
		const double step = QFontMetricsF(font).lineSpacing();
		for (const snurra_cue &cue : m_cues) {
			if (!cue.image.isNull()) {
				const QRectF place(at.left() + cue.place.x() * at.width(), at.top() + cue.place.y() * at.height(),
				                   cue.place.width() * at.width(), cue.place.height() * at.height());
				painter.drawImage(place, cue.image);
				continue;
			}
			std::vector<std::vector<snurra_subtitle_span>> lines;
			for (const auto &line : cue.lines)
				for (auto &part : wrapped(line, font, room)) lines.push_back(std::move(part));
			const double height = step * lines.size();
			double y = cue.top ? at.top() + margin + QFontMetricsF(font).ascent()
			                   : at.bottom() - margin - height + QFontMetricsF(font).ascent();
			for (const auto &line : lines) {
				double width = 0;
				for (const snurra_subtitle_span &span : line) {
					QFont styled = font;
					styled.setItalic(span.italic);
					styled.setBold(span.bold);
					width += QFontMetricsF(styled).horizontalAdvance(span.text);
				}
				outlined(painter, QPointF(at.left() + (at.width() - width) / 2, y), line, font);
				y += step;
			}
		}
	}

	QImage m_image;
	std::vector<snurra_cue> m_cues;
	QString m_clock;
	QString m_notice;
	QElapsedTimer m_notice_age;
};

snurra_tile::snurra_tile(const snurra_tile_state &state, snurra_deck *deck, QWidget *parent)
    : QWidget(parent), m_state(state), m_deck(deck) {
	setObjectName(QStringLiteral("snurra-tile"));
	setAttribute(Qt::WA_StyledBackground);
	setStyleSheet(QStringLiteral(
	        "#snurra-tile { background: palette(window); border: 1px solid palette(mid); border-radius: 3px; }"));
	setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

	QVBoxLayout *column = new QVBoxLayout(this);
	column->setContentsMargins(4, 2, 4, 4);
	column->setSpacing(2);
	m_column = column;

	/* The upward drawer: the video, when the track has one. */
	m_above = new QWidget(this);
	m_above->setObjectName(QStringLiteral("snurra-drawer-above"));
	m_above->setAttribute(Qt::WA_StyledBackground);
	m_above->setStyleSheet(drawer_style(m_above->objectName()));
	m_above->hide();
	column->addWidget(m_above);

	QHBoxLayout *top = new QHBoxLayout;
	m_top = top;
	/* A stem's lane says which stem it is. */
	const QStringList stems = snurra_stem_names(state.source);
	const QString stem = state.audio_track >= 1 && state.audio_track <= stems.size() ? stems.at(state.audio_track - 1) : QString();
	const QString file_name = QFileInfo(state.source).fileName();
	const QString name = stem.isEmpty() ? file_name : file_name + QStringLiteral(" -- ") + stem;
	m_title = new QLabel(name.isEmpty() ? state.source : name, this);
	m_title_base = m_title->text();
	/* A mix's tracklist: chapters, read from a file here (a URL's or a
	 * host's bytes are not opened twice for it). */
	if (QFileInfo(state.source).isFile()) m_chapters = snurra_read_chapters(state.source);
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
	/* A fraction of this track is that much of its lane's time, at its
	 * tempo. Only in a group: alone there is no timeline to move along. */
	m_waveform->on_shift = [this](double fraction, bool done, bool snap) {
		if (!m_grouped) return;
		const double frames = fraction * double(length_frames()) / double(m_deck->tempo());
		emit shift_requested(this, qint64(snurra_frames_to_us(std::llround(frames), m_deck->rate())), done, snap);
	};
	column->addWidget(m_waveform, 1);

	/* The downward drawer: the controls (sec 3c). */
	m_controls = new QWidget(this);
	m_controls->setObjectName(QStringLiteral("snurra-controls"));
	m_controls->setAttribute(Qt::WA_StyledBackground);
	m_controls->setStyleSheet(drawer_style(m_controls->objectName()));
	QVBoxLayout *strip = new QVBoxLayout(m_controls);
	strip->setContentsMargins(2, 2, 2, 4);
	strip->setSpacing(2);
	QHBoxLayout *transport = new QHBoxLayout;
	m_play = new QToolButton(m_controls);
	m_play->setObjectName(QStringLiteral("snurra-play"));
	m_play->setAutoRaise(true);
	m_play->setCheckable(true);
	transport->addWidget(m_play);
	m_cue = new QToolButton(m_controls);
	m_cue->setObjectName(QStringLiteral("snurra-cue"));
	m_cue->setAutoRaise(true);
	m_cue->setText(tr("cue"));
	m_cue->setAccessibleName(tr("Cue"));
	m_cue->setToolTip(tr("Paused, set the cue here; playing, go back to it and stop"));
	transport->addWidget(m_cue);
	/* Jumps (the holder, 2026-11-12): one pair, whose step is the track's
	 * -- bars where it has a beat, else time or a share of it -- and says
	 * so on its face. Held, they go on; with Ctrl, they nudge. */
	m_back = new QToolButton(m_controls);
	m_forward = new QToolButton(m_controls);
	for (QToolButton *jumper : { m_back, m_forward }) {
		jumper->setAutoRaise(true);
		jumper->setAutoRepeat(true);
		jumper->setAutoRepeatDelay(400);
		jumper->setAutoRepeatInterval(150);
		transport->addWidget(jumper);
		connect(jumper, &QToolButton::clicked, this, [this, jumper] {
			const int direction = jumper == m_forward ? 1 : -1;
			if (QGuiApplication::keyboardModifiers() & Qt::ControlModifier) nudge(direction);
			else jump(direction, false);
		});
	}
	m_back->setObjectName(QStringLiteral("snurra-back"));
	m_forward->setObjectName(QStringLiteral("snurra-forward"));
	/* The envelope control: on, the waveform edits the level curve rather
	 * than seeking (sec 3b, step 5) -- pulled out with the controls, never
	 * shown by default (sec 3c). */
	m_shape = new QToolButton(m_controls);
	m_shape->setObjectName(QStringLiteral("snurra-envelope"));
	m_shape->setAutoRaise(true);
	m_shape->setCheckable(true);
	m_shape->setText(QStringLiteral("~"));
	m_shape->setAccessibleName(tr("Edit the envelope"));
	m_shape->setToolTip(m_shape->accessibleName());
	m_position = new QSlider(Qt::Horizontal, m_controls);
	m_position->setAccessibleName(tr("Position"));
	m_position->setRange(0, int(qMax<qint64>(1, state.duration_us / 100000)));
	transport->addStretch(1);
	strip->addLayout(transport);
	/* The position, a row of its own: beside the buttons it was squeezed
	 * to nothing. */
	/* The time beside it, as mplayer shows it: where, of how long. */
	QHBoxLayout *where = new QHBoxLayout;
	where->addWidget(m_position, 1);
	m_time = new QLabel(m_controls);
	m_time->setObjectName(QStringLiteral("snurra-time"));
	where->addWidget(m_time);
	/* The envelope's toggle by the position, whose slider gives way: in
	 * the buttons' row, with a beat ("4 bars"), a phone's deck gave the
	 * jumps 44 of the 53 they ask. */
	where->addWidget(m_shape);
	strip->addLayout(where);
	m_level = new QSlider(Qt::Horizontal, m_controls);
	m_level->setAccessibleName(tr("Level"));
	m_level->setRange(0, 100);
	m_level->setValue(state.level);
	/* The level, its icon beside it: three bare sliders read alike. */
	QHBoxLayout *loud = new QHBoxLayout;
	QLabel *speaker = new QLabel(m_controls);
	const int icon = fontMetrics().height();
	speaker->setPixmap(style()->standardIcon(QStyle::SP_MediaVolume).pixmap(icon, icon));
	speaker->setToolTip(tr("Level"));
	loud->addWidget(speaker);
	loud->addWidget(m_level, 1);
	strip->addLayout(loud);
	/* The tempo, +-16 % in tenths of a per cent -- the holder's 12 to 16,
	 * "8 % was never enough"; Mixxx offers 16 -- and key lock: off, as a
	 * turntable, the pitch moves with the tempo; on, it stays (sec 4).
	 * Dragged, it snaps to the middle; a double-click puts it back. */
	QHBoxLayout *speed = new QHBoxLayout;
	m_tempo = new QSlider(Qt::Horizontal, m_controls);
	m_tempo->setObjectName(QStringLiteral("snurra-tempo"));
	m_tempo->setAccessibleName(tr("Tempo"));
	m_tempo->setToolTip(tr("Tempo; a double-click puts it back"));
	m_tempo->setRange(-TEMPO_RANGE, TEMPO_RANGE);
	m_tempo->installEventFilter(this);
	speed->addWidget(m_tempo, 1);
	m_tempo_label = new QLabel(m_controls);
	m_tempo_label->setMinimumWidth(fontMetrics().horizontalAdvance(QStringLiteral("-16.0 %")));
	speed->addWidget(m_tempo_label);
	/* The beat and the key as heard, as a DJ deck's head shows them: the
	 * BPM at this tempo, the key moved with it unless locked. */
	m_beat_label = new QLabel(m_controls);
	m_beat_label->setObjectName(QStringLiteral("snurra-beat"));
	speed->addWidget(m_beat_label);
	/* The key and the sync on a row of their own: on one row with the
	 * tempo, a phone's deck, 186 wide, clipped the buttons' words. */
	QHBoxLayout *keys = new QHBoxLayout;
	m_key_lock = new QToolButton(m_controls);
	m_key_lock->setObjectName(QStringLiteral("snurra-key-lock"));
	m_key_lock->setAutoRaise(true);
	m_key_lock->setCheckable(true);
	m_key_lock->setText(tr("key"));
	m_key_lock->setAccessibleName(tr("Key lock"));
	m_key_lock->setToolTip(tr("Key lock: the pitch stays as the tempo moves"));
	keys->addWidget(m_key_lock);
	/* The key shift, a DJ's key knob: semitones, 0 at the top, an octave
	 * either way; a double-click puts it back. */
	m_key_shift = new QDial(m_controls);
	m_key_shift->setObjectName(QStringLiteral("snurra-key-shift"));
	m_key_shift->setAccessibleName(tr("Key shift"));
	m_key_shift->setToolTip(tr("Key shift, in semitones; a double-click puts it back"));
	m_key_shift->setRange(-12, 12);
	m_key_shift->setNotchesVisible(true);
	m_key_shift->setFixedSize(fontMetrics().height() * 2, fontMetrics().height() * 2);
	m_key_shift->installEventFilter(this);
	keys->addWidget(m_key_shift);
	/* Key sync: the key moved to the leader's, as Traktor's key sync. */
	m_key_sync = new QToolButton(m_controls);
	m_key_sync->setObjectName(QStringLiteral("snurra-key-sync"));
	m_key_sync->setAutoRaise(true);
	m_key_sync->setText(tr("in key"));
	m_key_sync->setAccessibleName(tr("Key sync"));
	m_key_sync->setToolTip(tr("Move this tile's key to another playing tile's, the smallest way"));
	m_key_sync->setEnabled(false);
	keys->addWidget(m_key_sync);
	/* Sync: this tile's beat to another playing one's, once both have a
	 * grid (sec 4). */
	m_sync = new QToolButton(m_controls);
	m_sync->setObjectName(QStringLiteral("snurra-sync"));
	m_sync->setAutoRaise(true);
	m_sync->setText(tr("sync"));
	m_sync->setAccessibleName(tr("Sync"));
	m_sync->setToolTip(tr("Match this tile's beat to another playing tile's: its tempo, then its phase"));
	m_sync->setEnabled(false);
	keys->addWidget(m_sync);
	keys->addStretch(1);
	strip->addLayout(speed);
	strip->addLayout(keys);
	/* The EQ, a DJ's three bands: 0 dB at the top of the dial, as on a
	 * mixer -- the left half down by 2 dB a step to -22 and, at its end,
	 * the band killed, as an isolator's; the right up to +6 by 0.5. A
	 * double-click puts one back. */
	QHBoxLayout *bands = new QHBoxLayout;
	const QString band_names[3] = { tr("lo"), tr("mid"), tr("hi") };
	const char *band_objects[3] = { "snurra-eq-low", "snurra-eq-mid", "snurra-eq-high" };
	for (int band = 0; band < 3; ++band) {
		QLabel *name = new QLabel(band_names[band], m_controls);
		snurra_mute(name);
		m_eq[band] = new QDial(m_controls);
		m_eq[band]->setObjectName(QLatin1String(band_objects[band]));
		m_eq[band]->setAccessibleName(tr("EQ %1").arg(band_names[band]));
		m_eq[band]->setToolTip(tr("EQ %1, killed at the left end to +6 dB; a double-click puts it back").arg(band_names[band]));
		m_eq[band]->setRange(-12, 12);
		m_eq[band]->setNotchesVisible(true);
		const int side = fontMetrics().height() * 2;
		m_eq[band]->setFixedSize(side, side);
		m_eq[band]->installEventFilter(this);
		bands->addWidget(name);
		bands->addWidget(m_eq[band]);
		connect(m_eq[band], &QDial::valueChanged, this, [this] {
			auto db = [](int step) { return step <= -12 ? SNURRA_EQ_KILL : step < 0 ? 2.0f * float(step) : 0.5f * float(step); };
			set_eq(db(m_eq[0]->value()), db(m_eq[1]->value()), db(m_eq[2]->value()));
		});
	}
	bands->addStretch(1);
	strip->addLayout(bands);
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
	connect(m_play, &QToolButton::toggled, this, &snurra_tile::request_playing);
	connect(m_shape, &QToolButton::toggled, this, [this](bool on) { m_waveform->set_editing(on); });
	m_waveform->on_envelope = [this](const QList<QPointF> &points) {
		QList<QPair<qint64, float>> envelope;
		for (const QPointF &point : points)
			envelope << qMakePair(qint64(point.x() * double(m_state.duration_us)), float(point.y()));
		set_envelope(envelope);
	};
	connect(m_position, &QSlider::sliderMoved, this, [this](int tenths) { seek(qint64(tenths) * 100000); });
	connect(m_tempo, &QSlider::valueChanged, this, [this](int tenths) {
		/* Held near the middle while dragged, it is the middle: a pitch
		 * fader's centre is felt, not found to the tenth by eye. Keys
		 * and the speed keys still set any tenth. */
		if (m_tempo->isSliderDown() && tenths != 0 && std::abs(tenths) <= TEMPO_SNAP) tenths = 0;
		set_tempo(1.0f + float(tenths) / 1000.0f);
	});
	connect(m_key_lock, &QToolButton::toggled, this, &snurra_tile::set_key_lock);
	connect(m_key_shift, &QDial::valueChanged, this, &snurra_tile::set_key_shift);
	connect(m_key_sync, &QToolButton::clicked, this, &snurra_tile::request_key_sync);
	connect(m_sync, &QToolButton::clicked, this, &snurra_tile::request_sync);
	connect(m_cue, &QToolButton::clicked, this, &snurra_tile::cue);
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
	set_envelope(state.envelope);
	set_lane_start_us(state.lane_start_us);
	show_cue();
	set_tempo(state.tempo);
	set_key_lock(state.key_lock);
	set_key_shift(state.key_shift);
	set_eq(state.eq_db[0], state.eq_db[1], state.eq_db[2]);
	tick();

	/* A URL (mplayer's http://...) is played and its picture shown, but
	 * not read through again for a waveform and a beat grid: a radio
	 * stream has no end to read to, and a file would be fetched twice
	 * more. */
	const bool url = snurra_is_url(state.source);
	if (!url) read_peaks(nullptr);
	if (QFileInfo(state.source).isFile() || url) read_video(state.controls);
	if (QFileInfo(state.source).isFile()) read_beats(nullptr);
	show_jumps();
	/* Decided on from the next tick: the constructor's ran before the
	 * picture's reader existed -- and a host's tile gets its readers
	 * only after construction (embedded) -- so a picture alone read as a
	 * failure. */
	QMetaObject::invokeMethod(this, [this] { m_settled = true; }, Qt::QueuedConnection);
}

/* The beat grid, read on a thread of its own from the file the state
 * names, or from bytes a host allows a second read of (embed.h). */
void snurra_tile::read_beats(std::shared_ptr<snurra_bytes> bytes) {
	QPointer<snurra_tile> self = this;
	auto done = [self](const snurra_beat_grid &grid) {
		QMetaObject::invokeMethod(
		        self, [self, grid] {
			        if (self) self->set_beats(grid);
		        },
		        Qt::QueuedConnection);
	};
	m_beats.reset();
	if (bytes) m_beats = std::make_unique<snurra_beats>(std::move(bytes), m_state.source, m_deck->rate(), done);
	else m_beats = std::make_unique<snurra_beats>(m_state.source, m_deck->rate(), done, m_state.audio_track);
}

void snurra_tile::set_beats(const snurra_beat_grid &grid) {
	m_grid = grid;
	show_beat();
	if (m_sync) m_sync->setEnabled(grid.valid() && (m_grouped || m_sync_available));
	if (m_key_sync) m_key_sync->setEnabled(grid.key >= 0 && (m_grouped || m_sync_available));
	const double length = double(length_frames());
	if (grid.valid() && length > 0) m_waveform->set_beats(grid.first / length, grid.period / length);
	else m_waveform->set_beats(0, 0);
	show_jumps();
}

/*
 * The step, the holder's (2026-11-12): on a track with a beat, 4 bars, or
 * 16 large -- a bar four beats, so a jump keeps the place in the beat;
 * else under ten minutes 10 s, or 60 s large, mplayer's; else 5 %, or
 * 20 % large, of the whole.
 */
qint64 snurra_tile::jump_us(bool large) const {
	if (m_grid.valid()) {
		const int64_t frames = std::llround((large ? 64 : 16) * m_grid.period);
		return qint64(snurra_frames_to_us(frames, m_deck->rate()));
	}
	constexpr qint64 SECOND = 1000000;
	if (m_state.duration_us < 600 * SECOND) return (large ? 60 : 10) * SECOND;
	return m_state.duration_us * (large ? 20 : 5) / 100;
}

QString snurra_tile::jump_step(bool large) const {
	if (m_grid.valid()) return large ? tr("16 bars") : tr("4 bars");
	if (m_state.duration_us < 600 * qint64(1000000)) return large ? tr("60 s") : tr("10 s");
	return large ? tr("20 %") : tr("5 %");
}

void snurra_tile::jump(int direction, bool large) {
	seek_by(direction * jump_us(large));
}

/* 10 ms, this tile alone: in a window, its lane against the others. */
void snurra_tile::nudge(int direction) {
	constexpr qint64 NUDGE_US = 10000;
	if (m_grouped) emit nudge_requested(this, direction * NUDGE_US);
	else seek_by(direction * NUDGE_US);
}

void snurra_tile::show_jumps() {
	const QString step = jump_step(false);
	/* Arrow and step close-set (and the controls' sides 2 px, and the
	 * envelope's toggle on the position's row): a phone's deck, 178 wide,
	 * gave the jumps 42 of the 44 they ask. */
	m_back->setText(QStringLiteral("\u2039") + step);
	m_forward->setText(step + QStringLiteral("\u203a"));
	m_back->setAccessibleName(tr("Back %1").arg(step));
	m_forward->setAccessibleName(tr("Forward %1").arg(step));
	const QString nudge = tr("; with Ctrl, a 10 ms nudge");
	m_back->setToolTip(m_back->accessibleName() + nudge);
	m_forward->setToolTip(m_forward->accessibleName() + nudge);
}

/*
 * The upward drawer, for a track with a picture (project.md sec 3b): the
 * picture, as wide as the tile and as tall as its shape makes it, with a
 * maximize button; the drawer's own button on the title opens and closes
 * it. It opens with the controls -- the first tile's -- and the others
 * keep theirs closed, so a screen of tiles is not a screen of videos. The
 * frame shown is the one for the deck's heard position, the clock the
 * sound is placed by, looked at each 16 ms while it shows.
 */
void snurra_tile::read_video(bool open, std::shared_ptr<snurra_bytes> bytes) {
	m_video = bytes ? std::make_unique<snurra_video>(std::move(bytes), m_state.source)
	                : std::make_unique<snurra_video>(m_state.source);
	QPointer<snurra_tile> self = this;
	m_video->set_on_open([self, open](bool has_video) {
		if (!has_video) return;
		QMetaObject::invokeMethod(
		        self, [self, open] {
			        if (self) self->build_video_drawer(open);
		        },
		        Qt::QueuedConnection);
	});
	/* Opened before the callback was set: asked again, here. */
	if (m_video->has_video()) QMetaObject::invokeMethod(this, [this, open] { build_video_drawer(open); }, Qt::QueuedConnection);
}

void snurra_tile::build_video_drawer(bool open) {
	if (m_view || !m_video) return;
	QVBoxLayout *drawer = new QVBoxLayout(m_above);
	drawer->setContentsMargins(2, 2, 2, 2);
	m_view = new snurra_video_view(m_above);
	m_view->on_wheel = [this](int direction) { jump(direction, false); };
	drawer->addWidget(m_view);
	m_view->set_corner(false, [this] { set_full_screen(!m_full); });
	m_maximize = m_view->corner();
	m_above->installEventFilter(this);

	m_video_toggle = new QToolButton(this);
	m_video_toggle->setObjectName(QStringLiteral("snurra-video"));
	m_video_toggle->setAutoRaise(true);
	m_video_toggle->setCheckable(true);
	m_video_toggle->setFocusPolicy(Qt::NoFocus);
	m_video_toggle->setText(QStringLiteral("\u25b4"));
	m_video_toggle->setAccessibleName(tr("Show the video"));
	m_video_toggle->setToolTip(m_video_toggle->accessibleName());
	m_top->insertWidget(1, m_video_toggle);
	connect(m_video_toggle, &QToolButton::toggled, this, &snurra_tile::set_video_open);

	m_frames = new QTimer(this);
	m_frames->setInterval(16);
	connect(m_frames, &QTimer::timeout, this, &snurra_tile::show_video_frame);
	find_subtitles();
	set_video_open(open);
	if (m_full_wanted) set_full_screen(true);
}

/*
 * The subtitle tracks: a .srt beside the file first, then the file's own
 * streams. Shown from the start where there is a file beside it -- put
 * there to be shown -- or a stream marked default or forced; otherwise
 * none until v or j asks, so a film carrying twenty languages does not
 * open on the first of them. mpv chooses so too.
 */
void snurra_tile::find_subtitles() {
	m_subtitle_tracks.clear();
	m_subtitle = -1;
	QStringList files = m_subtitle_files;
	if (QFileInfo(m_state.source).isFile())
		for (const QString &path : snurra_srt_beside(m_state.source))
			if (!files.contains(path)) files << path;
	for (const QString &path : files) {
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024 * 1024) continue;
		auto track = std::make_shared<snurra_subtitles>(QFileInfo(path).fileName());
		for (snurra_cue &cue : snurra_read_srt(file.readAll())) track->add(std::move(cue));
		if (track->size() > 0) m_subtitle_tracks.push_back(track);
	}
	const size_t beside = m_subtitle_tracks.size();
	for (const auto &track : m_video->subtitle_tracks()) m_subtitle_tracks.push_back(track);
	if (beside > 0) m_subtitle = 0;
	else if (m_video->subtitle_default() >= 0) m_subtitle = m_video->subtitle_default();
}

void snurra_tile::set_subtitle_files(const QStringList &paths) {
	m_subtitle_files = paths;
	if (m_view) find_subtitles();
}

QString snurra_tile::subtitles_name() const {
	if (!m_subtitles_shown || m_subtitle < 0 || m_subtitle >= int(m_subtitle_tracks.size())) return QString();
	return m_subtitle_tracks[size_t(m_subtitle)]->name();
}

void snurra_tile::say_subtitles() {
	const QString name = subtitles_name();
	const QString notice = m_subtitle_tracks.empty() ? tr("No subtitles")
	                       : name.isEmpty()           ? tr("Subtitles off")
	                                                  : tr("Subtitles: %1").arg(name);
	if (m_view) m_view->show_notice(notice);
	if (m_full) static_cast<snurra_video_view *>(m_full.data())->show_notice(notice);
	show_video_frame();
}

/* mplayer's v: shown or hidden; with none chosen, the first. */
void snurra_tile::toggle_subtitles() {
	if (!m_view) return;
	if (m_subtitle < 0 && !m_subtitle_tracks.empty()) {
		m_subtitle = 0;
		m_subtitles_shown = true;
	} else {
		m_subtitles_shown = !m_subtitles_shown;
	}
	say_subtitles();
}

void snurra_tile::delay_subtitles(qint64 us) {
	if (!m_view) return;
	m_subtitle_delay += us;
	const QString notice = tr("Subtitle delay: %1 s").arg(m_subtitle_delay / 1e6, 0, 'f', 1);
	if (m_view) m_view->show_notice(notice);
	if (m_full) static_cast<snurra_video_view *>(m_full.data())->show_notice(notice);
	show_video_frame();
}

/* mplayer's + and -: the sound later (positive) or earlier than the
 * picture, as its -delay -- so the picture shown is the one for that much
 * after what is heard, and the subtitles, which belong to the picture,
 * with it. The sound itself is not moved: it is the clock, and in a mix
 * what the other tracks are in step with. */
void snurra_tile::delay_sound(qint64 us) {
	if (!m_view) return;
	m_sound_delay += us;
	const QString notice = tr("Sound delay: %1 s").arg(m_sound_delay / 1e6, 0, 'f', 1);
	if (m_view) m_view->show_notice(notice);
	if (m_full) static_cast<snurra_video_view *>(m_full.data())->show_notice(notice);
	show_video_frame();
}

void snurra_tile::toggle_clock() {
	m_clock = !m_clock;
	show_video_frame();
}

void snurra_tile::next_audio_track() {
	const QStringList tracks = m_deck->audio_tracks();
	if (tracks.size() < 2) return;
	const int next = (m_deck->audio_track() + 1) % int(tracks.size());
	m_deck->choose_audio_track(next);
	seek_by(0);
	const QString notice = tr("Sound: %1").arg(tracks[next]);
	if (m_view) m_view->show_notice(notice);
	if (m_full) static_cast<snurra_video_view *>(m_full.data())->show_notice(notice);
}

/* mplayer's j: the next track, and after the last, none. */
void snurra_tile::next_subtitles() {
	if (!m_view) return;
	m_subtitles_shown = true;
	m_subtitle = m_subtitle + 1 < int(m_subtitle_tracks.size()) ? m_subtitle + 1 : -1;
	say_subtitles();
}

void snurra_tile::set_video_open(bool open) {
	if (!m_view) return;
	m_video_toggle->blockSignals(true);
	m_video_toggle->setChecked(open);
	m_video_toggle->blockSignals(false);
	fit_video_drawer();
	/* Said before it shows: showing lays the window out at once, and the
	 * window keeps the tiles still by where they were before. */
	emit drawers_changed(this);
	m_above->setVisible(open);
	if (open || m_full) {
		m_frames->start();
		show_video_frame();
	} else {
		m_frames->stop();
	}
}

bool snurra_tile::video_open() const { return m_above && m_above->isVisibleTo(m_above->parentWidget()) && m_view; }

/* As tall as the picture's shape at the drawer's width. */
void snurra_tile::fit_video_drawer() {
	if (!m_view || !m_video) return;
	const QSize picture = m_video->size();
	if (picture.isEmpty()) return;
	const int width = qMax(1, m_above->width() > 1 ? m_above->width() : this->width());
	const int height = int(int64_t(width - 4) * picture.height() / picture.width()) + 4;
	if (m_above->height() != height || m_above->minimumHeight() != height) {
		m_above->setFixedHeight(height);
		emit drawers_changed(this);
	}
}

/*
 * The screen saver put off while a film plays, as mplayer's heartbeat does:
 * on X11 the server's idle time reset (ForceScreenSaver, Reset), which is
 * what its own saver and DPMS count from, and what lockers like TDE's and
 * xautolock read -- so it holds whatever the desktop, where a D-Bus
 * inhibit holds only on desktops that serve one. libxcb is resolved when
 * first needed, on Qt's own connection, so nobody linking snurra builds
 * against it; nothing on Wayland, offscreen or Android.
 */
static void keep_screen_awake() {
#if QT_CONFIG(xcb)
	auto *x11 = qGuiApp ? qGuiApp->nativeInterface<QNativeInterface::QX11Application>() : nullptr;
	if (!x11 || !x11->connection()) return;
	struct cookie {
		unsigned int sequence;   /* xcb_void_cookie_t */
	};
	using force_t = cookie (*)(xcb_connection_t *, uint8_t);
	using flush_t = int (*)(xcb_connection_t *);
	static const force_t force = reinterpret_cast<force_t>(QLibrary::resolve(QStringLiteral("xcb"), 1, "xcb_force_screen_saver"));
	static const flush_t flush = reinterpret_cast<flush_t>(QLibrary::resolve(QStringLiteral("xcb"), 1, "xcb_flush"));
	if (!force || !flush) return;
	force(x11->connection(), 0);   /* XCB_SCREEN_SAVER_RESET */
	flush(x11->connection());
#endif
}

void snurra_tile::show_video_frame() {
	if (!m_video) return;
	/* Each ten seconds while a picture plays where it can be seen. */
	constexpr qint64 AWAKE_MS = 10000;
	if (m_state.playing && (m_full || video_open()) && (!m_awake.isValid() || m_awake.elapsed() >= AWAKE_MS)) {
		keep_screen_awake();
		m_awake.start();
	}
	/* Made as wide as it is shown -- the picture fitted in the drawer or
	 * on the full screen, the wider, in device pixels -- so it is drawn
	 * one to one. The view's width alone made a 4K film 3440 wide on a
	 * 3440 x 1440 screen, where it fills 2560, and every paint scaled it
	 * down again (the holder, 2026-11-13: full screen still poor). */
	const QSize film = m_video->size();
	auto fitted_width = [&film](const QWidget *view) {
		if (film.isEmpty()) return int(std::lround(view->width() * view->devicePixelRatioF()));
		const double width = std::min(double(view->width()), double(view->height()) * film.width() / film.height());
		return int(std::lround(width * view->devicePixelRatioF()));
	};
	int shown = 0;
	if (m_view && video_open()) shown = fitted_width(m_view);
	if (m_full) shown = std::max(shown, fitted_width(m_full));
	if (shown > 0) m_video->set_shown_width(shown);
	const qint64 heard = qint64(snurra_frames_to_us(m_deck->position_at(snurra_clock_us()), m_deck->rate()));
	const qint64 picture = heard + m_sound_delay;
	const snurra_video_frame frame = m_video->frame_at(picture);
	const QString name = subtitles_name();
	std::vector<snurra_cue> cues;
	if (!name.isEmpty()) cues = m_subtitle_tracks[size_t(m_subtitle)]->at(picture - m_subtitle_delay);
	/* mplayer's o: where it is and how long, as h:mm:ss, while asked for. */
	QString clock;
	if (m_clock) {
		auto text = [](qint64 us) {
			const qint64 seconds = qMax<qint64>(0, us) / 1000000;
			return QStringLiteral("%1:%2:%3")
			        .arg(seconds / 3600)
			        .arg(seconds / 60 % 60, 2, 10, QLatin1Char('0'))
			        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
		};
		clock = text(heard) + QStringLiteral(" / ") + text(m_state.duration_us);
	}
	if (m_view) {
		m_view->show_frame(frame);
		m_view->show_cues(cues);
		m_view->show_clock(clock);
	}
	if (m_full) {
		static_cast<snurra_video_view *>(m_full.data())->show_frame(frame);
		static_cast<snurra_video_view *>(m_full.data())->show_cues(cues);
		static_cast<snurra_video_view *>(m_full.data())->show_clock(clock);
	}
}

void snurra_tile::set_full_screen(bool full) {
	m_full_wanted = full;
	if (full == bool(m_full) || !m_view) return;
	if (!full) {
		m_full->deleteLater();
		m_full = nullptr;
		if (!video_open()) m_frames->stop();
		return;
	}
	snurra_video_view *view = new snurra_video_view;
	view->setAttribute(Qt::WA_DeleteOnClose);
	view->setWindowTitle(QFileInfo(m_state.source).fileName());
	view->on_leave = [this] { set_full_screen(false); };
	view->on_key = [this](QKeyEvent *event) {
		emit touched(this);
		QCoreApplication::sendEvent(window(), event);
	};
	view->on_wheel = [this](int direction) { jump(direction, false); };
	view->set_corner(true, [this] { set_full_screen(false); });
	if (isSignalConnected(QMetaMethod::fromSignal(&snurra_tile::files_dropped))) {
		view->on_drop = [this](const QStringList &files) { emit files_dropped(files); };
		view->setAcceptDrops(true);
	}
	connect(this, &QObject::destroyed, view, &QObject::deleteLater);
	m_full = view;
	view->hide_pointer_when_still();
	view->showFullScreen();
	m_frames->start();
	show_video_frame();
}

/* The waveform's peaks, read from a source of their own and handed over on
 * this thread -- to the waveform, which a queued call never reaches once it
 * has gone. From `bytes` when given, else from what the state names. */
void snurra_tile::read_peaks(std::shared_ptr<snurra_bytes> bytes) {
	constexpr int COLUMNS = 1024;
	QPointer<snurra_waveform> waveform = m_waveform;
	auto piece = [waveform](int first, const std::vector<float> &values) {
		QMetaObject::invokeMethod(
		        waveform, [waveform, first, values] {
			        if (waveform) waveform->set_peaks(COLUMNS, first, values);
		        },
		        Qt::QueuedConnection);
	};
	m_peaks.reset();   /* the last reader stopped before the next begins */
	if (bytes) m_peaks = std::make_unique<snurra_peaks>(std::move(bytes), m_state.source, m_deck->rate(), COLUMNS, piece);
	else m_peaks = std::make_unique<snurra_peaks>(m_state.source, m_deck->rate(), COLUMNS, piece, m_state.audio_track);
}

snurra_tile::~snurra_tile() {
	m_peaks.reset();   /* before the deck can go: the waveform first */
	m_video.reset();   /* its reader, joined */
	m_beats.reset();
	delete m_full.data();
	/* Drawers a window took out are the tile's still, and go with it. */
	if (m_drawers_out) {
		delete m_above.data();
		delete m_controls.data();
	}
	if (m_owner) m_owner->retire(m_deck);
}

snurra_tile *snurra_tile::embedded(snurra_engine &engine, const snurra_tile_state &state,
                                   std::shared_ptr<snurra_bytes> bytes, QWidget *parent,
                                   std::shared_ptr<snurra_bytes> peak_bytes) {
	snurra_deck::start_t start;
	start.source = state.source;
	start.bytes = std::move(bytes);
	start.rate = engine.rate();
	start.fallback_length = snurra_us_to_frames(state.duration_us, start.rate);
	start.anchor_frame = snurra_us_to_frames(state.position_us, start.rate);
	start.anchor_us = state.sampled_at_us;
	start.playing = state.playing;
	start.level = float(state.level) / 100;
	start.audio_track = state.audio_track;
	snurra_tile *tile = new snurra_tile(state, engine.add(start), parent);
	tile->m_owner = &engine;
	/* A second read the host allows: the waveform's, the picture's and
	 * the beats', each on its own thread. */
	if (peak_bytes) {
		tile->read_peaks(peak_bytes);
		tile->read_video(state.controls, peak_bytes);
		tile->read_beats(peak_bytes);
	}
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
	if (m_deck->audio_track() >= 0) now.audio_track = m_deck->audio_track();
	return now;
}

void snurra_tile::step_frame() {
	if (!m_video || !m_video->has_video()) return;
	if (m_state.playing) request_playing(false);
	const qint64 picture = qint64(snurra_frames_to_us(m_deck->position_at(snurra_clock_us()), m_deck->rate())) + m_sound_delay;
	const snurra_video_frame shown = m_video->frame_at(picture);
	/* A little into the frame, not on its first microsecond: a position
	 * goes through frames of sound and back, and rounding it down by one
	 * would show the frame before. */
	seek(m_video->next_frame_after(shown.valid() ? shown.pts_us : picture) + 1000 - m_sound_delay);
}

void snurra_tile::seek(qint64 position_us) {
	if (m_grouped) {
		emit seek_requested(this, qBound<qint64>(0, position_us, m_state.duration_us));
		return;
	}
	const int64_t frame = us_to_frames(qBound<qint64>(0, position_us, m_state.duration_us), m_deck->rate());
	/* Playing, as a window's group does (start_group_at): the old place on
	 * until a moment a lead ahead, and the new one taken as far on, so a
	 * jump of bars lands on the beat with no gap (a DJ player's decks). */
	if (m_state.playing && m_owner) {
		const qint64 now = snurra_clock_us();
		const qint64 at = now + m_owner->latency_us() + 50000;
		const int64_t on = int64_t(std::llround(double(us_to_frames(at - now, m_deck->rate())) * m_deck->tempo()));
		m_deck->start_at(std::min(frame + on, us_to_frames(m_state.duration_us, m_deck->rate())), at);
	} else {
		m_deck->seek(frame);
	}
	tick();
}

void snurra_tile::set_eq(float low_db, float mid_db, float high_db) {
	const float asked[3] = { low_db, mid_db, high_db };
	for (int band = 0; band < 3; ++band) {
		const float db = std::isnan(asked[band]) ? 0.0f : asked[band] < -24.0f ? SNURRA_EQ_KILL : std::min(asked[band], 6.0f);
		m_state.eq_db[band] = db;
		if (m_eq[band]) {
			m_eq[band]->blockSignals(true);
			/* -24 dB, kept by an older snurra, is the deepest step short of the kill. */
			m_eq[band]->setValue(db <= SNURRA_EQ_KILL ? -12 : db < 0 ? std::max(-11, int(std::lround(db / 2))) : int(std::lround(db * 2)));
			m_eq[band]->blockSignals(false);
		}
	}
	m_deck->set_eq(m_state.eq_db[0], m_state.eq_db[1], m_state.eq_db[2]);
}

void snurra_tile::cue() {
	if (m_state.playing) {
		if (m_state.cue_us < 0) return;
		request_playing(false);
		seek(m_state.cue_us);
		return;
	}
	m_state.cue_us = frames_to_us(frame_now(), m_deck->rate());
	show_cue();
}

int snurra_tile::chapter_at(qint64 us) const {
	int found = -1;
	for (size_t i = 0; i < m_chapters.size(); ++i)
		if (m_chapters[i].start_us <= us + 1000) found = int(i);
	return found;
}

QString snurra_tile::chapter_title() const {
	return m_chapter_shown >= 0 ? m_chapters[size_t(m_chapter_shown)].title : QString();
}

/* mplayer's ! and @: the chapter before or after the one playing, from
 * its start; before the first, the track's start. */
void snurra_tile::step_chapter(int direction) {
	if (m_chapters.empty()) return;
	const int now = chapter_at(frames_to_us(frame_now(), m_deck->rate()));
	const int to = now + direction;
	if (to >= int(m_chapters.size())) return;
	seek(to < 0 ? 0 : m_chapters[size_t(to)].start_us);
}

void snurra_tile::show_chapters() {
	QList<double> starts;
	if (m_state.duration_us > 0)
		for (const snurra_chapter &chapter : m_chapters) starts << double(chapter.start_us) / double(m_state.duration_us);
	m_waveform->set_chapters(starts);
}

void snurra_tile::show_cue() {
	m_waveform->set_cue(m_state.cue_us >= 0 && m_state.duration_us > 0 ? double(m_state.cue_us) / double(m_state.duration_us) : -1);
}

void snurra_tile::set_sync_available(bool available) {
	m_sync_available = available;
	if (m_sync) m_sync->setEnabled(m_grid.valid() && (m_grouped || m_sync_available));
	if (m_key_sync) m_key_sync->setEnabled(m_grid.key >= 0 && (m_grouped || m_sync_available));
}

/*
 * Sync (project.md sec 4): this tile's tempo set so its beat runs at the
 * leader's -- a grid's BPM at its tile's tempo; off by more than a DJ's
 * range, the double or the half is the beat meant, 87.5 and 175 being one
 * tempo -- and how far its phase is from the leader's now, the shorter way
 * round, for the caller to bend out. 0 where either has no beat.
 */
qint64 snurra_tile::match_beat_of(const snurra_tile *leader) {
	const snurra_beat_grid mine = beats();
	if (!leader || !mine.valid() || !leader->beats().valid()) return 0;
	const snurra_beat_grid theirs = leader->beats();
	const double target = theirs.bpm() * leader->tempo();
	double tempo = target / mine.bpm();
	for (double octave : { 2.0, 0.5 })
		if (std::abs(std::log2(tempo * octave)) < std::abs(std::log2(tempo))) tempo *= octave;
	set_tempo(float(tempo));
	auto phase = [](const snurra_beat_grid &grid, int64_t frame) {
		const double beats = (double(frame) - grid.first) / grid.period;
		return beats - std::floor(beats);
	};
	double shift = phase(theirs, leader->frame_now()) - phase(mine, frame_now());
	if (shift > 0.5) shift -= 1;
	if (shift < -0.5) shift += 1;
	return qint64(snurra_frames_to_us(std::llround(shift * mine.period), m_deck->rate()));
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

void snurra_tile::request_playing(bool playing) {
	if (!m_grouped) {
		set_playing(playing);
		return;
	}
	/* The button shows what is, not what was asked, until the group says. */
	m_play->blockSignals(true);
	m_play->setChecked(m_state.playing);
	m_play->blockSignals(false);
	emit play_requested(this, playing);
}

void snurra_tile::play_from(int64_t frame, qint64 at_us) {
	m_deck->fade(0, 0, 1, 1);
	m_deck->start_at(frame, at_us);   /* before playing: deck.h */
	set_playing(true);
	tick();
}

void snurra_tile::hold_at(int64_t frame) {
	set_playing(false);
	m_deck->fade(0, 0, 1, 1);
	m_deck->seek(frame);
	tick();
}

void snurra_tile::set_envelope(const QList<QPair<qint64, float>> &points) {
	m_state.envelope = points.mid(0, snurra_deck::ENVELOPE_POINTS);
	snurra_deck::envelope_t frames;
	for (const auto &point : m_state.envelope)
		frames.emplace_back(us_to_frames(point.first, m_deck->rate()), qBound(0.0f, point.second, 1.0f));
	m_deck->set_envelope(frames);
	show_envelope();
}

void snurra_tile::set_lane_start_us(qint64 us) {
	m_state.lane_start_us = qMax<qint64>(0, us);
	/* To a tenth: a start on a beat is finer than a second. */
	const qint64 tenths = (m_state.lane_start_us + 50000) / 100000;
	m_waveform->set_note(m_state.lane_start_us > 0 ? QStringLiteral("+%1:%2.%3")
	                                                         .arg(tenths / 600)
	                                                         .arg(tenths / 10 % 60, 2, 10, QLatin1Char('0'))
	                                                         .arg(tenths % 10)
	                                               : QString());
}

void snurra_tile::set_crossfade(double in, double out) {
	m_crossfade = qMakePair(in, out);
	m_waveform->set_crossfade(in, out);
}

/* On the waveform, as fractions of the track: again whenever its length
 * becomes known, since a fraction of a length not yet said means nothing. */
void snurra_tile::show_envelope() {
	QList<QPointF> points;
	if (m_state.duration_us > 0)
		for (const auto &point : m_state.envelope)
			points << QPointF(double(point.first) / double(m_state.duration_us), point.second);
	m_waveform->set_envelope(points);
}

/* m:ss, or h:mm:ss from an hour, as a player's clock reads. */
QString snurra_tile::clock_text(qint64 us) {
	const qint64 seconds = qMax<qint64>(0, us) / 1000000;
	const qint64 h = seconds / 3600;
	const qint64 m = seconds / 60 % 60;
	const qint64 s = seconds % 60;
	if (h > 0) return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
	return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

QString snurra_tile::drawer_style(const QString &name) {
	return QStringLiteral("#%1 { background: palette(window); border: 1px solid palette(mid); border-radius: 3px; }")
	        .arg(name);
}

void snurra_tile::take_drawers_out() {
	if (m_drawers_out) return;
	m_drawers_out = true;
	m_column->removeWidget(m_above);
	m_column->removeWidget(m_controls);
}

void snurra_tile::set_tempo(float tempo) {
	m_state.tempo = std::clamp(tempo, 0.25f, 4.0f);
	m_deck->set_tempo(m_state.tempo);
	const int tenths = int(std::lround((m_state.tempo - 1) * 1000));
	m_tempo->blockSignals(true);
	m_tempo->setValue(tenths);
	m_tempo->blockSignals(false);
	m_tempo_label->setText(QStringLiteral("%1%2 %").arg(tenths > 0 ? QStringLiteral("+") : QString()).arg(tenths / 10.0, 0, 'f', 1));
	show_beat();
}

/* The key as heard against the track's: its shift, and without key lock
 * the tempo's too -- 6 % faster is a semitone up. */
int snurra_tile::key_shift() const {
	return m_state.key_shift + (m_state.key_lock ? 0 : int(std::lround(12 * std::log2(double(m_state.tempo)))));
}

int snurra_tile::key_heard() const {
	return snurra_key_shifted(m_grid.key, key_shift());
}

bool snurra_tile::match_key_of(const snurra_tile *leader) {
	if (!leader || m_grid.key < 0) return false;
	const int theirs = leader->key_heard();
	if (theirs < 0) return false;
	const int by_tempo = key_shift() - m_state.key_shift;
	/* Each number is twelve semitones round, so one of these reaches it. */
	for (int shift : { 0, 1, -1, 2, -2, 3, -3, 4, -4, 5, -5, 6 })
		if (snurra_key_order(snurra_key_shifted(m_grid.key, by_tempo + shift)) / 2 == snurra_key_order(theirs) / 2) {
			set_key_shift(shift);
			return true;
		}
	return false;
}

void snurra_tile::show_beat() {
	if (!m_beat_label) return;
	QStringList said;
	if (m_grid.valid()) said << tr("%1 BPM").arg(m_grid.bpm() * double(m_state.tempo), 0, 'f', 1);
	const QString key = snurra_key_name(m_grid.key, key_shift());
	if (!key.isEmpty()) said << key.section(QLatin1Char(' '), 0, 0);
	m_beat_label->setText(said.join(QLatin1Char(' ')));
	m_beat_label->setToolTip(key.isEmpty() ? QString() : tr("Key %1 as heard").arg(key));
}

void snurra_tile::set_key_lock(bool on) {
	m_state.key_lock = on;
	m_deck->set_key_lock(on);
	m_key_lock->blockSignals(true);
	m_key_lock->setChecked(on);
	m_key_lock->blockSignals(false);
	show_beat();
}

void snurra_tile::set_key_shift(int semitones) {
	m_state.key_shift = std::clamp(semitones, -12, 12);
	m_deck->set_key_shift(m_state.key_shift);
	m_key_shift->blockSignals(true);
	m_key_shift->setValue(m_state.key_shift);
	m_key_shift->blockSignals(false);
	show_beat();
}

int64_t snurra_tile::frame_now() const {
	return m_deck->position_at(snurra_clock_us());
}

int64_t snurra_tile::length_frames() const {
	const int64_t length = m_deck->length();
	return length > 0 ? length : us_to_frames(m_state.duration_us, m_deck->rate());
}

void snurra_tile::set_controls(bool out) {
	m_state.controls = out;
	m_controls->setVisible(out);
	if (!out) m_shape->setChecked(false);   /* the envelope is edited only from its control */
	m_pull->blockSignals(true);
	m_pull->setChecked(out);
	m_pull->blockSignals(false);
	m_pull->setArrowType(out ? Qt::UpArrow : Qt::DownArrow);
	m_pull->setAccessibleName(out ? tr("Push the controls in") : tr("Pull the controls out"));
	m_pull->setToolTip(m_pull->accessibleName());
	/* Pushed back in, a tile alone stops: what plays is what can be
	 * stopped. In a window the group decides (snurra_node). */
	if (!out && m_state.playing && !m_grouped) set_playing(false);
	emit controls_changed(this);
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
		show_envelope();
		show_cue();
		show_chapters();
		set_beats(m_grid);
	}
	/* The chapter playing, said after the name: a mix's track. */
	if (!m_chapters.empty()) {
		const int now = chapter_at(frames_to_us(frame_now(), m_deck->rate()));
		if (now != m_chapter_shown) {
			m_chapter_shown = now;
			const QString title = now >= 0 ? m_chapters[size_t(now)].title : QString();
			m_title->setText(title.isEmpty() ? m_title_base : m_title_base + QStringLiteral(" -- ") + title);
		}
	}
	if (m_state.playing && m_deck->ended()) set_playing(false);
	/* No sound, and only then a failure if no picture either: a GIF or a
	 * screen recording has none, and was told it could not be played.
	 * The picture's reader opens later than the sound gives up, so wait
	 * for it to have looked. */
	const bool picture_known = m_settled && (!m_video || m_video->opened());
	const bool picture = m_video && m_video->has_video();
	if (!m_failed_said && m_deck->failed() && picture_known && picture) {
		/* A picture alone: not a failure, and as long as the picture --
		 * the stand-in silence's three minutes ran on past a 3 s GIF. */
		m_failed_said = true;
		if (m_video->length_us() > 0) m_deck->set_stand_in_length(us_to_frames(m_video->length_us(), m_deck->rate()));
	}
	if (!m_failed_said && m_deck->failed() && picture_known) {
		m_failed_said = true;
		/* Said, not stopped: the stand-in silence still behaves as a tile.
		 * From the event loop: a source that fails at once fails within
		 * the constructor's own tick, before anyone can be listening. */
		QMetaObject::invokeMethod(this, [this] { emit failed(this); }, Qt::QueuedConnection);
		/* And to the person, where they look: a silent tile with no word
		 * was all an unplayable file gave. */
		m_waveform->set_message(tr("snurra cannot play this: it is not sound or video it can read."));
	}
	const qint64 now = frames_to_us(m_deck->position_at(snurra_clock_us()), m_deck->rate());
	m_state.position_us = now;
	m_state.sampled_at_us = snurra_clock_us();
	if (!m_position->isSliderDown()) m_position->setValue(int(now / 100000));
	m_time->setText(clock_text(now) + QStringLiteral(" / ") + clock_text(m_state.duration_us));
	m_waveform->set_fraction(m_state.duration_us > 0 ? double(now) / m_state.duration_us : 0);
}

QSize snurra_tile::sizeHint() const {
	return QSize(fontMetrics().averageCharWidth() * 26, QWidget::sizeHint().height());
}

QSize snurra_tile::minimumSizeHint() const {
	return QSize(fontMetrics().averageCharWidth() * 14, QWidget::minimumSizeHint().height());
}

/* From where the deck is -- or was just asked to go -- not from the tile's
 * last tick: two jumps in a row add up. */
void snurra_tile::seek_by(qint64 delta_us) {
	seek(frames_to_us(frame_now(), m_deck->rate()) + delta_us);
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
	/* After the event, not in it: a height set inside the drawer's own
	 * resize is undone by its layout, which then handles the older size --
	 * the picture was left at half its drawer. */
	if (watched == m_above && event->type() == QEvent::Resize)
		QMetaObject::invokeMethod(this, [this] { fit_video_drawer(); }, Qt::QueuedConnection);
	if (watched == m_tempo && event->type() == QEvent::MouseButtonDblClick) {
		set_tempo(1);
		return true;
	}
	if (watched == m_key_shift && event->type() == QEvent::MouseButtonDblClick) {
		set_key_shift(0);
		return true;
	}
	for (QDial *dial : m_eq)
		if (watched == dial && event->type() == QEvent::MouseButtonDblClick) {
			dial->setValue(0);
			return true;
		}
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
