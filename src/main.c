// Feature test macros for cross-platform compatibility
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#else
#define _GNU_SOURCE
#endif

#include "commands.h"
#include "config.h"
#include "utils.h"
#include "tui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Global flag for disabling colors (shared with other modules)
bool tui_no_colors = false;

// Compact help for direct mode
static void print_help(void) {
  Z_CLEANUP(zstr_free) zstr default_path = get_default_tries_path();

  Z_CLEANUP(zstr_free) zstr help = zstr_init();

  // Title
  tui_zstr_printf(&help, TUI_H1, "try");
  zstr_cat(&help, " v" TRY_VERSION " - ephemeral workspace manager\n\n");

  // Setup section
  tui_zstr_printf(&help, TUI_H1, "To use try, add to your shell config:");
  zstr_cat(&help, "\n\n");

  zstr_cat(&help, "  ");
  tui_zstr_printf(&help, ANSI_BRIGHT_BLUE, "# bash/zsh (~/.bashrc or ~/.zshrc)");
  zstr_cat(&help, "\n");
  zstr_cat(&help, "  eval \"$(try init ~/src/tries)\"\n\n");

  zstr_cat(&help, "  ");
  tui_zstr_printf(&help, ANSI_BRIGHT_BLUE, "# fish (~/.config/fish/config.fish)");
  zstr_cat(&help, "\n");
  zstr_cat(&help, "  eval (try init ~/src/tries | string collect)\n\n");

  // Commands section
  tui_zstr_printf(&help, TUI_H1, "Commands:");
  zstr_cat(&help, "\n");

  zstr_cat(&help, "  ");
  tui_zstr_printf(&help, TUI_BOLD, "try");
  zstr_cat(&help, " [query|url]      ");
  tui_zstr_printf(&help, TUI_DIM, "Interactive selector, or clone if URL");
  zstr_cat(&help, "\n");

  zstr_cat(&help, "  ");
  tui_zstr_printf(&help, TUI_BOLD, "try clone");
  zstr_cat(&help, " <url>      ");
  tui_zstr_printf(&help, TUI_DIM, "Clone repo into dated directory");
  zstr_cat(&help, "\n");

  zstr_cat(&help, "  ");
  tui_zstr_printf(&help, TUI_BOLD, "try worktree");
  zstr_cat(&help, " <name>  ");
  tui_zstr_printf(&help, TUI_DIM, "Create worktree from current git repo");
  zstr_cat(&help, "\n");

  zstr_cat(&help, "  ");
  tui_zstr_printf(&help, TUI_BOLD, "try exec");
  zstr_cat(&help, " [query]     ");
  tui_zstr_printf(&help, TUI_DIM, "Output shell script (for manual eval)");
  zstr_cat(&help, "\n");

  zstr_cat(&help, "  ");
  tui_zstr_printf(&help, TUI_BOLD, "try --help");
  zstr_cat(&help, "           ");
  tui_zstr_printf(&help, TUI_DIM, "Show this help");
  zstr_cat(&help, "\n\n");

  // Defaults section
  tui_zstr_printf(&help, TUI_H1, "Defaults:");
  zstr_cat(&help, "\n");

  zstr_cat(&help, "  Path: ");
  tui_zstr_printf(&help, TUI_BOLD, "~/src/tries");
  zstr_cat(&help, " (override with ");
  tui_zstr_printf(&help, TUI_BOLD, "--path");
  zstr_cat(&help, " on init)\n");

  zstr_cat(&help, "  Current: ");
  tui_zstr_printf(&help, TUI_BOLD, zstr_cstr(&default_path));
  zstr_cat(&help, "\n\n");

  // Examples section
  tui_zstr_printf(&help, TUI_H1, "Examples:");
  zstr_cat(&help, "\n");

  zstr_cat(&help, "  try clone https://github.com/user/repo.git       ");
  tui_zstr_printf(&help, ANSI_BRIGHT_BLUE, "# YYYY-MM-DD-user-repo");
  zstr_cat(&help, "\n");

  zstr_cat(&help, "  try clone https://github.com/user/repo.git foo   ");
  tui_zstr_printf(&help, ANSI_BRIGHT_BLUE, "# YYYY-MM-DD-foo");
  zstr_cat(&help, "\n");

  zstr_cat(&help, "  try https://github.com/user/repo.git             ");
  tui_zstr_printf(&help, ANSI_BRIGHT_BLUE, "# shorthand for clone");
  zstr_cat(&help, "\n");

  zstr_cat(&help, "  try ./my-project worktree feature                ");
  tui_zstr_printf(&help, ANSI_BRIGHT_BLUE, "# YYYY-MM-DD-feature");
  zstr_cat(&help, "\n");

  fprintf(stderr, "%s", zstr_cstr(&help));
}

