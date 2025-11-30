#ifndef TUI_H
#define TUI_H

#include "zstr.h"
#include <stdbool.h>
#include <time.h>

typedef enum { ACTION_NONE, ACTION_CD, ACTION_MKDIR, ACTION_CANCEL } ActionType;

typedef struct {
  zstr path;     // Full path
  zstr name;     // Directory name
  zstr rendered; // Pre-rendered string with tokens
  time_t mtime;
  float score;
} TryEntry;

typedef struct {
  ActionType type;
  zstr path;
} SelectionResult;

// Test mode configuration (for automated testing)
typedef struct {
  bool test_mode;         // Enable test mode
  bool render_once;       // Render once and exit (--and-exit)
  const char *inject_keys; // Keys to inject (--and-keys)
  int key_index;          // Current position in inject_keys
} TestMode;

// Run the interactive selector
// base_path: directory to scan for tries
// initial_filter: initial search term (can be NULL)
// test_mode: test configuration (can be NULL for normal operation)
SelectionResult run_selector(const char *base_path, const char *initial_filter,
                             TestMode *test_mode);

#endif // TUI_H
