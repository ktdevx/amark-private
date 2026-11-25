/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Public API test of the parse: links Amark::amark and uses only
 * <amark/amark.h>. Parses a short document with no configuration, visits
 * its tree with the traversal of the comment in <amark/tree.h>, reads the
 * kind, the range, the lines and the parent of each node (the blocks and
 * the inline nodes of the paragraphs and the heading) and some attributes,
 * and destroys the document. A second document holds every inline kind
 * and links of the four forms: the kinds and ranges of its inline nodes,
 * the form, destination, title and definition of its links and images and
 * the type of its autolinks are read the same way. Also checks that a
 * failed parse stores NULL in *out. In the shared configuration this links
 * against the exported symbols only, so it shows that amark_parse is
 * exported.
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
    int is_block; /* 1 for a block, 0 for an inline node */
    size_t depth; /* the root is at depth 0 */
    size_t start;
    size_t end;
    size_t start_line;
    size_t end_line;
} expected_node_t;

static const expected_node_t k_nodes[] = {
    {AMARK_KIND_DOCUMENT, 1, 0, 0, 38, 1, 7},
    {AMARK_KIND_HEADING, 1, 1, 0, 7, 1, 1},
    {AMARK_KIND_TEXT, 0, 2, 2, 7, 1, 1},
    {AMARK_KIND_LIST, 1, 1, 9, 20, 3, 4},
    {AMARK_KIND_LIST_ITEM, 1, 2, 9, 14, 3, 3},
    {AMARK_KIND_PARAGRAPH, 1, 3, 11, 14, 3, 3},
    {AMARK_KIND_TEXT, 0, 4, 11, 14, 3, 3},
    {AMARK_KIND_LIST_ITEM, 1, 2, 15, 20, 4, 4},
    {AMARK_KIND_PARAGRAPH, 1, 3, 17, 20, 4, 4},
    {AMARK_KIND_TEXT, 0, 4, 17, 20, 4, 4},
    {AMARK_KIND_LIST, 1, 1, 22, 30, 6, 6},
    {AMARK_KIND_LIST_ITEM, 1, 2, 22, 30, 6, 6},
    {AMARK_KIND_PARAGRAPH, 1, 3, 25, 30, 6, 6},
    {AMARK_KIND_TEXT, 0, 4, 25, 30, 6, 6},
    {AMARK_KIND_BLOCK_QUOTE, 1, 1, 31, 38, 7, 7},
    {AMARK_KIND_PARAGRAPH, 1, 2, 33, 38, 7, 7},
    {AMARK_KIND_TEXT, 0, 3, 33, 38, 7, 7}};

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

