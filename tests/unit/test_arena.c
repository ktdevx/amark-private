/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/* Unit tests of the arena (src/arena.h). */

#include "arena.h"
#include "test_alloc.h"

#include <naos/naos.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * Fixture. NAOS_REQUIRE_* leaves a test with longjmp, so the arena a test
 * uses is this static variable, and teardown (which always runs) releases it
 * and then checks that no block is left and that every call received the
 * context value it was given.
 */
#define SMALL_COUNT 10000
#define ONE_MIB ((size_t)1024 * 1024)

static amark_test_alloc_t g_state;
static amark_allocator_t g_allocator;
static amark_arena_t g_arena;

/* Pointers returned by the arena in a test, to check their contents later. */
static unsigned char *g_ptrs[SMALL_COUNT];
static size_t g_sizes[SMALL_COUNT];

NAOS_SETUP(counting)
{
    amark_test_alloc_init(&g_state);
    g_allocator = amark_test_allocator(&g_state);
    amark_arena_init(&g_arena, &g_allocator);
}

NAOS_TEARDOWN(counting)
{
    amark_arena_deinit(&g_arena);
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
    NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
}

static int is_aligned(const void *ptr)
{
    return (uintptr_t)ptr % AMARK_ARENA_ALIGN == 0;
}

/* The byte written at offset j of the i-th allocation. */
static unsigned char pattern(size_t i, size_t j)
{
    return (unsigned char)((i * 31u + j * 7u + 1u) & 0xFFu);
}

static void fill(unsigned char *ptr, size_t size, size_t i)
{
    size_t j;

    for (j = 0; j < size; j++) {
        ptr[j] = pattern(i, j);
    }
}

/* Returns 1 if the first count allocations of g_ptrs still hold their data. */
static int contents_intact(size_t count)
{
    size_t i;
    size_t j;

    for (i = 0; i < count; i++) {
        for (j = 0; j < g_sizes[i]; j++) {
            if (g_ptrs[i][j] != pattern(i, j)) {
                return 0;
            }
        }
    }
    return 1;
}

/* Keeps the members of an arena, to see that a failed call changes nothing. */
typedef struct arena_snapshot {
    amark_arena_block_t *current;
    size_t next_block_size;
    size_t malloc_calls;
    size_t live;
} arena_snapshot_t;

static arena_snapshot_t snapshot(void)
{
    arena_snapshot_t s;

    s.current = g_arena.current;
    s.next_block_size = g_arena.next_block_size;
    s.malloc_calls = g_state.malloc_calls;
    s.live = g_state.live;
    return s;
}

/* ---- the alignment (7.1) ---- */

/*
 * The alignment of each type, measured as C99 allows (there is no alignof):
 * the offset of a member that follows a char.
 */
typedef void (*function_ptr_t)(void);

struct probe_ll { char c; long long member; };
struct probe_ld { char c; long double member; };
struct probe_d { char c; double member; };
struct probe_p { char c; void *member; };
struct probe_fp { char c; function_ptr_t member; };
struct probe_size { char c; size_t member; };

#define ALIGN_OF(probe) offsetof(struct probe, member)

NAOS_TEST(align_is_the_strictest_c99_alignment)
{
    NAOS_CHECK_UINT_GE(AMARK_ARENA_ALIGN, ALIGN_OF(probe_ll));
    NAOS_CHECK_UINT_GE(AMARK_ARENA_ALIGN, ALIGN_OF(probe_ld));
    NAOS_CHECK_UINT_GE(AMARK_ARENA_ALIGN, ALIGN_OF(probe_d));
    NAOS_CHECK_UINT_GE(AMARK_ARENA_ALIGN, ALIGN_OF(probe_p));
    NAOS_CHECK_UINT_GE(AMARK_ARENA_ALIGN, ALIGN_OF(probe_fp));
    NAOS_CHECK_UINT_GE(AMARK_ARENA_ALIGN, ALIGN_OF(probe_size));
    /* A power of two. */
    NAOS_CHECK_UINT_EQ(AMARK_ARENA_ALIGN & (AMARK_ARENA_ALIGN - 1), 0);
}

