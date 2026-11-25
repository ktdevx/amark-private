/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * The examples of the CommonMark specification for emphasis and strong
 * emphasis, checked through amark_parse: each example's input is parsed with
 * the default configuration (and the counting test allocator), the tree must
 * pass amark_tree_check, and the whole tree, blocks and inlines, as
 * amark_test_dump_tree writes it, must equal the expected string of the
 * table.
 *
 * The inputs below are taken from the CommonMark Spec, version 0.31.2,
 * by John MacFarlane, https://spec.commonmark.org/0.31.2/ (spec.txt), which
 * is licensed under the Creative Commons Attribution-ShareAlike 4.0
 * International License (CC BY-SA 4.0,
 * https://creativecommons.org/licenses/by-sa/4.0/). The expected strings are
 * derived from the HTML that the specification gives for each example. Each
 * entry carries the number of its example in the specification (counted from
 * the start of spec.txt); bytes outside ASCII are written as octal escapes of
 * their UTF-8 encoding.
 *
 * The tree keeps the source, so each node is written with the bytes of its
 * range (and a link with the bytes of its destination). The expected strings
 * give, for the structure that the HTML shows, the source bytes that each
 * node covers. An emphasis or a strong emphasis covers the delimiters it uses
 * (one or two on each side), which the dump does not write, and its children
 * lie between them. When only a part of a delimiter run is used, the opening
 * run is used from its right end and the closing run from its left end, and
 * the unused part is text. Adjacent texts on the same line are one node, so
 * an unused delimiter run is part of the text around it.
 *
 * Section: Emphasis and strong emphasis.
 */

#include "test_alloc.h"
#include "tree_check.h"
#include "tree_dump.h"

#include <amark/amark.h>

#include <naos/naos.h>

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define DUMP_SIZE 2048

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

        AMARK_CONFIG_INIT(&config);
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

        length = amark_test_dump_tree(g_doc, dump, sizeof(dump));
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

/* Emphasis and strong emphasis (examples 350-481). */
NAOS_TEST(spec_emphasis)
{
    static const spec_example_t examples[] = {
        {350,
         "*foo bar*\n",
         "(doc (p (em (text \"foo bar\"))))"},
        {351,
         "a * foo bar*\n",
         "(doc (p (text \"a * foo bar*\")))"},
        {352,
         "a*\"foo\"*\n",
         "(doc (p (text \"a*\\\"foo\\\"*\")))"},
        {353,
         "*\302\240a\302\240*\n",
         "(doc (p (text \"*\\xC2\\xA0a\\xC2\\xA0*\")))"},
        {354,
         "*$*alpha.\n\n*\302\243*bravo.\n\n*\342\202\254*charlie.\n",
         "(doc (p (text \"*$*alpha.\")) (p (text \"*\\xC2\\xA3*bravo.\")) (p"
         " (text \"*\\xE2\\x82\\xAC*charlie.\")))"},
        {355,
         "foo*bar*\n",
         "(doc (p (text \"foo\") (em (text \"bar\"))))"},
        {356,
         "5*6*78\n",
         "(doc (p (text \"5\") (em (text \"6\")) (text \"78\")))"},
        {357,
         "_foo bar_\n",
         "(doc (p (em (text \"foo bar\"))))"},
        {358,
         "_ foo bar_\n",
         "(doc (p (text \"_ foo bar_\")))"},
        {359,
         "a_\"foo\"_\n",
         "(doc (p (text \"a_\\\"foo\\\"_\")))"},
        {360,
         "foo_bar_\n",
         "(doc (p (text \"foo_bar_\")))"},
        {361,
         "5_6_78\n",
         "(doc (p (text \"5_6_78\")))"},
        {362,
         "\320\277\321\200\320\270\321\201\321\202\320\260\320\275\321\217"
         "\320\274_\321\201\321\202\321\200\320\265\320\274\321\217\321\202"
         "\321\201\321\217_\n",
         "(doc (p"
         " (text \"\\xD0\\xBF\\xD1\\x80\\xD0\\xB8\\xD1\\x81\\xD1\\x82\\xD0"
         "\\xB0\\xD0\\xBD\\xD1\\x8F\\xD0\\xBC_\\xD1\\x81\\xD1\\x82\\xD1\\x80"
         "\\xD0\\xB5\\xD0\\xBC\\xD1\\x8F\\xD1\\x82\\xD1\\x81\\xD1\\x8F_\")))"},
        {363,
         "aa_\"bb\"_cc\n",
         "(doc (p (text \"aa_\\\"bb\\\"_cc\")))"},
        {364,
         "foo-_(bar)_\n",
         "(doc (p (text \"foo-\") (em (text \"(bar)\"))))"},
        {365,
         "_foo*\n",
         "(doc (p (text \"_foo*\")))"},
        {366,
         "*foo bar *\n",
         "(doc (p (text \"*foo bar *\")))"},
        {367,
         "*foo bar\n*\n",
         "(doc (p (text \"*foo bar\") (sbr) (text \"*\")))"},
        {368,
         "*(*foo)\n",
         "(doc (p (text \"*(*foo)\")))"},
        {369,
         "*(*foo*)*\n",
         "(doc (p (em (text \"(\") (em (text \"foo\")) (text \")\"))))"},
        {370,
         "*foo*bar\n",
         "(doc (p (em (text \"foo\")) (text \"bar\")))"},
        {371,
         "_foo bar _\n",
         "(doc (p (text \"_foo bar _\")))"},
        {372,
         "_(_foo)\n",
         "(doc (p (text \"_(_foo)\")))"},
        {373,
         "_(_foo_)_\n",
         "(doc (p (em (text \"(\") (em (text \"foo\")) (text \")\"))))"},
        {374,
         "_foo_bar\n",
         "(doc (p (text \"_foo_bar\")))"},
        {375,
         "_\320\277\321\200\320\270\321\201\321\202\320\260\320\275\321\217"
         "\320\274_\321\201\321\202\321\200\320\265\320\274\321\217\321\202"
         "\321\201\321\217\n",
         "(doc (p"
         " (text \"_\\xD0\\xBF\\xD1\\x80\\xD0\\xB8\\xD1\\x81\\xD1\\x82\\xD0"
         "\\xB0\\xD0\\xBD\\xD1\\x8F\\xD0\\xBC_\\xD1\\x81\\xD1\\x82\\xD1\\x80"
         "\\xD0\\xB5\\xD0\\xBC\\xD1\\x8F\\xD1\\x82\\xD1\\x81\\xD1\\x8F\")))"},
        {376,
         "_foo_bar_baz_\n",
         "(doc (p (em (text \"foo_bar_baz\"))))"},
        {377,
         "_(bar)_.\n",
         "(doc (p (em (text \"(bar)\")) (text \".\")))"},
        {378,
         "**foo bar**\n",
         "(doc (p (strong (text \"foo bar\"))))"},
        {379,
         "** foo bar**\n",
         "(doc (p (text \"** foo bar**\")))"},
        {380,
         "a**\"foo\"**\n",
         "(doc (p (text \"a**\\\"foo\\\"**\")))"},
        {381,
         "foo**bar**\n",
         "(doc (p (text \"foo\") (strong (text \"bar\"))))"},
        {382,
         "__foo bar__\n",
         "(doc (p (strong (text \"foo bar\"))))"},
        {383,
         "__ foo bar__\n",
         "(doc (p (text \"__ foo bar__\")))"},
        {384,
         "__\nfoo bar__\n",
         "(doc (p (text \"__\") (sbr) (text \"foo bar__\")))"},
        {385,
         "a__\"foo\"__\n",
         "(doc (p (text \"a__\\\"foo\\\"__\")))"},
        {386,
         "foo__bar__\n",
         "(doc (p (text \"foo__bar__\")))"},
        {387,
         "5__6__78\n",
         "(doc (p (text \"5__6__78\")))"},
        {388,
         "\320\277\321\200\320\270\321\201\321\202\320\260\320\275\321\217"
         "\320\274__\321\201\321\202\321\200\320\265\320\274\321\217\321\202"
         "\321\201\321\217__\n",
         "(doc (p"
         " (text \"\\xD0\\xBF\\xD1\\x80\\xD0\\xB8\\xD1\\x81\\xD1\\x82\\xD0"
         "\\xB0\\xD0\\xBD\\xD1\\x8F\\xD0\\xBC__\\xD1\\x81\\xD1\\x82\\xD1"
         "\\x80\\xD0\\xB5\\xD0\\xBC\\xD1\\x8F\\xD1\\x82\\xD1\\x81\\xD1\\x8F_"
         "_\")))"},
        {389,
         "__foo, __bar__, baz__\n",
         "(doc (p (strong (text \"foo, \") (strong (text \"bar\"))"
         " (text \", baz\"))))"},
        {390,
         "foo-__(bar)__\n",
         "(doc (p (text \"foo-\") (strong (text \"(bar)\"))))"},
        {391,
         "**foo bar **\n",
         "(doc (p (text \"**foo bar **\")))"},
        {392,
         "**(**foo)\n",
         "(doc (p (text \"**(**foo)\")))"},
        {393,
         "*(**foo**)*\n",
         "(doc (p (em (text \"(\") (strong (text \"foo\")) (text \")\"))))"},
        {394,
         "**Gomphocarpus (*Gomphocarpus physocarpus*, syn.\n"
         "*Asclepias physocarpa*)**\n",
         "(doc (p (strong (text \"Gomphocarpus (\") (em"
         " (text \"Gomphocarpus physocarpus\")) (text \", syn.\") (sbr) (em"
         " (text \"Asclepias physocarpa\")) (text \")\"))))"},
        {395,
         "**foo \"*bar*\" foo**\n",
         "(doc (p (strong (text \"foo \\\"\") (em (text \"bar\"))"
         " (text \"\\\" foo\"))))"},
        {396,
         "**foo**bar\n",
         "(doc (p (strong (text \"foo\")) (text \"bar\")))"},
        {397,
         "__foo bar __\n",
         "(doc (p (text \"__foo bar __\")))"},
        {398,
         "__(__foo)\n",
         "(doc (p (text \"__(__foo)\")))"},
        {399,
         "_(__foo__)_\n",
         "(doc (p (em (text \"(\") (strong (text \"foo\")) (text \")\"))))"},
        {400,
         "__foo__bar\n",
         "(doc (p (text \"__foo__bar\")))"},
        {401,
         "__\320\277\321\200\320\270\321\201\321\202\320\260\320\275\321\217"
         "\320\274__\321\201\321\202\321\200\320\265\320\274\321\217\321\202"
         "\321\201\321\217\n",
         "(doc (p"
         " (text \"__\\xD0\\xBF\\xD1\\x80\\xD0\\xB8\\xD1\\x81\\xD1\\x82\\xD0"
         "\\xB0\\xD0\\xBD\\xD1\\x8F\\xD0\\xBC__\\xD1\\x81\\xD1\\x82\\xD1"
         "\\x80\\xD0\\xB5\\xD0\\xBC\\xD1\\x8F\\xD1\\x82\\xD1\\x81\\xD1\\x8F"
         "\")))"},
        {402,
         "__foo__bar__baz__\n",
         "(doc (p (strong (text \"foo__bar__baz\"))))"},
        {403,
         "__(bar)__.\n",
         "(doc (p (strong (text \"(bar)\")) (text \".\")))"},
        {404,
         "*foo [bar](/url)*\n",
         "(doc (p (em (text \"foo \") (link inline dest=\"/url\""
         " (text \"bar\")))))"},
        {405,
         "*foo\nbar*\n",
         "(doc (p (em (text \"foo\") (sbr) (text \"bar\"))))"},
        {406,
         "_foo __bar__ baz_\n",
         "(doc (p (em (text \"foo \") (strong (text \"bar\"))"
         " (text \" baz\"))))"},
        {407,
         "_foo _bar_ baz_\n",
         "(doc (p (em (text \"foo \") (em (text \"bar\")) (text \" baz\"))))"},
        {408,
         "__foo_ bar_\n",
         "(doc (p (em (em (text \"foo\")) (text \" bar\"))))"},
        {409,
         "*foo *bar**\n",
         "(doc (p (em (text \"foo \") (em (text \"bar\")))))"},
        {410,
         "*foo **bar** baz*\n",
         "(doc (p (em (text \"foo \") (strong (text \"bar\"))"
         " (text \" baz\"))))"},
        {411,
         "*foo**bar**baz*\n",
         "(doc (p (em (text \"foo\") (strong (text \"bar\"))"
         " (text \"baz\"))))"},
        {412,
         "*foo**bar*\n",
         "(doc (p (em (text \"foo**bar\"))))"},
        {413,
         "***foo** bar*\n",
         "(doc (p (em (strong (text \"foo\")) (text \" bar\"))))"},
        {414,
         "*foo **bar***\n",
         "(doc (p (em (text \"foo \") (strong (text \"bar\")))))"},
        {415,
         "*foo**bar***\n",
         "(doc (p (em (text \"foo\") (strong (text \"bar\")))))"},
        {416,
         "foo***bar***baz\n",
         "(doc (p (text \"foo\") (em (strong (text \"bar\")))"
         " (text \"baz\")))"},
        {417,
         "foo******bar*********baz\n",
         "(doc (p (text \"foo\") (strong (strong (strong (text \"bar\"))))"
         " (text \"***baz\")))"},
        {418,
         "*foo **bar *baz* bim** bop*\n",
         "(doc (p (em (text \"foo \") (strong (text \"bar \") (em"
         " (text \"baz\")) (text \" bim\")) (text \" bop\"))))"},
        {419,
         "*foo [*bar*](/url)*\n",
         "(doc (p (em (text \"foo \") (link inline dest=\"/url\" (em"
         " (text \"bar\"))))))"},
        {420,
         "** is not an empty emphasis\n",
         "(doc (p (text \"** is not an empty emphasis\")))"},
        {421,
         "**** is not an empty strong emphasis\n",
         "(doc (p (text \"**** is not an empty strong emphasis\")))"},
        {422,
         "**foo [bar](/url)**\n",
         "(doc (p (strong (text \"foo \") (link inline dest=\"/url\""
         " (text \"bar\")))))"},
        {423,
         "**foo\nbar**\n",
         "(doc (p (strong (text \"foo\") (sbr) (text \"bar\"))))"},
        {424,
         "__foo _bar_ baz__\n",
         "(doc (p (strong (text \"foo \") (em (text \"bar\"))"
         " (text \" baz\"))))"},
        {425,
         "__foo __bar__ baz__\n",
         "(doc (p (strong (text \"foo \") (strong (text \"bar\"))"
         " (text \" baz\"))))"},
        {426,
         "____foo__ bar__\n",
         "(doc (p (strong (strong (text \"foo\")) (text \" bar\"))))"},
        {427,
         "**foo **bar****\n",
         "(doc (p (strong (text \"foo \") (strong (text \"bar\")))))"},
        {428,
         "**foo *bar* baz**\n",
         "(doc (p (strong (text \"foo \") (em (text \"bar\"))"
         " (text \" baz\"))))"},
        {429,
         "**foo*bar*baz**\n",
         "(doc (p (strong (text \"foo\") (em (text \"bar\"))"
         " (text \"baz\"))))"},
        {430,
         "***foo* bar**\n",
         "(doc (p (strong (em (text \"foo\")) (text \" bar\"))))"},
        {431,
         "**foo *bar***\n",
         "(doc (p (strong (text \"foo \") (em (text \"bar\")))))"},
        {432,
         "**foo *bar **baz**\nbim* bop**\n",
         "(doc (p (strong (text \"foo \") (em (text \"bar \") (strong"
         " (text \"baz\")) (sbr) (text \"bim\")) (text \" bop\"))))"},
        {433,
         "**foo [*bar*](/url)**\n",
         "(doc (p (strong (text \"foo \") (link inline dest=\"/url\" (em"
         " (text \"bar\"))))))"},
        {434,
         "__ is not an empty emphasis\n",
         "(doc (p (text \"__ is not an empty emphasis\")))"},
        {435,
         "____ is not an empty strong emphasis\n",
         "(doc (p (text \"____ is not an empty strong emphasis\")))"},
        {436,
         "foo ***\n",
         "(doc (p (text \"foo ***\")))"},
        {437,
         "foo *\\**\n",
         "(doc (p (text \"foo \") (em (esc \"\\\\*\"))))"},
        {438,
         "foo *_*\n",
         "(doc (p (text \"foo \") (em (text \"_\"))))"},
        {439,
         "foo *****\n",
         "(doc (p (text \"foo *****\")))"},
        {440,
         "foo **\\***\n",
         "(doc (p (text \"foo \") (strong (esc \"\\\\*\"))))"},
        {441,
         "foo **_**\n",
         "(doc (p (text \"foo \") (strong (text \"_\"))))"},
        {442,
         "**foo*\n",
         "(doc (p (text \"*\") (em (text \"foo\"))))"},
        {443,
         "*foo**\n",
         "(doc (p (em (text \"foo\")) (text \"*\")))"},
        {444,
         "***foo**\n",
         "(doc (p (text \"*\") (strong (text \"foo\"))))"},
        {445,
         "****foo*\n",
         "(doc (p (text \"***\") (em (text \"foo\"))))"},
        {446,
         "**foo***\n",
         "(doc (p (strong (text \"foo\")) (text \"*\")))"},
        {447,
         "*foo****\n",
         "(doc (p (em (text \"foo\")) (text \"***\")))"},
        {448,
         "foo ___\n",
         "(doc (p (text \"foo ___\")))"},
        {449,
         "foo _\\__\n",
         "(doc (p (text \"foo \") (em (esc \"\\\\_\"))))"},
        {450,
         "foo _*_\n",
         "(doc (p (text \"foo \") (em (text \"*\"))))"},
        {451,
         "foo _____\n",
         "(doc (p (text \"foo _____\")))"},
        {452,
         "foo __\\___\n",
         "(doc (p (text \"foo \") (strong (esc \"\\\\_\"))))"},
        {453,
         "foo __*__\n",
         "(doc (p (text \"foo \") (strong (text \"*\"))))"},
        {454,
         "__foo_\n",
         "(doc (p (text \"_\") (em (text \"foo\"))))"},
        {455,
         "_foo__\n",
         "(doc (p (em (text \"foo\")) (text \"_\")))"},
        {456,
         "___foo__\n",
         "(doc (p (text \"_\") (strong (text \"foo\"))))"},
        {457,
         "____foo_\n",
         "(doc (p (text \"___\") (em (text \"foo\"))))"},
        {458,
         "__foo___\n",
         "(doc (p (strong (text \"foo\")) (text \"_\")))"},
        {459,
         "_foo____\n",
         "(doc (p (em (text \"foo\")) (text \"___\")))"},
        {460,
         "**foo**\n",
         "(doc (p (strong (text \"foo\"))))"},
        {461,
         "*_foo_*\n",
         "(doc (p (em (em (text \"foo\")))))"},
        {462,
         "__foo__\n",
         "(doc (p (strong (text \"foo\"))))"},
        {463,
         "_*foo*_\n",
         "(doc (p (em (em (text \"foo\")))))"},
        {464,
         "****foo****\n",
         "(doc (p (strong (strong (text \"foo\")))))"},
        {465,
         "____foo____\n",
         "(doc (p (strong (strong (text \"foo\")))))"},
        {466,
         "******foo******\n",
         "(doc (p (strong (strong (strong (text \"foo\"))))))"},
        {467,
         "***foo***\n",
         "(doc (p (em (strong (text \"foo\")))))"},
        {468,
         "_____foo_____\n",
         "(doc (p (em (strong (strong (text \"foo\"))))))"},
        {469,
         "*foo _bar* baz_\n",
         "(doc (p (em (text \"foo _bar\")) (text \" baz_\")))"},
        {470,
         "*foo __bar *baz bim__ bam*\n",
         "(doc (p (em (text \"foo \") (strong (text \"bar *baz bim\"))"
         " (text \" bam\"))))"},
        {471,
         "**foo **bar baz**\n",
         "(doc (p (text \"**foo \") (strong (text \"bar baz\"))))"},
        {472,
         "*foo *bar baz*\n",
         "(doc (p (text \"*foo \") (em (text \"bar baz\"))))"},
        {473,
         "*[bar*](/url)\n",
         "(doc (p (text \"*\") (link inline dest=\"/url\" (text \"bar*\"))))"},
        {474,
         "_foo [bar_](/url)\n",
         "(doc (p (text \"_foo \") (link inline dest=\"/url\""
         " (text \"bar_\"))))"},
        {475,
         "*<img src=\"foo\" title=\"*\"/>\n",
         "(doc (p (text \"*\")"
         " (html \"<img src=\\\"foo\\\" title=\\\"*\\\"/>\")))"},
        {476,
         "**<a href=\"**\">\n",
         "(doc (p (text \"**\") (html \"<a href=\\\"**\\\">\")))"},
        {477,
         "__<a href=\"__\">\n",
         "(doc (p (text \"__\") (html \"<a href=\\\"__\\\">\")))"},
        {478,
         "*a `*`*\n",
         "(doc (p (em (text \"a \") (code \"`*`\"))))"},
        {479,
         "_a `_`_\n",
         "(doc (p (em (text \"a \") (code \"`_`\"))))"},
        {480,
         "**a<https://foo.bar/?q=**>\n",
         "(doc (p (text \"**a\")"
         " (autolink uri \"<https://foo.bar/?q=**>\")))"},
        {481,
         "__a<https://foo.bar/?q=__>\n",
         "(doc (p (text \"__a\")"
         " (autolink uri \"<https://foo.bar/?q=__>\")))"},
    };

    CHECK_EXAMPLES(examples);
}

NAOS_SUITE(spec_emphasis_and_strong,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(spec_emphasis)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(spec_emphasis_and_strong)
)
