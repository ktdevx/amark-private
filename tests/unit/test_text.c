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
 *
 * amark_node_text, the part and kind table: every row of the table (the
 * logical text of non-container blocks and of inlines, dialect inlines
 * included; the value of text, escapes, character references, autolinks,
 * code spans and code blocks; the destination and title of inline links,
 * images and link reference definitions; the info of fenced code blocks)
 * and pairs that are not in it (containers, dialect blocks, the value of
 * other kinds, the destination of reference links, the info of indented
 * code blocks). A destination or title that is not written is not
 * available, one that is written but empty gives length 0, in agreement
 * with the tree readers; reference links get their values from the
 * definition. Every call that cannot be answered (NULL document or length,
 * an invalid ID or part, a document without parse data) leaves the buffer
 * and *length unchanged and allocates nothing.
 *
 * amark_node_text, passing the result: a size-only call (NULL buffer, any
 * capacity) reports the length; a buffer one byte too small gives
 * BUFFER_TOO_SMALL with the length and nothing written past its capacity;
 * calling again with the length reported succeeds; a result of length 0
 * succeeds; a range longer than (SIZE_MAX - 2) / 3 gives LIMIT_EXCEEDED with
 * the buffer and *length unchanged; none of these allocates.
 */

#include "document.h"
#include "test_alloc.h"
#include "tree_internal.h"

#include <amark/amark.h>

#include <naos/naos.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
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

/* --- amark_node_text: the part and kind table ---------------------------- */

/*
 * Checks that part of node of g_doc gives want, of want_length bytes, with
 * AMARK_ERROR_NONE, in a buffer that is large enough.
 */
static void check_text_n(amark_node_id_t node, amark_text_part_t part,
                         const char *want, size_t want_length)
{
    char buffer[256];
    size_t length = (size_t)-1;

    NAOS_REQUIRE_UINT_NE(node, AMARK_NODE_NONE);
    memset(buffer, '#', sizeof buffer);
    NAOS_REQUIRE_INT_EQ(amark_node_text(g_doc, node, part, buffer,
                                        sizeof buffer, &length),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_MEM_EQ(buffer, length, want, want_length);
}

static void check_text(amark_node_id_t node, amark_text_part_t part,
                       const char *want)
{
    check_text_n(node, part, want, strlen(want));
}

/*
 * Checks that part of node of doc is AMARK_ERROR_NOT_AVAILABLE and that the
 * buffer, *length and the allocator are untouched.
 */
static void check_none(const amark_document_t *doc, amark_node_id_t node,
                       int part)
{
    char buffer[16];
    size_t length = 12345;
    size_t mallocs = g_state.malloc_calls;
    size_t i;

    memset(buffer, 'S', sizeof buffer);
    NAOS_CHECK_INT_EQ(amark_node_text(doc, node, (amark_text_part_t)part,
                                      buffer, sizeof buffer, &length),
                      AMARK_ERROR_NOT_AVAILABLE);
    NAOS_CHECK_UINT_EQ(length, 12345);
    for (i = 0; i < sizeof buffer; i++) {
        NAOS_CHECK_INT_EQ(buffer[i], 'S');
    }
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, mallocs);
}

/* Returns whether kind is a container (document, block quote, list, item). */
static bool is_container(amark_kind_t kind)
{
    return kind == AMARK_KIND_DOCUMENT || kind == AMARK_KIND_BLOCK_QUOTE ||
           kind == AMARK_KIND_LIST || kind == AMARK_KIND_LIST_ITEM;
}

NAOS_TEST(logical_of_non_container_blocks_and_inlines)
{
    static const char content[] =
        "> # Head *x*\n"
        "> ***\n"
        ">\n"
        "> para `one`\r\n"
        "> \tline *two*\n"
        "\n"
        "```c\n"
        "code\n"
        "```\n"
        "\n"
        "<div>\n"
        "</div>\n"
        "\n"
        "[ref]: /u\n"
        "  'title'\n";
    amark_node_id_t para;

    parse_bytes(content, sizeof content - 1);
    check_text(find(AMARK_KIND_HEADING, 0), AMARK_TEXT_LOGICAL,
               "# Head *x*");
    check_text(find(AMARK_KIND_THEMATIC_BREAK, 0), AMARK_TEXT_LOGICAL,
               "***");
    para = find(AMARK_KIND_PARAGRAPH, 0);
    /* The prefix and the leading tab of the continuation line are not part
       of the line's content. */
    check_text(para, AMARK_TEXT_LOGICAL, "para `one`\nline *two*");
    check_text(find(AMARK_KIND_CODE_BLOCK, 0), AMARK_TEXT_LOGICAL,
               "```c\ncode\n```");
    check_text(find(AMARK_KIND_HTML_BLOCK, 0), AMARK_TEXT_LOGICAL,
               "<div>\n</div>");
    check_text(find(AMARK_KIND_LINK_REFERENCE_DEFINITION, 0),
               AMARK_TEXT_LOGICAL, "[ref]: /u\n'title'");
    /* Inlines: written as they are. */
    check_text(find(AMARK_KIND_EMPHASIS, 0), AMARK_TEXT_LOGICAL, "*x*");
    check_text(find(AMARK_KIND_CODE_SPAN, 0), AMARK_TEXT_LOGICAL, "`one`");
    check_text(find(AMARK_KIND_SOFT_BREAK, 0), AMARK_TEXT_LOGICAL, "\n");
}

