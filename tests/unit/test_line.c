/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of the parse data (src/parse_data.h) and the line records
 * (src/line.h): creating the parse data of a document (the parsed range, the
 * number of lines, the initial line records), the inline contents, and
 * allocation failures while creating it.
 */

#include "document.h"
#include "line.h"
#include "parse_data.h"
#include "test_alloc.h"

#include <naos/naos.h>

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/*
 * Fixture. NAOS_REQUIRE_* leaves a test with longjmp, so the document a test
 * creates is kept in g_doc, and teardown (which always runs) destroys it and
 * then checks that no block is left and that every call received the
 * context value it was given.
 */
#define LARGE_LINES 1000

static amark_test_alloc_t g_state;
static amark_allocator_t g_allocator;
static amark_document_t *g_doc;
static unsigned char g_large[LARGE_LINES * 2];

NAOS_SETUP(counting)
{
    amark_test_alloc_init(&g_state);
    g_allocator = amark_test_allocator(&g_state);
    g_doc = NULL;
}

NAOS_TEARDOWN(counting)
{
    g_state.fail_at = 0;
    amark_document_destroy(g_doc);
    g_doc = NULL;
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
    NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
}

/*
 * "ab\n" is line 1 (offsets 0-2), "cd\r\n" line 2 (3-6), "ef\r" line 3
 * (7-9) and "gh" line 4 (10-11); offset 12 (the length) is on line 4.
 */
static const char k_mixed[] = "ab\ncd\r\nef\rgh";
#define MIXED_LENGTH (sizeof(k_mixed) - 1)

/* Creates g_doc over k_mixed with the range [start, end). */
static void create_mixed(size_t start, size_t end)
{
    NAOS_REQUIRE_INT_EQ(amark_document_create(&g_allocator,
                                              (const unsigned char *)k_mixed,
                                              MIXED_LENGTH, start, end, NULL,
                                              &g_doc),
                        AMARK_ERROR_NONE);
}

/* Creates g_doc over k_mixed and its parse data; returns the parse data. */
static const amark_parse_data_t *create_mixed_data(size_t start, size_t end)
{
    const amark_parse_data_t *data;

    create_mixed(start, end);
    NAOS_REQUIRE_INT_EQ(amark_parse_data_create(g_doc), AMARK_ERROR_NONE);
    data = amark_document_parse_data(g_doc);
    NAOS_REQUIRE_NOT_NULL(data);
    return data;
}

/* Checks the parts every new parse data has: the range and empty lists. */
static void check_new_data(const amark_parse_data_t *data, size_t start,
                           size_t end, size_t first_line, size_t line_count)
{
    NAOS_CHECK_UINT_EQ(data->range_start, start);
    NAOS_CHECK_UINT_EQ(data->range_end, end);
    NAOS_CHECK_UINT_EQ(data->first_line, first_line);
    NAOS_CHECK_UINT_EQ(data->line_count, line_count);
    NAOS_CHECK_UINT_EQ(data->definition_count, 0);
    NAOS_CHECK_UINT_EQ(amark_inline_content_count(g_doc), 0);
}

/* Checks that the record at index i is (content_start, pad 0). */
static void check_record(const amark_parse_data_t *data, size_t i,
                         size_t content_start)
{
    NAOS_REQUIRE_UINT_LT(i, data->line_count);
    NAOS_CHECK_UINT_EQ(data->lines[i].content_start, content_start);
    NAOS_CHECK_UINT_EQ(data->lines[i].pad, 0);
}

/* ---- the number of lines and the initial records ---- */

NAOS_TEST(empty_range_has_no_lines)
{
    const amark_parse_data_t *data = create_mixed_data(5, 5);

    check_new_data(data, 5, 5, 0, 0);
}

NAOS_TEST(empty_range_at_the_end_has_no_lines)
{
    const amark_parse_data_t *data = create_mixed_data(MIXED_LENGTH,
                                                       MIXED_LENGTH);

    check_new_data(data, MIXED_LENGTH, MIXED_LENGTH, 0, 0);
}

NAOS_TEST(empty_content_has_no_lines)
{
    const amark_parse_data_t *data;

    NAOS_REQUIRE_INT_EQ(amark_document_create(&g_allocator, NULL, 0, 0, 0,
                                              NULL, &g_doc),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_parse_data_create(g_doc), AMARK_ERROR_NONE);
    data = amark_document_parse_data(g_doc);
    NAOS_REQUIRE_NOT_NULL(data);
    check_new_data(data, 0, 0, 0, 0);
}

