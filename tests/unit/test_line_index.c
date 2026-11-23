/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/* Unit tests of the line index (src/line_index.h). */

#include "line_index.h"
#include "test_alloc.h"

#include <naos/naos.h>

#include <stddef.h>
#include <string.h>

/*
 * Fixture. NAOS_REQUIRE_* leaves a test with longjmp, so the arena a test
 * uses is this static variable, and teardown (which always runs) releases it
 * and then checks that no block is left and that every call received the
 * context value it was given.
 */
#define MAX_INPUT 4096
#define LARGE_INPUT 200000

static amark_test_alloc_t g_state;
static amark_allocator_t g_allocator;
static amark_arena_t g_arena;
static unsigned char g_large[LARGE_INPUT];
static unsigned char g_large_copy[LARGE_INPUT];

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

/*
 * The reference: the number of the line that contains offset, found by
 * reading the bytes before offset one at a time. A line ending is LF, or CR
 * not followed by LF; the CR of CR LF is passed over, and its LF ends the
 * line.
 */
static size_t ref_line_at(const unsigned char *bytes, size_t length,
                          size_t offset)
{
    size_t line = 1;
    size_t i;

    for (i = 0; i < offset; i++) {
        if (bytes[i] == '\n') {
            line++;
        } else if (bytes[i] == '\r') {
            if (i + 1 >= length || bytes[i + 1] != '\n') {
                line++;
            }
        }
    }
    return line;
}

/*
 * Returns 1 if the table of index is well formed: line_starts[0] is 0, the
 * starts ascend strictly, the last is at most the length, and every start
 * after the first is the byte right after a line ending.
 */
static int table_well_formed(const amark_line_index_t *index,
                             const unsigned char *bytes, size_t length)
{
    size_t i;

    if (index->line_count < 1 || index->line_starts == NULL ||
        index->line_starts[0] != 0) {
        return 0;
    }
    for (i = 1; i < index->line_count; i++) {
        size_t start = index->line_starts[i];

        if (start <= index->line_starts[i - 1] || start > length) {
            return 0;
        }
        if (bytes[start - 1] == '\n') {
            continue;
        }
        if (bytes[start - 1] == '\r' && (start == length || bytes[start] != '\n')) {
            continue;
        }
        return 0;
    }
    return 1;
}

/*
 * Builds an index of bytes in g_arena and compares the line number of every
 * offset from 0 to length with the reference. Also checks the line count,
 * the table, that the bytes are unchanged, and that the build allocated at
 * most one block. Returns the number of offsets that disagreed (0 if all
 * agree), or (size_t)-1 if the build failed.
 */
static size_t compare_with_reference(const unsigned char *bytes, size_t length,
                                     unsigned char *copy)
{
    amark_line_index_t index;
    size_t calls_before = g_state.malloc_calls;
    size_t mismatches = 0;
    size_t offset;

    memcpy(copy, bytes, length);
    if (amark_line_index_build(&index, &g_arena, bytes, length) !=
        AMARK_ERROR_NONE) {
        return (size_t)-1;
    }
    NAOS_CHECK_UINT_LE(g_state.malloc_calls - calls_before, 1);
    NAOS_CHECK_TRUE(memcmp(copy, bytes, length) == 0);
    NAOS_CHECK_UINT_EQ(index.length, length);
    NAOS_CHECK_UINT_EQ(amark_line_index_line_count(&index),
                       ref_line_at(bytes, length, length));
    NAOS_CHECK_TRUE(table_well_formed(&index, bytes, length));
    for (offset = 0; offset <= length; offset++) {
        if (amark_line_index_line_at(&index, offset) !=
            ref_line_at(bytes, length, offset)) {
            mismatches++;
        }
    }
    return mismatches;
}

/* compare_with_reference for a short string literal (without its NUL). */
static size_t compare_text(const char *text, size_t length)
{
    unsigned char copy[MAX_INPUT];

    return compare_with_reference((const unsigned char *)text, length, copy);
}

#define COMPARE(literal) compare_text((literal), sizeof(literal) - 1)

/*
 * Builds an index of text and checks the line count and the line number of
 * every offset against the hand-written expected[] (length + 1 entries).
 */
