#include "estate.h"

#include <QCoreApplication>
#include <QHash>
#include <QPointer>
#include <QTimer>

#include <algorithm>
#include <cstring>
#include <thread>

#include "snurra/library.h"

QByteArray snurra_estate_socket() {
	const QByteArray set = qgetenv("SNURRA_FUZZNET_SOCKET");
	return set.isEmpty() ? QByteArrayLiteral("default") : set;
}

QString snurra_estate_name(const uint8_t root[FZN_RPC_BLOB_HASH_LEN]) {
	return QStringLiteral("fuzznet:")
	       + QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(root), FZN_RPC_BLOB_HASH_LEN).toHex());
}

bool snurra_estate_root(const QString &name, uint8_t root[FZN_RPC_BLOB_HASH_LEN]) {
	if (!name.startsWith(QLatin1String("fuzznet:"))) return false;
	const QByteArray hex = name.mid(8).toLatin1();
	if (hex.size() != 2 * FZN_RPC_BLOB_HASH_LEN) return false;
	for (char c : hex)
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
	const QByteArray bytes = QByteArray::fromHex(hex);
	memcpy(root, bytes.constData(), FZN_RPC_BLOB_HASH_LEN);
	return true;
}

std::vector<snurra_estate_file> snurra_estate_list(QString *error) {
	std::vector<snurra_estate_file> files;
	fzn_rpc_t call;
	fzn_rpc_init(&call);
	if (fzn_rpc_set_node_socket(&call, snurra_estate_socket().constData()) != 0) {
		if (error) *error = QStringLiteral("no socket for the node");
		return files;
	}
	fzn_rpc_library_list_all_t all{};
	const int asked = fzn_rpc_library_list(&call, &all);
	if (asked != FZN_RPC_OK && error) *error = QString::fromUtf8(call.error);
	for (size_t i = 0; i < all.count; ++i) {
		const fzn_rpc_library_list_entry_t &entry = all.entries[i];
		snurra_estate_file file;
		file.name = QString::fromUtf8(entry.name, int(entry.name_len));
		file.kind = QString::fromUtf8(entry.kind, int(entry.kind_len));
		file.root = QByteArray(reinterpret_cast<const char *>(entry.root), sizeof entry.root);
		file.size = qint64(entry.size);
		file.duration_ms = qint64(entry.duration_ms);
		file.holders = entry.holders;
		file.here = entry.held_here != 0;
		files.push_back(file);
	}
	fzn_rpc_library_list_release(&all);
	return files;
}

snurra_estate_bytes::snurra_estate_bytes(const uint8_t root[FZN_RPC_BLOB_HASH_LEN])
    : m_reply(std::make_unique<fzn_rpc_library_play_resp_t>()) {
	fzn_rpc_init(&m_call);
	(void)fzn_rpc_set_node_socket(&m_call, snurra_estate_socket().constData());
	memcpy(m_root, root, FZN_RPC_BLOB_HASH_LEN);
	/* The size, and how much is here, from a one-byte read: the status
	 * probe the node's own description names. */
	std::lock_guard<std::mutex> hold(m_lock);
	const int probed = fzn_rpc_library_play(&m_call, m_root, 0, 1, m_reply.get());
	if (probed == FZN_RPC_OK) {
		took(*m_reply);
		return;
	}
	/* None of it here yet: a file another host holds, which this node reads
	 * only once a fetch has begun. fuzznet says so with its own code since
	 * bbe14fc (it said "not in the library" before, 2026-11-14), and leaves
	 * beginning it to the player, a probe being what a listing makes of
	 * every row. Opened to be played, it is begun, from the start; the
	 * reader moves it where it seeks (want). Any other failure is left. */
	m_error = QString::fromUtf8(m_call.error);
	if (probed != FZN_RPC_ERR_NOT_FETCHED) return;
	if (fzn_rpc_blob_fetch_at(&m_call, m_root, 0) != FZN_RPC_OK) {
		m_error = QString::fromUtf8(m_call.error);
		return;
	}
	m_begun = true;
	if (fzn_rpc_library_play(&m_call, m_root, 0, 1, m_reply.get()) == FZN_RPC_OK) {
		took(*m_reply);
		m_error.clear();
	}
}

QString snurra_estate_bytes::last_error() {
	std::lock_guard<std::mutex> hold(m_lock);
	return m_error;
}

void snurra_estate_bytes::took(const fzn_rpc_library_play_resp_t &reply) {
	if (reply.size > 0) m_size.store(int64_t(reply.size));
	m_playable.store(int64_t(reply.playable));
	m_complete.store(reply.complete != 0);
}

