# snurra for a hand-written Makefile -- the counterpart of snurra.pri.
#
#     SNURRA_DIR = path/to/snurra
#     include $(SNURRA_DIR)/snurra.mk
#
# and then compile $(SNURRA_SRCS) and the moc output of $(SNURRA_MOC_HDRS)
# with -I$(SNURRA_INCLUDE), $(SNURRA_DEFINES), C++17, Qt Widgets and
# Network, and `pkg-config --cflags --libs $(SNURRA_PKGS)`. Set
# SNURRA_QT_AUDIO=1 before the include for the QAudioSink output, which
# brings Qt Multimedia (Qt6Multimedia among the packages).
#
# The lists are derived from the tree rather than written out, so a file
# snurra adds reaches every consumer without an edit there: the sources are
# every src/*.cpp but the player's main.cpp, and the headers moc must see
# are the ones that declare Q_OBJECT. `make check-mk` in this tree builds
# the player from these lists alone and runs the handover tests against
# it, which is what keeps them honest.

SNURRA_DIR ?= snurra

SNURRA_INCLUDE  := $(SNURRA_DIR)/include
# Not main.cpp (the player's, not the library's), nor output_qt.cpp (opt-in,
# below), nor output_aaudio.cpp (Android's, which builds through qmake).
SNURRA_SRCS     := $(filter-out $(SNURRA_DIR)/src/main.cpp $(SNURRA_DIR)/src/output_qt.cpp $(SNURRA_DIR)/src/output_aaudio.cpp,$(wildcard $(SNURRA_DIR)/src/*.cpp))
SNURRA_HDRS     := $(wildcard $(SNURRA_INCLUDE)/snurra/*.h)
SNURRA_PKGS     := libavformat libavcodec libswresample libavutil alsa xcb
SNURRA_DEFINES  := -DSNURRA_ALSA=1 -DSNURRA_XCB=1
ifeq ($(SNURRA_QT_AUDIO),1)
SNURRA_SRCS     += $(SNURRA_DIR)/src/output_qt.cpp
SNURRA_PKGS     += Qt6Multimedia
SNURRA_DEFINES  += -DSNURRA_QT_AUDIO=1
endif

# /dev/null so that grep, given no headers, reads nothing rather than stdin.
SNURRA_MOC_HDRS := $(shell grep -l '\bQ_OBJECT\b' $(SNURRA_HDRS) /dev/null)
