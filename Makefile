CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=c11 -D_POSIX_C_SOURCE=200809L -g -Ilibs
LDFLAGS = 

SRC_DIR = src
OBJ_DIR = obj
BIN = try

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = obj/commands.o obj/main.o obj/terminal.o obj/tui.o obj/utils.o obj/fuzzy.o

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ -lm

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

clean:
	rm -rf $(OBJ_DIR) $(BIN)

install: $(BIN)
	install -m 755 $(BIN) /usr/local/bin/try

.PHONY: all clean install