NAOS_TEST(every_allocation_is_aligned)
{
    size_t i;

    for (i = 0; i < 200; i++) {
        void *ptr = NULL;
        size_t size = i % 37; /* 0 included; odd sizes break the alignment */

        NAOS_REQUIRE_INT_EQ(amark_arena_alloc(&g_arena, size, &ptr),
                            AMARK_ERROR_NONE);
        NAOS_REQUIRE_NOT_NULL(ptr);
        NAOS_REQUIRE_TRUE(is_aligned(ptr));
    }
}

NAOS_TEST(aligned_memory_holds_any_type)
{
    void *ptr = NULL;
    long double *ld;
    long long *ll;

    NAOS_REQUIRE_INT_EQ(amark_arena_alloc(&g_arena, 3, &ptr), AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_arena_alloc(&g_arena, sizeof(long double), &ptr),
                        AMARK_ERROR_NONE);
    ld = (long double *)ptr;
    *ld = 1.5L;
    NAOS_REQUIRE_INT_EQ(amark_arena_alloc(&g_arena, 1, &ptr), AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_arena_alloc(&g_arena, sizeof(long long), &ptr),
                        AMARK_ERROR_NONE);
    ll = (long long *)ptr;
    *ll = -42;
    NAOS_CHECK_TRUE(*ld == 1.5L);
    NAOS_CHECK_TRUE(*ll == -42);
}

/* ---- sizes ---- */

NAOS_TEST(zero_size_returns_distinct_usable_memory)
{
    void *a = NULL;
    void *b = NULL;

    NAOS_REQUIRE_INT_EQ(amark_arena_alloc(&g_arena, 0, &a), AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_arena_alloc(&g_arena, 0, &b), AMARK_ERROR_NONE);
    NAOS_REQUIRE_NOT_NULL(a);
    NAOS_REQUIRE_NOT_NULL(b);
    NAOS_CHECK_TRUE(a != b);
    *(unsigned char *)a = 1;
    *(unsigned char *)b = 2;
    NAOS_CHECK_UINT_EQ(*(unsigned char *)a, 1);
}

NAOS_TEST(array_allocates_count_times_size)
{
    void *ptr = NULL;
    double *values;
    size_t i;

    NAOS_REQUIRE_INT_EQ(
        amark_arena_alloc_array(&g_arena, 100, sizeof(double), &ptr),
        AMARK_ERROR_NONE);
    NAOS_REQUIRE_NOT_NULL(ptr);
    NAOS_CHECK_TRUE(is_aligned(ptr));
    values = (double *)ptr;
    for (i = 0; i < 100; i++) {
        values[i] = (double)i;
    }
    NAOS_CHECK_TRUE(values[99] == 99.0);
}

/* ---- init and the allocator (7.4) ---- */

NAOS_TEST(init_does_not_allocate)
{
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 0);
    NAOS_CHECK_NULL(g_arena.current);
    NAOS_CHECK_PTR_EQ(g_arena.allocator.ctx, &g_state);
}

NAOS_TEST(memory_comes_from_the_given_allocator_only)
{
    void *ptr = NULL;

    NAOS_REQUIRE_INT_EQ(amark_arena_alloc(&g_arena, 8, &ptr), AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 1);
    NAOS_CHECK_UINT_EQ(g_state.live, 1);
}

/* ---- many small allocations (7.2, 7.8) ---- */

NAOS_TEST(small_allocations_are_grouped_into_blocks)
{
    size_t i;

    for (i = 0; i < SMALL_COUNT; i++) {
        void *ptr = NULL;

        g_sizes[i] = 1 + i % 24;
        NAOS_REQUIRE_INT_EQ(amark_arena_alloc(&g_arena, g_sizes[i], &ptr),
                            AMARK_ERROR_NONE);
        g_ptrs[i] = (unsigned char *)ptr;
        fill(g_ptrs[i], g_sizes[i], i);
    }
    NAOS_CHECK_UINT_LT(g_state.malloc_calls, 100);
    /* Later allocations did not move or overwrite earlier ones. */
    NAOS_CHECK_TRUE(contents_intact(SMALL_COUNT));
}

