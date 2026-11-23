/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of the tree check (src/tree_check.h): the structure, range,
 * line, nesting and attribute conditions, the first violation in document
 * order, the subtree check, that the check changes nothing and allocates
 * nothing, and that it ends on trees of a million nodes and on cycles.
 * Valid trees are built with the tree builder (src/tree.h); broken trees are
 * made by passing wrong ranges to it or by writing the node fields through
 * the internal layout (src/document.h).
 */

#include "document.h"
#include "test_alloc.h"
#include "tree.h"
#include "tree_check.h"

#include <naos/naos.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * Fixture. NAOS_REQUIRE_* leaves a test with longjmp, so the document a test
 * creates is kept in g_doc, and teardown (which always runs) restores a node
 * array length a test changed, destroys it and then checks that no block is
 * left and that every call received the context value it was given.
 */
static amark_test_alloc_t g_state;
static amark_allocator_t g_allocator;
static amark_document_t *g_doc;
static size_t g_saved_len; /* 0: the length of g_doc's node array is real */

NAOS_SETUP(counting)
{
    amark_test_alloc_init(&g_state);
    g_allocator = amark_test_allocator(&g_state);
    g_doc = NULL;
    g_saved_len = 0;
}

NAOS_TEARDOWN(counting)
{
    if (g_doc != NULL && g_saved_len != 0) {
        g_doc->nodes.len = g_saved_len;
    }
    g_saved_len = 0;
    amark_document_destroy(g_doc);
    g_doc = NULL;
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
    NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
}

/*
 * "ab\n" is line 1 (offsets 0-2), "cd\r\n" line 2 (3-6), "ef\r" line 3
 * (7-9) and "gh" line 4 (10-11); offset 12 (the length) is on line 4.
 */
static const char k_mixed[] = "ab\ncd\r\nef\rgh";
#define MIXED_LENGTH (sizeof(k_mixed) - 1)

static amark_node_t *node_at(amark_node_id_t id)
{
    return (amark_node_t *)amark_array_at(&g_doc->nodes, id);
}

/* Creates g_doc over the whole of k_mixed; leaves the test on failure. */
static void create_doc(void)
{
    NAOS_REQUIRE_INT_EQ(amark_document_create(&g_allocator,
                                              (const unsigned char *)k_mixed,
                                              MIXED_LENGTH, 0, MIXED_LENGTH,
                                              "t.md", &g_doc),
                        AMARK_ERROR_NONE);
}

/* Appends a node of the given class; leaves the test on failure. */
static amark_node_id_t add_class(amark_node_id_t parent, amark_kind_t kind,
                                 amark_node_class_t cls, size_t start,
                                 size_t end)
{
    amark_node_id_t id = AMARK_NODE_NONE;

    NAOS_REQUIRE_INT_EQ(amark_tree_append(g_doc, parent, kind, cls, start,
                                          end, &id),
                        AMARK_ERROR_NONE);
    return id;
}

static amark_node_id_t add(amark_node_id_t parent, amark_kind_t kind,
                           size_t start, size_t end)
{
    return add_class(parent, kind, AMARK_NODE_CLASS_AUTO, start, end);
}

/*
 * The valid tree that most tests start from. IDs in document order, except
 * the removed ones:
 *
 *   1 document [0,12)
 *     2 heading [0,3)
 *       3 text [0,2)
 *       4 emphasis [2,3)
 *         5 text [2,3)
 *     6 block quote [3,12)
 *       7 paragraph [3,7)
 *         8 text [3,5)
 *         9 dialect inline [5,7)
 *       10 list [7,12)
 *         11 list item [7,12)
 *           12 dialect block [7,10)
 *           13 paragraph [10,12)
 *             14 code span [10,12)
 *             15 text [12,12), removed
 *     16 thematic break [12,12)
 *     17 paragraph [12,12), removed
 *       18 text [12,12), not removed but unreachable
 */
enum {
    N_ROOT = 1,
    N_HEADING,
    N_TEXT1,
    N_EMPH,
    N_TEXT2,
    N_QUOTE,
    N_PARA,
    N_TEXT3,
    N_DINLINE,
    N_LIST,
    N_ITEM,
    N_DBLOCK,
    N_PARA2,
    N_CODE,
    N_REMOVED,
    N_BREAK,
    N_REMOVED_PARENT,
    N_ORPHAN,
    N_COUNT = N_ORPHAN
};

static const amark_node_id_t k_reachable[] = {
    N_ROOT, N_HEADING, N_TEXT1, N_EMPH, N_TEXT2, N_QUOTE, N_PARA, N_TEXT3,
    N_DINLINE, N_LIST, N_ITEM, N_DBLOCK, N_PARA2, N_CODE, N_BREAK
};

static void build_valid(void)
{
    create_doc();
    NAOS_REQUIRE_UINT_EQ(add(N_ROOT, AMARK_KIND_HEADING, 0, 3), N_HEADING);
    NAOS_REQUIRE_UINT_EQ(add(N_HEADING, AMARK_KIND_TEXT, 0, 2), N_TEXT1);
    NAOS_REQUIRE_UINT_EQ(add(N_HEADING, AMARK_KIND_EMPHASIS, 2, 3), N_EMPH);
    NAOS_REQUIRE_UINT_EQ(add(N_EMPH, AMARK_KIND_TEXT, 2, 3), N_TEXT2);
    NAOS_REQUIRE_UINT_EQ(add(N_ROOT, AMARK_KIND_BLOCK_QUOTE, 3, 12),
                         N_QUOTE);
    NAOS_REQUIRE_UINT_EQ(add(N_QUOTE, AMARK_KIND_PARAGRAPH, 3, 7), N_PARA);
    NAOS_REQUIRE_UINT_EQ(add(N_PARA, AMARK_KIND_TEXT, 3, 5), N_TEXT3);
    NAOS_REQUIRE_UINT_EQ(add_class(N_PARA, AMARK_KIND_DIALECT_MIN,
                                   AMARK_NODE_CLASS_INLINE, 5, 7),
                         N_DINLINE);
    NAOS_REQUIRE_UINT_EQ(add(N_QUOTE, AMARK_KIND_LIST, 7, 12), N_LIST);
    NAOS_REQUIRE_UINT_EQ(add(N_LIST, AMARK_KIND_LIST_ITEM, 7, 12), N_ITEM);
    NAOS_REQUIRE_UINT_EQ(add_class(N_ITEM, AMARK_KIND_DIALECT_MIN + 1,
                                   AMARK_NODE_CLASS_BLOCK, 7, 10),
                         N_DBLOCK);
    NAOS_REQUIRE_UINT_EQ(add(N_ITEM, AMARK_KIND_PARAGRAPH, 10, 12), N_PARA2);
    NAOS_REQUIRE_UINT_EQ(add(N_PARA2, AMARK_KIND_CODE_SPAN, 10, 12), N_CODE);
    NAOS_REQUIRE_UINT_EQ(add(N_PARA2, AMARK_KIND_TEXT, 12, 12), N_REMOVED);
    NAOS_REQUIRE_UINT_EQ(add(N_ROOT, AMARK_KIND_THEMATIC_BREAK, 12, 12),
                         N_BREAK);
    NAOS_REQUIRE_UINT_EQ(add(N_ROOT, AMARK_KIND_PARAGRAPH, 12, 12),
                         N_REMOVED_PARENT);
    NAOS_REQUIRE_UINT_EQ(add(N_REMOVED_PARENT, AMARK_KIND_TEXT, 12, 12),
                         N_ORPHAN);
    amark_tree_remove(g_doc, N_REMOVED);
    amark_tree_remove(g_doc, N_REMOVED_PARENT);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_heading(g_doc, N_HEADING, 1,
                                               AMARK_HEADING_ATX),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_list(g_doc, N_LIST, AMARK_LIST_BULLET,
                                            '-', 0, AMARK_LIST_TIGHT),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_UINT_EQ(amark_document_node_count(g_doc), N_COUNT);
}

static void check_result(amark_tree_check_result_t r,
                         amark_tree_violation_t violation,
                         amark_node_id_t node)
{
    NAOS_CHECK_INT_EQ(r.violation, violation);
    NAOS_CHECK_UINT_EQ(r.node, node);
}

#define CHECK_TREE(violation, node)                                         \
    check_result(amark_tree_check(g_doc), (violation), (node))
#define CHECK_SUBTREE(root, violation, node)                                \
    check_result(amark_tree_check_subtree(g_doc, (root)), (violation),      \
                 (node))

/* ---- a valid tree (6.6) ---- */

NAOS_TEST(valid_tree_has_no_violation)
{
    size_t i;

    build_valid();
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
    for (i = 0; i < sizeof(k_reachable) / sizeof(k_reachable[0]); i++) {
        CHECK_SUBTREE(k_reachable[i], AMARK_TREE_VIOLATION_NONE,
                      AMARK_NODE_NONE);
    }
}

NAOS_TEST(root_only_and_empty_content)
{
    NAOS_REQUIRE_INT_EQ(amark_document_create(&g_allocator, NULL, 0, 0, 0,
                                              NULL, &g_doc),
                        AMARK_ERROR_NONE);
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
    CHECK_SUBTREE(N_ROOT, AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
    /* Zero-length children at the end are adjacent, not overlapping. */
    add(N_ROOT, AMARK_KIND_THEMATIC_BREAK, 0, 0);
    add(N_ROOT, AMARK_KIND_PARAGRAPH, 0, 0);
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
}

/* ---- ROOT (6.1) ---- */

NAOS_TEST(root_conditions)
{
    amark_node_t *root;

    build_valid();
    root = node_at(N_ROOT);

    root->kind = AMARK_KIND_PARAGRAPH;
    CHECK_TREE(AMARK_TREE_VIOLATION_ROOT, N_ROOT);
    root->kind = AMARK_KIND_DOCUMENT;

    root->parent = N_HEADING;
    CHECK_TREE(AMARK_TREE_VIOLATION_ROOT, N_ROOT);
    root->parent = AMARK_NODE_NONE;

    root->prev_sibling = N_QUOTE;
    CHECK_TREE(AMARK_TREE_VIOLATION_ROOT, N_ROOT);
    root->prev_sibling = AMARK_NODE_NONE;

    root->next_sibling = N_BREAK;
    CHECK_TREE(AMARK_TREE_VIOLATION_ROOT, N_ROOT);
    root->next_sibling = AMARK_NODE_NONE;
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);

    /* No root element at all, and no document. */
    g_saved_len = g_doc->nodes.len;
    g_doc->nodes.len = 1;
    CHECK_TREE(AMARK_TREE_VIOLATION_ROOT, AMARK_NODE_NONE);
    g_doc->nodes.len = g_saved_len;
    g_saved_len = 0;
    check_result(amark_tree_check(NULL), AMARK_TREE_VIOLATION_ROOT,
                 AMARK_NODE_NONE);
}

