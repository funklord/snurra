# snurra

## 1. What this is

A small graphical media player, and the library it is built from. Named
2026-11-11 by the copyright holder ("snurra", Swedish for spin, as a
record does), the one name of more than fifty checked that was free.

**The aims, from the holder (2026-11-11):**

- **A graphical but tiny player to replace mplayer** in the holder's own
  use, **with some mixing**: more than one stream, their levels, moving
  from one to the next. "A media player is rarely small" -- so small is
  a goal held deliberately, not a description. **Replacing the holder's
  use, not mplayer** (the holder, 2026-11-23): "the aim was not to be
  100% compatible, just to replace my usage of it. If we can use the
  same arguments, then that is great for familiarity, but we also
  shouldn't copy bad design, and snurra is a superset of its features in
  many regards, so some things necessarily have to change." So an
  mplayer option is taken where it serves how the holder plays and fits
  snurra's own design; it is not added because mplayer has it. What
  the holder's shell history shows of that use (2026-11-23): three
  lines, each a bare `mplayer FILE` -- an `.m4a` track, a downloaded
  `.webm` DJ set with its picture -- no option at all; so the plain
  open, sound and picture, is what has to be right.
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

## 1a. State (folded 2026-11-21)

- Level with origin at the run's last commit. **Decks and the train,
  sec 3f, built 2026-11-20/21:** the timeline as data with channels
  seated; a deck plays a player (nested, its tracks' EQ, trim, beat and
  waveform); the deck view on the wire with 2 or 4 decks, arranged and
  sided as the person's controller; a player unfolded across the decks
  and conducted, a hand taking over, rides and moves written back, a
  deck started by hand, folded back, unpacked into a window; MIDI in with
  learning by Alt-hold, both ends named, each player its own (an
  emergency mix), 14-bit, browse and load, learning without Alt (the
  deck view's "learn" button, Ctrl+L in a window -- TDE takes Alt-drag);
  listeners as plugs on decks and on a window's tiles, several, chosen;
  a player tile analyses its tracks never analysed. The first day's new
  tests passed under ASan and UBSan.
- **The window reshaped, 2026-11-21 (evening), from the holder's notes**
  (sec 3b, 3c, 3f): one bar to a window, tiles named on and dragged by
  their waveforms, a track's menu (Remove, its own window, Open in the
  decks), one control panel under every lane, a tile dragged above or
  below into a lane of its own with a preview, beats and bars along the
  top, one picture on top dissolving through a crossfade, and the way
  back from the decks ("Into a window"). The EQ's knobs move their third
  with the drawn shape kept, the three splits stay, flat neighbours join
  (sec 4, More bands).
- Earlier the same day: live streams shown as live, an icon, -shuffle,
  -audiofile, --chapters-file, Wayland idle via the portal.
- The suite is built at `-Os`, as the player ships (since 2026-11-23;
  qmake had built it at `-O2`, so it never ran the code people run).
  The suite takes some 4 to 5 minutes on a quiet machine
  (`TEST_TIMEOUT` 600 s). Read its exit code, not its FAIL lines. A
  test waiting for a snurra process to end by itself uses `ended()`:
  `waitForFinished()` is false for one already gone, which read as a
  window that stayed (2026-11-21). Its
  test binary runs one function when named, but QtTest fails every class
  that lacks the name -- read the named class's line. `make check-mk`
  is not part of `make test`; run it after a change to the build lists
  or the fuzznet pin.
- Waiting on the holder: a real screen and speaker trial, a Wayland
  compositor (weston), the AcoustID key, the phone (install, a node of
  its own, the fold cover, Android's FFmpeg), moving a joined window, the
  cross-process drop's entry point, a deck's
  hand gain (Mixxx's pregain, sec 3f), what the phone's deck view cuts
  (sec 3f step 3). qtkit's remote came 2026-11-23: it is the submodule
  at `qtkit/` (sec 3).

## 2. Decided

- **Qt Widgets**, C++17, as every GUI in this workspace. No QML.
- **Prefix `snurra_`** for everything the library exports; module-private
  names unprefixed (code-style.md).
- **Vendored by its consumers** as a submodule, with a `.pri` for qmake
  and a `.mk` for hand-written Makefiles, as softkeys is. Built
  2026-11-12: `snurra.pri` compiles the library into the consumer, moc
  included, and `snurra.mk` derives its lists from the tree; `make
  check-mk` builds the player from the `.mk` alone and runs the tests
  against it ($SNURRA_BIN). `make test` does not run it, so it stopped
  linking unseen when the fuzznet pin brought wire code (f1a6bbd,
  2026-11-18) into `app/fuzznet.pri` and not the Makefile's list; put
  right 2026-11-20. Run it after moving the pin. Headers are in `include/snurra/` and included
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
  **The kit's FFmpeg version moves (BeerSSH, inbox 20261113T0126Z,
  2026-11-13):** this machine's 6.12.0 kit is a July pre-release with
  FFmpeg 7.1 (libavformat 61), the released kit CI installs carries 9.0
  (63), while the headers come from the desktop (Debian 13: 7.1). The
  Android rule now compares the header's `LIBAVFORMAT_VERSION_MAJOR`
  with the `Lavf/NN.` the kit's library names itself by and refuses a
  mismatch, as BeerSSH's does; tried both ways (61 and 61 passes,
  headers claiming 63 are refused). BeerSSH measured all of snurra's
  sources compiling against 9.0's headers for arm64; nothing has played
  against 9.0 yet; on 2026-11-13 BeerSSH's CI linked snurra (4fea617)
  in its Android job against 9.0's headers and the release kit, green. A
  release-kit build needs 9.0's headers given as
  `FFMPEG_HEADERS`. BeerSSH also measured its APK growing 17 MB with
  snurra when FFmpeg's four libraries go as `ANDROID_EXTRA_LIBS` and no
  QML is pulled in, against 27 MB above.
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