/* Parses length bytes at content with no configuration into g_doc. */
static void parse_bytes(const char *content, size_t length)
{
    NAOS_REQUIRE_INT_EQ(amark_parse(content, length, NULL, &g_doc),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_NOT_NULL(g_doc);
}

/* Parses k_content with no configuration into g_doc. */
static void parse_content(void)
{
    parse_bytes(k_content, sizeof(k_content) - 1);
}

/* Requires node to be the nth node of nodes, at the given depth. */
static void check_node_of(const expected_node_t *nodes,
                          amark_node_id_t node, size_t nth, size_t depth)
{
    const expected_node_t *want = &nodes[nth];
    amark_range_t range = {0, 0};

    NAOS_TRACE_PUSH("node %lu", (unsigned long)nth);
    NAOS_CHECK_UINT_EQ(amark_node_kind(g_doc, node), want->kind);
    NAOS_CHECK_INT_EQ(amark_node_is_block(g_doc, node) ? 1 : 0,
                      want->is_block);
    NAOS_CHECK_INT_EQ(amark_node_is_inline(g_doc, node) ? 1 : 0,
                      !want->is_block);
    NAOS_CHECK_UINT_EQ(depth, want->depth);
    NAOS_CHECK_TRUE(amark_node_range(g_doc, node, &range));
    NAOS_CHECK_UINT_EQ(range.start, want->start);
    NAOS_CHECK_UINT_EQ(range.end, want->end);
    NAOS_CHECK_UINT_EQ(amark_node_start_line(g_doc, node), want->start_line);
    NAOS_CHECK_UINT_EQ(amark_node_end_line(g_doc, node), want->end_line);
    NAOS_TRACE_POP();
}

/* Requires node to be the nth node of k_nodes, at the given depth. */
static void check_node(amark_node_id_t node, size_t nth, size_t depth)
{
    check_node_of(k_nodes, node, nth, depth);
}

/*
 * Returns the nth node of g_doc in document order, visiting the tree with
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
 * Visits every node of g_doc in document order, keeping its depth from the
 * moves of the traversal, and compares each with the count nodes of nodes.
 */
static void check_traversal(const expected_node_t *nodes, size_t count)
{
    amark_node_id_t root;
    amark_node_id_t node;
    size_t depth = 0;
    size_t visits = 0;

    root = amark_document_root(g_doc);
    node = root;
    while (node != AMARK_NODE_NONE) {
        amark_node_id_t next;

        NAOS_REQUIRE_UINT_LT(visits, count);
        check_node_of(nodes, node, visits, depth);
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
    NAOS_CHECK_UINT_EQ(visits, count);
}

NAOS_TEST(traversal_reads_kinds_and_ranges)
{
    parse_content();
    check_traversal(k_nodes, NODE_COUNT);
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
    bullet = find_node(3);
    NAOS_REQUIRE_UINT_EQ(amark_node_kind(g_doc, bullet), AMARK_KIND_LIST);
    NAOS_CHECK_INT_EQ(amark_list_type(g_doc, bullet), AMARK_LIST_BULLET);
    NAOS_CHECK_INT_EQ(amark_list_spacing(g_doc, bullet), AMARK_LIST_TIGHT);
    NAOS_CHECK_INT_EQ(amark_list_bullet_char(g_doc, bullet), '-');
    NAOS_CHECK_FALSE(amark_list_start(g_doc, bullet, &start));

    ordered = find_node(10);
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
/* Inline nodes and their attributes                                      */
/* ---------------------------------------------------------------------- */

/*
 * Offsets:   0 "[d]: /def \"T\""  14 ""
 *           15 "*a* [b](</u> 't') ![c][d]"
 *           41 "[D][] [d] <x@y.z>" and two spaces
 *           61 "<http://h> `e`\"
 *           77 "**f** &amp; <b> \*" (no line ending), 95 bytes.
 */
static const char k_inline_content[] =
    "[d]: /def \"T\"\n"
    "\n"
    "*a* [b](</u> 't') ![c][d]\n"
    "[D][] [d] <x@y.z>  \n"
    "<http://h> `e`\\\n"
    "**f** &amp; <b> \\*";

static const expected_node_t k_inline_nodes[] = {
    {AMARK_KIND_DOCUMENT, 1, 0, 0, 95, 1, 6},
    {AMARK_KIND_LINK_REFERENCE_DEFINITION, 1, 1, 0, 13, 1, 1},
    {AMARK_KIND_PARAGRAPH, 1, 1, 15, 95, 3, 6},
    {AMARK_KIND_EMPHASIS, 0, 2, 15, 18, 3, 3},
    {AMARK_KIND_TEXT, 0, 3, 16, 17, 3, 3},
    {AMARK_KIND_TEXT, 0, 2, 18, 19, 3, 3},
    {AMARK_KIND_LINK, 0, 2, 19, 32, 3, 3},
    {AMARK_KIND_TEXT, 0, 3, 20, 21, 3, 3},
    {AMARK_KIND_TEXT, 0, 2, 32, 33, 3, 3},
    {AMARK_KIND_IMAGE, 0, 2, 33, 40, 3, 3},
    {AMARK_KIND_TEXT, 0, 3, 35, 36, 3, 3},
    {AMARK_KIND_SOFT_BREAK, 0, 2, 40, 41, 3, 3},
    {AMARK_KIND_LINK, 0, 2, 41, 46, 4, 4},
    {AMARK_KIND_TEXT, 0, 3, 42, 43, 4, 4},
    {AMARK_KIND_TEXT, 0, 2, 46, 47, 4, 4},
    {AMARK_KIND_LINK, 0, 2, 47, 50, 4, 4},
    {AMARK_KIND_TEXT, 0, 3, 48, 49, 4, 4},
    {AMARK_KIND_TEXT, 0, 2, 50, 51, 4, 4},
    {AMARK_KIND_AUTOLINK, 0, 2, 51, 58, 4, 4},
    {AMARK_KIND_HARD_BREAK, 0, 2, 58, 61, 4, 4},
    {AMARK_KIND_AUTOLINK, 0, 2, 61, 71, 5, 5},
    {AMARK_KIND_TEXT, 0, 2, 71, 72, 5, 5},
    {AMARK_KIND_CODE_SPAN, 0, 2, 72, 75, 5, 5},
    {AMARK_KIND_HARD_BREAK, 0, 2, 75, 77, 5, 5},
    {AMARK_KIND_STRONG, 0, 2, 77, 82, 6, 6},
    {AMARK_KIND_TEXT, 0, 3, 79, 80, 6, 6},
    {AMARK_KIND_TEXT, 0, 2, 82, 83, 6, 6},
    {AMARK_KIND_CHARACTER_REFERENCE, 0, 2, 83, 88, 6, 6},
    {AMARK_KIND_TEXT, 0, 2, 88, 89, 6, 6},
    {AMARK_KIND_RAW_HTML, 0, 2, 89, 92, 6, 6},
    {AMARK_KIND_TEXT, 0, 2, 92, 93, 6, 6},
    {AMARK_KIND_BACKSLASH_ESCAPE, 0, 2, 93, 95, 6, 6}};

#define INLINE_NODE_COUNT (sizeof(k_inline_nodes) / sizeof(k_inline_nodes[0]))

/* The positions of some nodes in k_inline_nodes. */
#define NTH_DEFINITION 1
#define NTH_EMPHASIS 3
#define NTH_INLINE_LINK 6
#define NTH_FULL_IMAGE 9
#define NTH_COLLAPSED_LINK 12
#define NTH_SHORTCUT_LINK 15
#define NTH_EMAIL 18
#define NTH_URI 20

/* Parses k_inline_content with no configuration into g_doc. */
static void parse_inline_content(void)
{
    parse_bytes(k_inline_content, sizeof(k_inline_content) - 1);
}

/* Requires the range reader to answer true with [start, end). */
#define CHECK_RANGE(call, start_, end_)                                     \
    do {                                                                    \
        amark_range_t r_ = {0, 0};                                          \
        NAOS_CHECK_TRUE(call(g_doc, node, &r_));                            \
        NAOS_CHECK_UINT_EQ(r_.start, (start_));                             \
        NAOS_CHECK_UINT_EQ(r_.end, (end_));                                 \
    } while (0)

/* Requires the range reader to answer false, leaving *out unchanged. */
#define CHECK_NO_RANGE(call)                                                \
    do {                                                                    \
        amark_range_t r_ = {7, 7};                                          \
        NAOS_CHECK_FALSE(call(g_doc, node, &r_));                           \
        NAOS_CHECK_UINT_EQ(r_.start, 7);                                    \
        NAOS_CHECK_UINT_EQ(r_.end, 7);                                      \
    } while (0)

/*
 * Visits every node, the inline nodes of the paragraph included, and
 * compares each with k_inline_nodes: kinds, the block or inline class,
 * depths, ranges and lines.
 */
NAOS_TEST(inline_nodes_kinds_and_ranges)
{
    parse_inline_content();
    check_traversal(k_inline_nodes, INLINE_NODE_COUNT);
}

NAOS_TEST(inline_link_attributes)
{
    amark_node_id_t node;

    parse_inline_content();
    /* [b](</u> 't'): the destination inside the angle brackets, the title
     * inside its quotes, and no definition. */
    node = find_node(NTH_INLINE_LINK);
    NAOS_REQUIRE_UINT_EQ(amark_node_kind(g_doc, node), AMARK_KIND_LINK);
    NAOS_CHECK_INT_EQ(amark_link_form(g_doc, node), AMARK_LINK_INLINE);
    CHECK_RANGE(amark_link_destination, 24, 26);
    CHECK_RANGE(amark_link_title, 29, 30);
    NAOS_CHECK_UINT_EQ(amark_link_definition(g_doc, node), AMARK_NODE_NONE);
    NAOS_CHECK_INT_EQ(amark_autolink_type(g_doc, node),
                      AMARK_AUTOLINK_TYPE_NONE);
}

/*
 * The three reference forms: each refers to the definition, whose label,
 * destination and title are read from it, and has no destination or title
 * of its own.
 */
NAOS_TEST(reference_link_attributes)
{
    static const struct {
        size_t nth;
        amark_kind_t kind;
        amark_link_form_t form;
    } refs[] = {
        {NTH_FULL_IMAGE, AMARK_KIND_IMAGE, AMARK_LINK_FULL},
        {NTH_COLLAPSED_LINK, AMARK_KIND_LINK, AMARK_LINK_COLLAPSED},
        {NTH_SHORTCUT_LINK, AMARK_KIND_LINK, AMARK_LINK_SHORTCUT}};
    amark_node_id_t definition;
    amark_node_id_t node;
    size_t i;

    parse_inline_content();
    definition = find_node(NTH_DEFINITION);
    NAOS_REQUIRE_UINT_EQ(amark_node_kind(g_doc, definition),
                         AMARK_KIND_LINK_REFERENCE_DEFINITION);
    for (i = 0; i < sizeof(refs) / sizeof(refs[0]); i++) {
        NAOS_TRACE_PUSH("reference %lu", (unsigned long)i);
        node = find_node(refs[i].nth);
        NAOS_REQUIRE_UINT_EQ(amark_node_kind(g_doc, node), refs[i].kind);
        NAOS_CHECK_INT_EQ(amark_link_form(g_doc, node), refs[i].form);
        NAOS_CHECK_UINT_EQ(amark_link_definition(g_doc, node), definition);
        CHECK_NO_RANGE(amark_link_destination);
        CHECK_NO_RANGE(amark_link_title);
        NAOS_TRACE_POP();
    }

    node = definition;
    CHECK_RANGE(amark_definition_label, 1, 2);
    CHECK_RANGE(amark_definition_destination, 5, 9);
    CHECK_RANGE(amark_definition_title, 11, 12);
    /* A definition is no link. */
    NAOS_CHECK_INT_EQ(amark_link_form(g_doc, node), AMARK_LINK_FORM_NONE);
}

NAOS_TEST(autolink_types)
{
    amark_node_id_t node;

    parse_inline_content();
    node = find_node(NTH_EMAIL);
    NAOS_REQUIRE_UINT_EQ(amark_node_kind(g_doc, node), AMARK_KIND_AUTOLINK);
    NAOS_CHECK_INT_EQ(amark_autolink_type(g_doc, node), AMARK_AUTOLINK_EMAIL);
    node = find_node(NTH_URI);
    NAOS_REQUIRE_UINT_EQ(amark_node_kind(g_doc, node), AMARK_KIND_AUTOLINK);
    NAOS_CHECK_INT_EQ(amark_autolink_type(g_doc, node), AMARK_AUTOLINK_URI);
    /* An autolink has none of the attributes of a link. */
    NAOS_CHECK_INT_EQ(amark_link_form(g_doc, node), AMARK_LINK_FORM_NONE);
    CHECK_NO_RANGE(amark_link_destination);
    NAOS_CHECK_UINT_EQ(amark_link_definition(g_doc, node), AMARK_NODE_NONE);
    /* An emphasis has neither. */
    node = find_node(NTH_EMPHASIS);
    NAOS_REQUIRE_UINT_EQ(amark_node_kind(g_doc, node), AMARK_KIND_EMPHASIS);
    NAOS_CHECK_INT_EQ(amark_autolink_type(g_doc, node),
                      AMARK_AUTOLINK_TYPE_NONE);
    NAOS_CHECK_INT_EQ(amark_link_form(g_doc, node), AMARK_LINK_FORM_NONE);
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

    AMARK_CONFIG_INIT(&config);
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

NAOS_SUITE(inline,
    NAOS_SET_SETUP(document),
    NAOS_SET_TEARDOWN(document),
    NAOS_ADD_TEST(inline_nodes_kinds_and_ranges),
    NAOS_ADD_TEST(inline_link_attributes),
    NAOS_ADD_TEST(reference_link_attributes),
    NAOS_ADD_TEST(autolink_types)
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
    NAOS_ADD_SUITE(inline),
    NAOS_ADD_SUITE(errors)
)