NAOS_TEST(logical_of_every_inline_kind_is_its_range)
{
    static const char content[] =
        "*em* **st** `c` [l](/d) ![i](/s) <http://a> <b> \\* &amp; x  \n"
        "y\\\n"
        "z\n";
    amark_node_id_t node;
    size_t inlines = 0;

    parse_bytes(content, sizeof content - 1);
    node = amark_document_root(g_doc);
    while (node != AMARK_NODE_NONE) {
        amark_kind_t kind = amark_node_kind(g_doc, node);

        if (amark_node_is_inline(g_doc, node)) {
            const char *bytes = NULL;
            size_t length = 0;
            size_t i;

            /* Within one line, the logical text is the raw bytes (2.12);
               a line ending becomes '\n' (only the breaks hold one). */
            NAOS_REQUIRE_TRUE(amark_node_raw(g_doc, node, &bytes, &length));
            for (i = 0; i < length; i++) {
                NAOS_REQUIRE_INT_NE(bytes[i], '\r');
            }
            check_text_n(node, AMARK_TEXT_LOGICAL, bytes, length);
            inlines++;
        } else if (is_container(kind)) {
            check_none(g_doc, node, AMARK_TEXT_LOGICAL);
        } else {
            NAOS_CHECK_UINT_EQ(kind, AMARK_KIND_PARAGRAPH);
        }
        node = next_in_order(node);
    }
    NAOS_CHECK_UINT_GE(inlines, 12);
    NAOS_CHECK_UINT_NE(find(AMARK_KIND_HARD_BREAK, 1), AMARK_NODE_NONE);
    NAOS_CHECK_UINT_NE(find(AMARK_KIND_RAW_HTML, 0), AMARK_NODE_NONE);
}

NAOS_TEST(logical_of_containers_is_not_available)
{
    static const char content[] = "> - a\n>   - b\n> c\n";
    amark_node_id_t node;
    size_t containers = 0;

    parse_bytes(content, sizeof content - 1);
    node = amark_document_root(g_doc);
    while (node != AMARK_NODE_NONE) {
        if (is_container(amark_node_kind(g_doc, node))) {
            check_none(g_doc, node, AMARK_TEXT_LOGICAL);
            check_none(g_doc, node, AMARK_TEXT_VALUE);
            containers++;
        }
        node = next_in_order(node);
    }
    NAOS_CHECK_UINT_EQ(containers, 6);
}

NAOS_TEST(dialect_kinds_answer_the_logical_text_of_inlines_only)
{
    static const char content[] = "abcdef\n";
    amark_node_id_t para;
    amark_node_id_t block;
    amark_node_id_t inl;
    int part;

    parse_bytes(content, sizeof content - 1);
    para = find(AMARK_KIND_PARAGRAPH, 0);
    NAOS_REQUIRE_UINT_NE(para, AMARK_NODE_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_append(g_doc, 1, AMARK_KIND_DIALECT_MIN,
                                          AMARK_NODE_CLASS_BLOCK, 0, 6,
                                          &block),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_append(g_doc, para,
                                          AMARK_KIND_DIALECT_MIN + 1,
                                          AMARK_NODE_CLASS_INLINE, 1, 4,
                                          &inl),
                        AMARK_ERROR_NONE);
    check_text(inl, AMARK_TEXT_LOGICAL, "bcd");
    check_none(g_doc, block, AMARK_TEXT_LOGICAL);
    for (part = AMARK_TEXT_VALUE; part <= AMARK_TEXT_INFO; part++) {
        check_none(g_doc, block, part);
        check_none(g_doc, inl, part);
    }
}

