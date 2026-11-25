/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * The examples of the CommonMark specification for the inlines other than
 * emphasis, links and images, checked through amark_parse: each example's
 * input is parsed with the default configuration (and the counting test
 * allocator), the tree must pass amark_tree_check, and the whole tree,
 * blocks and inlines, as amark_test_dump_tree writes it, must equal the
 * expected string of the table.
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
 * The tree keeps the source: escapes and character references are not
 * decoded and destinations are not normalized, so each node is written with
 * the bytes of its range (and a link with the bytes of its destination and
 * title). The expected strings give, for the structure that the HTML shows,
 * the source bytes that each node covers. Adjacent texts on the same line
 * are one node, so a character that starts no construct (a "<" that is no
 * autolink or HTML, an unmatched backtick string, "*" or bracket) is part of
 * the text around it.
 *
 * Sections: Backslash escapes, Entity and numeric character references,
 * Inlines, Code spans, Autolinks, Raw HTML, Hard line breaks, Soft line
 * breaks and Textual content.
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

/* Backslash escapes (examples 12-24). */
NAOS_TEST(spec_backslash_escapes)
{
    static const spec_example_t examples[] = {
        {12,
         "\\!\\\"\\#\\$\\%\\&\\'\\(\\)\\*\\+\\,\\-\\.\\/\\:\\;\\<\\=\\>\\?\\"
         "@\\[\\\\\\]\\^\\_\\`\\{\\|\\}\\~\n",
         "(doc (p (esc \"\\\\!\") (esc \"\\\\\\\"\") (esc \"\\\\#\")"
         " (esc \"\\\\$\") (esc \"\\\\%\") (esc \"\\\\&\") (esc \"\\\\'\")"
         " (esc \"\\\\(\") (esc \"\\\\)\") (esc \"\\\\*\") (esc \"\\\\+\")"
         " (esc \"\\\\,\") (esc \"\\\\-\") (esc \"\\\\.\") (esc \"\\\\/\")"
         " (esc \"\\\\:\") (esc \"\\\\;\") (esc \"\\\\<\") (esc \"\\\\=\")"
         " (esc \"\\\\>\") (esc \"\\\\?\") (esc \"\\\\@\") (esc \"\\\\[\")"
         " (esc \"\\\\\\\\\") (esc \"\\\\]\") (esc \"\\\\^\")"
         " (esc \"\\\\_\") (esc \"\\\\`\") (esc \"\\\\{\") (esc \"\\\\|\")"
         " (esc \"\\\\}\") (esc \"\\\\~\")))"},
        {13,
         "\\\t\\A\\a\\ \\3\\\317\206\\\302\253\n",
         "(doc (p"
         " (text \"\\\\\\t\\\\A\\\\a\\\\ \\\\3\\\\\\xCF\\x86\\\\\\xC2\\xAB\""
         ")))"},
        {14,
         "\\*not emphasized*\n\\<br/> not a tag\n\\[not a link](/foo)\n"
         "\\`not code`\n1\\. not a list\n\\* not a list\n\\# not a heading\n"
         "\\[foo]: /url \"not a reference\"\n"
         "\\&ouml; not a character entity\n",
         "(doc (p (esc \"\\\\*\") (text \"not emphasized*\") (sbr)"
         " (esc \"\\\\<\") (text \"br/> not a tag\") (sbr) (esc \"\\\\[\")"
         " (text \"not a link](/foo)\") (sbr) (esc \"\\\\`\")"
         " (text \"not code`\") (sbr) (text \"1\") (esc \"\\\\.\")"
         " (text \" not a list\") (sbr) (esc \"\\\\*\")"
         " (text \" not a list\") (sbr) (esc \"\\\\#\")"
         " (text \" not a heading\") (sbr) (esc \"\\\\[\")"
         " (text \"foo]: /url \\\"not a reference\\\"\") (sbr)"
         " (esc \"\\\\&\") (text \"ouml; not a character entity\")))"},
        {15,
         "\\\\*emphasis*\n",
         "(doc (p (esc \"\\\\\\\\\") (em (text \"emphasis\"))))"},
        {16,
         "foo\\\nbar\n",
         "(doc (p (text \"foo\") (br) (text \"bar\")))"},
        {17,
         "`` \\[\\` ``\n",
         "(doc (p (code \"`` \\\\[\\\\` ``\")))"},
        {18,
         "    \\[\\]\n",
         "(doc (code indented))"},
        {19,
         "~~~\n\\[\\]\n~~~\n",
         "(doc (code fenced ~ 3))"},
        {20,
         "<https://example.com?find=\\*>\n",
         "(doc (p (autolink uri \"<https://example.com?find=\\\\*>\")))"},
        {21,
         "<a href=\"/bar\\/)\">\n",
         "(doc (html))"},
        {22,
         "[foo](/bar\\* \"ti\\*tle\")\n",
         "(doc (p (link inline dest=\"/bar\\\\*\" title=\"ti\\\\*tle\""
         " (text \"foo\"))))"},
        {23,
         "[foo]\n\n[foo]: /bar\\* \"ti\\*tle\"\n",
         "(doc (p (link shortcut def=\"foo\" (text \"foo\"))) (def))"},
        {24,
         "``` foo\\+bar\nfoo\n```\n",
         "(doc (code fenced ` 3 info))"},
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
         "(doc (p (ref \"&nbsp;\") (text \" \") (ref \"&amp;\") (text \" \")"
         " (ref \"&copy;\") (text \" \") (ref \"&AElig;\") (text \" \")"
         " (ref \"&Dcaron;\") (sbr) (ref \"&frac34;\") (text \" \")"
         " (ref \"&HilbertSpace;\") (text \" \") (ref \"&DifferentialD;\")"
         " (sbr) (ref \"&ClockwiseContourIntegral;\") (text \" \")"
         " (ref \"&ngE;\")))"},
        {26,
         "&#35; &#1234; &#992; &#0;\n",
         "(doc (p (ref \"&#35;\") (text \" \") (ref \"&#1234;\")"
         " (text \" \") (ref \"&#992;\") (text \" \") (ref \"&#0;\")))"},
        {27,
         "&#X22; &#XD06; &#xcab;\n",
         "(doc (p (ref \"&#X22;\") (text \" \") (ref \"&#XD06;\")"
         " (text \" \") (ref \"&#xcab;\")))"},
        {28,
         "&nbsp &x; &#; &#x;\n&#87654321;\n&#abcdef0;\n"
         "&ThisIsNotDefined; &hi?;\n",
         "(doc (p (text \"&nbsp &x; &#; &#x;\") (sbr) (text \"&#87654321;\")"
         " (sbr) (text \"&#abcdef0;\") (sbr)"
         " (text \"&ThisIsNotDefined; &hi?;\")))"},
        {29,
         "&copy\n",
         "(doc (p (text \"&copy\")))"},
        {30,
         "&MadeUpEntity;\n",
         "(doc (p (text \"&MadeUpEntity;\")))"},
        {31,
         "<a href=\"&ouml;&ouml;.html\">\n",
         "(doc (html))"},
        {32,
         "[foo](/f&ouml;&ouml; \"f&ouml;&ouml;\")\n",
         "(doc (p"
         " (link inline dest=\"/f&ouml;&ouml;\" title=\"f&ouml;&ouml;\""
         " (text \"foo\"))))"},
        {33,
         "[foo]\n\n[foo]: /f&ouml;&ouml; \"f&ouml;&ouml;\"\n",
         "(doc (p (link shortcut def=\"foo\" (text \"foo\"))) (def))"},
        {34,
         "``` f&ouml;&ouml;\nfoo\n```\n",
         "(doc (code fenced ` 3 info))"},
        {35,
         "`f&ouml;&ouml;`\n",
         "(doc (p (code \"`f&ouml;&ouml;`\")))"},
        {36,
         "    f&ouml;f&ouml;\n",
         "(doc (code indented))"},
        {37,
         "&#42;foo&#42;\n*foo*\n",
         "(doc (p (ref \"&#42;\") (text \"foo\") (ref \"&#42;\") (sbr) (em"
         " (text \"foo\"))))"},
        {38,
         "&#42; foo\n\n* foo\n",
         "(doc (p (ref \"&#42;\") (text \" foo\")) (ul * tight (li (p"
         " (text \"foo\")))))"},
        {39,
         "foo&#10;&#10;bar\n",
         "(doc (p (text \"foo\") (ref \"&#10;\") (ref \"&#10;\")"
         " (text \"bar\")))"},
        {40,
         "&#9;foo\n",
         "(doc (p (ref \"&#9;\") (text \"foo\")))"},
        {41,
         "[a](url &quot;tit&quot;)\n",
         "(doc (p (text \"[a](url \") (ref \"&quot;\") (text \"tit\")"
         " (ref \"&quot;\") (text \")\")))"},
    };

    CHECK_EXAMPLES(examples);
}

