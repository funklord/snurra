/*
 * Controllers (midi.h, project.md sec 3f step 6): MIDI in through ALSA's
 * sequencer, learning which control the hand moves, and the bindings.
 */
#include "snurra/midi.h"

#include <QCursor>
#include <QMouseEvent>
#include <QSettings>
#include <QTimer>
#include <QToolTip>
#include <QWidget>
#include <QSocketNotifier>

#include <algorithm>

#include "snurra/clock.h"

#if SNURRA_ALSA
#include <alsa/asoundlib.h>
#endif

QString snurra_midi_event::control() const {
	const QString head = port + QStringLiteral("/ch%1/").arg(channel);
	switch (kind) {
	case note_on:
	case note_off:
		return head + QStringLiteral("note%1").arg(number);
	case bend:
		return head + QStringLiteral("bend");
	case cc:
	default:
		return head + QStringLiteral("cc%1").arg(number);
	}
}

/* ---- learning ---------------------------------------------------------- */

namespace {

constexpr qint64 BEFORE_US = 1000000;   /* what moved this long before the hand is chatter */
constexpr int PLAIN_MESSAGES = 4;       /* a knob turned on purpose sends a run of them */
constexpr int PLAIN_RANGE = 12;         /* of 127: more than a pot's jitter */

}   // namespace

void snurra_midi_learn::add(moved &into, const snurra_midi_event &event) {
	++into.messages;
	into.kind = event.kind;
	if (event.kind == snurra_midi_event::note_on || event.kind == snurra_midi_event::note_off) {
		into.note = true;
		return;
	}
	into.low = std::min(into.low, event.value);
	into.high = std::max(into.high, event.value);
	const bool middle = event.value >= 61 && event.value <= 67;
	const bool step = event.value == 1 || event.value == 127;
	if (event.kind != snurra_midi_event::cc || !(middle || step)) into.near_middle = false;
}

bool snurra_midi_learn::plain(const moved &m) {
	if (m.note) return true;
	if (m.messages < PLAIN_MESSAGES) return false;
	/* A relative encoder never leaves the middle, so its run of steps is
	 * its movement; anything else must cover some of its range. */
	if (m.near_middle) return true;
	const int range = m.kind == snurra_midi_event::bend ? (m.high - m.low) / 128 : m.high - m.low;
	return range >= PLAIN_RANGE;
}

void snurra_midi_learn::see(const snurra_midi_event &event) {
	if (m_learning) {
		add(m_during[event.control()], event);
		return;
	}
	m_recent << event;
	while (!m_recent.isEmpty() && m_recent.first().at_us < event.at_us - BEFORE_US) m_recent.removeFirst();
}

void snurra_midi_learn::start(qint64 now_us) {
	m_before.clear();
	m_during.clear();
	for (const snurra_midi_event &event : m_recent)
		if (event.at_us >= now_us - BEFORE_US) add(m_before[event.control()], event);
	m_recent.clear();
	m_learning = true;
}

void snurra_midi_learn::stop() {
	m_learning = false;
	m_during.clear();
	m_before.clear();
}

snurra_midi_learn::verdict snurra_midi_learn::decide(bool final) const {
	verdict found;
	QStringList plainly;
	for (auto it = m_during.constBegin(); it != m_during.constEnd(); ++it) {
		if (!plain(it.value())) continue;
		/* Chatter set aside, unless it now moves far more than it did. */
		const auto was = m_before.constFind(it.key());
		if (was != m_before.constEnd() && !it.value().note
		    && it.value().messages < 4 * std::max(1, was->messages)
		    && (it.value().high - it.value().low) <= 2 * (was->high - was->low) + PLAIN_RANGE)
			continue;
		plainly << it.key();
	}
	/* A 14-bit control is a controller and its partner 32 above moving
	 * together: one control, named by the coarse half. */
	if (plainly.size() == 2) {
		const QString a = plainly[0], b = plainly[1];
		const QString stem_a = a.section(QLatin1String("/cc"), 0, 0), stem_b = b.section(QLatin1String("/cc"), 0, 0);
		if (a.contains(QLatin1String("/cc")) && b.contains(QLatin1String("/cc")) && stem_a == stem_b) {
			const int na = a.section(QLatin1String("/cc"), 1).toInt(), nb = b.section(QLatin1String("/cc"), 1).toInt();
			if (std::abs(na - nb) == 32) {
				found.state = verdict::clear;
				found.control = na < nb ? a : b;
				found.kind = snurra_control_kind::fine;
				return found;
			}
		}
	}
	if (plainly.size() == 1) {
		const moved &m = m_during.value(plainly.first());
		found.state = verdict::clear;
		found.control = plainly.first();
		if (m.note) found.kind = snurra_control_kind::button;
		else if (m.kind == snurra_midi_event::bend) found.kind = snurra_control_kind::bend;
		else if (m.near_middle) found.kind = snurra_control_kind::relative;
		else if (m.low == 0 && m.high == 127 && m.messages <= 6) found.kind = snurra_control_kind::button;
		else found.kind = snurra_control_kind::absolute;
		return found;
	}
	if (!final) return found;
	found.state = verdict::unsure;
	found.why = plainly.isEmpty() ? QObject::tr("No control moved clearly. Hold on and move one.")
	                              : QObject::tr("%1 controls moved at once. Move just one.").arg(plainly.size());
	return found;
}

