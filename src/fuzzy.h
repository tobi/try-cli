#ifndef FUZZY_H
#define FUZZY_H

#include "tui.h" // Need full definition of TryEntry
#include <time.h>

// Updates entry->score and entry->rendered in-place
void fuzzy_match(TryEntry *entry, const char *query);

// Legacy/Convenience: just calculate score (read-only)
float calculate_score(const char *text, const char *query, time_t mtime);

// Highlight matching characters in text with {b} tokens
// Caller must free the returned string
char *highlight_matches(const char *text, const char *query);

#endif // FUZZY_H
