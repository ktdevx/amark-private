/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of the inline parser (src/inline.h). Each test runs the block
 * parser and then amark_inline_parse right after it, as amark_parse will.
 *
 * Covered here: the ranges of texts (one line, never the white space before
 * a line ending), hard and soft line breaks with LF, CR and CR LF, no break
 * at the end of a content, ranges that leave out the container prefixes and
 * the leading white space of a line (block quotes, list items, lazy
 * continuation lines, paragraph continuation lines), every byte of the
 * content of the lines in one direct child of the paragraph or heading, no
 * children for the other blocks, the empty ATX heading, a parsed range that
 * does not start at 0, special bytes that start no structure as text, bytes
 * outside ASCII and U+0000 in texts, the depth and node count limits counted
 * from the paragraph's depth, and the tree check on every tree.
 */

#include "arena.h"
#include "block.h"
#include "config_internal.h"
#include "cursor.h"
#include "document.h"
#include "inline.h"
#include "parse_data.h"
#include "test_alloc.h"
#include "tree_check.h"
#include "tree_dump.h"

#include <amark/amark.h>

#include <naos/naos.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * Fixture. NAOS_REQUIRE_* leaves a test with longjmp, so the document and the
 * scratch arena are kept in static variables, and teardown releases them and
 * checks that no block is left.
 */
#define BUFFER_SIZE 1024
#define DUMP_SIZE 2048

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
    amark_arena_deinit(&g_scratch);
    amark_document_destroy(g_doc);
    g_doc = NULL;
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
    NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
}

/* The settings of one parse. */
typedef struct parse_options {
    size_t start;
    size_t end; /* AMARK_END_OF_CONTENT: to the end */
    size_t max_depth;
    size_t max_nodes;
} parse_options_t;

static parse_options_t default_options(void)
{
    parse_options_t options;

    options.start = 0;
    options.end = AMARK_END_OF_CONTENT;
    options.max_depth = 0;
    options.max_nodes = 0;
    return options;
}

/*
 * Copies length bytes to g_buffer, creates g_doc over them with parse data,
 * runs the block parser (ill-formed UTF-8 replaced) and requires it to
 * succeed, then releases the scratch arena, initializes it again and runs
 * the inline parser, as amark_parse does. Returns what the inline parser
 * returns.
 */
static amark_error_t parse_with(const char *bytes, size_t length,
                                const parse_options_t *options)
{
    amark_config_t config;
    amark_resolved_config_t resolved;

    NAOS_REQUIRE_UINT_LE(length, BUFFER_SIZE);
    memcpy(g_buffer, bytes, length);
    g_length = length;
    amark_config_init(&config);
    config.allocator = g_allocator;
    config.utf8_mode = AMARK_UTF8_REPLACE;
    config.start = options->start;
    config.end = options->end;
    config.max_nesting_depth = options->max_depth;
    config.max_node_count = options->max_nodes;
    NAOS_REQUIRE_INT_EQ(amark_config_resolve(&config, g_length, &resolved),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_document_create(&resolved.allocator, g_buffer,
                                              g_length, resolved.start,
                                              resolved.end, NULL, &g_doc),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_parse_data_create(g_doc), AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_block_parse(g_doc, &resolved, &g_scratch),
                        AMARK_ERROR_NONE);
    amark_arena_deinit(&g_scratch);
    amark_arena_init(&g_scratch, &g_allocator);
    return amark_inline_parse(g_doc, &resolved, &g_scratch);
}

/* Parses the whole of length bytes without limits, requiring success. */
static void parse_bytes(const char *bytes, size_t length)
{
    parse_options_t options = default_options();

    NAOS_REQUIRE_INT_EQ(parse_with(bytes, length, &options),
                        AMARK_ERROR_NONE);
}

#define PARSE(s) parse_bytes((s), sizeof(s) - 1)

/* The whole tree of g_doc as one line. */
static const char *dump(void)
{
    size_t length = amark_test_dump_tree(g_doc, g_dump, sizeof(g_dump));

    NAOS_REQUIRE_UINT_LT(length, sizeof(g_dump));
    return g_dump;
}

static bool tree_ok(void)
{
    return amark_tree_check(g_doc).violation == AMARK_TREE_VIOLATION_NONE;
}

static amark_range_t node_range(amark_node_id_t node)
{
    amark_range_t range;

    NAOS_REQUIRE_TRUE(amark_node_range(g_doc, node, &range));
    return range;
}

/* The paragraph or heading of the inline content at index. */
static amark_node_id_t block_at(size_t index)
{
    amark_inline_content_t content;

    NAOS_REQUIRE_TRUE(amark_inline_content_at(g_doc, index, &content));
    return content.node;
}

/* The child at index (from 0) of node; it must exist. */
static amark_node_id_t child_at(amark_node_id_t node, size_t index)
{
    amark_node_id_t child = amark_node_first_child(g_doc, node);

    while (index > 0) {
        NAOS_REQUIRE_UINT_NE(child, AMARK_NODE_NONE);
        child = amark_node_next_sibling(g_doc, child);
        index--;
    }
    NAOS_REQUIRE_UINT_NE(child, AMARK_NODE_NONE);
    return child;
}

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

/*
 * Checks the child at index of the block of content 0: its kind, range and
 * start line.
 */
static void check_child(size_t index, amark_kind_t kind, size_t start,
                        size_t end, size_t line)
{
    amark_node_id_t child = child_at(block_at(0), index);
    amark_range_t range = node_range(child);

    NAOS_CHECK_UINT_EQ(amark_node_kind(g_doc, child), kind);
    NAOS_CHECK_UINT_EQ(range.start, start);
    NAOS_CHECK_UINT_EQ(range.end, end);
    NAOS_CHECK_UINT_EQ(amark_node_start_line(g_doc, child), line);
}

/* The number of direct children of node whose range holds the byte pos. */
static size_t children_holding(amark_node_id_t node, size_t pos)
{
    amark_node_id_t child = amark_node_first_child(g_doc, node);
    amark_range_t range;
    size_t count = 0;

    while (child != AMARK_NODE_NONE) {
        range = node_range(child);
        if (range.start <= pos && pos < range.end) {
            count++;
        }
        child = amark_node_next_sibling(g_doc, child);
    }
    return count;
}

/*
 * Checks, for every inline content (walked with a cursor), that every byte
 * of the content of its lines and of the line endings inside it is in
 * exactly one direct child of its paragraph or heading, and that no child
 * holds a byte of the prefix or the leading white space of a line (the
 * bytes from right after a line ending to the start of the next line's
 * content).
 */
static void check_bytes_covered(void)
{
    size_t index;
    size_t pos;
    size_t from;
    amark_inline_content_t content;
    amark_cursor_t cursor;

    for (index = 0; amark_inline_content_at(g_doc, index, &content);
         index++) {
        amark_cursor_init(&cursor, g_doc, content.start, content.end);
        for (;;) {
            if (amark_cursor_at_line_end(&cursor)) {
                if (amark_cursor_at_end(&cursor)) {
                    break;
                }
                from = amark_cursor_line_break_end(&cursor);
                for (pos = cursor.pos; pos < from; pos++) {
                    NAOS_CHECK_UINT_EQ(children_holding(content.node, pos),
                                       1);
                }
                amark_cursor_next_line(&cursor);
                for (pos = from; pos < cursor.pos; pos++) {
                    NAOS_CHECK_UINT_EQ(children_holding(content.node, pos),
                                       0);
                }
                continue;
            }
            NAOS_CHECK_UINT_EQ(children_holding(content.node, cursor.pos), 1);
            amark_cursor_seek(&cursor, cursor.pos + 1);
        }
    }
}

/* ---- Texts ---- */

NAOS_TEST(one_line_is_one_text)
{
    PARSE("foo bar");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (text \"foo bar\")))");
    NAOS_CHECK_UINT_EQ(child_count(block_at(0)), 1);
    check_child(0, AMARK_KIND_TEXT, 0, 7, 1);
    NAOS_CHECK_TRUE(amark_node_is_inline(g_doc, child_at(block_at(0), 0)));
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
}

