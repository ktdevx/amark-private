/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * The examples of the CommonMark specification for links and images,
 * checked through the values of their nodes: each example's input is parsed
 * with the default configuration (and the counting test allocator), and the
 * values, destinations, titles and info strings of its nodes, as
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
 * start of spec.txt); bytes outside ASCII are written as octal escapes of
 * their UTF-8 encoding.
 *
 * A link or image of the inline form has the destination and title that
 * the HTML gives it, as written (an empty destination between "<" and ">" is
 * an empty string; one not written at all, as in "()", and a title not
 * written do not appear). A link or image of a reference form has none of
 * its own: the destination and title of the HTML are those of the
 * definition it refers to, which appears where the input writes it. Raw
 * HTML, breaks, emphasis and the containers have no value, so they do not
 * appear in the expected strings; texts on the same line that the HTML shows
 * together are one node (as test_spec_links.c gives them), but escapes and
 * character references are nodes of their own.
 *
 * Sections: Links and Images.
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

/* Links (examples 482-571). */
NAOS_TEST(spec_links)
{
    static const spec_example_t examples[] = {
        {482,
         "[link](/uri \"title\")\n",
         "link:dest=\"/uri\" link:title=\"title\" text:value=\"link\""},
        {483,
         "[link](/uri)\n",
         "link:dest=\"/uri\" text:value=\"link\""},
        {484,
         "[](./target.md)\n",
         "link:dest=\"./target.md\""},
        {485,
         "[link]()\n",
         "text:value=\"link\""},
        {486,
         "[link](<>)\n",
         "link:dest=\"\" text:value=\"link\""},
        {487,
         "[]()\n",
         ""},
        {488,
         "[link](/my uri)\n",
         "text:value=\"[link](/my uri)\""},
        {489,
         "[link](</my uri>)\n",
         "link:dest=\"/my uri\" text:value=\"link\""},
        {490,
         "[link](foo\nbar)\n",
         "text:value=\"[link](foo\" text:value=\"bar)\""},
        {491,
         "[link](<foo\nbar>)\n",
         "text:value=\"[link](\" text:value=\")\""},
        {492,
         "[a](<b)c>)\n",
         "link:dest=\"b)c\" text:value=\"a\""},
        {493,
         "[link](<foo\\>)\n",
         "text:value=\"[link](<foo\" esc:value=\">\" text:value=\")\""},
        {494,
         "[a](<b)c\n[a](<b)c>\n[a](<b>c)\n",
         "text:value=\"[a](<b)c\" text:value=\"[a](<b)c>\""
         " text:value=\"[a](\" text:value=\"c)\""},
        {495,
         "[link](\\(foo\\))\n",
         "link:dest=\"(foo)\" text:value=\"link\""},
        {496,
         "[link](foo(and(bar)))\n",
         "link:dest=\"foo(and(bar))\" text:value=\"link\""},
        {497,
         "[link](foo(and(bar))\n",
         "text:value=\"[link](foo(and(bar))\""},
        {498,
         "[link](foo\\(and\\(bar\\))\n",
         "link:dest=\"foo(and(bar)\" text:value=\"link\""},
        {499,
         "[link](<foo(and(bar)>)\n",
         "link:dest=\"foo(and(bar)\" text:value=\"link\""},
        {500,
         "[link](foo\\)\\:)\n",
         "link:dest=\"foo):\" text:value=\"link\""},
        {501,
         "[link](#fragment)\n\n[link](https://example.com#fragment)\n\n"
         "[link](https://example.com?foo=3#frag)\n",
         "link:dest=\"#fragment\" text:value=\"link\""
         " link:dest=\"https://example.com#fragment\" text:value=\"link\""
         " link:dest=\"https://example.com?foo=3#frag\""
         " text:value=\"link\""},
        {502,
         "[link](foo\\bar)\n",
         "link:dest=\"foo\\\\bar\" text:value=\"link\""},
        {503,
         "[link](foo%20b&auml;)\n",
         "link:dest=\"foo%20b\303\244\" text:value=\"link\""},
        {504,
         "[link](\"title\")\n",
         "link:dest=\"\\\"title\\\"\" text:value=\"link\""},
        {505,
         "[link](/url \"title\")\n[link](/url 'title')\n"
         "[link](/url (title))\n",
         "link:dest=\"/url\" link:title=\"title\" text:value=\"link\""
         " link:dest=\"/url\" link:title=\"title\" text:value=\"link\""
         " link:dest=\"/url\" link:title=\"title\" text:value=\"link\""},
        {506,
         "[link](/url \"title \\\"&quot;\")\n",
         "link:dest=\"/url\" link:title=\"title \\\"\\\"\""
         " text:value=\"link\""},
        {507,
         "[link](/url\302\240\"title\")\n",
         "link:dest=\"/url\302\240\\\"title\\\"\" text:value=\"link\""},
        {508,
         "[link](/url \"title \"and\" title\")\n",
         "text:value=\"[link](/url \\\"title \\\"and\\\" title\\\")\""},
        {509,
         "[link](/url 'title \"and\" title')\n",
         "link:dest=\"/url\" link:title=\"title \\\"and\\\" title\""
         " text:value=\"link\""},
        {510,
         "[link](   /uri\n  \"title\"  )\n",
         "link:dest=\"/uri\" link:title=\"title\" text:value=\"link\""},
        {511,
         "[link] (/uri)\n",
         "text:value=\"[link] (/uri)\""},
        {512,
         "[link [foo [bar]]](/uri)\n",
         "link:dest=\"/uri\" text:value=\"link [foo [bar]]\""},
        {513,
         "[link] bar](/uri)\n",
         "text:value=\"[link] bar](/uri)\""},
        {514,
         "[link [bar](/uri)\n",
         "text:value=\"[link \" link:dest=\"/uri\" text:value=\"bar\""},
        {515,
         "[link \\[bar](/uri)\n",
         "link:dest=\"/uri\" text:value=\"link \" esc:value=\"[\""
         " text:value=\"bar\""},
        {516,
         "[link *foo **bar** `#`*](/uri)\n",
         "link:dest=\"/uri\" text:value=\"link \" text:value=\"foo \""
         " text:value=\"bar\" text:value=\" \" code:value=\"#\""},
        {517,
         "[![moon](moon.jpg)](/uri)\n",
         "link:dest=\"/uri\" image:dest=\"moon.jpg\" text:value=\"moon\""},
        {518,
         "[foo [bar](/uri)](/uri)\n",
         "text:value=\"[foo \" link:dest=\"/uri\" text:value=\"bar\""
         " text:value=\"](/uri)\""},
        {519,
         "[foo *[bar [baz](/uri)](/uri)*](/uri)\n",
         "text:value=\"[foo \" text:value=\"[bar \" link:dest=\"/uri\""
         " text:value=\"baz\" text:value=\"](/uri)\""
         " text:value=\"](/uri)\""},
        {520,
         "![[[foo](uri1)](uri2)](uri3)\n",
         "image:dest=\"uri3\" text:value=\"[\" link:dest=\"uri1\""
         " text:value=\"foo\" text:value=\"](uri2)\""},
        {521,
         "*[foo*](/uri)\n",
         "text:value=\"*\" link:dest=\"/uri\" text:value=\"foo*\""},
        {522,
         "[foo *bar](baz*)\n",
         "link:dest=\"baz*\" text:value=\"foo *bar\""},
        {523,
         "*foo [bar* baz]\n",
         "text:value=\"foo [bar\" text:value=\" baz]\""},
        {524,
         "[foo <bar attr=\"](baz)\">\n",
         "text:value=\"[foo \""},
        {525,
         "[foo`](/uri)`\n",
         "text:value=\"[foo\" code:value=\"](/uri)\""},
        {526,
         "[foo<https://example.com/?search=](uri)>\n",
         "text:value=\"[foo\""
         " autolink:value=\"https://example.com/?search=](uri)\""},
        {527,
         "[foo][bar]\n\n[bar]: /url \"title\"\n",
         "text:value=\"foo\" def:dest=\"/url\" def:title=\"title\""},
        {528,
         "[link [foo [bar]]][ref]\n\n[ref]: /uri\n",
         "text:value=\"link [foo [bar]]\" def:dest=\"/uri\""},
        {529,
         "[link \\[bar][ref]\n\n[ref]: /uri\n",
         "text:value=\"link \" esc:value=\"[\" text:value=\"bar\""
         " def:dest=\"/uri\""},
        {530,
         "[link *foo **bar** `#`*][ref]\n\n[ref]: /uri\n",
         "text:value=\"link \" text:value=\"foo \" text:value=\"bar\""
         " text:value=\" \" code:value=\"#\" def:dest=\"/uri\""},
        {531,
         "[![moon](moon.jpg)][ref]\n\n[ref]: /uri\n",
         "image:dest=\"moon.jpg\" text:value=\"moon\" def:dest=\"/uri\""},
        {532,
         "[foo [bar](/uri)][ref]\n\n[ref]: /uri\n",
         "text:value=\"[foo \" link:dest=\"/uri\" text:value=\"bar\""
         " text:value=\"]\" text:value=\"ref\" def:dest=\"/uri\""},
        {533,
         "[foo *bar [baz][ref]*][ref]\n\n[ref]: /uri\n",
         "text:value=\"[foo \" text:value=\"bar \" text:value=\"baz\""
         " text:value=\"]\" text:value=\"ref\" def:dest=\"/uri\""},
        {534,
         "*[foo*][ref]\n\n[ref]: /uri\n",
         "text:value=\"*\" text:value=\"foo*\" def:dest=\"/uri\""},
        {535,
         "[foo *bar][ref]*\n\n[ref]: /uri\n",
         "text:value=\"foo *bar\" text:value=\"*\" def:dest=\"/uri\""},
        {536,
         "[foo <bar attr=\"][ref]\">\n\n[ref]: /uri\n",
         "text:value=\"[foo \" def:dest=\"/uri\""},
        {537,
         "[foo`][ref]`\n\n[ref]: /uri\n",
         "text:value=\"[foo\" code:value=\"][ref]\" def:dest=\"/uri\""},
        {538,
         "[foo<https://example.com/?search=][ref]>\n\n[ref]: /uri\n",
         "text:value=\"[foo\""
         " autolink:value=\"https://example.com/?search=][ref]\""
         " def:dest=\"/uri\""},
        {539,
         "[foo][BaR]\n\n[bar]: /url \"title\"\n",
         "text:value=\"foo\" def:dest=\"/url\" def:title=\"title\""},
        {540,
         "[\341\272\236]\n\n[SS]: /url\n",
         "text:value=\"\341\272\236\" def:dest=\"/url\""},
        {541,
         "[Foo\n  bar]: /url\n\n[Baz][Foo bar]\n",
         "def:dest=\"/url\" text:value=\"Baz\""},
        {542,
         "[foo] [bar]\n\n[bar]: /url \"title\"\n",
         "text:value=\"[foo] \" text:value=\"bar\" def:dest=\"/url\""
         " def:title=\"title\""},
        {543,
         "[foo]\n[bar]\n\n[bar]: /url \"title\"\n",
         "text:value=\"[foo]\" text:value=\"bar\" def:dest=\"/url\""
         " def:title=\"title\""},
        {544,
         "[foo]: /url1\n\n[foo]: /url2\n\n[bar][foo]\n",
         "def:dest=\"/url1\" def:dest=\"/url2\" text:value=\"bar\""},
        {545,
         "[bar][foo\\!]\n\n[foo!]: /url\n",
         "text:value=\"[bar][foo\" esc:value=\"!\" text:value=\"]\""
         " def:dest=\"/url\""},
        {546,
         "[foo][ref[]\n\n[ref[]: /uri\n",
         "text:value=\"[foo][ref[]\" text:value=\"[ref[]: /uri\""},
        {547,
         "[foo][ref[bar]]\n\n[ref[bar]]: /uri\n",
         "text:value=\"[foo][ref[bar]]\" text:value=\"[ref[bar]]: /uri\""},
        {548,
         "[[[foo]]]\n\n[[[foo]]]: /url\n",
         "text:value=\"[[[foo]]]\" text:value=\"[[[foo]]]: /url\""},
        {549,
         "[foo][ref\\[]\n\n[ref\\[]: /uri\n",
         "text:value=\"foo\" def:dest=\"/uri\""},
        {550,
         "[bar\\\\]: /uri\n\n[bar\\\\]\n",
         "def:dest=\"/uri\" text:value=\"bar\" esc:value=\"\\\\\""},
        {551,
         "[]\n\n[]: /uri\n",
         "text:value=\"[]\" text:value=\"[]: /uri\""},
        {552,
         "[\n ]\n\n[\n ]: /uri\n",
         "text:value=\"[\" text:value=\"]\" text:value=\"[\""
         " text:value=\"]: /uri\""},
        {553,
         "[foo][]\n\n[foo]: /url \"title\"\n",
         "text:value=\"foo\" def:dest=\"/url\" def:title=\"title\""},
        {554,
         "[*foo* bar][]\n\n[*foo* bar]: /url \"title\"\n",
         "text:value=\"foo\" text:value=\" bar\" def:dest=\"/url\""
         " def:title=\"title\""},
        {555,
         "[Foo][]\n\n[foo]: /url \"title\"\n",
         "text:value=\"Foo\" def:dest=\"/url\" def:title=\"title\""},
        {556,
         "[foo] \n[]\n\n[foo]: /url \"title\"\n",
         "text:value=\"foo\" text:value=\"[]\" def:dest=\"/url\""
         " def:title=\"title\""},
        {557,
         "[foo]\n\n[foo]: /url \"title\"\n",
         "text:value=\"foo\" def:dest=\"/url\" def:title=\"title\""},
        {558,
         "[*foo* bar]\n\n[*foo* bar]: /url \"title\"\n",
         "text:value=\"foo\" text:value=\" bar\" def:dest=\"/url\""
         " def:title=\"title\""},
        {559,
         "[[*foo* bar]]\n\n[*foo* bar]: /url \"title\"\n",
         "text:value=\"[\" text:value=\"foo\" text:value=\" bar\""
         " text:value=\"]\" def:dest=\"/url\" def:title=\"title\""},
        {560,
         "[[bar [foo]\n\n[foo]: /url\n",
         "text:value=\"[[bar \" text:value=\"foo\" def:dest=\"/url\""},
        {561,
         "[Foo]\n\n[foo]: /url \"title\"\n",
         "text:value=\"Foo\" def:dest=\"/url\" def:title=\"title\""},
        {562,
         "[foo] bar\n\n[foo]: /url\n",
         "text:value=\"foo\" text:value=\" bar\" def:dest=\"/url\""},
        {563,
         "\\[foo]\n\n[foo]: /url \"title\"\n",
         "esc:value=\"[\" text:value=\"foo]\" def:dest=\"/url\""
         " def:title=\"title\""},
        {564,
         "[foo*]: /url\n\n*[foo*]\n",
         "def:dest=\"/url\" text:value=\"*\" text:value=\"foo*\""},
        {565,
         "[foo][bar]\n\n[foo]: /url1\n[bar]: /url2\n",
         "text:value=\"foo\" def:dest=\"/url1\" def:dest=\"/url2\""},
        {566,
         "[foo][]\n\n[foo]: /url1\n",
         "text:value=\"foo\" def:dest=\"/url1\""},
        {567,
         "[foo]()\n\n[foo]: /url1\n",
         "text:value=\"foo\" def:dest=\"/url1\""},
        {568,
         "[foo](not a link)\n\n[foo]: /url1\n",
         "text:value=\"foo\" text:value=\"(not a link)\""
         " def:dest=\"/url1\""},
        {569,
         "[foo][bar][baz]\n\n[baz]: /url\n",
         "text:value=\"[foo]\" text:value=\"bar\" def:dest=\"/url\""},
        {570,
         "[foo][bar][baz]\n\n[baz]: /url1\n[bar]: /url2\n",
         "text:value=\"foo\" text:value=\"baz\" def:dest=\"/url1\""
         " def:dest=\"/url2\""},
        {571,
         "[foo][bar][baz]\n\n[baz]: /url1\n[foo]: /url2\n",
         "text:value=\"[foo]\" text:value=\"bar\" def:dest=\"/url1\""
         " def:dest=\"/url2\""},
    };

    CHECK_EXAMPLES(examples);
}

