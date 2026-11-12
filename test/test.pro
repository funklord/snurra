# The tests. The library comes in through snurra.pri, as a consumer takes it,
# so the .pri is under test too; the handover tests also start the built
# player and talk to it.
TEMPLATE = app
TARGET = snurra-tests
QT += testlib
CONFIG += c++17 testcase no_testcase_installs
include(../snurra.pri)
SOURCES += main.cpp handover_test.cpp codec_test.cpp deck_test.cpp embed_test.cpp video_test.cpp beats_test.cpp
HEADERS += test_bytes.h
isEmpty(SNURRA_BIN): SNURRA_BIN = $$OUT_PWD/../build/snurra
DEFINES += SNURRA_BIN=\\\"$$SNURRA_BIN\\\"