NAOS_TEST(special_bytes_that_start_nothing_are_text)
{
    /* Every special byte, placed where it can start no structure. */
    PARSE("a * b _ c [ d ] e ! f < g & h ` i \\ j");
    NAOS_CHECK_STR_EQ(dump(),
                      "(doc (p (text \"a * b _ c [ d ] e ! f < g & h ` i "
                      "\\\\ j\")))");
    check_child(0, AMARK_KIND_TEXT, 0, 37, 1);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
}

NAOS_TEST(special_bytes_at_the_ends_of_a_line)
{
    /* A special byte right before a line ending and right at the start of
     * the next line's content. */
    PARSE("a *\n! b");
    NAOS_CHECK_STR_EQ(dump(),
                      "(doc (p (text \"a *\") (sbr) (text \"! b\")))");
    check_child(0, AMARK_KIND_TEXT, 0, 3, 1);
    check_child(1, AMARK_KIND_SOFT_BREAK, 3, 4, 1);
    check_child(2, AMARK_KIND_TEXT, 4, 7, 2);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
}

NAOS_TEST(bytes_outside_ascii_and_nul_are_text)
{
    /* U+00E9, U+0000, an ill-formed byte, U+3042: all inside one text. */
    static const char input[] = "a\xC3\xA9\x00\xFF\xE3\x81\x82z";

    parse_bytes(input, sizeof(input) - 1);
    NAOS_CHECK_UINT_EQ(child_count(block_at(0)), 1);
    check_child(0, AMARK_KIND_TEXT, 0, sizeof(input) - 1, 1);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
}

NAOS_TEST(white_space_before_a_special_byte_stays_in_the_text)
{
    /* Only the white space right before the line ending is left out. */
    PARSE("a  *  \nb");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (text \"a  *\") (br) (text \"b\")))");
    check_child(0, AMARK_KIND_TEXT, 0, 4, 1);
    check_child(1, AMARK_KIND_HARD_BREAK, 4, 7, 1);
    check_child(2, AMARK_KIND_TEXT, 7, 8, 2);
    NAOS_CHECK_UINT_EQ(child_count(block_at(0)), 3);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
}

/* ---- Line breaks ---- */

NAOS_TEST(soft_break_with_lf_cr_and_crlf)
{
    PARSE("foo\nbar");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (text \"foo\") (sbr) (text \"bar\")))");
    check_child(0, AMARK_KIND_TEXT, 0, 3, 1);
    check_child(1, AMARK_KIND_SOFT_BREAK, 3, 4, 1);
    check_child(2, AMARK_KIND_TEXT, 4, 7, 2);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
    amark_document_destroy(g_doc);
    g_doc = NULL;

    PARSE("foo\rbar");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (text \"foo\") (sbr) (text \"bar\")))");
    check_child(1, AMARK_KIND_SOFT_BREAK, 3, 4, 1);
    check_child(2, AMARK_KIND_TEXT, 4, 7, 2);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
    amark_document_destroy(g_doc);
    g_doc = NULL;

    PARSE("foo\r\nbar");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (text \"foo\") (sbr) (text \"bar\")))");
    check_child(1, AMARK_KIND_SOFT_BREAK, 3, 5, 1);
    check_child(2, AMARK_KIND_TEXT, 5, 8, 2);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
}

NAOS_TEST(one_space_before_the_line_ending_is_a_soft_break)
{
    PARSE("foo \nbar");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (text \"foo\") (sbr) (text \"bar\")))");
    check_child(0, AMARK_KIND_TEXT, 0, 3, 1);
    check_child(1, AMARK_KIND_SOFT_BREAK, 3, 5, 1);
    check_child(2, AMARK_KIND_TEXT, 5, 8, 2);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
}

NAOS_TEST(two_or_more_spaces_make_a_hard_break)
{
    PARSE("foo  \nbar");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (text \"foo\") (br) (text \"bar\")))");
    check_child(0, AMARK_KIND_TEXT, 0, 3, 1);
    check_child(1, AMARK_KIND_HARD_BREAK, 3, 6, 1);
    check_child(2, AMARK_KIND_TEXT, 6, 9, 2);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
    amark_document_destroy(g_doc);
    g_doc = NULL;

    /* Many spaces and CR LF. */
    PARSE("foo     \r\nbar");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (text \"foo\") (br) (text \"bar\")))");
    check_child(1, AMARK_KIND_HARD_BREAK, 3, 10, 1);
    check_child(2, AMARK_KIND_TEXT, 10, 13, 2);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
    amark_document_destroy(g_doc);
    g_doc = NULL;

    /* CR. */
    PARSE("foo  \rbar");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (text \"foo\") (br) (text \"bar\")))");
    check_child(1, AMARK_KIND_HARD_BREAK, 3, 6, 1);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
}

NAOS_TEST(tabs_before_the_line_ending)
{
    /* A tab is white space left out of the text but not a space of a hard
     * break: what counts is the spaces right before the line ending. */
    PARSE("foo\t\nbar");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (text \"foo\") (sbr) (text \"bar\")))");
    check_child(1, AMARK_KIND_SOFT_BREAK, 3, 5, 1);
    NAOS_CHECK_TRUE(tree_ok());
    amark_document_destroy(g_doc);
    g_doc = NULL;

    PARSE("foo  \t\nbar");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (text \"foo\") (sbr) (text \"bar\")))");
    check_child(1, AMARK_KIND_SOFT_BREAK, 3, 7, 1);
    NAOS_CHECK_TRUE(tree_ok());
    amark_document_destroy(g_doc);
    g_doc = NULL;

    PARSE("foo\t  \nbar");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (text \"foo\") (br) (text \"bar\")))");
    check_child(1, AMARK_KIND_HARD_BREAK, 3, 7, 1);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
}

NAOS_TEST(no_break_at_the_end_of_a_content)
{
    /* The content of the paragraph leaves out the white space at its end;
     * a backslash at the end stays text. */
    PARSE("foo  \n");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (text \"foo\")))");
    check_child(0, AMARK_KIND_TEXT, 0, 3, 1);
    NAOS_CHECK_UINT_EQ(child_count(block_at(0)), 1);
    NAOS_CHECK_TRUE(tree_ok());
    amark_document_destroy(g_doc);
    g_doc = NULL;

    PARSE("foo\\\n\nbar\\");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (text \"foo\\\\\")) (p (text "
                              "\"bar\\\\\")))");
    NAOS_CHECK_UINT_EQ(child_count(block_at(0)), 1);
    NAOS_CHECK_UINT_EQ(child_count(block_at(1)), 1);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
}

NAOS_TEST(several_lines_alternate_texts_and_breaks)
{
    PARSE("a\nb  \nc\r\nd");
    NAOS_CHECK_STR_EQ(dump(),
                      "(doc (p (text \"a\") (sbr) (text \"b\") (br) "
                      "(text \"c\") (sbr) (text \"d\")))");
    check_child(3, AMARK_KIND_HARD_BREAK, 3, 6, 2);
    check_child(4, AMARK_KIND_TEXT, 6, 7, 3);
    check_child(5, AMARK_KIND_SOFT_BREAK, 7, 9, 3);
    check_child(6, AMARK_KIND_TEXT, 9, 10, 4);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
}

/* ---- Prefixes and leading white space ---- */

NAOS_TEST(leading_white_space_of_a_continuation_line_is_left_out)
{
    PARSE("foo\n   bar");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (text \"foo\") (sbr) (text \"bar\")))");
    check_child(1, AMARK_KIND_SOFT_BREAK, 3, 4, 1);
    check_child(2, AMARK_KIND_TEXT, 7, 10, 2);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
}

NAOS_TEST(block_quote_prefixes_are_left_out)
{
    PARSE("> foo  \n> bar");
    NAOS_CHECK_STR_EQ(dump(),
                      "(doc (bq (p (text \"foo\") (br) (text \"bar\"))))");
    check_child(0, AMARK_KIND_TEXT, 2, 5, 1);
    check_child(1, AMARK_KIND_HARD_BREAK, 5, 8, 1);
    check_child(2, AMARK_KIND_TEXT, 10, 13, 2);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
}

NAOS_TEST(list_item_indentation_is_left_out)
{
    PARSE("- foo\n  bar *");
    NAOS_CHECK_STR_EQ(dump(), "(doc (ul - tight (li (p (text \"foo\") (sbr) "
                              "(text \"bar *\")))))");
    check_child(0, AMARK_KIND_TEXT, 2, 5, 1);
    check_child(1, AMARK_KIND_SOFT_BREAK, 5, 6, 1);
    check_child(2, AMARK_KIND_TEXT, 8, 13, 2);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
}

NAOS_TEST(lazy_continuation_lines_follow_the_same_rules)
{
    PARSE("> > foo\nbar \n> baz");
    NAOS_CHECK_STR_EQ(dump(),
                      "(doc (bq (bq (p (text \"foo\") (sbr) (text \"bar\") "
                      "(sbr) (text \"baz\")))))");
    check_child(0, AMARK_KIND_TEXT, 4, 7, 1);
    check_child(1, AMARK_KIND_SOFT_BREAK, 7, 8, 1);
    check_child(2, AMARK_KIND_TEXT, 8, 11, 2);
    check_child(3, AMARK_KIND_SOFT_BREAK, 11, 13, 2);
    check_child(4, AMARK_KIND_TEXT, 15, 18, 3);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
}

/* ---- Paragraphs and headings only ---- */

NAOS_TEST(headings_get_inline_children)
{
    PARSE("# foo *  #\n\nbar\nbaz  \n===");
    NAOS_CHECK_STR_EQ(dump(),
                      "(doc (h 1 atx (text \"foo *\")) (h 1 setext "
                      "(text \"bar\") (sbr) (text \"baz\")))");
    check_child(0, AMARK_KIND_TEXT, 2, 7, 1);
    NAOS_CHECK_UINT_EQ(child_count(block_at(1)), 3);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
}

NAOS_TEST(empty_atx_headings_have_no_children)
{
    PARSE("#\n\n## ##\n\n###   \nfoo");
    NAOS_CHECK_STR_EQ(dump(), "(doc (h 1 atx) (h 2 atx) (h 3 atx) "
                              "(p (text \"foo\")))");
    NAOS_CHECK_TRUE(tree_ok());
}

NAOS_TEST(other_blocks_have_no_children)
{
    PARSE("```\n*a*\n```\n\n<div>\nb  \nc\n</div>\n\n---\n\n    d\n    e\n\n"
          "[x]: /u \"t\"\n");
    NAOS_CHECK_STR_EQ(dump(), "(doc (code fenced ` 3) (html) (hr) "
                              "(code indented) (def))");
    NAOS_CHECK_UINT_EQ(amark_inline_content_count(g_doc), 0);
    NAOS_CHECK_TRUE(tree_ok());
}

NAOS_TEST(contents_are_parsed_in_document_order)
{
    amark_node_id_t node;

    PARSE("# a\n\n> b\n\nc\nd\n");
    NAOS_CHECK_STR_EQ(dump(),
                      "(doc (h 1 atx (text \"a\")) (bq (p (text \"b\"))) "
                      "(p (text \"c\") (sbr) (text \"d\")))");
    /* The blocks are made first; the inline nodes follow all of them, in
     * the order of the contents. */
    node = child_at(block_at(0), 0);
    NAOS_CHECK_UINT_EQ(child_at(block_at(1), 0), node + 1);
    NAOS_CHECK_UINT_EQ(child_at(block_at(2), 0), node + 2);
    NAOS_CHECK_UINT_EQ(child_at(block_at(2), 2), node + 4);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
}

/* ---- The parsed range ---- */

NAOS_TEST(a_range_that_does_not_start_at_0)
{
    static const char input[] = "a  \nbc  \nde\nf  \ng";
    parse_options_t options = default_options();

    /* [5, 11): "c  \nde": nothing before or after it is read. */
    options.start = 5;
    options.end = 11;
    NAOS_REQUIRE_INT_EQ(parse_with(input, sizeof(input) - 1, &options),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (text \"c\") (br) (text \"de\")))");
    check_child(0, AMARK_KIND_TEXT, 5, 6, 2);
    check_child(1, AMARK_KIND_HARD_BREAK, 6, 9, 2);
    check_child(2, AMARK_KIND_TEXT, 9, 11, 3);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
}

/* ---- Limits ---- */

NAOS_TEST(the_depth_limit_counts_from_the_paragraph)
{
    /* doc 0, bq 1, bq 2, p 3, the inlines 4. */
    static const char input[] = "> > a\n> > b";
    parse_options_t options = default_options();

    options.max_depth = 4;
    NAOS_REQUIRE_INT_EQ(parse_with(input, sizeof(input) - 1, &options),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(child_count(block_at(0)), 3);
    NAOS_CHECK_TRUE(tree_ok());
    amark_document_destroy(g_doc);
    g_doc = NULL;

    options.max_depth = 3;
    NAOS_CHECK_INT_EQ(parse_with(input, sizeof(input) - 1, &options),
                      AMARK_ERROR_LIMIT_EXCEEDED);
}

NAOS_TEST(the_node_count_limit_counts_the_inline_nodes)
{
    /* doc and p, then text, sbr, text: 5 nodes. */
    static const char input[] = "a\nb";
    parse_options_t options = default_options();

    options.max_nodes = 5;
    NAOS_REQUIRE_INT_EQ(parse_with(input, sizeof(input) - 1, &options),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 5);
    NAOS_CHECK_TRUE(tree_ok());
    amark_document_destroy(g_doc);
    g_doc = NULL;

    options.max_nodes = 4;
    NAOS_CHECK_INT_EQ(parse_with(input, sizeof(input) - 1, &options),
                      AMARK_ERROR_LIMIT_EXCEEDED);
}

NAOS_TEST(an_empty_document_has_nothing_to_parse)
{
    PARSE("");
    NAOS_CHECK_STR_EQ(dump(), "(doc)");
    NAOS_CHECK_TRUE(tree_ok());
}

NAOS_SUITE(text,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(one_line_is_one_text),
    NAOS_ADD_TEST(special_bytes_that_start_nothing_are_text),
    NAOS_ADD_TEST(special_bytes_at_the_ends_of_a_line),
    NAOS_ADD_TEST(bytes_outside_ascii_and_nul_are_text),
    NAOS_ADD_TEST(white_space_before_a_special_byte_stays_in_the_text)
)

NAOS_SUITE(line_break,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(soft_break_with_lf_cr_and_crlf),
    NAOS_ADD_TEST(one_space_before_the_line_ending_is_a_soft_break),
    NAOS_ADD_TEST(two_or_more_spaces_make_a_hard_break),
    NAOS_ADD_TEST(tabs_before_the_line_ending),
    NAOS_ADD_TEST(no_break_at_the_end_of_a_content),
    NAOS_ADD_TEST(several_lines_alternate_texts_and_breaks)
)

NAOS_SUITE(prefix,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(leading_white_space_of_a_continuation_line_is_left_out),
    NAOS_ADD_TEST(block_quote_prefixes_are_left_out),
    NAOS_ADD_TEST(list_item_indentation_is_left_out),
    NAOS_ADD_TEST(lazy_continuation_lines_follow_the_same_rules)
)

NAOS_SUITE(content,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(headings_get_inline_children),
    NAOS_ADD_TEST(empty_atx_headings_have_no_children),
    NAOS_ADD_TEST(other_blocks_have_no_children),
    NAOS_ADD_TEST(contents_are_parsed_in_document_order),
    NAOS_ADD_TEST(a_range_that_does_not_start_at_0),
    NAOS_ADD_TEST(an_empty_document_has_nothing_to_parse)
)

NAOS_SUITE(limit,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(the_depth_limit_counts_from_the_paragraph),
    NAOS_ADD_TEST(the_node_count_limit_counts_the_inline_nodes)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(text),
    NAOS_ADD_SUITE(line_break),
    NAOS_ADD_SUITE(prefix),
    NAOS_ADD_SUITE(content),
    NAOS_ADD_SUITE(limit)
)