NAOS_TEST(value_of_text_escape_reference_autolink)
{
    static const char content[] =
        "a\\q \\* &amp; &#x41; &nbsp; <http://x.example/a\\b&amp;> "
        "<me@x.example>\n";

    parse_bytes(content, sizeof content - 1);
    /* Text is written as it is (a backslash before a non-punctuation
       character stays). */
    check_text(find(AMARK_KIND_TEXT, 0), AMARK_TEXT_VALUE, "a\\q ");
    check_text(find(AMARK_KIND_BACKSLASH_ESCAPE, 0), AMARK_TEXT_VALUE, "*");
    check_text(find(AMARK_KIND_CHARACTER_REFERENCE, 0), AMARK_TEXT_VALUE,
               "&");
    check_text(find(AMARK_KIND_CHARACTER_REFERENCE, 1), AMARK_TEXT_VALUE,
               "A");
    check_text(find(AMARK_KIND_CHARACTER_REFERENCE, 2), AMARK_TEXT_VALUE,
               "\xC2\xA0");
    /* An autolink without '<' and '>', nothing decoded, no "mailto:". */
    check_text(find(AMARK_KIND_AUTOLINK, 0), AMARK_TEXT_VALUE,
               "http://x.example/a\\b&amp;");
    check_text(find(AMARK_KIND_AUTOLINK, 1), AMARK_TEXT_VALUE,
               "me@x.example");
}

NAOS_TEST(value_of_code_span_strips_the_backtick_strings)
{
    static const char content[] =
        "`a` `` b`c `` ```\\&amp;``` ` `\n"
        "\n"
        "> x `` one\n"
        "> two ``\n";

    parse_bytes(content, sizeof content - 1);
    check_text(find(AMARK_KIND_CODE_SPAN, 0), AMARK_TEXT_VALUE, "a");
    check_text(find(AMARK_KIND_CODE_SPAN, 1), AMARK_TEXT_VALUE, "b`c");
    check_text(find(AMARK_KIND_CODE_SPAN, 2), AMARK_TEXT_VALUE,
               "\\&amp;");
    check_text(find(AMARK_KIND_CODE_SPAN, 3), AMARK_TEXT_VALUE, " ");
    /* Across lines in a block quote: the line ending is one space and the
       prefix is left out. */
    check_text(find(AMARK_KIND_CODE_SPAN, 4), AMARK_TEXT_VALUE, "one two");
}

NAOS_TEST(value_of_code_blocks_is_the_code_lines)
{
    static const char content[] =
        "    a\\*\n"
        "\n"
        "     b\n"
        "\n"
        "```\n"
        "c &amp;\n"
        "```\n"
        "\n"
        "~~~\n"
        "~~~\n"
        "\n"
        "> ```\n"
        "> d";

    parse_bytes(content, sizeof content - 1);
    check_text(find(AMARK_KIND_CODE_BLOCK, 0), AMARK_TEXT_VALUE,
               "a\\*\n\n b\n");
    check_text(find(AMARK_KIND_CODE_BLOCK, 1), AMARK_TEXT_VALUE,
               "c &amp;\n");
    check_text(find(AMARK_KIND_CODE_BLOCK, 2), AMARK_TEXT_VALUE, "");
    check_text(find(AMARK_KIND_CODE_BLOCK, 3), AMARK_TEXT_VALUE, "d\n");
}

NAOS_TEST(value_of_other_kinds_is_not_available)
{
    static const char content[] =
        "# h\n"
        "\n"
        "***\n"
        "\n"
        "<div>\n"
        "\n"
        "*e* **s** [l](/d) ![i](/s) <b> x  \n"
        "y\n"
        "z\n"
        "\n"
        "[r]: /u\n";
    static const amark_kind_t kinds[] = {
        AMARK_KIND_HEADING,     AMARK_KIND_THEMATIC_BREAK,
        AMARK_KIND_HTML_BLOCK,  AMARK_KIND_PARAGRAPH,
        AMARK_KIND_EMPHASIS,    AMARK_KIND_STRONG,
        AMARK_KIND_LINK,        AMARK_KIND_IMAGE,
        AMARK_KIND_RAW_HTML,    AMARK_KIND_HARD_BREAK,
        AMARK_KIND_SOFT_BREAK,  AMARK_KIND_LINK_REFERENCE_DEFINITION,
        AMARK_KIND_DOCUMENT,
    };
    size_t i;

    parse_bytes(content, sizeof content - 1);
    for (i = 0; i < sizeof kinds / sizeof kinds[0]; i++) {
        amark_node_id_t node = find(kinds[i], 0);

        NAOS_REQUIRE_UINT_NE(node, AMARK_NODE_NONE);
        check_none(g_doc, node, AMARK_TEXT_VALUE);
        check_none(g_doc, node, AMARK_TEXT_INFO);
    }
}

