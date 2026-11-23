/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of the block parser (src/block.h), calling amark_block_parse
 * directly on a document made with amark_document_create and
 * amark_parse_data_create, or through amark_parse: paragraphs and blank
 * lines, thematic breaks and ATX headings, code blocks, HTML blocks, link
 * reference definitions and setext headings, block quotes and lazy
 * continuation lines, lists and their spacing, runs of blank lines (the
 * blank line cutoff), the ranges and lines of their nodes, their
 * attributes, the definition table, their inline contents, the line
 * records, ranges that start past offset 0, the line position (the cached search for the
 * first non-space byte and the two advances), the node count and nesting
 * depth limits, and allocation failures. Every parsed document goes
 * through amark_tree_check.
 */

#include "arena.h"
#include "block.h"
#include "config_internal.h"
#include "definition.h"
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
#include <stdio.h>
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
/* Thematic breaks and ATX headings                                       */
/* ---------------------------------------------------------------------- */

/* Checks the level and the style of a heading node. */
static void check_heading(amark_node_id_t node, unsigned level,
                          amark_heading_style_t style)
{
    NAOS_CHECK_UINT_EQ(amark_heading_level(g_doc, node), level);
    NAOS_CHECK_INT_EQ(amark_heading_style(g_doc, node), style);
}

NAOS_TEST(atx_heading_of_one_line)
{
    PARSE("# foo");
    check_dump("(doc (h 1 atx))");
    check_root(0, 5, 1, 1);
    check_node(root_child(0), AMARK_KIND_HEADING, 0, 5, 1, 1);
    check_heading(root_child(0), 1, AMARK_HEADING_ATX);
    NAOS_CHECK_UINT_EQ(amark_inline_content_count(g_doc), 1);
    check_content(0, root_child(0), 2, 5);
    /* The record of an ATX heading line: the start of its content. */
    check_record(1, 2, 0);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 2);
}

NAOS_TEST(atx_heading_levels_one_to_six)
{
    /*
     * "# 1\n" 0-3, "## 2\n" 4-8, "### 3\n" 9-14, "#### 4\n" 15-21,
     * "##### 5\n" 22-29, "###### 6\n" 30-38, "####### 7" 39-47.
     */
    PARSE("# 1\n## 2\n### 3\n#### 4\n##### 5\n###### 6\n####### 7");
    check_dump("(doc (h 1 atx) (h 2 atx) (h 3 atx) (h 4 atx) (h 5 atx) "
               "(h 6 atx) (p))");
    check_node(root_child(0), AMARK_KIND_HEADING, 0, 3, 1, 1);
    check_node(root_child(1), AMARK_KIND_HEADING, 4, 8, 2, 2);
    check_node(root_child(2), AMARK_KIND_HEADING, 9, 14, 3, 3);
    check_node(root_child(3), AMARK_KIND_HEADING, 15, 21, 4, 4);
    check_node(root_child(4), AMARK_KIND_HEADING, 22, 29, 5, 5);
    check_node(root_child(5), AMARK_KIND_HEADING, 30, 38, 6, 6);
    /* Seven '#' are not a heading. */
    check_node(root_child(6), AMARK_KIND_PARAGRAPH, 39, 48, 7, 7);
    check_heading(root_child(0), 1, AMARK_HEADING_ATX);
    check_heading(root_child(1), 2, AMARK_HEADING_ATX);
    check_heading(root_child(2), 3, AMARK_HEADING_ATX);
    check_heading(root_child(3), 4, AMARK_HEADING_ATX);
    check_heading(root_child(4), 5, AMARK_HEADING_ATX);
    check_heading(root_child(5), 6, AMARK_HEADING_ATX);
    NAOS_CHECK_UINT_EQ(amark_inline_content_count(g_doc), 7);
    check_content(0, root_child(0), 2, 3);
    check_content(1, root_child(1), 7, 8);
    check_content(2, root_child(2), 13, 14);
    check_content(3, root_child(3), 20, 21);
    check_content(4, root_child(4), 28, 29);
    check_content(5, root_child(5), 37, 38);
    check_content(6, root_child(6), 39, 48);
    check_record(1, 2, 0);
    check_record(6, 37, 0);
    check_record(7, 39, 0);
}

NAOS_TEST(atx_heading_closing_sequence_and_trailing_spaces)
{
    /*
     * "##  foo  ##  \n" 0-13: the content is "foo" (4-6), the node keeps
     * the trailing spaces. "# foo#\n" 14-20: '#' not preceded by a space
     * is content. "### b ### x\n" 21-32: not a closing sequence. "#\tq\t#"
     * 33-37: tabs.
     */
    PARSE("##  foo  ##  \n# foo#\n### b ### x\n#\tq\t#");
    check_dump("(doc (h 2 atx) (h 1 atx) (h 3 atx) (h 1 atx))");
    check_node(root_child(0), AMARK_KIND_HEADING, 0, 13, 1, 1);
    check_node(root_child(1), AMARK_KIND_HEADING, 14, 20, 2, 2);
    check_node(root_child(2), AMARK_KIND_HEADING, 21, 32, 3, 3);
    check_node(root_child(3), AMARK_KIND_HEADING, 33, 38, 4, 4);
    check_content(0, root_child(0), 4, 7);
    check_content(1, root_child(1), 16, 20);
    check_content(2, root_child(2), 25, 32);
    check_content(3, root_child(3), 35, 36);
    check_record(1, 4, 0);
    check_record(2, 16, 0);
    check_record(3, 25, 0);
    check_record(4, 35, 0);
}

NAOS_TEST(empty_atx_headings)
{
    /*
     * "#\n" 0-1, "## \n" 2-5, "### ###\n" 6-13, "# #" 14-16. Each has a
     * content of length 0, at the position the scanner gives, and the
     * record is there.
     */
    PARSE("#\n## \n### ###\n# #");
    check_dump("(doc (h 1 atx) (h 2 atx) (h 3 atx) (h 1 atx))");
    check_node(root_child(0), AMARK_KIND_HEADING, 0, 1, 1, 1);
    check_node(root_child(1), AMARK_KIND_HEADING, 2, 5, 2, 2);
    check_node(root_child(2), AMARK_KIND_HEADING, 6, 13, 3, 3);
    check_node(root_child(3), AMARK_KIND_HEADING, 14, 17, 4, 4);
    NAOS_CHECK_UINT_EQ(amark_inline_content_count(g_doc), 4);
    check_content(0, root_child(0), 1, 1);
    check_content(1, root_child(1), 5, 5);
    check_content(2, root_child(2), 10, 10);
    check_content(3, root_child(3), 16, 16);
    check_record(1, 1, 0);
    check_record(2, 5, 0);
    check_record(3, 10, 0);
    check_record(4, 16, 0);
}

NAOS_TEST(not_atx_headings)
{
    /* "#5 bolt", "#hashtag" and "\\# foo" are paragraph text. */
    PARSE("#5 bolt\n#hashtag\n\\# foo");
    check_dump("(doc (p))");
    check_node(root_child(0), AMARK_KIND_PARAGRAPH, 0, 23, 1, 3);
}

NAOS_TEST(atx_heading_indentation)
{
    /*
     * "   # a\n" 0-6: three spaces, a heading from the '#' at 3.
     * "\n" 7. "b\n" 8-9, "    # c" 10-16: four columns of indentation
     * do not start a heading, so the line continues the paragraph.
     */
    PARSE("   # a\n\nb\n    # c");
    check_dump("(doc (h 1 atx) (p))");
    check_node(root_child(0), AMARK_KIND_HEADING, 3, 6, 1, 1);
    check_content(0, root_child(0), 5, 6);
    check_record(1, 5, 0);
    check_node(root_child(1), AMARK_KIND_PARAGRAPH, 8, 17, 3, 4);
    check_record(4, 14, 0);
}

NAOS_TEST(atx_heading_interrupts_a_paragraph)
{
    /* "a\n" 0-1, "# b\n" 2-5, "c" 6. */
    PARSE("a\n# b\nc");
    check_dump("(doc (p) (h 1 atx) (p))");
    check_node(root_child(0), AMARK_KIND_PARAGRAPH, 0, 1, 1, 1);
    check_node(root_child(1), AMARK_KIND_HEADING, 2, 5, 2, 2);
    check_node(root_child(2), AMARK_KIND_PARAGRAPH, 6, 7, 3, 3);
    /* The inline contents follow the document order. */
    NAOS_CHECK_UINT_EQ(amark_inline_content_count(g_doc), 3);
    check_content(0, root_child(0), 0, 1);
    check_content(1, root_child(1), 4, 5);
    check_content(2, root_child(2), 6, 7);
    check_record(1, 0, 0);
    check_record(2, 4, 0);
    check_record(3, 6, 0);
}

NAOS_TEST(atx_headings_with_each_line_ending)
{
    /*
     * "# a\n" 0-3 (LF), "## b #\r" 4-10 (CR), "### c\r\n" 11-17 (CRLF),
     * "#### d " 18-24 (none).
     */
    PARSE("# a\n## b #\r### c\r\n#### d ");
    check_dump("(doc (h 1 atx) (h 2 atx) (h 3 atx) (h 4 atx))");
    check_root(0, 25, 1, 4);
    check_node(root_child(0), AMARK_KIND_HEADING, 0, 3, 1, 1);
    check_node(root_child(1), AMARK_KIND_HEADING, 4, 10, 2, 2);
    check_node(root_child(2), AMARK_KIND_HEADING, 11, 16, 3, 3);
    check_node(root_child(3), AMARK_KIND_HEADING, 18, 25, 4, 4);
    check_content(0, root_child(0), 2, 3);
    check_content(1, root_child(1), 7, 8);
    check_content(2, root_child(2), 15, 16);
    check_content(3, root_child(3), 23, 24);
    check_record(1, 2, 0);
    check_record(2, 7, 0);
    check_record(3, 15, 0);
    check_record(4, 23, 0);
}

NAOS_TEST(thematic_breaks_with_each_line_ending)
{
    /*
     * "***\n" 0-3 (LF), " - - -  \r" 4-12 (CR), "___\r\n" 13-17 (CRLF),
     * "*\t*\t*" 18-22 (none). The node starts at the first character of
     * the break and keeps the trailing white space.
     */
    PARSE("***\n - - -  \r___\r\n*\t*\t*");
    check_dump("(doc (hr) (hr) (hr) (hr))");
    check_root(0, 23, 1, 4);
    check_node(root_child(0), AMARK_KIND_THEMATIC_BREAK, 0, 3, 1, 1);
    check_node(root_child(1), AMARK_KIND_THEMATIC_BREAK, 5, 12, 2, 2);
    check_node(root_child(2), AMARK_KIND_THEMATIC_BREAK, 13, 16, 3, 3);
    check_node(root_child(3), AMARK_KIND_THEMATIC_BREAK, 18, 23, 4, 4);
    /* No inline content; the record is the first character. */
    NAOS_CHECK_UINT_EQ(amark_inline_content_count(g_doc), 0);
    check_record(1, 0, 0);
    check_record(2, 5, 0);
    check_record(3, 13, 0);
    check_record(4, 18, 0);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 5);
}

NAOS_TEST(thematic_break_interrupts_a_paragraph)
{
    /* "a\n" 0-1, "***\n" 2-5, "b\n" 6-7, "   ___" 8-13. */
    PARSE("a\n***\nb\n   ___");
    check_dump("(doc (p) (hr) (p) (hr))");
    check_node(root_child(0), AMARK_KIND_PARAGRAPH, 0, 1, 1, 1);
    check_node(root_child(1), AMARK_KIND_THEMATIC_BREAK, 2, 5, 2, 2);
    check_node(root_child(2), AMARK_KIND_PARAGRAPH, 6, 7, 3, 3);
    check_node(root_child(3), AMARK_KIND_THEMATIC_BREAK, 11, 14, 4, 4);
    NAOS_CHECK_UINT_EQ(amark_inline_content_count(g_doc), 2);
    check_record(2, 2, 0);
    check_record(4, 11, 0);
}

NAOS_TEST(not_thematic_breaks)
{
    /*
     * "**", "+++", "*-*", "_ _ _ a" and "b\n    ***" (indented four
     * columns, continuing the paragraph) are paragraph text.
     */
    PARSE("**\n+++\n*-*\n_ _ _ a\n    ***");
    check_dump("(doc (p))");
    check_node(root_child(0), AMARK_KIND_PARAGRAPH, 0, 26, 1, 5);
}

NAOS_TEST(heading_and_break_in_a_range_starting_inside_a_line)
{
    /*
     * "xx# a\n" 0-5, "---\n" 6-9, "yy" 10-11. The range [2, 10) starts
     * at the '#': column 0 of the first line.
     */
    PARSE_RANGE("xx# a\n---\nyy", 2, 10);
    check_dump("(doc (h 1 atx) (hr))");
    check_root(2, 10, 1, 2);
    check_node(root_child(0), AMARK_KIND_HEADING, 2, 5, 1, 1);
    check_node(root_child(1), AMARK_KIND_THEMATIC_BREAK, 6, 9, 2, 2);
    check_content(0, root_child(0), 4, 5);
    check_record(1, 4, 0);
    check_record(2, 6, 0);
}

NAOS_TEST(node_count_limit_counts_headings_and_breaks)
{
    set_content("# a\n***", 7);
    NAOS_CHECK_INT_EQ(parse_buffer(0, 7, 0, 2), AMARK_ERROR_LIMIT_EXCEEDED);
    NAOS_CHECK_UINT_LE(amark_document_node_count(g_doc), 2);
    amark_document_destroy(g_doc);
    g_doc = NULL;

    set_content("# a\n***", 7);
    NAOS_REQUIRE_INT_EQ(parse_buffer(0, 7, 1, 3), AMARK_ERROR_NONE);
    check_dump("(doc (h 1 atx) (hr))");
}

NAOS_TEST(allocation_failure_with_headings_and_breaks)
{
    static const char k_input[] = "# a\nb\n***\n## c ##\n\n#\n";
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
    check_dump("(doc (h 1 atx) (p) (hr) (h 2 atx) (h 1 atx))");
}

/* ---------------------------------------------------------------------- */
/* Indented and fenced code blocks                                        */
/* ---------------------------------------------------------------------- */

/*
 * Checks the code block attributes of node. has_info false means the info
 * string is not written; info_start and info_end are then not read.
 */
static void check_code(amark_node_id_t node, amark_code_block_style_t style,
                       char fence_char, size_t fence_length, bool has_info,
                       size_t info_start, size_t info_end)
{
    amark_range_t info;

    NAOS_CHECK_INT_EQ(amark_code_block_style(g_doc, node), style);
    NAOS_CHECK_INT_EQ(amark_code_block_fence_char(g_doc, node), fence_char);
    NAOS_CHECK_UINT_EQ(amark_code_block_fence_length(g_doc, node),
                       fence_length);
    if (has_info) {
        NAOS_REQUIRE_TRUE(amark_code_block_info(g_doc, node, &info));
        NAOS_CHECK_UINT_EQ(info.start, info_start);
        NAOS_CHECK_UINT_EQ(info.end, info_end);
    } else {
        NAOS_CHECK_FALSE(amark_code_block_info(g_doc, node, &info));
    }
}

NAOS_TEST(indented_code_block_of_one_line)
{
    PARSE("    foo");
    check_dump("(doc (code indented))");
    check_node(root_child(0), AMARK_KIND_CODE_BLOCK, 4, 7, 1, 1);
    check_code(root_child(0), AMARK_CODE_BLOCK_INDENTED, '\0', 0, false, 0,
               0);
    /* Code is not inline content. The record: after the 4 columns. */
    NAOS_CHECK_UINT_EQ(amark_inline_content_count(g_doc), 0);
    check_record(1, 4, 0);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 2);
}

NAOS_TEST(indented_code_keeps_inner_blank_lines_only)
{
    /*
     * "    a\n" 0-5, "\n" 6, "      b\n" 7-14, "  \n" 15-17, "\n" 18. The
     * block ends at the end of the last line that is not blank; the blank
     * line inside stays.
     */
    PARSE("    a\n\n      b\n  \n\n");
    check_dump("(doc (code indented))");
    check_node(root_child(0), AMARK_KIND_CODE_BLOCK, 4, 14, 1, 3);
    /* Blank lines with less than 4 columns: the end of the content. */
    check_record(1, 4, 0);
    check_record(2, 6, 0);
    check_record(3, 11, 0);
    check_record(4, 17, 0);
    check_record(5, 18, 0);
    NAOS_CHECK_UINT_EQ(amark_inline_content_count(g_doc), 0);
}

NAOS_TEST(indented_code_blank_line_of_four_columns_or_more)
{
    /* "    a\n" 0-5, "      \n" 6-12, "    b" 13-17. */
    PARSE("    a\n      \n    b");
    check_dump("(doc (code indented))");
    check_node(root_child(0), AMARK_KIND_CODE_BLOCK, 4, 18, 1, 3);
    /* A blank line with 4 columns or more: after the 4 columns. */
    check_record(2, 10, 0);
    check_record(3, 17, 0);
}

NAOS_TEST(indented_code_does_not_start_on_a_blank_line)
{
    /* "    \n" 0-4, "    a" 5-9. */
    PARSE("    \n    a");
    check_dump("(doc (code indented))");
    check_node(root_child(0), AMARK_KIND_CODE_BLOCK, 9, 10, 2, 2);
    check_record(1, 4, 0);
    check_record(2, 9, 0);
}

NAOS_TEST(indented_code_does_not_interrupt_a_paragraph)
{
    /* "a\n" 0-1, "    b" 2-6: a continuation line of the paragraph. */
    PARSE("a\n    b");
    check_dump("(doc (p))");
    check_node(root_child(0), AMARK_KIND_PARAGRAPH, 0, 7, 1, 2);
    check_content(0, root_child(0), 0, 7);
    check_record(2, 6, 0);

    amark_document_destroy(g_doc);
    g_doc = NULL;
    /* "a\n" 0-1, "\n" 2, "    b" 3-7: after a blank line it is code. */
    PARSE("a\n\n    b");
    check_dump("(doc (p) (code indented))");
    check_node(root_child(1), AMARK_KIND_CODE_BLOCK, 7, 8, 3, 3);
    check_record(3, 7, 0);
}

NAOS_TEST(indented_code_after_a_heading_and_before_a_paragraph)
{
    /* "# h\n" 0-3, "    c\n" 4-9, "d" 10. */
    PARSE("# h\n    c\nd");
    check_dump("(doc (h 1 atx) (code indented) (p))");
    check_node(root_child(1), AMARK_KIND_CODE_BLOCK, 8, 9, 2, 2);
    check_node(root_child(2), AMARK_KIND_PARAGRAPH, 10, 11, 3, 3);
    NAOS_CHECK_UINT_EQ(amark_inline_content_count(g_doc), 2);
    check_content(0, root_child(0), 2, 3);
    check_content(1, root_child(2), 10, 11);
    check_record(2, 8, 0);
    check_record(3, 10, 0);
}

NAOS_TEST(indented_code_with_tabs_and_line_endings)
{
    /*
     * "\tfoo\r\n" 0-5, "  \tbar\r" 6-12, "    \tbaz" 13-21. A tab that
     * ends at column 4 is consumed whole; a tab after the 4 columns is
     * content.
     */
    PARSE("\tfoo\r\n  \tbar\r    \tbaz");
    check_dump("(doc (code indented))");
    check_node(root_child(0), AMARK_KIND_CODE_BLOCK, 1, 21, 1, 3);
    check_record(1, 1, 0);
    check_record(2, 9, 0);
    check_record(3, 17, 0);
}

NAOS_TEST(fence_indented_four_columns_is_indented_code)
{
    /* "    ```\n" 0-7, "    a" 8-12. */
    PARSE("    ```\n    a");
    check_dump("(doc (code indented))");
    check_node(root_child(0), AMARK_KIND_CODE_BLOCK, 4, 13, 1, 2);
}

NAOS_TEST(fenced_code_block)
{
    /* "```\n" 0-3, "foo\n" 4-7, "```" 8-10. */
    PARSE("```\nfoo\n```");
    check_dump("(doc (code fenced ` 3))");
    check_node(root_child(0), AMARK_KIND_CODE_BLOCK, 0, 11, 1, 3);
    check_code(root_child(0), AMARK_CODE_BLOCK_FENCED, '`', 3, false, 0, 0);
    /* The fence lines: their first character; the code: after the fence
     * indentation (none). */
    check_record(1, 0, 0);
    check_record(2, 4, 0);
    check_record(3, 8, 0);
    NAOS_CHECK_UINT_EQ(amark_inline_content_count(g_doc), 0);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 2);
}

NAOS_TEST(fenced_code_block_info_and_tildes)
{
    /*
     * "~~~~  ruby startline=3 \t\n" 0-24 (info 6-22), "x\n" 25-26,
     * "~~~~~\n" 27-32.
     */
    PARSE("~~~~  ruby startline=3 \t\nx\n~~~~~\n");
    check_dump("(doc (code fenced ~ 4 info))");
    check_node(root_child(0), AMARK_KIND_CODE_BLOCK, 0, 32, 1, 3);
    check_code(root_child(0), AMARK_CODE_BLOCK_FENCED, '~', 4, true, 6, 22);
    check_record(1, 0, 0);
    check_record(2, 25, 0);
    check_record(3, 27, 0);
}

