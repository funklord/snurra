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
  and a `.mk` for hand-written Makefiles, as softkeys is. Built
  2026-11-12: `snurra.pri` compiles the library into the consumer, moc
  included, and `snurra.mk` derives its lists from the tree; `make
  check-mk` builds the player from the `.mk` alone and runs the tests
  against it ($SNURRA_BIN). Headers are in `include/snurra/` and included
  as `"snurra/deck.h"`, measured against the consumers first: BeerSSH has
  a `wire.h` and fuzzypickles a `clock.h`, which a bare `src/` on their
  include path would have made a matter of include order.
- **No licence yet.** Absent on purpose until the holder decides; not a
  gap to fill.

## 3. Open -- the holder's, or to settle with the first code

- ~~The decoding and output backend.~~ **Settled 2026-11-12 by building
  it:** FFmpeg's libraries decode into snurra's own engine (sec 3a, 4),
  since `QMediaPlayer` cannot be a deck in a mixer and libmpv is not
  small. **What FFmpeg adds to an Android APK, measured 2026-11-12**: the
  x86_64 debug APK is 79.6 MB, of which the kit's FFmpeg is 18.8 MB
  (libavcodec alone 14.4, every decoder FFmpeg has), and a further 27 MB
  is Qt Quick, QML, PDF, SQLite, TIFF, SVG and Qt Multimedia -- none of
  it snurra's. They come with the kit's FFmpeg, not with snurra:
  androiddeployqt shows libavformat needing Qt's FFmpeg stub, which
  needs Qt Multimedia, which brings the rest; a build with snurra's own
  Qt Multimedia taken out deployed all of it still, so that change was
  not kept. **The option, its cost, and whose it is:** an FFmpeg of
  snurra's own for Android, configured with the audio demuxers and
  decoders sec 4 tests and nothing else, would drop most of 45 MB per
  ABI, for a cross build of FFmpeg per ABI kept in the tree's tooling
  (as bbq-predictor keeps OpenSSL's) and a second FFmpeg to keep current.
  The holder's to decide, since it is every consumer's APK too -- BeerSSH
  links snurra.pri.
- ~~The source interface's shape.~~ **Settled 2026-11-12 by building it**
  (`snurra_bytes`, sec 4): ranges, not a prefix -- a read at an offset
  says how much is there, `has()` asks for a range, and `want()` tells the
  transport where the reader goes next, so a seek ahead into what has not
  arrived waits there and asks for it, which a playable prefix could not
  express. fuzzypickles' rule of offering seeks only where they cannot
  stall is not snurra's: a DJ cues anywhere, and the seek waits.
- ~~Video, or audio first.~~ **Settled by the holder 2026-11-12:**
  "make it work first" -- audio, then video, as a drawer that opens
  upward from the tile (sec 3b).

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
- **Files dropped from a file manager** (2026-11-12): onto a window, a
  tile each, where the drop's side says -- at the right edge the first
  lane's train goes on, at the bottom a new lane starts -- paused, their
  controls in, as a launch's later files.
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
  a lane, each with its own level -- an arranger, not a DJ. Settled with
  step 4 (2026-11-12), from the aims: the rows of a window are its lanes
  on **one timeline**, each lane a train along it, so a window's grid is
  both shapes at once. **The transport is the group's**: play, pause or
  seek on any tile moves every lane, to the time that tile's place
  names on the timeline, and the lanes are started on one moment of the
  shared clock -- in step to the frame however their readers fare. A
  lane's level is its tiles' levels. So two tiles of one row no longer
  play at the same time: a row is a sequence, and what plays at once is
  stacked. A merge does not realign what is playing -- it is heard as
  nothing (above) -- so lanes merged while playing run apart until the
  group is next played or sought. Not yet: a lane starting later than
  the timeline's start; it comes with the layout below or with
  envelopes.
- **Beat-snapping:** a dragged tile's start or overlap snaps to the
  other's beat grid, which needs tempo and beat analysis per file (Mixxx
  analyses on load; aubio and the Queen Mary vamp plugins are the usual
  libraries -- their licences are the holder's to weigh before one is
  linked).
- **Envelopes:** a level curve per tile, drawn on it; the crossfade of a
  train is two of them. Settled with step 5 (2026-11-12): points of
  (time into the track, gain 0 to 1), straight lines in amplitude
  between them, holding before the first and after the last --
  Audacity's envelope, the older and plainer of the editors weighed
  (Ardour's gain line is the same shape in dB). In the track's own time,
  so a curve goes with its track through seeks, handovers and trains.
  Edited only from the controls (sec 3c): an envelope button turns the
  waveform from seeking to editing, where a press adds a point or takes
  the one under it, a drag moves it between its neighbours, and a drag
  out of the waveform removes it -- Audacity's envelope tool again.
  Not yet: a train's crossfade drawn as its two tiles' curves; it is
  still the deck's equal-power fade, at the overlap above.

**"Tiny" kept honest by building in steps**, each usable before the
next: (1) the tile -- one file, play/pause/seek, opened at the pointer,
the shared default output; (2) single instance and several tiles,
snapping into one window; (3) the train: sequence and crossfade; (4) the
stack: simultaneous lanes and levels; (5) envelopes; (6) analysis and
beat-snapping. Each step is a decision to go on, not a default.
Called steps, not layers (the holder, 2026-11-12): "layer" already
means a level of a program's structure -- the control layer above --
and reads as one here, where these are stages of what the player can
do, built in order.

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

**Beats are snurra's own** (2026-11-12, the holder asking for beat
detection, best in class, with tests): a detector written here, no
third-party code, so none of the licences above is taken on.
`snurra_find_beats` (`src/beats.cpp`): onsets as spectral flux over
512-frame hops, the tempo from their autocorrelation weighted toward
120 BPM, 70 to 180; the exact period and phase by folding the whole
track's onsets at each candidate, three passes finer; between a tempo
and its half, the half when the faster grid's lines alternate strong and
weak in the plain magnitude below 4 kHz (kick and snare, then hats
alone); no grid when the fold's peak is under 6 times its mean.
Measured on made signals, a minute each: a bare kick at 120 and a kick,
snare, hat and chord pattern at 87.5, 128, 140 and 127.3 BPM -- tempo
within 0.003 BPM, every beat within 2.7 ms of the grid (which sits 1.6
to 2.4 ms early); white noise, a steady tone and hits at random times
get no grid (fold 1.4, 2.4, 3.3). Without the exact search, every one
fails. A steady tone through a lossy codec read as a beat (seen in the
player on a test video: 4 bars, and the waveform full of ticks): the
codec's frames ripple the onsets at their own period, which a fold
finds as clearly as a drum (AAC 25, MP3 19) -- but faintly. So a beat
must also be loud enough: onset strength 50 a line on average, where
made drums give 1640 to 1770, the encoded tones 0.7 to 6.3, white noise
7. A made 128 BPM pattern through AAC is still found, 128.003 BPM, every
beat within 3.1 ms. **Set aside by the holder the same day** ("for now we should
just use the same method used by mixxx, this is sidequest"): this is
Mixxx's shape -- a detection function, a tempo, one constant grid fitted
to the whole track, Mixxx's default -- so it stays, and measuring it on
real music with published annotations waits. Not done: downbeats (a bar
is counted from any beat), tempo that changes.

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
snurra stems are simply a stack's lanes, so in steps: (1) play existing
stem files -- FFmpeg decodes multi-track MP4, each stem a lane; (2)
separate offline with Demucs as an outside tool, once, cached; (3) in
process through an ONNX runtime, later if ever -- large, and not real
time on Android.

**Two layouts** (the holder, 2026-11-12). The holder dislikes windows
that are not rectangles, but the stack needs them, and some systems
cannot show them. So the player keeps **its normal look** -- tiles with
small drawers sticking out of them, pulled to open the player out -- and
also has **a mode where every window is square**, as the tiles are now.
**The drawers, for now** (the holder, 2026-11-12): two per tile. **Upward,
the video**: as wide as the player, with a maximize button. **Downward,
the controls.** **A waveform stays the same size on every connected
track**, whatever drawers are open: a drawer adds room to the window, it
never squeezes or stretches a waveform. So a window's grid gives each
lane three rows -- its upward drawers, its waveforms, its downward
drawers -- and every waveform in a lane sits on one line. In the normal
look the window is transparent outside the tiles and their open drawers,
which is what makes it not a rectangle; in the square mode the same
layout is drawn on an opaque window.

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
  be played alongside the others -- in a window, as a lane of its stack
  (sec 3b). **Pushed back in, it stops**: what plays is what can be
  stopped from a tile. In a window that is the group: it stops when the
  last of its tiles' controls goes in.
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
  null, said (`qWarning`: stderr here, logcat on Android). PortAudio was
  planned first, but its headers (`portaudio19-dev`) are not installed
  here; AAudio waits for its platform.
- **`qt[:<device>]`, Qt Multimedia's QAudioSink** (`src/output_qt.cpp`,
  2026-11-12): the plain fallback sec 3a names, and Android's output
  until AAudio. Pull mode, on a thread of the sink's own; a pull is heard
  after what Qt still holds ahead of it, and what the platform holds below
  Qt is not Qt's to report, so this one plays late by that -- the trade
  sec 3a accepts for it. Built where Qt Multimedia is (`snurra.pri`;
  `SNURRA_QT_AUDIO=1` for `snurra.mk`), so "tiny" is not charged for it
  on a desktop that does not want it. **It cannot run on this machine**:
  Qt's audio on Linux is PulseAudio only, and no server runs here
  (measured: no outputs, with either backend asked for), so it falls back
  to null; it is proved on the Android emulator instead.
- **Every output's clock is one filter** (`snurra_heard_clock`,
  `snurra/output.h`): the median-nudged prediction below, taken out of
  the ALSA backend so the QAudioSink one has it too, its pulls being of
  any length. The loopback test measured the same 0.03-0.05 frames
  after the move.
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

