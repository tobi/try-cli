# Token System Specification

The token system provides a declarative way to apply ANSI formatting in terminal output. It uses a stack-based approach that allows proper nesting and clean restoration of previous styles.

> **Note**: It is totally OK to only implement a subset of this system to make try work. This spec is extensive because it's being used outside of just try.

## Implementation

The token parser is implemented using [Ragel](https://www.colm.net/open-source/ragel/) to generate a fast state machine. The source is in `src/tokens.rl` and generates `src/tokens.c`.

To regenerate after modifying `tokens.rl`:
```bash
ragel -C -G2 src/tokens.rl -o src/tokens.c
```

## Stack Semantics

Each style-setting token pushes its previous state onto a stack. The `{/}` token pops one level, restoring the previous state. This enables proper nesting:

```c
// Input:  "Normal {highlight}bold yellow {red}red{/} yellow{/} normal"
// Output: "Normal [bold+yellow]bold yellow [red]red[yellow] yellow[reset] normal"
```

Composite tokens (like `{highlight}` which sets both bold and yellow) push multiple attributes but are grouped so a single `{/}` restores all of them.

## Deferred Emission

ANSI codes are only emitted when an actual character is about to be printed. This provides several benefits:

1. **Redundancy avoidance**: `{dim}{dim}{dim}x` outputs only one `\033[90m` code
2. **No unused codes**: `{b}{/}x` outputs just `x` with no ANSI codes
3. **Efficient output**: Codes are batched when possible (e.g., `\033[1;33m` instead of `\033[1m\033[33m`)

## Auto-Reset at Newlines

All active styles are automatically reset before each newline character. This ensures:
- Clean line boundaries without trailing ANSI codes
- Proper terminal behavior when lines are scrolled or redrawn
- No style bleeding across lines

## Token Reference

### Semantic Tokens (Composite Styles)

| Token | Effect | ANSI Codes |
|-------|--------|------------|
| `{b}` | Bold only (same as `{strong}`) | `\033[1m` |
| `{highlight}` | Bold + yellow foreground (for highlighting matches) | `\033[1;33m` |
| `{h1}` | Bold + orange (256-color 214) | `\033[1;38;5;214m` |
| `{h2}` | Bold + blue | `\033[1;34m` |
| `{h3}` - `{h6}` | Bold + white | `\033[1;37m` |
| `{strong}` | Bold | `\033[1m` |
| `{dim}` | Gray foreground (bright black) | `\033[90m` |
| `{section}` | Bold only | `\033[1m` |
| `{danger}` | Dark red background (for warnings/deletions) | `\033[48;5;52m` |
| `{strike}` | Legacy alias for `{danger}` | `\033[48;5;52m` |
| `{text}` | Full reset | `\033[0m` |

### Attribute Tokens

| Token | Effect | ANSI Code |
|-------|--------|-----------|
| `{bold}`, `{B}` | Bold text | `\033[1m` |
| `{italic}`, `{I}`, `{i}` | Italic text | `\033[3m` |
| `{underline}`, `{U}`, `{u}` | Underlined text | `\033[4m` |
| `{reverse}` | Reverse/inverse video | `\033[7m` |
| `{strikethrough}` | Strikethrough text | `\033[9m` |
| `{bright}` | Brighten current foreground color | Converts 30-37 to 90-97 |

### Foreground Colors

Standard colors:
| Token | ANSI Code |
|-------|-----------|
| `{black}` | `\033[30m` |
| `{red}` | `\033[31m` |
| `{green}` | `\033[32m` |
| `{yellow}` | `\033[33m` |
| `{blue}` | `\033[34m` |
| `{magenta}` | `\033[35m` |
| `{cyan}` | `\033[36m` |
| `{white}` | `\033[37m` |
| `{gray}`, `{grey}` | `\033[90m` |

Bright colors:
| Token | ANSI Code |
|-------|-----------|
| `{bright:black}` | `\033[90m` |
| `{bright:red}` | `\033[91m` |
| `{bright:green}` | `\033[92m` |
| `{bright:yellow}` | `\033[93m` |
| `{bright:blue}` | `\033[94m` |
| `{bright:magenta}` | `\033[95m` |
| `{bright:cyan}` | `\033[96m` |
| `{bright:white}` | `\033[97m` |

256-color palette:
| Token | ANSI Code |
|-------|-----------|
| `{fg:N}` | `\033[38;5;Nm` where N is 0-255 |

### Background Colors

Standard colors:
| Token | ANSI Code |
|-------|-----------|
| `{bg:black}` | `\033[40m` |
| `{bg:red}` | `\033[41m` |
| `{bg:green}` | `\033[42m` |
| `{bg:yellow}` | `\033[43m` |
| `{bg:blue}` | `\033[44m` |
| `{bg:magenta}` | `\033[45m` |
| `{bg:cyan}` | `\033[46m` |
| `{bg:white}` | `\033[47m` |
| `{bg:gray}`, `{bg:grey}` | `\033[100m` |

256-color palette:
| Token | ANSI Code |
|-------|-----------|
| `{bg:N}` | `\033[48;5;Nm` where N is 0-255 |

### Reset/Pop Tokens

| Token | Effect |
|-------|--------|
| `{/}` | Pop one level, restore previous style |
| `{reset}` | Full reset, clear entire stack |
| `{/fg}` | Reset foreground color to default |
| `{/bg}` | Reset background color to default |
| `{/name}` | Same as `{/}` - name is ignored (e.g., `{/highlight}`, `{/b}`) |

### Control Tokens

| Token | Effect | ANSI Code |
|-------|--------|-----------|
| `{clr}` | Clear to end of line | `\033[K` |
| `{cls}` | Clear to end of screen | `\033[J` |
| `{home}` | Move cursor to home | `\033[H` |
| `{hide}` | Hide cursor | `\033[?25l` |
| `{show}` | Show cursor | `\033[?25h` |

### Special Tokens

| Token | Effect |
|-------|--------|
| `{cursor}` | Mark cursor position for cursor tracking |

## API

### `zstr_expand_tokens(const char *text)`

Expands tokens in the input text, returning a zstr with ANSI codes.

```c
Z_CLEANUP(zstr_free) zstr result = zstr_expand_tokens("{b}Hello{/} World");
printf("%s\n", zstr_cstr(&result));
// Output: [bold yellow]Hello[reset] World
```

### `zstr_expand_tokens_with_cursor(const char *text)`

Same as above, but also tracks the `{cursor}` marker position.

```c
TokenExpansion result = zstr_expand_tokens_with_cursor("Search: {cursor}");
// result.expanded contains the expanded text
// result.cursor_pos contains the visual column (1-indexed) of {cursor}
zstr_free(&result.expanded);
```

### Global Flags

| Flag | Effect |
|------|--------|
| `zstr_disable_token_expansion` | If true, tokens pass through unchanged |
| `zstr_no_colors` | If true, tokens expand to empty strings (no ANSI codes) |

## Usage Examples

### Basic Formatting

```c
// Simple bold highlight
zstr s = zstr_expand_tokens("Status: {b}OK{/}");

// Nested styles
zstr s = zstr_expand_tokens("{bold}Bold {italic}and italic{/} just bold{/} normal");

// Colors
zstr s = zstr_expand_tokens("{red}Error:{/} Something went wrong");
```

### 256-Color Support

```c
// Orange text (color 214)
zstr s = zstr_expand_tokens("{fg:214}Orange text{/}");

// Custom background
zstr s = zstr_expand_tokens("{bg:52}Dark red background{/}");
```

### TUI Building

```c
// Clear line and write header
zstr line = zstr_expand_tokens("{h1}Title{/}{clr}\n");

// Status with cursor tracking
TokenExpansion te = zstr_expand_tokens_with_cursor("Input: {cursor}{clr}");
// Use te.cursor_pos to position the terminal cursor
```

## Design Notes

### Why Ragel?

Ragel generates highly optimized state machines that are:
- Fast: No backtracking, O(n) parsing
- Compact: Efficient goto-based code generation with `-G2`
- Correct: Formal grammar prevents parsing bugs

### Stack Implementation

The stack stores restore actions, not full state. When a token like `{bold}` is applied:
1. The previous bold state (on/off) is pushed onto the stack
2. Bold is enabled
3. When `{/}` is called, the stack is popped and the previous state is restored

This approach minimizes memory usage and allows efficient restoration.

### Composite Token Handling

Tokens like `{b}` that set multiple attributes use a "composite marker" on the stack. When pushed:
1. Individual attribute changes are pushed (bold state, color state)
2. A composite marker is pushed with the count of changes

When popped:
1. The composite marker is popped
2. All individual changes are popped and restored

This ensures a single `{/}` can undo a composite style change.
