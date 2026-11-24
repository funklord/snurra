# snurra -- see project.md.
#
# TARGETS
#   make            the player, in $(BUILD_DIR)/snurra
#   make test       build and run the tests (never built by plain make), with
#                   a real fuzznetd built from the vendored fuzznet
#   make test-estate-loss  the estate's two hosts over a lossy link
#   make check-mk   build the player from snurra.mk alone, and run the tests on it
#   make install    the player, its desktop entry and its icons, under PREFIX
#                   (/usr/local) and DESTDIR; make uninstall removes them
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
# hung would otherwise wait for ever. About twice what the suite takes:
# 123 s on 2026-11-13 (24 cores, load 5), 240 s on 2026-11-18 -- when 300
# killed a run at load 38 midway, video_test alone taking 129 s. Killed
# ten seconds after the signal (-k 10): a run hung in a destructor has
# outlived SIGTERM alone.
TEST_TIMEOUT ?= 600

.DEFAULT_GOAL := all

all: $(BIN)

# Where the vendored siblings are: this tree's own submodules by default,
# or a consumer's single copies at its own root, which it names here --
# the leaf decides, one copy of each dependency (claude-guidelines'
# harmonization.md, 2026-11-24). A directory given is never fetched into:
# absent, the build stops and names it.
#
#     make FUZZNET_DIR=../fuzznet QTKIT_DIR=../qtkit MONOCYPHER_DIR=../monocypher
FUZZNET_DIR ?= fuzznet
QTKIT_DIR ?= qtkit
MONOCYPHER_DIR ?= monocypher
# Absolute, for qmake and the sub-make, which run elsewhere.
FUZZNET_ABS = $(abspath $(FUZZNET_DIR))
QTKIT_ABS = $(abspath $(QTKIT_DIR))
MONOCYPHER_ABS = $(abspath $(MONOCYPHER_DIR))
QMAKE_DIRS = FUZZNET_DIR=$(FUZZNET_ABS) QTKIT_DIR=$(QTKIT_ABS)

# fuzznet, vendored for the player's estate (app/fuzznet.pri): fetched when
# absent, at the commit the gitlink names and no other, shallow -- its
# history is over a gigabyte and the player needs one tree. A build that
# cannot fetch it stops and says so, rather than building a player that
# quietly has no estate.
FUZZNET_PROBE = $(FUZZNET_DIR)/local/rpc.c
$(FUZZNET_PROBE):
	@[ "$(FUZZNET_DIR)" = fuzznet ] || { echo "snurra: FUZZNET_DIR=$(FUZZNET_DIR) has no local/rpc.c; a directory given is not fetched" >&2; exit 1; }
	@command -v git >/dev/null 2>&1 || { echo "snurra: fuzznet/ is empty, and git is not installed to fetch it" >&2; exit 1; }
	@git rev-parse --git-dir >/dev/null 2>&1 || { echo "snurra: fuzznet/ is empty, and this is not a git checkout: put fuzznet at the commit snurra's gitlink names there" >&2; exit 1; }
	@echo "snurra: fetching fuzznet, the pinned commit only"
	git submodule update --init --depth 1 fuzznet
	@test -f $@ || { echo "snurra: fuzznet could not be fetched" >&2; exit 1; }

# qtkit, the workspace's shared Qt pieces (the theme), a submodule at
# qtkit/: fetched the same way, the pinned commit only.
QTKIT_PROBE = $(QTKIT_DIR)/qtkit.pri
$(QTKIT_PROBE):
	@[ "$(QTKIT_DIR)" = qtkit ] || { echo "snurra: QTKIT_DIR=$(QTKIT_DIR) has no qtkit.pri; a directory given is not fetched" >&2; exit 1; }
	@command -v git >/dev/null 2>&1 || { echo "snurra: qtkit/ is empty, and git is not installed to fetch it" >&2; exit 1; }
	@git rev-parse --git-dir >/dev/null 2>&1 || { echo "snurra: qtkit/ is empty, and this is not a git checkout: put qtkit at the commit snurra's gitlink names there" >&2; exit 1; }
	@echo "snurra: fetching qtkit, the pinned commit only"
	git submodule update --init --depth 1 qtkit
	@test -f $@ || { echo "snurra: qtkit could not be fetched" >&2; exit 1; }

