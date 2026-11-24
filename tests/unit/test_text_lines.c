/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of the lines of a range (src/text_lines.h), on documents made by
 * amark_parse: segments leave out container prefixes in block quotes, list
 * items, nested containers and lazy continuation lines; pad is given only
 * when a segment starts at the record's content start; line endings of the
 * three kinds and a last line without one; a range that ends right after a
 * line ending does not go on to the next line; an empty range gives no
 * segment; the first segment of an ATX heading starts at '#'; and the lines
 * are walked as the cursor walks them.
 */

#include "cursor.h"
#include "document.h"
#include "line.h"
#include "line_index.h"
#include "parse_data.h"
#include "test_alloc.h"
#include "text_lines.h"
#include "tree_dump.h"

#include <amark/amark.h>

#include <naos/naos.h>

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/*
 * Fixture. NAOS_REQUIRE_* leaves a test with longjmp, so the document a test
 * makes is kept in a static variable, and teardown destroys it and checks
 * that no block is left and that every call received its context value.
 */
#define MAX_SEGMENTS 16
#define DUMP_SIZE 1024

static amark_test_alloc_t g_state;
static amark_config_t g_config;
static amark_document_t *g_doc;
static amark_text_segment_t g_segments[MAX_SEGMENTS];
static size_t g_count;

NAOS_SETUP(counting)
{
    amark_test_alloc_init(&g_state);
    amark_config_init(&g_config);
    g_config.allocator = amark_test_allocator(&g_state);
    g_doc = NULL;
    g_count = 0;
}

NAOS_TEARDOWN(counting)
{
    amark_document_destroy(g_doc);
    g_doc = NULL;
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
    NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
}

