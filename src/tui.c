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

static void handle_winch(int sig) {
  (void)sig;
  // Do nothing, just interrupt read()
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

// Parse key from test mode injected keys (handles escape sequences)
static int read_test_key(TestMode *test_mode) {
  if (test_mode->inject_keys[test_mode->key_index] == '\0') {
    return -1; // End of keys
  }

  unsigned char c = test_mode->inject_keys[test_mode->key_index++];

  if (c == '\x1b') {
    // Check for escape sequence
    if (test_mode->inject_keys[test_mode->key_index] == '[') {
      test_mode->key_index++;
      unsigned char seq = test_mode->inject_keys[test_mode->key_index++];
      switch (seq) {
      case 'A':
        return ARROW_UP;
      case 'B':
        return ARROW_DOWN;
      case 'C':
        return ARROW_RIGHT;
      case 'D':
        return ARROW_LEFT;
      default:
        return ESC_KEY;
      }
    }
    return ESC_KEY;
  } else if (c == '\r') {
    return ENTER_KEY;
  } else {
    return c;
  }
}

static void render(const char *base_path) {
  int rows, cols;
  get_window_size(&rows, &cols);

  WRITE(STDERR_FILENO, "\x1b[?25l", 6); // Hide cursor
  WRITE(STDERR_FILENO, "\x1b[H", 3);    // Home

  char sep_line[512] = {0};
  for (int i = 0; i < cols && i < 300; i++)
    strcat(sep_line, "─");

  // Header
  // Use AUTO_ZSTR for temp strings
  {
    Z_CLEANUP(zstr_free)
    zstr header_fmt =
        zstr_from("{h1}📁 Try Directory Selection{reset}\x1b[K\r\n{dim}");
    zstr_cat(&header_fmt, sep_line);
    zstr_cat(&header_fmt, "{reset}\x1b[K\r\n");

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
    zstr_cat(&search_fmt, "{cursor}\x1b[K\r\n{dim}");
    zstr_cat(&search_fmt, sep_line);
    zstr_cat(&search_fmt, "{reset}\x1b[K\r\n");

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

      // Calculate metadata length (for later use)
      Z_CLEANUP(zstr_free) zstr rel_time = format_relative_time(entry->mtime);
      char score_text[16];
      snprintf(score_text, sizeof(score_text), ", %.1f", entry->score);
      int meta_len = zstr_len(&rel_time) + strlen(score_text);

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
      if (is_selected) {
        zstr_cat(&line, "{b}→ {/b}📁 {section}");
        zstr_cat(&line, zstr_cstr(&display_name));
        zstr_cat(&line, "{/section}");
      } else {
        zstr_cat(&line, "  📁 ");
        zstr_cat(&line, zstr_cstr(&display_name));
      }

      // Calculate positions for right-aligned metadata
      int actual_name_len = name_truncated ? max_name_len : plain_len;
      int path_end_pos = prefix_len + actual_name_len;
      int meta_start_pos = cols - meta_len - 1; // -1 to avoid wrapping at edge

      // Only show metadata if there's room without overlap (at least 1 space gap)
      if (!name_truncated && path_end_pos + 1 < meta_start_pos) {
        int padding_len = meta_start_pos - path_end_pos;

        char padding[256];
        int safe_padding = padding_len < 255 ? padding_len : 255;
        memset(padding, ' ', safe_padding);
        padding[safe_padding] = '\0';

        zstr_cat(&line, padding);
        zstr_cat(&line, "{dim}");
        zstr_cat(&line, zstr_cstr(&rel_time));
        zstr_cat(&line, score_text);
        zstr_cat(&line, "{reset}");
      }

      zstr_cat(&line, "\x1b[K\r\n");

      Z_CLEANUP(zstr_free) zstr exp = zstr_expand_tokens(zstr_cstr(&line));
      WRITE(STDERR_FILENO, zstr_cstr(&exp), zstr_len(&exp));

    } else if (idx == (int)filtered_ptrs.length && zstr_len(&filter_buffer) > 0) {
      // Add separator line before "Create new"
      WRITE(STDERR_FILENO, "\x1b[K\r\n", 5);
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
        zstr_cat(&line, "{reset}\x1b[K\r\n");

        Z_CLEANUP(zstr_free) zstr exp = zstr_expand_tokens(zstr_cstr(&line));
        WRITE(STDERR_FILENO, zstr_cstr(&exp), zstr_len(&exp));
      } else {
        Z_CLEANUP(zstr_free)
        zstr line = zstr_from("  📂 Create new: {dim}");
        zstr_cat(&line, zstr_cstr(&preview));
        zstr_cat(&line, "{reset}\x1b[K\r\n");

        Z_CLEANUP(zstr_free) zstr exp = zstr_expand_tokens(zstr_cstr(&line));
        WRITE(STDERR_FILENO, zstr_cstr(&exp), zstr_len(&exp));
      }
    } else {
      WRITE(STDERR_FILENO, "\x1b[K\r\n", 5);
    }
  }

  WRITE(STDERR_FILENO, "\x1b[J", 3); // Clear rest

  // Footer
  {
    Z_CLEANUP(zstr_free) zstr footer_fmt = zstr_from("{dim}");
    zstr_cat(&footer_fmt, sep_line);
    zstr_cat(&footer_fmt, "{reset}\x1b[K\r\n{dim}↑/↓: Navigate  Enter: "
                          "Select  ESC: Cancel{reset}\x1b[K\r\n");

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
                             TestMode *test_mode) {
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

  // Test mode: render once and exit
  if (test_mode && test_mode->render_once) {
    render(base_path);
    SelectionResult result = {.type = ACTION_CANCEL, .path = zstr_init()};
    return result;
  }

  // Only setup TTY if not in test mode or if we need to read keys
  if (!test_mode || !test_mode->inject_keys) {
    enable_raw_mode();

    struct sigaction sa;
    sa.sa_handler = handle_winch;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGWINCH, &sa, NULL);

    clear_screen();
  }

  SelectionResult result = {.type = ACTION_CANCEL, .path = zstr_init()};

  while (1) {
    if (!test_mode || !test_mode->inject_keys) {
      render(base_path);
    }

    // Read key from injected keys or real input
    int c;
    if (test_mode && test_mode->inject_keys) {
      c = read_test_key(test_mode);
    } else {
      c = read_key();
    }
    if (c == KEY_RESIZE) {
      continue;
    }
    if (c == -1)
      break;

    if (c == ESC_KEY || c == 3) {
      break;
    } else if (c == ENTER_KEY) {
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

  if (!test_mode || !test_mode->inject_keys) {
    // Clear the screen before exiting
    WRITE(STDERR_FILENO, "\x1b[2J\x1b[H", 7); // Clear screen and move cursor to home
    disable_raw_mode();
  }

  clear_state();
  vec_free_TryEntryPtr(&filtered_ptrs);
  zstr_free(&filter_buffer);

  return result;
}
