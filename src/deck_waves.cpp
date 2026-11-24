#include "snurra/deck_waves.h"

#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QMenu>
#include <QPainter>
#include <QSettings>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

snurra_deck_waves::snurra_deck_waves(QWidget *parent) : QWidget(parent) {
	setObjectName(QStringLiteral("snurra-deck-waves"));
	setAccessibleName(tr("The decks' waveforms"));
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	setAttribute(Qt::WA_OpaquePaintEvent);
	/* Scrolling: a repaint each frame while shown, as the playheads move. */
	QTimer *frame = new QTimer(this);
	frame->setInterval(16);
	/* Repainted only when something moved -- a deck's place, new peaks,
	 * a zoom: a still view costs nothing (the holder, 2026-11-25: "they
	 * still have bad performance"). */
	connect(frame, &QTimer::timeout, this, [this] {
		if (!isVisible()) return;
		bool moved = m_stale;
		m_last.resize(m_peaks.size(), -1);
		for (int i = 0; i < int(m_peaks.size()); ++i) {
			deck where;
			if (fill) fill(i, where);
			const double at = where.loaded ? where.seconds : -1;
			if (at != m_last[size_t(i)]) moved = true;
			m_last[size_t(i)] = at;
		}
		if (moved) update();
		m_stale = false;
	});
	frame->start();
	setCursor(Qt::OpenHandCursor);
	m_layout = layout_named(QSettings(QStringLiteral("snurra"), QStringLiteral("snurra")).value(QStringLiteral("decks/waveforms")).toString());
}

void snurra_deck_waves::set_deck_count(int count) {
	m_peaks.resize(size_t(std::max(0, count)));
	/* Never squeezed out by the decks below: enough to see a beat --
	 * stacked, a row a deck; side by side, one row; vertical, a lane
	 * tall enough to read ahead in. */
	setMinimumHeight(m_layout == stacked ? std::max(1, count) * 48 : m_layout == side_by_side ? 64 : 240);
	/* And never so tall it stops reading as a waveform: maximized, rows
	 * as high as the window made each a wall of colour (the holder,
	 * 2026-11-25, "don't look very good or readable maximized"). Vertical
	 * lanes run along the height, which they use. */
	const int line = fontMetrics().height();
	setMaximumHeight(m_layout == stacked ? std::max(1, count) * std::max(64, line * 7)
	                 : m_layout == side_by_side ? std::max(80, line * 8) : QWIDGETSIZE_MAX);
	updateGeometry();
	update();
}

void snurra_deck_waves::set_peaks(int deck, int first, const std::vector<float> &values) {
	if (deck < 0 || deck >= int(m_peaks.size()) || first < 0) return;
	std::vector<float> &peaks = m_peaks[size_t(deck)];
	if (peaks.size() < size_t(first) + values.size()) peaks.resize(size_t(first) + values.size(), 0.0f);
	std::copy(values.begin(), values.end(), peaks.begin() + first);
	if (m_pyramid.size() < m_peaks.size()) m_pyramid.resize(m_peaks.size());
	m_pyramid[size_t(deck)].clear();   /* built again when next drawn */
	m_stale = true;
}

void snurra_deck_waves::clear_peaks(int deck) {
	if (deck >= 0 && deck < int(m_peaks.size())) m_peaks[size_t(deck)].clear();
	if (deck >= 0 && deck < int(m_pyramid.size())) m_pyramid[size_t(deck)].clear();
	m_stale = true;
}

void snurra_deck_waves::set_span(double seconds) {
	/* From two seconds to ten minutes: the tracks that follow in view
	 * (the holder, 2026-11-25). */
	m_span = std::clamp(seconds, 2.0, 600.0);
	m_stale = true;
	update();
}

QColor snurra_deck_waves::colour_of(int deck) {
	/* Four hues far apart, A blue and B orange as most programs give them. */
	static const QColor HUES[4] = { QColor(0x3d, 0x9b, 0xe9), QColor(0xf0, 0x8a, 0x24), QColor(0x4c, 0xc0, 0x6a), QColor(0xc0, 0x6a, 0xe0) };
	return HUES[((deck % 4) + 4) % 4];
}

