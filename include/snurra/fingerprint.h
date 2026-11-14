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

#endif