/* Parses content (a C string) into g_doc, destroying any earlier one. */
static void parse(const char *content)
{
    amark_document_destroy(g_doc);
    g_doc = NULL;
    NAOS_REQUIRE_INT_EQ(amark_parse(content, strlen(content), &g_config,
                                    &g_doc),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_NOT_NULL(g_doc);
}

/* Returns the n-th node (from 0) of kind in document order, or none. */
static amark_node_id_t find(amark_kind_t kind, size_t n)
{
    amark_node_id_t node = amark_document_root(g_doc);

    while (node != AMARK_NODE_NONE) {
        amark_node_id_t next;

        if (amark_node_kind(g_doc, node) == kind) {
            if (n == 0) {
                return node;
            }
            n--;
        }
        next = amark_node_first_child(g_doc, node);
        while (next == AMARK_NODE_NONE && node != AMARK_NODE_NONE) {
            next = amark_node_next_sibling(g_doc, node);
            if (next == AMARK_NODE_NONE) {
                node = amark_node_parent(g_doc, node);
            }
        }
        node = next;
    }
    return AMARK_NODE_NONE;
}

/* Reads every segment of [start, end) from first_line into g_segments. */
static void collect_lines(size_t start, size_t end, size_t first_line)
{
    amark_text_lines_t lines;
    amark_text_segment_t segment;

    amark_text_lines_init(&lines, g_doc, start, end, first_line);
    g_count = 0;
    while (amark_text_lines_next(&lines, &segment)) {
        NAOS_REQUIRE_UINT_LT(g_count, MAX_SEGMENTS);
        g_segments[g_count++] = segment;
    }
    /* Once it has returned false, it keeps returning false. */
    NAOS_CHECK_FALSE(amark_text_lines_next(&lines, &segment));
}

/* Reads every segment of [start, end), from the line that holds start. */
static void collect(size_t start, size_t end)
{
    collect_lines(start, end,
                  amark_line_index_line_at(amark_document_line_index(g_doc),
                                           start));
}

/* Reads every segment of the range of the n-th node of kind. */
static void collect_node(amark_kind_t kind, size_t n, size_t want_start,
                         size_t want_end)
{
    amark_node_id_t node = find(kind, n);
    amark_range_t range;

    NAOS_REQUIRE_UINT_NE(node, AMARK_NODE_NONE);
    NAOS_REQUIRE_TRUE(amark_node_range(g_doc, node, &range));
    NAOS_REQUIRE_UINT_EQ(range.start, want_start);
    NAOS_REQUIRE_UINT_EQ(range.end, want_end);
    collect_lines(range.start, range.end, amark_node_start_line(g_doc, node));
}

/* Checks segment i of the last collect. */
static void check_segment(size_t i, size_t start, size_t end, unsigned pad,
                          bool line_end)
{
    NAOS_REQUIRE_UINT_LT(i, g_count);
    NAOS_CHECK_UINT_EQ(g_segments[i].start, start);
    NAOS_CHECK_UINT_EQ(g_segments[i].end, end);
    NAOS_CHECK_UINT_EQ(g_segments[i].pad, pad);
    NAOS_CHECK_TRUE(g_segments[i].line_end == line_end);
}

/* Checks the record of line of g_doc. */
static void check_record(size_t line, size_t content_start, unsigned pad)
{
    amark_line_record_t record;

    NAOS_REQUIRE_TRUE(amark_line_get(g_doc, line, &record));
    NAOS_CHECK_UINT_EQ(record.content_start, content_start);
    NAOS_CHECK_UINT_EQ(record.pad, pad);
}

/* --- Single and multiple lines ------------------------------------------ */

NAOS_TEST(one_line)
{
    parse("hello");
    collect_node(AMARK_KIND_PARAGRAPH, 0, 0, 5);
    NAOS_CHECK_UINT_EQ(g_count, 1);
    check_segment(0, 0, 5, 0, false);
}

NAOS_TEST(one_line_followed_by_a_line_ending)
{
    /* The paragraph ends before the line ending, so it is not in the range. */
    parse("hello\n");
    collect_node(AMARK_KIND_PARAGRAPH, 0, 0, 5);
    NAOS_CHECK_UINT_EQ(g_count, 1);
    check_segment(0, 0, 5, 0, false);
}

NAOS_TEST(several_lines)
{
    /* "ab\n" 0-2, "  cd\n" 3-7, "ef" 8-9. */
    parse("ab\n  cd\nef");
    check_record(2, 5, 0);
    collect_node(AMARK_KIND_PARAGRAPH, 0, 0, 10);
    NAOS_CHECK_UINT_EQ(g_count, 3);
    check_segment(0, 0, 2, 0, true);
    check_segment(1, 5, 7, 0, true);
    check_segment(2, 8, 10, 0, false);
}

NAOS_TEST(range_starting_and_ending_inside_lines)
{
    /* "abc\n" 0-3, "def\n" 4-7, "ghi" 8-10; the range is [1, 9). */
    parse("abc\ndef\nghi");
    collect(1, 9);
    NAOS_CHECK_UINT_EQ(g_count, 3);
    check_segment(0, 1, 3, 0, true);
    check_segment(1, 4, 7, 0, true);
    check_segment(2, 8, 9, 0, false);
}

/* --- Containers ---------------------------------------------------------- */

NAOS_TEST(block_quote_prefixes_are_left_out)
{
    /* "> a\n" 0-3, "> b" 4-6. */
    parse("> a\n> b");
    collect_node(AMARK_KIND_PARAGRAPH, 0, 2, 7);
    NAOS_CHECK_UINT_EQ(g_count, 2);
    check_segment(0, 2, 3, 0, true);
    check_segment(1, 6, 7, 0, false);
}

NAOS_TEST(list_item_indentation_is_left_out)
{
    /* "- a\n" 0-3, "  b" 4-6. */
    parse("- a\n  b");
    collect_node(AMARK_KIND_PARAGRAPH, 0, 2, 7);
    NAOS_CHECK_UINT_EQ(g_count, 2);
    check_segment(0, 2, 3, 0, true);
    check_segment(1, 6, 7, 0, false);
}

NAOS_TEST(nested_container_prefixes_are_left_out)
{
    /* "> - a\n" 0-5, ">   b\n" 6-11, ">   c" 12-16. */
    parse("> - a\n>   b\n>   c");
    collect_node(AMARK_KIND_PARAGRAPH, 0, 4, 17);
    NAOS_CHECK_UINT_EQ(g_count, 3);
    check_segment(0, 4, 5, 0, true);
    check_segment(1, 10, 11, 0, true);
    check_segment(2, 16, 17, 0, false);
}

NAOS_TEST(lazy_continuation_line_in_a_block_quote)
{
    /* "> a\n" 0-3, "b" 4. */
    parse("> a\nb");
    collect_node(AMARK_KIND_PARAGRAPH, 0, 2, 5);
    NAOS_CHECK_UINT_EQ(g_count, 2);
    check_segment(0, 2, 3, 0, true);
    check_segment(1, 4, 5, 0, false);
}

NAOS_TEST(lazy_continuation_line_in_nested_containers)
{
    /* "> - a\n" 0-5, "> c" 6-8: "c" continues the paragraph lazily. */
    parse("> - a\n> c");
    collect_node(AMARK_KIND_PARAGRAPH, 0, 4, 9);
    NAOS_CHECK_UINT_EQ(g_count, 2);
    check_segment(0, 4, 5, 0, true);
    check_segment(1, 8, 9, 0, false);
}

/* --- Pad ----------------------------------------------------------------- */

NAOS_TEST(tab_with_three_columns_left_gets_pad_3)
{
    /* " ```\n" 0-4, "\tx\n" 5-7, "```" 8-10. */
    parse(" ```\n\tx\n```");
    check_record(2, 5, 3);
    collect_node(AMARK_KIND_CODE_BLOCK, 0, 1, 11);
    NAOS_CHECK_UINT_EQ(g_count, 3);
    check_segment(0, 1, 4, 0, true);
    check_segment(1, 5, 7, 3, true);
    check_segment(2, 8, 11, 0, false);
}

NAOS_TEST(tab_with_two_columns_left_gets_pad_2)
{
    /* "  ```\n" 0-5, "\tx\n" 6-8, "```" 9-11. */
    parse("  ```\n\tx\n```");
    check_record(2, 6, 2);
    collect_node(AMARK_KIND_CODE_BLOCK, 0, 2, 12);
    NAOS_CHECK_UINT_EQ(g_count, 3);
    check_segment(1, 6, 8, 2, true);
}

NAOS_TEST(tab_with_one_column_left_gets_pad_1)
{
    /* "   ```\n" 0-6, "\tx\n" 7-9, "```" 10-12. */
    parse("   ```\n\tx\n```");
    check_record(2, 7, 1);
    collect_node(AMARK_KIND_CODE_BLOCK, 0, 3, 13);
    NAOS_CHECK_UINT_EQ(g_count, 3);
    check_segment(1, 7, 9, 1, true);
}

NAOS_TEST(pad_on_the_first_line_when_the_range_starts_at_the_record)
{
    /*
     * ">\t\tfoo": the first tab gives 1 column to the block quote marker
     * and 2 to the code indentation, the second tab gives the other 2 and
     * keeps 2 as pad.
     */
    parse(">\t\tfoo");
    check_record(1, 2, 2);
    collect_node(AMARK_KIND_CODE_BLOCK, 0, 2, 6);
    NAOS_CHECK_UINT_EQ(g_count, 1);
    check_segment(0, 2, 6, 2, false);
}

NAOS_TEST(no_pad_when_the_range_starts_inside_the_line)
{
    /* "   ```\n" 0-6, "\tx\n" 7-9, "```" 10-12; the tab line has pad 1. */
    parse("   ```\n\tx\n```");
    collect(8, 10);
    NAOS_CHECK_UINT_EQ(g_count, 1);
    check_segment(0, 8, 9, 0, true);

    /* Starting at the tab itself, the range starts at the record. */
    collect(7, 9);
    NAOS_CHECK_UINT_EQ(g_count, 1);
    check_segment(0, 7, 9, 1, false);
}

NAOS_TEST(no_pad_when_the_range_starts_before_the_record)
{
    /* "> ```\n" 0-5, ">\tx\n" 6-9, "> ```" 10-14; line 2 is (7, pad 2). */
    parse("> ```\n>\tx\n> ```");
    check_record(2, 7, 2);
    collect(6, 9);
    NAOS_CHECK_UINT_EQ(g_count, 1);
    check_segment(0, 6, 9, 0, false);

    /* Read from line 1, line 2 starts at its record and gets the pad. */
    collect_node(AMARK_KIND_CODE_BLOCK, 0, 2, 15);
    NAOS_CHECK_UINT_EQ(g_count, 3);
    check_segment(0, 2, 5, 0, true);
    check_segment(1, 7, 9, 2, true);
    check_segment(2, 12, 15, 0, false);
}

NAOS_TEST(no_pad_on_an_empty_segment)
{
    /* "> ```\n" 0-5, ">\tx\n" 6-9, "> ```" 10-14; line 2 is (7, pad 2). */
    parse("> ```\n>\tx\n> ```");
    collect(2, 7);
    NAOS_CHECK_UINT_EQ(g_count, 2);
    check_segment(0, 2, 5, 0, true);
    check_segment(1, 7, 7, 0, false);
}

/* --- Line endings -------------------------------------------------------- */

NAOS_TEST(three_kinds_of_line_endings_and_a_last_line_without_one)
{
    /* "a\n" 0-1, "b\r" 2-3, "c\r\n" 4-6, "d" 7. */
    parse("a\nb\rc\r\nd");
    collect_node(AMARK_KIND_PARAGRAPH, 0, 0, 8);
    NAOS_CHECK_UINT_EQ(g_count, 4);
    check_segment(0, 0, 1, 0, true);
    check_segment(1, 2, 3, 0, true);
    check_segment(2, 4, 5, 0, true);
    check_segment(3, 7, 8, 0, false);
}

NAOS_TEST(range_ending_right_after_an_lf_stays_on_its_line)
{
    /* "a\\\n" 0-2, "b" 3: the hard break is "\\\n" [1, 3). */
    parse("a\\\nb");
    collect_node(AMARK_KIND_HARD_BREAK, 0, 1, 3);
    NAOS_CHECK_UINT_EQ(g_count, 1);
    check_segment(0, 1, 2, 0, true);
}

NAOS_TEST(range_ending_right_after_a_cr_lf_stays_on_its_line)
{
    /* "a\\\r\n" 0-3, "b" 4: the hard break is "\\\r\n" [1, 4). */
    parse("a\\\r\nb");
    collect_node(AMARK_KIND_HARD_BREAK, 0, 1, 4);
    NAOS_CHECK_UINT_EQ(g_count, 1);
    check_segment(0, 1, 2, 0, true);
}

NAOS_TEST(range_of_only_a_line_ending_gives_one_empty_segment)
{
    /* "a\r" 0-1, "b" 2: the soft break is "\r" [1, 2). */
    parse("a\rb");
    collect_node(AMARK_KIND_SOFT_BREAK, 0, 1, 2);
    NAOS_CHECK_UINT_EQ(g_count, 1);
    check_segment(0, 1, 1, 0, true);
}

NAOS_TEST(range_ending_right_after_the_last_line_ending)
{
    /* "a\n" 0-1; the last line is empty and starts at 2. */
    parse("a\n");
    collect(0, 2);
    NAOS_CHECK_UINT_EQ(g_count, 1);
    check_segment(0, 0, 1, 0, true);
}

/* --- Empty ranges -------------------------------------------------------- */

NAOS_TEST(empty_range_gives_no_segment)
{
    parse("abc\ndef");
    collect(1, 1);
    NAOS_CHECK_UINT_EQ(g_count, 0);
    collect(4, 4);
    NAOS_CHECK_UINT_EQ(g_count, 0);
    collect(7, 7);
    NAOS_CHECK_UINT_EQ(g_count, 0);
}

/* --- ATX headings -------------------------------------------------------- */

NAOS_TEST(atx_heading_starts_at_the_number_sign)
{
    /* The record of the line points at "foo", after "# ". */
    parse("# foo");
    check_record(1, 2, 0);
    collect_node(AMARK_KIND_HEADING, 0, 0, 5);
    NAOS_CHECK_UINT_EQ(g_count, 1);
    check_segment(0, 0, 5, 0, false);
}

NAOS_TEST(atx_heading_in_a_block_quote_starts_at_the_number_sign)
{
    /* "> ## foo" 0-7. */
    parse("> ## foo");
    check_record(1, 5, 0);
    collect_node(AMARK_KIND_HEADING, 0, 2, 8);
    NAOS_CHECK_UINT_EQ(g_count, 1);
    check_segment(0, 2, 8, 0, false);
}

/* --- The cursor and the document ----------------------------------------- */

NAOS_TEST(lines_are_walked_as_the_cursor_walks_them)
{
    amark_cursor_t cursor;
    size_t i = 0;

    /* "> - a\n" 0-5, ">   b  \n" 6-13, "> c\r\n" 14-18, ">   d" 19-23. */
    parse("> - a\n>   b  \n> c\r\n>   d");
    collect_node(AMARK_KIND_PARAGRAPH, 0, 4, 24);
    NAOS_REQUIRE_UINT_EQ(g_count, 4);

    amark_cursor_init(&cursor, g_doc, 4, 24);
    for (;;) {
        NAOS_REQUIRE_UINT_LT(i, g_count);
        NAOS_CHECK_UINT_EQ(g_segments[i].start, cursor.line_start);
        NAOS_CHECK_UINT_EQ(g_segments[i].end, cursor.line_end);
        amark_cursor_seek(&cursor, cursor.line_end);
        i++;
        if (amark_cursor_at_end(&cursor)) {
            break;
        }
        amark_cursor_next_line(&cursor);
    }
    NAOS_CHECK_UINT_EQ(i, g_count);
}

NAOS_TEST(reading_changes_neither_the_document_nor_the_records)
{
    static const char content[] = "> - a\n>\t\tb\n> c";
    char before[DUMP_SIZE];
    char after[DUMP_SIZE];
    amark_line_record_t records[3];
    size_t line;
    size_t allocations;

    parse(content);
    NAOS_REQUIRE_UINT_LT(amark_test_dump_tree(g_doc, before, DUMP_SIZE),
                         DUMP_SIZE);
    for (line = 1; line <= 3; line++) {
        NAOS_REQUIRE_TRUE(amark_line_get(g_doc, line, &records[line - 1]));
    }
    allocations = g_state.malloc_calls;

    collect(0, sizeof content - 1);
    NAOS_CHECK_UINT_EQ(g_count, 3);

    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, allocations);
    NAOS_REQUIRE_UINT_LT(amark_test_dump_tree(g_doc, after, DUMP_SIZE),
                         DUMP_SIZE);
    NAOS_CHECK_STR_EQ(after, before);
    NAOS_CHECK_MEM_EQ(amark_document_content(g_doc), sizeof content - 1,
                      content, sizeof content - 1);
    for (line = 1; line <= 3; line++) {
        amark_line_record_t record;

        NAOS_REQUIRE_TRUE(amark_line_get(g_doc, line, &record));
        NAOS_CHECK_UINT_EQ(record.content_start,
                           records[line - 1].content_start);
        NAOS_CHECK_UINT_EQ(record.pad, records[line - 1].pad);
    }
}

