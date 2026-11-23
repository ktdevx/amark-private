/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of the block parser (src/block.h), calling amark_block_parse
 * directly on a document made with amark_document_create and
 * amark_parse_data_create: paragraphs and blank lines, the ranges and lines
 * of paragraph nodes, their inline contents, the line records, ranges that
 * start past offset 0, the line position (the cached search for the first
 * non-space byte and the two advances), the node count and nesting depth limits, and
 * allocation failures. Every parsed document goes through amark_tree_check.
 */

#include "arena.h"
#include "block.h"
#include "config_internal.h"
#include "document.h"
#include "line.h"
#include "parse_data.h"
#include "test_alloc.h"
#include "tree_check.h"
#include "tree_dump.h"

#include <amark/amark.h>

#include <naos/naos.h>

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/*
 * Fixture. NAOS_REQUIRE_* leaves a test with longjmp, so the document and the
 * scratch arena are kept in static variables, and teardown releases them and
 * checks that no block is left.
 */
#define BUFFER_SIZE 8192
#define DUMP_SIZE 1024

static amark_test_alloc_t g_state;
static amark_allocator_t g_allocator;
static amark_document_t *g_doc;
static amark_arena_t g_scratch;
static unsigned char g_buffer[BUFFER_SIZE];
static size_t g_length;
static char g_dump[DUMP_SIZE];

NAOS_SETUP(counting)
{
    amark_test_alloc_init(&g_state);
    g_allocator = amark_test_allocator(&g_state);
    amark_arena_init(&g_scratch, &g_allocator);
    g_doc = NULL;
    g_length = 0;
}

NAOS_TEARDOWN(counting)
{
    g_state.fail_at = 0;
    amark_arena_deinit(&g_scratch);
    amark_document_destroy(g_doc);
    g_doc = NULL;
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
    NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
}

/*
 * Creates g_doc over the first length bytes of g_buffer with the range
 * [start, end) and the two limits, attaches its parse data and runs the
 * block parser with g_scratch. Returns what the block parser returns; the
 * document stays in g_doc for the test to read (or for teardown).
 */
static amark_error_t parse_buffer(size_t start, size_t end, size_t max_depth,
                                  size_t max_nodes)
{
    amark_config_t config;
    amark_resolved_config_t resolved;
    amark_error_t err;

    amark_config_init(&config);
    config.start = start;
    config.end = end;
    config.allocator = g_allocator;
    config.max_nesting_depth = max_depth;
    config.max_node_count = max_nodes;
    NAOS_REQUIRE_INT_EQ(amark_config_resolve(&config, g_length, &resolved),
                        AMARK_ERROR_NONE);

    err = amark_document_create(&resolved.allocator, g_buffer, g_length,
                                resolved.start, resolved.end, NULL, &g_doc);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    err = amark_parse_data_create(g_doc);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    return amark_block_parse(g_doc, &resolved, &g_scratch);
}

static void set_content(const char *bytes, size_t length)
{
    NAOS_REQUIRE_UINT_LE(length, BUFFER_SIZE);
    memcpy(g_buffer, bytes, length);
    g_length = length;
}

/*
 * Parses the bytes of a string literal over [start, end) with no limits,
 * requires success and a tree that passes the tree check.
 */
static void parse_range_ok(const char *bytes, size_t length, size_t start,
                           size_t end)
{
    amark_tree_check_result_t check;

    set_content(bytes, length);
    NAOS_REQUIRE_INT_EQ(parse_buffer(start, end, 0, 0), AMARK_ERROR_NONE);
    check = amark_tree_check(g_doc);
    NAOS_CHECK_INT_EQ(check.violation, AMARK_TREE_VIOLATION_NONE);
    NAOS_CHECK_UINT_EQ(check.node, AMARK_NODE_NONE);
}

#define PARSE(s) parse_range_ok((s), sizeof(s) - 1, 0, sizeof(s) - 1)
#define PARSE_RANGE(s, start, end) \
    parse_range_ok((s), sizeof(s) - 1, (start), (end))

/* Checks the block structure of g_doc as amark_test_dump_blocks writes it. */
static void check_dump(const char *want)
{
    size_t length = amark_test_dump_blocks(g_doc, g_dump, DUMP_SIZE);

    NAOS_REQUIRE_UINT_LT(length, DUMP_SIZE);
    NAOS_CHECK_STR_EQ(g_dump, want);
}