/* Images (examples 572-593). */
NAOS_TEST(spec_images)
{
    static const spec_example_t examples[] = {
        {572,
         "![foo](/url \"title\")\n",
         "image:dest=\"/url\" image:title=\"title\" text:value=\"foo\""},
        {573,
         "![foo *bar*]\n\n[foo *bar*]: train.jpg \"train & tracks\"\n",
         "text:value=\"foo \" text:value=\"bar\" def:dest=\"train.jpg\""
         " def:title=\"train & tracks\""},
        {574,
         "![foo ![bar](/url)](/url2)\n",
         "image:dest=\"/url2\" text:value=\"foo \" image:dest=\"/url\""
         " text:value=\"bar\""},
        {575,
         "![foo [bar](/url)](/url2)\n",
         "image:dest=\"/url2\" text:value=\"foo \" link:dest=\"/url\""
         " text:value=\"bar\""},
        {576,
         "![foo *bar*][]\n\n[foo *bar*]: train.jpg \"train & tracks\"\n",
         "text:value=\"foo \" text:value=\"bar\" def:dest=\"train.jpg\""
         " def:title=\"train & tracks\""},
        {577,
         "![foo *bar*][foobar]\n\n[FOOBAR]: train.jpg \"train & tracks\"\n",
         "text:value=\"foo \" text:value=\"bar\" def:dest=\"train.jpg\""
         " def:title=\"train & tracks\""},
        {578,
         "![foo](train.jpg)\n",
         "image:dest=\"train.jpg\" text:value=\"foo\""},
        {579,
         "My ![foo bar](/path/to/train.jpg  \"title\"   )\n",
         "text:value=\"My \" image:dest=\"/path/to/train.jpg\""
         " image:title=\"title\" text:value=\"foo bar\""},
        {580,
         "![foo](<url>)\n",
         "image:dest=\"url\" text:value=\"foo\""},
        {581,
         "![](/url)\n",
         "image:dest=\"/url\""},
        {582,
         "![foo][bar]\n\n[bar]: /url\n",
         "text:value=\"foo\" def:dest=\"/url\""},
        {583,
         "![foo][bar]\n\n[BAR]: /url\n",
         "text:value=\"foo\" def:dest=\"/url\""},
        {584,
         "![foo][]\n\n[foo]: /url \"title\"\n",
         "text:value=\"foo\" def:dest=\"/url\" def:title=\"title\""},
        {585,
         "![*foo* bar][]\n\n[*foo* bar]: /url \"title\"\n",
         "text:value=\"foo\" text:value=\" bar\" def:dest=\"/url\""
         " def:title=\"title\""},
        {586,
         "![Foo][]\n\n[foo]: /url \"title\"\n",
         "text:value=\"Foo\" def:dest=\"/url\" def:title=\"title\""},
        {587,
         "![foo] \n[]\n\n[foo]: /url \"title\"\n",
         "text:value=\"foo\" text:value=\"[]\" def:dest=\"/url\""
         " def:title=\"title\""},
        {588,
         "![foo]\n\n[foo]: /url \"title\"\n",
         "text:value=\"foo\" def:dest=\"/url\" def:title=\"title\""},
        {589,
         "![*foo* bar]\n\n[*foo* bar]: /url \"title\"\n",
         "text:value=\"foo\" text:value=\" bar\" def:dest=\"/url\""
         " def:title=\"title\""},
        {590,
         "![[foo]]\n\n[[foo]]: /url \"title\"\n",
         "text:value=\"![[foo]]\""
         " text:value=\"[[foo]]: /url \\\"title\\\"\""},
        {591,
         "![Foo]\n\n[foo]: /url \"title\"\n",
         "text:value=\"Foo\" def:dest=\"/url\" def:title=\"title\""},
        {592,
         "!\\[foo]\n\n[foo]: /url \"title\"\n",
         "text:value=\"!\" esc:value=\"[\" text:value=\"foo]\""
         " def:dest=\"/url\" def:title=\"title\""},
        {593,
         "\\![foo]\n\n[foo]: /url \"title\"\n",
         "esc:value=\"!\" text:value=\"foo\" def:dest=\"/url\""
         " def:title=\"title\""},
    };

    CHECK_EXAMPLES(examples);
}

NAOS_SUITE(spec_text_links,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(spec_links),
    NAOS_ADD_TEST(spec_images)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(spec_text_links)
)
