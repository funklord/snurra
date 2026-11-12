# snurra -- see project.md.
#
# TARGETS
#   make            the player, in $(BUILD_DIR)/snurra
#   make test       build and run the handover and codec tests (never built by plain make)
#   make check-mk   build the player from snurra.mk alone, and run the tests on it
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
# hung would otherwise wait for ever.
TEST_TIMEOUT ?= 120

.DEFAULT_GOAL := all

all: $(BIN)

$(BUILD_DIR)/Makefile: snurra.pro snurra.pri
	mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && $(QMAKE) ../snurra.pro CONFIG+=release

$(BIN): $(BUILD_DIR)/Makefile FORCE
	$(MAKE) -C $(BUILD_DIR)

$(TEST_BUILD_DIR)/Makefile: test/test.pro
	mkdir -p $(TEST_BUILD_DIR)
	cd $(TEST_BUILD_DIR) && $(QMAKE) ../test/test.pro SNURRA_BIN=$(abspath $(BIN))

tests-build: $(TEST_BUILD_DIR)/Makefile FORCE
	$(MAKE) -C $(TEST_BUILD_DIR)

# A run over no binary is a failure, not a pass.
test: $(BIN) tests-build
	@[ -x $(TEST_BIN) ] || { echo "test: no test binary at $(TEST_BIN)" >&2; exit 1; }
	timeout $(TEST_TIMEOUT) ./$(TEST_BIN)

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
MK_FLAGS  = -std=c++17 -fPIC -Os -I$(SNURRA_INCLUDE) $(shell pkg-config --cflags $(MK_QT) $(SNURRA_PKGS))
MK_LIBS   = $(shell pkg-config --libs $(MK_QT) $(SNURRA_PKGS))

$(MK_BUILD_DIR)/moc_%.cpp: $(SNURRA_INCLUDE)/snurra/%.h
	@mkdir -p $(MK_BUILD_DIR)
	$(MOC) $< -o $@

$(MK_BUILD_DIR)/snurra: $(SNURRA_SRCS) src/main.cpp $(MK_MOCS) $(SNURRA_HDRS)
	$(CXX) $(MK_FLAGS) $(SNURRA_SRCS) src/main.cpp $(MK_MOCS) -o $@ $(MK_LIBS)

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
	mkdir -p "$$dir/hooks"; \
	install -m 0755 tool/hooks/commit-msg "$$dir/hooks/commit-msg"; \
	echo "hooks: commit-msg installed into $$dir/hooks/"

help:
	@sed -n '/^# TARGETS/,/^#$$/p' $(firstword $(MAKEFILE_LIST)) | sed 's/^# \{0,1\}//'

.PHONY: all tests-build test check-mk style style-tool hooks help FORCE
FORCE:
