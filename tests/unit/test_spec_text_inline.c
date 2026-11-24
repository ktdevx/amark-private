/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * The examples of the CommonMark specification for escapes, character
 * references, code spans and autolinks, checked through the values of their
 * nodes: each example's input is parsed with the default configuration (and
 * the counting test allocator), and the values, destinations, titles and
 * info strings of its nodes, as amark_test_dump_text writes them, must equal
 * the expected string of the table.
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
 * The value of an autolink is its content without "<" and ">", as written
 * (the "mailto:" of the HTML of an email address is not part of it). Raw
 * HTML, HTML blocks, breaks and the containers have no value, so they do not
 * appear in the expected strings; texts on the same line that the HTML shows
 * together are one node (as test_spec_inline.c gives them), but escapes and
 * character references are nodes of their own.
 *
 * Sections: Backslash escapes, Entity and numeric character references,
 * Code spans and Autolinks.
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

/* Backslash escapes (examples 12-24). */
NAOS_TEST(spec_backslash_escapes)
{
    static const spec_example_t examples[] = {
        {12,
         "\\!\\\"\\#\\$\\%\\&\\'\\(\\)\\*\\+\\,\\-\\.\\/\\:\\;\\<\\=\\>\\?\\"
         "@\\[\\\\\\]\\^\\_\\`\\{\\|\\}\\~\n",
         "esc:value=\"!\" esc:value=\"\\\"\" esc:value=\"#\""
         " esc:value=\"$\" esc:value=\"%\" esc:value=\"&\""
         " esc:value=\"'\" esc:value=\"(\" esc:value=\")\""
         " esc:value=\"*\" esc:value=\"+\" esc:value=\",\""
         " esc:value=\"-\" esc:value=\".\" esc:value=\"/\""
         " esc:value=\":\" esc:value=\";\" esc:value=\"<\""
         " esc:value=\"=\" esc:value=\">\" esc:value=\"?\""
         " esc:value=\"@\" esc:value=\"[\" esc:value=\"\\\\\""
         " esc:value=\"]\" esc:value=\"^\" esc:value=\"_\""
         " esc:value=\"`\" esc:value=\"{\" esc:value=\"|\""
         " esc:value=\"}\" esc:value=\"~\""},
        {13,
         "\\\t\\A\\a\\ \\3\\\317\206\\\302\253\n",
         "text:value=\"\\\\\\t\\\\A\\\\a\\\\ \\\\3\\\\\317\206\\\\\302\253\""},
        {14,
         "\\*not emphasized*\n\\<br/> not a tag\n\\[not a link](/foo)\n"
         "\\`not code`\n1\\. not a list\n\\* not a list\n\\# not a heading\n"
         "\\[foo]: /url \"not a reference\"\n"
         "\\&ouml; not a character entity\n",
         "esc:value=\"*\" text:value=\"not emphasized*\""
         " esc:value=\"<\" text:value=\"br/> not a tag\""
         " esc:value=\"[\" text:value=\"not a link](/foo)\""
         " esc:value=\"`\" text:value=\"not code`\""
         " text:value=\"1\" esc:value=\".\" text:value=\" not a list\""
         " esc:value=\"*\" text:value=\" not a list\""
         " esc:value=\"#\" text:value=\" not a heading\""
         " esc:value=\"[\" text:value=\"foo]: /url \\\"not a reference\\\"\""
         " esc:value=\"&\" text:value=\"ouml; not a character entity\""},
        {15,
         "\\\\*emphasis*\n",
         "esc:value=\"\\\\\" text:value=\"emphasis\""},
        {16,
         "foo\\\nbar\n",
         "text:value=\"foo\" text:value=\"bar\""},
        {17,
         "`` \\[\\` ``\n",
         "code:value=\"\\\\[\\\\`\""},
        {18,
         "    \\[\\]\n",
         "code:value=\"\\\\[\\\\]\\n\""},
        {19,
         "~~~\n\\[\\]\n~~~\n",
         "code:value=\"\\\\[\\\\]\\n\""},
        {20,
         "<https://example.com?find=\\*>\n",
         "autolink:value=\"https://example.com?find=\\\\*\""},
        {21,
         "<a href=\"/bar\\/)\">\n",
         ""},
        {22,
         "[foo](/bar\\* \"ti\\*tle\")\n",
         "link:dest=\"/bar*\" link:title=\"ti*tle\" text:value=\"foo\""},
        {23,
         "[foo]\n\n[foo]: /bar\\* \"ti\\*tle\"\n",
         "text:value=\"foo\" def:dest=\"/bar*\" def:title=\"ti*tle\""},
        {24,
         "``` foo\\+bar\nfoo\n```\n",
         "code:value=\"foo\\n\" code:info=\"foo+bar\""},
    };

    CHECK_EXAMPLES(examples);
}

/* Entity and numeric character references (examples 25-41). */
NAOS_TEST(spec_entity_references)
{
    static const spec_example_t examples[] = {
        {25,
         "&nbsp; &amp; &copy; &AElig; &Dcaron;\n"
         "&frac34; &HilbertSpace; &DifferentialD;\n"
         "&ClockwiseContourIntegral; &ngE;\n",
         "ref:value=\"\302\240\" text:value=\" \" ref:value=\"&\""
         " text:value=\" \" ref:value=\"\302\251\" text:value=\" \""
         " ref:value=\"\303\206\" text:value=\" \" ref:value=\"\304\216\""
         " ref:value=\"\302\276\" text:value=\" \""
         " ref:value=\"\342\204\213\" text:value=\" \""
         " ref:value=\"\342\205\206\" ref:value=\"\342\210\262\""
         " text:value=\" \" ref:value=\"\342\211\247\314\270\""},
        {26,
         "&#35; &#1234; &#992; &#0;\n",
         "ref:value=\"#\" text:value=\" \" ref:value=\"\323\222\""
         " text:value=\" \" ref:value=\"\317\240\" text:value=\" \""
         " ref:value=\"\357\277\275\""},
        {27,
         "&#X22; &#XD06; &#xcab;\n",
         "ref:value=\"\\\"\" text:value=\" \" ref:value=\"\340\264\206\""
         " text:value=\" \" ref:value=\"\340\262\253\""},
        {28,
         "&nbsp &x; &#; &#x;\n&#87654321;\n&#abcdef0;\n"
         "&ThisIsNotDefined; &hi?;\n",
         "text:value=\"&nbsp &x; &#; &#x;\" text:value=\"&#87654321;\""
         " text:value=\"&#abcdef0;\""
         " text:value=\"&ThisIsNotDefined; &hi?;\""},
        {29,
         "&copy\n",
         "text:value=\"&copy\""},
        {30,
         "&MadeUpEntity;\n",
         "text:value=\"&MadeUpEntity;\""},
        {31,
         "<a href=\"&ouml;&ouml;.html\">\n",
         ""},
        {32,
         "[foo](/f&ouml;&ouml; \"f&ouml;&ouml;\")\n",
         "link:dest=\"/f\303\266\303\266\" link:title=\"f\303\266\303\266\""
         " text:value=\"foo\""},
        {33,
         "[foo]\n\n[foo]: /f&ouml;&ouml; \"f&ouml;&ouml;\"\n",
         "text:value=\"foo\" def:dest=\"/f\303\266\303\266\""
         " def:title=\"f\303\266\303\266\""},
        {34,
         "``` f&ouml;&ouml;\nfoo\n```\n",
         "code:value=\"foo\\n\" code:info=\"f\303\266\303\266\""},
        {35,
         "`f&ouml;&ouml;`\n",
         "code:value=\"f&ouml;&ouml;\""},
        {36,
         "    f&ouml;f&ouml;\n",
         "code:value=\"f&ouml;f&ouml;\\n\""},
        {37,
         "&#42;foo&#42;\n*foo*\n",
         "ref:value=\"*\" text:value=\"foo\" ref:value=\"*\""
         " text:value=\"foo\""},
        {38,
         "&#42; foo\n\n* foo\n",
         "ref:value=\"*\" text:value=\" foo\" text:value=\"foo\""},
        {39,
         "foo&#10;&#10;bar\n",
         "text:value=\"foo\" ref:value=\"\\n\" ref:value=\"\\n\""
         " text:value=\"bar\""},
        {40,
         "&#9;foo\n",
         "ref:value=\"\\t\" text:value=\"foo\""},
        {41,
         "[a](url &quot;tit&quot;)\n",
         "text:value=\"[a](url \" ref:value=\"\\\"\" text:value=\"tit\""
         " ref:value=\"\\\"\" text:value=\")\""},
    };

    CHECK_EXAMPLES(examples);
}