NAOS_TEST(block_size_doubles_up_to_one_mib)
{
    size_t previous = g_arena.next_block_size;
    size_t i;

    NAOS_CHECK_UINT_EQ(previous, 4096);
    for (i = 0; i < 20; i++) {
        void *ptr = NULL;

        /* Fills the current block, so that each call takes a new one. */
        NAOS_REQUIRE_INT_EQ(
            amark_arena_alloc(&g_arena, previous / 2 + 1, &ptr),
            AMARK_ERROR_NONE);
        NAOS_REQUIRE_UINT_EQ(g_state.malloc_calls, i + 1);
        if (previous < ONE_MIB) {
            NAOS_CHECK_UINT_EQ(g_arena.next_block_size, previous * 2);
        } else {
            NAOS_CHECK_UINT_EQ(g_arena.next_block_size, ONE_MIB);
        }
        previous = g_arena.next_block_size;
    }
    NAOS_CHECK_UINT_EQ(g_arena.next_block_size, ONE_MIB);
}

/* ---- large allocations (7.7) ---- */

NAOS_TEST(allocation_larger_than_one_mib_succeeds)
{
    void *ptr = NULL;
    size_t size = 3 * ONE_MIB + 5;

    NAOS_REQUIRE_INT_EQ(amark_arena_alloc(&g_arena, size, &ptr),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_NOT_NULL(ptr);
    NAOS_CHECK_TRUE(is_aligned(ptr));
    memset(ptr, 0x5A, size); /* the whole range is writable */

    NAOS_REQUIRE_INT_EQ(amark_arena_alloc(&g_arena, 16, &ptr), AMARK_ERROR_NONE);
    NAOS_REQUIRE_NOT_NULL(ptr);
    memset(ptr, 0, 16);
}

NAOS_TEST(large_allocation_keeps_using_the_current_block)
{
    void *ptr = NULL;
    size_t i;

    for (i = 0; i < 3; i++) {
        NAOS_REQUIRE_INT_EQ(amark_arena_alloc(&g_arena, 32, &ptr),
                            AMARK_ERROR_NONE);
        g_ptrs[i] = (unsigned char *)ptr;
        g_sizes[i] = 32;
        fill(g_ptrs[i], 32, i);
    }
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 1);

    /* A dedicated block, larger than any block the arena would take. */
    NAOS_REQUIRE_INT_EQ(amark_arena_alloc(&g_arena, ONE_MIB + 1, &ptr),
                        AMARK_ERROR_NONE);
    g_ptrs[3] = (unsigned char *)ptr;
    g_sizes[3] = ONE_MIB + 1;
    fill(g_ptrs[3], g_sizes[3], 3);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 2);

    /* The remainder of the first block is still used. */
    for (i = 4; i < 8; i++) {
        NAOS_REQUIRE_INT_EQ(amark_arena_alloc(&g_arena, 32, &ptr),
                            AMARK_ERROR_NONE);
        g_ptrs[i] = (unsigned char *)ptr;
        g_sizes[i] = 32;
        fill(g_ptrs[i], 32, i);
    }
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 2);
    NAOS_CHECK_TRUE(contents_intact(8));

    amark_arena_deinit(&g_arena);
    NAOS_CHECK_UINT_EQ(g_state.free_calls, 2);
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
}

/*
 * Returns the usable bytes of the first block of an arena: 4096 minus the
 * block header. The header size is private to arena.c, so it is measured as
 * the distance from the block to the first allocation in it.
 */
static size_t first_block_capacity(void)
{
    amark_arena_t arena;
    void *ptr = NULL;
    size_t header = 0;

    amark_arena_init(&arena, &g_allocator);
    if (amark_arena_alloc(&arena, 1, &ptr) == AMARK_ERROR_NONE) {
        header = (size_t)((unsigned char *)ptr -
                          (unsigned char *)arena.current);
    }
    amark_arena_deinit(&arena);
    amark_test_alloc_init(&g_state); /* forget the calls made here */
    return 4096 - header;
}

