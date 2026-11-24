#ifndef SNURRA_MIXER_H
#define SNURRA_MIXER_H

#include <QWidget>

#include <functional>
#include <vector>

class QDial;
class QSlider;
class QToolButton;
class snurra_tile;
class snurra_engine;
class QLabel;
class QProgressBar;

/*
 * The decks' mixer, between them as Traktor's (project.md sec 3f, step 3,
 * the holder 2026-11-24: "for each channel on the mixer we also have gain,
 * 3ch eq and possible FX inserts"): a strip a deck -- its gain, its three
 * EQ knobs, its fader, its cue -- in the deck's colour. Each control is
 * the deck's tile's own (set_gain_db, set_eq, set_level), read back from
 * it ten times a second, so a change made anywhere else -- the tile's
 * panel, a controller, the automation -- is seen here too.
 *
 * Where there is no room for it -- a phone upright -- it hides, watching
 * its parent's width; the tile's panel has the same controls.
 */
class snurra_mixer : public QWidget {
	Q_OBJECT
public:
	explicit snurra_mixer(QWidget *parent = nullptr);

	void set_deck_count(int count);
	int deck_count() const { return int(m_strips.size()); }

	/* The tile on a deck, or none. */
	std::function<snurra_tile *(int deck)> tile_of;
	/* A strip's cue: the headphones onto that deck, or off it; and
	 * whether they are on it now, and whether there are any. */
	std::function<void(int deck, bool on)> on_cue;
	std::function<bool(int deck)> cued;
	std::function<bool()> has_headphones;
	/* A strip's EQ button: the deck's drawn EQ opened, or closed; and
	 * whether it is open now. */
	std::function<void(int deck, bool open)> on_eq;
	std::function<bool(int deck)> eq_opened;
	/* Told when the mixer shows or hides for want of room, so what it
	 * holds can show on the decks instead. */
	std::function<void(bool shown)> on_shown;

	/* The master section (project.md sec 3f, e): the level, the EQ's three
	 * parts and the limiter of everything sent out, with a meter -- opened
	 * and closed by hand, at the right end of the strips. */
	void set_master(snurra_engine *engine);
	void set_master_open(bool open);
	bool master_open() const;
	struct master_strip {
		QWidget *box = nullptr;
		QDial *level = nullptr;
		QDial *high = nullptr;
		QDial *mid = nullptr;
		QDial *low = nullptr;
		QToolButton *limit = nullptr;
		QProgressBar *meter = nullptr;
		QLabel *reduction = nullptr;
	};
	const master_strip &master() const { return m_master; }

	/* An EQ knob runs -100 to 100, 0 dB at its middle; -100 the kill. */
	static constexpr int KILL_STEP = -100;
	/* The strips left to right, by deck: as the decks sit, the left
	 * column's from the bottom up, then the right's from the top down --
	 * Traktor's C A B D for four. */
	void set_order(const std::vector<int> &decks);
	int place_of(int deck) const;

	struct strip {
		QDial *gain = nullptr;
		QDial *high = nullptr;
		QDial *mid = nullptr;
		QDial *low = nullptr;
		QSlider *fader = nullptr;
		QToolButton *cue = nullptr;
		QToolButton *eq = nullptr;
		QWidget *box = nullptr;
	};
	const strip &strip_of(int deck) const { return m_strips.at(size_t(deck)); }

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;
	QSize minimumSizeHint() const override { return QSize(0, 0); }

private:
	void refresh();

	std::vector<strip> m_strips;
	master_strip m_master;
	snurra_engine *m_engine = nullptr;
	class QHBoxLayout *m_row;
};

#endif
