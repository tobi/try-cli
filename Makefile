VERSION := $(shell cat VERSION 2>/dev/null || echo "dev")

CC ?= gcc
CFLAGS += -Wall -Wextra -Werror -Wpedantic -Wshadow -Wstrict-prototypes \
          -Wno-unused-function -std=c11 -Isrc/libs -DTRY_VERSION=\"$(VERSION)\"
LDFLAGS ?=

SRC_DIR = src
OBJ_DIR = obj
DIST_DIR = dist
BIN = $(DIST_DIR)/try

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = obj/commands.o obj/main.o obj/terminal.o obj/tui.o obj/tui_style.o obj/utils.o obj/fuzzy.o

all: $(BIN)

$(BIN): $(OBJS) | $(DIST_DIR)
	$(CC) $(LDFLAGS) -o $@ $^ -lm

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(DIST_DIR):
	mkdir -p $(DIST_DIR)

clean:
	rm -rf $(OBJ_DIR) $(DIST_DIR)

install: $(BIN)
	install -m 755 $(BIN) /usr/local/bin/try

# Fetch specs (clones if needed, pulls latest, creates upstream symlink)
spec-update:
	@./spec/get_specs.sh

test-fast: $(BIN) spec-update
	@echo "Running spec tests..."
	spec/upstream/tests/runner.sh ./dist/try

test-valgrind: $(BIN) spec-update
	@echo "Running spec tests under valgrind..."
	spec/upstream/tests/runner.sh "valgrind -q --leak-check=full ./dist/try"

test: test-fast
	@command -v valgrind >/dev/null 2>&1 && $(MAKE) test-valgrind || echo "Skipping valgrind tests (valgrind not installed)"

# Update PKGBUILD and .SRCINFO with current VERSION
update-pkg:
	@sed -i 's/^pkgver=.*/pkgver=$(VERSION)/' PKGBUILD
	@makepkg --printsrcinfo > .SRCINFO
	@echo "Updated PKGBUILD and .SRCINFO to version $(VERSION)"

.PHONY: all clean install test test-fast test-valgrind spec-update update-pkg
