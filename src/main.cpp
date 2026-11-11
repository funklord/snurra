#include <QApplication>
#include <QCommandLineParser>
#include <QCursor>
#include <QFileInfo>

#include "node.h"
#include "wire.h"

/*
 * snurra: one tile per process (project.md sec 3b). Started with a file,
 * it opens a tile for it at the pointer -- where the file was clicked in a
 * file manager, on X11. Started with --adopt, it is taking a tile another
 * snurra is letting go, and shows nothing until that tile arrives.
 */
int main(int argc, char *argv[]) {
	QApplication app(argc, argv);
	QCoreApplication::setApplicationName(QStringLiteral("snurra"));

	QCommandLineParser parser;
	parser.addHelpOption();
	parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("What to play."));
	const QCommandLineOption adopt(QStringLiteral("adopt"), QStringLiteral("Take a tile from this socket."),
	                               QStringLiteral("socket"));
	const QCommandLineOption token(QStringLiteral("token"), QStringLiteral("The handover's token."),
	                               QStringLiteral("token"));
	const QCommandLineOption index(QStringLiteral("index"), QStringLiteral("Which tile."), QStringLiteral("n"));
	const QCommandLineOption at(QStringLiteral("at"), QStringLiteral("Where, as x,y."), QStringLiteral("x,y"));
	const QCommandLineOption paused(QStringLiteral("paused"), QStringLiteral("Open without playing."));
	parser.addOptions({ adopt, token, index, at, paused });
	parser.process(app);

	snurra_node node;
	if (!node.listen()) return 1;

	if (parser.isSet(adopt)) {
		const QStringList xy = parser.value(at).split(QLatin1Char(','));
		const QPoint where = xy.size() == 2 ? QPoint(xy.at(0).toInt(), xy.at(1).toInt()) : QPoint();
		node.adopt(parser.value(adopt), parser.value(token), parser.value(index).toInt(), where);
		return app.exec();
	}

	/*
	 * Only the first snurra opens with controls and plays (project.md sec
	 * 3c); one opened while another runs is a waveform, paused, until its
	 * controls are pulled out. Within one launch the first file is first.
	 */
	const QStringList files = parser.positionalArguments();
	bool first = !snurra_wire::any_peer_alive(node.socket_path());
	int column = 0;
	for (const QString &file : files.isEmpty() ? QStringList{ QStringLiteral("untitled") } : files) {
		snurra_tile_state state;
		state.source = QFileInfo::exists(file) ? QFileInfo(file).absoluteFilePath() : file;
		state.duration_ms = 180000;   /* a dummy three minutes until there is audio */
		state.sampled_at = QDateTime::currentMSecsSinceEpoch();
		state.controls = first;
		state.playing = first && !parser.isSet(paused);
		first = false;
		state.column = column++;
		node.add_tile(state);
	}
	node.adjustSize();
	node.move(QCursor::pos() - QPoint(node.width() / 2, node.height() / 2));
	node.show();
	return app.exec();
}
