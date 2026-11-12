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

# Decoding: FFmpeg's libraries (project.md sec 3, 3a). On Android, the
# ones Qt's kit ships for its own multimedia -- FFmpeg 7.1, without
# headers -- compiled against 7.1's headers from SNURRA_FFMPEG_INCLUDE,
# which snurra's Makefile stages for its own Android build.
android {
    isEmpty(SNURRA_FFMPEG_INCLUDE): error("snurra: set SNURRA_FFMPEG_INCLUDE to FFmpeg 7.1's headers")
    INCLUDEPATH += $$SNURRA_FFMPEG_INCLUDE
    LIBS += -L$$[QT_INSTALL_LIBS] -lavformat -lavcodec -lswresample -lavutil
    # Output: AAudio, Android's own (API 26, the floor here).
    SOURCES += $$SNURRA_ROOT/src/output_aaudio.cpp
    LIBS += -laaudio
    DEFINES += SNURRA_AAUDIO=1
} else {
    PKGCONFIG += libavformat libavcodec libswresample libavutil
}

SOURCES += \
    $$SNURRA_ROOT/src/beats.cpp \
    $$SNURRA_ROOT/src/deck.cpp \
    $$SNURRA_ROOT/src/embed.cpp \
    $$SNURRA_ROOT/src/engine.cpp \
    $$SNURRA_ROOT/src/marker.cpp \
    $$SNURRA_ROOT/src/node.cpp \
    $$SNURRA_ROOT/src/output.cpp \
    $$SNURRA_ROOT/src/peaks.cpp \
    $$SNURRA_ROOT/src/snap.cpp \
    $$SNURRA_ROOT/src/source.cpp \
    $$SNURRA_ROOT/src/tile.cpp \
    $$SNURRA_ROOT/src/video.cpp \
    $$SNURRA_ROOT/src/wire.cpp

HEADERS += \
    $$SNURRA_ROOT/include/snurra/beats.h \
    $$SNURRA_ROOT/include/snurra/clock.h \
    $$SNURRA_ROOT/include/snurra/deck.h \
    $$SNURRA_ROOT/include/snurra/embed.h \
    $$SNURRA_ROOT/include/snurra/engine.h \
    $$SNURRA_ROOT/include/snurra/marker.h \
    $$SNURRA_ROOT/include/snurra/node.h \
    $$SNURRA_ROOT/include/snurra/output.h \
    $$SNURRA_ROOT/include/snurra/peaks.h \
    $$SNURRA_ROOT/include/snurra/snap.h \
    $$SNURRA_ROOT/include/snurra/source.h \
    $$SNURRA_ROOT/include/snurra/spsc.h \
    $$SNURRA_ROOT/include/snurra/tile.h \
    $$SNURRA_ROOT/include/snurra/tile_state.h \
    $$SNURRA_ROOT/include/snurra/video.h \
    $$SNURRA_ROOT/include/snurra/wire.h

# Outputs: ALSA on Linux, AAudio on Android (above); QAudioSink wherever
# Qt Multimedia is. Leaving it out on Android saves nothing: the kit's
# FFmpeg links Qt's FFmpeg stub, which links Qt Multimedia, which brings
# Qt Quick, QML and PDF -- 27 MB of an 80 MB APK, measured 2026-11-12
# (project.md sec 3).
linux:!android {
    SOURCES += $$SNURRA_ROOT/src/output_alsa.cpp
    PKGCONFIG += alsa
    DEFINES += SNURRA_ALSA=1
    # Whether an X11 window may be other than a rectangle: a compositor's
    # selection, asked of the X server (the normal look, project.md 3b).
    PKGCONFIG += xcb
    DEFINES += SNURRA_XCB=1
}
qtHaveModule(multimedia) {
    QT += multimedia
    SOURCES += $$SNURRA_ROOT/src/output_qt.cpp
    DEFINES += SNURRA_QT_AUDIO=1
}
