// Feature test macros for cross-platform compatibility
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#include <mach-o/dyld.h>
#else
#define _GNU_SOURCE
#endif

#include "commands.h"
#include "tui.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// ============================================================================
// Script building and execution
// ============================================================================

// Build a script and either execute it (direct mode) or print it (exec mode)
// Returns 0 on success, 1 on failure
static int run_script(const char *script, Mode *mode) {
  if (mode->type == MODE_EXEC) {
    // Exec mode: print script with header for alias to eval
    printf(SCRIPT_HEADER);
    printf("%s", script);
    return 0;
  } else {
    // Direct mode: execute via bash, then print cd hint if present
    // We run everything except cd (which can't work in subprocess)
    // Then print the cd command as a hint

    // Find the cd line (starts with "  cd '" on its own line - 2-space indent)
    const char *cd_line = NULL;
    const char *p = script;
    while (*p) {
      // Check if this line starts with "  cd '" (2-space indent)
      if (strncmp(p, "  cd '", 6) == 0) {
        cd_line = p;
        break;
      }
      // Skip to next line
      while (*p && *p != '\n') p++;
      if (*p == '\n') p++;
    }

    // Build script without cd line for execution
    Z_CLEANUP(zstr_free) zstr exec_script = zstr_init();
    if (cd_line && cd_line != script) {
      // Copy everything before the cd line, minus trailing " && \\\n"
      size_t len = cd_line - script;
      // Remove " && \\\n" from end (6 chars)
      if (len >= 6) len -= 6;
      zstr_cat_len(&exec_script, script, len);
    } else if (!cd_line) {
      // No cd, execute whole script
      zstr_cat(&exec_script, script);
    }
    // If cd is the only command, exec_script stays empty

    // Execute the non-cd part if any
    if (zstr_len(&exec_script) > 0) {
      // Remove trailing newlines/continuations
      while (zstr_len(&exec_script) > 0) {
        char last = zstr_cstr(&exec_script)[zstr_len(&exec_script) - 1];
        if (last == '\n' || last == '\\' || last == ' ') {
          zstr_pop_char(&exec_script);
        } else {
          break;
        }
      }

      Z_CLEANUP(zstr_free) zstr cmd = zstr_from("/usr/bin/env bash -c '");
      // Escape single quotes in script
      const char *s = zstr_cstr(&exec_script);
      while (*s) {
        if (*s == '\'') {
          zstr_cat(&cmd, "'\\''");
        } else {
          zstr_push(&cmd, *s);
        }
        s++;
      }
      zstr_cat(&cmd, "'");

      int rc = system(zstr_cstr(&cmd));
      if (rc != 0) {
        return 1;
      }
    }

    // Print cd hint
    if (cd_line) {
      const char *path_start = cd_line + 6; // Skip "  cd '"
      const char *path_end = strchr(path_start, '\'');
      if (path_end) {
        printf("cd '%.*s'\n", (int)(path_end - path_start), path_start);
      }
    }

    return 0;
  }
}

// Helper to generate date-prefixed directory name for clone
// URL format: https://github.com/user/repo.git -> 2025-11-30-user-repo
//             git@github.com:user/repo.git    -> 2025-11-30-user-repo
static zstr make_clone_dirname(const char *url, const char *name) {
  zstr dir_name = zstr_init();

  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  char date_prefix[20];
  strftime(date_prefix, sizeof(date_prefix), "%Y-%m-%d", t);
  zstr_cat(&dir_name, date_prefix);
  zstr_cat(&dir_name, "-");

  if (name) {
    zstr_cat(&dir_name, name);
  } else {
    // Extract user/repo from URL
    // Find the repo name (after last / or :)
    const char *last_slash = strrchr(url, '/');
    const char *last_colon = strrchr(url, ':');
    const char *repo_start = last_slash ? last_slash + 1 :
                             (last_colon ? last_colon + 1 : url);

    // Find the user name (between second-to-last separator and last separator)
    const char *user_start = NULL;
    const char *user_end = NULL;

    if (last_slash && last_slash > url) {
      // Walk back to find the previous / or :
      const char *p = last_slash - 1;
      while (p > url && *p != '/' && *p != ':') p--;
      if (*p == '/' || *p == ':') {
        user_start = p + 1;
        user_end = last_slash;
      }
    } else if (last_colon && last_colon > url) {
      // git@github.com:user/repo format - user is between : and /
      user_start = last_colon + 1;
      const char *slash_after = strchr(user_start, '/');
      if (slash_after) {
        user_end = slash_after;
      }
    }

    // Append user- if found
    if (user_start && user_end && user_end > user_start) {
      zstr_cat_len(&dir_name, user_start, user_end - user_start);
      zstr_cat(&dir_name, "-");
    }

    // Append repo name (strip .git suffix)
    const char *dot_git = strstr(repo_start, ".git");
    if (dot_git) {
      zstr_cat_len(&dir_name, repo_start, dot_git - repo_start);
    } else {
      zstr_cat(&dir_name, repo_start);
    }
  }

  return dir_name;
}

