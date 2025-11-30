CC ?= gcc
CFLAGS ?= -Wall -Wextra -Werror -Wpedantic -Wshadow -Wstrict-prototypes \
          -Wno-unused-function -std=c11 -g -Isrc/libs
LDFLAGS ?=

SRC_DIR = src
OBJ_DIR = obj
DIST_DIR = dist
BIN = $(DIST_DIR)/try

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = obj/commands.o obj/main.o obj/terminal.o obj/tui.o obj/utils.o obj/fuzzy.o

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

test: $(BIN)
	./test/test.sh

.PHONY: all clean install test
