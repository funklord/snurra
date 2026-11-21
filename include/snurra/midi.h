#ifndef SNURRA_MIDI_H
#define SNURRA_MIDI_H

#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>

class QTimer;
class QWidget;

#include <functional>
#include <memory>

/*
 * Controllers (project.md sec 3f, step 6): MIDI in through ALSA's
 * sequencer, the controls of the deck view named, and a binding between
 * the two made by holding a control and moving the hardware.
 *
 * A hardware control is named by its index -- the port it came from, its
 * channel, its message and number -- written "<port>/ch<n>/cc<n>",
 * ".../note<n>" or ".../bend". The person may give it a name of their own
 * (an alias), and snurra's controls may be renamed the same way; a
 * binding is a line of text, either side by name or by index:
 *
 *     ch_a_gain -> ch_1_level
 */

struct snurra_midi_event {
	enum kind_t { note_on, note_off, cc, bend };
	QString port;        /* the sending port, "client:port" by name -- stable across a replug */
	int channel = 1;     /* 1 to 16 */
	kind_t kind = cc;
	int number = 0;      /* the note or controller; 0 for bend */
	int value = 0;       /* 0 to 127; bend 0 to 16383 */
	qint64 at_us = 0;    /* snurra_clock_us() when read */

	/* Its control's index, as a binding names it. */
	QString control() const;
};

/*
 * What a hardware control is, told from how it moved (learn below):
 * a button sends notes or 0/127; an absolute knob or fader values across
 * its range; a relative encoder steps around 64, or 1 and 127; a 14-bit
 * one a controller and its partner 32 above; pitch bend a fader of 14
 * bits.
 */
enum class snurra_control_kind { button, absolute, relative, fine, bend };

/*
 * Choosing the one control being moved, against a controller's noise
 * (the holder, 2026-11-20: "we know the one thing that is being tweaked
 * and ignore if unsure until a clear signal"). Fed every event always, so
 * it knows what chatters untouched; start() when the hand goes to the
 * screen's control, then decide().
 */
class snurra_midi_learn {
public:
	/* Every event, before and during a learn. */
	void see(const snurra_midi_event &event);
	/* The hand is on a control now: what moved in the second before is
	 * chatter, set aside unless it moves far more than it did. */
	void start(qint64 now_us);
	void stop();
	bool learning() const { return m_learning; }

	struct verdict {
		enum state_t { waiting, clear, unsure } state = waiting;
		QString control;     /* the index, when clear */
		snurra_control_kind kind = snurra_control_kind::absolute;
		QString why;         /* in words for the person, when unsure */
	};
	/* Clear as soon as one control alone has moved plainly; unsure, at
	 * `final`, where two have or none has. */
	verdict decide(bool final) const;

private:
	struct moved {
		int messages = 0;
		int low = 16383, high = 0;
		bool note = false;
		bool near_middle = true;   /* every value within 61..67, or 1 / 127: a relative encoder */
		snurra_midi_event::kind_t kind = snurra_midi_event::cc;
	};
	static void add(moved &into, const snurra_midi_event &event);
	static bool plain(const moved &m);
	QList<snurra_midi_event> m_recent;    /* the last second before a learn */
	QMap<QString, moved> m_before;        /* chatter: what moved in that second */
	QMap<QString, moved> m_during;
	bool m_learning = false;
};

/*
 * MIDI in: a sequencer client "snurra" with one input port, connected
 * from every port that sends -- the hardware's and other programs' --
 * and from each one that appears later. Linux only (ALSA); elsewhere
 * open() is false.
 */
class snurra_midi_in : public QObject {
	Q_OBJECT

public:
	explicit snurra_midi_in(QObject *parent = nullptr);
	~snurra_midi_in() override;
	bool open();
	/* Its client and port, for a test's sender to connect to. */
	int client() const;
	int port() const;
	/* A message back to every controller that has sent one and takes
	 * them (its lights): a note on of `value`, or a controller's. */
	void send(int channel, bool note, int number, int value);

signals:
	/* A controller that takes messages was heard from the first time:
	 * what it should show is sent again. */
	void controller_seen();

	void received(const snurra_midi_event &event);

private:
	class impl;
	std::unique_ptr<impl> d;
};

/*
 * The bindings and the names: what the person chose, kept in the
 * settings (`controllers/bindings`, `controllers/aliases`), and only what
 * they chose -- learning one binding writes that one.
 */
class snurra_bindings {
public:
	/* `index` -> `control`, replacing what that index had. */
	void bind(const QString &index, const QString &control, snurra_control_kind kind);
	void unbind(const QString &index);
	/* The control an event moves, and how; empty where none. A binding
	 * from any port -- its index's port written as a star, as an imported
	 * mapping makes -- moves it where none names the port it came from. */
	QString control_for(const QString &index, snurra_control_kind *kind = nullptr, bool *inverted = nullptr) const;
	/* A name the person gave a hardware index. */
	void set_alias(const QString &index, const QString &name);
	QString name_of(const QString &index) const;
	/* And one they gave a control of snurra's (the holder: "we allow
	 * (re-)naming, the name is just an alias"): `deck_a_volume` for
	 * `ch_1_level`. Written so in the text, read back through it. */
	void set_control_name(const QString &control, const QString &name);
	QString control_name(const QString &control) const;

