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
 * compared as U+FFFD), without allocating; and building and searching the
 * definition table (the first of several definitions with the same label
 * wins, misses, an empty table, results that do not depend on the order of
 * the labels, thousands of definitions, and allocation failures).
 */

#include "arena.h"
#include "definition.h"
#include "document.h"
#include "line.h"
#include "parse_data.h"
#include "test_alloc.h"

#include <naos/naos.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/*
 * Fixture. NAOS_REQUIRE_* leaves a test with longjmp, so the document a test
 * creates is kept in g_doc, and teardown destroys it and checks that no
 * block is left.
 */
#define BUFFER_SIZE 65536

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

/* ---- the definition table ---------------------------------------------- */

/*
 * Fixture of the table tests: the counting fixture plus a scratch arena
 * (released in teardown, before the leak check) and the definitions and
 * queries the test writes into g_buffer.
 */
#define MAX_ENTRIES 4096

typedef struct text_range {
    size_t start;
    size_t end;
} text_range_t;

static amark_arena_t g_scratch;
static amark_definition_entry_t g_entries[MAX_ENTRIES];
static size_t g_count;
static text_range_t g_queries[MAX_ENTRIES];
static size_t g_query_count;

NAOS_SETUP(table)
{
    amark_test_alloc_init(&g_state);
    g_allocator = amark_test_allocator(&g_state);
    g_doc = NULL;
    g_length = 0;
    g_count = 0;
    g_query_count = 0;
    amark_arena_init(&g_scratch, &g_allocator);
}

NAOS_TEARDOWN(table)
{
    amark_arena_deinit(&g_scratch);
    amark_document_destroy(g_doc);
    g_doc = NULL;
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
    NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
}

/* Appends text and a line ending; returns the range of text. */
static text_range_t add_text(const char *text, size_t length)
{
    text_range_t range;

    range.start = g_length;
    append(text, length);
    range.end = g_length;
    APPEND("\n");
    return range;
}

/* Adds a definition whose label is text; its node is the next ID. */
static void add_definition(const char *text, size_t length)
{
    text_range_t range = add_text(text, length);

    NAOS_REQUIRE_UINT_LT(g_count, MAX_ENTRIES);
    g_entries[g_count].node = (amark_node_id_t)(g_count + 1);
    g_entries[g_count].label_start = range.start;
    g_entries[g_count].label_end = range.end;
    g_count++;
}

/* Adds a query label (not a definition); returns its index. */
static size_t add_query(const char *text, size_t length)
{
    NAOS_REQUIRE_UINT_LT(g_query_count, MAX_ENTRIES);
    g_queries[g_query_count] = add_text(text, length);
    return g_query_count++;
}

#define ADD_DEF(s) add_definition((s), sizeof(s) - 1)
#define ADD_QUERY(s) add_query((s), sizeof(s) - 1)

static const amark_parse_data_t *parse_data(void)
{
    return amark_document_parse_data(g_doc);
}

/* Creates g_doc with parse data and builds the table from g_entries. */
static void build(void)
{
    create_doc(true);
    NAOS_REQUIRE_INT_EQ(amark_definitions_build(g_doc, &g_scratch, g_entries,
                                                g_count),
                        AMARK_ERROR_NONE);
}

static amark_node_id_t find_range(size_t start, size_t end)
{
    return amark_definitions_find(g_doc, start, end);
}

static amark_node_id_t find_query(size_t query)
{
    return find_range(g_queries[query].start, g_queries[query].end);
}

/* The node of the first entry whose label equals [start, end), by search. */
static amark_node_id_t first_match(size_t start, size_t end)
{
    size_t i;

    for (i = 0; i < g_count; i++) {
        if (amark_label_compare(g_doc, start, end, g_entries[i].label_start,
                                g_entries[i].label_end) == 0) {
            return g_entries[i].node;
        }
    }
    return AMARK_NODE_NONE;
}

/*
 * Checks that the table is strictly increasing in normalized label order
 * (so that it holds one entry per label) and that each of its entries is the
 * first entry of g_entries with that label.
 */
static void check_table(size_t expected_count)
{
    const amark_parse_data_t *data = parse_data();
    size_t i;

    NAOS_REQUIRE_UINT_EQ(data->definition_count, expected_count);
    for (i = 0; i < data->definition_count; i++) {
        const amark_definition_entry_t *entry = &data->definitions[i];

        if (i > 0) {
            const amark_definition_entry_t *prev = &data->definitions[i - 1];

            NAOS_REQUIRE_INT_LT(amark_label_compare(g_doc, prev->label_start,
                                                    prev->label_end,
                                                    entry->label_start,
                                                    entry->label_end),
                                0);
        }
        NAOS_REQUIRE_UINT_EQ(entry->node,
                             first_match(entry->label_start,
                                         entry->label_end));
    }
}

