/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of the inline scan (src/inline_scan.h), on inline contents the
 * block parser prepared.
 *
 * Code spans: only a string of the same length closes, a string next to
 * another backtick is longer, spans cross lines, bytes outside the content
 * of the lines (container prefixes, leading white space, other blocks) are
 * never read, a length known to have no closing string is not searched
 * again, the memo survives from one content to the next, strings longer
 * than 1,000 backticks and contents with more than 1,001 strings, and a
 * failed growth of the memo.
 *
 * Character references: entity names, 7 and 8 decimal digits, 6 and 7
 * hexadecimal digits, a missing ';', names not in the table, the values 0,
 * surrogates and over U+10FFFF, and references that would cross a line.
 *
 * Whenever a scan does not match, the cursor must not move.
 */

#include "arena.h"
#include "array.h"
#include "block.h"
#include "config_internal.h"
#include "cursor.h"
#include "document.h"
#include "inline_scan.h"
#include "parse_data.h"
#include "test_alloc.h"

#include <amark/amark.h>

#include <naos/naos.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * Fixture. NAOS_REQUIRE_* leaves a test with longjmp, so the document, the
 * scratch arena and the memo are kept in static variables, and teardown
 * releases them and checks that no block is left.
 */
#define BUFFER_SIZE 65536

/* What span_end returns when no code span is found. */
#define NO_SPAN ((size_t)-1)

static amark_test_alloc_t g_state;
static amark_allocator_t g_allocator;
static amark_document_t *g_doc;
static amark_arena_t g_scratch;
static amark_arena_t g_memo_arena;
static amark_inline_scan_memo_t g_memo;
static unsigned char g_buffer[BUFFER_SIZE];
static size_t g_length;

NAOS_SETUP(counting)
{
    amark_test_alloc_init(&g_state);
    g_allocator = amark_test_allocator(&g_state);
    amark_arena_init(&g_scratch, &g_allocator);
    amark_arena_init(&g_memo_arena, &g_allocator);
    amark_inline_scan_memo_init(&g_memo, &g_memo_arena);
    g_doc = NULL;
    g_length = 0;
}

NAOS_TEARDOWN(counting)
{
    amark_arena_deinit(&g_memo_arena);
    amark_arena_deinit(&g_scratch);
    amark_document_destroy(g_doc);
    g_doc = NULL;
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
    NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
}

/*
 * Creates g_doc over the first length bytes of g_buffer with parse data and
 * runs the block parser over the whole content (no limits, ill-formed UTF-8
 * replaced), requiring success. Then starts the memo on the first content.
 */
static void parse_buffer(size_t length)
{
    amark_config_t config;
    amark_resolved_config_t resolved;

    NAOS_REQUIRE_UINT_LE(length, BUFFER_SIZE);
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
    NAOS_REQUIRE_INT_EQ(amark_block_parse(g_doc, &resolved, &g_scratch),
                        AMARK_ERROR_NONE);
    amark_inline_scan_memo_begin(&g_memo);
}

/* Copies length bytes to g_buffer and parses them with parse_buffer. */
static void parse_bytes(const char *bytes, size_t length)
{
    NAOS_REQUIRE_UINT_LE(length, BUFFER_SIZE);
    memcpy(g_buffer, bytes, length);
    parse_buffer(length);
}

#define PARSE(s) parse_bytes((s), sizeof(s) - 1)

/* Appends length copies of c at g_buffer[*length], advancing *length. */
static void fill(size_t *length, char c, size_t count)
{
    NAOS_REQUIRE_UINT_LE(count, BUFFER_SIZE - *length);
    memset(g_buffer + *length, c, count);
    *length += count;
}

/* Returns the inline content at index; it must exist. */
static amark_inline_content_t content_at(size_t index)
{
    amark_inline_content_t content;

    NAOS_REQUIRE_TRUE(amark_inline_content_at(g_doc, index, &content));
    return content;
}

/* Places cursor at pos in the inline content at index. */
static void cursor_at(amark_cursor_t *cursor, size_t index, size_t pos)
{
    amark_inline_content_t content = content_at(index);

    NAOS_REQUIRE_UINT_LE(content.start, pos);
    NAOS_REQUIRE_UINT_LE(pos, content.end);
    amark_cursor_init(cursor, g_doc, content.start, content.end);
    amark_cursor_seek(cursor, pos);
    NAOS_REQUIRE_UINT_EQ(cursor->pos, pos);
}

