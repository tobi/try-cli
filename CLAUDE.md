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

The binary is built to `dist/try` from C source files in `src/`. Object files are placed in `obj/`.

## Architecture

### Shell Integration Pattern

The tool uses a unique architecture where it emits shell commands that are sourced by the parent shell:

1. User runs `try` or similar command
2. The C binary executes and builds a shell script
3. In exec mode: script is printed to stdout for the shell wrapper to eval
4. In direct mode: script is executed via `system()`, with cd commands printed as hints
5. This allows the tool to change the current directory (impossible for normal subprocesses)

Commands return shell scripts as `zstr` strings. The `run_script()` function either prints (exec mode) or executes (direct mode) the script.

### Core Components

**Command Layer** (`src/commands.c`, `src/commands.h`):
- `cmd_init()`: Prints shell function definition for integration
- `cmd_clone()`: Returns shell script to clone repo into dated directory
- `cmd_selector()`: Launches interactive selector, returns cd/mkdir script
- `cmd_worktree()`: Returns shell script to create git worktree
- `cmd_route()`: Routes subcommands for exec mode
- `run_script()`: Executes or prints a shell script

**Interactive TUI** (`src/tui.c`, `src/tui.h`):
- `run_selector()`: Main interactive loop using raw terminal mode
- Scans directories in tries path (default: `~/src/tries`)
- Returns `SelectionResult` with action type and path
- Supports fuzzy filtering and keyboard navigation

**TUI Styling** (`src/tui_style.c`, `src/tui_style.h`):
- ANSI escape code constants and semantic style aliases
- `TuiStyleString`: Stack-based style management for proper nesting
- `tui_push()`/`tui_pop()`: Push/pop styles with automatic targeted resets
- `tui_print()`/`tui_printf()`: Styled text output
- Screen API for full-screen TUI rendering

**Fuzzy Matching** (`src/fuzzy.c`, `src/fuzzy.h`):
- `fuzzy_match()`: Updates TryEntry score and rendered output in-place
- `calculate_score()`: Combines fuzzy match score with recency (mtime)
- `highlight_matches()`: Wraps matched characters with ANSI highlight codes
- Algorithm favors consecutive character matches and recent access times
- **Documentation**: See `spec/fuzzy_matching.md` for complete algorithm specification

**Terminal Management** (`src/terminal.c`, `src/terminal.h`):
- Raw mode terminal control (disables canonical mode, echo)
- Escape sequence parsing for arrow keys, special keys
- Window size detection
- Cursor visibility control

**Utilities** (`src/utils.c`, `src/utils.h`):
- Path utilities: `join_path()`, `mkdir_p()`, directory existence checks
- Time formatting: `format_relative_time()` for human-readable timestamps
- `AUTO_FREE` macro: Cleanup helper for raw pointers using `Z_CLEANUP()`

**Data Structures** (`src/libs/zstr.h`, `src/libs/zvec.h`, `src/libs/zlist.h`):
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

1. User invokes `try [query]`
2. `main()` parses `--path` flag or uses default tries directory
3. `cmd_selector()` calls `run_selector()` with optional initial filter
4. `run_selector()` scans directories with `scan_tries()`
5. Interactive loop: user types to filter, arrows to navigate
6. On Enter: returns `SelectionResult` with action and path
7. `cmd_selector()` builds shell script for the action
8. `run_script()` executes or prints the script

## Configuration

Constants defined in `src/config.h`:
- `TRY_VERSION`: Current version string
- `DEFAULT_TRIES_PATH_SUFFIX`: Default path relative to HOME (`src/tries`)

The tries path can be overridden with `--path` or `--path=` flag.

## Testing Workflow

Automated tests are available via `make test`. Manual testing approach:

1. Build with `make`
2. Run `./dist/try init` to get shell function definition
3. Source the output or manually test commands
4. Test interactive selector: `./dist/try cd`
5. Test with filter: `./dist/try cd <query>`
6. Test cloning: `./dist/try clone <url> [name]`

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

**Styled text output:**
```c
// Simple styled text to zstr
tui_zstr_printf(&output, TUI_BOLD, "Hello");
zstr_cat(&output, " world\n");

// Stack-based styling for complex output
TuiStyleString ss = tui_start_zstr(&buf);
tui_push(&ss, TUI_SELECTED);  // Push background
tui_print(&ss, TUI_BOLD, "Name");  // Styled text (auto-resets, keeps bg)
tui_pop(&ss);  // Restore previous style
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

The `src/libs/` directory contains bundled single-header libraries (zstr, zvec, zlist) that are self-contained.

## Directory Structure

- `src/` - C source and header files
- `src/libs/` - Bundled single-header libraries (z-libs: zstr, zvec, zlist)
- `spec/` - Try specifications (CLI structure, fuzzy matching, TUI)
- `docs/` - Reference implementation and z-libs documentation
- `test/` - Test suite (test.sh)
- `obj/` - Object files (created by make, gitignored)
- `dist/` - Build output directory (created by make, gitignored)
- `dist/try` - Output binary
- `.github/workflows/` - CI/CD configuration

## Key Files

- `src/main.c` - Entry point, argument parsing
- `src/tui.c` - Interactive selector implementation
- `src/tui_style.c` - TUI styling and screen API
- `src/fuzzy.c` - Scoring and highlighting logic
- `src/utils.c` - Shared utilities
- `src/commands.c` - Command implementations, script building
- `Makefile` - Build configuration
- `docs/try.reference.rb` - Ruby reference implementation (source of truth for features)

## Documentation Maintenance

**IMPORTANT**: When making changes to certain subsystems, their corresponding documentation files must be updated:

- **Fuzzy matching algorithm** (`src/fuzzy.c`, `fuzzy_match()`):
  - Update `spec/fuzzy_matching.md` with algorithm changes
  - Update scoring examples if formulas change
  - Document any new bonuses, multipliers, or scoring components

These documentation files serve as specifications and must remain synchronized with the implementation.

## Release Process

The `VERSION` file is the single source of truth for version numbers. It is read by:
- **Makefile**: Passes `-DTRY_VERSION` to the compiler
- **flake.nix**: Uses `builtins.readFile ./VERSION`
- **PKGBUILD/.SRCINFO**: Updated via `make update-pkg`

### Release Steps

1. Update the `VERSION` file with the new version number
2. Run `make update-pkg` to sync PKGBUILD and .SRCINFO
3. Commit with excellent release notes in the commit message body:
   - Summarize all significant changes since the last release
   - Group changes by category (Features, Bug Fixes, Improvements)
   - Credit contributors where applicable
   - Be concise but comprehensive
4. Create and push tag: `git tag -a vX.Y.Z -m "Release vX.Y.Z" && git push origin master && git push origin vX.Y.Z`
5. GitHub Actions will automatically:
   - Build binaries for all platforms (Linux x86_64/aarch64, macOS x86_64/aarch64)
   - Create a GitHub release with binaries attached
   - Use commit messages to generate release notes

### Versioning Scheme

Follow semantic versioning (semver):
- **Major (X.0.0)**: Breaking changes to CLI interface or behavior
- **Minor (0.X.0)**: New features, non-breaking changes
- **Patch (0.0.X)**: Bug fixes, documentation updates
