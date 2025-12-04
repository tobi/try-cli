#include "tui_style.h"
#include <stdarg.h>
#include <string.h>

// ============================================================================
// Style Parsing
// ============================================================================

int tui_style_flags(const char *style) {
  if (!style) return 0;
  int flags = 0;
  const char *p = style;
  while (*p) {
    if (*p == '\033' && *(p+1) == '[') {
      p += 2;
      while (*p) {
        int code = 0;
        while (*p >= '0' && *p <= '9') {
          code = code * 10 + (*p - '0');
          p++;
        }
        if (code == 1) flags |= TUI_CHANGES_BOLD;
        else if (code == 2) flags |= TUI_CHANGES_DIM;
        else if ((code >= 30 && code <= 37) || code == 38 || code == 39 ||
                 (code >= 90 && code <= 97)) flags |= TUI_CHANGES_FG;
        else if ((code >= 40 && code <= 47) || code == 48 || code == 49 ||
                 (code >= 100 && code <= 107)) flags |= TUI_CHANGES_BG;
        if (*p == ';') p++;
        else if (*p == 'm') { p++; break; }
        else break;
      }
    } else {
      p++;
    }
  }
  return flags;
}

// ============================================================================
// Style Stack
// ============================================================================

TuiStyles tui_styles(void) {
  TuiStyles st = {0};
  st.stack[0] = (TuiStyleFrame){.fg = -1, .bg = -1, .bold = false};
  st.depth = 0;
  return st;
}

// ============================================================================
// TuiStyleString Creation
// ============================================================================

TuiStyleString tui_start_zstr(zstr *s) {
  zstr_clear(s);
  return (TuiStyleString){.str = s, .styles = tui_styles()};
}

TuiStyleString tui_start_line(zstr *s) {
  return tui_start_zstr(s);
}

TuiStyleString tui_wrap_zstr(zstr *s) {
  return (TuiStyleString){.str = s, .styles = tui_styles()};
}

// ============================================================================
// Internal Helpers
// ============================================================================

static void tui_reemit_flags(TuiStyleString *ss, int flags) {
  if (tui_no_colors) return;
  for (int i = 1; i <= ss->styles.depth; i++) {
    if (ss->styles.style_flags[i] & flags) {
      zstr_cat(ss->str, ss->styles.style_strs[i]);
    }
  }
}

static void tui_emit_resets(TuiStyleString *ss, int flags) {
  if (tui_no_colors) return;
  if (flags & TUI_CHANGES_BOLD) zstr_cat(ss->str, ANSI_BOLD_OFF);
  if (flags & TUI_CHANGES_DIM) zstr_cat(ss->str, ANSI_DIM_OFF);
  if (flags & TUI_CHANGES_FG) zstr_cat(ss->str, ANSI_RESET_FG);
  if (flags & TUI_CHANGES_BG) zstr_cat(ss->str, ANSI_RESET_BG);
}

// ============================================================================
// TuiStyleString Operations
// ============================================================================

void tui_push(TuiStyleString *ss, const char *style) {
  if (!style || !*style) return;
  if (ss->styles.depth >= TUI_STYLE_STACK_MAX - 1) return;

  ss->styles.depth++;
  size_t len = strlen(style);
  if (len >= TUI_STYLE_STR_MAX) len = TUI_STYLE_STR_MAX - 1;
  memcpy(ss->styles.style_strs[ss->styles.depth], style, len);
  ss->styles.style_strs[ss->styles.depth][len] = '\0';
  ss->styles.style_flags[ss->styles.depth] = tui_style_flags(style);

  if (!tui_no_colors) {
    zstr_cat(ss->str, style);
  }
}

void tui_pop(TuiStyleString *ss) {
  if (ss->styles.depth <= 0) return;
  int flags = ss->styles.style_flags[ss->styles.depth];
  ss->styles.depth--;
  if (!tui_no_colors && flags) {
    tui_emit_resets(ss, flags);
    tui_reemit_flags(ss, flags);
  }
}

void tui_print(TuiStyleString *ss, const char *style, const char *text) {
  int flags = 0;
  if (style && *style && !tui_no_colors) {
    flags = tui_style_flags(style);
    zstr_cat(ss->str, style);
  }
  zstr_cat(ss->str, text);
  if (flags && !tui_no_colors) {
    tui_emit_resets(ss, flags);
    tui_reemit_flags(ss, flags);
  }
}

void tui_putc(TuiStyleString *ss, char c) {
  zstr_push(ss->str, c);
}

