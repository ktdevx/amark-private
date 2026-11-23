/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_ARRAY_H
#define AMARK_ARRAY_H

#include <stdbool.h>
#include <stddef.h>

#include "arena.h"
#include "error.h"

/*
 * A growable array of elements of one size, stored in an arena. It also
 * serves as a stack (push and pop at the end).
 *
 * When the capacity is too small, the array takes a new place from the arena
 * with the capacity max(8, cap * 2, min_cap) and copies the elements there.
 * The old place stays in the arena until the arena is released; there is no
 * release of the array itself.
 */

/*
 * The members are visible in src/ but other modules use the functions only.
 */
typedef struct amark_array {
    amark_arena_t *arena;
    void *data; /* aligned to AMARK_ARENA_ALIGN, or NULL while cap is 0 */
    size_t len;
    size_t cap;
    size_t elem_size;
} amark_array_t;

/*
 * Initializes an empty array whose elements are elem_size bytes and whose
 * memory comes from arena. Allocates nothing. elem_size must be at least 1.
 */
void amark_array_init(amark_array_t *array, amark_arena_t *arena,
                      size_t elem_size);

/*
 * Copies elem_size bytes from elem to the end of the array, growing the
 * capacity if needed. On failure, returns AMARK_ERROR_NOMEM (the arena could
 * not allocate) or AMARK_ERROR_OVERFLOW (the new capacity in bytes does not
 * fit in size_t; nothing is allocated), and the data, length and capacity
 * are unchanged. A growth moves the elements, so pointers returned by
 * amark_array_at before it are no longer valid.
 */
amark_error_t amark_array_push(amark_array_t *array, const void *elem);

/*
 * Makes the capacity at least min_cap, as amark_array_push does when it
 * grows. The length is unchanged. Fails as amark_array_push does.
 */
amark_error_t amark_array_reserve(amark_array_t *array, size_t min_cap);

/* Returns the number of elements. */
size_t amark_array_len(const amark_array_t *array);

/*
 * Returns a pointer to the element at index, which must be less than the
 * length. The element can be read and written through it.
 */
void *amark_array_at(const amark_array_t *array, size_t index);

/*
 * If the array is empty, returns false. Otherwise copies the last element to
 * *out (unless out is NULL), removes it and returns true.
 */
bool amark_array_pop(amark_array_t *array, void *out);

#endif /* AMARK_ARRAY_H */
