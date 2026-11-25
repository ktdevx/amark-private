/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of the brackets (src/bracket.h), through the public entry
 * amark_parse: inline links and images with their form, destination and
 * title (absent and empty ones included) and their ranges, brackets that
 * make nothing (no opener, no part in parentheses, a part that does not
 * match, a '!' not followed by '[') as text, a link inside a link (the inner
 * one is the link and the outer brackets are text), images inside links,
 * links and images inside images, the link floor that a later bracket does
 * not bring back ("[a [b](c) [d] e](f)"), delimiters inside the link text
 * that never match those outside ("**x [a*b**c*](d)"), line endings inside
 * a title that make no line break, line breaks inside the link text as
 * children of the link, brackets inside the link text, the precedence of a
 * code span over the brackets, and a bracket stack that starts empty for
 * every content. The reference forms: the full, collapsed and shortcut forms
 * of links and images with their definition, the first of the definitions
 * with the same label, definitions after the link and inside containers,
 * labels without a definition, labels of 999 and 1000 characters, case
 * folding, labels across lines, labels compared as written, labels with
 * ill-formed UTF-8 (replaced) and U+0000 that match and resolve (0xFF and
 * 0xFE both read as U+FFFD, so they match each other), and the order of
 * the forms ("[a](b)" with a definition of a, "[a][b]" without one of b,
 * "[a][]", "[a] []" and "[a][ ]"). Every tree passes the tree check and has
 * no link inside a link.
 */

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
 * Fixture. NAOS_REQUIRE_* leaves a test with longjmp, so the document is
 * kept in a static variable, and teardown destroys it and checks that no
 * block is left.
 */
#define DUMP_SIZE 2048

static amark_test_alloc_t g_state;
static amark_allocator_t g_allocator;
static amark_document_t *g_doc;
static char g_dump[DUMP_SIZE];

NAOS_SETUP(counting)
{
    amark_test_alloc_init(&g_state);
    g_allocator = amark_test_allocator(&g_state);
    g_doc = NULL;
}

NAOS_TEARDOWN(counting)
{
    amark_document_destroy(g_doc);
    g_doc = NULL;
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
    NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
}

/*
 * Returns true if no link of g_doc has a link among its ancestors. Walks
 * the whole tree in document order without recursion.
 */
static bool no_link_inside_a_link(void)
{
    amark_node_id_t node = amark_document_root(g_doc);
    amark_node_id_t up;

    while (node != AMARK_NODE_NONE) {
        if (amark_node_kind(g_doc, node) == AMARK_KIND_LINK) {
            for (up = amark_node_parent(g_doc, node); up != AMARK_NODE_NONE;
                 up = amark_node_parent(g_doc, up)) {
                if (amark_node_kind(g_doc, up) == AMARK_KIND_LINK) {
                    return false;
                }
            }
        }
        if (amark_node_first_child(g_doc, node) != AMARK_NODE_NONE) {
            node = amark_node_first_child(g_doc, node);
            continue;
        }
        while (node != AMARK_NODE_NONE &&
               amark_node_next_sibling(g_doc, node) == AMARK_NODE_NONE) {
            node = amark_node_parent(g_doc, node);
        }
        if (node != AMARK_NODE_NONE) {
            node = amark_node_next_sibling(g_doc, node);
        }
    }
    return true;
}

/*
 * Parses length bytes through amark_parse with utf8_mode and without
 * limits, requires success, a tree that passes the tree check and no link
 * inside a link. The document of an earlier parse in the same test is
 * destroyed first.
 */
static void parse_with_mode(const char *bytes, size_t length,
                            amark_utf8_mode_t utf8_mode)
{
    amark_config_t config;
    amark_tree_check_result_t check;

    amark_document_destroy(g_doc);
    g_doc = NULL;
    AMARK_CONFIG_INIT(&config);
    config.allocator = g_allocator;
    config.utf8_mode = (int)utf8_mode;
    NAOS_REQUIRE_INT_EQ(amark_parse(bytes, length, &config, &g_doc),
                        AMARK_ERROR_NONE);
    check = amark_tree_check(g_doc);
    NAOS_CHECK_INT_EQ(check.violation, AMARK_TREE_VIOLATION_NONE);
    NAOS_CHECK_UINT_EQ(check.node, AMARK_NODE_NONE);
    NAOS_CHECK_TRUE(no_link_inside_a_link());
}

/* Parses length bytes with the default configuration (strict UTF-8). */
static void parse_bytes(const char *bytes, size_t length)
{
    parse_with_mode(bytes, length, AMARK_UTF8_STRICT);
}

#define PARSE(s) parse_bytes((s), sizeof(s) - 1)

/* The whole tree of g_doc as one line. */
static const char *dump(void)
{
    size_t length = amark_test_dump_tree(g_doc, g_dump, sizeof(g_dump));

    NAOS_REQUIRE_UINT_LT(length, sizeof(g_dump));
    return g_dump;
}

/* Parses s and checks its whole tree against expected. */
#define CHECK_TREE(s, expected)                                             \
    do {                                                                    \
        PARSE(s);                                                           \
        NAOS_CHECK_STR_EQ(dump(), (expected));                              \
    } while (0)

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

/* The first paragraph: the first child of the root. */
static amark_node_id_t paragraph(void)
{
    return child_at(amark_document_root(g_doc), 0);
}

/* Checks the kind, the range and the lines of node. */
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

/* Checks that node has a destination with the range [start, end). */
static void check_destination(amark_node_id_t node, size_t start, size_t end)
{
    amark_range_t range;

    NAOS_REQUIRE_TRUE(amark_link_destination(g_doc, node, &range));
    NAOS_CHECK_UINT_EQ(range.start, start);
    NAOS_CHECK_UINT_EQ(range.end, end);
}

/* Checks that node has a title with the range [start, end). */
static void check_title(amark_node_id_t node, size_t start, size_t end)
{
    amark_range_t range;

    NAOS_REQUIRE_TRUE(amark_link_title(g_doc, node, &range));
    NAOS_CHECK_UINT_EQ(range.start, start);
    NAOS_CHECK_UINT_EQ(range.end, end);
}

/* ---- Inline links and images ---- */

NAOS_TEST(an_inline_link_with_a_destination_and_a_title)
{
    amark_node_id_t link;

    CHECK_TREE("[a](/u \"t\")",
               "(doc (p (link inline dest=\"/u\" title=\"t\" (text \"a\"))))");
    link = child_at(paragraph(), 0);
    NAOS_CHECK_UINT_EQ(child_count(paragraph()), 1);
    check_node(link, AMARK_KIND_LINK, 0, 11, 1, 1);
    NAOS_CHECK_TRUE(amark_node_is_inline(g_doc, link));
    NAOS_CHECK_INT_EQ(amark_link_form(g_doc, link), AMARK_LINK_INLINE);
    check_destination(link, 4, 6);
    check_title(link, 8, 9);
    NAOS_CHECK_UINT_EQ(amark_link_definition(g_doc, link), AMARK_NODE_NONE);
    NAOS_CHECK_UINT_EQ(child_count(link), 1);
    check_node(child_at(link, 0), AMARK_KIND_TEXT, 1, 2, 1, 1);
}