/* ---- LINK (6.1, 6.11) ---- */

NAOS_TEST(link_parent_mismatch)
{
    build_valid();

    /* The first child names another parent: found on the parent. */
    node_at(N_TEXT3)->parent = N_QUOTE;
    CHECK_TREE(AMARK_TREE_VIOLATION_LINK, N_PARA);
    node_at(N_TEXT3)->parent = N_PARA;

    /* A later child names another parent: found on its previous sibling. */
    node_at(N_DINLINE)->parent = N_QUOTE;
    CHECK_TREE(AMARK_TREE_VIOLATION_LINK, N_TEXT3);
    node_at(N_DINLINE)->parent = N_PARA;

    /* The last child of the parent is not the node without a next sibling:
     * found on that node. */
    node_at(N_PARA2)->last_child = N_TEXT2;
    CHECK_TREE(AMARK_TREE_VIOLATION_LINK, N_CODE);
    node_at(N_PARA2)->last_child = N_CODE;

    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
}

NAOS_TEST(link_sibling_and_child_conditions)
{
    build_valid();

    /* The first child has a previous sibling. */
    node_at(N_TEXT1)->prev_sibling = N_EMPH;
    CHECK_TREE(AMARK_TREE_VIOLATION_LINK, N_HEADING);
    node_at(N_TEXT1)->prev_sibling = AMARK_NODE_NONE;

    /* The last child has a next sibling. */
    node_at(N_HEADING)->last_child = N_TEXT1;
    CHECK_TREE(AMARK_TREE_VIOLATION_LINK, N_HEADING);
    /* Only one of the first and the last child is present. */
    node_at(N_HEADING)->last_child = AMARK_NODE_NONE;
    CHECK_TREE(AMARK_TREE_VIOLATION_LINK, N_HEADING);
    node_at(N_HEADING)->last_child = N_EMPH;
    node_at(N_TEXT2)->last_child = N_TEXT2;
    CHECK_TREE(AMARK_TREE_VIOLATION_LINK, N_TEXT2);
    node_at(N_TEXT2)->last_child = AMARK_NODE_NONE;

    /* The next sibling names another previous sibling. */
    node_at(N_LIST)->prev_sibling = N_ITEM;
    CHECK_TREE(AMARK_TREE_VIOLATION_LINK, N_PARA);
    node_at(N_LIST)->prev_sibling = N_PARA;

    /* A removed node that is reachable. */
    node_at(N_CODE)->kind = AMARK_KIND_NONE;
    CHECK_TREE(AMARK_TREE_VIOLATION_LINK, N_CODE);
    node_at(N_CODE)->kind = AMARK_KIND_CODE_SPAN;

    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
}

NAOS_TEST(link_index_out_of_range)
{
    static const amark_node_id_t k_bad[] = {N_COUNT + 1, 0x7fffffffu,
                                            UINT32_MAX};
    size_t i;
    amark_node_t *n;
    amark_node_t saved;

    build_valid();
    n = node_at(N_PARA);
    saved = *n;
    for (i = 0; i < sizeof(k_bad) / sizeof(k_bad[0]); i++) {
        /* The node's own forward indices: found on the node. */
        n->first_child = k_bad[i];
        CHECK_TREE(AMARK_TREE_VIOLATION_LINK, N_PARA);
        *n = saved;
        n->last_child = k_bad[i];
        CHECK_TREE(AMARK_TREE_VIOLATION_LINK, N_PARA);
        *n = saved;
        n->next_sibling = k_bad[i];
        CHECK_TREE(AMARK_TREE_VIOLATION_LINK, N_PARA);
        *n = saved;
        /* The parent and previous sibling indices are first compared by the
         * parent (the node is its first child), without being followed. */
        n->parent = k_bad[i];
        CHECK_TREE(AMARK_TREE_VIOLATION_LINK, N_QUOTE);
        *n = saved;
        n->prev_sibling = k_bad[i];
        CHECK_TREE(AMARK_TREE_VIOLATION_LINK, N_QUOTE);
        *n = saved;
        /* A later sibling's out-of-range previous sibling is found on the
         * node before it. */
        node_at(N_LIST)->prev_sibling = k_bad[i];
        CHECK_TREE(AMARK_TREE_VIOLATION_LINK, N_PARA);
        node_at(N_LIST)->prev_sibling = N_PARA;
        /* Every out-of-range index makes the subtree check stop too. */
        n->first_child = k_bad[i];
        CHECK_SUBTREE(N_PARA, AMARK_TREE_VIOLATION_LINK, N_PARA);
        CHECK_SUBTREE(N_QUOTE, AMARK_TREE_VIOLATION_LINK, N_PARA);
        *n = saved;
        n->parent = k_bad[i];
        CHECK_SUBTREE(N_PARA, AMARK_TREE_VIOLATION_LINK, N_PARA);
        *n = saved;
        n->prev_sibling = k_bad[i];
        CHECK_SUBTREE(N_PARA, AMARK_TREE_VIOLATION_LINK, N_PARA);
        *n = saved;
    }
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
}

NAOS_TEST(link_cycles_end_the_check)
{
    build_valid();

    /* The next sibling is the node itself. */
    node_at(N_TEXT3)->next_sibling = N_TEXT3;
    CHECK_TREE(AMARK_TREE_VIOLATION_LINK, N_TEXT3);
    CHECK_SUBTREE(N_PARA, AMARK_TREE_VIOLATION_LINK, N_TEXT3);
    CHECK_SUBTREE(N_TEXT3, AMARK_TREE_VIOLATION_LINK, N_TEXT3);
    node_at(N_TEXT3)->next_sibling = N_DINLINE;

    /* The last child's next sibling goes back to an earlier sibling: found
     * on the parent (its last child has a next sibling), and by the subtree
     * check of the node itself. */
    node_at(N_DINLINE)->next_sibling = N_TEXT3;
    CHECK_TREE(AMARK_TREE_VIOLATION_LINK, N_PARA);
    CHECK_SUBTREE(N_DINLINE, AMARK_TREE_VIOLATION_LINK, N_DINLINE);
    node_at(N_DINLINE)->next_sibling = AMARK_NODE_NONE;

    /* A leaf's first child is the root, or the node itself. */
    node_at(N_TEXT1)->first_child = N_ROOT;
    node_at(N_TEXT1)->last_child = N_ROOT;
    CHECK_TREE(AMARK_TREE_VIOLATION_LINK, N_TEXT1);
    node_at(N_TEXT1)->first_child = N_TEXT1;
    node_at(N_TEXT1)->last_child = N_TEXT1;
    CHECK_TREE(AMARK_TREE_VIOLATION_LINK, N_TEXT1);
    CHECK_SUBTREE(N_TEXT1, AMARK_TREE_VIOLATION_LINK, N_TEXT1);
    node_at(N_TEXT1)->first_child = AMARK_NODE_NONE;
    node_at(N_TEXT1)->last_child = AMARK_NODE_NONE;

    /* A node is its own parent. */
    node_at(N_CODE)->parent = N_CODE;
    CHECK_TREE(AMARK_TREE_VIOLATION_LINK, N_PARA2);
    CHECK_SUBTREE(N_CODE, AMARK_TREE_VIOLATION_LINK, N_CODE);
    node_at(N_CODE)->parent = N_PARA2;

    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
}

/* ---- BOUNDS (6.2) ---- */

NAOS_TEST(bounds_conditions)
{
    build_valid();

    /* The end is past the content. */
    node_at(N_ROOT)->end = MIXED_LENGTH + 1;
    CHECK_TREE(AMARK_TREE_VIOLATION_BOUNDS, N_ROOT);
    node_at(N_ROOT)->end = MIXED_LENGTH;

    /* The start is after the end. */
    node_at(N_TEXT1)->start = 2;
    node_at(N_TEXT1)->end = 1;
    CHECK_TREE(AMARK_TREE_VIOLATION_BOUNDS, N_TEXT1);
    CHECK_SUBTREE(N_TEXT1, AMARK_TREE_VIOLATION_BOUNDS, N_TEXT1);
    node_at(N_TEXT1)->start = 0;
    node_at(N_TEXT1)->end = 2;

    /* A huge end does not reach the line index. */
    node_at(N_BREAK)->end = SIZE_MAX;
    CHECK_TREE(AMARK_TREE_VIOLATION_BOUNDS, N_BREAK);
    node_at(N_BREAK)->end = 12;

    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
}

/* ---- LINES (6.3) ---- */

NAOS_TEST(lines_conditions)
{
    build_valid();

    node_at(N_TEXT3)->end_line += 1;
    CHECK_TREE(AMARK_TREE_VIOLATION_LINES, N_TEXT3);
    node_at(N_TEXT3)->end_line -= 1;

    node_at(N_DBLOCK)->start_line -= 1;
    CHECK_TREE(AMARK_TREE_VIOLATION_LINES, N_DBLOCK);
    node_at(N_DBLOCK)->start_line += 1;

    /* A zero-length node: both lines are the line of start. */
    node_at(N_BREAK)->end_line = 5;
    CHECK_TREE(AMARK_TREE_VIOLATION_LINES, N_BREAK);
    node_at(N_BREAK)->end_line = 4;

    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
}

/* ---- PARENT_RANGE (6.2) ---- */

NAOS_TEST(parent_range_conditions)
{
    build_valid();

    /* The child starts before its parent. */
    amark_tree_set_range(g_doc, N_TEXT3, 2, 5);
    CHECK_TREE(AMARK_TREE_VIOLATION_PARENT_RANGE, N_TEXT3);
    amark_tree_set_range(g_doc, N_TEXT3, 3, 5);

    /* The child ends after its parent. */
    amark_tree_set_range(g_doc, N_DINLINE, 5, 8);
    CHECK_TREE(AMARK_TREE_VIOLATION_PARENT_RANGE, N_DINLINE);
    amark_tree_set_range(g_doc, N_DINLINE, 5, 7);

    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
}

