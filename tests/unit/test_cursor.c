/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of the cursor (src/cursor.h), on inline contents the block
 * parser prepared: walking only the content of the lines (one line, several
 * lines, inside block quotes and list items, lazy continuation lines),
 * moving with next_line and seek, the position after a line ending (LF, CR,
 * CR LF), the character before and at a position (ASCII, 2 to 4 byte
 * characters, U+000A at the ends of a line's content, U+FFFD for truncated
 * sequences, lone continuation bytes, overlong forms and U+0000), the pad
 * of the line records inside the contents, agreement with amark_label_scan
 * on labels that cross lines, and that the cursor allocates nothing and
 * does not change the content.
 */

#include "arena.h"
#include "block.h"
#include "config_internal.h"
#include "cursor.h"
#include "definition.h"
#include "document.h"
#include "line.h"
#include "parse_data.h"
#include "test_alloc.h"

#include <amark/amark.h>

#include <naos/naos.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * Fixture. NAOS_REQUIRE_* leaves a test with longjmp, so the document and the
 * scratch arena are kept in static variables, and teardown releases them and
 * checks that no block is left.
 */
#define BUFFER_SIZE 1024

static amark_test_alloc_t g_state;
static amark_allocator_t g_allocator;
static amark_document_t *g_doc;
static amark_arena_t g_scratch;
static unsigned char g_buffer[BUFFER_SIZE];
static size_t g_length;

NAOS_SETUP(counting)
{
    amark_test_alloc_init(&g_state);
    g_allocator = amark_test_allocator(&g_state);
    amark_arena_init(&g_scratch, &g_allocator);
    g_doc = NULL;
    g_length = 0;
}

NAOS_TEARDOWN(counting)
{
    amark_arena_deinit(&g_scratch);
    amark_document_destroy(g_doc);
    g_doc = NULL;
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
    NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
}

/*
 * Copies length bytes to g_buffer, creates g_doc over them with parse data
 * and runs the block parser over the whole content (no limits, ill-formed
 * UTF-8 replaced), requiring success.
 */
static void parse_bytes(const char *bytes, size_t length)
{
    amark_config_t config;
    amark_resolved_config_t resolved;

    NAOS_REQUIRE_UINT_LE(length, BUFFER_SIZE);
    memcpy(g_buffer, bytes, length);
    g_length = length;
    amark_config_init(&config);
    config.allocator = g_allocator;
    config.utf8_mode = AMARK_UTF8_REPLACE;
    NAOS_REQUIRE_INT_EQ(amark_config_resolve(&config, g_length, &resolved),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_document_create(&resolved.allocator, g_buffer,
                                              g_length, resolved.start,
                                              resolved.end, NULL, &g_doc),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_parse_data_create(g_doc), AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_block_parse(g_doc, &resolved, &g_scratch, NULL),
                        AMARK_ERROR_NONE);
}

#define PARSE(s) parse_bytes((s), sizeof(s) - 1)

/* Returns the inline content at index; it must exist. */
static amark_inline_content_t content_at(size_t index)
{
    amark_inline_content_t content;

    NAOS_REQUIRE_TRUE(amark_inline_content_at(g_doc, index, &content));
    return content;
}

/* Places cursor at the start of the inline content at index. */
static void init_at(amark_cursor_t *cursor, size_t index)
{
    amark_inline_content_t content = content_at(index);

    amark_cursor_init(cursor, g_doc, content.start, content.end);
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

/*
 * Checks that the invariants of the cursor hold: line_start <= pos <=
 * line_end <= limit, and line is the line that holds pos.
 */
static void check_invariants(const amark_cursor_t *cursor)
{
    size_t start_line;
    size_t end_line;

    NAOS_CHECK_UINT_LE(cursor->line_start, cursor->pos);
    NAOS_CHECK_UINT_LE(cursor->pos, cursor->line_end);
    NAOS_CHECK_UINT_LE(cursor->line_end, cursor->limit);
    amark_document_range_lines(g_doc, cursor->pos, cursor->pos, &start_line,
                               &end_line);
    NAOS_CHECK_UINT_EQ(cursor->line, start_line);
}

/*
 * Walks the inline content at index from its start to its end, one byte at
 * a time with seek and from line to line with next_line, and checks that it
 * reads exactly want: the bytes of the lines' content, each line after the
 * first preceded by '\n'. Also checks the invariants at every step and that
 * the walk allocates nothing.
 */
static void check_walk_bytes(size_t index, const char *want,
                             size_t want_length)
{
    amark_cursor_t cursor;
    char got[BUFFER_SIZE];
    size_t got_length = 0;
    size_t calls = g_state.malloc_calls;

    init_at(&cursor, index);
    check_invariants(&cursor);
    while (!amark_cursor_at_end(&cursor)) {
        NAOS_REQUIRE_UINT_LT(got_length, BUFFER_SIZE);
        if (amark_cursor_at_line_end(&cursor)) {
            got[got_length++] = '\n';
            amark_cursor_next_line(&cursor);
        } else {
            got[got_length++] = (char)cursor.bytes[cursor.pos];
            amark_cursor_seek(&cursor, cursor.pos + 1);
        }
        check_invariants(&cursor);
    }
    NAOS_CHECK_TRUE(amark_cursor_at_line_end(&cursor));
    NAOS_CHECK_UINT_EQ(cursor.pos, content_at(index).end);
    NAOS_CHECK_UINT_EQ(got_length, want_length);
    NAOS_CHECK_TRUE(got_length == want_length &&
                    memcmp(got, want, want_length) == 0);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, calls);
}

#define CHECK_WALK(index, s) check_walk_bytes((index), (s), sizeof(s) - 1)

/* Checks that every line the inline content at index covers has pad 0. */
static void check_pad_zero(size_t index)
{
    amark_inline_content_t content = content_at(index);
    size_t first;
    size_t last;
    size_t line;

    amark_document_range_lines(g_doc, content.start, content.end, &first,
                               &last);
    for (line = first; line <= last; line++) {
        amark_line_record_t record;

        NAOS_REQUIRE_TRUE(amark_line_get(g_doc, line, &record));
        NAOS_CHECK_UINT_EQ(record.pad, 0);
    }
}

/* ---- walking the content of the lines ----------------------------------- */

NAOS_TEST(walk_one_line)
{
    amark_cursor_t cursor;

    PARSE("abc");
    CHECK_WALK(0, "abc");

    init_at(&cursor, 0);
    NAOS_CHECK_UINT_EQ(cursor.pos, 0);
    NAOS_CHECK_UINT_EQ(cursor.line, 1);
    NAOS_CHECK_UINT_EQ(cursor.line_start, 0);
    NAOS_CHECK_UINT_EQ(cursor.line_end, 3);
    NAOS_CHECK_UINT_EQ(cursor.limit, 3);
    NAOS_CHECK_TRUE(cursor.bytes == g_buffer);
    NAOS_CHECK_FALSE(amark_cursor_at_end(&cursor));
    NAOS_CHECK_FALSE(amark_cursor_at_line_end(&cursor));
}

NAOS_TEST(walk_stops_at_the_end_of_the_range_not_the_line)
{
    amark_cursor_t cursor;

    /* The content leaves out the trailing spaces and the line ending. */
    PARSE("abc   \n");
    CHECK_WALK(0, "abc");
    init_at(&cursor, 0);
    NAOS_CHECK_UINT_EQ(cursor.line_end, 3);
}

NAOS_TEST(walk_a_range_that_starts_inside_a_line)
{
    amark_cursor_t cursor;

    PARSE("## ab #\n");
    CHECK_WALK(0, "ab");
    init_at(&cursor, 0);
    NAOS_CHECK_UINT_EQ(cursor.pos, 3);
    NAOS_CHECK_UINT_EQ(cursor.line_start, 3);
    NAOS_CHECK_UINT_EQ(cursor.line_end, 5);
}

NAOS_TEST(walk_several_lines)
{
    amark_cursor_t cursor;

    PARSE("ab\n  cd\n   e");
    CHECK_WALK(0, "ab\ncd\ne");

    init_at(&cursor, 0);
    amark_cursor_seek(&cursor, 2);
    NAOS_CHECK_TRUE(amark_cursor_at_line_end(&cursor));
    NAOS_CHECK_FALSE(amark_cursor_at_end(&cursor));
    amark_cursor_next_line(&cursor);
    NAOS_CHECK_UINT_EQ(cursor.pos, 5);
    NAOS_CHECK_UINT_EQ(cursor.line, 2);
    NAOS_CHECK_UINT_EQ(cursor.line_start, 5);
    NAOS_CHECK_UINT_EQ(cursor.line_end, 7);
    amark_cursor_seek(&cursor, 7);
    amark_cursor_next_line(&cursor);
    NAOS_CHECK_UINT_EQ(cursor.pos, 11);
    NAOS_CHECK_UINT_EQ(cursor.line, 3);
    NAOS_CHECK_UINT_EQ(cursor.line_end, 12);
    NAOS_CHECK_UINT_EQ(cursor.limit, 12);
}