NAOS_TEST(fenced_code_block_with_each_line_ending)
{
    /* "```\r\n" 0-4, "a\r" 5-6, "b\n" 7-8, "```" 9-11. */
    PARSE("```\r\na\rb\n```");
    check_dump("(doc (code fenced ` 3))");
    check_node(root_child(0), AMARK_KIND_CODE_BLOCK, 0, 12, 1, 4);
    check_record(1, 0, 0);
    check_record(2, 5, 0);
    check_record(3, 7, 0);
    check_record(4, 9, 0);
}

NAOS_TEST(lines_that_do_not_close_a_fence)
{
    /*
     * "````\n" 0-4, "a\n" 5-6, "```\n" 7-10 (shorter), "~~~~\n" 11-15
     * (other character), "    ````\n" 16-24 (4 columns), "```` x\n" 25-31
     * (text after it), "   `````  \n" 32-42 (closes), "b" 43.
     */
    PARSE("````\na\n```\n~~~~\n    ````\n```` x\n   `````  \nb");
    check_dump("(doc (code fenced ` 4) (p))");
    check_node(root_child(0), AMARK_KIND_CODE_BLOCK, 0, 42, 1, 7);
    check_node(root_child(1), AMARK_KIND_PARAGRAPH, 43, 44, 8, 8);
    check_record(2, 5, 0);
    check_record(3, 7, 0);
    check_record(4, 11, 0);
    check_record(5, 16, 0);
    check_record(6, 25, 0);
    check_record(7, 35, 0);
    check_record(8, 43, 0);
    NAOS_CHECK_UINT_EQ(amark_inline_content_count(g_doc), 1);
}

NAOS_TEST(unclosed_fence_closes_at_the_end_of_the_range)
{
    /*
     * "```\n" 0-3, "a\n" 4-5, "\n" 6: the blank line belongs to the code,
     * so the block ends at the end of its (empty) content. A range that
     * ends where an empty line starts reports the line before it as its end
     * line (syntax-tree: the line of end - 1).
     */
    PARSE("```\na\n\n");
    check_dump("(doc (code fenced ` 3))");
    check_node(root_child(0), AMARK_KIND_CODE_BLOCK, 0, 6, 1, 2);
    check_record(3, 6, 0);

    amark_document_destroy(g_doc);
    g_doc = NULL;
    /* "```\n" 0-3, "a\n" 4-5, "  " 6-7: a blank line with white space. */
    PARSE("```\na\n  ");
    check_node(root_child(0), AMARK_KIND_CODE_BLOCK, 0, 8, 1, 3);
    check_record(3, 6, 0);

    amark_document_destroy(g_doc);
    g_doc = NULL;
    /* "~~~ x\n" 0-5, "foo" 6-8. */
    PARSE("~~~ x\nfoo");
    check_dump("(doc (code fenced ~ 3 info))");
    check_node(root_child(0), AMARK_KIND_CODE_BLOCK, 0, 9, 1, 2);
    check_code(root_child(0), AMARK_CODE_BLOCK_FENCED, '~', 3, true, 4, 5);

    amark_document_destroy(g_doc);
    g_doc = NULL;
    /* An opening fence alone: "```" 0-2. */
    PARSE("```");
    check_dump("(doc (code fenced ` 3))");
    check_node(root_child(0), AMARK_KIND_CODE_BLOCK, 0, 3, 1, 1);
}

NAOS_TEST(fence_indentation_is_removed_from_code_lines)
{
    /*
     * "  ```\n" 0-5, "    a\n" 6-11, " b\n" 12-14, "c\n" 15-16, " \n" 17-18,
     * "\tx\n" 19-21, "  ```" 22-26. Up to 2 columns of white space go;
     * the tab gives 2 of its 4 columns and keeps 2 as pad.
     */
    PARSE("  ```\n    a\n b\nc\n \n\tx\n  ```");
    check_dump("(doc (code fenced ` 3))");
    check_node(root_child(0), AMARK_KIND_CODE_BLOCK, 2, 27, 1, 7);
    check_record(1, 2, 0);
    check_record(2, 8, 0);
    check_record(3, 13, 0);
    check_record(4, 15, 0);
    check_record(5, 18, 0);
    check_record(6, 19, 2);
    check_record(7, 24, 0);
}

NAOS_TEST(fenced_code_interrupts_a_paragraph_and_holds_raw_lines)
{
    /* "a\n" 0-1, "```\n" 2-5, "# b\n" 6-9, "***\n" 10-13, "```\n" 14-17,
     * "c" 18. */
    PARSE("a\n```\n# b\n***\n```\nc");
    check_dump("(doc (p) (code fenced ` 3) (p))");
    check_node(root_child(0), AMARK_KIND_PARAGRAPH, 0, 1, 1, 1);
    check_node(root_child(1), AMARK_KIND_CODE_BLOCK, 2, 17, 2, 5);
    check_node(root_child(2), AMARK_KIND_PARAGRAPH, 18, 19, 6, 6);
    NAOS_CHECK_UINT_EQ(amark_inline_content_count(g_doc), 2);
    check_content(0, root_child(0), 0, 1);
    check_content(1, root_child(2), 18, 19);
    check_record(3, 6, 0);
    check_record(4, 10, 0);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 4);
}

NAOS_TEST(backtick_in_the_info_of_a_backtick_fence_is_not_a_fence)
{
    PARSE("``` a`b\nc");
    check_dump("(doc (p))");
}

NAOS_TEST(fenced_code_in_a_range_starting_inside_a_line)
{
    /* "xx" 0-1 outside, "```\n" 2-5, "foo" 6-8. */
    PARSE_RANGE("xx```\nfoo", 2, 9);
    check_dump("(doc (code fenced ` 3))");
    check_node(root_child(0), AMARK_KIND_CODE_BLOCK, 2, 9, 1, 2);
    check_record(1, 2, 0);
    check_record(2, 6, 0);
}

NAOS_TEST(node_count_limit_counts_code_blocks)
{
    set_content("    a\n```\nb", 11);
    NAOS_CHECK_INT_EQ(parse_buffer(0, 11, 0, 2), AMARK_ERROR_LIMIT_EXCEEDED);
    amark_document_destroy(g_doc);
    g_doc = NULL;

    set_content("    a\n```\nb", 11);
    NAOS_REQUIRE_INT_EQ(parse_buffer(0, 11, 1, 3), AMARK_ERROR_NONE);
    check_dump("(doc (code indented) (code fenced ` 3))");
}

NAOS_TEST(allocation_failure_with_code_blocks)
{
    static const char k_input[] =
        "a\n```x\nb\n```\n    c\n\n    d\n~~~\ne\n";
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
    check_dump("(doc (p) (code fenced ` 3 info) (code indented) "
               "(code fenced ~ 3))");
}

/* ---------------------------------------------------------------------- */
/* HTML blocks                                                            */
/* ---------------------------------------------------------------------- */

/* Destroys g_doc so that the test can parse again. */
static void reset_doc(void)
{
    amark_document_destroy(g_doc);
    g_doc = NULL;
}

NAOS_TEST(html_block_conditions_one_to_five_close_on_the_end_line)
{
    /*
     * 1: "<pre x>\n" 0-7, "a\n" 8-9, "</PRE> b\n" 10-18, "c" 19. The end
     * tag is compared without regard to case; the line with it belongs to
     * the block.
     */
    PARSE("<pre x>\na\n</PRE> b\nc");
    check_dump("(doc (html) (p))");
    check_node(root_child(0), AMARK_KIND_HTML_BLOCK, 0, 18, 1, 3);
    check_node(root_child(1), AMARK_KIND_PARAGRAPH, 19, 20, 4, 4);
    check_record(1, 0, 0);
    check_record(2, 8, 0);
    check_record(3, 10, 0);
    check_record(4, 19, 0);
    /* HTML is not inline content. */
    NAOS_CHECK_UINT_EQ(amark_inline_content_count(g_doc), 1);
    check_content(0, root_child(1), 19, 20);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 3);
    reset_doc();

    /* 2: "<!--\n" 0-4, "# x\n" 5-8, "-->\n" 9-12, "c" 13. */
    PARSE("<!--\n# x\n-->\nc");
    check_dump("(doc (html) (p))");
    check_node(root_child(0), AMARK_KIND_HTML_BLOCK, 0, 12, 1, 3);
    check_node(root_child(1), AMARK_KIND_PARAGRAPH, 13, 14, 4, 4);
    check_record(2, 5, 0);
    check_record(3, 9, 0);
    reset_doc();

    /*
     * 3: "<?php\n" 0-5, "\n" 6, "?>a\n" 7-10, "b" 11. A blank line does not
     * end conditions 1 to 5.
     */
    PARSE("<?php\n\n?>a\nb");
    check_dump("(doc (html) (p))");
    check_node(root_child(0), AMARK_KIND_HTML_BLOCK, 0, 10, 1, 3);
    check_node(root_child(1), AMARK_KIND_PARAGRAPH, 11, 12, 4, 4);
    check_record(2, 6, 0);
    check_record(3, 7, 0);
    reset_doc();

    /* 4: "<!DOCTYPE\n" 0-9, "html>\n" 10-15, "b" 16. */
    PARSE("<!DOCTYPE\nhtml>\nb");
    check_dump("(doc (html) (p))");
    check_node(root_child(0), AMARK_KIND_HTML_BLOCK, 0, 15, 1, 2);
    check_node(root_child(1), AMARK_KIND_PARAGRAPH, 16, 17, 3, 3);
    reset_doc();

    /* 5: "<![CDATA[\n" 0-9, "x ]]> y\n" 10-17, "b" 18. */
    PARSE("<![CDATA[\nx ]]> y\nb");
    check_dump("(doc (html) (p))");
    check_node(root_child(0), AMARK_KIND_HTML_BLOCK, 0, 17, 1, 2);
    check_node(root_child(1), AMARK_KIND_PARAGRAPH, 18, 19, 3, 3);
    check_record(2, 10, 0);
}

NAOS_TEST(html_block_closes_on_its_start_line)
{
    /* "<!-- a -->\n" 0-10, "b" 11. */
    PARSE("<!-- a -->\nb");
    check_dump("(doc (html) (p))");
    check_node(root_child(0), AMARK_KIND_HTML_BLOCK, 0, 10, 1, 1);
    check_node(root_child(1), AMARK_KIND_PARAGRAPH, 11, 12, 2, 2);
    check_record(1, 0, 0);
    reset_doc();

    /*
     * "  <script>x</script>  \n" 0-22, "b" 23. The block and its record
     * start after the prefix, its indentation included; it ends at the end
     * of the line's content, trailing spaces included.
     */
    PARSE("  <script>x</script>  \nb");
    check_dump("(doc (html) (p))");
    check_node(root_child(0), AMARK_KIND_HTML_BLOCK, 0, 22, 1, 1);
    check_node(root_child(1), AMARK_KIND_PARAGRAPH, 23, 24, 2, 2);
    check_record(1, 0, 0);
    check_record(2, 23, 0);
    reset_doc();

    /* Two blocks: "<?a?>\n" 0-5, "<!X>" 6-9. */
    PARSE("<?a?>\n<!X>");
    check_dump("(doc (html) (html))");
    check_node(root_child(0), AMARK_KIND_HTML_BLOCK, 0, 5, 1, 1);
    check_node(root_child(1), AMARK_KIND_HTML_BLOCK, 6, 10, 2, 2);
}

NAOS_TEST(html_block_conditions_six_and_seven_end_before_a_blank_line)
{
    /*
     * 6: "<div>\n" 0-5, "*a*\n" 6-9, "\n" 10, "b" 11. The blank line is not
     * part of the block.
     */
    PARSE("<div>\n*a*\n\nb");
    check_dump("(doc (html) (p))");
    check_node(root_child(0), AMARK_KIND_HTML_BLOCK, 0, 9, 1, 2);
    check_node(root_child(1), AMARK_KIND_PARAGRAPH, 11, 12, 4, 4);
    check_record(1, 0, 0);
    check_record(2, 6, 0);
    check_record(3, 10, 0);
    check_record(4, 11, 0);
    NAOS_CHECK_UINT_EQ(amark_inline_content_count(g_doc), 1);
    reset_doc();

    /*
     * 7: "<a href=\"x\">\n" 0-12, "# b\n" 13-16, "  \n" 17-19, "c" 20. A
     * line of spaces is blank too; its record is the end of its content.
     */
    PARSE("<a href=\"x\">\n# b\n  \nc");
    check_dump("(doc (html) (p))");
    check_node(root_child(0), AMARK_KIND_HTML_BLOCK, 0, 16, 1, 2);
    check_node(root_child(1), AMARK_KIND_PARAGRAPH, 20, 21, 4, 4);
    check_record(2, 13, 0);
    check_record(3, 19, 0);
    reset_doc();

    /* 6, a closing tag in upper case, to the end of the range: 0-7. */
    PARSE("</DIV>\nx");
    check_dump("(doc (html))");
    check_node(root_child(0), AMARK_KIND_HTML_BLOCK, 0, 8, 1, 2);
    reset_doc();

    /* 7, a closing tag of any name: "</custom>\n" 0-9, "x" 10. */
    PARSE("</custom>\nx");
    check_dump("(doc (html))");
    check_node(root_child(0), AMARK_KIND_HTML_BLOCK, 0, 11, 1, 2);
    NAOS_CHECK_UINT_EQ(amark_inline_content_count(g_doc), 0);
}

NAOS_TEST(html_block_condition_seven_does_not_interrupt_a_paragraph)
{
    /* "a\n" 0-1, "<span>\n" 2-8, "b" 9: one paragraph. */
    PARSE("a\n<span>\nb");
    check_dump("(doc (p))");
    check_node(root_child(0), AMARK_KIND_PARAGRAPH, 0, 10, 1, 3);
    check_content(0, root_child(0), 0, 10);
    reset_doc();

    /* After a blank line it starts: "a\n" 0-1, "\n" 2, "<span>\n" 3-9,
     * "b" 10. */
    PARSE("a\n\n<span>\nb");
    check_dump("(doc (p) (html))");
    check_node(root_child(1), AMARK_KIND_HTML_BLOCK, 3, 11, 3, 4);
    check_record(3, 3, 0);
    check_record(4, 10, 0);
    reset_doc();

    /* 6 interrupts: "a\n" 0-1, "<DIV>\n" 2-7, "b" 8. */
    PARSE("a\n<DIV>\nb");
    check_dump("(doc (p) (html))");
    check_node(root_child(0), AMARK_KIND_PARAGRAPH, 0, 1, 1, 1);
    check_node(root_child(1), AMARK_KIND_HTML_BLOCK, 2, 9, 2, 3);
    reset_doc();

    /* 1 to 5 interrupt: "a\n" 0-1, "<!-- c -->" 2-11. */
    PARSE("a\n<!-- c -->");
    check_dump("(doc (p) (html))");
    check_node(root_child(1), AMARK_KIND_HTML_BLOCK, 2, 12, 2, 2);
    reset_doc();

    PARSE("a\n<pre>\nb");
    check_dump("(doc (p) (html))");
}

NAOS_TEST(html_block_lines_are_raw)
{
    /*
     * "<table>\n" 0-7, "# a\n" 8-11, "```\n" 12-15, "    b\n" 16-21,
     * "- c\n" 22-25, "</table>" 26-33: no other block starts inside.
     */
    PARSE("<table>\n# a\n```\n    b\n- c\n</table>");
    check_dump("(doc (html))");
    check_node(root_child(0), AMARK_KIND_HTML_BLOCK, 0, 34, 1, 6);
    check_record(2, 8, 0);
    check_record(3, 12, 0);
    check_record(4, 16, 0);
    check_record(5, 22, 0);
    check_record(6, 26, 0);
    NAOS_CHECK_UINT_EQ(amark_inline_content_count(g_doc), 0);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 2);
    reset_doc();

    /* An HTML block started inside does not end the block: "<!--\n" 0-4,
     * "<div>\n" 5-10, "-->" 11-13. */
    PARSE("<!--\n<div>\n-->");
    check_dump("(doc (html))");
    check_node(root_child(0), AMARK_KIND_HTML_BLOCK, 0, 14, 1, 3);
}

NAOS_TEST(html_block_indentation)
{
    /* "   <div>\n" 0-8, "  a" 9-11. */
    PARSE("   <div>\n  a");
    check_dump("(doc (html))");
    check_node(root_child(0), AMARK_KIND_HTML_BLOCK, 0, 12, 1, 2);
    check_record(1, 0, 0);
    check_record(2, 9, 0);
    reset_doc();

    PARSE("    <div>");
    check_dump("(doc (code indented))");
    reset_doc();

    PARSE("  \t<!-- -->");
    check_dump("(doc (code indented))");
    reset_doc();

    /* Condition 7 needs only white space after the tag. */
    PARSE("<span> x");
    check_dump("(doc (p))");
}

NAOS_TEST(html_block_with_each_line_ending)
{
    /* "<div>\r\n" 0-6, "a\r" 7-8, "b\n" 9-10, "\r\n" 11-12, "c" 13. */
    PARSE("<div>\r\na\rb\n\r\nc");
    check_dump("(doc (html) (p))");
    check_node(root_child(0), AMARK_KIND_HTML_BLOCK, 0, 10, 1, 3);
    check_node(root_child(1), AMARK_KIND_PARAGRAPH, 13, 14, 5, 5);
    check_record(2, 7, 0);
    check_record(3, 9, 0);
    check_record(4, 11, 0);
    reset_doc();

    /* "<!--\r" 0-4, "-->\r" 5-8, "x" 9. */
    PARSE("<!--\r-->\rx");
    check_dump("(doc (html) (p))");
    check_node(root_child(0), AMARK_KIND_HTML_BLOCK, 0, 8, 1, 2);
    check_node(root_child(1), AMARK_KIND_PARAGRAPH, 9, 10, 3, 3);
}

NAOS_TEST(unclosed_html_block_closes_at_the_end_of_the_range)
{
    /*
     * "<!--\n" 0-4, "a\n" 5-6, "\n" 7: the blank line belongs to the block
     * (conditions 1 to 5 do not end on a blank line), so the block ends at
     * the end of its (empty) content, 7, and its end line is 2. This is the
     * design gap recorded in the 7.2 Implementation Note of tasks.md (the
     * same as unclosed_fence_closes_at_the_end_of_the_range); the range
     * rules are followed as written until it is decided upstream.
     */
    PARSE("<!--\na\n\n");
    check_dump("(doc (html))");
    check_node(root_child(0), AMARK_KIND_HTML_BLOCK, 0, 7, 1, 2);
    check_record(3, 7, 0);
    reset_doc();

    /* "<!--\n" 0-4, "a\n" 5-6, "  " 7-8: the record keeps the spaces. */
    PARSE("<!--\na\n  ");
    check_node(root_child(0), AMARK_KIND_HTML_BLOCK, 0, 9, 1, 3);
    check_record(3, 7, 0);
    reset_doc();

    /* A start line alone: "<style" 0-5. */
    PARSE("<style");
    check_dump("(doc (html))");
    check_node(root_child(0), AMARK_KIND_HTML_BLOCK, 0, 6, 1, 1);
}

NAOS_TEST(html_block_in_a_range_starting_inside_a_line)
{
    /* "xx" 0-1 outside, "<div>\n" 2-7, "a" 8. */
    PARSE_RANGE("xx<div>\na", 2, 9);
    check_dump("(doc (html))");
    check_node(root_child(0), AMARK_KIND_HTML_BLOCK, 2, 9, 1, 2);
    check_record(1, 2, 0);
    check_record(2, 8, 0);
}

NAOS_TEST(node_count_limit_counts_html_blocks)
{
    set_content("<div>\n\n<!-- -->", 15);
    NAOS_CHECK_INT_EQ(parse_buffer(0, 15, 0, 2), AMARK_ERROR_LIMIT_EXCEEDED);
    reset_doc();

    set_content("<div>\n\n<!-- -->", 15);
    NAOS_REQUIRE_INT_EQ(parse_buffer(0, 15, 1, 3), AMARK_ERROR_NONE);
    check_dump("(doc (html) (html))");
}

/* ---------------------------------------------------------------------- */
/* Link reference definitions and setext headings                         */
/* ---------------------------------------------------------------------- */

/*
 * Checks the label, destination and title of a definition node; has_title
 * false means no title is set (title_start and title_end are then unused).
 */
static void check_definition(amark_node_id_t node, size_t label_start,
                             size_t label_end, size_t dest_start,
                             size_t dest_end, bool has_title,
                             size_t title_start, size_t title_end)
{
    amark_range_t range;

    NAOS_REQUIRE_TRUE(amark_definition_label(g_doc, node, &range));
    NAOS_CHECK_UINT_EQ(range.start, label_start);
    NAOS_CHECK_UINT_EQ(range.end, label_end);
    NAOS_REQUIRE_TRUE(amark_definition_destination(g_doc, node, &range));
    NAOS_CHECK_UINT_EQ(range.start, dest_start);
    NAOS_CHECK_UINT_EQ(range.end, dest_end);
    if (has_title) {
        NAOS_REQUIRE_TRUE(amark_definition_title(g_doc, node, &range));
        NAOS_CHECK_UINT_EQ(range.start, title_start);
        NAOS_CHECK_UINT_EQ(range.end, title_end);
    } else {
        NAOS_CHECK_FALSE(amark_definition_title(g_doc, node, &range));
    }
}

/* The number of entries in the definition table of g_doc. */
static size_t definition_count(void)
{
    return amark_document_parse_data(g_doc)->definition_count;
}

