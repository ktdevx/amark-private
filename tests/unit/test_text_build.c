/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of the rules of the text extraction (src/text_build.h), on
 * documents made by amark_parse. PLAIN: the range of each non-container
 * block kind and each inline kind reads as written; a range on one line is
 * the same as the source; trailing spaces, escapes, character references and
 * syntax delimiters stay as written; pad spaces replace the tab at the
 * record, other tabs stay; line endings of the three kinds become '\n'.
 * DECODED: a backslash before each ASCII punctuation character gives that
 * character alone, any other backslash stays; a character reference gives
 * its value, any other '&' stays; the output is not read again; escapes and
 * references do not run past the end of a line or of the range; titles over
 * several lines leave out container prefixes and the leading whitespace of
 * their continuation lines; spaces and parentheses in a destination stay.
 * CODE_SPAN: each line ending becomes one space; one space is stripped from
 * each end only when both ends are spaces and the content is not all spaces;
 * tabs, other spaces, backslashes and '&' stay; container prefixes and the
 * leading whitespace of continuation lines are left out.
 */

#include "document.h"
#include "line_index.h"
#include "test_alloc.h"
#include "text_build.h"
#include "text_sink.h"

#include <amark/amark.h>

#include <naos/naos.h>

#include <stddef.h>
#include <string.h>

/*
 * Fixture. NAOS_REQUIRE_* leaves a test with longjmp, so the document a test
 * makes is kept in a static variable, and teardown destroys it and checks
 * that no block is left and that every call received its context value.
 */
#define OUT_SIZE 256

static amark_test_alloc_t g_state;
static amark_config_t g_config;
static amark_document_t *g_doc;
static unsigned char g_out[OUT_SIZE];
static size_t g_length;

NAOS_SETUP(counting)
{
    amark_test_alloc_init(&g_state);
    amark_config_init(&g_config);
    g_config.allocator = amark_test_allocator(&g_state);
    g_doc = NULL;
    g_length = 0;
}

NAOS_TEARDOWN(counting)
{
    amark_document_destroy(g_doc);
    g_doc = NULL;
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
    NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
}

/* Parses content (a C string) into g_doc, destroying any earlier one. */
static void parse(const char *content)
{
    amark_document_destroy(g_doc);
    g_doc = NULL;
    NAOS_REQUIRE_INT_EQ(amark_parse(content, strlen(content), &g_config,
                                    &g_doc),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_NOT_NULL(g_doc);
}

/* Returns the next node after node in document order, or none. */
static amark_node_id_t next_node(amark_node_id_t node)
{
    amark_node_id_t next = amark_node_first_child(g_doc, node);

    while (next == AMARK_NODE_NONE && node != AMARK_NODE_NONE) {
        next = amark_node_next_sibling(g_doc, node);
        if (next == AMARK_NODE_NONE) {
            node = amark_node_parent(g_doc, node);
        }
    }
    return next;
}

/* Returns the n-th node (from 0) of kind in document order, or none. */
static amark_node_id_t find(amark_kind_t kind, size_t n)
{
    amark_node_id_t node;

    for (node = amark_document_root(g_doc); node != AMARK_NODE_NONE;
         node = next_node(node)) {
        if (amark_node_kind(g_doc, node) == kind) {
            if (n == 0) {
                return node;
            }
            n--;
        }
    }
    return AMARK_NODE_NONE;
}

/* Writes [start, end) from first_line with rule into g_out. */
static void build_rule(size_t start, size_t end, size_t first_line,
                       amark_text_rule_t rule)
{
    amark_text_sink_t sink;

    memset(g_out, 0, sizeof(g_out));
    amark_text_sink_init(&sink, g_out, OUT_SIZE - 1);
    amark_text_build_range(g_doc, start, end, first_line, rule, &sink);
    g_length = sink.length;
    NAOS_REQUIRE_UINT_LT(g_length, OUT_SIZE);
}

/* Writes [start, end) from first_line with PLAIN into g_out. */
static void build_lines(size_t start, size_t end, size_t first_line)
{
    build_rule(start, end, first_line, AMARK_TEXT_RULE_PLAIN);
}

/* Returns the line that holds offset. */
static size_t line_at(size_t offset)
{
    return amark_line_index_line_at(amark_document_line_index(g_doc), offset);
}

/* Writes [start, end) with PLAIN, from the line that holds start. */
static void build(size_t start, size_t end)
{
    build_lines(start, end, line_at(start));
}

/* Writes the range of node with PLAIN into g_out. */
static void build_node_id(amark_node_id_t node)
{
    amark_range_t range;

    NAOS_REQUIRE_UINT_NE(node, AMARK_NODE_NONE);
    NAOS_REQUIRE_TRUE(amark_node_range(g_doc, node, &range));
    build_lines(range.start, range.end, amark_node_start_line(g_doc, node));
}

/* Checks that the last build wrote exactly want (a C string). */
static void check_out(const char *want)
{
    NAOS_CHECK_UINT_EQ(g_length, strlen(want));
    NAOS_CHECK_STR_EQ((const char *)g_out, want);
}

/* Checks the PLAIN text of the n-th node of kind. */
static void check_node(amark_kind_t kind, size_t n, const char *want)
{
    build_node_id(find(kind, n));
    check_out(want);
}

/* --- Non-container block kinds ------------------------------------------- */

NAOS_TEST(thematic_break)
{
    parse("- - -\n");
    check_node(AMARK_KIND_THEMATIC_BREAK, 0, "- - -");
}

NAOS_TEST(atx_heading_starts_at_the_number_signs)
{
    parse("## Hi ##\n");
    check_node(AMARK_KIND_HEADING, 0, "## Hi ##");
}

NAOS_TEST(setext_heading_keeps_its_underline)
{
    parse("Hi\nthere\n===\n");
    check_node(AMARK_KIND_HEADING, 0, "Hi\nthere\n===");
}

NAOS_TEST(fenced_code_block_keeps_its_fences)
{
    parse("``` c\nx\n  y\n```\n");
    check_node(AMARK_KIND_CODE_BLOCK, 0, "``` c\nx\n  y\n```");
}

NAOS_TEST(indented_code_block_leaves_out_the_indentation)
{
    parse("    a\n      b\n");
    check_node(AMARK_KIND_CODE_BLOCK, 0, "a\n  b");
}

NAOS_TEST(html_block)
{
    parse("<div>\n*x*\n</div>\n");
    check_node(AMARK_KIND_HTML_BLOCK, 0, "<div>\n*x*\n</div>");
}

NAOS_TEST(paragraph)
{
    parse("a *b*\n  c\n");
    check_node(AMARK_KIND_PARAGRAPH, 0, "a *b*\nc");
}

NAOS_TEST(link_reference_definition)
{
    parse("[a]:\n  /u\n  'the\n  title'\n");
    check_node(AMARK_KIND_LINK_REFERENCE_DEFINITION, 0,
               "[a]:\n/u\n'the\ntitle'");
}

/* --- Inline kinds -------------------------------------------------------- */

NAOS_TEST(inline_kinds)
{
    parse("x `c` *e* **s** [l](/u) ![i](/v \"t\") <http://a> <b>"
          " \\* &amp; y  \nz\\\nw\nv\n");
    check_node(AMARK_KIND_TEXT, 0, "x ");
    check_node(AMARK_KIND_CODE_SPAN, 0, "`c`");
    check_node(AMARK_KIND_EMPHASIS, 0, "*e*");
    check_node(AMARK_KIND_STRONG, 0, "**s**");
    check_node(AMARK_KIND_LINK, 0, "[l](/u)");
    check_node(AMARK_KIND_IMAGE, 0, "![i](/v \"t\")");
    check_node(AMARK_KIND_AUTOLINK, 0, "<http://a>");
    check_node(AMARK_KIND_RAW_HTML, 0, "<b>");
    check_node(AMARK_KIND_BACKSLASH_ESCAPE, 0, "\\*");
    check_node(AMARK_KIND_CHARACTER_REFERENCE, 0, "&amp;");
    check_node(AMARK_KIND_HARD_BREAK, 0, "  \n");
    check_node(AMARK_KIND_HARD_BREAK, 1, "\\\n");
    check_node(AMARK_KIND_SOFT_BREAK, 0, "\n");
}

NAOS_TEST(inline_node_over_several_lines_in_a_block_quote)
{
    /* The link text runs over two lines; the "> " prefix is left out. */
    parse("> [a\n> b](/u)\n");
    check_node(AMARK_KIND_LINK, 0, "[a\nb](/u)");
}

/* --- A range on one line is the source ----------------------------------- */

NAOS_TEST(every_node_on_one_line_is_the_source)
{
    static const char content[] =
        "# a *b* `c` [d](/e 'f') &amp; &#35; \\* <g> <http://h> ![i](j) #";
    amark_node_id_t node;
    size_t count = 0;

    parse(content);
    for (node = next_node(amark_document_root(g_doc));
         node != AMARK_NODE_NONE; node = next_node(node)) {
        amark_range_t range;

        NAOS_REQUIRE_TRUE(amark_node_range(g_doc, node, &range));
        build_node_id(node);
        NAOS_CHECK_MEM_EQ(g_out, g_length, content + range.start,
                          range.end - range.start);
        count++;
    }
    NAOS_CHECK_UINT_GT(count, 10);
}

/* --- As written ---------------------------------------------------------- */

NAOS_TEST(trailing_spaces_stay)
{
    parse("```\nx  \ny\t\n```\n");
    check_node(AMARK_KIND_CODE_BLOCK, 0, "```\nx  \ny\t\n```");

    parse("a   \nb\n");
    check_node(AMARK_KIND_PARAGRAPH, 0, "a   \nb");
}

NAOS_TEST(escapes_references_and_delimiters_stay)
{
    parse("\\* \\\\ &amp; &#x41; &copy; &bogus; `x` _y_ [z](</u v> \"t\")\n");
    check_node(AMARK_KIND_PARAGRAPH, 0,
               "\\* \\\\ &amp; &#x41; &copy; &bogus; `x` _y_ "
               "[z](</u v> \"t\")");
}

/* --- Pad and tabs -------------------------------------------------------- */

NAOS_TEST(pad_spaces_replace_the_tab_at_the_record)
{
    /* The tab line has 3 columns left after the fence's indentation. */
    parse(" ```\n\tx\n```");
    check_node(AMARK_KIND_CODE_BLOCK, 0, "```\n   x\n```");

    parse("  ```\n\tx\n```");
    check_node(AMARK_KIND_CODE_BLOCK, 0, "```\n  x\n```");

    parse("   ```\n\tx\n```");
    check_node(AMARK_KIND_CODE_BLOCK, 0, "```\n x\n```");
}

NAOS_TEST(pad_on_the_first_line)
{
    /* The second tab keeps 2 columns as pad. */
    parse(">\t\tfoo");
    check_node(AMARK_KIND_CODE_BLOCK, 0, "  foo");
}

NAOS_TEST(no_pad_when_the_range_starts_inside_the_line)
{
    /* "   ```\n" 0-6, "\tx\n" 7-9, "```" 10-12; the tab line has pad 1. */
    parse("   ```\n\tx\n```");
    build(8, 10);
    check_out("x\n");

    /* Starting at the tab itself, the range starts at the record. */
    build(7, 9);
    check_out(" x");
}

NAOS_TEST(other_tabs_stay_tabs)
{
    parse("```\n\tx\ta\n```\n");
    check_node(AMARK_KIND_CODE_BLOCK, 0, "```\n\tx\ta\n```");

    parse("a\tb\n");
    check_node(AMARK_KIND_PARAGRAPH, 0, "a\tb");
}

/* --- Line endings -------------------------------------------------------- */

NAOS_TEST(three_kinds_of_line_endings_become_lf)
{
    parse("a\nb\rc\r\nd");
    check_node(AMARK_KIND_PARAGRAPH, 0, "a\nb\nc\nd");

    parse("```\r\nx\ry\n```\r\n");
    check_node(AMARK_KIND_CODE_BLOCK, 0, "```\nx\ny\n```");
}

NAOS_TEST(breaks_end_with_one_lf)
{
    parse("a\\\r\nb\rc  \r\nd");
    check_node(AMARK_KIND_HARD_BREAK, 0, "\\\n");
    check_node(AMARK_KIND_SOFT_BREAK, 0, "\n");
    check_node(AMARK_KIND_HARD_BREAK, 1, "  \n");
}

NAOS_TEST(range_ending_right_after_the_last_line_ending)
{
    parse("a\r\n");
    build(0, 3);
    check_out("a\n");
}

/* --- Empty range --------------------------------------------------------- */

NAOS_TEST(empty_range_writes_nothing)
{
    parse("abc\n");
    build(1, 1);
    check_out("");
}

NAOS_SUITE(text_build_plain,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(thematic_break),
    NAOS_ADD_TEST(atx_heading_starts_at_the_number_signs),
    NAOS_ADD_TEST(setext_heading_keeps_its_underline),
    NAOS_ADD_TEST(fenced_code_block_keeps_its_fences),
    NAOS_ADD_TEST(indented_code_block_leaves_out_the_indentation),
    NAOS_ADD_TEST(html_block),
    NAOS_ADD_TEST(paragraph),
    NAOS_ADD_TEST(link_reference_definition),
    NAOS_ADD_TEST(inline_kinds),
    NAOS_ADD_TEST(inline_node_over_several_lines_in_a_block_quote),
    NAOS_ADD_TEST(every_node_on_one_line_is_the_source),
    NAOS_ADD_TEST(trailing_spaces_stay),
    NAOS_ADD_TEST(escapes_references_and_delimiters_stay),
    NAOS_ADD_TEST(pad_spaces_replace_the_tab_at_the_record),
    NAOS_ADD_TEST(pad_on_the_first_line),
    NAOS_ADD_TEST(no_pad_when_the_range_starts_inside_the_line),
    NAOS_ADD_TEST(other_tabs_stay_tabs),
    NAOS_ADD_TEST(three_kinds_of_line_endings_become_lf),
    NAOS_ADD_TEST(breaks_end_with_one_lf),
    NAOS_ADD_TEST(range_ending_right_after_the_last_line_ending),
    NAOS_ADD_TEST(empty_range_writes_nothing)
)

/* --- DECODED ------------------------------------------------------------- */

/* Writes [start, end) with DECODED, from the line that holds start. */
static void decode(size_t start, size_t end)
{
    build_rule(start, end, line_at(start), AMARK_TEXT_RULE_DECODED);
}

/* Writes range (a range from the tree API) with DECODED into g_out. */
static void decode_range(const amark_range_t *range)
{
    decode(range->start, range->end);
}

/* Writes the range of the n-th node of kind with DECODED into g_out. */
static void decode_node(amark_kind_t kind, size_t n)
{
    amark_node_id_t node = find(kind, n);
    amark_range_t range;

    NAOS_REQUIRE_UINT_NE(node, AMARK_NODE_NONE);
    NAOS_REQUIRE_TRUE(amark_node_range(g_doc, node, &range));
    build_rule(range.start, range.end, amark_node_start_line(g_doc, node),
               AMARK_TEXT_RULE_DECODED);
}

/* Checks the DECODED text of the whole of content (a C string). */
static void check_decoded(const char *content, const char *want)
{
    parse(content);
    decode(0, strlen(content));
    check_out(want);
}

NAOS_TEST(every_ascii_punctuation_escape_gives_the_character)
{
    static const char punctuation[] = "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
    char content[2 * sizeof(punctuation)];
    size_t i;

    NAOS_REQUIRE_UINT_EQ(strlen(punctuation), 32);
    for (i = 0; punctuation[i] != '\0'; i++) {
        content[2 * i] = '\\';
        content[2 * i + 1] = punctuation[i];
    }
    content[2 * i] = '\0';
    check_decoded(content, punctuation);
}

NAOS_TEST(each_ascii_punctuation_escape_alone)
{
    char content[4];
    char want[2];
    int c;

    for (c = 0x21; c <= 0x7E; c++) {
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z')) {
            continue;
        }
        content[0] = 'x';
        content[1] = '\\';
        content[2] = (char)c;
        content[3] = '\0';
        parse(content);
        decode(1, 3);
        want[0] = (char)c;
        want[1] = '\0';
        check_out(want);
    }
}

