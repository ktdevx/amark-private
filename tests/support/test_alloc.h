/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_TEST_ALLOC_H
#define AMARK_TEST_ALLOC_H

#include <stddef.h>

#include "alloc.h"

/*
 * A counting, failing allocator for the unit tests (amark_test_support, never
 * installed). It allocates with malloc and free, counts the calls, and fails
 * the fail_at-th call to malloc_fn. It also adds up the bytes requested by
 * the blocks still live, and keeps the highest value that sum has reached.
 *
 * Each block carries a hidden header that records the state it was allocated
 * from. When free_fn receives a context value other than that state, the
 * owner's ctx_mismatch is incremented (the block is still released and
 * counted on its owner).
 */
typedef struct amark_test_alloc {
    size_t malloc_calls; /* calls to malloc_fn, failed ones included */
    size_t free_calls;   /* calls to free_fn */
    size_t live;         /* blocks allocated and not yet released */
    size_t fail_at;      /* make this call to malloc_fn fail; 0: never fail */
    size_t ctx_mismatch; /* calls that received a different context value */
    size_t live_bytes;   /* bytes requested by the blocks still live */
    size_t peak_bytes;   /* the highest value live_bytes has reached; set it
                            to live_bytes to measure from a point on */
} amark_test_alloc_t;

/* Sets every count, both byte sums and fail_at to 0. */
void amark_test_alloc_init(amark_test_alloc_t *state);

/* Returns an allocator whose context value is state. */
amark_allocator_t amark_test_allocator(amark_test_alloc_t *state);

#endif /* AMARK_TEST_ALLOC_H */
