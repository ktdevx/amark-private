/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of the link labels (src/definition.h): scanning a label
 * (escaped and unescaped brackets, the 999 character limit, labels of only
 * white space, labels that cross lines, inside containers through the line
 * records) and comparing two labels after normalizing them (case folding,
 * one code point folding to several, collapsing white space, escapes and
 * character references compared as written, U+0000 and ill-formed bytes
 * compared as U+FFFD), without allocating.
 */

#include "definition.h"
#include "document.h"
#include "line.h"
#include "parse_data.h"
#include "test_alloc.h"

#include <naos/naos.h>

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/*
 * Fixture. NAOS_REQUIRE_* leaves a test with longjmp, so the document a test
 * creates is kept in g_doc, and teardown destroys it and checks that no
 * block is left.
 */
#define BUFFER_SIZE 8192

static amark_test_alloc_t g_state;
static amark_allocator_t g_allocator;
static amark_document_t *g_doc;
static unsigned char g_buffer[BUFFER_SIZE];
static size_t g_length;

NAOS_SETUP(counting)
{
    amark_test_alloc_init(&g_state);
    g_allocator = amark_test_allocator(&g_state);
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

/* Appends length bytes to g_buffer. */
static void append(const char *bytes, size_t length)
{
    NAOS_REQUIRE_UINT_LE(length, BUFFER_SIZE - g_length);
    memcpy(g_buffer + g_length, bytes, length);
    g_length += length;
}

/* Appends the bytes of a string literal, without the terminating NUL. */
#define APPEND(s) append((s), sizeof(s) - 1)

/* Appends count copies of the bytes of a string literal. */
#define APPEND_REPEATED(s, count)                                             \
    do {                                                                      \
        size_t repeat_;                                                       \
        for (repeat_ = 0; repeat_ < (count); repeat_++) {                     \
            APPEND(s);                                                        \
        }                                                                     \
    } while (0)

/* Creates g_doc over g_buffer, the range being the whole content. */
static void create_doc(bool with_parse_data)
{
    NAOS_REQUIRE_INT_EQ(amark_document_create(&g_allocator, g_buffer,
                                              g_length, 0, g_length, NULL,
                                              &g_doc),
                        AMARK_ERROR_NONE);
    if (with_parse_data) {
        NAOS_REQUIRE_INT_EQ(amark_parse_data_create(g_doc),
                            AMARK_ERROR_NONE);
    }
}

/* The offset of the first byte c at or after from; it must exist. */
static size_t find_byte(size_t from, char c)
{
    size_t i;

    for (i = from; i < g_length; i++) {
        if (g_buffer[i] == (unsigned char)c) {
            return i;
        }
    }
    NAOS_REQUIRE_TRUE(false);
    return 0;
}

/* Scans the label opening at open with limit g_length. */
static bool scan(size_t open, size_t *start, size_t *end)
{
    return amark_label_scan(g_doc, open, g_length, start, end);
}

/* Checks that the label opening at open is [start, end). */
static void check_scan(size_t open, size_t start, size_t end)
{
    size_t got_start = 0;
    size_t got_end = 0;

    NAOS_REQUIRE_TRUE(scan(open, &got_start, &got_end));
    NAOS_CHECK_UINT_EQ(got_start, start);
    NAOS_CHECK_UINT_EQ(got_end, end);
}

/* Checks that the label opening at open is no label and outputs are kept. */
static void check_no_label(size_t open)
{
    size_t start = 12345;
    size_t end = 67890;

    NAOS_CHECK_FALSE(scan(open, &start, &end));
    NAOS_CHECK_UINT_EQ(start, 12345);
    NAOS_CHECK_UINT_EQ(end, 67890);
}

/* Returns -1, 0 or 1 by the sign of value. */
static int sign(int value)
{
    return value < 0 ? -1 : (value > 0 ? 1 : 0);
}

/*
 * Replaces g_doc with one over a followed by b (no parse data) and returns
 * the sign of comparing them; also checks that the comparison is
 * antisymmetric and allocates nothing.
 */
static int compare_bytes(const char *a, size_t a_length, const char *b,
                         size_t b_length)
{
    size_t calls;
    int ab;
    int ba;

    amark_document_destroy(g_doc);
    g_doc = NULL;
    g_length = 0;
    append(a, a_length);
    append(b, b_length);
    create_doc(false);
    calls = g_state.malloc_calls;
    ab = sign(amark_label_compare(g_doc, 0, a_length, a_length,
                                  a_length + b_length));
    ba = sign(amark_label_compare(g_doc, a_length, a_length + b_length, 0,
                                  a_length));
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, calls);
    NAOS_CHECK_INT_EQ(ab, -ba);
    return ab;
}

