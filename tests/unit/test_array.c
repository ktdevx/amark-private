/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/* Unit tests of the growable array (src/array.h). */

#include "array.h"
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
#define MANY_COUNT 100000
#define MAX_ELEM_SIZE 64

static amark_test_alloc_t g_state;
static amark_allocator_t g_allocator;
static amark_arena_t g_arena;

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

/* Element types of 1, 8 and 24 bytes (8.9). */
typedef struct elem24 {
    long long a;
    long long b;
    long long c;
} elem24_t;

static int is_aligned(const void *ptr)
{
    return (uintptr_t)ptr % AMARK_ARENA_ALIGN == 0;
}

/* The byte at offset j of the i-th element pushed in a test. */
static unsigned char pattern(size_t i, size_t j)
{
    return (unsigned char)((i * 31u + j * 7u + 1u) & 0xFFu);
}

/* Pushes the i-th patterned element (array->elem_size bytes). */
static amark_error_t push_pattern(amark_array_t *array, size_t i)
{
    unsigned char buf[MAX_ELEM_SIZE];
    size_t j;

    for (j = 0; j < array->elem_size; j++) {
        buf[j] = pattern(i, j);
    }
    return amark_array_push(array, buf);
}

static int element_matches(const unsigned char *elem, size_t elem_size,
                           size_t i)
{
    size_t j;

    for (j = 0; j < elem_size; j++) {
        if (elem[j] != pattern(i, j)) {
            return 0;
        }
    }
    return 1;
}

/*
 * Returns 1 if the array holds exactly the patterned elements 0 .. count - 1
 * (its length is count and every byte of every element is as pushed).
 */
static int contents_intact(const amark_array_t *array, size_t count)
{
    size_t i;

    if (amark_array_len(array) != count) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        const unsigned char *elem =
            (const unsigned char *)amark_array_at(array, i);

        if (!element_matches(elem, array->elem_size, i)) {
            return 0;
        }
    }
    return 1;
}

/* Pushes the patterned elements 0 .. count - 1 into an empty array. */
static amark_error_t push_patterns(amark_array_t *array, size_t count)
{
    size_t i;

    for (i = 0; i < count; i++) {
        amark_error_t err = push_pattern(array, i);

        if (err != AMARK_ERROR_NONE) {
            return err;
        }
    }
    return AMARK_ERROR_NONE;
}

/* Keeps the members of an array, to see that a failed call changes nothing. */
typedef struct array_snapshot {
    void *data;
    size_t len;
    size_t cap;
    size_t malloc_calls;
} array_snapshot_t;

static array_snapshot_t snapshot(const amark_array_t *array)
{
    array_snapshot_t s;

    s.data = array->data;
    s.len = array->len;
    s.cap = array->cap;
    s.malloc_calls = g_state.malloc_calls;
    return s;
}

static int unchanged(const amark_array_t *array, const array_snapshot_t *s)
{
    return array->data == s->data && array->len == s->len &&
           array->cap == s->cap;
}

/* ---- init (8.8) ---- */

NAOS_TEST(init_allocates_nothing)
{
    amark_array_t array;

    amark_array_init(&array, &g_arena, sizeof(int));
    NAOS_CHECK_UINT_EQ(amark_array_len(&array), 0);
    NAOS_CHECK_UINT_EQ(array.cap, 0);
    NAOS_CHECK_NULL(array.data);
    NAOS_CHECK_UINT_EQ(array.elem_size, sizeof(int));
    NAOS_CHECK_PTR_EQ(array.arena, &g_arena);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 0);
}

NAOS_TEST(memory_comes_from_the_arena)
{
    amark_array_t array;
    int value = 7;

    amark_array_init(&array, &g_arena, sizeof(int));
    NAOS_REQUIRE_INT_EQ(amark_array_push(&array, &value), AMARK_ERROR_NONE);
    /* The first growth took the arena's first block from the allocator. */
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 1);
    NAOS_CHECK_UINT_EQ(g_state.live, 1);
    amark_arena_deinit(&g_arena);
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
}

/* ---- push, at and len (8.1, 8.3) ---- */

NAOS_TEST(push_keeps_the_order)
{
    amark_array_t array;
    size_t i;

    amark_array_init(&array, &g_arena, sizeof(size_t));
    for (i = 0; i < 100; i++) {
        size_t value = i * 3 + 1;

        NAOS_REQUIRE_INT_EQ(amark_array_push(&array, &value),
                            AMARK_ERROR_NONE);
        NAOS_REQUIRE_UINT_EQ(amark_array_len(&array), i + 1);
    }
    for (i = 0; i < 100; i++) {
        NAOS_CHECK_UINT_EQ(*(size_t *)amark_array_at(&array, i), i * 3 + 1);
    }
}

NAOS_TEST(push_copies_the_element)
{
    amark_array_t array;
    elem24_t value;
    elem24_t *stored;

    amark_array_init(&array, &g_arena, sizeof(elem24_t));
    value.a = 1;
    value.b = 2;
    value.c = 3;
    NAOS_REQUIRE_INT_EQ(amark_array_push(&array, &value), AMARK_ERROR_NONE);
    /* Changing the source afterwards does not change the stored copy. */
    value.a = 100;
    value.b = 200;
    value.c = 300;
    stored = (elem24_t *)amark_array_at(&array, 0);
    NAOS_CHECK_TRUE(stored != &value);
    NAOS_CHECK_TRUE(stored->a == 1);
    NAOS_CHECK_TRUE(stored->b == 2);
    NAOS_CHECK_TRUE(stored->c == 3);
}

NAOS_TEST(at_reads_and_writes)
{
    amark_array_t array;
    size_t i;

    amark_array_init(&array, &g_arena, sizeof(int));
    for (i = 0; i < 20; i++) {
        int value = (int)i;

        NAOS_REQUIRE_INT_EQ(amark_array_push(&array, &value),
                            AMARK_ERROR_NONE);
    }
    /* Consecutive elements are elem_size bytes apart. */
    NAOS_CHECK_PTR_EQ((char *)amark_array_at(&array, 1),
                      (char *)amark_array_at(&array, 0) + sizeof(int));
    NAOS_CHECK_PTR_EQ(amark_array_at(&array, 0), array.data);
    *(int *)amark_array_at(&array, 5) = -5;
    *(int *)amark_array_at(&array, 19) = -19;
    for (i = 0; i < 20; i++) {
        int expected = (i == 5 || i == 19) ? -(int)i : (int)i;

        NAOS_CHECK_INT_EQ(*(int *)amark_array_at(&array, i), expected);
    }
    NAOS_CHECK_UINT_EQ(amark_array_len(&array), 20);
}

/* ---- growth (8.1, 8.2, 8.7) ---- */

