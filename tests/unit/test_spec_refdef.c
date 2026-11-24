/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * The examples of the CommonMark specification for link reference
 * definitions, checked through amark_parse: each example's input is parsed
 * with the default configuration (and the counting test allocator), the
 * tree must pass amark_tree_check, and its block structure, as
 * amark_test_dump_blocks writes it, must equal the expected string of the
 * table.
 *
 * The inputs below are taken from the CommonMark Spec, version 0.31.2,
 * by John MacFarlane, https://spec.commonmark.org/0.31.2/ (spec.txt), which
 * is licensed under the Creative Commons Attribution-ShareAlike 4.0
 * International License (CC BY-SA 4.0,
 * https://creativecommons.org/licenses/by-sa/4.0/). The expected strings are
 * derived from the HTML that the specification gives for each example. Each
 * entry carries the number of its example in the specification (counted from
 * the start of spec.txt); tabs are written as "\t" where the specification
 * shows a right arrow, and other bytes outside ASCII as octal escapes of
 * their UTF-8 encoding.
 *
 * Section: Link reference definitions. Definitions do not appear in the HTML
 * of the specification; which leading lines of a paragraph form definitions
 * ("(def)" nodes, placed where the input writes them) is decided from the
 * input by the rules of the specification.
 */

#include "test_alloc.h"
#include "tree_check.h"
#include "tree_dump.h"

#include <amark/amark.h>

#include <naos/naos.h>

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define DUMP_SIZE 1024

typedef struct spec_example {
    int number;
    const char *input;
    const char *dump;
} spec_example_t;

/*
 * Fixture. NAOS_REQUIRE_* leaves a test with longjmp, so the document is kept
 * in a static variable, and teardown destroys it and checks that no block is
 * left and that every call received its context value.
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

/*
 * Parses each example, checks the tree and compares its dump with the
 * expected string. The example number is put before both sides of each
 * comparison so that a failure names the example.
 */
static void check_examples(const spec_example_t *examples, size_t count)
{
    size_t i;

    for (i = 0; i < count; i++) {
        const spec_example_t *ex = &examples[i];
        amark_config_t config;
        amark_tree_check_result_t check;
        char dump[DUMP_SIZE];
        char got[DUMP_SIZE + 64];
        char want[DUMP_SIZE + 64];
        size_t length;

        amark_config_init(&config);
        config.allocator = g_allocator;
        (void)snprintf(got, sizeof(got), "%d: error %d", ex->number,
                       (int)amark_parse(ex->input, strlen(ex->input),
                                        &config, &g_doc));
        (void)snprintf(want, sizeof(want), "%d: error %d", ex->number,
                       (int)AMARK_ERROR_NONE);
        NAOS_REQUIRE_STR_EQ(got, want);

        check = amark_tree_check(g_doc);
        (void)snprintf(got, sizeof(got), "%d: violation %d at %lu",
                       ex->number, (int)check.violation,
                       (unsigned long)check.node);
        (void)snprintf(want, sizeof(want), "%d: violation %d at %lu",
                       ex->number, (int)AMARK_TREE_VIOLATION_NONE,
                       (unsigned long)AMARK_NODE_NONE);
        NAOS_CHECK_STR_EQ(got, want);

        length = amark_test_dump_blocks(g_doc, dump, sizeof(dump));
        NAOS_REQUIRE_UINT_LT(length, sizeof(dump));
        (void)snprintf(got, sizeof(got), "%d: %s", ex->number, dump);
        (void)snprintf(want, sizeof(want), "%d: %s", ex->number, ex->dump);
        NAOS_CHECK_STR_EQ(got, want);

        amark_document_destroy(g_doc);
        g_doc = NULL;
        NAOS_CHECK_UINT_EQ(g_state.live, 0);
    }
}

#define CHECK_EXAMPLES(table) \
    check_examples((table), sizeof(table) / sizeof((table)[0]))

/* Link reference definitions (examples 192-218). */
NAOS_TEST(spec_link_reference_definitions)
{
    static const spec_example_t examples[] = {
        {192,
         "[foo]: /url \"title\"\n\n[foo]\n",
         "(doc (def) (p))"},
        {193,
         "   [foo]: \n      /url  \n           'the title'  \n\n[foo]\n",
         "(doc (def) (p))"},
        {194,
         "[Foo*bar\\]]:my_(url) 'title (with parens)'\n\n[Foo*bar\\]]\n",
         "(doc (def) (p))"},
        {195,
         "[Foo bar]:\n<my url>\n'title'\n\n[Foo bar]\n",
         "(doc (def) (p))"},
        {196,
         "[foo]: /url '\ntitle\nline1\nline2\n'\n\n[foo]\n",
         "(doc (def) (p))"},
        {197,
         "[foo]: /url 'title\n\nwith blank line'\n\n[foo]\n",
         "(doc (p) (p) (p))"},
        {198,
         "[foo]:\n/url\n\n[foo]\n",
         "(doc (def) (p))"},
        {199,
         "[foo]:\n\n[foo]\n",
         "(doc (p) (p))"},
        {200,
         "[foo]: <>\n\n[foo]\n",
         "(doc (def) (p))"},
        {201,
         "[foo]: <bar>(baz)\n\n[foo]\n",
         "(doc (p) (p))"},
        {202,
         "[foo]: /url\\bar\\*baz \"foo\\\"bar\\baz\"\n\n[foo]\n",
         "(doc (def) (p))"},
        {203,
         "[foo]\n\n[foo]: url\n",
         "(doc (p) (def))"},
        {204,
         "[foo]\n\n[foo]: first\n[foo]: second\n",
         "(doc (p) (def) (def))"},
        {205,
         "[FOO]: /url\n\n[Foo]\n",
         "(doc (def) (p))"},
        {206,
         "[\316\221\316\223\316\251]: /\317\206\316\277\317\205\n\n"
         "[\316\261\316\263\317\211]\n",
         "(doc (def) (p))"},
        {207,
         "[foo]: /url\n",
         "(doc (def))"},
        {208,
         "[\nfoo\n]: /url\nbar\n",
         "(doc (def) (p))"},
        {209,
         "[foo]: /url \"title\" ok\n",
         "(doc (p))"},
        {210,
         "[foo]: /url\n\"title\" ok\n",
         "(doc (def) (p))"},
        {211,
         "    [foo]: /url \"title\"\n\n[foo]\n",
         "(doc (code indented) (p))"},
        {212,
         "```\n[foo]: /url\n```\n\n[foo]\n",
         "(doc (code fenced ` 3) (p))"},
        {213,
         "Foo\n[bar]: /baz\n\n[bar]\n",
         "(doc (p) (p))"},
        {214,
         "# [Foo]\n[foo]: /url\n> bar\n",
         "(doc (h 1 atx) (def) (bq (p)))"},
        {215,
         "[foo]: /url\nbar\n===\n[foo]\n",
         "(doc (def) (h 1 setext) (p))"},
        {216,
         "[foo]: /url\n===\n[foo]\n",
         "(doc (def) (p))"},
        {217,
         "[foo]: /foo-url \"foo\"\n[bar]: /bar-url\n  \"bar\"\n"
         "[baz]: /baz-url\n\n[foo],\n[bar],\n[baz]\n",
         "(doc (def) (def) (def) (p))"},
        {218,
         "[foo]\n\n> [foo]: /url\n",
         "(doc (p) (bq (def)))"},
    };

    CHECK_EXAMPLES(examples);
}

NAOS_SUITE(spec_link_reference_definitions,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(spec_link_reference_definitions)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(spec_link_reference_definitions)
)