/* ---- SIBLING_ORDER (6.2) ---- */

NAOS_TEST(sibling_order_conditions)
{
    build_valid();

    /* Siblings overlapping by one byte. */
    amark_tree_set_range(g_doc, N_DINLINE, 4, 7);
    CHECK_TREE(AMARK_TREE_VIOLATION_SIBLING_ORDER, N_DINLINE);
    amark_tree_set_range(g_doc, N_DINLINE, 5, 7);

    /* Siblings in the reverse order. */
    amark_tree_set_range(g_doc, N_TEXT3, 5, 7);
    amark_tree_set_range(g_doc, N_DINLINE, 3, 5);
    CHECK_TREE(AMARK_TREE_VIOLATION_SIBLING_ORDER, N_DINLINE);
    amark_tree_set_range(g_doc, N_TEXT3, 3, 5);
    amark_tree_set_range(g_doc, N_DINLINE, 5, 7);

    /* Adjacent siblings (end == next start) are not a violation, also when
     * one of them is empty. */
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
    amark_tree_set_range(g_doc, N_TEXT3, 3, 3);
    amark_tree_set_range(g_doc, N_DINLINE, 3, 7);
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
}

/* ---- NESTING (6.4) ---- */

NAOS_TEST(nesting_conditions)
{
    amark_node_id_t id;

    build_valid();

    /* A text as a child of the document. */
    id = add(N_ROOT, AMARK_KIND_TEXT, 12, 12);
    CHECK_TREE(AMARK_TREE_VIOLATION_NESTING, id);
    amark_tree_remove(g_doc, id);

    /* A dialect inline as a child of the document. */
    id = add_class(N_ROOT, AMARK_KIND_DIALECT_MIN, AMARK_NODE_CLASS_INLINE,
                   12, 12);
    CHECK_TREE(AMARK_TREE_VIOLATION_NESTING, id);
    amark_tree_remove(g_doc, id);

    /* A paragraph as a child of an emphasis. */
    id = add(N_EMPH, AMARK_KIND_PARAGRAPH, 3, 3);
    CHECK_TREE(AMARK_TREE_VIOLATION_NESTING, id);
    amark_tree_remove(g_doc, id);

    /* A dialect block as a child of an inline. */
    id = add_class(N_DINLINE, AMARK_KIND_DIALECT_MIN, AMARK_NODE_CLASS_BLOCK,
                   5, 7);
    CHECK_TREE(AMARK_TREE_VIOLATION_NESTING, id);
    amark_tree_remove(g_doc, id);
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);

    /* The class of a CommonMark kind does not match its ID. */
    node_at(N_PARA)->is_inline = 1;
    CHECK_TREE(AMARK_TREE_VIOLATION_NESTING, N_PARA);
    node_at(N_PARA)->is_inline = 0;
    node_at(N_CODE)->is_inline = 0;
    CHECK_TREE(AMARK_TREE_VIOLATION_NESTING, N_CODE);
    node_at(N_CODE)->is_inline = 1;
    node_at(N_ROOT)->is_inline = 1;
    CHECK_TREE(AMARK_TREE_VIOLATION_NESTING, N_ROOT);
    node_at(N_ROOT)->is_inline = 0;

    /* Dialect blocks may hold blocks and sit under blocks other than the
     * document; dialect inlines may hold inlines. */
    add(N_DBLOCK, AMARK_KIND_PARAGRAPH, 7, 10);
    add(N_DINLINE, AMARK_KIND_TEXT, 5, 7);
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
}

/* ---- the first violation (6.7) ---- */

NAOS_TEST(first_violation_in_document_order)
{
    amark_node_id_t early = AMARK_NODE_NONE;

    build_valid();

    /* Node 19 comes before node 6 in document order. */
    NAOS_REQUIRE_INT_EQ(amark_tree_insert_before(g_doc, N_QUOTE,
                                                 AMARK_KIND_THEMATIC_BREAK,
                                                 AMARK_NODE_CLASS_AUTO, 3, 3,
                                                 &early),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_UINT_EQ(early, N_COUNT + 1);
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);

    node_at(N_TEXT3)->end_line += 1;  /* LINES on node 8 */
    node_at(early)->end = 13;         /* BOUNDS on node 19 */
    CHECK_TREE(AMARK_TREE_VIOLATION_BOUNDS, early);
    node_at(early)->end = 3;
    CHECK_TREE(AMARK_TREE_VIOLATION_LINES, N_TEXT3);

    /* Two violations on nodes in ID order: the first one. */
    node_at(N_TEXT1)->kind = AMARK_KIND_NONE;
    CHECK_TREE(AMARK_TREE_VIOLATION_LINK, N_TEXT1);
    node_at(N_TEXT1)->kind = AMARK_KIND_TEXT;
    node_at(N_TEXT3)->end_line -= 1;
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
}

NAOS_TEST(conditions_of_one_node_in_table_order)
{
    amark_node_t *n;

    build_valid();
    /* As a CommonMark inline kind, a block class breaks NESTING. */
    n = node_at(N_DINLINE);
    n->kind = AMARK_KIND_TEXT;
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);

    /* PARENT_RANGE, SIBLING_ORDER and NESTING at once: PARENT_RANGE. */
    amark_tree_set_range(g_doc, N_DINLINE, 4, 8);
    n->is_inline = 0;
    CHECK_TREE(AMARK_TREE_VIOLATION_PARENT_RANGE, N_DINLINE);
    /* With wrong lines as well: LINES. */
    n->end_line += 1;
    CHECK_TREE(AMARK_TREE_VIOLATION_LINES, N_DINLINE);
    /* With a start after the end as well: BOUNDS. */
    n->start = 9;
    CHECK_TREE(AMARK_TREE_VIOLATION_BOUNDS, N_DINLINE);
    /* With a first child out of range as well: LINK. */
    n->first_child = N_COUNT + 5;
    CHECK_TREE(AMARK_TREE_VIOLATION_LINK, N_DINLINE);
    n->first_child = AMARK_NODE_NONE;

    /* SIBLING_ORDER before NESTING. */
    n->is_inline = 1;
    amark_tree_set_range(g_doc, N_DINLINE, 4, 7);
    n->is_inline = 0;
    CHECK_TREE(AMARK_TREE_VIOLATION_SIBLING_ORDER, N_DINLINE);
    n->is_inline = 1;
    amark_tree_set_range(g_doc, N_DINLINE, 5, 7);
    n->is_inline = 0;
    CHECK_TREE(AMARK_TREE_VIOLATION_NESTING, N_DINLINE);
    n->is_inline = 1;
    n->kind = AMARK_KIND_DIALECT_MIN;

    /* On the root, ROOT before everything else. */
    node_at(N_ROOT)->kind = AMARK_KIND_PARAGRAPH;
    node_at(N_ROOT)->first_child = N_COUNT + 1;
    CHECK_TREE(AMARK_TREE_VIOLATION_ROOT, N_ROOT);
    node_at(N_ROOT)->kind = AMARK_KIND_DOCUMENT;
    CHECK_TREE(AMARK_TREE_VIOLATION_LINK, N_ROOT);
    node_at(N_ROOT)->first_child = N_HEADING;
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
}

/* ---- the subtree check (6.8) ---- */

NAOS_TEST(subtree_ids_that_are_not_nodes)
{
    build_valid();
    CHECK_SUBTREE(AMARK_NODE_NONE, AMARK_TREE_VIOLATION_LINK,
                  AMARK_NODE_NONE);
    CHECK_SUBTREE(N_COUNT + 1, AMARK_TREE_VIOLATION_LINK, N_COUNT + 1);
    CHECK_SUBTREE(UINT32_MAX, AMARK_TREE_VIOLATION_LINK, UINT32_MAX);
    check_result(amark_tree_check_subtree(NULL, N_ROOT),
                 AMARK_TREE_VIOLATION_LINK, N_ROOT);
    /* A removed node, and a node under a removed node. */
    CHECK_SUBTREE(N_REMOVED, AMARK_TREE_VIOLATION_LINK, N_REMOVED);
    CHECK_SUBTREE(N_REMOVED_PARENT, AMARK_TREE_VIOLATION_LINK,
                  N_REMOVED_PARENT);
    CHECK_SUBTREE(N_ORPHAN, AMARK_TREE_VIOLATION_LINK, N_ORPHAN);
}

