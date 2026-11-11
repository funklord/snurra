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

- **The decoding and output backend.** Qt Multimedia's `QMediaPlayer`
  (FFmpeg underneath in Qt 6) is the least code and plays from a
  `QIODevice`, but mixes nothing: one stream, one volume. Decoding with
  FFmpeg's libraries and mixing into one `QAudioSink` gives mixing and
  control of buffering, at the cost of owning the decode loop and video
  presentation. libmpv plays anything and is not small. Mixing is an aim,
  which leans to the second; measure before choosing -- what a
  `QMediaPlayer` build adds to an Android APK is itself unmeasured
  (BeerSSH's plan says "several MB").
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

## 4. State

A skeleton, 2026-11-11: this file, the README, the copied style rules and
gate, the commit-msg hook, and a Makefile with nothing to build yet.

**A bare `make` builds nothing, and says so**, because there is no
software yet; `all` gains the library and the player with the first code.
That is the exception build-and-commit.md asks to be written down.