#define COMPARE(a, b) compare_bytes((a), sizeof(a) - 1, (b), sizeof(b) - 1)

/* ---- scanning ----------------------------------------------------------- */

NAOS_TEST(scan_reads_up_to_the_closing_bracket)
{
    APPEND("[foo]: /url");
    create_doc(true);
    check_scan(0, 1, 4);
}

NAOS_TEST(scan_starts_at_open)
{
    APPEND("ab [x y] c");
    create_doc(true);
    check_scan(3, 4, 7);
}

NAOS_TEST(scan_skips_escaped_brackets)
{
    APPEND("[a\\]b\\[c]");
    create_doc(true);
    check_scan(0, 1, 8);
}

NAOS_TEST(scan_ends_at_bracket_after_escaped_backslash)
{
    APPEND("[a\\\\]b]");
    create_doc(true);
    check_scan(0, 1, 4);
}

NAOS_TEST(scan_rejects_an_unescaped_open_bracket)
{
    APPEND("[a[b]");
    create_doc(true);
    check_no_label(0);
}

NAOS_TEST(scan_rejects_open_bracket_after_escaped_backslash)
{
    APPEND("[\\\\[]");
    create_doc(true);
    check_no_label(0);
}

NAOS_TEST(scan_rejects_a_missing_closing_bracket)
{
    APPEND("[abc");
    create_doc(true);
    check_no_label(0);
}

NAOS_TEST(scan_reads_no_closing_bracket_at_or_past_limit)
{
    size_t start = 7;
    size_t end = 7;

    APPEND("[abc]");
    create_doc(true);
    NAOS_CHECK_FALSE(amark_label_scan(g_doc, 0, 4, &start, &end));
    NAOS_CHECK_UINT_EQ(start, 7);
    NAOS_CHECK_UINT_EQ(end, 7);
    NAOS_CHECK_TRUE(amark_label_scan(g_doc, 0, 5, &start, &end));
    NAOS_CHECK_UINT_EQ(end, 4);
}

NAOS_TEST(scan_rejects_an_escaped_closing_bracket_at_the_end)
{
    APPEND("[abc\\]");
    create_doc(true);
    check_no_label(0);
}

NAOS_TEST(scan_accepts_999_characters)
{
    APPEND("[");
    APPEND_REPEATED("a", 999);
    APPEND("]");
    create_doc(true);
    check_scan(0, 1, 1000);
}

NAOS_TEST(scan_rejects_1000_characters)
{
    APPEND("[");
    APPEND_REPEATED("a", 1000);
    APPEND("]");
    create_doc(true);
    check_no_label(0);
}

NAOS_TEST(scan_counts_code_points_not_bytes)
{
    /* U+00E9 is two bytes and U+1F600 four, each one character. */
    APPEND("[");
    APPEND_REPEATED("\xC3\xA9", 998);
    APPEND("\xF0\x9F\x98\x80");
    APPEND("]");
    create_doc(true);
    check_scan(0, 1, 1 + 998 * 2 + 4);
}

NAOS_TEST(scan_rejects_1000_code_points)
{
    APPEND("[");
    APPEND_REPEATED("\xC3\xA9", 1000);
    APPEND("]");
    create_doc(true);
    check_no_label(0);
}

