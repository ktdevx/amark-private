/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of the tree dump of the test support (tests/support/tree_dump.h):
 * trees built by hand with the tree builder (src/tree_internal.h), holding
 * every block kind and attribute, are written in the format of the design,
 * inline nodes are left out, a deep tree is written without recursion, and
 * a buffer that is too small gets a cut, NUL-terminated string and the
 * length of the whole one.
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
    NAOS_ADD_TEST(deep_nesting_without_recursion)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(tree_dump)
)
