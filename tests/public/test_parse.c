/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Public API test of the parse: links Amark::amark and uses only
 * <amark/amark.h>. Parses a short document with no configuration, visits
 * its tree with the traversal of the comment in <amark/tree.h>, reads the
 * kind, the range, the lines and the parent of each node and some
 * attributes, and destroys the document. Also checks that a failed parse
 * stores NULL in *out. In the shared configuration this links against the
 * exported symbols only, so it shows that amark_parse is exported.
 */

#include <amark/amark.h>

#include <naos/naos.h>

#include <stddef.h>
#include <stdint.h>

/*
 * Offsets:   0 "# Title"  8 ""  9 "- one"  15 "- two"  21 ""
 *           22 "3) first"  31 "> quote" (no line ending), 38 bytes.
 */
static const char k_content[] =
    "# Title\n"
    "\n"
    "- one\n"
    "- two\n"
    "\n"
    "3) first\n"
    "> quote";

/* A node as the traversal meets it, in document order. */
typedef struct expected_node {
    amark_kind_t kind;
    size_t depth; /* the root is at depth 0 */
    size_t start;
    size_t end;
    size_t start_line;
    size_t end_line;
} expected_node_t;

static const expected_node_t k_nodes[] = {
    {AMARK_KIND_DOCUMENT, 0, 0, 38, 1, 7},
    {AMARK_KIND_HEADING, 1, 0, 7, 1, 1},
    {AMARK_KIND_LIST, 1, 9, 20, 3, 4},
    {AMARK_KIND_LIST_ITEM, 2, 9, 14, 3, 3},
    {AMARK_KIND_PARAGRAPH, 3, 11, 14, 3, 3},
    {AMARK_KIND_LIST_ITEM, 2, 15, 20, 4, 4},
    {AMARK_KIND_PARAGRAPH, 3, 17, 20, 4, 4},
    {AMARK_KIND_LIST, 1, 22, 30, 6, 6},
    {AMARK_KIND_LIST_ITEM, 2, 22, 30, 6, 6},
    {AMARK_KIND_PARAGRAPH, 3, 25, 30, 6, 6},
    {AMARK_KIND_BLOCK_QUOTE, 1, 31, 38, 7, 7},
    {AMARK_KIND_PARAGRAPH, 2, 33, 38, 7, 7}};

#define NODE_COUNT (sizeof(k_nodes) / sizeof(k_nodes[0]))

/*
 * NAOS_REQUIRE_* leaves a test with longjmp, so the document is kept in a
 * static variable and teardown destroys it.
 */
static amark_document_t *g_doc;

/* A value of *out that no parse leaves there. */
static int g_sentinel_target;
#define SENTINEL ((amark_document_t *)(void *)&g_sentinel_target)

NAOS_SETUP(document)
{
    g_doc = NULL;
}

NAOS_TEARDOWN(document)
{
    if (g_doc != SENTINEL) {
        amark_document_destroy(g_doc);
    }
    g_doc = NULL;
}