NAOS_TEST(backslash_before_other_characters_stays)
{
    /* Letters, digits, space, tab, non-ASCII: the backslash stays. */
    check_decoded("\\a \\Z \\1 \\ x \\\tx \\\xC3\xA9 \\\x7F",
                  "\\a \\Z \\1 \\ x \\\tx \\\xC3\xA9 \\\x7F");
}

NAOS_TEST(backslash_at_the_end_stays)
{
    /* At the end of the content, of a line and of the range. */
    check_decoded("x\\", "x\\");
    check_decoded("a\\\nb", "a\\\nb");
    check_decoded("a\\\r\nb", "a\\\nb");

    parse("a\\*b");
    decode(0, 2);
    check_out("a\\");
}

NAOS_TEST(escaped_backslash_then_a_new_escape)
{
    check_decoded("\\\\*", "\\*");
    check_decoded("\\\\\\*", "\\*");
    check_decoded("\\\\\\\\", "\\\\");
}

NAOS_TEST(references_give_their_values)
{
    check_decoded("&amp; &copy; &#35; &#x41; &#X42; &ngE;",
                  "& \xC2\xA9 # A B \xE2\x89\xA7\xCC\xB8");
    /* 0, a surrogate and a value above U+10FFFF give U+FFFD. */
    check_decoded("&#0;&#xD800;&#x110000;",
                  "\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD");
}

