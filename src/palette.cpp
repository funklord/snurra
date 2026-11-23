#include "snurra/palette.h"

#include <QColor>
#include <QPalette>
#include <QWidget>

void snurra_mute(QWidget *label) {
	QPalette palette = label->palette();
	const QColor text = palette.color(QPalette::WindowText);
	const QColor ground = palette.color(QPalette::Window);
	palette.setColor(QPalette::WindowText, QColor((2 * text.red() + ground.red()) / 3, (2 * text.green() + ground.green()) / 3,
	                                              (2 * text.blue() + ground.blue()) / 3));
	label->setPalette(palette);
}