NAOS_TEST(destination_and_title_of_inline_links_and_images)
{
    static const char content[] =
        "[a](</d\\)&amp;> \"t &quot;x&quot;\") "
        "![i](/s 't') "
        "[e](<> \"\") "
        "[n](/only) "
        "[m]()\n";
    amark_node_id_t link;
    amark_node_id_t image;
    amark_node_id_t empty;
    amark_node_id_t no_title;

    parse_bytes(content, sizeof content - 1);
    link = find(AMARK_KIND_LINK, 0);
    image = find(AMARK_KIND_IMAGE, 0);
    empty = find(AMARK_KIND_LINK, 1);
    no_title = find(AMARK_KIND_LINK, 2);
    /* Written: decoded, without the delimiters. */
    check_text(link, AMARK_TEXT_DESTINATION, "/d)&");
    check_text(link, AMARK_TEXT_TITLE, "t \"x\"");
    check_text(image, AMARK_TEXT_DESTINATION, "/s");
    check_text(image, AMARK_TEXT_TITLE, "t");
    /* Written but empty: success with length 0. */
    check_text(empty, AMARK_TEXT_DESTINATION, "");
    check_text(empty, AMARK_TEXT_TITLE, "");
    /* Not written. */
    check_text(no_title, AMARK_TEXT_DESTINATION, "/only");
    check_none(g_doc, no_title, AMARK_TEXT_TITLE);
    /* Neither a value nor an info. */
    check_none(g_doc, link, AMARK_TEXT_VALUE);
    check_none(g_doc, image, AMARK_TEXT_INFO);
}

NAOS_TEST(written_or_not_agrees_with_the_tree_readers)
{
    static const char content[] =
        "[a](/d \"t\") [b](<>) [c]() [d](/e '') ![f]( \"g\" ) [h](</i> ())\n"
        "\n"
        "[j]: <> ''\n"
        "[k]: /l\n";
    amark_node_id_t node;
    size_t checked = 0;

    parse_bytes(content, sizeof content - 1);
    node = amark_document_root(g_doc);
    while (node != AMARK_NODE_NONE) {
        amark_kind_t kind = amark_node_kind(g_doc, node);
        bool is_link = kind == AMARK_KIND_LINK || kind == AMARK_KIND_IMAGE;

        if (is_link || kind == AMARK_KIND_LINK_REFERENCE_DEFINITION) {
            amark_range_t range;
            size_t length = (size_t)-1;
            char buffer[64];
            bool dest = is_link ? amark_link_destination(g_doc, node, &range)
                                : amark_definition_destination(g_doc, node,
                                                               &range);
            bool title;

            NAOS_CHECK_INT_EQ(amark_node_text(g_doc, node,
                                              AMARK_TEXT_DESTINATION, buffer,
                                              sizeof buffer, &length),
                              dest ? AMARK_ERROR_NONE
                                   : AMARK_ERROR_NOT_AVAILABLE);
            if (dest && range.start == range.end) {
                NAOS_CHECK_UINT_EQ(length, 0);
            }
            length = (size_t)-1;
            title = is_link ? amark_link_title(g_doc, node, &range)
                            : amark_definition_title(g_doc, node, &range);
            NAOS_CHECK_INT_EQ(amark_node_text(g_doc, node, AMARK_TEXT_TITLE,
                                              buffer, sizeof buffer, &length),
                              title ? AMARK_ERROR_NONE
                                    : AMARK_ERROR_NOT_AVAILABLE);
            if (title && range.start == range.end) {
                NAOS_CHECK_UINT_EQ(length, 0);
            }
            checked++;
        }
        node = next_in_order(node);
    }
    NAOS_CHECK_UINT_EQ(checked, 8);
}

NAOS_TEST(reference_links_take_their_values_from_the_definition)
{
    static const char content[] =
        "[a] [b][] [x][c] ![a]\n"
        "\n"
        "[a]: /u&amp;\\* \"t&#33;\"\n"
        "[b]: <>\n"
        "[c]: /v ''\n";
    amark_node_id_t node;
    amark_node_id_t def;
    size_t i;

    parse_bytes(content, sizeof content - 1);
    for (i = 0; i < 3; i++) {
        node = find(AMARK_KIND_LINK, i);
        NAOS_REQUIRE_UINT_NE(node, AMARK_NODE_NONE);
        check_none(g_doc, node, AMARK_TEXT_DESTINATION);
        check_none(g_doc, node, AMARK_TEXT_TITLE);
    }
    node = find(AMARK_KIND_IMAGE, 0);
    check_none(g_doc, node, AMARK_TEXT_DESTINATION);
    check_none(g_doc, node, AMARK_TEXT_TITLE);
    def = amark_link_definition(g_doc, node);
    check_text(def, AMARK_TEXT_DESTINATION, "/u&*");
    check_text(def, AMARK_TEXT_TITLE, "t!");

    def = amark_link_definition(g_doc, find(AMARK_KIND_LINK, 1));
    check_text(def, AMARK_TEXT_DESTINATION, "");
    check_none(g_doc, def, AMARK_TEXT_TITLE);

    def = amark_link_definition(g_doc, find(AMARK_KIND_LINK, 2));
    check_text(def, AMARK_TEXT_DESTINATION, "/v");
    check_text(def, AMARK_TEXT_TITLE, "");
    check_none(g_doc, def, AMARK_TEXT_VALUE);
    check_none(g_doc, def, AMARK_TEXT_INFO);
}

