# snurra -- the player, one tile per process (project.md sec 3b).
TEMPLATE = app
TARGET = snurra
QT += widgets network
CONFIG += c++17
QMAKE_CXXFLAGS += -Os -ffunction-sections -fdata-sections
QMAKE_LFLAGS += -Wl,--gc-sections
SOURCES += src/main.cpp src/marker.cpp src/node.cpp src/snap.cpp src/tile.cpp src/wire.cpp
HEADERS += src/marker.h src/node.h src/snap.h src/tile.h src/tile_state.h src/wire.h
