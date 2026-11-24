#ifndef SNURRA_DECK_HEAD_H
#define SNURRA_DECK_HEAD_H

#include <QPointer>
#include <QWidget>

class QLabel;
class QToolButton;
class snurra_tile;

/*
 * A deck's head, as Traktor's (project.md sec 3f, the holder's trial of
 * 2026-11-24, step 2): the deck's letter in its waveform's colour, the
 * track's title and artist, its tempo large -- the beat as it is heard, at
 * the deck's tempo -- its key as heard, how far the tempo is moved, the
 * time left, and play, cue and sync. Read from the deck's tile ten times a
 * second; its buttons are the tile's own play, cue and sync, so what they
 * do is what a tile does.
 */
class snurra_deck_head : public QWidget {
	Q_OBJECT
public:
	snurra_deck_head(int deck, QWidget *parent = nullptr);

	/* The tile the deck holds, or none. */
	void set_tile(snurra_tile *tile);
	/* What it shows now, as text: for tests and screen readers. */
	QString bpm_text() const;
	QString key_text() const;
	QString left_text() const;
	QString artist_text() const;

private:
	void refresh();

	int m_deck;
	QPointer<snurra_tile> m_tile;
	QLabel *m_letter;
	QLabel *m_title;
	QLabel *m_artist;
	QLabel *m_bpm;
	QLabel *m_key;
	QLabel *m_tempo;
	QLabel *m_left;
	QToolButton *m_play;
	QToolButton *m_cue;
	QToolButton *m_sync;
};

#endif