NAOS_TEST(other_ampersands_stay)
{
    check_decoded("& &amp &bogus; &aMp; &#; &#12345678; &#x1234567; &x;",
                  "& &amp &bogus; &aMp; &#; &#12345678; &#x1234567; &x;");
}

NAOS_TEST(output_is_not_read_again)
{
    check_decoded("&amp;lt;", "&lt;");
    check_decoded("\\&amp;", "&amp;");
    check_decoded("&#92;*", "\\*");
    check_decoded("&#38;amp;", "&amp;");
}

NAOS_TEST(references_do_not_run_past_a_line_or_the_range)
{
    check_decoded("&amp\n;", "&amp\n;");

    parse("x &amp; y");
    decode(2, 6);
    check_out("&amp");
    decode(2, 7);
    check_out("&");
}

NAOS_TEST(decoded_pads_and_line_endings_like_plain)
{
    /* The tab line has 3 columns left after the fence's indentation. */
    parse(" ```\n\t\\*\r\n```");
    decode_node(AMARK_KIND_CODE_BLOCK, 0);
    check_out("```\n   *\n```");
}

NAOS_TEST(source_bytes_still_become_u_fffd)
{
    static const char content[] = "a\0\\*\xFF&amp;";

    amark_document_destroy(g_doc);
    g_doc = NULL;
    g_config.utf8_mode = AMARK_UTF8_REPLACE;
    NAOS_REQUIRE_INT_EQ(amark_parse(content, sizeof(content) - 1, &g_config,
                                    &g_doc),
                        AMARK_ERROR_NONE);
    decode(0, sizeof(content) - 1);
    check_out("a\xEF\xBF\xBD*\xEF\xBF\xBD&");
}

