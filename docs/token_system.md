# Token System Specification

This document describes the token-based text formatting system used throughout the `try` CLI tool for dynamic styling and ANSI escape code management.

## Overview

The token system provides a declarative way to apply text formatting without hardcoding ANSI escape sequences. Text containing tokens is processed through an expansion function that replaces tokens with their corresponding ANSI codes, enabling consistent styling across the application.

## Token Format

Tokens are placeholder strings enclosed in curly braces: `{token_name}`

- Opening tokens apply formatting: `{bold}`, `{dim}`
- Closing tokens reset formatting: `{/bold}`, `{/fg}`
- Self-closing tokens may exist for specific use cases

## Available Tokens

### Text Formatting Tokens

| Token | ANSI Code | Description | Usage |
|-------|-----------|-------------|-------|
| `{b}` | `\033[1;33m` | Bold + Yellow | Highlighted text, fuzzy match characters |
| `{/b}` | `\033[22m` | Reset bold only | End bold formatting while preserving color |
| `{dim}` | `\033[2m` | Dim text | Secondary/de-emphasized text |
| `{text}` | `\033[0m` | Full reset | Normal text (complete reset) |
| `{reset}` | `\033[0m` | Full reset | Complete reset of all formatting |
| `{/fg}` | `\033[39m` | Reset foreground | Reset foreground color to default |

### Heading Tokens

| Token | ANSI Code | Description | Usage |
|-------|-----------|-------------|-------|
| `{h1}` | `\033[1;38;5;214m` | Bold + Orange | Primary headings |
| `{h2}` | `\033[1;34m` | Bold + Blue | Secondary headings |

### Selection Tokens

| Token | ANSI Code | Description | Usage |
|-------|-----------|-------------|-------|
| `{section}` | `\033[1m` | Bold | Start of selected/highlighted section |
| `{/section}` | `\033[0m` | Full reset | End of selected section |

## ANSI Constants

The following ANSI escape sequences are defined as constants:

```c
#define ANSI_RESET     "\033[0m"
#define ANSI_BOLD      "\033[1m"
#define ANSI_DIM       "\033[2m"
#define ANSI_RED       "\033[31m"
#define ANSI_GREEN     "\033[32m"
#define ANSI_YELLOW    "\033[33m"
#define ANSI_BLUE      "\033[34m"
#define ANSI_MAGENTA   "\033[35m"
#define ANSI_CYAN      "\033[36m"
#define ANSI_WHITE     "\033[37m"
```

## Token Expansion Process

1. **Input**: String containing embedded tokens
2. **Processing**: Replace each `{token}` with corresponding ANSI sequence
3. **Output**: ANSI-formatted string ready for terminal display

### Example Expansion

```c
// Input string
"Status: {b}OK{/b} - {dim}completed{/fg}"

// After expansion
"Status: \033[1;33mOK\033[22m - \033[2mcompleted\033[39m"
```

## Usage Patterns

### Fuzzy Matching Highlighting

```c
// Input: "2025-11-29-test", query: "te"
// Rendered: "2025-11-29-{b}te{/b}st"
// Display: "2025-11-29-[bold yellow]te[reset bold]st"
```

### UI Element Formatting

```c
// Status message
"{h1}Try Selector{/h1}\n"
"{dim}Query:{/fg} {b}%s{/b}\n"
"Found {section}%d{/section} matches\n"
```

### Date Dimming

```c
// Directory with date prefix
"{dim}2025-11-29-{/fg}project-name"
```

## Implementation Details

### Expansion Function

The `zstr_expand_tokens()` function in `src/utils.c` performs the replacement:

- Iterates through the input string
- Detects token patterns `{...}`
- Replaces each token with its ANSI equivalent
- Returns a new formatted string

### Token Validation

- Unknown tokens are left unchanged in the output
- Malformed tokens (missing closing `}`) are preserved as-is
- Nested tokens are not supported

### Performance Considerations

- Token expansion is performed once per render cycle
- ANSI codes are compact (4-10 bytes per sequence)
- No runtime allocation overhead for known tokens

## Design Principles

### Declarative Styling

Tokens allow styling to be defined in data rather than code:

```c
// Instead of:
printf("\033[1;33m%s\033[22m", text);

// Use:
printf("%s", expand_tokens("{b}" + text + "{/b}"));
```

### Consistency

Centralized token definitions ensure consistent appearance across all UI components.

### Maintainability

Adding new styles requires only:
1. Define the token in the expansion function
2. Use the token in formatting strings

### Terminal Compatibility

ANSI codes are widely supported in modern terminals. The system gracefully degrades when ANSI is unavailable (tokens remain visible as plain text).

## Integration with Fuzzy Matching

The fuzzy matching system (`src/fuzzy.c`) inserts `{b}` tokens around matched characters:

1. Character matching identifies positions
2. `{b}` tokens wrap matched characters
3. `{/b}` tokens close the highlighting
4. Final string is ready for token expansion and display

## Future Extensions

The token system can be extended with:

- Additional color tokens (`{red}`, `{green}`, etc.)
- Background color tokens (`{bg_red}`)
- Style combinations (`{bold_red}`)
- Conditional tokens based on terminal capabilities</content>
<parameter name="filePath">docs/token_system.md