/* Allocates size bytes into g_ptrs[i] and fills them all. */
static amark_error_t alloc_filled(size_t i, size_t size)
{
    void *ptr = NULL;
    amark_error_t err = amark_arena_alloc(&g_arena, size, &ptr);

    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    g_ptrs[i] = (unsigned char *)ptr;
    g_sizes[i] = size;
    fill(g_ptrs[i], size, i);
    return AMARK_ERROR_NONE;
}

NAOS_TEST(dedicated_block_boundary)
{
    size_t capacity = first_block_capacity();

    NAOS_REQUIRE_UINT_LT(capacity, 4096);
    NAOS_REQUIRE_UINT_EQ(capacity % AMARK_ARENA_ALIGN, 0);

    /*
     * One step past what a 4096-byte block holds: a dedicated block, and the
     * size of the next normal block does not change.
     */
    NAOS_REQUIRE_INT_EQ(alloc_filled(0, capacity + AMARK_ARENA_ALIGN),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 1);
    NAOS_CHECK_UINT_EQ(g_arena.next_block_size, 4096);

    /* Exactly what a 4096-byte block holds: a normal block, then doubling. */
    NAOS_REQUIRE_INT_EQ(alloc_filled(1, capacity), AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 2);
    NAOS_CHECK_UINT_EQ(g_arena.next_block_size, 8192);

    /* The normal block is full: the next request takes a new block. */
    NAOS_REQUIRE_INT_EQ(alloc_filled(2, 1), AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 3);

    NAOS_CHECK_TRUE(contents_intact(3));
}

NAOS_TEST(exact_fit_uses_the_current_block)
{
    size_t capacity = first_block_capacity();

    NAOS_REQUIRE_INT_EQ(alloc_filled(0, AMARK_ARENA_ALIGN), AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 1);
    /* The rest of the block, to the last byte. */
    NAOS_REQUIRE_INT_EQ(alloc_filled(1, capacity - AMARK_ARENA_ALIGN),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 1);
    NAOS_CHECK_PTR_EQ(g_ptrs[1], g_ptrs[0] + AMARK_ARENA_ALIGN);
    NAOS_CHECK_TRUE(contents_intact(2));
}

NAOS_TEST(medium_allocations_back_to_back)
{
    static const size_t sizes[] = {5000, 10000, 4000, 9000, 20000, 70000,
                                   3000, 600000, 1 + ((size_t)1024 * 1024)};
    size_t count = sizeof(sizes) / sizeof(sizes[0]);
    size_t i;

    for (i = 0; i < count; i++) {
        NAOS_REQUIRE_INT_EQ(alloc_filled(i, sizes[i]), AMARK_ERROR_NONE);
        NAOS_REQUIRE_TRUE(is_aligned(g_ptrs[i]));
    }
    NAOS_CHECK_TRUE(contents_intact(count));
}

NAOS_TEST(large_allocation_as_the_first_one)
{
    void *ptr = NULL;

    NAOS_REQUIRE_INT_EQ(amark_arena_alloc(&g_arena, 2 * ONE_MIB, &ptr),
                        AMARK_ERROR_NONE);
    memset(ptr, 1, 2 * ONE_MIB);
    NAOS_REQUIRE_INT_EQ(amark_arena_alloc(&g_arena, 10, &ptr), AMARK_ERROR_NONE);
    memset(ptr, 2, 10);
    NAOS_CHECK_UINT_EQ(g_state.live, 2);
}

/* ---- deinit (7.3, 7.9) ---- */

