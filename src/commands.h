#ifndef COMMANDS_H
#define COMMANDS_H

#include "tui.h"

// Script header for exec mode output
#define SCRIPT_HEADER "# if you can read this, you didn't launch try from an alias. run try --help.\n"

// Init command - outputs shell function definition (always prints directly)
// tries_path: single-root mode (NULL in multi-env mode)
// base_path: multi-env base directory (NULL in single-root mode)
void cmd_init(int argc, char **argv, const char *tries_path, const char *base_path);

// Commands return shell scripts to execute
// Returns empty zstr on error (after printing error to stderr)
// base_path: if non-NULL, enables multi-env mode (env picker first)
zstr cmd_clone(int argc, char **argv, const char *tries_path, const char *base_path);
zstr cmd_worktree(int argc, char **argv, const char *tries_path, const char *base_path);
zstr cmd_selector(int argc, char **argv, const char *tries_path, TestParams *test, const char *base_path);

// Route subcommands (for exec mode)
zstr cmd_route(int argc, char **argv, const char *tries_path, TestParams *test, const char *base_path);

// Execute or print a script
// exec_mode: true = print with header, false = execute via bash
int run_script(const char *script, bool exec_mode);

#endif // COMMANDS_H
