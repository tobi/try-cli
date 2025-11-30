# CLI Structure Specification

## Overview

`try` operates in two distinct modes: **direct mode** for standalone operations and **exec mode** for shell-integrated operations that need to affect the parent shell (like `cd`).

## Execution Modes

### Direct Mode

Invoked as `./try` or when the binary is called without the `exec` subcommand.

**Behavior:**
- `try` or `try --help` → Show compact help explaining alias requirement
- `try clone <url> [name]` → Clone immediately, print `cd /path/to/new-try` hint
- `try worktree ...` → Execute immediately, print cd hint

Direct mode **cannot** change the shell's working directory (subshell limitation), so it prints a cd hint for the user to copy/paste.

### Exec Mode

Invoked via `try exec ...` from the shell alias. Returns shell scripts to stdout for eval.

**Behavior:**
- All output is a shell script starting with a warning comment
- Exit code 0 → alias evals the output
- Exit code 1 → alias prints the output (error/cancellation)

## Shell Alias

### Bash/Zsh

```bash
try() {
  local out
  out=$(/path/to/try exec --path ~/src/tries "$@" 2>/dev/tty)
  if [ $? -eq 0 ]; then
    eval "$out"
  else
    echo "$out"
  fi
}
```

### Fish

```fish
function try
  set -l out (/path/to/try exec --path ~/src/tries $argv 2>/dev/tty)
  if test $status -eq 0
    eval $out
  else
    echo $out
  end
end
```

## Commands

| Command | Mode | Output |
|---------|------|--------|
| `try` | direct | Help text to stderr |
| `try --help` | direct | Help text to stderr |
| `try clone <url> [name]` | direct | Runs git clone, prints cd hint |
| `try worktree add <name>` | direct | Creates worktree, prints cd hint |
| `try exec` | exec | Interactive selector → cd script |
| `try exec <query>` | exec | Filtered selector → cd script |
| `try exec clone <url> [name]` | exec | git clone + cd script |
| `try exec worktree add <name>` | exec | worktree + cd script |

## Script Output Format

All `try exec` commands output shell scripts to stdout:

**Select existing directory (exit 0):**
```bash
# if you can read this, you didn't launch try from an alias. run try --help.
touch '/home/user/src/tries/2025-11-30-project' && \
  cd '/home/user/src/tries/2025-11-30-project' && \
  true
```

**Create new directory (exit 0):**
```bash
# if you can read this, you didn't launch try from an alias. run try --help.
mkdir -p '/home/user/src/tries/2025-11-30-new-idea' && \
  cd '/home/user/src/tries/2025-11-30-new-idea' && \
  true
```

**Clone via exec (exit 0):**
```bash
# if you can read this, you didn't launch try from an alias. run try --help.
git clone 'https://github.com/user/repo.git' '/home/user/src/tries/2025-11-30-repo' && \
  cd '/home/user/src/tries/2025-11-30-repo' && \
  true
```

**Cancellation/Error (exit 1):**
```
Cancelled.
```

## Exit Codes

| Code | Meaning | Alias Action |
|------|---------|--------------|
| 0 | Success | `eval` the output |
| 1 | Error or cancelled | Print the output |

## Help Text (Direct Mode)

Compact help shown when running `try` directly:

```
try - ephemeral workspace manager

To use try, add to your shell config:

  # bash/zsh (~/.bashrc or ~/.zshrc)
  eval "$(try init ~/src/tries)"

  # fish (~/.config/fish/config.fish)
  eval (try init ~/src/tries | string collect)

Then use: try [query]     - select directory
          try clone <url> - clone repo
          try --help      - full help
```

## Init Command

`try init <path>` outputs the shell function definition for the current shell:

```bash
$ try init ~/src/tries
# Outputs the appropriate function definition for bash/zsh/fish
```