NAOS_TEST(absent_and_empty_destinations_and_titles)
{
    amark_node_id_t link;
    amark_range_t range;

    /* Nothing inside the parentheses: neither is written. */
    CHECK_TREE("[a]()", "(doc (p (link inline (text \"a\"))))");
    link = child_at(paragraph(), 0);
    check_node(link, AMARK_KIND_LINK, 0, 5, 1, 1);
    NAOS_CHECK_INT_EQ(amark_link_form(g_doc, link), AMARK_LINK_INLINE);
    NAOS_CHECK_FALSE(amark_link_destination(g_doc, link, &range));
    NAOS_CHECK_FALSE(amark_link_title(g_doc, link, &range));

    /* Only white space inside the parentheses. */
    CHECK_TREE("[a]( )", "(doc (p (link inline (text \"a\"))))");
    link = child_at(paragraph(), 0);
    check_node(link, AMARK_KIND_LINK, 0, 6, 1, 1);
    NAOS_CHECK_FALSE(amark_link_destination(g_doc, link, &range));
    NAOS_CHECK_FALSE(amark_link_title(g_doc, link, &range));

    /* "<>": a destination that is written and empty, inside the brackets. */
    CHECK_TREE("[a](<>)", "(doc (p (link inline dest=\"\" (text \"a\"))))");
    link = child_at(paragraph(), 0);
    check_node(link, AMARK_KIND_LINK, 0, 7, 1, 1);
    check_destination(link, 5, 5);
    NAOS_CHECK_FALSE(amark_link_title(g_doc, link, &range));

    /* An empty title. */
    CHECK_TREE("[a](b \"\")",
               "(doc (p (link inline dest=\"b\" title=\"\" (text \"a\"))))");
    link = child_at(paragraph(), 0);
    check_node(link, AMARK_KIND_LINK, 0, 9, 1, 1);
    check_destination(link, 4, 5);
    check_title(link, 7, 7);

    /* A destination in pointy brackets holds a space; a title in single
     * quotes. */
    CHECK_TREE("[a](<b c> 't')",
               "(doc (p (link inline dest=\"b c\" title=\"t\" "
               "(text \"a\"))))");
    link = child_at(paragraph(), 0);
    check_node(link, AMARK_KIND_LINK, 0, 14, 1, 1);
    check_destination(link, 5, 8);
    check_title(link, 11, 12);

    /* A title in parentheses, and white space around the parts. */
    CHECK_TREE("[a]( b (t) )",
               "(doc (p (link inline dest=\"b\" title=\"t\" (text \"a\"))))");
    link = child_at(paragraph(), 0);
    check_node(link, AMARK_KIND_LINK, 0, 12, 1, 1);
    check_destination(link, 5, 6);
    check_title(link, 8, 9);

    /* An empty link text. */
    CHECK_TREE("[](b)", "(doc (p (link inline dest=\"b\")))");
    link = child_at(paragraph(), 0);
    check_node(link, AMARK_KIND_LINK, 0, 5, 1, 1);
    NAOS_CHECK_UINT_EQ(child_count(link), 0);
}

NAOS_TEST(an_inline_image_starts_at_the_exclamation_mark)
{
    amark_node_id_t image;
    amark_range_t range;

    CHECK_TREE("x![a](b)",
               "(doc (p (text \"x\") (image inline dest=\"b\" "
               "(text \"a\"))))");
    NAOS_CHECK_UINT_EQ(child_count(paragraph()), 2);
    check_node(child_at(paragraph(), 0), AMARK_KIND_TEXT, 0, 1, 1, 1);
    image = child_at(paragraph(), 1);
    check_node(image, AMARK_KIND_IMAGE, 1, 8, 1, 1);
    NAOS_CHECK_TRUE(amark_node_is_inline(g_doc, image));
    NAOS_CHECK_INT_EQ(amark_link_form(g_doc, image), AMARK_LINK_INLINE);
    check_destination(image, 6, 7);
    NAOS_CHECK_FALSE(amark_link_title(g_doc, image, &range));
    NAOS_CHECK_UINT_EQ(amark_link_definition(g_doc, image), AMARK_NODE_NONE);
    check_node(child_at(image, 0), AMARK_KIND_TEXT, 3, 4, 1, 1);

    /* A '!' before the image's '!' is text. */
    CHECK_TREE("!![a](b \"t\")",
               "(doc (p (text \"!\") (image inline dest=\"b\" title=\"t\" "
               "(text \"a\"))))");
    check_node(child_at(paragraph(), 1), AMARK_KIND_IMAGE, 1, 12, 1, 1);
    check_title(child_at(paragraph(), 1), 9, 10);

    /* An image with no description and no destination. */
    CHECK_TREE("![]()", "(doc (p (image inline)))");
    image = child_at(paragraph(), 0);
    check_node(image, AMARK_KIND_IMAGE, 0, 5, 1, 1);
    NAOS_CHECK_FALSE(amark_link_destination(g_doc, image, &range));
}

/* ---- Brackets that make nothing ---- */

NAOS_TEST(brackets_without_a_matching_part_are_text)
{
    /* No part in parentheses, nothing after the bracket, a closing bracket
     * without an opener. */
    CHECK_TREE("[a] b", "(doc (p (text \"[a] b\")))");
    CHECK_TREE("[a]", "(doc (p (text \"[a]\")))");
    CHECK_TREE("a]b", "(doc (p (text \"a]b\")))");
    CHECK_TREE("a]](b)", "(doc (p (text \"a]](b)\")))");
    /* A part in parentheses that does not match. */
    CHECK_TREE("[a](b", "(doc (p (text \"[a](b\")))");
    CHECK_TREE("[a](b c)", "(doc (p (text \"[a](b c)\")))");
    CHECK_TREE("[a](<b)", "(doc (p (text \"[a](<b)\")))");
    CHECK_TREE("[a](b \"t)", "(doc (p (text \"[a](b \\\"t)\")))");
    /* White space between the bracket and the parenthesis. */
    CHECK_TREE("[a] (b)", "(doc (p (text \"[a] (b)\")))");
    /* The text after the brackets stays in the same content. */
    CHECK_TREE("[a] [b](c)",
               "(doc (p (text \"[a] \") (link inline dest=\"c\" "
               "(text \"b\"))))");
    check_node(child_at(paragraph(), 0), AMARK_KIND_TEXT, 0, 4, 1, 1);
    check_node(child_at(paragraph(), 1), AMARK_KIND_LINK, 4, 10, 1, 1);
}

