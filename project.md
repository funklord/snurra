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

## 4. State

A skeleton, 2026-11-11: this file, the README, the copied style rules and
gate, the commit-msg hook, and a Makefile with nothing to build yet.

**A bare `make` builds nothing, and says so**, because there is no
software yet; `all` gains the library and the player with the first code.
That is the exception build-and-commit.md asks to be written down.
