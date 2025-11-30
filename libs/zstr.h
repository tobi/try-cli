/*
 * GENERATED FILE - DO NOT EDIT DIRECTLY
 * Source: zstr.c
 *
 * This file is part of the z-libs collection: https://github.com/z-libs
 * Licensed under the MIT License.
 */


/* ============================================================================
   z-libs Common Definitions (Bundled)
   This block is auto-generated. It is guarded so that if you include multiple
   z-libs it is only defined once.
   ============================================================================ */
#ifndef Z_COMMON_BUNDLED
#define Z_COMMON_BUNDLED


#ifndef ZCOMMON_H
#define ZCOMMON_H

#include <stddef.h>

// Return Codes.
#define Z_OK     0
#define Z_ERR   -1
#define Z_FOUND  1

// Memory Macros.
// If the user hasn't defined their own allocator, use the standard one.
#ifndef Z_MALLOC
    #include <stdlib.h>
    #define Z_MALLOC(sz)       malloc(sz)
    #define Z_CALLOC(n, sz)    calloc(n, sz)
    #define Z_REALLOC(p, sz)   realloc(p, sz)
    #define Z_FREE(p)          free(p)
#endif

// Compiler Extensions (Optional).
// We check for GCC/Clang features to enable RAII-style cleanup.
// Define Z_NO_EXTENSIONS to disable this manually.
#if !defined(Z_NO_EXTENSIONS) && (defined(__GNUC__) || defined(__clang__))
    #define Z_HAS_CLEANUP 1
    #define Z_CLEANUP(func) __attribute__((cleanup(func)))
#else
    #define Z_HAS_CLEANUP 0
    #define Z_CLEANUP(func) 
#endif

// Metaprogramming Markers.
// These macros are used by the Z-Scanner tool to find type definitions.
// For the C compiler, they are no-ops (they compile to nothing).
#define DEFINE_VEC_TYPE(T, Name)
#define DEFINE_LIST_TYPE(T, Name)
#define DEFINE_MAP_TYPE(Key, Val, Name)

//Growth Strategy.
// Determines how containers expand when full.
// Default is 2.0x (Geometric Growth).
//
// Alternatives:
// #define Z_GROWTH_FACTOR(c)  ((c) * 3 / 2) // 1.5x (Better for fragmentation)
// #define Z_GROWTH_FACTOR(c)  ((c) + 16)    // Linear (Not recommended)
#ifndef Z_GROWTH_FACTOR
    #define Z_GROWTH_FACTOR(cap) ((cap) == 0 ? 32 : (cap) * 2)
#endif

#endif


#endif // Z_COMMON_BUNDLED
/* ============================================================================ */


#ifndef ZSTR_H
#define ZSTR_H
// [Bundled] "zcommon.h" is included inline in this same file
#include <stddef.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <ctype.h>
#include <stdlib.h> 

