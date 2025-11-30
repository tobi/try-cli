#define _POSIX_C_SOURCE 200809L
#include "commands.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void print_help() {
  printf("Usage: try [command] [args]\n");
  printf("Commands:\n");
  printf("  init             Initialize shell integration\n");
  printf("  clone <url>      Clone a repo into a new try\n");
  printf("  worktree         Create a worktree (not implemented)\n");
  printf("  cd [query]       Interactive selector or cd to query\n");
  printf("  [query]          Shorthand for cd [query]\n");
}

int main(int argc, char **argv) {
  Z_CLEANUP(zstr_free) zstr tries_path = zstr_init();
  AUTO_FREE char **cmd_argv = NULL;
  int cmd_argc = 0;

  // Test mode flags (undocumented, for testing)
  TestMode test_mode = {0};
  const char *test_keys = NULL;

  // Simple arg parsing to find --path and test flags
  // We'll reconstruct argv for the command
  cmd_argv = malloc(sizeof(char *) * argc);

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--path") == 0 && i + 1 < argc) {
      zstr_free(&tries_path); // Free previous if any
      tries_path = zstr_from(argv[i + 1]);
      i++;
    } else if (strncmp(argv[i], "--path=", 7) == 0) {
      zstr_free(&tries_path);
      tries_path = zstr_from(argv[i] + 7);
    } else if (strcmp(argv[i], "--and-exit") == 0) {
      test_mode.test_mode = true;
      test_mode.render_once = true;
    } else if (strcmp(argv[i], "--and-keys") == 0 && i + 1 < argc) {
      test_mode.test_mode = true;
      test_keys = argv[i + 1];
      test_mode.inject_keys = test_keys;
      i++;
    } else if (strncmp(argv[i], "--and-keys=", 11) == 0) {
      test_mode.test_mode = true;
      test_keys = argv[i] + 11;
      test_mode.inject_keys = test_keys;
    } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      print_help();
      return 0;
    } else {
      cmd_argv[cmd_argc++] = argv[i];
    }
  }

  if (zstr_is_empty(&tries_path)) {
    tries_path = get_default_tries_path();
  }

  if (zstr_is_empty(&tries_path)) {
    fprintf(stderr,
            "Error: Could not determine tries path. Set HOME or use --path.\n");
    return 1;
  }

  // Ensure tries directory exists
  if (!dir_exists(zstr_cstr(&tries_path))) {
    if (mkdir_p(zstr_cstr(&tries_path)) != 0) {
      fprintf(stderr, "Error: Could not create tries directory: %s\n",
              zstr_cstr(&tries_path));
      return 1;
    }
  }

  char *command = (cmd_argc > 0) ? cmd_argv[0] : "cd";
  const char *path_cstr = zstr_cstr(&tries_path);

  TestMode *test_mode_ptr = test_mode.test_mode ? &test_mode : NULL;

  if (strcmp(command, "init") == 0) {
    cmd_init(cmd_argc - 1, cmd_argv + 1);
  } else if (strcmp(command, "clone") == 0) {
    cmd_clone(cmd_argc - 1, cmd_argv + 1, path_cstr);
  } else if (strcmp(command, "worktree") == 0) {
    cmd_worktree(cmd_argc - 1, cmd_argv + 1, path_cstr);
  } else if (strcmp(command, "cd") == 0) {
    cmd_cd(cmd_argc - 1, cmd_argv + 1, path_cstr, test_mode_ptr);
  } else {
    // Treat as "cd query" if not a known command
    cmd_cd(cmd_argc, cmd_argv, path_cstr, test_mode_ptr);
  }

  return 0;
}
