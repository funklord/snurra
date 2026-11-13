# snurra

A media player is rarely small. mplayer was close -- a command line and
a window -- and the players that replaced it grew into suites. snurra is
meant to be what mplayer was for the desktop, with a graphical face and
a DJ's mixing: play a file, its picture and subtitles in a drawer above it; play
several, side by side as one mix or stacked to sound at once, each with
its level, its drawn volume curve, its tempo -- with key lock -- and
jumps of whole bars where it has a beat; and stay out of the way. Each
file is a small tile that opens where it was clicked; tiles snap
together into one window, and pull apart again, without a gap in the
sound.

It is also a library. Programs that need to play something -- a terminal
showing a file on an SSH host, a file-sharing tool playing what is still
arriving -- link snurra rather than handing the file to another program.
A program includes one header, `snurra/embed.h`, which only ever grows,
and hands snurra its bytes. Its player reads from a byte source that
may not be complete yet: where the bytes stop it holds, silent, and
goes on from the very next sample when they come; a seek into what has not arrived waits there and tells
the transport what to fetch first. That is the part every network-fed
player gets wrong.

And it keeps time to the sample, because mixing is the point: a seek
lands on the frame playing straight through would reach, whatever the
codec; two windows snapped together hand their sound across in a
crossfade with no gap and no doubling; and tiles side by side play as
one mix, each starting on the exact frame the last one's overlap
begins.

How it differs from its neighbours:

- **mplayer, mpv, ffplay** play a file or a URL; snurra plays from a
  source the embedding program supplies, read at an offset, so a file on
  an SSH connection or a peer-to-peer swarm plays without a server or a
  temporary copy.
- **VLC** embeds, but brings its whole stack; snurra is a Qt Widgets
  library sized for embedding in small programs, Android included.
- **Mixxx** is a DJ program of fixed decks in one big window; snurra's
  decks are the tiles themselves, as many as are open, each a file you
  can see, with the same tools -- tempo, key lock, beat-sized jumps,
  levels -- and the player stays small when you only want to listen.
- **Mixing** -- more than one stream and their levels -- is part of the
  player, not a separate DAW.

Not there yet: matching one tile's beat to another's (tempo and phase
by themselves), and finding a track's key; `project.md` holds the plan
and the state.

## Building

Needs Qt 6 (Widgets, Network), FFmpeg's libraries and, on Linux, ALSA --
on Debian `qt6-base-dev libavformat-dev libavcodec-dev libswresample-dev
libasound2-dev libxcb1-dev`.

    make            # the player, in build/
    fmake

    make test       # every suite: timing, codecs, the deck, video, beats (some need ffmpeg)

    make style      # the style gate

## Copyright

Copyright (C) 2026 Nabeel Sowan <nabeel@vibes.se>
