/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Public API test of the text of a node: links Amark::amark and uses only
 * <amark/amark.h>. Parses a short document with no configuration, visits
 * its tree with the traversal of the comment in <amark/tree.h> and, for
 * every node, reads its source bytes with amark_node_raw and every part of
 * it both ways: with amark_node_text (a size-only call, a buffer one byte
 * too small, then a buffer of the exact length) and with
 * amark_node_text_alloc and amark_text_free. The two ways must agree on
 * which parts are available and on their values, and the values of the
 * document, written as one string, must be those expected. In the shared
 * configuration this links against the exported symbols only, so it shows
 * that the four functions of <amark/text.h> are exported.
 */

#include <amark/amark.h>

#include <naos/naos.h>

#include <stddef.h>
#include <string.h>

/*
 * A heading with emphasis, a block quote holding a paragraph with an
 * escape, a character reference and a code span, a fenced code block with
 * an info string and a paragraph holding an inline link with a title.
 */
static const char k_content[] =
    "# Head *em*\n"
    "\n"
    "> a \\* &amp; `x`\n"
    "\n"
    "```c\n"
    "code\n"
    "```\n"
    "\n"
    "[l](/u \"t\")\n";

/*
 * The available parts of every node in document order, written as
 * "kind:part=value" lines (kinds and parts by number, the value as it is).
 */
static const char k_expected[] =
    "6:1=# Head *em*\n"
    "32:1=Head \n"
    "32:2=Head \n"
    "34:1=*em*\n"
    "32:1=em\n"
    "32:2=em\n"
    "9:1=a \\* &amp; `x`\n"
    "32:1=a \n"
    "32:2=a \n"
    "42:1=\\*\n"
    "42:2=*\n"
    "32:1= \n"
    "32:2= \n"
    "43:1=&amp;\n"
    "43:2=&\n"
    "32:1= \n"
    "32:2= \n"
    "33:1=`x`\n"
    "33:2=x\n"
    "7:1=```c\ncode\n```\n"
    "7:2=code\n\n"
    "7:5=c\n"
    "9:1=[l](/u \"t\")\n"
    "36:1=[l](/u \"t\")\n"
    "36:3=/u\n"
    "36:4=t\n"
    "32:1=l\n"
    "32:2=l\n";

#define PART_FIRST AMARK_TEXT_LOGICAL
#define PART_LAST AMARK_TEXT_INFO

/* Large enough for every value of k_content and the dump of them all. */
#define VALUE_CAPACITY 64
#define DUMP_CAPACITY 1024

/* A byte the functions never write. */
#define FILL 0x5A

/*
 * NAOS_REQUIRE_* leaves a test with longjmp, so the document is kept in a
 * static variable and teardown destroys it.
 */
static amark_document_t *g_doc;

NAOS_SETUP(document)
{
    g_doc = NULL;
}

