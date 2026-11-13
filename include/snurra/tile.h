#ifndef SNURRA_TILE_H
#define SNURRA_TILE_H

#include <QElapsedTimer>
#include <QPointer>
#include <QWidget>

#include <memory>

#include "snurra/beats.h"
#include "snurra/source.h"
#include "snurra/tile_state.h"

class QHBoxLayout;
class QLabel;
class QTimer;
class QVBoxLayout;
class QSlider;
class QToolButton;
class snurra_bytes;
class snurra_deck;
class snurra_engine;
class snurra_peaks;
class snurra_video;
class snurra_video_view;
class snurra_subtitles;
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
	~snurra_tile() override;

	/*
	 * A tile in a program of its own (project.md sec 1: BeerSSH's tile,
	 * fuzzypickles' player), playing `bytes` -- from its transport, still
	 * arriving or not -- or, without them, what `state.source` names. It
	 * owns its deck in `engine` and retires it when the tile goes, so a
	 * closed tile never leaves a deck playing. No window of its own, no
	 * socket: a widget to put in the consumer's layout.
	 *
	 * Its waveform needs a second read of the track. With `peak_bytes` --
	 * a second snurra_bytes over the same track, for a consumer to whom
	 * that read is cheap -- it is drawn from them, as far as they are
	 * here; without, it is the stand-in, so a transport that pays per read
	 * (SFTP) never pays twice.
	 */
	static snurra_tile *embedded(snurra_engine &engine, const snurra_tile_state &state,
	                             std::shared_ptr<snurra_bytes> bytes, QWidget *parent = nullptr,
	                             std::shared_ptr<snurra_bytes> peak_bytes = nullptr);
	snurra_deck *deck() const { return m_deck; }

	/* As it is now, sampled this moment, for a handover. */
	snurra_tile_state state() const;

	void set_playing(bool playing);
	bool playing() const { return m_state.playing; }

	/*
	 * In a window's group (project.md sec 3b, step 4), play, pause and
	 * seek are the group's: the tile asks (play_requested, seek_requested)
	 * and the group moves every lane at once. Alone, or embedded, the tile
	 * does them itself.
	 */
	void set_grouped(bool grouped) { m_grouped = grouped; }
	void request_playing(bool playing);
	/* Plays `frame` from the moment `at_us` on the shared clock, any fade
	 * done with; or holds at `frame`, paused. */
	void play_from(int64_t frame, qint64 at_us);
	void hold_at(int64_t frame);
	/* Frames, at the deck's rate: where it is now, and how long. */
	int64_t frame_now() const;
	int64_t length_frames() const;
	void set_controls(bool out);
	bool controls() const { return m_state.controls; }
	int row() const { return m_state.row; }
	int column() const { return m_state.column; }
	void set_place(int row, int column);

	/* For the keys (snurra_node::keyPressEvent): seek by an amount, the
	 * level in per cent, and a mute that comes back to where it was. */
	void seek_by(qint64 delta_us);
	int level() const { return m_state.level; }
	void set_level(int level);
	void toggle_mute();

	/* Jumps (project.md sec 4): back or forward by the track's step --
	 * 4 or 16 bars where it has a beat, else 10 s or 60 s, or 5 % or
	 * 20 % from ten minutes up -- and a 10 ms nudge of this tile alone. */
	void jump(int direction, bool large);
	void nudge(int direction);
	QString jump_step(bool large) const;
	snurra_beat_grid beats() const { return m_grid; }
	void set_beats(const snurra_beat_grid &grid);
	/* How far this tile's track is ahead of its lane's time: nudges. */
	qint64 offset_us() const { return m_state.offset_us; }
	void set_offset_us(qint64 offset) { m_state.offset_us = offset; }
	/* Where its lane begins on the group's timeline, kept by the lane's
	 * first tile (tile_state.h) and said on its waveform. */
	qint64 lane_start_us() const { return m_state.lane_start_us; }
	void set_lane_start_us(qint64 us);

	/* Track frames a frame out (1: as recorded), and key lock (sec 4). */
	void set_tempo(float tempo);
	float tempo() const { return m_state.tempo; }
	void set_key_lock(bool on);
	/* The key moved on its own, in semitones (deck.h); its dial follows. */
	void set_key_shift(int semitones);
	bool key_lock() const { return m_state.key_lock; }

	/* Sync (project.md sec 4): asks the window to match this tile's beat to
	 * another's -- tempo, then phase. */
	void request_sync() { emit sync_requested(this); }
	void request_key_sync() { emit key_sync_requested(this); }
	/* The key as heard (beats.h's numbering), -1 for none: its shift,
	 * and without key lock the tempo's. */
	int key_heard() const;
	/* Its key shift set so its key is the leader's on the Camelot wheel --
	 * the same number, so relative major and minor count as one -- the
	 * smallest way there. False where either has no key. */
	bool match_key_of(const snurra_tile *leader);
	/* Its tempo matched to the leader's beat, and its phase's distance from
	 * the leader's now, in microseconds, for the caller to bend; 0 where
	 * either has no beat. */
	qint64 match_beat_of(const snurra_tile *leader);
	/* The sync button offered outside a window's group too (a DJ player's
	 * decks), wherever this track has a beat. */
	void set_sync_available(bool available);

	/* The cue (a CDJ's, Traktor's): paused, it is set where the track
	 * stands; playing, the track goes back to it and stops. */
	void cue();
	/* Chapters, a mix's tracklist (source.h): how many, the title of the
	 * one playing, and mplayer's ! and @ stepping between them. */
	int chapter_count() const { return int(m_chapters.size()); }
	QString chapter_title() const;
	void step_chapter(int direction);
	/* The EQ (deck.h), low, mid and high in dB; its dials follow. */
	void set_eq(float low_db, float mid_db, float high_db);
	qint64 cue_us() const { return m_state.cue_us; }

	/* The envelope, as in snurra_tile_state: shown on the waveform, heard
	 * through the deck, and edited on the waveform while the envelope
	 * control (in the controls, sec 3c) is on. */
	void set_envelope(const QList<QPair<qint64, float>> &points);
	QList<QPair<qint64, float>> envelope() const { return m_state.envelope; }
	/* A train's crossfade, drawn on the waveform: how much of the track
	 * fades in from its start and out to its end, as fractions of it --
	 * set by the window, which knows the tiles before and after. */
	void set_crossfade(double in, double out);
	QPair<double, double> crossfade() const { return m_crossfade; }

	/*
	 * The drawers (project.md sec 3b): upward the video, downward the
	 * controls. A tile alone keeps them in itself, above and below its
	 * waveform; a window takes them out, to lay them in rows of their own,
	 * so the waveforms of a lane stay on one line. Still the tile's: they
	 * show and hide with it, and go when it goes.
	 */
	void take_drawers_out();
	QWidget *drawer_above() const { return m_above; }
	QWidget *drawer_below() const { return m_controls; }

	/* The upward drawer, the video, for a track that has a picture:
	 * opened and closed by its button on the title, or here; and the
	 * picture full screen -- asked for before the picture is found
	 * (mplayer's -fs), shown so once it is. */
	void set_video_open(bool open);
	bool video_open() const;
	void set_full_screen(bool full);
	void toggle_full_screen() { set_full_screen(!full_screen()); }
	bool full_screen() const { return bool(m_full); }

	/* Subtitles over the picture (mplayer's v and j): the files beside the
	 * track, then its own streams. v shows or hides them, j takes the next
	 * track and, after the last, none. The name of the one showing, or an
	 * empty string. */
	void toggle_subtitles();
	/* Subtitle files offered before those beside the source: a host's,
	 * fetched from beside a remote file (embed.h). Set before the picture
	 * opens, or the tracks are found again. */
	void set_subtitle_files(const QStringList &paths);
	void next_subtitles();
	/* mplayer's z and x: the subtitles shown later (positive) or earlier
	 * by `us` more, for a file beside the picture that is out of step. */
	void delay_subtitles(qint64 us);
	/* mplayer's #: the file's next sound track (a film's languages), from
	 * where it is, crossed over as a seek. */
	void next_audio_track();
	/* mplayer's .: paused, the picture's next frame -- the whole group
	 * moved to where it begins. Nothing for a track with no picture. */
	void step_frame();
	qint64 subtitle_delay() const { return m_subtitle_delay; }
	/* mplayer's + and -: the sound shown later (positive) or earlier
	 * than the picture by `us` more -- the picture moved, not the sound. */
	void delay_sound(qint64 us);
	qint64 sound_delay() const { return m_sound_delay; }
	/* mplayer's o: where it is and how long, held over the picture, or
	 * not. */
	void toggle_clock();
	QString subtitles_name() const;

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

