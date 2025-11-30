#ifndef UTILS_H
#define UTILS_H

#include "zstr.h"
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

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

// Token expansion for UI
// Returns a zstr that must be freed (or use Z_CLEANUP(zstr_free))
zstr zstr_expand_tokens(const char *text);

// String helpers
char *trim(char *str); // Operates in-place
zstr join_path(const char *dir, const char *file);
zstr get_home_dir();
zstr get_default_tries_path();

// File helpers
bool dir_exists(const char *path);
bool file_exists(const char *path);
int mkdir_p(const char *path);
zstr format_relative_time(time_t mtime);

#endif // UTILS_H