**Sanitizers again, 2026-11-12, after the session's new threads** (the
stretch in the callback, the picture's, beats' and bytes' readers, the
facade): ThreadSanitizer found a use after free in the player at exit
(fixed 408996a) and, through its timing, a train's next tile sounding
early (fixed ff46f88). What it reports since all comes through
libraries not built with it -- Qt's own handover of a queued call or an
X11 event between its threads (the waveform's columns posted to the
main thread show as snurra's), and FFmpeg's frame threads and atomic
reference counts -- each read in full; a suppressions file was written
and dropped, since `race:` matches a library anywhere in a stack and so
would hide snurra's own races run through Qt. Under it the ALSA
handover test measured 1.0 frame against its 0.25 bar, the callback
several times slower; it passes without.

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
  tile dragged out of the window and let go is pulled out there.
- **Joining by drag and drop (Wayland), built 2026-11-12:** where a
  program can neither place its windows nor see the others', a tile's
  title starts a drag and drop (MIME `application/x-snurra-tile`: its
  socket and index). Dropped on another snurra, that window shows a strip
  on the nearer of its right and bottom edges while the drag is over it,
  and on the drop asks the giver for that one tile (`MERGE_INTO` with an
  index) -- the same handover as a snap. Dropped on nothing, a tile of a
  group is pulled out; a lone tile stays. The window's bare frame beside
  the tiles moves it (`startSystemMove`), everywhere. `SNURRA_DRAG=dnd`
  chooses this on X11, which is how it is tested:
  `a_tile_dragged_onto_another_window_joins_it` runs two snurras in a
  private Xvfb and drags with xdotool through XTEST -- joined on the
  right, pulled out onto nothing, joined below. A drop handler that
  ignores the drop fails it. Not shown: that a Wayland compositor
  carries it the same way; none is installed here.
- **A tile:** a title that is the handle, a handle for the controls,
  close; a waveform, with the playhead, where a click seeks; the
  controls, when out: play/pause, the deck's position, a level.
- **The waveform is the file's, built 2026-11-12** (`src/peaks.cpp`): the
  loudest sample of either channel in each of 1024 columns, read on a
  thread of its own from a source of its own -- never the deck's -- and
  filled in left to right as it comes; drawn a line a pixel, scaled to
  the loudest read so far, as Mixxx's visual gain does (a tone at an
  eighth of full scale was two pixels of flat line). Until then, and for
  a name with no audio, the stand-in bars from the name. `deck_test`
  checks the columns exactly (silent, half, quarter, silent read as
  0, 0, 0.5, 0.5, 0.25, 0.25, 0, 0) and that a tile closed mid-read stops
  at once; without the stop, it read all sixty seconds. Seen in Xvfb: a
  swelling tone draws its diamond. Not yet for bytes still arriving.
  About 26 characters wide, 14 at least, frameless with a size grip.
  Opened at the pointer, clamped to the screen it is on (2026-11-12; it
  was not clamped, and by the corner opened half off the screen), shown
  in Xvfb: `a_tile_opens_at_the_pointer_clamped_to_the_screen`. An Xvfb
  with no window open reports the pointer where it started however it
  is moved, so the test opens one first.
- **mplayer's keys, 2026-11-12** (sec 1: snurra is to replace it): space
  or p plays and pauses, the arrows seek 10 s and a minute, page up and
  down ten minutes, 9 and 0 (/ and *) the level by mplayer's 3, m mutes
  and comes back, q and escape quit -- on the tile last touched, else the
  first with its controls out, else the first; one played from the keys
  has its controls pulled out first (sec 3c). Nothing in a tile takes
  focus, or space would press whatever button had it. Driven through
  XTEST in Xvfb (`mplayers_keys_drive_the_tile`): paused, each seek
  moves by exactly what its key says; a handler that ignores the keys
  fails it at the first.
