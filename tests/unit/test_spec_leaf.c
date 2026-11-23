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
 * Sections: Tabs, Thematic breaks, ATX headings, Setext headings and
 * Indented code blocks.
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

NAOS_SUITE(spec_leaf_blocks,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(spec_tabs),
    NAOS_ADD_TEST(spec_thematic_breaks),
    NAOS_ADD_TEST(spec_atx_headings),
    NAOS_ADD_TEST(spec_setext_headings),
    NAOS_ADD_TEST(spec_indented_code_blocks)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(spec_leaf_blocks)
)