NAOS_TEST(scan_counts_an_escape_as_two_characters)
{
    APPEND("[");
    APPEND_REPEATED("\\]", 499);
    APPEND("a]");
    APPEND("[");
    APPEND_REPEATED("\\]", 500);
    APPEND("]");
    create_doc(true);
    check_scan(0, 1, 1 + 499 * 2 + 1);
    check_no_label(1 + 499 * 2 + 2);
}

NAOS_TEST(scan_rejects_labels_of_only_white_space)
{
    APPEND("[]\n[ \t ]\n[ \n ]");
    create_doc(false);
    check_no_label(0);
    check_no_label(3);
    check_no_label(9);
}

NAOS_TEST(scan_accepts_a_label_with_one_visible_character)
{
    APPEND("[ \t\\ ]");
    create_doc(false);
    check_scan(0, 1, 5);
}

NAOS_TEST(scan_crosses_lines)
{
    /* "[foo" is line 1, "bar]" line 2 (content_start 5). */
    APPEND("[foo\nbar]");
    create_doc(true);
    amark_line_set(g_doc, 2, 5, 0);
    check_scan(0, 1, 8);
}

NAOS_TEST(scan_crosses_lines_without_parse_data)
{
    APPEND("[foo\r\nbar]");
    create_doc(false);
    check_scan(0, 1, 9);
}

NAOS_TEST(scan_skips_container_prefixes)
{
    /* "> [foo" is line 1, "> bar]" line 2 whose content starts at 'b'. */
    APPEND("> [foo\n> bar]");
    create_doc(true);
    amark_line_set(g_doc, 2, 9, 0);
    check_scan(2, 3, 12);
}

NAOS_TEST(scan_counts_no_prefix_bytes_and_one_per_line_ending)
{
    size_t second;

    /* 997 + line ending + 1 = 999 characters; the prefix ">   " is none. */
    APPEND("> [");
    APPEND_REPEATED("a", 997);
    APPEND("\r\n>   b]\n");
    /* 997 + line ending + 2 = 1000 characters. */
    second = g_length;
    APPEND("> [");
    APPEND_REPEATED("a", 997);
    APPEND("\n>   bc]");
    create_doc(true);
    amark_line_set(g_doc, 2, find_byte(0, 'b'), 0);
    amark_line_set(g_doc, 4, find_byte(second, 'b'), 0);
    check_scan(2, 3, find_byte(0, ']'));
    check_no_label(second + 2);
}

NAOS_TEST(scan_reads_the_line_record_not_the_prefix)
{
    /* With the record at '>', the '[' of the prefix is inside the label. */
    APPEND("[a\n[b]");
    create_doc(true);
    amark_line_set(g_doc, 2, 4, 0);
    check_scan(0, 1, 5);
    amark_line_set(g_doc, 2, 3, 0);
    check_no_label(0);
}

NAOS_TEST(scan_allocates_nothing)
{
    size_t calls;
    size_t start;
    size_t end;

    APPEND("> [foo\n> bar]");
    create_doc(true);
    amark_line_set(g_doc, 2, 9, 0);
    calls = g_state.malloc_calls;
    NAOS_CHECK_TRUE(scan(2, &start, &end));
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, calls);
}

/* ---- comparing ---------------------------------------------------------- */

NAOS_TEST(compare_equal_labels)
{
    NAOS_CHECK_INT_EQ(COMPARE("foo", "foo"), 0);
}

NAOS_TEST(compare_orders_by_code_point)
{
    NAOS_CHECK_INT_EQ(COMPARE("a", "b"), -1);
    NAOS_CHECK_INT_EQ(COMPARE("ab", "a"), 1);
}

NAOS_TEST(compare_orders_a_prefix_first)
{
    NAOS_CHECK_INT_EQ(COMPARE("foo", "foo bar"), -1);
}

