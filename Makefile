# snurra -- see project.md.
#
# TARGETS
#   make            the player, in $(BUILD_DIR)/snurra
#   make test       build and run the handover and codec tests (never built by plain make)
#   make check-mk   build the player from snurra.mk alone, and run the tests on it
#   make install    the player and its desktop entry, under PREFIX (/usr/local)
#                   and DESTDIR; make uninstall removes those two files
#   make android    the player for Android (a Qt kit, NDK, SDK, JDK); and
#                   android-install, -run, -log, -uninstall, -check from
#                   tool/android.mk
#   make style      the style gate: its own suite, whitespace, indentation, the docs
#   make hooks      install the commit-msg hook
#   make help       this list
#

QMAKE ?= $(shell command -v qmake6 2>/dev/null || command -v qmake 2>/dev/null || echo qmake6)

BUILD_DIR      ?= build
TEST_BUILD_DIR ?= $(BUILD_DIR)-test
BIN      = $(BUILD_DIR)/snurra
TEST_BIN = $(TEST_BUILD_DIR)/snurra-tests

# A ceiling inside the target: the tests start processes, and one that
# hung would otherwise wait for ever. The suite took 123 s on 2026-11-13
# (24 cores, load 5), so about twice that.
TEST_TIMEOUT ?= 300

.DEFAULT_GOAL := all

all: $(BIN)

$(BUILD_DIR)/Makefile: snurra.pro snurra.pri
	mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && $(QMAKE) ../snurra.pro CONFIG+=release
	@# qmake's rules depend on sources alone, so a change to the flags,
	@# defines or libraries -- snurra.pri's, say -- left objects and the
	@# binary built the old way standing (measured 2026-11-12: Qt
	@# Multimedia stayed linked into the Android library, and an object
	@# kept a define snurra.pri no longer set). A new Makefile takes them
	@# with it: qmake's own clean, which names its files, and the binary.
	$(MAKE) -C $(BUILD_DIR) clean
	rm -f $(BIN)

$(BIN): $(BUILD_DIR)/Makefile FORCE
	$(MAKE) -C $(BUILD_DIR)

$(TEST_BUILD_DIR)/Makefile: test/test.pro snurra.pri
	mkdir -p $(TEST_BUILD_DIR)
	cd $(TEST_BUILD_DIR) && $(QMAKE) ../test/test.pro SNURRA_BIN=$(abspath $(BIN))
	@# As the player's: a test built the old way passes against nothing.
	$(MAKE) -C $(TEST_BUILD_DIR) clean
	rm -f $(TEST_BIN)

tests-build: $(TEST_BUILD_DIR)/Makefile FORCE
	$(MAKE) -C $(TEST_BUILD_DIR)

# A run over no binary is a failure, not a pass.
test: $(BIN) tests-build
	@[ -x $(TEST_BIN) ] || { echo "test: no test binary at $(TEST_BIN)" >&2; exit 1; }
	timeout $(TEST_TIMEOUT) ./$(TEST_BIN)

# --- install -----------------------------------------------------------------
# The player and its desktop entry, so a file manager offers snurra for
# audio and starts it with the files clicked (project.md sec 3b); several
# at once become a train. PREFIX and DESTDIR as packaging expects them.
PREFIX ?= /usr/local
INSTALLED = $(DESTDIR)$(PREFIX)/bin/snurra $(DESTDIR)$(PREFIX)/share/applications/snurra.desktop

install: $(BIN)
	install -D -m 0755 $(BIN) $(DESTDIR)$(PREFIX)/bin/snurra
	install -D -m 0644 snurra.desktop $(DESTDIR)$(PREFIX)/share/applications/snurra.desktop

# The two files install wrote, by name.
uninstall:
	rm -f $(INSTALLED)

# --- Android -----------------------------------------------------------------
# The player on Android, as BeerSSH's tile will first run (project.md sec
# 1): the vocabulary and the adb plumbing are tool/android.mk's, copied
# verbatim from ~/.claude/tool/android.mk, included here below `all`; the
# build rule is this project's, after bbq-predictor's.
TARGET  = snurra
APP_ID  = se.vibes.snurra
VERSION ?= $(shell cat VERSION)
include tool/android.mk

ANDROID_BUILD_DIR ?= build-android-$(ANDROID_ABI)
ANDROID_ARTIFACT = $(ANDROID_BUILD_DIR)/$(TARGET)-$(VERSION)-$(ANDROID_ABI).apk