NAOS_TEST(walk_inside_a_block_quote)
{
    PARSE("> ab\n>  cd\n> > e\n");
    CHECK_WALK(0, "ab\ncd");
    CHECK_WALK(1, "e");
}

NAOS_TEST(walk_inside_list_items)
{
    PARSE("- ab\n  cd\n\n1. e\n   f\n   - g\n     h\n");
    CHECK_WALK(0, "ab\ncd");
    CHECK_WALK(1, "e\nf");
    CHECK_WALK(2, "g\nh");
}

NAOS_TEST(walk_inside_a_list_item_in_a_block_quote)
{
    PARSE("> - ab\n>   cd\n>   > e\n>   > f\n");
    CHECK_WALK(0, "ab\ncd");
    CHECK_WALK(1, "e\nf");
}

NAOS_TEST(walk_lazy_continuation_lines)
{
    PARSE("> ab\ncd\n> e\n  f\n");
    CHECK_WALK(0, "ab\ncd\ne\nf");
}

NAOS_TEST(walk_lazy_continuation_lines_of_a_list_item)
{
    PARSE("- > ab\ncd\n  > e\n");
    CHECK_WALK(0, "ab\ncd\ne");
}

NAOS_TEST(walk_skips_tabs_in_prefixes)
{
    PARSE(">\tab\n>\t cd\n-\tef\n\tgh\n");
    CHECK_WALK(0, "ab\ncd");
    CHECK_WALK(1, "ef\ngh");
}

NAOS_TEST(seek_crosses_lines)
{
    amark_cursor_t cursor;
    size_t d;

    PARSE("> ab\n> cd\n> ef");
    d = find_byte(0, 'd');
    init_at(&cursor, 0);
    amark_cursor_seek(&cursor, d);
    NAOS_CHECK_UINT_EQ(cursor.pos, d);
    NAOS_CHECK_UINT_EQ(cursor.line, 2);
    NAOS_CHECK_UINT_EQ(cursor.line_start, find_byte(0, 'c'));
    NAOS_CHECK_UINT_EQ(cursor.line_end, d + 1);

    /* To the end of a line's content, and to the start of a later line. */
    amark_cursor_seek(&cursor, d + 1);
    NAOS_CHECK_TRUE(amark_cursor_at_line_end(&cursor));
    NAOS_CHECK_UINT_EQ(cursor.line, 2);
    amark_cursor_seek(&cursor, find_byte(0, 'e'));
    NAOS_CHECK_UINT_EQ(cursor.line, 3);
    NAOS_CHECK_UINT_EQ(cursor.line_start, cursor.pos);

    /* To the end of the range from the first line. */
    init_at(&cursor, 0);
    amark_cursor_seek(&cursor, g_length);
    NAOS_CHECK_TRUE(amark_cursor_at_end(&cursor));
    NAOS_CHECK_UINT_EQ(cursor.line, 3);
    check_invariants(&cursor);

    /* To the same position. */
    init_at(&cursor, 0);
    amark_cursor_seek(&cursor, cursor.pos);
    NAOS_CHECK_UINT_EQ(cursor.pos, 2);
    NAOS_CHECK_UINT_EQ(cursor.line, 1);
}

NAOS_TEST(a_copy_restores_the_position)
{
    amark_cursor_t cursor;
    amark_cursor_t saved;

    PARSE("> ab\n> cd");
    init_at(&cursor, 0);
    amark_cursor_seek(&cursor, 3);
    saved = cursor;
    amark_cursor_seek(&cursor, g_length);
    NAOS_CHECK_UINT_EQ(cursor.line, 2);
    cursor = saved;
    NAOS_CHECK_UINT_EQ(cursor.pos, 3);
    NAOS_CHECK_UINT_EQ(cursor.line, 1);
    NAOS_CHECK_UINT_EQ(cursor.line_end, 4);
}

NAOS_TEST(lines_inside_contents_have_pad_zero)
{
    size_t count;
    size_t i;

    PARSE(">\tab\n>\t cd\n-\tef\n\tgh\n\n -\tij\n  \tkl\n\n"
          "> -\tmn\n>\top\n");
    count = amark_inline_content_count(g_doc);
    NAOS_CHECK_UINT_EQ(count, 4);
    for (i = 0; i < count; i++) {
        check_pad_zero(i);
    }
}

/* ---- line endings ------------------------------------------------------- */

