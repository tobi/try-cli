# TUI (Terminal User Interface) Specification

## Overview

The TUI provides an interactive directory selector for the `try` CLI tool, featuring fuzzy search, keyboard navigation, and responsive layout that adapts to terminal window size changes.

## Terminal Size Introspection

### Window Size Detection

The TUI uses a multi-fallback approach to determine terminal dimensions:

1. **Primary Method**: `ioctl(STDERR_FILENO, TIOCGWINSZ, &ws)`
   - Uses POSIX `ioctl` to query terminal size directly
   - Most reliable and efficient method

2. **Secondary Method**: External `tput` command
   - Falls back to `tput cols` and `tput lines` commands
   - Useful when ioctl is unavailable

3. **Fallback Defaults**: 80 columns × 24 rows
   - Used when all detection methods fail
   - Ensures TUI functions even in constrained environments

### Dynamic Layout Calculation

Layout dimensions are recalculated on every render cycle:

```c
int rows, cols;
get_window_size(&rows, &cols);

// Header: 3 lines (title + separator + search)
int header_height = 3;

// Footer: 2 lines (separator + help)
int footer_height = 2;

// List area: remaining space
int list_height = rows - header_height - footer_height;
```

## Resize Handling

### SIGWINCH Signal Handling

Terminal resize events are handled through POSIX signals:

- **Signal Registration**: `sigaction(SIGWINCH, &handle_winch, NULL)`
- **Handler Behavior**: Minimal handler that simply interrupts blocking I/O
- **Interrupt Mechanism**: `read_key()` returns `KEY_RESIZE` when interrupted

### Resize Event Processing

When a resize occurs:

1. **Signal Interrupt**: `SIGWINCH` interrupts the `read()` call in `read_key()`
2. **Special Return Code**: `read_key()` returns `KEY_RESIZE` (-2)
3. **Main Loop Response**: TUI continues to next iteration without processing input
4. **Automatic Re-render**: Next `render()` call uses new window dimensions
5. **State Preservation**: Selection index and scroll offset are maintained

### Layout Adaptation

Resize handling ensures:
- **Immediate Responsiveness**: UI updates immediately after resize
- **State Continuity**: User selection and scroll position preserved
- **Content Reflow**: Text wrapping and truncation adapt to new dimensions

## Path and Metadata Layer Architecture

### Two-Layer Display System

The TUI uses a dual-layer approach for displaying directory entries:

#### Primary Layer: Directory Path
- **Content**: Directory name with fuzzy match highlighting
- **Position**: Left-aligned after icon and selection indicator
- **Behavior**: Expands to fill available space, truncated with ellipsis if needed

#### Secondary Layer: Metadata
- **Content**: Relative timestamp and fuzzy match score
- **Position**: Right-aligned, always anchored to terminal right edge
- **Behavior**: Only displayed when sufficient space exists

### Layout Calculation Algorithm

For each directory entry line:

```c
// Fixed prefix components
int prefix_len = 5; // "→ 📁 " (selection arrow + emoji + space)

// Metadata calculation
int meta_len = timestamp_length + score_length; // e.g., "2h ago, 3.2"

// Available space for path
int max_path_len = cols - prefix_len - 1; // Reserve 1 char safety margin

// Metadata positioning
int meta_start_col = cols - meta_len - 1; // Right-aligned with margin
int path_end_col = prefix_len + actual_path_len;

// Display metadata only if no overlap
bool show_metadata = (path_end_col + 1) < meta_start_col;
```

## Metadata Right-Bound Positioning

### Absolute Right Alignment

Metadata is always positioned relative to the terminal right edge:

- **Anchor Point**: `terminal_width - metadata_length - 1`
- **Coordinate System**: 1-based column indexing (ANSI terminal standard)
- **Margin**: 1 character gap from right edge prevents wrapping

### Conditional Display Logic

Metadata visibility depends on available space:

```c
// Only show metadata if path doesn't overlap
if (!path_truncated && path_end_position + 1 < metadata_start_position) {
    display_metadata_with_padding();
}
```

### Padding Calculation

When metadata is displayed:

```c
int padding_needed = metadata_start_column - path_end_column;
char padding[256];
memset(padding, ' ', padding_needed);
```

## Path Truncation with Ellipsis

### Truncation Algorithm

Paths are truncated when they exceed available space:

```c
bool needs_truncation = plain_path_length > max_allowed_length;
if (needs_truncation && max_allowed_length > 4) {
    int chars_to_keep = max_allowed_length - 1; // Reserve space for "…"

    // Copy characters, preserving token structure
    int visible_chars = 0;
    while (*source && visible_chars < chars_to_keep) {
        if (*source == '{') {
            // Copy entire token to preserve formatting
            copy_token_to_destination();
        } else {
            copy_char_to_destination();
            visible_chars++;
        }
    }

    append_ellipsis("…");
}
```

### Token-Aware Truncation

The truncation algorithm preserves ANSI token integrity:

- **Token Recognition**: Detects `{token}` patterns in rendered strings
- **Atomic Copying**: Tokens are copied entirely or not at all
- **Visible Character Counting**: Only counts displayable characters toward limit
- **Ellipsis Addition**: Unicode "…" character appended to indicate truncation

### Truncation Impact on Metadata

When paths are truncated, metadata is hidden:

```c
// Metadata suppressed when path truncation occurs
bool show_metadata = !path_truncated && has_sufficient_space;
```

This ensures the UI remains clean and readable when space is constrained.

## Layout Components

### Header Section (Lines 1-3)
```
┌─────────────────────────────────────────────────┐
│ 📁 Try Directory Selection                      │
├─────────────────────────────────────────────────┤
│ Search: [user input]                            │
└─────────────────────────────────────────────────┘
```

### List Section (Dynamic Height)
```
  → 📁 [path....................................] [metadata]
    📁 [path....................................] [metadata]
    📁 [path....................................] [metadata]
```

### Footer Section (Lines -2 to -1)
```
┌─────────────────────────────────────────────────┐
│ ↑/↓: Navigate  Enter: Select  ESC: Cancel       │
└─────────────────────────────────────────────────┘
```

## Performance Characteristics

- **Render Frequency**: Full re-render on every keypress and resize
- **Layout Calculation**: O(1) per entry, O(n) for n entries
- **Memory Usage**: Minimal, reuses buffers between renders
- **Responsiveness**: Immediate visual feedback for all interactions</content>
<parameter name="filePath">docs/tui_spec.md