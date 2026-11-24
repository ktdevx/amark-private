/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of the public text functions (include/amark/text.h).
 *
 * amark_node_raw: for every kind of node (containers and dialect kinds
 * included) the bytes are the range of the node in the content, at the same
 * position and of the same length; the root gives the whole parsed range;
 * an empty range gives true and length 0; a call that cannot be answered
 * (NULL arguments, AMARK_NODE_NONE, an ID past the nodes created, a removed
 * node) returns false and leaves both outputs unchanged; U+0000 and invalid
 * UTF-8 come back unchanged; and the allocator is never called.
 */

#include "document.h"
#include "test_alloc.h"
#include "tree_internal.h"

#include <amark/amark.h>

#include <naos/naos.h>

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/*
 * Fixture. NAOS_REQUIRE_* leaves a test with longjmp, so the document a test
 * makes is kept in a static variable, and teardown destroys it and checks
 * that no block is left and that every call received its context value.
 */
static amark_test_alloc_t g_state;
static amark_config_t g_config;
static amark_document_t *g_doc;

NAOS_SETUP(counting)
{
    amark_test_alloc_init(&g_state);
    amark_config_init(&g_config);
    g_config.allocator = amark_test_allocator(&g_state);
    g_doc = NULL;
}

NAOS_TEARDOWN(counting)
{
    amark_document_destroy(g_doc);
    g_doc = NULL;
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
    NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
}

/* Parses the length bytes at content into g_doc. */
static void parse_bytes(const char *content, size_t length)
{
    amark_document_destroy(g_doc);
    g_doc = NULL;
    NAOS_REQUIRE_INT_EQ(amark_parse(content, length, &g_config, &g_doc),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_NOT_NULL(g_doc);
}

/* Returns the node after node in document order, or AMARK_NODE_NONE. */
static amark_node_id_t next_in_order(amark_node_id_t node)
{
    amark_node_id_t next = amark_node_first_child(g_doc, node);

    while (next == AMARK_NODE_NONE && node != AMARK_NODE_NONE) {
        next = amark_node_next_sibling(g_doc, node);
        if (next == AMARK_NODE_NONE) {
            node = amark_node_parent(g_doc, node);
        }
    }
    return next;
}

/* Returns the n-th node (from 0) of kind in document order, or none. */
static amark_node_id_t find(amark_kind_t kind, size_t n)
{
    amark_node_id_t node = amark_document_root(g_doc);

    while (node != AMARK_NODE_NONE) {
        if (amark_node_kind(g_doc, node) == kind) {
            if (n == 0) {
                return node;
            }
            n--;
        }
        node = next_in_order(node);
    }
    return AMARK_NODE_NONE;
}

/*
 * Checks that amark_node_raw on node of g_doc gives the range of node in
 * content: the same position, the same length and the same bytes.
 */
static void check_raw(const char *content, amark_node_id_t node)
{
    amark_range_t range;
    const char *bytes = NULL;
    size_t length = (size_t)-1;

    NAOS_REQUIRE_TRUE(amark_node_range(g_doc, node, &range));
    NAOS_REQUIRE_TRUE(amark_node_raw(g_doc, node, &bytes, &length));
    NAOS_CHECK_PTR_EQ(bytes, content + range.start);
    NAOS_CHECK_UINT_EQ(length, range.end - range.start);
    NAOS_CHECK_MEM_EQ(bytes, length, content + range.start,
                      range.end - range.start);
}

/* Every CommonMark kind, blocks and inlines. */
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
    AMARK_KIND_CHARACTER_REFERENCE,
};

#define CORE_KIND_COUNT (sizeof(k_core_kinds) / sizeof(k_core_kinds[0]))