NAOS_TEST(deinit_returns_every_block)
{
    size_t i;

    for (i = 0; i < 2000; i++) {
        void *ptr = NULL;

        NAOS_REQUIRE_INT_EQ(amark_arena_alloc(&g_arena, 100 + i % 50, &ptr),
                            AMARK_ERROR_NONE);
    }
    {
        void *ptr = NULL;

        NAOS_REQUIRE_INT_EQ(amark_arena_alloc(&g_arena, 2 * ONE_MIB, &ptr),
                            AMARK_ERROR_NONE);
    }
    NAOS_CHECK_UINT_GT(g_state.malloc_calls, 2);

    amark_arena_deinit(&g_arena);
    NAOS_CHECK_UINT_EQ(g_state.free_calls, g_state.malloc_calls);
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
}

NAOS_TEST(deinit_restores_the_init_state)
{
    amark_arena_t fresh;
    void *ptr = NULL;

    amark_arena_init(&fresh, &g_allocator);
    NAOS_REQUIRE_INT_EQ(amark_arena_alloc(&g_arena, 5000, &ptr),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_arena_alloc(&g_arena, 5000, &ptr),
                        AMARK_ERROR_NONE);
    amark_arena_deinit(&g_arena);

    NAOS_CHECK_NULL(g_arena.current);
    NAOS_CHECK_UINT_EQ(g_arena.next_block_size, fresh.next_block_size);
    NAOS_CHECK_TRUE(g_arena.allocator.malloc_fn == fresh.allocator.malloc_fn);
    NAOS_CHECK_TRUE(g_arena.allocator.free_fn == fresh.allocator.free_fn);
    NAOS_CHECK_PTR_EQ(g_arena.allocator.ctx, fresh.allocator.ctx);
    amark_arena_deinit(&fresh);

    /* The arena can be used again. */
    NAOS_REQUIRE_INT_EQ(amark_arena_alloc(&g_arena, 8, &ptr), AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(g_state.live, 1);
}

NAOS_TEST(deinit_twice_is_safe)
{
    void *ptr = NULL;

    NAOS_REQUIRE_INT_EQ(amark_arena_alloc(&g_arena, 8, &ptr), AMARK_ERROR_NONE);
    amark_arena_deinit(&g_arena);
    NAOS_CHECK_UINT_EQ(g_state.free_calls, 1);
    amark_arena_deinit(&g_arena);
    NAOS_CHECK_UINT_EQ(g_state.free_calls, 1);
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
}

NAOS_TEST(deinit_of_an_unused_arena_calls_nothing)
{
    amark_arena_deinit(&g_arena);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 0);
    NAOS_CHECK_UINT_EQ(g_state.free_calls, 0);
}

/* ---- overflow (7.6) ---- */

NAOS_TEST(size_max_is_overflow)
{
    void *ptr = NULL;
    arena_snapshot_t before;

    NAOS_REQUIRE_INT_EQ(amark_arena_alloc(&g_arena, 16, &ptr), AMARK_ERROR_NONE);
    g_ptrs[0] = (unsigned char *)ptr;
    g_sizes[0] = 16;
    fill(g_ptrs[0], 16, 0);
    before = snapshot();

    ptr = &g_state;
    NAOS_CHECK_INT_EQ(amark_arena_alloc(&g_arena, SIZE_MAX, &ptr),
                      AMARK_ERROR_LIMIT_EXCEEDED);
    NAOS_CHECK_NULL(ptr);
    ptr = &g_state;
    /* Fits after rounding but not with the block header. */
    NAOS_CHECK_INT_EQ(
        amark_arena_alloc(&g_arena, SIZE_MAX - AMARK_ARENA_ALIGN, &ptr),
        AMARK_ERROR_LIMIT_EXCEEDED);
    NAOS_CHECK_NULL(ptr);
    ptr = &g_state;
    NAOS_CHECK_INT_EQ(amark_arena_alloc_array(&g_arena, SIZE_MAX, 2, &ptr),
                      AMARK_ERROR_LIMIT_EXCEEDED);
    NAOS_CHECK_NULL(ptr);
    ptr = &g_state;
    NAOS_CHECK_INT_EQ(amark_arena_alloc_array(&g_arena, SIZE_MAX / 4, 8, &ptr),
                      AMARK_ERROR_LIMIT_EXCEEDED);
    NAOS_CHECK_NULL(ptr);

    /* Nothing changed, and the earlier memory is intact. */
    NAOS_CHECK_PTR_EQ(g_arena.current, before.current);
    NAOS_CHECK_UINT_EQ(g_arena.next_block_size, before.next_block_size);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, before.malloc_calls);
    NAOS_CHECK_TRUE(contents_intact(1));

    NAOS_REQUIRE_INT_EQ(amark_arena_alloc(&g_arena, 16, &ptr), AMARK_ERROR_NONE);
    NAOS_CHECK_NOT_NULL(ptr);
}

