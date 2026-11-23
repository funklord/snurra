#ifndef SNURRA_PALETTE_H
#define SNURRA_PALETTE_H

class QWidget;

/*
 * Colours in the library's own widgets, from whatever palette the host or
 * the player gives (project.md sec 3b). Whether the desktop is dark is the
 * player's question, answered by qtkit's theme; a host keeps its own.
 */

/* A label said quieter: its text two thirds of the way from its ground,
 * in whatever palette it has. Not QPalette::PlaceholderText, which
 * Android's default palette leaves unseen (2026-11-13: the EQ's names,
 * the library's line and an empty deck's word were all missing). */
void snurra_mute(QWidget *label);

#endif
