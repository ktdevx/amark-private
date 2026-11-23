/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * The examples of the CommonMark specification for container blocks, checked
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
 * Sections: Block quotes, List items and Lists. Whether a list is tight or
 * loose is read from the HTML (whether "<p>" wraps the content of its
 * items); the bullet character, the delimiter and the start number of a
 * list, the style of headings and code blocks, and the "(def)" nodes are
 * decided from the input by the rules of the specification.
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

/* Block quotes (examples 228-252). */
NAOS_TEST(spec_block_quotes)
{
    static const spec_example_t examples[] = {
        {228,
         "> # Foo\n> bar\n> baz\n",
         "(doc (bq (h 1 atx) (p)))"},
        {229,
         "># Foo\n>bar\n> baz\n",
         "(doc (bq (h 1 atx) (p)))"},
        {230,
         "   > # Foo\n   > bar\n > baz\n",
         "(doc (bq (h 1 atx) (p)))"},
        {231,
         "    > # Foo\n    > bar\n    > baz\n",
         "(doc (code indented))"},
        {232,
         "> # Foo\n> bar\nbaz\n",
         "(doc (bq (h 1 atx) (p)))"},
        {233,
         "> bar\nbaz\n> foo\n",
         "(doc (bq (p)))"},
        {234,
         "> foo\n---\n",
         "(doc (bq (p)) (hr))"},
        {235,
         "> - foo\n- bar\n",
         "(doc (bq (ul - tight (li (p)))) (ul - tight (li (p))))"},
        {236,
         ">     foo\n    bar\n",
         "(doc (bq (code indented)) (code indented))"},
        {237,
         "> ```\nfoo\n```\n",
         "(doc (bq (code fenced ` 3)) (p) (code fenced ` 3))"},
        {238,
         "> foo\n    - bar\n",
         "(doc (bq (p)))"},
        {239,
         ">\n",
         "(doc (bq))"},
        {240,
         ">\n>  \n> \n",
         "(doc (bq))"},
        {241,
         ">\n> foo\n>  \n",
         "(doc (bq (p)))"},
        {242,
         "> foo\n\n> bar\n",
         "(doc (bq (p)) (bq (p)))"},
        {243,
         "> foo\n> bar\n",
         "(doc (bq (p)))"},
        {244,
         "> foo\n>\n> bar\n",
         "(doc (bq (p) (p)))"},
        {245,
         "foo\n> bar\n",
         "(doc (p) (bq (p)))"},
        {246,
         "> aaa\n***\n> bbb\n",
         "(doc (bq (p)) (hr) (bq (p)))"},
        {247,
         "> bar\nbaz\n",
         "(doc (bq (p)))"},
        {248,
         "> bar\n\nbaz\n",
         "(doc (bq (p)) (p))"},
        {249,
         "> bar\n>\nbaz\n",
         "(doc (bq (p)) (p))"},
        {250,
         "> > > foo\nbar\n",
         "(doc (bq (bq (bq (p)))))"},
        {251,
         ">>> foo\n> bar\n>>baz\n",
         "(doc (bq (bq (bq (p)))))"},
        {252,
         ">     code\n\n>    not code\n",
         "(doc (bq (code indented)) (bq (p)))"},
    };

    CHECK_EXAMPLES(examples);
}

