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

signals:
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
	/* The control an event moves, and how; empty where none. */
	QString control_for(const QString &index, snurra_control_kind *kind = nullptr) const;
	/* A name the person gave a hardware index. */
	void set_alias(const QString &index, const QString &name);
	QString name_of(const QString &index) const;

	/* As text, a line a binding: "name_or_index -> control". */
	QStringList lines() const;
	/* Read back; a line naming an alias resolves through it. False where
	 * a line is not a binding. */
	bool read(const QStringList &lines);

	void load();   /* from the settings */
	void save() const;

private:
	struct bound {
		QString control;
		snurra_control_kind kind = snurra_control_kind::absolute;
	};
	QMap<QString, bound> m_bound;      /* by index */
	QMap<QString, QString> m_alias;    /* index -> name */
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
	snurra_bindings &bindings() { return m_bindings; }

private:
	void received(const snurra_midi_event &event);
	void start(const QString &control);
	void finish(bool final);
	QString m_player;
	apply_t m_apply;
	snurra_midi_in *m_midi = nullptr;
	snurra_bindings m_bindings;
	snurra_midi_learn m_learn;
	QString m_learning;
	qint64 m_loaded_us = 0;
	QTimer *m_timer = nullptr;
	QObject *m_filter = nullptr;
};

/* "button", "absolute" ... and back; unknown reads as absolute. */
QString snurra_control_kind_name(snurra_control_kind kind);
snurra_control_kind snurra_control_kind_from(const QString &name);

#endif
