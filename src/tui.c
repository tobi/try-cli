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
static TuiInput filter_input = {0};
static int selected_index = 0;
static int scroll_offset = 0;
static int marked_count = 0;  // Number of items marked for deletion

// Memoized separator line
static zstr cached_sep_line = {0};
static int cached_sep_width = 0;

static const char *get_separator_line(int cols) {
  if (cols != cached_sep_width) {
    zstr_clear(&cached_sep_line);
    for (int i = 0; i < cols; i++)
      zstr_cat(&cached_sep_line, "─");
    cached_sep_width = cols;
  }
  return zstr_cstr(&cached_sep_line);
}

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
  const char *query = zstr_cstr(&filter_input.text);

  TryEntry *iter;
  vec_foreach(&all_tries, iter) {
    TryEntry *entry = iter;

    // Update score and rendered string
    fuzzy_match(entry, query);

    if (zstr_len(&filter_input.text) > 0 && entry->score <= 0.0) {
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
static int read_test_key(TestParams *test) {
  if (test->inject_keys[test->key_index] == '\0') {
    return -1; // End of keys
  }

  const char *keys = test->inject_keys;
  int *idx = &test->key_index;

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
static bool render_delete_confirmation(const char *base_path, TestParams *test) {
  (void)base_path;

  // Collect marked items
  vec_TryEntryPtr marked_items = {0};
  for (size_t i = 0; i < filtered_ptrs.length; i++) {
    if (filtered_ptrs.data[i]->marked_for_delete) {
      vec_push_TryEntryPtr(&marked_items, filtered_ptrs.data[i]);
    }
  }

  TuiInput input = tui_input_init();
  input.placeholder = "YES";
  bool confirmed = false;
  bool is_test = (test && test->inject_keys);

  int max_show = 10;
  if (max_show > (int)marked_items.length) max_show = (int)marked_items.length;

  while (1) {
    Tui t = tui_begin_screen(stderr);

    // Title
    TuiStyleString line = tui_screen_line(&t);
    tui_printf(&line, TUI_BOLD, "Delete %zu director%s?",
               marked_items.length, marked_items.length == 1 ? "y" : "ies");
    tui_screen_write(&t, &line);
    tui_screen_empty(&t);

    // List items
    for (int i = 0; i < max_show; i++) {
      line = tui_screen_line(&t);
      tui_print(&line, NULL, "  ");
      tui_print(&line, TUI_DARK, "-");
      tui_print(&line, NULL, " ");
      tui_print(&line, NULL, zstr_cstr(&marked_items.data[i]->name));
      tui_screen_write(&t, &line);
    }
    if ((int)marked_items.length > max_show) {
      line = tui_screen_line(&t);
      tui_printf(&line, TUI_DARK, "  ...and %zu more", marked_items.length - max_show);
      tui_screen_write(&t, &line);
    }

    // Prompt line with input
    tui_screen_empty(&t);
    line = tui_screen_line(&t);
    tui_print(&line, TUI_DARK, "Type ");
    tui_print(&line, TUI_HIGHLIGHT, "YES");
    tui_print(&line, TUI_DARK, " to confirm: ");
    tui_screen_input(&t, &input);
    tui_clr(line.str);  // Cut off overflow
    tui_screen_write(&t, &line);

    tui_free(&t);

    // Read key
    int c = is_test ? read_test_key(test) : read_key();

    if (c == -1 || c == ESC_KEY || c == 3) {
      break;
    } else if (c == ENTER_KEY) {
      if (strcmp(zstr_cstr(&input.text), "YES") == 0) {
        confirmed = true;
      }
      break;
    } else {
      tui_input_handle_key(&input, c);
    }
  }

  vec_free_TryEntryPtr(&marked_items);
  tui_input_free(&input);
  return confirmed;
}

static void render(const char *base_path) {
  (void)base_path;
  int rows, cols;
  get_window_size(&rows, &cols);
  const char *sep = get_separator_line(cols);

  Z_CLEANUP(tui_free) Tui t = tui_begin_screen(stderr);

  // Header
  TuiStyleString line = tui_screen_line(&t);
  tui_print(&line, TUI_H1, "🏠 Try Directory Selection");
  tui_screen_write_truncated(&t, &line, "… ");

  line = tui_screen_line(&t);
  tui_print(&line, TUI_DARK, sep);
  tui_screen_write_truncated(&t, &line, NULL);

  // Search bar with input
  line = tui_screen_line(&t);
  tui_print(&line, TUI_BOLD, "Search:");
  tui_print(&line, NULL, " ");
  tui_screen_input(&t, &filter_input);
  tui_clr(line.str);
  tui_screen_write_truncated(&t, &line, "… ");

  line = tui_screen_line(&t);
  tui_print(&line, TUI_DARK, sep);
  tui_screen_write_truncated(&t, &line, NULL);

  // List
  int list_height = rows - 8;
  if (list_height < 1) list_height = 1;

  if (selected_index < scroll_offset)
    scroll_offset = selected_index;
  if (selected_index >= scroll_offset + list_height)
    scroll_offset = selected_index - list_height + 1;

  for (int i = 0; i < list_height; i++) {
    int idx = scroll_offset + i;

    if (idx < (int)filtered_ptrs.length) {
      TryEntry *entry = filtered_ptrs.data[idx];
      bool is_selected = (idx == selected_index);
      bool is_marked = entry->marked_for_delete;

      // Get line (selected or normal)
      line = is_selected ? tui_screen_line_selected(&t) : tui_screen_line(&t);

      // Render entry prefix and name
      bool danger_pushed = false;
      if (is_selected) {
        tui_print(&line, TUI_HIGHLIGHT, "→ ");
        if (is_marked) {
          tui_print(&line, NULL, "🗑️ ");
          tui_push(&line, TUI_DANGER);
          danger_pushed = true;
        } else {
          tui_print(&line, NULL, "📁 ");
        }
      } else {
        if (is_marked) {
          tui_print(&line, NULL, "  🗑️ ");
          tui_push(&line, TUI_DANGER);
          danger_pushed = true;
        } else {
          tui_print(&line, NULL, "  📁 ");
        }
      }
      tui_print(&line, NULL, zstr_cstr(&entry->rendered));

      // Calculate metadata and positioning
      Z_CLEANUP(zstr_free) zstr rel_time = format_relative_time(entry->mtime);
      char score_buf[16];
      snprintf(score_buf, sizeof(score_buf), ", %.1f", entry->score);
      int meta_len = (int)zstr_len(&rel_time) + (int)strlen(score_buf);

      int prefix_len = 5;  // "→ " (2) + "📁 " (3): arrow=1, emoji=2, spaces=2
      int name_len = (int)zstr_len(&entry->name);
      int path_end = prefix_len + name_len;
      int meta_start = cols - 1 - meta_len;
      int available = meta_start - path_end;

      // Show metadata only if there's enough space (spec: hide if path truncated)
      if (available > 2) {
        for (int p = 0; p < available; p++) tui_putc(&line, ' ');
        tui_print(&line, TUI_DARK, zstr_cstr(&rel_time));
        tui_print(&line, TUI_DARK, score_buf);
      }

      if (danger_pushed) tui_pop(&line);
      tui_screen_write_truncated(&t, &line, "… ");

    } else if (idx == (int)filtered_ptrs.length && zstr_len(&filter_input.text) > 0) {
      // Separator before "Create new"
      tui_screen_empty(&t);
      i++;

      // Generate preview name
      time_t now = time(NULL);
      struct tm *tm = localtime(&now);
      char date_prefix[20];
      strftime(date_prefix, sizeof(date_prefix), "%Y-%m-%d", tm);

      Z_CLEANUP(zstr_free) zstr preview = zstr_from(date_prefix);
      zstr_cat(&preview, "-");
      const char *filter_text = zstr_cstr(&filter_input.text);
      for (size_t j = 0; j < zstr_len(&filter_input.text); j++) {
        zstr_push(&preview, isspace(filter_text[j]) ? '-' : filter_text[j]);
      }

      line = (idx == selected_index) ? tui_screen_line_selected(&t) : tui_screen_line(&t);
      if (idx == selected_index) {
        tui_print(&line, TUI_HIGHLIGHT, "→ ");
      } else {
        tui_print(&line, NULL, "  ");
      }
      tui_print(&line, NULL, "📂 Create new: ");
      tui_print(&line, TUI_DARK, zstr_cstr(&preview));
      tui_screen_write_truncated(&t, &line, "… ");
    } else {
      tui_screen_empty(&t);
    }
  }

  // Footer
  line = tui_screen_line(&t);
  tui_print(&line, TUI_DARK, sep);
  tui_screen_write_truncated(&t, &line, NULL);

  line = tui_screen_line(&t);
  if (marked_count > 0) {
    tui_print(&line, TUI_HIGHLIGHT, "DELETE MODE");
    tui_printf(&line, NULL, " | %d marked | ", marked_count);
    tui_print(&line, TUI_DARK, "Ctrl-D: Toggle  Enter: Confirm  Esc: Cancel");
  } else {
    tui_print(&line, TUI_DARK, "↑/↓: Navigate  Enter: Select  Ctrl-D: Delete  Esc: Cancel");
  }
  tui_screen_write_truncated(&t, &line, "… ");
  // tui_free(&t) called automatically via Z_CLEANUP
}

SelectionResult run_selector(const char *base_path,
                             const char *initial_filter,
                             TestParams *test) {
  // Initialize filter input
  if (zstr_len(&filter_input.text) == 0 && !filter_input.text.is_long) {
    filter_input = tui_input_init();
  } else {
    tui_input_clear(&filter_input);
  }

  if (initial_filter) {
    zstr_cat(&filter_input.text, initial_filter);
    filter_input.cursor = (int)zstr_len(&filter_input.text);
  }

  scan_tries(base_path);
  filter_tries();

  bool is_test = (test && (test->render_once || test->inject_keys));

  // Test mode: render once and exit
  if (is_test && test->render_once) {
    render(base_path);
    SelectionResult result = {.type = ACTION_CANCEL, .path = zstr_init()};
    return result;
  }

  // Only setup TTY if not in test mode or if we need to read keys
  if (!is_test || !test->inject_keys) {
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
    if (!is_test || !test->inject_keys) {
      render(base_path);
    }

    // Read key from injected keys or real input
    int c;
    if (is_test && test->inject_keys) {
      c = read_test_key(test);
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
        bool confirmed = render_delete_confirmation(base_path, test);
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
        Z_CLEANUP(zstr_free) zstr normalized = normalize_dir_name(zstr_cstr(&filter_input.text));
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
      if (zstr_len(&filter_input.text) > 0)
        max_idx++;
      if (selected_index < max_idx - 1)
        selected_index++;
    } else if (tui_input_handle_key(&filter_input, c)) {
      // Input was handled - re-filter
      filter_tries();
    }
  }

  if (!is_test || !test->inject_keys) {
    // Disable alternate screen buffer (restores original screen)
    disable_alternate_screen();
    // Consume any remaining input (e.g., leftover escape sequences)
    tui_drain_input();
    // Reset terminal state
    disable_raw_mode();
    // Reset all attributes
    tui_write_reset(stderr);
    fflush(stderr);
  }

  clear_state();
  vec_free_TryEntryPtr(&filtered_ptrs);
  tui_input_free(&filter_input);
  marked_count = 0;

  return result;
}
