/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of the tree dumps of the test support
 * (tests/support/tree_dump.h): trees built by hand with the tree builder
 * (src/tree_internal.h), holding every block kind and attribute, are written
 * in the format of the design; amark_test_dump_blocks leaves inline nodes
 * out, and amark_test_dump_tree writes every inline kind, the four link
 * forms, the "none" and empty destinations and titles, and quoted bytes with
 * their escapes; a deep tree is written without recursion, and a buffer
 * that is too small gets a cut, NUL-terminated string and the length of the
 * whole one.
 */

#include "document.h"
#include "test_alloc.h"
#include "tree_dump.h"
#include "tree_internal.h"

#include <naos/naos.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * Fixture. NAOS_REQUIRE_* leaves a test with longjmp, so the document a test
 * creates is kept in g_doc, and teardown destroys it and checks that no
 * block is left.
 */
static amark_test_alloc_t g_state;
static amark_allocator_t g_allocator;
static amark_document_t *g_doc;

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

/* Every node covers the empty content: the dump writes no ranges. */
static amark_error_t create_empty(void)
{
    return amark_document_create(&g_allocator, NULL, 0, 0, 0, NULL, &g_doc);
}

/* Appends a node of kind under parent, or returns AMARK_NODE_NONE. */
static amark_node_id_t add(amark_node_id_t parent, amark_kind_t kind)
{
    amark_node_id_t out = AMARK_NODE_NONE;

    if (amark_tree_append(g_doc, parent, kind, AMARK_NODE_CLASS_AUTO, 0, 0,
                          &out) != AMARK_ERROR_NONE) {
        return AMARK_NODE_NONE;
    }
    return out;
}

/* Appends a paragraph with a text and an emphasis holding a text. */
static amark_node_id_t add_paragraph(amark_node_id_t parent)
{
    amark_node_id_t p = add(parent, AMARK_KIND_PARAGRAPH);
    amark_node_id_t em;

    if (p == AMARK_NODE_NONE || add(p, AMARK_KIND_TEXT) == AMARK_NODE_NONE) {
        return AMARK_NODE_NONE;
    }
    em = add(p, AMARK_KIND_EMPHASIS);
    if (em == AMARK_NODE_NONE || add(em, AMARK_KIND_TEXT) == AMARK_NODE_NONE) {
        return AMARK_NODE_NONE;
    }
    return p;
}

/*
 * The example of the design, with inline nodes under each paragraph and a
 * heading:
 * (doc (bq (p)) (ul - tight (li (p))) (ol 3 . loose (li (p))) (h 2 atx)
 *  (h 1 setext) (code indented) (code fenced ` 3 info) (html) (hr) (def))
 */
static const char k_design_example[] =
    "(doc (bq (p)) (ul - tight (li (p))) (ol 3 . loose (li (p))) (h 2 atx) "
    "(h 1 setext) (code indented) (code fenced ` 3 info) (html) (hr) (def))";
#define DESIGN_EXAMPLE_LENGTH (sizeof(k_design_example) - 1)

