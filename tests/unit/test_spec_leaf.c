/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * The examples of the CommonMark specification for leaf blocks, checked
 * through amark_parse: each example's input is parsed with the default
 * configuration (and the counting test allocator), the tree must pass
 * amark_tree_check, and its block structure, as amark_test_dump_blocks
 * writes it, must equal the expected string of the table.
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
 * Sections: Tabs, Thematic breaks, ATX headings, Setext headings, Indented
 * code blocks, Fenced code blocks, HTML blocks, Paragraphs and Blank lines.
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

/* Tabs (examples 1-11). */
NAOS_TEST(spec_tabs)
{
    static const spec_example_t examples[] = {
        {1,
         "\tfoo\tbaz\t\tbim\n",
         "(doc (code indented))"},
        {2,
         "  \tfoo\tbaz\t\tbim\n",
         "(doc (code indented))"},
        {3,
         "    a\ta\n    \341\275\220\ta\n",
         "(doc (code indented))"},
        {4,
         "  - foo\n\n\tbar\n",
         "(doc (ul - loose (li (p) (p))))"},
        {5,
         "- foo\n\n\t\tbar\n",
         "(doc (ul - loose (li (p) (code indented))))"},
        {6,
         ">\t\tfoo\n",
         "(doc (bq (code indented)))"},
        {7,
         "-\t\tfoo\n",
         "(doc (ul - tight (li (code indented))))"},
        {8,
         "    foo\n\tbar\n",
         "(doc (code indented))"},
        {9,
         " - foo\n   - bar\n\t - baz\n",
         "(doc (ul - tight (li (p) (ul - tight (li (p) (ul - tight (li"
         " (p))))))))"},
        {10,
         "#\tFoo\n",
         "(doc (h 1 atx))"},
        {11,
         "*\t*\t*\t\n",
         "(doc (hr))"},
    };

    CHECK_EXAMPLES(examples);
}

/* Thematic breaks (examples 43-61). */
NAOS_TEST(spec_thematic_breaks)
{
    static const spec_example_t examples[] = {
        {43,
         "***\n---\n___\n",
         "(doc (hr) (hr) (hr))"},
        {44,
         "+++\n",
         "(doc (p))"},
        {45,
         "===\n",
         "(doc (p))"},
        {46,
         "--\n**\n__\n",
         "(doc (p))"},
        {47,
         " ***\n  ***\n   ***\n",
         "(doc (hr) (hr) (hr))"},
        {48,
         "    ***\n",
         "(doc (code indented))"},
        {49,
         "Foo\n    ***\n",
         "(doc (p))"},
        {50,
         "_____________________________________\n",
         "(doc (hr))"},
        {51,
         " - - -\n",
         "(doc (hr))"},
        {52,
         " **  * ** * ** * **\n",
         "(doc (hr))"},
        {53,
         "-     -      -      -\n",
         "(doc (hr))"},
        {54,
         "- - - -    \n",
         "(doc (hr))"},
        {55,
         "_ _ _ _ a\n\na------\n\n---a---\n",
         "(doc (p) (p) (p))"},
        {56,
         " *-*\n",
         "(doc (p))"},
        {57,
         "- foo\n***\n- bar\n",
         "(doc (ul - tight (li (p))) (hr) (ul - tight (li (p))))"},
        {58,
         "Foo\n***\nbar\n",
         "(doc (p) (hr) (p))"},
        {59,
         "Foo\n---\nbar\n",
         "(doc (h 2 setext) (p))"},
        {60,
         "* Foo\n* * *\n* Bar\n",
         "(doc (ul * tight (li (p))) (hr) (ul * tight (li (p))))"},
        {61,
         "- Foo\n- * * *\n",
         "(doc (ul - tight (li (p)) (li (hr))))"},
    };

    CHECK_EXAMPLES(examples);
}

