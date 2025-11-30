#define _POSIX_C_SOURCE 200809L
#include "commands.h"
#include "tui.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// Helper to emit shell commands
void emit_task(const char *type, const char *arg1, const char *arg2) {
  if (strcmp(type, "mkdir") == 0) {
    printf("mkdir -p '%s' \\\n  && ", arg1);
  } else if (strcmp(type, "cd") == 0) {
    printf("cd '%s' \\\n  && ", arg1);
  } else if (strcmp(type, "touch") == 0) {
    printf("touch '%s' \\\n  && ", arg1);
  } else if (strcmp(type, "git-clone") == 0) {
    printf("git clone '%s' '%s' \\\n  && ", arg1, arg2);
  } else if (strcmp(type, "echo") == 0) {
    // Expand tokens for echo
    Z_CLEANUP(zstr_free) zstr expanded = zstr_expand_tokens(arg1);
    printf("echo '%s' \\\n  && ", zstr_cstr(&expanded));
  }
}

void cmd_init(int argc, char **argv) {
  (void)argc;
  (void)argv;
  // Simplified init script emission
  const char *script = "try() {\n"
                       "  if [ \"$1\" = \"init\" ]; then\n"
                       "    /usr/local/bin/try \"$@\"\n"
                       "    return\n"
                       "  fi\n"
                       "  tmp=$(mktemp)\n"
                       "  /usr/local/bin/try \"$@\" > \"$tmp\"\n"
                       "  ret=$?\n"
                       "  if [ $ret -eq 0 ]; then\n"
                       "    . \"$tmp\"\n"
                       "  fi\n"
                       "  rm -f \"$tmp\"\n"
                       "  return $ret\n"
                       "}\n";
  printf("%s", script);
}

void cmd_clone(int argc, char **argv, const char *tries_path) {
  if (argc < 1) {
    fprintf(stderr, "Usage: try clone <url> [name]\n");
    exit(1);
  }

  char *url = argv[0];
  char *name = (argc > 1) ? argv[1] : NULL;

  // Generate name if not provided
  Z_CLEANUP(zstr_free) zstr dir_name = zstr_init();

  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  char date_prefix[20];
  strftime(date_prefix, sizeof(date_prefix), "%Y-%m-%d", t);
  zstr_cat(&dir_name, date_prefix);
  zstr_cat(&dir_name, "-");

  if (name) {
    zstr_cat(&dir_name, name);
  } else {
    // Extract repo name from URL
    char *last_slash = strrchr(url, '/');
    char *repo_name = last_slash ? last_slash + 1 : url;
    char *dot_git = strstr(repo_name, ".git");

    if (dot_git) {
      zstr_cat_len(&dir_name, repo_name, dot_git - repo_name);
    } else {
      zstr_cat(&dir_name, repo_name);
    }
  }

  Z_CLEANUP(zstr_free)
  zstr full_path = join_path(tries_path, zstr_cstr(&dir_name));

  emit_task("echo", "Cloning into {b}new try{/b}...", NULL);
  emit_task("mkdir", zstr_cstr(&full_path), NULL);
  emit_task("git-clone", url, zstr_cstr(&full_path));
  emit_task("touch", zstr_cstr(&full_path), NULL); // Update mtime
  emit_task("cd", zstr_cstr(&full_path), NULL);
  printf("true\n"); // End chain
}

void cmd_worktree(int argc, char **argv, const char *tries_path) {
  (void)argc;
  (void)argv;
  (void)tries_path;
  // Simplified worktree implementation
  // try worktree [dir] [name]

  // For now, just a placeholder or basic implementation
  fprintf(stderr, "Worktree command not fully implemented in this MVP.\n");
  exit(1);
}

void cmd_cd(int argc, char **argv, const char *tries_path, TestMode *test_mode) {
  // If args provided, try to find match or use as filter
  char *initial_filter = NULL;
  if (argc > 0) {
    // Join args
    // For simplicity, just take first arg
    initial_filter = argv[0];
  }

  SelectionResult result = run_selector(tries_path, initial_filter, test_mode);

  if (result.type == ACTION_CD) {
    emit_task("touch", zstr_cstr(&result.path), NULL); // Update mtime
    emit_task("cd", zstr_cstr(&result.path), NULL);
    printf("true\n");
  } else if (result.type == ACTION_MKDIR) {
    emit_task("mkdir", zstr_cstr(&result.path), NULL);
    emit_task("cd", zstr_cstr(&result.path), NULL);
    printf("true\n");
  } else {
    // Cancelled
    printf("true\n");
  }

  zstr_free(&result.path);
}