/* Checks that finding the label of every entry gives the first match. */
static void check_every_entry_found(void)
{
    size_t i;

    for (i = 0; i < g_count; i++) {
        NAOS_REQUIRE_UINT_EQ(find_range(g_entries[i].label_start,
                                        g_entries[i].label_end),
                             first_match(g_entries[i].label_start,
                                         g_entries[i].label_end));
    }
}

NAOS_TEST(table_finds_a_definition)
{
    size_t same;
    size_t other;

    ADD_DEF("foo");
    same = ADD_QUERY("foo");
    other = ADD_QUERY("bar");
    build();
    check_table(1);
    NAOS_CHECK_UINT_EQ(find_query(same), 1);
    NAOS_CHECK_UINT_EQ(find_query(other), AMARK_NODE_NONE);
}

NAOS_TEST(table_returns_the_first_of_duplicate_labels)
{
    size_t query;

    ADD_DEF("bar");
    ADD_DEF("Foo");
    ADD_DEF("  foo ");
    ADD_DEF("baz");
    ADD_DEF("FOO");
    ADD_DEF("BAR");
    query = ADD_QUERY("foo");
    build();
    check_table(3);
    NAOS_CHECK_UINT_EQ(find_query(query), 2);
    NAOS_CHECK_UINT_EQ(find_range(g_entries[2].label_start,
                                  g_entries[2].label_end), 2);
    NAOS_CHECK_UINT_EQ(find_range(g_entries[4].label_start,
                                  g_entries[4].label_end), 2);
    NAOS_CHECK_UINT_EQ(find_range(g_entries[5].label_start,
                                  g_entries[5].label_end), 1);
}

NAOS_TEST(table_returns_the_first_when_all_labels_are_the_same)
{
    size_t i;

    for (i = 0; i < 100; i++) {
        if (i % 2 == 0) {
            ADD_DEF("Same Label");
        } else {
            ADD_DEF("same   LABEL");
        }
    }
    build();
    check_table(1);
    for (i = 0; i < g_count; i++) {
        NAOS_CHECK_UINT_EQ(find_range(g_entries[i].label_start,
                                      g_entries[i].label_end), 1);
    }
}

NAOS_TEST(table_matches_case_and_white_space_variants)
{
    size_t lower;
    size_t upper;
    size_t tabs;
    size_t lines;
    size_t ss;
    size_t different;

    ADD_DEF("Hello   World");
    ADD_DEF("\xE1\xBA\x9E"); /* U+1E9E LATIN CAPITAL LETTER SHARP S */
    lower = ADD_QUERY("hello world");
    upper = ADD_QUERY(" HELLO\tWORLD ");
    tabs = ADD_QUERY("\thello \t world\t");
    lines = ADD_QUERY("hello\nworld");
    ss = ADD_QUERY("SS");
    different = ADD_QUERY("helloworld");
    build();
    check_table(2);
    {
        /* The second line of the query starts its content at "world". */
        size_t first_line;
        size_t last_line;

        amark_document_range_lines(g_doc, g_queries[lines].start,
                                   g_queries[lines].end, &first_line,
                                   &last_line);
        NAOS_REQUIRE_UINT_EQ(last_line, first_line + 1);
        amark_line_set(g_doc, last_line, g_queries[lines].end - 5, 0);
    }
    NAOS_CHECK_UINT_EQ(find_query(lower), 1);
    NAOS_CHECK_UINT_EQ(find_query(upper), 1);
    NAOS_CHECK_UINT_EQ(find_query(tabs), 1);
    NAOS_CHECK_UINT_EQ(find_query(lines), 1);
    NAOS_CHECK_UINT_EQ(find_query(ss), 2);
    NAOS_CHECK_UINT_EQ(find_query(different), AMARK_NODE_NONE);
}

NAOS_TEST(table_misses_labels_before_between_and_after)
{
    size_t before;
    size_t between;
    size_t after;
    size_t prefix;
    size_t longer;

    ADD_DEF("d");
    ADD_DEF("b");
    ADD_DEF("f");
    before = ADD_QUERY("a");
    between = ADD_QUERY("c");
    after = ADD_QUERY("g");
    prefix = ADD_QUERY("");
    longer = ADD_QUERY("bb");
    build();
    check_table(3);
    NAOS_CHECK_UINT_EQ(find_query(before), AMARK_NODE_NONE);
    NAOS_CHECK_UINT_EQ(find_query(between), AMARK_NODE_NONE);
    NAOS_CHECK_UINT_EQ(find_query(after), AMARK_NODE_NONE);
    NAOS_CHECK_UINT_EQ(find_query(prefix), AMARK_NODE_NONE);
    NAOS_CHECK_UINT_EQ(find_query(longer), AMARK_NODE_NONE);
    check_every_entry_found();
}