NAOS_SUITE(text_lines,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(one_line),
    NAOS_ADD_TEST(one_line_followed_by_a_line_ending),
    NAOS_ADD_TEST(several_lines),
    NAOS_ADD_TEST(range_starting_and_ending_inside_lines),
    NAOS_ADD_TEST(block_quote_prefixes_are_left_out),
    NAOS_ADD_TEST(list_item_indentation_is_left_out),
    NAOS_ADD_TEST(nested_container_prefixes_are_left_out),
    NAOS_ADD_TEST(lazy_continuation_line_in_a_block_quote),
    NAOS_ADD_TEST(lazy_continuation_line_in_nested_containers),
    NAOS_ADD_TEST(tab_with_three_columns_left_gets_pad_3),
    NAOS_ADD_TEST(tab_with_two_columns_left_gets_pad_2),
    NAOS_ADD_TEST(tab_with_one_column_left_gets_pad_1),
    NAOS_ADD_TEST(pad_on_the_first_line_when_the_range_starts_at_the_record),
    NAOS_ADD_TEST(no_pad_when_the_range_starts_inside_the_line),
    NAOS_ADD_TEST(no_pad_when_the_range_starts_before_the_record),
    NAOS_ADD_TEST(no_pad_on_an_empty_segment),
    NAOS_ADD_TEST(three_kinds_of_line_endings_and_a_last_line_without_one),
    NAOS_ADD_TEST(range_ending_right_after_an_lf_stays_on_its_line),
    NAOS_ADD_TEST(range_ending_right_after_a_cr_lf_stays_on_its_line),
    NAOS_ADD_TEST(range_of_only_a_line_ending_gives_one_empty_segment),
    NAOS_ADD_TEST(range_ending_right_after_the_last_line_ending),
    NAOS_ADD_TEST(empty_range_gives_no_segment),
    NAOS_ADD_TEST(atx_heading_starts_at_the_number_sign),
    NAOS_ADD_TEST(atx_heading_in_a_block_quote_starts_at_the_number_sign),
    NAOS_ADD_TEST(lines_are_walked_as_the_cursor_walks_them),
    NAOS_ADD_TEST(reading_changes_neither_the_document_nor_the_records)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(text_lines)
)