NAOS_TEST(titles_start_on_the_line_of_their_range)
{
    /* The title starts on a later line than the node: its first line is
       looked up from the start of its range, so the prefixes of the lines
       after it are left out and the first is read from the range start. */
    static const char content[] =
        "> [a](/u\n"
        ">  \"one\n"
        "> two\")\n"
        "\n"
        "> - [d]:\n"
        ">   /v\n"
        ">   'three\n"
        ">   four'\n";

    parse_bytes(content, sizeof content - 1);
    check_text(find(AMARK_KIND_LINK, 0), AMARK_TEXT_TITLE, "one\ntwo");
    check_text(find(AMARK_KIND_LINK_REFERENCE_DEFINITION, 0),
               AMARK_TEXT_DESTINATION, "/v");
    check_text(find(AMARK_KIND_LINK_REFERENCE_DEFINITION, 0),
               AMARK_TEXT_TITLE, "three\nfour");
}

NAOS_TEST(info_of_fenced_code_blocks_only)
{
    static const char content[] =
        "``` c&amp;  \\*d\n"
        "```\n"
        "\n"
        "```\n"
        "```\n"
        "\n"
        "    indented\n";
    amark_node_id_t fenced;
    amark_node_id_t bare;
    amark_node_id_t indented;

    parse_bytes(content, sizeof content - 1);
    fenced = find(AMARK_KIND_CODE_BLOCK, 0);
    bare = find(AMARK_KIND_CODE_BLOCK, 1);
    indented = find(AMARK_KIND_CODE_BLOCK, 2);
    check_text(fenced, AMARK_TEXT_INFO, "c&  *d");
    check_none(g_doc, bare, AMARK_TEXT_INFO);
    check_none(g_doc, indented, AMARK_TEXT_INFO);
    check_none(g_doc, fenced, AMARK_TEXT_DESTINATION);
    check_none(g_doc, fenced, AMARK_TEXT_TITLE);
    check_none(g_doc, find(AMARK_KIND_PARAGRAPH, 0), AMARK_TEXT_INFO);
}

NAOS_TEST(text_calls_that_cannot_be_answered_change_nothing)
{
    static const char content[] = "para\n\npara two\n";
    amark_allocator_t allocator = amark_test_allocator(&g_state);
    amark_document_t *bare = NULL;
    amark_node_id_t para;
    amark_node_id_t removed;
    char buffer[8];
    size_t i;

    parse_bytes(content, sizeof content - 1);
    para = find(AMARK_KIND_PARAGRAPH, 0);
    NAOS_REQUIRE_UINT_NE(para, AMARK_NODE_NONE);

    /* NULL document, invalid IDs. */
    check_none(NULL, para, AMARK_TEXT_LOGICAL);
    check_none(g_doc, AMARK_NODE_NONE, AMARK_TEXT_LOGICAL);
    check_none(g_doc, (amark_node_id_t)amark_document_node_count(g_doc) + 1,
               AMARK_TEXT_LOGICAL);
    check_none(g_doc, (amark_node_id_t)0xFFFFFFFFu, AMARK_TEXT_LOGICAL);
    /* A part that is none of the five. */
    check_none(g_doc, para, 0);
    check_none(g_doc, para, 6);
    check_none(g_doc, para, -1);
    /* NULL length: the buffer is unchanged. */
    memset(buffer, 'S', sizeof buffer);
    NAOS_CHECK_INT_EQ(amark_node_text(g_doc, para, AMARK_TEXT_LOGICAL,
                                      buffer, sizeof buffer, NULL),
                      AMARK_ERROR_NOT_AVAILABLE);
    for (i = 0; i < sizeof buffer; i++) {
        NAOS_CHECK_INT_EQ(buffer[i], 'S');
    }
    /* A removed node. */
    removed = find(AMARK_KIND_PARAGRAPH, 1);
    NAOS_REQUIRE_UINT_NE(removed, AMARK_NODE_NONE);
    amark_tree_remove(g_doc, removed);
    check_none(g_doc, removed, AMARK_TEXT_LOGICAL);
    check_text(para, AMARK_TEXT_LOGICAL, "para");

    /* A document without parse data (made with the internal API). */
    NAOS_REQUIRE_INT_EQ(amark_document_create(&allocator,
                                              (const unsigned char *)content,
                                              sizeof content - 1, 0,
                                              sizeof content - 1, NULL,
                                              &bare),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_append(bare, 1, AMARK_KIND_PARAGRAPH,
                                          AMARK_NODE_CLASS_AUTO, 0, 4,
                                          &para),
                        AMARK_ERROR_NONE);
    check_none(bare, para, AMARK_TEXT_LOGICAL);
    check_none(bare, 1, AMARK_TEXT_LOGICAL);
    amark_document_destroy(bare);
}