/* ATX headings (examples 62-79). */
NAOS_TEST(spec_atx_headings)
{
    static const spec_example_t examples[] = {
        {62,
         "# foo\n## foo\n### foo\n#### foo\n##### foo\n###### foo\n",
         "(doc (h 1 atx) (h 2 atx) (h 3 atx) (h 4 atx) (h 5 atx) (h 6 atx))"},
        {63,
         "####### foo\n",
         "(doc (p))"},
        {64,
         "#5 bolt\n\n#hashtag\n",
         "(doc (p) (p))"},
        {65,
         "\\## foo\n",
         "(doc (p))"},
        {66,
         "# foo *bar* \\*baz\\*\n",
         "(doc (h 1 atx))"},
        {67,
         "#                  foo                     \n",
         "(doc (h 1 atx))"},
        {68,
         " ### foo\n  ## foo\n   # foo\n",
         "(doc (h 3 atx) (h 2 atx) (h 1 atx))"},
        {69,
         "    # foo\n",
         "(doc (code indented))"},
        {70,
         "foo\n    # bar\n",
         "(doc (p))"},
        {71,
         "## foo ##\n  ###   bar    ###\n",
         "(doc (h 2 atx) (h 3 atx))"},
        {72,
         "# foo ##################################\n##### foo ##\n",
         "(doc (h 1 atx) (h 5 atx))"},
        {73,
         "### foo ###     \n",
         "(doc (h 3 atx))"},
        {74,
         "### foo ### b\n",
         "(doc (h 3 atx))"},
        {75,
         "# foo#\n",
         "(doc (h 1 atx))"},
        {76,
         "### foo \\###\n## foo #\\##\n# foo \\#\n",
         "(doc (h 3 atx) (h 2 atx) (h 1 atx))"},
        {77,
         "****\n## foo\n****\n",
         "(doc (hr) (h 2 atx) (hr))"},
        {78,
         "Foo bar\n# baz\nBar foo\n",
         "(doc (p) (h 1 atx) (p))"},
        {79,
         "## \n#\n### ###\n",
         "(doc (h 2 atx) (h 1 atx) (h 3 atx))"},
    };

    CHECK_EXAMPLES(examples);
}

/* Setext headings (examples 80-106). */
NAOS_TEST(spec_setext_headings)
{
    static const spec_example_t examples[] = {
        {80,
         "Foo *bar*\n=========\n\nFoo *bar*\n---------\n",
         "(doc (h 1 setext) (h 2 setext))"},
        {81,
         "Foo *bar\nbaz*\n====\n",
         "(doc (h 1 setext))"},
        {82,
         "  Foo *bar\nbaz*\t\n====\n",
         "(doc (h 1 setext))"},
        {83,
         "Foo\n-------------------------\n\nFoo\n=\n",
         "(doc (h 2 setext) (h 1 setext))"},
        {84,
         "   Foo\n---\n\n  Foo\n-----\n\n  Foo\n  ===\n",
         "(doc (h 2 setext) (h 2 setext) (h 1 setext))"},
        {85,
         "    Foo\n    ---\n\n    Foo\n---\n",
         "(doc (code indented) (hr))"},
        {86,
         "Foo\n   ----      \n",
         "(doc (h 2 setext))"},
        {87,
         "Foo\n    ---\n",
         "(doc (p))"},
        {88,
         "Foo\n= =\n\nFoo\n--- -\n",
         "(doc (p) (p) (hr))"},
        {89,
         "Foo  \n-----\n",
         "(doc (h 2 setext))"},
        {90,
         "Foo\\\n----\n",
         "(doc (h 2 setext))"},
        {91,
         "`Foo\n----\n`\n\n<a title=\"a lot\n---\nof dashes\"/>\n",
         "(doc (h 2 setext) (p) (h 2 setext) (p))"},
        {92,
         "> Foo\n---\n",
         "(doc (bq (p)) (hr))"},
        {93,
         "> foo\nbar\n===\n",
         "(doc (bq (p)))"},
        {94,
         "- Foo\n---\n",
         "(doc (ul - tight (li (p))) (hr))"},
        {95,
         "Foo\nBar\n---\n",
         "(doc (h 2 setext))"},
        {96,
         "---\nFoo\n---\nBar\n---\nBaz\n",
         "(doc (hr) (h 2 setext) (h 2 setext) (p))"},
        {97,
         "\n====\n",
         "(doc (p))"},
        {98,
         "---\n---\n",
         "(doc (hr) (hr))"},
        {99,
         "- foo\n-----\n",
         "(doc (ul - tight (li (p))) (hr))"},
        {100,
         "    foo\n---\n",
         "(doc (code indented) (hr))"},
        {101,
         "> foo\n-----\n",
         "(doc (bq (p)) (hr))"},
        {102,
         "\\> foo\n------\n",
         "(doc (h 2 setext))"},
        {103,
         "Foo\n\nbar\n---\nbaz\n",
         "(doc (p) (h 2 setext) (p))"},
        {104,
         "Foo\nbar\n\n---\n\nbaz\n",
         "(doc (p) (hr) (p))"},
        {105,
         "Foo\nbar\n* * *\nbaz\n",
         "(doc (p) (hr) (p))"},
        {106,
         "Foo\nbar\n\\---\nbaz\n",
         "(doc (p))"},
    };

    CHECK_EXAMPLES(examples);
}