NAOS_TEST(an_exclamation_mark_not_followed_by_a_bracket_is_text)
{
    CHECK_TREE("a ! b", "(doc (p (text \"a ! b\")))");
    CHECK_TREE("!a", "(doc (p (text \"!a\")))");
    CHECK_TREE("a!", "(doc (p (text \"a!\")))");
    CHECK_TREE("! [a](b)",
               "(doc (p (text \"! \") (link inline dest=\"b\" "
               "(text \"a\"))))");
    /* A '!' at the end of a line, before a '[' on the next line. */
    CHECK_TREE("a!\n[b](c)",
               "(doc (p (text \"a!\") (sbr) (link inline dest=\"c\" "
               "(text \"b\"))))");
    check_node(child_at(paragraph(), 2), AMARK_KIND_LINK, 3, 9, 2, 2);
    /* An image opener that makes nothing: both bytes are text. */
    CHECK_TREE("![a] b", "(doc (p (text \"![a] b\")))");
    CHECK_TREE("![a](b", "(doc (p (text \"![a](b\")))");
    CHECK_TREE("!\\[a](b)", "(doc (p (text \"!\") (esc \"\\\\[\") "
                            "(text \"a](b)\")))");
}

/* ---- Nesting ---- */

NAOS_TEST(a_link_inside_a_link_leaves_the_outer_brackets_as_text)
{
    amark_node_id_t link;

    CHECK_TREE("[a [b](c) d](e)",
               "(doc (p (text \"[a \") (link inline dest=\"c\" "
               "(text \"b\")) (text \" d](e)\")))");
    NAOS_CHECK_UINT_EQ(child_count(paragraph()), 3);
    check_node(child_at(paragraph(), 0), AMARK_KIND_TEXT, 0, 3, 1, 1);
    link = child_at(paragraph(), 1);
    check_node(link, AMARK_KIND_LINK, 3, 9, 1, 1);
    check_destination(link, 7, 8);
    check_node(child_at(paragraph(), 2), AMARK_KIND_TEXT, 9, 15, 1, 1);

    CHECK_TREE("[[a](b)](c)",
               "(doc (p (text \"[\") (link inline dest=\"b\" "
               "(text \"a\")) (text \"](c)\")))");
    /* Two levels: only the innermost is a link. */
    CHECK_TREE("[[[a](b)](c)](d)",
               "(doc (p (text \"[[\") (link inline dest=\"b\" "
               "(text \"a\")) (text \"](c)](d)\")))");
}

NAOS_TEST(images_inside_links_and_links_and_images_inside_images)
{
    amark_node_id_t outer;
    amark_node_id_t inner;

    /* An image inside a link. */
    CHECK_TREE("[![a](b)](c)",
               "(doc (p (link inline dest=\"c\" (image inline dest=\"b\" "
               "(text \"a\")))))");
    outer = child_at(paragraph(), 0);
    check_node(outer, AMARK_KIND_LINK, 0, 12, 1, 1);
    check_destination(outer, 10, 11);
    inner = child_at(outer, 0);
    check_node(inner, AMARK_KIND_IMAGE, 1, 8, 1, 1);
    check_destination(inner, 6, 7);
    check_node(child_at(inner, 0), AMARK_KIND_TEXT, 3, 4, 1, 1);

    /* A link inside an image: the image is made all the same. */
    CHECK_TREE("![[a](b)](c)",
               "(doc (p (image inline dest=\"c\" (link inline dest=\"b\" "
               "(text \"a\")))))");
    outer = child_at(paragraph(), 0);
    check_node(outer, AMARK_KIND_IMAGE, 0, 12, 1, 1);
    check_node(child_at(outer, 0), AMARK_KIND_LINK, 2, 8, 1, 1);

    /* An image inside an image. */
    CHECK_TREE("![![a](b)](c)",
               "(doc (p (image inline dest=\"c\" (image inline dest=\"b\" "
               "(text \"a\")))))");
    check_node(child_at(paragraph(), 0), AMARK_KIND_IMAGE, 0, 13, 1, 1);
    check_node(child_at(child_at(paragraph(), 0), 0), AMARK_KIND_IMAGE, 2, 9,
               1, 1);

    /* A link inside an image inside a link: the outer link is text. */
    CHECK_TREE("[![[a](b)](c)](d)",
               "(doc (p (text \"[\") (image inline dest=\"c\" "
               "(link inline dest=\"b\" (text \"a\"))) (text \"](d)\")))");

    /* A link inside brackets inside an image: the brackets between are
     * text, and the image is made. */
    CHECK_TREE("![[[a](b)](c)](d)",
               "(doc (p (image inline dest=\"d\" (text \"[\") "
               "(link inline dest=\"b\" (text \"a\")) (text \"](c)\"))))");
}

NAOS_TEST(the_link_floor_does_not_come_back_with_a_later_bracket)
{
    /* After [b](c), the outer '[' stays below the floor even though [d]
     * is pushed and taken off above it. */
    CHECK_TREE("[a [b](c) [d] e](f)",
               "(doc (p (text \"[a \") (link inline dest=\"c\" "
               "(text \"b\")) (text \" [d] e](f)\")))");
    check_node(child_at(paragraph(), 1), AMARK_KIND_LINK, 3, 9, 1, 1);
    check_node(child_at(paragraph(), 2), AMARK_KIND_TEXT, 9, 19, 1, 1);

    /* A bracket pushed after a link is above the floor and can still make a
     * link. */
    CHECK_TREE("[a [b](c) [d](e)",
               "(doc (p (text \"[a \") (link inline dest=\"c\" "
               "(text \"b\")) (text \" \") (link inline dest=\"e\" "
               "(text \"d\"))))");

    /* Taking an inactive bracket off lowers the floor to the height of the
     * stack, so a bracket pushed afterwards is active again. */
    CHECK_TREE("[[a](b)] [c](d)",
               "(doc (p (text \"[\") (link inline dest=\"b\" (text \"a\")) "
               "(text \"] \") (link inline dest=\"d\" (text \"c\"))))");
    /* The same after an image: its bracket is below the floor the inner link
     * set but still active, and taking it off lowers the floor too. */
    CHECK_TREE("[![[a](b)](c)] [e](f)",
               "(doc (p (text \"[\") (image inline dest=\"c\" "
               "(link inline dest=\"b\" (text \"a\"))) (text \"] \") "
               "(link inline dest=\"f\" (text \"e\"))))");

    /* A link closes the brackets of the earlier contents' links only within
     * its content: each content starts with an empty stack. */
    CHECK_TREE("[a\n\n](b)",
               "(doc (p (text \"[a\")) (p (text \"](b)\")))");
    CHECK_TREE("[a](b)\n\n[c](d)",
               "(doc (p (link inline dest=\"b\" (text \"a\"))) "
               "(p (link inline dest=\"d\" (text \"c\"))))");
}

NAOS_TEST(brackets_inside_the_link_text)
{
    /* Matched brackets inside the link text are allowed. */
    CHECK_TREE("[a [b] c](d)",
               "(doc (p (link inline dest=\"d\" (text \"a [b] c\"))))");
    CHECK_TREE("[a [b [c]] d](e)",
               "(doc (p (link inline dest=\"e\" (text \"a [b [c]] d\"))))");
    /* An escaped bracket does not count. */
    CHECK_TREE("[a \\] b](c)",
               "(doc (p (link inline dest=\"c\" (text \"a \") "
               "(esc \"\\\\]\") (text \" b\"))))");
    /* An unmatched closing bracket ends the link text. */
    CHECK_TREE("[a]b](c)", "(doc (p (text \"[a]b](c)\")))");
}

