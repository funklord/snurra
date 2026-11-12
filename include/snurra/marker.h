#ifndef SNURRA_MARKER_H
#define SNURRA_MARKER_H

#include <QRect>
#include <QWidget>

/*
 * The marker shown while dragging (project.md sec 3d): where the window
 * would land, filled, and the shape of the group it would make, outlined.
 * A frameless window of its own above the others, which takes no input,
 * so the drag goes on through it. On Wayland a program cannot place one,
 * so there it is not shown.
 */
class snurra_marker : public QWidget {
public:
	snurra_marker();

	/* Global rectangles; an empty landing hides the marker. */
	void show_at(const QRect &landing, const QRect &shape);

	QRect landing() const { return m_landing; }
	QRect shape() const { return m_shape; }

protected:
	void paintEvent(QPaintEvent *) override;

private:
	QRect m_landing;
	QRect m_shape;
};

#endif