// I am thinking of you too, C++ devs.
#ifdef __cplusplus
extern "C" {
#endif

/* Configuration and Macros */

#ifndef Z_STR_MALLOC
    #define Z_STR_MALLOC(sz)        (char *)Z_MALLOC(sz)
#endif

#ifndef Z_STR_CALLOC
    #define Z_STR_CALLOC(n, sz)     (char *)Z_CALLOC(n, sz)
#endif

#ifndef Z_STR_REALLOC
    #define Z_STR_REALLOC(p, sz)    (char *)Z_REALLOC(p, sz)
#endif

#ifndef Z_STR_FREE
    #define Z_STR_FREE(p)           Z_FREE(p)
#endif

#if defined(__GNUC__) || defined(__clang__)
    #define ZSTR_PRINTF_ATTR(fmt_idx, var_idx) __attribute__((format(printf, fmt_idx, var_idx)))
#else
    #define ZSTR_PRINTF_ATTR(fmt_idx, var_idx)
#endif

// SSO Capacity -> 23 bytes available.
// Max string length = 23 chars (if using the last byte for length/flag trick) 
// OR 22 chars + null terminator. We stick to 23 bytes total storage.
#define ZSTR_SSO_CAP 23
#define ZSTR_UTF8_INVALID 0xFFFD

#ifndef ZSTR_FMT
#define ZSTR_FMT "%.*s"
#define ZSTR_ARG(s) (int)zstr_len(&(s)), zstr_cstr(&(s))
#endif

// Alias macro for pushing a single char.
#define zstr_push(s, c) zstr_push_char(s, c)

/* Data Structures */

// Heap allocated string layout.
typedef struct 
{
    char   *ptr;
    size_t len;
    size_t cap;
} zstr_long;

// Stack allocated (SSO) layout.
typedef struct {
    char buf[ZSTR_SSO_CAP];
    uint8_t len; 
} zstr_short;

// The main string type.
typedef struct {
    uint8_t is_long;
    char _pad[7];   // Padding for alignment on 64-bit systems.
    union {
        zstr_long l;
        zstr_short s;
    };
} zstr;

// A read-only slice of a string (borrowed reference).
typedef struct
{
    const char *data;
    size_t len;
} zstr_view;

// Iterator state for splitting strings.
typedef struct {
    zstr_view source;
    zstr_view delim;
    size_t current_pos;
    bool finished;
} zstr_split_iter;


/* Internal Helpers and Accessors */

// Returns true if the string is heap-allocated.
static inline bool zstr_is_long(const zstr *s)
{
    return s->is_long;
}

// Returns a pointer to the mutable data buffer.
static inline char* zstr_data(zstr *s)
{
    return s->is_long ? s->l.ptr : s->s.buf;
}

// Returns a pointer to the const data buffer (C-string compatible).
static inline const char* zstr_cstr(const zstr *s)
{
    return s->is_long ? s->l.ptr : s->s.buf;  
}

// Returns the current length of the string (excluding null terminator).
static inline size_t zstr_len(const zstr *s)
{
    return s->is_long ? s->l.len : s->s.len;    
}

// Returns true if the string length is 0.
static inline bool zstr_is_empty(const zstr *s)
{
    return zstr_len(s) == 0;
}


/* Creation and Destruction */

// Initializes an empty string {0}.
static inline zstr zstr_init(void)
{
    zstr s;
    memset(&s, 0, sizeof(zstr));
    return s;
}

// Frees the string if it is on the heap, and resets it to empty.
static inline void zstr_free(zstr *s)
{
    if (s->is_long) Z_STR_FREE(s->l.ptr);
    *s = zstr_init();
}

// Clears the content (sets length to 0) but keeps the allocated capacity.
static inline void zstr_clear(zstr *s) 
{
    if (s->is_long) 
    {
        s->l.len = 0;
        s->l.ptr[0] = '\0';
    } 
    else 
    {
        s->s.len = 0;
        s->s.buf[0] = '\0';
    }
}


/* Memory Management */

// Ensures the string has at least `new_cap` capacity.
// Handles the transition from SSO (Stack) to Long (Heap).
static inline int zstr_reserve(zstr *s, size_t new_cap)
{
    if (new_cap < ZSTR_SSO_CAP) return Z_OK;
    if (s->is_long && new_cap <= s->l.cap) return Z_OK;

    char *new_ptr;
    if (s->is_long)
    {
        new_ptr = Z_STR_REALLOC(s->l.ptr, new_cap + 1);
    }
    else 
    {
        new_ptr = Z_STR_MALLOC(new_cap + 1);
        if (new_ptr)
        {
            memcpy(new_ptr, s->s.buf, s->s.len);
            new_ptr[s->s.len] = '\0';
        }
    }

    if (!new_ptr) return Z_ERR;

    // Transition state if we were short before.
    if (!s->is_long)
    {
        s->l.len   = s->s.len;
        s->is_long = 1;
    }

    s->l.ptr = new_ptr;
    s->l.cap = new_cap;

    return Z_OK;
}

// Creates a new empty string with pre-allocated capacity on the heap.
static inline zstr zstr_with_capacity(size_t cap)
{
    zstr s = zstr_init();
    if (cap > ZSTR_SSO_CAP)
    {
        zstr_reserve(&s, cap);
    }
    return s;
}

// Reduces heap usage to fit the exact string length (or moves back to SSO if small enough).
static inline void zstr_shrink_to_fit(zstr *s)
{
    if (!s->is_long) return;

    // Downgrade to SSO if possible.
    if (s->l.len <= ZSTR_SSO_CAP)
    {
        char temp[ZSTR_SSO_CAP];
        memcpy(temp, s->l.ptr, s->l.len);
        temp[s->l.len] = '\0';

        uint8_t old_len = (uint8_t)s->l.len;
        Z_STR_FREE(s->l.ptr);

        s->is_long = 0;
        memcpy(s->s.buf, temp, old_len + 1);
        s->s.len = old_len;
        return;
    }

    if (s->l.len < s->l.cap)
    {
        char *new_ptr = Z_STR_REALLOC(s->l.ptr, s->l.len + 1);
        if (new_ptr)
        {
            s->l.ptr = new_ptr;
            s->l.cap = s->l.len;
        }
    }
}


/* Construction Helpers */

// Helper: Creates zstr from ptr + explicit len.
static inline zstr zstr_from_len(const char *ptr, size_t len)
{
    zstr s = zstr_init();
    if (len >= ZSTR_SSO_CAP)
    {
        if (zstr_reserve(&s, len) != Z_OK) return s;
        memcpy(s.l.ptr, ptr, len);
        s.l.ptr[len] = '\0';
        s.l.len = len;
    }
    else 
    {
        memcpy(s.s.buf, ptr, len);
        s.s.buf[len] = '\0';
        s.s.len = (uint8_t)len;
        s.is_long = 0;
    }
    return s;
}

// Creates a zstr from a standard C-string.
static inline zstr zstr_from(const char *cstr)
{
    return zstr_from_len(cstr, strlen(cstr));
}

// Macro for compile-time string literals (avoids runtime strlen).
#define zstr_lit(s) zstr_from_len((s), sizeof(s) - 1)

// Creates a deep copy of a zstr.
static inline zstr zstr_dup(const zstr *s) 
{
    return zstr_from_len(zstr_cstr(s), zstr_len(s));
}

// Takes ownership of a malloc'd pointer.
static inline zstr zstr_own(char *ptr, size_t len, size_t cap)
{
    zstr s = zstr_init();
    
    if (cap <= ZSTR_SSO_CAP)
    {
        memcpy(s.s.buf, ptr, len);
        s.s.buf[len] = '\0';
        s.s.len = (uint8_t)len;
        s.is_long = 0;
        Z_STR_FREE(ptr);
    }
    else
    {
        s.is_long = 1;
        s.l.ptr = ptr;
        s.l.len = len;
        s.l.cap = cap;
    }
    return s;
}

// Releases ownership. Returns a malloc'd pointer the user MUST free.
static inline char* zstr_take(zstr *s)
{
    char *ptr;

    if (s->is_long)
    {
        ptr = s->l.ptr;
    }
    else
    {
        ptr = Z_STR_MALLOC(s->s.len + 1);
        if (ptr)
        {
            memcpy(ptr, s->s.buf, s->s.len);
            ptr[s->s.len] = '\0';
        }
    }
    
    // Reset the source struct so it doesn't double-free.
    *s = zstr_init();
    return ptr;
}


// Reads an entire file into a zstr. Returns empty on failure.
static inline zstr zstr_read_file(const char *path)
{
    zstr s = zstr_init();
    FILE *f = fopen(path, "rb"); 
    if (!f) return s;

    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    fseek(f, 0, SEEK_SET);

    // Pedantic check.
    if (length <= 0 || (sizeof(long) > sizeof(size_t) && (size_t)length > (size_t)-1))
    {
        fclose(f);
        return s;
    }

    if (zstr_reserve(&s, (size_t)length) != Z_OK) 
    {
        fclose(f);
        return s;
    }

    char *buf = zstr_data(&s);
    size_t read_count = fread(buf, 1, (size_t)length, f);
    buf[read_count] = '\0';

    if (s.is_long) s.l.len = read_count;
    else s.s.len = (uint8_t)read_count;

    fclose(f);
    return s;
}

// Appends a single character to the string.
static inline int zstr_push_char(zstr *s, char c)
{
    size_t len = zstr_len(s);
    if (len + 1 >= (s->is_long ? s->l.cap : ZSTR_SSO_CAP))
    {
        size_t cap = s->is_long ? s->l.cap : ZSTR_SSO_CAP;
        size_t new_cap = Z_GROWTH_FACTOR(cap);

        if (zstr_reserve(s, new_cap) != Z_OK) return Z_ERR;
    }

    char *p = zstr_data(s);
    p[len] = c;
    p[len + 1] = '\0';

    if (s->is_long) s->l.len++;
    else s->s.len++;

    return Z_OK;    
}

// Removes and returns the last character of the string.
static inline char zstr_pop_char(zstr *s)
{
    size_t len = zstr_len(s);
    if (len == 0) return '\0';
    
    char *p = zstr_data(s);
    char c = p[len - 1];
    p[len - 1] = '\0';
    
    if (s->is_long) s->l.len--;
    else s->s.len--;
    
    return c;
}

// Appends a raw char buffer of known length.
static inline int zstr_cat_len(zstr *s, const char *src, size_t src_len)
{
    size_t cur_len = zstr_len(s);
    size_t req_cap = cur_len + src_len;

    if (req_cap >= (s->is_long ? s->l.cap : ZSTR_SSO_CAP)) 
    {
        size_t new_cap = s->is_long ? s->l.cap : ZSTR_SSO_CAP;
        // Logic fixed: starting cap is 23. If we grow, we just multiply.
        // We do not fallback to 32 because 23 > 0.
        if (new_cap == 0) new_cap = ZSTR_SSO_CAP;
        
        while (new_cap <= req_cap) new_cap = Z_GROWTH_FACTOR(new_cap);
        
        if (zstr_reserve(s, new_cap) != Z_OK) return Z_ERR;
    }

    char *dest = zstr_data(s);
    memcpy(dest + cur_len, src, src_len);
    dest[cur_len + src_len] = '\0';

    if (s->is_long) s->l.len += src_len;
    else s->s.len += (uint8_t)src_len;

    return Z_OK;
}

// Appends a null-terminated C-string.
static inline int zstr_cat(zstr *s, const char *cstr)
{
    return zstr_cat_len(s, cstr, strlen(cstr));
}

// Joins an array of strings with a delimiter.
static inline zstr zstr_join(const char **strings, size_t count, const char *delim)
{
    zstr s = zstr_init();
    if (count == 0) return s;

    size_t delim_len = strlen(delim);
    size_t total_len = 0;

    for (size_t i = 0; i < count; i++)
    {
        total_len += strlen(strings[i]);
        if (i < count - 1) total_len += delim_len;
    }

    if (zstr_reserve(&s, total_len) != Z_OK) return s;
    
    for (size_t i = 0; i < count; i++)
    {
        zstr_cat(&s, strings[i]);
        if (i < count - 1) zstr_cat(&s, delim);
    }
    return s;
}

// Formats a string (printf style) and appends it.
ZSTR_PRINTF_ATTR(2, 3)
static inline int zstr_fmt(zstr *s, const char *fmt, ...) 
{
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(NULL, 0, fmt, args);
    va_end(args);

    if (len < 0) return Z_ERR;

    size_t cur_len = zstr_len(s);
    size_t req_cap = cur_len + len;
    
    if (req_cap >= (s->is_long ? s->l.cap : ZSTR_SSO_CAP)) 
    {
        if (zstr_reserve(s, req_cap) != Z_OK) return Z_ERR;
    }

    va_start(args, fmt);
    char *buf = zstr_data(s);
    vsnprintf(buf + cur_len, len + 1, fmt, args);
    va_end(args);

    if (s->is_long) s->l.len += len;
    else s->s.len += (uint8_t)len;

    return Z_OK;
}


/* In-Place Transformations */

// Converts the string to lowercase in-place (ASCII only).
static inline void zstr_to_lower(zstr *s)
{
    char *p = zstr_data(s);
    size_t len = zstr_len(s);
    for (size_t i = 0; i < len; i++)
    {
        p[i] = (char)tolower((unsigned char)p[i]);
    }
}

// Converts the string to uppercase in-place (ASCII only).
static inline void zstr_to_upper(zstr *s)
{
    char *p = zstr_data(s);
    size_t len = zstr_len(s);
    for (size_t i = 0; i < len; i++)
    {
        p[i] = (char)toupper((unsigned char)p[i]);
    }
}

// Removes leading and trailing whitespace in-place.
static inline void zstr_trim(zstr *s)
{
    if (zstr_len(s) == 0) return;

    char *start = zstr_data(s);
    char *end = start + zstr_len(s) - 1;

    while (end >= start && isspace((unsigned char)*end))
    {
        *end = '\0';
        end--;
    }

    size_t final_len = (end >= start) ? (size_t)(end - start + 1) : 0;
    
    char *new_start = start;
    while (new_start <= end && isspace((unsigned char)*new_start))
    {
        new_start++;
    }
    
    // Adjust final length based on left trim.
    final_len = (new_start <= end) ? (size_t)(end - new_start + 1) : 0;

    if (new_start > start)
    {
        memmove(start, new_start, final_len);
        start[final_len] = '\0';
    }

    if (s->is_long) s->l.len = final_len;
    else s->s.len = (uint8_t)final_len;
}

// Replaces all occurrences of "target" with "replacement". 
// This may reallocate the string if the size grows.
static inline int zstr_replace(zstr *s, const char *target, const char *replacement)
{
    if (!target || !*target) return Z_ERR;
    
    char *src = zstr_data(s);
    char *p = strstr(src, target);
    
    if (!p) return Z_OK;

    size_t target_len = strlen(target);
    size_t repl_len = strlen(replacement);
    size_t count = 0;

    char *scan = src;
    while ((scan = strstr(scan, target)) != NULL) 
    {
        count++;
        scan += target_len;
    }

    size_t old_len = zstr_len(s);
    size_t new_len = old_len + (count * (repl_len - target_len));

    zstr res = zstr_init();
    if (zstr_reserve(&res, new_len) != Z_OK) return Z_ERR; 

    char *dest = zstr_data(&res);
    char *curr_src = src;
    char *curr_dest = dest;
    
    while ((p = strstr(curr_src, target)) != NULL)
    {
        size_t segment_len = p - curr_src;
        memcpy(curr_dest, curr_src, segment_len);
        curr_dest += segment_len;
        
        memcpy(curr_dest, replacement, repl_len);
        curr_dest += repl_len;
        
        curr_src = p + target_len;
    }

    strcpy(curr_dest, curr_src);
    
    if (res.is_long) res.l.len = new_len;
    else res.s.len = (uint8_t)new_len;

    zstr_free(s);
    *s = res;

    return Z_OK;
}


/* Comparison */

// Checks equality between two zstr objects (faster than strcmp).
static inline bool zstr_eq(const zstr *a, const zstr *b)
{
    size_t la = zstr_len(a);
    size_t lb = zstr_len(b);
    if (la != lb) return false;
    return memcmp(zstr_cstr(a), zstr_cstr(b), la) == 0;
}

// Checks equality ignoring case (ASCII only).
static inline bool zstr_eq_ignore_case(const zstr *a, const zstr *b)
{
    size_t len = zstr_len(a);
    if (len != zstr_len(b)) return false;
    
    const char *p1 = zstr_cstr(a);
    const char *p2 = zstr_cstr(b);
    
    for (size_t i = 0; i < len; i++) {
        if (tolower((unsigned char)p1[i]) != tolower((unsigned char)p2[i])) {
            return false;
        }
    }
    return true;
}

// Standard strcmp behavior for zstr objects.
static inline int zstr_cmp(const zstr *a, const zstr *b)
{
    return strcmp(zstr_cstr(a), zstr_cstr(b));
}


/* Search */

// Returns the index of the first occurrence of needle, or -1 if not found.
static inline ptrdiff_t zstr_find(const zstr *s, const char *needle)
{
    const char *data = zstr_cstr(s);
    const char *found = strstr(data, needle);
    if (!found) return -1;
    return (ptrdiff_t)(found - data);
}

// Returns true if the string contains the substring.
static inline bool zstr_contains(const zstr *s, const char *needle)
{
    return zstr_find(s, needle) != -1;
}


/* UTF-8 Support */

// Decodes the next rune from the pointer and advances the pointer.
// Returns ZSTR_UTF8_INVALID (0xFFFD) on error.
static inline uint32_t zstr_next_rune(const char **p)
{
    const unsigned char *str = (const unsigned char *)*p;
    unsigned char c = *str;

    if (c == '\0') return 0;

    if (c < 0x80) 
    {
        *p += 1;
        return c;
    }

    uint32_t rune = 0;
    int len = 0;

    if ((c & 0xE0) == 0xC0) 
    {
        rune = c & 0x1F;
        len = 2;
    }
    else if ((c & 0xF0) == 0xE0) 
    {
        rune = c & 0x0F;
        len = 3;
    }
    else if ((c & 0xF8) == 0xF0) 
    {
        rune = c & 0x07;
        len = 4;
    }
    else 
    {
        *p += 1;
        return ZSTR_UTF8_INVALID;
    }

    for (int i = 1; i < len; i++) 
    {
        unsigned char next = str[i];
        if ((next & 0xC0) != 0x80) 
        {
            *p += 1;
            return ZSTR_UTF8_INVALID;
        }
        rune = (rune << 6) | (next & 0x3F);
    }

    *p += len;
    return rune;
}

// Counts the number of actual UTF-8 Runes, not bytes.
static inline size_t zstr_count_runes(const zstr *s)
{
    const char *ptr = zstr_cstr(s);
    size_t count = 0;
    while (*ptr)
    {
        zstr_next_rune(&ptr);
        count++;   
    }
    return count;
}

// Validates that the string is strictly valid UTF-8.
// Rejects Overlong encodings, Surrogates, and out-of-bounds values.
static inline bool zstr_is_valid_utf8(const zstr *s)
{
    const unsigned char *p = (const unsigned char *)zstr_cstr(s);
    while (*p)
    {
        if (*p < 0x80) 
        {
            p++; 
        } 
        else if ((*p & 0xE0) == 0xC0) // 2-byte.
        {
            if (*p < 0xC2) return false; // Overlong.
            p++;
            if ((*p & 0xC0) != 0x80) return false; 
            p++;
        } 
        else if ((*p & 0xF0) == 0xE0) // 3-byte.
        {
            unsigned char b1 = *p++;
            unsigned char b2 = *p++;
            if ((*p & 0xC0) != 0x80) return false; 
            
            if (b1 == 0xE0 && b2 < 0xA0) return false; // Overlong.
            if (b1 == 0xED && b2 >= 0xA0) return false; // Surrogate.
            if ((b2 & 0xC0) != 0x80) return false;
            p++;
        } 
        else if ((*p & 0xF8) == 0xF0) // 4-byte.
        {
            unsigned char b1 = *p++;
            if (b1 > 0xF4) return false; // > U+10FFFF.
            
            unsigned char b2 = *p++;
            if (b1 == 0xF0 && b2 < 0x90) return false; // Overlong.
            if (b1 == 0xF4 && b2 >= 0x90) return false; // > U+10FFFF.
            if ((b2 & 0xC0) != 0x80) return false;

            if ((*p++ & 0xC0) != 0x80) return false; 
            if ((*p++ & 0xC0) != 0x80) return false; 
        } 
        else 
        {
            return false;
        }
    }
    return true;
}


/* Views and Slices (Zero-Copy) */

// Helper macro to create a view from a string literal.
#define ZSV(lit) (zstr_view){ .data = (lit), .len = sizeof(lit) -1 }

// Creates a view from a C-string.
static inline zstr_view zstr_view_from(const char *cstr)
{
    return (zstr_view){ .data = cstr, .len = strlen(cstr) };
}

// Creates a view covering the entire zstr.
static inline zstr_view zstr_as_view(const zstr *s)
{
    return (zstr_view){ .data = zstr_cstr(s), .len = zstr_len(s) };
}

// Converts a view back into an owning zstr (allocates).
static inline zstr zstr_from_view(zstr_view v) 
{
    return zstr_from_len(v.data, v.len);
}

// Returns a substring view.
static inline zstr_view zstr_sub(zstr_view v, size_t start, size_t len)
{
    if (start >= v.len) return (zstr_view){ "", 0 };
    if (start + len > v.len) len = v.len - start;
    return (zstr_view){ .data = v.data + start, .len = len };
}

// Checks if view equals a C-string.
static inline bool zstr_view_eq(zstr_view v, const char *cstr)
{
    if (strlen(cstr) != v.len) return false;
    return memcmp(v.data, cstr, v.len) == 0;
}

// Checks if two views are equal.
static inline bool zstr_view_eq_view(zstr_view a, zstr_view b) 
{
    if (a.len != b.len) return false;
    return memcmp(a.data, b.data, a.len) == 0;
}

// Checks if view starts with prefix.
static inline bool zstr_view_starts_with(zstr_view v, const char *prefix) 
{
    size_t pre_len = strlen(prefix);
    if (pre_len > v.len) return false;
    return memcmp(v.data, prefix, pre_len) == 0;
}

// Checks if view ends with suffix.
static inline bool zstr_view_ends_with(zstr_view v, const char *suffix) 
{
    size_t suf_len = strlen(suffix);
    if (suf_len > v.len) return false;
    return memcmp(v.data + v.len - suf_len, suffix, suf_len) == 0;
}

// Wrapper for checking if an owning zstr starts with prefix.
static inline bool zstr_starts_with(const zstr *s, const char *prefix) 
{
    return zstr_view_starts_with(zstr_as_view(s), prefix);
}

// Wrapper for checking if an owning zstr ends with suffix.
static inline bool zstr_ends_with(const zstr *s, const char *suffix) 
{
    return zstr_view_ends_with(zstr_as_view(s), suffix);
}


// Trims whitespace from the start of the view.
static inline zstr_view zstr_view_lstrip(zstr_view v) 
{
    const char *start = v.data;
    const char *end = v.data + v.len;
    while (start < end && isspace((unsigned char)*start)) start++;
    return (zstr_view){ .data = start, .len = (size_t)(end - start) };
}

// Trims whitespace from the end of the view.
static inline zstr_view zstr_view_rstrip(zstr_view v) 
{
    const char *start = v.data;
    const char *end = v.data + v.len;
    while (end > start && isspace((unsigned char)*(end - 1))) end--;
    return (zstr_view){ .data = start, .len = (size_t)(end - start) };
}

// Trims whitespace from both ends.
static inline zstr_view zstr_view_trim(zstr_view v) 
{
    return zstr_view_lstrip(zstr_view_rstrip(v));
}

// Converts a view to an integer (simple atoi replacement).
// Returns true if successful, false if empty or invalid chars found.
static inline bool zstr_view_to_int(zstr_view v, int *out)
{
    if (v.len == 0) return false;
    
    int sign = 1;
    size_t i = 0;
    
    if (v.data[0] == '-') 
    { 
        sign = -1; 
        i++; 
    }
    else if (v.data[0] == '+')
    {
        i++;
    }
    
    if (i == v.len) return false;

    int result = 0;
    for (; i < v.len; i++) 
    {
        if (v.data[i] < '0' || v.data[i] > '9') return false; 
        result = result * 10 + (v.data[i] - '0');
    }
    
    *out = result * sign;
    return true;
}

// Initializes an iterator for splitting a string.
static inline zstr_split_iter zstr_split_init(zstr_view src, const char *delim) 
{
    return (zstr_split_iter){
        .source = src,
        .delim = zstr_view_from(delim),
        .current_pos = 0,
        .finished = false
    };
}

// Gets the next part in a split iteration. Returns false when done.
static inline bool zstr_split_next(zstr_split_iter *it, zstr_view *out_part) 
{
    if (it->finished) return false;

    const char *start = it->source.data + it->current_pos;
    size_t remaining = it->source.len - it->current_pos;
    
    size_t found_at = remaining;
    
    for (size_t i = 0; i <= remaining - it->delim.len; i++) 
    {
        if (memcmp(start + i, it->delim.data, it->delim.len) == 0) 
        {
            found_at = i;
            break;
        }
    }

    if (found_at == remaining) 
    {
        *out_part = (zstr_view){ .data = start, .len = remaining };
        it->finished = true;
    }
    else 
    {
        *out_part = (zstr_view){ .data = start, .len = found_at };
        it->current_pos += found_at + it->delim.len;
    }
    
    return true;
}

#if defined(Z_HAS_CLEANUP) && Z_HAS_CLEANUP
    #define zstr_autofree  Z_CLEANUP(zstr_free) zstr
#endif

#ifdef __cplusplus
} // extern "C"
#endif

/* C++ Integration Layer -> namespace: zstr */

#ifdef __cplusplus

#include <utility>
#include <iostream>
#include <cstring>
#include <string>

#if __cplusplus >= 201703L
#include <string_view>
#endif

namespace z_str
{
    class string;

    class view
    {
        ::zstr_view inner;

     public:
        // Iterator traits for view.
        using value_type      = char;
        using size_type       = size_t;
        using difference_type = std::ptrdiff_t;
        using const_pointer   = const char*;
        using const_iterator  = const char*;

        view() : inner{NULL, 0} {}
        view(const char *s) : inner(::zstr_view_from(s)) {}
        view(const char *s, size_t len) : inner{s, len}  {}
        view(const string &s);
        view(const string &&) = delete;

        const char *data() const { return inner.data; }
        size_t size() const      { return inner.len; }
        size_t length() const    { return inner.len; }
        bool empty() const       { return inner.len == 0; }

        const char *begin() const { return inner.data; }
        const char *end() const   { return inner.data + inner.len; }

        char operator[](size_t idx) const { return inner.data[idx]; }

#       if __cplusplus >= 201703L
        operator std::string_view() const { return std::string_view(data(), size()); }
#       endif

        // Comparisons.
        bool operator==(const char* other) const { return ::zstr_view_eq(inner, other); }
        bool operator==(const view& other) const { return ::zstr_view_eq_view(inner, other.inner); }
        bool operator!=(const char* other) const { return !(*this == other); }
        bool operator!=(const view& other) const { return !(*this == other); }
    };

    class string
    {
        ::zstr inner;
        friend class view;

        friend bool operator==(const string& lhs, const string& rhs);
        friend bool operator!=(const string& lhs, const string& rhs);
        friend bool operator<(const string& lhs, const string& rhs);

     public:
        // Iterator traits.
        using value_type      = char;
        using size_type       = size_t;
        using difference_type = std::ptrdiff_t;
        using pointer         = char*;
        using const_pointer   = const char*;
        using iterator        = char*;
        using const_iterator  = const char*;

        // Default constructor.
        string() : inner(::zstr_init()) {}

        // C-string constructor.
        string(const char *s) : inner(::zstr_from(s)) {}

        // Length constructor.
        string(const char *s, size_t len) : inner(::zstr_from_len(s, len)) {}

        // This one is for C++17 so we put it like this.
#       if __cplusplus >= 201703L
        string(std::string_view sv) : inner(::zstr_from_len(sv.data(), sv.size())) {}
#       endif

        // Copy constructor.
        string(const string &other) : inner(::zstr_dup(&other.inner)) {}

        // Move constructor (zero cost).
        string(string &&other) noexcept : inner(other.inner) 
        {
        other.inner = ::zstr_init();
        }

        // Destructor.
        ~string() { ::zstr_free(&inner); }

        // Copy assignment.
        string& operator=(const string &other)
        {
            if (this != &other)
            {
                ::zstr_free(&inner);
                inner = ::zstr_dup(&other.inner);
            }
            return *this;
        }