/* ---- Precedence ---- */

NAOS_TEST(delimiters_inside_and_outside_the_link_text_do_not_match)
{
    amark_node_id_t link;
    amark_node_id_t em;

    CHECK_TREE("**x [a*b**c*](d)",
               "(doc (p (text \"**x \") (link inline dest=\"d\" "
               "(text \"a\") (em (text \"b**c\")))))");
    link = child_at(paragraph(), 1);
    check_node(link, AMARK_KIND_LINK, 4, 16, 1, 1);
    em = child_at(link, 1);
    check_node(em, AMARK_KIND_EMPHASIS, 6, 12, 1, 1);

    /* An opener outside and a closer inside. */
    CHECK_TREE("*[a*](b)",
               "(doc (p (text \"*\") (link inline dest=\"b\" "
               "(text \"a*\"))))");
    /* An opener inside and a closer outside. */
    CHECK_TREE("[*a](b)*",
               "(doc (p (link inline dest=\"b\" (text \"*a\")) "
               "(text \"*\")))");
    /* Emphasis inside the link text, and around the whole link. */
    CHECK_TREE("[*a*](b)",
               "(doc (p (link inline dest=\"b\" (em (text \"a\")))))");
    CHECK_TREE("*[a](b)*",
               "(doc (p (em (link inline dest=\"b\" (text \"a\")))))");
    check_node(child_at(paragraph(), 0), AMARK_KIND_EMPHASIS, 0, 8, 1, 1);
    /* Delimiters inside brackets that make nothing still match outside. */
    CHECK_TREE("*[a*] b",
               "(doc (p (em (text \"[a\")) (text \"] b\")))");
}

NAOS_TEST(a_code_span_binds_tighter_than_the_brackets)
{
    /* Example 524 of the specification. */
    CHECK_TREE("[a`](b)`",
               "(doc (p (text \"[a\") (code \"`](b)`\")))");
    /* A closing bracket after a code span inside the link text. */
    CHECK_TREE("[a`]`](b)",
               "(doc (p (link inline dest=\"b\" (text \"a\") "
               "(code \"`]`\"))))");
    /* An autolink in the link text. */
    CHECK_TREE("[<http://x]>](b)",
               "(doc (p (link inline dest=\"b\" "
               "(autolink uri \"<http://x]>\"))))");
}

/* ---- Lines ---- */

NAOS_TEST(line_endings_inside_a_title_make_no_break)
{
    amark_node_id_t link;

    CHECK_TREE("[a](b \"c\nd\")",
               "(doc (p (link inline dest=\"b\" title=\"c\\nd\" "
               "(text \"a\"))))");
    NAOS_CHECK_UINT_EQ(child_count(paragraph()), 1);
    link = child_at(paragraph(), 0);
    check_node(link, AMARK_KIND_LINK, 0, 12, 1, 2);
    NAOS_CHECK_UINT_EQ(child_count(link), 1);
    check_destination(link, 4, 5);
    check_title(link, 7, 10);

    /* Line endings around the destination, with CR LF. */
    CHECK_TREE("[a](\r\nb\r\n'c')",
               "(doc (p (link inline dest=\"b\" title=\"c\" "
               "(text \"a\"))))");
    link = child_at(paragraph(), 0);
    check_node(link, AMARK_KIND_LINK, 0, 13, 1, 3);
    check_destination(link, 6, 7);
    check_title(link, 10, 11);
    NAOS_CHECK_UINT_EQ(child_count(link), 1);

    /* Inside a block quote, the range of a title across lines holds the
     * prefix of the next line (2.5). */
    PARSE("> [a](b \"c\n> d\")");
    link = child_at(child_at(child_at(amark_document_root(g_doc), 0), 0), 0);
    check_node(link, AMARK_KIND_LINK, 2, 16, 1, 2);
    check_destination(link, 6, 7);
    check_title(link, 9, 14);
    NAOS_CHECK_UINT_EQ(child_count(link), 1);
}

NAOS_TEST(line_breaks_inside_the_link_text_are_children_of_the_link)
{
    amark_node_id_t link;

    CHECK_TREE("[a\nb](c)",
               "(doc (p (link inline dest=\"c\" (text \"a\") (sbr) "
               "(text \"b\"))))");
    link = child_at(paragraph(), 0);
    check_node(link, AMARK_KIND_LINK, 0, 8, 1, 2);
    check_node(child_at(link, 0), AMARK_KIND_TEXT, 1, 2, 1, 1);
    check_node(child_at(link, 1), AMARK_KIND_SOFT_BREAK, 2, 3, 1, 1);
    check_node(child_at(link, 2), AMARK_KIND_TEXT, 3, 4, 2, 2);

    CHECK_TREE("![a  \nb\\\nc](d)",
               "(doc (p (image inline dest=\"d\" (text \"a\") (br) "
               "(text \"b\") (br) (text \"c\"))))");
    link = child_at(paragraph(), 0);
    check_node(link, AMARK_KIND_IMAGE, 0, 14, 1, 3);
    check_node(child_at(link, 1), AMARK_KIND_HARD_BREAK, 3, 6, 1, 1);
    check_node(child_at(link, 3), AMARK_KIND_HARD_BREAK, 7, 9, 2, 2);

    /* Inside a block quote, the prefix belongs to no item. */
    PARSE("> [a\n> b](c)");
    NAOS_CHECK_STR_EQ(dump(),
                      "(doc (bq (p (link inline dest=\"c\" (text \"a\") "
                      "(sbr) (text \"b\")))))");
    link = child_at(child_at(child_at(amark_document_root(g_doc), 0), 0), 0);
    check_node(link, AMARK_KIND_LINK, 2, 12, 1, 2);
    check_node(child_at(link, 1), AMARK_KIND_SOFT_BREAK, 4, 5, 1, 1);
    check_node(child_at(link, 2), AMARK_KIND_TEXT, 7, 8, 2, 2);
}

/* ---- Reference forms ---- */

/* The block at index (from 0) among the children of the root. */
static amark_node_id_t block_at(size_t index)
{
    return child_at(amark_document_root(g_doc), index);
}

/*
 * Checks that node, a link or an image, has the reference form form, the
 * definition def, and neither a destination nor a title.
 */
static void check_reference(amark_node_id_t node, amark_link_form_t form,
                            amark_node_id_t def)
{
    amark_range_t range;

    NAOS_CHECK_INT_EQ(amark_link_form(g_doc, node), form);
    NAOS_CHECK_UINT_EQ(amark_link_definition(g_doc, node), def);
    NAOS_CHECK_UINT_EQ(amark_node_kind(g_doc, def),
                       AMARK_KIND_LINK_REFERENCE_DEFINITION);
    NAOS_CHECK_FALSE(amark_link_destination(g_doc, node, &range));
    NAOS_CHECK_FALSE(amark_link_title(g_doc, node, &range));
}