/* Inlines (example 327). */
NAOS_TEST(spec_inlines)
{
    static const spec_example_t examples[] = {
        {327,
         "`hi`lo`\n",
         "(doc (p (code \"`hi`\") (text \"lo`\")))"},
    };

    CHECK_EXAMPLES(examples);
}

/* Code spans (examples 328-349). */
NAOS_TEST(spec_code_spans)
{
    static const spec_example_t examples[] = {
        {328,
         "`foo`\n",
         "(doc (p (code \"`foo`\")))"},
        {329,
         "`` foo ` bar ``\n",
         "(doc (p (code \"`` foo ` bar ``\")))"},
        {330,
         "` `` `\n",
         "(doc (p (code \"` `` `\")))"},
        {331,
         "`  ``  `\n",
         "(doc (p (code \"`  ``  `\")))"},
        {332,
         "` a`\n",
         "(doc (p (code \"` a`\")))"},
        {333,
         "`\302\240b\302\240`\n",
         "(doc (p (code \"`\\xC2\\xA0b\\xC2\\xA0`\")))"},
        {334,
         "`\302\240`\n`  `\n",
         "(doc (p (code \"`\\xC2\\xA0`\") (sbr) (code \"`  `\")))"},
        {335,
         "``\nfoo\nbar  \nbaz\n``\n",
         "(doc (p (code \"``\\nfoo\\nbar  \\nbaz\\n``\")))"},
        {336,
         "``\nfoo \n``\n",
         "(doc (p (code \"``\\nfoo \\n``\")))"},
        {337,
         "`foo   bar \nbaz`\n",
         "(doc (p (code \"`foo   bar \\nbaz`\")))"},
        {338,
         "`foo\\`bar`\n",
         "(doc (p (code \"`foo\\\\`\") (text \"bar`\")))"},
        {339,
         "``foo`bar``\n",
         "(doc (p (code \"``foo`bar``\")))"},
        {340,
         "` foo `` bar `\n",
         "(doc (p (code \"` foo `` bar `\")))"},
        {341,
         "*foo`*`\n",
         "(doc (p (text \"*foo\") (code \"`*`\")))"},
        {342,
         "[not a `link](/foo`)\n",
         "(doc (p (text \"[not a \") (code \"`link](/foo`\") (text \")\")))"},
        {343,
         "`<a href=\"`\">`\n",
         "(doc (p (code \"`<a href=\\\"`\") (text \"\\\">`\")))"},
        {344,
         "<a href=\"`\">`\n",
         "(doc (p (html \"<a href=\\\"`\\\">\") (text \"`\")))"},
        {345,
         "`<https://foo.bar.`baz>`\n",
         "(doc (p (code \"`<https://foo.bar.`\") (text \"baz>`\")))"},
        {346,
         "<https://foo.bar.`baz>`\n",
         "(doc (p (autolink uri \"<https://foo.bar.`baz>\") (text \"`\")))"},
        {347,
         "```foo``\n",
         "(doc (p (text \"```foo``\")))"},
        {348,
         "`foo\n",
         "(doc (p (text \"`foo\")))"},
        {349,
         "`foo``bar``\n",
         "(doc (p (text \"`foo\") (code \"``bar``\")))"},
    };

    CHECK_EXAMPLES(examples);
}