// Parse a --flag=value or --flag value option, returns value or NULL
// Sets *skip to 1 if value was in next arg (so caller can skip it)
static const char *parse_option_value(const char *arg, const char *next_arg,
                                       const char *flag, int *skip) {
  size_t flag_len = strlen(flag);
  *skip = 0;

  // Check --flag=value form
  if (strncmp(arg, flag, flag_len) == 0 && arg[flag_len] == '=') {
    return arg + flag_len + 1;
  }

  // Check --flag value form
  if (strcmp(arg, flag) == 0 && next_arg != NULL) {
    *skip = 1;
    return next_arg;
  }

  return NULL;
}

int main(int argc, char **argv) {
  Z_CLEANUP(zstr_free) zstr tries_path = zstr_init();
  Z_CLEANUP(vec_free_char_ptr) vec_char_ptr cmd_args = vec_init_capacity_char_ptr(argc);

  // Check NO_COLOR environment variable (https://no-color.org/)
  if (getenv("NO_COLOR") != NULL) {
    tui_no_colors = true;
  }

  // Mode configuration
  Mode mode = {.type = MODE_DIRECT};

  // Parse arguments - options can appear anywhere
  for (int i = 1; i < argc; i++) {
    const char *arg = argv[i];
    const char *next = (i + 1 < argc) ? argv[i + 1] : NULL;
    const char *value;
    int skip = 0;

    // Boolean flags
    if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
      print_help();
      return 0;
    }
    if (strcmp(arg, "--version") == 0 || strcmp(arg, "-v") == 0) {
      printf("try %s\n", TRY_VERSION);
      return 0;
    }
    if (strcmp(arg, "--no-colors") == 0) {
      tui_no_colors = true;
      continue;
    }
    if (strcmp(arg, "--and-exit") == 0) {
      mode.render_once = true;
      continue;
    }

    // Options with values
    if ((value = parse_option_value(arg, next, "--path", &skip))) {
      zstr_free(&tries_path);
      tries_path = zstr_from(value);
      i += skip;
      continue;
    }
    if ((value = parse_option_value(arg, next, "--and-keys", &skip))) {
      mode.inject_keys = value;
      i += skip;
      continue;
    }

    // Positional argument
    vec_push_char_ptr(&cmd_args, argv[i]);
  }

  // Default tries path
  if (zstr_is_empty(&tries_path)) {
    tries_path = get_default_tries_path();
  }

  if (zstr_is_empty(&tries_path)) {
    fprintf(stderr, "Error: Could not determine tries path. Set HOME or use --path.\n");
    return 1;
  }

  const char *path_cstr = zstr_cstr(&tries_path);

  // Ensure tries directory exists
  if (!dir_exists(path_cstr)) {
    if (mkdir_p(path_cstr) != 0) {
      fprintf(stderr, "Error: Could not create tries directory: %s\n", path_cstr);
      return 1;
    }
  }

  // No command = show help (direct mode)
  if (cmd_args.length == 0) {
    print_help();
    return 0;
  }

  const char *command = *vec_at_char_ptr(&cmd_args, 0);

  // Route commands
  if (strcmp(command, "init") == 0) {
    cmd_init((int)cmd_args.length - 1, cmd_args.data + 1, path_cstr);
    return 0;
  } else if (strcmp(command, "exec") == 0) {
    // Exec mode
    mode.type = MODE_EXEC;
    return cmd_exec((int)cmd_args.length - 1, cmd_args.data + 1, path_cstr, &mode);
  } else if (strcmp(command, "cd") == 0) {
    // Direct mode cd (interactive selector)
    return cmd_selector((int)cmd_args.length - 1, cmd_args.data + 1, path_cstr, &mode);
  } else if (strcmp(command, "clone") == 0) {
    // Direct mode clone
    return cmd_clone((int)cmd_args.length - 1, cmd_args.data + 1, path_cstr, &mode);
  } else if (strcmp(command, "worktree") == 0) {
    // Direct mode worktree
    return cmd_worktree((int)cmd_args.length - 1, cmd_args.data + 1, path_cstr, &mode);
  } else if (strncmp(command, "https://", 8) == 0 ||
             strncmp(command, "http://", 7) == 0 ||
             strncmp(command, "git@", 4) == 0) {
    // URL shorthand for clone: try <url> = try clone <url>
    return cmd_clone((int)cmd_args.length, cmd_args.data, path_cstr, &mode);
  } else {
    // Unknown command - show help
    fprintf(stderr, "Unknown command: %s\n\n", command);
    print_help();
    return 1;
  }
}