NAOS_TEST(paragraph_of_definitions_only_gives_no_paragraph)
{
    /*
     * "[foo]: /url \"t\"\n" 0-15, "[bar]: <b>\n" 16-26. Each definition
     * becomes a node ending at the end of its last line's content; nothing
     * is left for a paragraph.
     */
    PARSE("[foo]: /url \"t\"\n[bar]: <b>\n");
    check_dump("(doc (def) (def))");
    check_root(0, 27, 1, 2);
    check_node(root_child(0), AMARK_KIND_LINK_REFERENCE_DEFINITION, 0, 15, 1,
               1);
    check_definition(root_child(0), 1, 4, 7, 11, true, 13, 14);
    check_node(root_child(1), AMARK_KIND_LINK_REFERENCE_DEFINITION, 16, 26, 2,
               2);
    check_definition(root_child(1), 17, 20, 24, 25, false, 0, 0);
    /* Definitions are not inline content. */
    NAOS_CHECK_UINT_EQ(amark_inline_content_count(g_doc), 0);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 3);
    check_record(1, 0, 0);
    check_record(2, 16, 0);

    NAOS_CHECK_UINT_EQ(definition_count(), 2);
    NAOS_CHECK_UINT_EQ(amark_definitions_find(g_doc, 1, 4), root_child(0));
    NAOS_CHECK_UINT_EQ(amark_definitions_find(g_doc, 17, 20), root_child(1));
    /* "url" is no label of the document. */
    NAOS_CHECK_UINT_EQ(amark_definitions_find(g_doc, 8, 11), AMARK_NODE_NONE);
}

NAOS_TEST(definition_followed_by_paragraph_content)
{
    /* "[a]: /u\n" 0-7, "bar\n" 8-11, "baz" 12-14. */
    PARSE("[a]: /u\nbar\nbaz");
    check_dump("(doc (def) (p))");
    check_node(root_child(0), AMARK_KIND_LINK_REFERENCE_DEFINITION, 0, 7, 1,
               1);
    check_definition(root_child(0), 1, 2, 5, 7, false, 0, 0);
    /* The paragraph starts at what is left. */
    check_node(root_child(1), AMARK_KIND_PARAGRAPH, 8, 15, 2, 3);
    NAOS_CHECK_UINT_EQ(amark_inline_content_count(g_doc), 1);
    check_content(0, root_child(1), 8, 15);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 3);
    check_record(1, 0, 0);
    check_record(2, 8, 0);
    check_record(3, 12, 0);
}

NAOS_TEST(definition_spanning_lines_with_title_on_the_next_line)
{
    /*
     * "[foo]:\n" 0-6, "  /url\n" 7-13, "  'the title'\n" 14-27, "ok" 28-29.
     * The lines of the definition are recorded at their first non-space
     * byte, as paragraph lines are.
     */
    PARSE("[foo]:\n  /url\n  'the title'\nok");
    check_dump("(doc (def) (p))");
    check_node(root_child(0), AMARK_KIND_LINK_REFERENCE_DEFINITION, 0, 27, 1,
               3);
    check_definition(root_child(0), 1, 4, 9, 13, true, 17, 26);
    check_node(root_child(1), AMARK_KIND_PARAGRAPH, 28, 30, 4, 4);
    check_content(0, root_child(1), 28, 30);
    check_record(1, 0, 0);
    check_record(2, 9, 0);
    check_record(3, 16, 0);
    check_record(4, 28, 0);
    NAOS_CHECK_UINT_EQ(amark_definitions_find(g_doc, 1, 4), root_child(0));
}

NAOS_TEST(invalid_titles)
{
    /* A title followed by more on its line, on the destination's line: no
     * definition at all. */
    PARSE("[foo]: /url \"title\" ok");
    check_dump("(doc (p))");
    check_node(root_child(0), AMARK_KIND_PARAGRAPH, 0, 22, 1, 1);
    NAOS_CHECK_UINT_EQ(definition_count(), 0);
    NAOS_CHECK_UINT_EQ(amark_definitions_find(g_doc, 1, 4), AMARK_NODE_NONE);
    reset_doc();

    /*
     * On the next line: a definition without a title, and the title's line
     * stays in the paragraph. "[foo]: /url\n" 0-11, "\"title\" ok" 12-21.
     */
    PARSE("[foo]: /url\n\"title\" ok");
    check_dump("(doc (def) (p))");
    check_node(root_child(0), AMARK_KIND_LINK_REFERENCE_DEFINITION, 0, 11, 1,
               1);
    check_definition(root_child(0), 1, 4, 7, 11, false, 0, 0);
    check_node(root_child(1), AMARK_KIND_PARAGRAPH, 12, 22, 2, 2);
    check_content(0, root_child(1), 12, 22);
    NAOS_CHECK_UINT_EQ(amark_definitions_find(g_doc, 1, 4), root_child(0));
}

NAOS_TEST(definitions_do_not_interrupt_a_paragraph)
{
    /* "foo\n" 0-3, "[a]: /u" 4-10: the second line is paragraph text. */
    PARSE("foo\n[a]: /u");
    check_dump("(doc (p))");
    check_node(root_child(0), AMARK_KIND_PARAGRAPH, 0, 11, 1, 2);
    NAOS_CHECK_UINT_EQ(definition_count(), 0);
}

NAOS_TEST(duplicate_labels_find_the_first_definition)
{
    /* "[Foo]: /a\n" 0-9, "[foo]: /b\n" 10-19, "[BAR]: /c" 20-28. */
    PARSE("[Foo]: /a\n[foo]: /b\n[BAR]: /c");
    check_dump("(doc (def) (def) (def))");
    /* The later definition stays in the tree but not in the table. */
    NAOS_CHECK_UINT_EQ(definition_count(), 2);
    NAOS_CHECK_UINT_EQ(amark_definitions_find(g_doc, 1, 4), root_child(0));
    NAOS_CHECK_UINT_EQ(amark_definitions_find(g_doc, 11, 14), root_child(0));
    NAOS_CHECK_UINT_EQ(amark_definitions_find(g_doc, 21, 24), root_child(2));
    check_definition(root_child(1), 11, 14, 17, 19, false, 0, 0);
}

NAOS_TEST(definitions_in_separate_paragraphs_share_one_table)
{
    /* "[a]: /u\n" 0-7, "x\n" 8-9, "\n" 10, "[b]: /v\n" 11-18, "y" 19. */
    PARSE("[a]: /u\nx\n\n[b]: /v\ny");
    check_dump("(doc (def) (p) (def) (p))");
    NAOS_CHECK_UINT_EQ(definition_count(), 2);
    NAOS_CHECK_UINT_EQ(amark_definitions_find(g_doc, 1, 2), root_child(0));
    NAOS_CHECK_UINT_EQ(amark_definitions_find(g_doc, 12, 13), root_child(2));
    check_node(root_child(3), AMARK_KIND_PARAGRAPH, 19, 20, 5, 5);
}

NAOS_TEST(setext_headings_of_both_levels)
{
    /*
     * "Foo\n" 0-3, "===\n" 4-7, "\n" 8, "Bar  \n" 9-14, "---" 15-17. The
     * heading includes the underline; its content leaves it out and ends
     * after the last non-space byte of the paragraph.
     */
    PARSE("Foo\n===\n\nBar  \n---");
    check_dump("(doc (h 1 setext) (h 2 setext))");
    check_node(root_child(0), AMARK_KIND_HEADING, 0, 7, 1, 2);
    check_heading(root_child(0), 1, AMARK_HEADING_SETEXT);
    check_node(root_child(1), AMARK_KIND_HEADING, 9, 18, 4, 5);
    check_heading(root_child(1), 2, AMARK_HEADING_SETEXT);
    NAOS_CHECK_UINT_EQ(amark_inline_content_count(g_doc), 2);
    check_content(0, root_child(0), 0, 3);
    check_content(1, root_child(1), 9, 12);
    /* The underline line is recorded at its first character. */
    check_record(1, 0, 0);
    check_record(2, 4, 0);
    check_record(3, 8, 0);
    check_record(4, 9, 0);
    check_record(5, 15, 0);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 3);
}

NAOS_TEST(setext_heading_of_several_lines)
{
    /* "a\n" 0-1, "  b \n" 2-6, "  ==  " 7-12. */
    PARSE("a\n  b \n  ==  ");
    check_dump("(doc (h 1 setext))");
    check_node(root_child(0), AMARK_KIND_HEADING, 0, 13, 1, 3);
    check_content(0, root_child(0), 0, 5);
    check_record(1, 0, 0);
    check_record(2, 4, 0);
    check_record(3, 9, 0);
}

NAOS_TEST(setext_underline_with_each_line_ending)
{
    static const char *const k_inputs[] = {"x\n-\n", "x\r-\r", "x\r\n-\r\n"};
    static const size_t k_underline[] = {2, 2, 3};
    static const size_t k_end[] = {3, 3, 4};
    size_t i;

    for (i = 0; i < 3; i++) {
        size_t length = strlen(k_inputs[i]);

        parse_range_ok(k_inputs[i], length, 0, length);
        check_dump("(doc (h 2 setext))");
        check_node(root_child(0), AMARK_KIND_HEADING, 0, k_end[i], 1, 2);
        check_content(0, root_child(0), 0, 1);
        check_record(2, k_underline[i], 0);
        reset_doc();
    }
}

NAOS_TEST(lines_that_are_not_setext_underlines)
{
    /* Indented 4 columns: a continuation line. */
    PARSE("Foo\n    ===");
    check_dump("(doc (p))");
    reset_doc();

    /* Spaces inside: "- - -" is a thematic break, "= =" text. */
    PARSE("Foo\n- - -");
    check_dump("(doc (p) (hr))");
    reset_doc();
    PARSE("Foo\n= =");
    check_dump("(doc (p))");
    reset_doc();

    /* No paragraph before it. */
    PARSE("===");
    check_dump("(doc (p))");
    reset_doc();
    PARSE("# h\n===");
    check_dump("(doc (h 1 atx) (p))");
    reset_doc();
    PARSE("Foo\n\n---");
    check_dump("(doc (p) (hr))");
}

NAOS_TEST(setext_underline_takes_precedence_over_a_thematic_break)
{
    /* "Foo\n" 0-3, "---\n" 4-7, "---" 8-10. */
    PARSE("Foo\n---\n---");
    check_dump("(doc (h 2 setext) (hr))");
    check_node(root_child(0), AMARK_KIND_HEADING, 0, 7, 1, 2);
    check_node(root_child(1), AMARK_KIND_THEMATIC_BREAK, 8, 11, 3, 3);
}

NAOS_TEST(underline_after_definitions_only)
{
    /*
     * "[foo]: /url\n" 0-11, "===" 12-14: no heading; the underline line
     * becomes the first line of a paragraph.
     */
    PARSE("[foo]: /url\n===");
    check_dump("(doc (def) (p))");
    check_node(root_child(0), AMARK_KIND_LINK_REFERENCE_DEFINITION, 0, 11, 1,
               1);
    check_node(root_child(1), AMARK_KIND_PARAGRAPH, 12, 15, 2, 2);
    check_content(0, root_child(1), 12, 15);
    check_record(2, 12, 0);
    NAOS_CHECK_UINT_EQ(amark_definitions_find(g_doc, 1, 4), root_child(0));
    reset_doc();

    /* The spec's example: the paragraph goes on. "[foo]" is 16-20. */
    PARSE("[foo]: /url\n===\n[foo]");
    check_dump("(doc (def) (p))");
    check_node(root_child(1), AMARK_KIND_PARAGRAPH, 12, 21, 2, 3);
    check_content(0, root_child(1), 12, 21);
    reset_doc();

    /* "---" is then a thematic break. */
    PARSE("[foo]: /url\n---");
    check_dump("(doc (def) (hr))");
    check_node(root_child(1), AMARK_KIND_THEMATIC_BREAK, 12, 15, 2, 2);
    check_record(2, 12, 0);
    NAOS_CHECK_UINT_EQ(amark_inline_content_count(g_doc), 0);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 3);
    reset_doc();

    /*
     * The paragraph that the underline started can be underlined itself:
     * "[a]: /u\n" 0-7, "===\n" 8-11, "===" 12-14.
     */
    PARSE("[a]: /u\n===\n===");
    check_dump("(doc (def) (h 1 setext))");
    check_node(root_child(1), AMARK_KIND_HEADING, 8, 15, 2, 3);
    check_content(0, root_child(1), 8, 11);
    reset_doc();

    /* A blank line after it closes the empty paragraph: no node. */
    PARSE("[a]: /u\n\n");
    check_dump("(doc (def))");
}

NAOS_TEST(setext_heading_after_definitions)
{
    /* "[a]: /u\n" 0-7, "Foo\n" 8-11, "===" 12-14. */
    PARSE("[a]: /u\nFoo\n===");
    check_dump("(doc (def) (h 1 setext))");
    check_node(root_child(0), AMARK_KIND_LINK_REFERENCE_DEFINITION, 0, 7, 1,
               1);
    check_node(root_child(1), AMARK_KIND_HEADING, 8, 15, 2, 3);
    check_heading(root_child(1), 1, AMARK_HEADING_SETEXT);
    check_content(0, root_child(1), 8, 11);
    NAOS_CHECK_UINT_EQ(amark_inline_content_count(g_doc), 1);
    NAOS_CHECK_UINT_EQ(definition_count(), 1);
    NAOS_CHECK_UINT_EQ(amark_definitions_find(g_doc, 1, 2), root_child(0));
}

NAOS_TEST(inline_contents_in_document_order)
{
    /*
     * "[a]: /u\n" 0-7, "A\n" 8-9, "===\n" 10-13, "\n" 14, "B\n" 15-16,
     * "# C\n" 17-20, "D\n" 21-22, "-" 23.
     */
    PARSE("[a]: /u\nA\n===\n\nB\n# C\nD\n-");
    check_dump("(doc (def) (h 1 setext) (p) (h 1 atx) (h 2 setext))");
    NAOS_CHECK_UINT_EQ(amark_inline_content_count(g_doc), 4);
    check_content(0, root_child(1), 8, 9);
    check_content(1, root_child(2), 15, 16);
    check_content(2, root_child(3), 19, 20);
    check_content(3, root_child(4), 21, 22);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 6);
}

NAOS_TEST(definitions_and_setext_in_a_range_starting_inside_a_line)
{
    /* "zz[a]: /u\n" 0-9 with the range from 2, "x\n" 10-11, "-" 12. */
    PARSE_RANGE("zz[a]: /u\nx\n-", 2, 13);
    check_dump("(doc (def) (h 2 setext))");
    check_node(root_child(0), AMARK_KIND_LINK_REFERENCE_DEFINITION, 2, 9, 1,
               1);
    check_definition(root_child(0), 3, 4, 7, 9, false, 0, 0);
    check_node(root_child(1), AMARK_KIND_HEADING, 10, 13, 2, 3);
    NAOS_CHECK_UINT_EQ(amark_definitions_find(g_doc, 3, 4), root_child(0));
}

NAOS_TEST(node_count_limit_counts_definitions)
{
    set_content("[a]: /u\n[b]: /v", 15);
    NAOS_CHECK_INT_EQ(parse_buffer(0, 15, 0, 2), AMARK_ERROR_LIMIT_EXCEEDED);
    reset_doc();

    set_content("[a]: /u\n[b]: /v", 15);
    NAOS_REQUIRE_INT_EQ(parse_buffer(0, 15, 0, 3), AMARK_ERROR_NONE);
    check_dump("(doc (def) (def))");
}

NAOS_TEST(many_definitions)
{
    /* 200 definitions "[dNNN]: /u\n" of 11 bytes, every 4th a duplicate. */
    static char k_input[200 * 11 + 1];
    size_t i;

    for (i = 0; i < 200; i++) {
        size_t n = i % 4 == 3 ? i - 1 : i;
        char *line = k_input + i * 11;

        line[0] = '[';
        line[1] = 'd';
        line[2] = (char)('0' + n / 100);
        line[3] = (char)('0' + n / 10 % 10);
        line[4] = (char)('0' + n % 10);
        memcpy(line + 5, "]: /u\n", 6);
    }
    parse_range_ok(k_input, 200 * 11, 0, 200 * 11);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 201);
    NAOS_CHECK_UINT_EQ(definition_count(), 150);
    for (i = 0; i < 200; i++) {
        size_t first = i % 4 == 3 ? i - 1 : i;

        NAOS_CHECK_UINT_EQ(amark_definitions_find(g_doc, i * 11 + 1,
                                                  i * 11 + 5),
                           root_child(first));
    }
}

NAOS_TEST(definitions_and_setext_through_amark_parse)
{
    /*
     * Through the public entry, which releases its scratch arena before it
     * returns: the table lives in the document. "[Foo]: /u\n" 0-9,
     * "Bar\n" 10-13, "---\n" 14-17, "[foo]: /v" 18-26.
     */
    static const char k_input[] = "[Foo]: /u\nBar\n---\n[foo]: /v";
    amark_config_t config;

    amark_config_init(&config);
    config.allocator = g_allocator;
    NAOS_REQUIRE_INT_EQ(amark_parse(k_input,
                                    sizeof(k_input) - 1, &config, &g_doc),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_INT_EQ(amark_tree_check(g_doc).violation,
                      AMARK_TREE_VIOLATION_NONE);
    check_dump("(doc (def) (h 2 setext) (def))");
    check_node(root_child(1), AMARK_KIND_HEADING, 10, 17, 2, 3);
    check_content(0, root_child(1), 10, 13);
    NAOS_CHECK_UINT_EQ(definition_count(), 1);
    NAOS_CHECK_UINT_EQ(amark_definitions_find(g_doc, 19, 22), root_child(0));
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 4);
}

NAOS_TEST(allocation_failure_with_definitions_and_setext)
{
    static const char k_input[] =
        "[a]: /u\n[b]: /v 't'\nFoo\n===\n\n[c]: /w\n---\nx\n[a]: /z\n";
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
    NAOS_CHECK_UINT_GT(fail_at, 1);
    NAOS_CHECK_INT_EQ(amark_tree_check(g_doc).violation,
                      AMARK_TREE_VIOLATION_NONE);
    check_dump("(doc (def) (def) (h 1 setext) (def) (hr) (p))");
    NAOS_CHECK_UINT_EQ(definition_count(), 3);
}

/* ---------------------------------------------------------------------- */
/* Block quotes and lazy continuation lines                               */
/* ---------------------------------------------------------------------- */

/*
 * Parses length bytes at bytes through amark_parse with g_allocator and the
 * two limits, into g_doc. The bytes must outlive g_doc.
 */
static amark_error_t parse_public(const char *bytes, size_t length,
                                  size_t max_depth, size_t max_nodes)
{
    amark_config_t config;

    amark_config_init(&config);
    config.allocator = g_allocator;
    config.max_nesting_depth = max_depth;
    config.max_node_count = max_nodes;
    return amark_parse(bytes, length, &config, &g_doc);
}

/*
 * Parses through amark_parse with no limits, requires success and a tree
 * that passes the tree check.
 */
static void parse_public_ok(const char *bytes, size_t length)
{
    amark_tree_check_result_t check;

    NAOS_REQUIRE_INT_EQ(parse_public(bytes, length, 0, 0), AMARK_ERROR_NONE);
    check = amark_tree_check(g_doc);
    NAOS_CHECK_INT_EQ(check.violation, AMARK_TREE_VIOLATION_NONE);
    NAOS_CHECK_UINT_EQ(check.node, AMARK_NODE_NONE);
}

#define PARSE_PUBLIC(s) parse_public_ok((s), sizeof(s) - 1)

/* Returns the nth (from 0) child of node; it must exist. */
static amark_node_id_t nth_child(amark_node_id_t node, size_t nth)
{
    amark_node_id_t child = amark_node_first_child(g_doc, node);

    while (nth > 0) {
        NAOS_REQUIRE_UINT_NE(child, AMARK_NODE_NONE);
        child = amark_node_next_sibling(g_doc, child);
        nth--;
    }
    NAOS_REQUIRE_UINT_NE(child, AMARK_NODE_NONE);
    return child;
}

NAOS_TEST(block_quote_of_one_line_and_its_marker_forms)
{
    /* "> a": the quote starts at '>', the paragraph after "> ". */
    PARSE_PUBLIC("> a");
    check_dump("(doc (bq (p)))");
    check_node(root_child(0), AMARK_KIND_BLOCK_QUOTE, 0, 3, 1, 1);
    check_node(nth_child(root_child(0), 0), AMARK_KIND_PARAGRAPH, 2, 3, 1, 1);
    check_content(0, nth_child(root_child(0), 0), 2, 3);
    check_record(1, 2, 0);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 3);
    reset_doc();

    /* No space after the marker. */
    PARSE_PUBLIC(">a");
    check_dump("(doc (bq (p)))");
    check_node(root_child(0), AMARK_KIND_BLOCK_QUOTE, 0, 2, 1, 1);
    check_node(nth_child(root_child(0), 0), AMARK_KIND_PARAGRAPH, 1, 2, 1, 1);
    check_record(1, 1, 0);
    reset_doc();

    /* 3 columns of indentation: the quote starts at '>'. */
    PARSE_PUBLIC("   > a");
    check_dump("(doc (bq (p)))");
    check_node(root_child(0), AMARK_KIND_BLOCK_QUOTE, 3, 6, 1, 1);
    check_node(nth_child(root_child(0), 0), AMARK_KIND_PARAGRAPH, 5, 6, 1, 1);
    check_record(1, 5, 0);
    reset_doc();

    /* Only one space after '>' is the marker's. */
    PARSE_PUBLIC(">  a");
    check_node(nth_child(root_child(0), 0), AMARK_KIND_PARAGRAPH, 3, 4, 1, 1);
    check_record(1, 3, 0);
    reset_doc();

    /* 4 columns: indented code. */
    PARSE_PUBLIC("    > a");
    check_dump("(doc (code indented))");
    reset_doc();

    /* A marker with nothing after it is an empty quote. */
    PARSE_PUBLIC(">");
    check_dump("(doc (bq))");
    check_node(root_child(0), AMARK_KIND_BLOCK_QUOTE, 0, 1, 1, 1);
    check_record(1, 1, 0);
}