signals:
	/* The title was pressed and dragged: where to, in global coordinates. */
	void handle_moved(snurra_tile *tile, const QPoint &global);
	void handle_released(snurra_tile *tile, const QPoint &global);
	void close_requested(snurra_tile *tile);
	/* Pressed anywhere: the tile the keys act on. */
	void touched(snurra_tile *tile);
	/* What it was given could not be played: it holds silence. Once. */
	void failed(snurra_tile *tile);
	/* Grouped: what was asked of the tile, for the group to do. */
	void play_requested(snurra_tile *tile, bool playing);
	void seek_requested(snurra_tile *tile, qint64 position_us);
	void nudge_requested(snurra_tile *tile, qint64 delta_us);
	/* The waveform dragged with Ctrl: its lane moved along the timeline by
	 * `delta_us` from where the drag began; `done` on the release; `snap`
	 * unless Shift is held. */
	void shift_requested(snurra_tile *tile, qint64 delta_us, bool done, bool snap);
	void sync_requested(snurra_tile *tile);
	void key_sync_requested(snurra_tile *tile);
	void controls_changed(snurra_tile *tile);
	/* Files, folders or links dropped on the full-screen picture (a window
	 * of its own), for the window to add; nothing takes them embedded. */
	void files_dropped(const QStringList &files);
	/* A drawer opened, closed or changed height: the window re-fits. */
	void drawers_changed(snurra_tile *tile);

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;