static void check_expected(const char *text, size_t length, size_t lines,
                           const size_t *expected)
{
    amark_line_index_t index;
    size_t offset;

    NAOS_REQUIRE_INT_EQ(amark_line_index_build(&index, &g_arena,
                                               (const unsigned char *)text,
                                               length),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(amark_line_index_line_count(&index), lines);
    for (offset = 0; offset <= length; offset++) {
        NAOS_CHECK_UINT_EQ(amark_line_index_line_at(&index, offset),
                           expected[offset]);
    }
}

/* ---- hand-written expectations (9.2–9.7) ---- */

NAOS_TEST(empty_input_has_one_line)
{
    static const size_t expected[] = {1};

    check_expected("", 0, 1, expected);
    NAOS_CHECK_UINT_EQ(COMPARE(""), 0);
}

NAOS_TEST(no_line_ending_is_one_line)
{
    static const size_t expected[] = {1, 1, 1, 1};

    check_expected("abc", 3, 1, expected);
}

NAOS_TEST(lf_ends_a_line)
{
    /* "a\n": offsets 0 and 1 are line 1, offset 2 (the length) is line 2. */
    static const size_t expected[] = {1, 1, 2};

    check_expected("a\n", 2, 2, expected);
}

NAOS_TEST(cr_ends_a_line)
{
    static const size_t expected[] = {1, 1, 2, 2};

    check_expected("a\rb", 3, 2, expected);
}

NAOS_TEST(crlf_is_one_line_ending)
{
    /* "a\r\nb": CR (1) and LF (2) are line 1, b (3) is line 2. */
    static const size_t expected[] = {1, 1, 1, 2, 2};

    check_expected("a\r\nb", 4, 2, expected);
}

NAOS_TEST(lf_cr_is_two_line_endings)
{
    static const size_t expected[] = {1, 1, 2, 3};

    check_expected("a\n\r", 3, 3, expected);
}

NAOS_TEST(cr_cr_lf_is_two_line_endings)
{
    static const size_t expected[] = {1, 2, 2, 3};

    check_expected("\r\r\n", 3, 3, expected);
}

NAOS_TEST(mixed_line_endings)
{
    /* a LF b CR c CR LF d */
    static const size_t expected[] = {1, 1, 2, 2, 3, 3, 3, 4, 4};

    check_expected("a\nb\rc\r\nd", 8, 4, expected);
}

NAOS_TEST(input_ending_with_cr)
{
    static const size_t expected[] = {1, 1, 1, 2};

    check_expected("ab\r", 3, 2, expected);
}

NAOS_TEST(input_ending_with_crlf)
{
    static const size_t expected[] = {1, 1, 1, 1, 2};

    check_expected("ab\r\n", 4, 2, expected);
}

NAOS_TEST(only_line_endings)
{
    /* LF, CR LF, CR, CR: four line endings, five lines. */
    static const size_t expected[] = {1, 2, 2, 3, 4, 5};

    check_expected("\n\r\n\r\r", 5, 5, expected);
}

NAOS_TEST(table_holds_the_line_starts)
{
    amark_line_index_t index;
    const char *text = "ab\ncd\r\nef\rg\n";

    NAOS_REQUIRE_INT_EQ(amark_line_index_build(&index, &g_arena,
                                               (const unsigned char *)text,
                                               strlen(text)),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_UINT_EQ(index.line_count, 5);
    NAOS_CHECK_UINT_EQ(index.line_starts[0], 0);
    NAOS_CHECK_UINT_EQ(index.line_starts[1], 3);
    NAOS_CHECK_UINT_EQ(index.line_starts[2], 7);
    NAOS_CHECK_UINT_EQ(index.line_starts[3], 10);
    NAOS_CHECK_UINT_EQ(index.line_starts[4], 12);
    NAOS_CHECK_UINT_EQ(index.length, 12);
}

/* ---- every offset against the reference (9.1–9.6, 9.11) ---- */

NAOS_TEST(reference_without_line_endings)
{
    NAOS_CHECK_UINT_EQ(COMPARE("a"), 0);
    NAOS_CHECK_UINT_EQ(COMPARE("hello world"), 0);
}

NAOS_TEST(reference_lf)
{
    NAOS_CHECK_UINT_EQ(COMPARE("\n"), 0);
    NAOS_CHECK_UINT_EQ(COMPARE("a\nb"), 0);
    NAOS_CHECK_UINT_EQ(COMPARE("one\ntwo\nthree"), 0);
}

NAOS_TEST(reference_cr)
{
    NAOS_CHECK_UINT_EQ(COMPARE("\r"), 0);
    NAOS_CHECK_UINT_EQ(COMPARE("a\rb"), 0);
    NAOS_CHECK_UINT_EQ(COMPARE("one\rtwo\rthree"), 0);
}

NAOS_TEST(reference_crlf)
{
    NAOS_CHECK_UINT_EQ(COMPARE("\r\n"), 0);
    NAOS_CHECK_UINT_EQ(COMPARE("a\r\nb"), 0);
    NAOS_CHECK_UINT_EQ(COMPARE("one\r\ntwo\r\nthree"), 0);
}

NAOS_TEST(reference_mixed)
{
    NAOS_CHECK_UINT_EQ(COMPARE("a\nb\rc\r\nd\n\re"), 0);
    NAOS_CHECK_UINT_EQ(COMPARE("x\r\r\ny\n\r\nz"), 0);
}

NAOS_TEST(reference_consecutive_line_endings)
{
    NAOS_CHECK_UINT_EQ(COMPARE("a\n\n\nb"), 0);
    NAOS_CHECK_UINT_EQ(COMPARE("a\r\r\rb"), 0);
    NAOS_CHECK_UINT_EQ(COMPARE("a\r\n\r\n\r\nb"), 0);
    NAOS_CHECK_UINT_EQ(COMPARE("a\n\r\n\r\rb"), 0);
}

NAOS_TEST(reference_ending_with_line_ending)
{
    NAOS_CHECK_UINT_EQ(COMPARE("a\n"), 0);
    NAOS_CHECK_UINT_EQ(COMPARE("a\r"), 0);
    NAOS_CHECK_UINT_EQ(COMPARE("a\r\n"), 0);
    NAOS_CHECK_UINT_EQ(COMPARE("a\nb\n\n"), 0);
}

NAOS_TEST(reference_only_line_endings)
{
    NAOS_CHECK_UINT_EQ(COMPARE("\n\n"), 0);
    NAOS_CHECK_UINT_EQ(COMPARE("\r\r"), 0);
    NAOS_CHECK_UINT_EQ(COMPARE("\r\n\r\n"), 0);
    NAOS_CHECK_UINT_EQ(COMPARE("\n\r"), 0);
    NAOS_CHECK_UINT_EQ(COMPARE("\r\r\n\n\r"), 0);
}

NAOS_TEST(reference_invalid_utf8)
{
    /* Bytes that never appear in UTF-8, lone continuation bytes, a cut-off
       sequence, a surrogate and a NUL, around line endings. */
    static const unsigned char bytes[] = {
        0xFF, 0x0A, 0xC0, 0x80, 0x0D, 0xF5, 0x0D, 0x0A, 0xE2, 0x82,
        0x0D, 0xED, 0xA0, 0x80, 0x00, 0x0A, 0xFE, 0x0D
    };
    unsigned char copy[sizeof(bytes)];
    amark_line_index_t index;

    NAOS_CHECK_UINT_EQ(compare_with_reference(bytes, sizeof(bytes), copy), 0);
    NAOS_REQUIRE_INT_EQ(amark_line_index_build(&index, &g_arena, bytes,
                                               sizeof(bytes)),
                        AMARK_ERROR_NONE);
    /* LF, CR, CR LF, CR, LF, CR */
    NAOS_CHECK_UINT_EQ(amark_line_index_line_count(&index), 7);
    NAOS_CHECK_UINT_EQ(amark_line_index_line_at(&index, sizeof(bytes)), 7);
}

/* A deterministic pseudo-random number generator (an LCG). */
static unsigned long next_random(unsigned long *seed)
{
    *seed = (*seed * 1103515245ul + 12345ul) & 0x7FFFFFFFul;
    return *seed >> 16;
}

/*
 * Fills buf with length bytes drawn mostly from line endings, so that runs of
 * CR and LF in every order appear often, with some letters and invalid UTF-8.
 */
static void fill_random(unsigned char *buf, size_t length,
                        unsigned long *seed)
{
    static const unsigned char alphabet[] = {
        '\n', '\r', '\r', '\n', 'a', 'b', 0xFF, 0xC3
    };
    size_t i;

    for (i = 0; i < length; i++) {
        buf[i] = alphabet[next_random(seed) % sizeof(alphabet)];
    }
}

NAOS_TEST(reference_random_inputs)
{
    unsigned char buf[64];
    unsigned char copy[64];
    unsigned long seed = 12345;
    size_t round;

    for (round = 0; round < 2000; round++) {
        size_t length = (size_t)(next_random(&seed) % (sizeof(buf) + 1));

        fill_random(buf, length, &seed);
        NAOS_REQUIRE_UINT_EQ(compare_with_reference(buf, length, copy), 0);
    }
}

/*
 * A large input with many lines, so that the binary search runs over a deep
 * table. The reference is the running line number, kept while walking once.
 */
NAOS_TEST(large_input_every_offset)
{
    amark_line_index_t index;
    unsigned long seed = 777;
    size_t line = 1;
    size_t mismatches = 0;
    size_t offset;

    fill_random(g_large, LARGE_INPUT, &seed);
    memcpy(g_large_copy, g_large, LARGE_INPUT);
    NAOS_REQUIRE_INT_EQ(amark_line_index_build(&index, &g_arena, g_large,
                                               LARGE_INPUT),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 1);
    NAOS_CHECK_TRUE(memcmp(g_large, g_large_copy, LARGE_INPUT) == 0);
    NAOS_CHECK_TRUE(table_well_formed(&index, g_large, LARGE_INPUT));
    for (offset = 0; offset <= LARGE_INPUT; offset++) {
        if (amark_line_index_line_at(&index, offset) != line) {
            mismatches++;
        }
        if (offset < LARGE_INPUT) {
            if (g_large[offset] == '\n' ||
                (g_large[offset] == '\r' &&
                 (offset + 1 == LARGE_INPUT || g_large[offset + 1] != '\n'))) {
                line++;
            }
        }
    }
    NAOS_CHECK_UINT_EQ(mismatches, 0);
    NAOS_CHECK_UINT_EQ(amark_line_index_line_count(&index), line);
    NAOS_CHECK_UINT_GT(line, LARGE_INPUT / 4);
}

/* ---- the table comes from the arena, once (9.1, 9.9) ---- */

NAOS_TEST(build_allocates_once_from_the_arena)
{
    amark_line_index_t index;
    const char *text = "a\nb\nc\nd\n";

    NAOS_REQUIRE_INT_EQ(amark_line_index_build(&index, &g_arena,
                                               (const unsigned char *)text,
                                               strlen(text)),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 1);
    NAOS_CHECK_UINT_EQ(g_state.live, 1);
    amark_arena_deinit(&g_arena);
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
}

NAOS_TEST(two_indexes_in_one_arena_are_independent)
{
    amark_line_index_t first;
    amark_line_index_t second;
    const char *a = "1\n2\n3";
    const char *b = "x\ry";

    NAOS_REQUIRE_INT_EQ(amark_line_index_build(&first, &g_arena,
                                               (const unsigned char *)a,
                                               strlen(a)),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_line_index_build(&second, &g_arena,
                                               (const unsigned char *)b,
                                               strlen(b)),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_TRUE(first.line_starts != second.line_starts);
    NAOS_CHECK_UINT_EQ(amark_line_index_line_count(&first), 3);
    NAOS_CHECK_UINT_EQ(amark_line_index_line_count(&second), 2);
    NAOS_CHECK_UINT_EQ(amark_line_index_line_at(&first, 5), 3);
    NAOS_CHECK_UINT_EQ(amark_line_index_line_at(&second, 3), 2);
}

/*
 * Builds indexes with 1 to 9 lines one after another in one arena, then
 * checks every one of them again. A table written past its end would be
 * overwritten by the next table (which starts with 0), and the check of the
 * earlier index would fail.
 */
NAOS_TEST(consecutive_tables_do_not_overlap)
{
    static const char *const texts[] = {
        "a", "a\n", "a\nb\n", "\n\n\n", "\r\n\r\n\r\n\r\n", "a\rb\rc\rd\re\rf",
        "\n\r\n\r\n\r\n\n\r", "1\n2\n3\n4\n5\n6\n7\n8", "\r\r\r\r\r\r\r\r"
    };
    amark_line_index_t indexes[sizeof(texts) / sizeof(texts[0])];
    size_t count = sizeof(texts) / sizeof(texts[0]);
    size_t i;

    for (i = 0; i < count; i++) {
        NAOS_REQUIRE_INT_EQ(
            amark_line_index_build(&indexes[i], &g_arena,
                                   (const unsigned char *)texts[i],
                                   strlen(texts[i])),
            AMARK_ERROR_NONE);
        NAOS_REQUIRE_UINT_EQ(amark_line_index_line_count(&indexes[i]), i + 1);
    }
    for (i = 0; i < count; i++) {
        const unsigned char *bytes = (const unsigned char *)texts[i];
        size_t length = strlen(texts[i]);
        size_t offset;

        NAOS_CHECK_TRUE(table_well_formed(&indexes[i], bytes, length));
        for (offset = 0; offset <= length; offset++) {
            NAOS_CHECK_UINT_EQ(amark_line_index_line_at(&indexes[i], offset),
                               ref_line_at(bytes, length, offset));
        }
    }
}

/* ---- failure (9.10) ---- */

/* Fills *index with values a build never stores. */
static void poison(amark_line_index_t *index, const size_t *marker)
{
    index->line_starts = marker;
    index->line_count = 12345;
    index->length = 67890;
}

static int still_poisoned(const amark_line_index_t *index,
                          const size_t *marker)
{
    return index->line_starts == marker && index->line_count == 12345 &&
           index->length == 67890;
}

NAOS_TEST(allocation_failure_is_nomem_and_changes_nothing)
{
    static const size_t marker = 0;
    amark_line_index_t index;
    const char *text = "a\nb\r\nc";

    poison(&index, &marker);
    g_state.fail_at = 1;
    NAOS_CHECK_INT_EQ(amark_line_index_build(&index, &g_arena,
                                             (const unsigned char *)text,
                                             strlen(text)),
                      AMARK_ERROR_NOMEM);
    NAOS_CHECK_TRUE(still_poisoned(&index, &marker));
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 1);
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
}

NAOS_TEST(failure_on_an_empty_input_changes_nothing)
{
    static const size_t marker = 0;
    amark_line_index_t index;

    poison(&index, &marker);
    g_state.fail_at = 1;
    NAOS_CHECK_INT_EQ(amark_line_index_build(&index, &g_arena,
                                             (const unsigned char *)"", 0),
                      AMARK_ERROR_NOMEM);
    NAOS_CHECK_TRUE(still_poisoned(&index, &marker));
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
}

NAOS_TEST(failure_keeps_earlier_indexes)
{
    static const size_t marker = 0;
    amark_line_index_t first;
    amark_line_index_t index;
    size_t live_before;
    unsigned long seed = 1;

    NAOS_REQUIRE_INT_EQ(amark_line_index_build(&first, &g_arena,
                                               (const unsigned char *)"a\nb",
                                               3),
                        AMARK_ERROR_NONE);
    live_before = g_state.live;

    /* A table larger than any block so far needs a new block; make it fail. */
    fill_random(g_large, LARGE_INPUT, &seed);
    poison(&index, &marker);
    g_state.fail_at = g_state.malloc_calls + 1;
    NAOS_CHECK_INT_EQ(amark_line_index_build(&index, &g_arena, g_large,
                                             LARGE_INPUT),
                      AMARK_ERROR_NOMEM);
    NAOS_CHECK_TRUE(still_poisoned(&index, &marker));
    NAOS_CHECK_UINT_EQ(g_state.live, live_before);
    NAOS_CHECK_UINT_EQ(amark_line_index_line_count(&first), 2);
    NAOS_CHECK_UINT_EQ(amark_line_index_line_at(&first, 3), 2);
}

/*
 * Every possible failure point of a sequence of builds: each failing build
 * returns NOMEM and leaves its index unchanged, and releasing the arena
 * leaves nothing behind.
 */
NAOS_TEST(failure_injection_loop)
{
    static const size_t marker = 0;
    size_t fail_at;
    size_t failures = 0;
    size_t calls_without_failure = 0;
    int succeeded = 0;

    for (fail_at = 1; fail_at <= 100 && !succeeded; fail_at++) {
        amark_error_t err = AMARK_ERROR_NONE;
        size_t round;
        unsigned long seed = 99;

        amark_test_alloc_init(&g_state);
        g_state.fail_at = fail_at;
        amark_arena_init(&g_arena, &g_allocator);

        /* Growing inputs, so that later tables need new blocks. */
        for (round = 0; round < 8; round++) {
            size_t length = (size_t)1 << (round * 2 + 3);
            amark_line_index_t index;

            fill_random(g_large, length, &seed);
            poison(&index, &marker);
            err = amark_line_index_build(&index, &g_arena, g_large, length);
            if (err != AMARK_ERROR_NONE) {
                NAOS_CHECK_TRUE(still_poisoned(&index, &marker));
                break;
            }
            NAOS_CHECK_UINT_EQ(amark_line_index_line_at(&index, length),
                               ref_line_at(g_large, length, length));
        }
        if (err == AMARK_ERROR_NONE) {
            NAOS_CHECK_UINT_LT(g_state.malloc_calls, fail_at);
            calls_without_failure = g_state.malloc_calls;
            succeeded = 1;
        } else {
            NAOS_CHECK_INT_EQ(err, AMARK_ERROR_NOMEM);
            NAOS_CHECK_UINT_EQ(g_state.malloc_calls, fail_at);
            failures++;
        }

        amark_arena_deinit(&g_arena);
        NAOS_REQUIRE_UINT_EQ(g_state.live, 0);
        NAOS_REQUIRE_UINT_EQ(g_state.ctx_mismatch, 0);
    }
    NAOS_CHECK_TRUE(succeeded);
    /* One failure per allocator call the builds make. */
    NAOS_CHECK_UINT_EQ(failures, calls_without_failure);
    NAOS_CHECK_UINT_GT(failures, 2);
}

/* ---- the default allocator ---- */

NAOS_TEST(default_allocator_works)
{
    amark_allocator_t allocator = amark_allocator_default();
    amark_arena_t arena;
    amark_line_index_t index;
    const char *text = "a\r\nb\rc\nd";

    amark_arena_init(&arena, &allocator);
    if (amark_line_index_build(&index, &arena, (const unsigned char *)text,
                               strlen(text)) == AMARK_ERROR_NONE) {
        NAOS_CHECK_UINT_EQ(amark_line_index_line_count(&index), 4);
        NAOS_CHECK_UINT_EQ(amark_line_index_line_at(&index, 2), 1);
        NAOS_CHECK_UINT_EQ(amark_line_index_line_at(&index, 8), 4);
    } else {
        NAOS_CHECK_TRUE(0);
    }
    amark_arena_deinit(&arena);
}

NAOS_SUITE(line_index,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(empty_input_has_one_line),
    NAOS_ADD_TEST(no_line_ending_is_one_line),
    NAOS_ADD_TEST(lf_ends_a_line),
    NAOS_ADD_TEST(cr_ends_a_line),
    NAOS_ADD_TEST(crlf_is_one_line_ending),
    NAOS_ADD_TEST(lf_cr_is_two_line_endings),
    NAOS_ADD_TEST(cr_cr_lf_is_two_line_endings),
    NAOS_ADD_TEST(mixed_line_endings),
    NAOS_ADD_TEST(input_ending_with_cr),
    NAOS_ADD_TEST(input_ending_with_crlf),
    NAOS_ADD_TEST(only_line_endings),
    NAOS_ADD_TEST(table_holds_the_line_starts),
    NAOS_ADD_TEST(reference_without_line_endings),
    NAOS_ADD_TEST(reference_lf),
    NAOS_ADD_TEST(reference_cr),
    NAOS_ADD_TEST(reference_crlf),
    NAOS_ADD_TEST(reference_mixed),
    NAOS_ADD_TEST(reference_consecutive_line_endings),
    NAOS_ADD_TEST(reference_ending_with_line_ending),
    NAOS_ADD_TEST(reference_only_line_endings),
    NAOS_ADD_TEST(reference_invalid_utf8),
    NAOS_ADD_TEST(reference_random_inputs),
    NAOS_ADD_TEST(large_input_every_offset),
    NAOS_ADD_TEST(build_allocates_once_from_the_arena),
    NAOS_ADD_TEST(two_indexes_in_one_arena_are_independent),
    NAOS_ADD_TEST(consecutive_tables_do_not_overlap),
    NAOS_ADD_TEST(allocation_failure_is_nomem_and_changes_nothing),
    NAOS_ADD_TEST(failure_on_an_empty_input_changes_nothing),
    NAOS_ADD_TEST(failure_keeps_earlier_indexes),
    NAOS_ADD_TEST(failure_injection_loop)
)

NAOS_SUITE(default_allocator,
    NAOS_ADD_TEST(default_allocator_works)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(line_index),
    NAOS_ADD_SUITE(default_allocator)
)
