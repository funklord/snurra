#include "snurra/marker.h"

#include <QPainter>

snurra_marker::snurra_marker() : QWidget(nullptr) {
	setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
	               | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus);
	setAttribute(Qt::WA_TranslucentBackground);
	setAttribute(Qt::WA_ShowWithoutActivating);
	setObjectName(QStringLiteral("snurra-marker"));
}

void snurra_marker::show_at(const QRect &landing, const QRect &shape) {
	if (landing == m_landing && shape == m_shape && isVisible() == !landing.isEmpty()) return;
	m_landing = landing;
	m_shape = shape.isEmpty() ? landing : shape;
	if (landing.isEmpty()) {
		hide();
		return;
	}
	setGeometry(m_shape.adjusted(-2, -2, 2, 2));
	update();
	show();
}

void snurra_marker::paintEvent(QPaintEvent *) {
	QPainter painter(this);
	const QPoint origin = geometry().topLeft();
	QColor fill = palette().color(QPalette::Highlight);
	fill.setAlpha(70);
	painter.fillRect(m_landing.translated(-origin), fill);
	QPen outline(palette().color(QPalette::Highlight), 2, Qt::DashLine);
	painter.setPen(outline);
	painter.setBrush(Qt::NoBrush);
	painter.drawRect(m_shape.translated(-origin).adjusted(1, 1, -1, -1));
	painter.setPen(QPen(palette().color(QPalette::Highlight), 2));
	painter.drawRect(m_landing.translated(-origin).adjusted(1, 1, -1, -1));
}
