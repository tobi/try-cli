#define _POSIX_C_SOURCE 200809L
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

static void clear_state() {
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

static void filter_tries() {
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

static void render(const char *base_path) {
  int rows, cols;
  get_window_size(&rows, &cols);

  write(STDERR_FILENO, "\x1b[?25l", 6); // Hide cursor
  write(STDERR_FILENO, "\x1b[H", 3);    // Home

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
    write(STDERR_FILENO, zstr_cstr(&header), zstr_len(&header));
  }

  // Search bar
  {
    Z_CLEANUP(zstr_free)
    zstr search_fmt = zstr_from("{b}Search:{/b} ");
    zstr_cat(&search_fmt, zstr_cstr(&filter_buffer));
    zstr_cat(&search_fmt, "\x1b[K\r\n{dim}");
    zstr_cat(&search_fmt, sep_line);
    zstr_cat(&search_fmt, "{reset}\x1b[K\r\n");

    Z_CLEANUP(zstr_free) zstr search = zstr_expand_tokens(zstr_cstr(&search_fmt));
    write(STDERR_FILENO, zstr_cstr(&search), zstr_len(&search));
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

      Z_CLEANUP(zstr_free) zstr rel_time = format_relative_time(entry->mtime);

      // Calculate padding
      // Note: This is approximate as rendered string has escape codes
      // Ideally we'd strip codes to get length, but for now use name length
      int plain_len = zstr_len(&entry->name);
      int meta_len = zstr_len(&rel_time) + 10; // + score digits
      int padding_len = cols - 5 - plain_len - meta_len;
      if (padding_len < 1)
        padding_len = 1;

      char padding[256];
      memset(padding, ' ', padding_len < 255 ? padding_len : 255);
      padding[padding_len < 255 ? padding_len : 255] = '\0';

      Z_CLEANUP(zstr_free) zstr line = zstr_init();

      if (is_selected) {
        zstr_cat(&line, "{b}→ {/b}📁 {section}");
        zstr_cat(&line, zstr_cstr(&entry->rendered));
        zstr_cat(&line, "{/section}");
      } else {
        zstr_cat(&line, "  📁 ");
        zstr_cat(&line, zstr_cstr(&entry->rendered));
      }

      zstr_cat(&line, padding);
      zstr_cat(&line, "{dim}");
      zstr_cat(&line, zstr_cstr(&rel_time));
      zstr_fmt(&line, ", %.1f", entry->score);
      zstr_cat(&line, "{reset}\x1b[K\r\n");

      Z_CLEANUP(zstr_free) zstr exp = zstr_expand_tokens(zstr_cstr(&line));
      write(STDERR_FILENO, zstr_cstr(&exp), zstr_len(&exp));

    } else if (idx == (int)filtered_ptrs.length && zstr_len(&filter_buffer) > 0) {
      if (idx == selected_index) {
        Z_CLEANUP(zstr_free)
        zstr line = zstr_from("{b}→ {/b}+ Create new: ");
        zstr_cat(&line, zstr_cstr(&filter_buffer));
        zstr_cat(&line, "\x1b[K\r\n");

        Z_CLEANUP(zstr_free) zstr exp = zstr_expand_tokens(zstr_cstr(&line));
        write(STDERR_FILENO, zstr_cstr(&exp), zstr_len(&exp));
      } else {
        fprintf(stderr, "  + Create new: %s\x1b[K\r\n",
                zstr_cstr(&filter_buffer));
      }
    } else {
      write(STDERR_FILENO, "\x1b[K\r\n", 5);
    }
  }

  write(STDERR_FILENO, "\x1b[J", 3); // Clear rest

  // Footer
  {
    Z_CLEANUP(zstr_free) zstr footer_fmt = zstr_from("{dim}");
    zstr_cat(&footer_fmt, sep_line);
    zstr_cat(&footer_fmt, "{reset}\x1b[K\r\n{dim}↑/↓: Navigate  Enter: "
                          "Select  ESC: Cancel{reset}\x1b[K\r\n");

    Z_CLEANUP(zstr_free) zstr footer = zstr_expand_tokens(zstr_cstr(&footer_fmt));
    write(STDERR_FILENO, zstr_cstr(&footer), zstr_len(&footer));
  }

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
      if (test_mode->inject_keys[test_mode->key_index] == '\0') {
        // No more keys, exit
        break;
      }
      c = test_mode->inject_keys[test_mode->key_index++];
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
    disable_raw_mode();
    fprintf(stderr, "\n");
  }

  clear_state();
  vec_free_TryEntryPtr(&filtered_ptrs);
  zstr_free(&filter_buffer);

  return result;
}