/* ---- the bindings ------------------------------------------------------ */

QString snurra_control_kind_name(snurra_control_kind kind) {
	switch (kind) {
	case snurra_control_kind::button: return QStringLiteral("button");
	case snurra_control_kind::relative: return QStringLiteral("relative");
	case snurra_control_kind::fine: return QStringLiteral("fine");
	case snurra_control_kind::bend: return QStringLiteral("bend");
	case snurra_control_kind::absolute:
	default: return QStringLiteral("absolute");
	}
}

snurra_control_kind snurra_control_kind_from(const QString &name) {
	for (snurra_control_kind kind : { snurra_control_kind::button, snurra_control_kind::relative, snurra_control_kind::fine,
	                                  snurra_control_kind::bend })
		if (snurra_control_kind_name(kind) == name) return kind;
	return snurra_control_kind::absolute;
}

void snurra_bindings::bind(const QString &index, const QString &control, snurra_control_kind kind) {
	/* One hardware control moves one of snurra's; a control moved by one
	 * hardware control at a time, as a learn replaces what it had. */
	for (auto it = m_bound.begin(); it != m_bound.end();) {
		if (it.value().control == control) it = m_bound.erase(it);
		else ++it;
	}
	m_bound.insert(index, bound{ control, kind });
}

void snurra_bindings::unbind(const QString &index) {
	m_bound.remove(index);
}

QString snurra_bindings::control_for(const QString &index, snurra_control_kind *kind) const {
	const auto it = m_bound.constFind(index);
	if (it == m_bound.constEnd()) return {};
	if (kind) *kind = it->kind;
	return it->control;
}

void snurra_bindings::set_alias(const QString &index, const QString &name) {
	if (name.isEmpty()) m_alias.remove(index);
	else m_alias.insert(index, name);
}

QString snurra_bindings::name_of(const QString &index) const {
	return m_alias.value(index, index);
}

void snurra_bindings::set_control_name(const QString &control, const QString &name) {
	if (name.isEmpty() || name == control) m_names.remove(control);
	else m_names.insert(control, name);
}

QString snurra_bindings::control_name(const QString &control) const {
	return m_names.value(control, control);
}

QStringList snurra_bindings::lines() const {
	QStringList made;
	for (auto it = m_bound.constBegin(); it != m_bound.constEnd(); ++it)
		made << QStringLiteral("%1 -> %2 (%3)").arg(name_of(it.key()), control_name(it->control), snurra_control_kind_name(it->kind));
	return made;
}

bool snurra_bindings::read(const QStringList &lines) {
	QMap<QString, bound> got;
	for (const QString &line : lines) {
		if (line.trimmed().isEmpty()) continue;
		const int arrow = int(line.indexOf(QLatin1String(" -> ")));
		if (arrow <= 0) return false;
		QString from = line.left(arrow).trimmed();
		QString to = line.mid(arrow + 4).trimmed();
		snurra_control_kind kind = snurra_control_kind::absolute;
		if (to.endsWith(QLatin1Char(')')) && to.contains(QLatin1String(" ("))) {
			kind = snurra_control_kind_from(to.section(QLatin1String(" ("), 1).chopped(1));
			to = to.section(QLatin1String(" ("), 0, 0);
		}
		if (from.isEmpty() || to.isEmpty()) return false;
		/* A name the person gave: back to the index it stands for, and to
		 * the control. */
		for (auto it = m_alias.constBegin(); it != m_alias.constEnd(); ++it)
			if (it.value() == from) from = it.key();
		for (auto it = m_names.constBegin(); it != m_names.constEnd(); ++it)
			if (it.value() == to) to = it.key();
		got.insert(from, bound{ to, kind });
	}
	m_bound = got;
	return true;
}

