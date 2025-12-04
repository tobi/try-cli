// Feature test macros for cross-platform compatibility
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#else
#define _GNU_SOURCE
#endif

#include "tui.h"
#include "fuzzy.h"
#include "terminal.h"
#include "utils.h"
#include "zvec.h"
#include <ctype.h>
#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

// Helper macro to ignore write return values
#define WRITE(fd, buf, len) do { ssize_t unused = write(fd, buf, len); (void)unused; } while(0)

// Generate vector implementation for TryEntry
Z_VEC_GENERATE_IMPL(TryEntry, TryEntry)

// Generate vector implementation for TryEntry pointers
Z_VEC_GENERATE_IMPL(TryEntry *, TryEntryPtr)

static vec_TryEntry all_tries = {0};
static vec_TryEntryPtr filtered_ptrs = {0};
static zstr filter_buffer = {0};
static int filter_cursor = 0;  // Cursor position in filter_buffer
static int selected_index = 0;
static int scroll_offset = 0;
static int marked_count = 0;  // Number of items marked for deletion

/*
 * SIGWINCH handler - called when terminal is resized.
 * We don't need to do anything here; the signal delivery itself
 * interrupts the blocking read() in read_key(), which returns KEY_RESIZE.
 * The main loop then continues, calling render() which gets the new size.
 *
 * Cross-platform note: SIGWINCH is POSIX and works on Linux, macOS, and BSDs.
 * Windows would need WINDOW_BUFFER_SIZE_EVENT from ReadConsoleInput().
 */
static void handle_winch(int sig) {
  (void)sig;
}

static void free_entry(TryEntry *entry) {
  zstr_free(&entry->path);
  zstr_free(&entry->name);
  zstr_free(&entry->rendered);
}

static void clear_state(void) {
  // Free contents of all_tries
  for (size_t i = 0; i < all_tries.length; i++) {
    free_entry(&all_tries.data[i]);
  }
  vec_free_TryEntry(&all_tries);

  // filtered_ptrs just contains pointers, no need to free entries
  vec_free_TryEntryPtr(&filtered_ptrs);

  filter_cursor = 0;
}

static int compare_tries_by_score(const void *a, const void *b) {
  const TryEntry *const *ta = (const TryEntry *const *)a;
  const TryEntry *const *tb = (const TryEntry *const *)b;
  if ((*ta)->score > (*tb)->score)
    return -1;
  if ((*ta)->score < (*tb)->score)
    return 1;
  return 0;
}

static void scan_tries(const char *base_path) {
  // Clear existing
  for (size_t i = 0; i < all_tries.length; i++) {
    free_entry(&all_tries.data[i]);
  }
  vec_clear_TryEntry(&all_tries);

  DIR *d = opendir(base_path);
  if (!d)
    return;

  struct dirent *dir;
  while ((dir = readdir(d)) != NULL) {
    if (dir->d_name[0] == '.')
      continue;

    zstr full_path = join_path(base_path, dir->d_name);

    struct stat sb;
    if (stat(zstr_cstr(&full_path), &sb) == 0 && S_ISDIR(sb.st_mode)) {
      TryEntry entry = {0};
      entry.path = full_path; // Move ownership
      entry.name = zstr_from(dir->d_name);
      entry.mtime = sb.st_mtime;
      // Initial render = name (no highlighting)
      entry.rendered = zstr_dup(&entry.name);
      entry.score = 0; // Will be calculated in filter

      vec_push_TryEntry(&all_tries, entry);
    } else {
      zstr_free(&full_path);
    }
  }
  closedir(d);
}

static void filter_tries(void) {
  vec_clear_TryEntryPtr(&filtered_ptrs);
  const char *query = zstr_cstr(&filter_buffer);

  TryEntry *iter;
  vec_foreach(&all_tries, iter) {
    TryEntry *entry = iter;

    // Update score and rendered string
    fuzzy_match(entry, query);

    if (zstr_len(&filter_buffer) > 0 && entry->score <= 0.0) {
      continue;
    }

    vec_push_TryEntryPtr(&filtered_ptrs, entry);
  }

  qsort(filtered_ptrs.data, filtered_ptrs.length, sizeof(TryEntry *),
        compare_tries_by_score);

  if (selected_index >= (int)filtered_ptrs.length) {
    selected_index = 0;
  }
}

