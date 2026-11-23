/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/* Unit tests of the allocator abstraction (src/alloc.h). */

#include "alloc.h"
#include "test_alloc.h"

#include <naos/naos.h>

#include <stdint.h>
#include <string.h>

/*
 * Fixture. NAOS_REQUIRE_* leaves a test with longjmp, so anything a test
 * allocates is recorded in these variables and released in teardown, which
 * always runs. Teardown then checks that no block is left and that every call
 * received the context value it was given.
 */
#define BLOCK_COUNT 3

static amark_test_alloc_t g_state;
static amark_allocator_t g_allocator;
static void *g_blocks[BLOCK_COUNT];

NAOS_SETUP(counting)
{
    size_t i;

    amark_test_alloc_init(&g_state);
    g_allocator = amark_test_allocator(&g_state);
    for (i = 0; i < BLOCK_COUNT; i++) {
        g_blocks[i] = NULL;
    }
}

/* Releases the blocks of g_blocks. Safe to call more than once. */
static void release_blocks(void)
{
    size_t i;

    for (i = 0; i < BLOCK_COUNT; i++) {
        amark_free(&g_allocator, g_blocks[i]);
        g_blocks[i] = NULL;
    }
}

NAOS_TEARDOWN(counting)
{
    release_blocks();
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
    NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
}

/* ---- default allocator (6.1) ---- */

NAOS_TEST(default_allocates_and_frees)
{
    amark_allocator_t allocator = amark_allocator_resolve(NULL);
    void *ptr = NULL;

    NAOS_CHECK_PTR_EQ(allocator.ctx, NULL);
    NAOS_REQUIRE_INT_EQ(amark_malloc(&allocator, 64, &ptr), AMARK_ERROR_NONE);
    NAOS_REQUIRE_NOT_NULL(ptr);
    memset(ptr, 0xA5, 64); /* the whole block is writable */
    amark_free(&allocator, ptr);
}

NAOS_TEST(default_array_allocates_and_frees)
{
    amark_allocator_t allocator = amark_allocator_default();
    void *ptr = NULL;

    NAOS_REQUIRE_INT_EQ(amark_malloc_array(&allocator, 16, sizeof(double), &ptr),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_NOT_NULL(ptr);
    memset(ptr, 0, 16 * sizeof(double));
    amark_free(&allocator, ptr);
}

NAOS_TEST(default_zero_size_returns_a_block)
{
    amark_allocator_t allocator = amark_allocator_default();
    void *ptr = NULL;

    NAOS_REQUIRE_INT_EQ(amark_malloc(&allocator, 0, &ptr), AMARK_ERROR_NONE);
    NAOS_CHECK_NOT_NULL(ptr);
    amark_free(&allocator, ptr);
}

NAOS_TEST(default_free_null_does_nothing)
{
    amark_allocator_t allocator = amark_allocator_default();

    amark_free(&allocator, NULL);
    NAOS_PASS();
}

/* ---- a given allocator (6.2, 6.3, 6.4, 6.7) ---- */

NAOS_TEST(resolve_copies_the_given_allocator)
{
    amark_allocator_t resolved = amark_allocator_resolve(&g_allocator);

    NAOS_CHECK_TRUE(resolved.malloc_fn == g_allocator.malloc_fn);
    NAOS_CHECK_TRUE(resolved.free_fn == g_allocator.free_fn);
    NAOS_CHECK_PTR_EQ(resolved.ctx, &g_state);
}

NAOS_TEST(given_allocator_is_the_only_one_called)
{
    amark_allocator_t allocator = amark_allocator_resolve(&g_allocator);

    NAOS_REQUIRE_INT_EQ(amark_malloc(&allocator, 32, &g_blocks[0]),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_NOT_NULL(g_blocks[0]);
    NAOS_REQUIRE_INT_EQ(amark_malloc_array(&allocator, 4, 8, &g_blocks[1]),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_NOT_NULL(g_blocks[1]);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 2);
    NAOS_CHECK_UINT_EQ(g_state.live, 2);

    amark_free(&allocator, g_blocks[0]);
    g_blocks[0] = NULL;
    amark_free(&allocator, g_blocks[1]);
    g_blocks[1] = NULL;
    NAOS_CHECK_UINT_EQ(g_state.free_calls, 2);
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
    /* The context value reached both functions unchanged. */
    NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
}

NAOS_TEST(given_zero_size_requests_one_byte)
{
    NAOS_REQUIRE_INT_EQ(amark_malloc(&g_allocator, 0, &g_blocks[0]),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_NOT_NULL(g_blocks[0]);
    ((unsigned char *)g_blocks[0])[0] = 1; /* one byte is usable */
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 1);
}

NAOS_TEST(given_free_null_calls_nothing)
{
    amark_free(&g_allocator, NULL);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 0);
    NAOS_CHECK_UINT_EQ(g_state.free_calls, 0);
}

/* ---- failures (6.5, 6.6) ---- */

NAOS_TEST(allocator_failure_is_nomem)
{
    void *ptr = &g_state; /* any non-NULL value, to see it replaced */

    g_state.fail_at = 1;
    NAOS_CHECK_INT_EQ(amark_malloc(&g_allocator, 16, &ptr), AMARK_ERROR_NOMEM);
    NAOS_CHECK_NULL(ptr);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 1);
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
}

