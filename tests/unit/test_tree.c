/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of the tree builder (src/tree.h): adding nodes as the last
 * child of a parent and before a sibling, the lines derived from the range,
 * changing the range and the kind, removing and wrapping nodes, setting the
 * attributes of each kind, the node and attribute limits and allocation
 * failures. The results are checked through
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
 * runs) restores a node or attribute array length a test changed, destroys
 * them and then checks that no block is left and that every call received
 * the context value it was given.
 */
static amark_test_alloc_t g_state;
static amark_allocator_t g_allocator;
static amark_document_t *g_doc;
static amark_document_t *g_doc2;
static size_t g_saved_len; /* 0: the length of g_doc's node array is real */
static size_t g_saved_attrs_len; /* the same for the attribute array */

NAOS_SETUP(counting)
{
    amark_test_alloc_init(&g_state);
    g_allocator = amark_test_allocator(&g_state);
    g_doc = NULL;
    g_doc2 = NULL;
    g_saved_len = 0;
    g_saved_attrs_len = 0;
}

NAOS_TEARDOWN(counting)
{
    g_state.fail_at = 0;
    if (g_doc != NULL && g_saved_len != 0) {
        g_doc->nodes.len = g_saved_len;
    }
    if (g_doc != NULL && g_saved_attrs_len != 0) {
        g_doc->attrs.len = g_saved_attrs_len;
    }
    g_saved_len = 0;
    g_saved_attrs_len = 0;
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

/* ---- the attributes (3.1-3.14, 7.4, 7.6, 7.8) ---- */

/* Returns the attribute element of node id, which must have one. */
static const amark_node_attr_t *attr_of(amark_node_id_t id)
{
    const amark_node_t *n;

    NAOS_REQUIRE_UINT_LT(id, amark_array_len(&g_doc->nodes));
    n = node_at(g_doc, id);
    NAOS_REQUIRE_UINT_NE(n->attr, 0);
    NAOS_REQUIRE_UINT_LT(n->attr, amark_array_len(&g_doc->attrs));
    return (const amark_node_attr_t *)amark_array_at(&g_doc->attrs, n->attr);
}

/* Checks an optional range: not present, or present with [start, end). */
static void check_opt(const amark_opt_range_t *r, bool present, size_t start,
                      size_t end)
{
    NAOS_CHECK_TRUE(r->present == present);
    if (present) {
        NAOS_CHECK_UINT_EQ(r->start, start);
        NAOS_CHECK_UINT_EQ(r->end, end);
    }
}

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

NAOS_TEST(heading_attributes_and_overwrite)
{
    amark_node_id_t h;
    const amark_node_attr_t *a;

    NAOS_REQUIRE_INT_EQ(create_mixed(&g_doc), AMARK_ERROR_NONE);
    /* Element 0 of the attribute array is not used. */
    NAOS_CHECK_UINT_EQ(amark_array_len(&g_doc->attrs), 1);
    h = add(1, AMARK_KIND_HEADING, 0, 3);
    NAOS_CHECK_UINT_EQ(node_at(g_doc, h)->attr, 0);

    NAOS_REQUIRE_INT_EQ(amark_tree_set_heading(g_doc, h, 3, AMARK_HEADING_ATX),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(node_at(g_doc, h)->attr, 1);
    NAOS_CHECK_UINT_EQ(amark_array_len(&g_doc->attrs), 2);
    a = attr_of(h);
    NAOS_CHECK_UINT_EQ(a->heading.level, 3);
    NAOS_CHECK_INT_EQ(a->heading.style, AMARK_HEADING_ATX);

    /* A second call overwrites the same element and adds none. */
    NAOS_REQUIRE_INT_EQ(
        amark_tree_set_heading(g_doc, h, 6, AMARK_HEADING_SETEXT),
        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(node_at(g_doc, h)->attr, 1);
    NAOS_CHECK_UINT_EQ(amark_array_len(&g_doc->attrs), 2);
    a = attr_of(h);
    NAOS_CHECK_UINT_EQ(a->heading.level, 6);
    NAOS_CHECK_INT_EQ(a->heading.style, AMARK_HEADING_SETEXT);

    /* The kind, range, lines and links are untouched. */
    NAOS_CHECK_UINT_EQ(node_at(g_doc, h)->kind, AMARK_KIND_HEADING);
    NAOS_CHECK_UINT_EQ(node_at(g_doc, h)->start, 0);
    NAOS_CHECK_UINT_EQ(node_at(g_doc, h)->end, 3);
    NAOS_CHECK_UINT_EQ(node_at(g_doc, h)->start_line, 1);
    NAOS_CHECK_UINT_EQ(node_at(g_doc, h)->end_line, 1);
    check_links(h, 1, AMARK_NODE_NONE, AMARK_NODE_NONE, AMARK_NODE_NONE,
                AMARK_NODE_NONE);
}

NAOS_TEST(list_attributes_and_start_numbers)
{
    amark_node_id_t bullet;
    amark_node_id_t zero;
    amark_node_id_t big;
    const amark_node_attr_t *a;

    NAOS_REQUIRE_INT_EQ(create_mixed(&g_doc), AMARK_ERROR_NONE);
    bullet = add(1, AMARK_KIND_LIST, 0, 3);
    zero = add(1, AMARK_KIND_LIST, 3, 7);
    big = add(1, AMARK_KIND_LIST, 7, 12);

    NAOS_REQUIRE_INT_EQ(amark_tree_set_list(g_doc, bullet, AMARK_LIST_BULLET,
                                            '-', 0, AMARK_LIST_TIGHT),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_list(g_doc, zero, AMARK_LIST_ORDERED,
                                            ')', 0, AMARK_LIST_TIGHT),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_list(g_doc, big, AMARK_LIST_ORDERED,
                                            '.', 999999999u, AMARK_LIST_LOOSE),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(amark_array_len(&g_doc->attrs), 4);

    a = attr_of(bullet);
    NAOS_CHECK_INT_EQ(a->list.type, AMARK_LIST_BULLET);
    NAOS_CHECK_INT_EQ(a->list.marker, '-');
    NAOS_CHECK_INT_EQ(a->list.spacing, AMARK_LIST_TIGHT);
    a = attr_of(zero);
    NAOS_CHECK_INT_EQ(a->list.type, AMARK_LIST_ORDERED);
    NAOS_CHECK_INT_EQ(a->list.marker, ')');
    NAOS_CHECK_UINT_EQ(a->list.start_number, 0);
    NAOS_CHECK_INT_EQ(a->list.spacing, AMARK_LIST_TIGHT);
    a = attr_of(big);
    NAOS_CHECK_INT_EQ(a->list.type, AMARK_LIST_ORDERED);
    NAOS_CHECK_INT_EQ(a->list.marker, '.');
    NAOS_CHECK_UINT_EQ(a->list.start_number, 999999999u);
    NAOS_CHECK_INT_EQ(a->list.spacing, AMARK_LIST_LOOSE);

    /* The spacing decided later: the call is made again and overwrites. */
    NAOS_REQUIRE_INT_EQ(amark_tree_set_list(g_doc, bullet, AMARK_LIST_BULLET,
                                            '*', 0, AMARK_LIST_LOOSE),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(amark_array_len(&g_doc->attrs), 4);
    a = attr_of(bullet);
    NAOS_CHECK_INT_EQ(a->list.marker, '*');
    NAOS_CHECK_INT_EQ(a->list.spacing, AMARK_LIST_LOOSE);
    /* The other lists are unaffected. */
    NAOS_CHECK_UINT_EQ(attr_of(big)->list.start_number, 999999999u);
    NAOS_CHECK_UINT_EQ(attr_of(zero)->list.start_number, 0);
}

NAOS_TEST(code_block_attributes_and_info)
{
    amark_node_id_t indented;
    amark_node_id_t empty_info;
    amark_node_id_t info;
    amark_range_t r;
    const amark_node_attr_t *a;

    NAOS_REQUIRE_INT_EQ(create_mixed(&g_doc), AMARK_ERROR_NONE);
    indented = add(1, AMARK_KIND_CODE_BLOCK, 0, 3);
    empty_info = add(1, AMARK_KIND_CODE_BLOCK, 3, 7);
    info = add(1, AMARK_KIND_CODE_BLOCK, 7, 12);

    /* No info: not written. */
    NAOS_REQUIRE_INT_EQ(amark_tree_set_code_block(g_doc, indented,
                                                  AMARK_CODE_BLOCK_INDENTED,
                                                  0, 0, NULL),
                        AMARK_ERROR_NONE);
    /* start == end: written but empty. */
    r.start = 5;
    r.end = 5;
    NAOS_REQUIRE_INT_EQ(amark_tree_set_code_block(g_doc, empty_info,
                                                  AMARK_CODE_BLOCK_FENCED,
                                                  '`', 3, &r),
                        AMARK_ERROR_NONE);
    r.start = 8;
    r.end = 12;
    NAOS_REQUIRE_INT_EQ(amark_tree_set_code_block(g_doc, info,
                                                  AMARK_CODE_BLOCK_FENCED,
                                                  '~', 5, &r),
                        AMARK_ERROR_NONE);
    /* The range was copied: changing it afterwards changes nothing. */
    r.start = 0;
    r.end = 0;

    a = attr_of(indented);
    NAOS_CHECK_INT_EQ(a->code_block.style, AMARK_CODE_BLOCK_INDENTED);
    NAOS_CHECK_INT_EQ(a->code_block.fence_char, 0);
    NAOS_CHECK_UINT_EQ(a->code_block.fence_length, 0);
    check_opt(&a->code_block.info, false, 0, 0);
    a = attr_of(empty_info);
    NAOS_CHECK_INT_EQ(a->code_block.style, AMARK_CODE_BLOCK_FENCED);
    NAOS_CHECK_INT_EQ(a->code_block.fence_char, '`');
    NAOS_CHECK_UINT_EQ(a->code_block.fence_length, 3);
    check_opt(&a->code_block.info, true, 5, 5);
    a = attr_of(info);
    NAOS_CHECK_INT_EQ(a->code_block.fence_char, '~');
    NAOS_CHECK_UINT_EQ(a->code_block.fence_length, 5);
    check_opt(&a->code_block.info, true, 8, 12);

    /* Overwriting with no info makes the info "not written" again. */
    NAOS_REQUIRE_INT_EQ(amark_tree_set_code_block(g_doc, info,
                                                  AMARK_CODE_BLOCK_FENCED,
                                                  '`', 4, NULL),
                        AMARK_ERROR_NONE);
    a = attr_of(info);
    NAOS_CHECK_INT_EQ(a->code_block.fence_char, '`');
    NAOS_CHECK_UINT_EQ(a->code_block.fence_length, 4);
    check_opt(&a->code_block.info, false, 0, 0);
    NAOS_CHECK_UINT_EQ(amark_array_len(&g_doc->attrs), 4);
}

NAOS_TEST(link_and_image_attributes)
{
    amark_node_id_t para;
    amark_node_id_t def;
    amark_node_id_t l1;
    amark_node_id_t l2;
    amark_node_id_t img;
    amark_node_id_t ref;
    amark_range_t dest;
    amark_range_t title;
    const amark_node_attr_t *a;

    NAOS_REQUIRE_INT_EQ(create_mixed(&g_doc), AMARK_ERROR_NONE);
    def = add(1, AMARK_KIND_LINK_REFERENCE_DEFINITION, 0, 3);
    para = add(1, AMARK_KIND_PARAGRAPH, 3, 12);
    l1 = add(para, AMARK_KIND_LINK, 3, 5);
    l2 = add(para, AMARK_KIND_LINK, 5, 7);
    img = add(para, AMARK_KIND_IMAGE, 7, 10);
    ref = add(para, AMARK_KIND_LINK, 10, 12);

    /* Inline: a destination, no title. */
    dest.start = 4;
    dest.end = 5;
    NAOS_REQUIRE_INT_EQ(amark_tree_set_link(g_doc, l1, AMARK_LINK_INLINE,
                                            &dest, NULL, AMARK_NODE_NONE),
                        AMARK_ERROR_NONE);
    /* Inline: an empty destination and an empty title. */
    dest.start = 6;
    dest.end = 6;
    title.start = 6;
    title.end = 6;
    NAOS_REQUIRE_INT_EQ(amark_tree_set_link(g_doc, l2, AMARK_LINK_INLINE,
                                            &dest, &title, AMARK_NODE_NONE),
                        AMARK_ERROR_NONE);
    /* An image, inline: a destination and a title. */
    dest.start = 7;
    dest.end = 8;
    title.start = 8;
    title.end = 10;
    NAOS_REQUIRE_INT_EQ(amark_tree_set_link(g_doc, img, AMARK_LINK_INLINE,
                                            &dest, &title, AMARK_NODE_NONE),
                        AMARK_ERROR_NONE);
    /* A reference: the definition, no ranges. */
    NAOS_REQUIRE_INT_EQ(amark_tree_set_link(g_doc, ref, AMARK_LINK_FULL, NULL,
                                            NULL, def),
                        AMARK_ERROR_NONE);

    a = attr_of(l1);
    NAOS_CHECK_INT_EQ(a->link.form, AMARK_LINK_INLINE);
    check_opt(&a->link.destination, true, 4, 5);
    check_opt(&a->link.title, false, 0, 0);
    NAOS_CHECK_UINT_EQ(a->link.definition, AMARK_NODE_NONE);
    a = attr_of(l2);
    check_opt(&a->link.destination, true, 6, 6);
    check_opt(&a->link.title, true, 6, 6);
    a = attr_of(img);
    NAOS_CHECK_INT_EQ(a->link.form, AMARK_LINK_INLINE);
    check_opt(&a->link.destination, true, 7, 8);
    check_opt(&a->link.title, true, 8, 10);
    a = attr_of(ref);
    NAOS_CHECK_INT_EQ(a->link.form, AMARK_LINK_FULL);
    check_opt(&a->link.destination, false, 0, 0);
    check_opt(&a->link.title, false, 0, 0);
    NAOS_CHECK_UINT_EQ(a->link.definition, def);

    /* Each reference form; the last call overwrites the element. */
    NAOS_REQUIRE_INT_EQ(amark_tree_set_link(g_doc, ref, AMARK_LINK_COLLAPSED,
                                            NULL, NULL, def),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_INT_EQ(attr_of(ref)->link.form, AMARK_LINK_COLLAPSED);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_link(g_doc, ref, AMARK_LINK_SHORTCUT,
                                            NULL, NULL, def),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_INT_EQ(attr_of(ref)->link.form, AMARK_LINK_SHORTCUT);
    NAOS_CHECK_UINT_EQ(attr_of(ref)->link.definition, def);

    /* Inline to reference: the old ranges do not survive. */
    NAOS_REQUIRE_INT_EQ(amark_tree_set_link(g_doc, img, AMARK_LINK_SHORTCUT,
                                            NULL, NULL, def),
                        AMARK_ERROR_NONE);
    a = attr_of(img);
    NAOS_CHECK_INT_EQ(a->link.form, AMARK_LINK_SHORTCUT);
    check_opt(&a->link.destination, false, 0, 0);
    check_opt(&a->link.title, false, 0, 0);
    NAOS_CHECK_UINT_EQ(a->link.definition, def);

    /* One element per link; the paragraph and definition have none. */
    NAOS_CHECK_UINT_EQ(amark_array_len(&g_doc->attrs), 5);
    NAOS_CHECK_UINT_EQ(node_at(g_doc, para)->attr, 0);
    NAOS_CHECK_UINT_EQ(node_at(g_doc, def)->attr, 0);
}

NAOS_TEST(definition_attributes)
{
    amark_node_id_t none;
    amark_node_id_t empty;
    amark_node_id_t full;
    amark_range_t label;
    amark_range_t dest;
    amark_range_t title;
    const amark_node_attr_t *a;

    NAOS_REQUIRE_INT_EQ(create_mixed(&g_doc), AMARK_ERROR_NONE);
    none = add(1, AMARK_KIND_LINK_REFERENCE_DEFINITION, 0, 3);
    empty = add(1, AMARK_KIND_LINK_REFERENCE_DEFINITION, 3, 7);
    full = add(1, AMARK_KIND_LINK_REFERENCE_DEFINITION, 7, 12);

    label.start = 0;
    label.end = 1;
    dest.start = 1;
    dest.end = 2;
    NAOS_REQUIRE_INT_EQ(amark_tree_set_definition(g_doc, none, &label, &dest,
                                                  NULL),
                        AMARK_ERROR_NONE);
    label.start = 3;
    label.end = 4;
    dest.start = 5;
    dest.end = 5;
    title.start = 6;
    title.end = 6;
    NAOS_REQUIRE_INT_EQ(amark_tree_set_definition(g_doc, empty, &label, &dest,
                                                  &title),
                        AMARK_ERROR_NONE);
    label.start = 7;
    label.end = 8;
    dest.start = 8;
    dest.end = 10;
    title.start = 10;
    title.end = 12;
    NAOS_REQUIRE_INT_EQ(amark_tree_set_definition(g_doc, full, &label, &dest,
                                                  &title),
                        AMARK_ERROR_NONE);

    a = attr_of(none);
    check_opt(&a->definition.label, true, 0, 1);
    check_opt(&a->definition.destination, true, 1, 2);
    check_opt(&a->definition.title, false, 0, 0);
    a = attr_of(empty);
    check_opt(&a->definition.label, true, 3, 4);
    check_opt(&a->definition.destination, true, 5, 5);
    check_opt(&a->definition.title, true, 6, 6);
    a = attr_of(full);
    check_opt(&a->definition.label, true, 7, 8);
    check_opt(&a->definition.destination, true, 8, 10);
    check_opt(&a->definition.title, true, 10, 12);

    /* Overwrite: the title becomes "not written". */
    NAOS_REQUIRE_INT_EQ(amark_tree_set_definition(g_doc, full, &label, &dest,
                                                  NULL),
                        AMARK_ERROR_NONE);
    a = attr_of(full);
    check_opt(&a->definition.label, true, 7, 8);
    check_opt(&a->definition.title, false, 0, 0);
    NAOS_CHECK_UINT_EQ(amark_array_len(&g_doc->attrs), 4);
}

NAOS_TEST(autolink_attributes)
{
    amark_node_id_t para;
    amark_node_id_t uri;
    amark_node_id_t email;

    NAOS_REQUIRE_INT_EQ(create_mixed(&g_doc), AMARK_ERROR_NONE);
    para = add(1, AMARK_KIND_PARAGRAPH, 0, 12);
    uri = add(para, AMARK_KIND_AUTOLINK, 0, 2);
    email = add(para, AMARK_KIND_AUTOLINK, 3, 5);

    NAOS_REQUIRE_INT_EQ(amark_tree_set_autolink(g_doc, uri,
                                                AMARK_AUTOLINK_URI),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_autolink(g_doc, email,
                                                AMARK_AUTOLINK_EMAIL),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_INT_EQ(attr_of(uri)->autolink.type, AMARK_AUTOLINK_URI);
    NAOS_CHECK_INT_EQ(attr_of(email)->autolink.type, AMARK_AUTOLINK_EMAIL);

    NAOS_REQUIRE_INT_EQ(amark_tree_set_autolink(g_doc, uri,
                                                AMARK_AUTOLINK_EMAIL),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_INT_EQ(attr_of(uri)->autolink.type, AMARK_AUTOLINK_EMAIL);
    NAOS_CHECK_UINT_EQ(amark_array_len(&g_doc->attrs), 3);
}

NAOS_TEST(dialect_data_is_copied_and_aligned)
{
    amark_node_id_t block;
    amark_node_id_t span;
    amark_node_id_t empty;
    unsigned char bytes[13];
    unsigned char other[3] = { 7, 8, 9 };
    const amark_node_attr_t *a;
    const unsigned char *copy;
    size_t i;

    NAOS_REQUIRE_INT_EQ(create_mixed(&g_doc), AMARK_ERROR_NONE);
    block = add_class(1, AMARK_KIND_DIALECT_MIN, AMARK_NODE_CLASS_BLOCK, 0, 12);
    span = add_class(block, AMARK_KIND_DIALECT_MIN + 1,
                     AMARK_NODE_CLASS_INLINE, 0, 2);
    empty = add_class(block, AMARK_KIND_DIALECT_MIN + 2,
                      AMARK_NODE_CLASS_INLINE, 3, 5);

    for (i = 0; i < sizeof(bytes); i++) {
        bytes[i] = (unsigned char)(0xA0 + i);
    }
    NAOS_REQUIRE_INT_EQ(amark_tree_set_dialect_data(g_doc, block, bytes,
                                                    sizeof(bytes)),
                        AMARK_ERROR_NONE);
    /* An odd size first, so the next copy shows the alignment is kept. */
    NAOS_REQUIRE_INT_EQ(amark_tree_set_dialect_data(g_doc, span, bytes, 1),
                        AMARK_ERROR_NONE);
    /* Size 0: "none", and no element is added. */
    NAOS_REQUIRE_INT_EQ(amark_tree_set_dialect_data(g_doc, empty, NULL, 0),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(node_at(g_doc, empty)->attr, 0);
    NAOS_CHECK_UINT_EQ(amark_array_len(&g_doc->attrs), 3);

    /* Change the caller's array: the copies stay as they were. */
    memset(bytes, 0, sizeof(bytes));

    a = attr_of(block);
    NAOS_CHECK_UINT_EQ(a->dialect.size, sizeof(bytes));
    NAOS_REQUIRE_TRUE(a->dialect.data != NULL);
    NAOS_CHECK_TRUE(a->dialect.data != (const void *)bytes);
    NAOS_CHECK_UINT_EQ((uintptr_t)a->dialect.data % AMARK_ARENA_ALIGN, 0);
    copy = (const unsigned char *)a->dialect.data;
    for (i = 0; i < sizeof(bytes); i++) {
        NAOS_CHECK_UINT_EQ(copy[i], 0xA0 + i);
    }
    a = attr_of(span);
    NAOS_CHECK_UINT_EQ(a->dialect.size, 1);
    NAOS_REQUIRE_TRUE(a->dialect.data != NULL);
    NAOS_CHECK_UINT_EQ((uintptr_t)a->dialect.data % AMARK_ARENA_ALIGN, 0);
    NAOS_CHECK_UINT_EQ(((const unsigned char *)a->dialect.data)[0], 0xA0);

    /* Overwrite: the same element, a new copy of the new bytes. */
    NAOS_REQUIRE_INT_EQ(amark_tree_set_dialect_data(g_doc, block, other,
                                                    sizeof(other)),
                        AMARK_ERROR_NONE);
    other[0] = 0;
    NAOS_CHECK_UINT_EQ(amark_array_len(&g_doc->attrs), 3);
    a = attr_of(block);
    NAOS_CHECK_UINT_EQ(a->dialect.size, 3);
    NAOS_REQUIRE_TRUE(a->dialect.data != NULL);
    NAOS_CHECK_UINT_EQ((uintptr_t)a->dialect.data % AMARK_ARENA_ALIGN, 0);
    copy = (const unsigned char *)a->dialect.data;
    NAOS_CHECK_UINT_EQ(copy[0], 7);
    NAOS_CHECK_UINT_EQ(copy[1], 8);
    NAOS_CHECK_UINT_EQ(copy[2], 9);

    /* Size 0 on a node with data makes its attribute "none". */
    NAOS_REQUIRE_INT_EQ(amark_tree_set_dialect_data(g_doc, span, other, 0),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(node_at(g_doc, span)->attr, 0);
    NAOS_CHECK_UINT_EQ(node_at(g_doc, span)->is_inline, 1);
    NAOS_CHECK_UINT_EQ(node_at(g_doc, block)->is_inline, 0);
}

NAOS_TEST(elements_only_for_nodes_with_attributes)
{
    amark_node_id_t ids[8];
    size_t i;

    NAOS_REQUIRE_INT_EQ(create_mixed(&g_doc), AMARK_ERROR_NONE);
    for (i = 0; i < 8; i++) {
        ids[i] = add(1, i % 2 == 0 ? AMARK_KIND_HEADING : AMARK_KIND_PARAGRAPH,
                     i, i + 1);
    }
    /* Only the headings at 2 and 6 get attributes. */
    NAOS_REQUIRE_INT_EQ(amark_tree_set_heading(g_doc, ids[2], 2,
                                               AMARK_HEADING_ATX),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_heading(g_doc, ids[6], 4,
                                               AMARK_HEADING_ATX),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_heading(g_doc, ids[2], 5,
                                               AMARK_HEADING_SETEXT),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(amark_array_len(&g_doc->attrs), 3);
    for (i = 0; i < 8; i++) {
        uint32_t expected = i == 2 ? 1u : i == 6 ? 2u : 0u;

        NAOS_CHECK_UINT_EQ(node_at(g_doc, ids[i])->attr, expected);
    }
    NAOS_CHECK_UINT_EQ(node_at(g_doc, 1)->attr, 0);
    NAOS_CHECK_UINT_EQ(attr_of(ids[2])->heading.level, 5);
    NAOS_CHECK_UINT_EQ(attr_of(ids[6])->heading.level, 4);

    /* After set_kind the node has none; setting again adds an element. */
    amark_tree_set_kind(g_doc, ids[2], AMARK_KIND_HEADING,
                        AMARK_NODE_CLASS_AUTO);
    NAOS_CHECK_UINT_EQ(node_at(g_doc, ids[2])->attr, 0);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_heading(g_doc, ids[2], 1,
                                               AMARK_HEADING_ATX),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(node_at(g_doc, ids[2])->attr, 3);
    NAOS_CHECK_UINT_EQ(attr_of(ids[2])->heading.level, 1);
}

/* The seven setters, chosen by which (0 to 6), on a node of the kind. */
#define SETTER_COUNT 7
static const amark_kind_t k_setter_kinds[SETTER_COUNT] = {
    AMARK_KIND_HEADING,
    AMARK_KIND_LIST,
    AMARK_KIND_CODE_BLOCK,
    AMARK_KIND_LINK,
    AMARK_KIND_LINK_REFERENCE_DEFINITION,
    AMARK_KIND_AUTOLINK,
    AMARK_KIND_DIALECT_MIN
};

static const unsigned char k_dialect_bytes[24] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
    13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24
};

static amark_error_t set_by_index(amark_node_id_t id, size_t which, size_t i)
{
    amark_range_t r;

    r.start = 1;
    r.end = 2;
    switch (which) {
    case 0:
        return amark_tree_set_heading(g_doc, id, (unsigned)(i % 6 + 1),
                                      AMARK_HEADING_ATX);
    case 1:
        return amark_tree_set_list(g_doc, id, AMARK_LIST_ORDERED, '.',
                                   (uint32_t)i, AMARK_LIST_TIGHT);
    case 2:
        return amark_tree_set_code_block(g_doc, id, AMARK_CODE_BLOCK_FENCED,
                                         '`', i, &r);
    case 3:
        return amark_tree_set_link(g_doc, id, AMARK_LINK_INLINE, &r, &r,
                                   AMARK_NODE_NONE);
    case 4:
        return amark_tree_set_definition(g_doc, id, &r, &r, &r);
    case 5:
        return amark_tree_set_autolink(g_doc, id, AMARK_AUTOLINK_URI);
    default:
        return amark_tree_set_dialect_data(g_doc, id, k_dialect_bytes,
                                           i % sizeof(k_dialect_bytes) + 1);
    }
}

#define ATTR_NODES 300

/*
 * Adds ATTR_NODES nodes to the root (the kinds of the setters in turn)
 * without failure injection; node i has ID i + 2.
 */
static void add_attr_nodes(void)
{
    size_t i;

    for (i = 0; i < ATTR_NODES; i++) {
        size_t which = i % SETTER_COUNT;

        (void)add_class(1, k_setter_kinds[which],
                        which == SETTER_COUNT - 1 ? AMARK_NODE_CLASS_BLOCK
                                                  : AMARK_NODE_CLASS_AUTO,
                        0, MIXED_LENGTH);
    }
}

/*
 * Sets the attributes of the nodes from add_attr_nodes in order. Stops at
 * the first failure, checks that the failed call changed neither the node
 * nor the attribute array and returns its error; *done receives the number
 * of nodes whose attributes were set.
 */
static amark_error_t set_attr_nodes(size_t *done)
{
    size_t i;

    *done = 0;
    for (i = 0; i < ATTR_NODES; i++) {
        amark_node_id_t id = (amark_node_id_t)(i + 2);
        amark_node_t before = *node_at(g_doc, id);
        size_t attrs_len = amark_array_len(&g_doc->attrs);
        void *attrs_data = g_doc->attrs.data;
        amark_error_t err = set_by_index(id, i % SETTER_COUNT, i);

        if (err != AMARK_ERROR_NONE) {
            NAOS_CHECK_TRUE(memcmp(node_at(g_doc, id), &before,
                                   sizeof(before)) == 0);
            NAOS_CHECK_UINT_EQ(amark_array_len(&g_doc->attrs), attrs_len);
            NAOS_CHECK_PTR_EQ(g_doc->attrs.data, attrs_data);
            return err;
        }
        NAOS_CHECK_UINT_EQ(node_at(g_doc, id)->attr, (uint32_t)(i + 1));
        (*done)++;
    }
    return AMARK_ERROR_NONE;
}

NAOS_TEST(setter_failure_injection_loop)
{
    size_t fail_at;
    size_t set_failures = 0;
    bool completed = false;

    for (fail_at = 1; fail_at < 1000; fail_at++) {
        amark_error_t err;
        size_t done = 0;
        size_t target;
        size_t i;

        amark_test_alloc_init(&g_state);
        NAOS_REQUIRE_INT_EQ(create_mixed(&g_doc), AMARK_ERROR_NONE);
        add_attr_nodes();
        target = g_state.malloc_calls + fail_at;
        g_state.fail_at = target;

        err = set_attr_nodes(&done);
        g_state.fail_at = 0;
        if (err == AMARK_ERROR_NONE) {
            NAOS_CHECK_UINT_EQ(done, ATTR_NODES);
            completed = true;
        } else {
            NAOS_CHECK_INT_EQ(err, AMARK_ERROR_NOMEM);
            /* The failed call made the injected allocation the last one. */
            NAOS_CHECK_UINT_EQ(g_state.malloc_calls, target);
            set_failures++;
        }
        /* The nodes set keep their elements; the others have none. */
        NAOS_CHECK_UINT_EQ(amark_array_len(&g_doc->attrs), done + 1);
        for (i = 0; i < ATTR_NODES; i++) {
            NAOS_CHECK_UINT_EQ(node_at(g_doc, (amark_node_id_t)(i + 2))->attr,
                               i < done ? (uint32_t)(i + 1) : 0u);
        }
        /* A failed call can be made again and succeeds. */
        if (err != AMARK_ERROR_NONE) {
            NAOS_CHECK_INT_EQ(set_by_index((amark_node_id_t)(done + 2),
                                           done % SETTER_COUNT, done),
                              AMARK_ERROR_NONE);
            NAOS_CHECK_UINT_EQ(
                node_at(g_doc, (amark_node_id_t)(done + 2))->attr,
                (uint32_t)(done + 1));
        }

        amark_document_destroy(g_doc);
        g_doc = NULL;
        NAOS_CHECK_UINT_EQ(g_state.live, 0);
        NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
        if (completed) {
            break;
        }
    }
    NAOS_CHECK_TRUE(completed);
    /* 300 attribute elements and the dialect copies outgrow the arena. */
    NAOS_CHECK_UINT_GE(set_failures, 1);
}

NAOS_TEST(attribute_limit_leaves_the_node_unchanged)
{
    amark_node_id_t h;
    amark_node_id_t d;
    amark_node_t before;
    size_t saved;
    size_t mallocs;

    NAOS_REQUIRE_INT_EQ(create_mixed(&g_doc), AMARK_ERROR_NONE);
    h = add(1, AMARK_KIND_HEADING, 0, 3);
    d = add_class(1, AMARK_KIND_DIALECT_MIN, AMARK_NODE_CLASS_BLOCK, 3, 7);

    /* Pretend that the attribute array has UINT32_MAX + 1 elements. */
    saved = g_doc->attrs.len;
    g_saved_attrs_len = saved;
    g_doc->attrs.len = (size_t)UINT32_MAX + 1;
    mallocs = g_state.malloc_calls;

    before = *node_at(g_doc, h);
    NAOS_CHECK_INT_EQ(amark_tree_set_heading(g_doc, h, 1, AMARK_HEADING_ATX),
                      AMARK_ERROR_LIMIT_EXCEEDED);
    NAOS_CHECK_TRUE(memcmp(node_at(g_doc, h), &before, sizeof(before)) == 0);
    before = *node_at(g_doc, d);
    NAOS_CHECK_INT_EQ(amark_tree_set_dialect_data(g_doc, d, k_dialect_bytes,
                                                  4),
                      AMARK_ERROR_LIMIT_EXCEEDED);
    NAOS_CHECK_TRUE(memcmp(node_at(g_doc, d), &before, sizeof(before)) == 0);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, mallocs);
    NAOS_CHECK_UINT_EQ(g_doc->attrs.len, (size_t)UINT32_MAX + 1);

    /* With the real length, the element is added at the next index. */
    g_doc->attrs.len = saved;
    g_saved_attrs_len = 0;
    NAOS_CHECK_INT_EQ(amark_tree_set_heading(g_doc, h, 2, AMARK_HEADING_ATX),
                      AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(node_at(g_doc, h)->attr, (uint32_t)saved);
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
    NAOS_ADD_TEST(wrap_failure_injection_loop),
    NAOS_ADD_TEST(heading_attributes_and_overwrite),
    NAOS_ADD_TEST(list_attributes_and_start_numbers),
    NAOS_ADD_TEST(code_block_attributes_and_info),
    NAOS_ADD_TEST(link_and_image_attributes),
    NAOS_ADD_TEST(definition_attributes),
    NAOS_ADD_TEST(autolink_attributes),
    NAOS_ADD_TEST(dialect_data_is_copied_and_aligned),
    NAOS_ADD_TEST(elements_only_for_nodes_with_attributes),
    NAOS_ADD_TEST(setter_failure_injection_loop),
    NAOS_ADD_TEST(attribute_limit_leaves_the_node_unchanged)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(tree)
)