NAOS_TEST(the_three_reference_forms_of_links)
{
    amark_node_id_t link;

    /* Full: from the '[' to right after the label's ']'. */
    CHECK_TREE("[x][a]\n\n[a]: /u",
               "(doc (p (link full def=\"a\" (text \"x\"))) (def))");
    link = child_at(paragraph(), 0);
    NAOS_CHECK_UINT_EQ(child_count(paragraph()), 1);
    check_node(link, AMARK_KIND_LINK, 0, 6, 1, 1);
    check_reference(link, AMARK_LINK_FULL, block_at(1));
    NAOS_CHECK_UINT_EQ(child_count(link), 1);
    check_node(child_at(link, 0), AMARK_KIND_TEXT, 1, 2, 1, 1);

    /* Collapsed: to right after the "[]". */
    CHECK_TREE("[a][]\n\n[a]: /u",
               "(doc (p (link collapsed def=\"a\" (text \"a\"))) (def))");
    link = child_at(paragraph(), 0);
    NAOS_CHECK_UINT_EQ(child_count(paragraph()), 1);
    check_node(link, AMARK_KIND_LINK, 0, 5, 1, 1);
    check_reference(link, AMARK_LINK_COLLAPSED, block_at(1));
    check_node(child_at(link, 0), AMARK_KIND_TEXT, 1, 2, 1, 1);

    /* Shortcut: to right after the ']'. */
    CHECK_TREE("[a] b\n\n[a]: /u",
               "(doc (p (link shortcut def=\"a\" (text \"a\")) "
               "(text \" b\")) (def))");
    link = child_at(paragraph(), 0);
    check_node(link, AMARK_KIND_LINK, 0, 3, 1, 1);
    check_reference(link, AMARK_LINK_SHORTCUT, block_at(1));
    check_node(child_at(paragraph(), 1), AMARK_KIND_TEXT, 3, 5, 1, 1);

    /* The inlines of the link text are children of the link (9.13). */
    CHECK_TREE("[*a* `b`][c]\n\n[c]: /u",
               "(doc (p (link full def=\"c\" (em (text \"a\")) (text \" \") "
               "(code \"`b`\"))) (def))");
    check_node(child_at(paragraph(), 0), AMARK_KIND_LINK, 0, 12, 1, 1);
}

NAOS_TEST(the_three_reference_forms_of_images)
{
    amark_node_id_t image;

    CHECK_TREE("![x][a]\n\n[a]: /u",
               "(doc (p (image full def=\"a\" (text \"x\"))) (def))");
    image = child_at(paragraph(), 0);
    check_node(image, AMARK_KIND_IMAGE, 0, 7, 1, 1);
    check_reference(image, AMARK_LINK_FULL, block_at(1));
    check_node(child_at(image, 0), AMARK_KIND_TEXT, 2, 3, 1, 1);

    CHECK_TREE("![a][]\n\n[a]: /u",
               "(doc (p (image collapsed def=\"a\" (text \"a\"))) (def))");
    image = child_at(paragraph(), 0);
    check_node(image, AMARK_KIND_IMAGE, 0, 6, 1, 1);
    check_reference(image, AMARK_LINK_COLLAPSED, block_at(1));

    CHECK_TREE("x![a]\n\n[a]: /u",
               "(doc (p (text \"x\") (image shortcut def=\"a\" "
               "(text \"a\"))) (def))");
    image = child_at(paragraph(), 1);
    check_node(image, AMARK_KIND_IMAGE, 1, 5, 1, 1);
    check_reference(image, AMARK_LINK_SHORTCUT, block_at(1));

    /* A reference link inside a reference image. */
    CHECK_TREE("![[a]][b]\n\n[a]: /1\n[b]: /2",
               "(doc (p (image full def=\"b\" (link shortcut def=\"a\" "
               "(text \"a\")))) (def) (def))");
    image = child_at(paragraph(), 0);
    check_node(image, AMARK_KIND_IMAGE, 0, 9, 1, 1);
    check_reference(image, AMARK_LINK_FULL, block_at(2));
    check_node(child_at(image, 0), AMARK_KIND_LINK, 2, 5, 1, 1);
    check_reference(child_at(image, 0), AMARK_LINK_SHORTCUT, block_at(1));

    /* No definition: both bytes of "![" are text. */
    CHECK_TREE("![b]\n\n[a]: /u", "(doc (p (text \"![b]\")) (def))");
}

/* ---- Resolving a reference ---- */

NAOS_TEST(the_first_of_the_definitions_with_the_same_label)
{
    CHECK_TREE("[A]: /1\n[a]: /2\n\n[a]",
               "(doc (def) (def) (p (link shortcut def=\"A\" "
               "(text \"a\"))))");
    check_reference(child_at(block_at(2), 0), AMARK_LINK_SHORTCUT,
                    block_at(0));
    /* The same when the link comes before both. */
    CHECK_TREE("[x][ a ]\n\n[a]: /1\n[ A]: /2",
               "(doc (p (link full def=\"a\" (text \"x\"))) (def) (def))");
    check_reference(child_at(paragraph(), 0), AMARK_LINK_FULL, block_at(1));
}

NAOS_TEST(definitions_anywhere_in_the_document)
{
    amark_node_id_t quote;

    /* After the link, inside a block quote. */
    CHECK_TREE("[a]\n\n> [a]: /u",
               "(doc (p (link shortcut def=\"a\" (text \"a\"))) "
               "(bq (def)))");
    quote = block_at(1);
    check_reference(child_at(paragraph(), 0), AMARK_LINK_SHORTCUT,
                    child_at(quote, 0));

    /* Inside a list item, and the link inside a block quote. */
    CHECK_TREE("> [x][a]\n\n- [a]: /u",
               "(doc (bq (p (link full def=\"a\" (text \"x\")))) "
               "(ul - tight (li (def))))");
    check_node(child_at(child_at(block_at(0), 0), 0), AMARK_KIND_LINK, 2, 8,
               1, 1);
    check_reference(child_at(child_at(block_at(0), 0), 0), AMARK_LINK_FULL,
                    child_at(child_at(block_at(1), 0), 0));
}

NAOS_TEST(a_label_without_a_definition)
{
    /* Shortcut and collapsed: the brackets are text. */
    CHECK_TREE("[b]\n\n[a]: /u", "(doc (p (text \"[b]\")) (def))");
    CHECK_TREE("[b][]\n\n[a]: /u", "(doc (p (text \"[b][]\")) (def))");
    /* Full: the brackets are text, and the label is no shortcut either. */
    CHECK_TREE("[x][b]\n\n[a]: /u", "(doc (p (text \"[x][b]\")) (def))");
    /* No definition at all in the document. */
    CHECK_TREE("[a] [b][c] [d][]", "(doc (p (text \"[a] [b][c] [d][]\")))");
}

/* Parses "[" + count characters 'a' + "]" + tail, then a blank line and a
 * definition with the same label. */
