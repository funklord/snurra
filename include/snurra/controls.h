#ifndef SNURRA_CONTROLS_H
#define SNURRA_CONTROLS_H

#include <QDial>
#include <QSlider>

/*
 * The controls snurra uses everywhere (the holder, 2026-11-25: "Do not
 * use standard knobs, they are broken, we decide on a set of widgets and
 * their config we use throughout the software, use the ones we fixed
 * before. We also need DJ-faders"). Two kinds, and which is which by how
 * a DJ's hand uses it (project.md sec 3f):
 *
 *  - snurra_knob: turned a little and often -- gain, the EQ's parts, a
 *    key shift, a master level. Small.
 *  - snurra_fader: thrown far and watched -- a channel's fader and a
 *    tempo vertical, the crossfader horizontal. Long, with a cap.
 *
 * Neither jumps to where a press lands, both go back to their home value
 * on a double-click, and both take the wheel and the keys as Qt's do.
 */

/*
 * A knob (2026-11-18: "a knob should be held L mouse button or touch and
 * then up/down"): held and moved up to turn it up, down to turn it down,
 * its whole range in twelve lines of text, wherever on it the press began. Qt's own
 * QDial follows the pointer's angle round its centre, which at two lines
 * across leaves a few pixels between one end and the other, jumps to
 * wherever the press landed, and has no gesture a finger can make.
 * `home` is where a double-click puts it -- 0 dB, no shift.
 */
class snurra_knob : public QDial {
	Q_OBJECT
public:
	explicit snurra_knob(QWidget *parent = nullptr);
	void set_home(int home) { m_home = home; }
	int home() const { return m_home; }
	/* Steps a press and drag of `pixels` upward gives. */
	int steps_for(double pixels) const;

protected:
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
	bool m_held = false;
	int m_from = 0;
	double m_from_y = 0;
	int m_home = 0;
};

/*
 * A DJ fader: a groove, a flat cap with a line across it, ticks along the
 * scale, as a mixer's -- not a desktop slider's handle and filled track.
 * Held, it moves by as much as the hand does from where it was, never to
 * where the press landed, so a touch does not jump a channel to silence.
 * Vertical, up is more; horizontal, right is more.
 */
class snurra_fader : public QSlider {
	Q_OBJECT
public:
	explicit snurra_fader(Qt::Orientation orientation, QWidget *parent = nullptr);
	void set_home(int home) { m_home = home; }
	int home() const { return m_home; }
	/* Small, for a compact panel -- a window's tile -- rather than a
	 * mixer's long throw: a line and a half thick, its cap smaller. */
	void set_compact(bool compact);
	bool compact() const { return m_compact; }
	/* The value a drag of `pixels` along it, from `from`, gives. */
	int value_after(int from, double pixels) const;

protected:
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void mouseDoubleClickEvent(QMouseEvent *event) override;
	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

private:
	int travel() const;
	QRect cap_rect() const;
	bool m_held = false;
	int m_from = 0;
	double m_from_at = 0;
	int m_home = 0;
	bool m_compact = false;
};

#endif