/* Indented code blocks (examples 107-118). */
NAOS_TEST(spec_indented_code_blocks)
{
    static const spec_example_t examples[] = {
        {107,
         "    a simple\n      indented code block\n",
         "(doc (code indented))"},
        {108,
         "  - foo\n\n    bar\n",
         "(doc (ul - loose (li (p) (p))))"},
        {109,
         "1.  foo\n\n    - bar\n",
         "(doc (ol 1 . loose (li (p) (ul - tight (li (p))))))"},
        {110,
         "    <a/>\n    *hi*\n\n    - one\n",
         "(doc (code indented))"},
        {111,
         "    chunk1\n\n    chunk2\n  \n \n \n    chunk3\n",
         "(doc (code indented))"},
        {112,
         "    chunk1\n      \n      chunk2\n",
         "(doc (code indented))"},
        {113,
         "Foo\n    bar\n\n",
         "(doc (p))"},
        {114,
         "    foo\nbar\n",
         "(doc (code indented) (p))"},
        {115,
         "# Heading\n    foo\nHeading\n------\n    foo\n----\n",
         "(doc (h 1 atx) (code indented) (h 2 setext) (code indented) (hr))"},
        {116,
         "        foo\n    bar\n",
         "(doc (code indented))"},
        {117,
         "\n    \n    foo\n    \n\n",
         "(doc (code indented))"},
        {118,
         "    foo  \n",
         "(doc (code indented))"},
    };

    CHECK_EXAMPLES(examples);
}

/* Fenced code blocks (examples 119-147). */
NAOS_TEST(spec_fenced_code_blocks)
{
    static const spec_example_t examples[] = {
        {119,
         "```\n<\n >\n```\n",
         "(doc (code fenced ` 3))"},
        {120,
         "~~~\n<\n >\n~~~\n",
         "(doc (code fenced ~ 3))"},
        {121,
         "``\nfoo\n``\n",
         "(doc (p))"},
        {122,
         "```\naaa\n~~~\n```\n",
         "(doc (code fenced ` 3))"},
        {123,
         "~~~\naaa\n```\n~~~\n",
         "(doc (code fenced ~ 3))"},
        {124,
         "````\naaa\n```\n``````\n",
         "(doc (code fenced ` 4))"},
        {125,
         "~~~~\naaa\n~~~\n~~~~\n",
         "(doc (code fenced ~ 4))"},
        {126,
         "```\n",
         "(doc (code fenced ` 3))"},
        {127,
         "`````\n\n```\naaa\n",
         "(doc (code fenced ` 5))"},
        {128,
         "> ```\n> aaa\n\nbbb\n",
         "(doc (bq (code fenced ` 3)) (p))"},
        {129,
         "```\n\n  \n```\n",
         "(doc (code fenced ` 3))"},
        {130,
         "```\n```\n",
         "(doc (code fenced ` 3))"},
        {131,
         " ```\n aaa\naaa\n```\n",
         "(doc (code fenced ` 3))"},
        {132,
         "  ```\naaa\n  aaa\naaa\n  ```\n",
         "(doc (code fenced ` 3))"},
        {133,
         "   ```\n   aaa\n    aaa\n  aaa\n   ```\n",
         "(doc (code fenced ` 3))"},
        {134,
         "    ```\n    aaa\n    ```\n",
         "(doc (code indented))"},
        {135,
         "```\naaa\n  ```\n",
         "(doc (code fenced ` 3))"},
        {136,
         "   ```\naaa\n  ```\n",
         "(doc (code fenced ` 3))"},
        {137,
         "```\naaa\n    ```\n",
         "(doc (code fenced ` 3))"},
        {138,
         "``` ```\naaa\n",
         "(doc (p))"},
        {139,
         "~~~~~~\naaa\n~~~ ~~\n",
         "(doc (code fenced ~ 6))"},
        {140,
         "foo\n```\nbar\n```\nbaz\n",
         "(doc (p) (code fenced ` 3) (p))"},
        {141,
         "foo\n---\n~~~\nbar\n~~~\n# baz\n",
         "(doc (h 2 setext) (code fenced ~ 3) (h 1 atx))"},
        {142,
         "```ruby\ndef foo(x)\n  return 3\nend\n```\n",
         "(doc (code fenced ` 3 info))"},
        {143,
         "~~~~    ruby startline=3 $%@#$\ndef foo(x)\n  return 3\nend\n"
         "~~~~~~~\n",
         "(doc (code fenced ~ 4 info))"},
        {144,
         "````;\n````\n",
         "(doc (code fenced ` 4 info))"},
        {145,
         "``` aa ```\nfoo\n",
         "(doc (p))"},
        {146,
         "~~~ aa ``` ~~~\nfoo\n~~~\n",
         "(doc (code fenced ~ 3 info))"},
        {147,
         "```\n``` aaa\n```\n",
         "(doc (code fenced ` 3))"},
    };

    CHECK_EXAMPLES(examples);
}

