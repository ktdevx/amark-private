/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * The examples of the CommonMark specification for leaf blocks, checked
 * through the values of their nodes: each example's input is parsed with the
 * default configuration (and the counting test allocator), and the values,
 * destinations, titles and info strings of its nodes, as
 * amark_test_dump_text writes them, must equal the expected string of the
 * table.
 *
 * The inputs below are taken from the CommonMark Spec, version 0.31.2,
 * by John MacFarlane, https://spec.commonmark.org/0.31.2/ (spec.txt), which
 * is licensed under the Creative Commons Attribution-ShareAlike 4.0
 * International License (CC BY-SA 4.0,
 * https://creativecommons.org/licenses/by-sa/4.0/). The expected strings are
 * derived from the HTML that the specification gives for each example, with
 * the HTML escapes and the percent-encoding of URLs undone. Each entry
 * carries the number of its example in the specification (counted from the
 * start of spec.txt); tabs are written as "\t" where the specification
 * shows a right arrow, and other bytes outside ASCII as octal escapes of
 * their UTF-8 encoding.
 *
 * Link reference definitions do not appear in the HTML of the
 * specification; their destinations and titles are those that the links
 * referring to them get in the HTML (the links of a reference form have
 * none of their own).
 *
 * Sections: Tabs, Indented code blocks, Fenced code blocks and Link
 * reference definitions.
 */

#include "test_alloc.h"
#include "text_dump.h"

#include <amark/amark.h>

#include <naos/naos.h>

#include <stdbool.h>
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
 * Parses each example and compares the dump of its values with the expected
 * string. The example number is put before both sides of each comparison so
 * that a failure names the example.
 */
static void check_examples(const spec_example_t *examples, size_t count)
{
    size_t i;

    for (i = 0; i < count; i++) {
        const spec_example_t *ex = &examples[i];
        amark_config_t config;
        char dump[DUMP_SIZE];
        char got[DUMP_SIZE + 64];
        char want[DUMP_SIZE + 64];

        amark_config_init(&config);
        config.allocator = g_allocator;
        (void)snprintf(got, sizeof(got), "%d: error %d", ex->number,
                       (int)amark_parse(ex->input, strlen(ex->input),
                                        &config, &g_doc));
        (void)snprintf(want, sizeof(want), "%d: error %d", ex->number,
                       (int)AMARK_ERROR_NONE);
        NAOS_REQUIRE_STR_EQ(got, want);

        NAOS_REQUIRE_TRUE(amark_test_dump_text(g_doc, dump, sizeof(dump)));
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

/* The dump itself: escaping, the order of the parts, a buffer too small. */
NAOS_TEST(text_dump_format)
{
    static const char input[] =
        "```ruby\n\"\\\x01\x7f\t\r\n```\n\n[a]: /u 'b\"'\n";
    static const char expected[] =
        "code:value=\"\\\"\\\\\\x01\\x7F\\t\\n\" code:info=\"ruby\""
        " def:dest=\"/u\" def:title=\"b\\\"\"";
    amark_config_t config;
    char dump[DUMP_SIZE];
    char small[8];

    amark_config_init(&config);
    config.allocator = g_allocator;
    NAOS_REQUIRE_INT_EQ(
        amark_parse(input, sizeof(input) - 1, &config, &g_doc),
        AMARK_ERROR_NONE);

    NAOS_REQUIRE_TRUE(amark_test_dump_text(g_doc, dump, sizeof(dump)));
    NAOS_CHECK_STR_EQ(dump, expected);

    /* Exactly the size needed, and one byte less. */
    NAOS_CHECK_TRUE(
        amark_test_dump_text(g_doc, dump, sizeof(expected)));
    NAOS_CHECK_STR_EQ(dump, expected);
    NAOS_CHECK_FALSE(
        amark_test_dump_text(g_doc, dump, sizeof(expected) - 1));
    NAOS_CHECK_UINT_EQ(strlen(dump), sizeof(expected) - 2);

    NAOS_CHECK_FALSE(amark_test_dump_text(g_doc, small, sizeof(small)));
    NAOS_CHECK_STR_EQ(small, "code:va");
    NAOS_CHECK_FALSE(amark_test_dump_text(g_doc, NULL, 0));

    NAOS_CHECK_TRUE(amark_test_dump_text(NULL, dump, sizeof(dump)));
    NAOS_CHECK_STR_EQ(dump, "");
}

/* Tabs (examples 1-11). */
NAOS_TEST(spec_tabs)
{
    static const spec_example_t examples[] = {
        {1,
         "\tfoo\tbaz\t\tbim\n",
         "code:value=\"foo\\tbaz\\t\\tbim\\n\""},
        {2,
         "  \tfoo\tbaz\t\tbim\n",
         "code:value=\"foo\\tbaz\\t\\tbim\\n\""},
        {3,
         "    a\ta\n    \341\275\220\ta\n",
         "code:value=\"a\\ta\\n\341\275\220\\ta\\n\""},
        {4,
         "  - foo\n\n\tbar\n",
         "text:value=\"foo\" text:value=\"bar\""},
        {5,
         "- foo\n\n\t\tbar\n",
         "text:value=\"foo\" code:value=\"  bar\\n\""},
        {6,
         ">\t\tfoo\n",
         "code:value=\"  foo\\n\""},
        {7,
         "-\t\tfoo\n",
         "code:value=\"  foo\\n\""},
        {8,
         "    foo\n\tbar\n",
         "code:value=\"foo\\nbar\\n\""},
        {9,
         " - foo\n   - bar\n\t - baz\n",
         "text:value=\"foo\" text:value=\"bar\" text:value=\"baz\""},
        {10,
         "#\tFoo\n",
         "text:value=\"Foo\""},
        {11,
         "*\t*\t*\t\n",
         ""},
    };

    CHECK_EXAMPLES(examples);
}

/* Indented code blocks (examples 107-118). */
NAOS_TEST(spec_indented_code_blocks)
{
    static const spec_example_t examples[] = {
        {107,
         "    a simple\n      indented code block\n",
         "code:value=\"a simple\\n  indented code block\\n\""},
        {108,
         "  - foo\n\n    bar\n",
         "text:value=\"foo\" text:value=\"bar\""},
        {109,
         "1.  foo\n\n    - bar\n",
         "text:value=\"foo\" text:value=\"bar\""},
        {110,
         "    <a/>\n    *hi*\n\n    - one\n",
         "code:value=\"<a/>\\n*hi*\\n\\n- one\\n\""},
        {111,
         "    chunk1\n\n    chunk2\n  \n \n \n    chunk3\n",
         "code:value=\"chunk1\\n\\nchunk2\\n\\n\\n\\nchunk3\\n\""},
        {112,
         "    chunk1\n      \n      chunk2\n",
         "code:value=\"chunk1\\n  \\n  chunk2\\n\""},
        {113,
         "Foo\n    bar\n\n",
         "text:value=\"Foo\" text:value=\"bar\""},
        {114,
         "    foo\nbar\n",
         "code:value=\"foo\\n\" text:value=\"bar\""},
        {115,
         "# Heading\n    foo\nHeading\n------\n    foo\n----\n",
         "text:value=\"Heading\" code:value=\"foo\\n\""
         " text:value=\"Heading\" code:value=\"foo\\n\""},
        {116,
         "        foo\n    bar\n",
         "code:value=\"    foo\\nbar\\n\""},
        {117,
         "\n    \n    foo\n    \n\n",
         "code:value=\"foo\\n\""},
        {118,
         "    foo  \n",
         "code:value=\"foo  \\n\""},
    };

    CHECK_EXAMPLES(examples);
}

/* Fenced code blocks (examples 119-147). */
NAOS_TEST(spec_fenced_code_blocks)
{
    static const spec_example_t examples[] = {
        {119,
         "```\n<\n >\n```\n",
         "code:value=\"<\\n >\\n\""},
        {120,
         "~~~\n<\n >\n~~~\n",
         "code:value=\"<\\n >\\n\""},
        {121,
         "``\nfoo\n``\n",
         "code:value=\"foo\""},
        {122,
         "```\naaa\n~~~\n```\n",
         "code:value=\"aaa\\n~~~\\n\""},
        {123,
         "~~~\naaa\n```\n~~~\n",
         "code:value=\"aaa\\n```\\n\""},
        {124,
         "````\naaa\n```\n``````\n",
         "code:value=\"aaa\\n```\\n\""},
        {125,
         "~~~~\naaa\n~~~\n~~~~\n",
         "code:value=\"aaa\\n~~~\\n\""},
        {126,
         "```\n",
         "code:value=\"\""},
        {127,
         "`````\n\n```\naaa\n",
         "code:value=\"\\n```\\naaa\\n\""},
        {128,
         "> ```\n> aaa\n\nbbb\n",
         "code:value=\"aaa\\n\" text:value=\"bbb\""},
        {129,
         "```\n\n  \n```\n",
         "code:value=\"\\n  \\n\""},
        {130,
         "```\n```\n",
         "code:value=\"\""},
        {131,
         " ```\n aaa\naaa\n```\n",
         "code:value=\"aaa\\naaa\\n\""},
        {132,
         "  ```\naaa\n  aaa\naaa\n  ```\n",
         "code:value=\"aaa\\naaa\\naaa\\n\""},
        {133,
         "   ```\n   aaa\n    aaa\n  aaa\n   ```\n",
         "code:value=\"aaa\\n aaa\\naaa\\n\""},
        {134,
         "    ```\n    aaa\n    ```\n",
         "code:value=\"```\\naaa\\n```\\n\""},
        {135,
         "```\naaa\n  ```\n",
         "code:value=\"aaa\\n\""},
        {136,
         "   ```\naaa\n  ```\n",
         "code:value=\"aaa\\n\""},
        {137,
         "```\naaa\n    ```\n",
         "code:value=\"aaa\\n    ```\\n\""},
        {138,
         "``` ```\naaa\n",
         "code:value=\" \" text:value=\"aaa\""},
        {139,
         "~~~~~~\naaa\n~~~ ~~\n",
         "code:value=\"aaa\\n~~~ ~~\\n\""},
        {140,
         "foo\n```\nbar\n```\nbaz\n",
         "text:value=\"foo\" code:value=\"bar\\n\" text:value=\"baz\""},
        {141,
         "foo\n---\n~~~\nbar\n~~~\n# baz\n",
         "text:value=\"foo\" code:value=\"bar\\n\" text:value=\"baz\""},
        {142,
         "```ruby\ndef foo(x)\n  return 3\nend\n```\n",
         "code:value=\"def foo(x)\\n  return 3\\nend\\n\""
         " code:info=\"ruby\""},
        {143,
         "~~~~    ruby startline=3 $%@#$\ndef foo(x)\n  return 3\nend\n"
         "~~~~~~~\n",
         "code:value=\"def foo(x)\\n  return 3\\nend\\n\""
         " code:info=\"ruby startline=3 $%@#$\""},
        {144,
         "````;\n````\n",
         "code:value=\"\" code:info=\";\""},
        {145,
         "``` aa ```\nfoo\n",
         "code:value=\"aa\" text:value=\"foo\""},
        {146,
         "~~~ aa ``` ~~~\nfoo\n~~~\n",
         "code:value=\"foo\\n\" code:info=\"aa ``` ~~~\""},
        {147,
         "```\n``` aaa\n```\n",
         "code:value=\"``` aaa\\n\""},
    };

    CHECK_EXAMPLES(examples);
}

/* Link reference definitions (examples 192-218). */
NAOS_TEST(spec_link_reference_definitions)
{
    static const spec_example_t examples[] = {
        {192,
         "[foo]: /url \"title\"\n\n[foo]\n",
         "def:dest=\"/url\" def:title=\"title\" text:value=\"foo\""},
        {193,
         "   [foo]: \n      /url  \n           'the title'  \n\n[foo]\n",
         "def:dest=\"/url\" def:title=\"the title\" text:value=\"foo\""},
        {194,
         "[Foo*bar\\]]:my_(url) 'title (with parens)'\n\n[Foo*bar\\]]\n",
         "def:dest=\"my_(url)\" def:title=\"title (with parens)\""
         " text:value=\"Foo*bar\" esc:value=\"]\""},
        {195,
         "[Foo bar]:\n<my url>\n'title'\n\n[Foo bar]\n",
         "def:dest=\"my url\" def:title=\"title\" text:value=\"Foo bar\""},
        {196,
         "[foo]: /url '\ntitle\nline1\nline2\n'\n\n[foo]\n",
         "def:dest=\"/url\" def:title=\"\\ntitle\\nline1\\nline2\\n\""
         " text:value=\"foo\""},
        {197,
         "[foo]: /url 'title\n\nwith blank line'\n\n[foo]\n",
         "text:value=\"[foo]: /url 'title\" text:value=\"with blank line'\""
         " text:value=\"[foo]\""},
        {198,
         "[foo]:\n/url\n\n[foo]\n",
         "def:dest=\"/url\" text:value=\"foo\""},
        {199,
         "[foo]:\n\n[foo]\n",
         "text:value=\"[foo]:\" text:value=\"[foo]\""},
        {200,
         "[foo]: <>\n\n[foo]\n",
         "def:dest=\"\" text:value=\"foo\""},
        {201,
         "[foo]: <bar>(baz)\n\n[foo]\n",
         "text:value=\"[foo]: \" text:value=\"(baz)\" text:value=\"[foo]\""},
        {202,
         "[foo]: /url\\bar\\*baz \"foo\\\"bar\\baz\"\n\n[foo]\n",
         "def:dest=\"/url\\\\bar*baz\" def:title=\"foo\\\"bar\\\\baz\""
         " text:value=\"foo\""},
        {203,
         "[foo]\n\n[foo]: url\n",
         "text:value=\"foo\" def:dest=\"url\""},
        {204,
         "[foo]\n\n[foo]: first\n[foo]: second\n",
         "text:value=\"foo\" def:dest=\"first\" def:dest=\"second\""},
        {205,
         "[FOO]: /url\n\n[Foo]\n",
         "def:dest=\"/url\" text:value=\"Foo\""},
        {206,
         "[\316\221\316\223\316\251]: /\317\206\316\277\317\205\n\n"
         "[\316\261\316\263\317\211]\n",
         "def:dest=\"/\317\206\316\277\317\205\""
         " text:value=\"\316\261\316\263\317\211\""},
        {207,
         "[foo]: /url\n",
         "def:dest=\"/url\""},
        {208,
         "[\nfoo\n]: /url\nbar\n",
         "def:dest=\"/url\" text:value=\"bar\""},
        {209,
         "[foo]: /url \"title\" ok\n",
         "text:value=\"[foo]: /url \\\"title\\\" ok\""},
        {210,
         "[foo]: /url\n\"title\" ok\n",
         "def:dest=\"/url\" text:value=\"\\\"title\\\" ok\""},
        {211,
         "    [foo]: /url \"title\"\n\n[foo]\n",
         "code:value=\"[foo]: /url \\\"title\\\"\\n\" text:value=\"[foo]\""},
        {212,
         "```\n[foo]: /url\n```\n\n[foo]\n",
         "code:value=\"[foo]: /url\\n\" text:value=\"[foo]\""},
        {213,
         "Foo\n[bar]: /baz\n\n[bar]\n",
         "text:value=\"Foo\" text:value=\"[bar]: /baz\" text:value=\"[bar]\""},
        {214,
         "# [Foo]\n[foo]: /url\n> bar\n",
         "text:value=\"Foo\" def:dest=\"/url\" text:value=\"bar\""},
        {215,
         "[foo]: /url\nbar\n===\n[foo]\n",
         "def:dest=\"/url\" text:value=\"bar\" text:value=\"foo\""},
        {216,
         "[foo]: /url\n===\n[foo]\n",
         "def:dest=\"/url\" text:value=\"===\" text:value=\"foo\""},
        {217,
         "[foo]: /foo-url \"foo\"\n[bar]: /bar-url\n  \"bar\"\n"
         "[baz]: /baz-url\n\n[foo],\n[bar],\n[baz]\n",
         "def:dest=\"/foo-url\" def:title=\"foo\" def:dest=\"/bar-url\""
         " def:title=\"bar\" def:dest=\"/baz-url\" text:value=\"foo\""
         " text:value=\",\" text:value=\"bar\" text:value=\",\""
         " text:value=\"baz\""},
        {218,
         "[foo]\n\n> [foo]: /url\n",
         "text:value=\"foo\" def:dest=\"/url\""},
    };

    CHECK_EXAMPLES(examples);
}

NAOS_SUITE(text_dump,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(text_dump_format)
)

NAOS_SUITE(spec_text_leaf,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(spec_tabs),
    NAOS_ADD_TEST(spec_indented_code_blocks),
    NAOS_ADD_TEST(spec_fenced_code_blocks),
    NAOS_ADD_TEST(spec_link_reference_definitions)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(text_dump),
    NAOS_ADD_SUITE(spec_text_leaf)
)
