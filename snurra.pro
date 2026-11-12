# snurra -- the player, one tile per process (project.md sec 3b).
TEMPLATE = app
TARGET = snurra
QT += widgets network
CONFIG += c++17
QMAKE_CXXFLAGS += -Os -ffunction-sections -fdata-sections
QMAKE_LFLAGS += -Wl,--gc-sections
include(snurra.pri)
SOURCES += src/main.cpp