// Handle readline-style keybindings for text editing
// Returns true if the key was handled, false if it should be treated as regular input
static bool handle_readline_keybinding(zstr *buffer, int *cursor, int key) {
  if (key == 1) {  // Ctrl-A (move to start)
    *cursor = 0;
    return true;
  } else if (key == 5) {  // Ctrl-E (move to end)
    *cursor = (int)zstr_len(buffer);
    return true;
  } else if (key == 2 || key == ARROW_LEFT) {  // Ctrl-B or LEFT (move left)
    if (*cursor > 0)
      (*cursor)--;
    return true;
  } else if (key == 6 || key == ARROW_RIGHT) {  // Ctrl-F or RIGHT (move right)
    if (*cursor < (int)zstr_len(buffer))
      (*cursor)++;
    return true;
  } else if (key == 11) {  // Ctrl-K (kill after cursor)
    int buffer_len = (int)zstr_len(buffer);
    if (*cursor < buffer_len) {
      char *data = zstr_data(buffer);
      Z_CLEANUP(zstr_free) zstr new_buffer = zstr_init();
      for (int i = 0; i < *cursor; i++) {
        zstr_push(&new_buffer, data[i]);
      }
      zstr_free(buffer);
      *buffer = new_buffer;
    }
    return true;
  } else if (key == 23) {  // Ctrl-W (kill word)
    int buffer_len = (int)zstr_len(buffer);
    if (*cursor > 0) {
      char *data = zstr_data(buffer);

      // Move back past any trailing non-word characters
      int end_pos = *cursor - 1;
      while (end_pos >= 0 && !isalnum((unsigned char)data[end_pos])) {
        end_pos--;
      }

      // Move back past the word itself (alphanumeric characters)
      int start_pos = end_pos;
      while (start_pos >= 0 && isalnum((unsigned char)data[start_pos])) {
        start_pos--;
      }
      start_pos++;  // Move to the first char of the word

      // Build new buffer without the deleted word
      Z_CLEANUP(zstr_free) zstr new_buffer = zstr_init();
      for (int i = 0; i < start_pos; i++) {
        zstr_push(&new_buffer, data[i]);
      }
      for (int i = *cursor; i < buffer_len; i++) {
        zstr_push(&new_buffer, data[i]);
      }

      zstr_free(buffer);
      *buffer = new_buffer;
      *cursor = start_pos;
    }
    return true;
  } else if (key == BACKSPACE || key == 127 || key == 8) {  // Backspace, DEL, or Ctrl-H
    if (*cursor > 0) {
      // Delete character before cursor
      int buffer_len = (int)zstr_len(buffer);
      Z_CLEANUP(zstr_free) zstr new_buffer = zstr_init();
      char *data = zstr_data(buffer);

      for (int i = 0; i < buffer_len; i++) {
        if (i != *cursor - 1) {  // Skip the character before cursor
          zstr_push(&new_buffer, data[i]);
        }
      }

      zstr_free(buffer);
      *buffer = new_buffer;
      (*cursor)--;
    }
    return true;
  }
  return false;
}

// Parse symbolic key name to key code
// Supports: ENTER, RETURN, ESC, ESCAPE, UP, DOWN, LEFT, RIGHT, BACKSPACE, TAB, SPACE
// Also: CTRL-X (where X is A-Z)
static int parse_symbolic_key(const char *token, int len) {
  // Uppercase comparison helper
  #define MATCH(s) (len == (int)strlen(s) && strncasecmp(token, s, len) == 0)

  if (MATCH("ENTER") || MATCH("RETURN")) return ENTER_KEY;
  if (MATCH("ESC") || MATCH("ESCAPE")) return ESC_KEY;
  if (MATCH("UP")) return ARROW_UP;
  if (MATCH("DOWN")) return ARROW_DOWN;
  if (MATCH("LEFT")) return ARROW_LEFT;
  if (MATCH("RIGHT")) return ARROW_RIGHT;
  if (MATCH("BACKSPACE") || MATCH("BS")) return 127;
  if (MATCH("TAB")) return '\t';
  if (MATCH("SPACE")) return ' ';

  // CTRL-X format (e.g., CTRL-J, CTRL-C)
  if (len >= 6 && strncasecmp(token, "CTRL-", 5) == 0) {
    char key = token[5];
    if (key >= 'a' && key <= 'z') key -= 32; // uppercase
    if (key >= 'A' && key <= 'Z') {
      return key - 'A' + 1; // Ctrl-A = 1, Ctrl-Z = 26
    }
  }

  // Single character
  if (len == 1) {
    return (unsigned char)token[0];
  }

  #undef MATCH
  return -1; // Unknown
}