/*
 * Parses s, a paragraph of two lines "ab" and "cd" joined by a line ending of
 * ending_length bytes, and checks line_break_end and next_line.
 */
static void check_line_ending(const char *s, size_t length,
                              size_t ending_length)
{
    amark_cursor_t cursor;

    parse_bytes(s, length);
    init_at(&cursor, 0);
    amark_cursor_seek(&cursor, 2);
    NAOS_CHECK_TRUE(amark_cursor_at_line_end(&cursor));
    NAOS_CHECK_UINT_EQ(amark_cursor_line_break_end(&cursor),
                       2 + ending_length);
    amark_cursor_next_line(&cursor);
    NAOS_CHECK_UINT_EQ(cursor.pos, 2 + ending_length);
    NAOS_CHECK_UINT_EQ(cursor.line, 2);
    NAOS_CHECK_UINT_EQ(cursor.line_end, 4 + ending_length);
    CHECK_WALK(0, "ab\ncd");
}

#define CHECK_LINE_ENDING(s, n) check_line_ending((s), sizeof(s) - 1, (n))

NAOS_TEST(line_break_end_after_lf_cr_and_crlf)
{
    CHECK_LINE_ENDING("ab\ncd", 1);
    amark_document_destroy(g_doc);
    g_doc = NULL;
    CHECK_LINE_ENDING("ab\rcd", 1);
    amark_document_destroy(g_doc);
    g_doc = NULL;
    CHECK_LINE_ENDING("ab\r\ncd", 2);
}

NAOS_TEST(line_break_end_before_a_prefix)
{
    amark_cursor_t cursor;

    PARSE("> ab  \r\n>   cd\r\n");
    init_at(&cursor, 0);
    /* The spaces before a line ending inside the content are content. */
    amark_cursor_seek(&cursor, 4);
    NAOS_CHECK_FALSE(amark_cursor_at_line_end(&cursor));
    amark_cursor_seek(&cursor, 6);
    NAOS_CHECK_TRUE(amark_cursor_at_line_end(&cursor));
    /* After the line ending, not after the prefix or the white space. */
    NAOS_CHECK_UINT_EQ(amark_cursor_line_break_end(&cursor), 8);
    amark_cursor_next_line(&cursor);
    NAOS_CHECK_UINT_EQ(cursor.pos, find_byte(0, 'c'));
}

NAOS_TEST(line_break_end_on_the_last_line)
{
    amark_cursor_t cursor;

    PARSE("ab\ncd\r\n");
    init_at(&cursor, 0);
    amark_cursor_seek(&cursor, 5);
    NAOS_CHECK_TRUE(amark_cursor_at_end(&cursor));
    NAOS_CHECK_UINT_EQ(amark_cursor_line_break_end(&cursor), 7);
}

/* ---- the characters before and at a position ---------------------------- */

/*
 * Checks the character before and at pos in the inline content at index,
 * from a cursor moved to pos with seek.
 */
static void check_chars(size_t index, size_t pos, uint32_t before,
                        uint32_t at)
{
    amark_cursor_t cursor;

    init_at(&cursor, index);
    amark_cursor_seek(&cursor, pos);
    NAOS_CHECK_UINT_EQ(amark_cursor_char_before(&cursor, pos), before);
    NAOS_CHECK_UINT_EQ(amark_cursor_char_at(&cursor, pos), at);
}

NAOS_TEST(chars_ascii_and_the_ends_of_the_range)
{
    PARSE("ab");
    check_chars(0, 0, 0x0A, 'a');
    check_chars(0, 1, 'a', 'b');
    check_chars(0, 2, 'b', 0x0A);
}

NAOS_TEST(chars_at_the_ends_of_a_range_inside_a_line)
{
    PARSE("# *a* #\n");
    /* The content is "*a*"; the space before and after it is outside. */
    check_chars(0, 2, 0x0A, '*');
    check_chars(0, 5, '*', 0x0A);
}

NAOS_TEST(chars_at_the_ends_of_line_contents)
{
    PARSE("> ab\n>   cd");
    check_chars(0, 4, 'b', 0x0A);
    check_chars(0, find_byte(0, 'c'), 0x0A, 'c');
}

NAOS_TEST(chars_of_two_to_four_bytes)
{
    /* U+00E9, U+20AC, U+1F600, then 'x'. */
    PARSE("\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80x");
    check_chars(0, 0, 0x0A, 0xE9);
    check_chars(0, 2, 0xE9, 0x20AC);
    check_chars(0, 5, 0x20AC, 0x1F600);
    check_chars(0, 9, 0x1F600, 'x');
    check_chars(0, 10, 'x', 0x0A);
}

