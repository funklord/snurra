#include "wire.h"

#include <QDataStream>
#include <QDir>
#include <QLocalSocket>
#include <QIODevice>
#include <QStandardPaths>

namespace snurra_wire {

QByteArray encode(const message &m) {
	QByteArray payload;
	QDataStream out(&payload, QIODevice::WriteOnly);
	out.setVersion(QDataStream::Qt_6_0);
	out << quint8(m.type) << m.token << quint8(m.side) << m.states << m.frame << m.path << m.index << m.time
	    << m.started << m.stopped;
	QByteArray framed;
	QDataStream head(&framed, QIODevice::WriteOnly);
	head << quint32(payload.size());
	return framed + payload;
}

QList<message> decode(QByteArray &buffer) {
	QList<message> found;
	while (buffer.size() >= 4) {
		QDataStream head(buffer);
		quint32 size = 0;
		head >> size;
		/* A frame larger than any message this sends is a stranger on the
		 * socket: drop what is buffered rather than wait for it. */
		if (size > 16 * 1024 * 1024) {
			buffer.clear();
			break;
		}
		if (buffer.size() < qsizetype(4 + size)) break;
		QDataStream in(buffer.mid(4, size));
		in.setVersion(QDataStream::Qt_6_0);
		message m;
		quint8 type = 0;
		quint8 side = 0;
		in >> type >> m.token >> side >> m.states >> m.frame >> m.path >> m.index >> m.time >> m.started
		   >> m.stopped;
		m.type = type_t(type);
		m.side = side_t(side);
		buffer.remove(0, 4 + size);
		if (in.status() == QDataStream::Ok) found.append(m);
	}
	return found;
}

QString runtime_directory() {
	const QByteArray override_dir = qgetenv("SNURRA_RUNTIME_DIR");
	QString dir = override_dir.isEmpty()
	        ? QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/snurra")
	        : QString::fromLocal8Bit(override_dir);
	QDir().mkpath(dir);
	return dir;
}

QStringList peer_sockets(const QString &own) {
	QStringList found;
	const QDir dir(runtime_directory());
	for (const QString &name : dir.entryList({ QStringLiteral("*.sock") }, QDir::System | QDir::Files)) {
		const QString path = dir.filePath(name);
		if (path != own) found << path;
	}
	return found;
}

bool any_peer_alive(const QString &own) {
	for (const QString &path : peer_sockets(own)) {
		QLocalSocket probe;
		probe.connectToServer(path);
		if (probe.waitForConnected(100)) return true;
	}
	return false;
}

}
