// Feature test macros for cross-platform compatibility
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#else
#define _GNU_SOURCE
#endif

#include "terminal.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

static struct termios orig_termios;
static int raw_mode_enabled = 0;

void disable_raw_mode(void) {
  if (raw_mode_enabled) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    raw_mode_enabled = 0;
    show_cursor();
  }
}

void enable_raw_mode(void) {
  if (tcgetattr(STDIN_FILENO, &orig_termios) == -1)
    return; // Not a TTY?

  atexit(disable_raw_mode);

  struct termios raw = orig_termios;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

  // Timeout for read
  raw.c_cc[VMIN] = 1;
  raw.c_cc[VTIME] = 0;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    return;
  raw_mode_enabled = 1;
  hide_cursor();
}

int read_key(void) {
  int nread;
  unsigned char c;
  // Blocking read (VMIN=1, VTIME=0 set in enable_raw_mode)
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1) {
      if (errno == EAGAIN)
        continue;
      if (errno == EINTR)
        return -2; // KEY_RESIZE
      return -1;
    }
    if (nread == 0) {
      return -1; // EOF
    }
  }

  if (c == '\x1b') {
    char seq[3];
    // Non-blocking read for sequence? No, we are in blocking mode.
    // But escape sequences come quickly.
    // If we block here, a standalone ESC key will hang until next key.
    // We need to handle this.
    // Usually, we set VTIME to a small value for these subsequent reads?
    // Or we use non-blocking read for the sequence parts.

    // Let's try to read with a short timeout or just assume if it's an escape
    // sequence, bytes are there. But for a robust TUI, we should check if bytes
    // are available. For now, let's just try to read. If it blocks, it blocks.
    // Wait, if user presses ESC, we don't want to block.

    // Simple hack: set non-blocking for the next bytes
    struct termios original_state;
    memset(&original_state, 0, sizeof(original_state));
    tcgetattr(STDIN_FILENO, &original_state);
    struct termios nonblock;
    memcpy(&nonblock, &original_state, sizeof(nonblock));
    nonblock.c_cc[VMIN] = 0;
    nonblock.c_cc[VTIME] = 1; // 100ms
    tcsetattr(STDIN_FILENO, TCSANOW, &nonblock);

    if (read(STDIN_FILENO, &seq[0], 1) != 1) {
      tcsetattr(STDIN_FILENO, TCSANOW, &original_state);
      return '\x1b';
    }
    if (read(STDIN_FILENO, &seq[1], 1) != 1) {
      tcsetattr(STDIN_FILENO, TCSANOW, &original_state);
      return '\x1b';
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &original_state);

    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1)
          return '\x1b';
        if (seq[2] == '~') {
          switch (seq[1]) {
          case '1':
            return HOME_KEY;
          case '3':
            return DEL_KEY;
          case '4':
            return END_KEY;
          case '5':
            return PAGE_UP;
          case '6':
            return PAGE_DOWN;
          case '7':
            return HOME_KEY;
          case '8':
            return END_KEY;
          }
        }
      } else {
        switch (seq[1]) {
        case 'A':
          return ARROW_UP;
        case 'B':
          return ARROW_DOWN;
        case 'C':
          return ARROW_RIGHT;
        case 'D':
          return ARROW_LEFT;
        case 'H':
          return HOME_KEY;
        case 'F':
          return END_KEY;
        }
      }
    } else if (seq[0] == 'O') {
      switch (seq[1]) {
      case 'H':
        return HOME_KEY;
      case 'F':
        return END_KEY;
      }
    }
    return '\x1b';
  } else {
    return c;
  }
}

int get_window_size(int *rows, int *cols) {
  struct winsize ws;

  // 1. Try ioctl on STDERR
  if (ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) != -1 && ws.ws_col != 0) {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }

  // 2. Try tput
  FILE *fp = popen("tput cols", "r");
  if (fp) {
    if (fscanf(fp, "%d", cols) == 1) {
      pclose(fp);
      fp = popen("tput lines", "r");
      if (fp) {
        if (fscanf(fp, "%d", rows) == 1) {
          pclose(fp);
          return 0;
        }
        pclose(fp);
      }
    } else {
      pclose(fp);
    }
  }

  // 3. Fallback defaults
  *cols = 80;
  *rows = 24;
  return 0;
}

void clear_screen(void) {
  // Clear screen and home cursor
  ssize_t unused1 = write(STDERR_FILENO, "\x1b[2J", 4);
  ssize_t unused2 = write(STDERR_FILENO, "\x1b[H", 3);
  (void)unused1; (void)unused2;
}

void hide_cursor(void) {
  ssize_t unused = write(STDERR_FILENO, "\x1b[?25l", 6);
  (void)unused;
}

void show_cursor(void) {
  ssize_t unused = write(STDERR_FILENO, "\x1b[?25h", 6);
  (void)unused;
}