/* Checks that the cursor is exactly where saved was. */
static void check_unmoved(const amark_cursor_t *cursor,
                          const amark_cursor_t *saved)
{
    NAOS_CHECK_UINT_EQ(cursor->pos, saved->pos);
    NAOS_CHECK_UINT_EQ(cursor->line, saved->line);
    NAOS_CHECK_UINT_EQ(cursor->line_start, saved->line_start);
    NAOS_CHECK_UINT_EQ(cursor->line_end, saved->line_end);
    NAOS_CHECK_UINT_EQ(cursor->limit, saved->limit);
}

/* The number of backticks at pos and after it, in g_buffer. */
static size_t backtick_length(size_t pos)
{
    size_t n = 0;

    while (pos + n < g_length && g_buffer[pos + n] == '`') {
        n++;
    }
    return n;
}

/*
 * Scans a code span whose opening string starts at pos of the content at
 * index, with g_memo and the length of the string at pos. Returns the
 * position right after the closing string, or NO_SPAN; then checks that
 * the cursor did not move. Requires that the scan does not fail.
 */
static size_t span_end(size_t index, size_t pos)
{
    amark_cursor_t cursor;
    amark_cursor_t saved;
    bool found = true;

    cursor_at(&cursor, index, pos);
    saved = cursor;
    NAOS_REQUIRE_INT_EQ(amark_inline_scan_code_span(&cursor, &g_memo,
                                                    backtick_length(pos),
                                                    &found),
                        AMARK_ERROR_NONE);
    if (!found) {
        check_unmoved(&cursor, &saved);
        return NO_SPAN;
    }
    return cursor.pos;
}

/*
 * Walks the content at index as the inline parser does with backticks: at
 * each backtick string it tries a code span and, on a match, goes on right
 * after it; otherwise it goes on after the opening string. Writes the end
 * of each span to ends (at most max_ends) and returns the number of spans.
 * If fresh is true, starts the memo again before each scan, so that no scan
 * can rely on an earlier one.
 */
static size_t walk_spans(size_t index, bool fresh, size_t *ends,
                         size_t max_ends)
{
    amark_inline_content_t content = content_at(index);
    amark_cursor_t cursor;
    size_t count = 0;

    amark_cursor_init(&cursor, g_doc, content.start, content.end);
    while (!amark_cursor_at_end(&cursor)) {
        if (amark_cursor_at_line_end(&cursor)) {
            amark_cursor_next_line(&cursor);
        } else if (cursor.bytes[cursor.pos] == '`') {
            size_t n = 0;
            bool found = false;

            while (cursor.pos + n < cursor.line_end &&
                   cursor.bytes[cursor.pos + n] == '`') {
                n++;
            }
            if (fresh) {
                amark_inline_scan_memo_begin(&g_memo);
            }
            NAOS_REQUIRE_INT_EQ(amark_inline_scan_code_span(&cursor, &g_memo,
                                                            n, &found),
                                AMARK_ERROR_NONE);
            if (found) {
                NAOS_REQUIRE_UINT_LT(count, max_ends);
                ends[count++] = cursor.pos;
            } else {
                amark_cursor_seek(&cursor, cursor.pos + n);
            }
        } else {
            amark_cursor_seek(&cursor, cursor.pos + 1);
        }
    }
    return count;
}

/*
 * Checks that walk_spans finds the same spans with the memo kept for the
 * whole content as with a memo started again before each scan, and returns
 * their number.
 */
#define MAX_ENDS 2048
static size_t g_kept[MAX_ENDS];
static size_t g_fresh[MAX_ENDS];

static size_t check_memo_agrees(size_t index)
{
    size_t kept;
    size_t fresh;
    size_t i;

    amark_inline_scan_memo_begin(&g_memo);
    kept = walk_spans(index, false, g_kept, MAX_ENDS);
    fresh = walk_spans(index, true, g_fresh, MAX_ENDS);
    NAOS_CHECK_UINT_EQ(kept, fresh);
    for (i = 0; i < kept && i < fresh; i++) {
        NAOS_CHECK_UINT_EQ(g_kept[i], g_fresh[i]);
    }
    return kept;
}

/* Returns the entry for length in the memo's backticks array. */
static size_t memo_at(size_t length)
{
    NAOS_REQUIRE_UINT_LT(length, amark_array_len(&g_memo.backticks));
    return *(const size_t *)amark_array_at(&g_memo.backticks, length);
}

/* ---- code spans: which string closes ------------------------------------ */

NAOS_TEST(code_span_closes_with_the_same_length)
{
    PARSE("`a`");
    NAOS_CHECK_UINT_EQ(span_end(0, 0), 3);
    g_doc = (amark_document_destroy(g_doc), NULL);

    PARSE("``a`b``c");
    NAOS_CHECK_UINT_EQ(span_end(0, 0), 7);
    g_doc = (amark_document_destroy(g_doc), NULL);

    /* The strings of 2 and 3 are passed over; the next one of 1 closes. */
    PARSE("x `a``b```c` d`");
    NAOS_CHECK_UINT_EQ(span_end(0, 2), 12);
}

NAOS_TEST(code_span_strings_of_other_lengths_do_not_close)
{
    PARSE("``a```b`c");
    NAOS_CHECK_UINT_EQ(span_end(0, 0), NO_SPAN);
    g_doc = (amark_document_destroy(g_doc), NULL);

    /* "``" is one string of 2, not two strings of 1. */
    PARSE("`a``");
    NAOS_CHECK_UINT_EQ(span_end(0, 0), NO_SPAN);
    g_doc = (amark_document_destroy(g_doc), NULL);

    PARSE("```a``");
    NAOS_CHECK_UINT_EQ(span_end(0, 0), NO_SPAN);
}

NAOS_TEST(code_span_needs_something_after_the_opening_string)
{
    PARSE("a ``");
    NAOS_CHECK_UINT_EQ(span_end(0, 2), NO_SPAN);
    g_doc = (amark_document_destroy(g_doc), NULL);

    /* Right after the opening string, the closing one may follow. */
    PARSE("`a` ``");
    NAOS_CHECK_UINT_EQ(span_end(0, 4), NO_SPAN);
}

NAOS_TEST(code_span_ends_at_the_first_closing_string)
{
    PARSE("`a` b `c`");
    NAOS_CHECK_UINT_EQ(span_end(0, 0), 3);
    NAOS_CHECK_UINT_EQ(span_end(0, 6), 9);
}

NAOS_TEST(code_span_without_closing_string_does_not_move_the_cursor)
{
    amark_cursor_t cursor;
    amark_cursor_t saved;
    bool found = true;

    PARSE("a\n`b\nc");
    cursor_at(&cursor, 0, 2);
    saved = cursor;
    NAOS_CHECK_INT_EQ(amark_inline_scan_code_span(&cursor, &g_memo, 1,
                                                  &found),
                      AMARK_ERROR_NONE);
    NAOS_CHECK_FALSE(found);
    check_unmoved(&cursor, &saved);
}

/* ---- code spans across lines -------------------------------------------- */

NAOS_TEST(code_span_across_lines)
{
    amark_cursor_t cursor;
    bool found = false;

    PARSE("a `b\nc\nd` e");
    cursor_at(&cursor, 0, 2);
    NAOS_CHECK_INT_EQ(amark_inline_scan_code_span(&cursor, &g_memo, 1,
                                                  &found),
                      AMARK_ERROR_NONE);
    NAOS_CHECK_TRUE(found);
    NAOS_CHECK_UINT_EQ(cursor.pos, 9);
    NAOS_CHECK_UINT_EQ(cursor.line, 3);
    NAOS_CHECK_UINT_EQ(cursor.line_start, 7);
    NAOS_CHECK_UINT_EQ(cursor.line_end, 11);
}

NAOS_TEST(code_span_across_crlf_and_cr)
{
    PARSE("`a\r\nb\rc`");
    NAOS_CHECK_UINT_EQ(span_end(0, 0), 8);
}

NAOS_TEST(code_span_across_lines_in_containers)
{
    PARSE("> `a\n> b`\n");
    NAOS_CHECK_UINT_EQ(span_end(0, 2), 9);
    g_doc = (amark_document_destroy(g_doc), NULL);

    PARSE("- ``a\n  b``\n");
    NAOS_CHECK_UINT_EQ(span_end(0, 2), 11);
    g_doc = (amark_document_destroy(g_doc), NULL);

    /* A lazy continuation line. */
    PARSE("> `a\nb`\n");
    NAOS_CHECK_UINT_EQ(span_end(0, 2), 7);
}

NAOS_TEST(code_span_does_not_count_prefixes)
{
    /*
     * After the block parse, the prefix "> " of the second line is turned
     * into "``". The line records still say where the content of the line
     * starts, so the scan must not see a closing string there, only the one
     * after "b".
     */
    PARSE("> ``a\n> b``");
    g_buffer[6] = '`';
    g_buffer[7] = '`';
    NAOS_CHECK_UINT_EQ(span_end(0, 2), 11);
    g_doc = (amark_document_destroy(g_doc), NULL);

    /* The same with the leading white space of a continuation line. */
    PARSE("``a\n   b``");
    g_buffer[4] = '`';
    g_buffer[5] = '`';
    NAOS_CHECK_UINT_EQ(span_end(0, 0), 10);
    g_doc = (amark_document_destroy(g_doc), NULL);

    /* A list item's indentation. */
    PARSE("- `a\n  b`");
    g_buffer[5] = '`';
    NAOS_CHECK_UINT_EQ(span_end(0, 2), 9);
}

NAOS_TEST(code_span_does_not_leave_the_content)
{
    /* The closing strings are in other blocks. */
    PARSE("`a\n\n`b\n");
    NAOS_CHECK_UINT_EQ(span_end(0, 0), NO_SPAN);
    g_doc = (amark_document_destroy(g_doc), NULL);

    PARSE("# `a\n`b\n");
    NAOS_CHECK_UINT_EQ(span_end(0, 2), NO_SPAN);
    g_doc = (amark_document_destroy(g_doc), NULL);

    /* A lazy continuation line. */
    PARSE("> `a\n`\n");
    NAOS_CHECK_UINT_EQ(span_end(0, 2), 6);
    g_doc = (amark_document_destroy(g_doc), NULL);

    /* The closing sequence of an ATX heading is not content. */
    PARSE("# `a #`\n");
    NAOS_CHECK_UINT_EQ(span_end(0, 2), 7);
    g_doc = (amark_document_destroy(g_doc), NULL);

    PARSE("# `a ` #\n");
    NAOS_CHECK_UINT_EQ(span_end(0, 2), 6);
    g_doc = (amark_document_destroy(g_doc), NULL);

    PARSE("## `a #\n`\n");
    NAOS_CHECK_UINT_EQ(span_end(0, 3), NO_SPAN);
}

/* ---- the memo ----------------------------------------------------------- */

NAOS_TEST(memo_answers_without_searching_once_complete)
{
    /* Positions: "``" 0, 'a' 2, '`' 3, 'b' 4, ' ' 5, 'x' 6. */
    PARSE("``a`b x");
    NAOS_CHECK_UINT_EQ(span_end(0, 0), NO_SPAN);
    NAOS_CHECK_TRUE(g_memo.backticks_complete);
    NAOS_CHECK_UINT_EQ(memo_at(1), 3);

    /*
     * 'x' becomes a backtick behind the memo's back. The memo knows there
     * is no string of 1 after position 3, so the scan does not search.
     */
    g_buffer[6] = '`';
    NAOS_CHECK_UINT_EQ(span_end(0, 3), NO_SPAN);

    /* Started again, the memo knows nothing and the search finds it. */
    amark_inline_scan_memo_begin(&g_memo);
    NAOS_CHECK_FALSE(g_memo.backticks_complete);
    NAOS_CHECK_UINT_EQ(span_end(0, 3), 7);
}

NAOS_TEST(memo_does_not_hide_a_later_string)
{
    /*
     * "` `` ``` `` ``` ```": the scan from 0 (length 1) goes to the end and
     * sees the last string of 3 at 16. The scan from 2 (length 2) passes
     * the string of 3 at 5 and stops at 9. The string of 3 at 12 must still
     * find the one at 16.
     */
    PARSE("` `` ``` `` ``` ```");
    NAOS_CHECK_UINT_EQ(span_end(0, 0), NO_SPAN);
    NAOS_CHECK_TRUE(g_memo.backticks_complete);
    NAOS_CHECK_UINT_EQ(span_end(0, 2), 11);
    NAOS_CHECK_UINT_EQ(memo_at(3), 16);
    NAOS_CHECK_UINT_EQ(span_end(0, 12), 19);
    NAOS_CHECK_UINT_EQ(check_memo_agrees(0), 2);
}

NAOS_TEST(memo_agrees_with_a_fresh_search)
{
    static const char *const inputs[] = {
        "` `` ``` `` ``` ```",
        "``` ` `` ` ``` `` `",
        "`a``b```c``d`e```",
        "x`` `a` ``` `` ` ```` ``` `",
        "`` ` ``` ` `` ``` `` ` ```",
        "`a\nb``c\n``d`\n```e`",
    };
    size_t i;

    for (i = 0; i < sizeof(inputs) / sizeof(inputs[0]); i++) {
        parse_bytes(inputs[i], strlen(inputs[i]));
        (void)check_memo_agrees(0);
        amark_document_destroy(g_doc);
        g_doc = NULL;
    }
}

