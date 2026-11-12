# snurra for a qmake consumer: include(path/to/snurra/snurra.pri) and the
# player's library -- engine, decks, sources, outputs, the tile -- is
# compiled into the application, moc included (project.md sec 2).
#
# Compiled in rather than linked, as softkeys is: a consumer builds it with
# its own flags and its own Qt anyway, and qmake runs moc on the headers
# listed here without a second project to keep in step. Headers are
# included as "snurra/deck.h", so none of snurra's names (wire.h, clock.h)
# can be taken for a consumer's own.
#
# A consumer that plays from its own transport implements snurra_bytes
# (snurra/source.h) and hands a deck its bytes.

SNURRA_ROOT = $$PWD

QT += widgets network
CONFIG += c++17 link_pkgconfig
INCLUDEPATH += $$SNURRA_ROOT/include

# Decoding: FFmpeg's libraries (project.md sec 3, 3a).
PKGCONFIG += libavformat libavcodec libswresample libavutil

SOURCES += \
    $$SNURRA_ROOT/src/deck.cpp \
    $$SNURRA_ROOT/src/engine.cpp \
    $$SNURRA_ROOT/src/marker.cpp \
    $$SNURRA_ROOT/src/node.cpp \
    $$SNURRA_ROOT/src/output.cpp \
    $$SNURRA_ROOT/src/snap.cpp \
    $$SNURRA_ROOT/src/source.cpp \
    $$SNURRA_ROOT/src/tile.cpp \
    $$SNURRA_ROOT/src/wire.cpp

HEADERS += \
    $$SNURRA_ROOT/include/snurra/clock.h \
    $$SNURRA_ROOT/include/snurra/deck.h \
    $$SNURRA_ROOT/include/snurra/engine.h \
    $$SNURRA_ROOT/include/snurra/marker.h \
    $$SNURRA_ROOT/include/snurra/node.h \
    $$SNURRA_ROOT/include/snurra/output.h \
    $$SNURRA_ROOT/include/snurra/snap.h \
    $$SNURRA_ROOT/include/snurra/source.h \
    $$SNURRA_ROOT/include/snurra/spsc.h \
    $$SNURRA_ROOT/include/snurra/tile.h \
    $$SNURRA_ROOT/include/snurra/tile_state.h \
    $$SNURRA_ROOT/include/snurra/wire.h

# Output: ALSA on Linux; PortAudio, AAudio and QAudioSink are to come.
linux:!android {
    SOURCES += $$SNURRA_ROOT/src/output_alsa.cpp
    PKGCONFIG += alsa
}