static void build_design_example(void)
{
    amark_node_id_t node;
    amark_node_id_t item;
    amark_range_t info;
    amark_range_t label;
    amark_range_t destination;

    info.start = 0;
    info.end = 0;
    label = info;
    destination = info;

    NAOS_REQUIRE_INT_EQ(create_empty(), AMARK_ERROR_NONE);

    node = add(1, AMARK_KIND_BLOCK_QUOTE);
    NAOS_REQUIRE_UINT_NE(node, AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add_paragraph(node), AMARK_NODE_NONE);

    node = add(1, AMARK_KIND_LIST);
    NAOS_REQUIRE_UINT_NE(node, AMARK_NODE_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_list(g_doc, node, AMARK_LIST_BULLET,
                                            '-', 0, AMARK_LIST_TIGHT),
                        AMARK_ERROR_NONE);
    item = add(node, AMARK_KIND_LIST_ITEM);
    NAOS_REQUIRE_UINT_NE(item, AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add_paragraph(item), AMARK_NODE_NONE);

    node = add(1, AMARK_KIND_LIST);
    NAOS_REQUIRE_UINT_NE(node, AMARK_NODE_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_list(g_doc, node, AMARK_LIST_ORDERED,
                                            '.', 3, AMARK_LIST_LOOSE),
                        AMARK_ERROR_NONE);
    item = add(node, AMARK_KIND_LIST_ITEM);
    NAOS_REQUIRE_UINT_NE(item, AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add_paragraph(item), AMARK_NODE_NONE);

    node = add(1, AMARK_KIND_HEADING);
    NAOS_REQUIRE_UINT_NE(node, AMARK_NODE_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_heading(g_doc, node, 2,
                                               AMARK_HEADING_ATX),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_UINT_NE(add(node, AMARK_KIND_TEXT), AMARK_NODE_NONE);

    node = add(1, AMARK_KIND_HEADING);
    NAOS_REQUIRE_UINT_NE(node, AMARK_NODE_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_heading(g_doc, node, 1,
                                               AMARK_HEADING_SETEXT),
                        AMARK_ERROR_NONE);

    node = add(1, AMARK_KIND_CODE_BLOCK);
    NAOS_REQUIRE_UINT_NE(node, AMARK_NODE_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_code_block(g_doc, node,
                                                  AMARK_CODE_BLOCK_INDENTED,
                                                  0, 0, NULL),
                        AMARK_ERROR_NONE);

    node = add(1, AMARK_KIND_CODE_BLOCK);
    NAOS_REQUIRE_UINT_NE(node, AMARK_NODE_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_code_block(g_doc, node,
                                                  AMARK_CODE_BLOCK_FENCED,
                                                  '`', 3, &info),
                        AMARK_ERROR_NONE);

    NAOS_REQUIRE_UINT_NE(add(1, AMARK_KIND_HTML_BLOCK), AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add(1, AMARK_KIND_THEMATIC_BREAK), AMARK_NODE_NONE);

    node = add(1, AMARK_KIND_LINK_REFERENCE_DEFINITION);
    NAOS_REQUIRE_UINT_NE(node, AMARK_NODE_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_definition(g_doc, node, &label,
                                                  &destination, NULL),
                        AMARK_ERROR_NONE);
}

NAOS_TEST(design_example_with_every_block_kind)
{
    char buffer[256];

    build_design_example();
    memset(buffer, 'x', sizeof(buffer));
    NAOS_CHECK_UINT_EQ(amark_test_dump_blocks(g_doc, buffer, sizeof(buffer)),
                       DESIGN_EXAMPLE_LENGTH);
    NAOS_CHECK_STR_EQ(buffer, k_design_example);
}

NAOS_TEST(document_with_no_children)
{
    char buffer[16];

    NAOS_REQUIRE_INT_EQ(create_empty(), AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(amark_test_dump_blocks(g_doc, buffer, sizeof(buffer)),
                       5);
    NAOS_CHECK_STR_EQ(buffer, "(doc)");
}

NAOS_TEST(null_document_gives_the_empty_string)
{
    char buffer[4] = "xyz";

    NAOS_CHECK_UINT_EQ(amark_test_dump_blocks(NULL, buffer, sizeof(buffer)),
                       0);
    NAOS_CHECK_STR_EQ(buffer, "");
}

/*
 * The other attribute values: the other bullet characters, the ')'
 * delimiter, a large and a zero start number, levels 3 to 6, the '~' fence,
 * an info string that is not written and one that is written but empty, a
 * long fence, and nested containers (a list in a block quote in a list).
 */
NAOS_TEST(other_attribute_values_and_nesting)
{
    static const char k_expected[] =
        "(doc (ul + loose (li (bq (ol 123456789 ) tight (li (p)) (li))))) "
        "(ul * tight (li (hr))) (ol 0 . tight (li (code fenced ~ 12))) "
        "(h 3 atx) (h 4 atx) (h 5 setext) (h 6 atx) "
        "(code fenced ` 3 info) (code fenced ~ 4))";
    amark_range_t empty;
    amark_range_t info;
    amark_node_id_t ul;
    amark_node_id_t li;
    amark_node_id_t bq;
    amark_node_id_t ol;
    amark_node_id_t node;
    unsigned level;
    char buffer[256];

    empty.start = 0;
    empty.end = 0;
    info = empty;

    NAOS_REQUIRE_INT_EQ(create_empty(), AMARK_ERROR_NONE);

    ul = add(1, AMARK_KIND_LIST);
    NAOS_REQUIRE_UINT_NE(ul, AMARK_NODE_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_list(g_doc, ul, AMARK_LIST_BULLET, '+',
                                            0, AMARK_LIST_LOOSE),
                        AMARK_ERROR_NONE);
    li = add(ul, AMARK_KIND_LIST_ITEM);
    NAOS_REQUIRE_UINT_NE(li, AMARK_NODE_NONE);
    bq = add(li, AMARK_KIND_BLOCK_QUOTE);
    NAOS_REQUIRE_UINT_NE(bq, AMARK_NODE_NONE);
    ol = add(bq, AMARK_KIND_LIST);
    NAOS_REQUIRE_UINT_NE(ol, AMARK_NODE_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_list(g_doc, ol, AMARK_LIST_ORDERED,
                                            ')', 123456789u,
                                            AMARK_LIST_TIGHT),
                        AMARK_ERROR_NONE);
    li = add(ol, AMARK_KIND_LIST_ITEM);
    NAOS_REQUIRE_UINT_NE(li, AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add_paragraph(li), AMARK_NODE_NONE);
    /* An empty list item, the last child at three levels at once. */
    NAOS_REQUIRE_UINT_NE(add(ol, AMARK_KIND_LIST_ITEM), AMARK_NODE_NONE);

    ul = add(1, AMARK_KIND_LIST);
    NAOS_REQUIRE_UINT_NE(ul, AMARK_NODE_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_list(g_doc, ul, AMARK_LIST_BULLET, '*',
                                            0, AMARK_LIST_TIGHT),
                        AMARK_ERROR_NONE);
    li = add(ul, AMARK_KIND_LIST_ITEM);
    NAOS_REQUIRE_UINT_NE(li, AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add(li, AMARK_KIND_THEMATIC_BREAK), AMARK_NODE_NONE);

    ol = add(1, AMARK_KIND_LIST);
    NAOS_REQUIRE_UINT_NE(ol, AMARK_NODE_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_list(g_doc, ol, AMARK_LIST_ORDERED,
                                            '.', 0, AMARK_LIST_TIGHT),
                        AMARK_ERROR_NONE);
    li = add(ol, AMARK_KIND_LIST_ITEM);
    NAOS_REQUIRE_UINT_NE(li, AMARK_NODE_NONE);
    node = add(li, AMARK_KIND_CODE_BLOCK);
    NAOS_REQUIRE_UINT_NE(node, AMARK_NODE_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_code_block(g_doc, node,
                                                  AMARK_CODE_BLOCK_FENCED,
                                                  '~', 12, NULL),
                        AMARK_ERROR_NONE);

    for (level = 3; level <= 6; level++) {
        node = add(1, AMARK_KIND_HEADING);
        NAOS_REQUIRE_UINT_NE(node, AMARK_NODE_NONE);
        NAOS_REQUIRE_INT_EQ(
            amark_tree_set_heading(g_doc, node, level,
                                   level == 5 ? AMARK_HEADING_SETEXT
                                              : AMARK_HEADING_ATX),
            AMARK_ERROR_NONE);
    }

    /* Written but empty: still "info". */
    node = add(1, AMARK_KIND_CODE_BLOCK);
    NAOS_REQUIRE_UINT_NE(node, AMARK_NODE_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_code_block(g_doc, node,
                                                  AMARK_CODE_BLOCK_FENCED,
                                                  '`', 3, &info),
                        AMARK_ERROR_NONE);
    node = add(1, AMARK_KIND_CODE_BLOCK);
    NAOS_REQUIRE_UINT_NE(node, AMARK_NODE_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_code_block(g_doc, node,
                                                  AMARK_CODE_BLOCK_FENCED,
                                                  '~', 4, NULL),
                        AMARK_ERROR_NONE);

    NAOS_CHECK_UINT_EQ(amark_test_dump_blocks(g_doc, buffer, sizeof(buffer)),
                       sizeof(k_expected) - 1);
    NAOS_CHECK_STR_EQ(buffer, k_expected);
}

/*
 * Inline nodes are left out wherever they are, also before, between and
 * after blocks among the children of one node, and with their descendants.
 */
NAOS_TEST(inline_nodes_are_left_out)
{
    amark_node_id_t bq;
    amark_node_id_t em;
    char buffer[64];

    NAOS_REQUIRE_INT_EQ(create_empty(), AMARK_ERROR_NONE);
    bq = add(1, AMARK_KIND_BLOCK_QUOTE);
    NAOS_REQUIRE_UINT_NE(bq, AMARK_NODE_NONE);
    em = add(bq, AMARK_KIND_STRONG);
    NAOS_REQUIRE_UINT_NE(em, AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add(em, AMARK_KIND_TEXT), AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add(bq, AMARK_KIND_PARAGRAPH), AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add(bq, AMARK_KIND_SOFT_BREAK), AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add(bq, AMARK_KIND_HTML_BLOCK), AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add(bq, AMARK_KIND_TEXT), AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add(1, AMARK_KIND_TEXT), AMARK_NODE_NONE);

    NAOS_CHECK_UINT_EQ(amark_test_dump_blocks(g_doc, buffer, sizeof(buffer)),
                       21);
    NAOS_CHECK_STR_EQ(buffer, "(doc (bq (p) (html)))");
}

/*
 * Attributes that were never set are written as "none", and a block kind
 * without a short name (a dialect block) as "(?)".
 */
NAOS_TEST(missing_attributes_and_other_kinds)
{
    amark_node_id_t node = AMARK_NODE_NONE;
    char buffer[128];

    NAOS_REQUIRE_INT_EQ(create_empty(), AMARK_ERROR_NONE);
    NAOS_REQUIRE_UINT_NE(add(1, AMARK_KIND_LIST), AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add(1, AMARK_KIND_HEADING), AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add(1, AMARK_KIND_CODE_BLOCK), AMARK_NODE_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_append(g_doc, 1, AMARK_KIND_DIALECT_MIN,
                                          AMARK_NODE_CLASS_BLOCK, 0, 0,
                                          &node),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_UINT_NE(add(node, AMARK_KIND_PARAGRAPH), AMARK_NODE_NONE);

    NAOS_CHECK_UINT_EQ(amark_test_dump_blocks(g_doc, buffer, sizeof(buffer)),
                       43);
    NAOS_CHECK_STR_EQ(buffer,
                      "(doc (list) (h 0 none) (code none) (? (p)))");
}

/*
 * Every size from 0 to one past the whole string: the return value is
 * always the whole length, the buffer holds the first size - 1 bytes and a
 * NUL, and nothing past size is written.
 */
NAOS_TEST(cut_strings_for_every_size)
{
    char buffer[DESIGN_EXAMPLE_LENGTH + 8];
    size_t size;

    build_design_example();

    NAOS_CHECK_UINT_EQ(amark_test_dump_blocks(g_doc, NULL, 0),
                       DESIGN_EXAMPLE_LENGTH);

    for (size = 0; size <= DESIGN_EXAMPLE_LENGTH + 1; size++) {
        size_t kept = size == 0 ? 0 : size - 1;

        memset(buffer, 'x', sizeof(buffer));
        NAOS_CHECK_UINT_EQ(amark_test_dump_blocks(g_doc, buffer, size),
                           DESIGN_EXAMPLE_LENGTH);
        if (size > 0) {
            NAOS_CHECK_INT_EQ(memcmp(buffer, k_design_example, kept), 0);
            NAOS_CHECK_INT_EQ(buffer[kept], '\0');
        }
        NAOS_CHECK_INT_EQ(buffer[size], 'x');
    }

    /* Exactly fits: size is the length plus 1. */
    NAOS_CHECK_UINT_EQ(amark_test_dump_blocks(g_doc, buffer,
                                              DESIGN_EXAMPLE_LENGTH + 1),
                       DESIGN_EXAMPLE_LENGTH);
    NAOS_CHECK_STR_EQ(buffer, k_design_example);
}

/*
 * 100,000 nested block quotes are written without recursion:
 * "(doc" + " (bq" * N + ")" * (N + 1).
 */
#define DEEP_COUNT 100000u
#define DEEP_LENGTH (4u + 4u * DEEP_COUNT + DEEP_COUNT + 1u)

static char g_deep_buffer[DEEP_LENGTH + 1];

NAOS_TEST(deep_nesting_without_recursion)
{
    amark_node_id_t parent = 1;
    size_t i;
    size_t pos;
    int same = 1;

    NAOS_REQUIRE_INT_EQ(create_empty(), AMARK_ERROR_NONE);
    for (i = 0; i < DEEP_COUNT; i++) {
        parent = add(parent, AMARK_KIND_BLOCK_QUOTE);
        NAOS_REQUIRE_UINT_NE(parent, AMARK_NODE_NONE);
    }

    NAOS_CHECK_UINT_EQ(amark_test_dump_blocks(g_doc, g_deep_buffer,
                                              sizeof(g_deep_buffer)),
                       DEEP_LENGTH);
    NAOS_CHECK_INT_EQ(memcmp(g_deep_buffer, "(doc", 4), 0);
    pos = 4;
    for (i = 0; i < DEEP_COUNT; i++, pos += 4) {
        if (memcmp(g_deep_buffer + pos, " (bq", 4) != 0) {
            same = 0;
        }
    }
    for (i = 0; i <= DEEP_COUNT; i++, pos++) {
        if (g_deep_buffer[pos] != ')') {
            same = 0;
        }
    }
    NAOS_CHECK_INT_EQ(same, 1);
    NAOS_CHECK_INT_EQ(g_deep_buffer[pos], '\0');
}

/*
 * amark_test_dump_tree.
 *
 * The content the inline nodes of the tests below cover. Each node takes the
 * range of the first occurrence of a part of it (see span).
 */
static const char k_content[] =
    "lbl foo\\*&amp;`a`<b><http://a><a@b.c> emph strong /u t img "
    "\"q\\\x01\x7f\xc3\xa9\t\r\n";

/* Creates the document over k_content (without its NUL). */
static amark_error_t create_with_content(void)
{
    return amark_document_create(&g_allocator,
                                 (const unsigned char *)k_content,
                                 sizeof(k_content) - 1, 0,
                                 sizeof(k_content) - 1, NULL, &g_doc);
}

/*
 * Returns the range of the first occurrence of part in k_content, or the
 * empty range at the end of the content if part is not in it (which the
 * test then sees as a wrong string).
 */
static amark_range_t span(const char *part)
{
    amark_range_t range;
    const char *found = strstr(k_content, part);

    if (found == NULL) {
        range.start = sizeof(k_content) - 1;
        range.end = range.start;
        return range;
    }
    range.start = (size_t)(found - k_content);
    range.end = range.start + strlen(part);
    return range;
}

/* Appends a node of kind under parent over the range of part. */
static amark_node_id_t add_over(amark_node_id_t parent, amark_kind_t kind,
                                const char *part)
{
    amark_node_id_t out = AMARK_NODE_NONE;
    amark_range_t range = span(part);

    if (amark_tree_append(g_doc, parent, kind, AMARK_NODE_CLASS_AUTO,
                          range.start, range.end, &out) != AMARK_ERROR_NONE) {
        return AMARK_NODE_NONE;
    }
    return out;
}

/*
 * Appends a link or an image of the inline form under parent, holding one
 * text over child (NULL: no children). dest and title are NULL (not
 * written) or a part of k_content ("" is written but empty).
 */
static amark_node_id_t add_inline_link(amark_node_id_t parent,
                                       amark_kind_t kind, const char *dest,
                                       const char *title, const char *child)
{
    amark_node_id_t node = add(parent, kind);
    amark_range_t d;
    amark_range_t t;

    if (node == AMARK_NODE_NONE) {
        return AMARK_NODE_NONE;
    }
    d = span(dest == NULL ? "" : dest);
    t = span(title == NULL ? "" : title);
    if (amark_tree_set_link(g_doc, node, AMARK_LINK_INLINE,
                            dest == NULL ? NULL : &d,
                            title == NULL ? NULL : &t,
                            AMARK_NODE_NONE) != AMARK_ERROR_NONE) {
        return AMARK_NODE_NONE;
    }
    if (child != NULL &&
        add_over(node, AMARK_KIND_TEXT, child) == AMARK_NODE_NONE) {
        return AMARK_NODE_NONE;
    }
    return node;
}

/* Appends a link or an image of a reference form referring to def. */
static amark_node_id_t add_reference_link(amark_node_id_t parent,
                                          amark_kind_t kind,
                                          amark_link_form_t form,
                                          amark_node_id_t def,
                                          const char *child)
{
    amark_node_id_t node = add(parent, kind);

    if (node == AMARK_NODE_NONE ||
        amark_tree_set_link(g_doc, node, form, NULL, NULL, def) !=
            AMARK_ERROR_NONE) {
        return AMARK_NODE_NONE;
    }
    if (child != NULL &&
        add_over(node, AMARK_KIND_TEXT, child) == AMARK_NODE_NONE) {
        return AMARK_NODE_NONE;
    }
    return node;
}

/*
 * Every inline kind in a paragraph, a heading holding inlines, and a block
 * quote among them: the inlines are written as children of their blocks.
 */
static const char k_every_inline[] =
    "(doc (def) "
    "(p (text \"foo\") (esc \"\\\\*\") (ref \"&amp;\") (code \"`a`\") "
    "(html \"<b>\") (autolink uri \"<http://a>\") "
    "(autolink email \"<a@b.c>\") (em (text \"emph\")) "
    "(strong (em (text \"strong\")) (text \"emph\")) (sbr) (br) "
    "(link inline dest=\"/u\" title=\"t\" (text \"foo\")) "
    "(link full def=\"lbl\" (text \"foo\")) "
    "(link collapsed def=\"lbl\" (text \"lbl\")) "
    "(link shortcut def=\"lbl\" (text \"lbl\")) "
    "(image inline dest=\"/u\" (text \"img\")) "
    "(image full def=\"lbl\" (text \"img\")) "
    "(image collapsed def=\"lbl\") "
    "(image shortcut def=\"lbl\" (text \"lbl\"))) "
    "(bq (h 2 atx (text \"foo\") (em) (text \"\"))))";
#define EVERY_INLINE_LENGTH (sizeof(k_every_inline) - 1)

static void build_every_inline(void)
{
    amark_node_id_t def;
    amark_node_id_t p;
    amark_node_id_t node;
    amark_node_id_t bq;
    amark_range_t label;
    amark_range_t empty;

    NAOS_REQUIRE_INT_EQ(create_with_content(), AMARK_ERROR_NONE);

    label = span("lbl");
    empty.start = 0;
    empty.end = 0;
    def = add(1, AMARK_KIND_LINK_REFERENCE_DEFINITION);
    NAOS_REQUIRE_UINT_NE(def, AMARK_NODE_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_definition(g_doc, def, &label, &empty,
                                                  NULL),
                        AMARK_ERROR_NONE);

    p = add(1, AMARK_KIND_PARAGRAPH);
    NAOS_REQUIRE_UINT_NE(p, AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add_over(p, AMARK_KIND_TEXT, "foo"), AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add_over(p, AMARK_KIND_BACKSLASH_ESCAPE, "\\*"),
                         AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add_over(p, AMARK_KIND_CHARACTER_REFERENCE, "&amp;"),
                         AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add_over(p, AMARK_KIND_CODE_SPAN, "`a`"),
                         AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add_over(p, AMARK_KIND_RAW_HTML, "<b>"),
                         AMARK_NODE_NONE);
    node = add_over(p, AMARK_KIND_AUTOLINK, "<http://a>");
    NAOS_REQUIRE_UINT_NE(node, AMARK_NODE_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_autolink(g_doc, node,
                                                AMARK_AUTOLINK_URI),
                        AMARK_ERROR_NONE);
    node = add_over(p, AMARK_KIND_AUTOLINK, "<a@b.c>");
    NAOS_REQUIRE_UINT_NE(node, AMARK_NODE_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_autolink(g_doc, node,
                                                AMARK_AUTOLINK_EMAIL),
                        AMARK_ERROR_NONE);
    node = add(p, AMARK_KIND_EMPHASIS);
    NAOS_REQUIRE_UINT_NE(node, AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add_over(node, AMARK_KIND_TEXT, "emph"),
                         AMARK_NODE_NONE);
    node = add(p, AMARK_KIND_STRONG);
    NAOS_REQUIRE_UINT_NE(node, AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add_over(node, AMARK_KIND_TEXT, "emph"),
                         AMARK_NODE_NONE);
    /* An emphasis inserted before the text: not the last child. */
    {
        amark_node_id_t em = AMARK_NODE_NONE;
        amark_range_t r = span("strong");

        NAOS_REQUIRE_INT_EQ(
            amark_tree_insert_before(g_doc,
                                     amark_node_first_child(g_doc, node),
                                     AMARK_KIND_EMPHASIS,
                                     AMARK_NODE_CLASS_AUTO, r.start, r.end,
                                     &em),
            AMARK_ERROR_NONE);
        NAOS_REQUIRE_UINT_NE(add_over(em, AMARK_KIND_TEXT, "strong"),
                             AMARK_NODE_NONE);
    }
    NAOS_REQUIRE_UINT_NE(add(p, AMARK_KIND_SOFT_BREAK), AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add(p, AMARK_KIND_HARD_BREAK), AMARK_NODE_NONE);

    NAOS_REQUIRE_UINT_NE(add_inline_link(p, AMARK_KIND_LINK, "/u", "t",
                                         "foo"),
                         AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add_reference_link(p, AMARK_KIND_LINK,
                                            AMARK_LINK_FULL, def, "foo"),
                         AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add_reference_link(p, AMARK_KIND_LINK,
                                            AMARK_LINK_COLLAPSED, def, "lbl"),
                         AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add_reference_link(p, AMARK_KIND_LINK,
                                            AMARK_LINK_SHORTCUT, def, "lbl"),
                         AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add_inline_link(p, AMARK_KIND_IMAGE, "/u", NULL,
                                         "img"),
                         AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add_reference_link(p, AMARK_KIND_IMAGE,
                                            AMARK_LINK_FULL, def, "img"),
                         AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add_reference_link(p, AMARK_KIND_IMAGE,
                                            AMARK_LINK_COLLAPSED, def, NULL),
                         AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add_reference_link(p, AMARK_KIND_IMAGE,
                                            AMARK_LINK_SHORTCUT, def, "lbl"),
                         AMARK_NODE_NONE);

    bq = add(1, AMARK_KIND_BLOCK_QUOTE);
    NAOS_REQUIRE_UINT_NE(bq, AMARK_NODE_NONE);
    node = add(bq, AMARK_KIND_HEADING);
    NAOS_REQUIRE_UINT_NE(node, AMARK_NODE_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_heading(g_doc, node, 2,
                                               AMARK_HEADING_ATX),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_UINT_NE(add_over(node, AMARK_KIND_TEXT, "foo"),
                         AMARK_NODE_NONE);
    /* An emphasis with no children, and a text over an empty range. */
    NAOS_REQUIRE_UINT_NE(add(node, AMARK_KIND_EMPHASIS), AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add(node, AMARK_KIND_TEXT), AMARK_NODE_NONE);
}

NAOS_TEST(tree_every_inline_kind_and_link_form)
{
    char buffer[EVERY_INLINE_LENGTH + 16];

    build_every_inline();
    memset(buffer, 'x', sizeof(buffer));
    NAOS_CHECK_UINT_EQ(amark_test_dump_tree(g_doc, buffer, sizeof(buffer)),
                       EVERY_INLINE_LENGTH);
    NAOS_CHECK_STR_EQ(buffer, k_every_inline);
}

/*
 * The destination and the title of the inline form: each not written
 * (left out), written but empty ("") or written, in every combination.
 */
NAOS_TEST(tree_destination_and_title_none_and_empty)
{
    static const char k_expected[] =
        "(doc (p (link inline) (link inline dest=\"\") "
        "(link inline title=\"\") (link inline dest=\"\" title=\"\") "
        "(link inline dest=\"/u\") (link inline title=\"t\") "
        "(image inline dest=\"/u\" title=\"\") "
        "(image inline dest=\"\" title=\"t\" (text \"img\"))))";
    amark_node_id_t p;
    char buffer[256];

    NAOS_REQUIRE_INT_EQ(create_with_content(), AMARK_ERROR_NONE);
    p = add(1, AMARK_KIND_PARAGRAPH);
    NAOS_REQUIRE_UINT_NE(p, AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add_inline_link(p, AMARK_KIND_LINK, NULL, NULL,
                                         NULL),
                         AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add_inline_link(p, AMARK_KIND_LINK, "", NULL, NULL),
                         AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add_inline_link(p, AMARK_KIND_LINK, NULL, "", NULL),
                         AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add_inline_link(p, AMARK_KIND_LINK, "", "", NULL),
                         AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add_inline_link(p, AMARK_KIND_LINK, "/u", NULL,
                                         NULL),
                         AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add_inline_link(p, AMARK_KIND_LINK, NULL, "t",
                                         NULL),
                         AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add_inline_link(p, AMARK_KIND_IMAGE, "/u", "", NULL),
                         AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add_inline_link(p, AMARK_KIND_IMAGE, "", "t",
                                         "img"),
                         AMARK_NODE_NONE);

    NAOS_CHECK_UINT_EQ(amark_test_dump_tree(g_doc, buffer, sizeof(buffer)),
                       sizeof(k_expected) - 1);
    NAOS_CHECK_STR_EQ(buffer, k_expected);
}

/*
 * The escapes in a quoted string: '"' and '\', LF, CR, tab, another control
 * byte, DEL and the bytes of a two-byte character; the space and the other
 * printable bytes are written as they are. They apply to the label of def=
 * and to dest= as well.
 */
NAOS_TEST(tree_escapes_in_quoted_strings)
{
    static const char k_expected[] =
        "(doc (def) "
        "(p (text \"t img \\\"q\\\\\\x01\\x7F\\xC3\\xA9\\t\\r\\n\") "
        "(link shortcut def=\"\\\"q\\\\\") "
        "(link inline dest=\"\\t\\r\\n\" title=\"\\xC3\\xA9\")))";
    amark_node_id_t def;
    amark_node_id_t p;
    amark_range_t label;
    amark_range_t empty;
    char buffer[256];

    NAOS_REQUIRE_INT_EQ(create_with_content(), AMARK_ERROR_NONE);
    label = span("\"q\\");
    empty.start = 0;
    empty.end = 0;
    def = add(1, AMARK_KIND_LINK_REFERENCE_DEFINITION);
    NAOS_REQUIRE_UINT_NE(def, AMARK_NODE_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_definition(g_doc, def, &label, &empty,
                                                  NULL),
                        AMARK_ERROR_NONE);
    p = add(1, AMARK_KIND_PARAGRAPH);
    NAOS_REQUIRE_UINT_NE(p, AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add_over(p, AMARK_KIND_TEXT,
                                  "t img \"q\\\x01\x7f\xc3\xa9\t\r\n"),
                         AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add_reference_link(p, AMARK_KIND_LINK,
                                            AMARK_LINK_SHORTCUT, def, NULL),
                         AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add_inline_link(p, AMARK_KIND_LINK, "\t\r\n",
                                         "\xc3\xa9", NULL),
                         AMARK_NODE_NONE);

    NAOS_CHECK_UINT_EQ(amark_test_dump_tree(g_doc, buffer, sizeof(buffer)),
                       sizeof(k_expected) - 1);
    NAOS_CHECK_STR_EQ(buffer, k_expected);
}

/*
 * Attributes that were never set are written as "none", and an inline kind
 * without a short name (a dialect inline) as "(?)" with its children.
 */
NAOS_TEST(tree_missing_attributes_and_other_kinds)
{
    static const char k_expected[] =
        "(doc (p (autolink none \"foo\") (link none) (image none "
        "(text \"img\")) (? (text \"foo\") (?))))";
    amark_node_id_t p;
    amark_node_id_t node = AMARK_NODE_NONE;
    amark_node_id_t inner = AMARK_NODE_NONE;
    char buffer[128];

    NAOS_REQUIRE_INT_EQ(create_with_content(), AMARK_ERROR_NONE);
    p = add(1, AMARK_KIND_PARAGRAPH);
    NAOS_REQUIRE_UINT_NE(p, AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add_over(p, AMARK_KIND_AUTOLINK, "foo"),
                         AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add(p, AMARK_KIND_LINK), AMARK_NODE_NONE);
    node = add(p, AMARK_KIND_IMAGE);
    NAOS_REQUIRE_UINT_NE(node, AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(add_over(node, AMARK_KIND_TEXT, "img"),
                         AMARK_NODE_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_append(g_doc, p, AMARK_KIND_DIALECT_MIN,
                                          AMARK_NODE_CLASS_INLINE, 0, 0,
                                          &node),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_UINT_NE(add_over(node, AMARK_KIND_TEXT, "foo"),
                         AMARK_NODE_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_append(g_doc, node,
                                          AMARK_KIND_DIALECT_MIN + 1u,
                                          AMARK_NODE_CLASS_INLINE, 0, 0,
                                          &inner),
                        AMARK_ERROR_NONE);

    NAOS_CHECK_UINT_EQ(amark_test_dump_tree(g_doc, buffer, sizeof(buffer)),
                       sizeof(k_expected) - 1);
    NAOS_CHECK_STR_EQ(buffer, k_expected);
}

/* The blocks are written as amark_test_dump_blocks writes them. */
NAOS_TEST(tree_blocks_as_in_the_block_dump)
{
    static const char k_expected[] =
        "(doc (bq (p (text \"\") (em (text \"\")))) "
        "(ul - tight (li (p (text \"\") (em (text \"\"))))) "
        "(ol 3 . loose (li (p (text \"\") "
        "(em (text \"\"))))) (h 2 atx (text \"\")) "
        "(h 1 setext) (code indented) "
        "(code fenced ` 3 info) (html) (hr) (def))";
    char blocks[256];
    char tree[256];

    NAOS_REQUIRE_INT_EQ(create_empty(), AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(amark_test_dump_tree(g_doc, tree, sizeof(tree)), 5);
    NAOS_CHECK_STR_EQ(tree, "(doc)");
    amark_document_destroy(g_doc);
    g_doc = NULL;

    /* The design example, now with the inlines of its paragraphs. */
    build_design_example();
    NAOS_CHECK_UINT_EQ(amark_test_dump_blocks(g_doc, blocks, sizeof(blocks)),
                       DESIGN_EXAMPLE_LENGTH);
    NAOS_CHECK_STR_EQ(blocks, k_design_example);
    NAOS_CHECK_UINT_EQ(amark_test_dump_tree(g_doc, tree, sizeof(tree)),
                       sizeof(k_expected) - 1);
    NAOS_CHECK_STR_EQ(tree, k_expected);
}

NAOS_TEST(tree_null_document_gives_the_empty_string)
{
    char buffer[4] = "xyz";

    NAOS_CHECK_UINT_EQ(amark_test_dump_tree(NULL, buffer, sizeof(buffer)), 0);
    NAOS_CHECK_STR_EQ(buffer, "");
}

/*
 * Every size from 0 to one past the whole string, cutting also inside the
 * quoted strings and their escapes.
 */
NAOS_TEST(tree_cut_strings_for_every_size)
{
    char buffer[EVERY_INLINE_LENGTH + 8];
    size_t size;

    build_every_inline();

    NAOS_CHECK_UINT_EQ(amark_test_dump_tree(g_doc, NULL, 0),
                       EVERY_INLINE_LENGTH);

    for (size = 0; size <= EVERY_INLINE_LENGTH + 1; size++) {
        size_t kept = size == 0 ? 0 : size - 1;

        memset(buffer, 'x', sizeof(buffer));
        NAOS_CHECK_UINT_EQ(amark_test_dump_tree(g_doc, buffer, size),
                           EVERY_INLINE_LENGTH);
        if (size > 0) {
            NAOS_CHECK_INT_EQ(memcmp(buffer, k_every_inline, kept), 0);
            NAOS_CHECK_INT_EQ(buffer[kept], '\0');
        }
        NAOS_CHECK_INT_EQ(buffer[size], 'x');
    }
}

/*
 * 100,000 nested emphases in a paragraph are written without recursion:
 * "(doc (p" + " (em" * N + ")" * (N + 2).
 */
#define DEEP_INLINE_LENGTH (7u + 4u * DEEP_COUNT + DEEP_COUNT + 2u)

static char g_deep_inline_buffer[DEEP_INLINE_LENGTH + 1];

NAOS_TEST(tree_deep_inline_nesting_without_recursion)
{
    amark_node_id_t parent;
    size_t i;
    size_t pos;
    int same = 1;

    NAOS_REQUIRE_INT_EQ(create_empty(), AMARK_ERROR_NONE);
    parent = add(1, AMARK_KIND_PARAGRAPH);
    NAOS_REQUIRE_UINT_NE(parent, AMARK_NODE_NONE);
    for (i = 0; i < DEEP_COUNT; i++) {
        parent = add(parent, AMARK_KIND_EMPHASIS);
        NAOS_REQUIRE_UINT_NE(parent, AMARK_NODE_NONE);
    }

    NAOS_CHECK_UINT_EQ(amark_test_dump_tree(g_doc, g_deep_inline_buffer,
                                            sizeof(g_deep_inline_buffer)),
                       DEEP_INLINE_LENGTH);
    NAOS_CHECK_INT_EQ(memcmp(g_deep_inline_buffer, "(doc (p", 7), 0);
    pos = 7;
    for (i = 0; i < DEEP_COUNT; i++, pos += 4) {
        if (memcmp(g_deep_inline_buffer + pos, " (em", 4) != 0) {
            same = 0;
        }
    }
    for (i = 0; i < DEEP_COUNT + 2u; i++, pos++) {
        if (g_deep_inline_buffer[pos] != ')') {
            same = 0;
        }
    }
    NAOS_CHECK_INT_EQ(same, 1);
    NAOS_CHECK_INT_EQ(g_deep_inline_buffer[pos], '\0');
}

NAOS_SUITE(tree_dump,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(design_example_with_every_block_kind),
    NAOS_ADD_TEST(document_with_no_children),
    NAOS_ADD_TEST(null_document_gives_the_empty_string),
    NAOS_ADD_TEST(other_attribute_values_and_nesting),
    NAOS_ADD_TEST(inline_nodes_are_left_out),
    NAOS_ADD_TEST(missing_attributes_and_other_kinds),
    NAOS_ADD_TEST(cut_strings_for_every_size),
    NAOS_ADD_TEST(deep_nesting_without_recursion),
    NAOS_ADD_TEST(tree_every_inline_kind_and_link_form),
    NAOS_ADD_TEST(tree_destination_and_title_none_and_empty),
    NAOS_ADD_TEST(tree_escapes_in_quoted_strings),
    NAOS_ADD_TEST(tree_missing_attributes_and_other_kinds),
    NAOS_ADD_TEST(tree_blocks_as_in_the_block_dump),
    NAOS_ADD_TEST(tree_null_document_gives_the_empty_string),
    NAOS_ADD_TEST(tree_cut_strings_for_every_size),
    NAOS_ADD_TEST(tree_deep_inline_nesting_without_recursion)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(tree_dump)
)