NAOS_TEST(subtree_ignores_violations_outside)
{
    build_valid();
    node_at(N_TEXT1)->end_line += 1;
    node_at(N_TEXT3)->end_line += 1;
    node_at(N_BREAK)->end = 13;
    CHECK_TREE(AMARK_TREE_VIOLATION_LINES, N_TEXT1);

    CHECK_SUBTREE(N_LIST, AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
    CHECK_SUBTREE(N_EMPH, AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
    CHECK_SUBTREE(N_DINLINE, AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
    /* Violations inside are found, the first in document order. */
    CHECK_SUBTREE(N_QUOTE, AMARK_TREE_VIOLATION_LINES, N_TEXT3);
    CHECK_SUBTREE(N_HEADING, AMARK_TREE_VIOLATION_LINES, N_TEXT1);
    CHECK_SUBTREE(N_BREAK, AMARK_TREE_VIOLATION_BOUNDS, N_BREAK);

    /* The subtree check of the root does not check ROOT. */
    node_at(N_TEXT1)->end_line -= 1;
    node_at(N_TEXT3)->end_line -= 1;
    node_at(N_BREAK)->end = 12;
    node_at(N_ROOT)->kind = AMARK_KIND_BLOCK_QUOTE;
    CHECK_TREE(AMARK_TREE_VIOLATION_ROOT, N_ROOT);
    CHECK_SUBTREE(N_ROOT, AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
}

NAOS_TEST(subtree_root_and_the_nodes_around_it)
{
    amark_node_id_t id;

    build_valid();

    /* Outside its parent. */
    amark_tree_set_range(g_doc, N_DBLOCK, 6, 10);
    CHECK_SUBTREE(N_DBLOCK, AMARK_TREE_VIOLATION_PARENT_RANGE, N_DBLOCK);
    amark_tree_set_range(g_doc, N_DBLOCK, 7, 10);

    /* Overlapping its previous sibling. */
    amark_tree_set_range(g_doc, N_DINLINE, 4, 7);
    CHECK_SUBTREE(N_DINLINE, AMARK_TREE_VIOLATION_SIBLING_ORDER, N_DINLINE);
    amark_tree_set_range(g_doc, N_DINLINE, 5, 7);

    /* Overlapping its next sibling: the whole check finds it on the next
     * sibling; the subtree check of the earlier one finds it too. */
    amark_tree_set_range(g_doc, N_TEXT3, 3, 6);
    CHECK_TREE(AMARK_TREE_VIOLATION_SIBLING_ORDER, N_DINLINE);
    CHECK_SUBTREE(N_TEXT3, AMARK_TREE_VIOLATION_SIBLING_ORDER, N_TEXT3);
    CHECK_SUBTREE(N_DINLINE, AMARK_TREE_VIOLATION_SIBLING_ORDER, N_DINLINE);
    amark_tree_set_range(g_doc, N_TEXT3, 3, 5);

    /* Nested wrongly under its parent. */
    id = add_class(N_EMPH, AMARK_KIND_DIALECT_MIN, AMARK_NODE_CLASS_BLOCK, 3,
                   3);
    CHECK_SUBTREE(id, AMARK_TREE_VIOLATION_NESTING, id);
    CHECK_SUBTREE(N_TEXT2, AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
    amark_tree_remove(g_doc, id);

    /* The previous sibling does not name the node as its next sibling. */
    node_at(N_TEXT3)->next_sibling = AMARK_NODE_NONE;
    CHECK_SUBTREE(N_DINLINE, AMARK_TREE_VIOLATION_LINK, N_DINLINE);
    node_at(N_TEXT3)->next_sibling = N_DINLINE;
    /* The previous sibling has another parent. */
    node_at(N_TEXT3)->parent = N_QUOTE;
    CHECK_SUBTREE(N_DINLINE, AMARK_TREE_VIOLATION_LINK, N_DINLINE);
    node_at(N_TEXT3)->parent = N_PARA;

    /* Without a previous sibling, the parent's first child is another. */
    node_at(N_ITEM)->first_child = N_PARA2;
    CHECK_SUBTREE(N_DBLOCK, AMARK_TREE_VIOLATION_LINK, N_DBLOCK);
    node_at(N_ITEM)->first_child = N_DBLOCK;

    /* A node that is not the root has no parent. */
    node_at(N_DBLOCK)->parent = AMARK_NODE_NONE;
    CHECK_SUBTREE(N_DBLOCK, AMARK_TREE_VIOLATION_LINK, N_DBLOCK);
    node_at(N_DBLOCK)->parent = N_ITEM;

    /* The next sibling does not name the node as its previous sibling. */
    node_at(N_PARA2)->prev_sibling = AMARK_NODE_NONE;
    CHECK_SUBTREE(N_DBLOCK, AMARK_TREE_VIOLATION_LINK, N_DBLOCK);
    node_at(N_PARA2)->prev_sibling = N_DBLOCK;

    /* Violations of the next sibling's subtree are not reported. */
    node_at(N_CODE)->end_line += 1;
    CHECK_SUBTREE(N_DBLOCK, AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
    CHECK_SUBTREE(N_ITEM, AMARK_TREE_VIOLATION_LINES, N_CODE);
    node_at(N_CODE)->end_line -= 1;

    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
}

/* ---- no change, no allocation (6.9, 6.11) ---- */

#define SNAP_NODES 32
static amark_node_t g_snap_nodes[SNAP_NODES];

/* Runs both checks on g_doc and checks that the node bytes are unchanged
 * and that the allocator was not called. */
static void check_without_effects(void)
{
    size_t len = amark_array_len(&g_doc->nodes);
    size_t mallocs = g_state.malloc_calls;
    size_t frees = g_state.free_calls;
    size_t i;

    NAOS_REQUIRE_UINT_LE(len, SNAP_NODES);
    memcpy(g_snap_nodes, amark_array_at(&g_doc->nodes, 0),
           len * sizeof(amark_node_t));
    (void)amark_tree_check(g_doc);
    for (i = 0; i <= len; i++) {
        (void)amark_tree_check_subtree(g_doc, (amark_node_id_t)i);
    }
    NAOS_CHECK_UINT_EQ(amark_array_len(&g_doc->nodes), len);
    NAOS_CHECK_MEM_EQ(amark_array_at(&g_doc->nodes, 0),
                      len * sizeof(amark_node_t), g_snap_nodes,
                      len * sizeof(amark_node_t));
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, mallocs);
    NAOS_CHECK_UINT_EQ(g_state.free_calls, frees);
}

NAOS_TEST(checks_change_nothing_and_allocate_nothing)
{
    build_valid();
    check_without_effects();

    /* Broken trees: a cycle, indices out of range, a wrong range. */
    node_at(N_TEXT3)->next_sibling = N_TEXT3;
    check_without_effects();
    node_at(N_TEXT3)->next_sibling = N_DINLINE;
    node_at(N_PARA)->first_child = UINT32_MAX;
    node_at(N_ITEM)->parent = UINT32_MAX;
    node_at(N_CODE)->prev_sibling = N_COUNT + 1;
    check_without_effects();
    node_at(N_PARA)->first_child = N_TEXT3;
    node_at(N_ITEM)->parent = N_LIST;
    node_at(N_CODE)->prev_sibling = AMARK_NODE_NONE;
    node_at(N_BREAK)->start = 13;
    check_without_effects();
    node_at(N_BREAK)->start = 12;
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
}

/* ---- the trees the builder's operations leave (7.11) ---- */

/* The first violation, if any, is neither LINK nor LINES. */
static void check_links_and_lines_hold(void)
{
    amark_tree_check_result_t r = amark_tree_check(g_doc);

    NAOS_CHECK_INT_NE(r.violation, AMARK_TREE_VIOLATION_LINK);
    NAOS_CHECK_INT_NE(r.violation, AMARK_TREE_VIOLATION_LINES);
}

NAOS_TEST(after_insert_before)
{
    amark_node_id_t para;
    amark_node_id_t t1;
    amark_node_id_t t2;
    amark_node_id_t out = AMARK_NODE_NONE;

    create_doc();
    para = add(N_ROOT, AMARK_KIND_PARAGRAPH, 0, 12);
    t1 = add(para, AMARK_KIND_TEXT, 0, 2);
    t2 = add(para, AMARK_KIND_TEXT, 3, 5);
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);

    /* At the head and in the middle of the paragraph's children. */
    NAOS_REQUIRE_INT_EQ(amark_tree_insert_before(g_doc, t1, AMARK_KIND_TEXT,
                                                 AMARK_NODE_CLASS_AUTO, 0, 0,
                                                 &out),
                        AMARK_ERROR_NONE);
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_insert_before(g_doc, t2,
                                                 AMARK_KIND_SOFT_BREAK,
                                                 AMARK_NODE_CLASS_AUTO, 2, 3,
                                                 &out),
                        AMARK_ERROR_NONE);
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);

    /* At the head of the root's children; it overlaps the paragraph, so
     * only the range conditions fail. */
    NAOS_REQUIRE_INT_EQ(amark_tree_insert_before(g_doc, para,
                                                 AMARK_KIND_THEMATIC_BREAK,
                                                 AMARK_NODE_CLASS_AUTO, 0, 3,
                                                 &out),
                        AMARK_ERROR_NONE);
    check_links_and_lines_hold();
    CHECK_TREE(AMARK_TREE_VIOLATION_SIBLING_ORDER, para);
    amark_tree_set_range(g_doc, out, 0, 0);
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
}

NAOS_TEST(after_set_range_and_set_kind)
{
    amark_node_id_t para;
    amark_node_id_t text;
    amark_node_id_t other;

    create_doc();
    para = add(N_ROOT, AMARK_KIND_PARAGRAPH, 0, 3);
    text = add(para, AMARK_KIND_TEXT, 0, 2);

    amark_tree_set_range(g_doc, para, 0, 12);
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
    amark_tree_set_range(g_doc, para, 3, 6);
    check_links_and_lines_hold();
    CHECK_TREE(AMARK_TREE_VIOLATION_PARENT_RANGE, text);
    amark_tree_set_range(g_doc, para, 6, 9);
    check_links_and_lines_hold();
    amark_tree_set_range(g_doc, para, 10, 10);
    check_links_and_lines_hold();
    amark_tree_set_range(g_doc, para, 12, 12);
    check_links_and_lines_hold();
    amark_tree_set_range(g_doc, para, 0, 7);
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);

    /* A paragraph becomes a setext heading; a CommonMark inline kind and a
     * dialect kind take their classes. */
    other = add(N_ROOT, AMARK_KIND_PARAGRAPH, 7, 12);
    amark_tree_set_kind(g_doc, para, AMARK_KIND_HEADING,
                        AMARK_NODE_CLASS_AUTO);
    check_links_and_lines_hold();
    CHECK_TREE(AMARK_TREE_VIOLATION_ATTRIBUTE, para);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_heading(g_doc, para, 2,
                                               AMARK_HEADING_SETEXT),
                        AMARK_ERROR_NONE);
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
    amark_tree_set_kind(g_doc, other, AMARK_KIND_TEXT, AMARK_NODE_CLASS_AUTO);
    check_links_and_lines_hold();
    CHECK_TREE(AMARK_TREE_VIOLATION_NESTING, other);
    amark_tree_set_kind(g_doc, other, AMARK_KIND_DIALECT_MIN,
                        AMARK_NODE_CLASS_BLOCK);
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
    amark_tree_set_kind(g_doc, text, AMARK_KIND_DIALECT_MIN + 1,
                        AMARK_NODE_CLASS_INLINE);
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
}

