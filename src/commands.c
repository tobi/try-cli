// Feature test macros for cross-platform compatibility
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#include <mach-o/dyld.h>
#else
#define _GNU_SOURCE
#endif

#include "commands.h"
#include "config.h"
#include "tui.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// ============================================================================
// Script execution
// ============================================================================

int run_script(const char *script, bool exec_mode) {
  if (!script || !*script) {
    return 1;
  }

  if (exec_mode) {
    // Exec mode: print script with header for alias to eval
    printf(SCRIPT_HEADER);
    printf("%s", script);
    return 0;
  }

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

  // Print cd hint and path
  if (cd_line) {
    const char *path_start = cd_line + 6; // Skip "  cd '"
    const char *path_end = strchr(path_start, '\'');
    if (path_end) {
      printf("cd '%.*s'\n", (int)(path_end - path_start), path_start);
      printf("%.*s\n", (int)(path_end - path_start), path_start);
    }
  }

  return 0;
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
  zstr_fmt(&script, "  cd %s && \\\n", zstr_cstr(&escaped_path));
  zstr_fmt(&script, "  printf '%%s\\n' %s\n", zstr_cstr(&escaped_path));
  return script;
}

static zstr build_mkdir_script(const char *path) {
  zstr script = zstr_init();
  Z_CLEANUP(zstr_free) zstr escaped_path = shell_escape(path);
  zstr_fmt(&script, "mkdir -p %s && \\\n", zstr_cstr(&escaped_path));
  zstr_fmt(&script, "  cd %s && \\\n", zstr_cstr(&escaped_path));
  zstr_fmt(&script, "  printf '%%s\\n' %s\n", zstr_cstr(&escaped_path));
  return script;
}

static zstr build_clone_script(const char *url, const char *path) {
  zstr script = zstr_init();
  Z_CLEANUP(zstr_free) zstr escaped_url = shell_escape(url);
  Z_CLEANUP(zstr_free) zstr escaped_path = shell_escape(path);
  zstr_fmt(&script, "git clone %s %s && \\\n", zstr_cstr(&escaped_url), zstr_cstr(&escaped_path));
  zstr_fmt(&script, "  cd %s && \\\n", zstr_cstr(&escaped_path));
  zstr_fmt(&script, "  printf '%%s\\n' %s\n", zstr_cstr(&escaped_path));
  return script;
}

static zstr build_worktree_script(const char *worktree_path) {
  zstr script = zstr_init();
  Z_CLEANUP(zstr_free) zstr escaped_path = shell_escape(worktree_path);
  zstr_fmt(&script, "git worktree add %s && \\\n", zstr_cstr(&escaped_path));
  zstr_fmt(&script, "  cd %s && \\\n", zstr_cstr(&escaped_path));
  zstr_fmt(&script, "  printf '%%s\\n' %s\n", zstr_cstr(&escaped_path));
  return script;
}

