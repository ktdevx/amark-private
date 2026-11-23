/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#include "alloc.h"

#include <stdint.h>
#include <stdlib.h>

/* The only calls to the standard malloc and free in the library. */
static void *amark_default_malloc(void *ctx, size_t size)
{
    (void)ctx;
    return malloc(size);
}

static void amark_default_free(void *ctx, void *ptr)
{
    (void)ctx;
    free(ptr);
}

amark_allocator_t amark_allocator_default(void)
{
    amark_allocator_t allocator;

    allocator.malloc_fn = amark_default_malloc;
    allocator.free_fn = amark_default_free;
    allocator.ctx = NULL;
    return allocator;
}

amark_allocator_t amark_allocator_resolve(const amark_allocator_t *requested)
{
    if (requested == NULL) {
        return amark_allocator_default();
    }
    return *requested;
}

amark_error_t amark_malloc(const amark_allocator_t *allocator, size_t size,
                           void **out)
{
    void *ptr;

    if (size == 0) {
        size = 1;
    }
    ptr = allocator->malloc_fn(allocator->ctx, size);
    *out = ptr;
    if (ptr == NULL) {
        return AMARK_ERROR_NOMEM;
    }
    return AMARK_ERROR_NONE;
}

amark_error_t amark_malloc_array(const amark_allocator_t *allocator,
                                 size_t count, size_t elem_size, void **out)
{
    size_t size;

    if (!amark_size_mul(count, elem_size, &size)) {
        *out = NULL;
        return AMARK_ERROR_LIMIT_EXCEEDED;
    }
    return amark_malloc(allocator, size, out);
}

void amark_free(const amark_allocator_t *allocator, void *ptr)
{
    if (ptr == NULL) {
        return;
    }
    allocator->free_fn(allocator->ctx, ptr);
}

bool amark_size_add(size_t a, size_t b, size_t *out)
{
    if (a > SIZE_MAX - b) {
        return false;
    }
    *out = a + b;
    return true;
}

bool amark_size_mul(size_t a, size_t b, size_t *out)
{
    if (a != 0 && b > SIZE_MAX / a) {
        return false;
    }
    *out = a * b;
    return true;
}