NAOS_TEST(chars_of_a_truncated_sequence)
{
    /* E2 82 is a maximal subpart: one U+FFFD. */
    PARSE("a\xE2\x82" "b\xF0\x9F\x98");
    check_chars(0, 1, 'a', 0xFFFD);
    check_chars(0, 3, 0xFFFD, 'b');
    check_chars(0, 4, 'b', 0xFFFD);
    /* At the end of the range, cut off by the end of the line's content. */
    check_chars(0, 7, 0xFFFD, 0x0A);
}

NAOS_TEST(chars_of_lone_continuation_bytes)
{
    PARSE("a\x80" "b\x80\x80\x80\x80" "c");
    check_chars(0, 1, 'a', 0xFFFD);
    check_chars(0, 2, 0xFFFD, 'b');
    check_chars(0, 3, 'b', 0xFFFD);
    check_chars(0, 4, 0xFFFD, 0xFFFD);
    /* Four continuation bytes: steps back over three only. */
    check_chars(0, 7, 0xFFFD, 'c');
}

NAOS_TEST(chars_after_a_character_followed_by_a_continuation_byte)
{
    /* U+1F600 then a stray 80: the character before the 'x' is U+FFFD. */
    PARSE("\xF0\x9F\x98\x80\x80x");
    check_chars(0, 4, 0x1F600, 0xFFFD);
    check_chars(0, 5, 0xFFFD, 'x');
}

NAOS_TEST(chars_of_overlong_forms)
{
    /* C0 AF and E0 80 AF are overlong '/'; F0 80 80 AF too. */
    PARSE("a\xC0\xAF" "b\xE0\x80\xAF" "c\xF0\x80\x80\xAF" "d");
    check_chars(0, 1, 'a', 0xFFFD);
    check_chars(0, 3, 0xFFFD, 'b');
    check_chars(0, 4, 'b', 0xFFFD);
    check_chars(0, 7, 0xFFFD, 'c');
    check_chars(0, 8, 'c', 0xFFFD);
    check_chars(0, 12, 0xFFFD, 'd');
}

NAOS_TEST(chars_of_u0000)
{
    static const char s[] = "a\0b\0";

    parse_bytes(s, sizeof(s) - 1);
    check_chars(0, 1, 'a', 0xFFFD);
    check_chars(0, 2, 0xFFFD, 'b');
    check_chars(0, 3, 'b', 0xFFFD);
    check_chars(0, 4, 0xFFFD, 0x0A);
}

NAOS_TEST(chars_allocate_nothing_and_change_nothing)
{
    amark_cursor_t cursor;
    unsigned char copy[BUFFER_SIZE];
    size_t calls;
    size_t pos;

    PARSE("> a\xC3\xA9\x80\n> \xF0\x9F\x98\x80*\n");
    memcpy(copy, g_buffer, g_length);
    calls = g_state.malloc_calls;
    init_at(&cursor, 0);
    while (!amark_cursor_at_end(&cursor)) {
        (void)amark_cursor_char_before(&cursor, cursor.pos);
        (void)amark_cursor_char_at(&cursor, cursor.pos);
        (void)amark_cursor_line_break_end(&cursor);
        if (amark_cursor_at_line_end(&cursor)) {
            amark_cursor_next_line(&cursor);
        } else {
            pos = cursor.pos + 1;
            amark_cursor_seek(&cursor, pos);
        }
    }
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, calls);
    NAOS_CHECK_TRUE(memcmp(copy, g_buffer, g_length) == 0);
}

/* ---- agreement with amark_label_scan ------------------------------------ */

/*
 * In the inline content at index, scans the label whose '[' is at open with
 * amark_label_scan, limited to the content, and walks the cursor from
 * open + 1 to the first ']', checking that it stops where the label ends and
 * that it reads want, the inside of the label as the cursor reads it.
 */