// Parse key from test mode injected keys
// Supports both raw escape sequences AND symbolic format (comma-separated)
// Symbolic: "CTRL-J,DOWN,ENTER" or "beta,ENTER"
// Raw: "\x0a\x1b[B\r" (legacy)
static int read_test_key(Mode *mode) {
  if (mode->inject_keys[mode->key_index] == '\0') {
    return -1; // End of keys
  }

  const char *keys = mode->inject_keys;
  int *idx = &mode->key_index;

  // Skip leading comma
  while (keys[*idx] == ',') (*idx)++;
  if (keys[*idx] == '\0') return -1;

  unsigned char c = keys[*idx];

  // Check if this looks like a symbolic token
  // Only trigger for known keywords (followed by comma or end of string)
  #define IS_SYM(kw, klen) (strncasecmp(&keys[*idx], kw, klen) == 0 && \
                           (keys[*idx + klen] == ',' || keys[*idx + klen] == '\0'))
  // CTRL-X is 6 chars (CTRL- plus one letter)
  #define IS_CTRL() (strncasecmp(&keys[*idx], "CTRL-", 5) == 0 && \
                     keys[*idx + 5] != '\0' && \
                     (keys[*idx + 6] == ',' || keys[*idx + 6] == '\0'))

  if (IS_CTRL() ||
      IS_SYM("ENTER", 5) ||
      IS_SYM("RETURN", 6) ||
      IS_SYM("ESCAPE", 6) ||
      IS_SYM("ESC", 3) ||
      IS_SYM("UP", 2) ||
      IS_SYM("DOWN", 4) ||
      IS_SYM("LEFT", 4) ||
      IS_SYM("RIGHT", 5) ||
      IS_SYM("BACKSPACE", 9) ||
      IS_SYM("BS", 2) ||
      IS_SYM("TAB", 3) ||
      IS_SYM("SPACE", 5)) {
  #undef IS_SYM
  #undef IS_CTRL
    // Find end of token (comma or end of string)
    int start = *idx;
    while (keys[*idx] != ',' && keys[*idx] != '\0') (*idx)++;
    int len = *idx - start;
    return parse_symbolic_key(&keys[start], len);
  }

  // Raw escape sequence handling (legacy format)
  (*idx)++;

  if (c == '\x1b') {
    // Check for escape sequence
    if (keys[*idx] == '[') {
      (*idx)++;
      unsigned char seq = keys[(*idx)++];
      switch (seq) {
      case 'A': return ARROW_UP;
      case 'B': return ARROW_DOWN;
      case 'C': return ARROW_RIGHT;
      case 'D': return ARROW_LEFT;
      default: return ESC_KEY;
      }
    }
    return ESC_KEY;
  } else if (c == '\r') {
    return ENTER_KEY;
  } else {
    return c;
  }
}