// ============================================================================
// Script builders
// ============================================================================

// Shell-safe quoting based on Python's shlex.quote(). Single quotes protect
// ALL characters in POSIX shells except ' itself, which we escape as '"'"'.
static zstr shell_escape(const char *str) {
  zstr result = zstr_init();

  // Opening quote
  zstr_push(&result, '\'');

  // Copy string, escaping single quotes
  for (const char *p = str; *p; p++) {
    if (*p == '\'') {
      zstr_cat(&result, "'\"'\"'");
    } else {
      zstr_push(&result, *p);
    }
  }

  // Closing quote
  zstr_push(&result, '\'');
  return result;
}

static zstr build_cd_script(const char *path) {
  zstr script = zstr_init();
  Z_CLEANUP(zstr_free) zstr escaped_path = shell_escape(path);
  zstr_fmt(&script, "touch %s && \\\n", zstr_cstr(&escaped_path));
  zstr_fmt(&script, "  cd %s\n", zstr_cstr(&escaped_path));
  return script;
}

static zstr build_mkdir_script(const char *path) {
  zstr script = zstr_init();
  Z_CLEANUP(zstr_free) zstr escaped_path = shell_escape(path);
  zstr_fmt(&script, "mkdir -p %s && \\\n", zstr_cstr(&escaped_path));
  zstr_fmt(&script, "  cd %s\n", zstr_cstr(&escaped_path));
  return script;
}

static zstr build_clone_script(const char *url, const char *path) {
  zstr script = zstr_init();
  Z_CLEANUP(zstr_free) zstr escaped_url = shell_escape(url);
  Z_CLEANUP(zstr_free) zstr escaped_path = shell_escape(path);
  zstr_fmt(&script, "git clone %s %s && \\\n", zstr_cstr(&escaped_url), zstr_cstr(&escaped_path));
  zstr_fmt(&script, "  cd %s\n", zstr_cstr(&escaped_path));
  return script;
}

static zstr build_delete_script(const char *base_path, char **names, size_t count) {
  zstr script = zstr_init();

  // Get current working directory for PWD restoration
  char cwd[1024];
  if (getcwd(cwd, sizeof(cwd)) == NULL) {
    cwd[0] = '\0';
  }

  // cd to base path first
  Z_CLEANUP(zstr_free) zstr escaped_base = shell_escape(base_path);
  zstr_fmt(&script, "cd %s && \\\n", zstr_cstr(&escaped_base));

  // Per-item delete commands
  for (size_t i = 0; i < count; i++) {
    Z_CLEANUP(zstr_free) zstr escaped_name = shell_escape(names[i]);
    zstr_fmt(&script, "  [[ -d %s ]] && rm -rf %s && \\\n",
             zstr_cstr(&escaped_name), zstr_cstr(&escaped_name));
  }

  // PWD restoration
  if (cwd[0] != '\0') {
    Z_CLEANUP(zstr_free) zstr escaped_cwd = shell_escape(cwd);
    zstr_fmt(&script, "  ( cd %s 2>/dev/null || cd \"$HOME\" )\n", zstr_cstr(&escaped_cwd));
  } else {
    zstr_cat(&script, "  cd \"$HOME\"\n");
  }

  return script;
}

// ============================================================================
// Init command - outputs shell function definition
// ============================================================================

