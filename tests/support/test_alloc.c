/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#include "test_alloc.h"

#include <stdint.h>
#include <stdlib.h>

/*
 * The hidden header in front of each block. It records the state the block
 * was allocated from and the size requested. The union makes its size a
 * multiple of the strictest C99 alignment, so the block that follows keeps
 * malloc's alignment.
 */
typedef union test_alloc_header {
    struct {
        amark_test_alloc_t *owner;
        size_t size;
    } info;
    long double ld;
    long long ll;
    double d;
    void *p;
    void (*fp)(void);
} test_alloc_header_t;

static void *test_alloc_malloc(void *ctx, size_t size)
{
    amark_test_alloc_t *state = (amark_test_alloc_t *)ctx;
    test_alloc_header_t *header;

    state->malloc_calls++;
    if (state->fail_at != 0 && state->malloc_calls == state->fail_at) {
        return NULL;
    }
    if (size > SIZE_MAX - sizeof(test_alloc_header_t)) {
        return NULL;
    }
    header = (test_alloc_header_t *)malloc(sizeof(test_alloc_header_t) + size);
    if (header == NULL) {
        return NULL;
    }
    header->info.owner = state;
    header->info.size = size;
    state->live++;
    state->live_bytes += size;
    if (state->live_bytes > state->peak_bytes) {
        state->peak_bytes = state->live_bytes;
    }
    return header + 1;
}

static void test_alloc_free(void *ctx, void *ptr)
{
    test_alloc_header_t *header = (test_alloc_header_t *)ptr - 1;
    amark_test_alloc_t *owner = header->info.owner;

    /*
     * Count on the owner, the state the block came from: ctx may point to
     * something that is not a state at all.
     */
    if (ctx != (void *)owner) {
        owner->ctx_mismatch++;
    }
    owner->free_calls++;
    owner->live--;
    owner->live_bytes -= header->info.size;
    free(header);
}

void amark_test_alloc_init(amark_test_alloc_t *state)
{
    state->malloc_calls = 0;
    state->free_calls = 0;
    state->live = 0;
    state->fail_at = 0;
    state->ctx_mismatch = 0;
    state->live_bytes = 0;
    state->peak_bytes = 0;
}

amark_allocator_t amark_test_allocator(amark_test_alloc_t *state)
{
    amark_allocator_t allocator;

    allocator.malloc_fn = test_alloc_malloc;
    allocator.free_fn = test_alloc_free;
    allocator.ctx = state;
    return allocator;
}
