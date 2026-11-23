/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_ALLOC_H
#define AMARK_ALLOC_H

#include <stdbool.h>
#include <stddef.h>

#include <amark/error.h>

/*
 * The allocator abstraction. Every allocation and release in the library
 * goes through an amark_allocator_t; only this module calls the standard
 * malloc and free.
 */

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

/* Returns the default allocator, which uses malloc and free. ctx is NULL. */
amark_allocator_t amark_allocator_default(void);

/*
 * Returns the default allocator if requested is NULL, otherwise a copy of
 * *requested. When requested is not NULL, both of its functions must be
 * non-NULL.
 */
amark_allocator_t amark_allocator_resolve(const amark_allocator_t *requested);

/*
 * Allocates size bytes (1 byte if size is 0). On success, stores a non-NULL
 * pointer in *out and returns AMARK_ERROR_NONE. If the allocator fails,
 * stores NULL in *out and returns AMARK_ERROR_NOMEM. allocator and out must
 * not be NULL.
 */
amark_error_t amark_malloc(const amark_allocator_t *allocator, size_t size,
                           void **out);

/*
 * Allocates count * elem_size bytes, as amark_malloc does. If the product
 * overflows, stores NULL in *out and returns AMARK_ERROR_LIMIT_EXCEEDED without
 * calling the allocator.
 */
amark_error_t amark_malloc_array(const amark_allocator_t *allocator,
                                 size_t count, size_t elem_size, void **out);

/*
 * Releases ptr through allocator. Does nothing if ptr is NULL; otherwise
 * calls free_fn(ctx, ptr) once. allocator must not be NULL.
 */
void amark_free(const amark_allocator_t *allocator, void *ptr);

/*
 * Stores a + b (or a * b) in *out and returns true. On overflow, returns
 * false and leaves *out unchanged.
 */
bool amark_size_add(size_t a, size_t b, size_t *out);
bool amark_size_mul(size_t a, size_t b, size_t *out);

#endif /* AMARK_ALLOC_H */