NAOS_TEST(every_core_kind_gives_its_range)
{
    /* Every CommonMark kind, inside containers so that ranges span
       prefixes, tabs and line endings of three kinds. */
    static const char content[] =
        "# Heading\r\n"
        "\n"
        "***\n"
        "\n"
        "> - item *em* **strong** `code`\r"
        ">   [link](/d \"t\") ![image](/s)\n"
        "\n"
        "\tindented code\n"
        "\n"
        "```c\n"
        "fenced\n"
        "```\n"
        "\n"
        "<div>\n"
        "html\n"
        "</div>\n"
        "\n"
        "[ref]: /url \"title\"\n"
        "\n"
        "<http://a.example> <span> \\* &amp; hard  \n"
        "soft\n"
        "last";
    bool seen[CORE_KIND_COUNT];
    amark_node_id_t node;
    size_t visited = 0;
    size_t i;

    memset(seen, 0, sizeof seen);
    parse_bytes(content, sizeof content - 1);
    node = amark_document_root(g_doc);
    while (node != AMARK_NODE_NONE) {
        amark_kind_t kind = amark_node_kind(g_doc, node);

        check_raw(content, node);
        for (i = 0; i < CORE_KIND_COUNT; i++) {
            if (k_core_kinds[i] == kind) {
                seen[i] = true;
            }
        }
        visited++;
        node = next_in_order(node);
    }
    NAOS_CHECK_UINT_GT(visited, CORE_KIND_COUNT);
    for (i = 0; i < CORE_KIND_COUNT; i++) {
        NAOS_CHECK_TRUE(seen[i]);
    }
}