        // Move assignment (transfer ownership).
        string& operator=(string &&other) noexcept
        {
            if (this != &other)
            {
                ::zstr_free(&inner);
                inner = other.inner;
                other.inner = ::zstr_init();
            }
            return *this;
        }

        // Assignment from C-string.
        string& operator=(const char *s)
        {
            ::zstr_free(&inner);
            inner = ::zstr_from(s);
            return *this;
        }

        // Accessors.
        const char *c_str() const { return ::zstr_cstr(&inner); }
        const char *data() const  { return ::zstr_cstr(&inner); }
        char *data()              { return ::zstr_data(&inner); }
        size_t size() const       { return ::zstr_len(&inner); }
        size_t length() const     { return ::zstr_len(&inner); }
        size_t capacity() const   { return inner.is_long ? inner.l.cap : ZSTR_SSO_CAP; }
        bool is_empty() const     { return ::zstr_is_empty(&inner); }

#       if __cplusplus >= 201703L
        operator std::string_view() const { return std::string_view(data(), size()); }
#       endif

        // Iterators.
        char *begin()             { return ::zstr_data(&inner); }
        char *end()               { return ::zstr_data(&inner) + size(); }
        const char *begin() const { return ::zstr_cstr(&inner); }
        const char *end() const   { return ::zstr_cstr(&inner) + size(); }