NAOS_TEST(table_empty_allocates_nothing_and_finds_nothing)
{
    size_t query;
    size_t calls;

    query = ADD_QUERY("foo");
    create_doc(true);
    calls = g_state.malloc_calls;
    NAOS_CHECK_INT_EQ(amark_definitions_build(g_doc, &g_scratch, g_entries,
                                              0),
                      AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, calls);
    NAOS_CHECK_NULL(parse_data()->definitions);
    NAOS_CHECK_UINT_EQ(parse_data()->definition_count, 0);
    NAOS_CHECK_UINT_EQ(find_query(query), AMARK_NODE_NONE);
}

NAOS_TEST(table_find_without_parse_data_finds_nothing)
{
    size_t query;

    query = ADD_QUERY("foo");
    create_doc(false);
    NAOS_CHECK_UINT_EQ(find_query(query), AMARK_NODE_NONE);
}

NAOS_TEST(table_find_allocates_nothing)
{
    size_t hit;
    size_t miss;
    size_t calls;

    ADD_DEF("foo");
    ADD_DEF("bar");
    hit = ADD_QUERY("FOO");
    miss = ADD_QUERY("qux");
    build();
    calls = g_state.malloc_calls;
    NAOS_CHECK_UINT_EQ(find_query(hit), 1);
    NAOS_CHECK_UINT_EQ(find_query(miss), AMARK_NODE_NONE);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, calls);
}

NAOS_TEST(table_outlives_the_scratch_arena)
{
    size_t query;

    ADD_DEF("b");
    ADD_DEF("a");
    ADD_DEF("B");
    query = ADD_QUERY("A");
    build();
    amark_arena_deinit(&g_scratch);
    check_table(2);
    NAOS_CHECK_UINT_EQ(find_query(query), 2);
    NAOS_CHECK_UINT_EQ(find_range(g_entries[2].label_start,
                                  g_entries[2].label_end), 1);
}

/* A small deterministic generator for the permutations below. */
static unsigned long next_random(unsigned long *state)
{
    *state = (*state * 1103515245UL + 12345UL) & 0x7FFFFFFFUL;
    return *state >> 8;
}

NAOS_TEST(table_result_does_not_depend_on_the_order)
{
    static const char *const labels[] = {
        "alpha", "Beta", "ALPHA", "gamma", " beta", "Delta", "gAmMa",
        "epsilon", "alpha  ", "zeta"
    };
    const size_t label_count = sizeof(labels) / sizeof(labels[0]);
    size_t order[sizeof(labels) / sizeof(labels[0])];
    unsigned long state = 1;
    size_t round;

    for (round = 0; round < 40; round++) {
        size_t i;
        size_t queries[3];

        for (i = 0; i < label_count; i++) {
            order[i] = i;
        }
        for (i = label_count - 1; i > 0; i--) {
            size_t j = (size_t)(next_random(&state) % (i + 1));
            size_t t = order[i];

            order[i] = order[j];
            order[j] = t;
        }
        amark_document_destroy(g_doc);
        g_doc = NULL;
        amark_arena_deinit(&g_scratch);
        g_length = 0;
        g_count = 0;
        g_query_count = 0;
        for (i = 0; i < label_count; i++) {
            add_definition(labels[order[i]], strlen(labels[order[i]]));
        }
        queries[0] = ADD_QUERY("ALPHA");
        queries[1] = ADD_QUERY("beta");
        queries[2] = ADD_QUERY("theta");
        build();
        check_table(6);
        check_every_entry_found();
        NAOS_CHECK_UINT_EQ(find_query(queries[0]),
                           first_match(g_queries[0].start,
                                       g_queries[0].end));
        NAOS_CHECK_UINT_EQ(find_query(queries[1]),
                           first_match(g_queries[1].start,
                                       g_queries[1].end));
        NAOS_CHECK_UINT_EQ(find_query(queries[2]), AMARK_NODE_NONE);
    }
}

/*
 * Adds count definitions whose labels are "label <key>" with key
 * (i * 7919) % keys, written in three case and white space variants, and
 * stores in first[key] the node of the first definition of each key.
 */
