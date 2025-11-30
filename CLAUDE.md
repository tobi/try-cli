# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

`try` is a CLI tool for managing ephemeral development workspaces (called "tries"). It provides an interactive directory selector with fuzzy search and integrates with the shell to quickly navigate between project directories. The tool tracks recently accessed tries and allows cloning repositories into dated directories.

## Reference Implementation

A Ruby reference implementation exists at `docs/try.reference.rb` which demonstrates the full feature set and UI behavior. Use this as the source of truth for expected functionality when implementing features in C.

## Build System

Build the project:
```bash
make
```

Clean build artifacts:
```bash
make clean
```

Install to `/usr/local/bin/try`:
```bash
make install
```

The binary is named `try` and built from C source files in `src/`.

## Architecture

### Shell Integration Pattern

The tool uses a unique architecture where it emits shell commands that are sourced by the parent shell:

1. User runs `try cd` or similar command
2. The C binary executes and prints shell commands to stdout
3. A shell wrapper function sources the output, executing it in the current shell context
4. This allows the tool to change the current directory (impossible for normal subprocesses)

Commands are emitted via `emit_task()` in `src/commands.c`, which prints shell snippets chained with `&&`. Each command chain ends with `true\n`.

### Core Components

**Command Layer** (`src/commands.c`, `src/commands.h`):
- `cmd_init()`: Prints shell function definition for integration
- `cmd_clone()`: Generates shell commands to clone repo into dated directory
- `cmd_cd()`: Launches interactive selector, emits cd command
- `cmd_worktree()`: Placeholder (not implemented)

**Interactive TUI** (`src/tui.c`, `src/tui.h`):
- `run_selector()`: Main interactive loop using raw terminal mode
- Scans directories in tries path (default: `~/src/tries`)
- Returns `SelectionResult` with action type and path
- Supports fuzzy filtering and keyboard navigation

**Fuzzy Matching** (`src/fuzzy.c`, `src/fuzzy.h`):
- `fuzzy_match()`: Updates TryEntry score and rendered output in-place
- `calculate_score()`: Combines fuzzy match score with recency (mtime)
- `highlight_matches()`: Inserts `{b}` tokens around matched characters
- Algorithm favors consecutive character matches and recent access times

**Terminal Management** (`src/terminal.c`, `src/terminal.h`):
- Raw mode terminal control (disables canonical mode, echo)
- Escape sequence parsing for arrow keys, special keys
- Window size detection
- Cursor visibility control

**Utilities** (`src/utils.c`, `src/utils.h`):
- Token expansion: Replaces `{b}`, `{reset}`, etc. with ANSI codes via `zstr_expand_tokens()`
- Path utilities: `join_path()`, `mkdir_p()`, directory existence checks
- Time formatting: `format_relative_time()` for human-readable timestamps
- `AUTO_FREE` macro: Cleanup helper for raw pointers using `Z_CLEANUP()`

**Data Structures** (`libs/zstr.h`, `libs/zvec.h`, `libs/zlist.h`):
- Custom string library (`zstr`) with SSO (Small String Optimization)
- Generic vector implementation (`zvec`) used for TryEntry collections
- Generic list implementation (`zlist`) available but rarely needed
- All libs support RAII-style cleanup with `Z_CLEANUP()` attribute
- **Always use zstr and zvec for all string management and arrays**

### Memory Management

The codebase uses GCC/Clang's `__attribute__((cleanup))` for automatic resource cleanup:

```c
Z_CLEANUP(zstr_free) zstr path = zstr_init();
// path automatically freed when leaving scope
```

Manual cleanup still required in some cases:
- Vector elements must be freed before freeing the vector
- `TryEntry` structs contain zstr fields that need individual `zstr_free()` calls

### Data Flow

1. User invokes `try cd [query]`
2. `main()` parses `--path` flag or uses default tries directory
3. `cmd_cd()` calls `run_selector()` with optional initial filter
4. `run_selector()` scans directories with `scan_tries()`
5. Interactive loop: user types to filter, arrows to navigate
6. On Enter: returns `SelectionResult` with action and path
7. `cmd_cd()` emits shell commands to cd to selected path
8. Shell wrapper sources output, executing the cd command

### Token System

The UI uses a token-based formatting system for dynamic styling. Tokens are placeholder strings embedded in text that get expanded to ANSI escape codes via `zstr_expand_tokens()` in `src/utils.c`. This allows formatting to be defined declaratively without hardcoding ANSI sequences throughout the codebase.

**Available Tokens:**

| Token | ANSI Code | Description |
|-------|-----------|-------------|
| `{h1}` | Bold + Orange (38;5;214m) | Primary headings |
| `{h2}` | Bold + Blue | Secondary headings |
| `{b}` | Bold + Yellow | Bold/highlighted text, fuzzy matches |
| `{/b}` | Reset bold only | End bold formatting |
| `{dim}` | Dim | Dimmed/secondary text |
| `{text}` | Reset | Normal text (full reset) |
| `{reset}` | Reset | Full reset of all formatting |
| `{/fg}` | Reset foreground | Reset foreground color only |
| `{section}` | Bold | Start of selected section |
| `{/section}` | Reset | End of selected section |

**ANSI Constants** (defined in `src/utils.h`):
- `ANSI_RESET` - "\033[0m"
- `ANSI_BOLD` - "\033[1m"
- `ANSI_DIM` - "\033[2m"
- `ANSI_RED`, `ANSI_GREEN`, `ANSI_YELLOW`, `ANSI_BLUE`, `ANSI_MAGENTA`, `ANSI_CYAN`, `ANSI_WHITE`

**Usage Pattern:**
```c
Z_CLEANUP(zstr_free) zstr message = zstr_from("Status: {b}OK{/b}");
Z_CLEANUP(zstr_free) zstr expanded = zstr_expand_tokens(zstr_cstr(&message));
printf("%s\n", zstr_cstr(&expanded));
```

**In Fuzzy Matching:**
The fuzzy matching system in `src/fuzzy.c` inserts `{b}` tokens around matched characters. These tokens are preserved through the rendering pipeline and expanded to ANSI codes when displayed:

```c
// Input: "2025-11-29-test", query: "te"
// Output: "2025-11-29-{b}te{/b}st"
// Displayed: "2025-11-29-[bold yellow]te[reset]st"
```

## Configuration

Constants defined in `src/config.h`:
- `TRY_VERSION`: Current version string
- `DEFAULT_TRIES_PATH_SUFFIX`: Default path relative to HOME (`src/tries`)

The tries path can be overridden with `--path` or `--path=` flag.

## Testing Workflow

No automated tests currently exist. Manual testing approach:

1. Build with `make`
2. Run `./try init` to get shell function definition
3. Source the output or manually test commands
4. Test interactive selector: `./try cd`
5. Test with filter: `./try cd <query>`
6. Test cloning: `./try clone <url> [name]`

Reference the Ruby implementation in `docs/try.reference.rb` for expected behavior.

## Common Patterns

**Creating a new zstr from string literal:**
```c
Z_CLEANUP(zstr_free) zstr s = zstr_from("text");
```

**Building paths:**
```c
Z_CLEANUP(zstr_free) zstr path = join_path("/base", "subdir");
```

**Working with vectors:**
```c
vec_TryEntry entries = {0};  // Zero-initialize
TryEntry entry = {/* ... */};
vec_push(&entries, entry);
vec_free_TryEntry(&entries);  // Free vector (not contents!)
```

**Emitting shell commands:**
```c
emit_task("cd", "/some/path", NULL);
printf("true\n");  // End command chain
```

**Token expansion for UI text:**
```c
Z_CLEANUP(zstr_free) zstr formatted = zstr_from("{dim}Path:{/fg} {b}");
zstr_cat(&formatted, some_path);
zstr_cat(&formatted, "{/b}");
Z_CLEANUP(zstr_free) zstr expanded = zstr_expand_tokens(zstr_cstr(&formatted));
// Use expanded for output
```

## String and Array Management

**Critical:** Always use `zstr` for strings and `zvec` for arrays. Never use raw C strings or arrays except:
- When interfacing with system APIs that require `char*` (use `zstr_cstr()` to convert)
- For small stack-allocated buffers in performance-critical paths

`zlist` is available for linked list use cases but is rarely needed.

## Dependencies

External dependencies are minimal:
- Standard C library (POSIX)
- Math library (`-lm` for fuzzy scoring)
- No external libraries beyond libc

The `libs/` directory contains bundled single-header libraries (zstr, zvec, zlist) that are self-contained.

## Directory Structure

- `src/` - C source and header files
- `libs/` - Bundled single-header libraries (z-libs)
- `docs/` - Reference implementation and documentation
- `obj/` - Build artifacts (created by make)
- `try` - Output binary

## Key Files

- `src/main.c` - Entry point, argument parsing
- `src/tui.c` - Interactive selector implementation
- `src/fuzzy.c` - Scoring and highlighting logic
- `src/utils.c` - Shared utilities, token expansion
- `src/commands.c` - Command implementations, shell emission
- `Makefile` - Build configuration
- `docs/try.reference.rb` - Ruby reference implementation (source of truth for features)