void cmd_init(int argc, char **argv, const char *tries_path) {
  // If a path argument is provided, use it instead of the default
  if (argc > 0 && argv[0] != NULL) {
    tries_path = argv[0];
  }

  // Determine if we're in fish shell
  const char *shell = getenv("SHELL");
  bool is_fish = (shell && strstr(shell, "fish") != NULL);

  // Get the path to this executable using realpath for absolute path
  char exe_path[1024];
  char resolved_path[1024];
  bool got_path = false;

  // Try /proc/self/exe first (Linux)
  ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
  if (len != -1) {
    exe_path[len] = '\0';
    got_path = true;
  }
#ifdef __APPLE__
  else {
    // Try _NSGetExecutablePath on macOS
    uint32_t size = sizeof(exe_path);
    if (_NSGetExecutablePath(exe_path, &size) == 0) {
      got_path = true;
    }
  }
#endif

  // Resolve to absolute path, handling symlinks
  const char *self_path;
  if (got_path && realpath(exe_path, resolved_path) != NULL) {
    self_path = resolved_path;
  } else {
    // Fallback: use "command try" to bypass function shadowing
    self_path = "command try";
  }

  // Escape paths to prevent shell injection
  Z_CLEANUP(zstr_free) zstr escaped_self = shell_escape(self_path);
  Z_CLEANUP(zstr_free) zstr escaped_tries = shell_escape(tries_path);

  if (is_fish) {
    // Fish shell version
    printf(
      "function try\n"
      "  set -l out (%s exec --path %s $argv 2>/dev/tty)\n"
      "  or begin; echo $out; return $status; end\n"
      "  eval $out\n"
      "end\n",
      zstr_cstr(&escaped_self), zstr_cstr(&escaped_tries));
  } else {
    // Bash/Zsh version
    printf(
      "try() {\n"
      "  local out\n"
      "  out=$(%s exec --path %s \"$@\" 2>/dev/tty) || {\n"
      "    echo \"$out\"\n"
      "    return $?\n"
      "  }\n"
      "  eval \"$out\"\n"
      "}\n",
      zstr_cstr(&escaped_self), zstr_cstr(&escaped_tries));
  }
}

// ============================================================================
// Clone command
// ============================================================================

int cmd_clone(int argc, char **argv, const char *tries_path, Mode *mode) {
  if (argc < 1) {
    fprintf(stderr, "Usage: try clone <url> [name]\n");
    return 1;
  }

  const char *url = argv[0];
  const char *name = (argc > 1) ? argv[1] : NULL;

  Z_CLEANUP(zstr_free) zstr dir_name = make_clone_dirname(url, name);
  Z_CLEANUP(zstr_free) zstr full_path = join_path(tries_path, zstr_cstr(&dir_name));
  Z_CLEANUP(zstr_free) zstr script = build_clone_script(url, zstr_cstr(&full_path));

  return run_script(zstr_cstr(&script), mode);
}

// ============================================================================
// Worktree command
// ============================================================================

static zstr build_worktree_script(const char *worktree_path) {
  zstr script = zstr_init();
  Z_CLEANUP(zstr_free) zstr escaped_path = shell_escape(worktree_path);
  zstr_fmt(&script, "git worktree add %s && \\\n", zstr_cstr(&escaped_path));
  zstr_fmt(&script, "  cd %s\n", zstr_cstr(&escaped_path));
  return script;
}

static bool is_in_git_repo(void) {
  // Check if .git exists in current directory or any parent
  char cwd[1024];
  if (getcwd(cwd, sizeof(cwd)) == NULL) return false;

  char path[1024];
  strncpy(path, cwd, sizeof(path) - 1);
  path[sizeof(path) - 1] = '\0';

  while (strlen(path) > 0) {
    char git_path[1100];
    snprintf(git_path, sizeof(git_path), "%s/.git", path);
    if (access(git_path, F_OK) == 0) return true;

    // Go up one directory
    char *last_slash = strrchr(path, '/');
    if (last_slash == path) {
      // At root
      snprintf(git_path, sizeof(git_path), "/.git");
      return access(git_path, F_OK) == 0;
    }
    if (last_slash) *last_slash = '\0';
    else break;
  }
  return false;
}