- **A file manager can open audio with it, 2026-11-12** (sec 3b's launch):
  `snurra.desktop` declares the audio types FFmpeg decodes here and runs
  `snurra %F`, so several files clicked open as a train; `make install`
  puts the player and the entry under PREFIX (/usr/local) and DESTDIR,
  and `make uninstall` removes those two files by name. The entry passes
  `desktop-file-validate`; installed into a scratch DESTDIR, the player
  ran from there, and uninstall left no file. The icon is the theme's
  generic `multimedia-audio-player` until snurra has one.
- **A tile in a program of its own, in one call, 2026-11-12** (sec 1:
  BeerSSH's tile): `snurra_tile::embedded(engine, state, bytes, parent)`
  -- the consumer's engine, bytes from its transport -- gives a widget
  for its layout with no window or socket of snurra's; the tile owns its
  deck and retires it when it goes, through a guarded pointer, so an
  engine destroyed first is not called. `embed_test`: it plays, holds
  where a quarter of the bytes ends and goes on when the rest comes;
  closing it retires its deck (without the retire, that fails); an
  engine gone first leaves it safe to close -- shown not to crash, which
  without a sanitizer is weaker than shown safe. The tests now take the
  library through `snurra.pri`, as a consumer does, so the `.pri` is
  under test too. A tile's waveform needs a second read of the track:
  from a file it is read; from bytes, only when the consumer hands
  `embedded` a second snurra_bytes over the same track (`peak_bytes`,
  settled 2026-11-12 on fuzzypickles' ask, inbox 20261112T0430Z: its
  bytes are a local blob once fetched, and it showed stand-in bars where
  real peaks were cheap). Of its three shapes this one, because the
  other two -- peaks from the ranges `has()` says are here, or from the
  deck's own decoding -- read the deck's snurra_bytes from a second
  thread, which source.h promises a consumer never happens; a consumer
  whose transport pays per read (SFTP) hands none and keeps the
  stand-in. The peaks fill as far as the bytes are here and wait there
  (`a_waveform_from_bytes_fills_as_far_as_they_are_here`): they are now
  handed over as each read finishes a column, since the wait is inside
  the next read -- handed 64 columns at a time, or on a timer, nothing
  showed until the bytes went on.
- **A stable facade for hosts, asked by BeerSSH** (inbox 20261112T1444Z,
  built 2026-11-12 as proposed, `include/snurra/embed.h`, `src/embed.cpp`): `include/snurra/embed.h` alone -- `snurra_media` (name,
  path, shared bytes) and a pimpl'd `snurra_player` widget: `can_play`,
  `open`, `count`, `close_all`, signals `emptied` and `failed` -- with
  `SNURRA_EMBED_VERSION` raised per additive release, nothing ever
  removed or renamed. Its promises: the engine is inside (the process's
  one); bytes stall, never fail; a host's bytes may be read from any
  snurra thread at once (so the facade passes them as the peak bytes
  too); deleting the player at any moment retires its decks and keeps
  the bytes alive until no snurra thread can call them; several opens
  make a train inside the one widget. BeerSSH vendors snurra pinned,
  builds it only with `BSSH_MEDIA=1`, and switches to it by
  `__has_include("snurra/embed.h")`, so the header appearing is all it
  needs. Two things it needed underneath: a deck that stops reading on
  demand (`stop_reading`, joined) -- a retired deck's reader otherwise
  lives until the engine sweeps it, and with another player holding the
  engine it called the host's bytes 288 times in 300 ms after the
  destructor returned; and a deck that says it failed -- opened as the
  stand-in silence, or ending at its start without a frame (FFmpeg's MP3
  reader, hinted by a name, takes garbage and gives nothing) -- which a
  tile says once, from the event loop, since a source that fails at once
  fails inside the tile's constructor. Said, not stopped: the silence
  still behaves as a tile. Measured offscreen: bytes a quarter here play
  and hold; junk bytes say failed; deleted while a reader waits (the
  control: two threads calling 568 times in 300 ms), nothing calls the
  bytes after. **BeerSSH plays through it** (2026-11-12, beerssh
  `a696f52`, snurra pinned at `6fa8d4f`): a tile above its file list,
  an 8 s tone played to the end on the sound card under Xvfb against a
  loopback sshd; no API change asked. Why a whole widget rather than
  parts, in the holder's words relayed by beerssh (inbox 20261112T1612Z):
  "With a snurra-made widget, look and features can evolve" -- a host
  that places one widget takes each new look and feature without a
  change of its own.
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
  over right the frame heard), so no clocks are lined up. 24 runs, the
  qmake build and the `.mk` one: the gains sum to 1.0000, the taker is
  on the giver's very frame every time, and nothing moves by more than
  0.05 of a frame through the fade. The bar is a quarter frame. Skipped where there is no loopback, arecord or free
  subdevice. What it took:
  - **Placing frames by `snd_pcm_delay` put the two 48 to 110 frames
    apart**: read at an arbitrary moment, it is up to a period stale.
    `snd_pcm_htimestamp` gives the queue with the monotonic time it was
    true, and brought it to 0-3 frames.
  - **One reading per side jitters by two or three frames**, and a
    handover takes one from each. The presentation time is now filtered
    as JACK's DLL does: each period predicted one period after the last;
    a reading two periods off (a start, an underrun) is taken whole.
  - **The nudge is an eighth of the median of the last fifteen errors**,
    not of the latest: on a loaded machine one reading in a few hundred
    is 1 to 5 ms off (`SNURRA_TRACE_TIMING=1` prints the worst taken),
    and an eighth of one moved the prediction 24 frames -- a handover
    sampling just after it left the taker 18 frames off, in 2 of 6 runs
    of one build. A median never sees a rare outlier.
  - **The instrument first, twice.** dmix mixes 32-bit samples at 24
    bits, so the plain ramp read to two frames and showed a 3-5 frame
    "transient" that was two truncations summed; the wrapped ramp
    (modulo 2^16) reads finer. And at full scale two crossfading clients
    sum past it and dmix clips, a period at a time: exactly 256-frame
    bursts reading as 1.15 frames, gains still 1.0000. The wrapped ramp
    plays at half scale now (1/64 of a frame through dmix);
    `SNURRA_KEEP_RECORDING=<file>` keeps a run's recording to look at.
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
  fade, seen once before the median filter and with no underrun, fits
  an outlier reading taken whole by the old filter; not seen since.
  Not yet heard on a speaker; `tone:440` through the loopback was a clean
  440.3 Hz.
- **`make hooks` fails when the install does** (2026-11-12, from
  claude-guidelines, inbox 20261112T0227Z): one continued shell line
  reported only the echo's status, so a missing hook printed
  "installed" and exited 0; `set -e` after the guard -- exit 2 missing,
  0 present, measured in a scratch repository.
- **The marker** (sec 3d): `the_marker_and_the_drop_agree_on_where_a_tile_lands`
  checks the shared answer -- beside a right edge, under a bottom edge,
  too far, not level, and the nearer of two edges. The marker window
  itself is not yet seen on a real X11 screen.
- **The train plays as one, built 2026-11-12** (sec 3b, step 3): when a
  playing tile comes within 1.5 s of its length less the overlap, the
  next tile in its row is started at frame 0 on that exact moment
  (`snurra_deck::start_at`, the anchored start a handover's taker uses,
  aimed at a time to come) and the two cross over the overlap along a
  quarter circle -- equal power, since a train's two tracks are
  different signals and straight lines dip 3 dB, where a handover's
  identical ones cross straight. The controls follow what plays; a tile
  stopped before its next began takes the next back. **The overlap is
  5 s** ($SNURRA_TRAIN_OVERLAP_MS), never more than half either tile -- a
  DJ mix's crossfade, decided from the aims; a tile's own when
  envelopes come (step 5). Measured from a capture of two constant
  files, one per channel: the overlap exactly 48000 frames for 1 s, the
  summed power 1.0000 through it, each side whole outside it, and B all
  of itself; straight lines (power 0.31 off) and starting B at once (a
  third of it lost) each fail. Not yet: beat-snapping the overlap
  (step 6).
- **Drawers, built 2026-11-12** (sec 3b): a tile has an upward drawer
  (empty until video) and a downward one, its controls; one height for
  every waveform. A window takes the drawers out of its tiles and gives
  each lane three grid rows -- upward drawers, tiles, downward drawers --
  and re-fits its height (never its width, which a person may have set)
  as a drawer opens or closes; a tile alone, embedded, keeps its drawers
  in itself. The look: `drawers` makes the window transparent around its
  tiles and open drawers; `square` paints the same layout opaque; by
  default `drawers` on Wayland and on X11 with a compositor (the owner of
  `_NET_WM_CM_S0`, asked through xcb), `square` everywhere else,
  offscreen included; `--layout` / $SNURRA_LAYOUT decides outright.
  Measured offscreen (`a_drawer_adds_room_and_leaves_every_waveform_as_
  it_was`): A beside B, C below; A's controls out, all three waveforms
  keep their height, A's and B's tops stay level, the drawer sits flush
  under A at A's width, C and the window move down by the drawer's
  height exactly, and closing it restores every number; with the drawers
  left inside the tiles, B's waveform falls 24 px below A's and it fails.
  **Seen** (2026-11-12) by running the player itself in a private Xvfb
  and taking screenshots: square on a bare X server, and the drawers
  look under xfwm4's own compositor (Xvfb with `+extension Composite`;
  xfwm4 needs a session bus for its settings, and one started with
  dbus-run-session was orphaned when the run was killed, so the bus is
  started with its pid known and killed by name). The window is
  transparent around its tiles and their open drawers, the video sticks
  out upward and the controls down. The player says on start which look
  it chose. The screenshots found two faults, fixed in 4741389: a
  picture filling half its drawer, and the position slider squeezed to
  nothing.