// Render confirmation dialog for deletion
// Returns true if user typed "YES", false otherwise
static bool render_delete_confirmation(const char *base_path, Mode *mode) {
  int rows, cols;
  get_window_size(&rows, &cols);

  // Collect marked items
  vec_TryEntryPtr marked_items = {0};
  for (size_t i = 0; i < filtered_ptrs.length; i++) {
    if (filtered_ptrs.data[i]->marked_for_delete) {
      vec_push_TryEntryPtr(&marked_items, filtered_ptrs.data[i]);
    }
  }

  zstr confirm_input = zstr_init();
  int confirm_cursor = 0;
  bool confirmed = false;
  bool is_test = (mode && mode->inject_keys);

  while (1) {
    // Hide cursor and go home
    tui_write_hide_cursor(stderr);
    tui_write_home(stderr);

    // Title
    {
      Z_CLEANUP(zstr_free) zstr title = zstr_init();
      if (!tui_no_colors) zstr_cat(&title, TUI_BOLD);
      zstr_cat(&title, "Delete ");
      char count_str[32];
      snprintf(count_str, sizeof(count_str), "%zu", marked_items.length);
      zstr_cat(&title, count_str);
      zstr_cat(&title, " director");
      zstr_cat(&title, marked_items.length == 1 ? "y" : "ies");
      zstr_cat(&title, "?");
      if (!tui_no_colors) zstr_cat(&title, ANSI_RESET);
      tui_clr(&title);
      zstr_cat(&title, "\n");
      tui_clr(&title);
      zstr_cat(&title, "\n");
      tui_flush(stderr, &title);
    }

    // List items (max 10)
    int max_show = 10;
    if (max_show > (int)marked_items.length) max_show = (int)marked_items.length;
    for (int i = 0; i < max_show; i++) {
      Z_CLEANUP(zstr_free) zstr item = zstr_init();
      zstr_cat(&item, "  ");
      if (!tui_no_colors) zstr_cat(&item, TUI_DARK);
      zstr_cat(&item, "-");
      if (!tui_no_colors) zstr_cat(&item, ANSI_RESET);
      zstr_cat(&item, " ");
      zstr_cat(&item, zstr_cstr(&marked_items.data[i]->name));
      tui_clr(&item);
      zstr_cat(&item, "\n");
      tui_flush(stderr, &item);
    }
    if ((int)marked_items.length > max_show) {
      Z_CLEANUP(zstr_free) zstr more = zstr_init();
      zstr_cat(&more, "  ");
      if (!tui_no_colors) zstr_cat(&more, TUI_DARK);
      char more_text[80];
      snprintf(more_text, sizeof(more_text), "...and %zu more", marked_items.length - max_show);
      zstr_cat(&more, more_text);
      if (!tui_no_colors) zstr_cat(&more, ANSI_RESET);
      tui_clr(&more);
      zstr_cat(&more, "\n");
      tui_flush(stderr, &more);
    }

    // Prompt line
    {
      Z_CLEANUP(zstr_free) zstr prompt = zstr_init();
      tui_clr(&prompt);
      zstr_cat(&prompt, "\n");
      if (!tui_no_colors) zstr_cat(&prompt, TUI_DARK);
      zstr_cat(&prompt, "Type ");
      if (!tui_no_colors) zstr_cat(&prompt, ANSI_RESET);
      if (!tui_no_colors) zstr_cat(&prompt, TUI_HIGHLIGHT);
      zstr_cat(&prompt, "YES");
      if (!tui_no_colors) zstr_cat(&prompt, ANSI_RESET);
      if (!tui_no_colors) zstr_cat(&prompt, TUI_DARK);
      zstr_cat(&prompt, " to confirm:");
      if (!tui_no_colors) zstr_cat(&prompt, ANSI_RESET);
      zstr_cat(&prompt, " ");

      // Calculate cursor column: "Type YES to confirm: " + input before cursor
      // "Type YES to confirm: " is 21 visible chars
      int prompt_len = 21;

      const char *confirm_cstr = zstr_cstr(&confirm_input);
      int confirm_len = (int)zstr_len(&confirm_input);

      // Clamp cursor to valid range
      int cursor = confirm_cursor;
      if (cursor < 0) cursor = 0;
      if (cursor > confirm_len) cursor = confirm_len;

      // Add text before cursor
      zstr_cat_len(&prompt, confirm_cstr, cursor);

      // Remember cursor position (1-indexed)
      int cursor_col = prompt_len + cursor + 1;

      // Add text after cursor
      zstr_cat_len(&prompt, confirm_cstr + cursor, confirm_len - cursor);

      zstr_cat(&prompt, "\n");
      zstr_cat(&prompt, ANSI_CLS);
      tui_flush(stderr, &prompt);

      // Position cursor and show it
      tui_write_goto(stderr, 4 + max_show + ((int)marked_items.length > max_show ? 1 : 0), cursor_col);
      tui_write_show_cursor(stderr);
    }

    // Read key
    int c;
    if (is_test) {
      c = read_test_key(mode);
    } else {
      c = read_key();
    }

    if (c == -1 || c == ESC_KEY || c == 3) {
      break;
    } else if (c == ENTER_KEY) {
      // Check if input is exactly "YES"
      if (strcmp(zstr_cstr(&confirm_input), "YES") == 0) {
        confirmed = true;
      }
      break;
    } else if (handle_readline_keybinding(&confirm_input, &confirm_cursor, c)) {
      // Keybinding was handled
    } else if (!iscntrl(c) && c < 128) {
      // Insert character at cursor position
      int buffer_len = (int)zstr_len(&confirm_input);
      Z_CLEANUP(zstr_free) zstr new_buffer = zstr_init();
      char *data = zstr_data(&confirm_input);

      for (int i = 0; i < buffer_len; i++) {
        if (i == confirm_cursor) {
          zstr_push(&new_buffer, (char)c);
        }
        zstr_push(&new_buffer, data[i]);
      }

      // If cursor is at the end, just append
      if (confirm_cursor >= buffer_len) {
        zstr_push(&new_buffer, (char)c);
      }

      zstr_free(&confirm_input);
      confirm_input = new_buffer;
      confirm_cursor++;
    }
  }

  vec_free_TryEntryPtr(&marked_items);
  zstr_free(&confirm_input);

  (void)base_path;
  return confirmed;
}