NAOS_TEST(memo_is_kept_across_contents)
{
    size_t len;

    /* Contents: "``a`" at 0, "`b` ``c" at 6, "``d`` `e" at 15. */
    PARSE("``a`\n\n`b` ``c\n\n``d`` `e\n");
    NAOS_CHECK_UINT_EQ(span_end(0, 0), NO_SPAN);
    NAOS_CHECK_TRUE(g_memo.backticks_complete);
    NAOS_CHECK_UINT_EQ(memo_at(1), 3);
    len = amark_array_len(&g_memo.backticks);

    /* begin clears the mark but keeps the array and its entries. */
    amark_inline_scan_memo_begin(&g_memo);
    NAOS_CHECK_FALSE(g_memo.backticks_complete);
    NAOS_CHECK_UINT_EQ(amark_array_len(&g_memo.backticks), len);
    NAOS_CHECK_UINT_EQ(memo_at(1), 3);
    NAOS_CHECK_UINT_EQ(span_end(1, 6), 9);
    NAOS_CHECK_UINT_EQ(memo_at(1), 8);
    NAOS_CHECK_UINT_EQ(span_end(1, 10), NO_SPAN);
    NAOS_CHECK_TRUE(g_memo.backticks_complete);

    /*
     * The entries of the previous contents (8 for 1) are below every
     * position here, and the mark is cleared, so this content is searched.
     */
    amark_inline_scan_memo_begin(&g_memo);
    NAOS_CHECK_UINT_EQ(span_end(2, 15), 20);
    NAOS_CHECK_UINT_EQ(span_end(2, 21), NO_SPAN);
    NAOS_CHECK_TRUE(g_memo.backticks_complete);
    NAOS_CHECK_UINT_EQ(memo_at(1), 8);
}

NAOS_TEST(memo_init_and_begin_allocate_nothing)
{
    amark_inline_scan_memo_t memo;
    size_t calls = g_state.malloc_calls;

    amark_inline_scan_memo_init(&memo, &g_memo_arena);
    NAOS_CHECK_UINT_EQ(amark_array_len(&memo.backticks), 0);
    NAOS_CHECK_FALSE(memo.backticks_complete);
    NAOS_CHECK_UINT_EQ(memo.html_no_end, 0);
    memo.backticks_complete = true;
    memo.html_no_end = 5;
    amark_inline_scan_memo_begin(&memo);
    NAOS_CHECK_FALSE(memo.backticks_complete);
    NAOS_CHECK_UINT_EQ(memo.html_no_end, 0);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, calls);
}

/* ---- long strings and many strings -------------------------------------- */

NAOS_TEST(code_span_strings_longer_than_1000)
{
    size_t length = 0;

    fill(&length, '`', 1001);
    fill(&length, 'a', 1);
    fill(&length, '`', 1000);
    fill(&length, 'b', 1);
    fill(&length, '`', 1002);
    fill(&length, 'c', 1);
    fill(&length, '`', 1001);
    parse_buffer(length);
    NAOS_CHECK_UINT_EQ(span_end(0, 0), length);
    NAOS_CHECK_UINT_EQ(span_end(0, 1002), NO_SPAN);
    NAOS_CHECK_UINT_GE(amark_array_len(&g_memo.backticks), 1003);
    NAOS_CHECK_UINT_EQ(check_memo_agrees(0), 1);
}

NAOS_TEST(code_span_more_than_1001_strings)
{
    size_t length = 0;
    size_t i;

    /* 1,101 strings of one backtick: 550 spans and one left over. */
    for (i = 0; i < 1101; i++) {
        fill(&length, '`', 1);
        fill(&length, 'a', 1);
    }
    parse_buffer(length);
    NAOS_CHECK_UINT_EQ(check_memo_agrees(0), 550);
    NAOS_CHECK_UINT_EQ(g_kept[549], 2 * 1100 + 1 - 2);
}

NAOS_TEST(code_span_strings_of_growing_length)
{
    size_t length = 0;
    size_t k;

    /*
     * "e`", "e``", "e```" and so on: no string closes. Each scan after the
     * first is answered by the memo; the walk with the memo kept makes one
     * search to the end.
     */
    for (k = 1; k <= 300; k++) {
        fill(&length, 'e', 1);
        fill(&length, '`', k);
    }
    parse_buffer(length);
    NAOS_CHECK_UINT_EQ(check_memo_agrees(0), 0);
    NAOS_CHECK_UINT_EQ(memo_at(300), length - 300);
    NAOS_CHECK_UINT_EQ(memo_at(2), 3);
}

/* ---- allocation --------------------------------------------------------- */

NAOS_TEST(code_span_allocates_only_to_grow_the_memo)
{
    size_t calls;

    PARSE("`a``b` ``c`` `d`");
    NAOS_CHECK_UINT_EQ(span_end(0, 0), 6);
    calls = g_state.malloc_calls;
    /* Lengths 1 and 2 are in the memo now; nothing grows. */
    NAOS_CHECK_UINT_EQ(span_end(0, 7), 12);
    NAOS_CHECK_UINT_EQ(span_end(0, 13), 16);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, calls);
}

NAOS_TEST(code_span_fails_when_the_memo_cannot_grow)
{
    amark_cursor_t cursor;
    amark_cursor_t saved;
    bool found = true;

    PARSE("`a`");
    cursor_at(&cursor, 0, 0);
    saved = cursor;
    g_state.fail_at = g_state.malloc_calls + 1;
    NAOS_CHECK_INT_EQ(amark_inline_scan_code_span(&cursor, &g_memo, 1,
                                                  &found),
                      AMARK_ERROR_NOMEM);
    NAOS_CHECK_FALSE(found);
    check_unmoved(&cursor, &saved);
    g_state.fail_at = 0;

    NAOS_CHECK_UINT_EQ(span_end(0, 0), 3);
}

NAOS_TEST(code_span_does_not_change_the_content)
{
    static const char input[] = "> ``a\n> b`c``";
    unsigned char copy[sizeof(input) - 1];

    PARSE("> ``a\n> b`c``");
    memcpy(copy, g_buffer, sizeof(copy));
    NAOS_CHECK_UINT_EQ(span_end(0, 2), 13);
    NAOS_CHECK_UINT_EQ(span_end(0, 9), NO_SPAN);
    NAOS_CHECK_MEM_EQ(g_buffer, sizeof(copy), copy, sizeof(copy));
    NAOS_CHECK_MEM_EQ(g_buffer, sizeof(copy), input, sizeof(copy));
}

/* ---- character references ----------------------------------------------- */

/*
 * Scans a reference at pos of the content at index. Returns the position
 * right after it, or 0 if it does not match; then checks that the cursor did
 * not move.
 */
static size_t entity_end(size_t index, size_t pos)
{
    amark_cursor_t cursor;
    amark_cursor_t saved;
    size_t calls = g_state.malloc_calls;

    cursor_at(&cursor, index, pos);
    saved = cursor;
    if (!amark_inline_scan_entity(&cursor)) {
        check_unmoved(&cursor, &saved);
        NAOS_CHECK_UINT_EQ(g_state.malloc_calls, calls);
        return 0;
    }
    NAOS_CHECK_UINT_EQ(cursor.line, saved.line);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, calls);
    return cursor.pos;
}

/* Parses s alone and returns entity_end at its first byte. */
static size_t entity_alone(const char *s)
{
    size_t end;

    parse_bytes(s, strlen(s));
    end = entity_end(0, 0);
    amark_document_destroy(g_doc);
    g_doc = NULL;
    return end;
}

NAOS_TEST(entity_names)
{
    NAOS_CHECK_UINT_EQ(entity_alone("&amp;"), 5);
    NAOS_CHECK_UINT_EQ(entity_alone("&amp;b"), 5);
    NAOS_CHECK_UINT_EQ(entity_alone("&nbsp; &copy;"), 6);
    NAOS_CHECK_UINT_EQ(entity_alone("&auml;"), 6);
    NAOS_CHECK_UINT_EQ(entity_alone("&Auml;"), 6);
    NAOS_CHECK_UINT_EQ(entity_alone("&ClockwiseContourIntegral;"), 26);
    NAOS_CHECK_UINT_EQ(entity_alone("&frac34;"), 8);
    NAOS_CHECK_UINT_EQ(entity_alone("&HilbertSpace;"), 14);

    PARSE("a &amp; b &lt;");
    NAOS_CHECK_UINT_EQ(entity_end(0, 2), 7);
    NAOS_CHECK_UINT_EQ(entity_end(0, 10), 14);
}