NAOS_TEST(results_that_fit_are_written_without_allocating)
{
    static const char content[] = "> a `b` [c](/d 'e')\n\n```f\ng\n```\n";
    size_t mallocs;
    size_t frees;
    char buffer[4];
    size_t length = 99;
    amark_node_id_t span;

    parse_bytes(content, sizeof content - 1);
    mallocs = g_state.malloc_calls;
    frees = g_state.free_calls;
    check_text(find(AMARK_KIND_PARAGRAPH, 0), AMARK_TEXT_LOGICAL,
               "a `b` [c](/d 'e')");
    check_text(find(AMARK_KIND_LINK, 0), AMARK_TEXT_DESTINATION, "/d");
    check_text(find(AMARK_KIND_CODE_BLOCK, 0), AMARK_TEXT_VALUE, "g\n");
    check_text(find(AMARK_KIND_CODE_BLOCK, 0), AMARK_TEXT_INFO, "f");
    /* A buffer of exactly the length; nothing past it is written. */
    span = find(AMARK_KIND_CODE_SPAN, 0);
    memset(buffer, 'S', sizeof buffer);
    NAOS_REQUIRE_INT_EQ(amark_node_text(g_doc, span, AMARK_TEXT_LOGICAL,
                                        buffer, 3, &length),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(length, 3);
    NAOS_CHECK_MEM_EQ(buffer, (size_t)4, "`b`S", (size_t)4);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, mallocs);
    NAOS_CHECK_UINT_EQ(g_state.free_calls, frees);
}

NAOS_SUITE(text_table,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(logical_of_non_container_blocks_and_inlines),
    NAOS_ADD_TEST(logical_of_every_inline_kind_is_its_range),
    NAOS_ADD_TEST(logical_of_containers_is_not_available),
    NAOS_ADD_TEST(dialect_kinds_answer_the_logical_text_of_inlines_only),
    NAOS_ADD_TEST(value_of_text_escape_reference_autolink),
    NAOS_ADD_TEST(value_of_code_span_strips_the_backtick_strings),
    NAOS_ADD_TEST(value_of_code_blocks_is_the_code_lines),
    NAOS_ADD_TEST(value_of_other_kinds_is_not_available),
    NAOS_ADD_TEST(destination_and_title_of_inline_links_and_images),
    NAOS_ADD_TEST(written_or_not_agrees_with_the_tree_readers),
    NAOS_ADD_TEST(reference_links_take_their_values_from_the_definition),
    NAOS_ADD_TEST(titles_start_on_the_line_of_their_range),
    NAOS_ADD_TEST(info_of_fenced_code_blocks_only),
    NAOS_ADD_TEST(text_calls_that_cannot_be_answered_change_nothing),
    NAOS_ADD_TEST(results_that_fit_are_written_without_allocating)
)

/* --- amark_node_text: passing the result --------------------------------- */

/*
 * Checks the passing of the result of part of node of g_doc, which is want
 * (want_length bytes, at least 1): a size-only call (NULL buffer, with
 * capacity 0 and with a nonzero capacity, which is taken as 0), a buffer one
 * byte too small (BUFFER_TOO_SMALL with the needed length; sentinel bytes
 * past the capacity are not written), a buffer of exactly the length
 * reported (succeeds and writes nothing past it), and a larger buffer. None
 * of the calls allocates or frees.
 */