/*
 * Returns the nth (from 0) child of the root of g_doc; it must exist.
 */
static amark_node_id_t root_child(size_t nth)
{
    amark_node_id_t node =
        amark_node_first_child(g_doc, amark_document_root(g_doc));

    while (nth > 0) {
        NAOS_REQUIRE_UINT_NE(node, AMARK_NODE_NONE);
        node = amark_node_next_sibling(g_doc, node);
        nth--;
    }
    NAOS_REQUIRE_UINT_NE(node, AMARK_NODE_NONE);
    return node;
}

/* Checks the kind, range and lines of node. */
static void check_node(amark_node_id_t node, amark_kind_t kind, size_t start,
                       size_t end, size_t start_line, size_t end_line)
{
    amark_range_t range;

    NAOS_CHECK_UINT_EQ(amark_node_kind(g_doc, node), kind);
    NAOS_REQUIRE_TRUE(amark_node_range(g_doc, node, &range));
    NAOS_CHECK_UINT_EQ(range.start, start);
    NAOS_CHECK_UINT_EQ(range.end, end);
    NAOS_CHECK_UINT_EQ(amark_node_start_line(g_doc, node), start_line);
    NAOS_CHECK_UINT_EQ(amark_node_end_line(g_doc, node), end_line);
}

/* Checks that the inline content at index is (node, [start, end)). */
static void check_content(size_t index, amark_node_id_t node, size_t start,
                          size_t end)
{
    amark_inline_content_t content;

    NAOS_REQUIRE_TRUE(amark_inline_content_at(g_doc, index, &content));
    NAOS_CHECK_UINT_EQ(content.node, node);
    NAOS_CHECK_UINT_EQ(content.start, start);
    NAOS_CHECK_UINT_EQ(content.end, end);
}

/* Checks the record of line. */
static void check_record(size_t line, size_t content_start, unsigned pad)
{
    amark_line_record_t record;

    NAOS_REQUIRE_TRUE(amark_line_get(g_doc, line, &record));
    NAOS_CHECK_UINT_EQ(record.content_start, content_start);
    NAOS_CHECK_UINT_EQ(record.pad, pad);
}

/* Checks the range and the lines of the root. */
static void check_root(size_t start, size_t end, size_t start_line,
                       size_t end_line)
{
    check_node(amark_document_root(g_doc), AMARK_KIND_DOCUMENT, start, end,
               start_line, end_line);
}

/* ---------------------------------------------------------------------- */
/* Paragraphs and blank lines                                             */
/* ---------------------------------------------------------------------- */

NAOS_TEST(empty_content_gives_the_root_only)
{
    set_content("", 0);
    NAOS_REQUIRE_INT_EQ(parse_buffer(0, 0, 0, 0), AMARK_ERROR_NONE);
    NAOS_CHECK_INT_EQ(amark_tree_check(g_doc).violation,
                      AMARK_TREE_VIOLATION_NONE);
    check_dump("(doc)");
    check_root(0, 0, 1, 1);
    NAOS_CHECK_UINT_EQ(amark_inline_content_count(g_doc), 0);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 1);
}

NAOS_TEST(empty_range_inside_content_gives_the_root_only)
{
    PARSE_RANGE("abc\ndef", 5, 5);
    check_dump("(doc)");
    check_root(5, 5, 2, 2);
    NAOS_CHECK_UINT_EQ(amark_inline_content_count(g_doc), 0);
}

NAOS_TEST(blank_lines_only_give_no_node)
{
    /*     0 1 23 456 */
    PARSE("\n \n\t\r\n");
    check_dump("(doc)");
    check_root(0, 6, 1, 3);
    NAOS_CHECK_UINT_EQ(amark_inline_content_count(g_doc), 0);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 1);
    /* Blank lines: the end of the line's content. */
    check_record(1, 0, 0);
    check_record(2, 2, 0);
    check_record(3, 4, 0);
}

NAOS_TEST(one_line_paragraph)
{
    PARSE("foo");
    check_dump("(doc (p))");
    check_root(0, 3, 1, 1);
    check_node(root_child(0), AMARK_KIND_PARAGRAPH, 0, 3, 1, 1);
    NAOS_CHECK_UINT_EQ(amark_inline_content_count(g_doc), 1);
    check_content(0, root_child(0), 0, 3);
    check_record(1, 0, 0);
}