static void render(const char *base_path) {
  int rows, cols;
  get_window_size(&rows, &cols);

  // Hide cursor and go home
  tui_write_hide_cursor(stderr);
  tui_write_home(stderr);

  // Build separator line dynamically (handles any terminal width)
  Z_CLEANUP(zstr_free) zstr sep_line = zstr_init();
  for (int i = 0; i < cols; i++)
    zstr_cat(&sep_line, "─");

  // Header
  {
    Z_CLEANUP(zstr_free) zstr header = zstr_init();
    if (!tui_no_colors) zstr_cat(&header, TUI_H1);
    zstr_cat(&header, "🏠 Try Directory Selection");
    if (!tui_no_colors) zstr_cat(&header, ANSI_RESET);
    tui_clr(&header);
    zstr_cat(&header, "\n");
    if (!tui_no_colors) zstr_cat(&header, TUI_DARK);
    zstr_cat(&header, zstr_cstr(&sep_line));
    if (!tui_no_colors) zstr_cat(&header, ANSI_RESET);
    tui_clr(&header);
    zstr_cat(&header, "\n");
    tui_flush(stderr, &header);
  }

  // Search bar - track cursor position
  int search_cursor_col = -1;
  int search_line = 3;
  {
    Z_CLEANUP(zstr_free) zstr search = zstr_init();
    if (!tui_no_colors) zstr_cat(&search, TUI_BOLD);
    zstr_cat(&search, "Search:");
    if (!tui_no_colors) zstr_cat(&search, ANSI_RESET);
    zstr_cat(&search, " ");

    // Calculate visual position: "Search: " is 8 chars
    int prefix_visual_len = 8;

    const char *filter_cstr = zstr_cstr(&filter_buffer);
    int buffer_len = (int)zstr_len(&filter_buffer);

    // Clamp cursor to valid range
    int cursor = filter_cursor;
    if (cursor < 0) cursor = 0;
    if (cursor > buffer_len) cursor = buffer_len;

    // Add text before cursor
    for (int i = 0; i < cursor; i++) {
      zstr_push(&search, filter_cstr[i]);
    }

    // Record cursor column (1-indexed)
    search_cursor_col = prefix_visual_len + cursor + 1;

    // Add text after cursor
    for (int i = cursor; i < buffer_len; i++) {
      zstr_push(&search, filter_cstr[i]);
    }

    tui_clr(&search);
    zstr_cat(&search, "\n");
    if (!tui_no_colors) zstr_cat(&search, TUI_DARK);
    zstr_cat(&search, zstr_cstr(&sep_line));
    if (!tui_no_colors) zstr_cat(&search, ANSI_RESET);
    tui_clr(&search);
    zstr_cat(&search, "\n");
    tui_flush(stderr, &search);
  }

  // List
  int list_height = rows - 8;
  if (list_height < 1)
    list_height = 1;

  if (selected_index < scroll_offset)
    scroll_offset = selected_index;
  if (selected_index >= scroll_offset + list_height)
    scroll_offset = selected_index - list_height + 1;

  for (int i = 0; i < list_height; i++) {
    int idx = scroll_offset + i;

    if (idx < (int)filtered_ptrs.length) {
      TryEntry *entry = filtered_ptrs.data[idx];
      int is_selected = (idx == selected_index);

      Z_CLEANUP(zstr_free) zstr line = zstr_init();

      // Calculate available space
      // Prefix is 2 chars ("→ " or "  "), icon is 2 chars (emoji), space after icon is implicit in count
      int prefix_len = 5; // 2 (arrow/spaces) + 2 (emoji) + 1 (space)

      // Build metadata strings
      Z_CLEANUP(zstr_free) zstr rel_time = format_relative_time(entry->mtime);
      char score_text[16];
      snprintf(score_text, sizeof(score_text), ", %.1f", entry->score);

      // Calculate max length for directory name - allow it to use almost full width
      // Don't reserve space for metadata here; we'll check if it fits after
      int max_name_len = cols - prefix_len - 1; // Just leave 1 char for safety

      int plain_len = zstr_len(&entry->name);
      bool name_truncated = false;

      Z_CLEANUP(zstr_free) zstr display_name = zstr_init();
      if (plain_len > max_name_len && max_name_len > 4) {
        // Truncate and add ellipsis
        // Copy the rendered name but truncate it
        const char *rendered = zstr_cstr(&entry->rendered);
        // This is approximate since rendered has ANSI codes, but good enough
        int chars_to_copy = max_name_len - 1; // Leave room for ellipsis

        // Copy character by character, skipping ANSI sequences
        int visible_count = 0;
        const char *p = rendered;
        while (*p && visible_count < chars_to_copy) {
          if (*p == '\033') {
            // Copy ANSI sequence
            zstr_push(&display_name, *p++);
            while (*p && *p != 'm') {
              zstr_push(&display_name, *p++);
            }
            if (*p == 'm') {
              zstr_push(&display_name, *p++);
            }
          } else {
            zstr_push(&display_name, *p++);
            visible_count++;
          }
        }
        zstr_cat(&display_name, "…");
        name_truncated = true;
      } else {
        zstr_cat(&display_name, zstr_cstr(&entry->rendered));
      }

      // Render the directory entry using style stack for proper nesting
      TuiStyleString ss = tui_wrap_zstr(&line);
      bool is_marked = entry->marked_for_delete;
      bool danger_pushed = false;

      if (is_selected) {
        // Selection row: push section style (bold + dark gray bg)
        tui_push(&ss, TUI_SELECTED);

        if (is_marked) {
          // Arrow with highlight
          tui_push(&ss, TUI_HIGHLIGHT);
          tui_print(&ss, NULL, "→ ");
          tui_pop(&ss);  // back to section

          tui_print(&ss, NULL, "🗑️ ");

          // Push danger bg - extends to end of line (popped after metadata)
          tui_push(&ss, TUI_DANGER);
          danger_pushed = true;
          tui_print(&ss, NULL, zstr_cstr(&display_name));
        } else {
          // Arrow with highlight
          tui_push(&ss, TUI_HIGHLIGHT);
          tui_print(&ss, NULL, "→ ");
          tui_pop(&ss);  // back to section

          tui_print(&ss, NULL, "📁 ");
          // display_name has fg-only codes from fuzzy.c, section bg preserved
          tui_print(&ss, NULL, zstr_cstr(&display_name));
        }
      } else {
        // Non-selected row
        if (is_marked) {
          tui_print(&ss, NULL, "  🗑️ ");
          // Push danger bg - extends to end of line (popped after metadata)
          tui_push(&ss, TUI_DANGER);
          danger_pushed = true;
          tui_print(&ss, NULL, zstr_cstr(&display_name));
        } else {
          tui_print(&ss, NULL, "  📁 ");
          tui_print(&ss, NULL, zstr_cstr(&display_name));
        }
      }

      // Build full metadata string
      Z_CLEANUP(zstr_free) zstr full_meta = zstr_init();
      zstr_cat(&full_meta, zstr_cstr(&rel_time));
      zstr_cat(&full_meta, score_text);
      int full_meta_len = (int)zstr_len(&full_meta);

      // Calculate positions - metadata is always right-aligned at screen edge
      int actual_name_len = name_truncated ? max_name_len : plain_len;
      int path_end_pos = prefix_len + actual_name_len;
      int meta_end_pos = cols - 1; // -1 because cols is 1-indexed width
      int meta_start_pos = meta_end_pos - full_meta_len;
      int available_space = meta_start_pos - path_end_pos;

      // Show metadata if there's more than 2 chars gap, truncating from left if needed
      if (available_space > 2) {
        // Full metadata fits with gap - add padding and full metadata
        int padding_len = available_space;
        for (int p = 0; p < padding_len; p++) {
          tui_putc(&ss, ' ');
        }
        tui_push(&ss, TUI_DARK);
        tui_print(&ss, NULL, zstr_cstr(&full_meta));
        tui_pop(&ss);
      } else if (available_space > -full_meta_len + 3) {
        // Partial overlap - show truncated metadata (cut from left)
        // available_space can be negative if name extends into metadata area
        int chars_to_skip = (available_space < 1) ? (1 - available_space) : 0;
        int chars_to_show = full_meta_len - chars_to_skip;
        if (chars_to_show > 2) {
          tui_print(&ss, NULL, " ");
          tui_push(&ss, TUI_DARK);
          const char *meta_str = zstr_cstr(&full_meta);
          tui_print(&ss, NULL, meta_str + chars_to_skip);
          tui_pop(&ss);
        }
      }

      // Pop danger if we pushed it (extends to end of line)
      if (danger_pushed) {
        tui_pop(&ss);
      }

      // Pop section if we pushed it (selected rows)
      if (is_selected) {
        tui_pop(&ss);  // back to default
      }

      tui_clr(&line);
      zstr_cat(&line, "\n");
      tui_flush(stderr, &line);

    } else if (idx == (int)filtered_ptrs.length && zstr_len(&filter_buffer) > 0) {
      // Add separator line before "Create new"
      tui_write_clr(stderr);
      fputs("\n", stderr);
      i++; // Skip next iteration since we used a line for separator

      // Generate preview of what the directory name will be
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      char date_prefix[20];
      strftime(date_prefix, sizeof(date_prefix), "%Y-%m-%d", t);

      Z_CLEANUP(zstr_free) zstr preview = zstr_from(date_prefix);
      zstr_cat(&preview, "-");

      // Add filter text with spaces replaced by dashes
      const char *filter_text = zstr_cstr(&filter_buffer);
      for (size_t j = 0; j < zstr_len(&filter_buffer); j++) {
        if (isspace(filter_text[j])) {
          zstr_push(&preview, '-');
        } else {
          zstr_push(&preview, filter_text[j]);
        }
      }

      Z_CLEANUP(zstr_free) zstr line = zstr_init();
      TuiStyleString ss = tui_wrap_zstr(&line);

      if (idx == selected_index) {
        tui_push(&ss, TUI_SELECTED);
        tui_push(&ss, TUI_HIGHLIGHT);
        tui_print(&ss, NULL, "→ ");
        tui_pop(&ss);  // back to section
        tui_print(&ss, NULL, "📂 Create new: ");
        tui_push(&ss, TUI_DARK);
        tui_print(&ss, NULL, zstr_cstr(&preview));
        tui_pop(&ss);  // back to section
        tui_pop(&ss);  // back to default
        tui_clr(&line);
        zstr_cat(&line, "\n");
      } else {
        tui_print(&ss, NULL, "  📂 Create new: ");
        tui_push(&ss, TUI_DARK);
        tui_print(&ss, NULL, zstr_cstr(&preview));
        tui_pop(&ss);
        tui_clr(&line);
        zstr_cat(&line, "\n");
      }
      tui_flush(stderr, &line);
    } else {
      tui_write_clr(stderr);
      fputs("\n", stderr);
    }
  }

  tui_write_cls(stderr);

  // Footer
  {
    Z_CLEANUP(zstr_free) zstr footer = zstr_init();
    if (!tui_no_colors) zstr_cat(&footer, TUI_DARK);
    zstr_cat(&footer, zstr_cstr(&sep_line));
    if (!tui_no_colors) zstr_cat(&footer, ANSI_RESET);
    tui_clr(&footer);
    zstr_cat(&footer, "\n");

    if (marked_count > 0) {
      // Delete mode footer
      if (!tui_no_colors) zstr_cat(&footer, TUI_HIGHLIGHT);
      zstr_cat(&footer, "DELETE MODE");
      if (!tui_no_colors) zstr_cat(&footer, ANSI_RESET);
      zstr_cat(&footer, " | ");
      char count_str[32];
      snprintf(count_str, sizeof(count_str), "%d", marked_count);
      zstr_cat(&footer, count_str);
      zstr_cat(&footer, " marked | ");
      if (!tui_no_colors) zstr_cat(&footer, TUI_DARK);
      zstr_cat(&footer, "Ctrl-D: Toggle  Enter: Confirm  Esc: Cancel");
      if (!tui_no_colors) zstr_cat(&footer, ANSI_RESET);
      tui_clr(&footer);
      zstr_cat(&footer, "\n");
    } else {
      // Normal footer
      if (!tui_no_colors) zstr_cat(&footer, TUI_DARK);
      zstr_cat(&footer, "↑/↓: Navigate  Enter: Select  Ctrl-D: Delete  Esc: Cancel");
      if (!tui_no_colors) zstr_cat(&footer, ANSI_RESET);
      tui_clr(&footer);
      zstr_cat(&footer, "\n");
    }

    tui_flush(stderr, &footer);

    // Position cursor in search field and show it
    if (search_cursor_col >= 0) {
      tui_write_goto(stderr, search_line, search_cursor_col);
    }
    tui_write_show_cursor(stderr);
  }

  (void)base_path;
}

