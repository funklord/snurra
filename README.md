# snurra

A media player is rarely small. mplayer was close -- a command line and
a window -- and the players that replaced it grew into suites. snurra is
meant to be what mplayer was for the desktop, with a graphical face and
a little mixing: play a file, play two and set their levels, move from
one to the next, and stay out of the way.

It is also a library. Programs that need to play something -- a terminal
showing a file on an SSH host, a file-sharing tool playing what is still
arriving -- link snurra rather than handing the file to another program.
Its player reads from a byte source that may not be complete yet, pauses
cleanly while bytes are missing, and offers to seek only where seeking
cannot stall: the part every network-fed player gets wrong.

How it differs from its neighbours:

- **mplayer, mpv, ffplay** play a file or a URL; snurra plays from a
  source the embedding program supplies, read at an offset, so a file on
  an SSH connection or a peer-to-peer swarm plays without a server or a
  temporary copy.
- **VLC** embeds, but brings its whole stack; snurra is a Qt Widgets
  library sized for embedding in small programs, Android included.
- **Mixing** -- more than one stream and their levels -- is part of the
  player, not a separate DAW.

Not there yet: video, the train's and the stack's own mixing, Wayland's
merge by drag and drop, and outputs beyond ALSA; `project.md` holds the
plan and the state.

## Building

Needs Qt 6 (Widgets, Network), FFmpeg's libraries and, on Linux, ALSA --
on Debian `qt6-base-dev libavformat-dev libavcodec-dev libswresample-dev
libasound2-dev`.

    make            # the player, in build/
    fmake

    make test       # the handover tests

    make style      # the style gate

## Copyright

Copyright (C) 2026 Nabeel Sowan <nabeel@vibes.se>
