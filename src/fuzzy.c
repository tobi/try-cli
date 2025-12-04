#include "fuzzy.h"
#include "tui.h"
#include "tui.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Helper to check for date prefix (YYYY-MM-DD-)
static bool has_date_prefix(const char *text) {
  return (strlen(text) >= 11 && isdigit(text[0]) && isdigit(text[1]) &&
          isdigit(text[2]) && isdigit(text[3]) && text[4] == '-' &&
          isdigit(text[5]) && isdigit(text[6]) && text[7] == '-' &&
          isdigit(text[8]) && isdigit(text[9]) && text[10] == '-');
}

void fuzzy_match(TryEntry *entry, const char *query) {
  // Reset score
  entry->score = 0.0;

  // Style string for proper nesting (dark date section + match highlights)
  TuiStyleString ss = tui_start_zstr(&entry->rendered);

  const char *text = zstr_cstr(&entry->name);

  // 2. If no query, just render with dimmed date prefix
  if (!query || !*query) {
    // Check for date prefix and render with dimming
    if (has_date_prefix(text)) {
      // Render date prefix (YYYY-MM-DD-) with dark color, including the trailing dash
      tui_push(&ss, TUI_DARK);
      zstr_cat_len(&entry->rendered, text, 11); // Date + dash is 11 chars
      tui_pop(&ss);
      zstr_cat(&entry->rendered, text + 11); // Rest after dash
    } else {
      zstr_cat(&entry->rendered, text);
    }
    // Time-based scoring (matches Ruby reference)
    time_t now = time(NULL);
    double hours_since_access = difftime(now, entry->mtime) / 3600.0;
    entry->score += 3.0 / sqrt(hours_since_access + 1);
    return;
  }

  // 3. Fuzzy match with highlighting
  // We need lower-case versions for case-insensitive matching
  Z_CLEANUP(zstr_free) zstr text_lower = zstr_from(text);
  Z_CLEANUP(zstr_free) zstr query_lower = zstr_from(query);

  // In-place tolower
  char *text_data = zstr_data(&text_lower);
  char *query_data = zstr_data(&query_lower);
  for (size_t i = 0; i < zstr_len(&text_lower); i++)
    text_data[i] = tolower(text_data[i]);
  for (size_t i = 0; i < zstr_len(&query_lower); i++)
    query_data[i] = tolower(query_data[i]);

  const char *t_ptr = text_data;
  const char *q_ptr = query_data;
  const char *orig_ptr = text;

  int query_len = zstr_len(&query_lower);
  int query_idx = 0;
  int last_pos = -1;
  int current_pos = 0;
  bool has_date = has_date_prefix(text);
  bool in_date_section = false;

  // Track fuzzy match score separately
  float fuzzy_score = 0.0;

  while (*t_ptr) {
    // Handle date prefix dimming (including the trailing dash at position 10)
    if (has_date && current_pos == 0) {
      tui_push(&ss, TUI_DARK);
      in_date_section = true;
    }

    if (query_idx < query_len && *t_ptr == q_ptr[query_idx]) {
      // Match found!
      fuzzy_score += 1.0;

      // Word boundary bonus
      if (current_pos == 0 || !isalnum(*(t_ptr - 1))) {
        fuzzy_score += 1.0;
      }

      // Proximity bonus (bumped to favor consecutive matches)
      if (last_pos >= 0) {
        int gap = current_pos - last_pos - 1;
        fuzzy_score += 2.0 / sqrt(gap + 1);
      }

      last_pos = current_pos;
      query_idx++;

      // Append highlighted char (yellow fg, preserves dark if in date section)
      tui_push(&ss, TUI_MATCH);
      tui_putc(&ss, *orig_ptr);
      tui_pop(&ss);
    } else {
      // No match, append regular char
      tui_putc(&ss, *orig_ptr);
    }

    // Close dim section after the trailing dash (position 10)
    if (has_date && current_pos == 10 && in_date_section) {
      tui_pop(&ss);
      in_date_section = false;
    }

    t_ptr++;
    orig_ptr++;
    current_pos++;
  }

  // If we didn't match the full query, score is 0 (filter out)
  if (query_idx < query_len) {
    entry->score = 0.0;
    // Keep the rendered string as is (it will be filtered out anyway)
    return;
  }

  // Apply multipliers only to fuzzy match score
  // Density bonus
  if (last_pos >= 0) {
    fuzzy_score *= ((float)query_len / (last_pos + 1));
  }

  // Length penalty
  int text_len = zstr_len(&entry->name);
  fuzzy_score *= (10.0 / (text_len + 10.0));

  // Date prefix bonus (applied after multipliers to avoid crushing)
  float date_bonus = 0.0;
  if (has_date_prefix(text)) {
    date_bonus = 2.0;
  }

  // Now add contextual bonuses (not affected by multipliers)
  entry->score = fuzzy_score + date_bonus;

  // Time-based scoring (matches Ruby reference implementation)
  time_t now = time(NULL);

  // Access time bonus - recently accessed is better
  double hours_since_access = difftime(now, entry->mtime) / 3600.0;
  entry->score += 3.0 / sqrt(hours_since_access + 1);
}

float calculate_score(const char *text, const char *query, time_t mtime) {
  // Convenience wrapper using the new logic
  // We create a temporary entry just for scoring
  TryEntry tmp = {0};
  tmp.name = zstr_from(text);
  tmp.rendered = zstr_init();
  tmp.path = zstr_init();
  tmp.mtime = mtime;

  fuzzy_match(&tmp, query);

  float score = tmp.score;

  zstr_free(&tmp.name);
  zstr_free(&tmp.rendered);
  zstr_free(&tmp.path);

  return score;
}