QSize snurra_deck_waves::sizeHint() const {
	return QSize(600, std::max(1, int(m_peaks.size())) * 64);
}

int snurra_deck_waves::lane_at(QPointF at, double &along) const {
	const int count = int(m_peaks.size());
	if (count == 0 || !rect().contains(at.toPoint())) return -1;
	if (m_layout == stacked) {
		along = at.x();
		return std::clamp(int(at.y() * count / std::max(1, height())), 0, count - 1);
	}
	const int deck = std::clamp(int(at.x() * count / std::max(1, width())), 0, count - 1);
	along = m_layout == vertical ? at.y() : m_layout == vertical_up ? height() - at.y() : at.x() - double(width()) * deck / count;
	return deck;
}

/*
 * A scratch by hand: pressed on a deck's lane, the hand is on its
 * platter; dragged, the record moves under the playhead with the hand --
 * along a row, left pulls it forward as playing does; down a vertical
 * lane, up does -- the drag's pixels turned to seconds by the zoom; let
 * go, it plays on. Traktor's and Serato's waveforms scratch so.
 */
void snurra_deck_waves::mousePressEvent(QMouseEvent *event) {
	double along = 0;
	const int deck = event->button() == Qt::LeftButton ? lane_at(event->position(), along) : -1;
	snurra_deck_waves::deck where;
	if (deck >= 0 && fill) fill(deck, where);
	if (deck < 0 || !where.loaded) {
		QWidget::mousePressEvent(event);
		return;
	}
	m_held = deck;
	m_held_from = along;
	m_held_seconds = where.seconds;
	setCursor(Qt::ClosedHandCursor);
	if (on_hold) on_hold(deck, true);
	if (on_hand) on_hand(deck, where.seconds);
	event->accept();
}

void snurra_deck_waves::mouseMoveEvent(QMouseEvent *event) {
	if (m_held < 0) {
		QWidget::mouseMoveEvent(event);
		return;
	}
	const int count = std::max(1, int(m_peaks.size()));
	const double lane_length = upright() ? height() : m_layout == stacked ? width() : double(width()) / count;
	const double along = m_layout == vertical      ? event->position().y()
	                     : m_layout == vertical_up ? height() - event->position().y()
	                     : m_layout == stacked     ? event->position().x()
	                                               : event->position().x() - lane_length * m_held;
	const double seconds = m_held_seconds + (m_held_from - along) / (lane_length / m_span);
	if (on_hand) on_hand(m_held, std::max(0.0, seconds));
	event->accept();
}

void snurra_deck_waves::mouseReleaseEvent(QMouseEvent *event) {
	if (m_held < 0) {
		QWidget::mouseReleaseEvent(event);
		return;
	}
	const int deck = m_held;
	m_held = -1;
	setCursor(Qt::OpenHandCursor);
	if (on_hold) on_hold(deck, false);
	event->accept();
}

void snurra_deck_waves::wheelEvent(QWheelEvent *event) {
	/* Up zooms in, as a map does. */
	set_span(m_span * std::pow(0.8, event->angleDelta().y() / 120.0));
	event->accept();
}

void snurra_deck_waves::paintEvent(QPaintEvent *) {
	QPainter painter(this);
	const QPalette &colours = palette();
	painter.fillRect(rect(), colours.color(QPalette::Base));
	const int count = int(m_peaks.size());
	if (count == 0 || width() < 2 || height() < 2) return;
	QColor line = colours.color(QPalette::Text);
	line.setAlphaF(0.15f);
	for (int i = 0; i < count; ++i) {
		/* Its lane: a row of the whole width stacked, else a column. */
		const QRect lane = m_layout == stacked ? QRect(0, height() * i / count, width(), height() * (i + 1) / count - height() * i / count)
		                                       : QRect(width() * i / count, 0, width() * (i + 1) / count - width() * i / count, height());
		if (i > 0) painter.fillRect(m_layout == stacked ? QRect(0, lane.top(), width(), 1) : QRect(lane.left(), 0, 1, height()), line);
		deck where;
		if (fill) fill(i, where);
		painter.save();
		/* A vertical lane is a horizontal one turned a quarter: time runs
		 * down it, what is to come below the playhead. */
		double along = lane.width(), across = lane.height();
		if (m_layout == vertical) {
			painter.translate(lane.right() + 1, lane.top());
			painter.rotate(90);
			along = lane.height();
			across = lane.width();
		} else if (m_layout == vertical_up) {
			/* The other quarter: time runs up the lane, what is to come
			 * above the playhead (the holder, 2026-11-25, "like other
			 * players especially if its used on a long screen"). */
			painter.translate(lane.left(), lane.bottom() + 1);
			painter.rotate(-90);
			along = lane.height();
			across = lane.width();
		} else {
			painter.translate(lane.topLeft());
		}
		paint_lane(painter, i, where, along, across);
		painter.restore();
	}
}

