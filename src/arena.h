/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_ARENA_H
#define AMARK_ARENA_H

#include <stddef.h>

#include "alloc.h"
#include "error.h"

/*
 * The arena. It takes blocks from an allocator, keeps them in a singly linked
 * list, and carves allocations out of them. Memory it returns never moves and
 * is released all at once by amark_arena_deinit; there is no individual
 * release, rewind or reuse.
 *
 * One arena must not be used from several threads at the same time; separate
 * arenas may be used from separate threads.
 */

/*
 * A union of the types with the strictest alignment in C99 (which has no
 * max_align_t). Its alignment is AMARK_ARENA_ALIGN.
 */
typedef union amark_arena_max_align {
    long long ll;
    long double ld;
    double d;
    void *p;
    void (*fp)(void);
} amark_arena_max_align_t;

/* Measures the alignment of amark_arena_max_align_t without alignof. */
struct amark_arena_align_probe {
    char c;
    amark_arena_max_align_t member;
};

/* Every pointer the arena returns is a multiple of this value. */
#define AMARK_ARENA_ALIGN offsetof(struct amark_arena_align_probe, member)

typedef struct amark_arena_block amark_arena_block_t;

/*
 * The members are visible in src/ but other modules use the functions only.
 */
typedef struct amark_arena {
    amark_allocator_t allocator;
    amark_arena_block_t *current; /* the block for new allocations, or NULL */
    size_t next_block_size;
} amark_arena_t;

/*
 * Initializes arena with a copy of *allocator. Allocates nothing. allocator
 * must not be NULL, and must return memory aligned for any type, as malloc
 * does.
 */
void amark_arena_init(amark_arena_t *arena, const amark_allocator_t *allocator);

/*
 * Allocates size bytes (1 byte if size is 0), aligned to AMARK_ARENA_ALIGN.
 * The contents are not initialized. On success, stores the pointer in *out
 * and returns AMARK_ERROR_NONE. On failure, stores NULL in *out and returns
 * AMARK_ERROR_NOMEM (the allocator failed) or AMARK_ERROR_OVERFLOW (the size
 * with the block header and the alignment does not fit in size_t); the arena
 * is then unchanged and the memory it returned before stays valid.
 */
amark_error_t amark_arena_alloc(amark_arena_t *arena, size_t size, void **out);

/*
 * Allocates count * elem_size bytes, as amark_arena_alloc does. If the
 * product overflows, returns AMARK_ERROR_OVERFLOW.
 */
amark_error_t amark_arena_alloc_array(amark_arena_t *arena, size_t count,
                                      size_t elem_size, void **out);

/*
 * Returns every block to the allocator (one free_fn call per block) and puts
 * arena back in the state that amark_arena_init leaves. Calling it again is
 * safe and releases nothing.
 */
void amark_arena_deinit(amark_arena_t *arena);

#endif /* AMARK_ARENA_H */
