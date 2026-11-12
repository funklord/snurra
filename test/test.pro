# The handover tests: they start the built snurra and talk to it.
TEMPLATE = app
TARGET = snurra-tests
QT += testlib network
QT -= gui
CONFIG += c++17 testcase no_testcase_installs
SOURCES += main.cpp handover_test.cpp codec_test.cpp deck_test.cpp ../src/snap.cpp ../src/wire.cpp ../src/source.cpp \
           ../src/deck.cpp ../src/peaks.cpp

# The codec tests decode through snurra's own source.
CONFIG += link_pkgconfig
PKGCONFIG += libavformat libavcodec libswresample libavutil
INCLUDEPATH += ../include
HEADERS += ../include/snurra/snap.h ../include/snurra/wire.h ../include/snurra/tile_state.h \
           ../include/snurra/clock.h ../include/snurra/source.h ../include/snurra/deck.h ../include/snurra/spsc.h \
           ../include/snurra/peaks.h
isEmpty(SNURRA_BIN): SNURRA_BIN = $$OUT_PWD/../build/snurra
DEFINES += SNURRA_BIN=\\\"$$SNURRA_BIN\\\"
