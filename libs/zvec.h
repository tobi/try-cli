/*
 * GENERATED FILE - DO NOT EDIT DIRECTLY
 * Source: zvec.c
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

#ifndef ZVEC_H
#define ZVEC_H
// [Bundled] "zcommon.h" is included inline in this same file
#include <string.h>
#include <assert.h>

#ifndef Z_VEC_MALLOC
    #define Z_VEC_MALLOC(sz)      Z_MALLOC(sz)
#endif

#ifndef Z_VEC_CALLOC
    #define Z_VEC_CALLOC(n, sz)   Z_CALLOC(n, sz)
#endif

#ifndef Z_VEC_REALLOC
    #define Z_VEC_REALLOC(p, sz)  Z_REALLOC(p, sz)
#endif

#ifndef Z_VEC_FREE
    #define Z_VEC_FREE(p)         Z_FREE(p)
#endif

#define Z_VEC_GENERATE_IMPL(T, Name)                                                        \
                                                                                            \
typedef T zvec_T_##Name;                                                                    \
                                                                                            \
typedef struct {                                                                            \
    T *data;                                                                                \
    size_t length;                                                                          \
    size_t capacity;                                                                        \
} vec_##Name;                                                                               \
                                                                                            \
static inline vec_##Name vec_init_capacity_##Name(size_t cap) {                             \
    vec_##Name v = {0};                                                                     \
    if (cap > 0) {                                                                          \
        v.data = Z_VEC_CALLOC(cap, sizeof(T));                                              \
        v.capacity = v.data ? cap : 0;                                                      \
    }                                                                                       \
    return v;                                                                               \
}                                                                                           \
                                                                                            \
static inline vec_##Name vec_from_array_##Name(const T *arr, size_t count) {                \
    vec_##Name v = vec_init_capacity_##Name(count);                                         \
    if (v.data) {                                                                           \
        memcpy(v.data, arr, count * sizeof(T));                                             \
        v.length = count;                                                                   \
    }                                                                                       \
    return v;                                                                               \
}                                                                                           \
                                                                                            \
static inline int vec_reserve_##Name(vec_##Name *v, size_t new_cap) {                       \
    if (new_cap <= v->capacity) return Z_OK;                                                \
    T *new_data = Z_VEC_REALLOC(v->data, new_cap * sizeof(T));                              \
    if (!new_data) return Z_ERR;                                                            \
    v->data = new_data;                                                                     \
    v->capacity = new_cap;                                                                  \
    return Z_OK;                                                                            \
}                                                                                           \
                                                                                            \
static inline int vec_is_empty_##Name(vec_##Name *v) {                                      \
    return v->length == 0;                                                                  \
}                                                                                           \
                                                                                            \
static inline T* vec_push_slot_##Name(vec_##Name *v) {                                      \
    if (v->length >= v->capacity) {                                                         \
        size_t new_cap = Z_GROWTH_FACTOR(v->capacity);                                      \
        if (vec_reserve_##Name(v, new_cap) != Z_OK) return NULL;                            \
    }                                                                                       \
    return &v->data[v->length++];                                                           \
}                                                                                           \
                                                                                            \
static inline int vec_push_##Name(vec_##Name *v, T value) {                                 \
    T *slot = vec_push_slot_##Name(v);                                                      \
    if (!slot) return Z_ERR;                                                                \
    *slot = value;                                                                          \
    return Z_OK;                                                                            \
}                                                                                           \
                                                                                            \
static inline int vec_extend_##Name(vec_##Name *v, const T *items, size_t count) {          \
    if (v->length + count > v->capacity) {                                                  \
        size_t new_cap = v->capacity;                                                       \
        if (new_cap == 0) new_cap = Z_GROWTH_FACTOR(0);                                     \
        while (new_cap < v->length + count) new_cap = Z_GROWTH_FACTOR(new_cap);             \
        if (vec_reserve_##Name(v, new_cap) != Z_OK) return Z_ERR;                           \
    }                                                                                       \
    memcpy(v->data + v->length, items, count * sizeof(T));                                  \
    v->length += count;                                                                     \
    return Z_OK;                                                                            \
}                                                                                           \
                                                                                            \
static inline void vec_pop_##Name(vec_##Name *v) {                                          \
    assert(v->length > 0 && "Popping empty vector");                                        \
    v->length--;                                                                            \
}                                                                                           \
                                                                                            \
static inline T vec_pop_get_##Name(vec_##Name *v) {                                         \
    assert(v->length > 0 && "Vector is empty, cannot pop!");                                \
    return v->data[--v->length];                                                            \
}                                                                                           \
static inline void vec_shrink_to_fit_##Name(vec_##Name *v) {                                \
    if (v->length == 0) {                                                                   \
        Z_VEC_FREE(v->data);                                                                \
        *v = (vec_##Name){0};                                                               \
        return;                                                                             \
    }                                                                                       \
    if (v->length == v->capacity) return;                                                   \
    T *new_data = Z_VEC_REALLOC(v->data, v->length * sizeof(T));                            \
    if (!new_data) return;                                                                  \
    v->data = new_data;                                                                     \
    v->capacity = v->length;                                                                \
}                                                                                           \
                                                                                            \
static inline T* vec_at_##Name(vec_##Name *v, size_t index) {                               \
    return (index < v->length) ? &v->data[index] : NULL;                                    \
}                                                                                           \
                                                                                            \
static inline T* vec_data_##Name(vec_##Name *v) {                                           \
    return v->data;                                                                         \
}                                                                                           \
                                                                                            \
static inline T* vec_last_##Name(vec_##Name *v) {                                           \
    return (v->length > 0) ? &v->data[v->length - 1] : NULL;                                \
}                                                                                           \
                                                                                            \
static inline void vec_remove_##Name(vec_##Name *v, size_t index) {                         \
    if (index >= v->length) return;                                                         \
    memmove(&v->data[index], &v->data[index + 1],                                           \
            (v->length - index - 1) * sizeof(T));                                           \
    v->length--;                                                                            \
}                                                                                           \
                                                                                            \
static inline void vec_swap_remove_##Name(vec_##Name *v, size_t index) {                    \
    if (index >= v->length) return;                                                         \
    v->data[index] = v->data[--v->length];                                                  \
}                                                                                           \
                                                                                            \
static inline void vec_clear_##Name(vec_##Name *v) {                                        \
    v->length = 0;                                                                          \
}                                                                                           \
                                                                                            \
static inline void vec_free_##Name(vec_##Name *v) {                                         \
    Z_VEC_FREE(v->data);                                                                    \
    *v = (vec_##Name){0};                                                                   \
}                                                                                           \
                                                                                            \
static inline void vec_reverse_##Name(vec_##Name *v) {                                      \
    if (v->length < 2) return;                                                              \
    size_t i = 0, j = v->length - 1;                                                        \
    while (i < j) {                                                                         \
        T temp = v->data[i];                                                                \
        v->data[i] = v->data[j];                                                            \
        v->data[j] = temp;                                                                  \
        i++; j--;                                                                           \
    }                                                                                       \
}                                                                                           \
                                                                                            \
static inline void vec_sort_##Name(vec_##Name *v, int (*compar)(const T *, const T *)) {    \
    if (v->length > 1) {                                                                    \
        int (*qsort_cmp)(const void *, const void *) =                                      \
            (int (*)(const void *, const void *))compar;                                    \
        qsort(v->data, v->length, sizeof(T), qsort_cmp);                                    \
    }                                                                                       \
}                                                                                           \
                                                                                            \
static inline T* vec_bsearch_##Name(vec_##Name *v, const T *key,                            \
                                    int (*compar)(const T *, const T *)) {                  \
    if (v->length == 0) return NULL;                                                        \
    int (*bs_cmp)(const void *, const void *) =                                             \
        (int (*)(const void *, const void *))compar;                                        \
    return (T*) bsearch(key, v->data, v->length, sizeof(T), bs_cmp);                        \
}                                                                                           \
                                                                                            \
static inline T* vec_lower_bound_##Name(vec_##Name *v, const T *key,                        \
                                        int (*compar)(const T *, const T *)) {              \
    size_t l = 0;                                                                           \
    size_t r = v->length;                                                                   \
    while (l < r) {                                                                         \
        size_t mid = l + (r - l) / 2;                                                       \
        if (compar((const T *)&v->data[mid], key) < 0) {                                    \
            l = mid + 1;                                                                    \
        } else {                                                                            \
            r = mid;                                                                        \
        }                                                                                   \
    }                                                                                       \
    if (l == v->length) return NULL;                                                        \
    return &v->data[l];                                                                     \
}

#define PUSH_ENTRY(T, Name)     vec_##Name*: vec_push_##Name,
#define PUSH_SLOT_ENTRY(T, Name) vec_##Name*: vec_push_slot_##Name,
#define EXTEND_ENTRY(T, Name)   vec_##Name*: vec_extend_##Name,
#define RESERVE_ENTRY(T, Name)  vec_##Name*: vec_reserve_##Name,
#define IS_EMPTY_ENTRY(T, Name) vec_##Name*: vec_is_empty_##Name,
#define AT_ENTRY(T, Name)       vec_##Name*: vec_at_##Name,
#define DATA_ENTRY(T, Name)     vec_##Name*: vec_data_##Name,
#define LAST_ENTRY(T, Name)     vec_##Name*: vec_last_##Name,
#define FREE_ENTRY(T, Name)     vec_##Name*: vec_free_##Name,
#define POP_ENTRY(T, Name)      vec_##Name*: vec_pop_##Name,
#define POP_GET_ENTRY(T, Name)  vec_##Name*: vec_pop_get_##Name,
#define SHRINK_ENTRY(T, Name)   vec_##Name*: vec_shrink_to_fit_##Name,
#define REMOVE_ENTRY(T, Name)   vec_##Name*: vec_remove_##Name,
#define SWAP_REM_ENTRY(T, Name) vec_##Name*: vec_swap_remove_##Name,
#define CLEAR_ENTRY(T, Name)    vec_##Name*: vec_clear_##Name,
#define REVERSE_ENTRY(T, Name)  vec_##Name*: vec_reverse_##Name,
#define SORT_ENTRY(T, Name)     vec_##Name*: vec_sort_##Name,
#define BSEARCH_ENTRY(T, Name)  vec_##Name*: vec_bsearch_##Name,
#define LOWER_BOUND_ENTRY(T, Name) vec_##Name*: vec_lower_bound_##Name,

#if defined(__has_include) && __has_include("z_registry.h")
    #include "z_registry.h"
#endif

#ifndef Z_AUTOGEN_VECS
    #define Z_AUTOGEN_VECS(X)
#endif

#ifndef REGISTER_TYPES
    #define REGISTER_TYPES(X)
#endif

#define Z_ALL_VECS(X) \
    Z_AUTOGEN_VECS(X) \
    REGISTER_TYPES(X)

Z_ALL_VECS(Z_VEC_GENERATE_IMPL)

#if Z_HAS_CLEANUP
    #define vec_autofree(Name)  Z_CLEANUP(vec_free_##Name) vec_##Name
#endif

#define vec_from(Name, ...) \
    vec_from_array_##Name((zvec_T_##Name[])__VA_ARGS__, sizeof((zvec_T_##Name[])__VA_ARGS__) / sizeof(zvec_T_##Name))

#define vec_init(Name) {0}
#define vec_init_with_cap(Name, cap) vec_init_capacity_##Name(cap)

#define VEC_CAT(a, b) a##b
#define VEC_NAME(a, b) VEC_CAT(a, b)

#define vec_foreach(v, iter) \
    for (size_t VEC_NAME(_i_, __LINE__) = 0; \
         VEC_NAME(_i_, __LINE__) < (v)->length && ((iter) = &(v)->data[VEC_NAME(_i_, __LINE__)]); \
         ++VEC_NAME(_i_, __LINE__))

#define vec_push(v, val)          _Generic((v), Z_ALL_VECS(PUSH_ENTRY)      default: 0)       (v, val)
#define vec_push_slot(v)          _Generic((v), Z_ALL_VECS(PUSH_SLOT_ENTRY) default: (void*)0)(v)
#define vec_extend(v, arr, count) _Generic((v), Z_ALL_VECS(EXTEND_ENTRY)    default: 0)       (v, arr, count)
#define vec_reserve(v, cap)       _Generic((v), Z_ALL_VECS(RESERVE_ENTRY)   default: 0)       (v, cap)
#define vec_is_empty(v)           _Generic((v), Z_ALL_VECS(IS_EMPTY_ENTRY)  default: 0)       (v)
#define vec_at(v, idx)            _Generic((v), Z_ALL_VECS(AT_ENTRY)        default: (void*)0)(v, idx)
#define vec_data(v)               _Generic((v), Z_ALL_VECS(DATA_ENTRY)      default: (void*)0)(v)
#define vec_last(v)               _Generic((v), Z_ALL_VECS(LAST_ENTRY)      default: (void*)0)(v)
#define vec_free(v)               _Generic((v), Z_ALL_VECS(FREE_ENTRY)      default: (void)0) (v)
#define vec_pop(v)                _Generic((v), Z_ALL_VECS(POP_ENTRY)       default: (void)0) (v)
#define vec_pop_get(v)            _Generic((v), Z_ALL_VECS(POP_GET_ENTRY)   default: (void)0) (v)
#define vec_shrink_to_fit(v)      _Generic((v), Z_ALL_VECS(SHRINK_ENTRY)    default: (void)0) (v)
#define vec_remove(v, i)          _Generic((v), Z_ALL_VECS(REMOVE_ENTRY)    default: (void)0) (v, i)
#define vec_swap_remove(v, i)     _Generic((v), Z_ALL_VECS(SWAP_REM_ENTRY)  default: (void)0) (v, i)
#define vec_clear(v)              _Generic((v), Z_ALL_VECS(CLEAR_ENTRY)     default: (void)0) (v)
#define vec_reverse(v)            _Generic((v), Z_ALL_VECS(REVERSE_ENTRY)   default: (void)0) (v)
#define vec_sort(v, cmp)          _Generic((v), Z_ALL_VECS(SORT_ENTRY)      default: (void)0) (v, cmp)
#define vec_bsearch(v, k, c)      _Generic((v), Z_ALL_VECS(BSEARCH_ENTRY)     default: (void*)0)(v, k, c)
#define vec_lower_bound(v, k, c)  _Generic((v), Z_ALL_VECS(LOWER_BOUND_ENTRY) default: (void*)0)(v, k, c)

#endif