NAOS_TEST(array_allocator_failure_is_nomem)
{
    void *ptr = &g_state;

    g_state.fail_at = 1;
    NAOS_CHECK_INT_EQ(amark_malloc_array(&g_allocator, 2, 8, &ptr),
                      AMARK_ERROR_NOMEM);
    NAOS_CHECK_NULL(ptr);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 1);
}

NAOS_TEST(array_overflow_does_not_call_the_allocator)
{
    void *ptr = &g_state;

    NAOS_CHECK_INT_EQ(amark_malloc_array(&g_allocator, SIZE_MAX, 2, &ptr),
                      AMARK_ERROR_LIMIT_EXCEEDED);
    NAOS_CHECK_NULL(ptr);
    ptr = &g_state;
    NAOS_CHECK_INT_EQ(amark_malloc_array(&g_allocator, 2, SIZE_MAX / 2 + 1, &ptr),
                      AMARK_ERROR_LIMIT_EXCEEDED);
    NAOS_CHECK_NULL(ptr);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 0);
}

NAOS_TEST(array_of_zero_elements_requests_one_byte)
{
    NAOS_REQUIRE_INT_EQ(amark_malloc_array(&g_allocator, 0, SIZE_MAX, &g_blocks[0]),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_NOT_NULL(g_blocks[0]);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 1);
}

/* ---- the failure-injection loop (12.9, 12.10) ---- */

/*
 * An operation that allocates BLOCK_COUNT blocks into blocks (each NULL on
 * entry) and, on failure, returns the error without releasing what it
 * obtained: the caller cleans up, as with an arena. It stands in for the
 * operations of the later modules.
 */
static amark_error_t allocate_three(const amark_allocator_t *allocator,
                                    void *blocks[BLOCK_COUNT])
{
    amark_error_t err;

    err = amark_malloc(allocator, 24, &blocks[0]);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    err = amark_malloc_array(allocator, 10, sizeof(size_t), &blocks[1]);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    return amark_malloc(allocator, 0, &blocks[2]);
}

/*
 * The failure-injection pattern that every test of an allocating operation
 * follows:
 *
 *   for fail_at = 1, 2, ...:
 *       reset the counts and set fail_at
 *       run the operation
 *       if it failed: it returned NOMEM, at exactly the fail_at-th call
 *       clean up (deinit, free, ...)
 *       check live == 0 (and ctx_mismatch == 0)
 *       stop once the operation succeeds without reaching fail_at
 *
 * The loop has an upper bound so that a regression cannot make it endless.
 */
NAOS_TEST(failure_injection_loop)
{
    size_t fail_at;
    size_t failures = 0;
    int succeeded = 0;

    for (fail_at = 1; fail_at <= 100 && !succeeded; fail_at++) {
        amark_error_t err;

        amark_test_alloc_init(&g_state);
        g_state.fail_at = fail_at;

        err = allocate_three(&g_allocator, g_blocks);
        if (err == AMARK_ERROR_NONE) {
            /* fail_at is past the last allocation. */
            NAOS_CHECK_UINT_LT(g_state.malloc_calls, fail_at);
            succeeded = 1;
        } else {
            NAOS_CHECK_INT_EQ(err, AMARK_ERROR_NOMEM);
            NAOS_CHECK_UINT_EQ(g_state.malloc_calls, fail_at);
            failures++;
        }

        release_blocks();
        NAOS_REQUIRE_UINT_EQ(g_state.live, 0);
        NAOS_REQUIRE_UINT_EQ(g_state.ctx_mismatch, 0);
    }
    NAOS_CHECK_TRUE(succeeded);
    NAOS_CHECK_UINT_EQ(failures, 3); /* one failure per allocation */
}

/* ---- the same behavior with the default allocator (6.7) ---- */

NAOS_TEST(default_allocator_runs_the_same_operation)
{
    amark_allocator_t allocator = amark_allocator_default();
    void *blocks[BLOCK_COUNT] = {NULL, NULL, NULL};
    size_t i;

    NAOS_CHECK_INT_EQ(allocate_three(&allocator, blocks), AMARK_ERROR_NONE);
    for (i = 0; i < BLOCK_COUNT; i++) {
        NAOS_CHECK_NOT_NULL(blocks[i]);
        amark_free(&allocator, blocks[i]);
    }
}

/* ---- the test allocator itself ---- */

