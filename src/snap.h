#ifndef SNURRA_SNAP_H
#define SNURRA_SNAP_H

#include <QHash>
#include <QRect>
#include <QString>

#include "wire.h"

/*
 * Where a dragged window would join another (project.md sec 3b, 3d): one
 * answer used both by the marker shown while dragging and by the release,
 * so what is shown is what happens.
 */
struct snurra_snap {
	QString peer;                         /* empty: no snap */
	snurra_wire::side_t side = snurra_wire::RIGHT;
	QRect landing;                        /* where the dragged window's tiles go */
	QRect shape;                          /* the group they make, landing included */
};

/* How near two windows' edges must come to snap. */
constexpr int SNURRA_SNAP_PX = 24;

/*
 * `mine` near a peer's right edge joins its train, landing beside its
 * first row; near its bottom edge joins its stack, landing under its
 * first column. The nearest edge wins; nothing within reach is no snap.
 */
snurra_snap snurra_find_snap(const QRect &mine, const QHash<QString, QRect> &peers);

#endif