NAOS_TEST(block_quote_over_several_lines)
{
    /* "> a\n" 0-3, ">b\n" 4-6, "   > c\n" 7-13. */
    PARSE_PUBLIC("> a\n>b\n   > c\n");
    check_dump("(doc (bq (p)))");
    check_node(root_child(0), AMARK_KIND_BLOCK_QUOTE, 0, 13, 1, 3);
    check_node(nth_child(root_child(0), 0), AMARK_KIND_PARAGRAPH, 2, 13, 1, 3);
    check_content(0, nth_child(root_child(0), 0), 2, 13);
    check_record(1, 2, 0);
    check_record(2, 5, 0);
    check_record(3, 12, 0);
}

NAOS_TEST(marker_only_line_ends_the_paragraph)
{
    /* "> a\n" 0-3, ">\n" 4-5, "> b" 6-8. */
    PARSE_PUBLIC("> a\n>\n> b");
    check_dump("(doc (bq (p) (p)))");
    check_node(root_child(0), AMARK_KIND_BLOCK_QUOTE, 0, 9, 1, 3);
    check_node(nth_child(root_child(0), 0), AMARK_KIND_PARAGRAPH, 2, 3, 1, 1);
    check_node(nth_child(root_child(0), 1), AMARK_KIND_PARAGRAPH, 8, 9, 3, 3);
    check_record(1, 2, 0);
    check_record(2, 5, 0);
    check_record(3, 8, 0);
    reset_doc();

    /* The last line with a marker ends the quote, empty or not. */
    PARSE_PUBLIC("> a\n>\n");
    check_dump("(doc (bq (p)))");
    check_node(root_child(0), AMARK_KIND_BLOCK_QUOTE, 0, 5, 1, 2);
    check_node(nth_child(root_child(0), 0), AMARK_KIND_PARAGRAPH, 2, 3, 1, 1);
    check_record(2, 5, 0);
    reset_doc();

    /* With white space after the marker: "> a\n" 0-3, ">  \n" 4-7. */
    PARSE_PUBLIC("> a\n>  \n");
    check_node(root_child(0), AMARK_KIND_BLOCK_QUOTE, 0, 7, 1, 2);
    check_record(2, 7, 0);
}

NAOS_TEST(blank_line_ends_a_block_quote)
{
    /* "> a\n" 0-3, "\n" 4, "> b" 5-7. */
    PARSE_PUBLIC("> a\n\n> b");
    check_dump("(doc (bq (p)) (bq (p)))");
    check_node(root_child(0), AMARK_KIND_BLOCK_QUOTE, 0, 3, 1, 1);
    check_node(root_child(1), AMARK_KIND_BLOCK_QUOTE, 5, 8, 3, 3);
    check_record(2, 4, 0);
    check_record(3, 7, 0);
}

NAOS_TEST(nested_block_quotes)
{
    /*
     * "> > a\n" 0-5, "> b\n" 6-9, ">> c" 10-13. The second line lacks the
     * inner marker but continues the inner paragraph lazily.
     */
    PARSE_PUBLIC("> > a\n> b\n>> c");
    check_dump("(doc (bq (bq (p))))");
    check_node(root_child(0), AMARK_KIND_BLOCK_QUOTE, 0, 14, 1, 3);
    check_node(nth_child(root_child(0), 0), AMARK_KIND_BLOCK_QUOTE, 2, 14, 1,
               3);
    check_node(nth_child(nth_child(root_child(0), 0), 0),
               AMARK_KIND_PARAGRAPH, 4, 14, 1, 3);
    check_record(1, 4, 0);
    check_record(2, 8, 0);
    check_record(3, 13, 0);
    reset_doc();

    /*
     * "> > a\n" 0-5, ">\n" 6-7, "> b" 8-10: the marker-only line closes
     * the inner quote, and "b" is a paragraph of the outer one.
     */
    PARSE_PUBLIC("> > a\n>\n> b");
    check_dump("(doc (bq (bq (p)) (p)))");
    check_node(root_child(0), AMARK_KIND_BLOCK_QUOTE, 0, 11, 1, 3);
    check_node(nth_child(root_child(0), 0), AMARK_KIND_BLOCK_QUOTE, 2, 5, 1,
               1);
    check_node(nth_child(root_child(0), 1), AMARK_KIND_PARAGRAPH, 10, 11, 3,
               3);
    check_record(2, 7, 0);
    reset_doc();

    /* A quote interrupts a paragraph: "> a\n" 0-3, "> > b" 4-8. */
    PARSE_PUBLIC("> a\n> > b");
    check_dump("(doc (bq (p) (bq (p))))");
    check_node(nth_child(root_child(0), 0), AMARK_KIND_PARAGRAPH, 2, 3, 1, 1);
    check_node(nth_child(root_child(0), 1), AMARK_KIND_BLOCK_QUOTE, 6, 9, 2,
               2);
    reset_doc();

    /* Markers only: "> >" 0-2. */
    PARSE_PUBLIC("> >");
    check_dump("(doc (bq (bq)))");
    check_node(nth_child(root_child(0), 0), AMARK_KIND_BLOCK_QUOTE, 2, 3, 1,
               1);
}

NAOS_TEST(lazy_continuation_lines)
{
    /* "> a\n" 0-3, "b\n" 4-5, "  c" 6-8. */
    PARSE_PUBLIC("> a\nb\n  c");
    check_dump("(doc (bq (p)))");
    check_node(root_child(0), AMARK_KIND_BLOCK_QUOTE, 0, 9, 1, 3);
    check_node(nth_child(root_child(0), 0), AMARK_KIND_PARAGRAPH, 2, 9, 1, 3);
    check_content(0, nth_child(root_child(0), 0), 2, 9);
    check_record(1, 2, 0);
    check_record(2, 4, 0);
    check_record(3, 8, 0);
    reset_doc();

    /* Both quotes take the lazy line: "> > a\n" 0-5, "b" 6. */
    PARSE_PUBLIC("> > a\nb");
    check_dump("(doc (bq (bq (p))))");
    check_node(root_child(0), AMARK_KIND_BLOCK_QUOTE, 0, 7, 1, 2);
    check_node(nth_child(root_child(0), 0), AMARK_KIND_BLOCK_QUOTE, 2, 7, 1,
               2);
    check_record(2, 6, 0);
    reset_doc();

    /* Indented lazy lines are not code: "> a\n" 0-3, "\tb" 4-5. */
    PARSE_PUBLIC("> a\n\tb");
    check_dump("(doc (bq (p)))");
    check_node(nth_child(root_child(0), 0), AMARK_KIND_PARAGRAPH, 2, 6, 1, 2);
    check_record(2, 5, 0);
    reset_doc();

    /* "> a\n" 0-3, "    b" 4-8. */
    PARSE_PUBLIC("> a\n    b");
    check_dump("(doc (bq (p)))");
    check_node(nth_child(root_child(0), 0), AMARK_KIND_PARAGRAPH, 2, 9, 1, 2);
    check_record(2, 8, 0);
    reset_doc();

    /*
     * Condition 7 of HTML does not interrupt a paragraph, so the line is
     * lazy: "> a\n" 0-3, "<span>" 4-9.
     */
    PARSE_PUBLIC("> a\n<span>");
    check_dump("(doc (bq (p)))");
    check_node(root_child(0), AMARK_KIND_BLOCK_QUOTE, 0, 10, 1, 2);
    check_node(nth_child(root_child(0), 0), AMARK_KIND_PARAGRAPH, 2, 10, 1, 2);
    check_record(2, 4, 0);
    reset_doc();

    /* A blank line is never lazy: "> a\n" 0-3, "\n" 4, "b" 5. */
    PARSE_PUBLIC("> a\n\nb");
    check_dump("(doc (bq (p)) (p))");
    check_node(root_child(0), AMARK_KIND_BLOCK_QUOTE, 0, 3, 1, 1);
}

NAOS_TEST(lazy_line_is_not_a_setext_underline)
{
    /* Example 92: "> Foo\n" 0-5, "---" 6-8. */
    PARSE_PUBLIC("> Foo\n---");
    check_dump("(doc (bq (p)) (hr))");
    check_node(root_child(0), AMARK_KIND_BLOCK_QUOTE, 0, 5, 1, 1);
    check_node(root_child(1), AMARK_KIND_THEMATIC_BREAK, 6, 9, 2, 2);
    check_record(2, 6, 0);
    reset_doc();

    /* Example 93: "> foo\n" 0-5, "bar\n" 6-9, "===" 10-12. */
    PARSE_PUBLIC("> foo\nbar\n===");
    check_dump("(doc (bq (p)))");
    check_node(root_child(0), AMARK_KIND_BLOCK_QUOTE, 0, 13, 1, 3);
    check_node(nth_child(root_child(0), 0), AMARK_KIND_PARAGRAPH, 2, 13, 1, 3);
    check_content(0, nth_child(root_child(0), 0), 2, 13);
    check_record(3, 10, 0);
    reset_doc();

    /* Example 101. */
    PARSE_PUBLIC("> foo\n-----");
    check_dump("(doc (bq (p)) (hr))");
    reset_doc();

    /* With the marker it underlines: "> a\n" 0-3, "> ---" 4-8. */
    PARSE_PUBLIC("> a\n> ---");
    check_dump("(doc (bq (h 2 setext)))");
    check_node(root_child(0), AMARK_KIND_BLOCK_QUOTE, 0, 9, 1, 2);
    check_node(nth_child(root_child(0), 0), AMARK_KIND_HEADING, 2, 9, 1, 2);
    check_content(0, nth_child(root_child(0), 0), 2, 3);
    check_record(2, 6, 0);
    reset_doc();

    PARSE_PUBLIC("> a\n> ===");
    check_dump("(doc (bq (h 1 setext)))");
}

NAOS_TEST(lines_that_start_blocks_are_not_lazy)
{
    PARSE_PUBLIC("> a\n# h");
    check_dump("(doc (bq (p)) (h 1 atx))");
    reset_doc();

    PARSE_PUBLIC("> a\n***");
    check_dump("(doc (bq (p)) (hr))");
    reset_doc();

    PARSE_PUBLIC("> a\n```");
    check_dump("(doc (bq (p)) (code fenced ` 3))");
    reset_doc();

    /* "> a\n" 0-3, "<div>" 4-8. */
    PARSE_PUBLIC("> a\n<div>");
    check_dump("(doc (bq (p)) (html))");
    check_node(root_child(0), AMARK_KIND_BLOCK_QUOTE, 0, 3, 1, 1);
    check_node(root_child(1), AMARK_KIND_HTML_BLOCK, 4, 9, 2, 2);
}

NAOS_TEST(unmatched_block_quote_closes_its_leaf)
{
    /* Only a paragraph takes lazy lines: "> ```\n" 0-5, "foo" 6-8. */
    PARSE_PUBLIC("> ```\nfoo");
    check_dump("(doc (bq (code fenced ` 3)) (p))");
    check_node(root_child(0), AMARK_KIND_BLOCK_QUOTE, 0, 5, 1, 1);
    check_node(nth_child(root_child(0), 0), AMARK_KIND_CODE_BLOCK, 2, 5, 1, 1);
    check_node(root_child(1), AMARK_KIND_PARAGRAPH, 6, 9, 2, 2);
    reset_doc();

    PARSE_PUBLIC(">     code\nfoo");
    check_dump("(doc (bq (code indented)) (p))");
    reset_doc();

    PARSE_PUBLIC(">     code\n    more");
    check_dump("(doc (bq (code indented)) (code indented))");
    reset_doc();

    PARSE_PUBLIC("> <div>\nfoo");
    check_dump("(doc (bq (html)) (p))");
    reset_doc();

    /*
     * A closing fence inside the quote: "> ```\n" 0-5, "> a\n" 6-9,
     * "> ```\n" 10-15, "b" 16.
     */
    PARSE_PUBLIC("> ```\n> a\n> ```\nb");
    check_dump("(doc (bq (code fenced ` 3)) (p))");
    check_node(root_child(0), AMARK_KIND_BLOCK_QUOTE, 0, 15, 1, 3);
    check_node(nth_child(root_child(0), 0), AMARK_KIND_CODE_BLOCK, 2, 15, 1,
               3);
    check_node(root_child(1), AMARK_KIND_PARAGRAPH, 16, 17, 4, 4);
    check_record(2, 8, 0);
    check_record(3, 12, 0);
}

NAOS_TEST(tab_after_the_marker)
{
    /*
     * ">\tfoo": '>' takes 1 column of the tab; the paragraph starts at its
     * first non-space byte.
     */
    PARSE_PUBLIC(">\tfoo");
    check_dump("(doc (bq (p)))");
    check_node(nth_child(root_child(0), 0), AMARK_KIND_PARAGRAPH, 2, 5, 1, 1);
    check_record(1, 2, 0);
    reset_doc();

    /*
     * Example 6, ">\t\tfoo": after '>' and 1 column of the first tab, 6
     * columns of indentation; the 4 of the code end inside the second tab,
     * at offset 2, with 2 columns left.
     */
    PARSE_PUBLIC(">\t\tfoo");
    check_dump("(doc (bq (code indented)))");
    check_node(root_child(0), AMARK_KIND_BLOCK_QUOTE, 0, 6, 1, 1);
    check_node(nth_child(root_child(0), 0), AMARK_KIND_CODE_BLOCK, 2, 6, 1, 1);
    check_record(1, 2, 2);
    reset_doc();

    /* The next line the same: ">\t\tfoo\n" 0-6, ">\t\tbar" 7-12. */
    PARSE_PUBLIC(">\t\tfoo\n>\t\tbar");
    check_dump("(doc (bq (code indented)))");
    check_node(nth_child(root_child(0), 0), AMARK_KIND_CODE_BLOCK, 2, 13, 1,
               2);
    check_record(2, 9, 2);
}

NAOS_TEST(html_line_with_a_prefix_ending_inside_a_tab)
{
    /*
     * ">\t<div>\n" 0-7, ">\tb" 8-10: the block starts after the prefix, on
     * the tab with its 2 columns left.
     */
    PARSE_PUBLIC(">\t<div>\n>\tb");
    check_dump("(doc (bq (html)))");
    check_node(root_child(0), AMARK_KIND_BLOCK_QUOTE, 0, 11, 1, 2);
    check_node(nth_child(root_child(0), 0), AMARK_KIND_HTML_BLOCK, 1, 11, 1,
               2);
    check_record(1, 1, 2);
    check_record(2, 9, 2);
}

NAOS_TEST(definitions_in_a_block_quote)
{
    amark_node_id_t def;

    /* "> [a]: /u\n" 0-9, "> b" 10-12. */
    PARSE_PUBLIC("> [a]: /u\n> b");
    check_dump("(doc (bq (def) (p)))");
    def = nth_child(root_child(0), 0);
    check_node(def, AMARK_KIND_LINK_REFERENCE_DEFINITION, 2, 9, 1, 1);
    check_definition(def, 3, 4, 7, 9, false, 0, 0);
    check_node(nth_child(root_child(0), 1), AMARK_KIND_PARAGRAPH, 12, 13, 2,
               2);
    check_content(0, nth_child(root_child(0), 1), 12, 13);
    NAOS_CHECK_UINT_EQ(definition_count(), 1);
    NAOS_CHECK_UINT_EQ(amark_definitions_find(g_doc, 3, 4), def);
    reset_doc();

    /*
     * Over three lines, each with the marker: "> [a]:\n" 0-6, "> /u\n"
     * 7-11, "> 't'" 12-16.
     */
    PARSE_PUBLIC("> [a]:\n> /u\n> 't'");
    check_dump("(doc (bq (def)))");
    def = nth_child(root_child(0), 0);
    check_node(def, AMARK_KIND_LINK_REFERENCE_DEFINITION, 2, 17, 1, 3);
    check_definition(def, 3, 4, 9, 11, true, 15, 16);
    reset_doc();

    /* With a lazy line: "> [a]:\n" 0-6, "/u" 7-8. */
    PARSE_PUBLIC("> [a]:\n/u");
    check_dump("(doc (bq (def)))");
    def = nth_child(root_child(0), 0);
    check_node(def, AMARK_KIND_LINK_REFERENCE_DEFINITION, 2, 9, 1, 2);
    check_definition(def, 3, 4, 7, 9, false, 0, 0);
    check_node(root_child(0), AMARK_KIND_BLOCK_QUOTE, 0, 9, 1, 2);
    check_record(2, 7, 0);
    reset_doc();

    /*
     * An underline after definitions only: "> [a]: /u\n" 0-9, "> ===" 10-14;
     * the underline becomes the paragraph.
     */
    PARSE_PUBLIC("> [a]: /u\n> ===");
    check_dump("(doc (bq (def) (p)))");
    check_node(nth_child(root_child(0), 1), AMARK_KIND_PARAGRAPH, 12, 15, 2,
               2);
}

NAOS_TEST(block_quote_with_crlf_in_a_range_starting_inside_a_line)
{
    /*
     * "x> a\r\n" 0-5, "> b\r\n" 6-10; the range starts at 1, which is
     * column 0.
     */
    PARSE_RANGE("x> a\r\n> b\r\n", 1, 11);
    check_dump("(doc (bq (p)))");
    check_node(root_child(0), AMARK_KIND_BLOCK_QUOTE, 1, 9, 1, 2);
    check_node(nth_child(root_child(0), 0), AMARK_KIND_PARAGRAPH, 3, 9, 1, 2);
    check_record(1, 3, 0);
    check_record(2, 8, 0);
}

NAOS_TEST(nesting_depth_limit_with_block_quotes)
{
    /* "> > a": quotes at depths 1 and 2, the paragraph at 3. */
    NAOS_CHECK_INT_EQ(parse_public("> > a", 5, 2, 0),
                      AMARK_ERROR_LIMIT_EXCEEDED);
    NAOS_CHECK_TRUE(g_doc == NULL);
    NAOS_REQUIRE_INT_EQ(parse_public("> > a", 5, 3, 0), AMARK_ERROR_NONE);
    check_dump("(doc (bq (bq (p))))");
    reset_doc();

    /* Without the paragraph, depth 2 is enough. */
    NAOS_REQUIRE_INT_EQ(parse_public("> >", 3, 2, 0), AMARK_ERROR_NONE);
    check_dump("(doc (bq (bq)))");
    reset_doc();
    NAOS_CHECK_INT_EQ(parse_public("> >", 3, 1, 0),
                      AMARK_ERROR_LIMIT_EXCEEDED);
}

NAOS_TEST(node_count_limit_counts_block_quotes)
{
    /* The root, the quote and the paragraph. */
    NAOS_CHECK_INT_EQ(parse_public("> a", 3, 0, 2),
                      AMARK_ERROR_LIMIT_EXCEEDED);
    NAOS_REQUIRE_INT_EQ(parse_public("> a", 3, 0, 3), AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 3);
}

#define DEEP 10000

/* DEEP markers, "a", a line ending and the lazy line "b". */
static char g_deep[DEEP + 3];

NAOS_TEST(deeply_nested_block_quotes)
{
    amark_node_id_t node;
    size_t i;

    memset(g_deep, '>', DEEP);
    memcpy(g_deep + DEEP, "a\nb", 3);

    /* The paragraph is at depth DEEP + 1. */
    NAOS_CHECK_INT_EQ(parse_public(g_deep, sizeof(g_deep), DEEP, 0),
                      AMARK_ERROR_LIMIT_EXCEEDED);
    NAOS_REQUIRE_INT_EQ(parse_public(g_deep, sizeof(g_deep), DEEP + 1, 0),
                        AMARK_ERROR_NONE);
    reset_doc();

    parse_public_ok(g_deep, sizeof(g_deep));
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), DEEP + 2);
    node = amark_document_root(g_doc);
    for (i = 0; i < DEEP; i++) {
        amark_range_t range;

        node = amark_node_first_child(g_doc, node);
        NAOS_REQUIRE_UINT_EQ(amark_node_kind(g_doc, node),
                             AMARK_KIND_BLOCK_QUOTE);
        NAOS_REQUIRE_TRUE(amark_node_range(g_doc, node, &range));
        NAOS_REQUIRE_UINT_EQ(range.start, i);
        NAOS_REQUIRE_UINT_EQ(range.end, DEEP + 3);
        NAOS_REQUIRE_UINT_EQ(amark_node_next_sibling(g_doc, node),
                             AMARK_NODE_NONE);
    }
    check_node(amark_node_first_child(g_doc, node), AMARK_KIND_PARAGRAPH,
               DEEP, DEEP + 3, 1, 2);
    check_record(1, DEEP, 0);
    check_record(2, DEEP + 2, 0);
}

NAOS_TEST(allocation_failure_with_block_quotes)
{
    static const char k_input[] = "> a\n> > b\nc\n\n> [x]: /u\n";
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
    NAOS_CHECK_UINT_GT(fail_at, 1);
    NAOS_CHECK_INT_EQ(amark_tree_check(g_doc).violation,
                      AMARK_TREE_VIOLATION_NONE);
    check_dump("(doc (bq (p) (bq (p))) (bq (def)))");
    NAOS_CHECK_UINT_EQ(definition_count(), 1);
}

/* ---------------------------------------------------------------------- */
/* Lists and list items                                                   */
/* ---------------------------------------------------------------------- */

/*
 * Checks the attributes of the list node: a bullet list with its bullet
 * character, or an ordered list with its delimiter and start number. The
 * lists checked here are tight (the spacing has its own tests below).
 */
static void check_list(amark_node_id_t node, amark_list_type_t type,
                       char marker, uint32_t start_number)
{
    uint32_t start = 0;

    NAOS_REQUIRE_UINT_EQ(amark_node_kind(g_doc, node), AMARK_KIND_LIST);
    NAOS_CHECK_INT_EQ(amark_list_type(g_doc, node), type);
    NAOS_CHECK_INT_EQ(amark_list_spacing(g_doc, node), AMARK_LIST_TIGHT);
    if (type == AMARK_LIST_BULLET) {
        NAOS_CHECK_INT_EQ(amark_list_bullet_char(g_doc, node), marker);
        NAOS_CHECK_FALSE(amark_list_start(g_doc, node, &start));
    } else {
        NAOS_CHECK_INT_EQ(amark_list_delimiter(g_doc, node), marker);
        NAOS_REQUIRE_TRUE(amark_list_start(g_doc, node, &start));
        NAOS_CHECK_UINT_EQ(start, start_number);
    }
}

/* The number of children of node. */
static size_t child_count(amark_node_id_t node)
{
    amark_node_id_t child = amark_node_first_child(g_doc, node);
    size_t count = 0;

    while (child != AMARK_NODE_NONE) {
        count++;
        child = amark_node_next_sibling(g_doc, child);
    }
    return count;
}

NAOS_TEST(list_item_of_one_line_and_its_attributes)
{
    amark_node_id_t list;
    amark_node_id_t item;

    /* "- a": the list and the item start at '-', the paragraph after "- ". */
    PARSE_PUBLIC("- a");
    check_dump("(doc (ul - tight (li (p))))");
    list = root_child(0);
    item = nth_child(list, 0);
    check_list(list, AMARK_LIST_BULLET, '-', 0);
    check_node(list, AMARK_KIND_LIST, 0, 3, 1, 1);
    check_node(item, AMARK_KIND_LIST_ITEM, 0, 3, 1, 1);
    check_node(nth_child(item, 0), AMARK_KIND_PARAGRAPH, 2, 3, 1, 1);
    check_content(0, nth_child(item, 0), 2, 3);
    check_record(1, 2, 0);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 4);
    reset_doc();

    PARSE_PUBLIC("+ a");
    check_dump("(doc (ul + tight (li (p))))");
    check_list(root_child(0), AMARK_LIST_BULLET, '+', 0);
    reset_doc();

    PARSE_PUBLIC("* a");
    check_dump("(doc (ul * tight (li (p))))");
    check_list(root_child(0), AMARK_LIST_BULLET, '*', 0);
    reset_doc();

    /* "1. a": the marker is 2 bytes wide. */
    PARSE_PUBLIC("1. a");
    check_dump("(doc (ol 1 . tight (li (p))))");
    check_list(root_child(0), AMARK_LIST_ORDERED, '.', 1);
    check_node(nth_child(root_child(0), 0), AMARK_KIND_LIST_ITEM, 0, 4, 1, 1);
    check_node(nth_child(nth_child(root_child(0), 0), 0),
               AMARK_KIND_PARAGRAPH, 3, 4, 1, 1);
    check_record(1, 3, 0);
    reset_doc();

    PARSE_PUBLIC("12) a");
    check_dump("(doc (ol 12 ) tight (li (p))))");
    check_list(root_child(0), AMARK_LIST_ORDERED, ')', 12);
    check_record(1, 4, 0);
    reset_doc();

    /* 9 digits at most; leading zeros are kept out of the number. */
    PARSE_PUBLIC("123456789. a");
    check_list(root_child(0), AMARK_LIST_ORDERED, '.', 123456789);
    reset_doc();
    PARSE_PUBLIC("007. a");
    check_list(root_child(0), AMARK_LIST_ORDERED, '.', 7);
    reset_doc();
    PARSE_PUBLIC("1234567890. a");
    check_dump("(doc (p))");
    reset_doc();

    /* 3 columns of indentation: the list and the item start at '-'. */
    PARSE_PUBLIC("   - a");
    check_dump("(doc (ul - tight (li (p))))");
    check_node(root_child(0), AMARK_KIND_LIST, 3, 6, 1, 1);
    check_node(nth_child(root_child(0), 0), AMARK_KIND_LIST_ITEM, 3, 6, 1,
               1);
    check_record(1, 5, 0);
    reset_doc();

    /* 4 columns: indented code. No space after the marker: text. */
    PARSE_PUBLIC("    - a");
    check_dump("(doc (code indented))");
    reset_doc();
    PARSE_PUBLIC("-a\n1.a\n1 a");
    check_dump("(doc (p))");
}

NAOS_TEST(list_items_over_several_lines)
{
    amark_node_id_t list;
    amark_node_id_t item;

    /* "- a\n" 0-3, "  b\n" 4-7, "- c\n" 8-11. */
    PARSE_PUBLIC("- a\n  b\n- c\n");
    check_dump("(doc (ul - tight (li (p)) (li (p))))");
    list = root_child(0);
    check_node(list, AMARK_KIND_LIST, 0, 11, 1, 3);
    item = nth_child(list, 0);
    check_node(item, AMARK_KIND_LIST_ITEM, 0, 7, 1, 2);
    check_node(nth_child(item, 0), AMARK_KIND_PARAGRAPH, 2, 7, 1, 2);
    check_content(0, nth_child(item, 0), 2, 7);
    item = nth_child(list, 1);
    check_node(item, AMARK_KIND_LIST_ITEM, 8, 11, 3, 3);
    check_node(nth_child(item, 0), AMARK_KIND_PARAGRAPH, 10, 11, 3, 3);
    check_record(1, 2, 0);
    check_record(2, 6, 0);
    check_record(3, 10, 0);
    reset_doc();

    /* A lazy line: "- a\n" 0-3, "b" 4. */
    PARSE_PUBLIC("- a\nb");
    check_dump("(doc (ul - tight (li (p))))");
    check_node(root_child(0), AMARK_KIND_LIST, 0, 5, 1, 2);
    check_node(nth_child(root_child(0), 0), AMARK_KIND_LIST_ITEM, 0, 5, 1, 2);
    check_record(2, 4, 0);
    reset_doc();

    /* The item does not take the blank line after it. */
    PARSE_PUBLIC("- a\n\n");
    check_dump("(doc (ul - tight (li (p))))");
    check_node(root_child(0), AMARK_KIND_LIST, 0, 3, 1, 1);
    check_node(nth_child(root_child(0), 0), AMARK_KIND_LIST_ITEM, 0, 3, 1, 1);
    check_record(2, 4, 0);
    reset_doc();

    /* With trailing white space: the item ends at the end of the content. */
    PARSE_PUBLIC("- a  \nb");
    check_node(nth_child(root_child(0), 0), AMARK_KIND_LIST_ITEM, 0, 7, 1, 2);
    check_content(0, nth_child(nth_child(root_child(0), 0), 0), 2, 7);
}

NAOS_TEST(a_different_marker_starts_a_new_list)
{
    /* "- a\n" 0-3, "+ b\n" 4-7, "1. c\n" 8-12, "2) d\n" 13-17, "3) e" 18-21. */
    PARSE_PUBLIC("- a\n+ b\n1. c\n2) d\n3) e");
    check_dump("(doc (ul - tight (li (p))) (ul + tight (li (p))) "
               "(ol 1 . tight (li (p))) (ol 2 ) tight (li (p)) (li (p))))");
    check_node(root_child(0), AMARK_KIND_LIST, 0, 3, 1, 1);
    check_node(root_child(1), AMARK_KIND_LIST, 4, 7, 2, 2);
    check_node(root_child(2), AMARK_KIND_LIST, 8, 12, 3, 3);
    check_node(root_child(3), AMARK_KIND_LIST, 13, 22, 4, 5);
    check_list(root_child(3), AMARK_LIST_ORDERED, ')', 2);
    check_node(nth_child(root_child(3), 1), AMARK_KIND_LIST_ITEM, 18, 22, 5,
               5);
    reset_doc();

    /* The number of a later item does not matter. */
    PARSE_PUBLIC("3. a\n1. b\n7. c");
    check_dump("(doc (ol 3 . tight (li (p)) (li (p)) (li (p))))");
}

NAOS_TEST(content_indent_of_list_items)
{
    amark_node_id_t item;

    /*
     * 1 to 4 spaces after the marker: the content indent is the marker and
     * the spaces. "-   a\n" 0-5, "\n" 6, "    b" 7-11: b is the item's.
     */
    PARSE_PUBLIC("-   a\n\n    b");
    check_dump("(doc (ul - loose (li (p) (p))))");
    NAOS_CHECK_UINT_EQ(child_count(amark_document_root(g_doc)), 1);
    item = nth_child(root_child(0), 0);
    NAOS_CHECK_UINT_EQ(child_count(item), 2);
    check_node(item, AMARK_KIND_LIST_ITEM, 0, 12, 1, 3);
    check_node(nth_child(item, 0), AMARK_KIND_PARAGRAPH, 4, 5, 1, 1);
    check_node(nth_child(item, 1), AMARK_KIND_PARAGRAPH, 11, 12, 3, 3);
    check_record(1, 4, 0);
    check_record(3, 11, 0);
    reset_doc();

    /* One column less: b is not the item's. */
    PARSE_PUBLIC("-   a\n\n   b");
    check_dump("(doc (ul - tight (li (p))) (p))");
    check_node(nth_child(root_child(0), 0), AMARK_KIND_LIST_ITEM, 0, 5, 1, 1);
    check_node(root_child(1), AMARK_KIND_PARAGRAPH, 10, 11, 3, 3);
    reset_doc();

    /*
     * 5 spaces or more: the content indent is the marker and 1 space, so
     * the content is indented code. "-     a": the code starts at a.
     */
    PARSE_PUBLIC("-     a");
    check_dump("(doc (ul - tight (li (code indented))))");
    check_node(nth_child(nth_child(root_child(0), 0), 0),
               AMARK_KIND_CODE_BLOCK, 6, 7, 1, 1);
    check_record(1, 6, 0);
    reset_doc();
    PARSE_PUBLIC("10.     a");
    check_dump("(doc (ol 10 . tight (li (code indented))))");
    check_node(nth_child(nth_child(root_child(0), 0), 0),
               AMARK_KIND_CODE_BLOCK, 8, 9, 1, 1);
    reset_doc();

    /*
     * Example 7, "-\t\tfoo": after '-' the tabs reach column 8, 7 columns;
     * the content indent is 2, and the 4 columns of the code end inside the
     * second tab, at offset 2 with 2 columns left.
     */
    PARSE_PUBLIC("-\t\tfoo");
    check_dump("(doc (ul - tight (li (code indented))))");
    check_node(nth_child(root_child(0), 0), AMARK_KIND_LIST_ITEM, 0, 6, 1, 1);
    check_node(nth_child(nth_child(root_child(0), 0), 0),
               AMARK_KIND_CODE_BLOCK, 2, 6, 1, 1);
    check_record(1, 2, 2);
    reset_doc();

    /* A tab of 3 columns after the marker: "-\ta\n" 0-3, "    b" 4-8. */
    PARSE_PUBLIC("-\ta\n    b");
    check_dump("(doc (ul - tight (li (p))))");
    check_node(nth_child(nth_child(root_child(0), 0), 0),
               AMARK_KIND_PARAGRAPH, 2, 9, 1, 2);
    check_record(1, 2, 0);
    check_record(2, 8, 0);
}

NAOS_TEST(items_that_start_with_a_blank_line)
{
    amark_node_id_t item;

    /* "-\n" 0-1, "  a" 2-4: the content indent is 2. */
    PARSE_PUBLIC("-\n  a");
    check_dump("(doc (ul - tight (li (p))))");
    item = nth_child(root_child(0), 0);
    check_node(item, AMARK_KIND_LIST_ITEM, 0, 5, 1, 2);
    check_node(nth_child(item, 0), AMARK_KIND_PARAGRAPH, 4, 5, 2, 2);
    check_record(1, 1, 0);
    check_record(2, 4, 0);
    reset_doc();

    /* With spaces after the marker: "- \n" 0-2, "  a" 3-5. */
    PARSE_PUBLIC("- \n  a");
    check_dump("(doc (ul - tight (li (p))))");
    check_node(nth_child(root_child(0), 0), AMARK_KIND_LIST_ITEM, 0, 6, 1, 2);
    check_record(1, 2, 0);
    reset_doc();

    /* Example 280: one blank line at most. "-\n" 0-1, "\n" 2, "  a" 3-5. */
    PARSE_PUBLIC("-\n\n  a");
    check_dump("(doc (ul - tight (li)) (p))");
    check_node(root_child(0), AMARK_KIND_LIST, 0, 1, 1, 1);
    check_node(nth_child(root_child(0), 0), AMARK_KIND_LIST_ITEM, 0, 1, 1, 1);
    check_node(root_child(1), AMARK_KIND_PARAGRAPH, 5, 6, 3, 3);
    check_record(2, 2, 0);
    reset_doc();

    /* Empty items in a list: "- a\n" 0-3, "-\n" 4-5, "- c" 6-8. */
    PARSE_PUBLIC("- a\n-\n- c");
    check_dump("(doc (ul - tight (li (p)) (li) (li (p))))");
    check_node(nth_child(root_child(0), 1), AMARK_KIND_LIST_ITEM, 4, 5, 2, 2);
    check_record(2, 5, 0);
    reset_doc();

    /* An empty ordered item with spaces: "1.  " 0-3. */
    PARSE_PUBLIC("1.  ");
    check_dump("(doc (ol 1 . tight (li)))");
    check_node(nth_child(root_child(0), 0), AMARK_KIND_LIST_ITEM, 0, 4, 1, 1);
    check_record(1, 4, 0);
}

NAOS_TEST(blank_lines_inside_list_items)
{
    amark_node_id_t item;
    amark_node_id_t def;

    /* "- a\n" 0-3, "\n" 4, "  b" 5-7: the item goes on after the blank. */
    PARSE_PUBLIC("- a\n\n  b");
    check_dump("(doc (ul - loose (li (p) (p))))");
    NAOS_CHECK_UINT_EQ(child_count(amark_document_root(g_doc)), 1);
    NAOS_CHECK_UINT_EQ(child_count(root_child(0)), 1);
    item = nth_child(root_child(0), 0);
    NAOS_CHECK_UINT_EQ(child_count(item), 2);
    check_node(root_child(0), AMARK_KIND_LIST, 0, 8, 1, 3);
    check_node(item, AMARK_KIND_LIST_ITEM, 0, 8, 1, 3);
    check_node(nth_child(item, 0), AMARK_KIND_PARAGRAPH, 2, 3, 1, 1);
    check_node(nth_child(item, 1), AMARK_KIND_PARAGRAPH, 7, 8, 3, 3);
    check_record(2, 4, 0);
    check_record(3, 7, 0);
    reset_doc();

    /* Two blank lines: "- a\n" 0-3, "\n" 4, "\n" 5, "  b" 6-8. */
    PARSE_PUBLIC("- a\n\n\n  b");
    check_dump("(doc (ul - loose (li (p) (p))))");
    NAOS_CHECK_UINT_EQ(child_count(amark_document_root(g_doc)), 1);
    item = nth_child(root_child(0), 0);
    NAOS_CHECK_UINT_EQ(child_count(item), 2);
    check_node(item, AMARK_KIND_LIST_ITEM, 0, 9, 1, 4);
    reset_doc();

    /* Less indented after the blank: the item ends; "- a\n\n b". */
    PARSE_PUBLIC("- a\n\n b");
    check_dump("(doc (ul - tight (li (p))) (p))");
    check_node(root_child(0), AMARK_KIND_LIST, 0, 3, 1, 1);
    reset_doc();

    /*
     * A paragraph of a definition only counts as a child: the item goes on
     * after the blank. "- [a]: /u\n" 0-9, "\n" 10, "  b" 11-13.
     */
    PARSE_PUBLIC("- [a]: /u\n\n  b");
    check_dump("(doc (ul - loose (li (def) (p))))");
    NAOS_CHECK_UINT_EQ(child_count(amark_document_root(g_doc)), 1);
    item = nth_child(root_child(0), 0);
    NAOS_CHECK_UINT_EQ(child_count(item), 2);
    check_node(item, AMARK_KIND_LIST_ITEM, 0, 14, 1, 3);
    def = nth_child(item, 0);
    check_node(def, AMARK_KIND_LINK_REFERENCE_DEFINITION, 2, 9, 1, 1);
    check_definition(def, 3, 4, 7, 9, false, 0, 0);
    check_node(nth_child(item, 1), AMARK_KIND_PARAGRAPH, 13, 14, 3, 3);
    NAOS_CHECK_UINT_EQ(definition_count(), 1);
    NAOS_CHECK_UINT_EQ(amark_definitions_find(g_doc, 3, 4), def);
    reset_doc();

    /*
     * Example 5, "- foo\n\n\t\tbar": the item takes 2 columns of the first
     * tab, the code the other 2 and 2 of the second, which has 2 left.
     * "- foo\n" 0-5, "\n" 6, "\t\tbar" 7-11.
     */
    PARSE_PUBLIC("- foo\n\n\t\tbar");
    check_dump("(doc (ul - loose (li (p) (code indented))))");
    item = nth_child(root_child(0), 0);
    NAOS_CHECK_UINT_EQ(child_count(item), 2);
    check_node(item, AMARK_KIND_LIST_ITEM, 0, 12, 1, 3);
    check_node(nth_child(item, 1), AMARK_KIND_CODE_BLOCK, 8, 12, 3, 3);
    check_record(3, 8, 2);
    reset_doc();

    /*
     * An HTML line whose prefix ends inside a tab: "- a\n" 0-3, "\n" 4,
     * "\t <div>" 5-11. The item takes 2 columns of the tab; the block starts
     * on the tab with 2 columns left.
     */
    PARSE_PUBLIC("- a\n\n\t <div>");
    check_dump("(doc (ul - loose (li (p) (html))))");
    item = nth_child(root_child(0), 0);
    NAOS_CHECK_UINT_EQ(child_count(item), 2);
    check_node(nth_child(item, 1), AMARK_KIND_HTML_BLOCK, 5, 12, 3, 3);
    check_record(3, 5, 2);
}

NAOS_TEST(nested_lists)
{
    amark_node_id_t outer;
    amark_node_id_t inner;

    /*
     * Example 9, " - foo\n   - bar\n\t - baz": " - foo\n" 0-6,
     * "   - bar\n" 7-15, "\t - baz" 16-22. The outer item takes 3 columns of
     * the tab, the middle one the 4th and the space.
     */
    PARSE_PUBLIC(" - foo\n   - bar\n\t - baz");
    check_dump("(doc (ul - tight (li (p) (ul - tight (li (p) "
               "(ul - tight (li (p))))))))");
    outer = nth_child(root_child(0), 0);
    check_node(root_child(0), AMARK_KIND_LIST, 1, 23, 1, 3);
    check_node(outer, AMARK_KIND_LIST_ITEM, 1, 23, 1, 3);
    check_node(nth_child(outer, 0), AMARK_KIND_PARAGRAPH, 3, 6, 1, 1);
    inner = nth_child(outer, 1);
    check_node(inner, AMARK_KIND_LIST, 10, 23, 2, 3);
    inner = nth_child(inner, 0);
    check_node(inner, AMARK_KIND_LIST_ITEM, 10, 23, 2, 3);
    check_node(nth_child(inner, 0), AMARK_KIND_PARAGRAPH, 12, 15, 2, 2);
    inner = nth_child(inner, 1);
    check_node(inner, AMARK_KIND_LIST, 18, 23, 3, 3);
    check_node(nth_child(inner, 0), AMARK_KIND_LIST_ITEM, 18, 23, 3, 3);
    check_node(nth_child(nth_child(inner, 0), 0), AMARK_KIND_PARAGRAPH, 20,
               23, 3, 3);
    check_record(1, 3, 0);
    check_record(2, 12, 0);
    check_record(3, 20, 0);
    reset_doc();

    /* Two markers on one line: "- 1. a". */
    PARSE_PUBLIC("- 1. a");
    check_dump("(doc (ul - tight (li (ol 1 . tight (li (p))))))");
    check_node(nth_child(nth_child(root_child(0), 0), 0), AMARK_KIND_LIST, 2,
               6, 1, 1);
    check_record(1, 5, 0);
    reset_doc();

    /* Back to the outer list: "- a\n" 0-3, "  - b\n" 4-9, "- c" 10-12. */
    PARSE_PUBLIC("- a\n  - b\n- c");
    check_dump("(doc (ul - tight (li (p) (ul - tight (li (p)))) (li (p))))");
    outer = nth_child(root_child(0), 0);
    check_node(outer, AMARK_KIND_LIST_ITEM, 0, 9, 1, 2);
    check_node(nth_child(outer, 1), AMARK_KIND_LIST, 6, 9, 2, 2);
    check_node(nth_child(root_child(0), 1), AMARK_KIND_LIST_ITEM, 10, 13, 3,
               3);
    reset_doc();

    /*
     * Example 312: a marker indented 4 columns from the list's content is
     * not an item, even while the list is open; the line is lazy.
     */
    PARSE_PUBLIC("- a\n - b\n  - c\n   - d\n    - e");
    check_dump("(doc (ul - tight (li (p)) (li (p)) (li (p)) (li (p))))");
    check_node(nth_child(root_child(0), 3), AMARK_KIND_LIST_ITEM, 18, 29, 4,
               5);
}

NAOS_TEST(list_items_and_paragraphs)
{
    /* A bullet item interrupts a paragraph: "a\n" 0-1, "- b" 2-4. */
    PARSE_PUBLIC("a\n- b");
    check_dump("(doc (p) (ul - tight (li (p))))");
    check_node(root_child(0), AMARK_KIND_PARAGRAPH, 0, 1, 1, 1);
    check_node(root_child(1), AMARK_KIND_LIST, 2, 5, 2, 2);
    reset_doc();

    /* An ordered item, only when it starts with 1. */
    PARSE_PUBLIC("a\n1) b");
    check_dump("(doc (p) (ol 1 ) tight (li (p))))");
    reset_doc();
    PARSE_PUBLIC("a\n2. b");
    check_dump("(doc (p))");
    check_node(root_child(0), AMARK_KIND_PARAGRAPH, 0, 6, 1, 2);
    reset_doc();

    /* An empty item does not interrupt one. */
    PARSE_PUBLIC("a\n*");
    check_dump("(doc (p))");
    reset_doc();
    PARSE_PUBLIC("a\n1.  ");
    check_dump("(doc (p))");
    reset_doc();

    /* Inside an item, the same: "- a\n  2. b" is one paragraph. */
    PARSE_PUBLIC("- a\n  2. b");
    check_dump("(doc (ul - tight (li (p))))");
    reset_doc();
    PARSE_PUBLIC("- a\n  1. b");
    check_dump("(doc (ul - tight (li (p) (ol 1 . tight (li (p))))))");
    reset_doc();

    /*
     * A line that does not match the open block quote is not lazy if it
     * starts an item: "> a\n2. b".
     */
    PARSE_PUBLIC("> a\n2. b");
    check_dump("(doc (bq (p)) (ol 2 . tight (li (p))))");
    reset_doc();

    /* A lazy line starting an HTML block of condition 7 in an item. */
    PARSE_PUBLIC("- a\n<span>");
    check_dump("(doc (ul - tight (li (p))))");
    check_node(nth_child(root_child(0), 0), AMARK_KIND_LIST_ITEM, 0, 10, 1,
               2);
}

NAOS_TEST(list_items_and_thematic_breaks_and_setext_headings)
{
    amark_node_id_t item;

    /* A thematic break is tried first. */
    PARSE_PUBLIC("* * *");
    check_dump("(doc (hr))");
    reset_doc();
    PARSE_PUBLIC("- foo\n***");
    check_dump("(doc (ul - tight (li (p))) (hr))");
    check_node(root_child(0), AMARK_KIND_LIST, 0, 5, 1, 1);
    check_node(root_child(1), AMARK_KIND_THEMATIC_BREAK, 6, 9, 2, 2);
    reset_doc();

    /*
     * A failed try skips the later tries inside the same run of the
     * character only: "* - - -" is an item holding a break, "- - - a" three
     * nested items.
     */
    PARSE_PUBLIC("* - - -");
    check_dump("(doc (ul * tight (li (hr))))");
    check_node(nth_child(nth_child(root_child(0), 0), 0),
               AMARK_KIND_THEMATIC_BREAK, 2, 7, 1, 1);
    reset_doc();
    PARSE_PUBLIC("- - - a");
    check_dump("(doc (ul - tight (li (ul - tight (li (ul - tight "
               "(li (p))))))))");
    check_record(1, 6, 0);
    reset_doc();
    PARSE_PUBLIC("- - -\n- - - -");
    check_dump("(doc (hr) (hr))");
    reset_doc();

    /* Example 61: "- Foo\n" 0-5, "- * * *" 6-12. */
    PARSE_PUBLIC("- Foo\n- * * *");
    check_dump("(doc (ul - tight (li (p)) (li (hr))))");
    item = nth_child(root_child(0), 1);
    check_node(item, AMARK_KIND_LIST_ITEM, 6, 13, 2, 2);
    check_node(nth_child(item, 0), AMARK_KIND_THEMATIC_BREAK, 8, 13, 2, 2);
    check_record(2, 8, 0);
    reset_doc();

    /* Examples 94 and 99: an underline outside the item is a break. */
    PARSE_PUBLIC("- Foo\n---");
    check_dump("(doc (ul - tight (li (p))) (hr))");
    check_node(root_child(1), AMARK_KIND_THEMATIC_BREAK, 6, 9, 2, 2);
    reset_doc();
    PARSE_PUBLIC("- foo\n-----");
    check_dump("(doc (ul - tight (li (p))) (hr))");
    reset_doc();

    /* Inside the item it underlines: "- Foo\n" 0-5, "  ---" 6-10. */
    PARSE_PUBLIC("- Foo\n  ---");
    check_dump("(doc (ul - tight (li (h 2 setext))))");
    item = nth_child(root_child(0), 0);
    check_node(item, AMARK_KIND_LIST_ITEM, 0, 11, 1, 2);
    check_node(nth_child(item, 0), AMARK_KIND_HEADING, 2, 11, 1, 2);
    check_content(0, nth_child(item, 0), 2, 5);
    check_record(2, 8, 0);
    reset_doc();

    /* A heading and a fence as the first block of an item. */
    PARSE_PUBLIC("- # a\n- ```\n  b\n  ```");
    check_dump("(doc (ul - tight (li (h 1 atx)) (li (code fenced ` 3))))");
    check_node(nth_child(root_child(0), 1), AMARK_KIND_LIST_ITEM, 6, 21, 2,
               4);
    check_record(3, 14, 0);
}

NAOS_TEST(lists_in_block_quotes)
{
    /* Example 235: "> - foo\n" 0-7, "- bar" 8-12. */
    PARSE_PUBLIC("> - foo\n- bar");
    check_dump("(doc (bq (ul - tight (li (p)))) (ul - tight (li (p))))");
    check_node(root_child(0), AMARK_KIND_BLOCK_QUOTE, 0, 7, 1, 1);
    check_node(nth_child(root_child(0), 0), AMARK_KIND_LIST, 2, 7, 1, 1);
    check_node(nth_child(nth_child(root_child(0), 0), 0),
               AMARK_KIND_LIST_ITEM, 2, 7, 1, 1);
    check_node(root_child(1), AMARK_KIND_LIST, 8, 13, 2, 2);
    check_record(1, 4, 0);
    check_record(2, 10, 0);
    reset_doc();

    /* A block quote in an item: "- > a\n" 0-5, "  > b" 6-10. */
    PARSE_PUBLIC("- > a\n  > b");
    check_dump("(doc (ul - tight (li (bq (p)))))");
    check_node(nth_child(nth_child(root_child(0), 0), 0),
               AMARK_KIND_BLOCK_QUOTE, 2, 11, 1, 2);
    check_record(2, 10, 0);
    reset_doc();

    /* A lazy line through both: "> - a\n" 0-5, "b" 6. */
    PARSE_PUBLIC("> - a\nb");
    check_dump("(doc (bq (ul - tight (li (p)))))");
    check_node(root_child(0), AMARK_KIND_BLOCK_QUOTE, 0, 7, 1, 2);
    check_node(nth_child(root_child(0), 0), AMARK_KIND_LIST, 2, 7, 1, 2);
    check_record(2, 6, 0);
}

NAOS_TEST(list_with_crlf_in_a_range_starting_inside_a_line)
{
    /*
     * "x- a\r\n" 0-5, "  b\r\n" 6-10; the range starts at 1, which is
     * column 0.
     */
    PARSE_RANGE("x- a\r\n  b\r\n", 1, 11);
    check_dump("(doc (ul - tight (li (p))))");
    check_node(root_child(0), AMARK_KIND_LIST, 1, 9, 1, 2);
    check_node(nth_child(root_child(0), 0), AMARK_KIND_LIST_ITEM, 1, 9, 1, 2);
    check_node(nth_child(nth_child(root_child(0), 0), 0),
               AMARK_KIND_PARAGRAPH, 3, 9, 1, 2);
    check_record(1, 3, 0);
    check_record(2, 8, 0);
}

NAOS_TEST(limits_with_lists)
{
    /* "- a": the list at depth 1, the item at 2, the paragraph at 3. */
    NAOS_CHECK_INT_EQ(parse_public("- a", 3, 2, 0),
                      AMARK_ERROR_LIMIT_EXCEEDED);
    NAOS_CHECK_TRUE(g_doc == NULL);
    NAOS_REQUIRE_INT_EQ(parse_public("- a", 3, 3, 0), AMARK_ERROR_NONE);
    reset_doc();

    /* The root, the list, the item and the paragraph. */
    NAOS_CHECK_INT_EQ(parse_public("- a", 3, 0, 3),
                      AMARK_ERROR_LIMIT_EXCEEDED);
    NAOS_CHECK_TRUE(g_doc == NULL);
    NAOS_REQUIRE_INT_EQ(parse_public("- a", 3, 0, 4), AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 4);
}

/* DEEP markers "- ", "a", a line ending, 2 DEEP spaces and "b". */
static char g_deep_list[4 * DEEP + 3];

NAOS_TEST(deeply_nested_lists)
{
    amark_node_id_t node;
    size_t i;

    for (i = 0; i < DEEP; i++) {
        g_deep_list[2 * i] = '-';
        g_deep_list[2 * i + 1] = ' ';
    }
    memcpy(g_deep_list + 2 * DEEP, "a\n", 2);
    memset(g_deep_list + 2 * DEEP + 2, ' ', 2 * DEEP);
    g_deep_list[4 * DEEP + 2] = 'b';

    /* The paragraph is at depth 2 DEEP + 1. */
    NAOS_CHECK_INT_EQ(
        parse_public(g_deep_list, sizeof(g_deep_list), 2 * DEEP, 0),
        AMARK_ERROR_LIMIT_EXCEEDED);
    NAOS_REQUIRE_INT_EQ(
        parse_public(g_deep_list, sizeof(g_deep_list), 2 * DEEP + 1, 0),
        AMARK_ERROR_NONE);
    reset_doc();

    parse_public_ok(g_deep_list, sizeof(g_deep_list));
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 2 * DEEP + 2);
    node = amark_document_root(g_doc);
    for (i = 0; i < 2 * DEEP; i++) {
        amark_range_t range;

        node = amark_node_first_child(g_doc, node);
        NAOS_REQUIRE_UINT_EQ(amark_node_kind(g_doc, node),
                             i % 2 == 0 ? AMARK_KIND_LIST
                                        : AMARK_KIND_LIST_ITEM);
        NAOS_REQUIRE_TRUE(amark_node_range(g_doc, node, &range));
        NAOS_REQUIRE_UINT_EQ(range.start, i - i % 2);
        NAOS_REQUIRE_UINT_EQ(range.end, 4 * DEEP + 3);
        NAOS_REQUIRE_UINT_EQ(amark_node_next_sibling(g_doc, node),
                             AMARK_NODE_NONE);
    }
    check_node(amark_node_first_child(g_doc, node), AMARK_KIND_PARAGRAPH,
               2 * DEEP, 4 * DEEP + 3, 1, 2);
    check_record(1, 2 * DEEP, 0);
    check_record(2, 4 * DEEP + 2, 0);
}

NAOS_TEST(allocation_failure_with_lists)
{
    static const char k_input[] = "- a\n  - b\n1. c\n\n> - [x]: /u\n";
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
    NAOS_CHECK_UINT_GT(fail_at, 1);
    NAOS_CHECK_INT_EQ(amark_tree_check(g_doc).violation,
                      AMARK_TREE_VIOLATION_NONE);
    check_dump("(doc (ul - tight (li (p) (ul - tight (li (p))))) "
               "(ol 1 . tight (li (p))) (bq (ul - tight (li (def)))))");
    NAOS_CHECK_UINT_EQ(definition_count(), 1);
}

/* ---------------------------------------------------------------------- */
/* Tight and loose lists                                                  */
/* ---------------------------------------------------------------------- */

/*
 * Parses s and checks its dump, with the example number put before both so
 * that a failure names the example.
 */
static void check_example_dump(int example, const char *s, const char *want)
{
    char got[DUMP_SIZE + 32];
    char expected[DUMP_SIZE + 32];

    parse_public_ok(s, strlen(s));
    (void)amark_test_dump_blocks(g_doc, g_dump, DUMP_SIZE);
    (void)snprintf(got, sizeof(got), "%d: %s", example, g_dump);
    (void)snprintf(expected, sizeof(expected), "%d: %s", example, want);
    NAOS_CHECK_STR_EQ(got, expected);
    reset_doc();
}

/*
 * Every example of the sections "List items" and "Lists" of spec.txt that
 * has a list. The spacing of each list is the one the HTML of the example
 * shows (a loose list wraps the paragraphs of its items in <p>); where no
 * item has a paragraph, it follows the rules of section 5.3.
 */
NAOS_TEST(spec_list_examples_are_tight_or_loose)
{
    static const struct {
        int example;
        const char *input;
        const char *dump;
    } examples[] = {
        {254,
         "1.  A paragraph\n    with two lines.\n\n        indented code\n\n"
         "    > A block quote.\n",
         "(doc (ol 1 . loose (li (p) (code indented) (bq (p)))))"},
        {255,
         "- one\n\n two\n",
         "(doc (ul - tight (li (p))) (p))"},
        {256,
         "- one\n\n  two\n",
         "(doc (ul - loose (li (p) (p))))"},
        {257,
         " -    one\n\n     two\n",
         "(doc (ul - tight (li (p))) (code indented))"},
        {258,
         " -    one\n\n      two\n",
         "(doc (ul - loose (li (p) (p))))"},
        {259,
         "   > > 1.  one\n>>\n>>     two\n",
         "(doc (bq (bq (ol 1 . loose (li (p) (p))))))"},
        {260,
         ">>- one\n>>\n  >  > two\n",
         "(doc (bq (bq (ul - tight (li (p))) (p))))"},
        {262,
         "- foo\n\n\n  bar\n",
         "(doc (ul - loose (li (p) (p))))"},
        {263,
         "1.  foo\n\n    ```\n    bar\n    ```\n\n    baz\n\n    > bam\n",
         "(doc (ol 1 . loose (li (p) (code fenced ` 3) (p) (bq (p)))))"},
        {264,
         "- Foo\n\n      bar\n\n\n      baz\n",
         "(doc (ul - loose (li (p) (code indented))))"},
        {265,
         "123456789. ok\n",
         "(doc (ol 123456789 . tight (li (p))))"},
        {267,
         "0. ok\n",
         "(doc (ol 0 . tight (li (p))))"},
        {268,
         "003. ok\n",
         "(doc (ol 3 . tight (li (p))))"},
        {270,
         "- foo\n\n      bar\n",
         "(doc (ul - loose (li (p) (code indented))))"},
        {271,
         "  10.  foo\n\n           bar\n",
         "(doc (ol 10 . loose (li (p) (code indented))))"},
        {273,
         "1.     indented code\n\n   paragraph\n\n       more code\n",
         "(doc (ol 1 . loose (li (code indented) (p) (code indented))))"},
        {274,
         "1.      indented code\n\n   paragraph\n\n       more code\n",
         "(doc (ol 1 . loose (li (code indented) (p) (code indented))))"},
        {276,
         "-    foo\n\n  bar\n",
         "(doc (ul - tight (li (p))) (p))"},
        {277,
         "-  foo\n\n   bar\n",
         "(doc (ul - loose (li (p) (p))))"},
        {278,
         "-\n  foo\n-\n  ```\n  bar\n  ```\n-\n      baz\n",
         "(doc (ul - tight (li (p)) (li (code fenced ` 3)) (li (code "
         "indented))))"},
        {279,
         "-   \n  foo\n",
         "(doc (ul - tight (li (p))))"},
        {280,
         "-\n\n  foo\n",
         "(doc (ul - tight (li)) (p))"},
        {281,
         "- foo\n-\n- bar\n",
         "(doc (ul - tight (li (p)) (li) (li (p))))"},
        {282,
         "- foo\n-   \n- bar\n",
         "(doc (ul - tight (li (p)) (li) (li (p))))"},
        {283,
         "1. foo\n2.\n3. bar\n",
         "(doc (ol 1 . tight (li (p)) (li) (li (p))))"},
        {284,
         "*\n",
         "(doc (ul * tight (li)))"},
        {286,
         " 1.  A paragraph\n     with two lines.\n\n"
         "         indented code\n\n     > A block quote.\n",
         "(doc (ol 1 . loose (li (p) (code indented) (bq (p)))))"},
        {287,
         "  1.  A paragraph\n      with two lines.\n\n"
         "          indented code\n\n      > A block quote.\n",
         "(doc (ol 1 . loose (li (p) (code indented) (bq (p)))))"},
        {288,
         "   1.  A paragraph\n       with two lines.\n\n"
         "           indented code\n\n       > A block quote.\n",
         "(doc (ol 1 . loose (li (p) (code indented) (bq (p)))))"},
        {290,
         "  1.  A paragraph\nwith two lines.\n\n          indented code\n\n"
         "      > A block quote.\n",
         "(doc (ol 1 . loose (li (p) (code indented) (bq (p)))))"},
        {291,
         "  1.  A paragraph\n    with two lines.\n",
         "(doc (ol 1 . tight (li (p))))"},
        {292,
         "> 1. > Blockquote\ncontinued here.\n",
         "(doc (bq (ol 1 . tight (li (bq (p))))))"},
        {293,
         "> 1. > Blockquote\n> continued here.\n",
         "(doc (bq (ol 1 . tight (li (bq (p))))))"},
        {294,
         "- foo\n  - bar\n    - baz\n      - boo\n",
         "(doc (ul - tight (li (p) (ul - tight (li (p) (ul - tight (li (p) "
         "(ul - tight (li (p))))))))))"},
        {295,
         "- foo\n - bar\n  - baz\n   - boo\n",
         "(doc (ul - tight (li (p)) (li (p)) (li (p)) (li (p))))"},
        {296,
         "10) foo\n    - bar\n",
         "(doc (ol 10 ) tight (li (p) (ul - tight (li (p))))))"},
        {297,
         "10) foo\n   - bar\n",
         "(doc (ol 10 ) tight (li (p))) (ul - tight (li (p))))"},
        {298,
         "- - foo\n",
         "(doc (ul - tight (li (ul - tight (li (p))))))"},
        {299,
         "1. - 2. foo\n",
         "(doc (ol 1 . tight (li (ul - tight (li (ol 2 . tight (li "
         "(p))))))))"},
        {300,
         "- # Foo\n- Bar\n  ---\n  baz\n",
         "(doc (ul - tight (li (h 1 atx)) (li (h 2 setext) (p))))"},
        {301,
         "- foo\n- bar\n+ baz\n",
         "(doc (ul - tight (li (p)) (li (p))) (ul + tight (li (p))))"},
        {302,
         "1. foo\n2. bar\n3) baz\n",
         "(doc (ol 1 . tight (li (p)) (li (p))) (ol 3 ) tight (li (p))))"},
        {303,
         "Foo\n- bar\n- baz\n",
         "(doc (p) (ul - tight (li (p)) (li (p))))"},
        {305,
         "The number of windows in my house is\n"
         "1.  The number of doors is 6.\n",
         "(doc (p) (ol 1 . tight (li (p))))"},
        {306,
         "- foo\n\n- bar\n\n\n- baz\n",
         "(doc (ul - loose (li (p)) (li (p)) (li (p))))"},
        {307,
         "- foo\n  - bar\n    - baz\n\n\n      bim\n",
         "(doc (ul - tight (li (p) (ul - tight (li (p) (ul - loose (li (p) "
         "(p))))))))"},
        {308,
         "- foo\n- bar\n\n<!-- -->\n\n- baz\n- bim\n",
         "(doc (ul - tight (li (p)) (li (p))) (html) (ul - tight (li (p)) "
         "(li (p))))"},
        {309,
         "-   foo\n\n    notcode\n\n-   foo\n\n<!-- -->\n\n    code\n",
         "(doc (ul - loose (li (p) (p)) (li (p))) (html) (code indented))"},
        {310,
         "- a\n - b\n  - c\n   - d\n  - e\n - f\n- g\n",
         "(doc (ul - tight (li (p)) (li (p)) (li (p)) (li (p)) (li (p)) "
         "(li (p)) (li (p))))"},
        {311,
         "1. a\n\n  2. b\n\n   3. c\n",
         "(doc (ol 1 . loose (li (p)) (li (p)) (li (p))))"},
        {312,
         "- a\n - b\n  - c\n   - d\n    - e\n",
         "(doc (ul - tight (li (p)) (li (p)) (li (p)) (li (p))))"},
        {313,
         "1. a\n\n  2. b\n\n    3. c\n",
         "(doc (ol 1 . loose (li (p)) (li (p))) (code indented))"},
        {314,
         "- a\n- b\n\n- c\n",
         "(doc (ul - loose (li (p)) (li (p)) (li (p))))"},
        {315,
         "* a\n*\n\n* c\n",
         "(doc (ul * loose (li (p)) (li) (li (p))))"},
        {316,
         "- a\n- b\n\n  c\n- d\n",
         "(doc (ul - loose (li (p)) (li (p) (p)) (li (p))))"},
        {317,
         "- a\n- b\n\n  [ref]: /url\n- d\n",
         "(doc (ul - loose (li (p)) (li (p) (def)) (li (p))))"},
        {318,
         "- a\n- ```\n  b\n\n\n  ```\n- c\n",
         "(doc (ul - tight (li (p)) (li (code fenced ` 3)) (li (p))))"},
        {319,
         "- a\n  - b\n\n    c\n- d\n",
         "(doc (ul - tight (li (p) (ul - loose (li (p) (p)))) (li (p))))"},
        {320,
         "* a\n  > b\n  >\n* c\n",
         "(doc (ul * tight (li (p) (bq (p))) (li (p))))"},
        {321,
         "- a\n  > b\n  ```\n  c\n  ```\n- d\n",
         "(doc (ul - tight (li (p) (bq (p)) (code fenced ` 3)) (li (p))))"},
        {322,
         "- a\n",
         "(doc (ul - tight (li (p))))"},
        {323,
         "- a\n  - b\n",
         "(doc (ul - tight (li (p) (ul - tight (li (p))))))"},
        {324,
         "1. ```\n   foo\n   ```\n\n   bar\n",
         "(doc (ol 1 . loose (li (code fenced ` 3) (p))))"},
        {325,
         "* foo\n  * bar\n\n  baz\n",
         "(doc (ul * loose (li (p) (ul * tight (li (p))) (p))))"},
        {326,
         "- a\n  - b\n  - c\n\n- d\n  - e\n  - f\n",
         "(doc (ul - loose (li (p) (ul - tight (li (p)) (li (p)))) (li (p) "
         "(ul - tight (li (p)) (li (p))))))"},
    };
    size_t i;

    for (i = 0; i < sizeof(examples) / sizeof(examples[0]); i++) {
        check_example_dump(examples[i].example, examples[i].input,
                           examples[i].dump);
    }
}

NAOS_TEST(blank_line_between_items_makes_the_list_loose)
{
    /* "- a\n" 0-3, "\n" 4, "- b" 5-7. */
    PARSE_PUBLIC("- a\n\n- b");
    check_dump("(doc (ul - loose (li (p)) (li (p))))");
    NAOS_CHECK_INT_EQ(amark_list_spacing(g_doc, root_child(0)),
                      AMARK_LIST_LOOSE);
    check_node(root_child(0), AMARK_KIND_LIST, 0, 8, 1, 3);
    reset_doc();

    /* A line of spaces and tabs is blank too. */
    PARSE_PUBLIC("1. a\n \t \n2. b");
    check_dump("(doc (ol 1 . loose (li (p)) (li (p))))");
    reset_doc();

    /* Only between the second and the third item. */
    PARSE_PUBLIC("- a\n- b\n\n- c");
    check_dump("(doc (ul - loose (li (p)) (li (p)) (li (p))))");
    reset_doc();

    /* With CRLF line endings. */
    PARSE_PUBLIC("- a\r\n\r\n- b\r\n");
    check_dump("(doc (ul - loose (li (p)) (li (p))))");
    reset_doc();

    /* After an empty item: the blank line ends it, the list goes on. */
    PARSE_PUBLIC("- a\n-\n\n- c");
    check_dump("(doc (ul - loose (li (p)) (li) (li (p))))");
    reset_doc();

    /* An item ending with indented code then a blank line. */
    PARSE_PUBLIC("-     a\n\n- b");
    check_dump("(doc (ul - loose (li (code indented)) (li (p))))");
    reset_doc();

    /* Without a blank line, tight. */
    PARSE_PUBLIC("- a\n- b\n- c");
    check_dump("(doc (ul - tight (li (p)) (li (p)) (li (p))))");
    NAOS_CHECK_INT_EQ(amark_list_spacing(g_doc, root_child(0)),
                      AMARK_LIST_TIGHT);
}

NAOS_TEST(blank_line_between_children_of_an_item_makes_the_list_loose)
{
    /* Between two paragraphs of the only item. */
    PARSE_PUBLIC("- a\n\n  b");
    check_dump("(doc (ul - loose (li (p) (p))))");
    reset_doc();

    /* Between a paragraph and a nested list. */
    PARSE_PUBLIC("- a\n\n  - b");
    check_dump("(doc (ul - loose (li (p) (ul - tight (li (p))))))");
    reset_doc();

    /* Between a closed fenced code block and a paragraph. */
    PARSE_PUBLIC("- ```\n  a\n  ```\n\n  b");
    check_dump("(doc (ul - loose (li (code fenced ` 3) (p))))");
    reset_doc();

    /* Between a block quote and a paragraph. */
    PARSE_PUBLIC("- > a\n\n  b");
    check_dump("(doc (ul - loose (li (bq (p)) (p))))");
    reset_doc();

    /* After an HTML block that a blank line ends (condition 6). */
    PARSE_PUBLIC("- <div>\n\n  b");
    check_dump("(doc (ul - loose (li (html) (p))))");
    reset_doc();

    /* In the second item of a list; the first is tight on its own. */
    PARSE_PUBLIC("1. a\n2. b\n\n   c");
    check_dump("(doc (ol 1 . loose (li (p)) (li (p) (p))))");
    reset_doc();

    /* Children with no blank line between them: tight. */
    PARSE_PUBLIC("- a\n  > b\n  # c\n  ---");
    check_dump("(doc (ul - tight (li (p) (bq (p)) (h 1 atx) (hr))))");
}

/*
 * A link reference definition is a block of the item, as in example 317,
 * where the blank line before "[ref]: /url" makes the list loose. cmark and
 * commonmark.js drop a paragraph of definitions only, so the first two
 * inputs below are tight there.
 */
NAOS_TEST(definitions_count_as_children_of_an_item)
{
    /* A definition, a blank line, a paragraph. */
    PARSE_PUBLIC("- [a]: /u\n\n  b");
    check_dump("(doc (ul - loose (li (def) (p))))");
    reset_doc();

    /* A paragraph, a blank line, a definition. */
    PARSE_PUBLIC("- b\n\n  [a]: /u");
    check_dump("(doc (ul - loose (li (p) (def))))");
    reset_doc();

    /* A definition then a paragraph, no blank line between them. */
    PARSE_PUBLIC("- [a]: /u\n  b");
    check_dump("(doc (ul - tight (li (def) (p))))");
    reset_doc();

    /* An item of a definition only, then a blank line and an item. */
    PARSE_PUBLIC("- [a]: /u\n\n- b");
    check_dump("(doc (ul - loose (li (def)) (li (p))))");
}

NAOS_TEST(blank_line_in_a_nested_list_leaves_the_outer_list_tight)
{
    /* The blank line is between children of the inner item only. */
    PARSE_PUBLIC("- a\n  - b\n\n    c\n- d");
    check_dump("(doc (ul - tight (li (p) (ul - loose (li (p) (p)))) "
               "(li (p))))");
    reset_doc();

    /* Between inner items. */
    PARSE_PUBLIC("- a\n  - b\n\n  - c\n- d");
    check_dump("(doc (ul - tight (li (p) (ul - loose (li (p)) (li (p)))) "
               "(li (p))))");
    reset_doc();

    /* Three levels; the blank line is in the innermost one. */
    PARSE_PUBLIC("- a\n  - b\n    - c\n\n      d\n  - e\n- f");
    check_dump("(doc (ul - tight (li (p) (ul - tight (li (p) "
               "(ul - loose (li (p) (p)))) (li (p)))) (li (p))))");
    reset_doc();

    /* The blank line ends the last item of the inner list and the outer
     * list goes on: only the outer list is loose. */
    PARSE_PUBLIC("- a\n  - b\n\n- c");
    check_dump("(doc (ul - loose (li (p) (ul - tight (li (p)))) (li (p))))");
    reset_doc();

    /* The same, and the outer item goes on with a paragraph. */
    PARSE_PUBLIC("- a\n  - b\n\n  c");
    check_dump("(doc (ul - loose (li (p) (ul - tight (li (p))) (p))))");
    reset_doc();

    /* A blank line in the inner list that ends with the document. */
    PARSE_PUBLIC("- a\n  - b\n\n");
    check_dump("(doc (ul - tight (li (p) (ul - tight (li (p))))))");
    reset_doc();

    /* An empty inner item, a blank line, an outer item. */
    PARSE_PUBLIC("- a\n  - b\n  -\n\n- c");
    check_dump("(doc (ul - loose (li (p) (ul - tight (li (p)) (li))) "
               "(li (p))))");
}

NAOS_TEST(blank_line_after_the_last_item_leaves_the_list_tight)
{
    /* At the end of the document, one or more, with spaces. */
    PARSE_PUBLIC("- a\n- b\n");
    check_dump("(doc (ul - tight (li (p)) (li (p))))");
    reset_doc();
    PARSE_PUBLIC("- a\n- b\n\n");
    check_dump("(doc (ul - tight (li (p)) (li (p))))");
    reset_doc();
    PARSE_PUBLIC("- a\n- b\n\n  \n\t\n");
    check_dump("(doc (ul - tight (li (p)) (li (p))))");
    reset_doc();

    /* Before a paragraph after the list. */
    PARSE_PUBLIC("- a\n- b\n\nc");
    check_dump("(doc (ul - tight (li (p)) (li (p))) (p))");
    reset_doc();

    /* Before a list of another marker. */
    PARSE_PUBLIC("- a\n- b\n\n+ c\n+ d");
    check_dump("(doc (ul - tight (li (p)) (li (p))) "
               "(ul + tight (li (p)) (li (p))))");
    reset_doc();

    /* Before a heading and a thematic break. */
    PARSE_PUBLIC("1. a\n2. b\n\n# c\n\n***");
    check_dump("(doc (ol 1 . tight (li (p)) (li (p))) (h 1 atx) (hr))");
    reset_doc();

    /* After an item ending with a closed fence. */
    PARSE_PUBLIC("- a\n- ```\n  b\n  ```\n\n");
    check_dump("(doc (ul - tight (li (p)) (li (code fenced ` 3))))");
}

NAOS_TEST(blank_lines_inside_code_and_html_do_not_count)
{
    /* Fenced code in an item, blank lines inside it. */
    PARSE_PUBLIC("- ```\n  a\n\n\n  b\n  ```\n- c");
    check_dump("(doc (ul - tight (li (code fenced ` 3)) (li (p))))");
    reset_doc();

    /* A blank line right after the opening fence, then a new item. */
    PARSE_PUBLIC("- ~~~\n\n  ~~~\n- c");
    check_dump("(doc (ul - tight (li (code fenced ~ 3)) (li (p))))");
    reset_doc();

    /* Indented code in an item with a blank line inside it. */
    PARSE_PUBLIC("-     a\n\n      b\n- c");
    check_dump("(doc (ul - tight (li (code indented)) (li (p))))");
    reset_doc();

    /* An HTML block (condition 2) with a blank line inside it. */
    PARSE_PUBLIC("- <!--\n\n  -->\n- c");
    check_dump("(doc (ul - tight (li (html)) (li (p))))");
    reset_doc();

    /* Fenced code in a nested item. */
    PARSE_PUBLIC("- a\n  - ```\n\n    ```\n- c");
    check_dump("(doc (ul - tight (li (p) (ul - tight (li (code fenced ` 3)))) "
               "(li (p))))");
    reset_doc();

    /* A blank line after the closing fence counts. */
    PARSE_PUBLIC("- ```\n  a\n  ```\n\n- c");
    check_dump("(doc (ul - loose (li (code fenced ` 3)) (li (p))))");
}

NAOS_TEST(items_that_start_with_a_blank_line_are_not_loose)
{
    /* The blank marker line is not a blank line between children. */
    PARSE_PUBLIC("-\n  a\n-\n  b");
    check_dump("(doc (ul - tight (li (p)) (li (p))))");
    reset_doc();
    PARSE_PUBLIC("1.  \n   a\n   b");
    check_dump("(doc (ol 1 . tight (li (p))))");
    reset_doc();

    /* Empty items next to each other. */
    PARSE_PUBLIC("-\n-\n-");
    check_dump("(doc (ul - tight (li) (li) (li)))");
    reset_doc();

    /* An empty item then a blank line before the next one. */
    PARSE_PUBLIC("-\n\n-");
    check_dump("(doc (ul - loose (li) (li)))");
}

NAOS_TEST(lists_in_block_quotes_are_tight_or_loose)
{
    /* A blank quote line between items. */
    PARSE_PUBLIC("> - a\n>\n> - b");
    check_dump("(doc (bq (ul - loose (li (p)) (li (p)))))");
    reset_doc();

    /* Between children of an item. */
    PARSE_PUBLIC("> - a\n>\n>   b");
    check_dump("(doc (bq (ul - loose (li (p) (p)))))");
    reset_doc();

    /* At the end of the quote. */
    PARSE_PUBLIC("> - a\n> - b\n>\n");
    check_dump("(doc (bq (ul - tight (li (p)) (li (p)))))");
    reset_doc();

    /* A blank line ends the quote: two quotes, two lists. */
    PARSE_PUBLIC("> - a\n\n> - b");
    check_dump("(doc (bq (ul - tight (li (p)))) (bq (ul - tight (li (p)))))");
    reset_doc();

    /* A quote in an item whose last line is blank inside the quote. */
    PARSE_PUBLIC("- > a\n  >\n- b");
    check_dump("(doc (ul - tight (li (bq (p))) (li (p))))");
    reset_doc();

    /* Nested quotes and lists. */
    PARSE_PUBLIC("> 1. > - a\n>    >\n>    > - b\n> 2. c");
    check_dump("(doc (bq (ol 1 . tight (li (bq (ul - loose (li (p)) "
               "(li (p))))) (li (p)))))");
}

/* ---------------------------------------------------------------------- */
/* Runs of blank lines (the blank line cutoff)                            */
/* ---------------------------------------------------------------------- */

#define TEXT_SIZE 512
#define WIDE_DUMP_SIZE 4096
#define RUN_COUNT 6

static char g_text[TEXT_SIZE];
static char g_want_dump[WIDE_DUMP_SIZE];
static char g_wide_dump[WIDE_DUMP_SIZE];

/*
 * The blank lines put between two parts of a text, without and with a block
 * quote marker. The first is one blank line, on which the cutoff never
 * happens (a list's mark is set on it and works from the next line); the
 * others are runs of two lines or more, some of spaces and tabs only, wider
 * or narrower than the content indents, on which it does.
 */
static const char g_plain_runs[RUN_COUNT][32] = {
    "\n",
    "\n\n",
    "\n\n\n",
    "\n  \n\t\n",
    "\n \t \n\n",
    "\n\n                  \n",
};
static const char g_quoted_runs[RUN_COUNT][32] = {
    ">\n",
    ">\n>\n",
    ">\n>\n>\n",
    ">\n>  \n>\t\n",
    ">\n> \t \n>\n",
    ">\n>\n>                  \n",
};

/*
 * Parses before, a line ending, each run of blank lines and after, and
 * checks that the block structure is the same for every run: want, or
 * with want NULL the structure with one blank line.
 */
static void check_runs_agree(const char *before, const char *after,
                             bool quoted, const char *want)
{
    size_t i;

    for (i = 0; i < RUN_COUNT; i++) {
        int length = snprintf(g_text, TEXT_SIZE, "%s\n%s%s", before,
                              quoted ? g_quoted_runs[i] : g_plain_runs[i],
                              after);

        NAOS_REQUIRE_TRUE(length > 0 && (size_t)length < TEXT_SIZE);
        parse_public_ok(g_text, (size_t)length);
        NAOS_REQUIRE_UINT_LT(
            amark_test_dump_blocks(g_doc, g_wide_dump, WIDE_DUMP_SIZE),
            WIDE_DUMP_SIZE);
        if (i == 0 && want == NULL) {
            memcpy(g_want_dump, g_wide_dump, WIDE_DUMP_SIZE);
        }
        NAOS_CHECK_STR_EQ(g_wide_dump, want != NULL ? want : g_want_dump);
        reset_doc();
    }
}

NAOS_TEST(runs_of_blank_lines_leave_lists_unchanged)
{
    /* Between items, between children of an item, after the list. */
    check_runs_agree("- a", "- b", false,
                     "(doc (ul - loose (li (p)) (li (p))))");
    check_runs_agree("- a", "  b", false, "(doc (ul - loose (li (p) (p))))");
    check_runs_agree("- a", "b", false, "(doc (ul - tight (li (p))) (p))");

    /* A nested list, continued at each depth or left. */
    check_runs_agree("- a\n  - b", "- c", false,
                     "(doc (ul - loose (li (p) (ul - tight (li (p)))) "
                     "(li (p))))");
    check_runs_agree("- a\n  - b", "  c", false,
                     "(doc (ul - loose (li (p) (ul - tight (li (p))) (p))))");
    check_runs_agree("- a\n  - b", "    c", false,
                     "(doc (ul - tight (li (p) (ul - loose (li (p) "
                     "(p))))))");
    check_runs_agree("1. a\n   - b\n     - c", "     d", false,
                     "(doc (ol 1 . tight (li (p) (ul - loose (li (p) "
                     "(ul - tight (li (p))) (p))))))");

    /* Empty items: the blank lines close them. */
    check_runs_agree("-", "  a", false, "(doc (ul - tight (li)) (p))");
    check_runs_agree("- a\n  - b\n  -", "  c", false,
                     "(doc (ul - loose (li (p) (ul - tight (li (p)) (li)) "
                     "(p))))");

    /* Code and HTML blocks in an item, and after them. */
    check_runs_agree("-     code", "- b", false,
                     "(doc (ul - loose (li (code indented)) (li (p))))");
    check_runs_agree("-     code", "      more", false,
                     "(doc (ul - tight (li (code indented))))");
    check_runs_agree("- ```\n  a", "  ```\n- b", false,
                     "(doc (ul - tight (li (code fenced ` 3)) (li (p))))");
    check_runs_agree("- <!--", "  -->\n- b", false,
                     "(doc (ul - tight (li (html)) (li (p))))");
    check_runs_agree("- <div>", "- b", false,
                     "(doc (ul - loose (li (html)) (li (p))))");
}

NAOS_TEST(runs_of_blank_lines_after_a_nested_list_at_each_depth)
{
    char after[32];
    size_t depth;

    /*
     * "- - - - - - - - a", the blank lines, then "b" indented 2 columns
     * per item it continues: the deeper items close.
     */
    for (depth = 0; depth <= 8; depth++) {
        memset(after, ' ', 2 * depth);
        memcpy(after + 2 * depth, "b", 2);
        check_runs_agree("- - - - - - - - a", after, false, NULL);
        check_runs_agree("- - - - - - - - ```", after, false, NULL);
    }
    check_runs_agree("- - - a", "b", false,
                     "(doc (ul - tight (li (ul - tight (li (ul - tight "
                     "(li (p))))))) (p))");
    check_runs_agree("- - - a", "    b", false,
                     "(doc (ul - tight (li (ul - loose (li (ul - tight "
                     "(li (p))) (p))))))");
    check_runs_agree("- - - a", "      b", false,
                     "(doc (ul - tight (li (ul - tight (li (ul - loose "
                     "(li (p) (p))))))))");
}

NAOS_TEST(runs_of_blank_lines_in_block_quotes)
{
    /* Blank quote lines in a list in a quote. */
    check_runs_agree("> - a", "> - b", true,
                     "(doc (bq (ul - loose (li (p)) (li (p)))))");
    check_runs_agree("> - a", ">   b", true,
                     "(doc (bq (ul - loose (li (p) (p)))))");
    check_runs_agree("> - a\n>   - b", ">     c", true,
                     "(doc (bq (ul - tight (li (p) (ul - loose (li (p) "
                     "(p)))))))");
    check_runs_agree("> - ```", ">   ```", true,
                     "(doc (bq (ul - tight (li (code fenced ` 3)))))");

    /* Blank lines end a quote, in a list or around one. */
    check_runs_agree("> - a", "> b", false,
                     "(doc (bq (ul - tight (li (p)))) (bq (p)))");
    check_runs_agree("- > a", "  b", false,
                     "(doc (ul - loose (li (bq (p)) (p))))");
    check_runs_agree("- > a", "- b", false,
                     "(doc (ul - loose (li (bq (p))) (li (p))))");
    check_runs_agree("- a\n  > - b", "  > c", false,
                     "(doc (ul - loose (li (p) (bq (ul - tight (li (p)))) "
                     "(bq (p)))))");
}

/*
 * The lines on which the cutoff happens get the records and the blocks the
 * ends that matching every element gives (the second blank line on).
 */
NAOS_TEST(blank_run_in_fenced_code_in_a_list_item)
{
    amark_node_id_t list;
    amark_node_id_t item;

    /*
     * "- ```\n" 0-5, "\n" 6, "\n" 7, 6 spaces 8-13 and "\n" 14, two tabs
     * 15-16 and "\n" 17, "\n" 18, "  ```\n" 19-24, "- c" 25-27.
     */
    PARSE_PUBLIC("- ```\n\n\n      \n\t\t\n\n  ```\n- c");
    check_dump("(doc (ul - tight (li (code fenced ` 3)) (li (p))))");
    list = root_child(0);
    item = nth_child(list, 0);
    check_node(list, AMARK_KIND_LIST, 0, 28, 1, 8);
    check_node(item, AMARK_KIND_LIST_ITEM, 0, 24, 1, 7);
    check_node(nth_child(item, 0), AMARK_KIND_CODE_BLOCK, 2, 24, 1, 7);
    check_record(1, 2, 0);
    check_record(2, 6, 0);
    check_record(3, 7, 0);
    /* The item takes 2 columns; the rest is the code's. */
    check_record(4, 10, 0);
    check_record(5, 15, 2);
    check_record(6, 18, 0);
    check_record(7, 21, 0);
    check_record(8, 27, 0);
}

NAOS_TEST(blank_run_in_indented_code_in_a_list_item)
{
    amark_node_id_t list;
    amark_node_id_t item;

    /*
     * "- a\n" 0-3, "\n" 4, "      b\n" 5-12, "\n" 13, "\n" 14, 8 spaces
     * 15-22 and "\n" 23, 3 spaces 24-26 and "\n" 27, "\t\t  c\n" 28-33,
     * "\n" 34, "\n" 35, "d" 36.
     */
    PARSE_PUBLIC("- a\n\n      b\n\n\n        \n   \n\t\t  c\n\n\nd");
    check_dump("(doc (ul - loose (li (p) (code indented))) (p))");
    list = root_child(0);
    item = nth_child(list, 0);
    check_node(list, AMARK_KIND_LIST, 0, 33, 1, 8);
    check_node(item, AMARK_KIND_LIST_ITEM, 0, 33, 1, 8);
    /* The code ends at its last line that is not blank. */
    check_node(nth_child(item, 1), AMARK_KIND_CODE_BLOCK, 11, 33, 3, 8);
    check_node(root_child(1), AMARK_KIND_PARAGRAPH, 36, 37, 11, 11);
    check_record(3, 11, 0);
    check_record(4, 13, 0);
    check_record(5, 14, 0);
    /* 2 columns for the item, 4 for the code. */
    check_record(6, 21, 0);
    /* Fewer than 4 columns after the item: all of the white space. */
    check_record(7, 27, 0);
    check_record(8, 29, 2);
    check_record(9, 34, 0);
    check_record(10, 35, 0);
}

NAOS_TEST(blank_run_in_an_html_block_in_a_list_item)
{
    amark_node_id_t list;
    amark_node_id_t item;

    /*
     * "- <!--\n" 0-6, "\n" 7, "\n" 8, 4 spaces 9-12 and "\n" 13, "\t\n"
     * 14-15, "  -->\n" 16-21, "- c" 22-24.
     */
    PARSE_PUBLIC("- <!--\n\n\n    \n\t\n  -->\n- c");
    check_dump("(doc (ul - tight (li (html)) (li (p))))");
    list = root_child(0);
    item = nth_child(list, 0);
    check_node(list, AMARK_KIND_LIST, 0, 25, 1, 7);
    check_node(item, AMARK_KIND_LIST_ITEM, 0, 21, 1, 6);
    check_node(nth_child(item, 0), AMARK_KIND_HTML_BLOCK, 2, 21, 1, 6);
    check_record(1, 2, 0);
    check_record(2, 7, 0);
    check_record(3, 8, 0);
    /* The item takes 2 columns; the HTML keeps the rest. */
    check_record(4, 11, 0);
    check_record(5, 14, 2);
    check_record(6, 18, 0);
}

NAOS_TEST(blank_run_in_fenced_code_in_nested_items_and_a_quote)
{
    amark_node_id_t node;

    /*
     * The items take 3 and 2 columns: the second tab of a line of two keeps
     * 3. "1. a\n" 0-4, "   - ```\n" 5-13, "\n" 14, "\n" 15, "\t\t\n"
     * 16-18, 9 spaces 19-27 and "x\n" 28-29, 5 spaces 30-34 and "```"
     * 35-37.
     */
    PARSE_PUBLIC("1. a\n   - ```\n\n\n\t\t\n         x\n     ```");
    check_dump("(doc (ol 1 . tight (li (p) (ul - tight (li "
               "(code fenced ` 3))))))");
    node = nth_child(nth_child(root_child(0), 0), 1);
    check_node(node, AMARK_KIND_LIST, 8, 38, 2, 7);
    node = nth_child(nth_child(node, 0), 0);
    check_node(node, AMARK_KIND_CODE_BLOCK, 10, 38, 2, 7);
    check_record(2, 10, 0);
    check_record(3, 14, 0);
    check_record(4, 15, 0);
    check_record(5, 17, 3);
    check_record(6, 24, 0);
    check_record(7, 35, 0);
    reset_doc();

    /*
     * In a quote: "> - ```\n" 0-7, ">\n" 8-9, ">\n" 10-11, ">" and 5
     * spaces 12-17 and "\n" 18, ">   ```" 19-25.
     */
    PARSE_PUBLIC("> - ```\n>\n>\n>     \n>   ```");
    check_dump("(doc (bq (ul - tight (li (code fenced ` 3)))))");
    check_node(root_child(0), AMARK_KIND_BLOCK_QUOTE, 0, 26, 1, 5);
    node = nth_child(nth_child(root_child(0), 0), 0);
    check_node(nth_child(node, 0), AMARK_KIND_CODE_BLOCK, 4, 26, 1, 5);
    check_record(1, 4, 0);
    check_record(2, 9, 0);
    check_record(3, 11, 0);
    check_record(4, 16, 0);
    check_record(5, 23, 0);
}

/*
 * An item with nothing in it goes on over a line of white space as wide as
 * its content indent, so it may be left open by the line before a blank
 * line: it closes on the next blank line narrower than that, even when the
 * blank line marks of its lists are set.
 */
NAOS_TEST(blank_run_with_an_empty_item_continued_by_white_space)
{
    amark_node_id_t list;

    /* "-\n" 0-1, "  \n" 2-4, "\n" 5, "  a" 6-8. */
    PARSE_PUBLIC("-\n  \n\n  a");
    check_dump("(doc (ul - tight (li)) (p))");
    check_node(root_child(0), AMARK_KIND_LIST, 0, 1, 1, 1);
    check_node(root_child(1), AMARK_KIND_PARAGRAPH, 8, 9, 4, 4);
    check_record(2, 4, 0);
    check_record(3, 5, 0);
    reset_doc();

    /* The second line of white space continues it, the empty one not. */
    PARSE_PUBLIC("-\n  \n  \n\n  a");
    check_dump("(doc (ul - tight (li)) (p))");
    check_node(root_child(1), AMARK_KIND_PARAGRAPH, 11, 12, 5, 5);
    check_record(3, 7, 0);
    check_record(4, 8, 0);
    reset_doc();

    /* In an item: the inner item closes, the outer one goes on. */
    PARSE_PUBLIC("- -\n    \n  \n\n  a");
    check_dump("(doc (ul - loose (li (ul - tight (li)) (p))))");
    list = root_child(0);
    check_node(list, AMARK_KIND_LIST, 0, 16, 1, 5);
    check_node(nth_child(nth_child(list, 0), 0), AMARK_KIND_LIST, 2, 3, 1,
               1);
    check_node(nth_child(nth_child(list, 0), 1), AMARK_KIND_PARAGRAPH, 15,
               16, 5, 5);
    reset_doc();
    PARSE_PUBLIC("- 1.\n     \n     \n\n     a");
    check_dump("(doc (ul - loose (li (ol 1 . tight (li)) (p))))");
    reset_doc();

    /* 5 columns, the sum of the content indents: the item goes on. */
    PARSE_PUBLIC("- 1.\n     \n\t \n     a");
    check_dump("(doc (ul - tight (li (ol 1 . tight (li (p))))))");
}

#define BLANK_RUN 50000

/* DEEP markers "- ", "a" or a fence, BLANK_RUN blank lines, then "b". */
static char g_blank_run[2 * DEEP + 4 + 4 * BLANK_RUN + 1];

/* The content end of each of the BLANK_RUN lines. */
static size_t g_run_end[BLANK_RUN];

/*
 * Writes DEEP nested list items, first (at most 3 bytes) in the innermost
 * one, BLANK_RUN lines that are empty or hold spaces and tabs, and the line
 * "b" into g_blank_run, and returns its length.
 */
static size_t build_blank_run(const char *first)
{
    static const char spaces[4][4] = {"", "  ", "\t", " \t "};
    size_t length = 0;
    size_t i;

    for (i = 0; i < DEEP; i++) {
        g_blank_run[length++] = '-';
        g_blank_run[length++] = ' ';
    }
    memcpy(g_blank_run + length, first, strlen(first));
    length += strlen(first);
    g_blank_run[length++] = '\n';
    for (i = 0; i < BLANK_RUN; i++) {
        size_t width = strlen(spaces[i % 4]);

        memcpy(g_blank_run + length, spaces[i % 4], width);
        length += width;
        g_run_end[i] = length;
        g_blank_run[length++] = '\n';
    }
    g_blank_run[length++] = 'b';
    return length;
}

/*
 * Checks the 2 DEEP nested lists and items of g_doc: each starts at its
 * marker and ends at end, on line end_line, and all but the outermost list
 * are only children.
 */
static void check_deep_lists(size_t end, size_t end_line)
{
    amark_node_id_t node = amark_document_root(g_doc);
    size_t i;

    for (i = 0; i < 2 * DEEP; i++) {
        amark_range_t range;

        node = amark_node_first_child(g_doc, node);
        NAOS_REQUIRE_UINT_EQ(amark_node_kind(g_doc, node),
                             i % 2 == 0 ? AMARK_KIND_LIST
                                        : AMARK_KIND_LIST_ITEM);
        NAOS_REQUIRE_TRUE(amark_node_range(g_doc, node, &range));
        NAOS_REQUIRE_UINT_EQ(range.start, i - i % 2);
        NAOS_REQUIRE_UINT_EQ(range.end, end);
        NAOS_REQUIRE_UINT_EQ(amark_node_end_line(g_doc, node), end_line);
        if (i > 0) {
            NAOS_REQUIRE_UINT_EQ(amark_node_next_sibling(g_doc, node),
                                 AMARK_NODE_NONE);
        }
    }
}

/*
 * A run of blank lines after a deep list: matching every element on each
 * line would take DEEP times BLANK_RUN steps; the cutoff makes it linear.
 * The tree and the records are those of matching every element.
 */
NAOS_TEST(blank_run_after_a_deeply_nested_list)
{
    size_t length = build_blank_run("a");
    amark_node_id_t node;
    size_t i;

    parse_public_ok(g_blank_run, length);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 2 * DEEP + 3);
    check_deep_lists(2 * DEEP + 1, 1);
    node = amark_document_root(g_doc);
    for (i = 0; i < 2 * DEEP; i++) {
        node = amark_node_first_child(g_doc, node);
    }
    check_node(amark_node_first_child(g_doc, node), AMARK_KIND_PARAGRAPH,
               2 * DEEP, 2 * DEEP + 1, 1, 1);
    check_node(root_child(1), AMARK_KIND_PARAGRAPH, length - 1, length,
               BLANK_RUN + 2, BLANK_RUN + 2);
    check_record(1, 2 * DEEP, 0);
    for (i = 0; i < BLANK_RUN; i++) {
        amark_line_record_t record;

        NAOS_REQUIRE_TRUE(amark_line_get(g_doc, i + 2, &record));
        NAOS_REQUIRE_UINT_EQ(record.content_start, g_run_end[i]);
        NAOS_REQUIRE_UINT_EQ(record.pad, 0);
    }
}

NAOS_TEST(blank_run_in_fenced_code_in_a_deeply_nested_list)
{
    size_t length = build_blank_run("```");
    size_t end = g_run_end[BLANK_RUN - 1];
    amark_node_id_t node;
    size_t i;

    /* The fence is not closed: the code ends with the last blank line. */
    parse_public_ok(g_blank_run, length);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 2 * DEEP + 3);
    check_deep_lists(end, BLANK_RUN + 1);
    node = amark_document_root(g_doc);
    for (i = 0; i < 2 * DEEP; i++) {
        node = amark_node_first_child(g_doc, node);
    }
    check_node(amark_node_first_child(g_doc, node), AMARK_KIND_CODE_BLOCK,
               2 * DEEP, end, 1, BLANK_RUN + 1);
    check_node(root_child(1), AMARK_KIND_PARAGRAPH, length - 1, length,
               BLANK_RUN + 2, BLANK_RUN + 2);
    check_record(1, 2 * DEEP, 0);
    /* Fewer columns than the items take: all of the white space. */
    for (i = 0; i < BLANK_RUN; i++) {
        amark_line_record_t record;

        NAOS_REQUIRE_TRUE(amark_line_get(g_doc, i + 2, &record));
        NAOS_REQUIRE_UINT_EQ(record.content_start, g_run_end[i]);
        NAOS_REQUIRE_UINT_EQ(record.pad, 0);
    }
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

NAOS_SUITE(block_leaves,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(atx_heading_of_one_line),
    NAOS_ADD_TEST(atx_heading_levels_one_to_six),
    NAOS_ADD_TEST(atx_heading_closing_sequence_and_trailing_spaces),
    NAOS_ADD_TEST(empty_atx_headings),
    NAOS_ADD_TEST(not_atx_headings),
    NAOS_ADD_TEST(atx_heading_indentation),
    NAOS_ADD_TEST(atx_heading_interrupts_a_paragraph),
    NAOS_ADD_TEST(atx_headings_with_each_line_ending),
    NAOS_ADD_TEST(thematic_breaks_with_each_line_ending),
    NAOS_ADD_TEST(thematic_break_interrupts_a_paragraph),
    NAOS_ADD_TEST(not_thematic_breaks),
    NAOS_ADD_TEST(heading_and_break_in_a_range_starting_inside_a_line),
    NAOS_ADD_TEST(node_count_limit_counts_headings_and_breaks),
    NAOS_ADD_TEST(allocation_failure_with_headings_and_breaks),
    NAOS_ADD_TEST(indented_code_block_of_one_line),
    NAOS_ADD_TEST(indented_code_keeps_inner_blank_lines_only),
    NAOS_ADD_TEST(indented_code_blank_line_of_four_columns_or_more),
    NAOS_ADD_TEST(indented_code_does_not_start_on_a_blank_line),
    NAOS_ADD_TEST(indented_code_does_not_interrupt_a_paragraph),
    NAOS_ADD_TEST(indented_code_after_a_heading_and_before_a_paragraph),
    NAOS_ADD_TEST(indented_code_with_tabs_and_line_endings),
    NAOS_ADD_TEST(fence_indented_four_columns_is_indented_code),
    NAOS_ADD_TEST(fenced_code_block),
    NAOS_ADD_TEST(fenced_code_block_info_and_tildes),
    NAOS_ADD_TEST(fenced_code_block_with_each_line_ending),
    NAOS_ADD_TEST(lines_that_do_not_close_a_fence),
    NAOS_ADD_TEST(unclosed_fence_closes_at_the_end_of_the_range),
    NAOS_ADD_TEST(fence_indentation_is_removed_from_code_lines),
    NAOS_ADD_TEST(fenced_code_interrupts_a_paragraph_and_holds_raw_lines),
    NAOS_ADD_TEST(backtick_in_the_info_of_a_backtick_fence_is_not_a_fence),
    NAOS_ADD_TEST(fenced_code_in_a_range_starting_inside_a_line),
    NAOS_ADD_TEST(node_count_limit_counts_code_blocks),
    NAOS_ADD_TEST(allocation_failure_with_code_blocks),
    NAOS_ADD_TEST(html_block_conditions_one_to_five_close_on_the_end_line),
    NAOS_ADD_TEST(html_block_closes_on_its_start_line),
    NAOS_ADD_TEST(html_block_conditions_six_and_seven_end_before_a_blank_line),
    NAOS_ADD_TEST(html_block_condition_seven_does_not_interrupt_a_paragraph),
    NAOS_ADD_TEST(html_block_lines_are_raw),
    NAOS_ADD_TEST(html_block_indentation),
    NAOS_ADD_TEST(html_block_with_each_line_ending),
    NAOS_ADD_TEST(unclosed_html_block_closes_at_the_end_of_the_range),
    NAOS_ADD_TEST(html_block_in_a_range_starting_inside_a_line),
    NAOS_ADD_TEST(node_count_limit_counts_html_blocks)
)

NAOS_SUITE(block_definitions,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(paragraph_of_definitions_only_gives_no_paragraph),
    NAOS_ADD_TEST(definition_followed_by_paragraph_content),
    NAOS_ADD_TEST(definition_spanning_lines_with_title_on_the_next_line),
    NAOS_ADD_TEST(invalid_titles),
    NAOS_ADD_TEST(definitions_do_not_interrupt_a_paragraph),
    NAOS_ADD_TEST(duplicate_labels_find_the_first_definition),
    NAOS_ADD_TEST(definitions_in_separate_paragraphs_share_one_table),
    NAOS_ADD_TEST(setext_headings_of_both_levels),
    NAOS_ADD_TEST(setext_heading_of_several_lines),
    NAOS_ADD_TEST(setext_underline_with_each_line_ending),
    NAOS_ADD_TEST(lines_that_are_not_setext_underlines),
    NAOS_ADD_TEST(setext_underline_takes_precedence_over_a_thematic_break),
    NAOS_ADD_TEST(underline_after_definitions_only),
    NAOS_ADD_TEST(setext_heading_after_definitions),
    NAOS_ADD_TEST(inline_contents_in_document_order),
    NAOS_ADD_TEST(definitions_and_setext_in_a_range_starting_inside_a_line),
    NAOS_ADD_TEST(node_count_limit_counts_definitions),
    NAOS_ADD_TEST(many_definitions),
    NAOS_ADD_TEST(definitions_and_setext_through_amark_parse),
    NAOS_ADD_TEST(allocation_failure_with_definitions_and_setext)
)

NAOS_SUITE(block_quotes,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(block_quote_of_one_line_and_its_marker_forms),
    NAOS_ADD_TEST(block_quote_over_several_lines),
    NAOS_ADD_TEST(marker_only_line_ends_the_paragraph),
    NAOS_ADD_TEST(blank_line_ends_a_block_quote),
    NAOS_ADD_TEST(nested_block_quotes),
    NAOS_ADD_TEST(lazy_continuation_lines),
    NAOS_ADD_TEST(lazy_line_is_not_a_setext_underline),
    NAOS_ADD_TEST(lines_that_start_blocks_are_not_lazy),
    NAOS_ADD_TEST(unmatched_block_quote_closes_its_leaf),
    NAOS_ADD_TEST(tab_after_the_marker),
    NAOS_ADD_TEST(html_line_with_a_prefix_ending_inside_a_tab),
    NAOS_ADD_TEST(definitions_in_a_block_quote),
    NAOS_ADD_TEST(block_quote_with_crlf_in_a_range_starting_inside_a_line),
    NAOS_ADD_TEST(nesting_depth_limit_with_block_quotes),
    NAOS_ADD_TEST(node_count_limit_counts_block_quotes),
    NAOS_ADD_TEST(deeply_nested_block_quotes),
    NAOS_ADD_TEST(allocation_failure_with_block_quotes)
)

NAOS_SUITE(block_lists,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(list_item_of_one_line_and_its_attributes),
    NAOS_ADD_TEST(list_items_over_several_lines),
    NAOS_ADD_TEST(a_different_marker_starts_a_new_list),
    NAOS_ADD_TEST(content_indent_of_list_items),
    NAOS_ADD_TEST(items_that_start_with_a_blank_line),
    NAOS_ADD_TEST(blank_lines_inside_list_items),
    NAOS_ADD_TEST(nested_lists),
    NAOS_ADD_TEST(list_items_and_paragraphs),
    NAOS_ADD_TEST(list_items_and_thematic_breaks_and_setext_headings),
    NAOS_ADD_TEST(lists_in_block_quotes),
    NAOS_ADD_TEST(list_with_crlf_in_a_range_starting_inside_a_line),
    NAOS_ADD_TEST(limits_with_lists),
    NAOS_ADD_TEST(deeply_nested_lists),
    NAOS_ADD_TEST(allocation_failure_with_lists)
)

NAOS_SUITE(block_list_spacing,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(spec_list_examples_are_tight_or_loose),
    NAOS_ADD_TEST(blank_line_between_items_makes_the_list_loose),
    NAOS_ADD_TEST(blank_line_between_children_of_an_item_makes_the_list_loose),
    NAOS_ADD_TEST(definitions_count_as_children_of_an_item),
    NAOS_ADD_TEST(blank_line_in_a_nested_list_leaves_the_outer_list_tight),
    NAOS_ADD_TEST(blank_line_after_the_last_item_leaves_the_list_tight),
    NAOS_ADD_TEST(blank_lines_inside_code_and_html_do_not_count),
    NAOS_ADD_TEST(items_that_start_with_a_blank_line_are_not_loose),
    NAOS_ADD_TEST(lists_in_block_quotes_are_tight_or_loose)
)

NAOS_SUITE(block_blank_runs,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(runs_of_blank_lines_leave_lists_unchanged),
    NAOS_ADD_TEST(runs_of_blank_lines_after_a_nested_list_at_each_depth),
    NAOS_ADD_TEST(runs_of_blank_lines_in_block_quotes),
    NAOS_ADD_TEST(blank_run_in_fenced_code_in_a_list_item),
    NAOS_ADD_TEST(blank_run_in_indented_code_in_a_list_item),
    NAOS_ADD_TEST(blank_run_in_an_html_block_in_a_list_item),
    NAOS_ADD_TEST(blank_run_in_fenced_code_in_nested_items_and_a_quote),
    NAOS_ADD_TEST(blank_run_with_an_empty_item_continued_by_white_space),
    NAOS_ADD_TEST(blank_run_after_a_deeply_nested_list),
    NAOS_ADD_TEST(blank_run_in_fenced_code_in_a_deeply_nested_list)
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
    NAOS_ADD_SUITE(block_leaves),
    NAOS_ADD_SUITE(block_definitions),
    NAOS_ADD_SUITE(block_quotes),
    NAOS_ADD_SUITE(block_lists),
    NAOS_ADD_SUITE(block_list_spacing),
    NAOS_ADD_SUITE(block_blank_runs),
    NAOS_ADD_SUITE(block_ranges),
    NAOS_ADD_SUITE(block_limits)
)
