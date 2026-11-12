# The handover tests: they start the built snurra and talk to it.
TEMPLATE = app
TARGET = snurra-tests
QT += testlib network
QT -= gui
CONFIG += c++17 testcase no_testcase_installs
SOURCES += main.cpp handover_test.cpp codec_test.cpp ../src/snap.cpp ../src/wire.cpp ../src/source.cpp

# The codec tests decode through snurra's own source.
CONFIG += link_pkgconfig
PKGCONFIG += libavformat libavcodec libswresample libavutil
HEADERS += ../src/snap.h ../src/wire.h ../src/tile_state.h ../src/clock.h ../src/source.h
isEmpty(SNURRA_BIN): SNURRA_BIN = $$OUT_PWD/../build/snurra
DEFINES += SNURRA_BIN=\\\"$$SNURRA_BIN\\\"