NAOS_TEST(container_ranges_include_the_prefixes)
{
    static const char content[] = "> a\n> b\n";
    const char *bytes = NULL;
    size_t length = 0;
    amark_node_id_t quote;
    amark_node_id_t para;

    parse_bytes(content, sizeof content - 1);
    quote = find(AMARK_KIND_BLOCK_QUOTE, 0);
    para = find(AMARK_KIND_PARAGRAPH, 0);
    NAOS_REQUIRE_UINT_NE(quote, AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(para, AMARK_NODE_NONE);
    NAOS_REQUIRE_TRUE(amark_node_raw(g_doc, quote, &bytes, &length));
    NAOS_CHECK_MEM_EQ(bytes, length, "> a\n> b", (size_t)7);
    /* The paragraph spans the second prefix as the source has it. */
    NAOS_REQUIRE_TRUE(amark_node_raw(g_doc, para, &bytes, &length));
    NAOS_CHECK_MEM_EQ(bytes, length, "a\n> b", (size_t)5);
}

NAOS_TEST(dialect_kinds_give_their_range)
{
    static const char content[] = "abcdef";
    amark_allocator_t allocator = amark_test_allocator(&g_state);
    amark_node_id_t block;
    amark_node_id_t inl;
    const char *bytes = NULL;
    size_t length = 0;

    /* A document made without amark_parse has no parse data; the raw bytes
       do not need it. */
    NAOS_REQUIRE_INT_EQ(amark_document_create(&allocator,
                                              (const unsigned char *)content,
                                              6, 0, 6, NULL, &g_doc),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_append(g_doc, 1, AMARK_KIND_DIALECT_MIN,
                                          AMARK_NODE_CLASS_BLOCK, 1, 5,
                                          &block),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_append(g_doc, block,
                                          AMARK_KIND_DIALECT_MIN + 1,
                                          AMARK_NODE_CLASS_INLINE, 2, 4,
                                          &inl),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_TRUE(amark_node_raw(g_doc, block, &bytes, &length));
    NAOS_CHECK_PTR_EQ(bytes, content + 1);
    NAOS_CHECK_UINT_EQ(length, 4);
    NAOS_REQUIRE_TRUE(amark_node_raw(g_doc, inl, &bytes, &length));
    NAOS_CHECK_PTR_EQ(bytes, content + 2);
    NAOS_CHECK_UINT_EQ(length, 2);
}

NAOS_TEST(root_gives_the_whole_parsed_range)
{
    static const char content[] = "skip\npara\nmore\nskip";
    const char *bytes = NULL;
    size_t length = 0;

    parse_bytes(content, sizeof content - 1);
    NAOS_REQUIRE_TRUE(amark_node_raw(g_doc, amark_document_root(g_doc),
                                     &bytes, &length));
    NAOS_CHECK_PTR_EQ(bytes, content);
    NAOS_CHECK_UINT_EQ(length, sizeof content - 1);

    /* A range that starts and ends inside the content. */
    g_config.start = 5;
    g_config.end = 15;
    parse_bytes(content, sizeof content - 1);
    NAOS_REQUIRE_TRUE(amark_node_raw(g_doc, amark_document_root(g_doc),
                                     &bytes, &length));
    NAOS_CHECK_PTR_EQ(bytes, content + 5);
    NAOS_CHECK_UINT_EQ(length, 10);
    NAOS_CHECK_MEM_EQ(bytes, length, "para\nmore\n", (size_t)10);
}

NAOS_TEST(empty_range_gives_true_and_length_zero)
{
    static const char content[] = "abc";
    amark_allocator_t allocator = amark_test_allocator(&g_state);
    amark_node_id_t node;
    const char *bytes = NULL;
    size_t length = 99;

    /* The empty parse: the root is [0, 0). */
    parse_bytes(content, 0);
    NAOS_REQUIRE_TRUE(amark_node_raw(g_doc, amark_document_root(g_doc),
                                     &bytes, &length));
    NAOS_CHECK_PTR_EQ(bytes, content);
    NAOS_CHECK_UINT_EQ(length, 0);

    /* An empty node in the middle of the content. */
    amark_document_destroy(g_doc);
    g_doc = NULL;
    NAOS_REQUIRE_INT_EQ(amark_document_create(&allocator,
                                              (const unsigned char *)content,
                                              3, 0, 3, NULL, &g_doc),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_append(g_doc, 1, AMARK_KIND_PARAGRAPH,
                                          AMARK_NODE_CLASS_AUTO, 2, 2, &node),
                        AMARK_ERROR_NONE);
    length = 99;
    NAOS_REQUIRE_TRUE(amark_node_raw(g_doc, node, &bytes, &length));
    NAOS_CHECK_PTR_EQ(bytes, content + 2);
    NAOS_CHECK_UINT_EQ(length, 0);

    /* NULL content of length 0: true, length 0, and bytes may be NULL. */
    amark_document_destroy(g_doc);
    g_doc = NULL;
    NAOS_REQUIRE_INT_EQ(amark_parse(NULL, 0, &g_config, &g_doc),
                        AMARK_ERROR_NONE);
    length = 99;
    NAOS_REQUIRE_TRUE(amark_node_raw(g_doc, amark_document_root(g_doc),
                                     &bytes, &length));
    NAOS_CHECK_UINT_EQ(length, 0);
}

/* Checks that amark_node_raw(doc, node, ...) fails and changes nothing. */
static void check_raw_fails(const amark_document_t *doc,
                            amark_node_id_t node)
{
    static const char sentinel[] = "sentinel";
    const char *bytes = sentinel;
    size_t length = 12345;

    NAOS_CHECK_FALSE(amark_node_raw(doc, node, &bytes, &length));
    NAOS_CHECK_PTR_EQ(bytes, sentinel);
    NAOS_CHECK_UINT_EQ(length, 12345);
}

NAOS_TEST(calls_that_cannot_be_answered_leave_the_outputs_unchanged)
{
    static const char content[] = "para\n";
    static const char sentinel[] = "sentinel";
    amark_node_id_t root;
    const char *bytes = sentinel;
    size_t length = 12345;

    parse_bytes(content, sizeof content - 1);
    root = amark_document_root(g_doc);

    check_raw_fails(NULL, root);
    check_raw_fails(g_doc, AMARK_NODE_NONE);
    check_raw_fails(g_doc,
                    (amark_node_id_t)amark_document_node_count(g_doc) + 1);
    check_raw_fails(g_doc, (amark_node_id_t)0xFFFFFFFFu);

    /* NULL bytes: length is unchanged. NULL length: bytes is unchanged. */
    NAOS_CHECK_FALSE(amark_node_raw(g_doc, root, NULL, &length));
    NAOS_CHECK_UINT_EQ(length, 12345);
    NAOS_CHECK_FALSE(amark_node_raw(g_doc, root, &bytes, NULL));
    NAOS_CHECK_PTR_EQ(bytes, sentinel);
    NAOS_CHECK_FALSE(amark_node_raw(g_doc, root, NULL, NULL));
}

NAOS_TEST(removed_node_leaves_the_outputs_unchanged)
{
    static const char content[] = "abcdef";
    amark_allocator_t allocator = amark_test_allocator(&g_state);
    amark_node_id_t kept;
    amark_node_id_t removed;

    NAOS_REQUIRE_INT_EQ(amark_document_create(&allocator,
                                              (const unsigned char *)content,
                                              6, 0, 6, NULL, &g_doc),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_append(g_doc, 1, AMARK_KIND_PARAGRAPH,
                                          AMARK_NODE_CLASS_AUTO, 0, 3,
                                          &removed),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_append(g_doc, 1, AMARK_KIND_PARAGRAPH,
                                          AMARK_NODE_CLASS_AUTO, 3, 6, &kept),
                        AMARK_ERROR_NONE);
    amark_tree_remove(g_doc, removed);

    check_raw_fails(g_doc, removed);
    /* The node after it still answers; the count includes the removed one. */
    check_raw(content, kept);
    check_raw_fails(g_doc, kept + 1);
}

NAOS_TEST(nul_and_invalid_utf8_come_back_unchanged)
{
    /* U+0000, a lone continuation byte, a truncated sequence and an
       overlong form, in a paragraph and in a code block. */
    static const char content[] =
        "a\0b \x80 c\xE2\x82 d\xC0\xAF\n"
        "\n"
        "    x\0\xFF\n";
    const size_t length_all = sizeof content - 1;
    amark_node_id_t node;
    const char *bytes = NULL;
    size_t length = 0;

    g_config.utf8_mode = AMARK_UTF8_REPLACE;
    parse_bytes(content, length_all);

    NAOS_REQUIRE_TRUE(amark_node_raw(g_doc, amark_document_root(g_doc),
                                     &bytes, &length));
    NAOS_CHECK_PTR_EQ(bytes, content);
    NAOS_CHECK_MEM_EQ(bytes, length, content, length_all);

    node = find(AMARK_KIND_PARAGRAPH, 0);
    NAOS_REQUIRE_UINT_NE(node, AMARK_NODE_NONE);
    NAOS_REQUIRE_TRUE(amark_node_raw(g_doc, node, &bytes, &length));
    NAOS_CHECK_MEM_EQ(bytes, length, "a\0b \x80 c\xE2\x82 d\xC0\xAF",
                      (size_t)13);

    node = find(AMARK_KIND_CODE_BLOCK, 0);
    NAOS_REQUIRE_UINT_NE(node, AMARK_NODE_NONE);
    check_raw(content, node);

    /* Every node, text nodes included. */
    node = amark_document_root(g_doc);
    while (node != AMARK_NODE_NONE) {
        check_raw(content, node);
        node = next_in_order(node);
    }
}

NAOS_TEST(raw_never_calls_the_allocator)
{
    static const char content[] = "> - *a* `b` [c](/d)\n\n    code\n";
    size_t mallocs;
    size_t frees;
    amark_node_id_t node;

    parse_bytes(content, sizeof content - 1);
    mallocs = g_state.malloc_calls;
    frees = g_state.free_calls;
    node = amark_document_root(g_doc);
    while (node != AMARK_NODE_NONE) {
        check_raw(content, node);
        node = next_in_order(node);
    }
    check_raw_fails(g_doc, AMARK_NODE_NONE);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, mallocs);
    NAOS_CHECK_UINT_EQ(g_state.free_calls, frees);
}

NAOS_SUITE(text_raw,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(every_core_kind_gives_its_range),
    NAOS_ADD_TEST(container_ranges_include_the_prefixes),
    NAOS_ADD_TEST(dialect_kinds_give_their_range),
    NAOS_ADD_TEST(root_gives_the_whole_parsed_range),
    NAOS_ADD_TEST(empty_range_gives_true_and_length_zero),
    NAOS_ADD_TEST(calls_that_cannot_be_answered_leave_the_outputs_unchanged),
    NAOS_ADD_TEST(removed_node_leaves_the_outputs_unchanged),
    NAOS_ADD_TEST(nul_and_invalid_utf8_come_back_unchanged),
    NAOS_ADD_TEST(raw_never_calls_the_allocator)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(text_raw)
)