/* HTML blocks (examples 148-191). */
NAOS_TEST(spec_html_blocks)
{
    static const spec_example_t examples[] = {
        {148,
         "<table><tr><td>\n<pre>\n**Hello**,\n\n_world_.\n</pre>\n"
         "</td></tr></table>\n",
         "(doc (html) (p) (html))"},
        {149,
         "<table>\n  <tr>\n    <td>\n           hi\n    </td>\n  </tr>\n"
         "</table>\n\nokay.\n",
         "(doc (html) (p))"},
        {150,
         " <div>\n  *hello*\n         <foo><a>\n",
         "(doc (html))"},
        {151,
         "</div>\n*foo*\n",
         "(doc (html))"},
        {152,
         "<DIV CLASS=\"foo\">\n\n*Markdown*\n\n</DIV>\n",
         "(doc (html) (p) (html))"},
        {153,
         "<div id=\"foo\"\n  class=\"bar\">\n</div>\n",
         "(doc (html))"},
        {154,
         "<div id=\"foo\" class=\"bar\n  baz\">\n</div>\n",
         "(doc (html))"},
        {155,
         "<div>\n*foo*\n\n*bar*\n",
         "(doc (html) (p))"},
        {156,
         "<div id=\"foo\"\n*hi*\n",
         "(doc (html))"},
        {157,
         "<div class\nfoo\n",
         "(doc (html))"},
        {158,
         "<div *?\?\?-&&&-<---\n*foo*\n",
         "(doc (html))"},
        {159,
         "<div><a href=\"bar\">*foo*</a></div>\n",
         "(doc (html))"},
        {160,
         "<table><tr><td>\nfoo\n</td></tr></table>\n",
         "(doc (html))"},
        {161,
         "<div></div>\n``` c\nint x = 33;\n```\n",
         "(doc (html))"},
        {162,
         "<a href=\"foo\">\n*bar*\n</a>\n",
         "(doc (html))"},
        {163,
         "<Warning>\n*bar*\n</Warning>\n",
         "(doc (html))"},
        {164,
         "<i class=\"foo\">\n*bar*\n</i>\n",
         "(doc (html))"},
        {165,
         "</ins>\n*bar*\n",
         "(doc (html))"},
        {166,
         "<del>\n*foo*\n</del>\n",
         "(doc (html))"},
        {167,
         "<del>\n\n*foo*\n\n</del>\n",
         "(doc (html) (p) (html))"},
        {168,
         "<del>*foo*</del>\n",
         "(doc (p))"},
        {169,
         "<pre language=\"haskell\"><code>\nimport Text.HTML.TagSoup\n\n"
         "main :: IO ()\nmain = print $ parseTags tags\n</code></pre>\n"
         "okay\n",
         "(doc (html) (p))"},
        {170,
         "<script type=\"text/javascript\">\n// JavaScript example\n\n"
         "document.getElementById(\"demo\").innerHTML = "
         "\"Hello JavaScript!\";\n</script>\nokay\n",
         "(doc (html) (p))"},
        {171,
         "<textarea>\n\n*foo*\n\n_bar_\n\n</textarea>\n",
         "(doc (html))"},
        {172,
         "<style\n  type=\"text/css\">\nh1 {color:red;}\n\n"
         "p {color:blue;}\n</style>\nokay\n",
         "(doc (html) (p))"},
        {173,
         "<style\n  type=\"text/css\">\n\nfoo\n",
         "(doc (html))"},
        {174,
         "> <div>\n> foo\n\nbar\n",
         "(doc (bq (html)) (p))"},
        {175,
         "- <div>\n- foo\n",
         "(doc (ul - tight (li (html)) (li (p))))"},
        {176,
         "<style>p{color:red;}</style>\n*foo*\n",
         "(doc (html) (p))"},
        {177,
         "<!-- foo -->*bar*\n*baz*\n",
         "(doc (html) (p))"},
        {178,
         "<script>\nfoo\n</script>1. *bar*\n",
         "(doc (html))"},
        {179,
         "<!-- Foo\n\nbar\n   baz -->\nokay\n",
         "(doc (html) (p))"},
        {180,
         "<?php\n\n  echo '>';\n\n?>\nokay\n",
         "(doc (html) (p))"},
        {181,
         "<!DOCTYPE html>\n",
         "(doc (html))"},
        {182,
         "<![CDATA[\nfunction matchwo(a,b)\n{\n"
         "  if (a < b && a < 0) then {\n    return 1;\n\n  } else {\n\n"
         "    return 0;\n  }\n}\n]]>\nokay\n",
         "(doc (html) (p))"},
        {183,
         "  <!-- foo -->\n\n    <!-- foo -->\n",
         "(doc (html) (code indented))"},
        {184,
         "  <div>\n\n    <div>\n",
         "(doc (html) (code indented))"},
        {185,
         "Foo\n<div>\nbar\n</div>\n",
         "(doc (p) (html))"},
        {186,
         "<div>\nbar\n</div>\n*foo*\n",
         "(doc (html))"},
        {187,
         "Foo\n<a href=\"bar\">\nbaz\n",
         "(doc (p))"},
        {188,
         "<div>\n\n*Emphasized* text.\n\n</div>\n",
         "(doc (html) (p) (html))"},
        {189,
         "<div>\n*Emphasized* text.\n</div>\n",
         "(doc (html))"},
        {190,
         "<table>\n\n<tr>\n\n<td>\nHi\n</td>\n\n</tr>\n\n</table>\n",
         "(doc (html) (html) (html) (html) (html))"},
        {191,
         "<table>\n\n  <tr>\n\n    <td>\n      Hi\n    </td>\n\n  </tr>\n\n"
         "</table>\n",
         "(doc (html) (html) (code indented) (html) (html))"},
    };

    CHECK_EXAMPLES(examples);
}

