# fuzznet's typed client calls (fuzznet's local/rpc.h), for the player
# alone: the estate's catalogue in the library (project.md sec 3b). The
# library itself -- snurra.pri, snurra.mk, what other projects link --
# knows nothing of fuzznet (sec 1), so this is included by snurra.pro and
# the tests, never by snurra.pri.
#
# fuzznet is vendored at the commit the gitlink names (a submodule, shallow),
# fetched by the Makefile before qmake runs. The calls, the format beneath
# them and its changes are fuzznet's; snurra speaks no verb itself.
# Measured 2026-11-14: these sources, sections collected, are some 20 KB.
FUZZNET_DIR = $$PWD/../fuzznet
INCLUDEPATH += $$FUZZNET_DIR $$PWD
SOURCES += $$files($$FUZZNET_DIR/local/*.c) \
           $$FUZZNET_DIR/hex/hex.c \
           $$FUZZNET_DIR/constant_time/constant_time.c
SOURCES += $$PWD/estate.cpp
HEADERS += $$PWD/estate.h
# Sections collected, and not only for size: rpc.c's calls for cards,
# stickers and locations reach fuzznet modules this closure leaves out, so
# a link that kept every function would not link (the tests, 2026-11-14).
QMAKE_LFLAGS += -Wl,--gc-sections
QMAKE_CXXFLAGS += -ffunction-sections -fdata-sections
QMAKE_CFLAGS += -std=c11 -ffunction-sections -fdata-sections
QMAKE_CFLAGS_RELEASE -= -O2
QMAKE_CFLAGS_RELEASE += -Os
