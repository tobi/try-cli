// Feature test macros for cross-platform compatibility
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#else
#define _GNU_SOURCE
#endif

/*
 * Terminal handling - Cross-platform notes:
 *
 * Linux:
 *   - Uses TIOCGWINSZ ioctl for window size
 *   - SIGWINCH delivered on terminal resize
 *   - termios API fully supported
 *
 * macOS:
 *   - Same APIs as Linux (POSIX compliant)
 *   - Requires _DARWIN_C_SOURCE for some features
 *   - SIGWINCH works the same way
 *
 * Windows (future):
 *   - Would need Windows Console API or ANSI via ConPTY
 *   - GetConsoleScreenBufferInfo() for window size
 *   - WINDOW_BUFFER_SIZE_EVENT for resize
 *   - Consider using a library like crossterm or PDCurses
 *
 * BSD variants:
 *   - Generally POSIX compliant like Linux/macOS
 *   - May need _BSD_SOURCE or specific feature macros
 */

#include "terminal.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

// Helper macro to ignore write return values
#define WRITE(fd, buf, len) do { ssize_t unused = write(fd, buf, len); (void)unused; } while(0)

static struct termios orig_termios;
static int raw_mode_enabled = 0;
static int alternate_screen_enabled = 0;

// Window size cache
static int cached_rows = 0;
static int cached_cols = 0;
static int window_size_valid = 0;

/*
 * Emergency cleanup - called on signal or atexit
 * Ensures terminal is always restored even on abnormal exit
 */
static void emergency_cleanup(void) {
  // Disable alternate screen first (most critical)
  if (alternate_screen_enabled) {
    WRITE(STDERR_FILENO, "\x1b[?1049l", 9);
    alternate_screen_enabled = 0;
  }
  // Restore terminal mode
  if (raw_mode_enabled) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    raw_mode_enabled = 0;
  }
  // Show cursor
  WRITE(STDERR_FILENO, "\x1b[?25h", 6);
  // Reset all attributes
  WRITE(STDERR_FILENO, "\x1b[0m", 4);
}

/*
 * Signal handler for abnormal termination (SIGINT, SIGTERM, SIGABRT)
 */
static void handle_signal(int sig) {
  exit(128 + sig);  // Standard convention for signal termination
}

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

  // Register emergency cleanup on exit (most important!)
  atexit(emergency_cleanup);

  // Register signal handlers to catch abnormal termination
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = handle_signal;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGINT, &sa, NULL);   // Ctrl-C
  sigaction(SIGTERM, &sa, NULL);  // Termination signal
  sigaction(SIGABRT, &sa, NULL);  // Abort signal

  struct termios raw = orig_termios;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  // Keep OPOST enabled so \n still converts to \r\n - safer for terminal state
  raw.c_cflag |= (CS8);
  // Note: Keep ISIG enabled so SIGWINCH can interrupt read() for resize handling
  // IEXTEN disabled to prevent Ctrl-V literal mode
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN);

  // Timeout for read
  raw.c_cc[VMIN] = 1;
  raw.c_cc[VTIME] = 0;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    return;
  raw_mode_enabled = 1;
  hide_cursor();
}

/*
 * Read a single keypress, handling escape sequences.
 * Returns:
 *   - Positive: ASCII char or special key constant (ARROW_UP, etc.)
 *   - KEY_RESIZE (-2): SIGWINCH interrupted read, caller should redraw
 *   - -1: EOF or error
 *
 * Resize handling:
 *   When the terminal is resized, SIGWINCH is delivered, which interrupts
 *   the blocking read() with EINTR. We return KEY_RESIZE so the caller
 *   can call get_window_size() and redraw the UI.
 */
int read_key(void) {
  int nread;
  unsigned char c;
  // Blocking read (VMIN=1, VTIME=0 set in enable_raw_mode)
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1) {
      if (errno == EAGAIN)
        continue;
      if (errno == EINTR) {
        window_size_valid = 0; // Invalidate cache on resize
        return KEY_RESIZE; // Signal interrupted read (likely SIGWINCH)
      }
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
      // SGR mouse: \x1b[<Ps;Ps;PsM or \x1b[<Ps;Ps;Psm
      if (seq[1] == '<') {
        char discard;
        // Consume until 'M' or 'm' (mouse button release/press)
        while (read(STDIN_FILENO, &discard, 1) == 1) {
          if (discard == 'M' || discard == 'm')
            break;
        }
        return KEY_UNKNOWN;
      }
      // X10 mouse: \x1b[M followed by 3 bytes
      if (seq[1] == 'M') {
        char discard[3];
        read(STDIN_FILENO, discard, 3);  // Consume button + coordinates
        return KEY_UNKNOWN;
      }
      // Simple arrow keys and similar
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
      // Sequences starting with digit: \x1b[1~ (Home), \x1b[3~ (Del), etc.
      // Also handles urxvt mouse: \x1b[96;32;15M
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1)
          return KEY_UNKNOWN;
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
          return KEY_UNKNOWN;
        }
        // Any other CSI sequence starting with digit - consume until terminator
        // CSI sequences end with a byte in range 0x40-0x7E (@ through ~)
        char last = seq[2];
        while (!(last >= 0x40 && last <= 0x7E)) {
          if (read(STDIN_FILENO, &last, 1) != 1)
            break;
        }
        return KEY_UNKNOWN;
      }
      // Any other unrecognized CSI sequence - consume until terminator
      if (!(seq[1] >= 0x40 && seq[1] <= 0x7E)) {
        char last = seq[1];
        while (!(last >= 0x40 && last <= 0x7E)) {
          if (read(STDIN_FILENO, &last, 1) != 1)
            break;
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
    return KEY_UNKNOWN;  // Unrecognized escape sequence - don't treat as ESC
  } else {
    return c;
  }
}

int get_window_size(int *rows, int *cols) {
  // Return cached values if valid
  if (window_size_valid) {
    *rows = cached_rows;
    *cols = cached_cols;
    return 0;
  }

  // Check TRY_WIDTH/TRY_HEIGHT env vars first (for testing)
  const char *env_width = getenv("TRY_WIDTH");
  const char *env_height = getenv("TRY_HEIGHT");
  if (env_width && env_height) {
    *cols = atoi(env_width);
    *rows = atoi(env_height);
    if (*cols > 0 && *rows > 0) {
      goto cache_and_return;
    }
  } else if (env_width) {
    *cols = atoi(env_width);
    if (*cols > 0) {
      *rows = 24;  // Default height
      goto cache_and_return;
    }
  }

  struct winsize ws;

  // 1. Try ioctl on STDERR
  if (ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) != -1 && ws.ws_col != 0) {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    goto cache_and_return;
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
          goto cache_and_return;
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

cache_and_return:
  cached_rows = *rows;
  cached_cols = *cols;
  window_size_valid = 1;
  return 0;
}

void enable_alternate_screen(void) {
  // Enable alternate screen buffer (saves current screen, switches to blank alternate)
  if (!alternate_screen_enabled) {
    WRITE(STDERR_FILENO, "\x1b[?1049h", 9);
    alternate_screen_enabled = 1;
  }
}

void disable_alternate_screen(void) {
  // Disable alternate screen buffer (restores original screen)
  if (alternate_screen_enabled) {
    WRITE(STDERR_FILENO, "\x1b[?1049l", 9);
    alternate_screen_enabled = 0;
  }
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