static void check_label_walk(size_t index, size_t open, const char *want,
                             size_t want_length)
{
    amark_inline_content_t content = content_at(index);
    amark_cursor_t cursor;
    size_t label_start = 0;
    size_t label_end = 0;
    char got[BUFFER_SIZE];
    size_t got_length = 0;

    NAOS_REQUIRE_TRUE(amark_label_scan(g_doc, open, content.end,
                                       &label_start, &label_end));
    NAOS_CHECK_UINT_EQ(label_start, open + 1);

    amark_cursor_init(&cursor, g_doc, content.start, content.end);
    amark_cursor_seek(&cursor, open + 1);
    for (;;) {
        NAOS_REQUIRE_FALSE(amark_cursor_at_end(&cursor));
        NAOS_REQUIRE_UINT_LT(got_length, BUFFER_SIZE);
        if (amark_cursor_at_line_end(&cursor)) {
            got[got_length++] = '\n';
            amark_cursor_next_line(&cursor);
        } else if (cursor.bytes[cursor.pos] == ']') {
            break;
        } else {
            got[got_length++] = (char)cursor.bytes[cursor.pos];
            amark_cursor_seek(&cursor, cursor.pos + 1);
        }
    }
    NAOS_CHECK_UINT_EQ(cursor.pos, label_end);
    NAOS_CHECK_UINT_EQ(got_length, want_length);
    NAOS_CHECK_TRUE(got_length == want_length &&
                    memcmp(got, want, want_length) == 0);
}

#define CHECK_LABEL_WALK(index, open, s) \
    check_label_walk((index), (open), (s), sizeof(s) - 1)

NAOS_TEST(label_across_lines_in_a_paragraph)
{
    PARSE("x [a\n   b\nc] y\n");
    CHECK_LABEL_WALK(0, 2, "a\nb\nc");
}

NAOS_TEST(label_across_lines_in_containers)
{
    PARSE("> - x [a\n>   b  \n>    c] y\n");
    CHECK_LABEL_WALK(0, find_byte(0, '['), "a\nb  \nc");
}

NAOS_TEST(label_across_a_lazy_continuation_line)
{
    PARSE("> x [a\n  b\n> c] y\n");
    CHECK_LABEL_WALK(0, find_byte(0, '['), "a\nb\nc");
}

NAOS_TEST(label_across_crlf_and_cr)
{
    PARSE("> [a\r\n>  b\r>   c]\r\n");
    CHECK_LABEL_WALK(0, find_byte(0, '['), "a\nb\nc");
}

NAOS_SUITE(walk,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(walk_one_line),
    NAOS_ADD_TEST(walk_stops_at_the_end_of_the_range_not_the_line),
    NAOS_ADD_TEST(walk_a_range_that_starts_inside_a_line),
    NAOS_ADD_TEST(walk_several_lines),
    NAOS_ADD_TEST(walk_inside_a_block_quote),
    NAOS_ADD_TEST(walk_inside_list_items),
    NAOS_ADD_TEST(walk_inside_a_list_item_in_a_block_quote),
    NAOS_ADD_TEST(walk_lazy_continuation_lines),
    NAOS_ADD_TEST(walk_lazy_continuation_lines_of_a_list_item),
    NAOS_ADD_TEST(walk_skips_tabs_in_prefixes),
    NAOS_ADD_TEST(seek_crosses_lines),
    NAOS_ADD_TEST(a_copy_restores_the_position),
    NAOS_ADD_TEST(lines_inside_contents_have_pad_zero)
)

NAOS_SUITE(line_ending,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(line_break_end_after_lf_cr_and_crlf),
    NAOS_ADD_TEST(line_break_end_before_a_prefix),
    NAOS_ADD_TEST(line_break_end_on_the_last_line)
)

NAOS_SUITE(chars,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(chars_ascii_and_the_ends_of_the_range),
    NAOS_ADD_TEST(chars_at_the_ends_of_a_range_inside_a_line),
    NAOS_ADD_TEST(chars_at_the_ends_of_line_contents),
    NAOS_ADD_TEST(chars_of_two_to_four_bytes),
    NAOS_ADD_TEST(chars_of_a_truncated_sequence),
    NAOS_ADD_TEST(chars_of_lone_continuation_bytes),
    NAOS_ADD_TEST(chars_after_a_character_followed_by_a_continuation_byte),
    NAOS_ADD_TEST(chars_of_overlong_forms),
    NAOS_ADD_TEST(chars_of_u0000),
    NAOS_ADD_TEST(chars_allocate_nothing_and_change_nothing)
)

NAOS_SUITE(label,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(label_across_lines_in_a_paragraph),
    NAOS_ADD_TEST(label_across_lines_in_containers),
    NAOS_ADD_TEST(label_across_a_lazy_continuation_line),
    NAOS_ADD_TEST(label_across_crlf_and_cr)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(walk),
    NAOS_ADD_SUITE(line_ending),
    NAOS_ADD_SUITE(chars),
    NAOS_ADD_SUITE(label)
)