NAOS_TEST(entity_names_not_in_the_table)
{
    NAOS_CHECK_UINT_EQ(entity_alone("&Amp;"), 0);
    NAOS_CHECK_UINT_EQ(entity_alone("&foo;"), 0);
    NAOS_CHECK_UINT_EQ(entity_alone("&MadeUpEntity;"), 0);
    NAOS_CHECK_UINT_EQ(entity_alone("&;"), 0);
    NAOS_CHECK_UINT_EQ(entity_alone("& amp;"), 0);
    /* Not alphanumeric: the name stops before '-' and ';' must follow. */
    NAOS_CHECK_UINT_EQ(entity_alone("&am-p;"), 0);
    NAOS_CHECK_UINT_EQ(entity_alone("&x\xC3\xA4;"), 0);
}

NAOS_TEST(entity_without_semicolon)
{
    NAOS_CHECK_UINT_EQ(entity_alone("&amp"), 0);
    NAOS_CHECK_UINT_EQ(entity_alone("&amp b;"), 0);
    NAOS_CHECK_UINT_EQ(entity_alone("&copy"), 0);
    NAOS_CHECK_UINT_EQ(entity_alone("&#35"), 0);
    NAOS_CHECK_UINT_EQ(entity_alone("&#x22"), 0);
    NAOS_CHECK_UINT_EQ(entity_alone("&"), 0);
    NAOS_CHECK_UINT_EQ(entity_alone("&#"), 0);
    NAOS_CHECK_UINT_EQ(entity_alone("&#x"), 0);
}

NAOS_TEST(entity_decimal)
{
    NAOS_CHECK_UINT_EQ(entity_alone("&#35;"), 5);
    NAOS_CHECK_UINT_EQ(entity_alone("&#1;"), 4);
    NAOS_CHECK_UINT_EQ(entity_alone("&#1234567;"), 10);
    NAOS_CHECK_UINT_EQ(entity_alone("&#0000035;"), 10);
    NAOS_CHECK_UINT_EQ(entity_alone("&#12345678;"), 0);
    NAOS_CHECK_UINT_EQ(entity_alone("&#00000035;"), 0);
    NAOS_CHECK_UINT_EQ(entity_alone("&#;"), 0);
    NAOS_CHECK_UINT_EQ(entity_alone("&#3a;"), 0);
    NAOS_CHECK_UINT_EQ(entity_alone("&# 35;"), 0);
}

NAOS_TEST(entity_hexadecimal)
{
    NAOS_CHECK_UINT_EQ(entity_alone("&#x22;"), 6);
    NAOS_CHECK_UINT_EQ(entity_alone("&#X22;"), 6);
    NAOS_CHECK_UINT_EQ(entity_alone("&#xD06;"), 7);
    NAOS_CHECK_UINT_EQ(entity_alone("&#xcab;"), 7);
    NAOS_CHECK_UINT_EQ(entity_alone("&#xABCDEF;"), 10);
    NAOS_CHECK_UINT_EQ(entity_alone("&#x10FFFF;"), 10);
    NAOS_CHECK_UINT_EQ(entity_alone("&#x1234567;"), 0);
    NAOS_CHECK_UINT_EQ(entity_alone("&#x0000041;"), 0);
    NAOS_CHECK_UINT_EQ(entity_alone("&#x;"), 0);
    NAOS_CHECK_UINT_EQ(entity_alone("&#X;"), 0);
    NAOS_CHECK_UINT_EQ(entity_alone("&#xG;"), 0);
    NAOS_CHECK_UINT_EQ(entity_alone("&#x2g;"), 0);
    NAOS_CHECK_UINT_EQ(entity_alone("&#87654321;"), 0);
}

NAOS_TEST(entity_values_are_not_checked)
{
    NAOS_CHECK_UINT_EQ(entity_alone("&#0;"), 4);
    NAOS_CHECK_UINT_EQ(entity_alone("&#x0;"), 5);
    NAOS_CHECK_UINT_EQ(entity_alone("&#xD800;"), 8);
    NAOS_CHECK_UINT_EQ(entity_alone("&#xDFFF;"), 8);
    NAOS_CHECK_UINT_EQ(entity_alone("&#55296;"), 8);
    NAOS_CHECK_UINT_EQ(entity_alone("&#x110000;"), 10);
    NAOS_CHECK_UINT_EQ(entity_alone("&#xFFFFFF;"), 10);
    NAOS_CHECK_UINT_EQ(entity_alone("&#9999999;"), 10);
    NAOS_CHECK_UINT_EQ(entity_alone("&#1114112;"), 10);
}