NAOS_TEST(after_remove)
{
    amark_node_id_t a;
    amark_node_id_t b;
    amark_node_id_t c;
    amark_node_id_t d;
    amark_node_id_t e;

    create_doc();
    a = add(N_ROOT, AMARK_KIND_THEMATIC_BREAK, 0, 3);
    b = add(N_ROOT, AMARK_KIND_PARAGRAPH, 3, 7);
    add(b, AMARK_KIND_TEXT, 3, 4);
    add(b, AMARK_KIND_TEXT, 4, 5);
    c = add(N_ROOT, AMARK_KIND_BLOCK_QUOTE, 7, 12);
    d = add(c, AMARK_KIND_PARAGRAPH, 7, 12);
    e = add(N_ROOT, AMARK_KIND_PARAGRAPH, 12, 12);
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);

    amark_tree_remove(g_doc, a);  /* the first child */
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
    amark_tree_remove(g_doc, b);  /* a middle child with its subtree */
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
    amark_tree_remove(g_doc, e);  /* the last child */
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
    amark_tree_remove(g_doc, d);  /* the only child of the block quote */
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
    amark_tree_remove(g_doc, c);  /* the only child of the root */
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
    add(N_ROOT, AMARK_KIND_PARAGRAPH, 0, 3);
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
}

/* Wraps first..last; leaves the test on failure. */
static amark_node_id_t wrap(amark_node_id_t first, amark_node_id_t last,
                            amark_kind_t kind, amark_node_class_t cls,
                            size_t start, size_t end)
{
    amark_node_id_t out = AMARK_NODE_NONE;

    NAOS_REQUIRE_INT_EQ(amark_tree_wrap(g_doc, first, last, kind, cls, start,
                                        end, &out),
                        AMARK_ERROR_NONE);
    return out;
}

NAOS_TEST(after_wrap)
{
    amark_node_id_t para;
    amark_node_id_t t[5];
    amark_node_id_t w1;
    amark_node_id_t w3;
    size_t i;

    create_doc();
    para = add(N_ROOT, AMARK_KIND_PARAGRAPH, 0, 12);
    for (i = 0; i < 5; i++) {
        t[i] = add(para, AMARK_KIND_TEXT, 2 * i, 2 * i + 2);
    }
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);

    w1 = wrap(t[0], t[0], AMARK_KIND_EMPHASIS, AMARK_NODE_CLASS_AUTO, 0, 2);
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
    (void)wrap(t[1], t[3], AMARK_KIND_STRONG, AMARK_NODE_CLASS_AUTO, 2, 8);
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
    w3 = wrap(t[4], t[4], AMARK_KIND_DIALECT_MIN, AMARK_NODE_CLASS_INLINE, 8,
              10);
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
    w1 = wrap(w1, w3, AMARK_KIND_LINK, AMARK_NODE_CLASS_AUTO, 0, 10);
    check_links_and_lines_hold();
    CHECK_TREE(AMARK_TREE_VIOLATION_ATTRIBUTE, w1);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_link(g_doc, w1, AMARK_LINK_INLINE,
                                            NULL, NULL, AMARK_NODE_NONE),
                        AMARK_ERROR_NONE);
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
    (void)wrap(para, para, AMARK_KIND_BLOCK_QUOTE, AMARK_NODE_CLASS_AUTO, 0,
               12);
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
}

NAOS_TEST(after_removing_and_wrapping)
{
    amark_node_id_t para;
    amark_node_id_t open;
    amark_node_id_t mid;
    amark_node_id_t close;
    amark_node_id_t emph;

    /* What the inline parser does with "*x*": wrap, remove delimiters. */
    create_doc();
    para = add(N_ROOT, AMARK_KIND_PARAGRAPH, 0, 3);
    open = add(para, AMARK_KIND_TEXT, 0, 1);
    mid = add(para, AMARK_KIND_TEXT, 1, 2);
    close = add(para, AMARK_KIND_TEXT, 2, 3);

    /* The emphasis covers the delimiters, which are still its siblings. */
    emph = wrap(mid, mid, AMARK_KIND_EMPHASIS, AMARK_NODE_CLASS_AUTO, 0, 3);
    check_links_and_lines_hold();
    CHECK_TREE(AMARK_TREE_VIOLATION_SIBLING_ORDER, emph);
    amark_tree_remove(g_doc, open);
    check_links_and_lines_hold();
    CHECK_TREE(AMARK_TREE_VIOLATION_SIBLING_ORDER, close);
    amark_tree_remove(g_doc, close);
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);

    /* The paragraph grows over line 2 and becomes a heading. */
    amark_tree_set_range(g_doc, para, 0, 7);
    amark_tree_set_kind(g_doc, para, AMARK_KIND_HEADING,
                        AMARK_NODE_CLASS_AUTO);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_heading(g_doc, para, 1,
                                               AMARK_HEADING_SETEXT),
                        AMARK_ERROR_NONE);
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
}

/* ---- ATTRIBUTE (6.5, 6.6, 6.7) ---- */

/* Eight lines of ten bytes: "abcdefghi\n" eight times. */
static const char k_full[] =
    "abcdefghi\nabcdefghi\nabcdefghi\nabcdefghi\n"
    "abcdefghi\nabcdefghi\nabcdefghi\nabcdefghi\n";
#define FULL_LENGTH (sizeof(k_full) - 1)

/*
 * The valid tree with every kind and every attribute. IDs in document
 * order; the ranges follow the tree, not the bytes of k_full.
 *
 *   1 document [0,80)
 *     2 heading [0,10) ATX 1
 *       3 text [0,2)
 *       4 emphasis [2,6)
 *         5 strong [2,6)
 *           6 text [2,6)
 *       7 hard break [6,7)
 *       8 soft break [7,8)
 *       9 backslash escape [8,9)
 *       10 character reference [9,10)
 *     11 heading [10,14) setext 2
 *     12 block quote [14,30)
 *       13 list [14,22) bullet '-' tight
 *         14 list item [14,22)
 *           15 paragraph [14,22)
 *             16 code span [14,16)
 *             17 raw HTML [16,18)
 *             18 autolink [18,20) URI
 *             19 autolink [20,22) email
 *       20 list [22,30) ordered ')' 7 loose
 *         21 list item [22,30)
 *           22 thematic break [22,24)
 *           23 HTML block [24,30)
 *     24 code block [30,40) fenced '`' 3, info [33,34)
 *     25 code block [40,44) indented
 *     26 link reference definition [44,56) label [45,46) dest [48,50)
 *        title [51,54)
 *     27 link reference definition [56,60) label [56,57) dest [58,58)
 *     28 paragraph [60,79)
 *       29 link [60,66) inline, dest [62,64) title [64,66)
 *         30 text [60,61)
 *       31 image [66,70) full -> 26
 *         32 text [66,67)
 *       33 link [70,74) collapsed -> 27
 *       34 link [74,76) shortcut -> 26
 *       35 image [76,78) inline, dest [77,77), no title
 *       36 dialect inline [78,79) with data
 *     37 dialect block [79,80) with data
 */
enum {
    F_ROOT = 1,
    F_ATX,
    F_TEXT1,
    F_EMPH,
    F_STRONG,
    F_TEXT2,
    F_HARD,
    F_SOFT,
    F_ESCAPE,
    F_CHARREF,
    F_SETEXT,
    F_QUOTE,
    F_BULLET,
    F_ITEM1,
    F_PARA1,
    F_CODE_SPAN,
    F_RAW,
    F_URI,
    F_EMAIL,
    F_ORDERED,
    F_ITEM2,
    F_BREAK,
    F_HTML,
    F_FENCED,
    F_INDENTED,
    F_DEF1,
    F_DEF2,
    F_PARA2,
    F_LINK_INLINE,
    F_TEXT3,
    F_IMAGE_FULL,
    F_TEXT4,
    F_LINK_COLLAPSED,
    F_LINK_SHORTCUT,
    F_IMAGE_INLINE,
    F_DINLINE,
    F_DBLOCK,
    F_COUNT = F_DBLOCK
};

static amark_range_t range_of(size_t start, size_t end)
{
    amark_range_t r;

    r.start = start;
    r.end = end;
    return r;
}

/* The attribute element of node id, through the internal layout. */
static amark_node_attr_t *attr_at(amark_node_id_t id)
{
    return (amark_node_attr_t *)amark_array_at(&g_doc->attrs,
                                               node_at(id)->attr);
}