NAOS_TEST(test_allocator_fails_only_the_given_call)
{
    g_state.fail_at = 2;
    NAOS_CHECK_INT_EQ(amark_malloc(&g_allocator, 8, &g_blocks[0]),
                      AMARK_ERROR_NONE);
    NAOS_CHECK_INT_EQ(amark_malloc(&g_allocator, 8, &g_blocks[1]),
                      AMARK_ERROR_NOMEM);
    NAOS_CHECK_INT_EQ(amark_malloc(&g_allocator, 8, &g_blocks[2]),
                      AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 3);
    NAOS_CHECK_UINT_EQ(g_state.live, 2);
}

NAOS_TEST(test_allocator_counts_a_context_mismatch)
{
    amark_test_alloc_t other;
    void *ptr = NULL;

    amark_test_alloc_init(&other);
    NAOS_REQUIRE_INT_EQ(amark_malloc(&g_allocator, 8, &ptr), AMARK_ERROR_NONE);
    NAOS_REQUIRE_NOT_NULL(ptr);

    /* Release with the right functions but another context value. */
    g_allocator.free_fn(&other, ptr);
    NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 1);
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
    NAOS_CHECK_UINT_EQ(other.free_calls, 0);
    g_state.ctx_mismatch = 0; /* expected here; teardown checks for 0 */
}

/* ---- overflow-checked arithmetic ---- */

NAOS_TEST(size_add_bounds)
{
    size_t out = 7;

    NAOS_CHECK_TRUE(amark_size_add(0, 0, &out));
    NAOS_CHECK_UINT_EQ(out, 0);
    NAOS_CHECK_TRUE(amark_size_add(SIZE_MAX - 1, 1, &out));
    NAOS_CHECK_TRUE(out == SIZE_MAX);
    NAOS_CHECK_TRUE(amark_size_add(SIZE_MAX, 0, &out));
    NAOS_CHECK_TRUE(out == SIZE_MAX);

    out = 7;
    NAOS_CHECK_FALSE(amark_size_add(SIZE_MAX, 1, &out));
    NAOS_CHECK_FALSE(amark_size_add(1, SIZE_MAX, &out));
    NAOS_CHECK_FALSE(amark_size_add(SIZE_MAX, SIZE_MAX, &out));
    NAOS_CHECK_UINT_EQ(out, 7); /* unchanged on overflow */
}

NAOS_TEST(size_mul_bounds)
{
    size_t out = 7;

    NAOS_CHECK_TRUE(amark_size_mul(0, SIZE_MAX, &out));
    NAOS_CHECK_UINT_EQ(out, 0);
    NAOS_CHECK_TRUE(amark_size_mul(SIZE_MAX, 0, &out));
    NAOS_CHECK_UINT_EQ(out, 0);
    NAOS_CHECK_TRUE(amark_size_mul(SIZE_MAX, 1, &out));
    NAOS_CHECK_TRUE(out == SIZE_MAX);
    NAOS_CHECK_TRUE(amark_size_mul(SIZE_MAX / 2, 2, &out));
    NAOS_CHECK_TRUE(out == SIZE_MAX - 1);

    out = 7;
    NAOS_CHECK_FALSE(amark_size_mul(SIZE_MAX, 2, &out));
    NAOS_CHECK_FALSE(amark_size_mul(2, SIZE_MAX / 2 + 1, &out));
    NAOS_CHECK_FALSE(amark_size_mul(SIZE_MAX, SIZE_MAX, &out));
    NAOS_CHECK_UINT_EQ(out, 7);
}

NAOS_SUITE(default_allocator,
    NAOS_ADD_TEST(default_allocates_and_frees),
    NAOS_ADD_TEST(default_array_allocates_and_frees),
    NAOS_ADD_TEST(default_zero_size_returns_a_block),
    NAOS_ADD_TEST(default_free_null_does_nothing),
    NAOS_ADD_TEST(default_allocator_runs_the_same_operation)
)

NAOS_SUITE(given_allocator,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(resolve_copies_the_given_allocator),
    NAOS_ADD_TEST(given_allocator_is_the_only_one_called),
    NAOS_ADD_TEST(given_zero_size_requests_one_byte),
    NAOS_ADD_TEST(given_free_null_calls_nothing),
    NAOS_ADD_TEST(allocator_failure_is_nomem),
    NAOS_ADD_TEST(array_allocator_failure_is_nomem),
    NAOS_ADD_TEST(array_overflow_does_not_call_the_allocator),
    NAOS_ADD_TEST(array_of_zero_elements_requests_one_byte),
    NAOS_ADD_TEST(failure_injection_loop),
    NAOS_ADD_TEST(test_allocator_fails_only_the_given_call),
    NAOS_ADD_TEST(test_allocator_counts_a_context_mismatch)
)

NAOS_SUITE(size_arithmetic,
    NAOS_ADD_TEST(size_add_bounds),
    NAOS_ADD_TEST(size_mul_bounds)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(default_allocator),
    NAOS_ADD_SUITE(given_allocator),
    NAOS_ADD_SUITE(size_arithmetic)
)
