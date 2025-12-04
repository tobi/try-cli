#ifndef TUI_H
#define TUI_H

#include "tui_style.h"
#include "libs/zvec.h"
#include <time.h>

// Generate vec_zstr type
Z_VEC_GENERATE_IMPL(zstr, zstr)

// ============================================================================
// Selector Types
// ============================================================================

typedef enum {
  ACTION_NONE,
  ACTION_CD,
  ACTION_MKDIR,
  ACTION_CANCEL,
  ACTION_DELETE
} ActionType;

typedef struct {
  zstr path;
  zstr name;
  zstr rendered;
  time_t mtime;
  float score;
  bool marked_for_delete;
} TryEntry;

typedef struct {
  ActionType type;
  zstr path;
  vec_zstr delete_names;
} SelectionResult;

// Testing parameters (for automated tests)
typedef struct {
  bool render_once;        // Render one frame and exit
  const char *inject_keys; // Simulate keypresses
  int key_index;           // Current position in inject_keys
} TestParams;

// Selector
SelectionResult run_selector(const char *base_path, const char *initial_filter,
                             TestParams *test);

#endif /* TUI_H */