private:
	void seek(qint64 position_us);
	void tick();
	void show_envelope();
	void show_jumps();
	qint64 jump_us(bool large) const;
	void read_video(bool open, std::shared_ptr<snurra_bytes> bytes = nullptr);
	void read_beats(std::shared_ptr<snurra_bytes> bytes);
	void build_video_drawer(bool open);
	void fit_video_drawer();
	void show_video_frame();
	void find_subtitles();
	void say_subtitles();
	void read_peaks(std::shared_ptr<snurra_bytes> bytes);

	snurra_tile_state m_state;
	snurra_deck *m_deck;
	QLabel *m_title;
	snurra_waveform *m_waveform;
	QToolButton *m_pull;
	static QString drawer_style(const QString &name);
	static QString clock_text(qint64 us);
	QLabel *m_time = nullptr;
	QVBoxLayout *m_column = nullptr;
	QPointer<QWidget> m_above;      /* the upward drawer */
	QPointer<QWidget> m_controls;   /* the downward drawer */
	bool m_drawers_out = false;
	QHBoxLayout *m_top = nullptr;
	std::unique_ptr<snurra_video> m_video;
	snurra_video_view *m_view = nullptr;
	QToolButton *m_maximize = nullptr;
	QToolButton *m_video_toggle = nullptr;
	QTimer *m_frames = nullptr;
	QPointer<QWidget> m_full;   /* the picture full screen, while it is */
	bool m_full_wanted = false;   /* asked for before the picture was found */
	std::vector<std::shared_ptr<snurra_subtitles>> m_subtitle_tracks;
	int m_subtitle = -1;            /* the track chosen, or none */
	bool m_subtitles_shown = true;
	qint64 m_subtitle_delay = 0;    /* microseconds the subtitles come later */
	qint64 m_sound_delay = 0;       /* mplayer's -delay: the picture is shown this much ahead */
	bool m_clock = false;           /* mplayer's o: the time over the picture */
	QElapsedTimer m_awake;          /* since the screen saver was last put off */
	QStringList m_subtitle_files;   /* a host's, offered first */
	QPair<double, double> m_crossfade{ 0, 0 };   /* a train's: in and out, of the track */
	QToolButton *m_play;
	QSlider *m_position;
	QSlider *m_level;
	QSlider *m_tempo = nullptr;
	static constexpr int TEMPO_RANGE = 160;   /* tenths of a per cent: +-16 % */
	static constexpr int TEMPO_SNAP = 5;      /* +-0.5 %, dragged, is the middle */
	QLabel *m_tempo_label = nullptr;
	QLabel *m_beat_label = nullptr;
	class QDial *m_key_shift = nullptr;
	class QToolButton *m_key_sync = nullptr;
	void show_beat();
	int key_shift() const;
	QToolButton *m_key_lock = nullptr;
	QToolButton *m_sync = nullptr;
	QToolButton *m_cue = nullptr;
	std::vector<snurra_chapter> m_chapters;
	int m_chapter_shown = -1;
	QString m_title_base;   /* the title before a chapter's is added */
	int chapter_at(qint64 us) const;
	void show_chapters();
	class QDial *m_eq[3] = { nullptr, nullptr, nullptr };
	void show_cue();
	QToolButton *m_back = nullptr;
	QToolButton *m_forward = nullptr;
	std::unique_ptr<snurra_beats> m_beats;
	snurra_beat_grid m_grid;
	QToolButton *m_shape;   /* the envelope control: the waveform edits it */
	/* Set when the tile owns its deck. Guarded: an engine destroyed first
	 * has deleted its decks already, and is not to be called. */
	QPointer<snurra_engine> m_owner;
	bool m_grouped = false;
	bool m_sync_available = false;   /* offered outside a group */
	bool m_failed_said = false;
	bool m_settled = false;   /* constructed, readers started: a failure can be judged */
	int m_muted_from = -1;   /* the level before a mute; -1: not muted */
	QPoint m_press;
	bool m_dragging = false;
	/* Last, so it is destroyed first: its thread is joined before the
	 * waveform it hands peaks to goes. */
	std::unique_ptr<snurra_peaks> m_peaks;
};

#endif