NAOS_TEST(compare_orders_code_points_above_ascii)
{
    /* U+00E9 is after 'z'; U+1F600 is after U+FFFD. */
    NAOS_CHECK_INT_EQ(COMPARE("z", "\xC3\xA9"), -1);
    NAOS_CHECK_INT_EQ(COMPARE("\xF0\x9F\x98\x80", "\xEF\xBF\xBD"), 1);
}

NAOS_TEST(compare_ignores_case)
{
    NAOS_CHECK_INT_EQ(COMPARE("Foo", "fOO"), 0);
    NAOS_CHECK_INT_EQ(COMPARE("\xCE\x91\xCE\x93\xCE\xA9",
                              "\xCE\xB1\xCE\xB3\xCF\x89"), 0);
}

NAOS_TEST(compare_folds_one_code_point_to_several)
{
    /* U+1E9E and U+00DF fold to "ss". */
    NAOS_CHECK_INT_EQ(COMPARE("\xE1\xBA\x9E", "ss"), 0);
}

NAOS_TEST(compare_capital_sharp_s_with_capital_ss)
{
    NAOS_CHECK_INT_EQ(COMPARE("\xE1\xBA\x9E", "SS"), 0);
}

NAOS_TEST(compare_sharp_s_with_capital_sharp_s)
{
    NAOS_CHECK_INT_EQ(COMPARE("\xC3\x9F", "\xE1\xBA\x9E"), 0);
}

NAOS_TEST(compare_expansion_in_the_middle)
{
    NAOS_CHECK_INT_EQ(COMPARE("Stra\xC3\x9F" "e", "STRASSE"), 0);
}

NAOS_TEST(compare_expansion_against_a_shorter_label)
{
    NAOS_CHECK_INT_EQ(COMPARE("\xE1\xBA\x9E", "s"), 1);
}

NAOS_TEST(compare_expansion_differing_in_its_second_code_point)
{
    NAOS_CHECK_INT_EQ(COMPARE("\xE1\xBA\x9E", "st"), -1);
}

NAOS_TEST(compare_expansions_on_both_sides)
{
    /* U+FB03 folds to "ffi"; U+FB00 to "ff". */
    NAOS_CHECK_INT_EQ(COMPARE("\xEF\xAC\x83", "\xEF\xAC\x80i"), 0);
    NAOS_CHECK_INT_EQ(COMPARE("\xEF\xAC\x83" "\xC3\x9F", "ffiSS"), 0);
}

NAOS_TEST(compare_collapses_white_space)
{
    NAOS_CHECK_INT_EQ(COMPARE("foo \t  bar", "foo bar"), 0);
    NAOS_CHECK_INT_EQ(COMPARE("foo\tbar", "foo bar"), 0);
}

NAOS_TEST(compare_keeps_one_space_between_words)
{
    NAOS_CHECK_INT_EQ(COMPARE("foo  bar", "foobar"), -1);
}

NAOS_TEST(compare_drops_leading_and_trailing_white_space)
{
    NAOS_CHECK_INT_EQ(COMPARE(" \t foo \t ", "foo"), 0);
    NAOS_CHECK_INT_EQ(COMPARE("\n foo\r\n", "foo"), 0);
}

NAOS_TEST(compare_line_endings_as_white_space)
{
    NAOS_CHECK_INT_EQ(COMPARE("foo\nbar", "foo bar"), 0);
    NAOS_CHECK_INT_EQ(COMPARE("foo \r\n bar", "foo bar"), 0);
    NAOS_CHECK_INT_EQ(COMPARE("foo\rbar", "foo bar"), 0);
}

NAOS_TEST(compare_empty_labels)
{
    NAOS_CHECK_INT_EQ(COMPARE("", ""), 0);
    NAOS_CHECK_INT_EQ(COMPARE(" \t", ""), 0);
    NAOS_CHECK_INT_EQ(COMPARE("", "a"), -1);
}

NAOS_TEST(compare_keeps_backslash_escapes)
{
    NAOS_CHECK_INT_EQ(COMPARE("\\*", "*"), 1);
    NAOS_CHECK_INT_EQ(COMPARE("a\\]", "a]"), -1); /* 0x5C < 0x5D */
}