NAOS_TEST(paragraphs_separated_by_blank_lines)
{
    /*
     * "aa\n" 0-2, "bb  \n" 3-7, "\n" 8, "  \n" 9-11, "  cc\n" 12-16,
     * "dd\n" 17-19.
     */
    PARSE("aa\nbb  \n\n  \n  cc\ndd\n");
    check_dump("(doc (p) (p))");
    check_root(0, 20, 1, 6);

    /* The node ends at the end of the line's content, trailing spaces
     * included; the inline content ends after the last non-space byte. */
    check_node(root_child(0), AMARK_KIND_PARAGRAPH, 0, 7, 1, 2);
    check_node(root_child(1), AMARK_KIND_PARAGRAPH, 14, 19, 5, 6);
    NAOS_CHECK_UINT_EQ(amark_inline_content_count(g_doc), 2);
    check_content(0, root_child(0), 0, 5);
    check_content(1, root_child(1), 14, 19);

    check_record(1, 0, 0);
    check_record(2, 3, 0);
    check_record(3, 8, 0);  /* blank */
    check_record(4, 11, 0); /* spaces only: the end of the line's content */
    check_record(5, 14, 0); /* the first non-space byte */
    check_record(6, 17, 0);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 3);
}

NAOS_TEST(three_paragraphs_in_document_order)
{
    /* "a\n" 0-1, "\n" 2, "b\n" 3-4, "\n" 5, "\n" 6, "c" 7. */
    PARSE("a\n\nb\n\n\nc");
    check_dump("(doc (p) (p) (p))");
    check_node(root_child(0), AMARK_KIND_PARAGRAPH, 0, 1, 1, 1);
    check_node(root_child(1), AMARK_KIND_PARAGRAPH, 3, 4, 3, 3);
    check_node(root_child(2), AMARK_KIND_PARAGRAPH, 7, 8, 6, 6);
    NAOS_CHECK_UINT_EQ(amark_inline_content_count(g_doc), 3);
    check_content(0, root_child(0), 0, 1);
    check_content(1, root_child(1), 3, 4);
    check_content(2, root_child(2), 7, 8);
    /* The node IDs follow the document order too. */
    NAOS_CHECK_UINT_LT(root_child(0), root_child(1));
    NAOS_CHECK_UINT_LT(root_child(1), root_child(2));
}

NAOS_TEST(indented_lines_and_trailing_spaces_with_cr_and_crlf)
{
    /*
     * "  a \r\n" 0-5 (a at 2), " b\t\r" 6-9 (b at 7), "\r" 10, "c" 11.
     */
    PARSE("  a \r\n b\t\r\rc");
    check_dump("(doc (p) (p))");
    check_node(root_child(0), AMARK_KIND_PARAGRAPH, 2, 9, 1, 2);
    check_node(root_child(1), AMARK_KIND_PARAGRAPH, 11, 12, 4, 4);
    check_content(0, root_child(0), 2, 8);
    check_content(1, root_child(1), 11, 12);
    check_record(1, 2, 0);
    check_record(2, 7, 0);
    check_record(3, 10, 0);
    check_record(4, 11, 0);
}

NAOS_TEST(line_with_spaces_and_tabs_only_closes_the_paragraph)
{
    /* "a\n" 0-1, " \t \n" 2-5, "b" 6. */
    PARSE("a\n \t \nb");
    check_dump("(doc (p) (p))");
    check_node(root_child(0), AMARK_KIND_PARAGRAPH, 0, 1, 1, 1);
    check_node(root_child(1), AMARK_KIND_PARAGRAPH, 6, 7, 3, 3);
    check_record(2, 5, 0);
}

NAOS_TEST(continuation_line_indented_with_a_tab)
{
    /*
     * "a\n" 0-1, "\tb" 2-3: an indented line cannot interrupt a paragraph,
     * so it continues it, and its content starts at b with no pad.
     */
    PARSE("a\n\tb");
    check_dump("(doc (p))");
    check_node(root_child(0), AMARK_KIND_PARAGRAPH, 0, 4, 1, 2);
    check_content(0, root_child(0), 0, 4);
    check_record(1, 0, 0);
    check_record(2, 3, 0);
}