static void parse_long_label(size_t count, const char *tail)
{
    static char buffer[2 * 1000 + 64];
    size_t length = 0;
    size_t i;
    size_t tail_length = strlen(tail);

    buffer[length++] = '[';
    for (i = 0; i < count; i++) {
        buffer[length++] = 'a';
    }
    buffer[length++] = ']';
    memcpy(buffer + length, tail, tail_length);
    length += tail_length;
    memcpy(buffer + length, "\n\n[", 3);
    length += 3;
    for (i = 0; i < count; i++) {
        buffer[length++] = 'a';
    }
    memcpy(buffer + length, "]: /u", 5);
    length += 5;
    parse_bytes(buffer, length);
}

NAOS_TEST(labels_of_999_and_1000_characters)
{
    amark_node_id_t link;

    /* 999 characters: a definition, and a shortcut link to it. */
    parse_long_label(999, "");
    NAOS_REQUIRE_UINT_EQ(child_count(amark_document_root(g_doc)), 2);
    NAOS_CHECK_UINT_EQ(amark_node_kind(g_doc, block_at(1)),
                       AMARK_KIND_LINK_REFERENCE_DEFINITION);
    link = child_at(paragraph(), 0);
    NAOS_CHECK_UINT_EQ(child_count(paragraph()), 1);
    check_node(link, AMARK_KIND_LINK, 0, 1001, 1, 1);
    check_reference(link, AMARK_LINK_SHORTCUT, block_at(1));

    /* And a collapsed one. */
    parse_long_label(999, "[]");
    link = child_at(paragraph(), 0);
    check_node(link, AMARK_KIND_LINK, 0, 1003, 1, 1);
    check_reference(link, AMARK_LINK_COLLAPSED, block_at(1));

    /* 1000 characters: no definition and no link. */
    parse_long_label(1000, "");
    NAOS_REQUIRE_UINT_EQ(child_count(amark_document_root(g_doc)), 2);
    NAOS_CHECK_UINT_EQ(amark_node_kind(g_doc, block_at(1)),
                       AMARK_KIND_PARAGRAPH);
    NAOS_CHECK_UINT_EQ(child_count(paragraph()), 1);
    check_node(child_at(paragraph(), 0), AMARK_KIND_TEXT, 0, 1002, 1, 1);
}

NAOS_TEST(a_label_of_1000_characters_after_the_text_is_no_label)
{
    static char buffer[1000 + 64];
    const char *head = "[a][";
    const char *tail = "]\n\n[a]: /u";
    size_t length = strlen(head);
    size_t i;

    /* The full reference does not match, so the text is a shortcut. */
    memcpy(buffer, head, length);
    for (i = 0; i < 1000; i++) {
        buffer[length++] = 'b';
    }
    memcpy(buffer + length, tail, strlen(tail));
    length += strlen(tail);
    parse_bytes(buffer, length);
    check_node(child_at(paragraph(), 0), AMARK_KIND_LINK, 0, 3, 1, 1);
    check_reference(child_at(paragraph(), 0), AMARK_LINK_SHORTCUT,
                    block_at(1));
    check_node(child_at(paragraph(), 1), AMARK_KIND_TEXT, 3, 1005, 1, 1);
}

NAOS_TEST(labels_are_compared_after_case_folding_and_white_space)
{
    /* Example 540 of the specification: U+1E9E folds to "ss". */
    CHECK_TREE("[\xE1\xBA\x9E]\n\n[SS]: /u",
               "(doc (p (link shortcut def=\"SS\" "
               "(text \"\\xE1\\xBA\\x9E\"))) (def))");
    CHECK_TREE("[x][Foo  \tBAR]\n\n[foo bar]: /u",
               "(doc (p (link full def=\"foo bar\" (text \"x\"))) (def))");
    CHECK_TREE("[ Foo bar ][]\n\n[foo bar]: /u",
               "(doc (p (link collapsed def=\"foo bar\" "
               "(text \" Foo bar \"))) (def))");
}

NAOS_TEST(a_label_across_lines)
{
    amark_node_id_t link;

    /* A full reference label across lines makes no line break (13.7). */
    CHECK_TREE("[x][a\nb]\n\n[a b]: /u",
               "(doc (p (link full def=\"a b\" (text \"x\"))) (def))");
    link = child_at(paragraph(), 0);
    NAOS_CHECK_UINT_EQ(child_count(paragraph()), 1);
    check_node(link, AMARK_KIND_LINK, 0, 8, 1, 2);
    NAOS_CHECK_UINT_EQ(child_count(link), 1);

    /* Inside a block quote, the prefix is not part of the label. */
    PARSE("> [x][a\n> b]\n\n[a b]: /u");
    NAOS_CHECK_STR_EQ(dump(),
                      "(doc (bq (p (link full def=\"a b\" (text \"x\")))) "
                      "(def))");
    link = child_at(child_at(block_at(0), 0), 0);
    check_node(link, AMARK_KIND_LINK, 2, 12, 1, 2);
    check_reference(link, AMARK_LINK_FULL, block_at(1));
    NAOS_CHECK_UINT_EQ(child_count(link), 1);

    /* A shortcut whose text is across lines: its line break is a child. */
    PARSE("> [a\n> b]\n\n[a b]: /u");
    NAOS_CHECK_STR_EQ(dump(),
                      "(doc (bq (p (link shortcut def=\"a b\" (text \"a\") "
                      "(sbr) (text \"b\")))) (def))");
    link = child_at(child_at(block_at(0), 0), 0);
    check_node(link, AMARK_KIND_LINK, 2, 9, 1, 2);
    check_reference(link, AMARK_LINK_SHORTCUT, block_at(1));
}

NAOS_TEST(labels_are_compared_as_written)
{
    /* An escape in the text is not undone before the comparison. */
    CHECK_TREE("[a\\*]\n\n[a*]: /u",
               "(doc (p (text \"[a\") (esc \"\\\\*\") (text \"]\")) (def))");
    CHECK_TREE("[a\\*]\n\n[a\\*]: /u",
               "(doc (p (link shortcut def=\"a\\\\*\" (text \"a\") "
               "(esc \"\\\\*\"))) (def))");
    /* Nor is a character reference or emphasis. */
    CHECK_TREE("[x][&amp;]\n\n[&]: /u",
               "(doc (p (text \"[x][\") (ref \"&amp;\") (text \"]\")) "
               "(def))");
    CHECK_TREE("[*a*]\n\n[a]: /u",
               "(doc (p (text \"[\") (em (text \"a\")) (text \"]\")) "
               "(def))");
    /* An escaped bracket inside the text can be part of a label. */
    CHECK_TREE("[a\\]b]\n\n[a\\]b]: /u",
               "(doc (p (link shortcut def=\"a\\\\]b\" (text \"a\") "
               "(esc \"\\\\]\") (text \"b\"))) (def))");
}

/* ---- Labels with ill-formed UTF-8 and U+0000 ---- */