static void check_passing(amark_node_id_t node, amark_text_part_t part,
                          const char *want, size_t want_length)
{
    char buffer[64];
    size_t length;
    size_t needed;
    size_t mallocs = g_state.malloc_calls;
    size_t frees = g_state.free_calls;
    size_t i;

    NAOS_REQUIRE_UINT_NE(node, AMARK_NODE_NONE);
    NAOS_REQUIRE_TRUE(want_length >= 1 && want_length < sizeof buffer);

    /* Size only. */
    length = 12345;
    NAOS_CHECK_INT_EQ(amark_node_text(g_doc, node, part, NULL, 0, &length),
                      AMARK_ERROR_BUFFER_TOO_SMALL);
    NAOS_CHECK_UINT_EQ(length, want_length);
    length = 12345;
    NAOS_CHECK_INT_EQ(amark_node_text(g_doc, node, part, NULL,
                                      sizeof buffer, &length),
                      AMARK_ERROR_BUFFER_TOO_SMALL);
    NAOS_CHECK_UINT_EQ(length, want_length);
    needed = length;

    /* One byte too small: nothing at or past the capacity is written. */
    memset(buffer, 'S', sizeof buffer);
    length = 12345;
    NAOS_CHECK_INT_EQ(amark_node_text(g_doc, node, part, buffer, needed - 1,
                                      &length),
                      AMARK_ERROR_BUFFER_TOO_SMALL);
    NAOS_CHECK_UINT_EQ(length, want_length);
    for (i = needed - 1; i < sizeof buffer; i++) {
        NAOS_CHECK_INT_EQ(buffer[i], 'S');
    }

    /* Calling again with the size reported succeeds, writing no more. */
    memset(buffer, 'S', sizeof buffer);
    length = 12345;
    NAOS_CHECK_INT_EQ(amark_node_text(g_doc, node, part, buffer, needed,
                                      &length),
                      AMARK_ERROR_NONE);
    NAOS_CHECK_MEM_EQ(buffer, length, want, want_length);
    for (i = needed; i < sizeof buffer; i++) {
        NAOS_CHECK_INT_EQ(buffer[i], 'S');
    }

    /* A larger buffer. */
    memset(buffer, 'S', sizeof buffer);
    length = 12345;
    NAOS_CHECK_INT_EQ(amark_node_text(g_doc, node, part, buffer,
                                      sizeof buffer, &length),
                      AMARK_ERROR_NONE);
    NAOS_CHECK_MEM_EQ(buffer, length, want, want_length);
    NAOS_CHECK_INT_EQ(buffer[want_length], 'S');

    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, mallocs);
    NAOS_CHECK_UINT_EQ(g_state.free_calls, frees);
}

NAOS_TEST(size_only_too_small_and_exact_buffers)
{
    /* U+0000 and ill-formed bytes that become U+FFFD (3 bytes each; in the
       second paragraph the too-small buffer ends inside one), a decoded
       reference, and code lines that gain a '\n'. */
    static const char content[] =
        "a\0b\xFF" "c &amp; d\n"
        "\n"
        "ab\xFF\n"
        "\n"
        "    code";
    amark_node_id_t para;
    amark_node_id_t code;

    g_config.utf8_mode = AMARK_UTF8_REPLACE;
    parse_bytes(content, sizeof content - 1);
    para = find(AMARK_KIND_PARAGRAPH, 0);
    code = find(AMARK_KIND_CODE_BLOCK, 0);
    check_passing(para, AMARK_TEXT_LOGICAL,
                  "a\xEF\xBF\xBD" "b\xEF\xBF\xBD" "c &amp; d", 17);
    check_passing(find(AMARK_KIND_CHARACTER_REFERENCE, 0), AMARK_TEXT_VALUE,
                  "&", 1);
    check_passing(find(AMARK_KIND_PARAGRAPH, 1), AMARK_TEXT_LOGICAL,
                  "ab\xEF\xBF\xBD", 5);
    check_passing(code, AMARK_TEXT_VALUE, "code\n", 5);
}