/* Paragraphs (examples 219-226). */
NAOS_TEST(spec_paragraphs)
{
    static const spec_example_t examples[] = {
        {219,
         "aaa\n\nbbb\n",
         "(doc (p) (p))"},
        {220,
         "aaa\nbbb\n\nccc\nddd\n",
         "(doc (p) (p))"},
        {221,
         "aaa\n\n\nbbb\n",
         "(doc (p) (p))"},
        {222,
         "  aaa\n bbb\n",
         "(doc (p))"},
        {223,
         "aaa\n             bbb\n"
         "                                       ccc\n",
         "(doc (p))"},
        {224,
         "   aaa\nbbb\n",
         "(doc (p))"},
        {225,
         "    aaa\nbbb\n",
         "(doc (code indented) (p))"},
        {226,
         "aaa     \nbbb     \n",
         "(doc (p))"},
    };

    CHECK_EXAMPLES(examples);
}

/* Blank lines (example 227). */
NAOS_TEST(spec_blank_lines)
{
    static const spec_example_t examples[] = {
        {227,
         "  \n\naaa\n  \n\n# aaa\n\n  \n",
         "(doc (p) (h 1 atx))"},
    };

    CHECK_EXAMPLES(examples);
}

NAOS_SUITE(spec_leaf_blocks,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(spec_tabs),
    NAOS_ADD_TEST(spec_thematic_breaks),
    NAOS_ADD_TEST(spec_atx_headings),
    NAOS_ADD_TEST(spec_setext_headings),
    NAOS_ADD_TEST(spec_indented_code_blocks),
    NAOS_ADD_TEST(spec_fenced_code_blocks),
    NAOS_ADD_TEST(spec_html_blocks),
    NAOS_ADD_TEST(spec_paragraphs),
    NAOS_ADD_TEST(spec_blank_lines)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(spec_leaf_blocks)
)