void snurra_deck_waves::paint_lane(QPainter &painter, int i, const deck &where, double along, double across) {
	const QColor text = palette().color(QPalette::Text);
	const double middle = along / 2.0;
	const double pixels_a_second = along / m_span;
	if (where.loaded) {
		/* The level of the pyramid whose entries are no wider than a
		 * pixel: one or two read for each, at any zoom. */
		const double per_pixel = COLUMNS_A_SECOND / pixels_a_second;
		const int level = per_pixel <= 2 ? 0 : int(std::floor(std::log2(per_pixel)));
		const std::vector<float> &peaks = level_of(i, level);
		const double scale = std::ldexp(1.0, level);
		/* Past the playhead the track is heard; ahead of it to come: the
		 * colour full ahead, dimmer behind. Each a batch of lines. */
		/* Opaque, mixed with the ground beforehand: a colour drawn at 45 %
		 * was blended at every pixel it covered. */
		const QColor ahead = colour_of(i);
		const QColor ground = palette().color(QPalette::Base);
		const QColor behind = QColor::fromRgbF(0.45f * ahead.redF() + 0.55f * ground.redF(), 0.45f * ahead.greenF() + 0.55f * ground.greenF(),
		                                       0.45f * ahead.blueF() + 0.55f * ground.blueF());
		const double centre = across / 2.0;
		const double half = (across - 4) / 2.0;
		/* Each part an outline filled in one go -- the rasterizer's spans
		 * -- rather than a line a pixel: the top edge left to right, then
		 * the bottom back. */
		std::vector<QPointF> heard, to_come;
		heard.reserve(size_t(along) / 2 + 2);
		to_come.reserve(size_t(along) / 2 + 2);
		for (int x = 0; x < int(along); ++x) {
			const double seconds = where.seconds + (x - middle) / pixels_a_second;
			if (seconds < 0) continue;
			const double from = seconds * COLUMNS_A_SECOND / scale;
			const double to = from + per_pixel / scale;
			if (int(from) >= int(peaks.size())) break;
			float loudest = 0;
			if (per_pixel / scale >= 1) {
				for (int c = int(from); c <= int(to) && c < int(peaks.size()); ++c)
					if (c >= 0) loudest = std::max(loudest, peaks[size_t(c)]);
			} else {
				/* Zoomed in past a column a pixel: from one column's peak
				 * to the next, not in steps (2026-11-25, "not granular"). */
				const int c = std::max(0, int(std::floor(from)));
				const int d = std::min(int(peaks.size()) - 1, c + 1);
				const float t = float(std::clamp(from - c, 0.0, 1.0));
				loudest = peaks[size_t(c)] + (peaks[size_t(d)] - peaks[size_t(c)]) * t;
			}
			const double h = std::max(0.5, std::min(1.0f, loudest) * half);
			(x < middle ? heard : to_come).push_back(QPointF(x, h));
		}
		auto outline = [&painter, centre](const std::vector<QPointF> &edge, const QColor &colour) {
			if (edge.empty()) return;
			QPolygonF shape;
			shape.reserve(int(edge.size()) * 4);
			for (const QPointF &at : edge) {
				shape << QPointF(at.x(), centre - at.y()) << QPointF(at.x() + 1, centre - at.y());
			}
			for (auto at = edge.rbegin(); at != edge.rend(); ++at) {
				shape << QPointF(at->x() + 1, centre + at->y()) << QPointF(at->x(), centre + at->y());
			}
			painter.setPen(Qt::NoPen);
			painter.setBrush(colour);
			painter.drawPolygon(shape);
		};
		outline(heard, behind);
		outline(to_come, ahead);
		/* Beats, and bars of four taller, as the grid places them. */
		if (where.beat > 0 && where.beat * pixels_a_second >= 4) {
			const double left = where.seconds - middle / pixels_a_second;
			const double right = where.seconds + (along - middle) / pixels_a_second;
			const int64_t k0 = int64_t(std::ceil((left - where.first) / where.beat));
			const int64_t k1 = int64_t(std::floor((right - where.first) / where.beat));
			QColor tick = text;
			for (int64_t k = k0; k <= k1 && k - k0 < 2000; ++k) {
				const double x = middle + (where.first + double(k) * where.beat - where.seconds) * pixels_a_second;
				const bool bar = ((k % 4) + 4) % 4 == 0;
				tick.setAlphaF(bar ? 0.7f : 0.3f);
				painter.fillRect(QRectF(x, 1, 1, bar ? across - 2 : across / 5.0), tick);
			}
		}
	}
	/* The playhead, across the lane. */
	painter.fillRect(QRectF(middle - 1, 0, 2, across), text);
}