/* Checks that the definition def has the label range [start, end). */
static void check_label(amark_node_id_t def, size_t start, size_t end)
{
    amark_range_t range;

    NAOS_CHECK_UINT_EQ(amark_node_kind(g_doc, def),
                       AMARK_KIND_LINK_REFERENCE_DEFINITION);
    NAOS_REQUIRE_TRUE(amark_definition_label(g_doc, def, &range));
    NAOS_CHECK_UINT_EQ(range.start, start);
    NAOS_CHECK_UINT_EQ(range.end, end);
}

NAOS_TEST(a_label_with_ill_formed_utf8_matches_and_resolves)
{
    /*
     * Line 1 "[a\xFF" "b]" (0-4), line 2 "[x][a\xFF" "b]" (6-13), a blank
     * line, then the definition (16-24). The 'b' is a literal of its own so
     * that it is not read as part of the hex escape.
     */
    static const char input[] = "[a\xFF" "b]\n[x][a\xFF" "b]\n\n"
                                "[a\xFF" "b]: /u";
    amark_node_id_t def;
    amark_node_id_t link;

    parse_with_mode(input, sizeof(input) - 1, AMARK_UTF8_REPLACE);
    NAOS_CHECK_UINT_EQ(child_count(amark_document_root(g_doc)), 2);
    def = block_at(1);
    check_node(def, AMARK_KIND_LINK_REFERENCE_DEFINITION, 16, 25, 4, 4);
    check_label(def, 17, 20);
    NAOS_CHECK_UINT_EQ(child_count(paragraph()), 3);

    /* The shortcut: the offsets are those of the bytes given. */
    link = child_at(paragraph(), 0);
    check_node(link, AMARK_KIND_LINK, 0, 5, 1, 1);
    check_reference(link, AMARK_LINK_SHORTCUT, def);
    NAOS_CHECK_UINT_EQ(child_count(link), 1);
    check_node(child_at(link, 0), AMARK_KIND_TEXT, 1, 4, 1, 1);
    check_node(child_at(paragraph(), 1), AMARK_KIND_SOFT_BREAK, 5, 6, 1, 1);

    /* The full reference with the same label. */
    link = child_at(paragraph(), 2);
    check_node(link, AMARK_KIND_LINK, 6, 14, 2, 2);
    check_reference(link, AMARK_LINK_FULL, def);
    NAOS_CHECK_UINT_EQ(child_count(link), 1);
    check_node(child_at(link, 0), AMARK_KIND_TEXT, 7, 8, 2, 2);
}

NAOS_TEST(a_label_with_nul_matches_and_resolves)
{
    /*
     * U+0000 is valid UTF-8, so this holds in both modes. The length is
     * given explicitly: the content holds NUL bytes. "[a\0b]" is 0-4, the
     * definition 7-15.
     */
    static const char input[] = "[a\0b]\n\n[a\0b]: /u";
    static const size_t length = sizeof(input) - 1;
    static const amark_utf8_mode_t modes[] = {AMARK_UTF8_REPLACE,
                                              AMARK_UTF8_STRICT};
    /* U+0000 is compared as U+FFFD, so it matches a U+FFFD written out. */
    static const char replaced[] = "[a\0b]\n\n[a\xEF\xBF\xBD" "b]: /u";
    amark_node_id_t link;
    size_t i;

    NAOS_REQUIRE_UINT_EQ(length, 16);
    for (i = 0; i < 2; i++) {
        NAOS_TRACE_PUSH("mode %d", (int)modes[i]);
        parse_with_mode(input, length, modes[i]);
        NAOS_CHECK_UINT_EQ(child_count(amark_document_root(g_doc)), 2);
        check_node(block_at(1), AMARK_KIND_LINK_REFERENCE_DEFINITION, 7, 16,
                   3, 3);
        check_label(block_at(1), 8, 11);
        NAOS_CHECK_UINT_EQ(child_count(paragraph()), 1);
        link = child_at(paragraph(), 0);
        check_node(link, AMARK_KIND_LINK, 0, 5, 1, 1);
        check_reference(link, AMARK_LINK_SHORTCUT, block_at(1));
        NAOS_CHECK_UINT_EQ(child_count(link), 1);
        check_node(child_at(link, 0), AMARK_KIND_TEXT, 1, 4, 1, 1);
        NAOS_TRACE_POP();
    }

    parse_with_mode(replaced, sizeof(replaced) - 1, AMARK_UTF8_REPLACE);
    check_label(block_at(1), 8, 13);
    link = child_at(paragraph(), 0);
    check_node(link, AMARK_KIND_LINK, 0, 5, 1, 1);
    check_reference(link, AMARK_LINK_SHORTCUT, block_at(1));
}

NAOS_TEST(different_ill_formed_bytes_in_labels_match)
{
    /*
     * "[a\xFF" "b]" against a definition of "[a\xFE" "b]": the labels
     * match. Labels are compared as the characters they decode to
     * (amark_label_compare in src/definition.c decodes with
     * amark_utf8_decode_lossy and folds the code points), and with
     * ill-formed bytes replaced, both 0xFF and 0xFE are one U+FFFD each, so
     * both labels are "a" U+FFFD "b". The input is parsed as if those bytes
     * were U+FFFD; only the offsets are those of the bytes given. This is
     * the comparison that test_definition.c checks with
     * compare_ill_formed_bytes_as_replacement_character, here end to end.
     * "[a\xFF" "b]" is 0-4, the definition 7-15.
     */
    static const char input[] = "[a\xFF" "b]\n\n[a\xFE" "b]: /u";
    amark_node_id_t link;

    parse_with_mode(input, sizeof(input) - 1, AMARK_UTF8_REPLACE);
    NAOS_CHECK_UINT_EQ(child_count(amark_document_root(g_doc)), 2);
    check_node(block_at(1), AMARK_KIND_LINK_REFERENCE_DEFINITION, 7, 16, 3,
               3);
    check_label(block_at(1), 8, 11);
    NAOS_CHECK_UINT_EQ(child_count(paragraph()), 1);
    link = child_at(paragraph(), 0);
    check_node(link, AMARK_KIND_LINK, 0, 5, 1, 1);
    check_reference(link, AMARK_LINK_SHORTCUT, block_at(1));
    NAOS_CHECK_UINT_EQ(child_count(link), 1);
    check_node(child_at(link, 0), AMARK_KIND_TEXT, 1, 4, 1, 1);
}

/* ---- Precedence of the forms ---- */

NAOS_TEST(the_inline_form_comes_first)
{
    CHECK_TREE("[a](b)\n\n[a]: /u",
               "(doc (p (link inline dest=\"b\" (text \"a\"))) (def))");
    /* A part in parentheses that does not match: a shortcut, then text. */
    CHECK_TREE("[a](b c)\n\n[a]: /u",
               "(doc (p (link shortcut def=\"a\" (text \"a\")) "
               "(text \"(b c)\")) (def))");
}