	/* As text, a line a binding: "name_or_index -> control (kind)", the
	 * kind "absolute, inverted" where it reads upside down. */
	QStringList lines() const;
	/* Read back; a line naming an alias resolves through it. False where
	 * a line is not a binding. */
	bool read(const QStringList &lines);

	/*
	 * A Mixxx controller mapping (its .midi.xml): the plain bindings onto
	 * the deck view's controls, from any port, replacing an earlier
	 * import's and leaving what was learned. A control bound to a script
	 * is not taken -- snurra runs no mapping's JavaScript (the holder,
	 * 2026-11-21) -- nor one snurra has no control for. The count bound,
	 * or -1 where the file is not a mapping; `skipped` a line for each
	 * control not taken, saying why.
	 */
	int import_mixxx(const QByteArray &xml, QStringList *skipped = nullptr);

	/*
	 * A light on the controller showing a control's state -- a deck
	 * playing, a listener's plug in it, its key lock -- as a mapping's
	 * <outputs> say: `on` sent while the control's value (0 or 1) is from
	 * `minimum` to `maximum`, `off` otherwise. Kept as text, a line each:
	 * "ch_1_play -> ch1/note59 127 0 0.5 1".
	 */
	struct light {
		QString control;
		int channel = 1;
		bool note = true;    /* a note on, or a controller */
		int number = 0;
		int on = 127, off = 0;
		double minimum = 0.5, maximum = 1;
	};
	const QList<light> &lights() const { return m_lights; }

	void load();   /* from the settings */
	void save() const;

private:
	struct bound {
		QString control;
		snurra_control_kind kind = snurra_control_kind::absolute;
		/* Read upside down: a fader at its top is the control's bottom
		 * (a mapping's tempo fader or crossfader made the other way). */
		bool inverted = false;
	};
	QMap<QString, bound> m_bound;      /* by index */
	QMap<QString, QString> m_alias;    /* index -> name */
	QMap<QString, QString> m_names;    /* control -> name */
	QList<light> m_lights;
};

/*
 * A player's controllers: MIDI in, its bindings, and learning by Alt held
 * with the pointer pressed on a named control. Each player is a process
 * of its own and names its controls within it (the holder: "they can't
 * live in the same namespace"); a binding reaches one player by naming it
 * -- `emergency:play` -- and a name with none is the deck view's. So two
 * processes may share a controller, each taking what is meant for it.
 */
class snurra_controller : public QObject {
	Q_OBJECT

public:
	using apply_t = std::function<void(const QString &control, snurra_control_kind kind, const snurra_midi_event &event)>;
	/* `player`: this process's name, empty for the deck view. */
	snurra_controller(const QString &player, apply_t apply, QObject *parent);
	/* MIDI in, and the bindings the person made; false without MIDI. */
	bool open();
	/* `widget` is the control `control` of this player: Alt held and the
	 * pointer pressed on it learns a binding to it. A name ending "_eq"
	 * is three, by the third of the widget under the pointer. */
	void name_widget(QWidget *widget, const QString &control);
	/* While on, a press on a named control learns as Alt held with it
	 * does -- for a desktop whose window manager takes Alt with the
	 * pointer to move windows (TDE's and KDE 3's do by default). */
	void set_learning(bool on) { m_learn_mode = on; }
	bool learning_mode() const { return m_learn_mode; }
	snurra_bindings &bindings() { return m_bindings; }
	/* An event as MIDI in gives it -- a test's way in. */
	void take(const snurra_midi_event &event) { received(event); }
	/* What a light's control is now, 0 or 1; the lights are sent as it
	 * changes, looked at ten times a second. */
	void show_lights(std::function<double(const QString &control)> value_of);

private:
	void received(const snurra_midi_event &event);
	/* A fine control's halves as they last came: by its coarse index. */
	QMap<QString, int> m_coarse, m_finer;
	void start(const QString &control);
	void finish(bool final);
	QString m_player;
	apply_t m_apply;
	snurra_midi_in *m_midi = nullptr;
	snurra_bindings m_bindings;
	snurra_midi_learn m_learn;
	QString m_learning;
	bool m_learn_mode = false;
	qint64 m_loaded_us = 0;
	QTimer *m_timer = nullptr;
	QTimer *m_lights_timer = nullptr;
	std::function<double(const QString &control)> m_value_of;
	QMap<int, int> m_lit;   /* by light: the value last sent */
	void send_lights();
	QObject *m_filter = nullptr;
};

/* "button", "absolute" ... and back; unknown reads as absolute. */
QString snurra_control_kind_name(snurra_control_kind kind);
snurra_control_kind snurra_control_kind_from(const QString &name);

/*
 * A relative encoder's step from its value. Controllers send one of two
 * ways: about 64 (65 up one, 63 down one) or two's complement (1 up one,
 * 127 down one). Either way a step is small, so the value says which: up
 * to 31 is up from 0, 33 to 95 is about 64, 97 and over down from 128.
 */
int snurra_relative_step(int value);

#endif