SelectionResult run_selector(const char *base_path,
                             const char *initial_filter,
                             Mode *mode) {
  // Initialize
  if (zstr_len(&filter_buffer) == 0 && !filter_buffer.is_long) {
    // First time - already zero-initialized
    filter_buffer = zstr_init();
  } else {
    zstr_clear(&filter_buffer);
  }

  filter_cursor = 0;

  if (initial_filter) {
    zstr_cat(&filter_buffer, initial_filter);
    filter_cursor = (int)zstr_len(&filter_buffer);  // Move cursor to end after initial filter
  }

  scan_tries(base_path);
  filter_tries();

  bool is_test = (mode && (mode->render_once || mode->inject_keys));

  // Test mode: render once and exit
  if (is_test && mode->render_once) {
    render(base_path);
    SelectionResult result = {.type = ACTION_CANCEL, .path = zstr_init()};
    return result;
  }

  // Only setup TTY if not in test mode or if we need to read keys
  if (!is_test || !mode->inject_keys) {
    enable_raw_mode();

    struct sigaction sa;

    // Handle SIGWINCH (terminal resize)
    sa.sa_handler = handle_winch;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGWINCH, &sa, NULL);

    enable_alternate_screen();
  }

  SelectionResult result = {.type = ACTION_CANCEL, .path = zstr_init()};

  while (1) {
    if (!is_test || !mode->inject_keys) {
      render(base_path);
    }

    // Read key from injected keys or real input
    int c;
    if (is_test && mode->inject_keys) {
      c = read_test_key(mode);
    } else {
      c = read_key();
    }

    if (c == KEY_RESIZE) {
      // Terminal was resized - continue to re-render with new dimensions
      // get_window_size() is called in render() to get updated size
      continue;
    }
    if (c == -1)
      break;

    if (c == ESC_KEY || c == 3) {
      // If in delete mode, just clear marks and continue
      if (marked_count > 0) {
        for (size_t i = 0; i < all_tries.length; i++) {
          all_tries.data[i].marked_for_delete = false;
        }
        marked_count = 0;
        continue;
      }
      break;
    } else if (c == 4) {
      // Ctrl-D: Toggle mark on current item
      if (selected_index < (int)filtered_ptrs.length) {
        TryEntry *entry = filtered_ptrs.data[selected_index];
        entry->marked_for_delete = !entry->marked_for_delete;
        if (entry->marked_for_delete) {
          marked_count++;
        } else {
          marked_count--;
        }
      }
    } else if (c == ENTER_KEY) {
      // If items are marked, show confirmation dialog
      if (marked_count > 0) {
        bool confirmed = render_delete_confirmation(base_path, mode);
        if (confirmed) {
          // Collect all marked paths
          result.type = ACTION_DELETE;
          // vec_zstr is initialized to 0 via result initialization
          for (size_t i = 0; i < filtered_ptrs.length; i++) {
            if (filtered_ptrs.data[i]->marked_for_delete) {
              vec_push_zstr(&result.delete_names, zstr_dup(&filtered_ptrs.data[i]->name));
            }
          }
          break;
        }
        // Not confirmed - continue (marks cleared by ESC in dialog, or just continue if typed wrong)
        continue;
      }

      if (selected_index < (int)filtered_ptrs.length) {
        result.type = ACTION_CD;
        result.path = zstr_dup(&filtered_ptrs.data[selected_index]->path);
      } else {
        // Create new - validate and normalize name first
        Z_CLEANUP(zstr_free) zstr normalized = normalize_dir_name(zstr_cstr(&filter_buffer));
        if (zstr_len(&normalized) == 0) {
          // Invalid name - don't create directory
          break;
        }

        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char date_prefix[20];
        strftime(date_prefix, sizeof(date_prefix), "%Y-%m-%d", t);

        zstr new_name = zstr_from(date_prefix);
        zstr_cat(&new_name, "-");
        zstr_cat(&new_name, zstr_cstr(&normalized));

        result.type = ACTION_MKDIR;
        result.path = join_path(base_path, zstr_cstr(&new_name));
        zstr_free(&new_name);
      }
      break;
    } else if (c == ARROW_UP || c == 16) {  // UP or Ctrl-P
      if (selected_index > 0)
        selected_index--;
    } else if (c == ARROW_DOWN || c == 14) {  // DOWN or Ctrl-N
      int max_idx = filtered_ptrs.length;
      if (zstr_len(&filter_buffer) > 0)
        max_idx++;
      if (selected_index < max_idx - 1)
        selected_index++;
    } else if (handle_readline_keybinding(&filter_buffer, &filter_cursor, c)) {
      // Readline keybinding was handled - re-filter if buffer changed
      filter_tries();
    } else if (!iscntrl(c) && c < 128) {
      // Insert character at cursor position
      int buffer_len = (int)zstr_len(&filter_buffer);
      Z_CLEANUP(zstr_free) zstr new_buffer = zstr_init();
      char *data = zstr_data(&filter_buffer);

      for (int i = 0; i < buffer_len; i++) {
        if (i == filter_cursor) {
          zstr_push(&new_buffer, (char)c);
        }
        zstr_push(&new_buffer, data[i]);
      }

      // If cursor is at the end, just append
      if (filter_cursor >= buffer_len) {
        zstr_push(&new_buffer, (char)c);
      }

      zstr_free(&filter_buffer);
      filter_buffer = new_buffer;
      filter_cursor++;
      filter_tries();
    }
  }

  if (!is_test || !mode->inject_keys) {
    // Disable alternate screen buffer (restores original screen)
    disable_alternate_screen();
    // Reset terminal state
    disable_raw_mode();
    // Reset all attributes
    tui_write_reset(stderr);
    fflush(stderr);
  }

  clear_state();
  vec_free_TryEntryPtr(&filtered_ptrs);
  zstr_free(&filter_buffer);
  marked_count = 0;

  return result;
}