/* Code spans (examples 328-349). */
NAOS_TEST(spec_code_spans)
{
    static const spec_example_t examples[] = {
        {328,
         "`foo`\n",
         "code:value=\"foo\""},
        {329,
         "`` foo ` bar ``\n",
         "code:value=\"foo ` bar\""},
        {330,
         "` `` `\n",
         "code:value=\"``\""},
        {331,
         "`  ``  `\n",
         "code:value=\" `` \""},
        {332,
         "` a`\n",
         "code:value=\" a\""},
        {333,
         "`\302\240b\302\240`\n",
         "code:value=\"\302\240b\302\240\""},
        {334,
         "`\302\240`\n`  `\n",
         "code:value=\"\302\240\" code:value=\"  \""},
        {335,
         "``\nfoo\nbar  \nbaz\n``\n",
         "code:value=\"foo bar   baz\""},
        {336,
         "``\nfoo \n``\n",
         "code:value=\"foo \""},
        {337,
         "`foo   bar \nbaz`\n",
         "code:value=\"foo   bar  baz\""},
        {338,
         "`foo\\`bar`\n",
         "code:value=\"foo\\\\\" text:value=\"bar`\""},
        {339,
         "``foo`bar``\n",
         "code:value=\"foo`bar\""},
        {340,
         "` foo `` bar `\n",
         "code:value=\"foo `` bar\""},
        {341,
         "*foo`*`\n",
         "text:value=\"*foo\" code:value=\"*\""},
        {342,
         "[not a `link](/foo`)\n",
         "text:value=\"[not a \" code:value=\"link](/foo\" text:value=\")\""},
        {343,
         "`<a href=\"`\">`\n",
         "code:value=\"<a href=\\\"\" text:value=\"\\\">`\""},
        {344,
         "<a href=\"`\">`\n",
         "text:value=\"`\""},
        {345,
         "`<https://foo.bar.`baz>`\n",
         "code:value=\"<https://foo.bar.\" text:value=\"baz>`\""},
        {346,
         "<https://foo.bar.`baz>`\n",
         "autolink:value=\"https://foo.bar.`baz\" text:value=\"`\""},
        {347,
         "```foo``\n",
         "text:value=\"```foo``\""},
        {348,
         "`foo\n",
         "text:value=\"`foo\""},
        {349,
         "`foo``bar``\n",
         "text:value=\"`foo\" code:value=\"bar\""},
    };

    CHECK_EXAMPLES(examples);
}

/* Autolinks (examples 594-612). */
NAOS_TEST(spec_autolinks)
{
    static const spec_example_t examples[] = {
        {594,
         "<http://foo.bar.baz>\n",
         "autolink:value=\"http://foo.bar.baz\""},
        {595,
         "<https://foo.bar.baz/test?q=hello&id=22&boolean>\n",
         "autolink:value=\"https://foo.bar.baz/test?q=hello&id=22&boolean\""},
        {596,
         "<irc://foo.bar:2233/baz>\n",
         "autolink:value=\"irc://foo.bar:2233/baz\""},
        {597,
         "<MAILTO:FOO@BAR.BAZ>\n",
         "autolink:value=\"MAILTO:FOO@BAR.BAZ\""},
        {598,
         "<a+b+c:d>\n",
         "autolink:value=\"a+b+c:d\""},
        {599,
         "<made-up-scheme://foo,bar>\n",
         "autolink:value=\"made-up-scheme://foo,bar\""},
        {600,
         "<https://../>\n",
         "autolink:value=\"https://../\""},
        {601,
         "<localhost:5001/foo>\n",
         "autolink:value=\"localhost:5001/foo\""},
        {602,
         "<https://foo.bar/baz bim>\n",
         "text:value=\"<https://foo.bar/baz bim>\""},
        {603,
         "<https://example.com/\\[\\>\n",
         "autolink:value=\"https://example.com/\\\\[\\\\\""},
        {604,
         "<foo@bar.example.com>\n",
         "autolink:value=\"foo@bar.example.com\""},
        {605,
         "<foo+special@Bar.baz-bar0.com>\n",
         "autolink:value=\"foo+special@Bar.baz-bar0.com\""},
        {606,
         "<foo\\+@bar.example.com>\n",
         "text:value=\"<foo\" esc:value=\"+\""
         " text:value=\"@bar.example.com>\""},
        {607,
         "<>\n",
         "text:value=\"<>\""},
        {608,
         "< https://foo.bar >\n",
         "text:value=\"< https://foo.bar >\""},
        {609,
         "<m:abc>\n",
         "text:value=\"<m:abc>\""},
        {610,
         "<foo.bar.baz>\n",
         "text:value=\"<foo.bar.baz>\""},
        {611,
         "https://example.com\n",
         "text:value=\"https://example.com\""},
        {612,
         "foo@bar.example.com\n",
         "text:value=\"foo@bar.example.com\""},
    };

    CHECK_EXAMPLES(examples);
}

NAOS_SUITE(spec_text_inline,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(spec_backslash_escapes),
    NAOS_ADD_TEST(spec_entity_references),
    NAOS_ADD_TEST(spec_code_spans),
    NAOS_ADD_TEST(spec_autolinks)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(spec_text_inline)
)