NAOS_TEST(unicode_space_is_not_a_space)
{
    /*
     * U+00A0 (C2 A0) and U+3000 (E3 80 80) are not spaces: the line
     * "\xC2\xA0" is not blank and starts a paragraph at its first byte;
     * the trailing U+3000 is part of the inline content.
     */
    PARSE("\xC2\xA0\n\nx\xE3\x80\x80");
    check_dump("(doc (p) (p))");
    check_node(root_child(0), AMARK_KIND_PARAGRAPH, 0, 2, 1, 1);
    check_node(root_child(1), AMARK_KIND_PARAGRAPH, 4, 8, 3, 3);
    check_content(0, root_child(0), 0, 2);
    check_content(1, root_child(1), 4, 8);
}

NAOS_TEST(content_ending_with_a_line_ending)
{
    /* "a\n" is one line; the range covers no line 2. */
    amark_line_record_t record;

    PARSE("a\n");
    check_dump("(doc (p))");
    check_root(0, 2, 1, 1);
    check_node(root_child(0), AMARK_KIND_PARAGRAPH, 0, 1, 1, 1);
    NAOS_CHECK_FALSE(amark_line_get(g_doc, 2, &record));
}

/* ---------------------------------------------------------------------- */
/* Parsed ranges that do not start at offset 0                           */
/* ---------------------------------------------------------------------- */

NAOS_TEST(range_starting_inside_a_line)
{
    /*
     * "xx\n" 0-2, "  foo bar \n" 3-13 (f at 5, r at 11), "\n" 14,
     * "baz\n" 15-18, "yy" 19-20. The range [4, 17) starts after the
     * first space of line 2 and ends inside "baz".
     */
    amark_line_record_t record;

    PARSE_RANGE("xx\n  foo bar \n\nbaz\nyy", 4, 17);
    check_dump("(doc (p) (p))");
    /* The root keeps the parsed range; offsets and lines count from the
     * start of the content. */
    check_root(4, 17, 2, 4);
    check_node(root_child(0), AMARK_KIND_PARAGRAPH, 5, 13, 2, 2);
    check_node(root_child(1), AMARK_KIND_PARAGRAPH, 15, 17, 4, 4);
    check_content(0, root_child(0), 5, 12);
    check_content(1, root_child(1), 15, 17);
    NAOS_CHECK_FALSE(amark_line_get(g_doc, 1, &record));
    check_record(2, 5, 0);
    check_record(3, 14, 0);
    check_record(4, 15, 0);
    NAOS_CHECK_FALSE(amark_line_get(g_doc, 5, &record));
}

NAOS_TEST(range_ending_between_cr_and_lf)
{
    /* "a\r\nb": the range [0, 2) ends after the CR, a line ending. */
    PARSE_RANGE("a\r\nb", 0, 2);
    check_dump("(doc (p))");
    check_root(0, 2, 1, 1);
    check_node(root_child(0), AMARK_KIND_PARAGRAPH, 0, 1, 1, 1);
    check_content(0, root_child(0), 0, 1);
}

NAOS_TEST(range_starting_at_a_lf_after_cr)
{
    /*
     * "a\r\nb\n" with the range [2, 5): the LF at 2 is a line ending of
     * its own, so line 1 of the range is blank and line 2 is "b".
     */
    PARSE_RANGE("a\r\nb\n", 2, 5);
    check_dump("(doc (p))");
    check_root(2, 5, 1, 2);
    check_node(root_child(0), AMARK_KIND_PARAGRAPH, 3, 4, 2, 2);
    check_record(1, 2, 0);
    check_record(2, 3, 0);
}

/* ---------------------------------------------------------------------- */
/* Limits                                                                 */
/* ---------------------------------------------------------------------- */

static const char k_two_paragraphs[] = "a\n\nb";

NAOS_TEST(node_count_limit_exactly_met_succeeds)
{
    /* The root and two paragraphs: 3 nodes. */
    set_content(k_two_paragraphs, sizeof(k_two_paragraphs) - 1);
    NAOS_REQUIRE_INT_EQ(parse_buffer(0, g_length, 0, 3), AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 3);
    NAOS_CHECK_INT_EQ(amark_tree_check(g_doc).violation,
                      AMARK_TREE_VIOLATION_NONE);
    check_dump("(doc (p) (p))");
}