        // Modifiers.
        void clear()             { ::zstr_clear(&inner); }
        void reserve(size_t cap) { ::zstr_reserve(&inner, cap); }
        void shrink_to_fit()     { ::zstr_shrink_to_fit(&inner); }
        
        void push_back(char c) { ::zstr_push_char(&inner, c); }
        void pop_back()        { ::zstr_pop_char(&inner); }
        
        string& append(const char *s)             { ::zstr_cat(&inner, s); return *this; }
        string& append(const char *s, size_t len) { ::zstr_cat_len(&inner, s, len); return *this; }

        // Operators.
        string& operator+=(const char *s)       { return append(s); }
        string& operator+=(char c)              { push_back(c); return *this; }
        string& operator+=(const string &other) { return append(other.c_str(), other.size()); }

        char& operator[](size_t idx)             { return ::zstr_data(&inner)[idx]; }
        const char& operator[](size_t idx) const { return ::zstr_cstr(&inner)[idx]; }

        // Some utilities.
        void to_lower() { ::zstr_to_lower(&inner); }
        void to_upper() { ::zstr_to_upper(&inner); }
        void trim()     { ::zstr_trim(&inner); }

        // Formating.
        // WARNING: Only POD types (int, double, char*) are safe here.
        // Passing std::string or objects will crash.
        template <typename... Args>
        static string fmt(const char *format, Args... args)
        {
            string s;
            ::zstr_fmt(&s.inner, format, args...);
            return s;
        }
    };

