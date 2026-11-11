# snurra -- see project.md.
#
# TARGETS
#   make            the player, in $(BUILD_DIR)/snurra
#   make test       build and run the handover tests (never built by plain make)
#   make style      the style gate: whitespace, indentation, and the docs
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

$(BUILD_DIR)/Makefile: snurra.pro
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

# The gate's own suite first, since a gate's pass means something only once
# the gate is known to work; both are copied from ~/.claude/tool/.
style:
	python3 tool/test_style_gate.py
	python3 tool/style_gate.py check
	python3 tool/style_gate.py docs

hooks:
	@dir=$$(git rev-parse --git-common-dir 2>/dev/null); \
	if [ -z "$$dir" ]; then echo "hooks: not a git repository" >&2; exit 1; fi; \
	mkdir -p "$$dir/hooks"; \
	install -m 0755 tool/hooks/commit-msg "$$dir/hooks/commit-msg"; \
	echo "hooks: commit-msg installed into $$dir/hooks/"

help:
	@sed -n '/^# TARGETS/,/^#$$/p' $(firstword $(MAKEFILE_LIST)) | sed 's/^# \{0,1\}//'

.PHONY: all tests-build test style hooks help FORCE
FORCE:
