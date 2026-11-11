#ifndef SNURRA_TILE_H
#define SNURRA_TILE_H

#include <QElapsedTimer>
#include <QWidget>

#include "tile_state.h"

class QLabel;
class QSlider;
class QToolButton;

/*
 * One tile (project.md sec 3b): compact, resizeable, its controls dummies
 * until there is audio -- play and pause, a position that a clock moves,
 * a level. Its title is the handle: dragged, it asks its window to move,
 * or, in a group, to be pulled out.
 */
class snurra_tile : public QWidget {
	Q_OBJECT

public:
	explicit snurra_tile(const snurra_tile_state &state, QWidget *parent = nullptr);

	/* As it is now, sampled this moment, for a handover. */
	snurra_tile_state state() const;

	void set_playing(bool playing);
	bool playing() const { return m_state.playing; }
	int row() const { return m_state.row; }
	int column() const { return m_state.column; }
	void set_place(int row, int column);

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

signals:
	/* The title was pressed and dragged: where to, in global coordinates. */
	void handle_moved(snurra_tile *tile, const QPoint &global);
	void handle_released(snurra_tile *tile, const QPoint &global);
	void close_requested(snurra_tile *tile);

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	void show_position();

	snurra_tile_state m_state;
	QLabel *m_title;
	QToolButton *m_play;
	QSlider *m_position;
	QSlider *m_level;
	QPoint m_press;
	bool m_dragging = false;
};

#endif
