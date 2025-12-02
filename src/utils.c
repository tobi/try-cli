// Feature test macros for cross-platform compatibility
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#else
#define _GNU_SOURCE
#endif

#include "utils.h"
#include "config.h"
#include <ctype.h>
#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

// Global flag to disable token expansion
bool zstr_disable_token_expansion = false;

// Global flag to disable colors (tokens expand to empty strings)
bool zstr_no_colors = false;

TokenExpansion zstr_expand_tokens_with_cursor(const char *text) {
  TokenExpansion result;
  result.expanded = zstr_init();
  result.cursor_pos = -1;

  const char *in = text;
  int visual_pos = 1; // 1-indexed for terminal positioning

  // If expansion is disabled, just return a copy of the text
  if (zstr_disable_token_expansion) {
    zstr_cat(&result.expanded, text);
    return result;
  }

  while (*in) {
    if (*in == '{') {
      if (strncmp(in, "{cursor}", 8) == 0) {
        result.cursor_pos = visual_pos;
        in += 8;
      } else if (strncmp(in, "{h1}", 4) == 0) {
        if (!zstr_no_colors) {
          zstr_cat(&result.expanded, ANSI_BOLD);
          zstr_cat(&result.expanded, "\033[38;5;214m"); // Orange
        }
        in += 4;
      } else if (strncmp(in, "{h2}", 4) == 0) {
        if (!zstr_no_colors) {
          zstr_cat(&result.expanded, ANSI_BOLD);
          zstr_cat(&result.expanded, ANSI_BLUE);
        }
        in += 4;
      } else if (strncmp(in, "{b}", 3) == 0) {
        if (!zstr_no_colors) {
          zstr_cat(&result.expanded, ANSI_BOLD);
          zstr_cat(&result.expanded, ANSI_YELLOW);
        }
        in += 3;
      } else if (strncmp(in, "{/b}", 4) == 0) {
        if (!zstr_no_colors) {
          zstr_cat(&result.expanded, "\033[22m"); // Turn off bold
          zstr_cat(&result.expanded, "\033[39m"); // Reset foreground color
        }
        in += 4;
      } else if (strncmp(in, "{dim}", 5) == 0) {
        if (!zstr_no_colors) {
          zstr_cat(&result.expanded, "\033[90m"); // Bright black (gray) like Ruby version
        }
        in += 5;
      } else if (strncmp(in, "{reset}", 7) == 0) {
        if (!zstr_no_colors) {
          zstr_cat(&result.expanded, ANSI_RESET);
        }
        in += 7;
      } else if (strncmp(in, "{/fg}", 5) == 0) {
        if (!zstr_no_colors) {
          zstr_cat(&result.expanded, "\033[39m"); // Reset foreground
        }
        in += 5;
      } else if (strncmp(in, "{text}", 6) == 0) {
        if (!zstr_no_colors) {
          zstr_cat(&result.expanded, ANSI_RESET);
        }
        in += 6;
      } else if (strncmp(in, "{section}", 9) == 0) {
        if (!zstr_no_colors) {
          zstr_cat(&result.expanded, ANSI_BOLD);
        }
        in += 9;
      } else if (strncmp(in, "{/section}", 10) == 0) {
        if (!zstr_no_colors) {
          zstr_cat(&result.expanded, ANSI_RESET);
        }
        in += 10;
      } else if (strncmp(in, "{strike}", 8) == 0) {
        if (!zstr_no_colors) {
          zstr_cat(&result.expanded, "\033[48;5;52m"); // Dark red background (#5f0000)
        }
        in += 8;
      } else if (strncmp(in, "{/strike}", 9) == 0) {
        if (!zstr_no_colors) {
          zstr_cat(&result.expanded, "\033[49m"); // Reset background
        }
        in += 9;
      } else {
        zstr_push(&result.expanded, *in++);
        visual_pos++;
      }
    } else if (*in == '\x1b') {
      // Skip ANSI escape sequences (don't count toward visual position)
      zstr_push(&result.expanded, *in++);
      while (*in && !isalpha((unsigned char)*in)) {
        zstr_push(&result.expanded, *in++);
      }
      if (*in) {
        zstr_push(&result.expanded, *in++);
      }
    } else if (*in == '\n') {
      // Newlines don't count toward visual position (next line starts at col 1)
      zstr_push(&result.expanded, *in++);
      visual_pos = 1;
    } else {
      zstr_push(&result.expanded, *in++);
      visual_pos++;
    }
  }
  return result;
}

zstr zstr_expand_tokens(const char *text) {
  TokenExpansion result = zstr_expand_tokens_with_cursor(text);
  return result.expanded;
}

char *trim(char *str) {
  char *end;
  while (isspace((unsigned char)*str))
    str++;
  if (*str == 0)
    return str;
  end = str + strlen(str) - 1;
  while (end > str && isspace((unsigned char)*end))
    end--;
  end[1] = '\0';
  return str;
}

zstr get_home_dir(void) {
  const char *home = getenv("HOME");
  if (!home) {
    struct passwd *pw = getpwuid(getuid());
    if (pw)
      home = pw->pw_dir;
  }
  return home ? zstr_from(home) : zstr_init();
}

zstr join_path(const char *dir, const char *file) {
  zstr s = zstr_from(dir);
  zstr_cat(&s, "/");
  zstr_cat(&s, file);
  return s;
}

zstr get_default_tries_path(void) {
  Z_CLEANUP(zstr_free) zstr home = get_home_dir();
  if (zstr_is_empty(&home))
    return home;
  zstr path = join_path(zstr_cstr(&home), DEFAULT_TRIES_PATH_SUFFIX);
  zstr_free(&home);
  return path;
}

bool dir_exists(const char *path) {
  struct stat sb;
  return (stat(path, &sb) == 0 && S_ISDIR(sb.st_mode));
}

bool file_exists(const char *path) {
  struct stat sb;
  return (stat(path, &sb) == 0 && S_ISREG(sb.st_mode));
}

int mkdir_p(const char *path) {
  Z_CLEANUP(zstr_free) zstr tmp = zstr_from(path);
  
  // Remove trailing slash
  if (zstr_len(&tmp) > 0) {
      char *data = zstr_data(&tmp);
      if (data[zstr_len(&tmp) - 1] == '/') {
          zstr_pop_char(&tmp);
      }
  }

  char *p_start = zstr_data(&tmp);
  // Start after the first char to avoid stopping at root /
  char *p = p_start + 1;

  while (*p) {
    if (*p == '/') {
      *p = 0;
      if (mkdir(p_start, S_IRWXU) != 0 && errno != EEXIST) {
        return -1;
      }
      *p = '/';
    }
    p++;
  }
  if (mkdir(p_start, S_IRWXU) != 0 && errno != EEXIST) {
    return -1;
  }
  return 0;
}

zstr format_relative_time(time_t mtime) {
  time_t now = time(NULL);
  double diff = difftime(now, mtime);
  zstr s = zstr_init();

  if (diff < 60) {
    zstr_cat(&s, "just now");
  } else if (diff < 3600) {
    zstr_fmt(&s, "%dm ago", (int)(diff / 60));
  } else if (diff < 86400) {
    zstr_fmt(&s, "%dh ago", (int)(diff / 3600));
  } else {
    zstr_fmt(&s, "%dd ago", (int)(diff / 86400));
  }
  return s;
}

// Check if a character is valid for directory names
// Valid: alphanumeric, underscore, hyphen, dot
static bool is_valid_dir_char(char c) {
  return isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.';
}

// Check if name contains only valid directory name characters
// (after normalization, so no spaces)
bool is_valid_dir_name(const char *name) {
  if (!name || !*name) return false;

  for (const char *p = name; *p; p++) {
    if (!is_valid_dir_char(*p) && *p != ' ') {
      return false;
    }
  }
  return true;
}

// Normalize directory name:
// - Convert spaces to hyphens
// - Collapse multiple consecutive hyphens to single hyphen
// - Strip leading/trailing hyphens and spaces
// - Return empty string if invalid characters found
zstr normalize_dir_name(const char *name) {
  zstr result = zstr_init();
  if (!name || !*name) return result;

  // First pass: check for invalid characters
  for (const char *p = name; *p; p++) {
    char c = *p;
    if (!is_valid_dir_char(c) && !isspace((unsigned char)c)) {
      // Invalid character found - return empty string
      return result;
    }
  }

  // Second pass: normalize
  bool last_was_hyphen = true;  // Start true to strip leading hyphens/spaces
  for (const char *p = name; *p; p++) {
    char c = *p;
    if (isspace((unsigned char)c) || c == '-') {
      // Convert space to hyphen, collapse multiple hyphens
      if (!last_was_hyphen) {
        zstr_push(&result, '-');
        last_was_hyphen = true;
      }
    } else {
      zstr_push(&result, c);
      last_was_hyphen = false;
    }
  }

  // Strip trailing hyphen
  while (zstr_len(&result) > 0) {
    char *data = zstr_data(&result);
    if (data[zstr_len(&result) - 1] == '-') {
      zstr_pop_char(&result);
    } else {
      break;
    }
  }

  return result;
}