/* Parses k_content with no configuration into g_doc. */
static void parse_content(void)
{
    NAOS_REQUIRE_INT_EQ(amark_parse(k_content, sizeof(k_content) - 1, NULL,
                                    &g_doc),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_NOT_NULL(g_doc);
}

/* Requires node to be the nth node of k_nodes, at the given depth. */
static void check_node(amark_node_id_t node, size_t nth, size_t depth)
{
    const expected_node_t *want = &k_nodes[nth];
    amark_range_t range = {0, 0};

    NAOS_TRACE_PUSH("node %lu", (unsigned long)nth);
    NAOS_CHECK_UINT_EQ(amark_node_kind(g_doc, node), want->kind);
    NAOS_CHECK_TRUE(amark_node_is_block(g_doc, node));
    NAOS_CHECK_FALSE(amark_node_is_inline(g_doc, node));
    NAOS_CHECK_UINT_EQ(depth, want->depth);
    NAOS_CHECK_TRUE(amark_node_range(g_doc, node, &range));
    NAOS_CHECK_UINT_EQ(range.start, want->start);
    NAOS_CHECK_UINT_EQ(range.end, want->end);
    NAOS_CHECK_UINT_EQ(amark_node_start_line(g_doc, node), want->start_line);
    NAOS_CHECK_UINT_EQ(amark_node_end_line(g_doc, node), want->end_line);
    NAOS_TRACE_POP();
}

/*
 * Returns the nth node of k_nodes, visiting the tree in document order with
 * the traversal of <amark/tree.h>; it must exist.
 */
static amark_node_id_t find_node(size_t nth)
{
    amark_node_id_t root = amark_document_root(g_doc);
    amark_node_id_t node = root;
    size_t visits = 0;

    while (node != AMARK_NODE_NONE && visits < nth) {
        amark_node_id_t next = amark_node_first_child(g_doc, node);

        visits++;
        while (next == AMARK_NODE_NONE && node != root) {
            next = amark_node_next_sibling(g_doc, node);
            if (next == AMARK_NODE_NONE) {
                node = amark_node_parent(g_doc, node);
            }
        }
        node = next;
    }
    NAOS_REQUIRE_UINT_NE(node, AMARK_NODE_NONE);
    return node;
}

NAOS_TEST(null_config_parses_the_whole_content)
{
    amark_node_id_t root;

    parse_content();
    root = amark_document_root(g_doc);
    NAOS_REQUIRE_UINT_NE(root, AMARK_NODE_NONE);
    NAOS_CHECK_UINT_EQ(amark_node_parent(g_doc, root), AMARK_NODE_NONE);
    NAOS_CHECK_UINT_EQ(amark_node_next_sibling(g_doc, root), AMARK_NODE_NONE);
    check_node(root, 0, 0);
    NAOS_CHECK_UINT_EQ(amark_document_diagnostic_count(g_doc), 0);
}

/*
 * Visits every node in document order, keeping its depth from the moves of
 * the traversal, and compares each with k_nodes.
 */
NAOS_TEST(traversal_reads_kinds_and_ranges)
{
    amark_node_id_t root;
    amark_node_id_t node;
    size_t depth = 0;
    size_t visits = 0;

    parse_content();
    root = amark_document_root(g_doc);
    node = root;
    while (node != AMARK_NODE_NONE) {
        amark_node_id_t next;

        NAOS_REQUIRE_UINT_LT(visits, NODE_COUNT);
        check_node(node, visits, depth);
        visits++;
        next = amark_node_first_child(g_doc, node);
        if (next != AMARK_NODE_NONE) {
            NAOS_CHECK_UINT_EQ(amark_node_parent(g_doc, next), node);
            depth++;
        }
        while (next == AMARK_NODE_NONE && node != root) {
            next = amark_node_next_sibling(g_doc, node);
            if (next == AMARK_NODE_NONE) {
                node = amark_node_parent(g_doc, node);
                depth--;
            }
        }
        node = next;
    }
    NAOS_CHECK_UINT_EQ(visits, NODE_COUNT);
}

NAOS_TEST(heading_attributes)
{
    amark_node_id_t heading;

    parse_content();
    heading = find_node(1);
    NAOS_REQUIRE_UINT_EQ(amark_node_kind(g_doc, heading), AMARK_KIND_HEADING);
    NAOS_CHECK_UINT_EQ(amark_heading_level(g_doc, heading), 1);
    NAOS_CHECK_INT_EQ(amark_heading_style(g_doc, heading), AMARK_HEADING_ATX);
    /* A heading has no list attributes. */
    NAOS_CHECK_INT_EQ(amark_list_type(g_doc, heading), AMARK_LIST_TYPE_NONE);
}

NAOS_TEST(list_attributes)
{
    amark_node_id_t bullet;
    amark_node_id_t ordered;
    uint32_t start = 0;

    parse_content();
    bullet = find_node(2);
    NAOS_REQUIRE_UINT_EQ(amark_node_kind(g_doc, bullet), AMARK_KIND_LIST);
    NAOS_CHECK_INT_EQ(amark_list_type(g_doc, bullet), AMARK_LIST_BULLET);
    NAOS_CHECK_INT_EQ(amark_list_spacing(g_doc, bullet), AMARK_LIST_TIGHT);
    NAOS_CHECK_INT_EQ(amark_list_bullet_char(g_doc, bullet), '-');
    NAOS_CHECK_FALSE(amark_list_start(g_doc, bullet, &start));

    ordered = find_node(7);
    NAOS_REQUIRE_UINT_EQ(amark_node_kind(g_doc, ordered), AMARK_KIND_LIST);
    NAOS_CHECK_INT_EQ(amark_list_type(g_doc, ordered), AMARK_LIST_ORDERED);
    NAOS_CHECK_INT_EQ(amark_list_spacing(g_doc, ordered), AMARK_LIST_TIGHT);
    NAOS_CHECK_INT_EQ(amark_list_delimiter(g_doc, ordered), ')');
    NAOS_CHECK_TRUE(amark_list_start(g_doc, ordered, &start));
    NAOS_CHECK_UINT_EQ(start, 3);
    /* A heading level is answered for a heading only. */
    NAOS_CHECK_UINT_EQ(amark_heading_level(g_doc, ordered), 0);
}

/* ---------------------------------------------------------------------- */
/* Errors                                                                 */
/* ---------------------------------------------------------------------- */

/* An allocator that always fails, and a free function never called. */
static void *failing_malloc(void *ctx, size_t size)
{
    (void)ctx;
    (void)size;
    return NULL;
}

static void unused_free(void *ctx, void *ptr)
{
    (void)ctx;
    (void)ptr;
}

NAOS_TEST(null_content_with_a_length_is_invalid_input)
{
    g_doc = SENTINEL;
    NAOS_CHECK_INT_EQ(amark_parse(NULL, 1, NULL, &g_doc),
                      AMARK_ERROR_INVALID_INPUT);
    NAOS_CHECK_NULL(g_doc);
}

NAOS_TEST(null_out_is_invalid_input)
{
    NAOS_CHECK_INT_EQ(amark_parse(k_content, sizeof(k_content) - 1, NULL,
                                  NULL),
                      AMARK_ERROR_INVALID_INPUT);
}

NAOS_TEST(failing_allocator_is_nomem)
{
    amark_config_t config;

    amark_config_init(&config);
    config.allocator.malloc_fn = failing_malloc;
    config.allocator.free_fn = unused_free;
    g_doc = SENTINEL;
    NAOS_CHECK_INT_EQ(amark_parse(k_content, sizeof(k_content) - 1, &config,
                                  &g_doc),
                      AMARK_ERROR_NOMEM);
    NAOS_CHECK_NULL(g_doc);
}

NAOS_SUITE(parse,
    NAOS_SET_SETUP(document),
    NAOS_SET_TEARDOWN(document),
    NAOS_ADD_TEST(null_config_parses_the_whole_content),
    NAOS_ADD_TEST(traversal_reads_kinds_and_ranges),
    NAOS_ADD_TEST(heading_attributes),
    NAOS_ADD_TEST(list_attributes)
)

NAOS_SUITE(errors,
    NAOS_SET_SETUP(document),
    NAOS_SET_TEARDOWN(document),
    NAOS_ADD_TEST(null_content_with_a_length_is_invalid_input),
    NAOS_ADD_TEST(null_out_is_invalid_input),
    NAOS_ADD_TEST(failing_allocator_is_nomem)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(parse),
    NAOS_ADD_SUITE(errors)
)