void snurra_bindings::load() {
	QSettings settings(QStringLiteral("snurra"), QStringLiteral("snurra"));
	m_alias.clear();
	const QStringList aliases = settings.value(QStringLiteral("controllers/aliases")).toStringList();
	for (const QString &line : aliases) {
		const int eq = int(line.indexOf(QLatin1String(" = ")));
		if (eq > 0) m_alias.insert(line.mid(eq + 3).trimmed(), line.left(eq).trimmed());
	}
	m_names.clear();
	for (const QString &line : settings.value(QStringLiteral("controllers/names")).toStringList()) {
		const int eq = int(line.indexOf(QLatin1String(" = ")));
		if (eq > 0) m_names.insert(line.mid(eq + 3).trimmed(), line.left(eq).trimmed());
	}
	read(settings.value(QStringLiteral("controllers/bindings")).toStringList());
}

void snurra_bindings::save() const {
	QSettings settings(QStringLiteral("snurra"), QStringLiteral("snurra"));
	QStringList aliases;
	for (auto it = m_alias.constBegin(); it != m_alias.constEnd(); ++it) aliases << it.value() + QStringLiteral(" = ") + it.key();
	/* Only what was chosen: nothing bound and named writes no key. */
	if (aliases.isEmpty()) settings.remove(QStringLiteral("controllers/aliases"));
	else settings.setValue(QStringLiteral("controllers/aliases"), aliases);
	QStringList names;
	for (auto it = m_names.constBegin(); it != m_names.constEnd(); ++it) names << it.value() + QStringLiteral(" = ") + it.key();
	if (names.isEmpty()) settings.remove(QStringLiteral("controllers/names"));
	else settings.setValue(QStringLiteral("controllers/names"), names);
	if (m_bound.isEmpty()) settings.remove(QStringLiteral("controllers/bindings"));
	else settings.setValue(QStringLiteral("controllers/bindings"), lines());
}

/* ---- MIDI in ----------------------------------------------------------- */

class snurra_midi_in::impl {
public:
#if SNURRA_ALSA
	snd_seq_t *seq = nullptr;
	int port = -1;
	QList<QSocketNotifier *> notifiers;

	/* "client:port" by name, as it says itself. */
	QString name_of(int client, int port_number) const {
		snd_seq_client_info_t *client_info;
		snd_seq_port_info_t *port_info;
		snd_seq_client_info_alloca(&client_info);
		snd_seq_port_info_alloca(&port_info);
		QString client_name = QString::number(client), port_name = QString::number(port_number);
		if (snd_seq_get_any_client_info(seq, client, client_info) >= 0)
			client_name = QString::fromUtf8(snd_seq_client_info_get_name(client_info));
		if (snd_seq_get_any_port_info(seq, client, port_number, port_info) >= 0)
			port_name = QString::fromUtf8(snd_seq_port_info_get_name(port_info));
		return client_name + QLatin1Char(':') + port_name;
	}

	/* From a port that sends, unless it is the system's or our own. */
	void connect_from(int client, int port_number) {
		if (client == SND_SEQ_CLIENT_SYSTEM || client == snd_seq_client_id(seq)) return;
		snd_seq_port_info_t *info;
		snd_seq_port_info_alloca(&info);
		if (snd_seq_get_any_port_info(seq, client, port_number, info) < 0) return;
		const unsigned capability = snd_seq_port_info_get_capability(info);
		if ((capability & (SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ)) != (SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ))
			return;
		if (capability & SND_SEQ_PORT_CAP_NO_EXPORT) return;
		snd_seq_connect_from(seq, port, client, port_number);
	}
#endif
};

snurra_midi_in::snurra_midi_in(QObject *parent) : QObject(parent), d(std::make_unique<impl>()) {}

snurra_midi_in::~snurra_midi_in() {
#if SNURRA_ALSA
	qDeleteAll(d->notifiers);
	if (d->seq) snd_seq_close(d->seq);
#endif
}

int snurra_midi_in::client() const {
#if SNURRA_ALSA
	return d->seq ? snd_seq_client_id(d->seq) : -1;
#else
	return -1;
#endif
}

int snurra_midi_in::port() const {
#if SNURRA_ALSA
	return d->port;
#else
	return -1;
#endif
}

