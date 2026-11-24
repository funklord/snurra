#include "snurra/controls.h"

#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

/* ---- the knob ---------------------------------------------------------- */

snurra_knob::snurra_knob(QWidget *parent) : QDial(parent) {}

/* The whole range in twelve lines of text moved, however many steps it
 * has: the key shift's 24 a step every half line, as it was, and an EQ
 * knob's 200 no farther. */
int snurra_knob::steps_for(double pixels) const {
	const double whole = std::max(48, fontMetrics().height() * 12);
	return int(std::lround(pixels / whole * (maximum() - minimum())));
}

void snurra_knob::mousePressEvent(QMouseEvent *event) {
	if (event->button() != Qt::LeftButton) {
		event->ignore();
		return;
	}
	m_held = true;
	m_from = value();
	m_from_y = event->position().y();
	setSliderDown(true);
	event->accept();
}

void snurra_knob::mouseMoveEvent(QMouseEvent *event) {
	if (!m_held) {
		event->ignore();
		return;
	}
	setSliderPosition(m_from + steps_for(m_from_y - event->position().y()));
	event->accept();
}

void snurra_knob::mouseReleaseEvent(QMouseEvent *event) {
	if (!m_held) {
		event->ignore();
		return;
	}
	m_held = false;
	setSliderDown(false);
	event->accept();
}

void snurra_knob::mouseDoubleClickEvent(QMouseEvent *event) {
	if (event->button() != Qt::LeftButton) {
		event->ignore();
		return;
	}
	setValue(m_home);
	event->accept();
}

/* ---- the fader --------------------------------------------------------- */

snurra_fader::snurra_fader(Qt::Orientation orientation, QWidget *parent) : QSlider(orientation, parent) {
	setFocusPolicy(Qt::StrongFocus);
}

void snurra_fader::set_compact(bool compact) {
	m_compact = compact;
	updateGeometry();
	update();
}

/* The cap: a line of text along the travel, two and a half lines across
 * it -- compact, two thirds of a line along and one and a quarter across. */
QRect snurra_fader::cap_rect() const {
	const int line = m_compact ? std::max(6, fontMetrics().height() * 2 / 3) : fontMetrics().height();
	const int span = maximum() - minimum();
	const double share = span > 0 ? double(value() - minimum()) / span : 0;
	const int across = m_compact ? fontMetrics().height() * 5 / 4 : line * 5 / 2;
	if (orientation() == Qt::Vertical) {
		const int length = line;
		const int along = int(std::lround((1 - share) * travel()));
		return QRect((width() - across) / 2, along, across, length);
	}
	const int length = line;
	const int along = int(std::lround(share * travel()));
	return QRect(along, (height() - across) / 2, length, across);
}

int snurra_fader::travel() const {
	const int line = m_compact ? std::max(6, fontMetrics().height() * 2 / 3) : fontMetrics().height();
	return std::max(1, (orientation() == Qt::Vertical ? height() : width()) - line);
}

int snurra_fader::value_after(int from, double pixels) const {
	const double per_pixel = double(maximum() - minimum()) / travel();
	return std::clamp(from + int(std::lround(pixels * per_pixel)), minimum(), maximum());
}

QSize snurra_fader::sizeHint() const {
	const int line = fontMetrics().height();
	if (m_compact) return orientation() == Qt::Vertical ? QSize(line * 3 / 2, line * 6) : QSize(line * 6, line * 3 / 2);
	return orientation() == Qt::Vertical ? QSize(line * 3, line * 8) : QSize(line * 12, line * 3);
}

QSize snurra_fader::minimumSizeHint() const {
	const int line = fontMetrics().height();
	if (m_compact) return orientation() == Qt::Vertical ? QSize(line * 3 / 2, line * 3) : QSize(line * 3, line * 3 / 2);
	return orientation() == Qt::Vertical ? QSize(line * 3, line * 4) : QSize(line * 6, line * 3);
}

