#ifndef SNURRA_BYTES_INPUT_H
#define SNURRA_BYTES_INPUT_H

#include <QString>

#include <cstdint>
#include <functional>
#include <memory>

#include "snurra/source.h"

struct AVFormatContext;
struct AVIOContext;

/*
 * FFmpeg reading a snurra_bytes (source.h): the input a sound's reader and a
 * picture's reader both open the host's bytes through. A read of bytes not
 * here yet waits for them, telling the transport where (want), and gives
 * the wait up when `interrupted` says; the bytes are opened as a stream
 * first, unseekable, so a format that would seek to their end does not
 * wait for all of them before anything plays.
 */
class snurra_bytes_input {
public:
	snurra_bytes_input() = default;
	snurra_bytes_input(const snurra_bytes_input &) = delete;
	snurra_bytes_input &operator=(const snurra_bytes_input &) = delete;
	/* Close the format open() gave before this goes: it reads through here. */
	~snurra_bytes_input();

	/* The bytes opened as a format, streams found, or null. `name` hints
	 * at the kind. Blocks until the format is known, or interrupted. */
	AVFormatContext *open(std::shared_ptr<snurra_bytes> bytes, const QString &name, std::function<bool()> interrupted);

	snurra_bytes *bytes() const { return m_bytes.get(); }

private:
	static int read(void *opaque, uint8_t *buffer, int size);
	static int64_t seek(void *opaque, int64_t offset, int whence);

	std::shared_ptr<snurra_bytes> m_bytes;
	std::function<bool()> m_interrupted;
	AVIOContext *m_io = nullptr;
	int64_t m_position = 0;
	int64_t m_wanted = -1;   /* the last offset told to the transport */
};

#endif
