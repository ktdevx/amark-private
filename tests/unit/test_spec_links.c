/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * The examples of the CommonMark specification for links and images,
 * checked through amark_parse: each example's input is parsed with the
 * default configuration (and the counting test allocator), the tree must pass
 * amark_tree_check, and the whole tree, blocks and inlines, as
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
 * The tree keeps the source: escapes and character references are not
 * decoded and destinations are not normalized, so each node is written with
 * the bytes of its range, an inline link or image with the bytes of its
 * destination (inside the angle brackets, if any) and title (inside the
 * delimiters), and a reference link or image with its form (full, collapsed
 * or shortcut) and the label of the definition it refers to, as that
 * definition writes it (the first one, when several have the same label).
 * The expected strings give, for the structure that the HTML shows, the
 * source bytes that each node covers. The alt text of an image is plain in
 * the HTML; its children are the inlines of the image description. Adjacent
 * texts on the same line are one node, so a bracket that starts no link is
 * part of the text around it. Definitions are "(def)" blocks, placed where
 * the input writes them.
 *
 * Sections: Links and Images.
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

/* Links (examples 482-571). */
NAOS_TEST(spec_links)
{
    static const spec_example_t examples[] = {
        {482,
         "[link](/uri \"title\")\n",
         "(doc (p (link inline dest=\"/uri\" title=\"title\""
         " (text \"link\"))))"},
        {483,
         "[link](/uri)\n",
         "(doc (p (link inline dest=\"/uri\" (text \"link\"))))"},
        {484,
         "[](./target.md)\n",
         "(doc (p (link inline dest=\"./target.md\")))"},
        {485,
         "[link]()\n",
         "(doc (p (link inline (text \"link\"))))"},
        {486,
         "[link](<>)\n",
         "(doc (p (link inline dest=\"\" (text \"link\"))))"},
        {487,
         "[]()\n",
         "(doc (p (link inline)))"},
        {488,
         "[link](/my uri)\n",
         "(doc (p (text \"[link](/my uri)\")))"},
        {489,
         "[link](</my uri>)\n",
         "(doc (p (link inline dest=\"/my uri\" (text \"link\"))))"},
        {490,
         "[link](foo\nbar)\n",
         "(doc (p (text \"[link](foo\") (sbr) (text \"bar)\")))"},
        {491,
         "[link](<foo\nbar>)\n",
         "(doc (p (text \"[link](\") (html \"<foo\\nbar>\") (text \")\")))"},
        {492,
         "[a](<b)c>)\n",
         "(doc (p (link inline dest=\"b)c\" (text \"a\"))))"},
        {493,
         "[link](<foo\\>)\n",
         "(doc (p (text \"[link](<foo\") (esc \"\\\\>\") (text \")\")))"},
        {494,
         "[a](<b)c\n[a](<b)c>\n[a](<b>c)\n",
         "(doc (p (text \"[a](<b)c\") (sbr) (text \"[a](<b)c>\") (sbr)"
         " (text \"[a](\") (html \"<b>\") (text \"c)\")))"},
        {495,
         "[link](\\(foo\\))\n",
         "(doc (p (link inline dest=\"\\\\(foo\\\\)\" (text \"link\"))))"},
        {496,
         "[link](foo(and(bar)))\n",
         "(doc (p (link inline dest=\"foo(and(bar))\" (text \"link\"))))"},
        {497,
         "[link](foo(and(bar))\n",
         "(doc (p (text \"[link](foo(and(bar))\")))"},
        {498,
         "[link](foo\\(and\\(bar\\))\n",
         "(doc (p (link inline dest=\"foo\\\\(and\\\\(bar\\\\)\""
         " (text \"link\"))))"},
        {499,
         "[link](<foo(and(bar)>)\n",
         "(doc (p (link inline dest=\"foo(and(bar)\" (text \"link\"))))"},
        {500,
         "[link](foo\\)\\:)\n",
         "(doc (p (link inline dest=\"foo\\\\)\\\\:\" (text \"link\"))))"},
        {501,
         "[link](#fragment)\n\n[link](https://example.com#fragment)\n\n"
         "[link](https://example.com?foo=3#frag)\n",
         "(doc (p (link inline dest=\"#fragment\" (text \"link\"))) (p"
         " (link inline dest=\"https://example.com#fragment\""
         " (text \"link\"))) (p"
         " (link inline dest=\"https://example.com?foo=3#frag\""
         " (text \"link\"))))"},
        {502,
         "[link](foo\\bar)\n",
         "(doc (p (link inline dest=\"foo\\\\bar\" (text \"link\"))))"},
        {503,
         "[link](foo%20b&auml;)\n",
         "(doc (p (link inline dest=\"foo%20b&auml;\" (text \"link\"))))"},
        {504,
         "[link](\"title\")\n",
         "(doc (p (link inline dest=\"\\\"title\\\"\" (text \"link\"))))"},
        {505,
         "[link](/url \"title\")\n[link](/url 'title')\n"
         "[link](/url (title))\n",
         "(doc (p (link inline dest=\"/url\" title=\"title\""
         " (text \"link\")) (sbr) (link inline dest=\"/url\" title=\"title\""
         " (text \"link\")) (sbr) (link inline dest=\"/url\" title=\"title\""
         " (text \"link\"))))"},
        {506,
         "[link](/url \"title \\\"&quot;\")\n",
         "(doc (p (link inline dest=\"/url\" title=\"title \\\\\\\"&quot;\""
         " (text \"link\"))))"},
        {507,
         "[link](/url\302\240\"title\")\n",
         "(doc (p (link inline dest=\"/url\\xC2\\xA0\\\"title\\\"\""
         " (text \"link\"))))"},
        {508,
         "[link](/url \"title \"and\" title\")\n",
         "(doc (p (text \"[link](/url \\\"title \\\"and\\\" title\\\")\")))"},
        {509,
         "[link](/url 'title \"and\" title')\n",
         "(doc (p"
         " (link inline dest=\"/url\" title=\"title \\\"and\\\" title\""
         " (text \"link\"))))"},
        {510,
         "[link](   /uri\n  \"title\"  )\n",
         "(doc (p (link inline dest=\"/uri\" title=\"title\""
         " (text \"link\"))))"},
        {511,
         "[link] (/uri)\n",
         "(doc (p (text \"[link] (/uri)\")))"},
        {512,
         "[link [foo [bar]]](/uri)\n",
         "(doc (p (link inline dest=\"/uri\" (text \"link [foo [bar]]\"))))"},
        {513,
         "[link] bar](/uri)\n",
         "(doc (p (text \"[link] bar](/uri)\")))"},
        {514,
         "[link [bar](/uri)\n",
         "(doc (p (text \"[link \") (link inline dest=\"/uri\""
         " (text \"bar\"))))"},
        {515,
         "[link \\[bar](/uri)\n",
         "(doc (p (link inline dest=\"/uri\" (text \"link \")"
         " (esc \"\\\\[\") (text \"bar\"))))"},
        {516,
         "[link *foo **bar** `#`*](/uri)\n",
         "(doc (p (link inline dest=\"/uri\" (text \"link \") (em"
         " (text \"foo \") (strong (text \"bar\")) (text \" \")"
         " (code \"`#`\")))))"},
        {517,
         "[![moon](moon.jpg)](/uri)\n",
         "(doc (p (link inline dest=\"/uri\" (image inline dest=\"moon.jpg\""
         " (text \"moon\")))))"},
        {518,
         "[foo [bar](/uri)](/uri)\n",
         "(doc (p (text \"[foo \") (link inline dest=\"/uri\""
         " (text \"bar\")) (text \"](/uri)\")))"},
        {519,
         "[foo *[bar [baz](/uri)](/uri)*](/uri)\n",
         "(doc (p (text \"[foo \") (em (text \"[bar \")"
         " (link inline dest=\"/uri\" (text \"baz\")) (text \"](/uri)\"))"
         " (text \"](/uri)\")))"},
        {520,
         "![[[foo](uri1)](uri2)](uri3)\n",
         "(doc (p (image inline dest=\"uri3\" (text \"[\")"
         " (link inline dest=\"uri1\" (text \"foo\")) (text \"](uri2)\"))))"},
        {521,
         "*[foo*](/uri)\n",
         "(doc (p (text \"*\") (link inline dest=\"/uri\" (text \"foo*\"))))"},
        {522,
         "[foo *bar](baz*)\n",
         "(doc (p (link inline dest=\"baz*\" (text \"foo *bar\"))))"},
        {523,
         "*foo [bar* baz]\n",
         "(doc (p (em (text \"foo [bar\")) (text \" baz]\")))"},
        {524,
         "[foo <bar attr=\"](baz)\">\n",
         "(doc (p (text \"[foo \") (html \"<bar attr=\\\"](baz)\\\">\")))"},
        {525,
         "[foo`](/uri)`\n",
         "(doc (p (text \"[foo\") (code \"`](/uri)`\")))"},
        {526,
         "[foo<https://example.com/?search=](uri)>\n",
         "(doc (p (text \"[foo\")"
         " (autolink uri \"<https://example.com/?search=](uri)>\")))"},
        {527,
         "[foo][bar]\n\n[bar]: /url \"title\"\n",
         "(doc (p (link full def=\"bar\" (text \"foo\"))) (def))"},
        {528,
         "[link [foo [bar]]][ref]\n\n[ref]: /uri\n",
         "(doc (p (link full def=\"ref\" (text \"link [foo [bar]]\")))"
         " (def))"},
        {529,
         "[link \\[bar][ref]\n\n[ref]: /uri\n",
         "(doc (p (link full def=\"ref\" (text \"link \") (esc \"\\\\[\")"
         " (text \"bar\"))) (def))"},
        {530,
         "[link *foo **bar** `#`*][ref]\n\n[ref]: /uri\n",
         "(doc (p (link full def=\"ref\" (text \"link \") (em"
         " (text \"foo \") (strong (text \"bar\")) (text \" \")"
         " (code \"`#`\")))) (def))"},
        {531,
         "[![moon](moon.jpg)][ref]\n\n[ref]: /uri\n",
         "(doc (p (link full def=\"ref\" (image inline dest=\"moon.jpg\""
         " (text \"moon\")))) (def))"},
        {532,
         "[foo [bar](/uri)][ref]\n\n[ref]: /uri\n",
         "(doc (p (text \"[foo \") (link inline dest=\"/uri\""
         " (text \"bar\")) (text \"]\") (link shortcut def=\"ref\""
         " (text \"ref\"))) (def))"},
        {533,
         "[foo *bar [baz][ref]*][ref]\n\n[ref]: /uri\n",
         "(doc (p (text \"[foo \") (em (text \"bar \")"
         " (link full def=\"ref\" (text \"baz\"))) (text \"]\")"
         " (link shortcut def=\"ref\" (text \"ref\"))) (def))"},
        {534,
         "*[foo*][ref]\n\n[ref]: /uri\n",
         "(doc (p (text \"*\") (link full def=\"ref\" (text \"foo*\")))"
         " (def))"},
        {535,
         "[foo *bar][ref]*\n\n[ref]: /uri\n",
         "(doc (p (link full def=\"ref\" (text \"foo *bar\")) (text \"*\"))"
         " (def))"},
        {536,
         "[foo <bar attr=\"][ref]\">\n\n[ref]: /uri\n",
         "(doc (p (text \"[foo \") (html \"<bar attr=\\\"][ref]\\\">\"))"
         " (def))"},
        {537,
         "[foo`][ref]`\n\n[ref]: /uri\n",
         "(doc (p (text \"[foo\") (code \"`][ref]`\")) (def))"},
        {538,
         "[foo<https://example.com/?search=][ref]>\n\n[ref]: /uri\n",
         "(doc (p (text \"[foo\")"
         " (autolink uri \"<https://example.com/?search=][ref]>\")) (def))"},
        {539,
         "[foo][BaR]\n\n[bar]: /url \"title\"\n",
         "(doc (p (link full def=\"bar\" (text \"foo\"))) (def))"},
        {540,
         "[\341\272\236]\n\n[SS]: /url\n",
         "(doc (p (link shortcut def=\"SS\" (text \"\\xE1\\xBA\\x9E\")))"
         " (def))"},
        {541,
         "[Foo\n  bar]: /url\n\n[Baz][Foo bar]\n",
         "(doc (def) (p (link full def=\"Foo\\n  bar\" (text \"Baz\"))))"},
        {542,
         "[foo] [bar]\n\n[bar]: /url \"title\"\n",
         "(doc (p (text \"[foo] \") (link shortcut def=\"bar\""
         " (text \"bar\"))) (def))"},
        {543,
         "[foo]\n[bar]\n\n[bar]: /url \"title\"\n",
         "(doc (p (text \"[foo]\") (sbr) (link shortcut def=\"bar\""
         " (text \"bar\"))) (def))"},
        {544,
         "[foo]: /url1\n\n[foo]: /url2\n\n[bar][foo]\n",
         "(doc (def) (def) (p (link full def=\"foo\" (text \"bar\"))))"},
        {545,
         "[bar][foo\\!]\n\n[foo!]: /url\n",
         "(doc (p (text \"[bar][foo\") (esc \"\\\\!\") (text \"]\")) (def))"},
        {546,
         "[foo][ref[]\n\n[ref[]: /uri\n",
         "(doc (p (text \"[foo][ref[]\")) (p (text \"[ref[]: /uri\")))"},
        {547,
         "[foo][ref[bar]]\n\n[ref[bar]]: /uri\n",
         "(doc (p (text \"[foo][ref[bar]]\")) (p"
         " (text \"[ref[bar]]: /uri\")))"},
        {548,
         "[[[foo]]]\n\n[[[foo]]]: /url\n",
         "(doc (p (text \"[[[foo]]]\")) (p (text \"[[[foo]]]: /url\")))"},
        {549,
         "[foo][ref\\[]\n\n[ref\\[]: /uri\n",
         "(doc (p (link full def=\"ref\\\\[\" (text \"foo\"))) (def))"},
        {550,
         "[bar\\\\]: /uri\n\n[bar\\\\]\n",
         "(doc (def) (p (link shortcut def=\"bar\\\\\\\\\" (text \"bar\")"
         " (esc \"\\\\\\\\\"))))"},
        {551,
         "[]\n\n[]: /uri\n",
         "(doc (p (text \"[]\")) (p (text \"[]: /uri\")))"},
        {552,
         "[\n ]\n\n[\n ]: /uri\n",
         "(doc (p (text \"[\") (sbr) (text \"]\")) (p (text \"[\") (sbr)"
         " (text \"]: /uri\")))"},
        {553,
         "[foo][]\n\n[foo]: /url \"title\"\n",
         "(doc (p (link collapsed def=\"foo\" (text \"foo\"))) (def))"},
        {554,
         "[*foo* bar][]\n\n[*foo* bar]: /url \"title\"\n",
         "(doc (p (link collapsed def=\"*foo* bar\" (em (text \"foo\"))"
         " (text \" bar\"))) (def))"},
        {555,
         "[Foo][]\n\n[foo]: /url \"title\"\n",
         "(doc (p (link collapsed def=\"foo\" (text \"Foo\"))) (def))"},
        {556,
         "[foo] \n[]\n\n[foo]: /url \"title\"\n",
         "(doc (p (link shortcut def=\"foo\" (text \"foo\")) (sbr)"
         " (text \"[]\")) (def))"},
        {557,
         "[foo]\n\n[foo]: /url \"title\"\n",
         "(doc (p (link shortcut def=\"foo\" (text \"foo\"))) (def))"},
        {558,
         "[*foo* bar]\n\n[*foo* bar]: /url \"title\"\n",
         "(doc (p (link shortcut def=\"*foo* bar\" (em (text \"foo\"))"
         " (text \" bar\"))) (def))"},
        {559,
         "[[*foo* bar]]\n\n[*foo* bar]: /url \"title\"\n",
         "(doc (p (text \"[\") (link shortcut def=\"*foo* bar\" (em"
         " (text \"foo\")) (text \" bar\")) (text \"]\")) (def))"},
        {560,
         "[[bar [foo]\n\n[foo]: /url\n",
         "(doc (p (text \"[[bar \") (link shortcut def=\"foo\""
         " (text \"foo\"))) (def))"},
        {561,
         "[Foo]\n\n[foo]: /url \"title\"\n",
         "(doc (p (link shortcut def=\"foo\" (text \"Foo\"))) (def))"},
        {562,
         "[foo] bar\n\n[foo]: /url\n",
         "(doc (p (link shortcut def=\"foo\" (text \"foo\"))"
         " (text \" bar\")) (def))"},
        {563,
         "\\[foo]\n\n[foo]: /url \"title\"\n",
         "(doc (p (esc \"\\\\[\") (text \"foo]\")) (def))"},
        {564,
         "[foo*]: /url\n\n*[foo*]\n",
         "(doc (def) (p (text \"*\") (link shortcut def=\"foo*\""
         " (text \"foo*\"))))"},
        {565,
         "[foo][bar]\n\n[foo]: /url1\n[bar]: /url2\n",
         "(doc (p (link full def=\"bar\" (text \"foo\"))) (def) (def))"},
        {566,
         "[foo][]\n\n[foo]: /url1\n",
         "(doc (p (link collapsed def=\"foo\" (text \"foo\"))) (def))"},
        {567,
         "[foo]()\n\n[foo]: /url1\n",
         "(doc (p (link inline (text \"foo\"))) (def))"},
        {568,
         "[foo](not a link)\n\n[foo]: /url1\n",
         "(doc (p (link shortcut def=\"foo\" (text \"foo\"))"
         " (text \"(not a link)\")) (def))"},
        {569,
         "[foo][bar][baz]\n\n[baz]: /url\n",
         "(doc (p (text \"[foo]\") (link full def=\"baz\" (text \"bar\")))"
         " (def))"},
        {570,
         "[foo][bar][baz]\n\n[baz]: /url1\n[bar]: /url2\n",
         "(doc (p (link full def=\"bar\" (text \"foo\"))"
         " (link shortcut def=\"baz\" (text \"baz\"))) (def) (def))"},
        {571,
         "[foo][bar][baz]\n\n[baz]: /url1\n[foo]: /url2\n",
         "(doc (p (text \"[foo]\") (link full def=\"baz\" (text \"bar\")))"
         " (def) (def))"},
    };

    CHECK_EXAMPLES(examples);
}

/* Images (examples 572-593). */
NAOS_TEST(spec_images)
{
    static const spec_example_t examples[] = {
        {572,
         "![foo](/url \"title\")\n",
         "(doc (p (image inline dest=\"/url\" title=\"title\""
         " (text \"foo\"))))"},
        {573,
         "![foo *bar*]\n\n[foo *bar*]: train.jpg \"train & tracks\"\n",
         "(doc (p (image shortcut def=\"foo *bar*\" (text \"foo \") (em"
         " (text \"bar\")))) (def))"},
        {574,
         "![foo ![bar](/url)](/url2)\n",
         "(doc (p (image inline dest=\"/url2\" (text \"foo \")"
         " (image inline dest=\"/url\" (text \"bar\")))))"},
        {575,
         "![foo [bar](/url)](/url2)\n",
         "(doc (p (image inline dest=\"/url2\" (text \"foo \")"
         " (link inline dest=\"/url\" (text \"bar\")))))"},
        {576,
         "![foo *bar*][]\n\n[foo *bar*]: train.jpg \"train & tracks\"\n",
         "(doc (p (image collapsed def=\"foo *bar*\" (text \"foo \") (em"
         " (text \"bar\")))) (def))"},
        {577,
         "![foo *bar*][foobar]\n\n[FOOBAR]: train.jpg \"train & tracks\"\n",
         "(doc (p (image full def=\"FOOBAR\" (text \"foo \") (em"
         " (text \"bar\")))) (def))"},
        {578,
         "![foo](train.jpg)\n",
         "(doc (p (image inline dest=\"train.jpg\" (text \"foo\"))))"},
        {579,
         "My ![foo bar](/path/to/train.jpg  \"title\"   )\n",
         "(doc (p (text \"My \")"
         " (image inline dest=\"/path/to/train.jpg\" title=\"title\""
         " (text \"foo bar\"))))"},
        {580,
         "![foo](<url>)\n",
         "(doc (p (image inline dest=\"url\" (text \"foo\"))))"},
        {581,
         "![](/url)\n",
         "(doc (p (image inline dest=\"/url\")))"},
        {582,
         "![foo][bar]\n\n[bar]: /url\n",
         "(doc (p (image full def=\"bar\" (text \"foo\"))) (def))"},
        {583,
         "![foo][bar]\n\n[BAR]: /url\n",
         "(doc (p (image full def=\"BAR\" (text \"foo\"))) (def))"},
        {584,
         "![foo][]\n\n[foo]: /url \"title\"\n",
         "(doc (p (image collapsed def=\"foo\" (text \"foo\"))) (def))"},
        {585,
         "![*foo* bar][]\n\n[*foo* bar]: /url \"title\"\n",
         "(doc (p (image collapsed def=\"*foo* bar\" (em (text \"foo\"))"
         " (text \" bar\"))) (def))"},
        {586,
         "![Foo][]\n\n[foo]: /url \"title\"\n",
         "(doc (p (image collapsed def=\"foo\" (text \"Foo\"))) (def))"},
        {587,
         "![foo] \n[]\n\n[foo]: /url \"title\"\n",
         "(doc (p (image shortcut def=\"foo\" (text \"foo\")) (sbr)"
         " (text \"[]\")) (def))"},
        {588,
         "![foo]\n\n[foo]: /url \"title\"\n",
         "(doc (p (image shortcut def=\"foo\" (text \"foo\"))) (def))"},
        {589,
         "![*foo* bar]\n\n[*foo* bar]: /url \"title\"\n",
         "(doc (p (image shortcut def=\"*foo* bar\" (em (text \"foo\"))"
         " (text \" bar\"))) (def))"},
        {590,
         "![[foo]]\n\n[[foo]]: /url \"title\"\n",
         "(doc (p (text \"![[foo]]\")) (p"
         " (text \"[[foo]]: /url \\\"title\\\"\")))"},
        {591,
         "![Foo]\n\n[foo]: /url \"title\"\n",
         "(doc (p (image shortcut def=\"foo\" (text \"Foo\"))) (def))"},
        {592,
         "!\\[foo]\n\n[foo]: /url \"title\"\n",
         "(doc (p (text \"!\") (esc \"\\\\[\") (text \"foo]\")) (def))"},
        {593,
         "\\![foo]\n\n[foo]: /url \"title\"\n",
         "(doc (p (esc \"\\\\!\") (link shortcut def=\"foo\""
         " (text \"foo\"))) (def))"},
    };

    CHECK_EXAMPLES(examples);
}

NAOS_SUITE(spec_links_and_images,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(spec_links),
    NAOS_ADD_TEST(spec_images)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(spec_links_and_images)
)