NAOS_TEST(node_count_limit_exceeded_fails)
{
    set_content(k_two_paragraphs, sizeof(k_two_paragraphs) - 1);
    NAOS_CHECK_INT_EQ(parse_buffer(0, g_length, 0, 2),
                      AMARK_ERROR_LIMIT_EXCEEDED);
    /* No node beyond the limit was created. */
    NAOS_CHECK_UINT_LE(amark_document_node_count(g_doc), 2);
    amark_document_destroy(g_doc);
    g_doc = NULL;

    set_content(k_two_paragraphs, sizeof(k_two_paragraphs) - 1);
    NAOS_CHECK_INT_EQ(parse_buffer(0, g_length, 0, 1),
                      AMARK_ERROR_LIMIT_EXCEEDED);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 1);
}

NAOS_TEST(node_count_limit_counts_the_paragraph_closed_at_the_end)
{
    /* The last paragraph is closed at the end of the range. */
    set_content("a", 1);
    NAOS_CHECK_INT_EQ(parse_buffer(0, 1, 0, 1), AMARK_ERROR_LIMIT_EXCEEDED);
    amark_document_destroy(g_doc);
    g_doc = NULL;

    set_content("a", 1);
    NAOS_CHECK_INT_EQ(parse_buffer(0, 1, 0, 2), AMARK_ERROR_NONE);
}

NAOS_TEST(nesting_depth_limit_of_one_allows_paragraphs)
{
    /* A paragraph is a child of the root: depth 1. */
    set_content(k_two_paragraphs, sizeof(k_two_paragraphs) - 1);
    NAOS_REQUIRE_INT_EQ(parse_buffer(0, g_length, 1, 0), AMARK_ERROR_NONE);
    NAOS_CHECK_INT_EQ(amark_tree_check(g_doc).violation,
                      AMARK_TREE_VIOLATION_NONE);
    check_dump("(doc (p) (p))");
}

NAOS_TEST(many_paragraphs)
{
    /* 1000 paragraphs "p\n\n": the root, 1000 nodes and 1000 contents. */
    size_t i;

    for (i = 0; i < 1000; i++) {
        memcpy(g_buffer + i * 3, "p\n\n", 3);
    }
    g_length = 3000;
    NAOS_REQUIRE_INT_EQ(parse_buffer(0, g_length, 1, 1001),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_INT_EQ(amark_tree_check(g_doc).violation,
                      AMARK_TREE_VIOLATION_NONE);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 1001);
    NAOS_REQUIRE_UINT_EQ(amark_inline_content_count(g_doc), 1000);
    for (i = 0; i < 1000; i++) {
        amark_inline_content_t content;

        NAOS_REQUIRE_TRUE(amark_inline_content_at(g_doc, i, &content));
        NAOS_CHECK_UINT_EQ(content.start, i * 3);
        NAOS_CHECK_UINT_EQ(content.end, i * 3 + 1);
        check_record(i * 2 + 1, i * 3, 0);
        check_record(i * 2 + 2, i * 3 + 2, 0);
    }
}

/* ---------------------------------------------------------------------- */
/* Allocation failures                                                    */
/* ---------------------------------------------------------------------- */

NAOS_TEST(allocation_failure_at_every_call)
{
    static const char k_input[] = "a\nb\n\n  c\n\nd e\n";
    size_t fail_at;
    amark_error_t err = AMARK_ERROR_NOMEM;

    for (fail_at = 1; fail_at < 1000; fail_at++) {
        amark_test_alloc_init(&g_state);
        g_state.fail_at = fail_at;
        set_content(k_input, sizeof(k_input) - 1);
        err = parse_buffer(0, g_length, 0, 0);
        g_state.fail_at = 0;
        amark_arena_deinit(&g_scratch);
        if (err == AMARK_ERROR_NONE) {
            break;
        }
        NAOS_CHECK_INT_EQ(err, AMARK_ERROR_NOMEM);
        amark_document_destroy(g_doc);
        g_doc = NULL;
        NAOS_REQUIRE_UINT_EQ(g_state.live, 0);
    }
    NAOS_REQUIRE_INT_EQ(err, AMARK_ERROR_NONE);
    NAOS_CHECK_INT_EQ(amark_tree_check(g_doc).violation,
                      AMARK_TREE_VIOLATION_NONE);
    check_dump("(doc (p) (p) (p))");
}

/* ---------------------------------------------------------------------- */
/* The line position                                                      */
/* ---------------------------------------------------------------------- */

