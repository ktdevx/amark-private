/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#include "array.h"

#include <string.h>

#include "alloc.h"

/* The capacity of the first growth. */
#define AMARK_ARRAY_MIN_CAP 8

void amark_array_init(amark_array_t *array, amark_arena_t *arena,
                      size_t elem_size)
{
    array->arena = arena;
    array->data = NULL;
    array->len = 0;
    array->cap = 0;
    array->elem_size = elem_size;
}

amark_error_t amark_array_reserve(amark_array_t *array, size_t min_cap)
{
    size_t new_cap;
    size_t bytes;
    void *data = NULL;
    amark_error_t err;

    if (min_cap <= array->cap) {
        return AMARK_ERROR_NONE;
    }

    /* new_cap = max(8, cap * 2, min_cap) */
    if (!amark_size_mul(array->cap, 2, &new_cap)) {
        return AMARK_ERROR_LIMIT_EXCEEDED;
    }
    if (new_cap < AMARK_ARRAY_MIN_CAP) {
        new_cap = AMARK_ARRAY_MIN_CAP;
    }
    if (new_cap < min_cap) {
        new_cap = min_cap;
    }
    if (!amark_size_mul(new_cap, array->elem_size, &bytes)) {
        return AMARK_ERROR_LIMIT_EXCEEDED;
    }

    err = amark_arena_alloc(array->arena, bytes, &data);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    if (array->len > 0) {
        /* len * elem_size <= cap * elem_size, which fit when cap was set. */
        memcpy(data, array->data, array->len * array->elem_size);
    }
    /* The old place stays in the arena until the arena is released. */
    array->data = data;
    array->cap = new_cap;
    return AMARK_ERROR_NONE;
}

amark_error_t amark_array_push(amark_array_t *array, const void *elem)
{
    if (array->len == array->cap) {
        size_t min_cap;
        amark_error_t err;

        if (!amark_size_add(array->len, 1, &min_cap)) {
            return AMARK_ERROR_LIMIT_EXCEEDED;
        }
        err = amark_array_reserve(array, min_cap);
        if (err != AMARK_ERROR_NONE) {
            return err;
        }
    }
    memcpy((unsigned char *)array->data + array->len * array->elem_size, elem,
           array->elem_size);
    array->len++;
    return AMARK_ERROR_NONE;
}

size_t amark_array_len(const amark_array_t *array)
{
    return array->len;
}

void *amark_array_at(const amark_array_t *array, size_t index)
{
    return (unsigned char *)array->data + index * array->elem_size;
}

bool amark_array_pop(amark_array_t *array, void *out)
{
    if (array->len == 0) {
        return false;
    }
    array->len--;
    if (out != NULL) {
        memcpy(out, amark_array_at(array, array->len), array->elem_size);
    }
    return true;
}

void amark_array_clear(amark_array_t *array)
{
    array->len = 0;
}