NAOS_TEST(entity_stays_in_one_line)
{
    PARSE("a &amp\n; &#35\n; &\namp;");
    NAOS_CHECK_UINT_EQ(entity_end(0, 2), 0);
    NAOS_CHECK_UINT_EQ(entity_end(0, 9), 0);
    NAOS_CHECK_UINT_EQ(entity_end(0, 16), 0);
    g_doc = (amark_document_destroy(g_doc), NULL);

    /* A reference on a later line of a block quote. */
    PARSE("> a\n> b &amp; c\n");
    NAOS_CHECK_UINT_EQ(entity_end(0, 8), 13);
    g_doc = (amark_document_destroy(g_doc), NULL);

    /* The end of the content, before a trailing space. */
    PARSE("a &amp   \n");
    NAOS_CHECK_UINT_EQ(entity_end(0, 2), 0);
}

NAOS_TEST(entity_long_alphanumeric_run)
{
    size_t length = 0;

    fill(&length, '&', 1);
    fill(&length, 'a', 5000);
    fill(&length, ';', 1);
    parse_buffer(length);
    NAOS_CHECK_UINT_EQ(entity_end(0, 0), 0);
}

NAOS_TEST(entity_does_not_change_the_content)
{
    static const char input[] = "&amp; &#x22; &#35; &nope;";
    unsigned char copy[sizeof(input) - 1];

    PARSE("&amp; &#x22; &#35; &nope;");
    memcpy(copy, g_buffer, sizeof(copy));
    NAOS_CHECK_UINT_EQ(entity_end(0, 0), 5);
    NAOS_CHECK_UINT_EQ(entity_end(0, 6), 12);
    NAOS_CHECK_UINT_EQ(entity_end(0, 13), 18);
    NAOS_CHECK_UINT_EQ(entity_end(0, 19), 0);
    NAOS_CHECK_MEM_EQ(g_buffer, sizeof(copy), copy, sizeof(copy));
    NAOS_CHECK_MEM_EQ(g_buffer, sizeof(copy), input, sizeof(copy));
}

NAOS_SUITE(code_span,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(code_span_closes_with_the_same_length),
    NAOS_ADD_TEST(code_span_strings_of_other_lengths_do_not_close),
    NAOS_ADD_TEST(code_span_needs_something_after_the_opening_string),
    NAOS_ADD_TEST(code_span_ends_at_the_first_closing_string),
    NAOS_ADD_TEST(code_span_without_closing_string_does_not_move_the_cursor),
    NAOS_ADD_TEST(code_span_across_lines),
    NAOS_ADD_TEST(code_span_across_crlf_and_cr),
    NAOS_ADD_TEST(code_span_across_lines_in_containers),
    NAOS_ADD_TEST(code_span_does_not_count_prefixes),
    NAOS_ADD_TEST(code_span_does_not_leave_the_content),
    NAOS_ADD_TEST(code_span_strings_longer_than_1000),
    NAOS_ADD_TEST(code_span_more_than_1001_strings),
    NAOS_ADD_TEST(code_span_strings_of_growing_length),
    NAOS_ADD_TEST(code_span_allocates_only_to_grow_the_memo),
    NAOS_ADD_TEST(code_span_fails_when_the_memo_cannot_grow),
    NAOS_ADD_TEST(code_span_does_not_change_the_content)
)

NAOS_SUITE(memo,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(memo_answers_without_searching_once_complete),
    NAOS_ADD_TEST(memo_does_not_hide_a_later_string),
    NAOS_ADD_TEST(memo_agrees_with_a_fresh_search),
    NAOS_ADD_TEST(memo_is_kept_across_contents),
    NAOS_ADD_TEST(memo_init_and_begin_allocate_nothing)
)

NAOS_SUITE(entity,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(entity_names),
    NAOS_ADD_TEST(entity_names_not_in_the_table),
    NAOS_ADD_TEST(entity_without_semicolon),
    NAOS_ADD_TEST(entity_decimal),
    NAOS_ADD_TEST(entity_hexadecimal),
    NAOS_ADD_TEST(entity_values_are_not_checked),
    NAOS_ADD_TEST(entity_stays_in_one_line),
    NAOS_ADD_TEST(entity_long_alphanumeric_run),
    NAOS_ADD_TEST(entity_does_not_change_the_content)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(code_span),
    NAOS_ADD_SUITE(memo),
    NAOS_ADD_SUITE(entity)
)