- **Tempo and key lock, built 2026-11-12** (the holder: key lock next).
  A deck has a tempo -- track frames a frame out -- and key lock, both
  heard from the next period, since the stretch runs in the callback,
  not in the reader half a second ahead (`snurra_stretch`, allocation-
  free once built). Without key lock, varispeed: Catmull-Rom between the
  track's frames, so the pitch moves with the tempo. With it, WSOLA,
  SoundTouch's method, which Mixxx uses for its cheaper key lock: 40 ms
  segments taken a tempo's step apart, each placed within 15 ms where
  its first 8 ms best match the last's end, crossfaded there. Neither
  library is installed here; this is snurra's own. At tempo 1 with no
  key lock a deck takes the plain path, so every exactness measured
  above stands. The tile: a tempo slider of +-8 % in tenths (a
  double-click resets it) and a key button, carried by a handover. In a
  window a lane's timeline counts each tile at its tempo -- seeks, group
  starts and when a train's next begins. Measured: varispeed 1.05 on the
  ramp, every frame out 1.05 on from the last to the ramp's own
  resolution (0.004), and a change heard the next period; a 440 Hz tone
  at 1.2 is 528 Hz in varispeed and 440.01 Hz with key lock, the track
  at 1.2 and its level within 0.09 dB through every join -- without the
  search, 434 Hz and 1 dB dips; a lane at +10 % sought with its group
  to 10 s stands at 11 s of its track and goes on 1.10 times as fast.
  mplayer's speed keys drive it (2026-11-12): ] and [ by 10 %, each the
  other's inverse, } and { double and halve, backspace back to 1 --
  tested by key presses through X. Not yet: pitch shifting on its own,
  and key detection. **Sync** (2026-11-12): a tile's button sets
  its tempo so its beat runs at the leader's -- the first other tile
  playing with a grid, at that tile's tempo; a double or half if nearer
  -- then bends its phase onto the leader's, the shorter way round, as a
  nudge kept in the lane's offset. Measured: 126 BPM synced to 120,
  tempo 0.95234, beats 0.57 ms apart once the bend is out; without the
  bend, 224 ms.
- **Jumps and nudge, built 2026-11-12** (the holder's steps): one pair
  of buttons in the controls, labelled with the track's step -- 4 bars
  where the beat detector found a grid (16 beats, so a jump keeps the
  place in the beat), else 10 s under ten minutes, else 5 % -- held,
  they repeat; with Ctrl they nudge. The keys keep mplayer's two levels
  at the track's step: left and right the small, down and up the large
  (16 bars, 60 s or 20 %); page up and down 10 minutes. A nudge moves
  one tile 10 ms against its lane's time and every other lane, and is
  kept as its offset (`offset_us`, carried by a handover), so the group
  transport keeps a lane lined up by ear. Found on the way: a jump read
  the position the deck last rendered, so a second jump inside one
  output period went from the old place; a deck now reports a seek it
  has been asked from the moment it is asked. Measured offscreen: steps
  and labels by length; a 120 BPM kick found at 119.999 and a jump of
  exactly 16 of its beats; a nudged lane 10 ms ahead, still so after a
  group seek. **A playing nudge is a bend** (2026-11-12): the track a
  tenth faster or slower until it has moved, worked off in the callback,
  exact in sum -- as a seek it left a silence the output's latency long
  at every press. Measured on the ramp: 10 ms gained over 0.5 s, no
  frame silent, no step over 1.1, 480.000 frames gained. With key lock
  on the bend goes through WSOLA, whose steps are a segment long, so its
  sum is near, not exact. A paused nudge is still a seek. **A jump while
  playing has no gap** (2026-11-12): the window starts every lane on one
  moment a little ahead, and a deck dropped what it played at once and
  waited in silence for that moment -- 100 ms of it in the test, about
  60 ms in the player, at every jump. A seek for a moment ahead, made
  while sounding, now plays the old place until then and switches on the
  frame nearest that moment, the new place's first block held till then.
  Measured on the ramp: no silent frame, one jump, onto the target, 7 us
  from its moment. **And a cut is crossed over** (2026-11-12): any seek
  seen while sounding -- a click on the waveform, a jump, a sync -- keeps
  what the old place had next in the block it was playing, up to 5 ms,
  and fades it out as the new place rises over exactly that length, so
  the two sum whole. The new place still starts on its frame; only its
  gain ramps. Measured on the ramp: the cut 7 us from its moment, a
  crossover of 160 frames (the block's rest), the summed gain whole to
  1e-4, halfway half of each; a fixed 5 ms rise against that shorter
  tail had dipped the sum to 0.67. A seek while paused, or a tile not
  yet sounding, keeps no tail; the stall rule -- no sound while not
  moving -- holds from 5 ms after a seek, as before it.
- **Subtitles, built 2026-11-13** (the holder: "yes, subtitles too,
  embedded and .srt"). `snurra_subtitles` (subtitles.h) is one track of
  cues -- start, end, lines of styled runs (italic, bold), or a picture
  placed in fractions of the video -- looked up by time. A `.srt` beside
  the file (`film.srt`, then `film.en.srt` and the like, in name order)
  is read whole by snurra's own reader rather than FFmpeg's: UTF-8 or
  UTF-16 by its mark, else Windows-1252, which FFmpeg's SubRip decoder
  refuses without iconv (not known to be in the Android kit's FFmpeg);
  CRLF, a missing number, a period for the comma, a missing hour and a
  position after the end time all read; `<i>` `<b>` kept, other tags
  dropped, `{\an8}` puts it at the top. Embedded streams are read by the
  picture's reader as it demuxes -- SubRip, ASS and MP4's text through
  FFmpeg's ASS text (overrides, drawings dropped), and bitmaps (PGS,
  DVD) as pictures; a Blu-ray cue with no end ends at the next, an
  empty one only ends the one before, a cue read again after a seek is
  not doubled. A seek lands on the key frame before its target, so the
  line on screen there began before anything read: after each seek a
  second reader of the file, every other stream discarded, reads the
  subtitle packets from 10 s before the target up to it -- or up to
  1 MB past where the picture's reader stands, bounded inside FFmpeg's
  read by its interrupt callback, since one read with the rest
  discarded runs on to the next subtitle however far. Files only: a
  host's bytes may not be there. Shown from the start
  where a `.srt` lies beside the file (put there to be shown) or a
  stream is marked default or forced; otherwise none until asked -- a
  film with twenty languages does not open on the first, as mpv
  chooses. mplayer's `v` shows or hides them (with none chosen, the
  first track), `j` takes the next track and after the last none, and
  a two-second notice says which. Drawn white on a black outline,
  1/15 of the picture's height, wrapped to its width, near its bottom.
  Bitmap subtitles are tested with a PGS stream the test writes by
  hand, FFmpeg having no way to encode text into one: shown from 1 s
  where it was placed, white, gone at the empty set at 2 s. (ffmpeg
  starts each input at zero, so the stream opens with an empty set at
  0, or the box moves there.) DVD subtitles share the path untested.
  Measured (`subtitles_test`): the reader's
  cases, ASS text, cue lookup, open-ended and re-read cues, the files
  found beside a video; SubRip in Matroska, ASS in Matroska and
  mov_text in MP4 read as the picture passes, italic kept; in a window,
  the words heard on screen, `v` and `j`. Breaking the embedded read,
  the Windows-1252 table or the beside-first choice each fails its test.
- **Video, built 2026-11-12** (sec 3b, the holder: audio, then video,
  in the drawer that opens upward). `snurra_video` decodes a file's
  picture on a thread of its own, six frames ahead, and gives the last
  frame whose time is not after the one asked -- times from the sound's
  start, the zero the deck counts from, so a container that starts the
  picture late or early is shown so; it seeks itself when asked behind
  what it has or more than a second past it. The tile asks for the
  frame at its deck's heard position every 16 ms while the drawer (or
  the full-screen picture) shows. The drawer is the tile's width and the
  picture's shape tall; it opens with the controls, so on the first
  tile only, and the window grows up so the tiles stay still on screen;
  a maximize button shows the picture full screen, Escape or a
  double-click brings it back. Pictures are converted to RGB here, not
  by libswscale, which this machine has no headers for and a tiny
  player need not link: 8-bit planar 4:2:0, 4:2:2 and 4:4:4 and
  NV12/NV21, and since 2026-11-12 their 10-bit kin (planar 10-bit,
  P010) that HEVC and AV1 releases are mostly in, BT.601/709/2020,
  limited or full range; and packed RGB in any byte order, a palette
  and grey. Anything else shows no picture yet. H.265 and AV1 at 10
  bits, QuickTime Animation and PNG in RGB, a paletted and a grey PNG
  and a GIF (FFmpeg decodes it to BGRA, not its palette) pass the frame
  timing test below; without its case, each of the new layouts fails.
  A picture without sound plays (3c54730), as long as its picture: the
  picture's reader says the length (its stream's, else the file's) and
  the deck's stand-in silence is told it, so a 3 s GIF is 3 s and ends
  with its last frame -- it had run three minutes. Measured (`video_test`): H.264
  with B-frames, the same with the picture 0.2 s late, MPEG-4 Part 2 and
  MJPEG in Matroska, VP9 with Opus in WebM, each frame grey at a level
  naming it: at 234 times forward, back and jumping, the frame shown is
  the one ffprobe's frame times say a screen shows then, by time and by
  picture, and never one from the future; counting from the picture's
  own start instead fails four of the five (Matroska's 3 ms, WebM's
  7 ms). In a window playing, 60 samples: 50 the frame heard, 10 one
  behind (the 16 ms), none ahead; shown 80 ms early, all 60 ahead and
  it fails. **From bytes too** (2026-11-12): the FFmpeg input over a
  host's bytes is one piece now (`snurra_bytes_input`), which the sound,
  the picture and the beats each open -- through the facade the host's
  bytes feed all of them, embed.h allowing reads from several threads,
  and the tile's going joins every reader. A picture from bytes is the
  file's: at 227 times the same frame, picture for picture. Found on the
  way, a livelock: a forward seek decodes from the key frame before its
  target, x264's key frames are 250 frames apart, and a reader behind by
  more than a second seeked again before arriving, from the key frame
  again; a seek on its way is no longer started over by a later target
  forward. That test also failed 4 times in its first 14 runs, twice
  after the fix, stalling 3 to 6 s; 42 runs since, alone, polled two ways
  and four at once under load, all passed. The cause is not established;
  the test now says, if it stalls, at what time and what each reader
  gave.
- **Envelopes, built 2026-11-12** (sec 3b, step 5): `snurra_deck::
  set_envelope` takes up to 64 points over the deck's frames, read by
  the callback under a seqlock as the fade is, and multiplies level,
  slew and fade; the tile keeps them in its state (`envelope`, carried by
  a handover), draws them over the waveform and edits them as sec 3b
  says. Measured (`an_envelope_is_heard_on_the_tracks_own_frames`): a
  ramp read back through a curve down to 0 at 1 s and up to 1 at 2 s,
  84864 frames, every gain on its line to 1.1e-07, and after a seek to
  0.5 s it goes on from that frame; laid on the frames played rather
  than the track's own, the same test is off by 1. The mouse (offscreen,
  `an_envelope_is_drawn_on_the_waveform`): off, a press seeks; on, a
  press makes a point a quarter across and up where it was pressed, a
  drag moves it to silence, and a drag out removes it.
- **On Android, built 2026-11-12** (sec 1: BeerSSH's tile, Android first).
  `make android` (tool/android.mk, copied from the shared fragment, with
  a build rule after bbq-predictor's) builds the player for a Qt kit's ABI;
  `VERSION` (0.1.0) gives the versionCode and `APP_ID` is se.vibes.snurra
  -- both chosen here, the holder's to change. FFmpeg is the kit's own: Qt
  ships 7.1 for its multimedia, libraries without headers, and the
  desktop's 7.1.5 headers serve (libavcodec 61.19.101 on both); only the
  four FFmpeg header directories are copied into the build tree, since the
  multiarch directory they sit in also holds glibc's. androiddeployqt's
  readobj cannot find them under Qt's `_<abi>` names and says so, but
  packs them as Qt Multimedia's dependencies. Measured on the emulator
  (Galaxy_Nexus_API_33, x86_64, API 33): `tone:440` opened an AAudio
  stream through QAudioSink, 48 kHz stereo float, shared; the audio
  server counted 99264 frames in 2.06 s of host time (48210 a second,
  the remainder adb's timing) and no underrun, and reports 131 ms of
  latency below Qt, which this output does not see. Not yet: heard on a
  phone; a tile per process does not apply there (one process per app),
  so the embedded tile is the shape BeerSSH will use.
- **AAudio, Android's default output, built 2026-11-12**
  (`src/output_aaudio.cpp`): the engine in AAudio's data callback, low
  latency asked, shared by default and `aaudio:exclusive` by choice
  (sec 3a); QAudioSink if it will not open, then null. Each callback's
  frames are placed by AAudioStream_getTimestamp -- the moment a known
  frame was presented, on CLOCK_MONOTONIC, clock.h's own clock -- through
  the shared filter; AAudio.h allows its getters in the callback, and
  nothing else is called there. It says on start what was GRANTED, which
  need not be what was asked. `--output <spec>` sets the output from the
  command line, since an app on Android has its arguments and no
  environment. On the emulator: AAudio, AAudio asked exclusive, and
  QAudioSink all pulled at the device's rate (47823-48279 frames a second
  of host time, no underrun); the emulator refuses exclusive ("EXCLUSIVE
  sharing mode not supported") and low latency, and snurra said so
  ("shared (exclusive asked) ... not low latency", bursts of 960, a
  buffer of 1920), its server reporting 128 ms against QAudioSink's 132.
  **Low latency is not shown**: that needs a phone. **A stream AAudio
  closes underneath is reopened** (2026-11-12): the error callback marks
  it dead, and a watcher thread of the output's own -- never an AAudio
  thread, as AAudio.h and Oboe have it -- closes it and opens another on
  whatever the route is, at the engine's rate or not at all (it tries
  again every half second), with the heard clock restarted; the decks
  never know. On the emulator no real disconnect can be made: killing
  the audio server, its legacy path restored the AudioTrack itself, no
  error raised (snurra's track active again, 0 underruns). So
  `--output aaudio:drop=<ms>` takes the error callback's own path that
  long after the start: stream s#1 closed, s#2 opened and started 9 ms
  later, snurra's track the only one active, 97152 frames counted in the
  next 2 s, 0 underruns. A route change on a phone (headphones pulled)
  is what remains to see. Seen once and not since: the first launch
  after a cold boot (an older install replaced) opened no audio at all
  -- no snurra line, no audio thread, Qt's main loop idle; three fresh
  installs, one without arguments, all opened AAudio. Unexplained.
- **The stack plays as one, built 2026-11-12** (sec 3b, step 4): a
  tile's button, waveform and the keys ask the window, which plays,
  pauses or seeks every lane at once (`snurra_node::start_group_at`):
  the time that tile's place names, found in each lane by the train's own
  overlaps, every lane started with `start_at` on one moment -- now plus
  the output's latency and 50 ms. `PLAY` and `SEEK` on the socket do what
  a tile's button and waveform do. Measured from a capture
  (`a_stack_plays_its_lanes_at_once_from_one_time`): two lanes of three
  and two seconds, paused, sought to 1 s from the lower and played, began
  on the same frame and played exactly 96000 and 48000 frames, at their
  level throughout; with each lane seeking and playing on its own instead
  (no shared moment) the test fails every run -- a period apart, or
  starting mid-slew. **Found on the way, a deck fault**: a pause and a
  seek within one period played the new position's first 5 ms -- the
  slew down to silence ran on over it -- and moved the deck 239 frames
  while paused, so two lanes held different frames. A seek seen paused,
  or a paused deck starved, now starts from silence
  (`a_seek_just_after_a_pause_is_heard_as_nothing` fails without it,
  loudest 0.99).

## 5. Resuming work here (2026-11-12)

Built and tested: the handover base, tiles as waveforms with controls
only for the first (sec 3c), the drop marker (sec 3d), and audio with the
handover as a crossfade (sec 4). Not seen or heard on a real screen and
speaker yet -- every test runs offscreen; trying the drag, the snap, the
marker and a merge by ear is the first thing to do.

Published 2026-11-12 at `github.com/funklord/snurra` (public), pushed
over SSH as the other trees are. Open, the holder's: audio or video
first; the analysis licences (sec 3e).

Next: BeerSSH and fuzzypickles can now vendor snurra and hand it their
bytes -- their sessions' to take up, signalled in their trees (inbox
20261112T0229Z); the drag and drop tried on a real Wayland compositor;
AAudio's low latency measured on a phone, and its reopen after a route
change heard there (sec 4); a handover between an ALSA output and a hw: one (two devices, two clocks
-- drift, not only latency, then matters).
