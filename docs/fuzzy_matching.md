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
- **Proximity bonus**: +1.0 / √(gap + 1) where gap is characters between consecutive matches

### 4. Score Multipliers

- **Density multiplier**: score × (query_length / last_match_position + 1)
  - Rewards matches concentrated toward the beginning of the string
- **Length penalty**: score × (10 / string_length + 10)
  - Penalizes longer directory names to favor concise matches

### 5. Contextual Bonuses

- **Date prefix bonus**: +2.0 if directory name starts with `YYYY-MM-DD-` pattern
- **Recency bonus**:
  - +0.5 if accessed within last hour
  - +0.3 if accessed within last day
  - +0.1 if accessed within last week

## Highlighting

Matched characters are wrapped with formatting tokens:
- `{b}` before matched character
- `{/b}` after matched character

These tokens are expanded to ANSI escape codes for terminal display.

## Scoring Examples

### Example 1: Perfect consecutive match
- Directory: `2025-11-29-project`
- Query: `pro`
- Matches: positions 11-12-13 (`p` `r` `o`)
- Score components:
  - Base: 3 × 1.0 = 3.0
  - Word boundary: +1.0 (at start of "project")
  - Proximity: +1.0/√1 + 1.0/√1 = 2.0
  - Density: × (3/14) ≈ ×0.214
  - Length: × (10/25) = ×0.4
  - Date bonus: +2.0
  - Total: ≈4.8

### Example 2: Scattered match
- Directory: `my-old-project`
- Query: `pro`
- Matches: positions 7-8-10 (`p` `r` `o`)
- Score components:
  - Base: 3 × 1.0 = 3.0
  - Word boundary: +1.0
  - Proximity: +1.0/√1 + 1.0/√2 ≈ 1.7
  - Density: × (3/11) ≈ ×0.273
  - Length: × (10/15) ≈ ×0.667
  - Total: ≈3.8

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
        score = 0.0
        tokenized = ""

        has_date_prefix = entry.name matches "^\d{4}-\d{2}-\d{2}-"
        if has_date_prefix:
            score += 2.0
            tokenized += "{dim}" + entry.name[0:11] + "{/fg}"
            remaining_text = entry.name[11:]
        else:
            remaining_text = entry.name

        if query == null or query == "":
            tokenized += remaining_text
            score += calculate_recency_bonus(entry.mtime)
            result.append({path: entry.path, score: score, tokenized_string: tokenized})
            continue

        text_lower = remaining_text.to_lower()
        query_lower = query.to_lower()

        query_idx = 0
        last_match_pos = -1
        current_pos = 0

        for i in 0..remaining_text.length-1:
            char = remaining_text[i]
            if query_idx < query.length and char.to_lower() == query_lower[query_idx]:
                score += 1.0

                if current_pos == 0 or not remaining_text[current_pos-1].is_alphanumeric():
                    score += 1.0

                if last_match_pos >= 0:
                    gap = current_pos - last_match_pos - 1
                    score += 1.0 / sqrt(gap + 1)

                last_match_pos = current_pos
                query_idx += 1

                tokenized += "{b}" + char + "{/b}"
            else:
                tokenized += char

            current_pos += 1

        if query_idx < query.length:
            continue

        if last_match_pos >= 0:
            score *= query.length / (last_match_pos + 1)

        score *= 10.0 / (entry.name.length + 10.0)

        score += calculate_recency_bonus(entry.mtime)

        result.append({path: entry.path, score: score, tokenized_string: tokenized})

    return result

function calculate_recency_bonus(mtime)
    now = current_time()
    age_hours = (now - mtime) / 3600

    if age_hours < 1:
        return 0.5
    else if age_hours < 24:
        return 0.3
    else if age_hours < 168:
        return 0.1
    else:
        return 0.0
```</content>
<parameter name="filePath">docs/fuzzy_matching.md