int64_t snurra_estate_bytes::read(int64_t offset, uint8_t *out, int64_t n) {
	if (offset < 0 || n <= 0) return 0;
	const int64_t size = m_size.load();
	if (size >= 0 && offset >= size) return -1;
	std::lock_guard<std::mutex> hold(m_lock);
	const uint16_t want = uint16_t(std::min<int64_t>(n, FZN_RPC_LIBRARY_PLAY_MAX_SLICE));
	if (fzn_rpc_library_play(&m_call, m_root, uint64_t(offset), want, m_reply.get()) != FZN_RPC_OK) {
		m_error = QString::fromUtf8(m_call.error);
		return 0;
	}
	m_error.clear();
	took(*m_reply);
	if (m_reply->underrun) return 0;        /* not here yet */
	if (m_reply->data_len == 0) return -1;  /* past the end */
	const int64_t got = std::min<int64_t>(int64_t(m_reply->data_len), n);
	memcpy(out, m_reply->data, size_t(got));
	return got;
}

bool snurra_estate_bytes::has(int64_t offset, int64_t n) const {
	if (m_complete.load()) return true;
	return offset >= 0 && offset + n <= m_playable.load();
}

void snurra_estate_bytes::want(int64_t offset) {
	if (m_complete.load() || offset < 0 || has(offset, 1)) return;
	std::lock_guard<std::mutex> hold(m_lock);
	if (offset == m_wanted) return;
	m_wanted = offset;
	if (fzn_rpc_blob_fetch_at(&m_call, m_root, uint64_t(offset)) == FZN_RPC_OK) m_wants++;
}

QString snurra_estate_title(const QString &name) {
	uint8_t root[FZN_RPC_BLOB_HASH_LEN];
	if (!snurra_estate_root(name, root)) return QString();
	/* Asked once a name: a tile is titled on the UI thread. */
	static std::mutex lock;
	static QHash<QString, QString> known;
	{
		std::lock_guard<std::mutex> hold(lock);
		if (known.contains(name)) return known.value(name);
	}
	fzn_rpc_t call;
	fzn_rpc_init(&call);
	QString title;
	auto shown = std::make_unique<fzn_rpc_library_show_resp_t>();
	if (fzn_rpc_set_node_socket(&call, snurra_estate_socket().constData()) == 0
	    && fzn_rpc_library_show(&call, root, shown.get()) == FZN_RPC_OK)
		title = QString::fromUtf8(shown->name, int(shown->name_len));
	if (!title.isEmpty()) {
		std::lock_guard<std::mutex> hold(lock);
		known.insert(name, title);
	}
	return title;
}

void snurra_estate_install() {
	snurra_set_bytes_resolver([](const QString &name) -> std::shared_ptr<snurra_bytes> {
		uint8_t root[FZN_RPC_BLOB_HASH_LEN];
		if (!snurra_estate_root(name, root)) return nullptr;
		return std::make_shared<snurra_estate_bytes>(root);
	});
	snurra_set_title_resolver(snurra_estate_title);
	/* A root names the file's bytes by their hash: what is kept of it,
	 * its beat and key, holds while the name does. */
	snurra_set_lasting_names([](const QString &name) {
		uint8_t root[FZN_RPC_BLOB_HASH_LEN];
		return snurra_estate_root(name, root);
	});
}

void snurra_estate_offer(snurra_library *library) {
	QPointer<snurra_library> to(library);
	auto list = [to] {
		/* Its own thread: a node that is busy answers late, and the window
		 * must not wait. Nothing it holds outlives the library but the
		 * pointer, asked on the UI thread. */
		std::thread([to] {
			QString error;
			const std::vector<snurra_estate_file> files = snurra_estate_list(&error);
			std::vector<snurra_library_entry> rows;
			for (const snurra_estate_file &file : files) {
				snurra_library_entry row;
				row.name = snurra_estate_name(reinterpret_cast<const uint8_t *>(file.root.constData()));
				row.title = file.name;
				row.where = file.here ? QCoreApplication::translate("estate", "on this host")
				                      : QCoreApplication::translate("estate", "on %n other host(s)", nullptr, file.holders);
				rows.push_back(row);
			}
			QMetaObject::invokeMethod(qApp, [to, rows] {
				if (to) to->set_entries(QStringLiteral("estate"), rows);
			});
		}).detach();
	};
	list();
	QTimer *again = new QTimer(library);
	again->setInterval(60000);
	QObject::connect(again, &QTimer::timeout, library, list);
	again->start();
}
