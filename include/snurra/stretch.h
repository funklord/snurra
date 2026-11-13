#ifndef SNURRA_STRETCH_H
#define SNURRA_STRETCH_H

#include <cstdint>
#include <functional>
#include <vector>

/*
 * A deck's tempo (project.md sec 4, key lock): track frames in, frames out
 * at `tempo` track frames per frame out. Run inside the output's callback,
 * so a tempo change is heard at once rather than after the half second a
 * reader decodes ahead -- and so it allocates nothing after construction.
 *
 * Without key lock, varispeed, as a turntable: each frame out read between
 * the track's frames by a cubic, so the pitch moves with the tempo. At
 * tempo 1 on a whole frame that cubic is the frame itself, exactly.
 *
 * With key lock, WSOLA -- SoundTouch's method, which Mixxx uses for its
 * cheaper key lock: segments of 40 ms are taken from the track a step of
 * `tempo` times the step out apart, each placed, within 15 ms, where its
 * first 8 ms best match the end of the last, and the two crossfaded there.
 * The pitch stays; the tempo moves. Written here, no library.
 *
 * A pitch of its own (`pitch`, a ratio; a DJ's key shift): WSOLA at the
 * tempo over the pitch heard, which keeps the pitch, then its frames read
 * by the same cubic at the pitch heard a frame, which raises it and gives
 * the tempo back. The pitch heard is `pitch` with key lock, the tempo
 * times it without. Where it comes to 1 the two ways above are taken, as
 * they were.
 */
class snurra_stretch {
public:
	/* `pull` gives the track's next frame, false when there is none yet. */
	snurra_stretch(int rate, std::function<bool(float &left, float &right)> pull);

	/* From track frame `frame`, nothing held. */
	void reset(int64_t frame);

	/* The next frame out; false when the track has not enough in yet. */
	bool next(float &left, float &right, double tempo, bool key_lock, double pitch = 1);

	/* The track frame the next frame out is from. */
	double position() const;

private:
	bool have(int64_t until);   /* the track in, up to frame `until` (exclusive) */
	float at(int64_t frame, int channel) const;
	bool step(double tempo);    /* one WSOLA segment into the queue out */
	void drop_before(int64_t frame);

	int m_seq;
	int m_overlap;
	int m_seek;
	std::function<bool(float &, float &)> m_pull;

	std::vector<float> m_in;   /* stereo track frames from m_in_start, m_in_len of them */
	int64_t m_in_start = 0;
	int m_in_len = 0;

	double m_place = 0;        /* varispeed: the track frame of the next frame out */

	double m_nominal = 0;      /* WSOLA: where the next segment is taken from, before its search */
	double m_nominal_out = 0;  /* and the track frame of the next frame out of the queue */
	std::vector<float> m_middle;   /* the last segment's last overlap, stereo */
	bool m_have_middle = false;
	std::vector<float> m_out;      /* frames out of the last segment, stereo */
	int m_out_at = 0;
	int m_out_len = 0;
	double m_out_tempo = 1;

	/* The pitch of its own: the last four frames out of WSOLA, stereo,
	 * with the track frame each is from; the next frame out lies at
	 * m_shift_at between the second and the third. */
	enum mode_t { VARISPEED, WSOLA, SHIFTED };
	mode_t m_mode = VARISPEED;
	float m_shift[4][2] = {};
	double m_shift_from[4] = {};
	double m_shift_at = 0;
	bool m_shift_primed = false;
	bool wsola(float &left, float &right, double tempo, double &from);
};

#endif
