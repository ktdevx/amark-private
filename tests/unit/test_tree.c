/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of the tree builder (src/tree.h): adding nodes as the last
 * child of a parent and before a sibling, the lines derived from the range,
 * the node limit and allocation failures. The results are checked through
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
    NAOS_ADD_TEST(same_operations_give_the_same_tree)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(tree)
)