const std::vector<float> &snurra_deck_waves::level_of(int deck, int level) {
	const std::vector<float> &peaks = m_peaks[size_t(deck)];
	if (level <= 0) return peaks;
	if (m_pyramid.size() < m_peaks.size()) m_pyramid.resize(m_peaks.size());
	std::vector<std::vector<float>> &levels = m_pyramid[size_t(deck)];
	/* Built when asked after new peaks; while a file is still being read,
	 * a quarter second at most out of date rather than built at each
	 * piece. */
	if (levels.empty() || (m_built.isValid() && m_built.elapsed() > 250 && levels.front().size() != (peaks.size() + 1) / 2)) {
		levels.clear();
		const std::vector<float> *below = &peaks;
		while (below->size() > 1) {
			std::vector<float> up((below->size() + 1) / 2);
			for (size_t j = 0; j < up.size(); ++j)
				up[j] = std::max((*below)[2 * j], 2 * j + 1 < below->size() ? (*below)[2 * j + 1] : 0.0f);
			levels.push_back(std::move(up));
			below = &levels.back();
		}
		m_built.restart();
	}
	if (levels.empty()) return peaks;
	return levels[size_t(std::min(level, int(levels.size()))) - 1];
}

void snurra_deck_waves::set_layout(layout_t layout) {
	m_layout = layout;
	set_deck_count(deck_count());   /* its least height follows */
}

void snurra_deck_waves::contextMenuEvent(QContextMenuEvent *event) {
	/* The layout, kept as the person's choice -- "Default" a choice too,
	 * following the build's, so nothing is written for it. */
	QSettings settings(QStringLiteral("snurra"), QStringLiteral("snurra"));
	const QString chosen = settings.value(QStringLiteral("decks/waveforms")).toString();
	QMenu menu(this);
	const QList<QPair<QString, QString>> choices{ { tr("Default (stacked)"), QString() },
		                                      { tr("Stacked"), QStringLiteral("stacked") },
		                                      { tr("Side by side"), QStringLiteral("side by side") },
		                                      { tr("Vertical, time running down"), QStringLiteral("vertical") },
		                                      { tr("Vertical, time running up"), QStringLiteral("vertical up") } };
	for (const auto &choice : choices) {
		QAction *action = menu.addAction(choice.first);
		action->setCheckable(true);
		action->setChecked(chosen == choice.second);
		connect(action, &QAction::triggered, this, [this, value = choice.second] { choose_layout(value); });
	}
	menu.exec(event->globalPos());
}

void snurra_deck_waves::choose_layout(const QString &value) {
	QSettings settings(QStringLiteral("snurra"), QStringLiteral("snurra"));
	if (value.isEmpty()) settings.remove(QStringLiteral("decks/waveforms"));
	else settings.setValue(QStringLiteral("decks/waveforms"), value);
	set_layout(layout_named(value));
}

snurra_deck_waves::layout_t snurra_deck_waves::layout_named(const QString &value) {
	if (value == QLatin1String("side by side")) return side_by_side;
	if (value == QLatin1String("vertical")) return vertical;
	if (value == QLatin1String("vertical up")) return vertical_up;
	return stacked;
}