NAOS_TEARDOWN(document)
{
    amark_document_destroy(g_doc);
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

/* Returns the node after node in document order, or AMARK_NODE_NONE. */
static amark_node_id_t next_in_order(amark_node_id_t root,
                                     amark_node_id_t node)
{
    amark_node_id_t next = amark_node_first_child(g_doc, node);

    while (next == AMARK_NODE_NONE && node != root) {
        next = amark_node_next_sibling(g_doc, node);
        if (next == AMARK_NODE_NONE) {
            node = amark_node_parent(g_doc, node);
        }
    }
    return next;
}

/* Appends length bytes at bytes to the dump, which must hold them. */
static void dump_append(char *dump, size_t *used, const char *bytes,
                        size_t length)
{
    NAOS_REQUIRE_UINT_LE(length, DUMP_CAPACITY - *used);
    if (length > 0) {
        memcpy(dump + *used, bytes, length);
    }
    *used += length;
}

/* Appends the decimal digits of value to the dump. */
static void dump_number(char *dump, size_t *used, unsigned long value)
{
    char digits[24];
    size_t count = 0;

    do {
        digits[sizeof(digits) - 1 - count] = (char)('0' + value % 10);
        value /= 10;
        count++;
    } while (value != 0);
    dump_append(dump, used, digits + sizeof(digits) - count, count);
}

/*
 * Reads part of node both ways. Returns 0 if the part is not available
 * (both ways must say so, leaving their outputs unchanged); otherwise
 * checks that the two ways agree, copies the value to value and its length
 * to *length, and returns 1.
 */
static int read_part(amark_node_id_t node, amark_text_part_t part,
                     char *value, size_t *length)
{
    char buffer[VALUE_CAPACITY];
    size_t size = (size_t)-1;
    size_t written = (size_t)-1;
    size_t alloc_length = (size_t)-1;
    char *text = NULL;
    amark_error_t error;
    amark_error_t alloc_error;

    /* The buffer way, first a size-only call. */
    error = amark_node_text(g_doc, node, part, NULL, 0, &size);
    alloc_error = amark_node_text_alloc(g_doc, node, part, &text,
                                        &alloc_length);
    if (error == AMARK_ERROR_NOT_AVAILABLE) {
        NAOS_CHECK_INT_EQ(alloc_error, AMARK_ERROR_NOT_AVAILABLE);
        NAOS_CHECK_UINT_EQ(size, (size_t)-1);
        NAOS_CHECK_NULL(text);
        NAOS_CHECK_UINT_EQ(alloc_length, (size_t)-1);
        amark_text_free(g_doc, text);
        return 0;
    }
    NAOS_CHECK_INT_EQ(error, size == 0 ? AMARK_ERROR_NONE
                                       : AMARK_ERROR_BUFFER_TOO_SMALL);
    NAOS_REQUIRE_UINT_LT(size, VALUE_CAPACITY);

    /* A buffer one byte too small: nothing written past its capacity. */
    if (size > 0) {
        memset(buffer, FILL, sizeof(buffer));
        written = (size_t)-1;
        NAOS_CHECK_INT_EQ(amark_node_text(g_doc, node, part, buffer,
                                          size - 1, &written),
                          AMARK_ERROR_BUFFER_TOO_SMALL);
        NAOS_CHECK_UINT_EQ(written, size);
        NAOS_CHECK_INT_EQ(buffer[size - 1], FILL);
    }

    /* A buffer of the exact length. */
    memset(buffer, FILL, sizeof(buffer));
    written = (size_t)-1;
    NAOS_CHECK_INT_EQ(amark_node_text(g_doc, node, part, buffer, size,
                                      &written),
                      AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(written, size);
    NAOS_CHECK_INT_EQ(buffer[size], FILL);

    /* The alloc way: the same bytes, then a NUL. */
    NAOS_CHECK_INT_EQ(alloc_error, AMARK_ERROR_NONE);
    if (alloc_error == AMARK_ERROR_NONE && text != NULL) {
        NAOS_CHECK_UINT_EQ(alloc_length, size);
        NAOS_CHECK_UINT_EQ(strlen(text), size);
        NAOS_CHECK_MEM_EQ(text, size, buffer, size);
        amark_text_free(g_doc, text);
    } else {
        NAOS_CHECK_NOT_NULL(text);
    }

    memcpy(value, buffer, size);
    *length = size;
    return 1;
}

NAOS_TEST(raw_bytes_of_every_node)
{
    amark_node_id_t root;
    amark_node_id_t node;
    size_t visits = 0;

    parse_content();
    root = amark_document_root(g_doc);
    NAOS_REQUIRE_UINT_NE(root, AMARK_NODE_NONE);
    for (node = root; node != AMARK_NODE_NONE;
         node = next_in_order(root, node)) {
        amark_range_t range = {0, 0};
        const char *bytes = NULL;
        size_t length = (size_t)-1;

        NAOS_TRACE_PUSH("node %lu", (unsigned long)visits);
        NAOS_REQUIRE_TRUE(amark_node_range(g_doc, node, &range));
        NAOS_REQUIRE_TRUE(amark_node_raw(g_doc, node, &bytes, &length));
        NAOS_CHECK_PTR_EQ(bytes, k_content + range.start);
        NAOS_CHECK_UINT_EQ(length, range.end - range.start);
        NAOS_TRACE_POP();
        visits++;
    }
    NAOS_CHECK_UINT_GT(visits, 1);
}

NAOS_TEST(raw_bytes_of_the_root_are_the_content)
{
    const char *bytes = NULL;
    size_t length = 0;

    parse_content();
    NAOS_REQUIRE_TRUE(amark_node_raw(g_doc, amark_document_root(g_doc),
                                     &bytes, &length));
    NAOS_CHECK_MEM_EQ(bytes, length, k_content, sizeof(k_content) - 1);
    NAOS_CHECK_FALSE(amark_node_raw(g_doc, AMARK_NODE_NONE, &bytes,
                                    &length));
}

NAOS_TEST(every_part_of_every_node_both_ways)
{
    char dump[DUMP_CAPACITY];
    size_t used = 0;
    amark_node_id_t root;
    amark_node_id_t node;
    size_t visits = 0;

    parse_content();
    root = amark_document_root(g_doc);
    NAOS_REQUIRE_UINT_NE(root, AMARK_NODE_NONE);
    for (node = root; node != AMARK_NODE_NONE;
         node = next_in_order(root, node)) {
        int part;

        NAOS_TRACE_PUSH("node %lu", (unsigned long)visits);
        for (part = PART_FIRST; part <= PART_LAST; part++) {
            char value[VALUE_CAPACITY];
            size_t length = 0;

            NAOS_TRACE_PUSH("part %d", part);
            if (read_part(node, (amark_text_part_t)part, value, &length)) {
                dump_number(dump, &used,
                            (unsigned long)amark_node_kind(g_doc, node));
                dump_append(dump, &used, ":", 1);
                dump_number(dump, &used, (unsigned long)part);
                dump_append(dump, &used, "=", 1);
                dump_append(dump, &used, value, length);
                dump_append(dump, &used, "\n", 1);
            }
            NAOS_TRACE_POP();
        }
        NAOS_TRACE_POP();
        visits++;
    }
    dump_append(dump, &used, "", 1);
    NAOS_CHECK_STR_EQ(dump, k_expected);
}

NAOS_TEST(a_part_that_is_not_written_is_not_available)
{
    amark_node_id_t root;
    char buffer[4] = {FILL, FILL, FILL, FILL};
    size_t length = (size_t)-1;
    char *text = NULL;

    parse_content();
    root = amark_document_root(g_doc);
    /* The root is a container: it has no logical text. */
    NAOS_CHECK_INT_EQ(amark_node_text(g_doc, root, AMARK_TEXT_LOGICAL,
                                      buffer, sizeof(buffer), &length),
                      AMARK_ERROR_NOT_AVAILABLE);
    NAOS_CHECK_UINT_EQ(length, (size_t)-1);
    NAOS_CHECK_INT_EQ(buffer[0], FILL);
    NAOS_CHECK_INT_EQ(amark_node_text_alloc(g_doc, root, AMARK_TEXT_LOGICAL,
                                            &text, &length),
                      AMARK_ERROR_NOT_AVAILABLE);
    NAOS_CHECK_NULL(text);
    NAOS_CHECK_UINT_EQ(length, (size_t)-1);
    /* Freeing NULL does nothing. */
    amark_text_free(g_doc, NULL);
}

NAOS_SUITE(text,
    NAOS_SET_SETUP(document),
    NAOS_SET_TEARDOWN(document),
    NAOS_ADD_TEST(raw_bytes_of_every_node),
    NAOS_ADD_TEST(raw_bytes_of_the_root_are_the_content),
    NAOS_ADD_TEST(every_part_of_every_node_both_ways),
    NAOS_ADD_TEST(a_part_that_is_not_written_is_not_available)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(text)
)