/* Autolinks (examples 594-612). */
NAOS_TEST(spec_autolinks)
{
    static const spec_example_t examples[] = {
        {594,
         "<http://foo.bar.baz>\n",
         "(doc (p (autolink uri \"<http://foo.bar.baz>\")))"},
        {595,
         "<https://foo.bar.baz/test?q=hello&id=22&boolean>\n",
         "(doc (p"
         " (autolink uri \"<https://foo.bar.baz/test?q=hello&id=22&boolean>"
         "\")))"},
        {596,
         "<irc://foo.bar:2233/baz>\n",
         "(doc (p (autolink uri \"<irc://foo.bar:2233/baz>\")))"},
        {597,
         "<MAILTO:FOO@BAR.BAZ>\n",
         "(doc (p (autolink uri \"<MAILTO:FOO@BAR.BAZ>\")))"},
        {598,
         "<a+b+c:d>\n",
         "(doc (p (autolink uri \"<a+b+c:d>\")))"},
        {599,
         "<made-up-scheme://foo,bar>\n",
         "(doc (p (autolink uri \"<made-up-scheme://foo,bar>\")))"},
        {600,
         "<https://../>\n",
         "(doc (p (autolink uri \"<https://../>\")))"},
        {601,
         "<localhost:5001/foo>\n",
         "(doc (p (autolink uri \"<localhost:5001/foo>\")))"},
        {602,
         "<https://foo.bar/baz bim>\n",
         "(doc (p (text \"<https://foo.bar/baz bim>\")))"},
        {603,
         "<https://example.com/\\[\\>\n",
         "(doc (p (autolink uri \"<https://example.com/\\\\[\\\\>\")))"},
        {604,
         "<foo@bar.example.com>\n",
         "(doc (p (autolink email \"<foo@bar.example.com>\")))"},
        {605,
         "<foo+special@Bar.baz-bar0.com>\n",
         "(doc (p (autolink email \"<foo+special@Bar.baz-bar0.com>\")))"},
        {606,
         "<foo\\+@bar.example.com>\n",
         "(doc (p (text \"<foo\") (esc \"\\\\+\")"
         " (text \"@bar.example.com>\")))"},
        {607,
         "<>\n",
         "(doc (p (text \"<>\")))"},
        {608,
         "< https://foo.bar >\n",
         "(doc (p (text \"< https://foo.bar >\")))"},
        {609,
         "<m:abc>\n",
         "(doc (p (text \"<m:abc>\")))"},
        {610,
         "<foo.bar.baz>\n",
         "(doc (p (text \"<foo.bar.baz>\")))"},
        {611,
         "https://example.com\n",
         "(doc (p (text \"https://example.com\")))"},
        {612,
         "foo@bar.example.com\n",
         "(doc (p (text \"foo@bar.example.com\")))"},
    };

    CHECK_EXAMPLES(examples);
}