- **A distributed music library: snurra on fuzznet, fuzzypickles on
  snurra, or both? (the holder, 2026-11-13).** Measured from the three
  trees rather than reasoned:
  - **fuzzypickles already plays through snurra.** Its Library tab
    (its sec 24, step 3g) is the estate's catalogue on fuzznet, and
    since 2026-11-12 it plays through a snurra deck over its own
    `snurra_bytes`, `fzp_library_bytes` (its steps 1, 2a, 2b), seeking
    in a file still arriving. What it has not taken up: the waveform,
    the library's BPM and key, more decks -- its session's to take.
  - **The distributed library exists, and is fuzznet's.** The holder
    decided 2026-09-05 (fuzzypickles sec 24) that the file store, the
    catalogue -- global per estate, holders as metadata of a file --
    and the streaming belong in fuzznet. So "snurra uses fuzznet" does
    not mean building a library; it means showing the estate's one.
  - **What that would cost snurra, measured:** not fuzznet's core. A
    node's local socket admits any process of its user (fuzznet sec 2:
    owner-only, same user), and the verbs are there -- `list catalog`,
    `get catalog ROOT`, `get file ROOT OFFSET LENGTH`. So the catalogue
    could be one more source of the library beside a folder, and a
    file's bytes one more `snurra_bytes`, as BeerSSH's are: a client of
    the user's own node, some hundreds of lines, no crypto, no estate
    identity in a player meant to be tiny. Linking fuzznet's core
    instead would put a second estate stack in the player and is not
    recommended.
  - **What it gives the user:** the holder's music on the NAS
    (dl180g6-03) in snurra's library and DJ player, on the phone too,
    without NFS -- what the phone app lacks today (it sees only the
    phone's own music).
  - **Decided by the holder 2026-11-14: both** -- "none of these
    projects is fully stable yet ... they won't get stable unless they
    are tested rigorously and flaws get ironed out", and the calls live
    in fuzznet's own code, so a change of format beneath them is
    fuzznet's. **Being built** (sec 3b, the estate):
    - fuzznet vendored at a pin, a shallow submodule (its history is
      1.2 GB, the pinned tree 7 MB), fetched by the Makefile, the
      closure the calls need -- `local/*.c`, `hex`, `constant_time`, and
      since f1a6bbd the status and situ wire code and `files_local` --
      compiled into the player and the tests only (`app/fuzznet.pri`;
      some 16 KB of the player's code), never into `snurra.pri` or
      `snurra.mk`. It links only with sections collected: `rpc.c`'s
      other calls reach modules the closure leaves out.
    - `app/estate.h`: the catalogue listed (`fzn_rpc_library_list`),
      and a file as `snurra_bytes` over `fzn_rpc_library_play` slices
      with `fzn_rpc_blob_fetch_at` for `want`, named `fuzznet:ROOT` and
      opened through a resolver the library core now takes
      (`snurra_set_bytes_resolver`), which the player sets in `main` for
      itself and every tile process.
    - Tested against a real `fuzznetd`, built by `make test` from the
      vendored copy (with its Monocypher, fetched the same way): a file
      stored and catalogued through fuzznet's calls is listed with its
      name, kind and this host holding it, plays 144000 frames at 440
      Hz, and a second file sought back and forth to five places gives
      the very frames it gives opened by path
      (`a_catalogued_file_is_listed_and_played_from_the_node`); no node
      says "not running" and lists nothing. The adapter reading two
      bytes off its offset fails it. A first expectation was wrong,
      not the node: a mono file comes out 3 dB down a side, by path too.
    - The catalogue as rows of the library and the DJ player's browser
      (`snurra_estate_offer`, listed off the UI thread at start and each
      minute): a file's catalogued name, "on this host" or "on N other
      hosts" in the Folder column, beside the folder's files; played and
      dragged by its `fuzznet:ROOT`. The core takes them as a host's own
      rows (`snurra_library::set_entries`), kept across a change of
      folder and replaced whole when listed again
      (`the_library_lists_the_estate_beside_the_folder`; without the
      keeping, a change of folder loses them and it fails). A name with
      a scheme -- `fuzznet:`, `tone:` -- is now given to a tile as it is
      (`snurra_source_of`): made absolute, it had become a file under
      the current folder.
    - **A file still arriving from another host**
      (`test/estate_two_hosts.py`, run by `make test`, 2 s): two of one
      user's hosts over loopback, made by fuzznet's own scenario helpers
      at the pin; A puts a 10 s WAV and catalogues it, B lists the name
      and holds the reference and none of the bytes; snurra plays it
      from B. Found on the way: **a node answers a play of a file it has
      never fetched with "not in the library"** -- though its catalogue
      lists it -- since fuzznet tells "never heard of it" from "none of
      it yet" and a play reads this node's store. So opening a file to
      play it begins the fetch, from the start, when the first probe
      says so (`snurra_estate_bytes::begun`); the reader moves it where
      it seeks. Measured: sought to the middle first, it played from
      there after 271 ms, the fetch moved once; then all 480000 frames,
      each the file's own as A was given it. With `want` doing nothing
      the check that the fetch was moved fails -- but over loopback the
      fetch from the start reached the middle as soon anyway (355 ms),
      so this shows the fetch is asked to move, not that it then plays
      sooner. **Over a lossy link** (`make test-estate-loss`: fuzznet's
      faults build, a fifth of what is sent dropped) it still plays
      every frame right, and shows the fetch does not follow the reader:
      sought to the middle, the middle became playable only as the whole
      file did, whether or not `want` moved the fetch -- 13.0 and 46.6 s
      with it (the whole 13.2, 47.9), 50.0 and 35.7 s without (51.2,
      35.9). The spread swamps any gain. That is fuzznet's playhead
      fetch (its sec 998), reported to it (inbox 20261113T2344Z).
      **fuzznet answered (inbox 20261114T1044Z):** "not in the library"
      is its own code now, `FZN_RPC_ERR_NOT_FETCHED` (bbe14fc), and the
      play still leaves beginning a fetch to the player, a probe being
      what a listing makes of every row -- so snurra, pinned at bbe14fc,
      begins one on that code alone; the qsort of an empty catalogue is
      guarded (2400a7b). On the loss it read a sibling marked silent at
      its rounds, and asked for B's log: the scenario prints it under
      `--loss` now, and it said otherwise -- `asking 1 peer(s)` a second
      after the play began, then nothing on the file for 24 s, then 768
      of its 938 leaves in one batch. Sent back (inbox 20261114T1109Z):
      a lost request or answer waiting out a long retry, more like, and
      fuzznet's to say. **fuzznet found it (inbox 20261114T1539Z, its
      8767ce7, sec 1165):** every want was answered, but its loop resent
      only when a poll of up to a second returned, so a span's 3 s wait
      held one or two resends of three and most waited it out; it polls
      no longer than the next resend now. Pinned at f1a6bbd since
      (2026-11-18), four runs at 20 % loss played the middle after 9.6,
      10.2, 10.9 and 11.7 s, the whole file there with it -- the 24 s
      silence gone. What is left is fuzznet's: a whole span resent for
      one missing piece (its sec 1164). The pin needed `wire/status.c`,
      the generated `situ.c`, `status.c` and `files_local.c` beside
      `local/`; and the glob over `local/` is in the qmake stamp now, which
      a new `rpc_binary.c` had slipped past. fuzznet then renamed its
      `wire/status.c` to `status_message.c` (inbox 20261118T2105Z, its
      814d033), apart from the generated `status.c`, and snurra lists it
      at that pin -- where a wrapper had compiled it under a name of its
      own, both being `status.o` in qmake's one directory. **The rename
      stopped the player's build from outside the tree**: qmake, writing
      paths relative to the build directory, looked headers up for the
      dependencies against the project's own, so `-I../fuzznet` found the
      sibling checkout -- `estate.o` depended on `../../fuzznet`, another
      tree's headers, and on none of the vendored copy's, and when the
      sibling renamed the file the build stopped (2026-11-18, for an hour
      before it was seen: `make` failed under a pipe that hid it). Paths
      are written absolute now (`QMAKE_PROJECT_DEPTH = 0`), and the
      Makefile names each `.pro` by its absolute path, as the Android
      rule already did, which a `BUILD_DIR` not one level down needs.
    - **Titled by its catalogued name**: a tile, the window, the picture's
      full screen and a DJ deck called an estate file by its 64 hex
      digits, each making a title of the name's file name. They ask
      `snurra_title_of` now, which takes a host's word where it has one
      (`snurra_set_title_resolver`; the estate asks the node once a
      name) and the file's name otherwise; without the estate's, the
      test sees the hex and fails.
    - **Kept by its lasting name**: the beat cache keyed a file by its
      size and time, which a `fuzznet:ROOT` has none of, so its beat and
      key were never kept and Find tempos fetched and analysed it again
      each time. A host now says which names last
      (`snurra_set_lasting_names`; a root names its bytes by their hash)
      and the cache keeps those by the name alone, and the library fills
      a host's rows from it as it does a folder's. Without the estate's
      word the grid is not kept and the test fails.
    - **Open, an intermittent crossfade, 2026-11-14**:
      `a_tile_pulled_out_crosses_over_in_step_without_a_gap` measured the
      gains summing to 0.56 once in a full suite and 0.94 once alone, at
      load 34 to 38 (other sessions' gates), on the build with the
      estate; 2 in 16 runs of it, none in 15 of the build before
      (3865735, from a worktree), then ten of each alternated, all clean
      (worst 0.0004 and 0.0003 off). Too few to blame the change or
      clear it; the change adds nothing to the crossfade's path that is
      seen. Kept open, not explained. **Since (2026-11-14):** 40 runs
      more at load 20 (8 busy loops added), none over. One explanation
      tried and disproved: the taker says READY after 500 ms whether its
      decks are in step or not, and a slow new process might fade in
      unready -- but with READY sent at once, six runs all summed to
      1.0000 within 0.0002, the COMMIT's moment being a latency and a
      period ahead, by which the decks have locked. Not the cause; still
      open.
    - **Android: not built, and why.** On a phone the node runs inside
      fuzzypickles' own app, and Android keeps apps' local sockets apart
      (SELinux puts each app in its own category; that it refuses one
      app connecting to another's socket is expected, not measured
      here). So snurra's phone app reaching the estate would need a
      node of its own, joined to the estate as one more host -- fuzznet
      linked into the APK and a pairing in the app -- where
      fuzzypickles' phone app already embeds snurra and is the estate's
      player there today. The estate is built for the desktop player;
      on the phone the build compiles the client and finds no node,
      so it lists nothing. Whether a phone should have both is the
      holder's.

- ~~Open, the holder's (2026-11-20): an About window, and where.~~
  **Settled by the holder 2026-11-23: in the right-click menu**, as
  guessed (XMMS and gmplayer put theirs there; snurra has no menu bar).
  "About snurra" is the window's menu's last entry, after a separator:
  the version and the copyright line `--version` prints, the address
  escaped for the box's rich text. The player sets it
  (`snurra_set_about`); a host's window, which has its own About, shows
  none; a window following the decks has no menu at all
  (`about_is_in_the_right_click_menu`).
- ~~Open (2026-11-20): the theme onto qtkit.~~ **Settled by the holder
  2026-11-23** ("qtkit should be used"): the player follows the desktop
  with `qtkit_follow_desktop(app, "SNURRA_THEME")` -- the portal where Qt
  DBus is linked, `kdeglobals` and `lxqt.conf` session first, abstaining
  on identical colours, and following a change, none of which snurra's
  own copy did. **A submodule at `qtkit/`**, at the tree's root as
  every vendored sibling is (harmonization.md, the holder 2026-11-23;
  first at `vendor/qtkit`, as qtkit's instructions then said, and before
  qtkit went public a byte-for-byte copy there), pinned at e6325af --
  the same `qtkit.pri`, `include/` and `src/` as ed18c25 -- fetched by the Makefile
  as fuzznet is -- when absent, the pinned commit only, a build that
  cannot fetch it stopping with the reason -- included by `snurra.pro`
  after `snurra.pri` (which links Qt DBus) and named in the Makefile's
  `check-mk` build; the style gate leaves the submodule to its own tree.
  Moving the pin is this tree's act, as for fuzznet. snurra's own detection and dark palette
  are gone from the library (no host called them); what stays is
  `snurra_mute`, in `palette.cpp` -- `theme.cpp` beside qtkit's was one
  `theme.o` to qmake. Tested on the screen in Xvfb
  (`a_dark_tde_desktop_gets_a_dark_player`: a TDE session whose
  kdeglobals is dark gives a window over 60% dark, `SNURRA_THEME=light`
  one under 40%; without the call, 1%).
- **One copy of each dependency, at the leaf** (the holder 2026-11-24,
  claude-guidelines' harmonization.md; snurra step 2 of 8, after
  fuzznet's 24071d77, inbox 20261124T0220Z). snurra keeps its
  submodules -- `fuzznet/`, `qtkit/`, and since this step `monocypher/`
  -- so it builds from its own clone, but **a consumer building snurra's
  player or tests names its own copies instead**:

      make FUZZNET_DIR=... QTKIT_DIR=... MONOCYPHER_DIR=...

  each defaulting to this tree's submodule. A directory given is never
  fetched into; absent, the build stops naming the variable. qmake
  takes `FUZZNET_DIR` and `QTKIT_DIR` on its command line (the Makefile
  passes them, written into the stamp that reruns qmake, so a changed
  directory is picked up); `check-mk` and the tests' `fuzznetd` read
  them too. **The tests' node no longer fetches fuzznet's nested
  Monocypher**: snurra vendors Monocypher at its root, at fuzznet's own
  pin (ab2b16d), and passes it in as `MONOCYPHER_DIR`, with `FLOG_DIR=`
  empty (fuzznet's way of building without its log). The library
  proper, `snurra.pri` and `snurra.mk`, names neither fuzznet nor qtkit,
  so a host linking only the library is untouched. fuzznet's pin moved
  to 6dc8c71 for its variables (184 commits; snurra's own sources from
  it built and passed unchanged).

- ~~Headphone pre-listen.~~ **Settled by the holder 2026-11-20**: a plug
  moved between taps, many listeners at once (sec 3f).

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
  controls in, as a launch's later files. **Dropped on a tile, a file
  replaces it** (the holder, 2026-11-13: "I thought it should replace
  the existing one"): only a strip along the right and bottom edges, a
  fifth of the window deep (at least two lines), adds; elsewhere on a
  tile the drop is that tile's, outlined as the drag moves. The new one
  keeps the place, level, controls and lane begin, plays if the old one
  did, from its start, at tempo 1 with no envelope or nudge; any further
  files go after the train. **A tile let go on another of its window
  trades places with it** (the holder, same day): the title's release on
  X11, its drag and drop on Wayland, the other tile outlined as it
  passes; the lanes' begins stay with the places, so a late lane starts
  where it did with whatever now heads it. Moving a whole joined window
  is still the bare frame between tiles (the holder: "not sure how yet";
  to be refined). Since 2026-11-13 (`snurra_dropped`):
  a folder is the audio and video under it, by the name's type (a cover,
  a cue sheet or a playlist makes no tile), sorted as people count (2 before 10),
  at most 64, with no more than 4096 files looked at so a drop of a home
  is not a walk of the disk; a link from a browser is its URL. Dropped on
  the full-screen picture -- a window of its own, all of the player
  there is to see -- they go on the train. An embedded tile takes no
  drops: where a host's files come from is the host's.
- **One bar for a window, tiles dragged by their waveforms** (the holder,
  2026-11-21: "Each player only has one control panel and once a tile is
  connected to it, it becomes a part of that player, with a single top
  bar with all the close minimize buttons etc, we move/drag/drop tiles by
  dragging the waveform tile itself"). A window has one bar on top: what
  it plays -- the tile last touched, and how many tracks -- the controls'
  handle, "decks" (sec 3f), minimise and close (every tile, so the
  window). Its tiles show no title row of their own (the tile still has
  one, shown where it is embedded alone, a deck's or a host's): each is
  named small in its waveform's corner. Held, the bar moves the window --
  a lone tile's as that tile, so it snaps as a title did (sec 3d), a
  group's by the window system where it can. A tile is dragged by its
  waveform: pulled out, onto another window, onto a deck view, traded
  with another; so a click on a waveform seeks when let go, not when
  pressed, a press being maybe a drag's start (a Ctrl drag still moves
  the lane along the timeline). This settles "moving a whole joined
  window" above: by its bar. Tested in `a_window_has_one_bar_for_its_tiles`
  (the tiles' rows hidden -- shown, it fails -- the bar naming the tile
  touched, a press not seeking and its release seeking, the handle and
  close) and in Xvfb, a tile of a group pulled out by its waveform
  (`a_tile_dragged_onto_another_window_joins_it`). A tile's own close went
  with its row, so a right-click on a tile offers "Remove" it -- that one
  track -- and, in a group, "Into a window of its own", beside the
  window (`a_tracks_menu_removes_it_from_its_window`; without the entry it
  fails).
- **Above or below, a lane of its own, shown before the drop** (the
  holder, 2026-11-21: "we allow them to be placed above or below in
  separate tracks, the preview should show what is going to happen
  before the drop"). A tile dragged by its waveform over the top or
  bottom third of a tile of its window lands in a new lane above or
  below that tile's, the lanes from there moving down; under the last
  lane, a new last one; over another's middle the two trade places as
  before. While held, a faint copy of it shows where it would sound --
  across where it is now, at the same moment -- on a line between the
  lanes it would come between; let go, it is there, its lane beginning
  when it came in, and the train it left closing up as when a tile goes.
  A tile alone in its lane gains nothing by a lane beside it. Tested in
  `a_tile_dragged_below_another_is_a_lane_of_its_own` (the copy shown
  and nothing moved while held; let go, a lane of its own, coming in
  where the train had it -- never moved, it fails), and looked at in
  Xvfb. The lane's late begin ("+2:55") is said at the waveform's right
  where its name has the left.
- **Beats and bars along the top** (the holder, 2026-11-21: "We should
  probably also put a timeline at the top, below the top bar with
  beats/bars"): under the bar and over the lanes, a ruler on the lanes'
  own scale -- each tile's span of the timeline where the tile is, so it
  reads true however lay_out_timeline spread them. From the first
  track's beat (its grid at its tempo, from where it comes in, less its
  offset): a tick a beat, a longer one and its number each bar of four,
  bars only where beats would crowd; with no beat known, seconds, said
  as m:ss where there is room. A mark where the window is while it
  plays. It asks what to draw at each paint, so it is never stale, and
  is part of the drawers look's shape (left out, it was cut away and
  looked empty -- seen in Xvfb). Tested in
  `a_ruler_marks_the_bars_on_the_lanes_scale`: a 4 s track at 120 BPM,
  its bars half its width apart, the first where it begins.
- **One picture to a window, mixed through a crossfade** (the holder,
  2026-11-21: "maybe video still goes on top of top bar or below it,
  only one video window, that for now mixes the two videos with an effect
  if they are in transition"). A window shows one tile's picture, on top
  over its bar, the window's width; every other tile's is closed. The
  picture follows what is heard: the first in the timeline that plays and
  has one -- through a train's crossfade the one going out, with the one
  coming in dissolved over it as far as the crossfade has gone (0 where
  it comes in, 1 where the first ends), then, the first ended, the one
  come in. Open or closed as the window last had it, by the bar's
  "video"; the tiles' own picture buttons are the bar's in a window. A
  dissolve is the first effect; others can follow it. Tested in
  `a_window_shows_one_picture_mixed_through_a_crossfade` (two films in a
  train: the first's picture over the bar, the second dissolved in as
  far as 1.00, never two drawers open, then the second's; with no mixing
  it fails) and looked at in Xvfb mid-crossfade. Open as the window last
  had it only where the picture it showed was one heard: the next film's
  picture, found before the first's and closed as a tile with no controls
  opens, once made the first open closed (one run in six). Lanes of a stack
  playing at once show the first lane's, unmixed: a mix is for a
  transition.
- **Started with no file: the music library** (the holder, 2026-11-13:
  started without a file, snurra said it could not play "untitled" and
  played three minutes of silence anyway). It opens a library instead,
  as a DJ program's: the audio and video under the desktop's music folder
  (XDG), or under one chosen with its button and then remembered
  (`library/folder` in `~/.config/snurra/snurra.conf`, only when
  chosen), listed by name and folder in path order as people count,
  narrowed by every word typed. Double-click or Enter plays: a window of
  one tile or none plays it in its place at once, a player's way; one of
  several -- a mix -- takes it at the end of its first lane, paused (an
  assumption: a double-click must not throw a mix away). Dragged, a tune
  carries snurra's own type (`application/x-snurra-files`), not a URI
  list, so a desktop's file manager cannot take it as a copy of the
  file; a snurra window takes it as any drop, and let go over nothing it
  opens as a tile of its own where it was let go. The process goes once
  the library and the window are both closed. Listed on a thread of its
  own, at most 50000 tunes of 200000 files looked at. A folder chosen
  while another is still listed shows only its own: what the stopped
  scan had already handed over, still queued, is let fall by the scan's
  number (found 2026-11-21 under a load of 25, a DJ browser of three
  tunes showing five; `a_folder_chosen_while_another_lists_shows_only_its_own`,
  5 for 3 without the number). **Names, if ever
  tidied for display** (the holder, 2026-11-13): a YouTube ID in a name
  stays -- "if we ever do this kind of cleanup we do not remove youtube
  IDs" -- and no file is renamed. **Fingerprints** (the holder,
  2026-11-14: "snurra should do fingerprinting as well";
  `fingerprint.h`): Chromaprint's, as AcoustID, Picard and beets use --
  made through FFmpeg's own chromaprint muxer, which FFmpeg already links,
  so snurra links nothing new (an FFmpeg without it, as a phone's kit may
  be, gives none). The first 120 s, raw for comparing here and in base64
  as AcoustID takes it, one reading for both. Measured on made tunes:
  snurra's equals the ffmpeg program's own (0.000 of bits apart), an MP3
  of the tune 0.000, another tune 0.412, as chance gives
  (`a_fingerprint_knows_a_tune_however_it_is_encoded`; byte-swapped
  values fail it). **Asking AcoustID** (`snurra_identify`): the
  fingerprint and the tune's whole length, nothing else, sent only when
  asked, to its v2 lookup; the best-scoring recording taken, artists
  joined, its MusicBrainz id kept; AcoustID's refusals in its words.
  Tested against a server of the test's own standing in for it
  (`acoustid_is_asked_and_its_answer_read`; taking the first result in
  place of the best fails it). The client key is registered to a person
  -- the holder's to make; `$SNURRA_ACOUSTID_KEY` or the setting
  `acoustid/key` gives it, and without one nothing is sent. **Identify**, a button in the
  library and the DJ player's browser: the chosen tunes fingerprinted one
  after another on a thread, AcoustID asked for each, and its name shown
  as a tag's is -- Name and Artist, no file renamed -- and kept beside
  the beat grids (`beats.ini`'s identity group, by the file's size and
  time, a lasting name by itself), so the next listing shows it asking
  nothing. A YouTube ID the file's name carries stays beside the title
  shown, "Night Drive [FYLJdKxTxJ0]" (`snurra_shown_title`, yt-dlp's
  "-ID" and " [ID]"), for tags' titles too (the holder, 2026-11-13).
  Tested end to end against the stand-in
  (`the_library_identifies_a_tune_and_keeps_its_id`; not reading what was
  kept fails it). The button is shown only where fingerprints can be
  made: the Android kit's FFmpeg (6.12.0, both ABIs) has no chromaprint
  muxer, so a phone has no Identify. The suite's settings are its own since: it had read
  and written the person's `~/.config/snurra`. **Not built: finding the
  library's tunes inside a DJ set** by fingerprint, measured first
  (2026-11-14): a real tune (the holder's "Impulse") sped up by 1 % came
  out 0.247 of bits from itself, 2 % 0.342, 3 % 0.390, 6 % 0.413 --
  chance is about 0.41 -- since Chromaprint compares pitch bands and a
  DJ's tempo moves them. Doing it would take the set fingerprinted again
  at some 25 trial speeds (25 times the decoding) and an index over the
  library's whole tunes, for what a saved mix's tracklist (hydra's)
  already gives; left until a set without one needs it. **A file's own tags are shown**
  (2026-11-13): Name is its title where it has one, beside an Artist
  column -- ID3, Vorbis comments, MP4 and Matroska tags through FFmpeg,
  the container's or else the first stream's (Ogg keeps them there) --
  read on the listing's thread after the list is out, so a large folder
  lists at once and its names come in after; search covers name, artist
  and folder. An untagged file keeps its own name, ID and all; nothing
  is renamed (`the_library_shows_a_files_own_title_and_artist`). **Beat
  grids are kept** (`beats.ini` in the user's cache, ~/.cache/snurra, or
  $SNURRA_CACHE -- the suite sets its own, so a run never touches the
  person's): by path and sound track, good while the file's size and time
  are the same, "no beat" kept too, given at any rate; and only while
  the analysis that kept it is this one's -- an entry carries the
  analysis's number, raised whenever it would answer differently, so
  the dotted check reaches a track kept at 88 (an older number is found
  again, which the test shows by ageing a kept entry). A deck loaded a
  second time has its grid at once (a test kick: 196 ms found, 1 ms
  kept; the 42 minute set took 27 s), and the library's BPM column shows
  what decks have found -- nothing is analysed for the list
  (`a_beat_found_once_is_kept_and_shown`). **Waveforms are kept too**
  (2026-11-24), each a file of its own under the cache's `peaks/`, one a
  resolution -- the file's stamp and column count, then the floats, 4 KB
  a track for the overview's 1024 -- good
  while the file's size and time are the same, kept only when read
  whole. Measured on the holder's own use, a three hour `.webm` DJ set
  opened bare: the first open costs 318 s of CPU over three minutes,
  two cores (the waveform's read, decoding the whole of the sound -- 91
  s for FFmpeg alone -- and the beat analysis, decoding it again); a
  second open had cost the waveform's 100 s again, and now costs 17.8 s
  in its first minute, the picture and sound alone
  (`a_files_waveform_is_kept_until_it_changes`; with the kept one never
  read, it fails). Not yet: one decode shared by the first open's two
  readers. **Find tempos**, a button,
  works out the rest -- every tune listed with no BPM kept, one after
  another on a thread of its own, kept as it goes; asked for, never by
  itself, since it holds a core a while and a phone's battery (Traktor
  analyses on import; snurra waits to be asked). A new folder or the
  library closed stops it (`the_library_finds_tempos_only_when_asked`).
  A header clicked sorts by its column, as a DJ's browser does: Name and
  Artist by their words, BPM by number with tunes that have none last
  whichever way, Folder -- the first order -- by path; ties by path.
  **The key** (2026-11-13, sec 3e's "not yet"; Traktor shows it, for
  mixing in key): found in the same reading as the beat, so Find tempos
  finds both and `beats.ini` keeps both (an entry kept before keys is
  found again). A chromagram -- 8192-frame blocks end to end, each bin
  from G2 to C7 to its nearest note, a block weighed by its own loudest
  note so a loud chorus does not outvote a quiet verse -- correlated
  with Krumhansl and Kessler's major and minor profiles at all 24
  rotations, snurra's own code like the beat's; none under a fit of 0.45
  (0.5 at first; see below).
  Read at a quarter the rate (a low pass at 3 kHz, every fourth frame
  kept): whole, it doubled an analysis's time, five one-minute signals
  3.0-4.0 s without it and 6.3-7.0 s with it; quartered, 5.0 s.
  Shown as the Camelot wheel's number and letter, then the key's name
  ("8A Am"), in a Key column sorted round the wheel so keys that mix sit
  together, and on a tile beside its tempo with its BPM, both as heard:
  the BPM at the tile's tempo, the key moved by 12 log2 of it, rounded,
  unless key lock holds it (`a_tile_says_its_beat_and_key_as_heard`).
  Measured on chords made in a key, a minute of I IV V I or i iv v i:
  C major, A minor (the same notes as C major), Gb major over a kick and
  Eb minor all found, fit 0.905 to 0.974; white noise and a bare kick
  none, 0.27 and 0.33 (`a_key_is_found`, `the_library_shows_each_tunes_key`).
  The chromagram began at C2 at first, and a bare kick read as Db minor:
  its 55 Hz leaks into C2's bins, and a block of kick alone, weighed by
  its loudest note, voted for it in full. Minor keys given the major
  profile fail the test. A lone held pitch is given its major key -- a
  440 Hz tone fits A major at 0.68.
  **On real music, against Mixxx** (2026-11-13): the 48 of the holder's
  tracks Mixxx has analysed for key (its library read, read-only; the
  files on the holder's NAS), through `snurra_find_beats`. Mixxx is a
  second detector, not the truth, so these are agreements. Tempo: of
  the 40 Mixxx gave one, 33 within 0.5 BPM, and 5 at two thirds of it
  -- 132 as 88 twice, 138 as 92 twice, 136 as 90.8: a pulse every beat
  and a half taken for the beat. Hence the dotted check in `grid_of`:
  folded in the body at two thirds of the period, a true beat has a
  kick on its line every cycle, where at the period the kicks fall on
  three lines in turn, so the stronger a cycle is the beat; all five
  then agree and no other track moved, 38 of 40
  (`a_dotted_rhythm_is_not_taken_for_the_beat`, an open hat every beat
  and a half over a kick at 132, read 88.00 before it). The two left,
  Mixxx 71.3 and 90 against snurra 95 and 120, are four to three and
  undecided. Key: 6 the same, 5 its relative, 7 a step round the wheel
  -- 18 of the 36 given, half, mix with Mixxx's -- 8 the same tonic in
  the other mode, 10 other, and 12 given none, the fit on real music
  lower (median 0.647, least 0.266) than on made chords. So the key is
  weaker than the tempo, the other mode the likeliest miss. The fit did
  not foretell agreement: the 12 under 0.5, given their best key all
  the same, agreed with Mixxx as often as the 36 over it (6 of 12, 18
  of 36). So the bar is 0.45: what has no key must stay under it, and
  the measured ones do with 0.05 to spare -- noise 0.27, a kick with a
  click 0.33, a bare 55 Hz kick 0.40 (a pitched note, an A, which is why
  a bar can only be a margin here) -- and 43 of the 48 have a key. 0.38
  was tried first and gave the bare kick one. Better profiles
  for electronic music (Faraldo's) are the next thing to try, measured
  the same way.
- **Dark where the desktop is** (the holder, 2026-11-13: the players
  are light on a dark TDE desktop that has no light/dark status;
  now qtkit's `qtkit_follow_desktop`, `qtkit/`). Asked in this order, the
  first that answers: `$SNURRA_THEME`; Qt's own colour scheme; the file
  a desktop with no status writes -- TDE's and KDE 3's `kdeglobals`
  ([General] windowBackground against windowForeground, `0,42,78` and
  `220,220,220` here), LXQt's `lxqt.conf` ([Palette] window_color
  against window_text_color), the running session's first; then the
  palette Qt was given. Not the sources' rank: the palette always
  answers, light where Qt is not integrated, so asked earlier it would
  hide the file. The scheme's name is never read (it says nothing in
  most), and the desktop's hue is not taken: dark means snurra's own dark
  scheme, in Fusion, its accents chosen against its own ground (since
  2026-11-23 qtkit's, sec 3: the same order, the portal added). Unknown
  leaves Qt's default -- a wrong dark is unreadable, a wrong light only
  plain. The player only; a host embedding the library keeps its own --
  so the tile draws from roles every palette gives the same sense: what
  of the waveform is still to come is two fifths of the way from the
  window to its text, not `Mid`, which snurra's dark made lighter than
  the ground and a host's (hydra's, qtkit's) makes darker, where it all
  but vanished (2026-11-23,
  `the_waveform_to_come_shows_on_a_hosts_dark_palette`; in `Mid` it
  fails).
  Not done: the freedesktop portal (Qt DBus is not linked; TDE has none),
  and Plasma 5/6's own kdeglobals keys, not readable on this machine.
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
  group is next played or sought. **A lane may start later than the
  timeline** (the holder, 2026-11-13: "lanes may start later, snap drags
  to beats"): its begin is kept by its first tile (`lane_start_us`,
  carried by a handover) and said on that tile's waveform as `+m:ss.s`.
  It is set by dragging any of the lane's waveforms with Ctrl -- Ctrl,
  since it is already the key that moves a track in time (the nudge),
  and the waveform, since that is the track to move -- never before the
  timeline's start. Played from before it, the lane waits, its first
  tile started for the moment on the shared clock that its begin
  names (a start to come, as a train's next tile's); paused, it holds at
  its start. Released while the group plays, the group is started
  again where it was, so the move is heard at once -- a gap of the
  output's latency, as a seek. A lane's first tile closed or pulled
  out leaves its begin to the next, so the lane keeps it and its train
  closes up behind, as one beginning at zero does. Not yet: a tile put
  in front of a lane's first (no gesture does that today) would take
  over with no begin of its own.
- **Beat-snapping:** a train's overlap snaps to the two grids (step 6),
  and a lane dragged later snaps so its first beat, at its tempo and
  offset, lands on the nearest beat of the tile another lane plays at
  that moment -- the first lane, in row order, with a beat there; by
  less than half a beat, and not at all where either has no grid. Shift
  held while dragging, it goes where it is put -- the one modifier left,
  Ctrl being the drag's own (an assumption, 2026-11-13: not checked
  against an editor's convention). **A train's later tile, Ctrl-dragged,
  moves where it comes in** (the holder, 2026-11-18: "they need to snap
  to beats when connecting"): later is a shorter crossing of the tile
  before, earlier a longer, its first beat moved onto the nearest beat of
  the one before (`snurra_overlap_on_beat`, by a beat, not bars: the hand
  chose the bar), Shift where it is put. Kept on the tile as
  `overlap_us`, carried by a handover; the train and every seek count
  with it. A lane's first tile, dragged so, still moves the lane.
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
  A train's crossfade stays the deck's equal-power fade (the holder,
  2026-11-13: "keep equal-power, show the crossfade as curves"), drawn
  on its two tiles: the quarter circle the deck plays, falling over the
  one's last overlap and rising over the next one's first, what it takes
  away shaded -- the same overlap the train plays, refreshed with the
  trains' tick. Shown, not edited: the user's envelope is the curve to
  edit, and multiplies with it.

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
alone); no grid when the fold's peak is under 6 times its mean. **A
DJ set** (2026-11-13: the holder's 42 minute set got no grid -- the
whole folded to 3.7, each of its minutes to 7.6 to 10.9, all 129 BPM,
its phase moving at each join): a track of 8 minutes or more whose whole
is not clear is judged a minute at a time at up to twelve places, and
where half of them, three at least, agree within 1 % the grid is their
middle tempo in the phase of the first -- Mixxx's constant grid for a
set, right in tempo throughout and in phase from that minute until the
next join moves it. The set now reads 129.01 BPM (confidence 10.1); a
made ten minute set at 128 whose beat moves 0.37 at each minute reads
128 on its first minute's beats, and gets none without the windows
(whole fold 5.7) (`a_set_whose_phase_moves_has_its_tempo`). Not yet: a
grid that follows the set's phase from track to track. **Tried and not
kept, 2026-11-23**, on a real set (a three hour festival livestream,
25 minutes of it from 30:00, which folds clear as a whole at 132.97 BPM,
confidence 6.9). Measured independently -- spectral flux at 11 kHz,
each 30 s window's phase as the circular mean of its fold at that tempo
-- the grid sits on the beat for some ten minutes and then wanders up to
200 ms, near half a beat; but mostly by **drifting through a track**
(240 to 370 ms over four minutes), not by stepping at the joins, so
"one tempo, the phase moving at each join" describes this set badly.
What was tried: the set read again at its tempo 15 s at a time, the
phase changing where two windows running agreed on one a tenth of a beat
from the one in force, the join placed to 5 s, the changes kept with the
grid (`phases`, `at(frame)`). It found ten changes in the 25 minutes,
where the measurement saw movement, but each 15 s fold's peak flipped
between the beat and the off-beat (about 225 ms apart: hats and claps
fold as well as kicks), and the grid came out no nearer the measured
beats (spread 93 ms rms against 85). What it would take instead: a beat
tracker following the tempo as it moves -- a local tempo, not only a
phase -- on the low band (`body`), which a kick dominates; and the same
independent measurement to judge it by. Measuring runs the player headless
with `SNURRA_CACHE` and reads what it keeps; its runtime folder must be
short, since a socket's path takes 107 bytes at most.
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

**Loudness is snurra's own too** (2026-11-14, a gap: Mixxx brings every
tune to one level with ReplayGain, on from the start, and snurra played
each at whatever level it was mastered to, so a mix's next tune could
come in several dB off the one playing). ITU-R BS.1770-4 integrated
loudness, gated as EBU R128 says, measured in the beat reading's own pass
and kept beside the grid (analysis 5) with the highest sample:
`snurra_beat_grid::loudness`, `peak`. The K-weighting's two biquads are
designed at the track's rate from their analogue prototypes. Held to EBU
Tech 3341's cases 1 to 4 within 0.1 LU, at 48 and 44.1 kHz; failing with
the relative gate taken out (-24.2 for -23) or the shelf (-23.7). On 40
of the holder's tracks Mixxx holds a ReplayGain for (a ratio against its
-18 LUFS), 9 agree within 0.05 LU and the median difference is 0.00;
the rest differ by 0.6 to 3.2 either way, and those figures are not
Mixxx's measurement but the files' own REPLAYGAIN_TRACK_GAIN tags, which
it took as found -- written by older ReplayGain 1 tools, whose loudness
model is not R128's (`12 One Take.flac`: tag -7.65 dB, so -10.35 by
Mixxx's reckoning; -7.63 here). So a tag is not read: the measurement is
the one a tag would have to agree with. A tile

that learns its tune's loudness sets a trim on its deck,
`snurra_loudness_gain`: to **-14 LUFS**, where YouTube and Spotify bring
theirs -- most of what is played here moves by little -- cut as far as
it takes, boosted only as far as the highest sample has room and 12 dB
at most. The trim is under the level slider and moves at one unit of gain
a second, so one that arrives mid-play (a tune not yet analysed) is a
half-second fade, never a step; before the first sound it is there at
once. **On unless the setting `loudness/match` is false**, and at -14
LUFS: settled by the holder 2026-11-18 ("keep loudness matching on, -14
is fine"), though it plays a loud master quieter than mplayer did.

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

**Step 1 built 2026-11-13.** A `.stem.mp4` or `.stem.m4a`, launched or
dropped, is four lanes -- drums, bass, other, vocals, Native
Instruments' order, the mix (track 0) left out -- each from the start of
the timeline and below what is there, since a lane cannot yet start
later than the timeline (sec 3b, the holder's). Known by the name, as
the format's own programs know it. The tile state carries
`audio_track`, so a deck opens on its stem before a frame is read, a
handover keeps it, and the waveform and the beats are read from the
stem, not the mix. The title says which stem. **The file's own names,
2026-11-23:** NI's tools write JSON in `moov/udta/stem` (`{"version": 1,
"stems": [{"name", "color"}, ...]}`, one per track after the mix), which
FFmpeg does not expose, so snurra walks the boxes itself, as Mixxx's
`StemInfoImporter` does (read 2026-11-23, main branch): plain boxes, no
version and flags, trailing NULs dropped. Unlike Mixxx's walk it takes a
64-bit size and a box running to the end, and a size leaving its parent
ends the walk; the manifest is read only up to 64 KiB, and at most eight
stems are opened. A missing name keeps that place's usual one, and a file
without a manifest keeps drums, bass, other, vocals. The colours are not
used yet (snurra's lanes have none). Still by the name `.stem.mp4`, so a
plain `.mp4` is never opened to look.

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
- **One panel to a window** (the holder, 2026-11-21: "Each player only
  has one control panel and once a tile is connected to it, it becomes a
  part of that player"): in a window the controls are out on one tile at
  most, shown under every lane and as wide as the lanes -- not a drawer
  under that tile's own span. A press on another tile moves them there;
  tiles joined with their controls out keep them out on one, the tile
  last touched, else the first. The bar's handle pulls them out and in
  (sec 3b), and pushed in, the group stops, as before. A tile pulled out
  into a window of its own carries its controls as it had them. Tested in
  `a_drawer_adds_room_and_leaves_every_waveform_as_it_was` (under the
  last lane, the width of the lanes, the lanes left where they were;
  touched, another tile takes them, never two -- with two allowed, it
  fails).

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

## 3f. Decks and the train: one timeline, two views, nested {#decks-and-train}

Status:  settled 2026-11-20
Decides: decks-and-train

**The holder, 2026-11-20:** "just like other software we want the 2/4
deck style setup too. It will be used on mobile as an optimized cut down
version, but on desktop it should be controllable with hardware too, and
we should have a realtime connection of some sort between the
(attachable) player and deck mode so they can be switched between and/or
combined effortlessly." Then: "Maybe the player view behaves like
automation for the deck view. So the envelope is a crossfader automation
etc. and the next track is loaded on the other side. Or each channel is
its own player and can have an already prepared mix in it? Both
possible?" -- and, both being possible: "let us make it so that the deck
view is in a symbiosis with a single player train and that a single deck
can also be the output endpoint for a player train/deck. Very trippy and
recursive."

**One thing underneath: a timeline.** A set is tracks -- each with its
start, tempo, key, EQ, envelope, cue, offset -- which a window's group
already is (sec 3b). It was computed by the window from its widgets
(`snurra_node::lane_start`, `lay_out_timeline`); it is data of its own,
`snurra_timeline`, which the window shows and the decks play.
Everything below is that one model seen two ways, or one inside another.

**Channels, not ABAB** (the holder, 2026-11-20: "I feel like the ABABAB
thing is going to be in the way ... we accept layout inversions half of
the time" -- or the next track goes "onto a separate channel, on a lower
vertical level (with reusable but non-overlapping tracks on each
channel) each player has its own timeline and automation until they are
connected in a train"; then, of connecting two trains: "that will force
a modification on the track ordering of the second one, as they might
start overlapping").

- **A channel is a deck is a lane**, for good: channel 2 is deck 2 in
  the deck view and the second lane in the timeline, so nothing swaps
  sides. A channel never sounds two tracks at once; tracks on it may
  have gaps between them and come back.
- **A track is free or follows another.** Free, it comes in at its own
  position; connected after another -- on any channel -- it comes in
  where that one ends less their overlap. Tracks so connected are a
  train; a train is a connection, not a layout. A window's lane today is
  a train whose first track is free.
- **Channels are seated, not stored** (`snurra_timeline::seat`): in the
  order tracks come in, each takes its pin, else the channel it was on
  where free, else the lowest free -- so the channels used are as few as
  the most tracks ever sounding at once. A train alternates over two,
  since an overlap is never more than half of either track (track 1 and
  3 at most touch). Connecting two trains makes one; the only change is
  that the second's alternation may flip -- the reordering the holder
  foresaw, done whole and computed, so nothing overlaps. Trains at once
  take as many channels as sound together: the 4-deck view's use.
- **What is fixed while it moves:** the decks, which hardware binds
  (below); a track sounding, which keeps its channel -- seating counts
  from what plays, and a move it cannot avoid is a gapless handover; and
  a pin, kept where put, a collision shown to the person
  (`conflicts`), never moved behind their back.
- **The crossfader is never automated.** A crossfade between channels is
  their two faders' envelopes, which is what a train's crossfade already
  is (two envelopes); the crossfader is left to the hand, whichever side
  the music is on. That is what removes the inversion.

**Symbiosis: the deck view is a view of one train, live, both ways.**

- Each track plays on the deck its channel is: the next is loaded and
  cued on its deck while the one before plays.
- **The timeline's automation drives the decks**: a track's envelope is
  its channel fader; tempo, key lock, key and EQ go with the track to its
  deck. The crossfader stays the hand's (above).
- **A hand takes over**, as a DAW's touch mode: a control touched on
  screen or on a controller leaves the automation while held, and
  follows it again when let go.
- **Moves made on the decks are written back** into the train: a fader
  ridden becomes the tile's envelope, a crossfade done by hand becomes
  the overlap and its curve, a deck started becomes where its tile comes
  in. So a mix performed live is a prepared mix in the player view,
  editable and played again -- the one model, not a recording beside it.
- Switching views moves nothing: both show the one timeline, and both may
  be open at once. **A first step, 2026-11-21:** a player unfolded on the
  decks shows above them as its timeline -- a row a channel, so a row a
  deck; each track a bar from where it comes in to where it ends, named,
  filled where a deck sounds it; a line where the player is -- so what
  comes next, on which deck and when, is there while the decks play it.
  Gone when the player is folded or a tune loaded by hand ends it.
  Pressed, the player goes there, as a seek on a deck does -- to hear a
  crossing again or go on to the next. Otherwise read-only: an edit is
  still made on the decks (written back) or in a window. Tested in
  `the_decks_show_the_player_they_play` (three tracks on two channels,
  the playhead moving, pressed near the end the playhead there, gone when
  folded; never shown, or the press not seeking, it fails) and looked at
  in Xvfb. **Both open at once, 2026-11-22:** beside the strip, "In a
  window too" starts a window (`--follow <the decks' socket>`) showing
  the player the decks play: its tracks as tiles, laid out as in a
  window, silent -- the decks are what is heard -- and in step with them
  (started again only when more than a fifth of a second apart, so the
  waveforms, the ruler and the picture move with the decks). It asks the
  decks ten times a second (FOLLOW, FOLLOWING: the player's tracks as
  they stand, a deck's EQ, tempo and key as the hand has them, where it
  is, whether it plays) and shows what changed there field by field; the
  tracks themselves changed, it makes its tiles again. **And back**:
  what the hand changes in the window -- a track's EQ, ride, level,
  tempo or key, a crossing or a lane moved along -- goes to the decks
  with the next answer (CHANGE: each track as the window has it, then as
  it was told it, so the decks take only the fields that differ, and a
  deck turned there meanwhile keeps its own; the deck holding the track
  takes it at once, and a moved crossing has the decks go on from where
  they are on the new times). Adding or removing a track in the window
  is not handed back: the decks' next answer puts the tracks back --
  the decks hold the player, a track goes on or off there. So nothing is
  rearranged in a following window: no track menu, no "decks" on its
  bar, no tile dragged out, swapped or into a lane of its own, no file
  or tile dropped in (an OFFER goes unanswered and the giver plays on).
  A track taken from it into a window of its own would have been heard
  twice -- there and on the decks. Its play,
  pause and seek are the decks' (PLAY and SEEK with no tile index). Folded,
  ended, or the decks gone half a second, the window goes. Polled rather
  than pushed: the decks keep no list of who watches, and a window that
  died leaves nothing behind there. Tested in
  `a_window_follows_the_player_on_the_decks` (three tracks as three
  silent tiles playing the first; a deck's EQ on its tile, the window's
  on the deck with the deck's own kept; a crossing moved in the window
  in the decks' player; the window's seek into the last track playing it
  on the decks and in the window; folded, the window gone -- the decks
  ignoring the seek, the tiles heard, or CHANGE dropped, it fails).
- **The way there from a window** (the holder, 2026-11-21: "I don't know
  how to switch to deck mode"): until then only `--dj`, or a tile
  dragged over a deck view already open. A window's menu (right-click)
  has "Open in the decks": a new process opens the deck view, connects
  back and asks for the player by a token (ADOPT, as a tile pulled out
  is asked for), every tile onto its deck A, gapless; it unfolds what it
  took across the decks once the crossfade is over. The window stays,
  following the player it gave (the holder, 2026-11-22: "leave the
  window open as a follower"), so the switch loses no view: READY names
  the decks' socket, and after COMMIT the window follows it as "In a
  window too" does. Empty until the decks have unfolded, it waits up to
  five seconds of answers with no tracks before taking one as folded,
  and the last fade heard out no longer closes it while it follows --
  so a following window emptied (closed by the hand, or the decks
  folded) stops following first, or it would hide and ask on unseen.
  Tested from a real right-click in Xvfb
  (`a_window_opens_its_player_in_the_decks`: the decks hold the two
  tracks, each on its own, and the window shows them as its two tiles;
  without the unfold, or the window not following, it fails). **And back**:
  each deck holding something has "Into a window", which opens a window
  of its own with it, as its title dragged out does -- where the deck is
  one of a player unfolded, the player gathered first (fold), so the
  window gets it whole, its tracks tiles again, not one torn out. A
  RELEASE over the wire does the same. Not on a phone, where the decks
  are the app and no window can be started. Tested in the same test, the round
  trip: the decks' player back in a window as two tiles, the decks empty,
  the window that followed them gone (released without gathering, one
  track comes out and it fails).

**Nested: a deck plays a player.** The holder, the same day: "a deck's
input can be a whole train or another deck view's mix -- because they
are the same thing, it is the player's output = all decks' output." A
train and a deck view are not two things a deck can take but one, a
player, whose output is the sum of all its decks.

- A deck's source is a file or a player's output -- a channel with a
  prepared set in it, under the live decks. A single track is the
  shortest timeline, so a deck needs no third kind.
- A deck's controls act on what it holds as a whole: the waveform is the
  whole mix, tempo and key lock stretch it, cue and jumps move in it,
  sync follows the beat of whichever track sounds at that moment.
- **Rendered in the source's own time**, as a source (`snurra_source`):
  the timeline mixes its tiles -- each its own source, tempo and
  envelope -- on the deck's reader thread, and the deck above applies its
  own tempo, EQ and fader to the result. So a timeline inside a timeline
  costs nothing new, and recursion is a tree: a timeline is moved into a
  deck, never referenced from two places, so it cannot contain itself.
- **Across processes, states travel, not sound** (sec 3b's rule): a
  player handed to a deck goes over as its tile states, a tree now, and
  the taker builds it -- the handover's OFFER, READY and
  COMMIT as for a tile, so a set playing moves into a deck without a gap.
- **What a nested level costs, said plainly:** a deck reads ahead of what
  is heard, so a change made inside a nested timeline -- a hand on the
  inner deck view -- is heard a read-ahead late. Live hands belong on the
  outermost level; nested levels play prepared. Measured when built.

**Pre-listen is a plug, not a channel button** (the holder, 2026-11-20:
"headphone prelistening is supposed to be more about running around
with a plug and plugging yourself into different points. So you click
on some 'pre-listen here' button (tiny which are everywhere relevant)
and then your prelistening jumps to there ... in this way multiple
pre-listeners can work on the same mix too.")

- **Every point in the tree is a tap**: a tile's track, a deck, a
  player's sum, a nested player inside a deck -- wherever a "listen
  here" makes sense, a tiny one, the plug's socket.
- **A listener is an output with a plug in one tap at a time**: clicking
  a socket moves that listener's plug there, and its headphones hear
  that point and nothing else, while the room hears the master.
- **Listeners are many**: each its own output -- a second ALSA device, a
  controller's second pair, and later another process or device -- each
  plugged in its own place, so two people prepare on one mix at once.
  Which listener a click moves is the one the clicking hand belongs to:
  on one screen, the chosen listener; on a controller, the listener it
  is bound to.
- A tap hears its point as the master would hear it, before the room's
  fader and crossfader where it is a deck's -- what a DJ checks on cue.

**The holder's trial, 2026-11-24 -- open** (the first on a real screen):
"there's a lot of work to do on the layout but we are getting there, the
decks need to look more like modern DJ software and the idea of decks
being a player and vice versa isn't being followed. The switch back and
forth or simultaneous view of both isn't being fully followed. It is
likely we'll need several layouts in the end for different needs.
waveforms horizontally side by side or on top of each other or vertical,
especially with 4ch". So three things are open: **(1)** the deck view's
look, toward modern DJ software (Traktor, rekordbox, Serato, Mixxx);
**(2)** the symbiosis above as the person meets it -- moving between the
window and the decks, and both shown at once on one timeline, kept in
step; **(3)** several deck layouts for different needs, the waveforms
side by side, stacked one above another, or vertical, above all with
four channels. Layouts are slots arranged (below), so (3) is more
arrangements of the same slots, not other code. **Asked the same day:**
the look closest to **Traktor** -- zoomed waveforms stacked full width
across the top, decks with large BPM, key and time, a mixer of channel
strips between them; **the look first**, the layouts coming out of it
(the waveforms a row of slots of their own); and "both at once" means
**two linked windows** -- the window and the deck view as today, but
fully in step: an edit in either shows in the other, either can play
the set. **Step 1 built, 2026-11-24: the waveforms stacked above the
decks** (`snurra_deck_waves`): a row a deck, full width, each its track
read at a hundred columns a second and scrolling past a playhead fixed at
the middle -- eight seconds across, the wheel zooming from two to sixty --
with its beats and, taller, its bars, in the deck's own colour (A blue,
B orange, C green, D violet); what is heard dims behind the playhead. A
row has 48 px at least, so the decks below never squeeze it out. A
player on a deck has no one file and keeps its overview for now. The
fine peaks are kept as the overview's are, a file a resolution
(`-<columns>` on the name), so the two never overwrite each other.
Tested in `the_decks_waveforms_scroll_stacked_above_them` (a 4 s WAV
read as 400 columns and kept so, its row following the deck as it
plays; asked at the overview's 1024 instead, it fails). **Step 2 built,
the same day: a head on each deck** (`snurra_deck_head`), as Traktor's --
the deck's letter in its waveform's colour, the title and artist from
the file's tags (else its name), the tempo large as heard (the grid's BPM
at the deck's tempo), the key as heard, the tempo moved in per cent, the
time left at that tempo, and cue, play and sync, which are the tile's own
(`cue`, `set_playing`, `request_sync`). Read from the tile ten times a
second. The deck view opens at nine tenths of the screen's free room,
never under the 1000 x 700 it was, so the waveforms above no longer
squeeze the decks. The tile's own panel stays under the head until its
gain, EQ and fader move to the mixer (step 3). Tested in
`a_decks_head_shows_its_tempo_key_and_time_left` (120.0, A major, -0:04;
played, -0:03; at +10%, 132.0 -- the BPM left unscaled by the tempo, it
fails). **Step 3a built, the same day: the mixer between the decks**
(`snurra_mixer`), in its own column at the centre of whatever arrangement
the person chose, the decks right of centre one column over; its strips
face their decks, the left column's from the bottom up and then the
right's from the top down -- Traktor's C A B D for four. A strip: the
deck's letter in its colour, GAIN (-12 to +12 dB, by hand, on top of the
level match's trim -- `snurra_tile::set_gain_db`, which settles the open
"hand gain" in the holder's words: "for each channel on the mixer we also
have gain"), HI, MID and LOW knobs as a DJ mixer's -- 0 dB at twelve
o'clock, the left half down to -24 dB and the kill at its end, the right
up to +6 -- which are the tile's three EQ parts, a fader that is the
tile's level, and CUE, the chosen headphones' plug, pressable only while
there are headphones. Each control is the tile's own, read back ten times
a second, so a change made on the tile's panel, a controller or the
automation shows here. Where the view is too narrow for the strips and
two decks of 300 px -- a phone upright -- it hides, margins and all, and
the tiles keep the same controls (what the phone's view cuts is still the
holder's, sec 3f step 3). Tested in `the_mixer_strips_are_each_decks_own`
(the order, each control reaching its tile, the gain doubling the trim at
+6 dB, the read-back; with the gain kept off the trim, or the order left
as made, it fails). Not yet: FX inserts (snurra has no effects), the
player's EQ as its master EQ (step 3b), and the tile's panel taken off a
deck now that the mixer and the head hold its controls. Then the windows
(4) and the layouts (5). **Step 5a built, the same day: the waveforms'
layout** -- stacked rows full width (the default), side by side (a
column a deck, its playhead at the column's middle), or vertical (a lane
a deck, time running down it, what is to come below the playhead -- an
assumption; Serato's vertical view is the neighbour, and which way it
should run is the holder's to say). Chosen on the waveforms' right-click
menu and kept as `decks/waveforms`, "Default (stacked)" written as no
key at all. Each layout is one lane drawn the same way, a vertical lane
the horizontal one turned a quarter. Tested in
`the_waveforms_layout_is_kept_as_chosen` (chosen, kept, read back by a
new view, Default writing nothing; with the kept choice not read back,
it fails).

**The holder's second trial, 2026-11-25 -- open:** "Most of the things
look great, the fact you put the waveforms on top of the video was a
great idea. The waveforms don't look very good or readable maximized and
they still have bad performance, can't zoom out enough (we have
following tracks), and can't scratch like on other software. Do not use
standard knobs, they are broken, we decide on a set of widgets and their
config we use throughout the software, use the ones we fixed before. We
also need DJ-faders and more work on which ones should be large and
which ones small, which ones horizontal and which ones vertical. Oh and
we want pitch shifting speed by default, when speed is changed we leave
the pitch/tone unaltered by default, not needing to press the key button
first. No idea how to get back to player view, maybe we should not close
it down when decks are opened? only leave it in the background? There
are master EQs on each channel now, we need to decide where the master
section goes. Which is likely to duplicate a lot of what each channel
already has but better, maybe compressor/limiter, FX etc also... In the
player we mess around mostly with the master section, in the decks
mostly with the channels, but master section still needs to exist."

So, each its own piece: **(a)** one set of controls for the whole
program -- `snurra_knob` (held and moved up or down, 2026-11-18) for
every knob, never Qt's stock dial, and a DJ fader -- with a rule for
which is large or small, horizontal or vertical; **(b)** key lock on by
default, a tempo change leaving the pitch; **(c)** the waveforms:
readable at any size, cheaper to draw, zoomed out far enough to see the
tracks that follow, and scratched by hand; **(d)** the way back to the
player: the window kept in the background, not closed, when the decks
open; **(e)** where the master section goes -- in the player most of
what is touched, in the decks the channels, the master there too.

**(b) built 2026-11-25: key lock on by default.** A new track's state
has it on (`snurra_tile_state::key_lock`), so a tempo moved keeps the
pitch with no press of the key button. Key lock alone, at the track's own
tempo, changes nothing and must cost nothing: the deck, the stretcher
and a player's timeline read such a track straight, as before, so an
untouched deck stays exact to the frame -- the handover, train and stack
tests, which count frames, said so when it did not (a taker jumping
frame 39540 to 39542). A bend at tempo 1 -- sync's nudge -- is varispeed,
a hand on a platter. Off tempo 1 a key-locked deck is read through WSOLA,
whose grains make sync land within about 1.2 ms where varispeed landed
under 1 (`sync_matches_a_beat_to_another` now asks 2 ms; a flam is heard
from about 5). Tests about varispeed itself turn key lock off by name.
Tested in `a_tempo_change_keeps_the_pitch_by_default` (on in a new state
and its deck, the key as heard unmoved at +20 %, three semitones up with
it off; with the old default, it fails).

**(a) built 2026-11-25: one set of controls** (`controls.h`), used
wherever snurra has a knob or a fader. `snurra_knob` -- the knob settled
2026-11-18, held and moved up or down, out of `tile.cpp` where only the
key shift had it, so the mixer's knobs are no longer Qt's stock dial --
turns its whole range in twelve lines of text however many steps it has
(the key shift's 24 a step every half line, as before; an EQ knob's 200
no farther). `snurra_fader` is a DJ fader: a groove, a flat cap with a
line across it, ticks, its home tick long; held, it moves by as much as
the hand from where it was, never to where the press landed. Both go
home on a double-click. **Which is which, by how a DJ's hand uses it:**
a knob for what is turned a little and often -- gain, the EQ's parts, a
key shift, a master level -- small; a fader for what is thrown far and
watched -- long, vertical for a channel's fader (home: full), horizontal
for the crossfader (home: the middle). In a window's compact panel the
level and the tempo are faders too, compact (a line and a half thick),
horizontal as the panel runs. Not yet: a tempo fader on the deck itself,
vertical and long, as Traktor's (step 2b). The seek bar is not a fader
and stays as it is. Tested in
`the_knob_and_the_fader_move_by_the_hand_not_the_press` (a press alone
moves neither, a drag moves them by the hand, a double-click puts them
home; with Qt's own press back on either, it fails).

**(c1, c2) built 2026-11-25: the decks' waveforms cheaper, capped, and
zoomed out to ten minutes.** Measured first: four decks of a three hour
set's peaks, 1900 x 400, took 20.7 ms a frame at 8 s across -- over a
60 Hz screen's 16.7 -- drawn a filled rectangle a pixel, the part heard
at 45 % so every pixel of it blended, and repainted sixty times a
second whether anything moved or not. Now each part of a lane is one
filled outline, opaque (the dimmed colour mixed with the ground
beforehand), read from a pyramid of the peaks -- each level the loudest
of two below -- so a pixel reads one or two values at any zoom; and the
view repaints only when a deck has moved, peaks have come or the zoom
changed. 7.7 ms a frame at 8 s and 3.2 ms at 600 s, at load 23
(`the_waveforms_draw_cheaply_and_zoom_out_to_the_set`, which asks under
16 ms; the code before it took 20.7 and could not zoom past 60 s). The
wheel now zooms from 2 s to 600 s. Stacked rows stop at about seven
lines of text however tall the window, so a maximized deck view keeps
its waveforms waveform-shaped and gives the rest of the height to the
decks and the browser. **Open, asked:** which view's waveforms read badly
maximized -- the decks' (now capped) or the player's, whose tile shows
the whole track's overview, 1024 columns stretched to the window's
width.

**(d) built 2026-11-25: the way back to the player view.** The switch
of 2026-11-24 closed the window once the decks took its player, and with
it the only way back short of a deck's "Into a window", which folds the
decks. Now the window stays, following the decks' player as it did on
2026-11-22, and goes to the background (minimized); the decks have a
"player" button, at the left of the crossfader's row, that brings it
forward -- a count the decks send in their answer to FOLLOW, raised by
each press, a following window that sees it rise restoring and raising
itself -- or, where no window follows (decks opened on their own),
starts one (`snurra_dj::show_player`, `SNURRA_EMBED_VERSION` 8). Folding
the decks still ends the window, as before. Tested in
`a_window_opens_its_player_in_the_decks` (the window stays and holds the
two tracks; closed instead, it fails) and
`a_window_follows_the_player_on_the_decks` (minimized, the button brings
it back; with the count ignored, it fails).

**(c3) Scratching -- designed 2026-11-25, not built.** A scratch is the
sound following the hand, both ways, at the hand's speed; the deck reads
forward only, from a decoder, so a seek a mouse move -- the cheap way --
only stutters, and is not what the holder asked for. The steps, each
usable before the next: **(1)** a ring of the last few seconds the deck
has decoded, kept beside its reader, so the audio thread can read
backwards within it without the decoder; **(2)** a held state on the
deck -- while a hand is on the platter, the position is driven by a
target the UI thread moves, the audio thread reading at whatever rate,
negative included, reaches it smoothly (a slew, as the level's 5 ms),
by the cubic the stretcher already has, never through WSOLA, since a
scratch is vinyl and its pitch moves; **(3)** the gesture: on the decks'
waveform row, press and drag along the lane, the playhead fixed and the
track dragged under it, the drag's pixels turned to seconds by the zoom;
let go, the deck goes on as it was playing (or stays paused), as
Traktor's and Serato's do; **(4)** the same on a controller's jog wheel,
touch and turn. Each step has its test: the ring read backwards gives
the frames forwards reversed; a target moved at -1x is heard as the
track reversed; a drag of a beat's width moves the deck a beat. **Step (1)
built 2026-11-25:** `start_t::history_seconds` gives a deck a ring of
the frames it has played, indexed by track frame, written as each is
played (`keep`) and read back by `history_frame`; a jump -- a seek --
begins it again, so what it holds is always one unbroken run of the
track. Fixed when the deck is made, so the callback never allocates; 0
by default, so a deck never scratched -- every window's -- pays nothing
(6 s would be some 2.3 MB a deck at 48 kHz). Tested in
`a_deck_keeps_what_it_played_to_read_back` (two seconds of the ramp,
the last second read back frame for frame, the one before gone, a seek
beginning it again, a deck without one keeping nothing; with the write
taken out, it fails). Next: step (2), the held state the hand drives.

**(e) Where the master section goes -- the holder's, a proposal
(2026-11-25).** The holder: "we need to decide where the master section
goes ... likely to duplicate a lot of what each channel already has but
better, maybe compressor/limiter, FX ... In the player we mess around
mostly with the master section, in the decks mostly with the channels,
but master section still needs to exist." With the player's drawn EQ as
its master EQ (decided 2026-11-24, 3b, not built), a proposal to answer:
**in the player window, the one control panel is the master section** --
its drawn EQ the master EQ, its level the master level, a limiter and
later FX beside them -- what is touched there most; **in the decks, a
master strip at the right end of the mixer**, after the channel strips,
as a DJ mixer's master section sits: the master level (a knob), the
same master EQ as three knobs over its parts (the drawn line's shape
kept, as the channel knobs keep a tile's), a limiter switch with its
gain reduction shown, and the booth/headphone levels where a listener
exists. One master, two views of it, as the window and the decks are
two views of one set. Building it needs 3b first: the master EQ applied
once to the player's sum.

**One player or its decks, for now** (the holder, 2026-11-24: "For now
let's avoid the more complex features and stick to a single
player/decks, at least in the UI, if two of them are running
simultaneously they output separately and don't depend on each other";
asked, one instance is **one set, shown as the player or as the decks,
switched between**; and "the second one opens as a plain tile but can
either be dragged into a player or be made into a player of its own.
They can be dragged between other tiles or players too."). This narrows,
in the UI only, what is above -- the machinery stays, and the switch runs
on it:

- **The switch:** the window's "decks" hands its player to the decks and
  the window goes; the decks' "Into a window" hands it back. Not both at
  once: the window that followed the decks (2026-11-23, "leave the window
  open as a follower") and the decks' "In a window too" are off the
  screen, which also sets aside "two linked windows" asked earlier today.
- **No nesting on screen:** a player arriving on the decks unfolds across
  them by itself, so the decks' Unfold and Fold buttons go, and the
  listen button's menu of a nested player's tracks with them.
- **Tiles still travel:** a second snurra opens as a plain tile; it is
  dragged into a player or made a player of its own, and tiles go between
  players by drag, as before.

**Built the same day.** The window's "decks" hands its player over and the
window ends (`a_window_opens_its_player_in_the_decks`; left open, it
fails). A player arriving on a deck unfolds once faded in
(`snurra_dj::set_unfold_on_arrival`, on by default, `SNURRA_EMBED_VERSION`
7): a host -- or a test of the machinery, unfold, fold, a ride written
back, a deck started by hand -- turns it off and calls `unfold()` itself.
A player needing more than four decks at once cannot unfold and stays
whole on its deck, the one case where a nested track's socket
(`plug_track`) is still reached, which its test now uses. Unfold, Fold,
"In a window too" and the listen button's menu are off the screen; the
decks remember a deck is part of an unfolded player by a flag of their
own, where "Into a window" had asked whether Fold was visible. Found on
the way: closing the decks mid-handover crashed -- QWidget's destructor
deleted the server after the private part had gone, and an aborted
socket's "disconnected" ran the handover's fade-in on it; the sockets'
connections are now cut first in `~snurra_dj`.

**The player's EQ is the main EQ; a channel has its own** (the holder,
2026-11-24: "When we bring out the eq in the player it is the main eq,
for each channel on the mixer we also have gain, 3ch eq and possible FX
inserts etc."; asked, the player's EQ is **the player's master EQ**, one
EQ for its whole output, not a track's). So two stages, kept apart:

- **The main EQ**, the drawn line of up to twelve bands (`snurra_eq_line`)
  in the player's one control panel, is the player's: it shapes the
  player's whole mix after its tracks are summed, as a mixer's master
  section does.
- **A channel strip** -- a deck's on the mixer, and so a lane's, a
  channel being a deck being a lane -- has its own gain, a three-band EQ
  (knobs, as a DJ mixer's), FX inserts when there are effects, then its
  fader, then the crossfader.

**Where the code differs today:** the EQ is a track's own (`state.eq` on
each tile, carried by a handover), applied on that track's deck, and the
drawn line edits the tile whose panel is out. The move: the line edits
the player's master EQ, applied once to the sum; the per-track EQ
becomes the channel strip's three bands. Open in it, to settle when
building: what a handover carries -- the strip's EQ goes with the
channel, the master with the player -- and what a track's EQ kept from
before (a saved state, a handover from an older snurra) becomes;
the likeliest reading is its channel's strip, since it shaped that one
track.

**A layout that matches the hardware in front of you** (the holder,
2026-11-20: "we must not forget the user who will want to configure
snurra exactly according to the layout they see in front of them on a HW
controller. Auto generated controls can't be mapped to hardware for
example.")

- **Two layers, kept apart.** The tree above -- decks a train unfolded
  onto, players inside decks, a socket on every tap -- is made and
  unmade as the music moves, so none of it has a name that lasts. A
  controller binds to what lasts: **slots**, laid out by the person to
  match their controller -- deck 1 to 4, a channel strip each (fader,
  EQ, gain, a cue button), the crossfader, the effect units, the browse
  knob, the listeners' plugs.
- **The tree is put into the slots, never bound to directly.** Deck
  slot 1 shows and controls whatever deck the train has unfolded onto
  it, or the player a person loaded there; when that changes, the knob
  under the hand still means "deck 1's tempo". A slot with nothing in it
  is a slot, not an absence.
- **The person's layout is what is saved** -- which slots exist, in
  which order and place on screen, which deck slot sits on which side of
  the crossfader -- and a controller's mapping names slots and their
  controls ("deck 2 / tempo"), the same names in MIDI learn, in a mapping
  file and on the wire. Automatic things only ever fill slots.
- **A hardware cue button is a plug with a fixed place**: it moves its
  listener's plug to its slot's tap, so the dynamic "listen here"
  sockets and a controller's fixed buttons are one mechanism.
- The phone's cut-down view is a layout of fewer slots, not other code.
- **Remapping is live and in place** (the holder, 2026-11-20: "a context
  sensitive easy remapping method, for example to alt+click_hold on a
  control then twiddle a knob to set it to that one. so remapping can be
  done easily live, this requires that we filter out controller noise so
  we know the one thing that is being tweaked and ignore if unsure until
  a clear signal"). Alt held and the pointer held on a control listens;
  the one control moved binds to that control's slot. Against noise:
  listening starts before the press, so a control that chatters
  untouched (a worn pot, a jog's drift) is known and set aside; clock,
  active sensing and other system messages are never candidates; a
  binding is made only when exactly one control shows a clear change --
  a note on, or a run of values over a real part of its range -- and two
  moving, or none clearly, binds nothing and says to move just one. The
  same movement tells what the control is: absolute, a relative encoder
  (about 64, or 1 and 127), a 14-bit pair (CC n and n + 32), pitch bend.
  A control in no slot offers to make one. The gesture is the same for
  MIDI, HID and a key.
- **Named on both ends, so a mapping reads as text** (the holder,
  2026-11-20: "we wanna be able to name the controls on both ends, so
  they can be reasoned about in text as well ch_1_gain -> ch_a_gain etc.
  So they are readable instead of just numbers (which we do allow) but
  we allow (re-)naming, the name is just an alias for controller
  index"). A hardware control has its index -- port, MIDI channel,
  message, number -- and the person may give it a name, an alias of that
  index: `ch_a_gain`. snurra's receiving controls are named from their
  slots, `ch_1_gain`, renamable the same way. A binding is one line,
  `ch_a_gain -> ch_1_gain`, either side written by name or by number.
- **A name belongs to its player's process.** A second player -- an
  emergency one, a prepared mix for when something technical or human
  goes wrong, its play and stop on a button of their own -- is a process
  of its own, not a prefix in the first's names (the holder, correcting
  `main_*` and `emergency_*` the same day: "that player is a separate
  process ... they can't live in the same namespace"). Each player is
  known by its name on the wire (sec 3b's sockets), its controls named
  within it, and a binding says which player it reaches. How a
  controller reaches a second process -- each subscribing to the device,
  which ALSA's sequencer allows, or one passing it on over the wire -- is
  settled with step 6.

**2 or 4 decks**, a choice in the view: two layouts of slots. **Where
the decks sit, 2026-11-20** (built; `snurra_dj::arrange`, and a deck's
right-click menu): by default two to a row, A and B above C and D; or as
the person arranges them, letters in order and "/" between rows -- the
menu offers A B C D and C A B D in a row, the order many four-deck
controllers have. Only an arrangement chosen is written
(`decks/arrangement`); with two decks only, those named sit where named.
Tested in `the_decks_sit_as_the_person_arranged_them` (default grid,
"C A B D" in one row, kept and read again, two of them in place, the
default writing nothing); with the arrangement ignored, it fails. Each
deck's side button begins with its letter ("A: left (default)"): a loaded
deck shows its track, not its name, and seen in Xvfb arranged C A B D
nothing said which was which. **Each
deck's side of the crossfader, 2026-11-20** (built): a small button on
each deck goes from its default -- A and C left, B and D right, shown as
"left (default)" -- to left, right and thru (out of the crossfader's
reach, as a mixer's assign switch), and round to the default again. A
side chosen is kept (`decks/<letter>/crossfader`); a deck on its default
writes nothing, so a later default reaches it (working-practice.md, *A
config file records choices*); the crossfader's ends name the decks on
each side. Tested in `each_deck_has_its_side_of_the_crossfader` (C to
thru full at either end, D to left with A, both kept and read by a deck
view made again, D round to its default forgotten); thru taken as left,
it fails. The phone gets the cut-down one:
two decks, the controls that fit (sec 3b's fold-cover measures stand).
**Hardware control on the desktop**: every deck and mixer control is a
typed control (sec 3a kept the string bus out; a name per control is
layered on the typed ones), reachable from a controller's MIDI and from
the wire, so a controller drives the deck view and a window's train
alike.

**The order it is built in**, each step usable before the next:

1. `snurra_timeline`: the window's timeline as data, the window drawing
   it; nothing heard changes (the suite is the proof). **Built
   2026-11-20** (`timeline.h`): lanes by row, each a begin and tiles of
   length, tempo, offset, chosen overlap and grid; `start`, `overlap`,
   `at` and `time_of`, with the train's two overlap functions moved in
   beside them. The window takes a snapshot of its tiles
   (`snurra_node::timeline`) and asks it. The suite passed whole on it,
   and `a_timeline_places_its_tiles` holds the model alone -- with the
   overlaps left out of `start` it fails. **Reworked the same day** to
   tracks free or following another, and channels seated (above): a
   window's lane is a train whose first is free, so it asks as before.
   `channels_are_seated_so_none_overlaps`: a train of five alternates
   0 1 0 1 0; a second train connected after it flips to start on 1;
   two trains 10 s apart take three channels, the most sounding at once;
   a channel kept, a pin kept and its collision named -- with what still
   sounds ignored, or the kept channel ignored, it fails.
2. A timeline as a source, so a deck plays a player's output -- a
   channel with a prepared mix. **Built 2026-11-20**
   (`snurra_open_timeline`, `src/timeline_source.cpp`): a tile state
   holds a player (`player`, its tiles' states, a tree), and a tile so
   given plays their sum on its deck, the deck opening it through a
   factory (`start_t::open`) so it knows nothing of timelines. Each
   track is read as a deck reads it -- straight at tempo 1 with no key
   lock or shift, through its own stretch otherwise -- at its level and
   envelope, crossing over on the quarter circle a train's decks use;
   a nested player is a track whose source is a timeline in turn. A
   handover carries the tree, and a state read off the wire nests no
   deeper than `SNURRA_PLAYER_DEPTH` (16), each level being a call.
   Tested sample by sample through the test ramp
   (`a_player_sounds_as_its_tiles`: a train of two, the second at half
   level and tempo 2, the crossfade, a seek landing on the very samples,
   and the same player nested inside another), and on a real deck and the
   wire (`a_tile_plays_a_player_and_the_wire_carries_it`); the crossfade
   or the level left out fails the first, the depth limit or the deck's
   factory left out the second. Found on the way: a track read through
   the stretch at tempo 1 lost its last two frames, the cubic wanting
   frames past the end -- why a deck reads straight there, and now this
   does too. **A nested track's EQ, 2026-11-20:** the deck's isolator
   is a type of its own now (`snurra_isolator`, moved out of
   `snurra_deck` unchanged), and each nested track runs one at its
   state's bands, as its deck would -- an 8 kHz tone with its high band
   killed comes out frame for frame as the isolator makes it, below -40
   dB (`a_nested_tracks_eq_is_the_decks`; with the isolator left out it
   fails). **And its level match, 2026-11-20:** a nested track whose
   analysis is kept takes the trim its deck would (`snurra_loudness_gain`
   toward -14 LUFS) while `loudness/match` is on; one not yet analysed
   plays as it is, as a deck's does until its analysis comes -- nested
   tracks are not analysed of themselves yet. Tested by a tune kept as
   -20 LUFS: nested, every sample at the deck's gain with the match on,
   as it is with it off (`a_nested_track_is_brought_to_the_level_its_deck_would_give`;
   without the trim it fails). **A player's beat, 2026-11-20:** a tile
   holding a player takes as its own grid the kept grid of the track it
   sounds -- in a crossfade the one coming in -- mapped into the player's
   time (its start, its tempo, its offset), its loudness not the tile's;
   so sync onto or from a deck holding a player follows whichever track
   is heard. Tested with two tunes kept at 120 BPM and at 128 played at
   1.25: standing in each, the tile's beat is that track's where it falls
   (`a_player_in_a_tile_beats_with_the_track_it_sounds`; unmapped, it
   fails). A track never analysed gives the tile no beat there. **A player's
   waveform, 2026-11-20:** a tile holding a player draws its whole mix,
   read over the timeline on the peaks' own thread (a `snurra_peaks`
   given a function opening its source); tested by a tone coming in at
   1 s on a player of 2 s -- silent columns before, 0.2 after
   (`a_players_waveform_is_its_mix`; measured over half, it fails).
   **A player's tracks analysed in its tile, 2026-11-21:** a tile
   holding a player analyses its tracks never analysed -- one after
   another on the analysis's thread, each kept as a deck's would be -- and
   takes its beat again as each comes, so sync follows a prepared mix of
   tunes fresh from the folder (tested by two such tunes at 120 and 128
   BPM: the tile's beat becomes the second's and it is kept;
   `a_players_tracks_are_analysed_in_its_tile`, without the analysis it
   fails). Its tracks' level match comes from what is kept when the
   player's sound opens, so a track analysed later is matched the next
   time it opens.
3. The deck view on the wire as a peer, 2 or 4 decks: a tile or a whole
   player handed onto a deck and pulled off it, gapless. **Built
   2026-11-20.** `snurra_dj` takes two decks or four (`set_deck_count`,
   `--decks 4`; A and B above C and D, A and C on the crossfader's left)
   and, in the player, listens on `<pid>.decks.sock` (`listen`). It
   answers WHERE with its frame and its deck count, so a window knows a
   deck view from a window; a tile let go over it is offered to it with
   the point (`message::deck`, else `frame`), and lands on the deck under
   that point -- the tile, or with Shift the whole player, which arrives
   as one tile holding the group (`snurra_player_state`: as long as its
   timeline, at the group's time). On Wayland a deck takes the tile's
   drag and asks its window, as a window does (MERGE_INTO with the deck).
   The handover is the window's own -- held silent and in step, READY,
   COMMIT, faded in -- with its times moved into `wire.h` so both sides
   share them. A deck's title dragged out of the window opens a window
   of its own that takes it (ADOPT, as a pulled-out tile). The crossfader
   is a gain of its own in the deck now (`set_mix`): it was `fade()`,
   which a handover's fade-in would have overwritten. An embedded tile
   can hand its deck a retire time (`retire_deck_at`), so a deck given
   away sounds on through its fade. A deck view does not count as a
   player already running, so a file opened beside one still opens
   first and playing. Tested between real processes
   (`tiles_go_onto_decks_and_back_out_in_step`: a playing tile onto deck
   C of four, its position carried within 2 ms and the crossfade taken,
   the empty window gone; a train of two onto deck A as one player at
   the group's time; deck C out into a window that plays on); taking the
   first tile alone in place of the whole player, or ignoring RELEASE,
   fails it. Seen in Xvfb at 1100 x 760 with two tones on A and B: the
   four decks in two rows, the crossfader's ends "A C" and "B D", the
   browser under them. **A player out of a deck is tiles again**
   (2026-11-20, `snurra_player_tracks`): a window that is empty when it
   takes one tile holding a player unpacks it, each track a tile where
   the player stands -- the one sounding playing at its frame, those to
   come at their start, those past at their end -- so a deck pulled out
   is an editable train again; into a window with tiles it stays one
   tile, a channel with a prepared mix. Tested by deck A's train of two
   pulled out: two tiles, the first playing within 5 ms of where the
   player stood, the deck left empty; without the unpacking it fails. A
   player taken out inside a crossfade has both its tracks playing full
   until the overlap ends (the train's fades are started by the window
   only for a next it starts itself). **Not yet:** the deck view's own
   look at four decks on a small screen. Measured 2026-11-21 offscreen at
   412 x 892 (a phone upright): the width fits -- two decks to a row, the
   view's minimum 338 wide -- but each loaded deck is given 286 px of the
   301 its tile needs, cutting its EQ off, and the view's minimum is some
   800 px tall: more than a phone has under its bars. So the phone's view
   has to be cut down, not merely laid out; which controls stay is the
   holder's ("an optimized cut down version"). Best guess: two decks on a
   phone, each its waveform, play, cue, level and tempo, the EQ and keys
   behind the deck's pull-out, the browser a tab of its own.
4. A train unfolded onto the decks, its automation driving them, a hand
   taking over. **Built 2026-11-20** (`snurra_dj::unfold`, the deck's
   "Unfold across the decks"): a deck holding a whole player spreads it
   over the decks, its tracks seated on channels (`seat`), channel c on
   deck c, two decks becoming four where the player needs them. The deck
   view conducts it on the shared clock: each track loaded and started a
   lead (1.5 s) ahead of its moment, as a window's train starts its next;
   its crossfades the deck's fade, in then out, on the quarter circle; a
   deck whose track has ended shows the next to come there, cued. The
   decks' play and seek are the player's (`set_grouped`, as in a
   window's group). A hand on a deck's fader (`level_held`) takes that
   deck from the automation -- no fade, no envelope -- while held, and
   gives it back when let go. The deck that held the player fades out
   over the moment the decks come in. A tune loaded by hand ends the
   conducting. Tested in `a_player_unfolds_across_the_decks`: a train of
   three tones over the wire onto deck A, unfolded onto A, B, A -- B cued,
   then in at 2 s half a second into its track by 2.5 s with A fading
   out; the third cued on A once A's track ended at 3 s and still at its
   first frame at 3.7 s, half a second in at 4.5 s; B's fader held lifts
   its fade and let go restores it. Without the hold, or without the
   automation, it fails. A started track's tile says playing from the
   moment it is started, a lead before it sounds -- as a train's next
   does. **The automation seen, 2026-11-21:** a conducted deck shows a
   thin meter under its fader of what is heard of it -- the gain the
   deck's callback last gave a frame (`heard_gain`: level, trim, mix,
   fade and envelope together) -- so the automation is seen moving while
   the hand's fader stays where the hand left it, and a hand taking over
   sees what it takes over from; tested mid-crossfade in the unfold test
   (B's meter part of the way in, A's part of the way out; with the gain
   never published, it fails). (A hand on the crossfader is not
   recorded: the crossfader is the hand's, never automated, above.)
5. Moves on the decks written back into the train. **First part built
   2026-11-20**: a fader ridden on a conducted deck -- held, moved, let
   go -- is recorded against the track's own frames (a point each 50 ms
   or each move) and, when let go, written into that track's envelope in
   the player: the ride where it was, the envelope as it was on either
   side, thinned to the deck's 64 points; the fader goes back where the
   hand found it, so the automation plays the move from then on, at the
   gains heard (the ride's level over the level found). Tested in
   `a_fader_ridden_is_written_into_the_track` (a fader pulled from 100 to
   50 while playing: the envelope ends its ride at half, then 1 as it
   was, the fader back at 100; not written, it fails). **Not yet:** a
   crossfade done by hand on the channel faders becoming the overlap.
   What is written already (2026-11-21 reading): the incoming track's
   start, when the hand starts it (`start_now`), and both faders' rides
   in the envelopes. What is not: an outgoing track faded to nothing
   before its end keeps its silent tail, so the overlap stays as long as
   the track. Whether that tail is cut -- the track's end moved to where
   the hand let it fall silent, shortening the overlap -- or kept as the
   envelope's silence is the holder's: one edits the track, the other
   only its level. Best guess: cut, since a train's overlap is what the
   person hears crossing.
   **A deck started by hand, 2026-11-20** (`snurra_dj::start_now`;
   Shift with a cued deck's play): the track cued there comes in now
   from its start, and where it follows another and the train can say
   it -- inside the one before, at most half of either -- the overlap
   that brings it in then is written into the player and the score's
   times taken again (its channels kept: decks do not change places
   mid-set); elsewhere it starts all the same, unwritten. Tested by two
   6 s tones crossing 1 s, the second started by hand at 3 s: it plays
   from its start, and folded the player has it crossing by some 2.8 s;
   not written, it stays at 1 s and fails. **And the
   rest of the hand's moves:** a conducted deck's EQ, tempo, key lock and
   key as the hand left them are written into its track in the player
   when the track leaves the deck and when the decks are folded
   (`keep_moves`); tested by a band cut and one killed on a conducted
   deck, found in the folded player (without it, it fails). **And folded
   back** (`snurra_dj::fold`, a conducted deck's "Fold back into one
   deck"): the conducting ends and deck A holds the player again, as
   edited -- its rides in its envelopes -- where it stands, crossing over
   on the shared clock as unfold did; pulled out from there it is the
   whole player, and an empty window unpacks it into tiles. So the round
   trip is whole: a train dropped onto a deck, unfolded and played by
   hand, folded, taken out, edited as tiles. Tested at the end of the
   ride's test: folded, deck A holds both tracks with the first's ride in
   its envelope, standing past where the track stood and within the
   crossing, deck B empty; folded from where nothing is unfolded, false;
   folded at 0, it fails.
6. Slots and the person's layout; controllers: MIDI in, bound to slots.
   **First part built 2026-11-20** (`midi.h`, the deck view's
   `take_controllers`, on in the player's `--dj`): MIDI in through
   ALSA's sequencer -- a client "snurra" connected from every port that
   sends, and from each that appears (the system's announcements), clock
   and active sensing never read. The deck view's controls are named by
   slot, `ch_1_play` ... `ch_4_eq_high` and `crossfader`, each widget
   found by what it says it is. A hardware control is named by its index,
   "<client:port>/ch<n>/cc<n>" (or `note<n>`, `bend`), by the port's name
   so a replug keeps it. **Learning:** Alt held with the pointer pressed
   on a control listens (the EQ's band is the third under the pointer);
   what moved in the second before is chatter and set aside unless it now
   moves far more; a binding is made as soon as exactly one control moved
   plainly -- a note, or a run of four or more over 12 of 127 -- and on
   letting go, two or none bind nothing and say so. The same movement says
   what it is: button, absolute, relative (about 64, or 1 and 127), fine
   (a controller and its partner 32 above), bend. A binding is a line,
   `ch_a_gain -> ch_1_level (absolute)`, either side an alias or an
   index; only bindings and names the person made are written
   (`controllers/` in the settings). Applied: a button on its press (play,
   cue, sync), a level 0 to 100, a tempo over the deck's +-16 %, an EQ
   knob killing at its bottom, flat at its middle, +6 dB at its top, a
   relative step adding. Tested pure (`the_control_moved_is_told_from_the_noise`)
   and the whole way through the sequencer
   (`a_controller_learned_by_holding_moves_the_deck`: a sender of the
   test's own, deck A's level held with Alt while a pot wandering 14 of
   127 chatters and a fader moves -- the fader bound and then moving the
   level, the pot moving nothing); without the chatter rule both fail,
   the second binding the pot. **Each player its own names, 2026-11-20**
   (`snurra_controller`, shared by the deck view and a window): a window
   started with `--name <player>` takes controllers too -- an emergency
   mix, its own process -- its controls `play` (the group's, both ways)
   and `stop`, learned on a tile's play button. A binding names the
   player it reaches, `emergency:play`; one naming none is the deck
   view's. Every process with controllers reads the same device (ALSA's
   sequencer lets each subscribe) and takes only what names it, reading
   the bindings again at most each second, so one learned in another
   process is heard. Tested in one process with the deck view and two
   named windows (`each_player_takes_the_bindings_named_for_it`):
   `emergency:play` plays that window and neither deck A nor "backup",
   `ch_1_play` deck A alone, a note learned on the window's play button
   replaces what played it; without the player's check, "backup" plays
   on the emergency button. **Both ends named, 2026-11-20:** snurra's
   controls take names of the person's as hardware controls do
   (`set_control_name`, the settings' `controllers/names`, lines
   `deck_a_volume = ch_1_level`), so a binding reads
   `ch_a_gain -> deck_a_volume (absolute)` and is read back through both
   names; nothing named writes nothing (tested in
   `the_control_moved_is_told_from_the_noise`; without the names read
   back, it fails). **A fine control's both halves, 2026-11-20:** a
   control learned as fine (a controller and its partner 32 above) is
   moved by either half, the two read as one value of 0 to 16383 and
   given on as a bend's, so a 14-bit pitch fader is not stepped in
   128ths (tested by feeding the controller the halves in turn; with the
   finer half ignored, it fails). **The browser on a controller,
   2026-11-21:** `browse` steps the browser's choice by a relative
   encoder, or puts it at its place down the list by a knob, and
   `ch_N_load` puts the chosen tune on deck N; learned by Alt held on the
   list and on a deck's frame. Tested by an encoder stepping down three
   and back one, then deck B's load (with the steps ignored, it fails).
   **An encoder's step, 2026-11-21:** controllers send a step about 64
   (65 up, 63 down) or as two's complement (1 up, 127 down); the step was
   read as two's complement below 64, so a turn down sent as 63 went 63
   steps up. Small steps tell the two apart (`snurra_relative_step`: up
   to 31 up from 0, 33 to 95 about 64, over 96 down from 128); tested by
   65, 65, 65, 63 on the browser (with the old reading, the choice ran to
   the list's end).
   **Not yet:** slots other than the decks, their strips and the browser
   (snurra has no effect units) -- the decks' own are arranged and sided
   below -- and HID and Android MIDI. **Learning
   without Alt, 2026-11-21:** a window manager that takes Alt with the
   pointer to move windows -- TDE's and KDE 3's do by default, so the
   holder's desktop would likely never pass the press to snurra -- is met
   by a "learn" button by the crossfader: while it is on, a control
   pressed learns as Alt held with it does (`snurra_controller::set_learning`).
   The end-to-end test learns its fader that way; with the mode ignored,
   it fails. In a window with controllers (an emergency player) Ctrl+L
   turns the same mode on and off -- free among mplayer's keys -- and the
   emergency player's test learns its play button so; with the key
   ignored, it fails.

7. Taps and listeners: a listener's output, the sockets, the plug moved.
   **First part built 2026-11-20** (`snurra_dj::add_listener`, `plug`;
   the player's `--listen <output>`, as many as wanted): a listener is a
   second engine on an output of its own (`snurra_engine(output, ...)`),
   plugged into one deck at a time. It hears a mirror of that deck -- the
   same track opened again on its own deck, anchored on the shared clock
   where the deck will be heard, its tempo, key lock, key shift and EQ
   followed, re-anchored when the deck is played, paused or sought or
   drifts past 10 ms -- at full level, so before the deck's fader and the
   crossfader, as a cue channel is. States travel, not sound (sec 3b):
   the mirror reads the track itself, so two devices' clocks never meet in
   one buffer. A deck's track replaced -- a tune loaded, a player's next
   track -- the plug stays in the deck and hears the new one. Each deck
   shows "listen" while a listener exists; clicking it moves the first
   listener's plug there, again takes it out. Tested in
   `a_listener_hears_the_deck_its_plug_is_in` against a capture: deck A
   at 440 Hz with its fader down and the crossfader all on B (1320 Hz);
   plugged into A it hears 440, moved to B 1320, unplugged nothing; a
   mirror at the deck's fader, or an old mirror left sounding, fails it.
   The mirror takes the deck's level-match trim as well: a cue hears the
   deck after its gain staging, as the room does, and before its fader
   (tested by a trim of a half on A, heard at half in the headphones;
   without it, full). A deck's socket is a control, `ch_N_listen`, so a controller's cue
   button learned onto it is a plug with a fixed place: pressed, the
   first listener's plug goes there, pressed again out
   (`a_controller_learned_by_holding_moves_the_deck` learns a note onto
   deck A's socket and plugs and unplugs with it; with the control
   ignored, it fails). **A window's tile is a socket too, 2026-11-20:**
   a deck view says in its answer to WHERE how many listeners it has; a
   window asks its peers that every two seconds (a question no peer
   answers is given up after 1.5 s), and while one has, each of its tiles
   shows "listen". Clicked, the window sends the deck view LISTEN with
   its socket and the tile; the deck view plugs its first listener in
   there and follows the tile by its status, asked each 50 ms on one
   socket -- the mirror anchored where the tile's state says it is heard,
   at its tempo, key and EQ, its level match from the track's kept
   analysis -- states travelling, not sound, as everywhere. Tested in one
   process with a window and a deck view on the wire
   (`a_listener_plugs_into_a_windows_tile`: the socket appears, the
   headphones hear the tile's 440 Hz, silent when it pauses, the plug out
   when clicked again); ignoring LISTEN, or the tile's pause, fails it.
   With two listeners or more, a chooser by the crossfader says whose
   headphones a deck's "listen" moves (`choose_listener`); tested with a
   second listener, chosen there, its plug moved and the first's left
   (with the choice ignored, it fails). **A nested player's track is a
   socket too, 2026-11-23:** on a deck holding a player, "listen" has a
   menu -- the whole deck, then each track by name -- and a track chosen
   is heard as the player sounds it: its own file mirrored where the
   deck's player will play it, silent before it comes in (the mirror
   started at its first frame for the moment the deck reaches it) and
   after it ends, at its tempo in the player times the deck's, its own
   key, EQ and level match -- before the player's envelope and the deck's
   fader, as a deck's socket is before its fader. A tune loaded in place
   of the player puts the plug back on the whole deck
   (`snurra_dj::plug_track`, `SNURRA_EMBED_VERSION` 6). Tested in
   `a_listener_hears_one_track_of_a_decks_player`: two tones, 440 Hz and
   880 Hz coming in at 2.5 s, deck A's fader down; the first track heard
   at 440, the second silent before it is in and 880 after; with the
   mirror taken from the whole deck, or the second started at once, it
   fails. Not yet: a socket inside a nested player's nested player (a
   track that is itself a player is mirrored as that whole player).

**Mixxx's mappings: plain, no JavaScript** {#mixxx-mappings}

Status:  settled 2026-11-21
Decides: mixxx-mappings

The holder, 2026-11-21: "plain Mixxx mappings, no JavaScript" (QJSEngine
is in QtQml, which this player does not link). `snurra --import-mapping
<file.midi.xml>` reads a mapping's `<controls>` into the bindings
(`snurra_bindings::import_mixxx`) and says, a line each, what it did not
take: a control bound to a script, one snurra has no control for, a
note on a fader. A fader or knob marked `<invert/>` -- ten mappings'
crossfaders and tempo faders -- is taken upside down, written
`(absolute, inverted)` in its binding's line (2026-11-21; tested by an
inverted tempo fader at its top giving the slowest tempo, which without
the inversion gives the fastest). Mixxx's controls map to the deck view's
slots: `[ChannelN]` volume, play, cue_default, pfl, rate,
LoadSelectedTrack, beatsync and sync_enabled, keylock, filterLow/Mid/High
to ch_N_level ... ch_N_eq_high (`ch_N_key_lock` a deck control from
2026-11-21, its press turning the tile's key lock over); the EQ rack's parameter1 to 3 to the same
EQ; `[Master]` crossfader; and the library's SelectTrackKnob and
MoveVertical to `browse`, SelectNext/PrevTrack and MoveDown/Up to the new
`browse_next` and `browse_prev`. Options decide the kind: selectknob,
diff, rot64, spread64 and the jogs are relative; fourteen-bit-msb on a
controller under 32 is fine (its partner 32 above, as snurra pairs them);
a note a button; a bend a bend. A mapping does not know which port the
controller is on, so the import binds from any port (`*/ch1/cc7`); a
binding learned for a port wins over it there, and a second import
replaces the first's while what was learned stays. Several hardware
controls may move one control, as in Mixxx -- and learning that control
afterwards replaces them all, as a learn does. Of the 132 MIDI mappings Mixxx
2.5 installs, 108 give bindings, 1501 in all; the rest are their scripts.
`rate` is taken in Mixxx's raw direction (0 slowest). Mixxx itself
turns it by the person's slider-direction preference -- its speed is
`1 + rate_dir * rateRange * rate` (`ratecontrol.cpp`, 2.5) -- so a
mapping cannot say which way its fader runs: a guess, to be put right
on a real controller, where `(absolute, inverted)` in the binding's line
turns it. Tested in `a_mixxx_mappings_plain_bindings_move_the_decks`
(with the any-port lookup removed, it fails).

**Open, the holder's:** the plain binding most left untaken is `pregain`,
a deck's gain knob (157 in the installed mappings; then `headMix`, 60).
snurra levels each track by its analysis (sec 3a's loudness), so a hand
gain would sit on top of that trim; whether decks get one is a design
choice, not the import's to make. Best guess: +-12 dB on the trim,
centred flat, carried in the tile's state as its EQ is.

**The controller's lights, 2026-11-21:** a mapping's `<outputs>` light
the buttons with the states snurra has -- a deck playing (`play` and
`play_indicator`), a listener's plug in it (`pfl`), its key lock -- kept
as `controllers/lights`, a line each (`ch_1_play -> ch1/note59 127 0
0.5 1`: the message, `on`, `off`, and the range of the value lit). The
sequencer is opened both ways, with a second port, "lights", unexported,
that sends only to a controller that has been heard from and takes
messages -- so no other program is sent lights unasked, and a
controller plugged in gets every light again once it is first heard.
The deck view's states are looked at ten times a second and a light
sent only as it changes. Other lights (VU meters, hot cues, effects)
are not taken, and the import says so. Mixxx's mapping format makes
`<minimum>` required (its wiki, *Midi Controller Mapping File Format*);
where one leaves it out -- 75 of 2257 installed outputs -- 0.5 is taken,
so a stopped deck is dark. An `<off>` of 0xFF sends nothing, as the
format says (4 outputs, one mapping); taken as 127 it lit the button
while off, which the test's key lock light now guards. Of the installed
mappings, 54 give lights, 306 in all. Tested in
`a_mappings_lights_show_the_deck_playing`: an ALSA client as the
controller, nothing sent before it is heard, then its play light off,
on as deck A plays, off as it pauses (with the play state not read, it
stays off and fails).

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

**-O2 faster than -Os, 2026-11-14: a code smell, and it was the code.**
The holder, on an -O2 exception made for the picture and the analysis:
"-O2 being faster than -Os is a code smell, please research this deeper
before deciding on an exception". The exception was taken back, and perf
on each, built both ways, found what -Os left undone -- each a habit of
code leaning on -O2, not a reason to change flags:
- **The picture's conversion** (`to_rgb`, a 4K frame shown 1280 wide,
  benchmarked alone): the `wide` and `interleaved` tests read each
  sample inside its loop, which -O2 hoists and -Os does not (a sixth of
  the time); a row-sum array written a sample, which -O2 vectorises; a
  float divide a pixel; `qRgb` left a call. With 8-bit planar loops that
  test nothing a sample, each 8-bit pixel summed straight from the
  film's rows, a reciprocal a column and the pixel packed in place: -Os
  24 ms a frame to 12 to 14 (-O2 15 to 9), under the 16.7 a picture at
  60 allows; images identical, hash for hash, at four widths and both
  branches. Earlier, under load and before the helpers were forced
  inline, the tile had shown 6 pictures a second at -Os against 44 at
  -O2.
- **The analysis** (`beats.cpp`): 44 % of its time in libgcc's
  `__mulsc3`, `std::complex`'s C99 multiply with its infinity and NaN
  handling, which -Os calls for every FFT product where -O2 inlines a
  fast path; and `std::abs` on a complex is `hypotf`. Written out in real
  arithmetic, with a square root for the magnitude: a minute 0.93 s to
  0.42-0.48 at -Os, the same as -O2; the holder's 48 Mixxx-analysed
  tracks 320 s to 149 s, every key, BPM and fit the same.
- **Key lock** (`snurra_stretch::step`, in the audio callback): the
  same lens found it -- 7.9 % of a core a deck at -Os, 2.8 % at -O2, 43 %
  of it in `at()`, left a call clamping its index for every sample of
  every offset of the WSOLA search. The window is all here once
  `have()` has run, so its channels are summed once into buffers made
  with the stretch, the same sums in the same order: 2.5 % at -Os (1.4
  at -O2), bit for bit the same output, and every key-lock figure in the
  suite unchanged.
- **Sanitizers over the day's new code** (the estate, the picture's
  bands, the stretch's buffers, the DASH lock; the whole suite under ASan
  and UBSan): no report from snurra's code; two from fuzznet's, both a
  qsort of a NULL array when a fresh node's catalogue is empty
  (`local/rpc.c` 2822, 2823) -- reported to it (inbox 20261114T0146Z).
- **The deck's callback as a whole** (a tone, EQ in with a band killed,
  varispeed at 1.03): 1.35 % of real time at -Os, 1.27 % at -O2 -- no
  smell there; nor in the waveform's peaks, a minute's file in 77 ms at
  -Os and 80 at -O2. So the lens stopped.
The player stays -Os throughout. What the forced inlining and the row
bands kept cost: 29 KB of code (538369 to 567498 bytes).

**A double free playing DASH in a tile, 2026-11-14, found and closed.**
Once in a full suite, `an_hls_stream_plays_and_seeks(DASH)`'s tile aborted
with "double free or corruption"; under AddressSanitizer, 3 runs of 10.
FFmpeg's DASH demuxer calls libxml2's `xmlCleanupParser` after each
manifest, which frees process-wide state, and a tile's sound and picture
readers open one DASH URL at once: both cleaned up, one freed what the
other had. Every `avformat_open_input` now goes through
`snurra_avformat_open`, which lets one thread at a time open a URL or a
`.mpd` (a local file as before); under ASan, 0 in 20 since. QtTest's
crash handler then printed "Received signal 6" 2.4 million times until
the suite's 256 MB file cap ended it, so the test binary now gives every
class `-nocrashhandler` (and allows no core file): a crash ends the run
there. That rests on QtTest's documented option -- an abort sent from
outside was tried to show the difference and did not reach the handler
either way, so the loop itself was not reproduced.

**Built for size, 2026-11-13** (sec 1: tiny, held deliberately):
`-Os` with sections collected (`--gc-sections`). The `-Os` stood in
`QMAKE_CXXFLAGS`, where qmake's release `-O2` follows it and wins, so
it was never in force; in `QMAKE_CXXFLAGS_RELEASE` in its place, the
player's code went from 716906 bytes to 522752 (27 % less), and the
suite -- whose handover tests run the real player -- passes on it.

**Sanitizers, 2026-11-12:** the whole suite under ThreadSanitizer, and
under AddressSanitizer with UBSan, both built out of tree with qmake's
`CONFIG+=sanitizer CONFIG+=sanitize_thread` (or `sanitize_address
sanitize_undefined`), the snurra the tests start included: no report.
Each was shown live first by a deliberate race and a deliberate heap
overread, both reported. `test/main.cpp` sets
`QTEST_DISABLE_STACK_DUMP`: QtTest's crash handler re-faulted and
re-attached gdb 361 times in 281 s on a fixture's overflow.

**Sanitizers over 2026-11-20's new code** (the timeline and its source,
unfold and the conductor, the listeners, MIDI, the deck view on the wire):
its nineteen new tests run one by one under AddressSanitizer with UBSan,
built out of tree with qmake's `CONFIG+=sanitizer sanitize_address
sanitize_undefined`, the player the handover tests start included, leak
reports off (Qt's own at exit): all passed, no report. The runtime was
checked live in both binaries (linked, and answering `ASAN_OPTIONS=help=1`);
no deliberate fault was planted this time. The window's sockets came after
this run.

**Fuzzing, 2026-11-24, the first:** libFuzzer with ASan and UBSan over
the parsers of outside text -- `.srt` subtitles (they come beside
downloaded films), ASS dialogue, the wire's `decode`, playlists and cue
sheets -- each harness a few lines that link only the file it tests:

    clang++ -std=c++17 -g -O1 -fPIC -fsanitize=fuzzer,address,undefined \
        -Iinclude fuzz.cpp src/subtitles.cpp src/wire.cpp \
        $(pkg-config --cflags --libs Qt6Core Qt6Gui Qt6Network) -o run
    ./run -max_total_time=180 -rss_limit_mb=2048 -max_len=4096 corpus

(playlists and cue sheets link `src/source.cpp src/bytes_input.cpp` and
FFmpeg's four libraries, and write each input to a file first). Two
found, both in three minutes. **The wire:** a frame whose list counted
past its bytes made Qt's own `QList` reader reserve that count -- 140 GB
-- an out-of-memory under ASan that stopped the fuzzer there; in a plain
build the machine refuses the allocation and Qt passes it over, so no
user saw it. Lists now go through `snurra_read_list`, which refuses a
count past the bytes left (tile_state.h). **The `.srt` time:** twenty
digits of hours overflowed the sum in 64 bits, undefined behaviour; each
field is now six digits at most, past any film. Both are pinned by tests
in `subtitles_test`, but the plain build behaves the same with or
without either fix, so their sabotage was checked in the harness: the
fixes taken out, it reports the overflow and the out-of-memory again;
with them, nothing. Then over a million runs of the three text parsers
and 220,000 of playlists and cue sheets found nothing more.

**Sanitizers over 2026-11-21's new code** (the EQ's knobs and joining,
the library's scan number, the way to and from the decks, the window's
bar, panel, new lanes, ruler and one picture): the fourteen tests that
cover it run one by one under AddressSanitizer with UBSan, built out of
tree from a worktree at f43591b with the same qmake flags, the players
the handover tests start included, leaks off: all passed, no report.
The runtime was checked live in both binaries as before; no deliberate
fault planted.

**Sanitizers over 2026-11-22's new code** (a window following the decks
and the way back, the window staying after "Open in the decks",
mplayer's options, standard input's reader thread): the four tests that
cover it run one by one under AddressSanitizer with UBSan, built out of
tree as before, the players they start included, leaks off: all passed,
no report. Under ThreadSanitizer (a TSan player given by `SNURRA_BIN`)
the stdin and options tests passed and reported races -- every frame in
QtGui's and QtCore's pooled threads, none in snurra's code or the stdin
thread (a `std::thread`), and as many from
`started_with_nothing_it_plays_nothing` (2026-11-13), which plays
nothing: Qt itself, not built for TSan, not this code. The 2026-11-12
TSan run's "no report" predates whatever of Qt's pool those are.
cppcheck over the files changed since 2026-11-18: nothing real (the
`!self` above again; a `QPointer` checked in a later lambda).


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

**AddressSanitizer with UBSan, 2026-11-13, after the library, tags,
beat cache, chapters, cue sheets and sorting:** no report over the whole
suite. One timing bar fails under it and not without: the first picture
after a 9 s skip into a 1080p long-GOP file (`video_test`) lagged the
sound 133-156 ms in four runs against its 150 ms bar, where the plain
build measures 6-50 ms in five -- the instrumented conversion of some
290 decoded frames, not a fault.

**Static analysis, 2026-11-18, the first run:** cppcheck (warning,
performance, portability) and clang-tidy (bugprone, clang-analyzer) over
`src/` and `app/estate.cpp` -- nothing real. The ones that look like it
and are not: a division by a sample rate of 0 (`source.cpp`, unreachable:
`swr_init` refuses that rate and `append` returns first), null `m_loop`
and `m_title` (always made in the constructor), a `!self` checked twice
(the second in a later callback), a frame size cast after a 16 MB cap.

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
  swelling tone draws its diamond. A host's bytes are read for it too,
  where the host allows a second read (embed.h).
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
- **mplayer's command line, 2026-11-13** (sec 1): one dash and a word,
  as the hands type them -- `-ss` (seconds, m:ss or h:mm:ss, a fraction
  on the last), `-fs`, `-sub a.srt,b.srt` (offered before the files
  beside the film), `-volume` (the level, per cent) and `-speed` (the
  tempo); `--ss` and the long forms work too. The start, the full
  screen and the subtitles are the first file's, a launch of several
  being a train started at its head; the level and the speed every
  file's. A start that is no time is refused (exit 2) rather than read
  as 0. Full screen asked for before the picture is found -- `-fs`, or
  f pressed while it opens -- is shown once it is. Tested: the three
  forms of `-ss` land exactly, paused, with the level and tempo
  (`mplayers_options_say_where_and_how_it_plays`), and an early full
  screen arrives with the picture; each fails with its line taken out.
  **Chapters** (`snurra_read_chapters`, 2026-11-13, for hydra's saved
  mixes): a local file's chapters -- a mix's tracklist in Matroska or
  MP4, "Artist - Title" -- are marked on the waveform from the bottom
  edge, the one playing is said after the file's name in the title, and
  mplayer's ! and @ step to the previous and next one's start (before
  the first, the track's start), through the group as any seek
  (`a_mixs_chapters_are_its_tracklist`). A URL's and a host's bytes are
  not opened a second time for them. **A cue sheet beside the file**
  (its name with .cue, as hydra writes one) is its chapters where it has
  none of its own: each track's INDEX 01 at 75 frames a second, INDEX 00
  passed over, titled "PERFORMER - TITLE" with the sheet's performer
  where a track has none; a sheet of several FILEs gives each its own,
  and of one FILE gives it whatever it names, the file renamed since
  (`a_cue_sheet_beside_a_file_is_its_tracklist`).
  **Playlists** (`snurra_playlist`): an `.m3u`, `.m3u8` or `.pls`, named
  as a file or given with `-playlist`, is the files it names in its
  place in the train; dropped, the same. Paths are taken from the
  playlist's folder; a playlist inside one is left out, so none loops;
  at most 1000 entries (assumed). The same file straight after
  itself is one entry: hydra writes a saved mix's .m3u as the mix once a
  track, each with VLC's start-time, which had played as the whole mix
  once a track; its tracks come from its chapters or its .cue. Tested by reading both kinds, a launch
  of both and a drop (`a_playlist_is_a_train_of_what_it_names`). `-loop N`
  plays the group N times in all, from the timeline's start each time it
  ends of itself -- `-loop 0` for ever, as mplayer's; a pause, or the
  playing tile closed, is not an end (`loop_plays_again_from_the_start`).
  It found a deck's end still reported after a seek asked and not yet
  rendered, so a play asked at the end was stopped by the tile's next
  tick; `ended()` now waits for the seek, as `last_rendered` does. Played
  at its end -- every lane past its last tile -- a group plays again from
  the start, as Winamp's play after the end does, where it played
  nothing. `-shuffle` (2026-11-20, as `mplayer -shuffle *.mp3` is
  typed) puts the train in a random order once, playlists' files among
  the rest; `-loop` plays that order again rather than a new one, which
  mplayer would shuffle afresh -- not yet asked for
  (`shuffle_plays_the_files_in_another_order`, eight names twice; with
  the shuffle out it fails).
- **mplayer's everyday options, 2026-11-22**: snurra refused any option
  it did not know, so `snurra -really-quiet x` said "Unknown option" and
  played nothing -- a habit or a script typing mplayer's options failed
  outright. Now `-nosound` (and `-mute`) start every tile
  muted, its level kept for M; `-novideo` keeps the picture closed;
  `-endpos` ends the player that long after `-ss`, as mplayer's does
  (the first file's, as `-ss` is); `-ontop` keeps the window above;
  `-geometry` takes `+x+y`, `x:y` or `WxH+x+y` (a minus from the right or
  bottom of the primary screen). `-quiet`, `-really-quiet`, `-nolirc`,
  `-noconsolecontrols`, `-nojoystick`, `-nomouseinput` and `-fixed-vo`
  are taken and do nothing: snurra has no console chatter, remote or
  joystick of mplayer's to quiet. Options with a value whose meaning
  here would be a guess -- `-vo`, `-ao`, `-af`, `-vf`, `-cache` -- are
  still refused, which says so rather than playing something else.
  `-aid`, `-alang` (a film's audio track) and `-identify` are refused
  too: not wanted (the holder, 2026-11-22, of `-aid` and `-alang`)
  (`mplayers_everyday_options_are_taken`: muted, placed at +50+60, and
  `-ss 1 -endpos 1` ending by itself after at least 0.8 s; with the mute
  or the end taken out it fails).
- **mplayer's "-", 2026-11-22**: `curl ... | snurra -` plays what
  arrives on standard input, as mplayer's does -- bytes still arriving,
  the library's own case (sec 1): a thread of the player reads the input
  into memory and keeps it, so a seek back finds it; the size is known
  when the pipe closes; one reader however many sources open "-" (the
  waveform's second read too). "-" always means the input, never a file
  of that name. In the player only (a resolver chained after the
  estate's, `snurra_estate_install(other)`), not the library: a host's
  standard input is not a media source. What it costs: the whole input
  is kept in memory, as a seek back needs; and only this process has
  this input, so such a tile handed to another window plays silence
  there. Analysis is never kept for it (the store takes files only), so
  one stream's beat is never given to the next. Tested in
  `standard_input_plays_as_mplayers_dash`: a 2 s WAV piped in two halves
  0.3 s apart is 96000 frames heard at its level; a stall read as the
  end, it is none.
- **mplayer's . , 2026-11-13**: paused, the picture's next frame -- the
  group moved to where it begins (from the frames decoded ahead, or one
  frame's length on where none is yet), a playing tile paused first;
  nothing for a track with no picture. The seek lands 1 ms into the
  frame, since a position rounded through frames of sound and back
  could otherwise show the one before. Tested against ffprobe's frame
  times in an x264 file with B-frames, five steps each exactly the next
  (`a_period_steps_one_frame_on_paused`); a step that returns where it
  was fails it.
- **mplayer's + and -, 2026-11-13**: the sound's delay against the
  picture by 0.1 s, as mplayer's -delay (positive: the sound later). The
  picture moves, not the sound: the sound is the clock, and in a mix the
  other tracks are in step with it. Subtitles go with the picture, and .
  steps the picture's frames. Tested paused: five + show the frame half a
  second after what is heard with the sound unmoved, five - bring it back
  (`plus_and_minus_move_the_picture_against_the_sound`); a picture that
  ignores the delay fails it.
- **mplayer's < and >, 2026-11-13**: the playlist's previous and next
  file, which here is the train's -- the lane's previous or next tile,
  from its start, the group with it (a seek of the group to where that
  tile begins). The one now is the lane's playing tile, else the one the
  keys are on; > past the last does nothing, < at the first goes to its
  start. Tested with a train of three
  (`less_and_greater_step_along_the_train`); a step that stays put fails
  it.
- **Full screen takes the player's keys, and mplayer's o, 2026-11-13.**
  The full-screen picture is a window of its own, and took only Escape:
  space, the arrows, f and every other key did nothing once a film
  filled the screen, which is where mplayer is mostly driven from. Every
  other key now goes to the player's window as if pressed there, on that
  tile. o holds the time over the picture, h:mm:ss / h:mm:ss at its top
  right, until pressed again. Tested in full screen: space pauses, o
  shows and clears the time, f comes back
  (`the_full_screen_picture_takes_the_players_keys`); without the
  forwarding, space fails it.
- **The pointer leaves the full-screen film, 2026-11-13**: after a second
  still, as mplayer's does (its default delay), back when moved. Tested
  with the full-screen keys; without it the blank pointer never comes.
- **A film keeps the screen awake, 2026-11-13**, as mplayer's heartbeat
  does: while a tile's picture plays where it can be seen (the drawer
  open, or full screen), each ten seconds the X server's idle time is
  reset (ForceScreenSaver, Reset). That is what the server's own saver
  and DPMS count from and what lockers read -- TDE's kdesktop, which the
  holder runs, and xautolock -- so it holds whatever the desktop, where
  a D-Bus inhibit (org.freedesktop.ScreenSaver) holds only on desktops
  serving one. libxcb is resolved at run time on Qt's own connection,
  so no consumer builds against it. Tested on Xvfb by the idle time a
  saver reads (libXss, resolved by the test too): 1850 ms twelve seconds
  into a film, 12694 ms with the reset taken out
  (`a_film_playing_keeps_the_screen_awake`). **On Wayland, 2026-11-20,
  through the portal**: where there is no X server, the tile holds the
  desktop portal's Inhibit (idle) while its film plays where it is seen,
  and closes it on a pause, a closed drawer or the tile's end -- one
  request, not one per tick; an answer arriving after it stopped being
  wanted is given straight back. Only where `DBUS_SESSION_BUS_ADDRESS`
  is set (asked with none, Qt autolaunches a dbus-daemon nothing reaps,
  qtkit's finding). QtDBus is linked on a Linux desktop (snurra.pri,
  snurra.mk), which QtGui already links there; not on Android. Tested on
  a dbus-daemon the test starts and ends, with a stand-in portal: asked
  once with idle's flag, closed on pause, asked again on play, closed
  when the tile goes (`a_film_holds_the_portals_inhibit_while_it_plays`);
  with the asking or the closing taken out it fails. The suite unsets
  the session bus for every other test, so a run offscreen on the
  holder's desktop never holds the real screen. Not yet tried on a real
  compositor (weston waits on the holder).
- **mplayer's wheel, 2026-11-13**: over the picture, in its drawer or
  full screen, a notch up is the small jump forward and down back -- the
  arrows' step (sec 4), where mplayer's is its 10 s. Whole notches,
  however a touchpad divides them. Over the waveform the wheel is left
  alone. Tested in full screen: half a notch does nothing, a whole one
  jumps.
- **The suite caps the files it writes at 256 MB, 2026-11-13**
  (RLIMIT_FSIZE, set in the test binary's main, so a run by hand has it
  too). A sabotaged run -- the URL interrupts taken out -- hung closing
  its window while printing, outlived `timeout 30`'s SIGTERM, and wrote
  4.3 GB into its log at about 23 MB/s, filling / for every session on
  the machine until another session reported it. Sabotage runs of a
  hang are now also run under `timeout -s KILL`.
- **URLs, as mplayer plays them, 2026-11-13**: a name with a scheme and
  "://" (http, https, rtsp ...) goes to FFmpeg as it is, for the sound
  and the picture; it is not read again for a waveform or a beat grid --
  a radio stream has no end to read to, and a file would be fetched
  twice more -- nor for subtitles' preroll. Its network waits are given
  up for a close or a seek (FFmpeg's interrupt callback), in the opening
  and every read; a file's reads keep no interrupt, as before. Tested
  with an HTTP server in the test that sends the file whole, without
  ranges, so FFmpeg reads it as a stream: the sound plays, the picture
  opens, the length is the film's (`a_url_plays_with_its_picture`); and
  a server that never answers lets the window close in 99 ms
  (`a_url_that_never_answers_lets_the_window_close`) -- without the
  interrupts it never closed. A live stream has no length: the tile
  shows its three-minute stand-in, but plays on past it to the stream's
  own end -- measured 2026-11-13 with a live Matroska over HTTP under a
  1 s stand-in, heard to its 3 s end -- since a deck ends only where its
  source does and a train skips a tile of no known length. **Shown as
  live, 2026-11-20**: a tile whose source opened with no length of its
  own (`snurra_tile::live`, from the deck's new `opened()`) shows its
  time as "1:23 live", as mplayer shows a radio's, its position slider
  disabled, and takes no seek -- before, it read "of 3:00" and a seek
  asked went to the stand-in's place (60 s on, measured) though the
  stream played on where it was. Tested with a live Matroska served
  unsized, as a radio is (`a_live_stream_counts_up_and_does_not_seek`);
  with `live()` false, or its seek guard out, it fails. Served with a
  Content-Length but no ranges, FFmpeg takes the stream for seekable and
  the sound fails to open -- a server that sizes a body and ignores
  ranges, not a radio; not chased.
- **A film's sound from a file of its own, 2026-11-20** (mplayer's
  `-audiofile`, mpv's `--audio-file=`): a site serves a film as a
  picture stream and a sound stream, and hydra hands players both --
  for snurra it muxed them first, since snurra stopped on the option
  (hydra's `player_launcher.cpp`). The tile state carries `sound`
  beside `source`, through handovers too; the deck, the waveform and
  the beats read the sound, the picture its source. Given to the first
  file of a launch, as `-ss` and `-sub` are. Tested by a 3 s film with a
  5 s sound file -- the picture shown, the length the sound's
  (`a_films_sound_can_come_from_a_file_of_its_own`) -- and by both
  spellings arriving in the first tile's state over the wire
  (`audiofile_gives_the_first_file_its_sound`); with the deck on the
  source again, or the option dropped, each fails.
- **A tracklist from a file, 2026-11-20** (mpv's `--chapters-file`):
  hydra writes a streamed set's tracklist in FFmpeg's metadata format
  for a player that reads one, and snurra reads it with the chapter
  reader it already had (FFmpeg opens that format as any other). The
  list itself, not the file's name, goes in the first tile's state
  (`chapters`) and through handovers -- hydra overwrites its one file
  with the next set. Tested by a file of two chapters arriving in the
  first tile's state over the wire and a tile given them showing them
  (`a_chapters_file_gives_the_first_file_its_tracklist`); each half
  taken out fails it. hydra passes it to mpv only today
  (`takes_chapters`); told in its inbox.
- **A file manager can open audio with it, 2026-11-12** (sec 3b's launch):
  `snurra.desktop` declares the audio types FFmpeg decodes here and runs
  `snurra %F`, so several files clicked open as a train; `make install`
  puts the player and the entry under PREFIX (/usr/local) and DESTDIR,
  and `make uninstall` removes those two files by name. The entry passes
  `desktop-file-validate`; installed into a scratch DESTDIR, the player
  ran from there, and uninstall left no file. **Its own icon,
  2026-11-20**: `snurra.svg`, a record spinning, installed into hicolor
  as `snurra`, named by the entry and taken by every window from the
  theme (`QIcon::fromTheme`, the generic `multimedia-audio-player` where
  it is not installed); the desktop file name is set too, which is where
  Wayland looks for a window's icon.
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
  removed or renamed. Version 2 (2026-11-13, BeerSSH's proposal, inbox
  20261113T0139Z): `snurra_media::subtitles`, local paths of subtitle
  files the host fetched from beside a remote file, offered first as a
  `.srt` beside a local file is -- BeerSSH plays remote files as bytes,
  so a film's `.srt` on the host was never seen; empty changes nothing,
  and a brace-initialised `snurra_media` of three still compiles. A
  `.srt` beside `path` is offered after the host's, bytes or not (BeerSSH,
  inbox 20261113T0834Z: a local film given as bytes and path showed none,
  the tile looking beside the bytes' name). Version 4 (2026-11-14,
  fuzzypickles' ask, inbox 20261114T1128Z: its Library would host
  `snurra_player` in place of a bare deck, and keep its own words --
  "playing 1:02", "waiting for the rest to arrive" -- beside it):
  `position_ms`, `length_ms` (-1 until known) and `state_of` by the
  tile's place among those open -- `playing`, `paused`, `waiting` (asked
  to play, its last period put out nothing: the bytes stop there),
  `ended`, `none` for no such tile -- and a signal `ended(name)` once a
  tile plays to its end, so a host lets the fetch-backed bytes go as a
  close would; the tile stays, paused at the end. Getters for a host's
  poll rather than a signal per change, as the host asked for a poll;
  `a_player_says_where_each_tile_is_and_that_it_ended`, failing with the
  waiting branch or the emit taken out. **Hydra plays through
  snurra** (inbox 20261113T1442Z, a signal): listed beside mpv and
  mplayer when `snurra` is on PATH, called `snurra <url>` and nothing
  else -- the URL hydra's local proxy, which adds the CDN's Referer,
  cookies and User-Agent and answers ranges; an HLS or DASH stream is
  assembled first. **Measured 2026-11-14** (`an_hls_stream_plays_and_seeks`,
  a 6 s film cut by ffmpeg, served over HTTP): an HLS playlist plays
  through every segment, unbroken across the joins, and seeks exactly
  -- once fixed: the first AAC frame of a segment sought into has no
  timestamp, and placed anyway it played from the segment's start,
  2 s before a 4 s target; such a frame is dropped now and the next
  timed one placed (after a seek past the start, sixteen at most). A
  DASH manifest plays too, but FFmpeg 7.1's DASH demuxer loses some 70
  ms at a segment join: the ffmpeg program jumps at the same place and
  gives the same 285327 frames, while the audio's segments joined by
  hand are whole (288671). Upstream's; the test expects it and will say
  when it is mended. So hydra's assembling of DASH is still worth it. Seen playing such a URL at 12 s. Its saved mixes
  carry their tracklist as chapters in the file (Matroska for Opus, MP4
  for AAC, titles "Artist - Title"), a .cue beside it, or an .m3u with
  VLC's start- and stop-times, with title, artist, date, source and
  description tags -- snurra's to read or not. Its promises: the engine is inside (the process's
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
  **The widgets offered, set by the holder 2026-11-13:** the single
  player (`snurra_player`) and a simple DJ player, "something that
  reminds of a simplified traktor"; on a phone the same, "everything is
  obviously a static screen", with the file browser above or below
  covering most of it -- "let's see if they diverge". **`snurra_dj`
  (embed.h 3), step 1:** decks A and B side by side, each a tile with
  its controls, jumps and sync to the other; an equal-power crossfader
  under them (cos and sin, -3 dB each in the middle, as a train's
  crossfade); under that the library, whose tune double-clicked loads
  onto the deck that is empty, else the one not playing, else the one
  the crossfader has quieter, and dragged loads onto the deck let go
  on; a tune let go over nothing opens no player here. `load(deck,
  media)` for a host that browses its own, `set_browser_visible`.
  One layout -- decks above, browser below -- for the desktop and the
  phone until they are seen to need two. An embedded tile's jump is now
  seamless too, as a window's group's (0.0 ms off its step, 26 ms short
  before). `snurra --dj [a b]` opens it, its names as a tile takes them
  (`snurra_source_of`, 2026-11-18: made absolute, `tone:220` was a file
  under the current folder and the deck said it could not play it; seen
  in Xvfb, both tones load now). **A film on a deck starts with its
  picture closed** (2026-11-18), its button on the title opening it: a
  tile opens the picture with its controls, and a deck's are open, so a
  film's picture in a window of 700 cut the deck's controls off at the
  EQ (`a_dj_decks_film_starts_with_its_picture_closed`; without the
  deck's `set_picture_closed_at_first` it fails). Seen in Xvfb -- and at 412 x 915, a
  phone's portrait, the one layout holds: each deck half the width, the
  browser most of the screen, the tempo slider the first control
  squeezed. **Every button has its room there** (2026-11-13), checked
  in `the_dj_player_loads_two_decks_and_crosses_them` at 412 x 915 with
  a beat, so the jumps carry their widest words: the key lock, key knob,
  key sync and sync got a row of their own when the key's three, beside
  the tempo, clipped "in key" to 28 of its 47; and the jumps, clipped
  since before (with a beat, "4 bars", 44 of the 53 they ask), have
  the buttons' row with play and cue only -- the envelope's toggle went
  to the position's row, whose slider gives way -- arrow and step
  close-set, the controls' sides 2 px. A first fix (805231b) measured
  the jumps before the beat's words had been laid out and passed short
  of them; a check run again after the next change caught it. The
  player's least width (252) shows none of this -- a tile asks only 14
  characters, so a crowded row is clipped, not widened -- which is why
  the check is each button's own. **The browser there** gave the name
  38 of its 370: under 20 letters' room for it, Folder is hidden (the
  search still finds by it) and Artist takes a quarter of what BPM and
  Key leave, the name 204; wide again, both come back.
  **Seen on Android** (2026-11-13, an x86_64 build on the `bssh_fold`
  AVD, read-only, 1768 x 2208 at 420 dpi): unfolded, about 673 dp,
  both decks loaded and every row whole -- the key row, the toggle by
  the position, the key found (11B for an A and E, and for a lone 440
  Hz) -- and the browser with Key. **Not on a fold's cover screen**:
  at 884 wide, about 336 dp (the Fold3's cover is near 344), each deck
  has some 150 dp, and "in key" is elided, the EQ's third dial cut off
  and its names run into the dials; by the offscreen measure the
  buttons' row with a beat wants some 170. Two decks side by side
  cannot hold every control with its words there: stacking them takes
  the screen from the browser, which the holder wants most of; shorter
  faces ("4" for "4 bars") undo the holder's rule that a jump says its
  step. Whose: the holder's -- asked, not decided. The console's
  `fold` did not change this AVD's display; `wm size 884x2208` did. **On a phone it is the app** (2026-11-13): started with no
  file on Android, snurra opens the DJ player full screen and asks for
  the phone's music (READ_MEDIA_AUDIO and _VIDEO, declared in
  `android/AndroidManifest.xml` -- qmake's ANDROID_PERMISSIONS was left
  out by this kit's deploy -- and READ_EXTERNAL_STORAGE up to Android
  12), then browses all of shared storage, where downloaded music lies.
  Seen on the API 36 emulator against the released kit's FFmpeg 9.0, the
  first time snurra played against 9.0: both prompts, the two tunes put
  in Download listed, loaded by double-tap onto A and B, A heard through
  AAudio. Not yet on the holder's phone: the install failed with no
  reason given and the phone left USB (a Fold3, Android 15). **The cue** (every tile, so a window's too): a CDJ's and
  Traktor's -- paused, the cue button sets it where the track stands;
  playing, it goes back there and stops; marked on the waveform by a
  triangle from the top, kept by a handover (`cue_us`). A deck's level
  slider is its channel fader. **The EQ** (every tile): three bands
  of an isolator, as Mixxx's and a DJ mixer's (2026-11-13; shelves
  before, which stop at -24 dB, so no band could be killed) -- the track
  split at 250 Hz and 4 kHz by Linkwitz-Riley 8 crossovers, whose bands
  add back to an allpass, the low band put through the upper split's
  allpass so the three add up flat; each band weighted, the weights
  gliding over 10 ms, and the dry track crossfaded to the isolator and
  back in the same 10 ms, so at 0 dB everywhere an untouched deck stays
  exact. Down 2 dB a step to -22 and below that the band killed
  (`SNURRA_EQ_KILL`), up to +6 by 0.5; carried by a handover. **Drawn
  as one line** (the holder, 2026-11-18: "a line where segments of it
  can be pulled up or down, and where it can be drawn from one side to
  the other"; `snurra_eq_line`, in place of the three dials it had --
  the key shift keeps its knob): low, mid and high its three segments left to right,
  each as high as the band is loud, 0 dB on a dashed middle, the kill at
  the bottom, three lines of text high, as wide as the controls. Held,
  the segment under the pointer or finger goes to its height; moved
  across while held, each band it reaches takes the height it was
  reached at, and a band a quick stroke jumped over the next one's --
  so one stroke draws the whole curve. A double-click puts a band back,
  the wheel steps one. Found by looking at it in Xvfb: the first fill
  ran from the last band to this one inclusive, so leaving the mid for
  the high took the mid along to the high's height; the test now draws
  band by band and fails with that fill. **By touch too**, on the API 36
  emulator (2026-11-18, `adb shell input swipe`, touch events and not a
  mouse): a swipe up the key knob turned it three semitones, the key
  read 9A to 6A, and one swipe from the low band's bottom to the high
  band's top drew a rising line of three steps -- Qt handing a finger on
  as the left button, which both rely on.
  Fourth order was tried first and was not a kill: a killed high band
  left 8 kHz, an octave from its split, at -27 dB. Measured on tones
  through a deck: cut to -24, 60 Hz -24.0, 8 kHz -23.7, 1 kHz -24.0,
  the far bands 0.0; killed, 60 Hz -98.9, 1 kHz -91.1, 8 kHz -53.4; in
  but at unity, 150 Hz to 4 kHz within 0.001 dB
  (`the_eq_cuts_its_band_and_leaves_the_rest`), which the low band's
  allpass taken out (-0.11 dB at 250 Hz) and one wrong Q (-1.9 dB at
  150 Hz) each fail. Not yet: the phone's layout tried on a phone.
  **More bands, 2026-11-21** (the holder: "splittable into 3/5/7/12?
  Maybe automatically by dragging that part"): the isolator splits at any
  of a grid of eleven crossovers -- octaves from 32 Hz to 8 kHz, then 12
  and 16 kHz (`eq.h`) -- so a layout is a set of bits and three, five,
  seven and twelve bands nest (250 Hz and 4 kHz; then 125 Hz and 8 kHz;
  then 64 Hz and 1 kHz; then all). Split from the bottom, the bands taken
  so far go through each new crossover's allpass before the next band
  joins them, so any layout adds back flat; with the two splits it is the
  three-band isolator it was, figure for figure. A state keeps the layout
  and a dB for each of the grid's twelve narrowest bands (a band of
  several holds one value in each), streamed in place of the three; a
  DJ's low, mid and high (a controller's, `set_eq(low, mid, high)`) move
  every band starting in that part. A new layout is heard once the old
  has faded to the dry track, 10 ms, and the new faded in. The sections
  run in double: in float, twelve bands at unity came out 0.024 dB down
  at 64 Hz, the poles of a 32 Hz section too near 1. Tested in
  `the_eq_splits_into_more_bands_and_is_heard_as_drawn`: twelve bands at
  unity within 0.01 dB from 40 Hz to 12 kHz; five bands' sub killed, 60
  Hz -51 dB and 400 Hz untouched; three bands' mid killed then five
  bands' sub alone, 1 kHz back from -91 dB; and seven bands set unevenly,
  each tone measured within 0.1 dB of the isolator's computed response
  (`snurra_isolator::response`, what the line will draw). Without the
  allpass between splits, 40 Hz comes out 3 dB down and it fails.
  What it costs: the isolator's loop alone, timed here, takes 1.0 % of
  one core for a stereo deck at 48 kHz at three bands and 5.2 % at
  twelve (108 sections a channel, in double) -- measured with the
  machine at a load of 16 of 24 cores, so roughly. Heard only while a
  band is off 0 dB; a phone's core being some times slower, twelve bands
  on four decks there is worth measuring before it is offered there.
  **The line shows the slopes, 2026-11-21:** it draws what the isolator
  does to each frequency, 20 Hz to 20 kHz in octaves, from the isolator's
  own sections (`response`, a point every two pixels) -- so the slopes
  between bands are the crossovers', a killed band a notch as wide as it
  sounds, and a narrow band set low dips less than its setting, as it is
  heard. dB to height as the steps were (0.5 dB up, 2 dB down to -22),
  the last step stretched to -60 where the kill sits; a faint line at
  each split; lo, mid and hi named only at three bands. The bands are
  found by frequency under the pointer, as a controller learned on the
  line takes its low, mid or high (`snurra_eq_hz_at`). A double-click on
  a split joins the two bands there, the lower's height taken; the
  context menu splits into 3, 5, 7 or 12 bands, each new band taking the
  height its middle had. Tested in
  `the_eq_line_draws_its_slopes_and_splits` from the pixels: five bands,
  the sub killed, the line at the bottom at 45 Hz, on the middle at 1
  kHz, partway at 125 Hz and still above the bottom at 105 Hz (drawn as
  steps, at the bottom there, it fails); a press sets the band under it
  alone; joined at 125 Hz, four bands; the menu, twelve.
  **Split by drawing, 2026-11-21** (the holder: "maybe automatically by
  dragging that part"): a stroke that stays in one band and reaches both
  sides of its middle crossover of the grid at heights two steps or more
  apart -- a slope drawn inside one band -- splits the band there, each
  half the height it was drawn at last; drawing on, a half splits again
  the same way, down to the grid's twelve. Pulled straight up or down, a
  band stays whole, and the two-step margin keeps a hand's wobble from
  splitting one. Bands are joined again by a double-click on their split,
  or by the menu. Tested in the same test: three bands, a stroke in the
  mid from 400 Hz flat to 2.5 kHz at +4 dB splits it at 1 kHz, each half
  as drawn; on from 1.2 kHz to 3.5 kHz at -8 dB it splits at 2 kHz; a
  band pulled straight up stays one (with the split held back, three
  bands stay and it fails).
  **Where the bands are**, 2026-11-21: past three, each split is
  labelled small with its frequency (64, 125 ... 1k, 4k) where there is
  room, and hovered, the line says the band under the pointer -- "125 Hz
  to 250 Hz: +6.0 dB", "below 125 Hz: killed" -- above its usual help;
  tested from the tooltip (with the line's own tooltip only, it fails).
  **By touch, 2026-11-21**, on the API 36 emulator (x86_64, the release
  kit's FFmpeg 9.0 headers given as `FFMPEG_HEADERS`): one rising swipe
  inside the mid band, `adb shell input swipe`, split it at 1 kHz and
  again at 2 kHz, each part as drawn -- a finger handed on as the left
  button, as the line's other strokes. Found on the way: deck A loaded
  beside an empty deck B had a quarter of the phone's width, its controls
  elided and the EQ cut off after its low band, the grid's columns never
  stretched alike; now they are, and `decks_share_a_phones_width`
  checks it at 412 wide (unstretched, A 114 against B 270, it fails).
  **The knobs move their part, the three stay, flat bands go, 2026-11-21**
  (the holder: "ones that are close to zero and near together are merged
  together, but the 3 original bands remain always, and the controller
  mapped to them raises/lowers all the bands in that 1/3 part of the
  scale, so it continues working even though there are more bands"): a
  state holds the drawn shape and, beside it, the three knobs -- low, mid,
  high, `snurra_eq::part` -- each moving every band in its part by its dB;
  what is heard is the two added, no higher than +6, killed where either
  is (`level`). So a kill on a controller and back finds the shape as
  drawn. Drawing a band sets what it is heard at, the shape taking what
  the knob leaves; where a part's bands all stand alike the knob takes
  the height and the shape goes flat, so at three bands the knobs and the
  line are one, as before. The splits at 250 Hz and 4 kHz are in every
  layout and are never joined (a double-click there does nothing).
  Let go of a stroke -- or after a double-click -- neighbours heard within
  2 dB of 0 and 1 dB of each other, or within a quarter dB of each other
  at any height, join, the joined band their mean (`merge_near`): drawing
  a part back flat takes its bands away, as drawing a slope adds them.
  Rejected: the knob adding its change to every band's value directly --
  a kill and back would clamp the shape away. Tested in the same test:
  a mid of three bands killed by its knob and back, the shape as it was,
  and up 2 dB, each band up 2; a band drawn to 0 beside one at 0 joins on
  release and not while held; the mid then one band, its knob the height;
  a double-click at 250 Hz leaves four bands. Broken one at a time -- the
  knob not added, no merging, the 250 Hz split joinable -- each fails.
  The handover carries the knobs (`handover_test`).
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
- **A jump while playing lands exactly its step on** (2026-11-13, the
  holder: "the jump 4 bars isn't transparent"): a seek while the group
  plays is switched a lead ahead -- the output's latency and 50 ms --
  with the old place playing on until then, but it took the place asked
  as of the moment asked, so every jump landed that lead short, off the
  beat: 60 ms here. The time is now carried to the switch
  (`start_group_at(time, moving)`); measured 0.0 ms off a 10 s step
  (`a_jump_while_playing_lands_exactly_its_step_on`).
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
  third of it lost) each fail. **Beat-snapped, 2026-11-13 (step 6):**
  where both tracks have a beat grid, the overlap is the first's whole
  bars nearest 5 s, moved by less than a beat so the next one's first
  beat falls on a beat of the first -- a record brought in on the beat,
  at either's tempo, within half the shorter. One function
  (`snurra_train_overlap`) for the train as it plays and for every seek
  and lane time, so they agree. Tempos are not matched for it: sync is
  the DJ's (sec 4), and two tempos apart drift from in phase through the
  overlap. Tested on grids alone, four cases including tempos and a
  short next, to under a frame (`a_trains_overlap_brings_the_next_in_on_the_beat`);
  whole bars without the phase fail it. Not yet heard on real music.
- **Tiles along the timeline, 2026-11-18** (the holder: "the
  tracklengths probably need to be proportional for mixing (without going
  off screen)"): a window's grid columns are the spans between every
  tile's start and end on the group's timeline, each stretched by its
  time, and a tile with its drawers spans its own -- from where its lane
  starts it to where the next one comes in, its whole length if it is
  last. So every lane is on one scale, a lane begun later is drawn
  further right, and a train's join is drawn where the next track comes
  in, on the beat its overlap snapped to. A tile keeps ten letters'
  width, and the one with the controls theirs, so a 3:39 tune beside an
  hour's set (seen in Xvfb) is narrow, not gone. The window grows to the
  width at which the tile asking most for its time has its least -- never
  narrower than it was, never wider than the screen, and moved back onto
  it, since one opened at the pointer and grown ran off the right edge.
  Laid out again, once the event loop is back, when a length becomes
  known, a tempo moves, beats are found or a lane's begin is set -- not
  while a lane is being dragged, whose waveform is under the pointer
  (`tiles_are_as_wide_as_their_time_on_the_timeline`, failing with every
  column stretched alike). **A width the person set is kept** (the
  holder, 2026-11-18: "keep my width if I set it"): a resize to another
  width that is not one of the window's own -- the grip, a window manager
  -- marks it, and from then on neither the timeline's widening, a tile
  added nor one closed changes it; the tiles fill it at its scale. Left to
  itself, a window with a tile closed fits the timeline again: narrowed
  to the size hint, as before the tiles went along it, it was their least
  widths -- 146 px where 759 had fitted
  (`a_width_the_person_set_is_kept`, failing both ways).
- **Drawers, built 2026-11-12** (sec 3b): a tile has an upward drawer
  (empty until video) and a downward one, its controls; one height for
  every waveform. A window takes the drawers out of its tiles and gives
  each lane three grid rows -- upward drawers, tiles, downward drawers --
  and re-fits its height (never its width, which a person may have set)
  as a drawer opens or closes; a tile alone, embedded, keeps its drawers
  in itself. The look: `drawers` makes the window the shape of its tiles and
  open drawers; `square` paints the same layout opaque; `drawers` by
  default on Wayland and X11, `square` offscreen; `--layout` /
  $SNURRA_LAYOUT decides outright. Since 2026-11-13 (the holder: "we
  should not have a large square window around the players", on X11
  with no compositor, where the look had been square) the window is cut
  to each tile and open drawer, 2 px round them so the frame between
  tiles still moves it, and the corner grip (`reshape`, a window mask:
  X11's shape needs no compositor); seen through as well only where a
  compositor draws behind it (Wayland, or X11's `_NET_WM_CM_S0` owned).
  Seen in a bare Xvfb: A with its controls out beside B, and nothing in
  the corner under B. The shape also leaves the empty corners' clicks to
  what is behind. Under xcb it broke drops (the drag-and-drop handover
  test): it was cut before the layout had placed the tiles, and is now
  cut again after every layout and on show; the size grip had a row of
  its own under the tiles, cut away, where a drop by the bottom edge
  landed, and now sits over the bottom right corner; and a window that
  lost a tile kept its width, a cut-away stretch where drops missed, and
  now narrows to what is left.
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
  above stands. The tile: a tempo slider of +-16 % in tenths
  (the holder, 2026-11-13: 12 to 16, "8 % was never enough"; Mixxx
  offers 16), snapping to the middle when dragged within 0.5 % of it (a
  double-click resets it; key lock off, so pitch moves with the speed,
  by default) and a key button, carried by a handover. In a
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
  tested by key presses through X. **The key shift** (2026-11-13, a
  DJ's key knob, with the key now shown -- sec 3b, the library): a dial
  of semitones, an octave either way, 0 at the top and back on a
  double-click, carried by a handover. **Turned by holding it and moving
  up or down** (the holder, 2026-11-18: "a knob should be held L mouse
  button or touch and then up/down"), a step every half line of text,
  wherever the press began -- `snurra_knob`, a `QDial` drawn as before.
  It had been Qt's stock `QDial`, taken with the EQ (a59ddff) and this
  knob (484de7c) as the nearest thing to a mixer's: it follows the
  pointer's angle round its centre, so at two lines across a few pixels
  span the range, a press alone jumps it (to -7 from the left edge, the
  test's sabotage), and a finger has no gesture for it. The EQ, which
  had three, became a line the same day; on top of the tempo's pitch
  without key lock, alone with it. WSOLA at the tempo over the pitch
  heard keeps the pitch, then its frames are read by the same cubic at
  the pitch heard a frame, which raises it and gives the tempo back; at
  a pitch heard of 1 the two plain ways run as before, so an unshifted
  deck is exactly what it was. Measured on the 440 Hz tone: key lock at
  1.2, +7, 659.24 Hz (659.26 asked); no key lock at 1.1, -7, 323.03 Hz;
  key lock at 1, +12, 880.00 Hz; the track at its tempo and the level
  within 0.22 dB in each (`key_lock_keeps_the_pitch_as_the_tempo_moves`).
  The resampler stepping a frame at a time instead leaves the pitch at
  440 and fails it. The tile's key reads as heard, shift and all.
  **A beat loop** (2026-11-13, Traktor's loop, by the position: "loop"):
  the jump's step -- 4 bars with a beat -- from the beat at or before
  where it is heard, shown as a band on the waveform; again, it plays
  on. Each way back is asked of the deck for the moment the loop's end
  will be heard, the old place on until then (`start_at`), once a pass
  when that moment is within a seek's lead and a tick; asked late, it
  lands as far past the start as it was past the end, so the beat
  holds. A tile on its own only, as a DJ player's deck: in a window a
  seek is the group's and would move every lane, so the button is off
  there. Measured at 240 BPM (4 bars, 4 s) over 10 s: never past the
  end, twice round, 0.0 ms off an unbroken count round the loop
  (`a_beat_loop_plays_its_bars_again_seamlessly`); switched a lead
  early instead, it plays from before the start and fails -- the count
  alone still agreed, the phase being kept, so the test checks the
  bounds too. **A deck's report of a timed seek's place** (closed 2026-11-14): asked
  and not yet taken, the target was reported held; taken, the old place
  until its moment -- so for some 100 ms before a jump or a loop's way
  back the time shown read the target, the old place, then the target.
  `position_at` now reports what is heard, the old place carried along
  until the moment, and `planned_at` where the deck is going, which the
  tile's jumps plan from, so a second jump still goes on from the first
  (`a_timed_seek_is_reported_where_it_is_heard`: before the moment no
  step back and never the target, after it 0.0 ms off; the held report
  read 74 wrong).
  **Key sync** beside the knob ("in key", Traktor's): the shift that
  puts this tile's key on the leader's Camelot number -- relative major
  and minor being one, as they mix -- the smallest way, the tempo's own
  pitch counted where key lock is off and the leader's shift counted
  too; the leader as for Sync, the first other tile playing with a key,
  or in the DJ player the other deck
  (`key_sync_moves_a_tile_into_the_leaders_key`, which the tempo's pitch
  left out fails).
  **Sync** (2026-11-12): a tile's button sets
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
  a two-second notice says which; `z` and `x` show them 0.1 s earlier
  or later, for a file beside the picture that is out of step (per
  tile, not kept). Drawn white on a black outline,
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
  what it has or more than a second past it, and decodes from the key
  frame to the time playing when each frame comes out, not the time
  asked when the seek began -- that was behind the sound by the time it
  got there, 200 to 300 ms after a 9 s skip into a 10 s group of
  pictures, and then every frame since came out as fast as it decoded:
  the jitter the holder saw at each skip (2026-11-13). Now the first
  picture after the skip is within two frames of the sound
  (`a_skip_shows_the_frame_heard_not_one_behind`). That made a reader
  on its way follow its target as the clock moves -- and so never seek
  again while on its way: a jump then, or a film started partway in, was
  decoded from where it had been, 7 s of 4K frames (the holder:
  "jumping takes a long time to load video"). A target past where the
  clock alone takes it by more than a second is a new seek
  (`a_far_target_while_on_the_way_is_a_new_seek`: 281 frames decoded,
  1362 without). And a seek lands on an indexed key frame, of which a
  file may have few, so the reader reads on to the target without
  decoding, keeping the packets from the last key frame on, and decodes
  from there. The 4K film's 7.4 s jump: 6.65 s to a picture before, about
  1 s now; the h264 set's under 0.3 s. The tile asks for the
  frame at its deck's heard position every 16 ms while the drawer (or
  the full-screen picture) shows. The drawer is the tile's width and the
  picture's shape tall; it opens with the controls, so on the first
  tile only, and the window grows up so the tiles stay still on screen;
  a full-screen button shows the picture full screen, Escape or a
  double-click brings it back. The button is YouTube's and most
  players' now (the holder, 2026-11-13): in the picture's bottom right
  corner while the pointer moves over it, gone two seconds after it
  stops or leaves; full screen the same button leaves, and goes with
  the pointer after a second still. Pictures are converted to RGB here, not
  by libswscale, which this machine had no headers for and a tiny
  player need not link -- in whole numbers from tables made once a
  frame since 2026-11-13, when the holder found video "very poor": in
  doubles with a 64-bit divide a pixel, a 1080p 30 fps film showed 13
  to 18 frames a second, and now its 30, at a load average of 29
  (measured offscreen; the holder's "worse in the small window" was not
  seen there, and the picture is now opaque, so the drawer and window
  behind it are not painted again for each frame). A picture is
  made as wide as it is shown, in device pixels -- the picture fitted in
  the drawer or on the screen, never wider than the film -- where it was
  1280 wide always: small, it costs little, and full screen on a screen
  wider than 1280 it is sharp, not 720p stretched
  (`a_picture_is_made_as_wide_as_it_is_shown`). Fitted, not the view's
  width (the holder, 2026-11-13, full screen still poor on a 3440 x 1440
  screen): a 4K film was made 3440 wide where it fills 2560, 13 frames a
  second of 25 and 11.7 ms a paint scaling it down; made 2560 it keeps
  up, drawn one to one in 5.6 ms. Shown at half the film's width or
  less, each pixel is the mean of the samples it covers, luma and chroma
  (the holder: the small view "could do with some anti-aliasing");
  one-pixel stripes come out grey, not one stripe picked
  (`a_picture_made_small_is_averaged_not_picked`), and the 4K film keeps
  its 25 a second in a drawer 208 wide -- and at 1280 wide only once its
  divides went (three a pixel had held it to 16 a second). Colour is tested
  against ffmpeg's own RGB, BT.601 and BT.709, limited and full range,
  to 4 steps (`colours_are_ffmpegs_own`). It converts: 8-bit planar 4:2:0, 4:2:2 and 4:4:4 and
  NV12/NV21, and since 2026-11-12 their 10-bit kin (planar 10-bit,
  P010) that HEVC and AV1 releases are mostly in, BT.601/709/2020,
  limited or full range; and packed RGB in any byte order, a palette
  and grey. Anything else shows no picture yet. H.265 and AV1 at 10
  bits, QuickTime Animation and PNG in RGB, a paletted and a grey PNG
  and a GIF (FFmpeg decodes it to BGRA, not its palette) pass the frame
  timing test below; without its case, each of the new layouts fails.
  Pixels that are not square (a DVD's 720 wide, anamorphic releases)
  are shown at the shape meant, from the frame's sample aspect ratio
  (2026-11-13); a clip stored sideways with a display matrix -- phones
  record upright video so -- is turned by the nearest quarter, the way
  ffmpeg's own output turns it, which the test reads back as its
  reference; the drawer takes the shape shown.
  A film's sound tracks (its languages) are listed by language and
  title, FFmpeg's best playing first; mplayer's `#` takes the next, and
  the source opens its decoder on that stream at the deck's next seek,
  which the tile makes at once to where it is -- so the change crosses
  over as a seek does, and lands on the exact frame (2026-11-13).
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
over SSH as the other trees are. Settled since: audio first, then video
as a drawer (sec 3); beat analysis by Mixxx's method for now, licensing
once the program works (the holder, 2026-11-12). Open, the holder's: an
FFmpeg of snurra's own for Android (sec 3).

Next: BeerSSH and fuzzypickles can now vendor snurra and hand it their
bytes -- their sessions' to take up, signalled in their trees (inbox
20261112T0229Z); the drag and drop tried on a real Wayland compositor;
AAudio's low latency measured on a phone, and its reopen after a route
change heard there (sec 4); a handover between an ALSA output and a hw: one (two devices, two clocks
-- drift, not only latency, then matters).
