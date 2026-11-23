/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of the block parser (src/block.h), calling amark_block_parse
 * directly on a document made with amark_document_create and
 * amark_parse_data_create: paragraphs and blank lines, thematic breaks and
 * ATX headings, the ranges and lines of their nodes, heading attributes,
 * their inline contents, the line records, ranges that start past offset 0,
 * the line position (the cached search for the first non-space byte and the
 * two advances), the node count and nesting depth limits, and allocation
 * failures. Every parsed document goes through amark_tree_check.
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
    NAOS_ADD_SUITE(block_ranges),
    NAOS_ADD_SUITE(block_limits)
)
