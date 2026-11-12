#include "snurra/bytes_input.h"

#include <QFile>

extern "C" {
#include <libavformat/avformat.h>
}

snurra_bytes_input::~snurra_bytes_input() {
	if (m_io) av_freep(&m_io->buffer);
	avio_context_free(&m_io);
}

AVFormatContext *snurra_bytes_input::open(std::shared_ptr<snurra_bytes> bytes, const QString &name,
                                          std::function<bool()> interrupted) {
	m_bytes = std::move(bytes);
	m_interrupted = std::move(interrupted);
	constexpr int BUFFER = 65536;
	uint8_t *buffer = static_cast<uint8_t *>(av_malloc(BUFFER));
	if (!buffer) return nullptr;
	m_io = avio_alloc_context(buffer, BUFFER, 0, this, &snurra_bytes_input::read, nullptr, &snurra_bytes_input::seek);
	if (!m_io) {
		av_free(buffer);
		return nullptr;
	}
	m_bytes->want(0);
	/*
	 * Bytes not all here are opened as a stream, unseekable: FFmpeg's WAV
	 * reader, given a seekable input, seeks past the audio to look for
	 * trailing chunks and waits there -- for the whole file, before playing
	 * any of it. A format that cannot open so (MP4 with its index at the
	 * end) is opened again seekable, and waits, which is honest: it cannot
	 * play before that index arrives. Seeking is turned on once open, for
	 * the seeks a person makes.
	 */
	const bool whole = m_bytes->size() > 0 && m_bytes->has(0, m_bytes->size());
	for (bool seekable : { whole, true }) {
		m_io->seekable = seekable ? AVIO_SEEKABLE_NORMAL : 0;
		m_position = 0;
		avio_seek(m_io, 0, SEEK_SET);
		m_io->eof_reached = 0;
		m_io->error = 0;
		AVFormatContext *format = avformat_alloc_context();
		if (!format) return nullptr;
		format->pb = m_io;
		format->flags |= AVFMT_FLAG_CUSTOM_IO;
		/* Not all here: the stream analysis reads up to five seconds of
		 * audio to estimate what headers already say, and would wait for
		 * them; a fifth of a second is enough to know the codec. */
		if (!whole) {
			format->max_analyze_duration = AV_TIME_BASE / 5;
			format->probesize = 65536;
		}
		if (avformat_open_input(&format, QFile::encodeName(name).constData(), nullptr, nullptr) == 0
		    && avformat_find_stream_info(format, nullptr) >= 0) {
			m_io->seekable = AVIO_SEEKABLE_NORMAL;
			return format;
		}
		avformat_close_input(&format);
		if (seekable || (m_interrupted && m_interrupted())) return nullptr;
	}
	return nullptr;
}

int snurra_bytes_input::read(void *opaque, uint8_t *buffer, int size) {
	snurra_bytes_input *self = static_cast<snurra_bytes_input *>(opaque);
	for (;;) {
		const int64_t n = self->m_bytes->read(self->m_position, buffer, size);
		if (n > 0) {
			self->m_position += n;
			return int(n);
		}
		if (n < 0) return AVERROR_EOF;
		if (self->m_interrupted && self->m_interrupted()) return AVERROR_EXIT;
		/* Waiting, say where: FFmpeg reads through a short seek rather than
		 * seeking, so a seek alone does not tell the transport. */
		if (self->m_wanted != self->m_position) {
			self->m_wanted = self->m_position;
			self->m_bytes->want(self->m_position);
		}
		self->m_bytes->wait(20);
	}
}

int64_t snurra_bytes_input::seek(void *opaque, int64_t offset, int whence) {
	snurra_bytes_input *self = static_cast<snurra_bytes_input *>(opaque);
	const int64_t size = self->m_bytes->size();
	whence &= ~AVSEEK_FORCE;
	if (whence == AVSEEK_SIZE) return size >= 0 ? size : AVERROR(ENOSYS);
	int64_t to = offset;
	if (whence == SEEK_CUR) to = self->m_position + offset;
	else if (whence == SEEK_END) {
		if (size < 0) return AVERROR(ENOSYS);
		to = size + offset;
	} else if (whence != SEEK_SET) {
		return AVERROR(EINVAL);
	}
	if (to < 0) return AVERROR(EINVAL);
	self->m_position = to;
	self->m_wanted = to;
	self->m_bytes->want(to);
	return to;
}