static void build_full(void)
{
    static const unsigned char k_data[] = {0xff, 0x00, 0x07};
    amark_range_t a;
    amark_range_t b;
    amark_range_t c;

    NAOS_REQUIRE_INT_EQ(amark_document_create(&g_allocator,
                                              (const unsigned char *)k_full,
                                              FULL_LENGTH, 0, FULL_LENGTH,
                                              "full.md", &g_doc),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_UINT_EQ(add(F_ROOT, AMARK_KIND_HEADING, 0, 10), F_ATX);
    NAOS_REQUIRE_UINT_EQ(add(F_ATX, AMARK_KIND_TEXT, 0, 2), F_TEXT1);
    NAOS_REQUIRE_UINT_EQ(add(F_ATX, AMARK_KIND_EMPHASIS, 2, 6), F_EMPH);
    NAOS_REQUIRE_UINT_EQ(add(F_EMPH, AMARK_KIND_STRONG, 2, 6), F_STRONG);
    NAOS_REQUIRE_UINT_EQ(add(F_STRONG, AMARK_KIND_TEXT, 2, 6), F_TEXT2);
    NAOS_REQUIRE_UINT_EQ(add(F_ATX, AMARK_KIND_HARD_BREAK, 6, 7), F_HARD);
    NAOS_REQUIRE_UINT_EQ(add(F_ATX, AMARK_KIND_SOFT_BREAK, 7, 8), F_SOFT);
    NAOS_REQUIRE_UINT_EQ(add(F_ATX, AMARK_KIND_BACKSLASH_ESCAPE, 8, 9),
                         F_ESCAPE);
    NAOS_REQUIRE_UINT_EQ(add(F_ATX, AMARK_KIND_CHARACTER_REFERENCE, 9, 10),
                         F_CHARREF);
    NAOS_REQUIRE_UINT_EQ(add(F_ROOT, AMARK_KIND_HEADING, 10, 14), F_SETEXT);
    NAOS_REQUIRE_UINT_EQ(add(F_ROOT, AMARK_KIND_BLOCK_QUOTE, 14, 30),
                         F_QUOTE);
    NAOS_REQUIRE_UINT_EQ(add(F_QUOTE, AMARK_KIND_LIST, 14, 22), F_BULLET);
    NAOS_REQUIRE_UINT_EQ(add(F_BULLET, AMARK_KIND_LIST_ITEM, 14, 22),
                         F_ITEM1);
    NAOS_REQUIRE_UINT_EQ(add(F_ITEM1, AMARK_KIND_PARAGRAPH, 14, 22),
                         F_PARA1);
    NAOS_REQUIRE_UINT_EQ(add(F_PARA1, AMARK_KIND_CODE_SPAN, 14, 16),
                         F_CODE_SPAN);
    NAOS_REQUIRE_UINT_EQ(add(F_PARA1, AMARK_KIND_RAW_HTML, 16, 18), F_RAW);
    NAOS_REQUIRE_UINT_EQ(add(F_PARA1, AMARK_KIND_AUTOLINK, 18, 20), F_URI);
    NAOS_REQUIRE_UINT_EQ(add(F_PARA1, AMARK_KIND_AUTOLINK, 20, 22), F_EMAIL);
    NAOS_REQUIRE_UINT_EQ(add(F_QUOTE, AMARK_KIND_LIST, 22, 30), F_ORDERED);
    NAOS_REQUIRE_UINT_EQ(add(F_ORDERED, AMARK_KIND_LIST_ITEM, 22, 30),
                         F_ITEM2);
    NAOS_REQUIRE_UINT_EQ(add(F_ITEM2, AMARK_KIND_THEMATIC_BREAK, 22, 24),
                         F_BREAK);
    NAOS_REQUIRE_UINT_EQ(add(F_ITEM2, AMARK_KIND_HTML_BLOCK, 24, 30),
                         F_HTML);
    NAOS_REQUIRE_UINT_EQ(add(F_ROOT, AMARK_KIND_CODE_BLOCK, 30, 40),
                         F_FENCED);
    NAOS_REQUIRE_UINT_EQ(add(F_ROOT, AMARK_KIND_CODE_BLOCK, 40, 44),
                         F_INDENTED);
    NAOS_REQUIRE_UINT_EQ(add(F_ROOT, AMARK_KIND_LINK_REFERENCE_DEFINITION,
                             44, 56),
                         F_DEF1);
    NAOS_REQUIRE_UINT_EQ(add(F_ROOT, AMARK_KIND_LINK_REFERENCE_DEFINITION,
                             56, 60),
                         F_DEF2);
    NAOS_REQUIRE_UINT_EQ(add(F_ROOT, AMARK_KIND_PARAGRAPH, 60, 79), F_PARA2);
    NAOS_REQUIRE_UINT_EQ(add(F_PARA2, AMARK_KIND_LINK, 60, 66),
                         F_LINK_INLINE);
    NAOS_REQUIRE_UINT_EQ(add(F_LINK_INLINE, AMARK_KIND_TEXT, 60, 61),
                         F_TEXT3);
    NAOS_REQUIRE_UINT_EQ(add(F_PARA2, AMARK_KIND_IMAGE, 66, 70),
                         F_IMAGE_FULL);
    NAOS_REQUIRE_UINT_EQ(add(F_IMAGE_FULL, AMARK_KIND_TEXT, 66, 67),
                         F_TEXT4);
    NAOS_REQUIRE_UINT_EQ(add(F_PARA2, AMARK_KIND_LINK, 70, 74),
                         F_LINK_COLLAPSED);
    NAOS_REQUIRE_UINT_EQ(add(F_PARA2, AMARK_KIND_LINK, 74, 76),
                         F_LINK_SHORTCUT);
    NAOS_REQUIRE_UINT_EQ(add(F_PARA2, AMARK_KIND_IMAGE, 76, 78),
                         F_IMAGE_INLINE);
    NAOS_REQUIRE_UINT_EQ(add_class(F_PARA2, AMARK_KIND_DIALECT_MIN,
                                   AMARK_NODE_CLASS_INLINE, 78, 79),
                         F_DINLINE);
    NAOS_REQUIRE_UINT_EQ(add_class(F_ROOT, AMARK_KIND_DIALECT_MIN + 1,
                                   AMARK_NODE_CLASS_BLOCK, 79, 80),
                         F_DBLOCK);
    NAOS_REQUIRE_UINT_EQ(amark_document_node_count(g_doc), F_COUNT);

    NAOS_REQUIRE_INT_EQ(amark_tree_set_heading(g_doc, F_ATX, 1,
                                               AMARK_HEADING_ATX),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_heading(g_doc, F_SETEXT, 2,
                                               AMARK_HEADING_SETEXT),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_list(g_doc, F_BULLET,
                                            AMARK_LIST_BULLET, '-', 0,
                                            AMARK_LIST_TIGHT),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_list(g_doc, F_ORDERED,
                                            AMARK_LIST_ORDERED, ')', 7,
                                            AMARK_LIST_LOOSE),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_autolink(g_doc, F_URI,
                                                AMARK_AUTOLINK_URI),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_autolink(g_doc, F_EMAIL,
                                                AMARK_AUTOLINK_EMAIL),
                        AMARK_ERROR_NONE);
    a = range_of(33, 34);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_code_block(g_doc, F_FENCED,
                                                  AMARK_CODE_BLOCK_FENCED,
                                                  '`', 3, &a),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_code_block(g_doc, F_INDENTED,
                                                  AMARK_CODE_BLOCK_INDENTED,
                                                  0, 0, NULL),
                        AMARK_ERROR_NONE);
    a = range_of(45, 46);
    b = range_of(48, 50);
    c = range_of(51, 54);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_definition(g_doc, F_DEF1, &a, &b, &c),
                        AMARK_ERROR_NONE);
    a = range_of(56, 57);
    b = range_of(58, 58);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_definition(g_doc, F_DEF2, &a, &b,
                                                  NULL),
                        AMARK_ERROR_NONE);
    a = range_of(62, 64);
    b = range_of(64, 66);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_link(g_doc, F_LINK_INLINE,
                                            AMARK_LINK_INLINE, &a, &b,
                                            AMARK_NODE_NONE),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_link(g_doc, F_IMAGE_FULL,
                                            AMARK_LINK_FULL, NULL, NULL,
                                            F_DEF1),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_link(g_doc, F_LINK_COLLAPSED,
                                            AMARK_LINK_COLLAPSED, NULL, NULL,
                                            F_DEF2),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_link(g_doc, F_LINK_SHORTCUT,
                                            AMARK_LINK_SHORTCUT, NULL, NULL,
                                            F_DEF1),
                        AMARK_ERROR_NONE);
    a = range_of(77, 77);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_link(g_doc, F_IMAGE_INLINE,
                                            AMARK_LINK_INLINE, &a, NULL,
                                            AMARK_NODE_NONE),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_dialect_data(g_doc, F_DINLINE, k_data,
                                                    sizeof(k_data)),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_dialect_data(g_doc, F_DBLOCK, k_data,
                                                    sizeof(k_data)),
                        AMARK_ERROR_NONE);
}