NAOS_TEST(multi_line_definition_title_in_a_block_quote)
{
    amark_range_t range;

    parse("> [a]: /u 'it\\'s\n>    &amp; more\n>  x'\n");
    NAOS_REQUIRE_TRUE(amark_definition_title(
        g_doc, find(AMARK_KIND_LINK_REFERENCE_DEFINITION, 0), &range));
    decode_range(&range);
    check_out("it's\n& more\nx");
}

NAOS_TEST(multi_line_link_title_in_a_list_item)
{
    amark_range_t range;

    parse("- [a](/u \"one\r\n     two\\\"\n\t three\")\n");
    NAOS_REQUIRE_TRUE(amark_link_title(g_doc, find(AMARK_KIND_LINK, 0),
                                       &range));
    decode_range(&range);
    check_out("one\ntwo\"\nthree");
}

NAOS_TEST(destination_keeps_spaces_and_parentheses)
{
    amark_range_t range;

    parse("[a](</u v(w) \\> &amp;>)\n");
    NAOS_REQUIRE_TRUE(amark_link_destination(g_doc, find(AMARK_KIND_LINK, 0),
                                             &range));
    decode_range(&range);
    check_out("/u v(w) > &");

    parse("[a](/u(v(w))x\\)%20&#x20;)\n");
    NAOS_REQUIRE_TRUE(amark_link_destination(g_doc, find(AMARK_KIND_LINK, 0),
                                             &range));
    decode_range(&range);
    check_out("/u(v(w))x)%20 ");

    parse("[a]: <b c(> 'd'\n");
    NAOS_REQUIRE_TRUE(amark_definition_destination(
        g_doc, find(AMARK_KIND_LINK_REFERENCE_DEFINITION, 0), &range));
    decode_range(&range);
    check_out("b c(");
}

