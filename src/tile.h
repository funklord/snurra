#ifndef SNURRA_TILE_H
#define SNURRA_TILE_H

#include <QWidget>

#include "tile_state.h"

class QLabel;
class QSlider;
class QToolButton;
class snurra_deck;
class snurra_waveform;

/*
 * One tile (project.md sec 3b, 3c): compact, resizeable, a waveform
 * always, and its controls only when pulled out. The first tile opens
 * with them and plays; the others open as a waveform alone, paused, so a
 * screen of tiles is not a screen of control panels. The controls are
 * play and pause, the position and a level, all of them the deck's: the
 * tile shows what the engine is playing rather than keeping a clock of
 * its own. The title is the handle: dragged, it asks its window to move,
 * or, in a group, to be pulled out.
 */
class snurra_tile : public QWidget {
	Q_OBJECT

public:
	/* `deck` is the engine's, and outlives the tile when a handover's
	 * fade is still sounding after the tile has gone. */
	snurra_tile(const snurra_tile_state &state, snurra_deck *deck, QWidget *parent = nullptr);
	snurra_deck *deck() const { return m_deck; }

	/* As it is now, sampled this moment, for a handover. */
	snurra_tile_state state() const;

	void set_playing(bool playing);
	bool playing() const { return m_state.playing; }
	void set_controls(bool out);
	bool controls() const { return m_state.controls; }
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
	void seek(qint64 position_us);
	void tick();

	snurra_tile_state m_state;
	snurra_deck *m_deck;
	QLabel *m_title;
	snurra_waveform *m_waveform;
	QToolButton *m_pull;
	QWidget *m_controls;
	QToolButton *m_play;
	QSlider *m_position;
	QSlider *m_level;
	QPoint m_press;
	bool m_dragging = false;
};

#endif
