/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/* The allocator types of Amark, shared by the public API and the internals. */

#ifndef AMARK_ALLOCATOR_H
#define AMARK_ALLOCATOR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns a block of at least size bytes, or NULL on failure. */
typedef void *(*amark_malloc_fn_t)(void *ctx, size_t size);

/* Releases a block returned by the matching malloc_fn. ptr is not NULL. */
typedef void (*amark_free_fn_t)(void *ctx, void *ptr);

/*
 * A pair of functions and the context value passed unchanged to each call.
 * The members end in _fn because some C runtimes define malloc and free as
 * macros. Keep a copy by value and release with the same value used to
 * allocate.
 */
typedef struct amark_allocator {
    amark_malloc_fn_t malloc_fn;
    amark_free_fn_t free_fn;
    void *ctx;
} amark_allocator_t;

#ifdef __cplusplus
}
#endif

#endif /* AMARK_ALLOCATOR_H */