NAOS_TEST(info_is_one_string_with_its_inner_spaces)
{
    amark_range_t range;

    parse("``` c\\+\\+  x&amp;y \n```\n");
    NAOS_REQUIRE_TRUE(amark_code_block_info(
        g_doc, find(AMARK_KIND_CODE_BLOCK, 0), &range));
    decode_range(&range);
    check_out("c++  x&y");
}

NAOS_SUITE(text_build_decoded,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(every_ascii_punctuation_escape_gives_the_character),
    NAOS_ADD_TEST(each_ascii_punctuation_escape_alone),
    NAOS_ADD_TEST(backslash_before_other_characters_stays),
    NAOS_ADD_TEST(backslash_at_the_end_stays),
    NAOS_ADD_TEST(escaped_backslash_then_a_new_escape),
    NAOS_ADD_TEST(references_give_their_values),
    NAOS_ADD_TEST(other_ampersands_stay),
    NAOS_ADD_TEST(output_is_not_read_again),
    NAOS_ADD_TEST(references_do_not_run_past_a_line_or_the_range),
    NAOS_ADD_TEST(decoded_pads_and_line_endings_like_plain),
    NAOS_ADD_TEST(source_bytes_still_become_u_fffd),
    NAOS_ADD_TEST(multi_line_definition_title_in_a_block_quote),
    NAOS_ADD_TEST(multi_line_link_title_in_a_list_item),
    NAOS_ADD_TEST(destination_keeps_spaces_and_parentheses),
    NAOS_ADD_TEST(info_is_one_string_with_its_inner_spaces)
)

/* --- CODE_SPAN ----------------------------------------------------------- */

/*
 * Writes the content of the n-th code span with CODE_SPAN into g_out: its
 * range without the backtick strings that open and close it, which have the
 * same length.
 */
static void code_span_node(size_t n)
{
    amark_node_id_t node = find(AMARK_KIND_CODE_SPAN, n);
    const unsigned char *content;
    amark_range_t range;
    size_t ticks = 0;

    NAOS_REQUIRE_UINT_NE(node, AMARK_NODE_NONE);
    NAOS_REQUIRE_TRUE(amark_node_range(g_doc, node, &range));
    content = amark_document_content(g_doc);
    while (range.start + ticks < range.end &&
           content[range.start + ticks] == '`') {
        ticks++;
    }
    NAOS_REQUIRE_UINT_LE(2 * ticks, range.end - range.start);
    build_rule(range.start + ticks, range.end - ticks,
               amark_node_start_line(g_doc, node), AMARK_TEXT_RULE_CODE_SPAN);
}

/* Checks the CODE_SPAN text of the first code span of content. */
static void check_code_span(const char *content, const char *want)
{
    parse(content);
    code_span_node(0);
    check_out(want);
}

NAOS_TEST(code_span_strips_one_space_on_both_sides)
{
    check_code_span("` a `", "a");
    check_code_span("` `` `", "``");
    check_code_span("x ` \xC3\xA9 ` y", "\xC3\xA9");
}

NAOS_TEST(code_span_keeps_a_space_on_one_side_only)
{
    check_code_span("` a`", " a");
    check_code_span("`a `", "a ");
    check_code_span("`a`", "a");
}

NAOS_TEST(code_span_of_spaces_only_stays)
{
    check_code_span("` `", " ");
    check_code_span("`  `", "  ");
    check_code_span("``   ``", "   ");
}

NAOS_TEST(code_span_strips_only_one_of_several_spaces)
{
    check_code_span("`  a  `", " a ");
    check_code_span("`  ``  `", " `` ");
    check_code_span("` a  b `", "a  b");
}

NAOS_TEST(code_span_keeps_tabs)
{
    check_code_span("`\ta\t`", "\ta\t");
    check_code_span("` \ta\t `", "\ta\t");
    check_code_span("`\t \t`", "\t \t");
}

NAOS_TEST(code_span_line_endings_become_spaces)
{
    check_code_span("`a\nb`", "a b");
    check_code_span("`a\r\nb`", "a b");
    check_code_span("`a\rb`", "a b");
    /* Trailing spaces of a line stay; a line ending adds one space. */
    check_code_span("``\nfoo\nbar  \nbaz\n``", "foo bar   baz");
}

NAOS_TEST(code_span_starting_or_ending_with_a_line_ending)
{
    /* The spaces from line endings are stripped like written spaces. */
    check_code_span("``\nfoo\n``", "foo");
    check_code_span("`\nfoo`", " foo");
    check_code_span("`foo\n`", "foo ");
    check_code_span("` \nfoo\n `", " foo");
    check_code_span("`  \nfoo\n`", "  foo");
    check_code_span("`a\n`", "a ");
    check_code_span("`\n`", " ");
}

NAOS_TEST(code_span_in_containers)
{
    /* Prefixes and the leading whitespace of continuation lines go. */
    check_code_span("> `a\n>   b`", "a b");
    check_code_span("> ``\n> foo\n> ``", "foo");
    check_code_span("> `a\nb`", "a b");
    check_code_span("- ``\n  foo\r\n    bar\n  ``", "foo bar");
    check_code_span("1. > - `a\n   >   b `\n", "a b ");
}

NAOS_TEST(code_span_keeps_backslashes_and_ampersands)
{
    check_code_span("`\\*&amp;`", "\\*&amp;");
    check_code_span("`a\\`b", "a\\");
    check_code_span("` &#35; \\\\ `", "&#35; \\\\");
}

NAOS_TEST(code_span_second_span_on_a_later_line)
{
    parse("`a`\nx `` b\nc ``");
    code_span_node(1);
    check_out("b c");
}

NAOS_SUITE(text_build_code_span,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(code_span_strips_one_space_on_both_sides),
    NAOS_ADD_TEST(code_span_keeps_a_space_on_one_side_only),
    NAOS_ADD_TEST(code_span_of_spaces_only_stays),
    NAOS_ADD_TEST(code_span_strips_only_one_of_several_spaces),
    NAOS_ADD_TEST(code_span_keeps_tabs),
    NAOS_ADD_TEST(code_span_line_endings_become_spaces),
    NAOS_ADD_TEST(code_span_starting_or_ending_with_a_line_ending),
    NAOS_ADD_TEST(code_span_in_containers),
    NAOS_ADD_TEST(code_span_keeps_backslashes_and_ampersands),
    NAOS_ADD_TEST(code_span_second_span_on_a_later_line)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(text_build_plain),
    NAOS_ADD_SUITE(text_build_decoded),
    NAOS_ADD_SUITE(text_build_code_span)
)
