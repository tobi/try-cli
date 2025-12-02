#ifndef UTILS_H
#define UTILS_H

#include "zstr.h"
#include "zvec.h"
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

// Generate common vector types
Z_VEC_GENERATE_IMPL(zstr, zstr)
Z_VEC_GENERATE_IMPL(char *, char_ptr)

// ANSI Colors
#define ANSI_RESET "\033[0m"
#define ANSI_BOLD "\033[1m"
#define ANSI_DIM "\033[2m"
#define ANSI_RED "\033[31m"
#define ANSI_GREEN "\033[32m"
#define ANSI_YELLOW "\033[33m"
#define ANSI_BLUE "\033[34m"
#define ANSI_MAGENTA "\033[35m"
#define ANSI_CYAN "\033[36m"
#define ANSI_WHITE "\033[37m"

// Defer helper for standard pointers (zstr has Z_CLEANUP(zstr_free))
static inline void cleanup_free(void *p) { free(*(void **)p); }
#define AUTO_FREE Z_CLEANUP(cleanup_free)

// Global flag to disable token expansion (for testing)
extern bool zstr_disable_token_expansion;

// Global flag to disable colors (tokens expand to empty strings)
extern bool zstr_no_colors;

// Result of token expansion with optional cursor position
typedef struct {
  zstr expanded;
  int cursor_pos; // -1 if no cursor marker found, otherwise visual column (1-indexed)
} TokenExpansion;

// Token expansion for UI
// Returns a zstr that must be freed (or use Z_CLEANUP(zstr_free))
zstr zstr_expand_tokens(const char *text);

// Token expansion with cursor tracking
TokenExpansion zstr_expand_tokens_with_cursor(const char *text);

// String helpers
char *trim(char *str); // Operates in-place
zstr join_path(const char *dir, const char *file);
zstr get_home_dir(void);
zstr get_default_tries_path(void);

// File helpers
bool dir_exists(const char *path);
bool file_exists(const char *path);
int mkdir_p(const char *path);
zstr format_relative_time(time_t mtime);

// Directory name validation
// Returns normalized name (spaces -> hyphens, collapse multiples, strip edges)
// Returns empty string if name contains invalid characters
// Valid chars: [a-zA-Z0-9_.-]
zstr normalize_dir_name(const char *name);

// Check if name contains only valid directory name characters
bool is_valid_dir_name(const char *name);

#endif // UTILS_H