/* Raw HTML (examples 613-632). */
NAOS_TEST(spec_raw_html)
{
    static const spec_example_t examples[] = {
        {613,
         "<a><bab><c2c>\n",
         "(doc (p (html \"<a>\") (html \"<bab>\") (html \"<c2c>\")))"},
        {614,
         "<a/><b2/>\n",
         "(doc (p (html \"<a/>\") (html \"<b2/>\")))"},
        {615,
         "<a  /><b2\ndata=\"foo\" >\n",
         "(doc (p (html \"<a  />\") (html \"<b2\\ndata=\\\"foo\\\" >\")))"},
        {616,
         "<a foo=\"bar\" bam = 'baz <em>\"</em>'\n"
         "_boolean zoop:33=zoop:33 />\n",
         "(doc (p"
         " (html \"<a foo=\\\"bar\\\" bam = 'baz <em>\\\"</em>'\\n_boolean z"
         "oop:33=zoop:33 />\")))"},
        {617,
         "Foo <responsive-image src=\"foo.jpg\" />\n",
         "(doc (p (text \"Foo \")"
         " (html \"<responsive-image src=\\\"foo.jpg\\\" />\")))"},
        {618,
         "<33> <__>\n",
         "(doc (p (text \"<33> <__>\")))"},
        {619,
         "<a h*#ref=\"hi\">\n",
         "(doc (p (text \"<a h*#ref=\\\"hi\\\">\")))"},
        {620,
         "<a href=\"hi'> <a href=hi'>\n",
         "(doc (p (text \"<a href=\\\"hi'> <a href=hi'>\")))"},
        {621,
         "< a><\nfoo><bar/ >\n<foo bar=baz\nbim!bop />\n",
         "(doc (p (text \"< a><\") (sbr) (text \"foo><bar/ >\") (sbr)"
         " (text \"<foo bar=baz\") (sbr) (text \"bim!bop />\")))"},
        {622,
         "<a href='bar'title=title>\n",
         "(doc (p (text \"<a href='bar'title=title>\")))"},
        {623,
         "</a></foo >\n",
         "(doc (p (html \"</a>\") (html \"</foo >\")))"},
        {624,
         "</a href=\"foo\">\n",
         "(doc (p (text \"</a href=\\\"foo\\\">\")))"},
        {625,
         "foo <!-- this is a --\ncomment - with hyphens -->\n",
         "(doc (p (text \"foo \")"
         " (html \"<!-- this is a --\\ncomment - with hyphens -->\")))"},
        {626,
         "foo <!--> foo -->\n\nfoo <!---> foo -->\n",
         "(doc (p (text \"foo \") (html \"<!-->\") (text \" foo -->\")) (p"
         " (text \"foo \") (html \"<!--->\") (text \" foo -->\")))"},
        {627,
         "foo <?php echo $a; ?>\n",
         "(doc (p (text \"foo \") (html \"<?php echo $a; ?>\")))"},
        {628,
         "foo <!ELEMENT br EMPTY>\n",
         "(doc (p (text \"foo \") (html \"<!ELEMENT br EMPTY>\")))"},
        {629,
         "foo <![CDATA[>&<]]>\n",
         "(doc (p (text \"foo \") (html \"<![CDATA[>&<]]>\")))"},
        {630,
         "foo <a href=\"&ouml;\">\n",
         "(doc (p (text \"foo \") (html \"<a href=\\\"&ouml;\\\">\")))"},
        {631,
         "foo <a href=\"\\*\">\n",
         "(doc (p (text \"foo \") (html \"<a href=\\\"\\\\*\\\">\")))"},
        {632,
         "<a href=\"\\\"\">\n",
         "(doc (p (text \"<a href=\\\"\") (esc \"\\\\\\\"\")"
         " (text \"\\\">\")))"},
    };

    CHECK_EXAMPLES(examples);
}