NAOS_TEST(compare_keeps_character_references)
{
    NAOS_CHECK_INT_EQ(COMPARE("&amp;", "&"), 1);
    NAOS_CHECK_INT_EQ(COMPARE("&#65;", "a"), -1);
    NAOS_CHECK_INT_EQ(COMPARE("&AMP;", "&amp;"), 0);
}

NAOS_TEST(compare_nul_as_replacement_character)
{
    NAOS_CHECK_INT_EQ(compare_bytes("a\0b", 3, "a\xEF\xBF\xBD" "b", 5), 0);
}

NAOS_TEST(compare_ill_formed_bytes_as_replacement_character)
{
    NAOS_CHECK_INT_EQ(COMPARE("a\xFF" "b", "a\xEF\xBF\xBD" "b"), 0);
}

NAOS_TEST(compare_truncated_sequence_as_one_replacement_character)
{
    /* E1 BA is the maximal subpart of U+1E9E: one U+FFFD, then 'b'. */
    NAOS_CHECK_INT_EQ(COMPARE("a\xE1\xBA" "b", "a\xEF\xBF\xBD" "b"), 0);
    NAOS_CHECK_INT_EQ(COMPARE("\xE1\xBA",
                              "\xEF\xBF\xBD" "\xEF\xBF\xBD"), -1);
}

NAOS_TEST(compare_ill_formed_bytes_at_the_end_of_the_range)
{
    NAOS_CHECK_INT_EQ(COMPARE("\xF0\x9F\x98", "\xEF\xBF\xBD"), 0);
}

NAOS_TEST(compare_skips_container_prefixes)
{
    size_t second;
    size_t calls;

    /* "> [Foo" line 1, ">   bar]" line 2 (content at 'b'), "foo bar". */
    APPEND("> [Foo\n>   bar]\nfoo bar");
    create_doc(true);
    amark_line_set(g_doc, 2, find_byte(0, 'b'), 0);
    second = find_byte(0, ']') + 2;
    calls = g_state.malloc_calls;
    NAOS_CHECK_INT_EQ(amark_label_compare(g_doc, 3, second - 2, second,
                                          g_length), 0);
    NAOS_CHECK_INT_EQ(amark_label_compare(g_doc, second, g_length, 3,
                                          second - 2), 0);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, calls);
}

NAOS_TEST(compare_reads_prefix_when_record_includes_it)
{
    /* With the record at '>', the prefix is part of the label. */
    APPEND("foo\n> bar|foo > bar");
    create_doc(true);
    amark_line_set(g_doc, 2, 4, 0);
    NAOS_CHECK_INT_EQ(amark_label_compare(g_doc, 0, 9, 10, g_length), 0);
    amark_line_set(g_doc, 2, 6, 0);
    NAOS_CHECK_INT_NE(amark_label_compare(g_doc, 0, 9, 10, g_length), 0);
    NAOS_CHECK_INT_GT(amark_label_compare(g_doc, 0, 9, 10, 13), 0);
}

NAOS_TEST(compare_labels_crossing_several_lines)
{
    /* Three lines in a list item: prefix "  " on lines 2 and 3. */
    APPEND("- [a\n  B\n  c]x|A b C");
    create_doc(true);
    amark_line_set(g_doc, 2, 7, 0);
    amark_line_set(g_doc, 3, 11, 0);
    NAOS_CHECK_INT_EQ(amark_label_compare(g_doc, 3, 12, 15, g_length), 0);
}

NAOS_TEST(compare_long_labels)
{
    APPEND_REPEATED("\xC3\x9F", 999);
    APPEND_REPEATED("SS", 999);
    create_doc(false);
    NAOS_CHECK_INT_EQ(amark_label_compare(g_doc, 0, 999 * 2, 999 * 2,
                                          g_length), 0);
    NAOS_CHECK_INT_EQ(amark_label_compare(g_doc, 0, 999 * 2, 999 * 2,
                                          g_length - 1), 1);
}

