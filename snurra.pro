# snurra -- the player, one tile per process (project.md sec 3b).
TEMPLATE = app
TARGET = snurra
QT += widgets network
CONFIG += c++17
QMAKE_CXXFLAGS += -Os -ffunction-sections -fdata-sections
QMAKE_LFLAGS += -Wl,--gc-sections
SOURCES += src/main.cpp src/marker.cpp src/node.cpp src/snap.cpp src/tile.cpp src/wire.cpp \
           src/deck.cpp src/engine.cpp src/output.cpp src/source.cpp
HEADERS += src/marker.h src/node.h src/snap.h src/tile.h src/tile_state.h src/wire.h \
           src/clock.h src/deck.h src/engine.h src/output.h src/source.h src/spsc.h

# Decoding: FFmpeg's libraries (project.md sec 3, 3a).
CONFIG += link_pkgconfig
PKGCONFIG += libavformat libavcodec libswresample libavutil

# Output: ALSA on Linux; PortAudio, AAudio and QAudioSink are to come.
linux:!android {
	SOURCES += src/output_alsa.cpp
	PKGCONFIG += alsa
}