NAOS_TEST(results_of_length_zero_succeed)
{
    static const char content[] = "[a](<> \"\")\n\n```\n```\n";
    amark_node_id_t link;
    amark_node_id_t code;
    char buffer[4];
    size_t length;
    size_t mallocs;

    parse_bytes(content, sizeof content - 1);
    link = find(AMARK_KIND_LINK, 0);
    code = find(AMARK_KIND_CODE_BLOCK, 0);
    NAOS_REQUIRE_UINT_NE(link, AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_NE(code, AMARK_NODE_NONE);
    mallocs = g_state.malloc_calls;

    length = 12345;
    NAOS_CHECK_INT_EQ(amark_node_text(g_doc, link, AMARK_TEXT_DESTINATION,
                                      NULL, 0, &length),
                      AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(length, 0);
    length = 12345;
    memset(buffer, 'S', sizeof buffer);
    NAOS_CHECK_INT_EQ(amark_node_text(g_doc, link, AMARK_TEXT_TITLE,
                                      buffer, 0, &length),
                      AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(length, 0);
    NAOS_CHECK_INT_EQ(buffer[0], 'S');
    length = 12345;
    NAOS_CHECK_INT_EQ(amark_node_text(g_doc, code, AMARK_TEXT_VALUE,
                                      buffer, sizeof buffer, &length),
                      AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(length, 0);
    NAOS_CHECK_INT_EQ(buffer[0], 'S');
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, mallocs);
}

/*
 * The largest range length that is read: (SIZE_MAX - 2) / 3 (design, "the
 * length limit").
 */
#define RANGE_LIMIT ((SIZE_MAX - 2) / 3)

/*
 * Checks that part of node of g_doc is AMARK_ERROR_LIMIT_EXCEEDED, with
 * nothing written: the buffer and *length are unchanged, also for a
 * size-only call, and nothing is allocated.
 */
static void check_limit(amark_node_id_t node, amark_text_part_t part)
{
    char buffer[16];
    size_t length = 12345;
    size_t mallocs = g_state.malloc_calls;
    size_t i;

    memset(buffer, 'S', sizeof buffer);
    NAOS_CHECK_INT_EQ(amark_node_text(g_doc, node, part, buffer,
                                      sizeof buffer, &length),
                      AMARK_ERROR_LIMIT_EXCEEDED);
    NAOS_CHECK_UINT_EQ(length, 12345);
    for (i = 0; i < sizeof buffer; i++) {
        NAOS_CHECK_INT_EQ(buffer[i], 'S');
    }
    NAOS_CHECK_INT_EQ(amark_node_text(g_doc, node, part, NULL, 0, &length),
                      AMARK_ERROR_LIMIT_EXCEEDED);
    NAOS_CHECK_UINT_EQ(length, 12345);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, mallocs);
}

/*
 * A range longer than the limit is refused before anything is read, so a
 * range that is made that long by hand in a small document (its end far
 * past the content) is never read. The ranges are put back before the
 * document is destroyed.
 */
NAOS_TEST(ranges_longer_than_the_limit_are_refused_before_writing)
{
    static const char content[] = "a [b](/c)\n\n    code\n";
    amark_node_t *text;
    amark_node_t *code;
    amark_node_attr_t *attr;
    amark_node_t *link;
    size_t text_end;
    size_t code_end;
    size_t dest_end;

    parse_bytes(content, sizeof content - 1);
    text = (amark_node_t *)amark_array_at(&g_doc->nodes,
                                          find(AMARK_KIND_TEXT, 0));
    code = (amark_node_t *)amark_array_at(&g_doc->nodes,
                                          find(AMARK_KIND_CODE_BLOCK, 0));
    link = (amark_node_t *)amark_array_at(&g_doc->nodes,
                                          find(AMARK_KIND_LINK, 0));
    attr = (amark_node_attr_t *)amark_array_at(&g_doc->attrs, link->attr);
    text_end = text->end;
    code_end = code->end;
    dest_end = attr->link.destination.end;

    /* One past the limit: refused, for the plain range, the code lines
       (bounded by the node's range) and an attribute's range. */
    text->end = text->start + RANGE_LIMIT + 1;
    code->end = code->start + RANGE_LIMIT + 1;
    attr->link.destination.end = attr->link.destination.start +
                                 RANGE_LIMIT + 1;
    check_limit(find(AMARK_KIND_TEXT, 0), AMARK_TEXT_VALUE);
    check_limit(find(AMARK_KIND_TEXT, 0), AMARK_TEXT_LOGICAL);
    check_limit(find(AMARK_KIND_CODE_BLOCK, 0), AMARK_TEXT_VALUE);
    check_limit(find(AMARK_KIND_LINK, 0), AMARK_TEXT_DESTINATION);
    /* The largest length representable. */
    text->end = SIZE_MAX;
    check_limit(find(AMARK_KIND_TEXT, 0), AMARK_TEXT_VALUE);

    text->end = text_end;
    code->end = code_end;
    attr->link.destination.end = dest_end;
    check_text(find(AMARK_KIND_TEXT, 0), AMARK_TEXT_VALUE, "a ");
    check_text(find(AMARK_KIND_CODE_BLOCK, 0), AMARK_TEXT_VALUE, "code\n");
    check_text(find(AMARK_KIND_LINK, 0), AMARK_TEXT_DESTINATION, "/c");
}

NAOS_SUITE(text_passing,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(size_only_too_small_and_exact_buffers),
    NAOS_ADD_TEST(results_of_length_zero_succeed),
    NAOS_ADD_TEST(ranges_longer_than_the_limit_are_refused_before_writing)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(text_raw),
    NAOS_ADD_SUITE(text_table),
    NAOS_ADD_SUITE(text_passing)
)
