#include "snap.h"

snurra_snap snurra_find_snap(const QRect &mine, const QHash<QString, QRect> &peers) {
	snurra_snap best;
	int nearest = SNURRA_SNAP_PX + 1;
	for (auto it = peers.cbegin(); it != peers.cend(); ++it) {
		const QRect theirs = it.value();
		if (theirs.isNull()) continue;
		const int beside = qAbs(mine.left() - (theirs.right() + 1));
		if (beside < nearest && mine.bottom() > theirs.top() && mine.top() < theirs.bottom()) {
			nearest = beside;
			best.peer = it.key();
			best.side = snurra_wire::RIGHT;
			best.landing = QRect(QPoint(theirs.right() + 1, theirs.top()), mine.size());
		}
		const int under = qAbs(mine.top() - (theirs.bottom() + 1));
		if (under < nearest && mine.right() > theirs.left() && mine.left() < theirs.right()) {
			nearest = under;
			best.peer = it.key();
			best.side = snurra_wire::BELOW;
			best.landing = QRect(QPoint(theirs.left(), theirs.bottom() + 1), mine.size());
		}
		if (best.peer == it.key()) best.shape = theirs.united(best.landing);
	}
	return best;
}
