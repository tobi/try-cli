#ifndef TUI_STYLE_H
#define TUI_STYLE_H

#include "libs/zstr.h"
#include <stdbool.h>
#include <stdio.h>

// ============================================================================
// ANSI Escape Code Constants
// ============================================================================

// Attributes
#define ANSI_RESET "\033[0m"
#define ANSI_BOLD "\033[1m"
#define ANSI_DIM "\033[2m"
#define ANSI_ITALIC "\033[3m"
#define ANSI_UNDERLINE "\033[4m"
#define ANSI_REVERSE "\033[7m"
#define ANSI_STRIKE "\033[9m"

// Standard foreground colors
#define ANSI_BLACK "\033[30m"
#define ANSI_RED "\033[31m"
#define ANSI_GREEN "\033[32m"
#define ANSI_YELLOW "\033[33m"
#define ANSI_BLUE "\033[34m"
#define ANSI_MAGENTA "\033[35m"
#define ANSI_CYAN "\033[36m"
#define ANSI_WHITE "\033[37m"
#define ANSI_GRAY "\033[90m"
#define ANSI_GREY "\033[90m"

// Bright foreground colors
#define ANSI_BRIGHT_BLACK "\033[90m"
#define ANSI_BRIGHT_RED "\033[91m"
#define ANSI_BRIGHT_GREEN "\033[92m"
#define ANSI_BRIGHT_YELLOW "\033[93m"
#define ANSI_BRIGHT_BLUE "\033[94m"
#define ANSI_BRIGHT_MAGENTA "\033[95m"
#define ANSI_BRIGHT_CYAN "\033[96m"
#define ANSI_BRIGHT_WHITE "\033[97m"

// Standard background colors
#define ANSI_BG_BLACK "\033[40m"
#define ANSI_BG_RED "\033[41m"
#define ANSI_BG_GREEN "\033[42m"
#define ANSI_BG_YELLOW "\033[43m"
#define ANSI_BG_BLUE "\033[44m"
#define ANSI_BG_MAGENTA "\033[45m"
#define ANSI_BG_CYAN "\033[46m"
#define ANSI_BG_WHITE "\033[47m"
#define ANSI_BG_GRAY "\033[100m"

// Semantic composite styles
#define ANSI_HIGHLIGHT "\033[1;33m"
#define ANSI_H1 "\033[1;38;5;214m"
#define ANSI_H2 "\033[1;34m"
#define ANSI_H3 "\033[1;37m"
#define ANSI_DARK "\033[38;5;245m"
#define ANSI_SECTION "\033[1;48;5;237m"
#define ANSI_DANGER "\033[48;5;52m"

// TUI_* semantic style aliases
#define TUI_BOLD ANSI_BOLD
#define TUI_DIM ANSI_DIM
#define TUI_DARK ANSI_DARK
#define TUI_H1 ANSI_H1
#define TUI_H2 ANSI_H2
#define TUI_HIGHLIGHT ANSI_HIGHLIGHT
#define TUI_MATCH "\033[38;5;11m"
#define TUI_SELECTED "\033[48;5;237m"
#define TUI_DANGER ANSI_DANGER
#define TUI_BG_RED ANSI_BG_RED
#define TUI_BG_BLUE ANSI_BG_BLUE

// Control sequences
#define ANSI_CLR "\033[K"
#define ANSI_CLS "\033[J"
#define ANSI_HOME "\033[H"
#define ANSI_HIDE_CURSOR "\033[?25l"
#define ANSI_SHOW_CURSOR "\033[?25h"

// Reset specific attributes
#define ANSI_RESET_FG "\033[39m"
#define ANSI_RESET_BG "\033[49m"
#define ANSI_BOLD_OFF "\033[22m"
#define ANSI_DIM_OFF "\033[22m"

// Style flags
#define TUI_CHANGES_FG   (1 << 0)
#define TUI_CHANGES_BG   (1 << 1)
#define TUI_CHANGES_BOLD (1 << 2)
#define TUI_CHANGES_DIM  (1 << 3)

// ============================================================================
// Global Color Toggle
// ============================================================================

extern bool tui_no_colors;

// ============================================================================
// Types
// ============================================================================

typedef struct {
  int fg;
  int bg;
  bool bold;
} TuiStyleFrame;

#define TUI_STYLE_STACK_MAX 8
#define TUI_STYLE_STR_MAX 32

typedef struct {
  TuiStyleFrame stack[TUI_STYLE_STACK_MAX];
  char style_strs[TUI_STYLE_STACK_MAX][TUI_STYLE_STR_MAX];
  int style_flags[TUI_STYLE_STACK_MAX];
  int depth;
} TuiStyles;

typedef struct {
  zstr *str;
  TuiStyles styles;
} TuiStyleString;

// Text input field state (forward declare)
typedef struct {
  zstr text;
  int cursor;
  const char *placeholder;  // Optional placeholder shown when empty
} TuiInput;

typedef struct {
  FILE *file;
  zstr line_buf;
  int row;
  int cursor_row;
  int cursor_col;
  bool line_has_selection;
  TuiInput *active_input;  // Input field with cursor (if any)
} Tui;

// ============================================================================
// Function Declarations
// ============================================================================

int tui_style_flags(const char *style);
TuiStyles tui_styles(void);

TuiStyleString tui_start_zstr(zstr *s);
TuiStyleString tui_start_line(zstr *s);
TuiStyleString tui_wrap_zstr(zstr *s);

void tui_push(TuiStyleString *ss, const char *style);
void tui_pop(TuiStyleString *ss);
void tui_print(TuiStyleString *ss, const char *style, const char *text);
void tui_putc(TuiStyleString *ss, char c);
void tui_printf(TuiStyleString *ss, const char *style, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

Tui tui_begin_screen(FILE *f);
TuiStyleString tui_screen_line(Tui *t);
TuiStyleString tui_screen_line_selected(Tui *t);
void tui_screen_write(Tui *t, TuiStyleString *line);
void tui_screen_empty(Tui *t);
void tui_screen_clear_rest(Tui *t);
void tui_end_screen(Tui *t);
void tui_screen_input(Tui *t, TuiInput *input);

// Input field management
TuiInput tui_input_init(void);
void tui_input_free(TuiInput *input);
void tui_input_clear(TuiInput *input);
bool tui_input_handle_key(TuiInput *input, int key);

// Convenience: handle key for active input on screen
bool tui_handle_key(Tui *t, int key);

void tui_clr(zstr *s);
void tui_zstr_printf(zstr *s, const char *style, const char *text);

void tui_write(FILE *f, const char *s);
void tui_write_clr(FILE *f);
void tui_write_cls(FILE *f);
void tui_write_home(FILE *f);
void tui_write_reset(FILE *f);
void tui_write_hide_cursor(FILE *f);
void tui_write_show_cursor(FILE *f);
void tui_write_goto(FILE *f, int row, int col);
void tui_flush(FILE *f, zstr *s);

#endif /* TUI_STYLE_H */
