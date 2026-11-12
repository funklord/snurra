# snurra for a hand-written Makefile -- the counterpart of snurra.pri.
#
#     SNURRA_DIR = path/to/snurra
#     include $(SNURRA_DIR)/snurra.mk
#
# and then compile $(SNURRA_SRCS) and the moc output of $(SNURRA_MOC_HDRS)
# with -I$(SNURRA_INCLUDE), C++17, Qt Widgets and Network, and
# `pkg-config --cflags --libs $(SNURRA_PKGS)`.
#
# The lists are derived from the tree rather than written out, so a file
# snurra adds reaches every consumer without an edit there: the sources are
# every src/*.cpp but the player's main.cpp, and the headers moc must see
# are the ones that declare Q_OBJECT. `make check-mk` in this tree builds
# the player from these lists alone and runs the handover tests against
# it, which is what keeps them honest.

SNURRA_DIR ?= snurra

SNURRA_INCLUDE  := $(SNURRA_DIR)/include
SNURRA_SRCS     := $(filter-out $(SNURRA_DIR)/src/main.cpp,$(wildcard $(SNURRA_DIR)/src/*.cpp))
SNURRA_HDRS     := $(wildcard $(SNURRA_INCLUDE)/snurra/*.h)
SNURRA_PKGS     := libavformat libavcodec libswresample libavutil alsa

# /dev/null so that grep, given no headers, reads nothing rather than stdin.
SNURRA_MOC_HDRS := $(shell grep -l '\bQ_OBJECT\b' $(SNURRA_HDRS) /dev/null)
