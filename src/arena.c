/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#include "arena.h"

/* The size of the first block, and the size at which the doubling stops. */
#define AMARK_ARENA_FIRST_BLOCK_SIZE ((size_t)4096)
#define AMARK_ARENA_MAX_BLOCK_SIZE ((size_t)1024 * 1024)

/*
 * A block: this header, then the usable bytes. The union makes the size of
 * the header a multiple of AMARK_ARENA_ALIGN, so the usable bytes start at an
 * aligned position (the allocator returns memory aligned for any type).
 */
struct amark_arena_block {
    union {
        struct {
            amark_arena_block_t *next; /* the block taken before, or NULL */
            size_t capacity;           /* usable bytes after the header */
            size_t used;               /* a multiple of AMARK_ARENA_ALIGN */
        } fields;
        amark_arena_max_align_t align;
    } header;
};

#define AMARK_ARENA_HEADER_SIZE sizeof(amark_arena_block_t)

static unsigned char *amark_arena_block_data(amark_arena_block_t *block)
{
    return (unsigned char *)block + AMARK_ARENA_HEADER_SIZE;
}

/* Takes a block with capacity usable bytes from the allocator. */
static amark_error_t amark_arena_new_block(amark_arena_t *arena,
                                           size_t capacity,
                                           amark_arena_block_t **out)
{
    size_t total;
    void *memory;
    amark_error_t err;

    if (!amark_size_add(AMARK_ARENA_HEADER_SIZE, capacity, &total)) {
        *out = NULL;
        return AMARK_ERROR_OVERFLOW;
    }
    err = amark_malloc(&arena->allocator, total, &memory);
    if (err != AMARK_ERROR_NONE) {
        *out = NULL;
        return err;
    }
    *out = (amark_arena_block_t *)memory;
    (*out)->header.fields.next = NULL;
    (*out)->header.fields.capacity = capacity;
    (*out)->header.fields.used = 0;
    return AMARK_ERROR_NONE;
}

void amark_arena_init(amark_arena_t *arena, const amark_allocator_t *allocator)
{
    arena->allocator = *allocator;
    arena->current = NULL;
    arena->next_block_size = AMARK_ARENA_FIRST_BLOCK_SIZE;
}

amark_error_t amark_arena_alloc(amark_arena_t *arena, size_t size, void **out)
{
    amark_arena_block_t *block = arena->current;
    amark_arena_block_t *new_block;
    size_t rounded;
    size_t normal_capacity;
    amark_error_t err;

    if (size == 0) {
        size = 1;
    }
    /* Round up to a multiple of the alignment, so that used stays aligned. */
    if (!amark_size_add(size, AMARK_ARENA_ALIGN - 1, &rounded)) {
        *out = NULL;
        return AMARK_ERROR_OVERFLOW;
    }
    rounded -= rounded % AMARK_ARENA_ALIGN;

    /* It fits in the current block. */
    if (block != NULL && block->header.fields.capacity -
                                 block->header.fields.used >= rounded) {
        *out = amark_arena_block_data(block) + block->header.fields.used;
        block->header.fields.used += rounded;
        return AMARK_ERROR_NONE;
    }

    normal_capacity = arena->next_block_size - AMARK_ARENA_HEADER_SIZE;
    if (rounded > normal_capacity) {
        /*
         * Larger than the next block: a dedicated block, linked after the
         * current one, whose remainder stays in use.
         */
        err = amark_arena_new_block(arena, rounded, &new_block);
        if (err != AMARK_ERROR_NONE) {
            *out = NULL;
            return err;
        }
        new_block->header.fields.used = rounded;
        if (block != NULL) {
            new_block->header.fields.next = block->header.fields.next;
            block->header.fields.next = new_block;
        } else {
            arena->current = new_block;
        }
        *out = amark_arena_block_data(new_block);
        return AMARK_ERROR_NONE;
    }

    /* A new block of the next size, which becomes the current one. */
    err = amark_arena_new_block(arena, normal_capacity, &new_block);
    if (err != AMARK_ERROR_NONE) {
        *out = NULL;
        return err;
    }
    new_block->header.fields.next = block;
    new_block->header.fields.used = rounded;
    arena->current = new_block;
    if (arena->next_block_size <= AMARK_ARENA_MAX_BLOCK_SIZE / 2) {
        arena->next_block_size *= 2;
    } else {
        arena->next_block_size = AMARK_ARENA_MAX_BLOCK_SIZE;
    }
    *out = amark_arena_block_data(new_block);
    return AMARK_ERROR_NONE;
}

amark_error_t amark_arena_alloc_array(amark_arena_t *arena, size_t count,
                                      size_t elem_size, void **out)
{
    size_t size;

    if (!amark_size_mul(count, elem_size, &size)) {
        *out = NULL;
        return AMARK_ERROR_OVERFLOW;
    }
    return amark_arena_alloc(arena, size, out);
}

void amark_arena_deinit(amark_arena_t *arena)
{
    amark_arena_block_t *block = arena->current;

    /* A loop, not recursion, from the current block to the first one. */
    while (block != NULL) {
        amark_arena_block_t *next = block->header.fields.next;

        amark_free(&arena->allocator, block);
        block = next;
    }
    arena->current = NULL;
    arena->next_block_size = AMARK_ARENA_FIRST_BLOCK_SIZE;
}
