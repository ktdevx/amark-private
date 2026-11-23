/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of the block scanners (src/scan.h): thematic breaks, ATX
 * headings, setext heading underlines, opening and closing code fences, list
 * item markers, and the start and end conditions of HTML blocks. Each case
 * is one line given as bytes, starting at the first byte that is not white
 * space (the indentation is the caller's business), taken from the matching
 * and non-matching examples of the CommonMark 0.31.2 sections 4.1, 4.2,
 * 4.3, 4.5, 4.6 and 5.2. Only the space and the tab are white space; U+0000
 * and Unicode white space (U+00A0 as C2 A0) are not.
 */

#include "scan.h"

#include <amark/tree.h>
#include <naos/naos.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The bytes of a string literal, without the terminating NUL. */
#define BYTES(s) ((const unsigned char *)(s)), (sizeof(s) - 1)

/* ---- thematic breaks (4.1) ---------------------------------------------- */

NAOS_TEST(thematic_break_accepts_three_characters_of_one_kind)
{
    NAOS_CHECK_TRUE(amark_scan_thematic_break(BYTES("***")));
    NAOS_CHECK_TRUE(amark_scan_thematic_break(BYTES("---")));
    NAOS_CHECK_TRUE(amark_scan_thematic_break(BYTES("___")));
    NAOS_CHECK_TRUE(amark_scan_thematic_break(
        BYTES("_____________________________________")));
}

NAOS_TEST(thematic_break_rejects_other_characters_and_fewer_than_three)
{
    NAOS_CHECK_FALSE(amark_scan_thematic_break(BYTES("+++")));
    NAOS_CHECK_FALSE(amark_scan_thematic_break(BYTES("===")));
    NAOS_CHECK_FALSE(amark_scan_thematic_break(BYTES("--")));
    NAOS_CHECK_FALSE(amark_scan_thematic_break(BYTES("**")));
    NAOS_CHECK_FALSE(amark_scan_thematic_break(BYTES("__")));
    NAOS_CHECK_FALSE(amark_scan_thematic_break(BYTES("-")));
    NAOS_CHECK_FALSE(amark_scan_thematic_break(NULL, 0));
}

NAOS_TEST(thematic_break_allows_spaces_and_tabs_between_and_after)
{
    NAOS_CHECK_TRUE(amark_scan_thematic_break(BYTES("- - -")));
    NAOS_CHECK_TRUE(amark_scan_thematic_break(BYTES("**  * ** * ** * **")));
    NAOS_CHECK_TRUE(amark_scan_thematic_break(BYTES("-     -      -      -")));
    NAOS_CHECK_TRUE(amark_scan_thematic_break(BYTES("- - - -    ")));
    NAOS_CHECK_TRUE(amark_scan_thematic_break(BYTES("*\t*\t*\t")));
    NAOS_CHECK_TRUE(amark_scan_thematic_break(BYTES("* * *")));
}

NAOS_TEST(thematic_break_rejects_other_bytes_and_mixed_characters)
{
    NAOS_CHECK_FALSE(amark_scan_thematic_break(BYTES("_ _ _ _ a")));
    NAOS_CHECK_FALSE(amark_scan_thematic_break(BYTES("a------")));
    NAOS_CHECK_FALSE(amark_scan_thematic_break(BYTES("---a---")));
    NAOS_CHECK_FALSE(amark_scan_thematic_break(BYTES("*-*")));
    NAOS_CHECK_FALSE(amark_scan_thematic_break(BYTES("- - * * *")));
    NAOS_CHECK_FALSE(amark_scan_thematic_break(BYTES("- * * *")));
}

NAOS_TEST(thematic_break_treats_nul_and_unicode_space_as_content)
{
    NAOS_CHECK_FALSE(amark_scan_thematic_break(BYTES("---\0")));
    NAOS_CHECK_FALSE(amark_scan_thematic_break(BYTES("-\0-\0-")));
    NAOS_CHECK_FALSE(amark_scan_thematic_break(BYTES("***\xC2\xA0")));
    NAOS_CHECK_FALSE(amark_scan_thematic_break(BYTES("*\xC2\xA0*\xC2\xA0*")));
    NAOS_CHECK_FALSE(amark_scan_thematic_break(BYTES("---\v")));
    NAOS_CHECK_FALSE(amark_scan_thematic_break(BYTES("---\f")));
}

/* ---- ATX headings (4.2) ------------------------------------------------- */

/*
 * Checks that line is an ATX heading of level whose content is the bytes of
 * content, and that the content bounds are inside the line.
 */
static void check_atx(const char *line, size_t length, unsigned level,
                      const char *content, size_t content_length)
{
    unsigned got_level = 0;
    size_t start = 9999;
    size_t end = 9999;

    NAOS_REQUIRE_TRUE(amark_scan_atx_heading((const unsigned char *)line,
                                             length, &got_level, &start,
                                             &end));
    NAOS_CHECK_UINT_EQ(got_level, level);
    NAOS_REQUIRE_TRUE(start <= end);
    NAOS_REQUIRE_TRUE(end <= length);
    NAOS_CHECK_UINT_EQ(end - start, content_length);
    if (content_length > 0) {
        NAOS_CHECK_MEM_EQ(line + start, end - start, content, content_length);
    }
}

#define CHECK_ATX(line, level, content)                                      \
    check_atx((line), sizeof(line) - 1, (level), (content),                 \
              sizeof(content) - 1)

/* Checks that line is not an ATX heading and the outputs are unchanged. */
static void check_not_atx(const char *line, size_t length)
{
    unsigned level = 77;
    size_t start = 88;
    size_t end = 99;

    NAOS_CHECK_FALSE(amark_scan_atx_heading((const unsigned char *)line,
                                            length, &level, &start, &end));
    NAOS_CHECK_UINT_EQ(level, 77);
    NAOS_CHECK_UINT_EQ(start, 88);
    NAOS_CHECK_UINT_EQ(end, 99);
}

#define CHECK_NOT_ATX(line) check_not_atx((line), sizeof(line) - 1)

NAOS_TEST(atx_heading_levels_one_to_six)
{
    CHECK_ATX("# foo", 1, "foo");
    CHECK_ATX("## foo", 2, "foo");
    CHECK_ATX("### foo", 3, "foo");
    CHECK_ATX("#### foo", 4, "foo");
    CHECK_ATX("##### foo", 5, "foo");
    CHECK_ATX("###### foo", 6, "foo");
}

NAOS_TEST(atx_heading_rejects_seven_and_missing_space)
{
    CHECK_NOT_ATX("####### foo");
    CHECK_NOT_ATX("#######");
    CHECK_NOT_ATX("#5 bolt");
    CHECK_NOT_ATX("#hashtag");
    CHECK_NOT_ATX("\\## foo");
    CHECK_NOT_ATX("foo");
    check_not_atx(NULL, 0);
}

NAOS_TEST(atx_heading_strips_white_space_around_the_content)
{
    CHECK_ATX("# foo *bar* \\*baz\\*", 1, "foo *bar* \\*baz\\*");
    CHECK_ATX("#                  foo                     ", 1, "foo");
    CHECK_ATX("#\tfoo\t", 1, "foo");
    CHECK_ATX("# foo bar", 1, "foo bar");
}

NAOS_TEST(atx_heading_strips_the_closing_sequence)
{
    CHECK_ATX("## foo ##", 2, "foo");
    CHECK_ATX("###   bar    ###", 3, "bar");
    CHECK_ATX("# foo ##################################", 1, "foo");
    CHECK_ATX("##### foo ##", 5, "foo");
    CHECK_ATX("### foo ###     ", 3, "foo");
    CHECK_ATX("# foo\t#\t", 1, "foo");
}

NAOS_TEST(atx_heading_keeps_hashes_that_are_not_a_closing_sequence)
{
    CHECK_ATX("### foo ### b", 3, "foo ### b");
    CHECK_ATX("# foo#", 1, "foo#");
    CHECK_ATX("### foo \\###", 3, "foo \\###");
    CHECK_ATX("## foo #\\##", 2, "foo #\\##");
    CHECK_ATX("# foo \\#", 1, "foo \\#");
}

NAOS_TEST(atx_heading_with_no_content)
{
    CHECK_ATX("## ", 2, "");
    CHECK_ATX("#", 1, "");
    CHECK_ATX("### ###", 3, "");
    CHECK_ATX("######", 6, "");
    CHECK_ATX("# #", 1, "");
    CHECK_ATX("#\t\t", 1, "");
}

NAOS_TEST(atx_heading_empty_content_bounds_are_inside_the_line)
{
    unsigned level = 0;
    size_t start = 0;
    size_t end = 0;

    NAOS_REQUIRE_TRUE(amark_scan_atx_heading(BYTES("#"), &level, &start,
                                             &end));
    NAOS_CHECK_UINT_EQ(start, end);
    NAOS_CHECK_TRUE(end <= 1);
    NAOS_REQUIRE_TRUE(amark_scan_atx_heading(BYTES("## "), &level, &start,
                                             &end));
    NAOS_CHECK_UINT_EQ(start, end);
    NAOS_CHECK_TRUE(end <= 3);
    NAOS_REQUIRE_TRUE(amark_scan_atx_heading(BYTES("# foo"), &level, &start,
                                             &end));
    NAOS_CHECK_UINT_EQ(start, 2);
    NAOS_CHECK_UINT_EQ(end, 5);
}

NAOS_TEST(atx_heading_treats_nul_and_unicode_space_as_content)
{
    CHECK_NOT_ATX("#\0foo");
    CHECK_NOT_ATX("#\xC2\xA0" "foo");
    CHECK_ATX("# foo\0", 1, "foo\0");
    CHECK_ATX("# foo\xC2\xA0", 1, "foo\xC2\xA0");
    CHECK_ATX("# foo\xC2\xA0#", 1, "foo\xC2\xA0#");
    CHECK_ATX("# \0", 1, "\0");
    CHECK_ATX("# \xC2\xA0", 1, "\xC2\xA0");
}

/* ---- setext heading underlines (4.3) ------------------------------------ */

#define SETEXT(line) amark_scan_setext_underline(BYTES(line))

NAOS_TEST(setext_underline_of_equals_and_dashes)
{
    NAOS_CHECK_UINT_EQ(SETEXT("========="), 1);
    NAOS_CHECK_UINT_EQ(SETEXT("---------"), 2);
    NAOS_CHECK_UINT_EQ(SETEXT("="), 1);
    NAOS_CHECK_UINT_EQ(SETEXT("-"), 2);
    NAOS_CHECK_UINT_EQ(SETEXT("-------------------------"), 2);
    NAOS_CHECK_UINT_EQ(SETEXT("----      "), 2);
    NAOS_CHECK_UINT_EQ(SETEXT("===\t"), 1);
}

NAOS_TEST(setext_underline_rejects_gaps_and_other_bytes)
{
    NAOS_CHECK_UINT_EQ(SETEXT("= ="), 0);
    NAOS_CHECK_UINT_EQ(SETEXT("--- -"), 0);
    NAOS_CHECK_UINT_EQ(SETEXT("=-"), 0);
    NAOS_CHECK_UINT_EQ(SETEXT("-="), 0);
    NAOS_CHECK_UINT_EQ(SETEXT("\\---"), 0);
    NAOS_CHECK_UINT_EQ(SETEXT("***"), 0);
    NAOS_CHECK_UINT_EQ(SETEXT("___"), 0);
    NAOS_CHECK_UINT_EQ(SETEXT("---a"), 0);
    NAOS_CHECK_UINT_EQ(amark_scan_setext_underline(NULL, 0), 0);
}

NAOS_TEST(setext_underline_treats_nul_and_unicode_space_as_content)
{
    NAOS_CHECK_UINT_EQ(SETEXT("===\0"), 0);
    NAOS_CHECK_UINT_EQ(SETEXT("---\xC2\xA0"), 0);
    NAOS_CHECK_UINT_EQ(SETEXT("=\0="), 0);
}

/* ---- opening code fences (4.5) ------------------------------------------ */

/*
 * Checks that line is an opening fence of fence_char and fence_length whose
 * info string is the bytes of info.
 */
static void check_open(const char *line, size_t length, char fence_char,
                       size_t fence_length, const char *info,
                       size_t info_length)
{
    char got_char = 'x';
    size_t got_length = 9999;
    size_t start = 9999;
    size_t end = 9999;

    NAOS_REQUIRE_TRUE(amark_scan_open_fence((const unsigned char *)line,
                                            length, &got_char, &got_length,
                                            &start, &end));
    NAOS_CHECK_INT_EQ(got_char, fence_char);
    NAOS_CHECK_UINT_EQ(got_length, fence_length);
    NAOS_REQUIRE_TRUE(start <= end);
    NAOS_REQUIRE_TRUE(end <= length);
    NAOS_CHECK_UINT_EQ(end - start, info_length);
    if (info_length > 0) {
        NAOS_CHECK_MEM_EQ(line + start, end - start, info, info_length);
    }
}

#define CHECK_OPEN(line, fence_char, fence_length, info)                     \
    check_open((line), sizeof(line) - 1, (fence_char), (fence_length),       \
               (info), sizeof(info) - 1)

/* Checks that line is not an opening fence and the outputs are unchanged. */
static void check_not_open(const char *line, size_t length)
{
    char fence_char = 'x';
    size_t fence_length = 77;
    size_t start = 88;
    size_t end = 99;

    NAOS_CHECK_FALSE(amark_scan_open_fence((const unsigned char *)line,
                                           length, &fence_char,
                                           &fence_length, &start, &end));
    NAOS_CHECK_INT_EQ(fence_char, 'x');
    NAOS_CHECK_UINT_EQ(fence_length, 77);
    NAOS_CHECK_UINT_EQ(start, 88);
    NAOS_CHECK_UINT_EQ(end, 99);
}

#define CHECK_NOT_OPEN(line) check_not_open((line), sizeof(line) - 1)

NAOS_TEST(open_fence_of_backticks_and_tildes)
{
    CHECK_OPEN("```", '`', 3, "");
    CHECK_OPEN("~~~", '~', 3, "");
    CHECK_OPEN("````", '`', 4, "");
    CHECK_OPEN("~~~~", '~', 4, "");
    CHECK_OPEN("`````", '`', 5, "");
    CHECK_OPEN("~~~~~~", '~', 6, "");
}

NAOS_TEST(open_fence_rejects_short_runs_and_other_characters)
{
    CHECK_NOT_OPEN("``");
    CHECK_NOT_OPEN("~~");
    CHECK_NOT_OPEN("`");
    CHECK_NOT_OPEN("---");
    CHECK_NOT_OPEN("foo");
    CHECK_NOT_OPEN("`~~");
    CHECK_NOT_OPEN("~`~");
    check_not_open(NULL, 0);
}

NAOS_TEST(open_fence_info_string_without_surrounding_white_space)
{
    CHECK_OPEN("```ruby", '`', 3, "ruby");
    CHECK_OPEN("~~~~    ruby startline=3 $%@#$", '~', 4,
               "ruby startline=3 $%@#$");
    CHECK_OPEN("````;", '`', 4, ";");
    CHECK_OPEN("``` ruby \t", '`', 3, "ruby");
    CHECK_OPEN("```\truby\t", '`', 3, "ruby");
    CHECK_OPEN("```   ", '`', 3, "");
    CHECK_OPEN("~~~\t", '~', 3, "");
    CHECK_OPEN("```~~~", '`', 3, "~~~");
}

NAOS_TEST(open_fence_backtick_info_must_not_contain_backticks)
{
    CHECK_NOT_OPEN("``` ```");
    CHECK_NOT_OPEN("``` aa ```");
    CHECK_NOT_OPEN("```a`b");
    CHECK_NOT_OPEN("```a `");
    CHECK_OPEN("~~~ aa ``` ~~~", '~', 3, "aa ``` ~~~");
    CHECK_OPEN("~~~~ ~~", '~', 4, "~~");
}

NAOS_TEST(open_fence_treats_nul_and_unicode_space_as_content)
{
    CHECK_OPEN("```\0", '`', 3, "\0");
    CHECK_OPEN("```\xC2\xA0", '`', 3, "\xC2\xA0");
    CHECK_OPEN("~~~ \xC2\xA0rb\xC2\xA0 ", '~', 3, "\xC2\xA0rb\xC2\xA0");
    CHECK_OPEN("``` a\0b ", '`', 3, "a\0b");
    CHECK_NOT_OPEN("``\0`");
    CHECK_NOT_OPEN("~~\xC2\xA0~");
}

/* ---- closing code fences (4.5) ------------------------------------------ */

#define CLOSE(line, fence_char) amark_scan_close_fence(BYTES(line), (fence_char))

NAOS_TEST(close_fence_returns_the_length_of_the_run)
{
    NAOS_CHECK_UINT_EQ(CLOSE("```", '`'), 3);
    NAOS_CHECK_UINT_EQ(CLOSE("~~~", '~'), 3);
    NAOS_CHECK_UINT_EQ(CLOSE("``````", '`'), 6);
    NAOS_CHECK_UINT_EQ(CLOSE("~~~~", '~'), 4);
    NAOS_CHECK_UINT_EQ(CLOSE("```   ", '`'), 3);
    NAOS_CHECK_UINT_EQ(CLOSE("~~~~~~~\t ", '~'), 7);
}

NAOS_TEST(close_fence_rejects_other_characters_and_info_strings)
{
    NAOS_CHECK_UINT_EQ(CLOSE("~~~", '`'), 0);
    NAOS_CHECK_UINT_EQ(CLOSE("```", '~'), 0);
    NAOS_CHECK_UINT_EQ(CLOSE("``` aaa", '`'), 0);
    NAOS_CHECK_UINT_EQ(CLOSE("~~~ ~~", '~'), 0);
    NAOS_CHECK_UINT_EQ(CLOSE("```~", '`'), 0);
    NAOS_CHECK_UINT_EQ(CLOSE("``", '`'), 0);
    NAOS_CHECK_UINT_EQ(CLOSE("~~", '~'), 0);
    NAOS_CHECK_UINT_EQ(CLOSE("aaa", '`'), 0);
    NAOS_CHECK_UINT_EQ(amark_scan_close_fence(NULL, 0, '`'), 0);
}

NAOS_TEST(close_fence_treats_nul_and_unicode_space_as_content)
{
    NAOS_CHECK_UINT_EQ(CLOSE("```\0", '`'), 0);
    NAOS_CHECK_UINT_EQ(CLOSE("~~~\xC2\xA0", '~'), 0);
    NAOS_CHECK_UINT_EQ(CLOSE("```\v", '`'), 0);
}

/* ---- list item markers (5.2) -------------------------------------------- */

/*
 * Checks that line starts with a list marker of type, marker character,
 * start number (ordered only) and width, with blank_after as given.
 */
static void check_marker(const char *line, size_t length,
                         amark_list_type_t type, char marker,
                         uint32_t start_number, size_t width,
                         bool blank_after)
{
    amark_list_marker_t got;

    memset(&got, 0x5A, sizeof(got));
    got.start_number = 4242;
    NAOS_REQUIRE_TRUE(amark_scan_list_marker((const unsigned char *)line,
                                             length, &got));
    NAOS_CHECK_INT_EQ((int)got.type, (int)type);
    NAOS_CHECK_INT_EQ(got.marker, marker);
    if (type == AMARK_LIST_ORDERED) {
        NAOS_CHECK_UINT_EQ(got.start_number, start_number);
    }
    NAOS_CHECK_UINT_EQ(got.width, width);
    NAOS_CHECK_TRUE(got.blank_after == blank_after);
}

#define CHECK_BULLET(line, marker, blank_after)                              \
    check_marker((line), sizeof(line) - 1, AMARK_LIST_BULLET, (marker), 0,  \
                 1, (blank_after))

#define CHECK_ORDERED(line, delimiter, start, width, blank_after)            \
    check_marker((line), sizeof(line) - 1, AMARK_LIST_ORDERED, (delimiter), \
                 (start), (width), (blank_after))

/* Checks that line has no list marker and the output is unchanged. */
static void check_not_marker(const char *line, size_t length)
{
    amark_list_marker_t got;
    amark_list_marker_t before;

    memset(&got, 0x5A, sizeof(got));
    before = got;
    NAOS_CHECK_FALSE(amark_scan_list_marker((const unsigned char *)line,
                                            length, &got));
    NAOS_CHECK_MEM_EQ(&got, sizeof(got), &before, sizeof(before));
}

#define CHECK_NOT_MARKER(line) check_not_marker((line), sizeof(line) - 1)

NAOS_TEST(list_marker_bullets_of_three_characters)
{
    CHECK_BULLET("- one", '-', false);
    CHECK_BULLET("+ one", '+', false);
    CHECK_BULLET("* one", '*', false);
    CHECK_BULLET("-\tone", '-', false);
    CHECK_BULLET("-    one", '-', false);
    CHECK_BULLET("- > foo", '-', false);
}

NAOS_TEST(list_marker_ordered_with_two_delimiters)
{
    CHECK_ORDERED("1. A paragraph", '.', 1, 2, false);
    CHECK_ORDERED("2) foo", ')', 2, 2, false);
    CHECK_ORDERED("123456789. ok", '.', 123456789, 10, false);
    CHECK_ORDERED("003. ok", '.', 3, 4, false);
    CHECK_ORDERED("10)\tfoo", ')', 10, 3, false);
}

NAOS_TEST(list_marker_start_numbers_zero_and_largest)
{
    CHECK_ORDERED("0. ok", '.', 0, 2, false);
    CHECK_ORDERED("999999999. ok", '.', 999999999, 10, false);
    CHECK_ORDERED("999999999)", ')', 999999999, 10, true);
    CHECK_ORDERED("000000000. ok", '.', 0, 10, false);
}

NAOS_TEST(list_marker_rejects_ten_digits)
{
    CHECK_NOT_MARKER("1234567890. not ok");
    CHECK_NOT_MARKER("0000000000. not ok");
    CHECK_NOT_MARKER("9999999999)");
}

NAOS_TEST(list_marker_needs_white_space_or_end_after_the_marker)
{
    CHECK_NOT_MARKER("-one");
    CHECK_NOT_MARKER("+one");
    CHECK_NOT_MARKER("*one");
    CHECK_NOT_MARKER("2.two");
    CHECK_NOT_MARKER("1)x");
    CHECK_NOT_MARKER("--");
    CHECK_NOT_MARKER("-1. x");
    CHECK_NOT_MARKER("#5 bolt");
}

NAOS_TEST(list_marker_rejects_other_shapes)
{
    CHECK_NOT_MARKER("");
    CHECK_NOT_MARKER("a. foo");
    CHECK_NOT_MARKER("1 foo");
    CHECK_NOT_MARKER("1");
    CHECK_NOT_MARKER("1: foo");
    CHECK_NOT_MARKER(". foo");
    CHECK_NOT_MARKER(") foo");
    CHECK_NOT_MARKER("= foo");
    CHECK_NOT_MARKER("-1 foo");
    NAOS_CHECK_FALSE(amark_scan_list_marker(NULL, 0, NULL));
}

NAOS_TEST(list_marker_alone_on_the_line_is_blank_after)
{
    CHECK_BULLET("-", '-', true);
    CHECK_BULLET("+", '+', true);
    CHECK_BULLET("*", '*', true);
    CHECK_BULLET("-   ", '-', true);
    CHECK_BULLET("-\t \t", '-', true);
    CHECK_ORDERED("1.", '.', 1, 2, true);
    CHECK_ORDERED("1)", ')', 1, 2, true);
    CHECK_ORDERED("2.   ", '.', 2, 2, true);
    CHECK_ORDERED("10.\t", '.', 10, 3, true);
}

NAOS_TEST(list_marker_content_after_white_space_is_not_blank)
{
    CHECK_BULLET("-   x", '-', false);
    CHECK_BULLET("- \t x ", '-', false);
    CHECK_ORDERED("1.  \tx", '.', 1, 2, false);
}

NAOS_TEST(list_marker_treats_nul_and_unicode_space_as_content)
{
    CHECK_NOT_MARKER("-\0");
    CHECK_NOT_MARKER("1.\0");
    CHECK_NOT_MARKER("*\xC2\xA0x");
    CHECK_NOT_MARKER("1)\xC2\xA0x");
    CHECK_NOT_MARKER("-\v");
    CHECK_NOT_MARKER("-\f");
    CHECK_BULLET("- \0", '-', false);
    CHECK_BULLET("- \xC2\xA0", '-', false);
    CHECK_ORDERED("1. \0", '.', 1, 2, false);
}

NAOS_TEST(list_marker_reads_only_the_given_bytes)
{
    /* The bytes after len must not be read or counted. */
    static const char line[] = "12. x";
    static const char digits[] = "1234567890. x";
    amark_list_marker_t got;

    NAOS_REQUIRE_TRUE(amark_scan_list_marker((const unsigned char *)line, 3,
                                             &got));
    NAOS_CHECK_UINT_EQ(got.start_number, 12);
    NAOS_CHECK_UINT_EQ(got.width, 3);
    NAOS_CHECK_TRUE(got.blank_after);
    NAOS_CHECK_FALSE(amark_scan_list_marker((const unsigned char *)line, 2,
                                            &got));
    NAOS_CHECK_FALSE(amark_scan_list_marker((const unsigned char *)line, 1,
                                            &got));
    NAOS_CHECK_FALSE(amark_scan_list_marker((const unsigned char *)digits, 9,
                                            &got));
    NAOS_REQUIRE_TRUE(amark_scan_list_marker((const unsigned char *)"- x", 1,
                                             &got));
    NAOS_CHECK_INT_EQ((int)got.type, (int)AMARK_LIST_BULLET);
    NAOS_CHECK_TRUE(got.blank_after);
}

/* ---- HTML blocks (4.6) -------------------------------------------------- */

#define HTML_START(line) amark_scan_html_start(BYTES(line))
#define HTML_END(line, type) amark_scan_html_end(BYTES(line), (type))

NAOS_TEST(html_start_type_1_raw_text_tags)
{
    NAOS_CHECK_UINT_EQ(HTML_START("<pre language=\"haskell\"><code>"), 1);
    NAOS_CHECK_UINT_EQ(HTML_START("<script type=\"text/javascript\">"), 1);
    NAOS_CHECK_UINT_EQ(HTML_START("<style"), 1);
    NAOS_CHECK_UINT_EQ(HTML_START("<textarea>"), 1);
    NAOS_CHECK_UINT_EQ(HTML_START("<pre\tx"), 1);
    NAOS_CHECK_UINT_EQ(HTML_START("<PRE>"), 1);
    NAOS_CHECK_UINT_EQ(HTML_START("<ScRiPt>"), 1);
    NAOS_CHECK_UINT_EQ(HTML_START("<style>p{color:red;}</style>"), 1);
}

NAOS_TEST(html_start_type_1_needs_a_delimiter_after_the_name)
{
    /* Not type 1; these are complete open tags of other names (type 7). */
    NAOS_CHECK_UINT_EQ(HTML_START("<prex>"), 7);
    NAOS_CHECK_UINT_EQ(HTML_START("<pre-x>"), 7);
    NAOS_CHECK_UINT_EQ(HTML_START("<scripts>"), 7);
    /* A complete tag of a raw text name is not type 7, and "/" is not a
       delimiter of type 1. */
    NAOS_CHECK_UINT_EQ(HTML_START("<pre/>"), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("<Script/>"), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("<pr"), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("<pre\xC2\xA0x"), 0);
}

NAOS_TEST(html_start_type_2_comment)
{
    NAOS_CHECK_UINT_EQ(HTML_START("<!--"), 2);
    NAOS_CHECK_UINT_EQ(HTML_START("<!-- Foo"), 2);
    NAOS_CHECK_UINT_EQ(HTML_START("<!-->"), 2);
    NAOS_CHECK_UINT_EQ(HTML_START("<!-- a -->"), 2);
    NAOS_CHECK_UINT_EQ(HTML_START("<!-"), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("<!-x"), 0);
}

NAOS_TEST(html_start_type_3_processing_instruction)
{
    NAOS_CHECK_UINT_EQ(HTML_START("<?php"), 3);
    NAOS_CHECK_UINT_EQ(HTML_START("<?"), 3);
    NAOS_CHECK_UINT_EQ(HTML_START("<?xml ?>"), 3);
    NAOS_CHECK_UINT_EQ(HTML_START("<"), 0);
    NAOS_CHECK_UINT_EQ(HTML_START(" <?php"), 0);
}

NAOS_TEST(html_start_type_4_declaration)
{
    NAOS_CHECK_UINT_EQ(HTML_START("<!DOCTYPE html>"), 4);
    NAOS_CHECK_UINT_EQ(HTML_START("<!doctype html>"), 4);
    NAOS_CHECK_UINT_EQ(HTML_START("<!X"), 4);
    NAOS_CHECK_UINT_EQ(HTML_START("<!z"), 4);
    NAOS_CHECK_UINT_EQ(HTML_START("<!"), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("<!1"), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("<! DOCTYPE"), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("<![x"), 0);
}

NAOS_TEST(html_start_type_5_cdata)
{
    NAOS_CHECK_UINT_EQ(HTML_START("<![CDATA["), 5);
    NAOS_CHECK_UINT_EQ(HTML_START("<![CDATA[x]]>"), 5);
    /* CDATA is case-sensitive. */
    NAOS_CHECK_UINT_EQ(HTML_START("<![cdata["), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("<![CDATA"), 0);
}

NAOS_TEST(html_start_type_6_block_tag_names)
{
    NAOS_CHECK_UINT_EQ(HTML_START("<table><tr><td>"), 6);
    NAOS_CHECK_UINT_EQ(HTML_START("<div>"), 6);
    NAOS_CHECK_UINT_EQ(HTML_START("</div>"), 6);
    NAOS_CHECK_UINT_EQ(HTML_START("<div"), 6);
    NAOS_CHECK_UINT_EQ(HTML_START("</div"), 6);
    NAOS_CHECK_UINT_EQ(HTML_START("<div id=\"foo\""), 6);
    NAOS_CHECK_UINT_EQ(HTML_START("<div\tclass"), 6);
    NAOS_CHECK_UINT_EQ(HTML_START("<div/>"), 6);
    NAOS_CHECK_UINT_EQ(HTML_START("<div *?\?\?-&&&-<---"), 6);
    NAOS_CHECK_UINT_EQ(HTML_START("</ins>"), 7);
    /* The first, the last and the longest names, and names with digits. */
    NAOS_CHECK_UINT_EQ(HTML_START("<address>"), 6);
    NAOS_CHECK_UINT_EQ(HTML_START("<ul>"), 6);
    NAOS_CHECK_UINT_EQ(HTML_START("<blockquote>"), 6);
    NAOS_CHECK_UINT_EQ(HTML_START("<figcaption>"), 6);
    NAOS_CHECK_UINT_EQ(HTML_START("<h1>"), 6);
    NAOS_CHECK_UINT_EQ(HTML_START("<h6>"), 6);
    NAOS_CHECK_UINT_EQ(HTML_START("<p>"), 6);
    NAOS_CHECK_UINT_EQ(HTML_START("<search>"), 6);
    NAOS_CHECK_UINT_EQ(HTML_START("<menuitem>"), 6);
    NAOS_CHECK_UINT_EQ(HTML_START("<track>"), 6);
}

NAOS_TEST(html_start_type_6_every_name_of_the_list)
{
    static const char names[] =
        "address article aside base basefont blockquote body caption center "
        "col colgroup dd details dialog dir div dl dt fieldset figcaption "
        "figure footer form frame frameset h1 h2 h3 h4 h5 h6 head header hr "
        "html iframe legend li link main menu menuitem nav noframes ol "
        "optgroup option p param search section summary table tbody td "
        "tfoot th thead title tr track ul ";
    unsigned char line[16];
    size_t start = 0;
    size_t i;
    unsigned count = 0;

    for (i = 0; names[i] != '\0'; i++) {
        size_t n;
        if (names[i] != ' ') {
            continue;
        }
        n = i - start;
        NAOS_REQUIRE_TRUE(n + 2 <= sizeof(line));
        line[0] = '<';
        memcpy(line + 1, names + start, n);
        line[n + 1] = '>';
        NAOS_CHECK_UINT_EQ(amark_scan_html_start(line, n + 2), 6);
        NAOS_CHECK_UINT_EQ(amark_scan_html_start(line, n + 1), 6);
        start = i + 1;
        count++;
    }
    NAOS_CHECK_UINT_EQ(count, 62);
}

NAOS_TEST(html_start_type_6_names_ignore_ascii_case)
{
    NAOS_CHECK_UINT_EQ(HTML_START("<DIV>"), 6);
    NAOS_CHECK_UINT_EQ(HTML_START("</DiV>"), 6);
    NAOS_CHECK_UINT_EQ(HTML_START("<BlockQuote>"), 6);
    NAOS_CHECK_UINT_EQ(HTML_START("<H2 class=x>"), 6);
    NAOS_CHECK_UINT_EQ(HTML_START("<ADDRESS"), 6);
    NAOS_CHECK_UINT_EQ(HTML_START("<UL/>"), 6);
}

NAOS_TEST(html_start_type_6_rejects_other_names_and_delimiters)
{
    NAOS_CHECK_UINT_EQ(HTML_START("<divx"), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("<div-x"), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("<div/"), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("<div/x>"), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("<h7>"), 7);
    NAOS_CHECK_UINT_EQ(HTML_START("<h>"), 7);
    NAOS_CHECK_UINT_EQ(HTML_START("<blockquotes>"), 7);
    NAOS_CHECK_UINT_EQ(HTML_START("<abc>"), 7);
    NAOS_CHECK_UINT_EQ(HTML_START("<zzz>"), 7);
    NAOS_CHECK_UINT_EQ(HTML_START("<a>"), 7);
    NAOS_CHECK_UINT_EQ(HTML_START("<div\xC2\xA0"), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("<div\0"), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("< div>"), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("<</div>"), 0);
}

NAOS_TEST(html_start_type_7_complete_open_tags)
{
    NAOS_CHECK_UINT_EQ(HTML_START("<a href=\"foo\">"), 7);
    NAOS_CHECK_UINT_EQ(HTML_START("<Warning>"), 7);
    NAOS_CHECK_UINT_EQ(HTML_START("<del>"), 7);
    NAOS_CHECK_UINT_EQ(HTML_START("<a>  \t"), 7);
    NAOS_CHECK_UINT_EQ(HTML_START("<a/>"), 7);
    NAOS_CHECK_UINT_EQ(HTML_START("<a />"), 7);
    NAOS_CHECK_UINT_EQ(HTML_START("<my-tag1>"), 7);
    NAOS_CHECK_UINT_EQ(HTML_START("<a b c>"), 7);
    NAOS_CHECK_UINT_EQ(HTML_START("<a _b :c.d-e_f=1>"), 7);
    NAOS_CHECK_UINT_EQ(HTML_START("<a b='x\"y' c=\"x'y\" d=z>"), 7);
    NAOS_CHECK_UINT_EQ(HTML_START("<a b = 'c' >"), 7);
    NAOS_CHECK_UINT_EQ(HTML_START("<a b=''>"), 7);
    NAOS_CHECK_UINT_EQ(HTML_START("<a\tb\t=\t\"c\"\t/>"), 7);
    NAOS_CHECK_UINT_EQ(HTML_START("<Prez>"), 7);
}

NAOS_TEST(html_start_type_7_complete_closing_tags)
{
    NAOS_CHECK_UINT_EQ(HTML_START("</a>"), 7);
    NAOS_CHECK_UINT_EQ(HTML_START("</a \t>"), 7);
    NAOS_CHECK_UINT_EQ(HTML_START("</ins>"), 7);
    /* A closing tag of a raw text name is type 7. */
    NAOS_CHECK_UINT_EQ(HTML_START("</pre>"), 7);
    NAOS_CHECK_UINT_EQ(HTML_START("</SCRIPT>"), 7);
}

NAOS_TEST(html_start_type_7_rejects_incomplete_or_followed_tags)
{
    /* Only white space may follow the tag. */
    NAOS_CHECK_UINT_EQ(HTML_START("<a> x"), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("<del>*foo*</del>"), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("<a><b>"), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("<a>\xC2\xA0"), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("<a>\0"), 0);
    /* Incomplete tags. */
    NAOS_CHECK_UINT_EQ(HTML_START("<a"), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("<a href=\"foo"), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("<a href='foo>"), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("<a b="), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("<a b=>"), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("</a"), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("</"), 0);
    /* Bad names, attributes and values. */
    NAOS_CHECK_UINT_EQ(HTML_START("<1a>"), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("<a_b>"), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("<a.b>"), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("<a 1b>"), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("<a b=c=d>"), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("<a b=c`d>"), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("<a b=c<d>"), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("<a b=\"c\"d>"), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("<a b='c'd='e'>"), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("<a/ >"), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("<a//>"), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("</a b>"), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("</a/>"), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("<a\xC2\xA0" "b>"), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("<a\vb>"), 0);
}

NAOS_TEST(html_start_rejects_other_lines)
{
    NAOS_CHECK_UINT_EQ(amark_scan_html_start(NULL, 0), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("<"), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("div>"), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("<>"), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("<\xC3\xA9>"), 0);
    NAOS_CHECK_UINT_EQ(HTML_START("Foo"), 0);
}

NAOS_TEST(html_start_reads_only_the_given_bytes)
{
    static const char div[] = "<divx>";
    static const char pre[] = "<prex";
    static const char tag[] = "<a> x";
    static const char cdata[] = "<![CDATA[";

    /* The bytes after len must not count: "<div" ends the line. */
    NAOS_CHECK_UINT_EQ(amark_scan_html_start((const unsigned char *)div, 4),
                       6);
    NAOS_CHECK_UINT_EQ(amark_scan_html_start((const unsigned char *)div, 5),
                       0);
    NAOS_CHECK_UINT_EQ(amark_scan_html_start((const unsigned char *)pre, 4),
                       1);
    NAOS_CHECK_UINT_EQ(amark_scan_html_start((const unsigned char *)tag, 3),
                       7);
    NAOS_CHECK_UINT_EQ(amark_scan_html_start((const unsigned char *)tag, 2),
                       0);
    NAOS_CHECK_UINT_EQ(amark_scan_html_start((const unsigned char *)cdata, 8),
                       0);
    NAOS_CHECK_UINT_EQ(amark_scan_html_start((const unsigned char *)cdata, 3),
                       0);
}

NAOS_TEST(html_end_type_1_closing_raw_text_tags)
{
    NAOS_CHECK_TRUE(HTML_END("</pre>", 1));
    NAOS_CHECK_TRUE(HTML_END("main = print $ parseTags tags</code></pre>", 1));
    NAOS_CHECK_TRUE(HTML_END("</script>", 1));
    NAOS_CHECK_TRUE(HTML_END("x</style>y", 1));
    NAOS_CHECK_TRUE(HTML_END("</textarea>", 1));
    /* Case-insensitive, and need not match the start tag. */
    NAOS_CHECK_TRUE(HTML_END("</PRE>", 1));
    NAOS_CHECK_TRUE(HTML_END("</TextArea>", 1));
    NAOS_CHECK_FALSE(HTML_END("</pre", 1));
    NAOS_CHECK_FALSE(HTML_END("</pre >", 1));
    NAOS_CHECK_FALSE(HTML_END("<pre>", 1));
    NAOS_CHECK_FALSE(HTML_END("</div>", 1));
    NAOS_CHECK_FALSE(HTML_END("</prex>", 1));
    NAOS_CHECK_FALSE(HTML_END("", 1));
    NAOS_CHECK_FALSE(amark_scan_html_end(NULL, 0, 1));
}

NAOS_TEST(html_end_type_1_after_partial_matches)
{
    /* A failed candidate must not hide a match that starts inside it. */
    NAOS_CHECK_TRUE(HTML_END("</</pre>", 1));
    NAOS_CHECK_TRUE(HTML_END("<</style>", 1));
    NAOS_CHECK_TRUE(HTML_END("</scrip</script>", 1));
    NAOS_CHECK_TRUE(HTML_END("</textare</textarea>", 1));
}

NAOS_TEST(html_end_types_2_3_and_5_substrings)
{
    NAOS_CHECK_TRUE(HTML_END("-->", 2));
    NAOS_CHECK_TRUE(HTML_END("bar baz -->", 2));
    NAOS_CHECK_TRUE(HTML_END("<!-- a -->", 2));
    NAOS_CHECK_TRUE(HTML_END("<!-->", 2));
    NAOS_CHECK_TRUE(HTML_END("--->", 2));
    NAOS_CHECK_FALSE(HTML_END("-- >", 2));
    NAOS_CHECK_FALSE(HTML_END("->", 2));
    NAOS_CHECK_FALSE(HTML_END("<!--", 2));

    NAOS_CHECK_TRUE(HTML_END("?>", 3));
    NAOS_CHECK_TRUE(HTML_END("<?php echo 1; ?> x", 3));
    NAOS_CHECK_FALSE(HTML_END("? >", 3));
    NAOS_CHECK_FALSE(HTML_END(">?", 3));

    NAOS_CHECK_TRUE(HTML_END("]]>", 5));
    NAOS_CHECK_TRUE(HTML_END("<![CDATA[x]]>", 5));
    NAOS_CHECK_TRUE(HTML_END("]]]>", 5));
    NAOS_CHECK_FALSE(HTML_END("]>", 5));
    NAOS_CHECK_FALSE(HTML_END("]] >", 5));
}

NAOS_TEST(html_end_type_4_greater_than)
{
    NAOS_CHECK_TRUE(HTML_END(">", 4));
    NAOS_CHECK_TRUE(HTML_END("<!DOCTYPE html>", 4));
    NAOS_CHECK_FALSE(HTML_END("<!DOCTYPE html", 4));
    NAOS_CHECK_FALSE(HTML_END("", 4));
}

NAOS_TEST(html_end_types_6_and_7_are_decided_by_the_caller)
{
    NAOS_CHECK_FALSE(HTML_END("</div>", 6));
    NAOS_CHECK_FALSE(HTML_END("", 6));
    NAOS_CHECK_FALSE(HTML_END("<a>", 7));
    NAOS_CHECK_FALSE(HTML_END("-->", 0));
    NAOS_CHECK_FALSE(HTML_END("-->", 8));
}

NAOS_TEST(html_end_on_the_start_line)
{
    /* The start line itself may hold the end condition. */
    static const char *const lines[] = {
        "<style>p{color:red;}</style>", "<!-- foo -->", "<?php ?>",
        "<!DOCTYPE html>", "<![CDATA[x]]>"
    };
    unsigned i;

    for (i = 0; i < 5; i++) {
        const unsigned char *p = (const unsigned char *)lines[i];
        size_t len = strlen(lines[i]);
        NAOS_CHECK_UINT_EQ(amark_scan_html_start(p, len), i + 1);
        NAOS_CHECK_TRUE(amark_scan_html_end(p, len, i + 1));
    }
}

NAOS_TEST(html_end_reads_only_the_given_bytes)
{
    static const char line[] = "x</pre>";
    static const char comment[] = "a-->";

    NAOS_CHECK_TRUE(amark_scan_html_end((const unsigned char *)line, 7, 1));
    NAOS_CHECK_FALSE(amark_scan_html_end((const unsigned char *)line, 6, 1));
    NAOS_CHECK_FALSE(amark_scan_html_end((const unsigned char *)comment, 3,
                                         2));
    NAOS_CHECK_FALSE(amark_scan_html_end((const unsigned char *)comment, 3,
                                         4));
}

NAOS_TEST(html_end_scans_a_long_line_of_near_misses)
{
    /* Many candidates that fail late, then a match at the very end. */
    unsigned char line[3 * 4096];
    size_t i;

    for (i = 0; i < sizeof(line); i += 3) {
        line[i] = '<';
        line[i + 1] = '/';
        line[i + 2] = 'p';
    }
    NAOS_CHECK_FALSE(amark_scan_html_end(line, sizeof(line), 1));
    NAOS_CHECK_FALSE(amark_scan_html_end(line, sizeof(line), 2));
    line[sizeof(line) - 3] = '-';
    line[sizeof(line) - 2] = '-';
    line[sizeof(line) - 1] = '>';
    NAOS_CHECK_TRUE(amark_scan_html_end(line, sizeof(line), 2));
    NAOS_CHECK_TRUE(amark_scan_html_end(line, sizeof(line), 4));
    NAOS_CHECK_FALSE(amark_scan_html_end(line, sizeof(line), 1));
}

NAOS_SUITE(thematic_break,
    NAOS_ADD_TEST(thematic_break_accepts_three_characters_of_one_kind),
    NAOS_ADD_TEST(thematic_break_rejects_other_characters_and_fewer_than_three),
    NAOS_ADD_TEST(thematic_break_allows_spaces_and_tabs_between_and_after),
    NAOS_ADD_TEST(thematic_break_rejects_other_bytes_and_mixed_characters),
    NAOS_ADD_TEST(thematic_break_treats_nul_and_unicode_space_as_content)
)

NAOS_SUITE(atx_heading,
    NAOS_ADD_TEST(atx_heading_levels_one_to_six),
    NAOS_ADD_TEST(atx_heading_rejects_seven_and_missing_space),
    NAOS_ADD_TEST(atx_heading_strips_white_space_around_the_content),
    NAOS_ADD_TEST(atx_heading_strips_the_closing_sequence),
    NAOS_ADD_TEST(atx_heading_keeps_hashes_that_are_not_a_closing_sequence),
    NAOS_ADD_TEST(atx_heading_with_no_content),
    NAOS_ADD_TEST(atx_heading_empty_content_bounds_are_inside_the_line),
    NAOS_ADD_TEST(atx_heading_treats_nul_and_unicode_space_as_content)
)

NAOS_SUITE(setext_underline,
    NAOS_ADD_TEST(setext_underline_of_equals_and_dashes),
    NAOS_ADD_TEST(setext_underline_rejects_gaps_and_other_bytes),
    NAOS_ADD_TEST(setext_underline_treats_nul_and_unicode_space_as_content)
)

NAOS_SUITE(fence,
    NAOS_ADD_TEST(open_fence_of_backticks_and_tildes),
    NAOS_ADD_TEST(open_fence_rejects_short_runs_and_other_characters),
    NAOS_ADD_TEST(open_fence_info_string_without_surrounding_white_space),
    NAOS_ADD_TEST(open_fence_backtick_info_must_not_contain_backticks),
    NAOS_ADD_TEST(open_fence_treats_nul_and_unicode_space_as_content),
    NAOS_ADD_TEST(close_fence_returns_the_length_of_the_run),
    NAOS_ADD_TEST(close_fence_rejects_other_characters_and_info_strings),
    NAOS_ADD_TEST(close_fence_treats_nul_and_unicode_space_as_content)
)

NAOS_SUITE(list_marker,
    NAOS_ADD_TEST(list_marker_bullets_of_three_characters),
    NAOS_ADD_TEST(list_marker_ordered_with_two_delimiters),
    NAOS_ADD_TEST(list_marker_start_numbers_zero_and_largest),
    NAOS_ADD_TEST(list_marker_rejects_ten_digits),
    NAOS_ADD_TEST(list_marker_needs_white_space_or_end_after_the_marker),
    NAOS_ADD_TEST(list_marker_rejects_other_shapes),
    NAOS_ADD_TEST(list_marker_alone_on_the_line_is_blank_after),
    NAOS_ADD_TEST(list_marker_content_after_white_space_is_not_blank),
    NAOS_ADD_TEST(list_marker_treats_nul_and_unicode_space_as_content),
    NAOS_ADD_TEST(list_marker_reads_only_the_given_bytes)
)

NAOS_SUITE(html_block,
    NAOS_ADD_TEST(html_start_type_1_raw_text_tags),
    NAOS_ADD_TEST(html_start_type_1_needs_a_delimiter_after_the_name),
    NAOS_ADD_TEST(html_start_type_2_comment),
    NAOS_ADD_TEST(html_start_type_3_processing_instruction),
    NAOS_ADD_TEST(html_start_type_4_declaration),
    NAOS_ADD_TEST(html_start_type_5_cdata),
    NAOS_ADD_TEST(html_start_type_6_block_tag_names),
    NAOS_ADD_TEST(html_start_type_6_every_name_of_the_list),
    NAOS_ADD_TEST(html_start_type_6_names_ignore_ascii_case),
    NAOS_ADD_TEST(html_start_type_6_rejects_other_names_and_delimiters),
    NAOS_ADD_TEST(html_start_type_7_complete_open_tags),
    NAOS_ADD_TEST(html_start_type_7_complete_closing_tags),
    NAOS_ADD_TEST(html_start_type_7_rejects_incomplete_or_followed_tags),
    NAOS_ADD_TEST(html_start_rejects_other_lines),
    NAOS_ADD_TEST(html_start_reads_only_the_given_bytes),
    NAOS_ADD_TEST(html_end_type_1_closing_raw_text_tags),
    NAOS_ADD_TEST(html_end_type_1_after_partial_matches),
    NAOS_ADD_TEST(html_end_types_2_3_and_5_substrings),
    NAOS_ADD_TEST(html_end_type_4_greater_than),
    NAOS_ADD_TEST(html_end_types_6_and_7_are_decided_by_the_caller),
    NAOS_ADD_TEST(html_end_on_the_start_line),
    NAOS_ADD_TEST(html_end_reads_only_the_given_bytes),
    NAOS_ADD_TEST(html_end_scans_a_long_line_of_near_misses)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(thematic_break),
    NAOS_ADD_SUITE(atx_heading),
    NAOS_ADD_SUITE(setext_underline),
    NAOS_ADD_SUITE(fence),
    NAOS_ADD_SUITE(list_marker),
    NAOS_ADD_SUITE(html_block)
)
