#ifndef SNURRA_THEME_H
#define SNURRA_THEME_H

#include <QColor>
#include <QPalette>
#include <QString>

class QApplication;

/*
 * Whether the desktop is dark (project.md sec 3b), so the player can be.
 * The desktop's own darkness, never its hue: snurra paints its own dark
 * scheme, its accents chosen against its own ground.
 */
enum class snurra_scheme { unknown, light, dark };

/* A background against its own foreground: darker is dark. */
snurra_scheme snurra_scheme_of(const QColor &background, const QColor &foreground);

/* What a desktop that has no dark status writes down instead: TDE's and
 * KDE 3's kdeglobals, [General] windowBackground / windowForeground as
 * "r,g,b"; LXQt's lxqt.conf, [Palette] window_color / window_text_color.
 * Unknown where the file or the pair is not there. */
snurra_scheme snurra_scheme_from_kdeglobals(const QString &path);
snurra_scheme snurra_scheme_from_lxqt(const QString &path);

/*
 * In this order, the first that answers: $SNURRA_THEME (dark or light);
 * Qt's own colour scheme; the desktop's file (kdeglobals, lxqt.conf), the
 * session's first; the palette Qt was given. The order is not the
 * sources' rank: the palette always answers -- light, on a desktop Qt is
 * not integrated with -- so it is asked last, or a file that says dark
 * would never be read.
 */
snurra_scheme snurra_desktop_scheme();

/* snurra's own dark scheme. */
QPalette snurra_dark_palette();

/* The dark scheme put on `app` where the desktop is dark; nothing else
 * otherwise -- a wrong dark is unreadable, a wrong light only plain. */
void snurra_follow_desktop(QApplication &app);

#endif