/* Initializes *pos at column 0 of a line of length bytes at offset 0. */
static void pos_init(amark_line_pos_t *pos, size_t length)
{
    amark_line_bounds_t bounds;

    bounds.start = 0;
    bounds.content_end = length;
    bounds.end = length;
    amark_line_pos_init(pos, 1, &bounds);
}

#define POS_INIT(pos, s) pos_init((pos), sizeof(s) - 1)
#define BYTES(s) ((const unsigned char *)(s))

/*
 * Calls amark_line_pos_find_nonspace on *pos and on a copy of it forced to
 * scan again (first_nonspace set to offset), and checks that both give the
 * same results.
 */
static void find_and_compare(amark_line_pos_t *pos, const char *bytes)
{
    amark_line_pos_t fresh = *pos;

    fresh.first_nonspace = fresh.offset;
    amark_line_pos_find_nonspace(pos, BYTES(bytes));
    amark_line_pos_find_nonspace(&fresh, BYTES(bytes));
    NAOS_CHECK_UINT_EQ(pos->first_nonspace, fresh.first_nonspace);
    NAOS_CHECK_UINT_EQ(pos->first_nonspace_column,
                       fresh.first_nonspace_column);
    NAOS_CHECK_UINT_EQ(pos->indent, fresh.indent);
    NAOS_CHECK_INT_EQ(pos->blank, fresh.blank);
}

NAOS_TEST(find_nonspace_counts_tab_stops)
{
    amark_line_pos_t pos;

    /* ' ' ' ' '\t' ' ' 'x': columns 0, 1, 2 (to 4), 4, then x at 5. */
    POS_INIT(&pos, "  \t x");
    amark_line_pos_find_nonspace(&pos, BYTES("  \t x"));
    NAOS_CHECK_UINT_EQ(pos.first_nonspace, 4);
    NAOS_CHECK_UINT_EQ(pos.first_nonspace_column, 5);
    NAOS_CHECK_UINT_EQ(pos.indent, 5);
    NAOS_CHECK_FALSE(pos.blank);
    NAOS_CHECK_UINT_EQ(pos.offset, 0);
    NAOS_CHECK_UINT_EQ(pos.column, 0);
}

NAOS_TEST(find_nonspace_on_a_blank_line)
{
    amark_line_pos_t pos;

    POS_INIT(&pos, " \t ");
    amark_line_pos_find_nonspace(&pos, BYTES(" \t "));
    NAOS_CHECK_UINT_EQ(pos.first_nonspace, 3);
    NAOS_CHECK_UINT_EQ(pos.first_nonspace_column, 5);
    NAOS_CHECK_TRUE(pos.blank);
    /* Advanced partway, the rest is still blank. */
    amark_line_pos_advance(&pos, BYTES(" \t "), 2, AMARK_ADVANCE_COLUMNS);
    find_and_compare(&pos, " \t ");
    NAOS_CHECK_TRUE(pos.blank);
    NAOS_CHECK_UINT_EQ(pos.indent, 3);
}

NAOS_TEST(find_nonspace_reuses_the_earlier_result)
{
    amark_line_pos_t pos;

    /*
     * After one scan, a second call before first_nonspace does not read the
     * bytes again: given other bytes, it still gives the earlier result,
     * with indent and blank computed for the new column.
     */
    POS_INIT(&pos, "    x");
    amark_line_pos_find_nonspace(&pos, BYTES("    x"));
    NAOS_CHECK_UINT_EQ(pos.first_nonspace, 4);
    amark_line_pos_advance(&pos, BYTES("    x"), 1, AMARK_ADVANCE_COLUMNS);
    amark_line_pos_find_nonspace(&pos, BYTES("zzzzz"));
    NAOS_CHECK_UINT_EQ(pos.first_nonspace, 4);
    NAOS_CHECK_UINT_EQ(pos.first_nonspace_column, 4);
    NAOS_CHECK_UINT_EQ(pos.indent, 3);
    NAOS_CHECK_FALSE(pos.blank);

    /* Once offset reaches first_nonspace, it scans again. */
    amark_line_pos_advance(&pos, BYTES("    x"), 3, AMARK_ADVANCE_COLUMNS);
    NAOS_CHECK_UINT_EQ(pos.offset, 4);
    amark_line_pos_find_nonspace(&pos, BYTES("    z "));
    NAOS_CHECK_UINT_EQ(pos.first_nonspace, 4);
    NAOS_CHECK_UINT_EQ(pos.indent, 0);
    amark_line_pos_advance(&pos, BYTES("    x"), 1, AMARK_ADVANCE_BYTES);
    amark_line_pos_find_nonspace(&pos, BYTES("    x"));
    NAOS_CHECK_UINT_EQ(pos.first_nonspace, 5);
    NAOS_CHECK_TRUE(pos.blank);
}

