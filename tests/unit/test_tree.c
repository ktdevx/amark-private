/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of the tree builder (src/tree.h): adding nodes as the last
 * child of a parent and before a sibling, the lines derived from the range,
 * changing the range and the kind, removing and wrapping nodes, the node
 * limit and allocation failures. The results are checked through
 * the internal layout of the document (src/document.h).
 */

#include "document.h"
#include "test_alloc.h"
#include "tree.h"

#include <naos/naos.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * Fixture. NAOS_REQUIRE_* leaves a test with longjmp, so the documents a
 * test creates are kept in g_doc and g_doc2, and teardown (which always
 * runs) restores a node array length a test changed, destroys them and then
 * checks that no block is left and that every call received the context
 * value it was given.
 */
static amark_test_alloc_t g_state;
static amark_allocator_t g_allocator;
static amark_document_t *g_doc;
static amark_document_t *g_doc2;
static size_t g_saved_len; /* 0: the length of g_doc's node array is real */

NAOS_SETUP(counting)
{
    amark_test_alloc_init(&g_state);
    g_allocator = amark_test_allocator(&g_state);
    g_doc = NULL;
    g_doc2 = NULL;
    g_saved_len = 0;
}

NAOS_TEARDOWN(counting)
{
    g_state.fail_at = 0;
    if (g_doc != NULL && g_saved_len != 0) {
        g_doc->nodes.len = g_saved_len;
    }
    g_saved_len = 0;
    amark_document_destroy(g_doc);
    amark_document_destroy(g_doc2);
    g_doc = NULL;
    g_doc2 = NULL;
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
    NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
}

/*
 * "ab\n" is line 1 (offsets 0-2), "cd\r\n" line 2 (3-6), "ef\r" line 3
 * (7-9) and "gh" line 4 (10-11); offset 12 (the length) is on line 4.
 */
static const char k_mixed[] = "ab\ncd\r\nef\rgh";
#define MIXED_LENGTH (sizeof(k_mixed) - 1)

static amark_node_t *node_at(const amark_document_t *doc, amark_node_id_t id)
{
    return (amark_node_t *)amark_array_at(&doc->nodes, id);
}

static amark_error_t create_mixed(amark_document_t **out)
{
    return amark_document_create(&g_allocator,
                                 (const unsigned char *)k_mixed, MIXED_LENGTH,
                                 0, MIXED_LENGTH, "t.md", out);
}

/* Checks the kind, class, range and lines of a node. */
static void check_node(amark_node_id_t id, amark_kind_t kind,
                       uint32_t is_inline, size_t start, size_t end,
                       size_t start_line, size_t end_line)
{
    const amark_node_t *n;

    NAOS_REQUIRE_UINT_LT(id, amark_array_len(&g_doc->nodes));
    n = node_at(g_doc, id);
    NAOS_CHECK_UINT_EQ(n->kind, kind);
    NAOS_CHECK_UINT_EQ(n->is_inline, is_inline);
    NAOS_CHECK_UINT_EQ(n->attr, 0);
    NAOS_CHECK_UINT_EQ(n->start, start);
    NAOS_CHECK_UINT_EQ(n->end, end);
    NAOS_CHECK_UINT_EQ(n->start_line, start_line);
    NAOS_CHECK_UINT_EQ(n->end_line, end_line);
}

/* Checks the five links of a node. */
static void check_links(amark_node_id_t id, amark_node_id_t parent,
                        amark_node_id_t first_child,
                        amark_node_id_t last_child,
                        amark_node_id_t prev_sibling,
                        amark_node_id_t next_sibling)
{
    const amark_node_t *n;

    NAOS_REQUIRE_UINT_LT(id, amark_array_len(&g_doc->nodes));
    n = node_at(g_doc, id);
    NAOS_CHECK_UINT_EQ(n->parent, parent);
    NAOS_CHECK_UINT_EQ(n->first_child, first_child);
    NAOS_CHECK_UINT_EQ(n->last_child, last_child);
    NAOS_CHECK_UINT_EQ(n->prev_sibling, prev_sibling);
    NAOS_CHECK_UINT_EQ(n->next_sibling, next_sibling);
}

/* The 22 CommonMark kinds: 10 blocks, then 12 inlines. */
static const amark_kind_t k_core_kinds[] = {
    AMARK_KIND_DOCUMENT,
    AMARK_KIND_BLOCK_QUOTE,
    AMARK_KIND_LIST,
    AMARK_KIND_LIST_ITEM,
    AMARK_KIND_THEMATIC_BREAK,
    AMARK_KIND_HEADING,
    AMARK_KIND_CODE_BLOCK,
    AMARK_KIND_HTML_BLOCK,
    AMARK_KIND_PARAGRAPH,
    AMARK_KIND_LINK_REFERENCE_DEFINITION,
    AMARK_KIND_TEXT,
    AMARK_KIND_CODE_SPAN,
    AMARK_KIND_EMPHASIS,
    AMARK_KIND_STRONG,
    AMARK_KIND_LINK,
    AMARK_KIND_IMAGE,
    AMARK_KIND_AUTOLINK,
    AMARK_KIND_RAW_HTML,
    AMARK_KIND_HARD_BREAK,
    AMARK_KIND_SOFT_BREAK,
    AMARK_KIND_BACKSLASH_ESCAPE,
    AMARK_KIND_CHARACTER_REFERENCE
};
#define CORE_KIND_COUNT (sizeof(k_core_kinds) / sizeof(k_core_kinds[0]))
#define CORE_BLOCK_COUNT 10

/* Line of each offset 0..12 of k_mixed. */
static const size_t k_line_of[MIXED_LENGTH + 1] = {
    1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4
};

/* ---- the kinds (1.1, 2.1, 7.2, 7.6) ---- */

NAOS_TEST(every_core_kind_as_children_of_the_root)
{
    size_t i;
    amark_node_id_t out = 999;

    NAOS_REQUIRE_INT_EQ(create_mixed(&g_doc), AMARK_ERROR_NONE);
    NAOS_REQUIRE_UINT_EQ(CORE_KIND_COUNT, 22);

    for (i = 0; i < CORE_KIND_COUNT; i++) {
        /* Ranges [i % 12, 12) cover each start offset and the end. */
        size_t start = i % MIXED_LENGTH;

        out = 999;
        NAOS_REQUIRE_INT_EQ(amark_tree_append(g_doc, 1, k_core_kinds[i],
                                              AMARK_NODE_CLASS_AUTO, start,
                                              MIXED_LENGTH, &out),
                            AMARK_ERROR_NONE);
        /* IDs are sequential in the order the nodes are added. */
        NAOS_CHECK_UINT_EQ(out, (amark_node_id_t)(i + 2));
    }
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), CORE_KIND_COUNT + 1);

    check_links(1, AMARK_NODE_NONE, 2, (amark_node_id_t)(CORE_KIND_COUNT + 1),
                AMARK_NODE_NONE, AMARK_NODE_NONE);
    for (i = 0; i < CORE_KIND_COUNT; i++) {
        amark_node_id_t id = (amark_node_id_t)(i + 2);
        size_t start = i % MIXED_LENGTH;

        check_node(id, k_core_kinds[i], i < CORE_BLOCK_COUNT ? 0u : 1u, start,
                   MIXED_LENGTH, k_line_of[start], 4);
        check_links(id, 1, AMARK_NODE_NONE, AMARK_NODE_NONE,
                    i == 0 ? AMARK_NODE_NONE : (amark_node_id_t)(id - 1),
                    i + 1 == CORE_KIND_COUNT ? AMARK_NODE_NONE
                                             : (amark_node_id_t)(id + 1));
    }
}

