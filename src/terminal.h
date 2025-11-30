#ifndef TERMINAL_H
#define TERMINAL_H

#include <termios.h>

// Key definitions
enum EditorKey {
  BACKSPACE = 127,
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN,
  ENTER_KEY = 13,
  ESC_KEY = 27,
  KEY_RESIZE = -2
};

void enable_raw_mode();
void disable_raw_mode();
int get_window_size(int *rows, int *cols);
int read_key();
void clear_screen();
void hide_cursor();
void show_cursor();

#endif // TERMINAL_H