void tui_printf(TuiStyleString *ss, const char *style, const char *fmt, ...) {
  int flags = 0;
  if (style && *style && !tui_no_colors) {
    flags = tui_style_flags(style);
    zstr_cat(ss->str, style);
  }
  va_list args, args2;
  va_start(args, fmt);
  va_copy(args2, args);
  int len = vsnprintf(NULL, 0, fmt, args);
  va_end(args);
  if (len > 0) {
    size_t old_len = zstr_len(ss->str);
    zstr_reserve(ss->str, old_len + len);
    vsnprintf(zstr_data(ss->str) + old_len, len + 1, fmt, args2);
    if (ss->str->is_long)
      ss->str->l.len += len;
    else
      ss->str->s.len += (uint8_t)len;
  }
  va_end(args2);
  if (flags && !tui_no_colors) {
    tui_emit_resets(ss, flags);
    tui_reemit_flags(ss, flags);
  }
}

// ============================================================================
// Screen API
// ============================================================================

Tui tui_begin_screen(FILE *f) {
  fputs(ANSI_HIDE_CURSOR ANSI_HOME, f);
  return (Tui){.file = f,
               .line_buf = zstr_init(),
               .row = 1,
               .cursor_row = -1,
               .cursor_col = -1,
               .line_has_selection = false};
}

TuiStyleString tui_screen_line(Tui *t) {
  zstr_clear(&t->line_buf);
  t->line_has_selection = false;
  return (TuiStyleString){.str = &t->line_buf, .styles = tui_styles()};
}

TuiStyleString tui_screen_line_selected(Tui *t) {
  zstr_clear(&t->line_buf);
  t->line_has_selection = true;
  TuiStyleString ss = {.str = &t->line_buf, .styles = tui_styles()};
  tui_push(&ss, TUI_SELECTED);
  return ss;
}

void tui_screen_write(Tui *t, TuiStyleString *line) {
  if (t->line_has_selection) {
    tui_pop(line);
    t->line_has_selection = false;
  }
  zstr_cat(&t->line_buf, ANSI_CLR "\n");
  fwrite(zstr_cstr(&t->line_buf), 1, zstr_len(&t->line_buf), t->file);
  t->row++;
}

void tui_screen_empty(Tui *t) {
  fputs(ANSI_CLR "\n", t->file);
  t->row++;
}

void tui_screen_clear_rest(Tui *t) {
  fputs(ANSI_CLS, t->file);
}

void tui_end_screen(Tui *t) {
  if (t->cursor_row >= 0 && t->cursor_col >= 0) {
    fprintf(t->file, "\033[%d;%dH", t->cursor_row, t->cursor_col);
  }
  fputs(ANSI_SHOW_CURSOR, t->file);
  zstr_free(&t->line_buf);
}

void tui_screen_input(Tui *t, const char *text, int cursor_pos) {
  int len = text ? (int)strlen(text) : 0;
  if (cursor_pos < 0) cursor_pos = 0;
  if (cursor_pos > len) cursor_pos = len;

  int visual_col = 0;
  const char *p = zstr_cstr(&t->line_buf);
  while (*p) {
    if (*p == '\033') {
      while (*p && *p != 'm') p++;
      if (*p) p++;
    } else {
      visual_col++;
      p++;
    }
  }

  zstr_cat_len(&t->line_buf, text, cursor_pos);
  t->cursor_col = visual_col + cursor_pos + 1;
  t->cursor_row = t->row;

  if (cursor_pos < len) {
    zstr_cat(&t->line_buf, text + cursor_pos);
  }
}

// ============================================================================
// zstr Utilities
// ============================================================================

void tui_clr(zstr *s) {
  zstr_cat(s, ANSI_CLR);
}

void tui_zstr_printf(zstr *s, const char *style, const char *text) {
  if (style && *style && !tui_no_colors)
    zstr_cat(s, style);
  zstr_cat(s, text);
  if (style && *style && !tui_no_colors)
    zstr_cat(s, ANSI_RESET);
}

// ============================================================================
// Direct Output
// ============================================================================

void tui_write(FILE *f, const char *s) { fputs(s, f); }
void tui_write_clr(FILE *f) { fputs(ANSI_CLR, f); }
void tui_write_cls(FILE *f) { fputs(ANSI_CLS, f); }
void tui_write_home(FILE *f) { fputs(ANSI_HOME, f); }
void tui_write_reset(FILE *f) { fputs(ANSI_RESET, f); }
void tui_write_hide_cursor(FILE *f) { fputs(ANSI_HIDE_CURSOR, f); }
void tui_write_show_cursor(FILE *f) { fputs(ANSI_SHOW_CURSOR, f); }
void tui_write_goto(FILE *f, int row, int col) {
  fprintf(f, "\033[%d;%dH", row, col);
}
void tui_flush(FILE *f, zstr *s) {
  fwrite(zstr_cstr(s), 1, zstr_len(s), f);
}
