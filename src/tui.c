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
  bool confirmed = false;
  bool is_test = (mode && mode->inject_keys);

  while (1) {
    WRITE(STDERR_FILENO, "\x1b[?25l", 6); // Hide cursor
    WRITE(STDERR_FILENO, "\x1b[H", 3);    // Home

    // Title
    Z_CLEANUP(zstr_free) zstr title = zstr_from("{b}Delete ");
    char count_str[32];
    snprintf(count_str, sizeof(count_str), "%zu", marked_items.length);
    zstr_cat(&title, count_str);
    zstr_cat(&title, " director");
    zstr_cat(&title, marked_items.length == 1 ? "y" : "ies");
    zstr_cat(&title, "?{/b}\x1b[K\n\x1b[K\n");

    Z_CLEANUP(zstr_free) zstr title_exp = zstr_expand_tokens(zstr_cstr(&title));
    WRITE(STDERR_FILENO, zstr_cstr(&title_exp), zstr_len(&title_exp));

    // List items (max 10)
    int max_show = 10;
    if (max_show > (int)marked_items.length) max_show = (int)marked_items.length;
    for (int i = 0; i < max_show; i++) {
      Z_CLEANUP(zstr_free) zstr item = zstr_from("  {dim}-{reset} ");
      zstr_cat(&item, zstr_cstr(&marked_items.data[i]->name));
      zstr_cat(&item, "\x1b[K\n");
      Z_CLEANUP(zstr_free) zstr item_exp = zstr_expand_tokens(zstr_cstr(&item));
      WRITE(STDERR_FILENO, zstr_cstr(&item_exp), zstr_len(&item_exp));
    }
    if ((int)marked_items.length > max_show) {
      char more[64];
      snprintf(more, sizeof(more), "  {dim}...and %zu more{reset}\x1b[K\n",
               marked_items.length - max_show);
      Z_CLEANUP(zstr_free) zstr more_exp = zstr_expand_tokens(more);
      WRITE(STDERR_FILENO, zstr_cstr(&more_exp), zstr_len(&more_exp));
    }

    // Prompt - track line for cursor positioning
    // Line 1: title, Line 2: blank, Lines 3..3+items-1: items, then blank, then prompt
    // So prompt is at line: 1 (title) + 1 (blank) + items + 1 (blank before prompt) + 1 = 4 + items
    int prompt_line = 4 + max_show + ((int)marked_items.length > max_show ? 1 : 0);
    int prompt_col = 22 + (int)zstr_len(&confirm_input); // "Type YES to confirm: " = 21 chars + 1

    Z_CLEANUP(zstr_free) zstr prompt = zstr_from("\x1b[K\n{dim}Type {/fg}{b}YES{/b}{dim} to confirm:{reset} ");
    zstr_cat(&prompt, zstr_cstr(&confirm_input));
    zstr_cat(&prompt, "\x1b[K");

    Z_CLEANUP(zstr_free) zstr prompt_exp = zstr_expand_tokens(zstr_cstr(&prompt));
    WRITE(STDERR_FILENO, zstr_cstr(&prompt_exp), zstr_len(&prompt_exp));

    WRITE(STDERR_FILENO, "\n\x1b[J", 4); // Newline then clear rest

    // Position cursor on prompt line after input
    char cursor_pos[32];
    snprintf(cursor_pos, sizeof(cursor_pos), "\x1b[%d;%dH", prompt_line, prompt_col);
    WRITE(STDERR_FILENO, cursor_pos, strlen(cursor_pos));
    WRITE(STDERR_FILENO, "\x1b[?25h", 6); // Show cursor

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
    } else if (c == BACKSPACE || c == 127) {
      if (zstr_len(&confirm_input) > 0) {
        zstr_pop_char(&confirm_input);
      }
    } else if (!iscntrl(c) && c < 128) {
      zstr_push(&confirm_input, (char)c);
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

  WRITE(STDERR_FILENO, "\x1b[?25l", 6); // Hide cursor
  WRITE(STDERR_FILENO, "\x1b[H", 3);    // Home

  // Build separator line dynamically (handles any terminal width)
  Z_CLEANUP(zstr_free) zstr sep_line = zstr_init();
  for (int i = 0; i < cols; i++)
    zstr_cat(&sep_line, "─");

  // Header
  // Use AUTO_ZSTR for temp strings
  {
    Z_CLEANUP(zstr_free)
    zstr header_fmt =
        zstr_from("{h1}📁 Try Directory Selection{reset}\x1b[K\n{dim}");
    zstr_cat(&header_fmt, zstr_cstr(&sep_line));
    zstr_cat(&header_fmt, "{reset}\x1b[K\n");

    Z_CLEANUP(zstr_free) zstr header = zstr_expand_tokens(zstr_cstr(&header_fmt));
    WRITE(STDERR_FILENO, zstr_cstr(&header), zstr_len(&header));
  }

  // Search bar - track cursor position
  int search_cursor_col = -1;
  int search_line = 3;
  {
    Z_CLEANUP(zstr_free)
    zstr search_fmt = zstr_from("{b}Search:{/b} ");
    zstr_cat(&search_fmt, zstr_cstr(&filter_buffer));
    zstr_cat(&search_fmt, "{cursor}\x1b[K\n{dim}");
    zstr_cat(&search_fmt, zstr_cstr(&sep_line));
    zstr_cat(&search_fmt, "{reset}\x1b[K\n");

    TokenExpansion search_exp = zstr_expand_tokens_with_cursor(zstr_cstr(&search_fmt));
    search_cursor_col = search_exp.cursor_pos;
    WRITE(STDERR_FILENO, zstr_cstr(&search_exp.expanded), zstr_len(&search_exp.expanded));
    zstr_free(&search_exp.expanded);
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
        // This is approximate since rendered has tokens, but good enough
        int chars_to_copy = max_name_len - 1; // Leave room for ellipsis

        // Copy character by character, skipping tokens
        int visible_count = 0;
        const char *p = rendered;
        while (*p && visible_count < chars_to_copy) {
          if (*p == '{') {
            // Copy token
            zstr_push(&display_name, *p++);
            while (*p && *p != '}') {
              zstr_push(&display_name, *p++);
            }
            if (*p == '}') {
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

      // Render the directory entry
      bool is_marked = entry->marked_for_delete;
      if (is_selected) {
        if (is_marked) {
          zstr_cat(&line, "{b}→ {/b}🗑️ {strike}{section}");
          zstr_cat(&line, zstr_cstr(&display_name));
          zstr_cat(&line, "{/section}{/strike}");
        } else {
          zstr_cat(&line, "{b}→ {/b}📁 {section}");
          zstr_cat(&line, zstr_cstr(&display_name));
          zstr_cat(&line, "{/section}");
        }
      } else {
        if (is_marked) {
          zstr_cat(&line, "  🗑️ {strike}");
          zstr_cat(&line, zstr_cstr(&display_name));
          zstr_cat(&line, "{/strike}");
        } else {
          zstr_cat(&line, "  📁 ");
          zstr_cat(&line, zstr_cstr(&display_name));
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
          zstr_push(&line, ' ');
        }
        zstr_cat(&line, "{dim}");
        zstr_cat(&line, zstr_cstr(&full_meta));
        zstr_cat(&line, "{reset}");
      } else if (available_space > -full_meta_len + 3) {
        // Partial overlap - show truncated metadata (cut from left)
        // available_space can be negative if name extends into metadata area
        int chars_to_skip = (available_space < 1) ? (1 - available_space) : 0;
        int chars_to_show = full_meta_len - chars_to_skip;
        if (chars_to_show > 2) {
          zstr_cat(&line, " {dim}");
          const char *meta_str = zstr_cstr(&full_meta);
          zstr_cat(&line, meta_str + chars_to_skip);
          zstr_cat(&line, "{reset}");
        }
      }

      zstr_cat(&line, "\x1b[K\n");

      Z_CLEANUP(zstr_free) zstr exp = zstr_expand_tokens(zstr_cstr(&line));
      WRITE(STDERR_FILENO, zstr_cstr(&exp), zstr_len(&exp));

    } else if (idx == (int)filtered_ptrs.length && zstr_len(&filter_buffer) > 0) {
      // Add separator line before "Create new"
      WRITE(STDERR_FILENO, "\x1b[K\n", 4);
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

      if (idx == selected_index) {
        Z_CLEANUP(zstr_free)
        zstr line = zstr_from("{b}→ {/b}📂 Create new: {dim}");
        zstr_cat(&line, zstr_cstr(&preview));
        zstr_cat(&line, "{reset}\x1b[K\n");

        Z_CLEANUP(zstr_free) zstr exp = zstr_expand_tokens(zstr_cstr(&line));
        WRITE(STDERR_FILENO, zstr_cstr(&exp), zstr_len(&exp));
      } else {
        Z_CLEANUP(zstr_free)
        zstr line = zstr_from("  📂 Create new: {dim}");
        zstr_cat(&line, zstr_cstr(&preview));
        zstr_cat(&line, "{reset}\x1b[K\n");

        Z_CLEANUP(zstr_free) zstr exp = zstr_expand_tokens(zstr_cstr(&line));
        WRITE(STDERR_FILENO, zstr_cstr(&exp), zstr_len(&exp));
      }
    } else {
      WRITE(STDERR_FILENO, "\x1b[K\n", 4);
    }
  }

  WRITE(STDERR_FILENO, "\x1b[J", 3); // Clear rest

  // Footer
  {
    Z_CLEANUP(zstr_free) zstr footer_fmt = zstr_from("{dim}");
    zstr_cat(&footer_fmt, zstr_cstr(&sep_line));
    zstr_cat(&footer_fmt, "{reset}\x1b[K\n");

    if (marked_count > 0) {
      // Delete mode footer
      char count_str[32];
      snprintf(count_str, sizeof(count_str), "%d", marked_count);
      zstr_cat(&footer_fmt, "{b}DELETE MODE{/b} | ");
      zstr_cat(&footer_fmt, count_str);
      zstr_cat(&footer_fmt, " marked | {dim}Ctrl-D: Toggle  Enter: Confirm  Esc: Cancel{reset}\x1b[K\n");
    } else {
      // Normal footer
      zstr_cat(&footer_fmt, "{dim}↑/↓: Navigate  Enter: Select  Ctrl-D: Delete  Esc: Cancel{reset}\x1b[K\n");
    }

    Z_CLEANUP(zstr_free) zstr footer = zstr_expand_tokens(zstr_cstr(&footer_fmt));
    WRITE(STDERR_FILENO, zstr_cstr(&footer), zstr_len(&footer));
  }

  // Position cursor in search field and show it
  if (search_cursor_col >= 0) {
    char cursor_pos[32];
    snprintf(cursor_pos, sizeof(cursor_pos), "\x1b[%d;%dH", search_line, search_cursor_col);
    WRITE(STDERR_FILENO, cursor_pos, strlen(cursor_pos));
  }
  WRITE(STDERR_FILENO, "\x1b[?25h", 6); // Show cursor

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

  if (initial_filter) {
    zstr_cat(&filter_buffer, initial_filter);
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
          result.delete_names = malloc(marked_count * sizeof(char*));
          result.delete_count = 0;
          for (size_t i = 0; i < filtered_ptrs.length; i++) {
            if (filtered_ptrs.data[i]->marked_for_delete) {
              result.delete_names[result.delete_count++] = strdup(zstr_cstr(&filtered_ptrs.data[i]->name));
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
        // Create new
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char date_prefix[20];
        strftime(date_prefix, sizeof(date_prefix), "%Y-%m-%d", t);

        zstr new_name = zstr_from(date_prefix);
        zstr_cat(&new_name, "-");
        zstr_cat(&new_name, zstr_cstr(&filter_buffer));

        // Replace spaces with dashes
        char *new_name_data = zstr_data(&new_name);
        for (size_t i = 0; i < zstr_len(&new_name); i++) {
          if (isspace(new_name_data[i]))
            new_name_data[i] = '-';
        }

        result.type = ACTION_MKDIR;
        result.path = join_path(base_path, zstr_cstr(&new_name));
        zstr_free(&new_name);
      }
      break;
    } else if (c == ARROW_UP) {
      if (selected_index > 0)
        selected_index--;
    } else if (c == ARROW_DOWN) {
      int max_idx = filtered_ptrs.length;
      if (zstr_len(&filter_buffer) > 0)
        max_idx++;
      if (selected_index < max_idx - 1)
        selected_index++;
    } else if (c == BACKSPACE || c == 127) {
      if (zstr_len(&filter_buffer) > 0) {
        // Simple backspace (remove last char)
        zstr_pop_char(&filter_buffer);
        filter_tries();
      }
    } else if (!iscntrl(c) && c < 128) {
      zstr_push(&filter_buffer, (char)c);
      filter_tries();
    }
  }

  if (!is_test || !mode->inject_keys) {
    // Disable alternate screen buffer (restores original screen)
    disable_alternate_screen();
    // Reset terminal state
    disable_raw_mode();
    // Reset all attributes
    fprintf(stderr, "\x1b[0m");
    fflush(stderr);
  }

  clear_state();
  vec_free_TryEntryPtr(&filtered_ptrs);
  zstr_free(&filter_buffer);
  marked_count = 0;

  return result;
}
