#include "snurra/deck_waves.h"

#include <QPainter>
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
	connect(frame, &QTimer::timeout, this, [this] {
		if (isVisible()) update();
	});
	frame->start();
}

void snurra_deck_waves::set_deck_count(int count) {
	m_peaks.resize(size_t(std::max(0, count)));
	/* Never squeezed out by the decks below: enough to see a beat. */
	setMinimumHeight(std::max(1, count) * 48);
	updateGeometry();
	update();
}

void snurra_deck_waves::set_peaks(int deck, int first, const std::vector<float> &values) {
	if (deck < 0 || deck >= int(m_peaks.size()) || first < 0) return;
	std::vector<float> &peaks = m_peaks[size_t(deck)];
	if (peaks.size() < size_t(first) + values.size()) peaks.resize(size_t(first) + values.size(), 0.0f);
	std::copy(values.begin(), values.end(), peaks.begin() + first);
}

void snurra_deck_waves::clear_peaks(int deck) {
	if (deck >= 0 && deck < int(m_peaks.size())) m_peaks[size_t(deck)].clear();
}

void snurra_deck_waves::set_span(double seconds) {
	m_span = std::clamp(seconds, 2.0, 60.0);
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
	if (count == 0 || width() < 2) return;
	const double middle = width() / 2.0;
	const double pixels_a_second = width() / m_span;
	QColor text = colours.color(QPalette::Text);
	for (int i = 0; i < count; ++i) {
		const int top = height() * i / count;
		const int bottom = height() * (i + 1) / count;
		const int row_height = bottom - top;
		const double centre = top + row_height / 2.0;
		deck where;
		if (fill) fill(i, where);
		/* A line between rows. */
		if (i > 0) {
			QColor line = text;
			line.setAlphaF(0.15f);
			painter.fillRect(QRect(0, top, width(), 1), line);
		}
		if (!where.loaded) continue;
		const std::vector<float> &peaks = m_peaks[size_t(i)];
		/* Past the playhead the track is heard; ahead of it to come: the
		 * colour full ahead, dimmer behind. */
		QColor ahead = colour_of(i);
		QColor behind = ahead;
		behind.setAlphaF(0.45f);
		const double half = (row_height - 4) / 2.0;
		for (int x = 0; x < width(); ++x) {
			const double seconds = where.seconds + (x - middle) / pixels_a_second;
			if (seconds < 0) continue;
			/* The loudest of the columns this pixel covers. */
			const double from = seconds * COLUMNS_A_SECOND;
			const double to = from + COLUMNS_A_SECOND / pixels_a_second;
			float loudest = 0;
			for (int c = int(from); c <= int(to) && c < int(peaks.size()); ++c)
				if (c >= 0) loudest = std::max(loudest, peaks[size_t(c)]);
			if (int(from) >= int(peaks.size())) break;
			const double h = std::min(1.0f, loudest) * half;
			painter.fillRect(QRectF(x, centre - h, 1, std::max(1.0, 2 * h)), x < middle ? behind : ahead);
		}
		/* Beats, and bars of four taller, as the grid places them. */
		if (where.beat > 0 && where.beat * pixels_a_second >= 4) {
			const double left = where.seconds - middle / pixels_a_second;
			const double right = where.seconds + (width() - middle) / pixels_a_second;
			const int64_t k0 = int64_t(std::ceil((left - where.first) / where.beat));
			const int64_t k1 = int64_t(std::floor((right - where.first) / where.beat));
			QColor tick = text;
			for (int64_t k = k0; k <= k1 && k - k0 < 2000; ++k) {
				const double x = middle + (where.first + double(k) * where.beat - where.seconds) * pixels_a_second;
				const bool bar = ((k % 4) + 4) % 4 == 0;
				tick.setAlphaF(bar ? 0.7f : 0.3f);
				painter.fillRect(QRectF(x, top + 1, 1, bar ? row_height - 2 : row_height / 5.0), tick);
			}
		}
	}
	/* The playhead, through every row. */
	painter.fillRect(QRectF(middle - 1, 0, 2, height()), text);
}