/* Hard line breaks (examples 633-647). */
NAOS_TEST(spec_hard_line_breaks)
{
    static const spec_example_t examples[] = {
        {633,
         "foo  \nbaz\n",
         "(doc (p (text \"foo\") (br) (text \"baz\")))"},
        {634,
         "foo\\\nbaz\n",
         "(doc (p (text \"foo\") (br) (text \"baz\")))"},
        {635,
         "foo       \nbaz\n",
         "(doc (p (text \"foo\") (br) (text \"baz\")))"},
        {636,
         "foo  \n     bar\n",
         "(doc (p (text \"foo\") (br) (text \"bar\")))"},
        {637,
         "foo\\\n     bar\n",
         "(doc (p (text \"foo\") (br) (text \"bar\")))"},
        {638,
         "*foo  \nbar*\n",
         "(doc (p (em (text \"foo\") (br) (text \"bar\"))))"},
        {639,
         "*foo\\\nbar*\n",
         "(doc (p (em (text \"foo\") (br) (text \"bar\"))))"},
        {640,
         "`code  \nspan`\n",
         "(doc (p (code \"`code  \\nspan`\")))"},
        {641,
         "`code\\\nspan`\n",
         "(doc (p (code \"`code\\\\\\nspan`\")))"},
        {642,
         "<a href=\"foo  \nbar\">\n",
         "(doc (p (html \"<a href=\\\"foo  \\nbar\\\">\")))"},
        {643,
         "<a href=\"foo\\\nbar\">\n",
         "(doc (p (html \"<a href=\\\"foo\\\\\\nbar\\\">\")))"},
        {644,
         "foo\\\n",
         "(doc (p (text \"foo\\\\\")))"},
        {645,
         "foo  \n",
         "(doc (p (text \"foo\")))"},
        {646,
         "### foo\\\n",
         "(doc (h 3 atx (text \"foo\\\\\")))"},
        {647,
         "### foo  \n",
         "(doc (h 3 atx (text \"foo\")))"},
    };

    CHECK_EXAMPLES(examples);
}

/* Soft line breaks (examples 648-649). */
NAOS_TEST(spec_soft_line_breaks)
{
    static const spec_example_t examples[] = {
        {648,
         "foo\nbaz\n",
         "(doc (p (text \"foo\") (sbr) (text \"baz\")))"},
        {649,
         "foo \n baz\n",
         "(doc (p (text \"foo\") (sbr) (text \"baz\")))"},
    };

    CHECK_EXAMPLES(examples);
}

/* Textual content (examples 650-652). */
NAOS_TEST(spec_textual_content)
{
    static const spec_example_t examples[] = {
        {650,
         "hello $.;'there\n",
         "(doc (p (text \"hello $.;'there\")))"},
        {651,
         "Foo \317\207\317\201\341\277\206\316\275\n",
         "(doc (p"
         " (text \"Foo \\xCF\\x87\\xCF\\x81\\xE1\\xBF\\x86\\xCE\\xBD\")))"},
        {652,
         "Multiple     spaces\n",
         "(doc (p (text \"Multiple     spaces\")))"},
    };

    CHECK_EXAMPLES(examples);
}

NAOS_SUITE(spec_inlines_other,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(spec_backslash_escapes),
    NAOS_ADD_TEST(spec_entity_references),
    NAOS_ADD_TEST(spec_inlines),
    NAOS_ADD_TEST(spec_code_spans),
    NAOS_ADD_TEST(spec_autolinks),
    NAOS_ADD_TEST(spec_raw_html),
    NAOS_ADD_TEST(spec_hard_line_breaks),
    NAOS_ADD_TEST(spec_soft_line_breaks),
    NAOS_ADD_TEST(spec_textual_content)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(spec_inlines_other)
)
