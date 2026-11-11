# snurra -- see project.md.
#
# TARGETS
#   make            nothing yet: the library and player arrive with the first code
#   make style      the style gate: whitespace, indentation, and the docs
#   make hooks      install the commit-msg hook
#   make help       this list
#

BUILD_DIR ?= build

.DEFAULT_GOAL := all

# project.md sec 4: no software yet, so the default says so and succeeds.
all:
	@echo "snurra: nothing to build yet -- see project.md"

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

.PHONY: all style hooks help
