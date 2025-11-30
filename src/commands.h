#ifndef COMMANDS_H
#define COMMANDS_H

#include "tui.h"

void cmd_init(int argc, char **argv);
void cmd_clone(int argc, char **argv, const char *tries_path);
void cmd_worktree(int argc, char **argv, const char *tries_path);
void cmd_cd(int argc, char **argv, const char *tries_path, TestMode *test_mode);

#endif // COMMANDS_H