bool snurra_midi_in::open() {
#if SNURRA_ALSA
	if (d->seq) return true;
	if (snd_seq_open(&d->seq, "default", SND_SEQ_OPEN_INPUT, SND_SEQ_NONBLOCK) < 0) {
		d->seq = nullptr;
		return false;
	}
	snd_seq_set_client_name(d->seq, "snurra");
	d->port = snd_seq_create_simple_port(d->seq, "controllers", SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
	                                     SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
	if (d->port < 0) {
		snd_seq_close(d->seq);
		d->seq = nullptr;
		return false;
	}
	/* Every port that sends now, and each that appears (the system's
	 * announcements). */
	snd_seq_connect_from(d->seq, d->port, SND_SEQ_CLIENT_SYSTEM, SND_SEQ_PORT_SYSTEM_ANNOUNCE);
	snd_seq_client_info_t *client_info;
	snd_seq_port_info_t *port_info;
	snd_seq_client_info_alloca(&client_info);
	snd_seq_port_info_alloca(&port_info);
	snd_seq_client_info_set_client(client_info, -1);
	while (snd_seq_query_next_client(d->seq, client_info) >= 0) {
		const int client = snd_seq_client_info_get_client(client_info);
		snd_seq_port_info_set_client(port_info, client);
		snd_seq_port_info_set_port(port_info, -1);
		while (snd_seq_query_next_port(d->seq, port_info) >= 0) d->connect_from(client, snd_seq_port_info_get_port(port_info));
	}
	const int count = snd_seq_poll_descriptors_count(d->seq, POLLIN);
	std::vector<pollfd> fds(size_t(std::max(0, count)));
	snd_seq_poll_descriptors(d->seq, fds.data(), unsigned(fds.size()), POLLIN);
	for (const pollfd &fd : fds) {
		QSocketNotifier *notifier = new QSocketNotifier(fd.fd, QSocketNotifier::Read);
		d->notifiers << notifier;
		connect(notifier, &QSocketNotifier::activated, this, [this] {
			snd_seq_event_t *in = nullptr;
			while (snd_seq_event_input(d->seq, &in) >= 0 && in) {
				snurra_midi_event event;
				event.at_us = snurra_clock_us();
				event.port = d->name_of(in->source.client, in->source.port);
				switch (in->type) {
				case SND_SEQ_EVENT_PORT_START:
					d->connect_from(in->data.addr.client, in->data.addr.port);
					continue;
				case SND_SEQ_EVENT_NOTEON:
					event.channel = in->data.note.channel + 1;
					event.number = in->data.note.note;
					event.value = in->data.note.velocity;
					/* A note on of velocity 0 is the common note off. */
					event.kind = event.value > 0 ? snurra_midi_event::note_on : snurra_midi_event::note_off;
					break;
				case SND_SEQ_EVENT_NOTEOFF:
					event.channel = in->data.note.channel + 1;
					event.number = in->data.note.note;
					event.kind = snurra_midi_event::note_off;
					break;
				case SND_SEQ_EVENT_CONTROLLER:
					event.channel = in->data.control.channel + 1;
					event.number = int(in->data.control.param);
					event.value = in->data.control.value;
					event.kind = snurra_midi_event::cc;
					break;
				case SND_SEQ_EVENT_PITCHBEND:
					event.channel = in->data.control.channel + 1;
					event.value = in->data.control.value + 8192;
					event.kind = snurra_midi_event::bend;
					break;
				default:
					/* Clock, active sensing, the rest: never a control. */
					continue;
				}
				emit received(event);
			}
		});
	}
	return true;
#else
	return false;
#endif
}

/* ---- a player's controllers ------------------------------------------- */

namespace {

class event_hook : public QObject {
public:
	explicit event_hook(std::function<bool(QObject *, QEvent *)> hook, QObject *parent) : QObject(parent), m_hook(std::move(hook)) {}
	bool eventFilter(QObject *watched, QEvent *event) override { return m_hook(watched, event); }

private:
	std::function<bool(QObject *, QEvent *)> m_hook;
};

const char *const CONTROL_PROPERTY = "snurra_control";

}   // namespace

snurra_controller::snurra_controller(const QString &player, apply_t apply, QObject *parent)
    : QObject(parent), m_player(player), m_apply(std::move(apply)) {}

bool snurra_controller::open() {
	if (m_midi) return true;
	m_midi = new snurra_midi_in(this);
	if (!m_midi->open()) {
		delete m_midi;
		m_midi = nullptr;
		return false;
	}
	m_bindings.load();
	m_loaded_us = snurra_clock_us();
	connect(m_midi, &snurra_midi_in::received, this, [this](const snurra_midi_event &event) { received(event); });
	return true;
}

void snurra_controller::name_widget(QWidget *widget, const QString &control) {
	if (!widget) return;
	widget->setProperty(CONTROL_PROPERTY, control);
	if (!m_filter) {
		m_filter = new event_hook(
		        [this](QObject *watched, QEvent *event) {
			        QWidget *widget = qobject_cast<QWidget *>(watched);
			        const QString name = widget ? widget->property(CONTROL_PROPERTY).toString() : QString();
			        if (name.isEmpty() || !m_midi) return false;
			        if (event->type() == QEvent::MouseButtonPress
			            && (static_cast<QMouseEvent *>(event)->modifiers() & Qt::AltModifier)) {
				        QString control = name;
				        /* An EQ is one line over three bands: the band under the hand. */
				        if (control.endsWith(QLatin1String("_eq"))) {
					        const int third = int(3 * static_cast<QMouseEvent *>(event)->position().x() / std::max(1, widget->width()));
					        control += third <= 0 ? QStringLiteral("_low") : third == 1 ? QStringLiteral("_mid") : QStringLiteral("_high");
				        }
				        start(control);
				        return true;   /* a learn, not a use of the control */
			        }
			        if (event->type() == QEvent::MouseButtonRelease && !m_learning.isEmpty()) {
				        finish(true);
				        return true;
			        }
			        return false;
		        },
		        this);
	}
	widget->installEventFilter(m_filter);
}

void snurra_controller::start(const QString &control) {
	m_learning = control;
	m_learn.start(snurra_clock_us());
	if (!m_timer) {
		m_timer = new QTimer(this);
		m_timer->setInterval(20);
		connect(m_timer, &QTimer::timeout, this, [this] { finish(false); });
	}
	m_timer->start();
	QToolTip::showText(QCursor::pos(), tr("Move the control for %1").arg(control));
}

void snurra_controller::finish(bool final) {
	if (m_learning.isEmpty()) return;
	const snurra_midi_learn::verdict found = m_learn.decide(final);
	if (found.state == snurra_midi_learn::verdict::waiting) return;
	if (found.state == snurra_midi_learn::verdict::clear) {
		/* Named for this player, unless it is the deck view's. */
		const QString control = m_player.isEmpty() ? m_learning : m_player + QLatin1Char(':') + m_learning;
		m_bindings.load();   /* another player may have learned since */
		m_bindings.bind(found.control, control, found.kind);
		m_bindings.save();
		QToolTip::showText(QCursor::pos(), QStringLiteral("%1 -> %2").arg(m_bindings.name_of(found.control), control));
	} else {
		QToolTip::showText(QCursor::pos(), found.why);
	}
	m_learning.clear();
	m_learn.stop();
	if (m_timer) m_timer->stop();
}

void snurra_controller::received(const snurra_midi_event &event) {
	m_learn.see(event);
	if (!m_learning.isEmpty()) return;   /* while learning, nothing moves */
	/* Another process may have learned one since: read again, at most
	 * once a second. */
	if (event.at_us - m_loaded_us > 1000000) {
		m_bindings.load();
		m_loaded_us = event.at_us;
	}
	snurra_control_kind kind = snurra_control_kind::absolute;
	QString control = m_bindings.control_for(event.control(), &kind);
	snurra_midi_event heard = event;
	/* A fine control (a controller and its partner 32 above, MIDI's 14
	 * bits): either half moves it, the two read as one value of 0 to
	 * 16383, given on as a bend's -- so a 14-bit fader is not stepped in
	 * 128ths. The finer half is bound by its coarse one. */
	if (event.kind == snurra_midi_event::cc && event.number < 64) {
		QString coarse = event.control();
		snurra_control_kind coarse_kind = kind;
		if (control.isEmpty() && event.number >= 32) {
			snurra_midi_event partner = event;
			partner.number = event.number - 32;
			coarse = partner.control();
			control = m_bindings.control_for(coarse, &coarse_kind);
			if (coarse_kind != snurra_control_kind::fine) control.clear();
			else m_finer[coarse] = event.value;
		} else if (kind == snurra_control_kind::fine) {
			m_coarse[coarse] = event.value;
		}
		if (!control.isEmpty() && coarse_kind == snurra_control_kind::fine) {
			kind = snurra_control_kind::fine;
			heard.kind = snurra_midi_event::bend;
			heard.value = m_coarse.value(coarse) * 128 + m_finer.value(coarse);
		}
	}
	if (control.isEmpty()) return;
	/* Another player's binding is that player's to take. */
	const int colon = int(control.indexOf(QLatin1Char(':')));
	const QString player = colon < 0 ? QString() : control.left(colon);
	if (player != m_player) return;
	if (colon >= 0) control = control.mid(colon + 1);
	m_apply(control, kind, heard);
}