NAOS_TEST(full_tree_has_no_violation)
{
    amark_node_id_t id;
    amark_kind_t kind;
    unsigned kinds = 0;

    build_full();
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
    for (id = F_ROOT; id <= F_COUNT; id++) {
        CHECK_SUBTREE(id, AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
    }
    /* The tree holds all 22 CommonMark kinds. */
    for (kind = AMARK_KIND_DOCUMENT; kind <= AMARK_KIND_CHARACTER_REFERENCE;
         kind++) {
        int found = 0;

        if (!amark_kind_is_core(kind)) {
            continue;
        }
        for (id = F_ROOT; id <= F_COUNT; id++) {
            if (node_at(id)->kind == kind) {
                found = 1;
            }
        }
        NAOS_CHECK_INT_EQ(found, 1);
        kinds++;
    }
    NAOS_CHECK_UINT_EQ(kinds, 22);
}

/*
 * Each mutation breaks one condition of node id's attribute element; the
 * element is restored after each check.
 */
static amark_node_attr_t g_saved_attr;

static void save_attr(amark_node_id_t id)
{
    g_saved_attr = *attr_at(id);
}

static void expect_attribute(amark_node_id_t id)
{
    CHECK_TREE(AMARK_TREE_VIOLATION_ATTRIBUTE, id);
    CHECK_SUBTREE(id, AMARK_TREE_VIOLATION_ATTRIBUTE, id);
    *attr_at(id) = g_saved_attr;
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
}

static void expect_valid(amark_node_id_t id)
{
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
    *attr_at(id) = g_saved_attr;
}

/* The attribute index of node id is 0 or out of the attribute array. */
static void check_attr_indices(amark_node_id_t id)
{
    uint32_t saved = node_at(id)->attr;
    size_t len = amark_array_len(&g_doc->attrs);

    node_at(id)->attr = 0;
    CHECK_TREE(AMARK_TREE_VIOLATION_ATTRIBUTE, id);
    node_at(id)->attr = (uint32_t)len;
    CHECK_TREE(AMARK_TREE_VIOLATION_ATTRIBUTE, id);
    CHECK_SUBTREE(id, AMARK_TREE_VIOLATION_ATTRIBUTE, id);
    node_at(id)->attr = UINT32_MAX;
    CHECK_TREE(AMARK_TREE_VIOLATION_ATTRIBUTE, id);
    node_at(id)->attr = saved;
}

NAOS_TEST(attribute_heading_conditions)
{
    build_full();
    check_attr_indices(F_ATX);
    check_attr_indices(F_SETEXT);

    save_attr(F_ATX);
    attr_at(F_ATX)->heading.level = 0;
    expect_attribute(F_ATX);
    attr_at(F_ATX)->heading.level = 7;
    expect_attribute(F_ATX);
    attr_at(F_ATX)->heading.level = 6;
    expect_valid(F_ATX);
    attr_at(F_ATX)->heading.style = AMARK_HEADING_STYLE_NONE;
    expect_attribute(F_ATX);
    attr_at(F_ATX)->heading.style = (amark_heading_style_t)3;
    expect_attribute(F_ATX);

    save_attr(F_SETEXT);
    attr_at(F_SETEXT)->heading.level = 3;
    expect_attribute(F_SETEXT);
    attr_at(F_SETEXT)->heading.level = 0;
    expect_attribute(F_SETEXT);
    attr_at(F_SETEXT)->heading.level = 1;
    expect_valid(F_SETEXT);
}

NAOS_TEST(attribute_list_conditions)
{
    build_full();
    check_attr_indices(F_BULLET);
    check_attr_indices(F_ORDERED);

    save_attr(F_BULLET);
    attr_at(F_BULLET)->list.type = AMARK_LIST_TYPE_NONE;
    expect_attribute(F_BULLET);
    attr_at(F_BULLET)->list.type = (amark_list_type_t)3;
    expect_attribute(F_BULLET);
    attr_at(F_BULLET)->list.marker = '.';
    expect_attribute(F_BULLET);
    attr_at(F_BULLET)->list.marker = 'x';
    expect_attribute(F_BULLET);
    attr_at(F_BULLET)->list.marker = '\0';
    expect_attribute(F_BULLET);
    attr_at(F_BULLET)->list.marker = '+';
    expect_valid(F_BULLET);
    attr_at(F_BULLET)->list.marker = '*';
    expect_valid(F_BULLET);
    attr_at(F_BULLET)->list.spacing = AMARK_LIST_SPACING_NONE;
    expect_attribute(F_BULLET);
    attr_at(F_BULLET)->list.spacing = (amark_list_spacing_t)3;
    expect_attribute(F_BULLET);
    attr_at(F_BULLET)->list.spacing = AMARK_LIST_LOOSE;
    expect_valid(F_BULLET);

    save_attr(F_ORDERED);
    attr_at(F_ORDERED)->list.marker = '-';
    expect_attribute(F_ORDERED);
    attr_at(F_ORDERED)->list.marker = '(';
    expect_attribute(F_ORDERED);
    attr_at(F_ORDERED)->list.marker = '.';
    expect_valid(F_ORDERED);
    attr_at(F_ORDERED)->list.spacing = AMARK_LIST_SPACING_NONE;
    expect_attribute(F_ORDERED);
}

NAOS_TEST(attribute_code_block_conditions)
{
    amark_opt_range_t *info;

    build_full();
    check_attr_indices(F_FENCED);
    check_attr_indices(F_INDENTED);

    save_attr(F_FENCED);
    info = &attr_at(F_FENCED)->code_block.info;
    attr_at(F_FENCED)->code_block.style = AMARK_CODE_BLOCK_STYLE_NONE;
    expect_attribute(F_FENCED);
    attr_at(F_FENCED)->code_block.style = (amark_code_block_style_t)3;
    expect_attribute(F_FENCED);
    attr_at(F_FENCED)->code_block.fence_char = '-';
    expect_attribute(F_FENCED);
    attr_at(F_FENCED)->code_block.fence_char = '\0';
    expect_attribute(F_FENCED);
    attr_at(F_FENCED)->code_block.fence_char = '~';
    expect_valid(F_FENCED);
    attr_at(F_FENCED)->code_block.fence_length = 2;
    expect_attribute(F_FENCED);
    attr_at(F_FENCED)->code_block.fence_length = 0;
    expect_attribute(F_FENCED);
    attr_at(F_FENCED)->code_block.fence_length = 1000;
    expect_valid(F_FENCED);
    /* The info outside the node, reversed, and at the node's edges. */
    info->start = 29;
    expect_attribute(F_FENCED);
    info->end = 41;
    expect_attribute(F_FENCED);
    info->end = FULL_LENGTH + 1;
    expect_attribute(F_FENCED);
    info->start = 34;
    info->end = 33;
    expect_attribute(F_FENCED);
    info->start = 30;
    info->end = 40;
    expect_valid(F_FENCED);
    info->start = 40;
    info->end = 40;
    expect_valid(F_FENCED);
    info->present = false;
    expect_valid(F_FENCED);

    /* An indented code block: the fence is not checked; a range it holds
     * must still be a range in the content. */
    save_attr(F_INDENTED);
    info = &attr_at(F_INDENTED)->code_block.info;
    attr_at(F_INDENTED)->code_block.style = AMARK_CODE_BLOCK_STYLE_NONE;
    expect_attribute(F_INDENTED);
    info->present = true;
    info->start = 42;
    info->end = 41;
    expect_attribute(F_INDENTED);
    info->present = true;
    info->start = 42;
    info->end = FULL_LENGTH + 1;
    expect_attribute(F_INDENTED);
}

NAOS_TEST(attribute_link_conditions)
{
    static const amark_node_id_t k_links[] = {
        F_LINK_INLINE, F_IMAGE_FULL, F_LINK_COLLAPSED, F_LINK_SHORTCUT,
        F_IMAGE_INLINE
    };
    amark_node_attr_t *a;
    size_t i;

    build_full();
    for (i = 0; i < sizeof(k_links) / sizeof(k_links[0]); i++) {
        check_attr_indices(k_links[i]);
        save_attr(k_links[i]);
        attr_at(k_links[i])->link.form = AMARK_LINK_FORM_NONE;
        expect_attribute(k_links[i]);
        save_attr(k_links[i]);
        attr_at(k_links[i])->link.form = (amark_link_form_t)5;
        expect_attribute(k_links[i]);
    }

    /* The inline form: dest and title inside the node, no target. */
    save_attr(F_LINK_INLINE);
    a = attr_at(F_LINK_INLINE);
    a->link.destination.start = 59;
    expect_attribute(F_LINK_INLINE);
    a->link.destination.end = 67;
    expect_attribute(F_LINK_INLINE);
    a->link.destination.start = 64;
    a->link.destination.end = 63;
    expect_attribute(F_LINK_INLINE);
    a->link.title.start = 59;
    expect_attribute(F_LINK_INLINE);
    a->link.title.end = 67;
    expect_attribute(F_LINK_INLINE);
    a->link.title.start = 66;
    a->link.title.end = 65;
    expect_attribute(F_LINK_INLINE);
    a->link.definition = F_DEF1;
    expect_attribute(F_LINK_INLINE);
    a->link.destination.start = 60;
    a->link.title.end = 66;
    expect_valid(F_LINK_INLINE);
    a->link.title.present = false;
    expect_valid(F_LINK_INLINE);

    /* The reference forms: a link reference definition as the target, no
     * dest or title. */
    save_attr(F_IMAGE_FULL);
    a = attr_at(F_IMAGE_FULL);
    a->link.definition = AMARK_NODE_NONE;
    expect_attribute(F_IMAGE_FULL);
    a->link.definition = F_PARA2;
    expect_attribute(F_IMAGE_FULL);
    a->link.definition = F_ROOT;
    expect_attribute(F_IMAGE_FULL);
    a->link.definition = F_COUNT + 1;
    expect_attribute(F_IMAGE_FULL);
    a->link.definition = UINT32_MAX;
    expect_attribute(F_IMAGE_FULL);
    a->link.destination.present = true;
    a->link.destination.start = 67;
    a->link.destination.end = 68;
    expect_attribute(F_IMAGE_FULL);
    a->link.title.present = true;
    a->link.title.start = 67;
    a->link.title.end = 68;
    expect_attribute(F_IMAGE_FULL);
    a->link.definition = F_DEF2;
    expect_valid(F_IMAGE_FULL);

    save_attr(F_LINK_SHORTCUT);
    attr_at(F_LINK_SHORTCUT)->link.definition = F_LINK_SHORTCUT;
    expect_attribute(F_LINK_SHORTCUT);

    /* A target that was removed is not a link reference definition: the
     * link that names it violates. */
    amark_tree_remove(g_doc, F_DEF2);
    CHECK_TREE(AMARK_TREE_VIOLATION_ATTRIBUTE, F_LINK_COLLAPSED);
    CHECK_SUBTREE(F_LINK_COLLAPSED, AMARK_TREE_VIOLATION_ATTRIBUTE,
                  F_LINK_COLLAPSED);
    attr_at(F_LINK_COLLAPSED)->link.definition = F_DEF1;
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
}

NAOS_TEST(attribute_definition_conditions)
{
    amark_node_attr_t *a;

    build_full();
    check_attr_indices(F_DEF1);
    check_attr_indices(F_DEF2);

    save_attr(F_DEF1);
    a = attr_at(F_DEF1);
    a->definition.label.present = false;
    expect_attribute(F_DEF1);
    a->definition.destination.present = false;
    expect_attribute(F_DEF1);
    a->definition.label.start = 43;
    expect_attribute(F_DEF1);
    a->definition.label.end = 57;
    expect_attribute(F_DEF1);
    a->definition.destination.start = 43;
    expect_attribute(F_DEF1);
    a->definition.destination.end = 57;
    expect_attribute(F_DEF1);
    a->definition.destination.start = 50;
    a->definition.destination.end = 48;
    expect_attribute(F_DEF1);
    a->definition.title.start = 43;
    expect_attribute(F_DEF1);
    a->definition.title.end = 57;
    expect_attribute(F_DEF1);
    a->definition.title.start = 54;
    a->definition.title.end = 51;
    expect_attribute(F_DEF1);
    a->definition.title.present = false;
    expect_valid(F_DEF1);
    a->definition.label.start = 44;
    a->definition.title.end = 56;
    expect_valid(F_DEF1);
}

NAOS_TEST(attribute_autolink_conditions)
{
    build_full();
    check_attr_indices(F_URI);
    check_attr_indices(F_EMAIL);

    save_attr(F_URI);
    attr_at(F_URI)->autolink.type = AMARK_AUTOLINK_TYPE_NONE;
    expect_attribute(F_URI);
    attr_at(F_URI)->autolink.type = (amark_autolink_type_t)3;
    expect_attribute(F_URI);
}

NAOS_TEST(attribute_of_dialect_kinds_not_checked)
{
    build_full();
    /* Whatever the bytes of a dialect attribute are. */
    attr_at(F_DINLINE)->dialect.size = SIZE_MAX;
    attr_at(F_DBLOCK)->dialect.data = NULL;
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
    /* A dialect node without an attribute. */
    NAOS_REQUIRE_INT_EQ(amark_tree_set_dialect_data(g_doc, F_DINLINE, NULL,
                                                    0),
                        AMARK_ERROR_NONE);
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
    /* Kinds without attributes are not checked either. */
    node_at(F_PARA2)->attr = UINT32_MAX;
    node_at(F_TEXT1)->attr = UINT32_MAX;
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
    node_at(F_PARA2)->attr = 0;
    node_at(F_TEXT1)->attr = 0;
}

NAOS_TEST(attribute_order_and_first_violation)
{
    build_full();

    /* NESTING before ATTRIBUTE on one node. */
    attr_at(F_ATX)->heading.level = 0;
    node_at(F_ATX)->is_inline = 1;
    CHECK_TREE(AMARK_TREE_VIOLATION_NESTING, F_ATX);
    node_at(F_ATX)->is_inline = 0;
    CHECK_TREE(AMARK_TREE_VIOLATION_ATTRIBUTE, F_ATX);

    /* Two attribute violations: the first in document order. */
    attr_at(F_URI)->autolink.type = AMARK_AUTOLINK_TYPE_NONE;
    CHECK_TREE(AMARK_TREE_VIOLATION_ATTRIBUTE, F_ATX);
    attr_at(F_ATX)->heading.level = 1;
    CHECK_TREE(AMARK_TREE_VIOLATION_ATTRIBUTE, F_URI);

    /* An attribute violation after a range violation in document order. */
    node_at(F_LINK_SHORTCUT)->end_line += 1;
    CHECK_TREE(AMARK_TREE_VIOLATION_ATTRIBUTE, F_URI);
    attr_at(F_URI)->autolink.type = AMARK_AUTOLINK_URI;
    CHECK_TREE(AMARK_TREE_VIOLATION_LINES, F_LINK_SHORTCUT);
    node_at(F_LINK_SHORTCUT)->end_line -= 1;

    /* The subtree check finds attribute violations inside only. */
    attr_at(F_IMAGE_FULL)->link.definition = F_PARA2;
    CHECK_SUBTREE(F_PARA2, AMARK_TREE_VIOLATION_ATTRIBUTE, F_IMAGE_FULL);
    CHECK_SUBTREE(F_QUOTE, AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
    CHECK_SUBTREE(F_LINK_INLINE, AMARK_TREE_VIOLATION_NONE,
                  AMARK_NODE_NONE);
    attr_at(F_IMAGE_FULL)->link.definition = F_DEF1;
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
}

/* ---- large trees and cycles (6.10, 6.11) ---- */

/*
 * The number of visits is not observed through a counter: the walk returns
 * LINK as soon as it has visited more nodes than were created, so a NONE
 * result over a tree means the walk visited at most that many nodes. The
 * cycle cases show the bound is enforced: a walk that would go on forever
 * stops at visit count + 1, on the node the parity of that visit names.
 */
#define LARGE_COUNT 1000000u

NAOS_TEST(large_wide_tree)
{
    amark_node_id_t i;

    create_doc();
    for (i = 2; i <= LARGE_COUNT; i++) {
        add(N_ROOT, AMARK_KIND_THEMATIC_BREAK, 12, 12);
    }
    NAOS_REQUIRE_UINT_EQ(amark_document_node_count(g_doc), LARGE_COUNT);
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
    CHECK_SUBTREE(N_ROOT, AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
    CHECK_SUBTREE(LARGE_COUNT, AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);

    /* The last child names the first as its next sibling: found on the
     * root, whose last child has a next sibling. */
    node_at(LARGE_COUNT)->next_sibling = 2;
    CHECK_TREE(AMARK_TREE_VIOLATION_LINK, N_ROOT);
    node_at(LARGE_COUNT)->next_sibling = AMARK_NODE_NONE;
    /* A violation on the last node is found after the whole walk. */
    node_at(LARGE_COUNT)->end_line += 1;
    CHECK_TREE(AMARK_TREE_VIOLATION_LINES, LARGE_COUNT);
    node_at(LARGE_COUNT)->end_line -= 1;
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
}

NAOS_TEST(large_deep_chain)
{
    amark_node_id_t i;
    amark_node_id_t parent = N_ROOT;

    create_doc();
    for (i = 2; i <= LARGE_COUNT; i++) {
        parent = add(parent, AMARK_KIND_BLOCK_QUOTE, 0, 12);
    }
    NAOS_REQUIRE_UINT_EQ(parent, LARGE_COUNT);
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
    CHECK_SUBTREE(2, AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);

    /* A violation at the bottom of the chain. */
    node_at(LARGE_COUNT)->end = 13;
    CHECK_TREE(AMARK_TREE_VIOLATION_BOUNDS, LARGE_COUNT);
    node_at(LARGE_COUNT)->end = 12;

    /*
     * Nodes 2 and 3 name each other as parent, first and last child: every
     * local link condition of both holds, so the subtree check of node 2
     * goes round 2, 3, 2, ... until the visit count passes the number of
     * nodes created; visit LARGE_COUNT + 1 (odd) is on node 2.
     */
    node_at(2)->parent = 3;
    node_at(3)->first_child = 2;
    node_at(3)->last_child = 2;
    CHECK_SUBTREE(2, AMARK_TREE_VIOLATION_LINK, 2);
    CHECK_SUBTREE(3, AMARK_TREE_VIOLATION_LINK, 3);
    CHECK_TREE(AMARK_TREE_VIOLATION_LINK, N_ROOT);
    node_at(2)->parent = N_ROOT;
    node_at(3)->first_child = 4;
    node_at(3)->last_child = 4;
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
}

NAOS_TEST(subtree_cycle_of_two_parents)
{
    amark_node_id_t a;
    amark_node_id_t b;

    build_valid();
    a = add(N_ROOT, AMARK_KIND_PARAGRAPH, 12, 12);
    b = add(a, AMARK_KIND_PARAGRAPH, 12, 12);
    NAOS_REQUIRE_UINT_EQ(b, N_COUNT + 2);
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);

    /* Unlink a from the root, then make a and b each other's parent. */
    node_at(N_BREAK)->next_sibling = AMARK_NODE_NONE;
    node_at(N_ROOT)->last_child = N_BREAK;
    node_at(a)->prev_sibling = AMARK_NODE_NONE;
    node_at(a)->parent = b;
    node_at(b)->first_child = a;
    node_at(b)->last_child = a;
    /* Neither is reachable from the root. */
    CHECK_TREE(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
    /* The walk alternates a, b, a, ...; visit N_COUNT + 3 (odd) stops it on
     * the node it started from. */
    CHECK_SUBTREE(a, AMARK_TREE_VIOLATION_LINK, a);
    CHECK_SUBTREE(b, AMARK_TREE_VIOLATION_LINK, b);
    check_without_effects();
}

NAOS_TEST(broken_attribute_indices_end_safely)
{
    build_full();
    /* Every attributed node with an index past the attribute array, and
     * every reference with a target past the node array. */
    node_at(F_ATX)->attr = UINT32_MAX;
    node_at(F_SETEXT)->attr = UINT32_MAX - 1;
    node_at(F_BULLET)->attr = (uint32_t)amark_array_len(&g_doc->attrs);
    node_at(F_FENCED)->attr = 0x7fffffffu;
    attr_at(F_IMAGE_FULL)->link.definition = UINT32_MAX;
    attr_at(F_LINK_COLLAPSED)->link.definition = F_COUNT + 1;
    node_at(F_DEF1)->attr = UINT32_MAX;
    node_at(F_URI)->attr = UINT32_MAX;
    CHECK_TREE(AMARK_TREE_VIOLATION_ATTRIBUTE, F_ATX);
    CHECK_SUBTREE(F_PARA2, AMARK_TREE_VIOLATION_ATTRIBUTE, F_IMAGE_FULL);
    CHECK_SUBTREE(F_QUOTE, AMARK_TREE_VIOLATION_ATTRIBUTE, F_BULLET);
    CHECK_SUBTREE(F_URI, AMARK_TREE_VIOLATION_ATTRIBUTE, F_URI);
}

NAOS_SUITE(tree_check,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(valid_tree_has_no_violation),
    NAOS_ADD_TEST(root_only_and_empty_content),
    NAOS_ADD_TEST(root_conditions),
    NAOS_ADD_TEST(link_parent_mismatch),
    NAOS_ADD_TEST(link_sibling_and_child_conditions),
    NAOS_ADD_TEST(link_index_out_of_range),
    NAOS_ADD_TEST(link_cycles_end_the_check),
    NAOS_ADD_TEST(bounds_conditions),
    NAOS_ADD_TEST(lines_conditions),
    NAOS_ADD_TEST(parent_range_conditions),
    NAOS_ADD_TEST(sibling_order_conditions),
    NAOS_ADD_TEST(nesting_conditions),
    NAOS_ADD_TEST(first_violation_in_document_order),
    NAOS_ADD_TEST(conditions_of_one_node_in_table_order),
    NAOS_ADD_TEST(subtree_ids_that_are_not_nodes),
    NAOS_ADD_TEST(subtree_ignores_violations_outside),
    NAOS_ADD_TEST(subtree_root_and_the_nodes_around_it),
    NAOS_ADD_TEST(checks_change_nothing_and_allocate_nothing),
    NAOS_ADD_TEST(after_insert_before),
    NAOS_ADD_TEST(after_set_range_and_set_kind),
    NAOS_ADD_TEST(after_remove),
    NAOS_ADD_TEST(after_wrap),
    NAOS_ADD_TEST(after_removing_and_wrapping),
    NAOS_ADD_TEST(full_tree_has_no_violation),
    NAOS_ADD_TEST(attribute_heading_conditions),
    NAOS_ADD_TEST(attribute_list_conditions),
    NAOS_ADD_TEST(attribute_code_block_conditions),
    NAOS_ADD_TEST(attribute_link_conditions),
    NAOS_ADD_TEST(attribute_definition_conditions),
    NAOS_ADD_TEST(attribute_autolink_conditions),
    NAOS_ADD_TEST(attribute_of_dialect_kinds_not_checked),
    NAOS_ADD_TEST(attribute_order_and_first_violation),
    NAOS_ADD_TEST(large_wide_tree),
    NAOS_ADD_TEST(large_deep_chain),
    NAOS_ADD_TEST(subtree_cycle_of_two_parents),
    NAOS_ADD_TEST(broken_attribute_indices_end_safely)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(tree_check)
)
