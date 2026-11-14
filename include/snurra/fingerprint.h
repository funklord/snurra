#ifndef SNURRA_FINGERPRINT_H
#define SNURRA_FINGERPRINT_H

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <functional>
#include <vector>

/*
 * A tune's acoustic fingerprint (project.md sec 3b, the holder 2026-11-14:
 * "snurra should do fingerprinting as well"): Chromaprint's, which AcoustID
 * and MusicBrainz identify recordings by -- through FFmpeg's own chromaprint
 * muxer, so no library is linked that FFmpeg did not already bring.
 *
 * `raw` is Chromaprint's sequence of 32-bit sub-fingerprints, about eight a
 * second, for comparing two fingerprints here (snurra_fingerprint_distance);
 * `base64` its compressed form, as AcoustID takes it; `seconds` how much
 * of the tune they cover.
 */
struct snurra_fingerprint {
	std::vector<uint32_t> raw;
	QByteArray base64;
	double seconds = 0;
	double length = 0;   /* the whole tune's, in seconds, as AcoustID asks it */
	bool valid() const { return !raw.empty(); }
};

/*
 * The first `seconds` of what `name` names -- anything snurra_open_source
 * opens, a host's "fuzznet:ROOT" included -- 120 by default, as AcoustID's
 * own fpcalc takes. Empty where the source has no sound, or this FFmpeg was
 * built without Chromaprint (a phone's kit may be); `stop` returning true
 * gives up. Decodes on the caller's thread.
 */
snurra_fingerprint snurra_fingerprint_of(const QString &name, double seconds = 120,
                                         const std::function<bool()> &stop = {});

/* Whether this FFmpeg can make fingerprints at all. */
bool snurra_fingerprints_available();

/*
 * How unlike two fingerprints are: the share of bits that differ, 0 to 1,
 * at the best alignment of the two within `shift` sub-fingerprints either
 * way (about an eighth of a second each), over at least `least` of them
 * overlapping. Two encodings of one recording come out well under 0.2;
 * unrelated sound near 0.5, as chance gives. 1 where they do not overlap
 * enough to say.
 */
double snurra_fingerprint_distance(const std::vector<uint32_t> &a, const std::vector<uint32_t> &b, int shift = 80,
                                   int least = 40);

/*
 * What AcoustID says a fingerprint is (acoustid.org's v2 lookup, as Picard
 * asks it): the best-scoring result's first recording -- its title, its
 * artists joined, its MusicBrainz id -- and the score, 0 to 1. Shown, never
 * written into a file or its name (the holder, 2026-11-13: no renaming,
 * YouTube IDs kept).
 */
struct snurra_identity {
	QString title;
	QString artist;
	QString recording;   /* MusicBrainz recording id */
	double score = 0;
	bool found() const { return !title.isEmpty(); }
};

/* The client key AcoustID asks every caller for: $SNURRA_ACOUSTID_KEY, else
 * the settings' acoustid/key; empty for none. Registered to a person at
 * acoustid.org, so none is built in. */
QString snurra_acoustid_key();

/*
 * Asks AcoustID who `print` is -- the fingerprint and the tune's whole
 * length in seconds, and nothing else, sent only when this is called --
 * and calls `told` on `network`'s thread with what it says, or with an
 * error in words: no key, the service's own message, no answer. The
 * service is $SNURRA_ACOUSTID_URL where set (the tests' own server), else
 * https://api.acoustid.org/v2/lookup.
 */
class QNetworkAccessManager;
void snurra_identify(QNetworkAccessManager *network, const snurra_fingerprint &print, int duration_s,
                     const std::function<void(const snurra_identity &identity, const QString &error)> &told);

/* Identities kept, as beat grids are (beats.h's kept things): by the file
 * and its size and time, a lasting name by itself; given back only while
 * those match. False where none is kept. */
bool snurra_cached_identity(const QString &name, snurra_identity &identity);
void snurra_cache_identity(const QString &name, const snurra_identity &identity);

/* What to show for `path` titled `title`: the title, with the YouTube ID its
 * file's name carries -- "...-FYLJdKxTxJ0" or "... [FYLJdKxTxJ0]" -- kept
 * beside it (the holder, 2026-11-13: a tidied name never loses its ID). */
QString snurra_shown_title(const QString &path, const QString &title);

#endif
