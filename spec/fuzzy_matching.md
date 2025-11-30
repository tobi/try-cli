# Fuzzy Matching Specification

This document describes the fuzzy matching algorithm used in the `try` CLI tool for scoring and filtering directory entries during interactive selection.

## Overview

The fuzzy matching system evaluates how well a directory name matches a user's search query. It combines character-level matching with contextual bonuses to rank results, favoring recently accessed directories and those with structured naming conventions.

## Input/Output

- **Input**: Directory name (string), search query (string), last modification time (timestamp)
- **Output**: Numeric score (float), highlighted text with formatting tokens

## Algorithm Phases

### 1. Preprocessing

- Convert both directory name and query to lowercase for case-insensitive matching
- Check for date prefix pattern: `YYYY-MM-DD-` at start of directory name

### 2. Character Matching

Perform sequential matching of query characters against the directory name:

- Iterate through each character in the directory name
- For each query character found in sequence, record match position
- Track gaps between consecutive matches
- If entire query is not matched, score = 0 (entry filtered out)

### 3. Base Scoring

- **Character match**: +1.0 point per matched character
- **Word boundary bonus**: +1.0 if match occurs at word start (position 0 or after non-alphanumeric character)
- **Proximity bonus**: +2.0 / √(gap + 1) where gap is characters between consecutive matches
  - Consecutive matches (gap=0): +2.0
  - Gap of 1: +1.41
  - Gap of 5: +0.82

### 4. Score Multipliers

**Important**: These multipliers are applied **only to the fuzzy match score** (character matches + bonuses), not to contextual bonuses. This ensures that adding a query doesn't crush the base score.

- **Density multiplier**: fuzzy_score × (query_length / last_match_position + 1)
  - Rewards matches concentrated toward the beginning of the string
- **Length penalty**: fuzzy_score × (10 / string_length + 10)
  - Penalizes longer directory names to favor concise matches

### 5. Contextual Bonuses

**Important**: These bonuses are added **after** multipliers are applied, so they maintain their full value.

- **Date prefix bonus**: +2.0 if directory name starts with `YYYY-MM-DD-` pattern
- **Recency bonus**: +3.0 / √(hours_since_access + 1)
  - Provides smooth decay favoring recently accessed directories
  - Just accessed: +3.0
  - 1 hour ago: +2.1
  - 24 hours ago: +0.6
  - 1 week ago: +0.2

**Final score calculation**: `final_score = (fuzzy_score × density × length) + date_bonus + recency_bonus`

## Highlighting

Matched characters are wrapped with formatting tokens:
- `{b}` before matched character
- `{/b}` after matched character

These tokens are expanded to ANSI escape codes for terminal display.

## Scoring Examples

### Example 1: Perfect consecutive match (recent access)
- Directory: `2025-11-29-project`
- Query: `pro`
- Last accessed: 1 hour ago
- Matches: positions 11-12-13 (`p` `r` `o`)
- Score components:
  - **Fuzzy score calculation:**
    - Base: 3 × 1.0 = 3.0
    - Word boundary: +1.0 (at start of "project")
    - Proximity: +2.0/√1 + 2.0/√1 = 4.0 (consecutive matches)
    - Subtotal: 8.0
    - Density: × (3/14) ≈ ×0.214
    - Length: × (10/19) ≈ ×0.526
    - After multipliers: 8.0 × 0.214 × 0.526 ≈ 0.90
  - **Contextual bonuses (added after multipliers):**
    - Date bonus: +2.0
    - Recency: +3.0/√2 ≈ +2.1
  - **Final score**: 0.90 + 2.0 + 2.1 ≈ **5.0**

### Example 2: Scattered match (no date prefix, older)
- Directory: `my-old-project`
- Query: `pro`
- Last accessed: 24 hours ago
- Matches: positions 7-8-10 (`p` `r` `o`)
- Score components:
  - **Fuzzy score calculation:**
    - Base: 3 × 1.0 = 3.0
    - Word boundary: +1.0
    - Proximity: +2.0/√1 + 2.0/√2 ≈ 3.4
    - Subtotal: 7.4
    - Density: × (3/11) ≈ ×0.273
    - Length: × (10/24) ≈ ×0.417
    - After multipliers: 7.4 × 0.273 × 0.417 ≈ 0.84
  - **Contextual bonuses (added after multipliers):**
    - Date bonus: +0.0 (no date prefix)
    - Recency: +3.0/√25 = +0.6
  - **Final score**: 0.84 + 0.0 + 0.6 ≈ **1.4**

## Design Principles

- **Favor recency**: Recently accessed directories appear higher
- **Structured naming**: Date-prefixed directories get priority
- **Word boundaries**: Matches at logical breaks score higher
- **Consecutive matches**: Characters close together score better
- **Early matches**: Matches near string start are preferred
- **Conciseness**: Shorter directory names are favored

## Filtering Behavior

- Entries with score = 0 are hidden from results
- Zero score occurs when query characters cannot be matched in sequence
- Partial matches are not allowed - all query characters must be found

## Pseudocode Implementation

```
function process_entries(query: optional string, entries: list of {name, path, mtime})
    result = []

    for entry in entries:
        tokenized = ""
        has_date_prefix = entry.name matches "^\d{4}-\d{2}-\d{2}-"

        # Calculate date bonus (applied after multipliers)
        date_bonus = 0.0
        if has_date_prefix:
            date_bonus = 2.0

        if query == null or query == "":
            # No query - just render and score by recency
            if has_date_prefix:
                tokenized += "{dim}" + entry.name[0:11] + "{/fg}" + entry.name[11:]
            else:
                tokenized += entry.name

            score = date_bonus + calculate_recency_bonus(entry.mtime)
            result.append({path: entry.path, score: score, tokenized_string: tokenized})
            continue

        # Fuzzy matching with query
        text_lower = entry.name.to_lower()
        query_lower = query.to_lower()

        fuzzy_score = 0.0
        query_idx = 0
        last_match_pos = -1
        current_pos = 0

        for i in 0..entry.name.length-1:
            char = entry.name[i]
            if query_idx < query.length and char.to_lower() == query_lower[query_idx]:
                fuzzy_score += 1.0

                if current_pos == 0 or not entry.name[current_pos-1].is_alphanumeric():
                    fuzzy_score += 1.0

                if last_match_pos >= 0:
                    gap = current_pos - last_match_pos - 1
                    fuzzy_score += 2.0 / sqrt(gap + 1)

                last_match_pos = current_pos
                query_idx += 1

                tokenized += "{b}" + char + "{/b}"
            else:
                tokenized += char

            current_pos += 1

        if query_idx < query.length:
            # Partial match - skip this entry
            continue

        # Apply multipliers only to fuzzy score
        if last_match_pos >= 0:
            fuzzy_score *= query.length / (last_match_pos + 1)

        fuzzy_score *= 10.0 / (entry.name.length + 10.0)

        # Add contextual bonuses after multipliers
        final_score = fuzzy_score + date_bonus + calculate_recency_bonus(entry.mtime)

        result.append({path: entry.path, score: final_score, tokenized_string: tokenized})

    return result

function calculate_recency_bonus(mtime)
    now = current_time()
    hours_since_access = (now - mtime) / 3600
    return 3.0 / sqrt(hours_since_access + 1)
```</content>
<parameter name="filePath">docs/fuzzy_matching.md