    // View constructor implementation.
    inline view::view(const string &s) : inner(::zstr_as_view(&s.inner)) {}

    // The global operators...
    inline std::ostream& operator<<(std::ostream &os, const string &s)
    {
        return os.write(s.data(), s.size());
    }

    inline std::ostream& operator<<(std::ostream &os, const view &s)
    {
        return os.write(s.data(), s.size());
    }

    // Comparison Operators (string vs string).
    inline bool operator==(const string& lhs, const string& rhs) { return ::zstr_eq(&lhs.inner, &rhs.inner); }
    inline bool operator!=(const string& lhs, const string& rhs) { return !::zstr_eq(&lhs.inner, &rhs.inner); }
    inline bool operator<(const string& lhs, const string& rhs)  { return ::zstr_cmp(&lhs.inner, &rhs.inner) < 0; }
   
    // Comparison Operators (string vs const char*).
    inline bool operator==(const string& lhs, const char* rhs) { return strcmp(lhs.c_str(), rhs) == 0; }
    inline bool operator==(const char* lhs, const string& rhs) { return strcmp(lhs, rhs.c_str()) == 0; }
    inline bool operator!=(const string& lhs, const char* rhs) { return strcmp(lhs.c_str(), rhs) != 0; }
    inline bool operator!=(const char* lhs, const string& rhs) { return strcmp(lhs, rhs.c_str()) != 0; }
}

#endif  // __cplusplus

#endif  // ZSTR_H