NAOS_TEST(first_push_grows_to_eight)
{
    amark_array_t array;

    amark_array_init(&array, &g_arena, sizeof(elem24_t));
    NAOS_REQUIRE_INT_EQ(push_pattern(&array, 0), AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(array.cap, 8);
    NAOS_CHECK_UINT_EQ(amark_array_len(&array), 1);
    NAOS_CHECK_NOT_NULL(array.data);
    NAOS_CHECK_TRUE(is_aligned(array.data));
    NAOS_CHECK_TRUE(contents_intact(&array, 1));
}

NAOS_TEST(growth_happens_exactly_when_full)
{
    amark_array_t array;
    void *data;
    size_t i;

    amark_array_init(&array, &g_arena, sizeof(elem24_t));
    NAOS_REQUIRE_INT_EQ(push_pattern(&array, 0), AMARK_ERROR_NONE);
    data = array.data;
    /* Elements 1 .. 7 fill the capacity of 8 without moving the data. */
    for (i = 1; i < 8; i++) {
        NAOS_REQUIRE_INT_EQ(push_pattern(&array, i), AMARK_ERROR_NONE);
        NAOS_CHECK_UINT_EQ(array.cap, 8);
        NAOS_CHECK_PTR_EQ(array.data, data);
    }
    NAOS_CHECK_TRUE(contents_intact(&array, 8));

    /* The 9th element doubles the capacity and moves the data. */
    NAOS_REQUIRE_INT_EQ(push_pattern(&array, 8), AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(array.cap, 16);
    NAOS_CHECK_TRUE(array.data != data);
    NAOS_CHECK_TRUE(is_aligned(array.data));
    NAOS_CHECK_TRUE(contents_intact(&array, 9));

    /* 16 fits; the 17th element doubles again. */
    data = array.data;
    for (i = 9; i < 16; i++) {
        NAOS_REQUIRE_INT_EQ(push_pattern(&array, i), AMARK_ERROR_NONE);
    }
    NAOS_CHECK_UINT_EQ(array.cap, 16);
    NAOS_CHECK_PTR_EQ(array.data, data);
    NAOS_REQUIRE_INT_EQ(push_pattern(&array, 16), AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(array.cap, 32);
    NAOS_CHECK_TRUE(contents_intact(&array, 17));
}

NAOS_TEST(values_survive_every_growth)
{
    amark_array_t array;
    size_t i;
    size_t growths = 0;

    amark_array_init(&array, &g_arena, sizeof(elem24_t));
    for (i = 0; i < 3000; i++) {
        size_t cap = array.cap;

        NAOS_REQUIRE_INT_EQ(push_pattern(&array, i), AMARK_ERROR_NONE);
        if (array.cap != cap) {
            /* Every growth doubles (after the first, which gives 8). */
            NAOS_CHECK_UINT_EQ(array.cap, cap == 0 ? 8 : cap * 2);
            NAOS_REQUIRE_TRUE(contents_intact(&array, i + 1));
            growths++;
        }
        NAOS_REQUIRE_TRUE(array.len <= array.cap);
    }
    NAOS_CHECK_TRUE(contents_intact(&array, 3000));
    /* 8, 16, ..., 4096 */
    NAOS_CHECK_UINT_EQ(growths, 10);
}

/* ---- reserve (8.1, 8.2) ---- */

NAOS_TEST(reserve_zero_on_empty_allocates_nothing)
{
    amark_array_t array;

    amark_array_init(&array, &g_arena, sizeof(int));
    NAOS_CHECK_INT_EQ(amark_array_reserve(&array, 0), AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(array.cap, 0);
    NAOS_CHECK_NULL(array.data);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 0);
}

NAOS_TEST(reserve_small_on_empty_gives_eight)
{
    amark_array_t array;

    amark_array_init(&array, &g_arena, sizeof(int));
    NAOS_REQUIRE_INT_EQ(amark_array_reserve(&array, 1), AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(array.cap, 8);
    NAOS_CHECK_UINT_EQ(amark_array_len(&array), 0);
    NAOS_CHECK_NOT_NULL(array.data);

    amark_array_init(&array, &g_arena, sizeof(int));
    NAOS_REQUIRE_INT_EQ(amark_array_reserve(&array, 7), AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(array.cap, 8);

    amark_array_init(&array, &g_arena, sizeof(int));
    NAOS_REQUIRE_INT_EQ(amark_array_reserve(&array, 8), AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(array.cap, 8);
}

NAOS_TEST(reserve_large_on_empty_gives_min_cap)
{
    amark_array_t array;

    amark_array_init(&array, &g_arena, sizeof(int));
    NAOS_REQUIRE_INT_EQ(amark_array_reserve(&array, 9), AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(array.cap, 9);

    amark_array_init(&array, &g_arena, sizeof(int));
    NAOS_REQUIRE_INT_EQ(amark_array_reserve(&array, 1000), AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(array.cap, 1000);
    NAOS_CHECK_UINT_EQ(amark_array_len(&array), 0);
    /* The reserved capacity is usable: 1000 pushes do not grow it. */
    NAOS_REQUIRE_INT_EQ(push_patterns(&array, 1000), AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(array.cap, 1000);
    NAOS_CHECK_TRUE(contents_intact(&array, 1000));
}

/* Fills an array of capacity 8 with 8 elements. */
static void fill_eight(amark_array_t *array)
{
    amark_array_init(array, &g_arena, sizeof(elem24_t));
    NAOS_REQUIRE_INT_EQ(push_patterns(array, 8), AMARK_ERROR_NONE);
    NAOS_REQUIRE_UINT_EQ(array->cap, 8);
}

NAOS_TEST(reserve_within_capacity_does_nothing)
{
    amark_array_t array;
    array_snapshot_t before;

    fill_eight(&array);
    before = snapshot(&array);
    NAOS_CHECK_INT_EQ(amark_array_reserve(&array, 0), AMARK_ERROR_NONE);
    NAOS_CHECK_INT_EQ(amark_array_reserve(&array, 7), AMARK_ERROR_NONE);
    NAOS_CHECK_INT_EQ(amark_array_reserve(&array, 8), AMARK_ERROR_NONE);
    NAOS_CHECK_TRUE(unchanged(&array, &before));
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, before.malloc_calls);
    NAOS_CHECK_TRUE(contents_intact(&array, 8));
}

NAOS_TEST(reserve_keeps_a_short_array)
{
    amark_array_t array;

    /* Growing an array that holds fewer than 8 elements copies all of them. */
    amark_array_init(&array, &g_arena, sizeof(elem24_t));
    NAOS_REQUIRE_INT_EQ(push_patterns(&array, 1), AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_array_reserve(&array, 100), AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(array.cap, 100);
    NAOS_CHECK_TRUE(contents_intact(&array, 1));
    NAOS_REQUIRE_INT_EQ(push_pattern(&array, 1), AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(push_pattern(&array, 2), AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_array_reserve(&array, 300), AMARK_ERROR_NONE);
    NAOS_CHECK_TRUE(contents_intact(&array, 3));
}

NAOS_TEST(reserve_one_more_doubles)
{
    amark_array_t array;

    /* cap * 2 (16) is larger than min_cap (9). */
    fill_eight(&array);
    NAOS_REQUIRE_INT_EQ(amark_array_reserve(&array, 9), AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(array.cap, 16);
    NAOS_CHECK_TRUE(contents_intact(&array, 8));
}

NAOS_TEST(reserve_twice_the_capacity)
{
    amark_array_t array;

    /* cap * 2 equals min_cap. */
    fill_eight(&array);
    NAOS_REQUIRE_INT_EQ(amark_array_reserve(&array, 16), AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(array.cap, 16);
    NAOS_CHECK_TRUE(contents_intact(&array, 8));
}

NAOS_TEST(reserve_more_than_twice_gives_min_cap)
{
    amark_array_t array;
    size_t i;

    /* min_cap (17) is larger than cap * 2 (16). */
    fill_eight(&array);
    NAOS_REQUIRE_INT_EQ(amark_array_reserve(&array, 17), AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(array.cap, 17);
    NAOS_CHECK_UINT_EQ(amark_array_len(&array), 8);
    NAOS_CHECK_TRUE(contents_intact(&array, 8));

    /* Growing from 17 when full doubles it. */
    for (i = 8; i < 17; i++) {
        NAOS_REQUIRE_INT_EQ(push_pattern(&array, i), AMARK_ERROR_NONE);
    }
    NAOS_CHECK_UINT_EQ(array.cap, 17);
    NAOS_REQUIRE_INT_EQ(push_pattern(&array, 17), AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(array.cap, 34);
    NAOS_CHECK_TRUE(contents_intact(&array, 18));
}

/* ---- pop (8.4) ---- */

NAOS_TEST(pop_returns_in_reverse_then_reports_empty)
{
    amark_array_t array;
    size_t i;
    long long out;

    amark_array_init(&array, &g_arena, sizeof(long long));
    for (i = 0; i < 50; i++) {
        long long value = (long long)i * 1000 - 7;

        NAOS_REQUIRE_INT_EQ(amark_array_push(&array, &value),
                            AMARK_ERROR_NONE);
    }
    for (i = 50; i > 0; i--) {
        out = 0;
        NAOS_REQUIRE_TRUE(amark_array_pop(&array, &out));
        NAOS_CHECK_TRUE(out == (long long)(i - 1) * 1000 - 7);
        NAOS_CHECK_UINT_EQ(amark_array_len(&array), i - 1);
    }
    /* Empty: false, and *out is not written. */
    out = 12345;
    NAOS_CHECK_FALSE(amark_array_pop(&array, &out));
    NAOS_CHECK_TRUE(out == 12345);
    NAOS_CHECK_UINT_EQ(amark_array_len(&array), 0);
    NAOS_CHECK_FALSE(amark_array_pop(&array, NULL));
    NAOS_CHECK_UINT_EQ(amark_array_len(&array), 0);
}

NAOS_TEST(pop_on_a_new_array_reports_empty)
{
    amark_array_t array;
    int out = 99;

    amark_array_init(&array, &g_arena, sizeof(int));
    NAOS_CHECK_FALSE(amark_array_pop(&array, &out));
    NAOS_CHECK_INT_EQ(out, 99);
    NAOS_CHECK_UINT_EQ(amark_array_len(&array), 0);
}

NAOS_TEST(pop_with_null_out_removes_the_last)
{
    amark_array_t array;

    amark_array_init(&array, &g_arena, sizeof(elem24_t));
    NAOS_REQUIRE_INT_EQ(push_patterns(&array, 3), AMARK_ERROR_NONE);
    NAOS_CHECK_TRUE(amark_array_pop(&array, NULL));
    NAOS_CHECK_TRUE(contents_intact(&array, 2));
}

NAOS_TEST(pop_copies_the_whole_element)
{
    amark_array_t array;
    unsigned char out[sizeof(elem24_t) + 1];

    amark_array_init(&array, &g_arena, sizeof(elem24_t));
    NAOS_REQUIRE_INT_EQ(push_patterns(&array, 2), AMARK_ERROR_NONE);
    memset(out, 0xEE, sizeof(out));
    NAOS_REQUIRE_TRUE(amark_array_pop(&array, out));
    NAOS_CHECK_TRUE(element_matches(out, sizeof(elem24_t), 1));
    /* Exactly elem_size bytes are written. */
    NAOS_CHECK_UINT_EQ(out[sizeof(elem24_t)], 0xEE);
    NAOS_REQUIRE_TRUE(amark_array_pop(&array, out));
    NAOS_CHECK_TRUE(element_matches(out, sizeof(elem24_t), 0));
}

NAOS_TEST(push_after_pop_reuses_the_capacity)
{
    amark_array_t array;
    array_snapshot_t before;

    amark_array_init(&array, &g_arena, sizeof(elem24_t));
    NAOS_REQUIRE_INT_EQ(push_patterns(&array, 8), AMARK_ERROR_NONE);
    NAOS_REQUIRE_TRUE(amark_array_pop(&array, NULL));
    NAOS_REQUIRE_TRUE(amark_array_pop(&array, NULL));
    before = snapshot(&array);
    NAOS_REQUIRE_INT_EQ(push_pattern(&array, 6), AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(push_pattern(&array, 7), AMARK_ERROR_NONE);
    NAOS_CHECK_PTR_EQ(array.data, before.data);
    NAOS_CHECK_UINT_EQ(array.cap, 8);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, before.malloc_calls);
    NAOS_CHECK_TRUE(contents_intact(&array, 8));
}

/* ---- element sizes (8.9) ---- */

NAOS_TEST(one_byte_elements)
{
    amark_array_t array;
    size_t i;
    unsigned char out;

    amark_array_init(&array, &g_arena, 1);
    for (i = 0; i < 300; i++) {
        unsigned char value = (unsigned char)(i * 7u);

        NAOS_REQUIRE_INT_EQ(amark_array_push(&array, &value),
                            AMARK_ERROR_NONE);
    }
    for (i = 0; i < 300; i++) {
        NAOS_CHECK_UINT_EQ(*(unsigned char *)amark_array_at(&array, i),
                           (unsigned char)(i * 7u));
    }
    for (i = 300; i > 0; i--) {
        NAOS_REQUIRE_TRUE(amark_array_pop(&array, &out));
        NAOS_CHECK_UINT_EQ(out, (unsigned char)((i - 1) * 7u));
    }
    NAOS_CHECK_FALSE(amark_array_pop(&array, &out));
}

NAOS_TEST(eight_byte_elements)
{
    amark_array_t array;
    size_t i;
    long long out;

    NAOS_REQUIRE_UINT_EQ(sizeof(long long), 8);
    amark_array_init(&array, &g_arena, sizeof(long long));
    for (i = 0; i < 300; i++) {
        long long value = -(long long)i * 1000003LL;

        NAOS_REQUIRE_INT_EQ(amark_array_push(&array, &value),
                            AMARK_ERROR_NONE);
    }
    NAOS_CHECK_TRUE(is_aligned(array.data));
    for (i = 0; i < 300; i++) {
        NAOS_CHECK_TRUE(*(long long *)amark_array_at(&array, i) ==
                        -(long long)i * 1000003LL);
    }
    for (i = 300; i > 0; i--) {
        NAOS_REQUIRE_TRUE(amark_array_pop(&array, &out));
        NAOS_CHECK_TRUE(out == -(long long)(i - 1) * 1000003LL);
    }
    NAOS_CHECK_FALSE(amark_array_pop(&array, &out));
}

NAOS_TEST(twenty_four_byte_elements)
{
    amark_array_t array;
    size_t i;
    elem24_t out;

    NAOS_REQUIRE_UINT_EQ(sizeof(elem24_t), 24);
    amark_array_init(&array, &g_arena, sizeof(elem24_t));
    for (i = 0; i < 300; i++) {
        elem24_t value;

        value.a = (long long)i;
        value.b = -(long long)i;
        value.c = (long long)i * 3;
        NAOS_REQUIRE_INT_EQ(amark_array_push(&array, &value),
                            AMARK_ERROR_NONE);
    }
    NAOS_CHECK_TRUE(is_aligned(array.data));
    for (i = 0; i < 300; i++) {
        const elem24_t *elem = (const elem24_t *)amark_array_at(&array, i);

        NAOS_CHECK_TRUE(elem->a == (long long)i);
        NAOS_CHECK_TRUE(elem->b == -(long long)i);
        NAOS_CHECK_TRUE(elem->c == (long long)i * 3);
    }
    for (i = 300; i > 0; i--) {
        NAOS_REQUIRE_TRUE(amark_array_pop(&array, &out));
        NAOS_CHECK_TRUE(out.a == (long long)(i - 1));
        NAOS_CHECK_TRUE(out.b == -(long long)(i - 1));
        NAOS_CHECK_TRUE(out.c == (long long)(i - 1) * 3);
    }
    NAOS_CHECK_FALSE(amark_array_pop(&array, &out));
}

/* ---- overflow (8.6) ---- */

NAOS_TEST(reserve_size_max_is_overflow)
{
    amark_array_t array;
    array_snapshot_t before;

    amark_array_init(&array, &g_arena, sizeof(elem24_t));
    NAOS_REQUIRE_INT_EQ(push_patterns(&array, 3), AMARK_ERROR_NONE);
    before = snapshot(&array);
    NAOS_CHECK_INT_EQ(amark_array_reserve(&array, SIZE_MAX),
                      AMARK_ERROR_OVERFLOW);
    NAOS_CHECK_TRUE(unchanged(&array, &before));
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, before.malloc_calls);
    NAOS_CHECK_TRUE(contents_intact(&array, 3));

    /* The array still works. */
    NAOS_REQUIRE_INT_EQ(push_pattern(&array, 3), AMARK_ERROR_NONE);
    NAOS_CHECK_TRUE(contents_intact(&array, 4));
}

NAOS_TEST(reserve_overflow_boundary)
{
    amark_array_t array;
    array_snapshot_t before;

    /* The smallest capacity whose byte size does not fit in size_t. */
    amark_array_init(&array, &g_arena, sizeof(elem24_t));
    before = snapshot(&array);
    NAOS_CHECK_INT_EQ(
        amark_array_reserve(&array, SIZE_MAX / sizeof(elem24_t) + 1),
        AMARK_ERROR_OVERFLOW);
    NAOS_CHECK_TRUE(unchanged(&array, &before));
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 0);

    amark_array_init(&array, &g_arena, 8);
    NAOS_CHECK_INT_EQ(amark_array_reserve(&array, SIZE_MAX / 8 + 1),
                      AMARK_ERROR_OVERFLOW);
    NAOS_CHECK_UINT_EQ(array.cap, 0);
    NAOS_CHECK_NULL(array.data);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 0);
}

NAOS_TEST(reserve_size_max_of_bytes_is_overflow)
{
    amark_array_t array;

    /* SIZE_MAX one-byte elements: the byte size fits, the arena's does not. */
    amark_array_init(&array, &g_arena, 1);
    NAOS_CHECK_INT_EQ(amark_array_reserve(&array, SIZE_MAX),
                      AMARK_ERROR_OVERFLOW);
    NAOS_CHECK_UINT_EQ(array.cap, 0);
    NAOS_CHECK_NULL(array.data);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 0);
}

/*
 * A push into a full array whose capacity cannot be doubled (or incremented)
 * within size_t. Such an array cannot exist in memory, so the members are set
 * by hand; the push must fail before it touches the data or the arena.
 */
NAOS_TEST(push_when_the_capacity_cannot_double_is_overflow)
{
    amark_array_t array;
    array_snapshot_t before;
    unsigned char value = 1;
    unsigned char storage[1];

    amark_array_init(&array, &g_arena, 1);
    array.data = storage;
    array.len = SIZE_MAX / 2 + 1;
    array.cap = SIZE_MAX / 2 + 1;
    before = snapshot(&array);
    NAOS_CHECK_INT_EQ(amark_array_push(&array, &value), AMARK_ERROR_OVERFLOW);
    NAOS_CHECK_TRUE(unchanged(&array, &before));
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 0);

    array.len = SIZE_MAX;
    array.cap = SIZE_MAX;
    before = snapshot(&array);
    NAOS_CHECK_INT_EQ(amark_array_push(&array, &value), AMARK_ERROR_OVERFLOW);
    NAOS_CHECK_TRUE(unchanged(&array, &before));
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 0);
}

/* ---- allocation failure (8.5, 12.9, 12.10) ---- */

NAOS_TEST(first_growth_failure_changes_nothing)
{
    amark_array_t array;

    amark_array_init(&array, &g_arena, sizeof(elem24_t));
    g_state.fail_at = 1;
    NAOS_CHECK_INT_EQ(push_pattern(&array, 0), AMARK_ERROR_NOMEM);
    NAOS_CHECK_UINT_EQ(amark_array_len(&array), 0);
    NAOS_CHECK_UINT_EQ(array.cap, 0);
    NAOS_CHECK_NULL(array.data);
    NAOS_CHECK_UINT_EQ(g_state.live, 0);

    g_state.fail_at = 0;
    NAOS_REQUIRE_INT_EQ(push_pattern(&array, 0), AMARK_ERROR_NONE);
    NAOS_CHECK_TRUE(contents_intact(&array, 1));
}

/*
 * Pushes until a growth needs a new block from the allocator, and makes that
 * call fail. The data, length, capacity and contents stay as they were, and a
 * later push (with the allocator working again) succeeds.
 */
NAOS_TEST(growth_failure_changes_nothing)
{
    amark_array_t array;
    size_t i;
    int failed = 0;

    amark_array_init(&array, &g_arena, sizeof(elem24_t));
    /* The first push takes the arena's first block. */
    NAOS_REQUIRE_INT_EQ(push_pattern(&array, 0), AMARK_ERROR_NONE);
    for (i = 1; i < 2000 && !failed; i++) {
        array_snapshot_t before = snapshot(&array);
        amark_error_t err;

        g_state.fail_at = g_state.malloc_calls + 1;
        err = push_pattern(&array, i);
        if (err == AMARK_ERROR_NONE) {
            NAOS_REQUIRE_UINT_EQ(g_state.malloc_calls, before.malloc_calls);
            continue;
        }
        NAOS_CHECK_INT_EQ(err, AMARK_ERROR_NOMEM);
        /* The failure happens exactly when the array is full. */
        NAOS_CHECK_UINT_EQ(before.len, before.cap);
        NAOS_CHECK_UINT_GT(before.len, 8);
        NAOS_CHECK_TRUE(unchanged(&array, &before));
        NAOS_CHECK_TRUE(contents_intact(&array, i));
        failed = 1;

        g_state.fail_at = 0;
        NAOS_REQUIRE_INT_EQ(push_pattern(&array, i), AMARK_ERROR_NONE);
        NAOS_CHECK_UINT_EQ(array.cap, before.cap * 2);
        NAOS_CHECK_TRUE(contents_intact(&array, i + 1));
    }
    NAOS_CHECK_TRUE(failed);
    g_state.fail_at = 0;
}

NAOS_TEST(reserve_failure_changes_nothing)
{
    amark_array_t array;
    array_snapshot_t before;

    fill_eight(&array);
    before = snapshot(&array);
    /* 100,000 elements of 24 bytes need a new block. */
    g_state.fail_at = g_state.malloc_calls + 1;
    NAOS_CHECK_INT_EQ(amark_array_reserve(&array, 100000), AMARK_ERROR_NOMEM);
    NAOS_CHECK_TRUE(unchanged(&array, &before));
    NAOS_CHECK_TRUE(contents_intact(&array, 8));
    g_state.fail_at = 0;
}

/*
 * Pushes enough 24-byte elements to need several blocks, failing the
 * fail_at-th call to the allocator, for fail_at = 1, 2, ... until the pushes
 * all succeed. Each round checks the array and then releases the arena and
 * checks that nothing is left.
 */
#define INJECTION_COUNT 20000

NAOS_TEST(failure_injection_loop)
{
    size_t fail_at;
    size_t failures = 0;
    size_t calls_without_failure = 0;
    int succeeded = 0;

    for (fail_at = 1; fail_at <= 100 && !succeeded; fail_at++) {
        amark_array_t array;
        amark_error_t err = AMARK_ERROR_NONE;
        size_t done;

        amark_test_alloc_init(&g_state);
        g_state.fail_at = fail_at;
        amark_arena_init(&g_arena, &g_allocator);
        amark_array_init(&array, &g_arena, sizeof(elem24_t));

        for (done = 0; done < INJECTION_COUNT; done++) {
            array_snapshot_t before = snapshot(&array);

            err = push_pattern(&array, done);
            if (err != AMARK_ERROR_NONE) {
                NAOS_CHECK_TRUE(unchanged(&array, &before));
                break;
            }
        }
        if (err == AMARK_ERROR_NONE) {
            NAOS_CHECK_UINT_LT(g_state.malloc_calls, fail_at);
            calls_without_failure = g_state.malloc_calls;
            succeeded = 1;
        } else {
            NAOS_CHECK_INT_EQ(err, AMARK_ERROR_NOMEM);
            NAOS_CHECK_UINT_EQ(g_state.malloc_calls, fail_at);
            NAOS_CHECK_UINT_LT(done, INJECTION_COUNT);
            failures++;
        }
        NAOS_CHECK_TRUE(contents_intact(&array, done));

        amark_arena_deinit(&g_arena);
        NAOS_REQUIRE_UINT_EQ(g_state.live, 0);
        NAOS_REQUIRE_UINT_EQ(g_state.ctx_mismatch, 0);
    }
    NAOS_CHECK_TRUE(succeeded);
    /* One failure per call the pushes make. */
    NAOS_CHECK_UINT_EQ(failures, calls_without_failure);
    NAOS_CHECK_UINT_GT(failures, 2);
    g_state.fail_at = 0;
}

/* ---- many appends (8.7) ---- */

NAOS_TEST(many_appends_call_the_allocator_rarely)
{
    amark_array_t array;
    size_t i;

    amark_array_init(&array, &g_arena, sizeof(size_t));
    for (i = 0; i < MANY_COUNT; i++) {
        NAOS_REQUIRE_INT_EQ(amark_array_push(&array, &i), AMARK_ERROR_NONE);
    }
    NAOS_CHECK_UINT_EQ(amark_array_len(&array), MANY_COUNT);
    /* About log2(n) growths, and the arena groups the small ones. */
    NAOS_CHECK_UINT_LT(g_state.malloc_calls, 30);
    /* The capacity is at most twice what is needed. */
    NAOS_CHECK_UINT_LT(array.cap, 2 * MANY_COUNT);
    for (i = 0; i < MANY_COUNT; i++) {
        NAOS_REQUIRE_UINT_EQ(*(size_t *)amark_array_at(&array, i), i);
    }
}

/* ---- the default allocator ---- */

NAOS_TEST(default_allocator_works)
{
    amark_allocator_t allocator = amark_allocator_default();
    amark_arena_t arena;
    amark_array_t array;
    size_t i;

    amark_arena_init(&arena, &allocator);
    amark_array_init(&array, &arena, sizeof(elem24_t));
    for (i = 0; i < 1000; i++) {
        if (push_pattern(&array, i) != AMARK_ERROR_NONE) {
            break;
        }
    }
    NAOS_CHECK_TRUE(contents_intact(&array, 1000));
    amark_arena_deinit(&arena);
}

NAOS_SUITE(array,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(init_allocates_nothing),
    NAOS_ADD_TEST(memory_comes_from_the_arena),
    NAOS_ADD_TEST(push_keeps_the_order),
    NAOS_ADD_TEST(push_copies_the_element),
    NAOS_ADD_TEST(at_reads_and_writes),
    NAOS_ADD_TEST(first_push_grows_to_eight),
    NAOS_ADD_TEST(growth_happens_exactly_when_full),
    NAOS_ADD_TEST(values_survive_every_growth),
    NAOS_ADD_TEST(reserve_zero_on_empty_allocates_nothing),
    NAOS_ADD_TEST(reserve_small_on_empty_gives_eight),
    NAOS_ADD_TEST(reserve_large_on_empty_gives_min_cap),
    NAOS_ADD_TEST(reserve_within_capacity_does_nothing),
    NAOS_ADD_TEST(reserve_keeps_a_short_array),
    NAOS_ADD_TEST(reserve_one_more_doubles),
    NAOS_ADD_TEST(reserve_twice_the_capacity),
    NAOS_ADD_TEST(reserve_more_than_twice_gives_min_cap),
    NAOS_ADD_TEST(pop_returns_in_reverse_then_reports_empty),
    NAOS_ADD_TEST(pop_on_a_new_array_reports_empty),
    NAOS_ADD_TEST(pop_with_null_out_removes_the_last),
    NAOS_ADD_TEST(pop_copies_the_whole_element),
    NAOS_ADD_TEST(push_after_pop_reuses_the_capacity),
    NAOS_ADD_TEST(one_byte_elements),
    NAOS_ADD_TEST(eight_byte_elements),
    NAOS_ADD_TEST(twenty_four_byte_elements),
    NAOS_ADD_TEST(reserve_size_max_is_overflow),
    NAOS_ADD_TEST(reserve_overflow_boundary),
    NAOS_ADD_TEST(reserve_size_max_of_bytes_is_overflow),
    NAOS_ADD_TEST(push_when_the_capacity_cannot_double_is_overflow),
    NAOS_ADD_TEST(first_growth_failure_changes_nothing),
    NAOS_ADD_TEST(growth_failure_changes_nothing),
    NAOS_ADD_TEST(reserve_failure_changes_nothing),
    NAOS_ADD_TEST(failure_injection_loop),
    NAOS_ADD_TEST(many_appends_call_the_allocator_rarely)
)

NAOS_SUITE(default_allocator,
    NAOS_ADD_TEST(default_allocator_works)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(array),
    NAOS_ADD_SUITE(default_allocator)
)