void snurra_fader::paintEvent(QPaintEvent *) {
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	const QPalette &colours = palette();
	const bool vertical = orientation() == Qt::Vertical;
	const int line = m_compact ? std::max(6, fontMetrics().height() * 2 / 3) : fontMetrics().height();
	const QRect cap = cap_rect();
	/* The groove, down the middle of the travel. */
	const QRectF groove = vertical ? QRectF(width() / 2.0 - 2, line / 2.0, 4, travel())
	                               : QRectF(line / 2.0, height() / 2.0 - 2, travel(), 4);
	painter.setPen(Qt::NoPen);
	painter.setBrush(colours.color(QPalette::Shadow));
	painter.drawRoundedRect(groove, 2, 2);
	/* Ticks: ten steps along, the home one long. */
	QColor tick = colours.color(QPalette::WindowText);
	tick.setAlphaF(0.4f);
	painter.setPen(QPen(tick, 1));
	const int span = maximum() - minimum();
	for (int i = 0; i <= 10; ++i) {
		const double at = line / 2.0 + travel() * (vertical ? 1 - i / 10.0 : i / 10.0);
		const bool home = span > 0 && std::abs(double(m_home - minimum()) / span - i / 10.0) < 0.05;
		const double reach = home ? line * 1.3 : line * 0.8;
		if (vertical) {
			painter.drawLine(QPointF(width() / 2.0 - reach, at), QPointF(width() / 2.0 - line * 0.5, at));
			painter.drawLine(QPointF(width() / 2.0 + line * 0.5, at), QPointF(width() / 2.0 + reach, at));
		} else {
			painter.drawLine(QPointF(at, height() / 2.0 - reach), QPointF(at, height() / 2.0 - line * 0.5));
			painter.drawLine(QPointF(at, height() / 2.0 + line * 0.5), QPointF(at, height() / 2.0 + reach));
		}
	}
	/* The cap, flat, a line across it where it reads. */
	QColor face = isEnabled() ? colours.color(QPalette::Button) : colours.color(QPalette::Window);
	painter.setPen(QPen(colours.color(QPalette::Shadow), 1));
	painter.setBrush(face);
	painter.drawRoundedRect(QRectF(cap).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
	painter.setPen(QPen(isEnabled() ? colours.color(QPalette::Highlight) : tick, 2));
	if (vertical) painter.drawLine(QPointF(cap.left() + 3, cap.center().y() + 0.5), QPointF(cap.right() - 3, cap.center().y() + 0.5));
	else painter.drawLine(QPointF(cap.center().x() + 0.5, cap.top() + 3), QPointF(cap.center().x() + 0.5, cap.bottom() - 3));
	if (hasFocus()) {
		painter.setPen(QPen(colours.color(QPalette::Highlight), 1, Qt::DotLine));
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(rect().adjusted(0, 0, -1, -1));
	}
}

void snurra_fader::mousePressEvent(QMouseEvent *event) {
	if (event->button() != Qt::LeftButton) {
		event->ignore();
		return;
	}
	m_held = true;
	m_from = value();
	m_from_at = orientation() == Qt::Vertical ? event->position().y() : event->position().x();
	setSliderDown(true);
	event->accept();
}

void snurra_fader::mouseMoveEvent(QMouseEvent *event) {
	if (!m_held) {
		event->ignore();
		return;
	}
	const double moved = orientation() == Qt::Vertical ? m_from_at - event->position().y() : event->position().x() - m_from_at;
	setSliderPosition(value_after(m_from, moved));
	event->accept();
}

void snurra_fader::mouseReleaseEvent(QMouseEvent *event) {
	if (!m_held) {
		event->ignore();
		return;
	}
	m_held = false;
	setSliderDown(false);
	event->accept();
}

void snurra_fader::mouseDoubleClickEvent(QMouseEvent *event) {
	if (event->button() != Qt::LeftButton) {
		event->ignore();
		return;
	}
	setValue(m_home);
	event->accept();
}