NAOS_TEST(one_line_without_line_ending)
{
    const amark_parse_data_t *data = create_mixed_data(0, 2);

    check_new_data(data, 0, 2, 1, 1);
    check_record(data, 0, 2);
}

/* The line ending belongs to its line, so "ab\n" is still one line. */
NAOS_TEST(one_line_with_line_ending)
{
    const amark_parse_data_t *data = create_mixed_data(0, 3);

    check_new_data(data, 0, 3, 1, 1);
    check_record(data, 0, 2);
}

NAOS_TEST(whole_content_has_four_lines)
{
    const amark_parse_data_t *data = create_mixed_data(0, MIXED_LENGTH);

    check_new_data(data, 0, MIXED_LENGTH, 1, 4);
    check_record(data, 0, 2);  /* LF */
    check_record(data, 1, 5);  /* CR LF */
    check_record(data, 2, 9);  /* CR */
    check_record(data, 3, 12); /* no line ending */
}

/* "d\r\ne": starts inside line 2 and ends inside line 3. */
NAOS_TEST(range_starting_and_ending_inside_lines)
{
    const amark_parse_data_t *data = create_mixed_data(4, 8);

    check_new_data(data, 4, 8, 2, 2);
    check_record(data, 0, 5);
    check_record(data, 1, 8);
}

/* "ab\ncd\r": ends between the CR and the LF of line 2. */
NAOS_TEST(range_ending_between_cr_and_lf)
{
    const amark_parse_data_t *data = create_mixed_data(0, 6);

    check_new_data(data, 0, 6, 1, 2);
    check_record(data, 0, 2);
    check_record(data, 1, 5);
}

/* "\nef\rgh": starts at the LF of line 2, which is then an empty line. */
NAOS_TEST(range_starting_between_cr_and_lf)
{
    const amark_parse_data_t *data = create_mixed_data(6, MIXED_LENGTH);

    check_new_data(data, 6, MIXED_LENGTH, 2, 3);
    check_record(data, 0, 6);
    check_record(data, 1, 9);
    check_record(data, 2, 12);
}

/* "ef\r" alone: a range that ends right after a CR line ending. */
NAOS_TEST(range_of_one_cr_line)
{
    const amark_parse_data_t *data = create_mixed_data(7, 10);

    check_new_data(data, 7, 10, 3, 1);
    check_record(data, 0, 9);
}

/* A range of only a line ending: one line whose content is empty. */
NAOS_TEST(range_of_only_a_line_ending)
{
    const amark_parse_data_t *data = create_mixed_data(2, 3);

    check_new_data(data, 2, 3, 1, 1);
    check_record(data, 0, 2);
}

/* ---- inline contents ---- */

