/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of the block scanners (src/scan.h): thematic breaks, ATX
 * headings, setext heading underlines, and opening and closing code fences.
 * Each case is one line given as bytes, starting at the first byte that is
 * not white space (the indentation is the caller's business), taken from the
 * matching and non-matching examples of the CommonMark 0.31.2 sections 4.1,
 * 4.2, 4.3 and 4.5. Only the space and the tab are white space; U+0000 and
 * Unicode white space (U+00A0 as C2 A0) are not.
 */

#include "scan.h"

#include <naos/naos.h>

#include <stdbool.h>
#include <stddef.h>
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

NAOS_MAIN(
    NAOS_ADD_SUITE(thematic_break),
    NAOS_ADD_SUITE(atx_heading),
    NAOS_ADD_SUITE(setext_underline),
    NAOS_ADD_SUITE(fence)
)