$(BUILD_DIR)/Makefile: snurra.pro snurra.pri app/fuzznet.pri | $(FUZZNET_PROBE) $(QTKIT_PROBE)
	mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && $(QMAKE) $(CURDIR)/snurra.pro CONFIG+=release $(QMAKE_DIRS)
	@# qmake's rules depend on sources alone, so a change to the flags,
	@# defines or libraries -- snurra.pri's, say -- left objects and the
	@# binary built the old way standing (measured 2026-11-12: Qt
	@# Multimedia stayed linked into the Android library, and an object
	@# kept a define snurra.pri no longer set). A new Makefile takes them
	@# with it: qmake's own clean, which names its files, and the binary.
	$(MAKE) -C $(BUILD_DIR) clean
	rm -f $(BIN)

# qmake writes each object's headers into the Makefile when it runs, and
# only then: an #include added later is not a dependency, and a header
# changed afterwards rebuilds the library's objects but not that one. So
# embed.cpp, which came to include library.h with the DJ player, kept
# an object of the old snurra_library while library.h grew members -- a
# widget read at the wrong offsets, a crash in a layout (2026-11-13). The
# include lines of every source and header are the stamp: written only
# when they change, and qmake run again then, without a clean, the flags
# being the same. And the files app/fuzznet.pri takes by a glob, which
# qmake expands only when it runs: moving the pin to a fuzznet with a new
# local/rpc_binary.c left it uncompiled and the link short of its
# symbols (2026-11-18).
INCLUDE_SOURCES = $(sort $(wildcard src/*.cpp include/snurra/*.h test/*.cpp test/*.h))
GLOBBED_SOURCES = $(sort $(wildcard $(FUZZNET_DIR)/local/*.c))
%/includes.list: FORCE
	@mkdir -p $(@D); { grep -H '^#include "' $(INCLUDE_SOURCES); printf '%s\n' $(GLOBBED_SOURCES) $(QMAKE_DIRS); } > $@.new || true
	@if cmp -s $@.new $@; then rm -f $@.new; else mv $@.new $@; fi
$(BUILD_DIR)/qmake.stamp: $(BUILD_DIR)/includes.list $(BUILD_DIR)/Makefile
	cd $(BUILD_DIR) && $(QMAKE) $(CURDIR)/snurra.pro CONFIG+=release $(QMAKE_DIRS)
	touch $@

$(BIN): $(BUILD_DIR)/Makefile $(BUILD_DIR)/qmake.stamp FORCE
	$(MAKE) -C $(BUILD_DIR)

$(TEST_BUILD_DIR)/Makefile: test/test.pro snurra.pri app/fuzznet.pri | $(FUZZNET_PROBE)
	mkdir -p $(TEST_BUILD_DIR)
	cd $(TEST_BUILD_DIR) && $(QMAKE) $(CURDIR)/test/test.pro SNURRA_BIN=$(abspath $(BIN)) $(QMAKE_DIRS)
	@# As the player's: a test built the old way passes against nothing.
	$(MAKE) -C $(TEST_BUILD_DIR) clean
	rm -f $(TEST_BIN)

$(TEST_BUILD_DIR)/qmake.stamp: $(TEST_BUILD_DIR)/includes.list $(TEST_BUILD_DIR)/Makefile
	cd $(TEST_BUILD_DIR) && $(QMAKE) $(CURDIR)/test/test.pro SNURRA_BIN=$(abspath $(BIN)) $(QMAKE_DIRS)
	touch $@

tests-build: $(TEST_BUILD_DIR)/Makefile $(TEST_BUILD_DIR)/qmake.stamp FORCE
	$(MAKE) -C $(TEST_BUILD_DIR)

# A run over no binary is a failure, not a pass.
# A real node for the estate's tests (test/estate_test.cpp): fuzznetd,
# built from the vendored fuzznet into the test build -- not from a
# sibling checkout, whose state is its session's. It needs fuzznet's own
# vendored Monocypher, fetched as fuzznet is: when absent, at the pin,
# shallow, or the reason. The sub-make runs every time, since only it knows
# whether fuzznet's sources moved, and writes nothing when they did not.
FUZZNETD = $(abspath $(TEST_BUILD_DIR)/fuzznet/fuzznetd)
# fuzznet's own dependencies, from this tree's copies -- never fuzznet's
# nested submodules, which a recursive fetch would pull in a second time:
# Monocypher, which the node needs, and no flog (its log, off when empty).
FUZZNET_DEPS = MONOCYPHER_DIR=$(MONOCYPHER_ABS) FLOG_DIR=
MONOCYPHER_PROBE = $(MONOCYPHER_DIR)/src/monocypher.h
$(MONOCYPHER_PROBE):
	@[ "$(MONOCYPHER_DIR)" = monocypher ] || { echo "snurra: MONOCYPHER_DIR=$(MONOCYPHER_DIR) has no src/monocypher.h; a directory given is not fetched" >&2; exit 1; }
	@command -v git >/dev/null 2>&1 || { echo "snurra: monocypher/ is empty, and git is not installed to fetch it" >&2; exit 1; }
	@git rev-parse --git-dir >/dev/null 2>&1 || { echo "snurra: monocypher/ is empty, and this is not a git checkout: put Monocypher at the commit snurra's gitlink names there" >&2; exit 1; }
	@echo "snurra: fetching Monocypher, for the tests' node, the pinned commit only"
	git submodule update --init --depth 1 monocypher
	@test -f $@ || { echo "snurra: Monocypher could not be fetched" >&2; exit 1; }
fuzznetd: $(MONOCYPHER_PROBE) FORCE | $(FUZZNET_PROBE)
	$(MAKE) -C $(FUZZNET_DIR) --no-print-directory $(FUZZNET_DEPS) BUILD_DIR=$(abspath $(TEST_BUILD_DIR)/fuzznet) $(FUZZNETD)

# The two hosts again over a link dropping a fifth of what is sent, against
# fuzznet's faults build: 30 to 60 s and varying, so not part of `test`.
FUZZNETD_FAULTS = $(abspath $(TEST_BUILD_DIR)/fuzznet-faults/fuzznetd)
fuzznetd-faults: $(MONOCYPHER_PROBE) FORCE | $(FUZZNET_PROBE)
	$(MAKE) -C $(FUZZNET_DIR) --no-print-directory $(FUZZNET_DEPS) FAULTS=1 BUILD_DIR=$(abspath $(TEST_BUILD_DIR)/fuzznet-faults) $(FUZZNETD_FAULTS)
test-estate-loss: tests-build fuzznetd-faults
	timeout -k 10 1000 python3 test/estate_two_hosts.py $(FUZZNETD_FAULTS) ./$(TEST_BIN) --loss 20

test: $(BIN) tests-build fuzznetd
	@[ -x $(TEST_BIN) ] || { echo "test: no test binary at $(TEST_BIN)" >&2; exit 1; }
	@[ -x $(FUZZNETD) ] || { echo "test: no fuzznetd at $(FUZZNETD)" >&2; exit 1; }
	SNURRA_FUZZNETD=$(FUZZNETD) timeout -k 10 $(TEST_TIMEOUT) ./$(TEST_BIN)
	@# Two of one user's hosts, a file played from the one that has only
	@# its name as it arrives from the other: 2 s, bounded inside by
	@# fuzznet's own scenario deadline, and outside here.
	timeout -k 10 $(TEST_TIMEOUT) python3 test/estate_two_hosts.py $(FUZZNETD) ./$(TEST_BIN)

# --- install -----------------------------------------------------------------
# The player and its desktop entry, so a file manager offers snurra for
# audio and starts it with the files clicked (project.md sec 3b); several
# at once become a train. PREFIX and DESTDIR as packaging expects them.
PREFIX ?= /usr/local
# The id is reverse-DNS, se.vibes.snurra, as the freedesktop specification
# recommends; the program keeps its name. The icon in hicolor, the theme
# every desktop falls back to, as the SVG and as PNGs: TDE takes a PNG
# before an SVG and draws SVG with an engine of its own.
ID = se.vibes.snurra
ICON_SIZES = 16 22 32 48 64 128
APPS = $(DESTDIR)$(PREFIX)/share/applications
HICOLOR = $(DESTDIR)$(PREFIX)/share/icons/hicolor
INSTALLED = $(DESTDIR)$(PREFIX)/bin/snurra $(APPS)/$(ID).desktop $(HICOLOR)/scalable/apps/$(ID).svg \
            $(foreach n,$(ICON_SIZES),$(HICOLOR)/$(n)x$(n)/apps/$(ID).png)
# What an install before 2026-11-25 wrote, under the name the id replaced:
# removed on install too, so a menu does not show snurra twice.
INSTALLED_BEFORE = $(APPS)/snurra.desktop $(HICOLOR)/scalable/apps/snurra.svg

install: $(BIN)
	install -D -m 0755 $(BIN) $(DESTDIR)$(PREFIX)/bin/snurra
	install -D -m 0644 packaging/$(ID).desktop $(APPS)/$(ID).desktop
	install -D -m 0644 packaging/$(ID).svg $(HICOLOR)/scalable/apps/$(ID).svg
	for n in $(ICON_SIZES); do \
		install -D -m 0644 packaging/icon/$$n.png $(HICOLOR)/$${n}x$${n}/apps/$(ID).png || exit 1; \
	done
	rm -f $(INSTALLED_BEFORE)
	@# A packager's install refreshes nothing: the package's triggers do.
	@# Otherwise what is present, and nothing that is not.
	@if [ -z "$(DESTDIR)" ]; then \
		! command -v update-desktop-database >/dev/null || update-desktop-database -q $(APPS) || true; \
		! command -v gtk-update-icon-cache >/dev/null || gtk-update-icon-cache -q -t -f $(HICOLOR) || true; \
		! command -v tdebuildsycoca >/dev/null || tdebuildsycoca >/dev/null 2>&1 || true; \
	fi

# The files install wrote, by name, and the ones it replaced.
uninstall:
	rm -f $(INSTALLED) $(INSTALLED_BEFORE)

# --- Android -----------------------------------------------------------------
# The player on Android, as BeerSSH's tile will first run (project.md sec
# 1): the vocabulary and the adb plumbing are tool/android.mk's, copied
# verbatim from ~/.claude/tool/android.mk, included here below `all`; the
# build rule is this project's, after bbq-predictor's.
TARGET  = snurra
APP_ID  = se.vibes.snurra
VERSION ?= $(shell cat VERSION)
# Before the fragment: its kit record's rule names a file in it, and a rule's
# target is expanded as the rule is read. ANDROID_ABI is the fragment's,
# set before that rule; this is expanded when used.
ANDROID_BUILD_DIR ?= build-android-$(ANDROID_ABI)
include tool/android.mk

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

# And the copy already made is checked against the kit at every build, not
# only when the kit's library is newer than it: a release kit installed over
# a pre-release carries its release's older dates, so the 7.1 headers of
# the first stayed in the x86_64 build against the second's 9.0 libraries,
# and every AVFrame was read at the wrong offsets -- channels 0, rate 0, and
# no sound (2026-11-13). On a mismatch the stamp goes, and the rule above
# copies the headers again, over the old ones.
android-ffmpeg-fresh:
	@copied=$$(sed -n 's/^#define LIBAVFORMAT_VERSION_MAJOR *\([0-9]*\).*/\1/p' $(ANDROID_BUILD_DIR)/ffmpeg-include/libavformat/version_major.h 2>/dev/null); \
	kit=$$(strings $(FFMPEG_KIT_LIB) | sed -n 's|^.*Lavf/\([0-9]*\)\..*$$|\1|p' | head -1); \
	if [ -n "$$copied" ] && [ "$$copied" != "$$kit" ]; then \
		echo "android: the copied FFmpeg headers are libavformat $$copied, the kit's $$kit: copying again"; \
		rm -f $(ANDROID_BUILD_DIR)/ffmpeg-include/.copied; \
	fi

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
# The kit's record (tool/android.mk) does date it, and only when the kit
# changes: then the build's objects and staged libraries are cleared, as
# a kit updated in place carries dates older than what was built from the
# last one.
$(ANDROID_BUILD_DIR)/Makefile: snurra.pro snurra.pri app/fuzznet.pri | $(FUZZNET_PROBE) $(QTKIT_PROBE)
$(ANDROID_BUILD_DIR)/Makefile: $(ANDROID_BUILD_DIR)/ffmpeg-include/.copied $(ANDROID_KIT_SOURCE) \
                              $(ANDROID_BUILD_DIR)/includes.list | android-check
	mkdir -p $(ANDROID_BUILD_DIR)
	cd $(ANDROID_BUILD_DIR) && $(QT_ANDROID_ROOT)/bin/qmake $(CURDIR)/snurra.pro CONFIG+=release $(QMAKE_DIRS) \
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
# Qt's .jar files too: androiddeployqt copies a file only when it is newer,
# and a kit installed over another carries its release's older dates, so
# the old kit's Qt6Android.jar stayed beside the new kit's libraries and Qt
# failed in JNI_OnLoad at start (2026-11-13, the x86_64 build after the
# 6.12.0 release kit replaced the pre-release).
ANDROID_STAGED_LIBS = $(ANDROID_BUILD_DIR)/android-build/libs

android-staged-fresh:
	@case "$(ANDROID_BUILD_DIR)" in ""|/*|*..*) echo "android: unsafe ANDROID_BUILD_DIR '$(ANDROID_BUILD_DIR)'" >&2; exit 1;; esac
	@if [ -d "$(ANDROID_STAGED_LIBS)" ]; then find "$(ANDROID_STAGED_LIBS)" -type f \( -name '*.so' -o -name '*.jar' \) -print -delete; fi

android: android-fresh android-staged-fresh android-ffmpeg-fresh $(ANDROID_BUILD_DIR)/Makefile
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

# The player's own, beside the library's lists: the estate (app/) and the
# fuzznet calls it is built on, compiled as C -- the same files as
# app/fuzznet.pri names. The wire code joined that list with the pin at
# f1a6bbd (2026-11-18) and not this one, and nothing built from this list
# linked until 2026-11-20: keep the two together.
MK_FZN_SRCS := $(wildcard $(FUZZNET_DIR)/local/*.c) $(addprefix $(FUZZNET_DIR)/,hex/hex.c constant_time/constant_time.c \
               wire/status_message.c wire/generated/situ.c wire/generated/files_local.c wire/generated/status.c)
MK_FZN_OBJS := $(patsubst $(FUZZNET_DIR)/%.c,$(MK_BUILD_DIR)/fzn/%.o,$(MK_FZN_SRCS))
$(MK_BUILD_DIR)/fzn/%.o: $(FUZZNET_DIR)/%.c | $(FUZZNET_PROBE)
	@mkdir -p $(@D)
	$(CC) -std=c11 -Os -ffunction-sections -fdata-sections -MMD -MP -I$(FUZZNET_DIR) -c $< -o $@
-include $(MK_FZN_OBJS:.o=.d)

# And qtkit, the submodule at qtkit/, as its qtkit.pri names it:
# its sources and the three headers moc reads, with the portal (Qt DBus is
# in SNURRA_PKGS).
MK_QTKIT_SRCS := $(addprefix $(QTKIT_DIR)/,src/theme.cpp src/flow_layout.cpp)
MK_QTKIT_HDRS := $(addprefix $(QTKIT_DIR)/,include/qtkit/theme.h include/qtkit/flow_layout.h src/portal_listener.h)
MK_QTKIT_MOCS := $(patsubst %.h,$(MK_BUILD_DIR)/qtkit/moc_%.cpp,$(notdir $(MK_QTKIT_HDRS)))
$(MK_QTKIT_SRCS) $(MK_QTKIT_HDRS): | $(QTKIT_PROBE)
$(MK_BUILD_DIR)/qtkit/moc_%.cpp: $(QTKIT_DIR)/include/qtkit/%.h
	@mkdir -p $(@D)
	$(MOC) -DQTKIT_HAVE_DBUS $< -o $@
$(MK_BUILD_DIR)/qtkit/moc_%.cpp: $(QTKIT_DIR)/src/%.h
	@mkdir -p $(@D)
	$(MOC) -DQTKIT_HAVE_DBUS $< -o $@

# The window's icon, compiled in (packaging/icon.qrc), as snurra.pro has it.
RCC ?= $(shell $(QMAKE) -query QT_HOST_LIBEXECS)/rcc
$(MK_BUILD_DIR)/qrc_icon.cpp: packaging/icon.qrc $(wildcard packaging/icon/*.png)
	@mkdir -p $(@D)
	$(RCC) --name icon $< -o $@

$(MK_BUILD_DIR)/snurra: $(SNURRA_SRCS) src/main.cpp app/estate.cpp app/estate.h $(MK_MOCS) $(SNURRA_HDRS) $(MK_FZN_OBJS) $(MK_QTKIT_SRCS) $(MK_QTKIT_HDRS) $(MK_QTKIT_MOCS) $(MK_BUILD_DIR)/qrc_icon.cpp VERSION
	$(CXX) $(MK_FLAGS) -Iapp -I$(FUZZNET_DIR) -I$(QTKIT_DIR)/include -I$(QTKIT_DIR)/src -DQTKIT_HAVE_DBUS -DSNURRA_VERSION_TEXT='"$(VERSION)"' $(SNURRA_SRCS) src/main.cpp app/estate.cpp $(MK_MOCS) $(MK_FZN_OBJS) $(MK_QTKIT_SRCS) $(MK_QTKIT_MOCS) $(MK_BUILD_DIR)/qrc_icon.cpp -Wl,--gc-sections -o $@ $(MK_LIBS)

check-mk: $(MK_BUILD_DIR)/snurra tests-build fuzznetd
	SNURRA_BIN=$(abspath $(MK_BUILD_DIR)/snurra) SNURRA_FUZZNETD=$(FUZZNETD) timeout -k 10 $(TEST_TIMEOUT) ./$(TEST_BIN)

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

.PHONY: all tests-build test test-estate-loss fuzznetd fuzznetd-faults check-mk install uninstall android android-staged-fresh android-ffmpeg-fresh style style-tool hooks help FORCE
FORCE:
