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
  positions in microseconds, so a taker can be in step to a frame.

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
  Sabotaged by hand 2026-11-12, each failing its test: the giver cutting
  at the fade's start (sum 0), the taker not anchored (919 frames apart,
  and the position test), the giver's deck retired before its fade (sum
  0). Earlier: stopping the giver at OFFER, never aborting, every launch
  taken as the first, pushed-in controls left playing.
- **ALSA on a real device clock:** `tone:440` to `plughw:Loopback,0,5`
  (this machine's `snd-aloop`; subdevice 0 was busy with someone else's
  use), recorded from `hw:Loopback,1,5`: 440.3 Hz at the 0.2 peak, no
  quiet run inside it longer than a zero crossing. Not yet a handover
  between two ALSA outputs, and not yet heard on a speaker.
- **The marker** (sec 3d): `the_marker_and_the_drop_agree_on_where_a_tile_lands`
  checks the shared answer -- beside a right edge, under a bottom edge,
  too far, not level, and the nearer of two edges. The marker window
  itself is not yet seen on a real X11 screen.
- **Not yet:** codec timing (sec 5), Wayland's drag-and-drop merge, a
  real waveform, and the train's and stack's own mixing (sec 3b).

## 5. Resuming work here (2026-11-12)

Built and tested: the handover base, tiles as waveforms with controls
only for the first (sec 3c), the drop marker (sec 3d), and audio with the
handover as a crossfade (sec 4). Not seen or heard on a real screen and
speaker yet -- every test runs offscreen; trying the drag, the snap, the
marker and a merge by ear is the first thing to do.

Published 2026-11-12 at `github.com/funklord/snurra` (public), pushed
over SSH as the other trees are. Open, the holder's: audio or video
first; the analysis licences (sec 3e).

**Next, from the holder (2026-11-12): codec timing tests.** Every codec
added should be tested for how it keeps time against other files, so
timing faults are found and fixed before anything depends on them. The
crossfade test proves decks agree with each other only for the ramp
source, which is exact by construction; real codecs bring encoder delay
and padding (MP3, AAC, Opus pre-skip), resampler delay (44.1 kHz into
48 kHz), and key-frame seeks, none of it measured yet. Then: the Wayland
merge by drag and drop; a handover between two ALSA outputs measured
through the loopback.