NAOS_TEST(every_core_kind_by_insert_before)
{
    size_t i;
    amark_node_id_t last = AMARK_NODE_NONE;
    amark_node_id_t out = AMARK_NODE_NONE;

    NAOS_REQUIRE_INT_EQ(create_mixed(&g_doc), AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_append(g_doc, 1, AMARK_KIND_PARAGRAPH,
                                          AMARK_NODE_CLASS_AUTO, 0,
                                          MIXED_LENGTH, &last),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_UINT_EQ(last, 2);

    /* Each new node goes directly before the paragraph (ID 2). */
    for (i = 0; i < CORE_KIND_COUNT; i++) {
        out = 999;
        NAOS_REQUIRE_INT_EQ(amark_tree_insert_before(g_doc, 2, k_core_kinds[i],
                                                     AMARK_NODE_CLASS_AUTO,
                                                     i % MIXED_LENGTH,
                                                     i % MIXED_LENGTH, &out),
                            AMARK_ERROR_NONE);
        NAOS_CHECK_UINT_EQ(out, (amark_node_id_t)(i + 3));
    }

    /* Order: 3, 4, ..., 24, 2. */
    check_links(1, AMARK_NODE_NONE, 3, 2, AMARK_NODE_NONE, AMARK_NODE_NONE);
    check_links(2, 1, AMARK_NODE_NONE, AMARK_NODE_NONE,
                (amark_node_id_t)(CORE_KIND_COUNT + 2), AMARK_NODE_NONE);
    for (i = 0; i < CORE_KIND_COUNT; i++) {
        amark_node_id_t id = (amark_node_id_t)(i + 3);
        size_t at = i % MIXED_LENGTH;

        check_node(id, k_core_kinds[i], i < CORE_BLOCK_COUNT ? 0u : 1u, at, at,
                   k_line_of[at], k_line_of[at]);
        check_links(id, 1, AMARK_NODE_NONE, AMARK_NODE_NONE,
                    i == 0 ? AMARK_NODE_NONE : (amark_node_id_t)(id - 1),
                    i + 1 == CORE_KIND_COUNT ? 2 : (amark_node_id_t)(id + 1));
    }
}

NAOS_TEST(dialect_kinds_use_the_given_class)
{
    amark_node_id_t block = AMARK_NODE_NONE;
    amark_node_id_t inl = AMARK_NODE_NONE;
    amark_node_id_t top_block = AMARK_NODE_NONE;
    amark_node_id_t top_inline = AMARK_NODE_NONE;

    NAOS_REQUIRE_INT_EQ(create_mixed(&g_doc), AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_append(g_doc, 1, AMARK_KIND_DIALECT_MIN,
                                          AMARK_NODE_CLASS_BLOCK, 0, 3,
                                          &block),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_append(g_doc, block,
                                          AMARK_KIND_DIALECT_MIN,
                                          AMARK_NODE_CLASS_INLINE, 0, 2, &inl),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_insert_before(g_doc, block, UINT32_MAX,
                                                 AMARK_NODE_CLASS_INLINE, 0,
                                                 0, &top_inline),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_append(g_doc, 1,
                                          AMARK_KIND_DIALECT_MIN + 1,
                                          AMARK_NODE_CLASS_BLOCK, 3, 7,
                                          &top_block),
                        AMARK_ERROR_NONE);

    NAOS_CHECK_UINT_EQ(block, 2);
    NAOS_CHECK_UINT_EQ(inl, 3);
    NAOS_CHECK_UINT_EQ(top_inline, 4);
    NAOS_CHECK_UINT_EQ(top_block, 5);

    check_node(block, AMARK_KIND_DIALECT_MIN, 0, 0, 3, 1, 1);
    check_node(inl, AMARK_KIND_DIALECT_MIN, 1, 0, 2, 1, 1);
    check_node(top_inline, UINT32_MAX, 1, 0, 0, 1, 1);
    check_node(top_block, AMARK_KIND_DIALECT_MIN + 1, 0, 3, 7, 2, 2);

    check_links(1, AMARK_NODE_NONE, top_inline, top_block, AMARK_NODE_NONE,
                AMARK_NODE_NONE);
    check_links(top_inline, 1, AMARK_NODE_NONE, AMARK_NODE_NONE,
                AMARK_NODE_NONE, block);
    check_links(block, 1, inl, inl, top_inline, top_block);
    check_links(inl, block, AMARK_NODE_NONE, AMARK_NODE_NONE, AMARK_NODE_NONE,
                AMARK_NODE_NONE);
    check_links(top_block, 1, AMARK_NODE_NONE, AMARK_NODE_NONE, block,
                AMARK_NODE_NONE);
}

/* ---- links (7.2) ---- */

NAOS_TEST(nested_append_and_insert_before)
{
    amark_node_id_t quote = AMARK_NODE_NONE;
    amark_node_id_t para = AMARK_NODE_NONE;
    amark_node_id_t text1 = AMARK_NODE_NONE;
    amark_node_id_t text2 = AMARK_NODE_NONE;
    amark_node_id_t first = AMARK_NODE_NONE;
    amark_node_id_t middle = AMARK_NODE_NONE;

    NAOS_REQUIRE_INT_EQ(create_mixed(&g_doc), AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_append(g_doc, 1, AMARK_KIND_BLOCK_QUOTE,
                                          AMARK_NODE_CLASS_AUTO, 0, 12,
                                          &quote),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_append(g_doc, quote, AMARK_KIND_PARAGRAPH,
                                          AMARK_NODE_CLASS_AUTO, 0, 12, &para),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_append(g_doc, para, AMARK_KIND_TEXT,
                                          AMARK_NODE_CLASS_AUTO, 0, 2, &text1),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_append(g_doc, para, AMARK_KIND_TEXT,
                                          AMARK_NODE_CLASS_AUTO, 3, 5, &text2),
                        AMARK_ERROR_NONE);
    /* Before the first child, then between the first and the second. */
    NAOS_REQUIRE_INT_EQ(amark_tree_insert_before(g_doc, text1,
                                                 AMARK_KIND_CODE_SPAN,
                                                 AMARK_NODE_CLASS_AUTO, 0, 0,
                                                 &first),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_insert_before(g_doc, text2,
                                                 AMARK_KIND_SOFT_BREAK,
                                                 AMARK_NODE_CLASS_AUTO, 2, 3,
                                                 &middle),
                        AMARK_ERROR_NONE);

    NAOS_CHECK_UINT_EQ(quote, 2);
    NAOS_CHECK_UINT_EQ(para, 3);
    NAOS_CHECK_UINT_EQ(text1, 4);
    NAOS_CHECK_UINT_EQ(text2, 5);
    NAOS_CHECK_UINT_EQ(first, 6);
    NAOS_CHECK_UINT_EQ(middle, 7);

    check_links(1, AMARK_NODE_NONE, quote, quote, AMARK_NODE_NONE,
                AMARK_NODE_NONE);
    check_links(quote, 1, para, para, AMARK_NODE_NONE, AMARK_NODE_NONE);
    check_links(para, quote, first, text2, AMARK_NODE_NONE, AMARK_NODE_NONE);
    check_links(first, para, AMARK_NODE_NONE, AMARK_NODE_NONE,
                AMARK_NODE_NONE, text1);
    check_links(text1, para, AMARK_NODE_NONE, AMARK_NODE_NONE, first, middle);
    check_links(middle, para, AMARK_NODE_NONE, AMARK_NODE_NONE, text1, text2);
    check_links(text2, para, AMARK_NODE_NONE, AMARK_NODE_NONE, middle,
                AMARK_NODE_NONE);

    check_node(middle, AMARK_KIND_SOFT_BREAK, 1, 2, 3, 1, 1);
    check_node(text2, AMARK_KIND_TEXT, 1, 3, 5, 2, 2);
}

/* ---- the lines of a range (2.3, 2.4, 2.5, 2.6, 7.3) ---- */

/* Appends [start, end) to the root and checks its lines. */
static void check_append_lines(size_t start, size_t end, size_t start_line,
                               size_t end_line)
{
    amark_node_id_t id = AMARK_NODE_NONE;

    NAOS_REQUIRE_INT_EQ(amark_tree_append(g_doc, 1, AMARK_KIND_TEXT,
                                          AMARK_NODE_CLASS_AUTO, start, end,
                                          &id),
                        AMARK_ERROR_NONE);
    check_node(id, AMARK_KIND_TEXT, 1, start, end, start_line, end_line);
}

/* Inserts [start, end) before the first child of the root; checks lines. */
static void check_insert_lines(size_t start, size_t end, size_t start_line,
                               size_t end_line)
{
    amark_node_id_t id = AMARK_NODE_NONE;
    amark_node_id_t sibling = node_at(g_doc, 1)->first_child;

    NAOS_REQUIRE_UINT_NE(sibling, AMARK_NODE_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_insert_before(g_doc, sibling,
                                                 AMARK_KIND_PARAGRAPH,
                                                 AMARK_NODE_CLASS_AUTO, start,
                                                 end, &id),
                        AMARK_ERROR_NONE);
    check_node(id, AMARK_KIND_PARAGRAPH, 0, start, end, start_line, end_line);
}

NAOS_TEST(lines_of_ranges_in_mixed_line_endings)
{
    NAOS_REQUIRE_INT_EQ(create_mixed(&g_doc), AMARK_ERROR_NONE);

    /* Ending with a line ending: LF (2), the LF of CR LF (6), a CR (9). */
    check_append_lines(0, 3, 1, 1);
    check_append_lines(3, 7, 2, 2);
    check_append_lines(7, 10, 3, 3);
    check_append_lines(2, 3, 1, 1);
    /* Spanning lines. */
    check_append_lines(0, 12, 1, 4);
    check_append_lines(2, 4, 1, 2);
    check_append_lines(9, 11, 3, 4);
    /* Ending between the CR and the LF of CR LF (last byte is the CR). */
    check_append_lines(0, 6, 1, 2);
    check_append_lines(5, 6, 2, 2);
    /* Starting at the LF of CR LF. */
    check_append_lines(6, 8, 2, 3);
    /* Zero length: both lines are the line of start. */
    check_append_lines(0, 0, 1, 1);
    check_append_lines(3, 3, 2, 2);
    check_append_lines(6, 6, 2, 2);
    check_append_lines(7, 7, 3, 3);
    check_append_lines(10, 10, 4, 4);
    /* At the end of the content. */
    check_append_lines(10, 12, 4, 4);
    check_append_lines(11, 12, 4, 4);
    check_append_lines(12, 12, 4, 4);

    /* insert_before derives the lines by the same rule. */
    check_insert_lines(3, 7, 2, 2);
    check_insert_lines(0, 6, 1, 2);
    check_insert_lines(9, 9, 3, 3);
    check_insert_lines(12, 12, 4, 4);
    check_insert_lines(7, 12, 3, 4);
}

NAOS_TEST(lines_of_every_range)
{
    size_t start;
    size_t end;

    NAOS_REQUIRE_INT_EQ(create_mixed(&g_doc), AMARK_ERROR_NONE);
    for (start = 0; start <= MIXED_LENGTH; start++) {
        for (end = start; end <= MIXED_LENGTH; end++) {
            size_t end_line = start < end ? k_line_of[end - 1]
                                          : k_line_of[start];

            check_append_lines(start, end, k_line_of[start], end_line);
        }
    }
}

NAOS_TEST(empty_content)
{
    amark_node_id_t id = AMARK_NODE_NONE;

    NAOS_REQUIRE_INT_EQ(amark_document_create(&g_allocator, NULL, 0, 0, 0,
                                              NULL, &g_doc),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_append(g_doc, 1, AMARK_KIND_PARAGRAPH,
                                          AMARK_NODE_CLASS_AUTO, 0, 0, &id),
                        AMARK_ERROR_NONE);
    check_node(id, AMARK_KIND_PARAGRAPH, 0, 0, 0, 1, 1);
    check_links(id, 1, AMARK_NODE_NONE, AMARK_NODE_NONE, AMARK_NODE_NONE,
                AMARK_NODE_NONE);
}

/* ---- the node limit (7.9) ---- */

/* A copy of the parts of g_doc that a refused call must not change. */
typedef struct snapshot {
    void *data;
    size_t cap;
    size_t len;
    amark_node_t root;
    amark_node_t child;
} snapshot_t;

static snapshot_t take_snapshot(amark_node_id_t child)
{
    snapshot_t s;

    memset(&s, 0, sizeof(s));
    s.data = g_doc->nodes.data;
    s.cap = g_doc->nodes.cap;
    s.len = g_doc->nodes.len;
    s.root = *node_at(g_doc, 1);
    s.child = *node_at(g_doc, child);
    return s;
}

static void check_snapshot(const snapshot_t *s, amark_node_id_t child)
{
    NAOS_CHECK_PTR_EQ(g_doc->nodes.data, s->data);
    NAOS_CHECK_UINT_EQ(g_doc->nodes.cap, s->cap);
    NAOS_CHECK_UINT_EQ(g_doc->nodes.len, s->len);
    NAOS_CHECK_TRUE(memcmp(node_at(g_doc, 1), &s->root, sizeof(s->root)) ==
                    0);
    NAOS_CHECK_TRUE(memcmp(node_at(g_doc, child), &s->child,
                           sizeof(s->child)) == 0);
}

NAOS_TEST(limit_is_checked_before_the_node_storage)
{
    amark_node_id_t child = AMARK_NODE_NONE;
    amark_node_id_t out = 999;
    size_t mallocs;
    snapshot_t s;

    NAOS_REQUIRE_INT_EQ(create_mixed(&g_doc), AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_append(g_doc, 1, AMARK_KIND_PARAGRAPH,
                                          AMARK_NODE_CLASS_AUTO, 0, 3, &child),
                        AMARK_ERROR_NONE);

    /*
     * Pretend that UINT32_MAX - 1 nodes exist: the array length (nodes + the
     * unused element 0) becomes UINT32_MAX. Only elements below the real
     * length are touched; teardown restores the length if a check fails.
     */
    g_saved_len = g_doc->nodes.len;
    g_doc->nodes.len = (size_t)UINT32_MAX;
    s = take_snapshot(child);
    mallocs = g_state.malloc_calls;

    NAOS_CHECK_INT_EQ(amark_tree_append(g_doc, 1, AMARK_KIND_TEXT,
                                        AMARK_NODE_CLASS_AUTO, 0, 1, &out),
                      AMARK_ERROR_LIMIT_EXCEEDED);
    NAOS_CHECK_UINT_EQ(out, AMARK_NODE_NONE);
    check_snapshot(&s, child);

    out = 999;
    NAOS_CHECK_INT_EQ(amark_tree_insert_before(g_doc, child, AMARK_KIND_TEXT,
                                               AMARK_NODE_CLASS_AUTO, 0, 1,
                                               &out),
                      AMARK_ERROR_LIMIT_EXCEEDED);
    NAOS_CHECK_UINT_EQ(out, AMARK_NODE_NONE);
    check_snapshot(&s, child);

    out = 999;
    NAOS_CHECK_INT_EQ(amark_tree_append(g_doc, child, AMARK_KIND_DIALECT_MIN,
                                        AMARK_NODE_CLASS_INLINE, 0, 1, &out),
                      AMARK_ERROR_LIMIT_EXCEEDED);
    NAOS_CHECK_UINT_EQ(out, AMARK_NODE_NONE);
    check_snapshot(&s, child);

    /* Nothing was allocated. */
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, mallocs);

    g_doc->nodes.len = g_saved_len;
    g_saved_len = 0;

    /* With the real length, adding works again and the ID is the next one. */
    NAOS_CHECK_INT_EQ(amark_tree_append(g_doc, 1, AMARK_KIND_TEXT,
                                        AMARK_NODE_CLASS_AUTO, 0, 1, &out),
                      AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(out, 3);
}

/* ---- allocation failures (7.8) ---- */

#define LOOP_NODES 300

/*
 * Adds LOOP_NODES nodes to g_doc: odd steps append to the root, even steps
 * insert before the previous node. Stops at the first failure and returns
 * its error; *added receives the number of nodes added.
 */
static amark_error_t add_nodes(size_t *added)
{
    size_t i;
    amark_node_id_t prev = AMARK_NODE_NONE;

    *added = 0;
    for (i = 0; i < LOOP_NODES; i++) {
        amark_node_id_t out = 999;
        size_t len_before = g_doc->nodes.len;
        amark_node_t root_before = *node_at(g_doc, 1);
        amark_node_t prev_before;
        amark_error_t err;

        memset(&prev_before, 0, sizeof(prev_before));
        if (prev != AMARK_NODE_NONE) {
            prev_before = *node_at(g_doc, prev);
        }
        if (i % 2 == 0 || prev == AMARK_NODE_NONE) {
            err = amark_tree_append(g_doc, 1, AMARK_KIND_PARAGRAPH,
                                    AMARK_NODE_CLASS_AUTO, i % MIXED_LENGTH,
                                    MIXED_LENGTH, &out);
        } else {
            err = amark_tree_insert_before(g_doc, prev, AMARK_KIND_TEXT,
                                           AMARK_NODE_CLASS_AUTO, 0, 0, &out);
        }
        if (err != AMARK_ERROR_NONE) {
            /* The tree is as it was before the call. */
            NAOS_CHECK_UINT_EQ(out, AMARK_NODE_NONE);
            NAOS_CHECK_UINT_EQ(g_doc->nodes.len, len_before);
            NAOS_CHECK_TRUE(memcmp(node_at(g_doc, 1), &root_before,
                                   sizeof(root_before)) == 0);
            if (prev != AMARK_NODE_NONE) {
                NAOS_CHECK_TRUE(memcmp(node_at(g_doc, prev), &prev_before,
                                       sizeof(prev_before)) == 0);
            }
            return err;
        }
        NAOS_CHECK_UINT_EQ(out, (amark_node_id_t)(i + 2));
        prev = out;
        (*added)++;
    }
    return AMARK_ERROR_NONE;
}

NAOS_TEST(failure_injection_loop)
{
    size_t fail_at;
    size_t append_failures = 0;
    bool completed = false;

    for (fail_at = 1; fail_at < 1000; fail_at++) {
        amark_error_t err;
        size_t added = 0;

        amark_test_alloc_init(&g_state);
        g_state.fail_at = fail_at;
        err = create_mixed(&g_doc);
        if (err != AMARK_ERROR_NONE) {
            NAOS_CHECK_INT_EQ(err, AMARK_ERROR_NOMEM);
            NAOS_CHECK_UINT_EQ(g_state.live, 0);
            continue;
        }
        err = add_nodes(&added);
        if (err == AMARK_ERROR_NONE) {
            NAOS_CHECK_UINT_EQ(added, LOOP_NODES);
            completed = true;
        } else {
            NAOS_CHECK_INT_EQ(err, AMARK_ERROR_NOMEM);
            NAOS_CHECK_UINT_EQ(g_state.malloc_calls, fail_at);
            NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), added + 1);
            append_failures++;
        }
        /* The document can still be destroyed safely. */
        g_state.fail_at = 0;
        amark_document_destroy(g_doc);
        g_doc = NULL;
        NAOS_CHECK_UINT_EQ(g_state.live, 0);
        NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
        if (completed) {
            break;
        }
    }
    NAOS_CHECK_TRUE(completed);
    /* 300 nodes of 64 bytes need more than the first arena block. */
    NAOS_CHECK_UINT_GE(append_failures, 1);
}

