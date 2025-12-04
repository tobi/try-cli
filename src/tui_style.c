#include "tui_style.h"
#include "terminal.h"
#include <ctype.h>
#include <stdarg.h>
#include <string.h>

// ============================================================================
// Style Parsing
// ============================================================================

int tui_style_flags(const char *style) {
  if (!style)
    return 0;
  int flags = 0;
  const char *p = style;
  while (*p) {
    if (*p == '\033' && *(p + 1) == '[') {
      p += 2;
      while (*p) {
        int code = 0;
        while (*p >= '0' && *p <= '9') {
          code = code * 10 + (*p - '0');
          p++;
        }
        if (code == 1)
          flags |= TUI_CHANGES_BOLD;
        else if (code == 2)
          flags |= TUI_CHANGES_DIM;
        else if ((code >= 30 && code <= 37) || code == 38 || code == 39 ||
                 (code >= 90 && code <= 97))
          flags |= TUI_CHANGES_FG;
        else if ((code >= 40 && code <= 47) || code == 48 || code == 49 ||
                 (code >= 100 && code <= 107))
          flags |= TUI_CHANGES_BG;
        if (*p == ';')
          p++;
        else if (*p == 'm') {
          p++;
          break;
        } else
          break;
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

TuiStyleString tui_start_line(zstr *s) { return tui_start_zstr(s); }

TuiStyleString tui_wrap_zstr(zstr *s) {
  return (TuiStyleString){.str = s, .styles = tui_styles()};
}

// ============================================================================
// Internal Helpers
// ============================================================================

// Write style code to zstr only if colors are enabled
static inline void tui_style(zstr *s, const char *style) {
  if (!tui_no_colors && style && *style)
    zstr_cat(s, style);
}

static void tui_reemit_flags(TuiStyleString *ss, int flags) {
  if (tui_no_colors)
    return;
  for (int i = 1; i <= ss->styles.depth; i++) {
    if (ss->styles.style_flags[i] & flags) {
      zstr_cat(ss->str, ss->styles.style_strs[i]);
    }
  }
}

static void tui_emit_resets(TuiStyleString *ss, int flags) {
  if (tui_no_colors)
    return;
  if (flags & TUI_CHANGES_BOLD)
    zstr_cat(ss->str, ANSI_BOLD_OFF);
  if (flags & TUI_CHANGES_DIM)
    zstr_cat(ss->str, ANSI_DIM_OFF);
  if (flags & TUI_CHANGES_FG)
    zstr_cat(ss->str, ANSI_RESET_FG);
  if (flags & TUI_CHANGES_BG)
    zstr_cat(ss->str, ANSI_RESET_BG);
}

// ============================================================================
// TuiStyleString Operations
// ============================================================================

void tui_push(TuiStyleString *ss, const char *style) {
  if (!style || !*style)
    return;
  if (ss->styles.depth >= TUI_STYLE_STACK_MAX - 1)
    return;

  ss->styles.depth++;
  size_t len = strlen(style);
  if (len >= TUI_STYLE_STR_MAX)
    len = TUI_STYLE_STR_MAX - 1;
  memcpy(ss->styles.style_strs[ss->styles.depth], style, len);
  ss->styles.style_strs[ss->styles.depth][len] = '\0';
  ss->styles.style_flags[ss->styles.depth] = tui_style_flags(style);

  tui_style(ss->str, style);
}

void tui_pop(TuiStyleString *ss) {
  if (ss->styles.depth <= 0)
    return;
  int flags = ss->styles.style_flags[ss->styles.depth];
  ss->styles.depth--;
  if (!tui_no_colors && flags) {
    tui_emit_resets(ss, flags);
    tui_reemit_flags(ss, flags);
  }
}

void tui_print(TuiStyleString *ss, const char *style, const char *text) {
  int flags = 0;
  if (!tui_no_colors && style && *style) {
    flags = tui_style_flags(style);
    zstr_cat(ss->str, style);
  }
  zstr_cat(ss->str, text);
  if (flags) {  // flags is 0 when tui_no_colors, so no redundant check needed
    tui_emit_resets(ss, flags);
    tui_reemit_flags(ss, flags);
  }
}

void tui_putc(TuiStyleString *ss, char c) { zstr_push(ss->str, c); }

void tui_printf(TuiStyleString *ss, const char *style, const char *fmt, ...) {
  int flags = 0;
  if (!tui_no_colors && style && *style) {
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
  if (flags) {
    tui_emit_resets(ss, flags);
    tui_reemit_flags(ss, flags);
  }
}

// ============================================================================
// Screen API
// ============================================================================

Tui tui_begin_screen(FILE *f) {
  int rows, cols;
  get_window_size(&rows, &cols);
  (void)rows;
  fputs(ANSI_HIDE_CURSOR ANSI_HOME, f);
  return (Tui){.file = f,
               .line_buf = zstr_init(),
               .row = 1,
               .cols = cols,
               .cursor_row = -1,
               .cursor_col = -1,
               .line_has_selection = false,
               .active_input = NULL};
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

// Decode UTF-8 codepoint starting at s[i], return codepoint and advance i
static unsigned int decode_utf8(const char *s, size_t len, size_t *i) {
  unsigned char c = (unsigned char)s[*i];
  unsigned int cp = 0;
  if ((c & 0x80) == 0) {
    cp = c;
  } else if ((c & 0xE0) == 0xC0 && *i + 1 < len) {
    cp = (c & 0x1F) << 6 | ((unsigned char)s[*i + 1] & 0x3F);
    (*i)++;
  } else if ((c & 0xF0) == 0xE0 && *i + 2 < len) {
    cp = (c & 0x0F) << 12 | ((unsigned char)s[*i + 1] & 0x3F) << 6 |
         ((unsigned char)s[*i + 2] & 0x3F);
    (*i) += 2;
  } else if ((c & 0xF8) == 0xF0 && *i + 3 < len) {
    cp = (c & 0x07) << 18 | ((unsigned char)s[*i + 1] & 0x3F) << 12 |
         ((unsigned char)s[*i + 2] & 0x3F) << 6 |
         ((unsigned char)s[*i + 3] & 0x3F);
    (*i) += 3;
  }
  return cp;
}

// Get display width of a Unicode codepoint
static int codepoint_width(unsigned int cp) {
  if (cp < 0x80) return 1;                    // ASCII
  if (cp >= 0x1F300 && cp <= 0x1FAFF) return 2;  // Emojis
  if (cp >= 0x2600 && cp <= 0x27BF) return 2;    // Misc symbols, dingbats
  return 1;  // Default: arrows, box drawing, most other chars
}

// Calculate visible width of string (excluding ANSI escape sequences)
static int visible_width(const char *s, size_t len) {
  int width = 0;
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)s[i];
    if (c == '\033' && i + 1 < len && s[i + 1] == '[') {
      // Skip ANSI escape sequence
      i += 2;
      while (i < len && !((s[i] >= 'A' && s[i] <= 'Z') ||
                          (s[i] >= 'a' && s[i] <= 'z'))) {
        i++;
      }
    } else if ((c & 0xC0) == 0x80) {
      // Skip UTF-8 continuation bytes (already counted)
      continue;
    } else {
      unsigned int cp = decode_utf8(s, len, &i);
      width += codepoint_width(cp);
    }
  }
  return width;
}

// Truncate string to max visible width, preserving ANSI sequences
// Returns byte offset where truncation should occur
static size_t truncate_at_width(const char *s, size_t len, int max_width) {
  int width = 0;
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)s[i];
    if (c == '\033' && i + 1 < len && s[i + 1] == '[') {
      // Skip ANSI escape sequence (include it in output)
      i += 2;
      while (i < len && !((s[i] >= 'A' && s[i] <= 'Z') ||
                          (s[i] >= 'a' && s[i] <= 'z'))) {
        i++;
      }
    } else if ((c & 0xC0) == 0x80) {
      // Skip UTF-8 continuation bytes (already counted)
      continue;
    } else {
      size_t start = i;
      unsigned int cp = decode_utf8(s, len, &i);
      int char_width = codepoint_width(cp);
      if (width + char_width > max_width) {
        return start;
      }
      width += char_width;
    }
  }
  return len;
}

void tui_screen_write_truncated(Tui *t, TuiStyleString *line,
                                const char *overflow) {
  if (t->line_has_selection) {
    tui_pop(line);
    t->line_has_selection = false;
  }

  const char *buf = zstr_cstr(&t->line_buf);
  size_t len = zstr_len(&t->line_buf);
  int width = visible_width(buf, len);
  int overflow_len = overflow ? (int)strlen(overflow) : 0;

  if (width > t->cols) {
    // Need to truncate
    int max_content = t->cols - overflow_len;
    if (max_content < 0) max_content = 0;
    size_t trunc_pos = truncate_at_width(buf, len, max_content);

    // Write truncated content
    fwrite(buf, 1, trunc_pos, t->file);
    // Reset styles and write overflow indicator
    fputs(ANSI_RESET, t->file);
    if (overflow) fputs(overflow, t->file);
    fputs(ANSI_CLR "\n", t->file);
  } else {
    // No truncation needed
    zstr_cat(&t->line_buf, ANSI_CLR "\n");
    fwrite(zstr_cstr(&t->line_buf), 1, zstr_len(&t->line_buf), t->file);
  }
  t->row++;
}

void tui_screen_empty(Tui *t) {
  fputs(ANSI_CLR "\n", t->file);
  t->row++;
}

void tui_screen_clear_rest(Tui *t) { fputs(ANSI_CLS, t->file); }

void tui_free(Tui *t) {
  fputs(ANSI_CLS, t->file);  // Clear from cursor to end of screen
  if (t->cursor_row >= 0 && t->cursor_col >= 0) {
    fprintf(t->file, "\033[%d;%dH", t->cursor_row, t->cursor_col);
  }
  fputs(ANSI_SHOW_CURSOR, t->file);
  zstr_free(&t->line_buf);
}

void tui_screen_input(Tui *t, TuiInput *input) {
  t->active_input = input;

  const char *text = zstr_cstr(&input->text);
  int len = (int)zstr_len(&input->text);
  int cursor_pos = input->cursor;
  if (cursor_pos < 0)
    cursor_pos = 0;
  if (cursor_pos > len)
    cursor_pos = len;

  // Calculate visual column (skip ANSI codes)
  int visual_col = 0;
  const char *p = zstr_cstr(&t->line_buf);
  while (*p) {
    if (*p == '\033') {
      while (*p && *p != 'm')
        p++;
      if (*p)
        p++;
    } else {
      visual_col++;
      p++;
    }
  }

  // Set cursor position (before any text/placeholder)
  t->cursor_col = visual_col + cursor_pos + 1;
  t->cursor_row = t->row;

  // Check if input matches placeholder prefix
  bool matches_placeholder = false;
  int placeholder_len = 0;
  if (input->placeholder) {
    placeholder_len = (int)strlen(input->placeholder);
    matches_placeholder =
        (len <= placeholder_len) &&
        (len == 0 || strncmp(text, input->placeholder, len) == 0);
  }

  if (matches_placeholder && placeholder_len > 0) {
    // Show typed text in normal style
    zstr_cat_len(&t->line_buf, text, cursor_pos);
    // Cursor goes here
    if (cursor_pos < len) {
      zstr_cat(&t->line_buf, text + cursor_pos);
    }
    // Show remaining placeholder in dim
    if (len < placeholder_len) {
      zstr_cat(&t->line_buf, TUI_DIM);
      zstr_cat(&t->line_buf, input->placeholder + len);
      zstr_cat(&t->line_buf, ANSI_DIM_OFF);
    }
  } else {
    // Input diverged from placeholder - just show the text
    zstr_cat_len(&t->line_buf, text, cursor_pos);
    if (cursor_pos < len) {
      zstr_cat(&t->line_buf, text + cursor_pos);
    }
  }
}

// ============================================================================
// zstr Utilities
// ============================================================================

void tui_clr(zstr *s) { zstr_cat(s, ANSI_CLR); }

void tui_zstr_printf(zstr *s, const char *style, const char *text) {
  tui_style(s, style);
  zstr_cat(s, text);
  if (style && *style)
    tui_style(s, ANSI_RESET);
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
void tui_flush(FILE *f, zstr *s) { fwrite(zstr_cstr(s), 1, zstr_len(s), f); }

// ============================================================================
// Input Field Management
// ============================================================================

TuiInput tui_input_init(void) {
  return (TuiInput){.text = zstr_init(), .cursor = 0, .placeholder = NULL};
}

void tui_input_free(TuiInput *input) {
  zstr_free(&input->text);
  input->cursor = 0;
}

void tui_input_clear(TuiInput *input) {
  zstr_clear(&input->text);
  input->cursor = 0;
}

bool tui_input_handle_key(TuiInput *input, int key) {
  zstr *buffer = &input->text;
  int *cursor = &input->cursor;
  int len = (int)zstr_len(buffer);

  // Navigation
  if (key == 1) { // Ctrl-A (start)
    *cursor = 0;
    return true;
  }
  if (key == 5) { // Ctrl-E (end)
    *cursor = len;
    return true;
  }
  if (key == 2 || key == ARROW_LEFT) { // Ctrl-B or LEFT
    if (*cursor > 0)
      (*cursor)--;
    return true;
  }
  if (key == 6 || key == ARROW_RIGHT) { // Ctrl-F or RIGHT
    if (*cursor < len)
      (*cursor)++;
    return true;
  }

  // Deletion
  if (key == BACKSPACE || key == 8) { // Backspace or Ctrl-H
    if (*cursor > 0) {
      char *data = zstr_data(buffer);
      zstr new_buf = zstr_init();
      for (int i = 0; i < len; i++) {
        if (i != *cursor - 1)
          zstr_push(&new_buf, data[i]);
      }
      zstr_free(buffer);
      *buffer = new_buf;
      (*cursor)--;
    }
    return true;
  }
  if (key == DEL_KEY) {
    if (*cursor < len) {
      char *data = zstr_data(buffer);
      zstr new_buf = zstr_init();
      for (int i = 0; i < len; i++) {
        if (i != *cursor)
          zstr_push(&new_buf, data[i]);
      }
      zstr_free(buffer);
      *buffer = new_buf;
    }
    return true;
  }
  if (key == 11) { // Ctrl-K (kill to end)
    if (*cursor < len) {
      char *data = zstr_data(buffer);
      zstr new_buf = zstr_init();
      for (int i = 0; i < *cursor; i++) {
        zstr_push(&new_buf, data[i]);
      }
      zstr_free(buffer);
      *buffer = new_buf;
    }
    return true;
  }
  if (key == 21) { // Ctrl-U (kill to start)
    if (*cursor > 0) {
      char *data = zstr_data(buffer);
      zstr new_buf = zstr_init();
      for (int i = *cursor; i < len; i++) {
        zstr_push(&new_buf, data[i]);
      }
      zstr_free(buffer);
      *buffer = new_buf;
      *cursor = 0;
    }
    return true;
  }
  if (key == 23) { // Ctrl-W (kill word)
    if (*cursor > 0) {
      char *data = zstr_data(buffer);
      int end_pos = *cursor - 1;
      while (end_pos >= 0 && !isalnum((unsigned char)data[end_pos]))
        end_pos--;
      int start_pos = end_pos;
      while (start_pos >= 0 && isalnum((unsigned char)data[start_pos]))
        start_pos--;
      start_pos++;

      zstr new_buf = zstr_init();
      for (int i = 0; i < start_pos; i++)
        zstr_push(&new_buf, data[i]);
      for (int i = *cursor; i < len; i++)
        zstr_push(&new_buf, data[i]);
      zstr_free(buffer);
      *buffer = new_buf;
      *cursor = start_pos;
    }
    return true;
  }

  // Character insertion
  if (!iscntrl(key) && key >= 32 && key < 127) {
    char *data = zstr_data(buffer);
    zstr new_buf = zstr_init();
    for (int i = 0; i < len; i++) {
      if (i == *cursor)
        zstr_push(&new_buf, (char)key);
      zstr_push(&new_buf, data[i]);
    }
    if (*cursor >= len)
      zstr_push(&new_buf, (char)key);
    zstr_free(buffer);
    *buffer = new_buf;
    (*cursor)++;
    return true;
  }

  return false; // Key not handled
}

bool tui_handle_key(Tui *t, int key) {
  if (t->active_input) {
    return tui_input_handle_key(t->active_input, key);
  }
  return false;
}
