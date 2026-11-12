# snurra

## 1. What this is

A small graphical media player, and the library it is built from. Named
2026-11-11 by the copyright holder ("snurra", Swedish for spin, as a
record does), the one name of more than fifty checked that was free.

**The aims, from the holder (2026-11-11):**

- **A graphical but tiny player to replace mplayer** in the holder's own
  use, **with some mixing**: more than one stream, their levels, moving
  from one to the next. "A media player is rarely small" -- so small is
  a goal held deliberately, not a description.
- **The library the other projects link**, rather than each growing a
  player of its own:
  - **BeerSSH**'s media tile (its project.md sec 6.4): a file on an SSH
    host played from the host over the session's connection, as a tile
    with play, pause and a seek bar, on Android first.
  - **fuzzypickles**' player, which plays files arriving over fuzznet and
    already solved the hard half: a source that can run dry, playback
    that pauses while it does, and seeking offered only where it cannot
    stall (its project.md, "The player people actually use", 2026-09-06).
  - **fuzznet**, whose fetch follows the playhead, so a seek is the next
    range asked for.

What the three share is the player and its tile; what differs is where
the bytes come from. So the library's centre is a **byte source that may
still be arriving** -- read at an offset, saying which ranges it holds --
and everything above it knows nothing of SFTP or fuzznet.

## 2. Decided

- **Qt Widgets**, C++17, as every GUI in this workspace. No QML.
- **Prefix `snurra_`** for everything the library exports; module-private
  names unprefixed (code-style.md).
- **Vendored by its consumers** as a submodule, with a `.pri` for qmake
  and a `.mk` for hand-written Makefiles, as softkeys is.
- **No licence yet.** Absent on purpose until the holder decides; not a
  gap to fill.

## 3. Open -- the holder's, or to settle with the first code

- ~~The decoding and output backend.~~ **Settled 2026-11-12 by building
  it:** FFmpeg's libraries decode into snurra's own engine (sec 3a, 4),
  since `QMediaPlayer` cannot be a deck in a mixer and libmpv is not
  small. What FFmpeg adds to an Android APK is still unmeasured, and is
  the number to take before the Android build.
- **The source interface's shape.** fuzzypickles' contract (a
  `QIODevice` whose reads return nothing while bytes are missing, and a
  playable prefix) is the starting point; whether ranges rather than a
  prefix are needed for seeking ahead is the first question for it.
- **Video, or audio first.** The mplayer use is the test; the holder to
  say which comes first.

## 3a. Mixing: Mixxx's structure, kept where it holds, improved where not