NAOS_TEST(adding_works_after_a_failure)
{
    size_t added = 0;
    amark_node_id_t out = AMARK_NODE_NONE;

    NAOS_REQUIRE_INT_EQ(create_mixed(&g_doc), AMARK_ERROR_NONE);
    g_state.fail_at = g_state.malloc_calls + 1;
    NAOS_REQUIRE_INT_EQ(add_nodes(&added), AMARK_ERROR_NOMEM);
    g_state.fail_at = 0;
    NAOS_REQUIRE_INT_EQ(amark_tree_append(g_doc, 1, AMARK_KIND_TEXT,
                                          AMARK_NODE_CLASS_AUTO, 0, 1, &out),
                        AMARK_ERROR_NONE);
    /* The failed call used no ID. */
    NAOS_CHECK_UINT_EQ(out, (amark_node_id_t)(added + 2));
    NAOS_CHECK_UINT_EQ(node_at(g_doc, 1)->last_child, out);
}

/* ---- the same operations give the same tree (12.7) ---- */

NAOS_TEST(same_operations_give_the_same_tree)
{
    amark_document_t *docs[2];
    size_t d;
    size_t i;

    NAOS_REQUIRE_INT_EQ(create_mixed(&g_doc), AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(create_mixed(&g_doc2), AMARK_ERROR_NONE);
    docs[0] = g_doc;
    docs[1] = g_doc2;

    for (d = 0; d < 2; d++) {
        amark_document_t *doc = docs[d];
        amark_node_id_t a = AMARK_NODE_NONE;
        amark_node_id_t b = AMARK_NODE_NONE;
        amark_node_id_t c = AMARK_NODE_NONE;

        NAOS_REQUIRE_INT_EQ(amark_tree_append(doc, 1, AMARK_KIND_LIST,
                                              AMARK_NODE_CLASS_AUTO, 0, 12,
                                              &a),
                            AMARK_ERROR_NONE);
        NAOS_REQUIRE_INT_EQ(amark_tree_append(doc, a, AMARK_KIND_LIST_ITEM,
                                              AMARK_NODE_CLASS_AUTO, 3, 7, &b),
                            AMARK_ERROR_NONE);
        NAOS_REQUIRE_INT_EQ(amark_tree_insert_before(doc, b,
                                                     AMARK_KIND_LIST_ITEM,
                                                     AMARK_NODE_CLASS_AUTO, 0,
                                                     3, &c),
                            AMARK_ERROR_NONE);
        NAOS_REQUIRE_INT_EQ(amark_tree_append(doc, c, AMARK_KIND_DIALECT_MIN,
                                              AMARK_NODE_CLASS_INLINE, 1, 2,
                                              &a),
                            AMARK_ERROR_NONE);
        NAOS_REQUIRE_INT_EQ(amark_tree_append(doc, 1, AMARK_KIND_PARAGRAPH,
                                              AMARK_NODE_CLASS_AUTO, 7, 12,
                                              &a),
                            AMARK_ERROR_NONE);
    }

    NAOS_REQUIRE_UINT_EQ(g_doc->nodes.len, g_doc2->nodes.len);
    for (i = 1; i < g_doc->nodes.len; i++) {
        NAOS_CHECK_TRUE(memcmp(node_at(g_doc, (amark_node_id_t)i),
                               node_at(g_doc2, (amark_node_id_t)i),
                               sizeof(amark_node_t)) == 0);
    }
}

/* ---- changing, removing and wrapping (4.8, 7.5, 7.13) ---- */

#define WALK_MAX 512

/*
 * Walks the tree of g_doc in document order through the internal layout,
 * without recursion, and compares the IDs with expected[0..count). For each
 * node reached it also checks that the kind is not AMARK_KIND_NONE, that
 * the lines match the range (k_line_of) and that the links agree: every
 * child names the node as its parent, the previous sibling of each child is
 * the child before it, the first child has no previous sibling and the last
 * child reached is the node's last child.
 */
static void check_walk(const amark_node_id_t *expected, size_t count)
{
    amark_node_id_t seen[WALK_MAX];
    size_t n = 0;
    size_t i;
    amark_node_id_t id = 1;

    while (id != AMARK_NODE_NONE) {
        const amark_node_t *node;
        amark_node_id_t prev = AMARK_NODE_NONE;
        amark_node_id_t c;

        NAOS_REQUIRE_UINT_LT(n, WALK_MAX);
        NAOS_REQUIRE_UINT_LT(id, amark_array_len(&g_doc->nodes));
        seen[n++] = id;
        node = node_at(g_doc, id);
        NAOS_CHECK_UINT_NE(node->kind, AMARK_KIND_NONE);
        NAOS_CHECK_UINT_EQ(node->start_line, k_line_of[node->start]);
        NAOS_CHECK_UINT_EQ(node->end_line,
                           node->start < node->end ? k_line_of[node->end - 1]
                                                   : k_line_of[node->start]);
        if (id == 1) {
            NAOS_CHECK_UINT_EQ(node->parent, AMARK_NODE_NONE);
            NAOS_CHECK_UINT_EQ(node->prev_sibling, AMARK_NODE_NONE);
            NAOS_CHECK_UINT_EQ(node->next_sibling, AMARK_NODE_NONE);
        }
        for (c = node->first_child; c != AMARK_NODE_NONE;
             c = node_at(g_doc, c)->next_sibling) {
            NAOS_REQUIRE_UINT_LT(c, amark_array_len(&g_doc->nodes));
            NAOS_CHECK_UINT_EQ(node_at(g_doc, c)->parent, id);
            NAOS_CHECK_UINT_EQ(node_at(g_doc, c)->prev_sibling, prev);
            prev = c;
        }
        NAOS_CHECK_UINT_EQ(node->last_child, prev);

        /* The next node in document order. */
        if (node->first_child != AMARK_NODE_NONE) {
            id = node->first_child;
            continue;
        }
        while (id != AMARK_NODE_NONE &&
               node_at(g_doc, id)->next_sibling == AMARK_NODE_NONE) {
            id = node_at(g_doc, id)->parent;
        }
        if (id != AMARK_NODE_NONE) {
            id = node_at(g_doc, id)->next_sibling;
        }
    }

    NAOS_CHECK_UINT_EQ(n, count);
    for (i = 0; i < n && i < count; i++) {
        NAOS_CHECK_UINT_EQ(seen[i], expected[i]);
    }
}

#define CHECK_WALK(...)                                                     \
    do {                                                                    \
        static const amark_node_id_t expected_[] = {__VA_ARGS__};           \
        check_walk(expected_, sizeof(expected_) / sizeof(expected_[0]));    \
    } while (0)

/* Appends a node and returns its ID; leaves the test on failure. */
static amark_node_id_t add(amark_node_id_t parent, amark_kind_t kind,
                           size_t start, size_t end)
{
    amark_node_id_t id = AMARK_NODE_NONE;

    NAOS_REQUIRE_INT_EQ(amark_tree_append(g_doc, parent, kind,
                                          AMARK_NODE_CLASS_AUTO, start, end,
                                          &id),
                        AMARK_ERROR_NONE);
    return id;
}

NAOS_TEST(walk_after_insert_before_at_the_head_and_in_the_middle)
{
    amark_node_id_t para;
    amark_node_id_t t1;
    amark_node_id_t t2;
    amark_node_id_t out = AMARK_NODE_NONE;

    NAOS_REQUIRE_INT_EQ(create_mixed(&g_doc), AMARK_ERROR_NONE);
    para = add(1, AMARK_KIND_PARAGRAPH, 0, 12);  /* 2 */
    t1 = add(para, AMARK_KIND_TEXT, 0, 2);       /* 3 */
    t2 = add(para, AMARK_KIND_TEXT, 3, 5);       /* 4 */
    CHECK_WALK(1, 2, 3, 4);

    /* At the head of the children of the paragraph. */
    NAOS_REQUIRE_INT_EQ(amark_tree_insert_before(g_doc, t1, AMARK_KIND_TEXT,
                                                 AMARK_NODE_CLASS_AUTO, 0, 0,
                                                 &out),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(out, 5);
    CHECK_WALK(1, 2, 5, 3, 4);

    /* In the middle. */
    NAOS_REQUIRE_INT_EQ(amark_tree_insert_before(g_doc, t2,
                                                 AMARK_KIND_SOFT_BREAK,
                                                 AMARK_NODE_CLASS_AUTO, 2, 3,
                                                 &out),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(out, 6);
    CHECK_WALK(1, 2, 5, 3, 6, 4);

    /* At the head of the children of the root. */
    NAOS_REQUIRE_INT_EQ(amark_tree_insert_before(g_doc, para,
                                                 AMARK_KIND_THEMATIC_BREAK,
                                                 AMARK_NODE_CLASS_AUTO, 0, 3,
                                                 &out),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(out, 7);
    CHECK_WALK(1, 7, 2, 5, 3, 6, 4);
    check_links(1, AMARK_NODE_NONE, 7, para, AMARK_NODE_NONE,
                AMARK_NODE_NONE);
}

NAOS_TEST(set_range_derives_the_lines_again)
{
    amark_node_id_t para;
    amark_node_id_t text;
    amark_node_t before;
    const amark_node_t *n;

    NAOS_REQUIRE_INT_EQ(create_mixed(&g_doc), AMARK_ERROR_NONE);
    para = add(1, AMARK_KIND_PARAGRAPH, 0, 3);
    text = add(para, AMARK_KIND_TEXT, 0, 2);
    before = *node_at(g_doc, para);

    /* Extended over lines 1-4, as an open block grows. */
    amark_tree_set_range(g_doc, para, 0, 12);
    check_node(para, AMARK_KIND_PARAGRAPH, 0, 0, 12, 1, 4);
    CHECK_WALK(1, 2, 3);

    /* Ending at the CR of CR LF, then spanning lines 2-3. */
    amark_tree_set_range(g_doc, para, 3, 6);
    check_node(para, AMARK_KIND_PARAGRAPH, 0, 3, 6, 2, 2);
    amark_tree_set_range(g_doc, para, 6, 9);
    check_node(para, AMARK_KIND_PARAGRAPH, 0, 6, 9, 2, 3);
    /* Zero length: both lines are the line of start. */
    amark_tree_set_range(g_doc, para, 10, 10);
    check_node(para, AMARK_KIND_PARAGRAPH, 0, 10, 10, 4, 4);
    amark_tree_set_range(g_doc, para, 12, 12);
    check_node(para, AMARK_KIND_PARAGRAPH, 0, 12, 12, 4, 4);
    /* Back to a range that holds the child. */
    amark_tree_set_range(g_doc, para, 0, 3);

    /* The links are unchanged. */
    n = node_at(g_doc, para);
    NAOS_CHECK_TRUE(memcmp(n, &before, sizeof(before)) == 0);
    check_links(para, 1, text, text, AMARK_NODE_NONE, AMARK_NODE_NONE);
    CHECK_WALK(1, 2, 3);
}

NAOS_TEST(set_kind_resets_the_attributes_and_keeps_the_children)
{
    amark_node_id_t para;
    amark_node_id_t t1;
    amark_node_id_t t2;
    amark_node_id_t text;

    NAOS_REQUIRE_INT_EQ(create_mixed(&g_doc), AMARK_ERROR_NONE);
    para = add(1, AMARK_KIND_PARAGRAPH, 0, 7);
    t1 = add(para, AMARK_KIND_TEXT, 0, 2);
    t2 = add(para, AMARK_KIND_TEXT, 3, 5);
    text = add(1, AMARK_KIND_PARAGRAPH, 7, 12);

    /* Pretend that attributes were set. */
    node_at(g_doc, para)->attr = 5;
    node_at(g_doc, text)->attr = 7;

    /* A paragraph becomes a setext heading: block, no attributes. */
    amark_tree_set_kind(g_doc, para, AMARK_KIND_HEADING,
                        AMARK_NODE_CLASS_AUTO);
    check_node(para, AMARK_KIND_HEADING, 0, 0, 7, 1, 2);
    check_links(para, 1, t1, t2, AMARK_NODE_NONE, text);

    /* A CommonMark inline kind is an inline. */
    amark_tree_set_kind(g_doc, text, AMARK_KIND_TEXT, AMARK_NODE_CLASS_AUTO);
    check_node(text, AMARK_KIND_TEXT, 1, 7, 12, 3, 4);

    /* A dialect kind takes the given class. */
    node_at(g_doc, t1)->attr = 3;
    amark_tree_set_kind(g_doc, t1, AMARK_KIND_DIALECT_MIN,
                        AMARK_NODE_CLASS_BLOCK);
    check_node(t1, AMARK_KIND_DIALECT_MIN, 0, 0, 2, 1, 1);
    amark_tree_set_kind(g_doc, t1, AMARK_KIND_DIALECT_MIN + 1,
                        AMARK_NODE_CLASS_INLINE);
    check_node(t1, AMARK_KIND_DIALECT_MIN + 1, 1, 0, 2, 1, 1);

    check_links(1, AMARK_NODE_NONE, para, text, AMARK_NODE_NONE,
                AMARK_NODE_NONE);
    check_links(t1, para, AMARK_NODE_NONE, AMARK_NODE_NONE, AMARK_NODE_NONE,
                t2);
    CHECK_WALK(1, 2, 3, 4, 5);
}

NAOS_TEST(remove_first_middle_last_and_only_child)
{
    amark_node_id_t a;
    amark_node_id_t b;
    amark_node_id_t b1;
    amark_node_id_t b2;
    amark_node_id_t c;
    amark_node_id_t d;
    amark_node_id_t e;
    amark_node_t b1_before;
    amark_node_t b2_before;
    size_t count;

    NAOS_REQUIRE_INT_EQ(create_mixed(&g_doc), AMARK_ERROR_NONE);
    a = add(1, AMARK_KIND_THEMATIC_BREAK, 0, 3);  /* 2 */
    b = add(1, AMARK_KIND_PARAGRAPH, 3, 7);       /* 3 */
    b1 = add(b, AMARK_KIND_TEXT, 3, 4);           /* 4 */
    b2 = add(b, AMARK_KIND_TEXT, 4, 5);           /* 5 */
    c = add(1, AMARK_KIND_BLOCK_QUOTE, 7, 12);    /* 6 */
    d = add(c, AMARK_KIND_PARAGRAPH, 7, 12);      /* 7 */
    e = add(1, AMARK_KIND_PARAGRAPH, 12, 12);     /* 8 */
    count = amark_document_node_count(g_doc);
    CHECK_WALK(1, 2, 3, 4, 5, 6, 7, 8);

    /* The first child. */
    amark_tree_remove(g_doc, a);
    NAOS_CHECK_UINT_EQ(node_at(g_doc, a)->kind, AMARK_KIND_NONE);
    check_links(1, AMARK_NODE_NONE, b, e, AMARK_NODE_NONE, AMARK_NODE_NONE);
    CHECK_WALK(1, 3, 4, 5, 6, 7, 8);

    /* A middle child, with its subtree; the descendants are not touched. */
    b1_before = *node_at(g_doc, b1);
    b2_before = *node_at(g_doc, b2);
    amark_tree_remove(g_doc, b);
    NAOS_CHECK_UINT_EQ(node_at(g_doc, b)->kind, AMARK_KIND_NONE);
    NAOS_CHECK_TRUE(memcmp(node_at(g_doc, b1), &b1_before,
                           sizeof(b1_before)) == 0);
    NAOS_CHECK_TRUE(memcmp(node_at(g_doc, b2), &b2_before,
                           sizeof(b2_before)) == 0);
    check_links(1, AMARK_NODE_NONE, c, e, AMARK_NODE_NONE, AMARK_NODE_NONE);
    check_links(c, 1, d, d, AMARK_NODE_NONE, e);
    CHECK_WALK(1, 6, 7, 8);

    /* The last child. */
    amark_tree_remove(g_doc, e);
    NAOS_CHECK_UINT_EQ(node_at(g_doc, e)->kind, AMARK_KIND_NONE);
    check_links(1, AMARK_NODE_NONE, c, c, AMARK_NODE_NONE, AMARK_NODE_NONE);
    check_links(c, 1, d, d, AMARK_NODE_NONE, AMARK_NODE_NONE);
    CHECK_WALK(1, 6, 7);

    /* The only child of the block quote, then the only child of the root. */
    amark_tree_remove(g_doc, d);
    check_links(c, 1, AMARK_NODE_NONE, AMARK_NODE_NONE, AMARK_NODE_NONE,
                AMARK_NODE_NONE);
    CHECK_WALK(1, 6);
    amark_tree_remove(g_doc, c);
    check_links(1, AMARK_NODE_NONE, AMARK_NODE_NONE, AMARK_NODE_NONE,
                AMARK_NODE_NONE, AMARK_NODE_NONE);
    CHECK_WALK(1);

    /* The elements are not reused: the count is unchanged, IDs go on. */
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), count);
    NAOS_CHECK_UINT_EQ(add(1, AMARK_KIND_PARAGRAPH, 0, 3), 9);
    CHECK_WALK(1, 9);
}

NAOS_TEST(wrap_one_several_and_all_children)
{
    amark_node_id_t para;
    amark_node_id_t t[5];
    amark_node_id_t out = 999;
    size_t i;

    NAOS_REQUIRE_INT_EQ(create_mixed(&g_doc), AMARK_ERROR_NONE);
    para = add(1, AMARK_KIND_PARAGRAPH, 0, 12);  /* 2 */
    for (i = 0; i < 5; i++) {
        /* [0,2) [2,4) [4,6) [6,8) [8,10): IDs 3..7 */
        t[i] = add(para, AMARK_KIND_TEXT, 2 * i, 2 * i + 2);
    }
    CHECK_WALK(1, 2, 3, 4, 5, 6, 7);

    /* One child, at the head. */
    NAOS_REQUIRE_INT_EQ(amark_tree_wrap(g_doc, t[0], t[0],
                                        AMARK_KIND_EMPHASIS,
                                        AMARK_NODE_CLASS_AUTO, 0, 2, &out),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(out, 8);
    check_node(8, AMARK_KIND_EMPHASIS, 1, 0, 2, 1, 1);
    check_links(8, para, t[0], t[0], AMARK_NODE_NONE, t[1]);
    check_links(t[0], 8, AMARK_NODE_NONE, AMARK_NODE_NONE, AMARK_NODE_NONE,
                AMARK_NODE_NONE);
    check_links(para, 1, 8, t[4], AMARK_NODE_NONE, AMARK_NODE_NONE);
    CHECK_WALK(1, 2, 8, 3, 4, 5, 6, 7);

    /* Several children, in the middle. */
    out = 999;
    NAOS_REQUIRE_INT_EQ(amark_tree_wrap(g_doc, t[1], t[3], AMARK_KIND_STRONG,
                                        AMARK_NODE_CLASS_AUTO, 2, 8, &out),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(out, 9);
    check_node(9, AMARK_KIND_STRONG, 1, 2, 8, 1, 3);
    check_links(9, para, t[1], t[3], 8, t[4]);
    check_links(t[1], 9, AMARK_NODE_NONE, AMARK_NODE_NONE, AMARK_NODE_NONE,
                t[2]);
    check_links(t[2], 9, AMARK_NODE_NONE, AMARK_NODE_NONE, t[1], t[3]);
    check_links(t[3], 9, AMARK_NODE_NONE, AMARK_NODE_NONE, t[2],
                AMARK_NODE_NONE);
    check_links(8, para, t[0], t[0], AMARK_NODE_NONE, 9);
    check_links(t[4], para, AMARK_NODE_NONE, AMARK_NODE_NONE, 9,
                AMARK_NODE_NONE);
    CHECK_WALK(1, 2, 8, 3, 9, 4, 5, 6, 7);

    /* One child, at the end. */
    out = 999;
    NAOS_REQUIRE_INT_EQ(amark_tree_wrap(g_doc, t[4], t[4],
                                        AMARK_KIND_DIALECT_MIN,
                                        AMARK_NODE_CLASS_INLINE, 8, 10, &out),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(out, 10);
    check_node(10, AMARK_KIND_DIALECT_MIN, 1, 8, 10, 3, 3);
    check_links(para, 1, 8, 10, AMARK_NODE_NONE, AMARK_NODE_NONE);
    CHECK_WALK(1, 2, 8, 3, 9, 4, 5, 6, 10, 7);

    /* All children of the paragraph. */
    out = 999;
    NAOS_REQUIRE_INT_EQ(amark_tree_wrap(g_doc, 8, 10, AMARK_KIND_LINK,
                                        AMARK_NODE_CLASS_AUTO, 0, 10, &out),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(out, 11);
    check_node(11, AMARK_KIND_LINK, 1, 0, 10, 1, 3);
    check_links(para, 1, 11, 11, AMARK_NODE_NONE, AMARK_NODE_NONE);
    check_links(11, para, 8, 10, AMARK_NODE_NONE, AMARK_NODE_NONE);
    check_links(8, 11, t[0], t[0], AMARK_NODE_NONE, 9);
    check_links(10, 11, t[4], t[4], 9, AMARK_NODE_NONE);
    CHECK_WALK(1, 2, 11, 8, 3, 9, 4, 5, 6, 10, 7);

    /* The only child of the root (the paragraph) as a block. */
    out = 999;
    NAOS_REQUIRE_INT_EQ(amark_tree_wrap(g_doc, para, para,
                                        AMARK_KIND_BLOCK_QUOTE,
                                        AMARK_NODE_CLASS_AUTO, 0, 12, &out),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(out, 12);
    check_node(12, AMARK_KIND_BLOCK_QUOTE, 0, 0, 12, 1, 4);
    check_links(1, AMARK_NODE_NONE, 12, 12, AMARK_NODE_NONE,
                AMARK_NODE_NONE);
    check_links(12, 1, para, para, AMARK_NODE_NONE, AMARK_NODE_NONE);
    check_links(para, 12, 11, 11, AMARK_NODE_NONE, AMARK_NODE_NONE);
    CHECK_WALK(1, 12, 2, 11, 8, 3, 9, 4, 5, 6, 10, 7);
}

NAOS_TEST(changes_after_removing_and_wrapping)
{
    amark_node_id_t para;
    amark_node_id_t open;
    amark_node_id_t mid;
    amark_node_id_t close;
    amark_node_id_t out = AMARK_NODE_NONE;

    /* What the inline parser does with "*x*": wrap, remove delimiters. */
    NAOS_REQUIRE_INT_EQ(create_mixed(&g_doc), AMARK_ERROR_NONE);
    para = add(1, AMARK_KIND_PARAGRAPH, 0, 3);  /* 2 */
    open = add(para, AMARK_KIND_TEXT, 0, 1);    /* 3 */
    mid = add(para, AMARK_KIND_TEXT, 1, 2);     /* 4 */
    close = add(para, AMARK_KIND_TEXT, 2, 3);   /* 5 */

    NAOS_REQUIRE_INT_EQ(amark_tree_wrap(g_doc, mid, mid, AMARK_KIND_EMPHASIS,
                                        AMARK_NODE_CLASS_AUTO, 0, 3, &out),
                        AMARK_ERROR_NONE);
    CHECK_WALK(1, 2, 3, 6, 4, 5);
    amark_tree_remove(g_doc, open);
    CHECK_WALK(1, 2, 6, 4, 5);
    amark_tree_remove(g_doc, close);
    CHECK_WALK(1, 2, 6, 4);
    check_links(para, 1, out, out, AMARK_NODE_NONE, AMARK_NODE_NONE);
    check_links(out, para, mid, mid, AMARK_NODE_NONE, AMARK_NODE_NONE);

    /* The paragraph grows over line 2 and becomes a heading. */
    amark_tree_set_range(g_doc, para, 0, 7);
    amark_tree_set_kind(g_doc, para, AMARK_KIND_HEADING,
                        AMARK_NODE_CLASS_AUTO);
    check_node(para, AMARK_KIND_HEADING, 0, 0, 7, 1, 2);
    CHECK_WALK(1, 2, 6, 4);
}

/* A full copy of g_doc's node array, to compare after a refused call. */
static amark_node_t g_nodes_before[LOOP_NODES + 8];
static size_t g_nodes_before_len;

static void save_nodes(void)
{
    g_nodes_before_len = amark_array_len(&g_doc->nodes);
    NAOS_REQUIRE_UINT_LE(g_nodes_before_len,
                         sizeof(g_nodes_before) / sizeof(g_nodes_before[0]));
    memcpy(g_nodes_before, g_doc->nodes.data,
           g_nodes_before_len * sizeof(amark_node_t));
}

static void check_nodes_unchanged(void)
{
    NAOS_CHECK_UINT_EQ(amark_array_len(&g_doc->nodes), g_nodes_before_len);
    NAOS_CHECK_TRUE(memcmp(g_nodes_before, g_doc->nodes.data,
                           g_nodes_before_len * sizeof(amark_node_t)) == 0);
}

NAOS_TEST(wrap_limit_leaves_the_tree_unchanged)
{
    amark_node_id_t para;
    amark_node_id_t t1;
    amark_node_id_t t2;
    amark_node_id_t out = 999;
    size_t mallocs;
    void *data;

    NAOS_REQUIRE_INT_EQ(create_mixed(&g_doc), AMARK_ERROR_NONE);
    para = add(1, AMARK_KIND_PARAGRAPH, 0, 12);
    t1 = add(para, AMARK_KIND_TEXT, 0, 2);
    t2 = add(para, AMARK_KIND_TEXT, 3, 5);
    save_nodes();

    /* Pretend that UINT32_MAX - 1 nodes exist (see the limit test above). */
    g_saved_len = g_doc->nodes.len;
    g_doc->nodes.len = (size_t)UINT32_MAX;
    data = g_doc->nodes.data;
    mallocs = g_state.malloc_calls;

    NAOS_CHECK_INT_EQ(amark_tree_wrap(g_doc, t1, t2, AMARK_KIND_EMPHASIS,
                                      AMARK_NODE_CLASS_AUTO, 0, 5, &out),
                      AMARK_ERROR_LIMIT_EXCEEDED);
    NAOS_CHECK_UINT_EQ(out, AMARK_NODE_NONE);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, mallocs);
    NAOS_CHECK_PTR_EQ(g_doc->nodes.data, data);
    NAOS_CHECK_UINT_EQ(g_doc->nodes.len, (size_t)UINT32_MAX);

    g_doc->nodes.len = g_saved_len;
    g_saved_len = 0;
    check_nodes_unchanged();
    CHECK_WALK(1, 2, 3, 4);

    /* With the real length, wrapping works and the ID is the next one. */
    NAOS_CHECK_INT_EQ(amark_tree_wrap(g_doc, t1, t2, AMARK_KIND_EMPHASIS,
                                      AMARK_NODE_CLASS_AUTO, 0, 5, &out),
                      AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(out, 5);
    CHECK_WALK(1, 2, 5, 3, 4);
}

/*
 * Wraps all children of the root LOOP_NODES times, so that each wrap nests
 * the previous ones one level deeper. Stops at the first failure, checks
 * that the failed call changed nothing and returns its error; *added
 * receives the number of wraps done.
 */
static amark_error_t wrap_nodes(size_t *added)
{
    size_t i;

    *added = 0;
    for (i = 0; i < LOOP_NODES; i++) {
        const amark_node_t *root = node_at(g_doc, 1);
        amark_node_id_t out = 999;
        amark_error_t err;

        save_nodes();
        err = amark_tree_wrap(g_doc, root->first_child, root->last_child,
                              AMARK_KIND_BLOCK_QUOTE, AMARK_NODE_CLASS_AUTO,
                              0, MIXED_LENGTH, &out);
        if (err != AMARK_ERROR_NONE) {
            NAOS_CHECK_UINT_EQ(out, AMARK_NODE_NONE);
            check_nodes_unchanged();
            return err;
        }
        NAOS_CHECK_UINT_EQ(out, (amark_node_id_t)(i + 4));
        (*added)++;
    }
    return AMARK_ERROR_NONE;
}

NAOS_TEST(wrap_failure_injection_loop)
{
    size_t fail_at;
    size_t wrap_failures = 0;
    bool completed = false;
    amark_node_id_t expected[LOOP_NODES + 3];

    for (fail_at = 1; fail_at < 1000; fail_at++) {
        amark_error_t err;
        amark_node_id_t a = AMARK_NODE_NONE;
        amark_node_id_t b = AMARK_NODE_NONE;
        size_t added = 0;
        size_t i;

        amark_test_alloc_init(&g_state);
        err = create_mixed(&g_doc);
        NAOS_REQUIRE_INT_EQ(err, AMARK_ERROR_NONE);
        NAOS_REQUIRE_INT_EQ(amark_tree_append(g_doc, 1, AMARK_KIND_PARAGRAPH,
                                              AMARK_NODE_CLASS_AUTO, 0, 3, &a),
                            AMARK_ERROR_NONE);
        NAOS_REQUIRE_INT_EQ(amark_tree_append(g_doc, 1, AMARK_KIND_PARAGRAPH,
                                              AMARK_NODE_CLASS_AUTO, 3, 12,
                                              &b),
                            AMARK_ERROR_NONE);
        g_state.fail_at = g_state.malloc_calls + fail_at;

        err = wrap_nodes(&added);
        g_state.fail_at = 0;
        if (err == AMARK_ERROR_NONE) {
            NAOS_CHECK_UINT_EQ(added, LOOP_NODES);
            completed = true;
        } else {
            NAOS_CHECK_INT_EQ(err, AMARK_ERROR_NOMEM);
            NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), added + 3);
            wrap_failures++;
        }
        /* The tree is whole: the wraps from the last down, then a and b. */
        expected[0] = 1;
        for (i = 0; i < added; i++) {
            expected[i + 1] = (amark_node_id_t)(added + 3 - i);
        }
        expected[added + 1] = a;
        expected[added + 2] = b;
        check_walk(expected, added + 3);

        amark_document_destroy(g_doc);
        g_doc = NULL;
        NAOS_CHECK_UINT_EQ(g_state.live, 0);
        NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
        if (completed) {
            break;
        }
    }
    NAOS_CHECK_TRUE(completed);
    /* 300 nodes of 64 bytes need more than the first arena block. */
    NAOS_CHECK_UINT_GE(wrap_failures, 1);
}

NAOS_SUITE(tree,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(every_core_kind_as_children_of_the_root),
    NAOS_ADD_TEST(every_core_kind_by_insert_before),
    NAOS_ADD_TEST(dialect_kinds_use_the_given_class),
    NAOS_ADD_TEST(nested_append_and_insert_before),
    NAOS_ADD_TEST(lines_of_ranges_in_mixed_line_endings),
    NAOS_ADD_TEST(lines_of_every_range),
    NAOS_ADD_TEST(empty_content),
    NAOS_ADD_TEST(limit_is_checked_before_the_node_storage),
    NAOS_ADD_TEST(failure_injection_loop),
    NAOS_ADD_TEST(adding_works_after_a_failure),
    NAOS_ADD_TEST(same_operations_give_the_same_tree),
    NAOS_ADD_TEST(walk_after_insert_before_at_the_head_and_in_the_middle),
    NAOS_ADD_TEST(set_range_derives_the_lines_again),
    NAOS_ADD_TEST(set_kind_resets_the_attributes_and_keeps_the_children),
    NAOS_ADD_TEST(remove_first_middle_last_and_only_child),
    NAOS_ADD_TEST(wrap_one_several_and_all_children),
    NAOS_ADD_TEST(changes_after_removing_and_wrapping),
    NAOS_ADD_TEST(wrap_limit_leaves_the_tree_unchanged),
    NAOS_ADD_TEST(wrap_failure_injection_loop)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(tree)
)