NAOS_TEST(a_full_reference_without_a_definition_tries_no_shortcut)
{
    /* The text [a] has a definition, the label [b] has none. */
    CHECK_TREE("[a][b]\n\n[a]: /u", "(doc (p (text \"[a][b]\")) (def))");
    /* The label goes on as brackets of its own (9.7). */
    CHECK_TREE("[b][a]\n\n[a]: /u",
               "(doc (p (link full def=\"a\" (text \"b\"))) (def))");
    CHECK_TREE("[a][b]\n\n[b]: /u",
               "(doc (p (link full def=\"b\" (text \"a\"))) (def))");
    /* Example 568 of the specification. */
    CHECK_TREE("[foo][bar][baz]\n\n[baz]: /1",
               "(doc (p (text \"[foo]\") (link full def=\"baz\" "
               "(text \"bar\"))) (def))");
    check_node(child_at(paragraph(), 1), AMARK_KIND_LINK, 5, 15, 1, 1);
    /* Example 569: the second label is the text of a shortcut. */
    CHECK_TREE("[foo][bar][baz]\n\n[baz]: /1\n[bar]: /2",
               "(doc (p (link full def=\"bar\" (text \"foo\")) "
               "(link shortcut def=\"baz\" (text \"baz\"))) (def) (def))");
    check_node(child_at(paragraph(), 0), AMARK_KIND_LINK, 0, 10, 1, 1);
    check_node(child_at(paragraph(), 1), AMARK_KIND_LINK, 10, 15, 1, 1);
    /* Example 570. */
    CHECK_TREE("[foo][bar][baz]\n\n[baz]: /1\n[foo]: /2",
               "(doc (p (text \"[foo]\") (link full def=\"baz\" "
               "(text \"bar\"))) (def) (def))");
}

NAOS_TEST(collapsed_and_what_is_not_a_label)
{
    amark_node_id_t link;

    CHECK_TREE("[a][]\n\n[a]: /u",
               "(doc (p (link collapsed def=\"a\" (text \"a\"))) (def))");
    /* White space between the text and the "[]": a shortcut (9.11). */
    CHECK_TREE("[a] []\n\n[a]: /u",
               "(doc (p (link shortcut def=\"a\" (text \"a\")) "
               "(text \" []\")) (def))");
    CHECK_TREE("[a]\n[]\n\n[a]: /u",
               "(doc (p (link shortcut def=\"a\" (text \"a\")) (sbr) "
               "(text \"[]\")) (def))");
    /* "[ ]" is neither a label nor "[]": a shortcut, then text. This differs
     * from both reference implementations; the specification has no
     * example. */
    CHECK_TREE("[a][ ]\n\n[a]: /u",
               "(doc (p (link shortcut def=\"a\" (text \"a\")) "
               "(text \"[ ]\")) (def))");
    link = child_at(paragraph(), 0);
    check_node(link, AMARK_KIND_LINK, 0, 3, 1, 1);
    check_reference(link, AMARK_LINK_SHORTCUT, block_at(1));
    check_node(child_at(paragraph(), 1), AMARK_KIND_TEXT, 3, 6, 1, 1);
    /* A label that is not closed: a shortcut. */
    CHECK_TREE("[a][b\n\n[a]: /u",
               "(doc (p (link shortcut def=\"a\" (text \"a\")) "
               "(text \"[b\")) (def))");
}

NAOS_TEST(a_text_with_brackets_is_no_label)
{
    /* The inner shortcut is a link; the outer brackets are text. */
    CHECK_TREE("[[a]]\n\n[a]: /u",
               "(doc (p (text \"[\") (link shortcut def=\"a\" (text \"a\")) "
               "(text \"]\")) (def))");
    /* Example 545 of the specification, with a full reference after it. */
    CHECK_TREE("[foo [bar](/uri)][ref]\n\n[ref]: /u",
               "(doc (p (text \"[foo \") (link inline dest=\"/uri\" "
               "(text \"bar\")) (text \"]\") (link shortcut def=\"ref\" "
               "(text \"ref\"))) (def))");
    /* Brackets in the text that make no link: no label, so no link. */
    CHECK_TREE("[a [b] c]\n\n[a]: /u", "(doc (p (text \"[a [b] c]\")) (def))");
    /* A ']' inside a code span ends the scan of the label early. */
    CHECK_TREE("[a`]`]\n\n[a`]: /u",
               "(doc (p (text \"[a\") (code \"`]`\") (text \"]\")) "
               "(def))");
}

NAOS_SUITE(inline_form,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(an_inline_link_with_a_destination_and_a_title),
    NAOS_ADD_TEST(absent_and_empty_destinations_and_titles),
    NAOS_ADD_TEST(an_inline_image_starts_at_the_exclamation_mark)
)

NAOS_SUITE(text,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(brackets_without_a_matching_part_are_text),
    NAOS_ADD_TEST(an_exclamation_mark_not_followed_by_a_bracket_is_text)
)

NAOS_SUITE(nesting,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(a_link_inside_a_link_leaves_the_outer_brackets_as_text),
    NAOS_ADD_TEST(images_inside_links_and_links_and_images_inside_images),
    NAOS_ADD_TEST(the_link_floor_does_not_come_back_with_a_later_bracket),
    NAOS_ADD_TEST(brackets_inside_the_link_text)
)

NAOS_SUITE(precedence,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(delimiters_inside_and_outside_the_link_text_do_not_match),
    NAOS_ADD_TEST(a_code_span_binds_tighter_than_the_brackets)
)

NAOS_SUITE(lines,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(line_endings_inside_a_title_make_no_break),
    NAOS_ADD_TEST(line_breaks_inside_the_link_text_are_children_of_the_link)
)

NAOS_SUITE(references,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(the_three_reference_forms_of_links),
    NAOS_ADD_TEST(the_three_reference_forms_of_images),
    NAOS_ADD_TEST(the_first_of_the_definitions_with_the_same_label),
    NAOS_ADD_TEST(definitions_anywhere_in_the_document),
    NAOS_ADD_TEST(a_label_without_a_definition),
    NAOS_ADD_TEST(labels_of_999_and_1000_characters),
    NAOS_ADD_TEST(a_label_of_1000_characters_after_the_text_is_no_label),
    NAOS_ADD_TEST(labels_are_compared_after_case_folding_and_white_space),
    NAOS_ADD_TEST(a_label_across_lines),
    NAOS_ADD_TEST(labels_are_compared_as_written),
    NAOS_ADD_TEST(a_label_with_ill_formed_utf8_matches_and_resolves),
    NAOS_ADD_TEST(a_label_with_nul_matches_and_resolves),
    NAOS_ADD_TEST(different_ill_formed_bytes_in_labels_match)
)

NAOS_SUITE(reference_precedence,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(the_inline_form_comes_first),
    NAOS_ADD_TEST(a_full_reference_without_a_definition_tries_no_shortcut),
    NAOS_ADD_TEST(collapsed_and_what_is_not_a_label),
    NAOS_ADD_TEST(a_text_with_brackets_is_no_label)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(inline_form),
    NAOS_ADD_SUITE(text),
    NAOS_ADD_SUITE(nesting),
    NAOS_ADD_SUITE(precedence),
    NAOS_ADD_SUITE(lines),
    NAOS_ADD_SUITE(references),
    NAOS_ADD_SUITE(reference_precedence)
)