The holder, 2026-11-11: copy Mixxx's structure for mixing, unless it is
outdated and worth improving on. Read from Mixxx 2.5.0 as installed here
(its binary's symbols and linked libraries), not from its source, which
is not on this machine -- so the names are its, the reading of how they
fit is from its design as published, to be checked against the source
before building on it.

**Kept, because it is still the right shape for real-time audio:**

- **The engine graph:** a channel per source (`EngineChannel`,
  `EngineDeck`), each with its buffer (`EngineBuffer`), into one mixer
  (`EngineMixer`, `EngineMaster` before 2.4) that sums into the output.
  For snurra: a deck per stream, one mixer, levels and crossfade there.
- **The reader apart from the audio callback:** `CachingReader` serves
  the engine from decoded chunks while `CachingReaderWorker` decodes on
  its own thread and hands chunks over without locks; the callback never
  waits on a disk or a decoder. This is exactly where snurra's
  byte-source-still-arriving belongs: a missing range is a chunk the
  worker cannot fill yet, and the deck plays silence or pauses instead of
  stalling the callback -- fuzzypickles' stall contract, in Mixxx's
  structure.
- **`ReadAheadManager`:** reading ahead of the play position, which for a
  network source is also what to ask for next (fuzznet's playhead).
- **Tempo and pitch behind one interface** (`EngineBufferScale` with
  linear, SoundTouch and RubberBand behind it): later, if at all, but
  the seam is worth having from the start.

**Improved on, because it has aged or does not fit:**

- **The control layer.** Mixxx's `ControlObject`/`ControlProxy` is a
  global bus keyed by strings (`[Channel1],play`), so that skins and
  controller scripts can reach anything. It is untyped and global, and a
  misspelt key fails silently. snurra's controls are typed members of
  the deck and mixer, with atomics where the engine reads them; a string
  map can be layered on later for scripting, from the typed ones.
- **Output, revised the same day.** This said `QAudioSink` instead of
  PortAudio; the holder then set two requirements that undo it: **snurra
  must be low latency**, and **supporting many or all of a platform's
  output types is never a bad idea for audio software**. `QAudioSink`
  pushes into a buffer of its own through the system's sound server, so
  its latency is not snurra's to set, and it reaches neither JACK nor raw
  ALSA nor ASIO. So **output is an interface with several backends, the
  engine run inside the device's callback** (pull, as Mixxx does): no
  lock, no allocation and no I/O in the callback, the reader thread
  keeping decoded audio ready ahead of it.
  - **PortAudio on the desktop** -- Mixxx's choice kept, for its reach:
    ALSA, JACK and OSS on Linux (PipeWire through its JACK and ALSA
    faces), CoreAudio, WASAPI and ASIO on Windows.
  - **AAudio on Android** (API 26 and up, below this workspace's floor),
    in its low-latency performance mode; Oboe is Google's C++ wrapper
    over it and OpenSL ES, worth weighing against a direct AAudio call.
  - **`QAudioSink` as the plain fallback**, for an embedding program that
    wants no further dependency and does not need low latency -- a media
    tile playing a file, not a mix.
  The latency to aim for, and how to measure it end to end, is the first
  thing to settle when the engine exists; the device's period and the
  reader's lead are the two numbers.
- **Shared by default, exclusive by choice** (the holder, the same day:
  "sometimes poor latency output is all we have on some systems so
  let's keep those options open too, and something that works with a
  normal alsa dmix setup (mixxx hogs the audio exclusively)"). So:
  - **The default output shares the device**: ALSA's `default` (dmix
    where that is the setup), PulseAudio or PipeWire, so other programs
    go on sounding beside snurra. Mixxx opens its devices for itself and
    silences everything else; snurra does not, unless asked.
  - **Low latency is opt-in**: a `hw:` device, JACK, ASIO, AAudio's
    exclusive mode -- chosen per output, said in the interface as the
    trade it is.
  - **Any latency must work, not only low.** A shared output's period
    can be tens of milliseconds, and some systems offer nothing better;
    the engine takes whatever period the device gives, the reader's lead
    grows with it, and the position shown and the seek bar are corrected
    by the latency the device reports, so a slow output plays late but
    never stutters or shows the wrong place.
- **QML:** Mixxx links Qt Quick and is moving its interface there; this
  workspace is Qt Widgets (harmonization.md), so the interface is
  snurra's own.
- **Size:** Mixxx is a DJ suite -- library, controllers, effects, sync,
  recording, broadcasting. snurra takes the engine's shape and none of
  the suite; each of those is a decision of its own, not a default.

So the backend question in sec 3 leans further: Mixxx decodes with
FFmpeg among others (`SoundSourceFFmpeg`) into its own engine, which is
the second option there. QMediaPlayer cannot be a deck in a mixer.

## 3b. The tile, the train and the stack -- the feature snurra is for

The holder, 2026-11-11, "the killer feature": snurra opens as a tiny,
squarish player **right above the file** it was opened from in a file
browser. Open a second and it appears above its own file; **tiles snap
together side by side into a train -- one mix, played in sequence -- and
one below another into multitrack**, with beat-snapping, envelopes "and
such features".

**A process per tile, merged when snapped** (the holder, the same day:
"just like beerssh these start as separate processes, but are merged
into one when snapped together"). BeerSSH hands a live session from one
process to another when panes are joined (its project.md sec 8.4); snurra
does the same with a tile's playback:

- **Opened, a tile is a process of its own**, playing on its own through
  the shared default output -- which is why shared must be the default
  (sec 3a): two tiles each holding an exclusive `hw:` device could not
  both sound.
- **Snapped, it merges**: the tile dropped onto another hands over what
  it is playing -- the file or source, the position, playing or paused,
  its level and envelope -- to the process it joined, which becomes the
  one engine mixing both. The giver keeps playing until the taker sounds
  the stream at the same position, then stops, so the merge is heard as
  nothing; then it exits. A source still arriving (BeerSSH's SFTP,
  fuzznet) moves as its address and offset, re-opened by the taker,
  since a byte stream cannot be handed across mid-read.
- **Pulled out**, a tile leaves the same way in reverse, into a new
  process.
- **The snap gesture between processes:** on X11 a tile can find the
  others' windows and snap edge to edge as it is moved; on Wayland a
  program cannot learn other windows' positions, so the gesture there
  is dragging one tile onto another -- a drag and drop, which Wayland
  carries between programs -- and the merged window is arranged inside.
- **Why not one process from the start:** a tile that crashes on a bad
  file takes only itself down, a lone tile stays a small program, and
  the merge is needed only where mixing begins.

**Above the file: what each platform allows.**

- **Embedded** (BeerSSH's file panel, fuzzypickles): exact -- the host
  knows the item's rectangle and gives it.
- **Launched from a file manager on X11** (TDE's Konqueror, and others):
  a file manager tells the program it starts nothing about where the
  file's icon is, but the pointer is on it at the double-click, so the
  tile opens at the pointer, clamped to the screen. A good heuristic, not
  a guarantee (Enter on a selected file opens it wherever the pointer is).
- **Wayland:** a program can neither place its own window nor read the
  pointer outside it; the compositor places the tile. Above-the-file is
  out of reach there short of a protocol the compositors do not offer.



**What the shapes mean:**

- **A train** (side by side) is a sequence on one timeline: each tile's
  end overlaps the next's start, and the overlap is the crossfade. This
  is a DJ mix, and Mixxx's engine shape (sec 3a) plays it: two decks and
  a crossfader, scheduled.
- **A stack** (one below another) is multitrack: tiles play at once, each
  a lane, each with its own level -- an arranger, not a DJ.
- **Beat-snapping:** a dragged tile's start or overlap snaps to the
  other's beat grid, which needs tempo and beat analysis per file (Mixxx
  analyses on load; aubio and the Queen Mary vamp plugins are the usual
  libraries -- their licences are the holder's to weigh before one is
  linked).
- **Envelopes:** a level curve per tile, drawn on it; the crossfade of a
  train is two of them.

**"Tiny" kept honest by building in layers**, each usable before the
next: (1) the tile -- one file, play/pause/seek, opened at the pointer,
the shared default output; (2) single instance and several tiles,
snapping into one window; (3) the train: sequence and crossfade; (4) the
stack: simultaneous lanes and levels; (5) envelopes; (6) analysis and
beat-snapping. Each layer is a decision to go on, not a default.

## 3e. Analysis, pitch and stems: where Mixxx gets them (2026-11-11)

Read from the installed Mixxx 2.5.0 (linked libraries, analyzer names in
its binary) and Debian's copyright files; licences are facts for the
holder to weigh before anything is linked, not choices made here.

| What | Mixxx's source | How | Licence |
|---|---|---|---|
| Beats, tempo | Queen Mary DSP (qm-dsp), its tempo tracker | built in, `lib/qm-dsp` | GPL-2+ (Mixxx's `Files: *`; its kissfft BSD-3) -- read qm-dsp's own terms before use |
| Musical key | qm-dsp's key detector; libKeyFinder optional, not linked in this build | built in | GPL-2+ (KeyFinder GPL-3+) |
| Time-stretch, pitch, quality | Rubber Band 3.3.0 | shared | GPL-2+, or a commercial licence |
| Time-stretch, pitch, cheap | SoundTouch 2.4.0 | shared | LGPL-2.1+ |
| Loudness | libebur128 | shared | MIT |
| Fingerprints | Chromaprint | shared | LGPL-2.1+ / MIT |

**The licence question this raises:** snurra has none yet, which is
right; but linking qm-dsp or Rubber Band makes the whole program
distributable only on GPL-compatible terms, so that choice is the
holder's before either is linked. SoundTouch and libebur128 do not force
it. Tempo and pitch go behind one interface either way (sec 3a).

**Stems.** Mixxx 2.5.0 has none (no stem code, no ML runtime). From
memory, not verified: Mixxx 2.6 plays Native Instruments' pre-separated
`.stem.mp4` files with per-stem controls, and separates nothing itself.
Separating a mix is a different, heavy job: Demucs v4 (Meta, MIT; check
the weights), Spleeter (Deezer, MIT, older), Open-Unmix (MIT, small);
the commercial DJ programs separate in real time with closed models. In
snurra stems are simply a stack's lanes, so in layers: (1) play existing
stem files -- FFmpeg decodes multi-track MP4, each stem a lane; (2)
separate offline with Demucs as an outside tool, once, cached; (3) in
process through an ONNX runtime, later if ever -- large, and not real
time on Android.

## 3c. One set of controls, not one per tile

The holder, 2026-11-11: **only the first window opens with its controls
-- above or below its waveform -- and starts playing; the others open as
a waveform alone**, paused, and their controls can be pulled out to play
them at the same time if wanted. "This also solves the too many
control panel / fx / config problem": a screen of tiles is a screen of
waveforms, not of panels, and the controls a person sees are the ones
they asked for.

- **First** means no other snurra answers on its socket when this one
  starts (a socket a crashed process left does not count); within one
  launch of several files, the first file.
- **Pulled out**, a tile's controls show below its waveform, and it can
  be played alongside the others. **Pushed back in, it stops**: what
  plays is what can be stopped from a tile.
- A handover carries whether a tile's controls are out.
- Effects and configuration are to follow the same rule when they come:
  pulled out from the tile they apply to, never shown by default.

## 3d. A marker for where a drag lands

The holder, 2026-11-12: a marker for the shape and where the window would
land on each drag and drop, connecting and disconnecting. While a lone
tile is dragged near another snurra, a marker shows **where it would
land, filled, and the shape of the group it would make, outlined**;
while a tile is dragged out of a group, it shows **where its new window
opens**. The marker and the release use one answer
(`snurra_find_snap()`, `src/snap.cpp`), so what is shown is what
happens. It is a frameless window of its own that takes no input, so the
drag goes on through it; on Wayland, where a program cannot place a
window, it is not shown.

## 4. State

**Audio, built 2026-11-12** (the holder: "start on the audio output and
how it acts when windows are connected and disconnected"). The shape is
sec 3a's:

- **A deck per tile** (`src/deck.*`): a reader thread decodes ahead into
  24 blocks of 1024 frames; the callback takes them through two lock-free
  queues (`src/spsc.h`) and atomics -- no lock, allocation or I/O in the
  callback. A block not there yet is silence and the position holds.
  Play/pause and level are slewed over 5 ms, so neither clicks.
- **Sources** (`src/source.*`): a file through FFmpeg (resampled to
  stereo float at the engine's rate; a seek decodes forward from the key
  frame and drops to the exact frame); `tone:<hz>` for trying an output by
  ear; `snurra-test:ramp`, whose samples are their own frame index and
  gain, for the tests; anything else is silence, so an unplayable name is
  still a tile.
- **The engine** (`src/engine.*`): one per process, decks in 64 atomic
  slots summed in the output's callback; a deck leaves at a time to come
  and is deleted only after two callbacks without it.
- **Outputs** (`src/output*`): an interface called with the buffer and
  the shared-clock time its first frame is heard. **ALSA** (`alsa:default`,
  shared, the default; `alsa:hw:...` exclusive), latency asked for by
  `$SNURRA_LATENCY_MS` (40 ms by default) and taken as granted; **null**
  and **capture:<dir>**, paced by the clock, for no sound card and for the
  tests. `$SNURRA_OUTPUT` chooses; one that will not open falls back to
  null, said on stderr. PortAudio was planned first, but its headers
  (`portaudio19-dev`) are not installed here; AAudio and `QAudioSink`
  wait for their platforms.
- **The shared clock** (`src/clock.h`) is `steady_clock` in microseconds,
  one for every process on a machine and never stepped. Tile states carry
  positions in microseconds, so a taker can be in step to a frame. Every
  frame/microsecond conversion rounds, through `clock.h`; truncating ones
  lost up to a frame each, the same way, and put two decks two frames
  apart.

**Seeking is to the sample** (the holder, 2026-11-12: "since we are a DJ
program, our seeking api needs to be very precise"). A seek lands on the
very frame playing through would reach, for every codec; a handover's
taker hands on the giver's exact (frame, time) pair, never a moment
re-sampled and rounded on each side. A deck plays whole frames, so two
decks in step are within half a frame of each other and no closer --
closer would need the taker resampled by a fraction, which nothing yet
asks for.

**Bytes still arriving, built 2026-11-12** -- the library's centre
(sec 1). `snurra_bytes` (`src/source.h`) is what a consumer implements to
have snurra play from its transport: read at an offset without blocking
(how many are here, 0 for not yet, -1 past the end), `has` for a range,
`want` for where the reader goes next -- fuzznet's playhead -- and
`wait`. FFmpeg reads it through custom I/O on the deck's reader thread,
which may block: a missing byte blocks the reader, the deck has no
blocks, and the output plays silence with the position held, so the
callback still never waits. A seek or a close interrupts the wait. A
deck takes `bytes` in its `start_t`. fuzzypickles' contract, held to the
sample (`deck_test`, WAV, FLAC, MP3, over a transport whose ranges the
test sets, driven as an output drives it):

- where the bytes stop, it holds, silent, and on arrival goes on from the
  very next frame, sample for sample the file;
- a seek into what is not here waits at the target and asks for it, and
  lands on the target when the bytes come;
- a play before any bytes waits, and begins at frame 0;
- bytes all here seek exactly as the file does.

What building it found and fixed:

- **Every playing deck was anchored**, so a newly opened file skipped
  what passed while it opened -- its first 80 ms on ALSA -- and one still
  arriving would skip all it waited for. Only a handover's taker anchors
  now (`start_t::anchored`).
- **Seeks were in the right place with the wrong sound.** A lossy
  codec's frame overlaps the last, and MP3 borrows bits from earlier
  frames, so a decoder restarted at the target differs for a frame or
  two. A seek now decodes from a pre-roll -- four codec frames, or the
  codec's own (Opus 80 ms), and a resampler's history -- and drops it.
  `codec_test` requires every sample after a seek to equal played
  through: exact for WAV, FLAC, ALAC, MP3, AAC, Opus and Vorbis; MP2 and
  AC3 within two 16-bit steps, since their decoders dither from a state
  no seek can rebuild (eight times the pre-roll changed nothing).
  Without the pre-roll, Opus and an MP3 seek over bytes fail.
- **WAV over arriving bytes never started**: FFmpeg's WAV reader seeks
  past the audio for trailing chunks, and stream analysis reads five
  seconds to estimate what the header says. Bytes not all here open as a
  stream, unseekable, with a fifth of a second of analysis; a format that
  cannot (MP4 indexed at the end) opens seekable and waits, which is
  honest -- it cannot play before its index arrives.
- **A short forward seek is read through, not seeked**, so the reader
  now says where it waits whenever it waits.
- **A packet cut short by a wait given up comes back marked corrupt**;
  it is dropped rather than decoded into the stream.

**What it costs, and is not fixed:** an exact seek in an arriving MP3
without a TOC needs the bytes before the target -- FFmpeg's accurate MP3
seek reads forward from the last frame it knows, since a CBR stream's
byte positions are not exact -- so it asks from the edge of what is
here, and waits for all of it. A fast seek (estimate, then refine) would
trade exactness for time; not built, and the DJ requirement argues
against it as a default. The length of bytes not all here is the
duration's estimate, not the last packets'.

**Sanitizers, 2026-11-12:** the whole suite under ThreadSanitizer, and
under AddressSanitizer with UBSan, both built out of tree with qmake's
`CONFIG+=sanitizer CONFIG+=sanitize_thread` (or `sanitize_address
sanitize_undefined`), the snurra the tests start included: no report.
Each was shown live first by a deliberate race and a deliberate heap
overread, both reported. `test/main.cpp` sets
`QTEST_DISABLE_STACK_DUMP`: QtTest's crash handler re-faulted and
re-attached gdb 361 times in 281 s on a fixture's overflow.

**Codecs keep time, measured** (`codec_test`, the holder 2026-11-12:
every codec added gets tests of how it keeps time against other files,
so timing faults are found and fixed before anything depends on them). A
reference of chirps at known times is encoded by the `ffmpeg` program
into WAV, FLAC, ALAC, MP3, AAC, Opus, Vorbis, MP2 and AC3, at 48 and
44.1 kHz, decoded through `src/source.cpp`, and each chirp found by
correlation to a fraction of a frame. Every codec must seek to within a
quarter frame of played-through and report the length it plays; every
codec that carries its priming and padding must also play as written,
to a quarter frame and to the frame in length. A new codec is a new row
in `every_codec_keeps_time_data`. What the first run found, all fixed in
the source:

- **Priming dropped twice.** FFmpeg 7.1's decoder drops MP3's encoder
  delay and Opus's pre-skip from the first frame but leaves its pts on
  the dropped start; read, they played 956 and 312 frames early. A first
  frame at the first packet's pts, shorter than that packet, now counts
  the difference.
- **A resampler restarted off its grid.** After a seek a 44.1 kHz file
  sat up to half a frame off played-through, since only every 147th
  input sample lands on a whole 48 kHz frame; the restart now skips to
  the next grid point.
- **Vorbis at 48 kHz seeked 465 frames off** (the best-effort timestamp,
  as above).
- **Coarse raw timestamps.** Raw AC3 at 44.1 kHz counts 3134.69 ticks of
  1/90000 a frame as 3134, so a seek lagged 50 frames by the fifth
  second; a frame's index (pts / duration) is exact, and is used.
- **Lengths from the last packets**, not the stream's duration: Opus's
  counts its pre-skip, MP3's its padding, and AAC in MP4 played its
  final frame's 768 padding samples. Ogg shortens the last packet by its
  discard and names it too; MP3 does not shorten it; a last packet as
  long as its predecessor is taken to still hold its padding.
- **Raw MP2 and AC3 play late by their encoder's delay** (481 and 256
  frames from FFmpeg's encoders): the format carries no word of it, so
  every player hears it so, Mixxx included. Recorded, held constant (no
  drift), and seeks still agree with played-through to the sample.
  Correcting it would need a delay assumed per encoder.

**A handover is a crossfade over one stretch of that clock.** The taker
builds its tiles with their decks *held* silent and *anchored* to the
giver's sampled position, so each plays whatever frame belongs to each
moment; once all are in step (500 ms at most) it sends READY with the
earliest moment it can sound. The giver takes the later of that and its
own (latency + a period + 20 ms for the COMMIT to cross), fades out over
50 ms from then, sends COMMIT with the moment, takes the tiles out of its
window at once, and keeps their decks sounding until the fade has been
rendered; the process ends when the engine is idle. The taker fades in
over the same 50 ms. Straight-line fades, because the two play the same
samples in step and straight lines then sum to one. A taker whose giver
disconnects, or sends no COMMIT within 2 s, fades in on its own.

**The handover base, built 2026-11-11** (the holder: "a good base for the
window connection/disconnection to make them as seamless handovers as
possible so we avoid dropouts and performance spikes", with dummy
controls; each tile very compact but resizeable).

- **A process per tile**, listening on `<runtime>/snurra/<pid>.sock`
  (`$SNURRA_RUNTIME_DIR` overrides); peers are found by listing the
  directory. Messages are length-framed QDataStream (`src/wire.*`).
- **The handover overlaps by design:** OFFER (states, each position
  stamped with when it is heard) -> the taker builds the tiles, in step
  and silent, and answers READY -> the giver fades out and sends COMMIT
  with the moment -> the taker fades in over it, and the giver goes once
  its fade is heard. No READY within 2 s: ABORT, and the giver plays on
  -- a failed handover costs nothing.
- **Pulling a tile out** starts a new process with `--adopt`; it
  connects back and asks for the tile by token, and the group plays it
  until READY. 8 s for that, as a process has to start.
- **No spikes:** a tile's states travel, not its widgets; the taker
  inserts every tile and lays the grid out once with updates held; a
  drag asks every peer where its window is as it starts, so the answers
  are in by the release and nothing waits on a socket mid-gesture.
- **Snapping (X11):** a lone tile dragged by its title moves the window;
  let go within 24 px of another snurra's right edge it joins as the
  next in a train, of its bottom edge as a lane below. In a group, a
  tile dragged out of the window and let go is pulled out there. On
  Wayland the window move is the compositor's (`startSystemMove`) and
  snapping waits for drag-and-drop onto a tile (sec 3b).
- **A tile:** a title that is the handle, a handle for the controls,
  close; a waveform (a stand-in drawn from the source's name, so a file
  always looks the same, with the playhead, and a click seeks); the
  controls, when out: play/pause, the deck's position, a level.
  About 26 characters wide, 14 at least, frameless with a size grip.
- **Measured** (`make test`, real processes over their sockets, offscreen,
  output null or capture): a merge is taken before it is let go and the
  moved tile's position carries on within 2 ms of the extrapolation; a
  tile pulled out plays on in a new process; a taker that never answers
  leaves the giver playing; only the first opens with controls and plays;
  and **from what each process rendered**, for a merge and for a pull-out,
  the two gains sum to 1 within 0.0003 at every frame from 200 ms before
  the fade to 200 ms after, the two are at most 1 frame apart where both
  sound, and each plays its frames without a skip or a repeat.
  Within 0.48 frames over ten runs, the bar 0.55, and continuity is
  checked on each capture's own frames (on a shared grid, two clocks
  half a frame apart round onto one frame twice -- that was an
  intermittent false failure, not a dropout). Sabotaged by hand
  2026-11-12, each failing its test: the giver cutting at the fade's
  start (sum 0), the taker not anchored (919 frames apart, and the
  position test), the giver's deck retired before its fade (sum 0). Earlier: stopping the giver at OFFER, never aborting, every launch
  taken as the first, pushed-in controls left playing.
- **A merge between two ALSA outputs, heard** (2026-11-12,
  `a_merge_between_two_alsa_outputs_is_heard_in_step`): both processes
  write to one dmix -- the shared default's shape -- on a free snd-aloop
  subdevice, defined in the test's own config through `ALSA_CONFIG_PATH`,
  and arecord takes the other end. The mix describes itself (from
  `snurra-test:wrapped-ramp`, the right channel is the gains summed, left
  over right the frame heard), so no clocks are lined up. Twelve runs:
  the gains sum to 1.0000 and the taker is on the giver's very frame,
  every time. Skipped where there is no loopback, arecord or free
  subdevice. What it took:
  - **Placing frames by `snd_pcm_delay` put the two 48 to 110 frames
    apart**: read at an arbitrary moment, it is up to a period stale.
    `snd_pcm_htimestamp` gives the queue with the monotonic time it was
    true, and brought it to 0-3 frames.
  - **One reading per side jitters by two or three frames**, and a
    handover takes one from each. The presentation time is now filtered
    as JACK's DLL does: each period predicted one period after the last,
    an eighth of a measurement's error taken; a reading two periods off
    (a start, an underrun) is taken whole. Then 0 in twelve of twelve.
  - **The instrument first**: dmix mixes 32-bit samples at 24 bits, so
    the plain ramp read to two frames and showed a 3-5 frame "transient"
    that was two truncations summed. The wrapped ramp (modulo 2^16)
    reads to 1/128 of a frame.
  - Underruns are counted and said on stderr when the output stops.
  - **A killed player leaked the dmix's shared memory**: two segments and
    a semaphore set a run, 90 and 45 of them by the time it was noticed,
    since a dmix frees them only when its last client closes. snurra now
    ends its event loop on SIGTERM and SIGINT (`src/main.cpp`), so the
    engine closes its output; the test ends the taker so and counts what
    is new in its key range from `/proc/sysvipc/shm`, and fails on any.
    Counted by difference, not by the configured key: alsa-lib did not
    use that key as given, and a check for it passed over a leak until a
    SIGKILL sabotage showed it.
  Sabotaged: `snd_pcm_delay` again (2.66 frames, caught), the giver
  cutting at the fade (gains 0.0001). An 82-frame excursion inside one
  fade, once, before the filter, was not seen again in 24 runs and left
  no underrun; unexplained, and the test would fail on it.
  Not yet heard on a speaker; `tone:440` through the loopback was a clean
  440.3 Hz.
- **The marker** (sec 3d): `the_marker_and_the_drop_agree_on_where_a_tile_lands`
  checks the shared answer -- beside a right edge, under a bottom edge,
  too far, not level, and the nearer of two edges. The marker window
  itself is not yet seen on a real X11 screen.
- **Not yet:** a tile for bytes (the window opens tiles by name; a
  consumer embedding snurra hands a deck its bytes); Wayland's
  drag-and-drop merge; a real waveform; the train's and stack's own
  mixing (sec 3b); the `.pri` and `.mk` a consumer vendors with (sec 2).

## 5. Resuming work here (2026-11-12)

Built and tested: the handover base, tiles as waveforms with controls
only for the first (sec 3c), the drop marker (sec 3d), and audio with the
handover as a crossfade (sec 4). Not seen or heard on a real screen and
speaker yet -- every test runs offscreen; trying the drag, the snap, the
marker and a merge by ear is the first thing to do.

Published 2026-11-12 at `github.com/funklord/snurra` (public), pushed
over SSH as the other trees are. Open, the holder's: audio or video
first; the analysis licences (sec 3e).

Next: the `.pri`/`.mk` for consumers, so BeerSSH and fuzzypickles can
link snurra and hand it their bytes; the Wayland merge by drag and drop;
a real waveform; a handover between an ALSA output and a hw: one (two
devices, two clocks -- drift, not only latency, then matters).
