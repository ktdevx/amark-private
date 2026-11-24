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
 * every content. Every tree passes the tree check and has no link inside a
 * link.
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
 * Parses length bytes through amark_parse without limits, requires
 * success, a tree that passes the tree check and no link inside a link.
 * The document of an earlier parse in the same test is destroyed first.
 */
static void parse_bytes(const char *bytes, size_t length)
{
    amark_config_t config;
    amark_tree_check_result_t check;

    amark_document_destroy(g_doc);
    g_doc = NULL;
    amark_config_init(&config);
    config.allocator = g_allocator;
    NAOS_REQUIRE_INT_EQ(amark_parse(bytes, length, &config, &g_doc),
                        AMARK_ERROR_NONE);
    check = amark_tree_check(g_doc);
    NAOS_CHECK_INT_EQ(check.violation, AMARK_TREE_VIOLATION_NONE);
    NAOS_CHECK_UINT_EQ(check.node, AMARK_NODE_NONE);
    NAOS_CHECK_TRUE(no_link_inside_a_link());
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

NAOS_MAIN(
    NAOS_ADD_SUITE(inline_form),
    NAOS_ADD_SUITE(text),
    NAOS_ADD_SUITE(nesting),
    NAOS_ADD_SUITE(precedence),
    NAOS_ADD_SUITE(lines)
)