static void add_many(size_t count, size_t keys, amark_node_id_t *first)
{
    size_t i;

    for (i = 0; i < keys; i++) {
        first[i] = AMARK_NODE_NONE;
    }
    for (i = 0; i < count; i++) {
        char text[64];
        size_t key = (i * 7919) % keys;
        int length;

        switch (i % 3) {
        case 0:
            length = snprintf(text, sizeof text, "label %u", (unsigned)key);
            break;
        case 1:
            length = snprintf(text, sizeof text, "LABEL\t%u", (unsigned)key);
            break;
        default:
            length = snprintf(text, sizeof text, " Label   %u ", (unsigned)key);
            break;
        }
        add_definition(text, (size_t)length);
        if (first[key] == AMARK_NODE_NONE) {
            first[key] = (amark_node_id_t)(i + 1);
        }
    }
}

NAOS_TEST(table_with_thousands_of_definitions)
{
    static amark_node_id_t first[1000];
    size_t i;
    size_t miss_low;
    size_t miss_high;

    add_many(3000, 1000, first);
    miss_low = ADD_QUERY("label");
    miss_high = ADD_QUERY("label 1000");
    build();
    check_table(1000);
    for (i = 0; i < g_count; i++) {
        size_t key = (i * 7919) % 1000;

        NAOS_REQUIRE_UINT_EQ(find_range(g_entries[i].label_start,
                                        g_entries[i].label_end),
                             first[key]);
    }
    NAOS_CHECK_UINT_EQ(find_query(miss_low), AMARK_NODE_NONE);
    NAOS_CHECK_UINT_EQ(find_query(miss_high), AMARK_NODE_NONE);
}

/*
 * Makes the k-th allocation of building the table fail, for k = 1, 2, ...
 * until the build succeeds, and checks that at least min_failures builds
 * failed; each failure must return AMARK_ERROR_NOMEM,
 * leave the table empty, and leak nothing once the scratch arena and the
 * document are released.
 */
static void check_allocation_failures(size_t count, size_t keys,
                                      size_t min_failures)
{
    static amark_node_id_t first[1000];
    size_t failures = 0;
    size_t k;

    add_many(count, keys, first);
    for (k = 1; k < 100; k++) {
        amark_error_t err;

        create_doc(true);
        amark_arena_init(&g_scratch, &g_allocator);
        g_state.fail_at = g_state.malloc_calls + k;
        err = amark_definitions_build(g_doc, &g_scratch, g_entries, g_count);
        g_state.fail_at = 0;
        if (err == AMARK_ERROR_NONE) {
            check_table(keys);
            check_every_entry_found();
            break;
        }
        failures++;
        NAOS_CHECK_INT_EQ(err, AMARK_ERROR_NOMEM);
        NAOS_CHECK_NULL(parse_data()->definitions);
        NAOS_CHECK_UINT_EQ(parse_data()->definition_count, 0);
        amark_arena_deinit(&g_scratch);
        amark_document_destroy(g_doc);
        g_doc = NULL;
        NAOS_CHECK_UINT_EQ(g_state.live, 0);
    }
    NAOS_CHECK_UINT_LT(k, 100);
    NAOS_CHECK_UINT_GE(failures, min_failures);
}

NAOS_TEST(table_build_reports_allocation_failure_small)
{
    check_allocation_failures(5, 3, 1);
}

NAOS_TEST(table_build_reports_each_allocation_failure_large)
{
    /* Each array is larger than an arena block: two scratch arrays and the
       table each need their own allocation. */
    check_allocation_failures(2000, 700, 3);
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

NAOS_SUITE(definition_table,
    NAOS_SET_SETUP(table),
    NAOS_SET_TEARDOWN(table),
    NAOS_ADD_TEST(table_finds_a_definition),
    NAOS_ADD_TEST(table_returns_the_first_of_duplicate_labels),
    NAOS_ADD_TEST(table_returns_the_first_when_all_labels_are_the_same),
    NAOS_ADD_TEST(table_matches_case_and_white_space_variants),
    NAOS_ADD_TEST(table_misses_labels_before_between_and_after),
    NAOS_ADD_TEST(table_empty_allocates_nothing_and_finds_nothing),
    NAOS_ADD_TEST(table_find_without_parse_data_finds_nothing),
    NAOS_ADD_TEST(table_find_allocates_nothing),
    NAOS_ADD_TEST(table_outlives_the_scratch_arena),
    NAOS_ADD_TEST(table_result_does_not_depend_on_the_order),
    NAOS_ADD_TEST(table_with_thousands_of_definitions),
    NAOS_ADD_TEST(table_build_reports_allocation_failure_small),
    NAOS_ADD_TEST(table_build_reports_each_allocation_failure_large)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(label_scan),
    NAOS_ADD_SUITE(label_compare),
    NAOS_ADD_SUITE(definition_table)
)