NAOS_TEST(cached_and_fresh_agree_column_by_column)
{
    /*
     * Spaces and tabs at every column alignment: step one column at a time
     * through the leading white space, stopping inside tabs, and compare
     * the cached result with a fresh scan at each step.
     */
    static const char k_line[] = "  \t \t\t   \tx  ";
    amark_line_pos_t pos;
    size_t steps = 0;

    POS_INIT(&pos, k_line);
    amark_line_pos_find_nonspace(&pos, BYTES(k_line));
    NAOS_CHECK_UINT_EQ(pos.first_nonspace, 10);
    NAOS_CHECK_UINT_EQ(pos.first_nonspace_column, 16);
    while (pos.offset < pos.first_nonspace) {
        size_t column = pos.column;

        amark_line_pos_advance(&pos, BYTES(k_line), 1, AMARK_ADVANCE_COLUMNS);
        NAOS_CHECK_UINT_EQ(pos.column, column + 1);
        find_and_compare(&pos, k_line);
        NAOS_CHECK_UINT_EQ(pos.indent, 16 - pos.column);
        NAOS_CHECK_FALSE(pos.blank);
        steps++;
    }
    NAOS_CHECK_UINT_EQ(steps, 16);
    NAOS_CHECK_UINT_EQ(pos.pad, 0);
}

NAOS_TEST(advance_by_columns_stops_inside_a_tab)
{
    amark_line_pos_t pos;

    /* ' ' at column 0, the tab spans columns 1 to 3, x at column 4. */
    POS_INIT(&pos, " \tx");
    amark_line_pos_advance(&pos, BYTES(" \tx"), 2, AMARK_ADVANCE_COLUMNS);
    NAOS_CHECK_UINT_EQ(pos.offset, 1); /* still on the tab */
    NAOS_CHECK_UINT_EQ(pos.column, 2);
    NAOS_CHECK_UINT_EQ(pos.pad, 2);
    find_and_compare(&pos, " \tx");
    NAOS_CHECK_UINT_EQ(pos.indent, 2);

    /* One more column: still inside the tab. */
    amark_line_pos_advance(&pos, BYTES(" \tx"), 1, AMARK_ADVANCE_COLUMNS);
    NAOS_CHECK_UINT_EQ(pos.offset, 1);
    NAOS_CHECK_UINT_EQ(pos.column, 3);
    NAOS_CHECK_UINT_EQ(pos.pad, 1);

    /* The last column of the tab consumes it. */
    amark_line_pos_advance(&pos, BYTES(" \tx"), 1, AMARK_ADVANCE_COLUMNS);
    NAOS_CHECK_UINT_EQ(pos.offset, 2);
    NAOS_CHECK_UINT_EQ(pos.column, 4);
    NAOS_CHECK_UINT_EQ(pos.pad, 0);
    find_and_compare(&pos, " \tx");
    NAOS_CHECK_UINT_EQ(pos.indent, 0);
}

NAOS_TEST(advance_by_columns_over_a_whole_tab)
{
    amark_line_pos_t pos;

    POS_INIT(&pos, " \tx");
    amark_line_pos_advance(&pos, BYTES(" \tx"), 4, AMARK_ADVANCE_COLUMNS);
    NAOS_CHECK_UINT_EQ(pos.offset, 2);
    NAOS_CHECK_UINT_EQ(pos.column, 4);
    NAOS_CHECK_UINT_EQ(pos.pad, 0);
}

NAOS_TEST(advance_by_bytes_consumes_a_tab_whole)
{
    amark_line_pos_t pos;

    /* "ab" then a tab at column 2 that reaches column 4. */
    POS_INIT(&pos, "ab\tc");
    amark_line_pos_advance(&pos, BYTES("ab\tc"), 3, AMARK_ADVANCE_BYTES);
    NAOS_CHECK_UINT_EQ(pos.offset, 3);
    NAOS_CHECK_UINT_EQ(pos.column, 4);
    NAOS_CHECK_UINT_EQ(pos.pad, 0);

    /* A partly consumed tab, advanced by one byte, ends at its tab stop. */
    POS_INIT(&pos, "ab\tc");
    amark_line_pos_advance(&pos, BYTES("ab\tc"), 3, AMARK_ADVANCE_COLUMNS);
    NAOS_CHECK_UINT_EQ(pos.offset, 2);
    NAOS_CHECK_UINT_EQ(pos.column, 3);
    NAOS_CHECK_UINT_EQ(pos.pad, 1);
    amark_line_pos_advance(&pos, BYTES("ab\tc"), 1, AMARK_ADVANCE_BYTES);
    NAOS_CHECK_UINT_EQ(pos.offset, 3);
    NAOS_CHECK_UINT_EQ(pos.column, 4);
    NAOS_CHECK_UINT_EQ(pos.pad, 0);
}

NAOS_TEST(advance_stops_at_the_end_of_the_content)
{
    amark_line_pos_t pos;

    POS_INIT(&pos, " \t");
    amark_line_pos_advance(&pos, BYTES(" \t"), 100, AMARK_ADVANCE_COLUMNS);
    NAOS_CHECK_UINT_EQ(pos.offset, 2);
    NAOS_CHECK_UINT_EQ(pos.column, 4);
    NAOS_CHECK_UINT_EQ(pos.pad, 0);
    POS_INIT(&pos, "ab");
    amark_line_pos_advance(&pos, BYTES("ab"), 100, AMARK_ADVANCE_BYTES);
    NAOS_CHECK_UINT_EQ(pos.offset, 2);
    NAOS_CHECK_UINT_EQ(pos.column, 2);
}

NAOS_SUITE(block_paragraphs,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(empty_content_gives_the_root_only),
    NAOS_ADD_TEST(empty_range_inside_content_gives_the_root_only),
    NAOS_ADD_TEST(blank_lines_only_give_no_node),
    NAOS_ADD_TEST(one_line_paragraph),
    NAOS_ADD_TEST(paragraphs_separated_by_blank_lines),
    NAOS_ADD_TEST(three_paragraphs_in_document_order),
    NAOS_ADD_TEST(indented_lines_and_trailing_spaces_with_cr_and_crlf),
    NAOS_ADD_TEST(line_with_spaces_and_tabs_only_closes_the_paragraph),
    NAOS_ADD_TEST(continuation_line_indented_with_a_tab),
    NAOS_ADD_TEST(unicode_space_is_not_a_space),
    NAOS_ADD_TEST(content_ending_with_a_line_ending)
)

NAOS_SUITE(block_ranges,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(range_starting_inside_a_line),
    NAOS_ADD_TEST(range_ending_between_cr_and_lf),
    NAOS_ADD_TEST(range_starting_at_a_lf_after_cr)
)

NAOS_SUITE(block_limits,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(node_count_limit_exactly_met_succeeds),
    NAOS_ADD_TEST(node_count_limit_exceeded_fails),
    NAOS_ADD_TEST(node_count_limit_counts_the_paragraph_closed_at_the_end),
    NAOS_ADD_TEST(nesting_depth_limit_of_one_allows_paragraphs),
    NAOS_ADD_TEST(many_paragraphs),
    NAOS_ADD_TEST(allocation_failure_at_every_call)
)

NAOS_SUITE(block_line_position,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(find_nonspace_counts_tab_stops),
    NAOS_ADD_TEST(find_nonspace_on_a_blank_line),
    NAOS_ADD_TEST(find_nonspace_reuses_the_earlier_result),
    NAOS_ADD_TEST(cached_and_fresh_agree_column_by_column),
    NAOS_ADD_TEST(advance_by_columns_stops_inside_a_tab),
    NAOS_ADD_TEST(advance_by_columns_over_a_whole_tab),
    NAOS_ADD_TEST(advance_by_bytes_consumes_a_tab_whole),
    NAOS_ADD_TEST(advance_stops_at_the_end_of_the_content)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(block_line_position),
    NAOS_ADD_SUITE(block_paragraphs),
    NAOS_ADD_SUITE(block_ranges),
    NAOS_ADD_SUITE(block_limits)
)