# FFmpeg on Android is the kit's: the libraries Qt ships for its own
# multimedia, 7.1, without headers. The desktop's 7.1 headers serve, but
# they live in the multiarch directory beside glibc's, which must not be on
# an NDK compile's path -- so the four FFmpeg directories alone are copied,
# by name, into this build's tree.
FFMPEG_HEADERS ?= $(shell pkg-config --variable=includedir libavformat)
FFMPEG_DIRS     = libavformat libavcodec libavutil libswresample

#
# The headers must be the kit's FFmpeg's, or one version's structures are
# compiled against another's libraries -- which links, and is wrong at run
# time. They are not always: this machine's kit, a pre-release, carries
# 7.1 (libavformat 61), the released 6.12.0 kit 9.0 (63), found by
# BeerSSH's CI (inbox 20261113T0126Z). So the header's major is compared
# with the one the kit's library names itself by, and a mismatch refused;
# the kit's library is a prerequisite, so a new kit is checked again.
FFMPEG_KIT_LIB = $(QT_ANDROID_ROOT)/lib/libavformat.so

$(ANDROID_BUILD_DIR)/ffmpeg-include/.copied: $(FFMPEG_KIT_LIB)
	@test -n "$(ANDROID_BUILD_DIR)" || { echo "ANDROID_BUILD_DIR is empty" >&2; exit 1; }
	@header=$$(sed -n 's/^#define LIBAVFORMAT_VERSION_MAJOR *\([0-9]*\).*/\1/p' $(FFMPEG_HEADERS)/libavformat/version_major.h); \
	kit=$$(strings $(FFMPEG_KIT_LIB) | sed -n 's|^.*Lavf/\([0-9]*\)\..*$$|\1|p' | head -1); \
	if [ -z "$$header" ] || [ -z "$$kit" ]; then echo "android: cannot read FFmpeg's version (headers '$$header', kit '$$kit')" >&2; exit 1; fi; \
	if [ "$$header" != "$$kit" ]; then echo "android: FFmpeg headers are libavformat $$header, the kit's library $$kit: give FFMPEG_HEADERS for $$kit" >&2; exit 1; fi; \
	echo "android: FFmpeg headers and kit both libavformat $$kit"
	mkdir -p $(ANDROID_BUILD_DIR)/ffmpeg-include
	for d in $(FFMPEG_DIRS); do cp -r $(FFMPEG_HEADERS)/$$d $(ANDROID_BUILD_DIR)/ffmpeg-include/; done
	touch $@

# The preflight runs before, but does not date the Makefile: a phony
# prerequisite would regenerate it, and rebuild everything, every time.
$(ANDROID_BUILD_DIR)/Makefile: snurra.pro snurra.pri $(ANDROID_BUILD_DIR)/ffmpeg-include/.copied | android-check
	mkdir -p $(ANDROID_BUILD_DIR)
	cd $(ANDROID_BUILD_DIR) && $(QT_ANDROID_ROOT)/bin/qmake $(CURDIR)/snurra.pro CONFIG+=release \
	        SNURRA_FFMPEG_INCLUDE=$(abspath $(ANDROID_BUILD_DIR)/ffmpeg-include) \
	        SNURRA_VERSION=$(VERSION) SNURRA_VERSION_CODE=$(ANDROID_VERSION_CODE) \
	        SNURRA_TARGET_API=$(ANDROID_TARGET_API)
	$(MAKE) -C $(ANDROID_BUILD_DIR) clean
	rm -f $(ANDROID_BUILD_DIR)/lib$(TARGET)_$(ANDROID_ABI).so

# Qt's generated `apk` is build-and-install, then androiddeployqt; only the
# second is replaced, to pass --android-platform, which the generated rule
# hardcodes away. androiddeployqt's staged libraries are cleared first, as
# bbq-predictor does: it adds and never removes, so a dependency dropped
# stays in every later APK (measured 2026-11-12: 72 libraries of the
# morning's build were still staged that afternoon). The staging tree is the build's own; only its .so files go,
# each named, and only under a build directory that is set and relative.
ANDROID_STAGED_LIBS = $(ANDROID_BUILD_DIR)/android-build/libs