/* List items (examples 253-300). */
NAOS_TEST(spec_list_items)
{
    static const spec_example_t examples[] = {
        {253,
         "A paragraph\nwith two lines.\n\n    indented code\n\n"
         "> A block quote.\n",
         "(doc (p) (code indented) (bq (p)))"},
        {254,
         "1.  A paragraph\n    with two lines.\n\n        indented code\n\n"
         "    > A block quote.\n",
         "(doc (ol 1 . loose (li (p) (code indented) (bq (p)))))"},
        {255,
         "- one\n\n two\n",
         "(doc (ul - tight (li (p))) (p))"},
        {256,
         "- one\n\n  two\n",
         "(doc (ul - loose (li (p) (p))))"},
        {257,
         " -    one\n\n     two\n",
         "(doc (ul - tight (li (p))) (code indented))"},
        {258,
         " -    one\n\n      two\n",
         "(doc (ul - loose (li (p) (p))))"},
        {259,
         "   > > 1.  one\n>>\n>>     two\n",
         "(doc (bq (bq (ol 1 . loose (li (p) (p))))))"},
        {260,
         ">>- one\n>>\n  >  > two\n",
         "(doc (bq (bq (ul - tight (li (p))) (p))))"},
        {261,
         "-one\n\n2.two\n",
         "(doc (p) (p))"},
        {262,
         "- foo\n\n\n  bar\n",
         "(doc (ul - loose (li (p) (p))))"},
        {263,
         "1.  foo\n\n    ```\n    bar\n    ```\n\n    baz\n\n    > bam\n",
         "(doc (ol 1 . loose (li (p) (code fenced ` 3) (p) (bq (p)))))"},
        {264,
         "- Foo\n\n      bar\n\n\n      baz\n",
         "(doc (ul - loose (li (p) (code indented))))"},
        {265,
         "123456789. ok\n",
         "(doc (ol 123456789 . tight (li (p))))"},
        {266,
         "1234567890. not ok\n",
         "(doc (p))"},
        {267,
         "0. ok\n",
         "(doc (ol 0 . tight (li (p))))"},
        {268,
         "003. ok\n",
         "(doc (ol 3 . tight (li (p))))"},
        {269,
         "-1. not ok\n",
         "(doc (p))"},
        {270,
         "- foo\n\n      bar\n",
         "(doc (ul - loose (li (p) (code indented))))"},
        {271,
         "  10.  foo\n\n           bar\n",
         "(doc (ol 10 . loose (li (p) (code indented))))"},
        {272,
         "    indented code\n\nparagraph\n\n    more code\n",
         "(doc (code indented) (p) (code indented))"},
        {273,
         "1.     indented code\n\n   paragraph\n\n       more code\n",
         "(doc (ol 1 . loose (li (code indented) (p) (code indented))))"},
        {274,
         "1.      indented code\n\n   paragraph\n\n       more code\n",
         "(doc (ol 1 . loose (li (code indented) (p) (code indented))))"},
        {275,
         "   foo\n\nbar\n",
         "(doc (p) (p))"},
        {276,
         "-    foo\n\n  bar\n",
         "(doc (ul - tight (li (p))) (p))"},
        {277,
         "-  foo\n\n   bar\n",
         "(doc (ul - loose (li (p) (p))))"},
        {278,
         "-\n  foo\n-\n  ```\n  bar\n  ```\n-\n      baz\n",
         "(doc (ul - tight (li (p)) (li (code fenced ` 3)) (li"
         " (code indented))))"},
        {279,
         "-   \n  foo\n",
         "(doc (ul - tight (li (p))))"},
        {280,
         "-\n\n  foo\n",
         "(doc (ul - tight (li)) (p))"},
        {281,
         "- foo\n-\n- bar\n",
         "(doc (ul - tight (li (p)) (li) (li (p))))"},
        {282,
         "- foo\n-   \n- bar\n",
         "(doc (ul - tight (li (p)) (li) (li (p))))"},
        {283,
         "1. foo\n2.\n3. bar\n",
         "(doc (ol 1 . tight (li (p)) (li) (li (p))))"},
        {284,
         "*\n",
         "(doc (ul * tight (li)))"},
        {285,
         "foo\n*\n\nfoo\n1.\n",
         "(doc (p) (p))"},
        {286,
         " 1.  A paragraph\n     with two lines.\n\n"
         "         indented code\n\n     > A block quote.\n",
         "(doc (ol 1 . loose (li (p) (code indented) (bq (p)))))"},
        {287,
         "  1.  A paragraph\n      with two lines.\n\n"
         "          indented code\n\n      > A block quote.\n",
         "(doc (ol 1 . loose (li (p) (code indented) (bq (p)))))"},
        {288,
         "   1.  A paragraph\n       with two lines.\n\n"
         "           indented code\n\n       > A block quote.\n",
         "(doc (ol 1 . loose (li (p) (code indented) (bq (p)))))"},
        {289,
         "    1.  A paragraph\n        with two lines.\n\n"
         "            indented code\n\n        > A block quote.\n",
         "(doc (code indented))"},
        {290,
         "  1.  A paragraph\nwith two lines.\n\n          indented code\n\n"
         "      > A block quote.\n",
         "(doc (ol 1 . loose (li (p) (code indented) (bq (p)))))"},
        {291,
         "  1.  A paragraph\n    with two lines.\n",
         "(doc (ol 1 . tight (li (p))))"},
        {292,
         "> 1. > Blockquote\ncontinued here.\n",
         "(doc (bq (ol 1 . tight (li (bq (p))))))"},
        {293,
         "> 1. > Blockquote\n> continued here.\n",
         "(doc (bq (ol 1 . tight (li (bq (p))))))"},
        {294,
         "- foo\n  - bar\n    - baz\n      - boo\n",
         "(doc (ul - tight (li (p) (ul - tight (li (p) (ul - tight (li"
         " (p) (ul - tight (li (p))))))))))"},
        {295,
         "- foo\n - bar\n  - baz\n   - boo\n",
         "(doc (ul - tight (li (p)) (li (p)) (li (p)) (li (p))))"},
        {296,
         "10) foo\n    - bar\n",
         "(doc (ol 10 ) tight (li (p) (ul - tight (li (p))))))"},
        {297,
         "10) foo\n   - bar\n",
         "(doc (ol 10 ) tight (li (p))) (ul - tight (li (p))))"},
        {298,
         "- - foo\n",
         "(doc (ul - tight (li (ul - tight (li (p))))))"},
        {299,
         "1. - 2. foo\n",
         "(doc (ol 1 . tight (li (ul - tight (li (ol 2 . tight (li"
         " (p))))))))"},
        {300,
         "- # Foo\n- Bar\n  ---\n  baz\n",
         "(doc (ul - tight (li (h 1 atx)) (li (h 2 setext) (p))))"},
    };

    CHECK_EXAMPLES(examples);
}

