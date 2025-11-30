// Feature test macros for cross-platform compatibility
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#else
#define _GNU_SOURCE
#endif

#include "commands.h"
#include "config.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Compact help for direct mode
static void print_help(void) {
  Z_CLEANUP(zstr_free) zstr default_path = get_default_tries_path();

  Z_CLEANUP(zstr_free) zstr help = zstr_from(
    "{h1}try{reset} v" TRY_VERSION " - ephemeral workspace manager\n\n"
    "To use try, add to your shell config:\n\n"
    "  {dim}# bash/zsh (~/.bashrc or ~/.zshrc){reset}\n"
    "  {b}eval \"$(try init ~/src/tries)\"{/b}\n\n"
    "  {dim}# fish (~/.config/fish/config.fish){reset}\n"
    "  {b}eval (try init ~/src/tries | string collect){/b}\n\n"
    "Then use:\n"
    "  {b}try{/b} [query]         Interactive directory selector\n"
    "  {b}try clone{/b} <url>     Clone repo into dated directory\n"
    "  {b}try worktree{/b} <name> Create worktree from current git repo\n"
    "  {b}try --help{/b}          Show this help\n\n"
    "{dim}Manual mode (without alias):{reset}\n"
    "  {b}try exec{/b} [query]    Output shell script to eval\n\n"
    "{dim}Defaults:{reset}\n"
    "  Default path: {dim}~/src/tries{reset} (override with {b}--path{/b} on init)\n"
    "  Current default: {dim}");
  zstr_cat(&help, zstr_cstr(&default_path));
  zstr_cat(&help, "{reset}\n");

  Z_CLEANUP(zstr_free) zstr expanded = zstr_expand_tokens(zstr_cstr(&help));
  fprintf(stderr, "%s", zstr_cstr(&expanded));
}

int main(int argc, char **argv) {
  Z_CLEANUP(zstr_free) zstr tries_path = zstr_init();
  AUTO_FREE char **cmd_argv = NULL;
  int cmd_argc = 0;

  // Mode configuration
  Mode mode = {.type = MODE_DIRECT};

  // Simple arg parsing
  cmd_argv = malloc(sizeof(char *) * (size_t)argc);

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--path") == 0 && i + 1 < argc) {
      zstr_free(&tries_path);
      tries_path = zstr_from(argv[i + 1]);
      i++;
    } else if (strncmp(argv[i], "--path=", 7) == 0) {
      zstr_free(&tries_path);
      tries_path = zstr_from(argv[i] + 7);
    } else if (strcmp(argv[i], "--and-exit") == 0) {
      mode.render_once = true;
    } else if (strcmp(argv[i], "--and-keys") == 0 && i + 1 < argc) {
      mode.inject_keys = argv[i + 1];
      i++;
    } else if (strncmp(argv[i], "--and-keys=", 11) == 0) {
      mode.inject_keys = argv[i] + 11;
    } else if (strcmp(argv[i], "--no-expand-tokens") == 0) {
      zstr_disable_token_expansion = true;
    } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      print_help();
      return 0;
    } else {
      cmd_argv[cmd_argc++] = argv[i];
    }
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
  if (cmd_argc == 0) {
    print_help();
    return 0;
  }

  const char *command = cmd_argv[0];

  // Route commands
  if (strcmp(command, "init") == 0) {
    cmd_init(cmd_argc - 1, cmd_argv + 1, path_cstr);
    return 0;
  } else if (strcmp(command, "exec") == 0) {
    // Exec mode
    mode.type = MODE_EXEC;
    return cmd_exec(cmd_argc - 1, cmd_argv + 1, path_cstr, &mode);
  } else if (strcmp(command, "clone") == 0) {
    // Direct mode clone
    return cmd_clone(cmd_argc - 1, cmd_argv + 1, path_cstr, &mode);
  } else if (strcmp(command, "worktree") == 0) {
    // Direct mode worktree
    return cmd_worktree(cmd_argc - 1, cmd_argv + 1, path_cstr, &mode);
  } else {
    // Unknown command - show help
    fprintf(stderr, "Unknown command: %s\n\n", command);
    print_help();
    return 1;
  }
}