android-staged-fresh:
	@case "$(ANDROID_BUILD_DIR)" in ""|/*|*..*) echo "android: unsafe ANDROID_BUILD_DIR '$(ANDROID_BUILD_DIR)'" >&2; exit 1;; esac
	@if [ -d "$(ANDROID_STAGED_LIBS)" ]; then find "$(ANDROID_STAGED_LIBS)" -type f -name '*.so' -print -delete; fi

android: android-fresh android-staged-fresh $(ANDROID_BUILD_DIR)/Makefile
	$(MAKE) -C $(ANDROID_BUILD_DIR) apk_install_target
	$(ANDROID_DEPLOY_QT) \
	        --input $(ANDROID_BUILD_DIR)/android-$(TARGET)-deployment-settings.json \
	        --output $(ANDROID_BUILD_DIR)/android-build \
	        --android-platform $(ANDROID_PLATFORM) \
	        --apk $(ANDROID_BUILD_DIR)/android-build/$(TARGET).apk
	cd $(ANDROID_BUILD_DIR)/android-build && ./gradlew assembleDebug
	@src=$$(find $(ANDROID_BUILD_DIR)/android-build/build/outputs/apk -name '*.apk' -print -quit); \
	if [ -z "$$src" ]; then echo "android: no .apk was produced" >&2; exit 1; fi; \
	cp "$$src" $(ANDROID_ARTIFACT); \
	echo "android: $(ANDROID_ARTIFACT)"
	$(call android_verify_signature,$(ANDROID_ARTIFACT))

# --- snurra.mk, proved -----------------------------------------------------
# A consumer with a hand-written Makefile builds from snurra.mk, which
# derives its lists from the tree. This builds the player from those lists
# alone -- no qmake -- and runs the tests against it, so a list that misses
# a file fails here rather than in a consumer.
MK_BUILD_DIR ?= $(BUILD_DIR)-mk
SNURRA_DIR := .
include snurra.mk
MOC ?= $(shell $(QMAKE) -query QT_HOST_LIBEXECS)/moc
MK_QT    := Qt6Widgets Qt6Network
MK_MOCS  := $(patsubst $(SNURRA_INCLUDE)/snurra/%.h,$(MK_BUILD_DIR)/moc_%.cpp,$(SNURRA_MOC_HDRS))
MK_FLAGS  = -std=c++17 -fPIC -Os -I$(SNURRA_INCLUDE) $(SNURRA_DEFINES) $(shell pkg-config --cflags $(MK_QT) $(SNURRA_PKGS))
MK_LIBS   = $(shell pkg-config --libs $(MK_QT) $(SNURRA_PKGS))

$(MK_BUILD_DIR)/moc_%.cpp: $(SNURRA_INCLUDE)/snurra/%.h
	@mkdir -p $(MK_BUILD_DIR)
	$(MOC) $< -o $@

$(MK_BUILD_DIR)/snurra: $(SNURRA_SRCS) src/main.cpp $(MK_MOCS) $(SNURRA_HDRS) VERSION
	$(CXX) $(MK_FLAGS) -DSNURRA_VERSION_TEXT='"$(VERSION)"' $(SNURRA_SRCS) src/main.cpp $(MK_MOCS) -o $@ $(MK_LIBS)

check-mk: $(MK_BUILD_DIR)/snurra tests-build
	SNURRA_BIN=$(abspath $(MK_BUILD_DIR)/snurra) timeout $(TEST_TIMEOUT) ./$(TEST_BIN)

# The gate's own suite first, since a gate's pass means something only once
# the gate is known to work; both are copied from ~/.claude/tool/.
style: style-tool
	python3 tool/style_gate.py check
	python3 tool/style_gate.py docs

style-tool:
	python3 tool/test_style_gate.py

hooks:
	@dir=$$(git rev-parse --git-common-dir 2>/dev/null); \
	if [ -z "$$dir" ]; then echo "hooks: not a git repository" >&2; exit 1; fi; \
	set -e; \
	mkdir -p "$$dir/hooks"; \
	install -m 0755 tool/hooks/commit-msg "$$dir/hooks/commit-msg"; \
	echo "hooks: commit-msg installed into $$dir/hooks/"

help:
	@sed -n '/^# TARGETS/,/^#$$/p' $(firstword $(MAKEFILE_LIST)) | sed 's/^# \{0,1\}//'

.PHONY: all tests-build test check-mk install uninstall android android-staged-fresh style style-tool hooks help FORCE
FORCE:
