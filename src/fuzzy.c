#include "fuzzy.h"
#include "tui.h"
#include "utils.h"
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

  // Reset rendered string (reuse capacity if possible)
  zstr_clear(&entry->rendered);

  const char *text = zstr_cstr(&entry->name);

  // 1. Date prefix bonus
  if (has_date_prefix(text)) {
    entry->score += 2.0;
  }

  // 2. If no query, just render plain text
  if (!query || !*query) {
    zstr_cat(&entry->rendered, text);
    // Time-based scoring still applies
    goto time_score;
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

  while (*t_ptr) {
    if (query_idx < query_len && *t_ptr == q_ptr[query_idx]) {
      // Match found!
      entry->score += 1.0;

      // Word boundary bonus
      if (current_pos == 0 || !isalnum(*(t_ptr - 1))) {
        entry->score += 1.0;
      }

      // Proximity bonus
      if (last_pos >= 0) {
        int gap = current_pos - last_pos - 1;
        entry->score += 1.0 / sqrt(gap + 1);
      }

      last_pos = current_pos;
      query_idx++;

      // Append highlighted char
      zstr_cat(&entry->rendered, "{b}");
      zstr_push(&entry->rendered, *orig_ptr);
      zstr_cat(&entry->rendered, "{/b}");
    } else {
      // No match, append regular char
      zstr_push(&entry->rendered, *orig_ptr);
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

  // Density bonus
  if (last_pos >= 0) {
    entry->score *= ((float)query_len / (last_pos + 1));
  }

  // Length penalty
  int text_len = zstr_len(&entry->name);
  entry->score *= (10.0 / (text_len + 10.0));

time_score:
  // Time-based scoring
  {
    time_t now = time(NULL);
    double age = difftime(now, entry->mtime);
    if (age < 3600)
      entry->score += 0.5;
    else if (age < 86400)
      entry->score += 0.3;
    else if (age < 604800)
      entry->score += 0.1;
  }
}

float calculate_score(const char *text, const char *query, time_t mtime) {
  // Convenience wrapper using the new logic
  // We create a temporary entry just for scoring
  TryEntry tmp = {0};
  tmp.name = zstr_from(text);
  tmp.mtime = mtime;
  // rendered and path can be empty/init

  fuzzy_match(&tmp, query);

  float score = tmp.score;

  zstr_free(&tmp.name);
  zstr_free(&tmp.rendered);
  // path is empty, no need to free

  return score;
}