NAOS_SUITE(label_scan,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(scan_reads_up_to_the_closing_bracket),
    NAOS_ADD_TEST(scan_starts_at_open),
    NAOS_ADD_TEST(scan_skips_escaped_brackets),
    NAOS_ADD_TEST(scan_ends_at_bracket_after_escaped_backslash),
    NAOS_ADD_TEST(scan_rejects_an_unescaped_open_bracket),
    NAOS_ADD_TEST(scan_rejects_open_bracket_after_escaped_backslash),
    NAOS_ADD_TEST(scan_rejects_a_missing_closing_bracket),
    NAOS_ADD_TEST(scan_reads_no_closing_bracket_at_or_past_limit),
    NAOS_ADD_TEST(scan_rejects_an_escaped_closing_bracket_at_the_end),
    NAOS_ADD_TEST(scan_accepts_999_characters),
    NAOS_ADD_TEST(scan_rejects_1000_characters),
    NAOS_ADD_TEST(scan_counts_code_points_not_bytes),
    NAOS_ADD_TEST(scan_rejects_1000_code_points),
    NAOS_ADD_TEST(scan_counts_an_escape_as_two_characters),
    NAOS_ADD_TEST(scan_rejects_labels_of_only_white_space),
    NAOS_ADD_TEST(scan_accepts_a_label_with_one_visible_character),
    NAOS_ADD_TEST(scan_crosses_lines),
    NAOS_ADD_TEST(scan_crosses_lines_without_parse_data),
    NAOS_ADD_TEST(scan_skips_container_prefixes),
    NAOS_ADD_TEST(scan_counts_no_prefix_bytes_and_one_per_line_ending),
    NAOS_ADD_TEST(scan_reads_the_line_record_not_the_prefix),
    NAOS_ADD_TEST(scan_allocates_nothing)
)

NAOS_SUITE(label_compare,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(compare_equal_labels),
    NAOS_ADD_TEST(compare_orders_by_code_point),
    NAOS_ADD_TEST(compare_orders_a_prefix_first),
    NAOS_ADD_TEST(compare_orders_code_points_above_ascii),
    NAOS_ADD_TEST(compare_ignores_case),
    NAOS_ADD_TEST(compare_folds_one_code_point_to_several),
    NAOS_ADD_TEST(compare_capital_sharp_s_with_capital_ss),
    NAOS_ADD_TEST(compare_sharp_s_with_capital_sharp_s),
    NAOS_ADD_TEST(compare_expansion_in_the_middle),
    NAOS_ADD_TEST(compare_expansion_against_a_shorter_label),
    NAOS_ADD_TEST(compare_expansion_differing_in_its_second_code_point),
    NAOS_ADD_TEST(compare_expansions_on_both_sides),
    NAOS_ADD_TEST(compare_collapses_white_space),
    NAOS_ADD_TEST(compare_keeps_one_space_between_words),
    NAOS_ADD_TEST(compare_drops_leading_and_trailing_white_space),
    NAOS_ADD_TEST(compare_line_endings_as_white_space),
    NAOS_ADD_TEST(compare_empty_labels),
    NAOS_ADD_TEST(compare_keeps_backslash_escapes),
    NAOS_ADD_TEST(compare_keeps_character_references),
    NAOS_ADD_TEST(compare_nul_as_replacement_character),
    NAOS_ADD_TEST(compare_ill_formed_bytes_as_replacement_character),
    NAOS_ADD_TEST(compare_truncated_sequence_as_one_replacement_character),
    NAOS_ADD_TEST(compare_ill_formed_bytes_at_the_end_of_the_range),
    NAOS_ADD_TEST(compare_skips_container_prefixes),
    NAOS_ADD_TEST(compare_reads_prefix_when_record_includes_it),
    NAOS_ADD_TEST(compare_labels_crossing_several_lines),
    NAOS_ADD_TEST(compare_long_labels)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(label_scan),
    NAOS_ADD_SUITE(label_compare)
)
