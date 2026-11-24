#ifndef SNURRA_DECK_WAVES_H
#define SNURRA_DECK_WAVES_H

#include <QColor>
#include <QString>
#include <QWidget>
#include <QElapsedTimer>

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
	/* A scratch (project.md sec 3f, c3 step 3): a hand put on a deck's
	 * lane, moved, and taken off -- `seconds` where the hand has the
	 * record, in the track. */
	std::function<void(int deck, bool on)> on_hold;
	std::function<void(int deck, double seconds)> on_hand;
	/* The deck whose lane is at `at`, and how far along the lane `at` is;
	 * -1 for none. */
	int lane_at(QPointF at, double &along) const;

	/* How the decks' lanes are laid out (the holder, 2026-11-24:
	 * "waveforms horizontally side by side or on top of each other or
	 * vertical, especially with 4ch"): rows stacked full width, columns
	 * side by side, or vertical lanes, time running down. A setting
	 * (decks/waveforms, on the waveforms' right-click menu), stacked by
	 * default. */
	enum layout_t { stacked, side_by_side, vertical };
	void set_layout(layout_t layout);
	layout_t layout() const { return m_layout; }
	/* Chosen by hand and kept; empty for the default. */
	void choose_layout(const QString &value);
	static layout_t layout_named(const QString &value);

	/* The colour a deck is known by, here and on its deck. */
	static QColor colour_of(int deck);

protected:
	void paintEvent(QPaintEvent *event) override;
	void wheelEvent(QWheelEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void contextMenuEvent(QContextMenuEvent *event) override;
	QSize sizeHint() const override;

private:
	std::vector<std::vector<float>> m_peaks;
	double m_span = 8;
	layout_t m_layout = stacked;
	/* Each deck's peaks halved again and again, each entry the loudest of
	 * the two below: what a pixel reads zoomed out (empty until drawn). */
	std::vector<std::vector<std::vector<float>>> m_pyramid;
	const std::vector<float> &level_of(int deck, int level);
	QElapsedTimer m_built;
	std::vector<double> m_last;   /* each deck's place at the last frame */
	bool m_stale = true;
	int m_held = -1;              /* the deck under the hand */
	double m_held_from = 0;       /* where along its lane the hand went down */
	double m_held_seconds = 0;    /* and where its record was then */
	void paint_lane(class QPainter &painter, int index, const deck &where, double along, double across);
};

#endif