/* ---- allocator failures (7.5) ---- */

NAOS_TEST(allocator_failure_is_nomem_and_changes_nothing)
{
    void *ptr = NULL;
    arena_snapshot_t before;

    NAOS_REQUIRE_INT_EQ(amark_arena_alloc(&g_arena, 4000, &ptr),
                        AMARK_ERROR_NONE);
    g_ptrs[0] = (unsigned char *)ptr;
    g_sizes[0] = 4000;
    fill(g_ptrs[0], 4000, 0);
    before = snapshot();

    /* A new normal block fails. */
    g_state.fail_at = 2;
    ptr = &g_state;
    NAOS_CHECK_INT_EQ(amark_arena_alloc(&g_arena, 1000, &ptr),
                      AMARK_ERROR_NOMEM);
    NAOS_CHECK_NULL(ptr);
    NAOS_CHECK_PTR_EQ(g_arena.current, before.current);
    NAOS_CHECK_UINT_EQ(g_arena.next_block_size, before.next_block_size);
    NAOS_CHECK_UINT_EQ(g_state.live, before.live);

    /* A dedicated block fails. */
    g_state.fail_at = 3;
    ptr = &g_state;
    NAOS_CHECK_INT_EQ(amark_arena_alloc(&g_arena, 2 * ONE_MIB, &ptr),
                      AMARK_ERROR_NOMEM);
    NAOS_CHECK_NULL(ptr);
    NAOS_CHECK_PTR_EQ(g_arena.current, before.current);
    NAOS_CHECK_UINT_EQ(g_arena.next_block_size, before.next_block_size);
    NAOS_CHECK_UINT_EQ(g_state.live, before.live);

    NAOS_CHECK_TRUE(contents_intact(1));

    /* The arena works after the failures. */
    g_state.fail_at = 0;
    NAOS_REQUIRE_INT_EQ(amark_arena_alloc(&g_arena, 1000, &ptr),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_NOT_NULL(ptr);
    NAOS_CHECK_TRUE(contents_intact(1));
}

/* ---- the failure-injection loop (12.9, 12.10) ---- */

/*
 * An operation that makes many small allocations, a large one, and more
 * small ones, writing a pattern into each and recording it in g_ptrs. On
 * failure it returns the error and *done is the number of allocations that
 * succeeded; the caller cleans up with amark_arena_deinit.
 */
#define OPERATION_COUNT 3000

static amark_error_t allocate_mixed(amark_arena_t *arena, size_t *done)
{
    size_t i;

    *done = 0;
    for (i = 0; i < OPERATION_COUNT; i++) {
        void *ptr = NULL;
        amark_error_t err;
        size_t size = (i == 1500) ? ONE_MIB + 100 : 1 + (i * 13) % 200;

        err = amark_arena_alloc(arena, size, &ptr);
        if (err != AMARK_ERROR_NONE) {
            return err;
        }
        g_ptrs[i] = (unsigned char *)ptr;
        g_sizes[i] = size;
        fill(g_ptrs[i], size, i);
        *done = i + 1;
    }
    return AMARK_ERROR_NONE;
}

/*
 * The failure-injection pattern of tests/unit/test_alloc.c: for fail_at = 1,
 * 2, ..., run the operation, check that a failure is NOMEM at exactly the
 * fail_at-th call and that the memory returned before it is intact, deinit,
 * and check live == 0. Stop once the operation succeeds.
 */
NAOS_TEST(failure_injection_loop)
{
    size_t fail_at;
    size_t failures = 0;
    size_t calls_without_failure = 0;
    int succeeded = 0;

    for (fail_at = 1; fail_at <= 100 && !succeeded; fail_at++) {
        amark_error_t err;
        size_t done = 0;

        amark_test_alloc_init(&g_state);
        g_state.fail_at = fail_at;
        amark_arena_init(&g_arena, &g_allocator);

        err = allocate_mixed(&g_arena, &done);
        if (err == AMARK_ERROR_NONE) {
            NAOS_CHECK_UINT_LT(g_state.malloc_calls, fail_at);
            calls_without_failure = g_state.malloc_calls;
            succeeded = 1;
        } else {
            NAOS_CHECK_INT_EQ(err, AMARK_ERROR_NOMEM);
            NAOS_CHECK_UINT_EQ(g_state.malloc_calls, fail_at);
            NAOS_CHECK_UINT_LT(done, OPERATION_COUNT);
            failures++;
        }
        NAOS_CHECK_TRUE(contents_intact(done));

        amark_arena_deinit(&g_arena);
        NAOS_REQUIRE_UINT_EQ(g_state.live, 0);
        NAOS_REQUIRE_UINT_EQ(g_state.ctx_mismatch, 0);
    }
    NAOS_CHECK_TRUE(succeeded);
    /* One failure per call the operation makes, the large block included. */
    NAOS_CHECK_UINT_EQ(failures, calls_without_failure);
    NAOS_CHECK_UINT_GT(failures, 2);
    g_state.fail_at = 0;
}

/* ---- the default allocator ---- */

NAOS_TEST(default_allocator_works)
{
    amark_allocator_t allocator = amark_allocator_default();
    amark_arena_t arena;
    size_t i;

    amark_arena_init(&arena, &allocator);
    for (i = 0; i < 1000; i++) {
        void *ptr = NULL;

        NAOS_CHECK_INT_EQ(amark_arena_alloc(&arena, 1 + i % 64, &ptr),
                          AMARK_ERROR_NONE);
        NAOS_CHECK_NOT_NULL(ptr);
    }
    amark_arena_deinit(&arena);
    amark_arena_deinit(&arena);
}

NAOS_SUITE(arena,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(align_is_the_strictest_c99_alignment),
    NAOS_ADD_TEST(every_allocation_is_aligned),
    NAOS_ADD_TEST(aligned_memory_holds_any_type),
    NAOS_ADD_TEST(zero_size_returns_distinct_usable_memory),
    NAOS_ADD_TEST(array_allocates_count_times_size),
    NAOS_ADD_TEST(init_does_not_allocate),
    NAOS_ADD_TEST(memory_comes_from_the_given_allocator_only),
    NAOS_ADD_TEST(small_allocations_are_grouped_into_blocks),
    NAOS_ADD_TEST(block_size_doubles_up_to_one_mib),
    NAOS_ADD_TEST(allocation_larger_than_one_mib_succeeds),
    NAOS_ADD_TEST(large_allocation_keeps_using_the_current_block),
    NAOS_ADD_TEST(large_allocation_as_the_first_one),
    NAOS_ADD_TEST(dedicated_block_boundary),
    NAOS_ADD_TEST(exact_fit_uses_the_current_block),
    NAOS_ADD_TEST(medium_allocations_back_to_back),
    NAOS_ADD_TEST(deinit_returns_every_block),
    NAOS_ADD_TEST(deinit_restores_the_init_state),
    NAOS_ADD_TEST(deinit_twice_is_safe),
    NAOS_ADD_TEST(deinit_of_an_unused_arena_calls_nothing),
    NAOS_ADD_TEST(size_max_is_overflow),
    NAOS_ADD_TEST(allocator_failure_is_nomem_and_changes_nothing),
    NAOS_ADD_TEST(failure_injection_loop)
)

NAOS_SUITE(default_allocator,
    NAOS_ADD_TEST(default_allocator_works)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(arena),
    NAOS_ADD_SUITE(default_allocator)
)
