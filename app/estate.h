#ifndef SNURRA_ESTATE_H
#define SNURRA_ESTATE_H

#include <QByteArray>
#include <QString>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "snurra/source.h"

extern "C" {
#include "local/rpc.h"
#include "local/rpc_err.h"
}

/*
 * The estate's music (project.md sec 3b): the files this user's hosts and
 * contacts have catalogued, as fuzznet's node lists them, played from
 * whichever host holds them. The player is a client of the user's own node
 * over its local socket -- fuzznet's typed calls (local/rpc.h), so no verb
 * and no format is snurra's -- and the library, which other projects link,
 * knows none of it: the player alone builds this (app/fuzznet.pri).
 *
 * A file is named "fuzznet:ROOT", ROOT its 64 hex digits, and opened as
 * bytes through snurra_set_bytes_resolver, so every process the player
 * starts reads it the same way.
 */

/* Where the node answers: $SNURRA_FUZZNET_SOCKET, else fuzznet's own
 * default ("default", its sec 663). */
QByteArray snurra_estate_socket();

/* "fuzznet:ROOT" for a root, and back; false for any other name. */
QString snurra_estate_name(const uint8_t root[FZN_RPC_BLOB_HASH_LEN]);
bool snurra_estate_root(const QString &name, uint8_t root[FZN_RPC_BLOB_HASH_LEN]);

/* One file the catalogue names. */
struct snurra_estate_file {
	QString name;       /* as catalogued, the reference */
	QString kind;
	QByteArray root;    /* 32 bytes */
	qint64 size = 0;    /* 0 where none is known */
	qint64 duration_ms = 0;
	int holders = 0;    /* this user's hosts that hold it */
	bool here = false;  /* this host among them */
};

/* The whole catalogue, paged as the node gives it; empty with `error`
 * saying why where the node did not answer -- not running is the usual. */
std::vector<snurra_estate_file> snurra_estate_list(QString *error);

/*
 * One file's bytes as the node holds them, whole or still arriving, read at
 * an offset without blocking (snurra's source.h):
 *  - read: one slice at the offset (the node's 8 KiB at most); none while
 *    the bytes are not here yet, the end of file past the end;
 *  - has: from the playable prefix the last read saw, or yes when whole;
 *  - want: where the reader goes next; when that is not here, the node's
 *    fetch is moved there (fuzznet sec 998), once for each place asked.
 * A connection of its own: snurra reads from its decoding thread.
 * fuzzypickles carries the same (its fzp_library_bytes); this is the
 * player's, since snurra cannot depend on a project that depends on it.
 */
class snurra_estate_bytes : public snurra_bytes {
public:
	explicit snurra_estate_bytes(const uint8_t root[FZN_RPC_BLOB_HASH_LEN]);

	int64_t size() const override { return m_size.load(); }
	int64_t read(int64_t offset, uint8_t *out, int64_t n) override;
	bool has(int64_t offset, int64_t n) const override;
	void want(int64_t offset) override;

	/* How many times the fetch was moved, for tests. */
	int wants_asked() const { return m_wants.load(); }

private:
	std::mutex m_lock;   /* the call and its reply: one request at a time */
	fzn_rpc_t m_call;
	uint8_t m_root[FZN_RPC_BLOB_HASH_LEN];
	std::unique_ptr<fzn_rpc_library_play_resp_t> m_reply;   /* 8 KiB and more: not on a stack */
	std::atomic<int64_t> m_size{ -1 };
	std::atomic<int64_t> m_playable{ 0 };
	std::atomic<bool> m_complete{ false };
	int64_t m_wanted = -1;
	std::atomic<int> m_wants{ 0 };
	void took(const fzn_rpc_library_play_resp_t &reply);
};

/* Has snurra_open_source open "fuzznet:ROOT" names as snurra_estate_bytes. */
void snurra_estate_install();

#endif
