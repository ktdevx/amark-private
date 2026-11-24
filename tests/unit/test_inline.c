/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of the inline parser (src/inline.h). Most tests run the block
 * parser and then amark_inline_parse right after it, as amark_parse does.
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
 * from the paragraph's depth, and the tree check on every tree. The
 * structures read to their end at the special byte: backslash escapes (and
 * a backslash before a line ending as a hard break), character references,
 * code spans (a whole unmatched backtick string as text, a string after an
 * escaped backtick), autolinks and raw HTML, across lines without breaks,
 * and their precedence (the one that starts first wins, no special byte
 * inside them starts anything, an escaped special byte starts nothing).
 * Emphasis and strong emphasis: their ranges and children, the part of a
 * delimiter run that is used, unmatched runs as text, emphasis across lines
 * holding the line breaks, the ends of a content as white space, and
 * ill-formed UTF-8 (in both UTF-8 modes) and U+0000 as punctuation around
 * delimiters, with the ranges as offsets of the bytes given. The dialect:
 * a content with every inline kind gives the same tree and link attributes
 * with no dialect and with non-NULL dialects (Requirement 17.5).
 * Through the public entry amark_parse: the block nodes (kinds, ranges,
 * lines, attributes and order) are those of the block parser alone, and
 * the inline nodes follow them.
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
static amark_document_t *g_blocks; /* the block parser alone */
static amark_document_t *g_other;  /* a second parse to compare with */
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
    g_blocks = NULL;
    g_other = NULL;
    g_length = 0;
}

NAOS_TEARDOWN(counting)
{
    amark_arena_deinit(&g_scratch);
    amark_document_destroy(g_doc);
    g_doc = NULL;
    amark_document_destroy(g_blocks);
    g_blocks = NULL;
    amark_document_destroy(g_other);
    g_other = NULL;
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

/* ---- Backslash escapes ---- */

NAOS_TEST(a_backslash_before_ascii_punctuation_is_an_escape)
{
    PARSE("a\\*b\\\\c");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (text \"a\") (esc \"\\\\*\") "
                              "(text \"b\") (esc \"\\\\\\\\\") "
                              "(text \"c\")))");
    check_child(0, AMARK_KIND_TEXT, 0, 1, 1);
    check_child(1, AMARK_KIND_BACKSLASH_ESCAPE, 1, 3, 1);
    check_child(2, AMARK_KIND_TEXT, 3, 4, 1);
    check_child(3, AMARK_KIND_BACKSLASH_ESCAPE, 4, 6, 1);
    check_child(4, AMARK_KIND_TEXT, 6, 7, 1);
    NAOS_CHECK_UINT_EQ(child_count(block_at(0)), 5);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
}

NAOS_TEST(every_ascii_punctuation_character_can_be_escaped)
{
    static const char punctuation[] = "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
    char input[2 * (sizeof(punctuation) - 1)];
    size_t i;

    for (i = 0; i < sizeof(punctuation) - 1; i++) {
        input[2 * i] = '\\';
        input[2 * i + 1] = punctuation[i];
    }
    parse_bytes(input, sizeof(input));
    NAOS_REQUIRE_UINT_EQ(child_count(block_at(0)), sizeof(punctuation) - 1);
    for (i = 0; i < sizeof(punctuation) - 1; i++) {
        NAOS_TRACE_PUSH("escape %lu", (unsigned long)i);
        check_child(i, AMARK_KIND_BACKSLASH_ESCAPE, 2 * i, 2 * i + 2, 1);
        NAOS_TRACE_POP();
    }
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
}

NAOS_TEST(a_backslash_before_anything_else_is_text)
{
    /* A letter, a space, a byte outside ASCII and a backslash that is the
     * last byte of the content: all text, joined into one node. */
    PARSE("\\a \\ b\\\xC3\xA9 c\\");
    NAOS_CHECK_UINT_EQ(child_count(block_at(0)), 1);
    check_child(0, AMARK_KIND_TEXT, 0, 12, 1);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
}

NAOS_TEST(a_backslash_before_a_line_ending_is_a_hard_break)
{
    PARSE("foo\\\nbar");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (text \"foo\") (br) (text \"bar\")))");
    check_child(0, AMARK_KIND_TEXT, 0, 3, 1);
    check_child(1, AMARK_KIND_HARD_BREAK, 3, 5, 1);
    check_child(2, AMARK_KIND_TEXT, 5, 8, 2);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
    amark_document_destroy(g_doc);
    g_doc = NULL;

    PARSE("foo\\\r\nbar");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (text \"foo\") (br) (text \"bar\")))");
    check_child(1, AMARK_KIND_HARD_BREAK, 3, 6, 1);
    check_child(2, AMARK_KIND_TEXT, 6, 9, 2);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
    amark_document_destroy(g_doc);
    g_doc = NULL;

    PARSE("foo\\\rbar");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (text \"foo\") (br) (text \"bar\")))");
    check_child(1, AMARK_KIND_HARD_BREAK, 3, 5, 1);
    check_child(2, AMARK_KIND_TEXT, 5, 8, 2);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
    amark_document_destroy(g_doc);
    g_doc = NULL;

    /* The prefix of the next line is in no item. */
    PARSE("> foo\\\n> bar");
    NAOS_CHECK_STR_EQ(dump(),
                      "(doc (bq (p (text \"foo\") (br) (text \"bar\"))))");
    check_child(0, AMARK_KIND_TEXT, 2, 5, 1);
    check_child(1, AMARK_KIND_HARD_BREAK, 5, 7, 1);
    check_child(2, AMARK_KIND_TEXT, 9, 12, 2);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
    amark_document_destroy(g_doc);
    g_doc = NULL;

    /* An escaped backslash before the line ending: a soft break. */
    PARSE("foo\\\\\nbar");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (text \"foo\") (esc \"\\\\\\\\\") "
                              "(sbr) (text \"bar\")))");
    check_child(1, AMARK_KIND_BACKSLASH_ESCAPE, 3, 5, 1);
    check_child(2, AMARK_KIND_SOFT_BREAK, 5, 6, 1);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
}

/* ---- Entity and numeric character references ---- */

NAOS_TEST(character_references_are_one_node)
{
    PARSE("a &amp; b");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (text \"a \") (ref \"&amp;\") "
                              "(text \" b\")))");
    check_child(0, AMARK_KIND_TEXT, 0, 2, 1);
    check_child(1, AMARK_KIND_CHARACTER_REFERENCE, 2, 7, 1);
    check_child(2, AMARK_KIND_TEXT, 7, 9, 1);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
    amark_document_destroy(g_doc);
    g_doc = NULL;

    /* Numeric references, whatever their value. */
    PARSE("&#0;&#1234567;&#xFFFFFF;&#XD800;");
    NAOS_CHECK_UINT_EQ(child_count(block_at(0)), 4);
    check_child(0, AMARK_KIND_CHARACTER_REFERENCE, 0, 4, 1);
    check_child(1, AMARK_KIND_CHARACTER_REFERENCE, 4, 14, 1);
    check_child(2, AMARK_KIND_CHARACTER_REFERENCE, 14, 24, 1);
    check_child(3, AMARK_KIND_CHARACTER_REFERENCE, 24, 32, 1);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
    amark_document_destroy(g_doc);
    g_doc = NULL;

    /* On the second line. */
    PARSE("a\n&copy;");
    check_child(2, AMARK_KIND_CHARACTER_REFERENCE, 2, 8, 2);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
}

NAOS_TEST(an_ampersand_that_starts_no_reference_is_text)
{
    /* No ';', a name not in the table, too many digits, a reference cut by
     * a line ending. */
    PARSE("&amp &nosuch; &#12345678; &#x1234567; &am\np;");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (text \"&amp &nosuch; &#12345678; "
                              "&#x1234567; &am\") (sbr) (text \"p;\")))");
    NAOS_CHECK_UINT_EQ(child_count(block_at(0)), 3);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
}

/* ---- Code spans ---- */

NAOS_TEST(a_code_span_runs_from_the_opening_to_the_closing_string)
{
    PARSE("a `b` c");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (text \"a \") (code \"`b`\") "
                              "(text \" c\")))");
    check_child(0, AMARK_KIND_TEXT, 0, 2, 1);
    check_child(1, AMARK_KIND_CODE_SPAN, 2, 5, 1);
    check_child(2, AMARK_KIND_TEXT, 5, 7, 1);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
    amark_document_destroy(g_doc);
    g_doc = NULL;

    /* Only a string of the same length closes. */
    PARSE("``a`b``");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (code \"``a`b``\")))");
    check_child(0, AMARK_KIND_CODE_SPAN, 0, 7, 1);
    NAOS_CHECK_UINT_EQ(child_count(block_at(0)), 1);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
}

NAOS_TEST(an_unmatched_backtick_string_is_text_as_a_whole)
{
    /* If only the first backtick were text, "``a``" would be a code span. */
    PARSE("```a``b``");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (text \"```a\") (code \"``b``\")))");
    check_child(0, AMARK_KIND_TEXT, 0, 4, 1);
    check_child(1, AMARK_KIND_CODE_SPAN, 4, 9, 1);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
}

NAOS_TEST(a_code_span_crosses_lines_without_breaks)
{
    PARSE("a `b  \nc` d");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (text \"a \") (code \"`b  \\nc`\") "
                              "(text \" d\")))");
    check_child(1, AMARK_KIND_CODE_SPAN, 2, 9, 1);
    check_child(2, AMARK_KIND_TEXT, 9, 11, 2);
    NAOS_CHECK_UINT_EQ(child_count(block_at(0)), 3);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
    amark_document_destroy(g_doc);
    g_doc = NULL;

    /* In a block quote: the range holds the prefix of the second line, and
     * no break is made for the line ending inside. */
    PARSE("> `a\n> b`");
    NAOS_CHECK_STR_EQ(dump(), "(doc (bq (p (code \"`a\\n> b`\"))))");
    check_child(0, AMARK_KIND_CODE_SPAN, 2, 9, 1);
    NAOS_CHECK_UINT_EQ(child_count(block_at(0)), 1);
    NAOS_CHECK_UINT_EQ(amark_node_end_line(g_doc, child_at(block_at(0), 0)),
                       2);
    NAOS_CHECK_TRUE(tree_ok());
}

NAOS_TEST(an_escaped_backtick_starts_a_new_string)
{
    /* The backticks after the escaped one are a string of their own. */
    PARSE("\\``a`");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (esc \"\\\\`\") (code \"`a`\")))");
    check_child(0, AMARK_KIND_BACKSLASH_ESCAPE, 0, 2, 1);
    check_child(1, AMARK_KIND_CODE_SPAN, 2, 5, 1);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
    amark_document_destroy(g_doc);
    g_doc = NULL;

    PARSE("\\```a``");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (esc \"\\\\`\") (code \"``a``\")))");
    check_child(1, AMARK_KIND_CODE_SPAN, 2, 7, 1);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
    amark_document_destroy(g_doc);
    g_doc = NULL;

    /* Inside a code span a backslash escapes nothing. */
    PARSE("`a\\`b`");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (code \"`a\\\\`\") (text \"b`\")))");
    check_child(0, AMARK_KIND_CODE_SPAN, 0, 4, 1);
    check_child(1, AMARK_KIND_TEXT, 4, 6, 1);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
}

/* ---- Autolinks ---- */

NAOS_TEST(autolinks_are_one_node_with_their_type)
{
    PARSE("a <http://x.y/*_]> <foo@bar.example.com>");
    NAOS_CHECK_STR_EQ(dump(),
                      "(doc (p (text \"a \") (autolink uri "
                      "\"<http://x.y/*_]>\") (text \" \") (autolink email "
                      "\"<foo@bar.example.com>\")))");
    check_child(1, AMARK_KIND_AUTOLINK, 2, 18, 1);
    check_child(3, AMARK_KIND_AUTOLINK, 19, 40, 1);
    NAOS_CHECK_INT_EQ(amark_autolink_type(g_doc, child_at(block_at(0), 1)),
                      AMARK_AUTOLINK_URI);
    NAOS_CHECK_INT_EQ(amark_autolink_type(g_doc, child_at(block_at(0), 3)),
                      AMARK_AUTOLINK_EMAIL);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
    amark_document_destroy(g_doc);
    g_doc = NULL;

    /* Backslashes and references inside are not read. */
    PARSE("<made-up-scheme://a\\[&amp;>");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (autolink uri "
                              "\"<made-up-scheme://a\\\\[&amp;>\")))");
    check_child(0, AMARK_KIND_AUTOLINK, 0, 27, 1);
    NAOS_CHECK_UINT_EQ(child_count(block_at(0)), 1);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
}

NAOS_TEST(a_less_than_sign_that_starts_nothing_is_text)
{
    /* A space in a URI, a line ending in an autolink, no tag. */
    PARSE("<http://a b> <http://a\nb> < c");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (text \"<http://a b> <http://a\") "
                              "(sbr) (text \"b> < c\")))");
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
}

/* ---- Raw HTML ---- */

NAOS_TEST(raw_html_is_one_node)
{
    PARSE("a <b>c</b>");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (text \"a \") (html \"<b>\") "
                              "(text \"c\") (html \"</b>\")))");
    check_child(1, AMARK_KIND_RAW_HTML, 2, 5, 1);
    check_child(2, AMARK_KIND_TEXT, 5, 6, 1);
    check_child(3, AMARK_KIND_RAW_HTML, 6, 10, 1);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
    amark_document_destroy(g_doc);
    g_doc = NULL;

    /* The other forms, and \ and & inside that are not read; after a
     * text, as at the start of a line they would open an HTML block. */
    PARSE("a <!-- *a* --><!--><?p \\* ?><!X &amp;><![CDATA[`]]>");
    NAOS_CHECK_STR_EQ(dump(),
                      "(doc (p (text \"a \") (html \"<!-- *a* -->\") "
                      "(html \"<!-->\") (html \"<?p \\\\* ?>\") "
                      "(html \"<!X &amp;>\") (html \"<![CDATA[`]]>\")))");
    check_child(1, AMARK_KIND_RAW_HTML, 2, 14, 1);
    check_child(2, AMARK_KIND_RAW_HTML, 14, 19, 1);
    check_child(3, AMARK_KIND_RAW_HTML, 19, 28, 1);
    check_child(4, AMARK_KIND_RAW_HTML, 28, 38, 1);
    check_child(5, AMARK_KIND_RAW_HTML, 38, 51, 1);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
}

NAOS_TEST(raw_html_crosses_lines_without_breaks)
{
    PARSE("a <b\nc=\"d  \ne\">f");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (text \"a \") "
                              "(html \"<b\\nc=\\\"d  \\ne\\\">\") "
                              "(text \"f\")))");
    check_child(1, AMARK_KIND_RAW_HTML, 2, 15, 1);
    check_child(2, AMARK_KIND_TEXT, 15, 16, 3);
    NAOS_CHECK_UINT_EQ(amark_node_end_line(g_doc, child_at(block_at(0), 1)),
                       3);
    NAOS_CHECK_UINT_EQ(child_count(block_at(0)), 3);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
}

NAOS_TEST(html_that_does_not_close_is_text)
{
    /* Not at the start of a line, which would open an HTML block. */
    PARSE("a <!-- b\nc <a href=\"x> <? d");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (text \"a <!-- b\") (sbr) "
                              "(text \"c <a href=\\\"x> <? d\")))");
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
}

/* ---- Precedence ---- */

NAOS_TEST(the_structure_that_starts_first_wins)
{
    PARSE("`<a href=\"`\">`");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (code \"`<a href=\\\"`\") "
                              "(text \"\\\">`\")))");
    check_child(0, AMARK_KIND_CODE_SPAN, 0, 11, 1);
    check_child(1, AMARK_KIND_TEXT, 11, 14, 1);
    NAOS_CHECK_TRUE(tree_ok());
    amark_document_destroy(g_doc);
    g_doc = NULL;

    PARSE("<a href=\"`\">`");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (html \"<a href=\\\"`\\\">\") "
                              "(text \"`\")))");
    check_child(0, AMARK_KIND_RAW_HTML, 0, 12, 1);
    check_child(1, AMARK_KIND_TEXT, 12, 13, 1);
    NAOS_CHECK_TRUE(tree_ok());
    amark_document_destroy(g_doc);
    g_doc = NULL;

    PARSE("`<http://foo.bar.`baz>`");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (code \"`<http://foo.bar.`\") "
                              "(text \"baz>`\")))");
    check_child(0, AMARK_KIND_CODE_SPAN, 0, 18, 1);
    check_child(1, AMARK_KIND_TEXT, 18, 23, 1);
    NAOS_CHECK_TRUE(tree_ok());
    amark_document_destroy(g_doc);
    g_doc = NULL;

    PARSE("<http://foo.bar.`baz>`");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (autolink uri "
                              "\"<http://foo.bar.`baz>\") (text \"`\")))");
    check_child(0, AMARK_KIND_AUTOLINK, 0, 21, 1);
    check_child(1, AMARK_KIND_TEXT, 21, 22, 1);
    NAOS_CHECK_TRUE(tree_ok());
}

NAOS_TEST(special_bytes_inside_a_structure_start_nothing)
{
    /* Brackets, delimiters, backslashes and ampersands inside a code span,
     * an autolink and raw HTML are part of that one node. */
    PARSE("`[*_\\&amp;]`<ab:c/[*_]><x y='[*_\\&amp;]'>");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (code \"`[*_\\\\&amp;]`\") "
                              "(autolink uri \"<ab:c/[*_]>\") "
                              "(html \"<x y='[*_\\\\&amp;]'>\")))");
    check_child(0, AMARK_KIND_CODE_SPAN, 0, 12, 1);
    check_child(1, AMARK_KIND_AUTOLINK, 12, 23, 1);
    check_child(2, AMARK_KIND_RAW_HTML, 23, 41, 1);
    NAOS_CHECK_UINT_EQ(child_count(block_at(0)), 3);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
}

NAOS_TEST(an_escaped_special_byte_starts_nothing)
{
    PARSE("\\`a` \\<b> \\&amp; \\<http://c>");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (esc \"\\\\`\") (text \"a` \") "
                              "(esc \"\\\\<\") (text \"b> \") "
                              "(esc \"\\\\&\") (text \"amp; \") "
                              "(esc \"\\\\<\") (text \"http://c>\")))");
    check_child(0, AMARK_KIND_BACKSLASH_ESCAPE, 0, 2, 1);
    check_child(2, AMARK_KIND_BACKSLASH_ESCAPE, 5, 7, 1);
    check_child(4, AMARK_KIND_BACKSLASH_ESCAPE, 10, 12, 1);
    check_child(6, AMARK_KIND_BACKSLASH_ESCAPE, 17, 19, 1);
    NAOS_CHECK_UINT_EQ(child_count(block_at(0)), 8);
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

/* ---- Emphasis ---- */

/* Checks the kind, range and start line of node. */
static void check_node(amark_node_id_t node, amark_kind_t kind, size_t start,
                       size_t end, size_t line)
{
    amark_range_t range = node_range(node);

    NAOS_CHECK_UINT_EQ(amark_node_kind(g_doc, node), kind);
    NAOS_CHECK_UINT_EQ(range.start, start);
    NAOS_CHECK_UINT_EQ(range.end, end);
    NAOS_CHECK_UINT_EQ(amark_node_start_line(g_doc, node), line);
}

NAOS_TEST(emphasis_and_strong_emphasis_wrap_their_children)
{
    amark_node_id_t em;
    amark_node_id_t strong;

    PARSE("*a* __b__ c");
    NAOS_CHECK_STR_EQ(dump(),
                      "(doc (p (em (text \"a\")) (text \" \") "
                      "(strong (text \"b\")) (text \" c\")))");
    em = child_at(block_at(0), 0);
    check_node(em, AMARK_KIND_EMPHASIS, 0, 3, 1);
    NAOS_CHECK_UINT_EQ(child_count(em), 1);
    check_node(child_at(em, 0), AMARK_KIND_TEXT, 1, 2, 1);
    check_child(1, AMARK_KIND_TEXT, 3, 4, 1);
    strong = child_at(block_at(0), 2);
    check_node(strong, AMARK_KIND_STRONG, 4, 9, 1);
    NAOS_CHECK_UINT_EQ(child_count(strong), 1);
    check_node(child_at(strong, 0), AMARK_KIND_TEXT, 6, 7, 1);
    check_child(3, AMARK_KIND_TEXT, 9, 11, 1);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
}

NAOS_TEST(nested_emphasis_and_other_items_inside)
{
    amark_node_id_t em;
    amark_node_id_t strong;

    /* ***a*** is em around strong; the code span is a child as it is. */
    PARSE("***a `*` b***");
    NAOS_CHECK_STR_EQ(dump(),
                      "(doc (p (em (strong (text \"a \") (code \"`*`\") "
                      "(text \" b\")))))");
    em = child_at(block_at(0), 0);
    check_node(em, AMARK_KIND_EMPHASIS, 0, 13, 1);
    strong = child_at(em, 0);
    check_node(strong, AMARK_KIND_STRONG, 1, 12, 1);
    NAOS_CHECK_UINT_EQ(child_count(strong), 3);
    check_node(child_at(strong, 1), AMARK_KIND_CODE_SPAN, 5, 8, 1);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
}

NAOS_TEST(part_of_a_delimiter_run_is_used)
{
    /* The opener is used from its inner side; the rest stays outside. */
    PARSE("***a**");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (text \"*\") (strong (text \"a\"))))");
    check_child(0, AMARK_KIND_TEXT, 0, 1, 1);
    check_child(1, AMARK_KIND_STRONG, 1, 6, 1);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
    amark_document_destroy(g_doc);
    g_doc = NULL;

    /* The closer is used from its inner side. */
    PARSE("*a***");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (em (text \"a\")) (text \"**\")))");
    check_child(0, AMARK_KIND_EMPHASIS, 0, 3, 1);
    check_child(1, AMARK_KIND_TEXT, 3, 5, 1);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
}

NAOS_TEST(unmatched_delimiters_are_text)
{
    /* No closer, an intraword `_`, and a run between spaces. */
    PARSE("**a a_b_c * d_");
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (text \"**a a_b_c * d_\")))");
    check_child(0, AMARK_KIND_TEXT, 0, 14, 1);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
    amark_document_destroy(g_doc);
    g_doc = NULL;

    /* An intraword `*` opens and closes. */
    PARSE("a*b*c");
    NAOS_CHECK_STR_EQ(dump(),
                      "(doc (p (text \"a\") (em (text \"b\")) (text \"c\")))");
    check_child(1, AMARK_KIND_EMPHASIS, 1, 4, 1);
    NAOS_CHECK_TRUE(tree_ok());
}

NAOS_TEST(emphasis_spans_lines_and_holds_their_breaks)
{
    amark_node_id_t em;

    /* A soft break with CR LF, and a hard break, inside emphasis. */
    PARSE("*a\r\nb  \nc*");
    NAOS_CHECK_STR_EQ(dump(),
                      "(doc (p (em (text \"a\") (sbr) (text \"b\") (br) "
                      "(text \"c\"))))");
    NAOS_CHECK_UINT_EQ(child_count(block_at(0)), 1);
    em = child_at(block_at(0), 0);
    check_node(em, AMARK_KIND_EMPHASIS, 0, 10, 1);
    NAOS_CHECK_UINT_EQ(amark_node_end_line(g_doc, em), 3);
    NAOS_CHECK_UINT_EQ(child_count(em), 5);
    check_node(child_at(em, 0), AMARK_KIND_TEXT, 1, 2, 1);
    check_node(child_at(em, 1), AMARK_KIND_SOFT_BREAK, 2, 4, 1);
    check_node(child_at(em, 2), AMARK_KIND_TEXT, 4, 5, 2);
    check_node(child_at(em, 3), AMARK_KIND_HARD_BREAK, 5, 8, 2);
    check_node(child_at(em, 4), AMARK_KIND_TEXT, 8, 9, 3);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
}

NAOS_TEST(emphasis_across_lines_of_a_block_quote)
{
    amark_node_id_t strong;

    /*
     * The range holds the prefix of the second line (2.5), so the bytes
     * are checked as numbers instead of with check_bytes_covered.
     */
    PARSE("> **a\n> b**");
    NAOS_CHECK_STR_EQ(dump(),
                      "(doc (bq (p (strong (text \"a\") (sbr) "
                      "(text \"b\")))))");
    NAOS_CHECK_UINT_EQ(child_count(block_at(0)), 1);
    strong = child_at(block_at(0), 0);
    check_node(strong, AMARK_KIND_STRONG, 2, 11, 1);
    NAOS_CHECK_UINT_EQ(child_count(strong), 3);
    check_node(child_at(strong, 0), AMARK_KIND_TEXT, 4, 5, 1);
    check_node(child_at(strong, 1), AMARK_KIND_SOFT_BREAK, 5, 6, 1);
    check_node(child_at(strong, 2), AMARK_KIND_TEXT, 8, 9, 2);
    NAOS_CHECK_TRUE(tree_ok());
}

NAOS_TEST(the_ends_of_the_content_are_white_space)
{
    /* A heading whose content starts and ends with a delimiter run. */
    PARSE("# _a_ #");
    NAOS_CHECK_STR_EQ(dump(), "(doc (h 1 atx (em (text \"a\"))))");
    check_child(0, AMARK_KIND_EMPHASIS, 2, 5, 1);
    NAOS_CHECK_TRUE(tree_ok());
    check_bytes_covered();
}

/*
 * Parses length bytes through amark_parse with utf8_mode into g_doc and
 * returns what it returns.
 */
static amark_error_t parse_public(const char *bytes, size_t length,
                                  amark_utf8_mode_t utf8_mode)
{
    amark_config_t config;

    NAOS_REQUIRE_UINT_LE(length, BUFFER_SIZE);
    memcpy(g_buffer, bytes, length);
    g_length = length;
    amark_config_init(&config);
    config.allocator = g_allocator;
    config.utf8_mode = utf8_mode;
    return amark_parse((const char *)g_buffer, g_length, &config, &g_doc);
}

NAOS_TEST(ill_formed_utf8_is_punctuation_around_delimiters)
{
    /*
     * As punctuation, U+FFFD lets `_` open and close next to it; as a
     * letter it would not (compare x_a_x).
     */
    static const char around[] = "\xFF_a_\xE3\x81";
    /*
     * As punctuation, U+FFFD after `*` and a letter before it make a run
     * that is not left-flanking; as a letter it would be (compare the
     * U+00E9 case, which is emphasis).
     */
    static const char inside[] = "a*\xFF" "b\xFF*c";
    static const char letter[] = "a*\xC3\xA9" "b\xC3\xA9*c";

    NAOS_REQUIRE_INT_EQ(parse_public(around, sizeof(around) - 1,
                                     AMARK_UTF8_REPLACE),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(child_count(block_at(0)), 3);
    /* The offsets are those of the bytes given, not of a replaced copy. */
    check_child(0, AMARK_KIND_TEXT, 0, 1, 1);
    check_child(1, AMARK_KIND_EMPHASIS, 1, 4, 1);
    check_child(2, AMARK_KIND_TEXT, 4, 6, 1);
    NAOS_CHECK_TRUE(tree_ok());
    amark_document_destroy(g_doc);
    g_doc = NULL;
    NAOS_CHECK_INT_EQ(parse_public(around, sizeof(around) - 1,
                                   AMARK_UTF8_STRICT),
                      AMARK_ERROR_INVALID_INPUT);
    NAOS_CHECK_PTR_EQ(g_doc, NULL);

    NAOS_REQUIRE_INT_EQ(parse_public(inside, sizeof(inside) - 1,
                                     AMARK_UTF8_REPLACE),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(child_count(block_at(0)), 1);
    check_child(0, AMARK_KIND_TEXT, 0, sizeof(inside) - 1, 1);
    NAOS_CHECK_TRUE(tree_ok());
    amark_document_destroy(g_doc);
    g_doc = NULL;
    NAOS_CHECK_INT_EQ(parse_public(inside, sizeof(inside) - 1,
                                   AMARK_UTF8_STRICT),
                      AMARK_ERROR_INVALID_INPUT);

    NAOS_REQUIRE_INT_EQ(parse_public(letter, sizeof(letter) - 1,
                                     AMARK_UTF8_REPLACE),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(child_count(block_at(0)), 3);
    check_child(1, AMARK_KIND_EMPHASIS, 1, 8, 1);
    NAOS_CHECK_TRUE(tree_ok());
}

NAOS_TEST(nul_is_punctuation_around_delimiters_in_both_modes)
{
    static const char around[] = "\x00_a_\x00";
    static const char inside[] = "a*\x00" "b\x00*c";
    static const amark_utf8_mode_t modes[] = {AMARK_UTF8_STRICT,
                                              AMARK_UTF8_REPLACE};
    size_t i;

    for (i = 0; i < 2; i++) {
        NAOS_TRACE_PUSH("mode %d", (int)modes[i]);
        NAOS_REQUIRE_INT_EQ(parse_public(around, sizeof(around) - 1,
                                         modes[i]),
                            AMARK_ERROR_NONE);
        NAOS_CHECK_UINT_EQ(child_count(block_at(0)), 3);
        check_child(0, AMARK_KIND_TEXT, 0, 1, 1);
        check_child(1, AMARK_KIND_EMPHASIS, 1, 4, 1);
        check_child(2, AMARK_KIND_TEXT, 4, 5, 1);
        NAOS_CHECK_TRUE(tree_ok());
        amark_document_destroy(g_doc);
        g_doc = NULL;

        NAOS_REQUIRE_INT_EQ(parse_public(inside, sizeof(inside) - 1,
                                         modes[i]),
                            AMARK_ERROR_NONE);
        NAOS_CHECK_UINT_EQ(child_count(block_at(0)), 1);
        check_child(0, AMARK_KIND_TEXT, 0, sizeof(inside) - 1, 1);
        NAOS_CHECK_TRUE(tree_ok());
        amark_document_destroy(g_doc);
        g_doc = NULL;
        NAOS_TRACE_POP();
    }
}

NAOS_TEST(well_formed_input_gives_the_same_tree_in_both_modes)
{
    static const char input[] =
        "\xC3\xA9 *\xE3\x80\x8C" "a\xE3\x80\x8D* _b_\xE2\x82\xAC\n"
        "> **c\xC2\xA0**d __e__";
    char strict[DUMP_SIZE];

    NAOS_REQUIRE_INT_EQ(parse_public(input, sizeof(input) - 1,
                                     AMARK_UTF8_STRICT),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_TRUE(tree_ok());
    NAOS_REQUIRE_UINT_LT(amark_test_dump_tree(g_doc, strict, sizeof(strict)),
                         sizeof(strict));
    amark_document_destroy(g_doc);
    g_doc = NULL;
    NAOS_REQUIRE_INT_EQ(parse_public(input, sizeof(input) - 1,
                                     AMARK_UTF8_REPLACE),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_TRUE(tree_ok());
    NAOS_CHECK_STR_EQ(dump(), strict);
    /* The emphasis around U+300C and U+300D (punctuation) is made. */
    NAOS_CHECK_UINT_EQ(amark_node_kind(g_doc, child_at(block_at(0), 1)),
                       AMARK_KIND_EMPHASIS);
}

/* ---- The dialect ---- */

/* Only their addresses are used, as opaque dialect pointers. */
static const int g_dialect_a = 1;
static const int g_dialect_b = 2;

/*
 * Parses the length bytes at bytes (copied to g_buffer) through amark_parse
 * with the default configuration and dialect into *out, requiring success.
 */
static void parse_dialect(const char *bytes, size_t length,
                          const amark_dialect_t *dialect,
                          amark_document_t **out)
{
    amark_config_t config;

    NAOS_REQUIRE_UINT_LE(length, BUFFER_SIZE);
    memcpy(g_buffer, bytes, length);
    g_length = length;
    amark_config_init(&config);
    config.allocator = g_allocator;
    config.dialect = dialect;
    NAOS_REQUIRE_INT_EQ(amark_parse((const char *)g_buffer, g_length, &config,
                                    out),
                        AMARK_ERROR_NONE);
}

/* Requires node to have the same link attributes in g_doc and g_other. */
static void check_same_link(amark_node_id_t node)
{
    amark_range_t a = {0, 0};
    amark_range_t b = {0, 0};
    bool has_a;
    bool has_b;

    NAOS_CHECK_INT_EQ(amark_link_form(g_other, node),
                      amark_link_form(g_doc, node));
    has_a = amark_link_destination(g_doc, node, &a);
    has_b = amark_link_destination(g_other, node, &b);
    NAOS_CHECK_INT_EQ(has_b, has_a);
    NAOS_CHECK_UINT_EQ(b.start, a.start);
    NAOS_CHECK_UINT_EQ(b.end, a.end);
    has_a = amark_link_title(g_doc, node, &a);
    has_b = amark_link_title(g_other, node, &b);
    NAOS_CHECK_INT_EQ(has_b, has_a);
    NAOS_CHECK_UINT_EQ(b.start, a.start);
    NAOS_CHECK_UINT_EQ(b.end, a.end);
    NAOS_CHECK_UINT_EQ(amark_link_definition(g_other, node),
                       amark_link_definition(g_doc, node));
    NAOS_CHECK_INT_EQ(amark_autolink_type(g_other, node),
                      amark_autolink_type(g_doc, node));
}

NAOS_TEST(the_dialect_does_not_change_the_inline_result)
{
    /*
     * Requirement 17.5: the dialect member is passed through and never
     * read, so no dialect (NULL, the default) and any non-NULL dialect give
     * the same tree. The content holds every inline kind: emphasis, strong
     * emphasis, an inline link with a title, an image, the full, collapsed
     * and shortcut references with their definition, a code span, an
     * autolink, raw HTML, an escape, a character reference, and hard and
     * soft breaks.
     */
    static const char content[] =
        "*em* **strong** [in](/u \"t\") ![img](/i)\n"
        "[x][ref] [ref][] [ref] `code` <http://a.b> <span> \\* &amp; hard  \n"
        "soft\n"
        "\n"
        "[ref]: /r \"rt\"\n";
    const amark_dialect_t *dialects[2];
    char want[DUMP_SIZE];
    size_t count;
    size_t i;
    amark_node_id_t node;
    bool kinds[64];
    bool forms[5];
    amark_range_t title;

    dialects[0] = (const amark_dialect_t *)(const void *)&g_dialect_a;
    dialects[1] = (const amark_dialect_t *)(const void *)&g_dialect_b;
    parse_dialect(content, sizeof(content) - 1, NULL, &g_doc);
    NAOS_CHECK_TRUE(tree_ok());
    NAOS_REQUIRE_UINT_LT(amark_test_dump_tree(g_doc, want, sizeof(want)),
                         sizeof(want));
    NAOS_CHECK_STR_EQ(want,
                      "(doc (p (em (text \"em\")) (text \" \") "
                      "(strong (text \"strong\")) (text \" \") "
                      "(link inline dest=\"/u\" title=\"t\" "
                      "(text \"in\")) (text \" \") "
                      "(image inline dest=\"/i\" (text \"img\")) (sbr) "
                      "(link full def=\"ref\" (text \"x\")) (text \" \") "
                      "(link collapsed def=\"ref\" (text \"ref\")) "
                      "(text \" \") "
                      "(link shortcut def=\"ref\" (text \"ref\")) "
                      "(text \" \") (code \"`code`\") (text \" \") "
                      "(autolink uri \"<http://a.b>\") (text \" \") "
                      "(html \"<span>\") (text \" \") "
                      "(esc \"\\\\*\") (text \" \") "
                      "(ref \"&amp;\") (text \" hard\") (br) "
                      "(text \"soft\")) (def))");

    /* Every inline kind is in the tree. */
    memset(kinds, 0, sizeof(kinds));
    memset(forms, 0, sizeof(forms));
    count = amark_document_node_count(g_doc);
    for (node = 1; node <= count; node++) {
        amark_kind_t kind = amark_node_kind(g_doc, node);
        amark_link_form_t form = amark_link_form(g_doc, node);

        if (kind < 64) {
            kinds[kind] = true;
        }
        if ((size_t)form < 5) {
            forms[form] = true;
        }
        if (form == AMARK_LINK_INLINE && kind == AMARK_KIND_LINK) {
            /* The inline link has its title. */
            NAOS_CHECK_TRUE(amark_link_title(g_doc, node, &title));
        }
        if (form == AMARK_LINK_FULL || form == AMARK_LINK_COLLAPSED ||
            form == AMARK_LINK_SHORTCUT) {
            NAOS_CHECK_UINT_NE(amark_link_definition(g_doc, node),
                               AMARK_NODE_NONE);
        }
    }
    NAOS_CHECK_TRUE(kinds[AMARK_KIND_TEXT]);
    NAOS_CHECK_TRUE(kinds[AMARK_KIND_EMPHASIS]);
    NAOS_CHECK_TRUE(kinds[AMARK_KIND_STRONG]);
    NAOS_CHECK_TRUE(kinds[AMARK_KIND_LINK]);
    NAOS_CHECK_TRUE(kinds[AMARK_KIND_IMAGE]);
    NAOS_CHECK_TRUE(kinds[AMARK_KIND_CODE_SPAN]);
    NAOS_CHECK_TRUE(kinds[AMARK_KIND_AUTOLINK]);
    NAOS_CHECK_TRUE(kinds[AMARK_KIND_RAW_HTML]);
    NAOS_CHECK_TRUE(kinds[AMARK_KIND_BACKSLASH_ESCAPE]);
    NAOS_CHECK_TRUE(kinds[AMARK_KIND_CHARACTER_REFERENCE]);
    NAOS_CHECK_TRUE(kinds[AMARK_KIND_HARD_BREAK]);
    NAOS_CHECK_TRUE(kinds[AMARK_KIND_SOFT_BREAK]);
    NAOS_CHECK_TRUE(forms[AMARK_LINK_INLINE]);
    NAOS_CHECK_TRUE(forms[AMARK_LINK_FULL]);
    NAOS_CHECK_TRUE(forms[AMARK_LINK_COLLAPSED]);
    NAOS_CHECK_TRUE(forms[AMARK_LINK_SHORTCUT]);

    for (i = 0; i < 2; i++) {
        NAOS_TRACE_PUSH("dialect %lu", (unsigned long)i);
        amark_document_destroy(g_other);
        g_other = NULL;
        parse_dialect(content, sizeof(content) - 1, dialects[i], &g_other);
        NAOS_CHECK_INT_EQ(amark_tree_check(g_other).violation,
                          AMARK_TREE_VIOLATION_NONE);
        NAOS_REQUIRE_UINT_LT(amark_test_dump_tree(g_other, g_dump,
                                                  sizeof(g_dump)),
                             sizeof(g_dump));
        NAOS_CHECK_STR_EQ(g_dump, want);
        NAOS_REQUIRE_UINT_EQ(amark_document_node_count(g_other), count);
        for (node = 1; node <= count; node++) {
            amark_range_t a;
            amark_range_t b;

            NAOS_TRACE_PUSH("node %lu", (unsigned long)node);
            NAOS_CHECK_UINT_EQ(amark_node_kind(g_other, node),
                               amark_node_kind(g_doc, node));
            NAOS_CHECK_UINT_EQ(amark_node_parent(g_other, node),
                               amark_node_parent(g_doc, node));
            NAOS_REQUIRE_TRUE(amark_node_range(g_doc, node, &a));
            NAOS_REQUIRE_TRUE(amark_node_range(g_other, node, &b));
            NAOS_CHECK_UINT_EQ(b.start, a.start);
            NAOS_CHECK_UINT_EQ(b.end, a.end);
            check_same_link(node);
            NAOS_TRACE_POP();
        }
        NAOS_TRACE_POP();
    }
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

/* ---- Through the public entry ---- */

/*
 * Parses length bytes at bytes twice: with the block parser alone into
 * g_blocks, and through amark_parse into g_doc. Both succeed.
 */
static void parse_both(const char *bytes, size_t length)
{
    amark_config_t config;
    amark_resolved_config_t resolved;

    NAOS_REQUIRE_UINT_LE(length, BUFFER_SIZE);
    memcpy(g_buffer, bytes, length);
    g_length = length;
    amark_config_init(&config);
    config.allocator = g_allocator;
    NAOS_REQUIRE_INT_EQ(amark_config_resolve(&config, g_length, &resolved),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_document_create(&resolved.allocator, g_buffer,
                                              g_length, resolved.start,
                                              resolved.end, NULL, &g_blocks),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_parse_data_create(g_blocks), AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_block_parse(g_blocks, &resolved, &g_scratch),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_parse((const char *)g_buffer, g_length, &config,
                                    &g_doc),
                        AMARK_ERROR_NONE);
}

/* Requires the ranges a and b of the two documents to be equal. */
static void check_same_range(bool has_a, amark_range_t a, bool has_b,
                             amark_range_t b)
{
    NAOS_CHECK_INT_EQ(has_a, has_b);
    if (has_a && has_b) {
        NAOS_CHECK_UINT_EQ(a.start, b.start);
        NAOS_CHECK_UINT_EQ(a.end, b.end);
    }
}

/* Requires the attributes of node to be the same in g_blocks and g_doc. */
static void check_same_attributes(amark_node_id_t node)
{
    amark_range_t a = {0, 0};
    amark_range_t b = {0, 0};
    uint32_t start_a = 0;
    uint32_t start_b = 0;
    bool has_a;
    bool has_b;

    NAOS_CHECK_UINT_EQ(amark_heading_level(g_doc, node),
                       amark_heading_level(g_blocks, node));
    NAOS_CHECK_INT_EQ(amark_heading_style(g_doc, node),
                      amark_heading_style(g_blocks, node));
    NAOS_CHECK_INT_EQ(amark_list_type(g_doc, node),
                      amark_list_type(g_blocks, node));
    NAOS_CHECK_INT_EQ(amark_list_spacing(g_doc, node),
                      amark_list_spacing(g_blocks, node));
    NAOS_CHECK_INT_EQ(amark_list_bullet_char(g_doc, node),
                      amark_list_bullet_char(g_blocks, node));
    NAOS_CHECK_INT_EQ(amark_list_delimiter(g_doc, node),
                      amark_list_delimiter(g_blocks, node));
    has_a = amark_list_start(g_blocks, node, &start_a);
    has_b = amark_list_start(g_doc, node, &start_b);
    NAOS_CHECK_INT_EQ(has_a, has_b);
    NAOS_CHECK_UINT_EQ(start_a, start_b);
    NAOS_CHECK_INT_EQ(amark_code_block_style(g_doc, node),
                      amark_code_block_style(g_blocks, node));
    NAOS_CHECK_INT_EQ(amark_code_block_fence_char(g_doc, node),
                      amark_code_block_fence_char(g_blocks, node));
    NAOS_CHECK_UINT_EQ(amark_code_block_fence_length(g_doc, node),
                       amark_code_block_fence_length(g_blocks, node));
    has_a = amark_code_block_info(g_blocks, node, &a);
    has_b = amark_code_block_info(g_doc, node, &b);
    check_same_range(has_a, a, has_b, b);
    has_a = amark_definition_label(g_blocks, node, &a);
    has_b = amark_definition_label(g_doc, node, &b);
    check_same_range(has_a, a, has_b, b);
    has_a = amark_definition_destination(g_blocks, node, &a);
    has_b = amark_definition_destination(g_doc, node, &b);
    check_same_range(has_a, a, has_b, b);
    has_a = amark_definition_title(g_blocks, node, &a);
    has_b = amark_definition_title(g_doc, node, &b);
    check_same_range(has_a, a, has_b, b);
}

/*
 * Node IDs run from 1 to the node count. Requires every block node of
 * g_blocks to be in g_doc with the same ID,
 * kind, range, lines, attributes, parent and next sibling, and the same
 * first child, except that a paragraph or heading of g_doc may have an
 * inline node as its first child. A node removed in g_blocks (no range) is
 * removed in g_doc too. Every other node of g_doc must be an
 * inline node whose parent is a paragraph or heading. Both trees pass the
 * tree check and give the same block structure.
 */
static void check_blocks_unchanged(void)
{
    size_t blocks = amark_document_node_count(g_blocks);
    size_t count = amark_document_node_count(g_doc);
    amark_node_id_t node;
    char want[DUMP_SIZE];

    NAOS_CHECK_TRUE(tree_ok());
    NAOS_CHECK_INT_EQ(amark_tree_check(g_blocks).violation,
                      AMARK_TREE_VIOLATION_NONE);
    NAOS_REQUIRE_UINT_LT(amark_test_dump_blocks(g_blocks, want, sizeof(want)),
                         sizeof(want));
    NAOS_REQUIRE_UINT_LT(amark_test_dump_blocks(g_doc, g_dump,
                                                sizeof(g_dump)),
                         sizeof(g_dump));
    NAOS_CHECK_STR_EQ(g_dump, want);
    NAOS_REQUIRE_UINT_GE(count, blocks);
    for (node = 1; node <= blocks; node++) {
        amark_kind_t kind = amark_node_kind(g_blocks, node);
        amark_node_id_t child = amark_node_first_child(g_doc, node);
        amark_range_t a;
        amark_range_t b;

        NAOS_TRACE_PUSH("node %lu", (unsigned long)node);
        NAOS_CHECK_UINT_EQ(amark_node_kind(g_doc, node), kind);
        if (!amark_node_range(g_blocks, node, &a)) {
            NAOS_CHECK_FALSE(amark_node_range(g_doc, node, &b));
            NAOS_TRACE_POP();
            continue;
        }
        NAOS_CHECK_TRUE(amark_node_is_block(g_doc, node));
        NAOS_REQUIRE_TRUE(amark_node_range(g_doc, node, &b));
        NAOS_CHECK_UINT_EQ(b.start, a.start);
        NAOS_CHECK_UINT_EQ(b.end, a.end);
        NAOS_CHECK_UINT_EQ(amark_node_start_line(g_doc, node),
                           amark_node_start_line(g_blocks, node));
        NAOS_CHECK_UINT_EQ(amark_node_end_line(g_doc, node),
                           amark_node_end_line(g_blocks, node));
        NAOS_CHECK_UINT_EQ(amark_node_parent(g_doc, node),
                           amark_node_parent(g_blocks, node));
        NAOS_CHECK_UINT_EQ(amark_node_next_sibling(g_doc, node),
                           amark_node_next_sibling(g_blocks, node));
        if (kind == AMARK_KIND_PARAGRAPH || kind == AMARK_KIND_HEADING) {
            NAOS_CHECK_UINT_EQ(amark_node_first_child(g_blocks, node),
                               AMARK_NODE_NONE);
            NAOS_CHECK_TRUE(child == AMARK_NODE_NONE || child > blocks);
        } else {
            NAOS_CHECK_UINT_EQ(child, amark_node_first_child(g_blocks, node));
        }
        check_same_attributes(node);
        NAOS_TRACE_POP();
    }
    for (node = (amark_node_id_t)blocks + 1; node <= count; node++) {
        amark_node_id_t parent = amark_node_parent(g_doc, node);

        NAOS_TRACE_PUSH("node %lu", (unsigned long)node);
        NAOS_CHECK_TRUE(amark_node_is_inline(g_doc, node));
        while (parent != AMARK_NODE_NONE &&
               amark_node_is_inline(g_doc, parent)) {
            parent = amark_node_parent(g_doc, parent);
        }
        NAOS_REQUIRE_UINT_NE(parent, AMARK_NODE_NONE);
        NAOS_REQUIRE_UINT_LE(parent, blocks);
        NAOS_CHECK_TRUE(amark_node_kind(g_doc, parent) ==
                            AMARK_KIND_PARAGRAPH ||
                        amark_node_kind(g_doc, parent) ==
                            AMARK_KIND_HEADING);
        NAOS_TRACE_POP();
    }
}

NAOS_TEST(amark_parse_keeps_the_block_nodes)
{
    /*
     * Every kind of block, with special bytes, line breaks and the forms of
     * inline structures in the paragraphs and headings.
     */
    static const char input[] =
        "# ATX *heading* #\n"
        "Setext `heading`  \n"
        "with two lines\n"
        "---\n"
        "\n"
        "***\n"
        "\n"
        "    indented *code*\n"
        "\n"
        "```c info\n"
        "fenced [code]\n"
        "```\n"
        "\n"
        "<div>\n"
        "html *block*\n"
        "</div>\n"
        "\n"
        "[foo]: /url \"title\"\n"
        "[bar]:\n"
        "  /bar 'x'\n"
        "\n"
        "> quoted [foo]  \n"
        "lazy <b>\r\n"
        "> - item &amp; \\\n"
        ">   more\n"
        "\n"
        "- tight a\\\n"
        "- b\n"
        "  3) nested ![i](u)\n"
        "\n"
        "1. loose\n"
        "\n"
        "2. two\n"
        "\n"
        "para <http://x> **strong** _em_\r"
        "end";

    parse_both(input, sizeof(input) - 1);
    check_blocks_unchanged();
    NAOS_CHECK_UINT_GT(amark_document_node_count(g_doc),
                       amark_document_node_count(g_blocks));
}

NAOS_TEST(amark_parse_keeps_the_blocks_of_an_empty_heading)
{
    /* Contents of length 0 and documents with no content at all. */
    parse_both("#\n\n## ##\n", 9);
    check_blocks_unchanged();
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc),
                       amark_document_node_count(g_blocks));
    amark_document_destroy(g_doc);
    g_doc = NULL;
    amark_document_destroy(g_blocks);
    g_blocks = NULL;

    parse_both("", 0);
    check_blocks_unchanged();
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 1);
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

NAOS_SUITE(escape,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(a_backslash_before_ascii_punctuation_is_an_escape),
    NAOS_ADD_TEST(every_ascii_punctuation_character_can_be_escaped),
    NAOS_ADD_TEST(a_backslash_before_anything_else_is_text),
    NAOS_ADD_TEST(a_backslash_before_a_line_ending_is_a_hard_break)
)

NAOS_SUITE(reference,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(character_references_are_one_node),
    NAOS_ADD_TEST(an_ampersand_that_starts_no_reference_is_text)
)

NAOS_SUITE(code_span,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(a_code_span_runs_from_the_opening_to_the_closing_string),
    NAOS_ADD_TEST(an_unmatched_backtick_string_is_text_as_a_whole),
    NAOS_ADD_TEST(a_code_span_crosses_lines_without_breaks),
    NAOS_ADD_TEST(an_escaped_backtick_starts_a_new_string)
)

NAOS_SUITE(autolink,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(autolinks_are_one_node_with_their_type),
    NAOS_ADD_TEST(a_less_than_sign_that_starts_nothing_is_text)
)

NAOS_SUITE(raw_html,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(raw_html_is_one_node),
    NAOS_ADD_TEST(raw_html_crosses_lines_without_breaks),
    NAOS_ADD_TEST(html_that_does_not_close_is_text)
)

NAOS_SUITE(precedence,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(the_structure_that_starts_first_wins),
    NAOS_ADD_TEST(special_bytes_inside_a_structure_start_nothing),
    NAOS_ADD_TEST(an_escaped_special_byte_starts_nothing)
)

NAOS_SUITE(emphasis,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(emphasis_and_strong_emphasis_wrap_their_children),
    NAOS_ADD_TEST(nested_emphasis_and_other_items_inside),
    NAOS_ADD_TEST(part_of_a_delimiter_run_is_used),
    NAOS_ADD_TEST(unmatched_delimiters_are_text),
    NAOS_ADD_TEST(emphasis_spans_lines_and_holds_their_breaks),
    NAOS_ADD_TEST(emphasis_across_lines_of_a_block_quote),
    NAOS_ADD_TEST(the_ends_of_the_content_are_white_space),
    NAOS_ADD_TEST(ill_formed_utf8_is_punctuation_around_delimiters),
    NAOS_ADD_TEST(nul_is_punctuation_around_delimiters_in_both_modes),
    NAOS_ADD_TEST(well_formed_input_gives_the_same_tree_in_both_modes)
)

NAOS_SUITE(dialect,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(the_dialect_does_not_change_the_inline_result)
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

NAOS_SUITE(entry,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(amark_parse_keeps_the_block_nodes),
    NAOS_ADD_TEST(amark_parse_keeps_the_blocks_of_an_empty_heading)
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
    NAOS_ADD_SUITE(escape),
    NAOS_ADD_SUITE(reference),
    NAOS_ADD_SUITE(code_span),
    NAOS_ADD_SUITE(autolink),
    NAOS_ADD_SUITE(raw_html),
    NAOS_ADD_SUITE(precedence),
    NAOS_ADD_SUITE(emphasis),
    NAOS_ADD_SUITE(dialect),
    NAOS_ADD_SUITE(content),
    NAOS_ADD_SUITE(entry),
    NAOS_ADD_SUITE(limit)
)