static zstr build_delete_script(const char *base_path, vec_zstr *names) {
  zstr script = zstr_init();

  // Security: verify no names contain path separators
  zstr *check;
  vec_foreach(names, check) {
    if (strchr(zstr_cstr(check), '/') != NULL) {
      return script;  // Return empty script
    }
  }

  // Get current working directory for PWD restoration
  char cwd[1024];
  if (getcwd(cwd, sizeof(cwd)) == NULL) {
    cwd[0] = '\0';
  }

  // cd to base path first
  Z_CLEANUP(zstr_free) zstr escaped_base = shell_escape(base_path);
  zstr_fmt(&script, "cd %s && \\\n", zstr_cstr(&escaped_base));

  // Per-item delete commands
  zstr *iter;
  vec_foreach(names, iter) {
    Z_CLEANUP(zstr_free) zstr escaped_name = shell_escape(zstr_cstr(iter));
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

static zstr build_rename_script(const char *base_path, const char *old_name, const char *new_name) {
  zstr script = zstr_init();

  // Security: names must not contain path separators
  if (strchr(old_name, '/') != NULL || strchr(new_name, '/') != NULL) {
    return script;  // Return empty script
  }

  Z_CLEANUP(zstr_free) zstr escaped_base = shell_escape(base_path);
  Z_CLEANUP(zstr_free) zstr escaped_old = shell_escape(old_name);
  Z_CLEANUP(zstr_free) zstr escaped_new = shell_escape(new_name);

  // cd to base path, rename, then cd to renamed directory
  zstr_fmt(&script, "cd %s && \\\n", zstr_cstr(&escaped_base));
  zstr_fmt(&script, "mv %s %s && \\\n", zstr_cstr(&escaped_old), zstr_cstr(&escaped_new));

  // Build path to new directory
  Z_CLEANUP(zstr_free) zstr new_path = join_path(base_path, new_name);
  Z_CLEANUP(zstr_free) zstr escaped_new_path = shell_escape(zstr_cstr(&new_path));
  zstr_fmt(&script, "  cd %s && \\\n", zstr_cstr(&escaped_new_path));
  zstr_fmt(&script, "  printf '%%s\\n' %s\n", zstr_cstr(&escaped_new_path));

  return script;
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

static bool is_in_git_repo(void) {
  // Check if .git exists in current directory or any parent
  char cwd[4096];
  if (getcwd(cwd, sizeof(cwd)) == NULL) return false;

  Z_CLEANUP(zstr_free) zstr path = zstr_from(cwd);

  while (zstr_len(&path) > 0) {
    Z_CLEANUP(zstr_free) zstr git_path = zstr_dup(&path);
    zstr_cat(&git_path, "/.git");

    if (access(zstr_cstr(&git_path), F_OK) == 0) return true;

    // Go up one directory
    char *p = zstr_data(&path);
    char *last_slash = strrchr(p, '/');

    if (last_slash == p) {
      // At root
      return access("/.git", F_OK) == 0;
    }

    if (last_slash) {
        *last_slash = '\0';
        // Update length
        if (path.is_long) path.l.len = last_slash - p;
        else path.s.len = last_slash - p;
    } else {
        break;
    }
  }
  return false;
}

// ============================================================================
// Init command - outputs shell function definition
// ============================================================================

void cmd_init(int argc, char **argv, const char *tries_path) {
  (void)argc; (void)argv; // May be used for future options

  // If a positional argument is provided, use it as the tries path
  // e.g., "try init /tmp/custom-path"
  if (argc > 0 && argv[0] && argv[0][0] != '-') {
    tries_path = argv[0];
  }

  // Determine if we're in fish shell
  const char *shell = getenv("SHELL");
  bool is_fish = (shell && strstr(shell, "fish") != NULL);

  // Get the path to this executable using realpath for absolute path
  char exe_path[1024];
  char *resolved_path = NULL;
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
  if (got_path) {
    resolved_path = realpath(exe_path, NULL);
  }

  if (resolved_path) {
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

  if (resolved_path) {
    free(resolved_path);
  }
}

// ============================================================================
// Clone command - returns script
// ============================================================================

zstr cmd_clone(int argc, char **argv, const char *tries_path) {
  if (argc < 1) {
    fprintf(stderr, "Usage: try clone <url> [name]\n");
    return zstr_init(); // Empty = error
  }

  const char *url = argv[0];
  const char *name = (argc > 1) ? argv[1] : NULL;

  Z_CLEANUP(zstr_free) zstr dir_name = make_clone_dirname(url, name);
  Z_CLEANUP(zstr_free) zstr full_path = join_path(tries_path, zstr_cstr(&dir_name));

  return build_clone_script(url, zstr_cstr(&full_path));
}

// ============================================================================
// Worktree command - returns script
// ============================================================================

zstr cmd_worktree(int argc, char **argv, const char *tries_path) {
  if (argc < 1) {
    fprintf(stderr, "Usage: try worktree <name>\n");
    return zstr_init(); // Empty = error
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
    return build_worktree_script(zstr_cstr(&full_path));
  } else {
    // Not in a git repo, just mkdir
    return build_mkdir_script(zstr_cstr(&full_path));
  }
}

// ============================================================================
// Selector command - returns script
// ============================================================================

zstr cmd_selector(int argc, char **argv, const char *tries_path, TestParams *test) {
  const char *initial_filter = (argc > 0) ? argv[0] : NULL;

  SelectionResult result = run_selector(tries_path, initial_filter, test);

  zstr script = zstr_init();

  if (result.type == ACTION_CD) {
    script = build_cd_script(zstr_cstr(&result.path));
  } else if (result.type == ACTION_MKDIR) {
    script = build_mkdir_script(zstr_cstr(&result.path));
  } else if (result.type == ACTION_DELETE) {
    script = build_delete_script(tries_path, &result.delete_names);
    // Free the delete_names vector
    zstr *iter;
    vec_foreach(&result.delete_names, iter) {
      zstr_free(iter);
    }
    vec_free_zstr(&result.delete_names);
  } else if (result.type == ACTION_RENAME) {
    script = build_rename_script(tries_path,
                                  zstr_cstr(&result.rename_old_name),
                                  zstr_cstr(&result.rename_new_name));
    zstr_free(&result.rename_old_name);
    zstr_free(&result.rename_new_name);
  } else {
    // Cancelled - return empty script but print message
    fprintf(stderr, "Cancelled.\n");
  }

  zstr_free(&result.path);
  return script;
}

// ============================================================================
// Route subcommands (for exec mode or main routing)
// ============================================================================

zstr cmd_route(int argc, char **argv, const char *tries_path, TestParams *test) {
  // No subcommand = interactive selector
  if (argc == 0) {
    return cmd_selector(0, NULL, tries_path, test);
  }

  const char *subcmd = argv[0];

  // Handle flags that may be passed through shell wrapper
  if (strcmp(subcmd, "--version") == 0 || strcmp(subcmd, "-v") == 0) {
    printf("try %s\n", TRY_VERSION);
    return zstr_init(); // Empty but not an error
  }
  if (strcmp(subcmd, "--help") == 0 || strcmp(subcmd, "-h") == 0) {
    // Help handled by main.c
    return zstr_init();
  }
  if (strcmp(subcmd, "--no-colors") == 0) {
    extern bool tui_no_colors;
    tui_no_colors = true;
    // Continue with remaining args
    return cmd_route(argc - 1, argv + 1, tries_path, test);
  }

  if (strcmp(subcmd, "init") == 0) {
    // Init always prints directly
    cmd_init(argc - 1, argv + 1, tries_path);
    return zstr_init();
  } else if (strcmp(subcmd, "cd") == 0) {
    // Check if argument is a URL (clone shorthand)
    if (argc > 1 && (strncmp(argv[1], "https://", 8) == 0 ||
                     strncmp(argv[1], "http://", 7) == 0 ||
                     strncmp(argv[1], "git@", 4) == 0)) {
      return cmd_clone(argc - 1, argv + 1, tries_path);
    }
    // Explicit cd command
    return cmd_selector(argc - 1, argv + 1, tries_path, test);
  } else if (strcmp(subcmd, "clone") == 0) {
    return cmd_clone(argc - 1, argv + 1, tries_path);
  } else if (strcmp(subcmd, "worktree") == 0) {
    return cmd_worktree(argc - 1, argv + 1, tries_path);
  } else if (strncmp(subcmd, "https://", 8) == 0 ||
             strncmp(subcmd, "http://", 7) == 0 ||
             strncmp(subcmd, "git@", 4) == 0) {
    // URL shorthand for clone
    return cmd_clone(argc, argv, tries_path);
  } else if (strcmp(subcmd, ".") == 0) {
    // Dot shorthand for worktree (requires name)
    if (argc < 2) {
      fprintf(stderr, "Usage: try . <name>\n");
      fprintf(stderr, "The name argument is required for worktree creation.\n");
      return zstr_init();
    }
    return cmd_worktree(argc - 1, argv + 1, tries_path);
  } else {
    // Treat as query for selector (cd is default)
    return cmd_selector(argc, argv, tries_path, test);
  }
}
