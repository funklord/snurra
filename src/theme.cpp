#include "snurra/theme.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QHash>
#include <QStyleFactory>
#include <QStyleHints>
#include <QTextStream>

namespace {

/* Rec. 709 luma: any correct luminance answers a background against its
 * own foreground, the two being far apart in any scheme one can read. */
double luma(const QColor &colour) {
	return 0.2126 * colour.redF() + 0.7152 * colour.greenF() + 0.0722 * colour.blueF();
}

/* The keys of one [section] of an INI-like file, as written. */
QHash<QString, QString> section_of(const QString &path, const QString &section) {
	QHash<QString, QString> keys;
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text) || file.size() > 1024 * 1024) return keys;
	QTextStream in(&file);
	bool inside = false;
	const QString header = QLatin1Char('[') + section + QLatin1Char(']');
	while (!in.atEnd()) {
		const QString line = in.readLine().trimmed();
		if (line.startsWith(QLatin1Char('['))) {
			inside = line == header;
			continue;
		}
		const qsizetype equals = line.indexOf(QLatin1Char('='));
		if (inside && equals > 0) keys.insert(line.left(equals).trimmed(), line.mid(equals + 1).trimmed());
	}
	return keys;
}

/* "r,g,b", as kdeglobals writes a colour; invalid otherwise. */
QColor from_triple(const QString &text) {
	const QStringList parts = text.split(QLatin1Char(','));
	if (parts.size() != 3) return QColor();
	int rgb[3];
	for (int i = 0; i < 3; ++i) {
		bool number = false;
		rgb[i] = parts.at(i).trimmed().toInt(&number);
		if (!number || rgb[i] < 0 || rgb[i] > 255) return QColor();
	}
	return QColor(rgb[0], rgb[1], rgb[2]);
}

QString home_or(const char *variable, const QString &fallback) {
	const QString set = qEnvironmentVariable(variable);
	return set.isEmpty() ? fallback : set;
}

}   // namespace

snurra_scheme snurra_scheme_of(const QColor &background, const QColor &foreground) {
	if (!background.isValid() || !foreground.isValid()) return snurra_scheme::unknown;
	return luma(background) < luma(foreground) ? snurra_scheme::dark : snurra_scheme::light;
}

snurra_scheme snurra_scheme_from_kdeglobals(const QString &path) {
	const QHash<QString, QString> general = section_of(path, QStringLiteral("General"));
	return snurra_scheme_of(from_triple(general.value(QStringLiteral("windowBackground"))),
	                        from_triple(general.value(QStringLiteral("windowForeground"))));
}

snurra_scheme snurra_scheme_from_lxqt(const QString &path) {
	const QHash<QString, QString> palette = section_of(path, QStringLiteral("Palette"));
	return snurra_scheme_of(QColor(palette.value(QStringLiteral("window_color"))),
	                        QColor(palette.value(QStringLiteral("window_text_color"))));
}

snurra_scheme snurra_desktop_scheme() {
	const QByteArray asked = qgetenv("SNURRA_THEME");
	if (asked == "dark") return snurra_scheme::dark;
	if (asked == "light") return snurra_scheme::light;

	switch (QGuiApplication::styleHints()->colorScheme()) {
	case Qt::ColorScheme::Dark: return snurra_scheme::dark;
	case Qt::ColorScheme::Light: return snurra_scheme::light;
	default: break;
	}

	/* The files desktops with no dark status write, the running
	 * session's first: an old one of another desktop says nothing about
	 * this one. */
	const QString home = QDir::homePath();
	const QString trinity = home_or("TDEHOME", home + QStringLiteral("/.trinity")) + QStringLiteral("/share/config/kdeglobals");
	const QString kde3 = home_or("KDEHOME", home + QStringLiteral("/.kde")) + QStringLiteral("/share/config/kdeglobals");
	const QString lxqt = home_or("XDG_CONFIG_HOME", home + QStringLiteral("/.config")) + QStringLiteral("/lxqt/lxqt.conf");
	const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP").toUpper();
	struct file_t {
		QString path;
		snurra_scheme (*read)(const QString &);
		bool session;
	};
	const file_t files[] = { { trinity, snurra_scheme_from_kdeglobals, desktop.contains(QLatin1String("TDE")) },
		                 { kde3, snurra_scheme_from_kdeglobals, desktop.contains(QLatin1String("KDE")) },
		                 { lxqt, snurra_scheme_from_lxqt, desktop.contains(QLatin1String("LXQT")) } };
	for (const file_t &file : files)
		if (file.session)
			if (const snurra_scheme said = file.read(file.path); said != snurra_scheme::unknown) return said;
	if (desktop.isEmpty())
		for (const file_t &file : files)
			if (const snurra_scheme said = file.read(file.path); said != snurra_scheme::unknown) return said;

	const QPalette given = QGuiApplication::palette();
	return snurra_scheme_of(given.color(QPalette::Window), given.color(QPalette::WindowText));
}

QPalette snurra_dark_palette() {
	QPalette dark;
	const QColor ground(0x2b, 0x2b, 0x2b);
	const QColor text(0xe0, 0xe0, 0xe0);
	dark.setColor(QPalette::Window, ground);
	dark.setColor(QPalette::WindowText, text);
	dark.setColor(QPalette::Base, QColor(0x1f, 0x1f, 0x1f));
	dark.setColor(QPalette::AlternateBase, QColor(0x26, 0x26, 0x26));
	dark.setColor(QPalette::Text, text);
	dark.setColor(QPalette::Button, QColor(0x36, 0x36, 0x36));
	dark.setColor(QPalette::ButtonText, text);
	dark.setColor(QPalette::BrightText, QColor(0xff, 0x6b, 0x6b));
	dark.setColor(QPalette::ToolTipBase, QColor(0x36, 0x36, 0x36));
	dark.setColor(QPalette::ToolTipText, text);
	dark.setColor(QPalette::PlaceholderText, QColor(0x90, 0x90, 0x90));
	/* The waveform: what has played in the highlight, what is to come in
	 * Mid -- as in Fusion's light scheme, the one lighter than the ground. */
	dark.setColor(QPalette::Highlight, QColor(0x3d, 0x7e, 0xb8));
	dark.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
	dark.setColor(QPalette::Mid, QColor(0x6a, 0x6a, 0x6a));
	dark.setColor(QPalette::Dark, QColor(0x1a, 0x1a, 0x1a));
	dark.setColor(QPalette::Midlight, QColor(0x44, 0x44, 0x44));
	dark.setColor(QPalette::Light, QColor(0x50, 0x50, 0x50));
	dark.setColor(QPalette::Shadow, QColor(0x10, 0x10, 0x10));
	dark.setColor(QPalette::Link, QColor(0x7a, 0xb8, 0xf5));   /* the envelope and the crossfade */
	dark.setColor(QPalette::LinkVisited, QColor(0xb0, 0x9a, 0xf5));
	const QColor faded(0x80, 0x80, 0x80);
	dark.setColor(QPalette::Disabled, QPalette::WindowText, faded);
	dark.setColor(QPalette::Disabled, QPalette::Text, faded);
	dark.setColor(QPalette::Disabled, QPalette::ButtonText, faded);
	return dark;
}

void snurra_follow_desktop(QApplication &app) {
	if (snurra_desktop_scheme() != snurra_scheme::dark) return;
	/* Fusion draws every control from the palette; a native style may
	 * not, and would leave light buttons on the dark ground. */
	if (QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion"))) app.setStyle(fusion);
	app.setPalette(snurra_dark_palette());
}
