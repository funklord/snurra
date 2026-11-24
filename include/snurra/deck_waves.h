#ifndef SNURRA_DECK_WAVES_H
#define SNURRA_DECK_WAVES_H

#include <QColor>
#include <QString>
#include <QWidget>

#include <functional>
#include <vector>

/*
 * The deck view's waveforms (project.md sec 3f, the holder's trial of
 * 2026-11-24: "the decks need to look more like modern DJ software",
 * Traktor's the look asked for): one row a deck, stacked full width above
 * the decks, each its track zoomed in and scrolling past a playhead fixed
 * at the middle, its beats and bars marked, in the deck's colour -- so
 * two tracks' beats are seen lining up, which an overview a deck wide
 * cannot show.
 *
 * Its own widget, filled from outside: `fill` is asked, at each repaint,
 * where each deck is. Peaks arrive as they are read, COLUMNS_A_SECOND of
 * them a second of the track.
 */
class snurra_deck_waves : public QWidget {
	Q_OBJECT
public:
	static constexpr int COLUMNS_A_SECOND = 100;

	struct deck {
		bool loaded = false;
		double seconds = 0;   /* where its playhead is, in the track */
		double beat = 0;      /* seconds a beat, 0 for none */
		double first = 0;     /* a beat, in seconds into the track */
		bool playing = false;
	};

	explicit snurra_deck_waves(QWidget *parent = nullptr);

	void set_deck_count(int count);
	int deck_count() const { return int(m_peaks.size()); }
	/* A deck's peaks, `first` on: the columns read so far. */
	void set_peaks(int deck, int first, const std::vector<float> &values);
	void clear_peaks(int deck);
	/* How many columns a deck has, as read so far. */
	int columns(int deck) const { return deck >= 0 && deck < int(m_peaks.size()) ? int(m_peaks[size_t(deck)].size()) : 0; }
	/* Seconds shown across the width; the wheel changes it. */
	void set_span(double seconds);
	double span() const { return m_span; }

	std::function<void(int deck, snurra_deck_waves::deck &where)> fill;

	/* The colour a deck is known by, here and on its deck. */
	static QColor colour_of(int deck);

protected:
	void paintEvent(QPaintEvent *event) override;
	void wheelEvent(QWheelEvent *event) override;
	QSize sizeHint() const override;

private:
	std::vector<std::vector<float>> m_peaks;
	double m_span = 8;
};

#endif