NAOS_TEST(inline_contents_are_read_in_the_order_added)
{
    amark_inline_content_t content;
    size_t i;

    create_mixed_data(0, MIXED_LENGTH);
    NAOS_REQUIRE_INT_EQ(amark_inline_content_add(g_doc, 2, 0, 2),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_inline_content_add(g_doc, 3, 3, 9),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_inline_content_add(g_doc, 4, 12, 12),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(amark_inline_content_count(g_doc), 3);

    NAOS_REQUIRE_TRUE(amark_inline_content_at(g_doc, 0, &content));
    NAOS_CHECK_UINT_EQ(content.node, 2);
    NAOS_CHECK_UINT_EQ(content.start, 0);
    NAOS_CHECK_UINT_EQ(content.end, 2);
    NAOS_REQUIRE_TRUE(amark_inline_content_at(g_doc, 1, &content));
    NAOS_CHECK_UINT_EQ(content.node, 3);
    NAOS_CHECK_UINT_EQ(content.start, 3);
    NAOS_CHECK_UINT_EQ(content.end, 9);
    NAOS_REQUIRE_TRUE(amark_inline_content_at(g_doc, 2, &content));
    NAOS_CHECK_UINT_EQ(content.node, 4);
    NAOS_CHECK_UINT_EQ(content.start, 12);
    NAOS_CHECK_UINT_EQ(content.end, 12);

    /* Many more, so that the list grows several times. */
    for (i = 0; i < 100; i++) {
        NAOS_REQUIRE_INT_EQ(amark_inline_content_add(g_doc,
                                                     (amark_node_id_t)(10 + i),
                                                     i, i + 1),
                            AMARK_ERROR_NONE);
    }
    NAOS_CHECK_UINT_EQ(amark_inline_content_count(g_doc), 103);
    for (i = 0; i < 100; i++) {
        NAOS_REQUIRE_TRUE(amark_inline_content_at(g_doc, 3 + i, &content));
        NAOS_CHECK_UINT_EQ(content.node, 10 + i);
        NAOS_CHECK_UINT_EQ(content.start, i);
        NAOS_CHECK_UINT_EQ(content.end, i + 1);
    }
}

NAOS_TEST(index_at_or_past_the_count_is_absent_and_keeps_out)
{
    amark_inline_content_t content;

    create_mixed_data(0, MIXED_LENGTH);
    content.node = 77;
    content.start = 88;
    content.end = 99;
    NAOS_CHECK_FALSE(amark_inline_content_at(g_doc, 0, &content));
    NAOS_REQUIRE_INT_EQ(amark_inline_content_add(g_doc, 2, 0, 2),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_FALSE(amark_inline_content_at(g_doc, 1, &content));
    NAOS_CHECK_FALSE(amark_inline_content_at(g_doc, (size_t)-1, &content));
    NAOS_CHECK_UINT_EQ(content.node, 77);
    NAOS_CHECK_UINT_EQ(content.start, 88);
    NAOS_CHECK_UINT_EQ(content.end, 99);
}

/* A document without parse data has no inline contents. */
NAOS_TEST(no_inline_contents_without_parse_data)
{
    amark_inline_content_t content;

    create_mixed(0, MIXED_LENGTH);
    content.node = 77;
    content.start = 88;
    content.end = 99;
    NAOS_CHECK_UINT_EQ(amark_inline_content_count(g_doc), 0);
    NAOS_CHECK_FALSE(amark_inline_content_at(g_doc, 0, &content));
    NAOS_CHECK_UINT_EQ(content.node, 77);
    NAOS_CHECK_UINT_EQ(content.start, 88);
    NAOS_CHECK_UINT_EQ(content.end, 99);
}

/* ---- allocation failures ---- */

/* Fills g_large with LARGE_LINES lines "x\n". */
static void fill_large(void)
{
    size_t i;

    for (i = 0; i < LARGE_LINES; i++) {
        g_large[i * 2] = 'x';
        g_large[i * 2 + 1] = '\n';
    }
}

/*
 * Uses up the current block of the arena of g_doc, so that the next
 * allocation from it takes a block from the allocator: allocates the
 * smallest unit until the arena asks the allocator, and makes that call
 * fail (a failed allocation leaves the arena unchanged).
 */
static void fill_current_block(void)
{
    amark_arena_t *arena = amark_document_arena(g_doc);
    size_t saved_fail_at = g_state.fail_at;
    size_t calls = g_state.malloc_calls;
    amark_error_t err = AMARK_ERROR_NONE;
    void *mem = NULL;

    g_state.fail_at = calls + 1;
    while (err == AMARK_ERROR_NONE) {
        err = amark_arena_alloc(arena, 1, &mem);
    }
    NAOS_REQUIRE_INT_EQ(err, AMARK_ERROR_NOMEM);
    NAOS_REQUIRE_UINT_EQ(g_state.malloc_calls, calls + 1);
    g_state.fail_at = saved_fail_at;
}

/*
 * Every allocator call amark_parse_data_create makes fails in turn: the
 * call returns NOMEM, attaches nothing, and destroying the document leaves
 * no block behind. With the current block used up and LARGE_LINES lines,
 * the structure and the records each take a block of their own.
 */
NAOS_TEST(failure_injection_loop_create)
{
    size_t k;
    size_t failures = 0;
    bool succeeded = false;

    fill_large();
    for (k = 1; k <= 10 && !succeeded; k++) {
        amark_error_t err;

        amark_test_alloc_init(&g_state);
        NAOS_REQUIRE_INT_EQ(amark_document_create(&g_allocator, g_large,
                                                  sizeof(g_large), 0,
                                                  sizeof(g_large), NULL,
                                                  &g_doc),
                            AMARK_ERROR_NONE);
        fill_current_block();
        g_state.fail_at = g_state.malloc_calls + k;
        err = amark_parse_data_create(g_doc);
        if (err == AMARK_ERROR_NONE) {
            const amark_parse_data_t *data =
                amark_document_parse_data(g_doc);

            NAOS_REQUIRE_NOT_NULL(data);
            check_new_data(data, 0, sizeof(g_large), 1, LARGE_LINES);
            check_record(data, 0, 1);
            check_record(data, LARGE_LINES - 1, sizeof(g_large) - 1);
            succeeded = true;
        } else {
            NAOS_CHECK_INT_EQ(err, AMARK_ERROR_NOMEM);
            NAOS_CHECK_NULL(amark_document_parse_data(g_doc));
            failures++;
        }
        g_state.fail_at = 0;
        amark_document_destroy(g_doc);
        g_doc = NULL;
        NAOS_REQUIRE_UINT_EQ(g_state.live, 0);
        NAOS_REQUIRE_UINT_EQ(g_state.ctx_mismatch, 0);
    }
    NAOS_CHECK_TRUE(succeeded);
    NAOS_CHECK_UINT_EQ(failures, 2);
}

/* A failed add leaves the inline contents as they were. */
NAOS_TEST(failed_add_keeps_inline_contents)
{
    amark_inline_content_t content;
    size_t i;
    size_t count;
    amark_error_t err = AMARK_ERROR_NONE;

    create_mixed_data(0, MIXED_LENGTH);
    NAOS_REQUIRE_INT_EQ(amark_inline_content_add(g_doc, 2, 0, 1),
                        AMARK_ERROR_NONE);
    fill_current_block();
    g_state.fail_at = g_state.malloc_calls + 1;
    /* Adds until the list has to grow, which needs a block and fails. */
    for (i = 1; i < 1000 && err == AMARK_ERROR_NONE; i++) {
        err = amark_inline_content_add(g_doc, (amark_node_id_t)(2 + i), i,
                                       i + 1);
    }
    NAOS_CHECK_INT_EQ(err, AMARK_ERROR_NOMEM);
    count = amark_inline_content_count(g_doc);
    NAOS_CHECK_UINT_GT(count, 1);
    NAOS_CHECK_UINT_EQ(count, i - 1);
    for (i = 0; i < count; i++) {
        NAOS_REQUIRE_TRUE(amark_inline_content_at(g_doc, i, &content));
        NAOS_CHECK_UINT_EQ(content.node, 2 + i);
        NAOS_CHECK_UINT_EQ(content.start, i);
        NAOS_CHECK_UINT_EQ(content.end, i + 1);
    }
    NAOS_CHECK_FALSE(amark_inline_content_at(g_doc, count, &content));
    /* Once the allocator works again, the next add succeeds. */
    g_state.fail_at = 0;
    NAOS_CHECK_INT_EQ(amark_inline_content_add(g_doc, 999, 5, 6),
                      AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(amark_inline_content_count(g_doc), count + 1);
}

NAOS_SUITE(line,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(empty_range_has_no_lines),
    NAOS_ADD_TEST(empty_range_at_the_end_has_no_lines),
    NAOS_ADD_TEST(empty_content_has_no_lines),
    NAOS_ADD_TEST(one_line_without_line_ending),
    NAOS_ADD_TEST(one_line_with_line_ending),
    NAOS_ADD_TEST(whole_content_has_four_lines),
    NAOS_ADD_TEST(range_starting_and_ending_inside_lines),
    NAOS_ADD_TEST(range_ending_between_cr_and_lf),
    NAOS_ADD_TEST(range_starting_between_cr_and_lf),
    NAOS_ADD_TEST(range_of_one_cr_line),
    NAOS_ADD_TEST(range_of_only_a_line_ending),
    NAOS_ADD_TEST(inline_contents_are_read_in_the_order_added),
    NAOS_ADD_TEST(index_at_or_past_the_count_is_absent_and_keeps_out),
    NAOS_ADD_TEST(no_inline_contents_without_parse_data),
    NAOS_ADD_TEST(failure_injection_loop_create),
    NAOS_ADD_TEST(failed_add_keeps_inline_contents)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(line)
)