int cmd_worktree(int argc, char **argv, const char *tries_path, Mode *mode) {
  if (argc < 1) {
    fprintf(stderr, "Usage: try worktree <name>\n");
    return 1;
  }

  const char *name = argv[0];

  // Build date-prefixed path
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  char date_prefix[20];
  strftime(date_prefix, sizeof(date_prefix), "%Y-%m-%d", t);

  Z_CLEANUP(zstr_free) zstr dir_name = zstr_from(date_prefix);
  zstr_cat(&dir_name, "-");
  zstr_cat(&dir_name, name);

  Z_CLEANUP(zstr_free) zstr full_path = join_path(tries_path, zstr_cstr(&dir_name));

  // Check if we're in a git repo
  if (is_in_git_repo()) {
    Z_CLEANUP(zstr_free) zstr script = build_worktree_script(zstr_cstr(&full_path));
    return run_script(zstr_cstr(&script), mode);
  } else {
    // Not in a git repo, just mkdir
    Z_CLEANUP(zstr_free) zstr script = build_mkdir_script(zstr_cstr(&full_path));
    return run_script(zstr_cstr(&script), mode);
  }
}

// ============================================================================
// Selector command (interactive directory picker)
// ============================================================================

int cmd_selector(int argc, char **argv, const char *tries_path, Mode *mode) {
  const char *initial_filter = (argc > 0) ? argv[0] : NULL;

  SelectionResult result = run_selector(tries_path, initial_filter, mode);

  if (result.type == ACTION_CD) {
    Z_CLEANUP(zstr_free) zstr script = build_cd_script(zstr_cstr(&result.path));
    zstr_free(&result.path);
    return run_script(zstr_cstr(&script), mode);
  } else if (result.type == ACTION_MKDIR) {
    Z_CLEANUP(zstr_free) zstr script = build_mkdir_script(zstr_cstr(&result.path));
    zstr_free(&result.path);
    return run_script(zstr_cstr(&script), mode);
  } else if (result.type == ACTION_DELETE) {
    Z_CLEANUP(zstr_free) zstr script = build_delete_script(tries_path, result.delete_names, result.delete_count);
    // Free the delete_names array
    for (size_t i = 0; i < result.delete_count; i++) {
      free(result.delete_names[i]);
    }
    free(result.delete_names);
    zstr_free(&result.path);
    return run_script(zstr_cstr(&script), mode);
  } else {
    // Cancelled
    zstr_free(&result.path);
    printf("Cancelled.\n");
    return 1;
  }
}

// ============================================================================
// Exec mode entry point
// ============================================================================

int cmd_exec(int argc, char **argv, const char *tries_path, Mode *mode) {
  // No subcommand = interactive selector
  if (argc == 0) {
    return cmd_selector(0, NULL, tries_path, mode);
  }

  const char *subcmd = argv[0];

  if (strcmp(subcmd, "cd") == 0) {
    // Check if argument is a URL (clone shorthand)
    if (argc > 1 && (strncmp(argv[1], "https://", 8) == 0 ||
                     strncmp(argv[1], "http://", 7) == 0 ||
                     strncmp(argv[1], "git@", 4) == 0)) {
      return cmd_clone(argc - 1, argv + 1, tries_path, mode);
    }
    // Explicit cd command
    return cmd_selector(argc - 1, argv + 1, tries_path, mode);
  } else if (strcmp(subcmd, "clone") == 0) {
    return cmd_clone(argc - 1, argv + 1, tries_path, mode);
  } else if (strcmp(subcmd, "worktree") == 0) {
    return cmd_worktree(argc - 1, argv + 1, tries_path, mode);
  } else if (strncmp(subcmd, "https://", 8) == 0 ||
             strncmp(subcmd, "http://", 7) == 0 ||
             strncmp(subcmd, "git@", 4) == 0) {
    // URL shorthand for clone
    return cmd_clone(argc, argv, tries_path, mode);
  } else if (strcmp(subcmd, ".") == 0) {
    // Dot shorthand for worktree (requires name)
    if (argc < 2) {
      fprintf(stderr, "Usage: try . <name>\n");
      fprintf(stderr, "The name argument is required for worktree creation.\n");
      return 1;
    }
    return cmd_worktree(argc - 1, argv + 1, tries_path, mode);
  } else {
    // Treat as query for selector (cd is default)
    return cmd_selector(argc, argv, tries_path, mode);
  }
}