/* Lists (examples 301-326). */
NAOS_TEST(spec_lists)
{
    static const spec_example_t examples[] = {
        {301,
         "- foo\n- bar\n+ baz\n",
         "(doc (ul - tight (li (p)) (li (p))) (ul + tight (li (p))))"},
        {302,
         "1. foo\n2. bar\n3) baz\n",
         "(doc (ol 1 . tight (li (p)) (li (p))) (ol 3 ) tight (li (p))))"},
        {303,
         "Foo\n- bar\n- baz\n",
         "(doc (p) (ul - tight (li (p)) (li (p))))"},
        {304,
         "The number of windows in my house is\n"
         "14.  The number of doors is 6.\n",
         "(doc (p))"},
        {305,
         "The number of windows in my house is\n"
         "1.  The number of doors is 6.\n",
         "(doc (p) (ol 1 . tight (li (p))))"},
        {306,
         "- foo\n\n- bar\n\n\n- baz\n",
         "(doc (ul - loose (li (p)) (li (p)) (li (p))))"},
        {307,
         "- foo\n  - bar\n    - baz\n\n\n      bim\n",
         "(doc (ul - tight (li (p) (ul - tight (li (p) (ul - loose (li"
         " (p) (p))))))))"},
        {308,
         "- foo\n- bar\n\n<!-- -->\n\n- baz\n- bim\n",
         "(doc (ul - tight (li (p)) (li (p))) (html) (ul - tight (li (p))"
         " (li (p))))"},
        {309,
         "-   foo\n\n    notcode\n\n-   foo\n\n<!-- -->\n\n    code\n",
         "(doc (ul - loose (li (p) (p)) (li (p))) (html) (code indented))"},
        {310,
         "- a\n - b\n  - c\n   - d\n  - e\n - f\n- g\n",
         "(doc (ul - tight (li (p)) (li (p)) (li (p)) (li (p)) (li (p))"
         " (li (p)) (li (p))))"},
        {311,
         "1. a\n\n  2. b\n\n   3. c\n",
         "(doc (ol 1 . loose (li (p)) (li (p)) (li (p))))"},
        {312,
         "- a\n - b\n  - c\n   - d\n    - e\n",
         "(doc (ul - tight (li (p)) (li (p)) (li (p)) (li (p))))"},
        {313,
         "1. a\n\n  2. b\n\n    3. c\n",
         "(doc (ol 1 . loose (li (p)) (li (p))) (code indented))"},
        {314,
         "- a\n- b\n\n- c\n",
         "(doc (ul - loose (li (p)) (li (p)) (li (p))))"},
        {315,
         "* a\n*\n\n* c\n",
         "(doc (ul * loose (li (p)) (li) (li (p))))"},
        {316,
         "- a\n- b\n\n  c\n- d\n",
         "(doc (ul - loose (li (p)) (li (p) (p)) (li (p))))"},
        {317,
         "- a\n- b\n\n  [ref]: /url\n- d\n",
         "(doc (ul - loose (li (p)) (li (p) (def)) (li (p))))"},
        {318,
         "- a\n- ```\n  b\n\n\n  ```\n- c\n",
         "(doc (ul - tight (li (p)) (li (code fenced ` 3)) (li (p))))"},
        {319,
         "- a\n  - b\n\n    c\n- d\n",
         "(doc (ul - tight (li (p) (ul - loose (li (p) (p)))) (li (p))))"},
        {320,
         "* a\n  > b\n  >\n* c\n",
         "(doc (ul * tight (li (p) (bq (p))) (li (p))))"},
        {321,
         "- a\n  > b\n  ```\n  c\n  ```\n- d\n",
         "(doc (ul - tight (li (p) (bq (p)) (code fenced ` 3)) (li (p))))"},
        {322,
         "- a\n",
         "(doc (ul - tight (li (p))))"},
        {323,
         "- a\n  - b\n",
         "(doc (ul - tight (li (p) (ul - tight (li (p))))))"},
        {324,
         "1. ```\n   foo\n   ```\n\n   bar\n",
         "(doc (ol 1 . loose (li (code fenced ` 3) (p))))"},
        {325,
         "* foo\n  * bar\n\n  baz\n",
         "(doc (ul * loose (li (p) (ul * tight (li (p))) (p))))"},
        {326,
         "- a\n  - b\n  - c\n\n- d\n  - e\n  - f\n",
         "(doc (ul - loose (li (p) (ul - tight (li (p)) (li (p)))) (li"
         " (p) (ul - tight (li (p)) (li (p))))))"},
    };

    CHECK_EXAMPLES(examples);
}

NAOS_SUITE(spec_container_blocks,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(spec_block_quotes),
    NAOS_ADD_TEST(spec_list_items),
    NAOS_ADD_TEST(spec_lists)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(spec_container_blocks)
)
