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
 * Autolinks: schemes of 1, 2, 32 and 33 characters, URIs with a space, '<'
 * or a control character, the shapes of an email address and the lengths
 * of its labels, and autolinks that would cross a line.
 *
 * Raw HTML: the tags of the HTML block tests of start condition 7
 * (tests/unit/test_scan.c), the examples of CommonMark 0.31.2 section 6.6,
 * matches and mismatches of the six kinds, "<!-->" and "<!--->", line
 * endings between attributes and in values, bytes outside the content of
 * the lines, and the bit each of the four unclosed kinds sets in the memo.
 *
 * Link tails: "()", "(<>)", a destination alone, the three delimiters of a
 * title, an empty title, a title across lines, escaped delimiters, 32 and
 * 33 levels of nested parentheses, no white space between the destination
 * and the title, "(\"t\")" read as a destination, white space with one
 * line ending, and bytes outside the content of the lines. The same
 * destinations and titles go through amark_refdef_parse, which reads them
 * by the same rules; 33 levels, which only the link tail rejects, are
 * checked on each side.
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
#include "refdef.h"
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
    AMARK_CONFIG_INIT(&config);
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

/* ---- autolinks ---------------------------------------------------------- */

/*
 * Checks that cursor is a cursor of the content at index placed at its
 * position with amark_cursor_seek, line records included.
 */
static void check_placed(const amark_cursor_t *cursor, size_t index)
{
    amark_cursor_t expected;

    cursor_at(&expected, index, cursor->pos);
    check_unmoved(cursor, &expected);
}

/*
 * Scans an autolink at pos of the content at index. Returns the position
 * right after it and writes its type to *type, or returns 0 if it does not
 * match; then checks that the cursor and *type did not change.
 */
static size_t autolink_end(size_t index, size_t pos,
                           amark_autolink_type_t *type)
{
    amark_cursor_t cursor;
    amark_cursor_t saved;
    size_t calls = g_state.malloc_calls;

    *type = AMARK_AUTOLINK_TYPE_NONE;
    cursor_at(&cursor, index, pos);
    saved = cursor;
    if (!amark_inline_scan_autolink(&cursor, type)) {
        check_unmoved(&cursor, &saved);
        NAOS_CHECK_INT_EQ(*type, AMARK_AUTOLINK_TYPE_NONE);
        NAOS_CHECK_UINT_EQ(g_state.malloc_calls, calls);
        return 0;
    }
    NAOS_CHECK_UINT_EQ(cursor.line, saved.line);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, calls);
    return cursor.pos;
}

/*
 * Parses "x " and s, so that s is inside a paragraph, and returns the length
 * of the autolink at the start of s (0 if none), writing its type to *type.
 */
static size_t autolink_in(const char *s, amark_autolink_type_t *type)
{
    size_t length = strlen(s);
    size_t end;

    NAOS_REQUIRE_UINT_LE(length, BUFFER_SIZE - 2);
    g_buffer[0] = 'x';
    g_buffer[1] = ' ';
    memcpy(g_buffer + 2, s, length);
    parse_buffer(length + 2);
    end = autolink_end(0, 2, type);
    amark_document_destroy(g_doc);
    g_doc = NULL;
    return end == 0 ? 0 : end - 2;
}

/* The length of the autolink at the start of s if it is a URI, else 0. */
static size_t uri_in(const char *s)
{
    amark_autolink_type_t type;
    size_t length = autolink_in(s, &type);

    NAOS_CHECK_TRUE(length == 0 || type == AMARK_AUTOLINK_URI);
    return type == AMARK_AUTOLINK_URI ? length : 0;
}

/* The length of the autolink at the start of s if it is an email, else 0. */
static size_t email_in(const char *s)
{
    amark_autolink_type_t type;
    size_t length = autolink_in(s, &type);

    NAOS_CHECK_TRUE(length == 0 || type == AMARK_AUTOLINK_EMAIL);
    return type == AMARK_AUTOLINK_EMAIL ? length : 0;
}

/* The length of the autolink at the start of s, of either type, or 0. */
static size_t any_autolink_in(const char *s)
{
    amark_autolink_type_t type;

    return autolink_in(s, &type);
}

NAOS_TEST(autolink_uri_examples)
{
    NAOS_CHECK_UINT_EQ(uri_in("<http://foo.bar.baz>"), 20);
    NAOS_CHECK_UINT_EQ(
        uri_in("<https://foo.bar.baz/test?q=hello&id=22&boolean>"), 48);
    NAOS_CHECK_UINT_EQ(uri_in("<irc://foo.bar:2233/baz>"), 24);
    NAOS_CHECK_UINT_EQ(uri_in("<MAILTO:FOO@BAR.BAZ>"), 20);
    NAOS_CHECK_UINT_EQ(uri_in("<a+b+c:d>"), 9);
    NAOS_CHECK_UINT_EQ(uri_in("<made-up-scheme://foo,bar>"), 26);
    NAOS_CHECK_UINT_EQ(uri_in("<https://../>"), 13);
    NAOS_CHECK_UINT_EQ(uri_in("<localhost:5001/foo>"), 20);
    NAOS_CHECK_UINT_EQ(uri_in("<https://example.com/\\[\\>"), 25);
    NAOS_CHECK_UINT_EQ(uri_in("<ab:>"), 5);
    NAOS_CHECK_UINT_EQ(uri_in("<a.b-c+d:x>"), 11);
    /* Bytes over 0x7F are not control characters. */
    NAOS_CHECK_UINT_EQ(uri_in("<http://a\xC3\xA4\xFF>"), 13);
    /* Only the first '>' ends it. */
    NAOS_CHECK_UINT_EQ(uri_in("<ab:c>d>"), 6);
}

NAOS_TEST(autolink_scheme_lengths)
{
    char s[64];

    /* 1 and 2 characters. */
    NAOS_CHECK_UINT_EQ(any_autolink_in("<a:b>"), 0);
    NAOS_CHECK_UINT_EQ(any_autolink_in("<m:abc>"), 0);
    NAOS_CHECK_UINT_EQ(uri_in("<ab:c>"), 6);
    /* 32 and 33 characters. */
    memset(s, 'a', sizeof(s));
    s[0] = '<';
    memcpy(s + 33, ":x>", 4);
    NAOS_CHECK_UINT_EQ(uri_in(s), 36);
    s[1] = 'Z';
    s[32] = '9';
    NAOS_CHECK_UINT_EQ(uri_in(s), 36);
    memset(s + 1, 'a', 33);
    memcpy(s + 34, ":x>", 4);
    NAOS_CHECK_UINT_EQ(any_autolink_in(s), 0);
    /* The scheme starts with a letter and has no other bytes. */
    NAOS_CHECK_UINT_EQ(any_autolink_in("<1a:b>"), 0);
    NAOS_CHECK_UINT_EQ(any_autolink_in("<+a:b>"), 0);
    NAOS_CHECK_UINT_EQ(any_autolink_in("<a_b:c>"), 0);
    NAOS_CHECK_UINT_EQ(any_autolink_in("<a b:c>"), 0);
    NAOS_CHECK_UINT_EQ(any_autolink_in("<:b>"), 0);
    NAOS_CHECK_UINT_EQ(any_autolink_in("<ab>"), 0);
}

NAOS_TEST(autolink_uri_without_space_lt_or_controls)
{
    NAOS_CHECK_UINT_EQ(any_autolink_in("<http://foo.bar/baz bim>"), 0);
    NAOS_CHECK_UINT_EQ(any_autolink_in("<http://a<b>"), 0);
    NAOS_CHECK_UINT_EQ(any_autolink_in("<http://a\tb>"), 0);
    NAOS_CHECK_UINT_EQ(any_autolink_in("<http://a\x01" "b>"), 0);
    NAOS_CHECK_UINT_EQ(any_autolink_in("<http://a\x1F" "b>"), 0);
    NAOS_CHECK_UINT_EQ(any_autolink_in("<http://a\x7F" "b>"), 0);
    NAOS_CHECK_UINT_EQ(any_autolink_in("< http://foo.bar >"), 0);
    NAOS_CHECK_UINT_EQ(any_autolink_in("<http://foo.bar"), 0);
    NAOS_CHECK_UINT_EQ(any_autolink_in("<>"), 0);
    NAOS_CHECK_UINT_EQ(any_autolink_in("<"), 0);
}

NAOS_TEST(autolink_email_examples)
{
    NAOS_CHECK_UINT_EQ(email_in("<foo@bar.example.com>"), 21);
    NAOS_CHECK_UINT_EQ(email_in("<foo+special@Bar.baz-bar0.com>"), 30);
    NAOS_CHECK_UINT_EQ(email_in("<a@b>"), 5);
    NAOS_CHECK_UINT_EQ(email_in("<a@b.c>"), 7);
    NAOS_CHECK_UINT_EQ(email_in("<.!#$%&'*+/=?^_`{|}~-@a>"), 24);
    NAOS_CHECK_UINT_EQ(email_in("<a@0-9.Z>"), 9);
    /* Not an email: '\', no '@', no local part, bad labels. */
    NAOS_CHECK_UINT_EQ(any_autolink_in("<foo\\+@bar.example.com>"), 0);
    NAOS_CHECK_UINT_EQ(any_autolink_in("<foo.bar.baz>"), 0);
    NAOS_CHECK_UINT_EQ(any_autolink_in("<@b.c>"), 0);
    NAOS_CHECK_UINT_EQ(any_autolink_in("<a@>"), 0);
    NAOS_CHECK_UINT_EQ(any_autolink_in("<a@-b>"), 0);
    NAOS_CHECK_UINT_EQ(any_autolink_in("<a@b->"), 0);
    NAOS_CHECK_UINT_EQ(any_autolink_in("<a@b.-c>"), 0);
    NAOS_CHECK_UINT_EQ(any_autolink_in("<a@b..c>"), 0);
    NAOS_CHECK_UINT_EQ(any_autolink_in("<a@b.>"), 0);
    NAOS_CHECK_UINT_EQ(any_autolink_in("<a@.b>"), 0);
    NAOS_CHECK_UINT_EQ(any_autolink_in("<a@b_c>"), 0);
    NAOS_CHECK_UINT_EQ(any_autolink_in("<a@b@c>"), 0);
    NAOS_CHECK_UINT_EQ(any_autolink_in("<a b@c>"), 0);
    NAOS_CHECK_UINT_EQ(any_autolink_in("<a@b c>"), 0);
    NAOS_CHECK_UINT_EQ(any_autolink_in("<a@b\xC3\xA4>"), 0);
    NAOS_CHECK_UINT_EQ(any_autolink_in("<a@b.c"), 0);
}

NAOS_TEST(autolink_email_label_lengths)
{
    char s[160];

    /* "<a@" + 63 characters + ".c>": 3 + 63 + 3. */
    memcpy(s, "<a@", 3);
    memset(s + 3, 'b', 63);
    memcpy(s + 66, ".c>", 4);
    NAOS_CHECK_UINT_EQ(email_in(s), 69);
    s[4] = '-';
    NAOS_CHECK_UINT_EQ(email_in(s), 69);
    memset(s + 3, 'b', 64);
    memcpy(s + 67, ".c>", 4);
    NAOS_CHECK_UINT_EQ(any_autolink_in(s), 0);
    /* The last label too. */
    memcpy(s, "<a@b.", 5);
    memset(s + 5, 'c', 63);
    memcpy(s + 68, ">", 2);
    NAOS_CHECK_UINT_EQ(email_in(s), 69);
    memset(s + 5, 'c', 64);
    memcpy(s + 69, ">", 2);
    NAOS_CHECK_UINT_EQ(any_autolink_in(s), 0);
}

NAOS_TEST(autolink_stays_in_one_line)
{
    amark_autolink_type_t type;

    PARSE("x <http://a\nb> <a@b\n.c> <a\n@b>");
    NAOS_CHECK_UINT_EQ(autolink_end(0, 2, &type), 0);
    NAOS_CHECK_UINT_EQ(autolink_end(0, 15, &type), 0);
    NAOS_CHECK_UINT_EQ(autolink_end(0, 24, &type), 0);
    g_doc = (amark_document_destroy(g_doc), NULL);

    /* An autolink on a later line of a block quote. */
    PARSE("> a\n> <http://b> <c@d>\n");
    NAOS_CHECK_UINT_EQ(autolink_end(0, 6, &type), 16);
    NAOS_CHECK_INT_EQ(type, AMARK_AUTOLINK_URI);
    NAOS_CHECK_UINT_EQ(autolink_end(0, 17, &type), 22);
    NAOS_CHECK_INT_EQ(type, AMARK_AUTOLINK_EMAIL);
    g_doc = (amark_document_destroy(g_doc), NULL);

    /* The end of the content, before a trailing space. */
    PARSE("x <http://a   \n");
    NAOS_CHECK_UINT_EQ(autolink_end(0, 2, &type), 0);
}

NAOS_TEST(autolink_does_not_change_the_content)
{
    static const char input[] = "<http://a> <b@c> <d>";
    unsigned char copy[sizeof(input) - 1];
    amark_autolink_type_t type;

    PARSE("<http://a> <b@c> <d>");
    memcpy(copy, g_buffer, sizeof(copy));
    NAOS_CHECK_UINT_EQ(autolink_end(0, 0, &type), 10);
    NAOS_CHECK_UINT_EQ(autolink_end(0, 11, &type), 16);
    NAOS_CHECK_UINT_EQ(autolink_end(0, 17, &type), 0);
    NAOS_CHECK_MEM_EQ(g_buffer, sizeof(copy), copy, sizeof(copy));
    NAOS_CHECK_MEM_EQ(g_buffer, sizeof(copy), input, sizeof(copy));
}

/* ---- raw HTML ----------------------------------------------------------- */

/*
 * Scans raw HTML at pos of the content at index. Returns the position right
 * after it, or 0 if it does not match; then checks that the cursor did not
 * move. On a match, checks that the cursor's line records are those of its
 * position.
 */
static size_t html_end(size_t index, size_t pos)
{
    amark_cursor_t cursor;
    amark_cursor_t saved;
    size_t calls = g_state.malloc_calls;

    cursor_at(&cursor, index, pos);
    saved = cursor;
    if (!amark_inline_scan_html(&cursor, &g_memo)) {
        check_unmoved(&cursor, &saved);
        NAOS_CHECK_UINT_EQ(g_state.malloc_calls, calls);
        return 0;
    }
    check_placed(&cursor, index);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, calls);
    return cursor.pos;
}

/*
 * Parses "x " and s, so that s is inside a paragraph, and returns the length
 * of the raw HTML at the start of s, or 0 if there is none. The memo is
 * started on the new content.
 */
static size_t html_in(const char *s)
{
    size_t length = strlen(s);
    size_t end;

    NAOS_REQUIRE_UINT_LE(length, BUFFER_SIZE - 2);
    g_buffer[0] = 'x';
    g_buffer[1] = ' ';
    memcpy(g_buffer + 2, s, length);
    parse_buffer(length + 2);
    end = html_end(0, 2);
    amark_document_destroy(g_doc);
    g_doc = NULL;
    return end == 0 ? 0 : end - 2;
}

#define HTML_ALL(s) NAOS_CHECK_UINT_EQ(html_in(s), sizeof(s) - 1)
#define HTML_NONE(s) NAOS_CHECK_UINT_EQ(html_in(s), 0)

/*
 * The tags of the HTML block tests of start condition 7 (tests/unit/
 * test_scan.c, html_start_type_7_*). Inline, a tag may be followed by
 * anything, so the tags those tests reject for what follows them match here.
 */
NAOS_TEST(html_open_tags_of_the_block_tests)
{
    HTML_ALL("<a href=\"foo\">");
    HTML_ALL("<Warning>");
    HTML_ALL("<del>");
    HTML_ALL("<a/>");
    HTML_ALL("<a />");
    HTML_ALL("<my-tag1>");
    HTML_ALL("<a b c>");
    HTML_ALL("<a _b :c.d-e_f=1>");
    HTML_ALL("<a b='x\"y' c=\"x'y\" d=z>");
    HTML_ALL("<a b = 'c' >");
    HTML_ALL("<a b=''>");
    HTML_ALL("<a\tb\t=\t\"c\"\t/>");
    HTML_ALL("<Prez>");
    HTML_ALL("<h7>");
    HTML_ALL("<h>");
    HTML_ALL("<abc>");
    /* Followed by something. */
    NAOS_CHECK_UINT_EQ(html_in("<a>  \t"), 3);
    NAOS_CHECK_UINT_EQ(html_in("<a> x"), 3);
    NAOS_CHECK_UINT_EQ(html_in("<del>*foo*</del>"), 5);
    NAOS_CHECK_UINT_EQ(html_in("<a><b>"), 3);
    NAOS_CHECK_UINT_EQ(html_in("<a>\xC2\xA0"), 3);
}

NAOS_TEST(html_closing_tags_of_the_block_tests)
{
    HTML_ALL("</a>");
    HTML_ALL("</a \t>");
    HTML_ALL("</ins>");
    HTML_ALL("</pre>");
    HTML_ALL("</SCRIPT>");
    HTML_NONE("</a");
    HTML_NONE("</");
    HTML_NONE("</a b>");
    HTML_NONE("</a/>");
    HTML_NONE("</ a>");
    HTML_NONE("</1>");
}

NAOS_TEST(html_tags_the_block_tests_reject)
{
    /* Incomplete tags. */
    HTML_NONE("<a");
    HTML_NONE("<a href=\"foo");
    HTML_NONE("<a href='foo>");
    HTML_NONE("<a b=");
    HTML_NONE("<a b=>");
    /* Bad names, attributes and values. */
    HTML_NONE("<1a>");
    HTML_NONE("<a_b>");
    HTML_NONE("<a.b>");
    HTML_NONE("<a 1b>");
    HTML_NONE("<a b=c=d>");
    HTML_NONE("<a b=c`d>");
    HTML_NONE("<a b=c<d>");
    HTML_NONE("<a b=\"c\"d>");
    HTML_NONE("<a b='c'd='e'>");
    HTML_NONE("<a/ >");
    HTML_NONE("<a//>");
    HTML_NONE("<a\xC2\xA0" "b>");
    HTML_NONE("<a\vb>");
    HTML_NONE("<div/x>");
    HTML_NONE("< div>");
    HTML_NONE("<</div>");
    HTML_NONE("<>");
    HTML_NONE("<");
}

/* CommonMark 0.31.2, section 6.6, examples 613 to 632. */
NAOS_TEST(html_spec_examples)
{
    /* 613, 614 */
    NAOS_CHECK_UINT_EQ(html_in("<a><bab><c2c>"), 3);
    HTML_ALL("<bab>");
    HTML_ALL("<c2c>");
    HTML_ALL("<a/>");
    HTML_ALL("<b2/>");
    /* 617 */
    HTML_ALL("<a foo=\"bar\" bam = 'baz <em>\"</em>'\n_boolean zoop:33=zoop:33 />");
    /* 618 */
    HTML_ALL("<responsive-image src=\"foo.jpg\" />");
    /* 619, 620, 621 */
    HTML_NONE("<33>");
    HTML_NONE("<__>");
    HTML_NONE("<a h*#ref=\"hi\">");
    HTML_NONE("<a href=\"hi'>");
    HTML_NONE("<a href=hi'>");
    /* 622 */
    HTML_NONE("< a>");
    HTML_NONE("<bar/ >");
    HTML_NONE("<foo bar=baz\nbim!bop />");
    /* 623 */
    HTML_NONE("<a href='bar'title=title>");
    /* 624, 625 */
    HTML_ALL("</a>");
    HTML_ALL("</foo >");
    HTML_NONE("</a href=\"foo\">");
    /* 626, 627, 628 */
    HTML_ALL("<!-- this is a --\ncomment - with hyphens -->");
    NAOS_CHECK_UINT_EQ(html_in("<!--> foo -->"), 5);
    NAOS_CHECK_UINT_EQ(html_in("<!---> foo -->"), 6);
    /* 629, 630, 631 */
    HTML_ALL("<?php echo $a; ?>");
    HTML_ALL("<!ELEMENT br EMPTY>");
    HTML_ALL("<![CDATA[>&<]]>");
    /* 632 and the examples with escapes in values */
    HTML_ALL("<a href=\"&ouml;\">");
    HTML_ALL("<a href=\"\\*\">");
    HTML_NONE("<a href=\"\\\"\">");
}

NAOS_TEST(html_comments)
{
    HTML_ALL("<!-->");
    HTML_ALL("<!--->");
    HTML_ALL("<!---->");
    HTML_ALL("<!-- a -- b -->");
    HTML_ALL("<!--a-->");
    HTML_ALL("<!-- <!-- -->");
    NAOS_CHECK_UINT_EQ(html_in("<!-- a --> b -->"), 10);
    HTML_NONE("<!-- a --!>");
    HTML_NONE("<!-- a ->");
    HTML_NONE("<!--");
    HTML_NONE("<!-");
    HTML_NONE("<!- a -->");
    HTML_NONE("<!-- a");
}

NAOS_TEST(html_processing_instructions)
{
    HTML_ALL("<?a?>");
    HTML_ALL("<?" "?>");
    HTML_ALL("<?>?>");
    HTML_ALL("<? a > b ?>");
    NAOS_CHECK_UINT_EQ(html_in("<?a?> b ?>"), 5);
    HTML_NONE("<?>");
    HTML_NONE("<?");
    HTML_NONE("<? a ? >");
}

NAOS_TEST(html_declarations)
{
    HTML_ALL("<!DOCTYPE html>");
    HTML_ALL("<!a>");
    HTML_ALL("<!Ab-c d=\"e\">");
    NAOS_CHECK_UINT_EQ(html_in("<!a> b>"), 4);
    HTML_NONE("<!>");
    HTML_NONE("<!1>");
    HTML_NONE("<! a>");
    HTML_NONE("<!a");
}

NAOS_TEST(html_cdata_sections)
{
    HTML_ALL("<![CDATA[x]]>");
    HTML_ALL("<![CDATA[]]>");
    HTML_ALL("<![CDATA[a]]b]>]]>");
    NAOS_CHECK_UINT_EQ(html_in("<![CDATA[a]]> b]]>"), 13);
    HTML_NONE("<![CDATA[");
    HTML_NONE("<![CDATA[x]>");
    HTML_NONE("<![CDATA>");
    HTML_NONE("<![CDAT[x]]>");
}

NAOS_TEST(html_across_lines)
{
    /* Between attributes, around '=' and before the end. */
    HTML_ALL("<a\nb>");
    HTML_ALL("<a \n b\n= 'c'\n/>");
    HTML_ALL("<a b =\n'c'>");
    HTML_ALL("<b2\ndata=\"foo\" >");
    HTML_ALL("<a b\n/>");
    /*
     * A closing tag takes only white space after its name, which may hold
     * one line ending. A '>' at the start of a line would open a block
     * quote, so the continuation line is indented (an indented code block
     * cannot interrupt a paragraph).
     */
    NAOS_CHECK_UINT_EQ(html_in("</a\n    >"), 9);
    NAOS_CHECK_UINT_EQ(html_in("</a\nb>"), 0);
    /* In a quoted value. */
    HTML_ALL("<a b=\"c\nd\">");
    HTML_ALL("<a b='c\nd'>");
    HTML_ALL("<a b=\"\n\">");
    /* The four kinds with an end string. */
    HTML_ALL("<!-- a\nb -->");
    HTML_ALL("<?a\nb?>");
    HTML_ALL("<!A\nB>");
    HTML_ALL("<![CDATA[a\nb]]>");
    /* An unquoted value and a name end at the line ending. */
    HTML_ALL("<a b=c\nd>");
    NAOS_CHECK_UINT_EQ(html_in("<a\n/>x"), 5);
    /* An end string does not span a line ending. */
    HTML_NONE("<!-- a -\n->");
    HTML_NONE("<!-- a --\n>");
    HTML_NONE("<? a ?\n>");
    HTML_NONE("<![CDATA[a]]\n>");
    /* The opening string and the tag name stay in the line of the '<'. */
    HTML_NONE("<\na>");
    HTML_NONE("<!-\n- a -->");
    HTML_NONE("</\na>");
    HTML_NONE("<!\nA>");
}

NAOS_TEST(html_across_lines_reads_only_the_content)
{
    /* A block quote: the second line's prefix is "> ". */
    PARSE("> x <a b='c\n> d'>");
    NAOS_CHECK_UINT_EQ(html_end(0, 4), 17);
    /*
     * The prefix turned into quotes behind the parser's back: they are not
     * content, so the value still ends after "d".
     */
    g_buffer[12] = '\'';
    g_buffer[13] = '\'';
    NAOS_CHECK_UINT_EQ(html_end(0, 4), 17);
    g_doc = (amark_document_destroy(g_doc), NULL);

    /* A comment whose end would be "--" + a prefix '>'. */
    PARSE("> x <!-- a --\n> b -->");
    NAOS_CHECK_UINT_EQ(html_end(0, 4), 21);
    g_doc = (amark_document_destroy(g_doc), NULL);

    /* A list item's indentation turned into '>' is not the end of a tag. */
    PARSE("- x <a\n  b>");
    NAOS_CHECK_UINT_EQ(html_end(0, 4), 11);
    g_buffer[8] = '>';
    NAOS_CHECK_UINT_EQ(html_end(0, 4), 11);
    g_doc = (amark_document_destroy(g_doc), NULL);

    /* A lazy continuation line and a setext heading. */
    PARSE("> x <a\nb='c'>");
    NAOS_CHECK_UINT_EQ(html_end(0, 4), 13);
    g_doc = (amark_document_destroy(g_doc), NULL);

    PARSE("x <!--\n-->\n===\n");
    NAOS_CHECK_UINT_EQ(html_end(0, 2), 10);
    g_doc = (amark_document_destroy(g_doc), NULL);

    /* CR LF and CR. */
    PARSE("x <a\r\nb='\rc'>");
    NAOS_CHECK_UINT_EQ(html_end(0, 2), 13);
    g_doc = (amark_document_destroy(g_doc), NULL);

    /* The content ends: a blank line, another block, a heading. */
    PARSE("x <a\n\nb>");
    NAOS_CHECK_UINT_EQ(html_end(0, 2), 0);
    g_doc = (amark_document_destroy(g_doc), NULL);

    PARSE("x <a b='\n\n'>");
    NAOS_CHECK_UINT_EQ(html_end(0, 2), 0);
    g_doc = (amark_document_destroy(g_doc), NULL);

    PARSE("# x <a\nb>");
    NAOS_CHECK_UINT_EQ(html_end(0, 4), 0);
    g_doc = (amark_document_destroy(g_doc), NULL);

    PARSE("x <!--\n# -->");
    NAOS_CHECK_UINT_EQ(html_end(0, 2), 0);
    g_doc = (amark_document_destroy(g_doc), NULL);

    /* The end of the content before trailing spaces. */
    PARSE("x <a   \n");
    NAOS_CHECK_UINT_EQ(html_end(0, 2), 0);
}

NAOS_TEST(html_unclosed_kinds_set_their_bit)
{
    static const struct {
        const char *input;
        size_t second; /* the second opening, in "x " + input */
        size_t fix;    /* the byte that turns into end, in "x " + input */
        char end;
        unsigned bit;
    } cases[] = {
        { "<!-- a <!-- b --x", 9, 18, '>', AMARK_INLINE_SCAN_NO_END_COMMENT },
        { "<? a <? b ?x", 7, 13, '>', AMARK_INLINE_SCAN_NO_END_PI },
        { "<!A a <!B b x", 8, 14, '>',
          AMARK_INLINE_SCAN_NO_END_DECLARATION },
        { "<![CDATA[ a <![CDATA[ b ]]x", 14, 28, '>',
          AMARK_INLINE_SCAN_NO_END_CDATA },
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        size_t length = strlen(cases[i].input);

        g_buffer[0] = 'x';
        g_buffer[1] = ' ';
        memcpy(g_buffer + 2, cases[i].input, length);
        parse_buffer(length + 2);
        NAOS_CHECK_UINT_EQ(g_memo.html_no_end, 0);
        NAOS_CHECK_UINT_EQ(html_end(0, 2), 0);
        NAOS_CHECK_UINT_EQ(g_memo.html_no_end, cases[i].bit);
        /*
         * The end appears behind the memo's back. The bit says there is no
         * end in the rest, so the scan does not search.
         */
        NAOS_REQUIRE_UINT_EQ(cases[i].fix, length + 1);
        g_buffer[cases[i].fix] = cases[i].end;
        NAOS_CHECK_UINT_EQ(html_end(0, cases[i].second), 0);
        /* Started again, the memo knows nothing and the search finds it. */
        amark_inline_scan_memo_begin(&g_memo);
        NAOS_CHECK_UINT_EQ(html_end(0, cases[i].second), length + 2);
        NAOS_CHECK_UINT_EQ(g_memo.html_no_end, 0);
        g_doc = (amark_document_destroy(g_doc), NULL);
    }
}

NAOS_TEST(html_bits_are_per_kind)
{
    /* Every kind unclosed, then each kind closed once. */
    PARSE("x <!-- <? <!A <![CDATA[ <a b=\"");
    NAOS_CHECK_UINT_EQ(html_end(0, 2), 0);
    NAOS_CHECK_UINT_EQ(g_memo.html_no_end, AMARK_INLINE_SCAN_NO_END_COMMENT);
    NAOS_CHECK_UINT_EQ(html_end(0, 7), 0);
    NAOS_CHECK_UINT_EQ(g_memo.html_no_end,
                       AMARK_INLINE_SCAN_NO_END_COMMENT |
                           AMARK_INLINE_SCAN_NO_END_PI);
    NAOS_CHECK_UINT_EQ(html_end(0, 10), 0);
    NAOS_CHECK_UINT_EQ(html_end(0, 14), 0);
    /* An unclosed tag sets no bit. */
    NAOS_CHECK_UINT_EQ(html_end(0, 24), 0);
    NAOS_CHECK_UINT_EQ(g_memo.html_no_end,
                       AMARK_INLINE_SCAN_NO_END_COMMENT |
                           AMARK_INLINE_SCAN_NO_END_PI |
                           AMARK_INLINE_SCAN_NO_END_DECLARATION |
                           AMARK_INLINE_SCAN_NO_END_CDATA);
    g_doc = (amark_document_destroy(g_doc), NULL);

    /* A comment with no end does not stop the other kinds. */
    PARSE("x <!-- <?a?> <!A> <![CDATA[]]> <a>");
    NAOS_CHECK_UINT_EQ(html_end(0, 2), 0);
    NAOS_CHECK_UINT_EQ(html_end(0, 7), 12);
    NAOS_CHECK_UINT_EQ(html_end(0, 13), 17);
    NAOS_CHECK_UINT_EQ(html_end(0, 18), 30);
    NAOS_CHECK_UINT_EQ(html_end(0, 31), 34);
    NAOS_CHECK_UINT_EQ(g_memo.html_no_end, AMARK_INLINE_SCAN_NO_END_COMMENT);
    g_doc = (amark_document_destroy(g_doc), NULL);

    /* "<!-->" and "<!--->" need no search, so the bit does not stop them. */
    PARSE("x <!--> <!--->");
    g_memo.html_no_end = AMARK_INLINE_SCAN_NO_END_COMMENT;
    NAOS_CHECK_UINT_EQ(html_end(0, 2), 7);
    NAOS_CHECK_UINT_EQ(html_end(0, 8), 14);
}

NAOS_TEST(html_many_unclosed_openings)
{
    size_t length = 0;
    size_t i;
    size_t n = 0;

    /* "<!--" repeated: one search, then answers from the memo. */
    fill(&length, 'x', 1);
    for (i = 0; i < 4000; i++) {
        fill(&length, ' ', 1);
        fill(&length, '<', 1);
        fill(&length, '!', 1);
        fill(&length, '-', 2);
    }
    parse_buffer(length);
    for (i = 2; i < length; i += 5) {
        n += html_end(0, i) == 0;
    }
    NAOS_CHECK_UINT_EQ(n, 4000);
    NAOS_CHECK_UINT_EQ(g_memo.html_no_end, AMARK_INLINE_SCAN_NO_END_COMMENT);
}

NAOS_TEST(html_does_not_change_the_content)
{
    static const char input[] = "> <a b='c\n> d'> <!-- e --> <f";
    unsigned char copy[sizeof(input) - 1];

    PARSE("> <a b='c\n> d'> <!-- e --> <f");
    memcpy(copy, g_buffer, sizeof(copy));
    NAOS_CHECK_UINT_EQ(html_end(0, 2), 15);
    NAOS_CHECK_UINT_EQ(html_end(0, 16), 26);
    NAOS_CHECK_UINT_EQ(html_end(0, 27), 0);
    NAOS_CHECK_MEM_EQ(g_buffer, sizeof(copy), copy, sizeof(copy));
    NAOS_CHECK_MEM_EQ(g_buffer, sizeof(copy), input, sizeof(copy));
}

/* ---- link tails --------------------------------------------------------- */

/* An absent range in the expectations of check_tail. */
#define ABSENT ((size_t)-1)

/*
 * Scans a link tail at pos of the content at index. Returns the position
 * right after it and writes *tail, or returns 0 if it does not match; then
 * checks that the cursor and *tail did not change. On a match, checks that
 * the cursor's line records are those of its position and that the absent
 * ranges are {0, 0}. Checks that nothing was allocated.
 */
static size_t tail_end(size_t index, size_t pos, amark_link_tail_t *tail)
{
    amark_cursor_t cursor;
    amark_cursor_t saved;
    amark_link_tail_t before;
    size_t calls = g_state.malloc_calls;

    memset(tail, 0xA5, sizeof(*tail));
    memcpy(&before, tail, sizeof(before));
    cursor_at(&cursor, index, pos);
    saved = cursor;
    if (!amark_inline_scan_link_tail(&cursor, tail)) {
        check_unmoved(&cursor, &saved);
        NAOS_CHECK_TRUE(memcmp(tail, &before, sizeof(before)) == 0);
        NAOS_CHECK_UINT_EQ(g_state.malloc_calls, calls);
        return 0;
    }
    check_placed(&cursor, index);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, calls);
    if (!tail->has_destination) {
        NAOS_CHECK_UINT_EQ(tail->destination.start, 0);
        NAOS_CHECK_UINT_EQ(tail->destination.end, 0);
    }
    if (!tail->has_title) {
        NAOS_CHECK_UINT_EQ(tail->title.start, 0);
        NAOS_CHECK_UINT_EQ(tail->title.end, 0);
    }
    return cursor.pos;
}

/* Subtracts base from both ends of a range. */
static void shift_range(amark_range_t *range, size_t base)
{
    NAOS_REQUIRE_UINT_LE(base, range->start);
    range->start -= base;
    range->end -= base;
}

/*
 * Parses "x " and the length bytes of s, so that s is inside a paragraph,
 * and returns the length of the link tail at the start of s, or 0 if there
 * is none. On a match, writes *tail with its present ranges as offsets in
 * s.
 */
static size_t tail_in(const char *s, size_t length, amark_link_tail_t *tail)
{
    size_t end;

    NAOS_REQUIRE_UINT_LE(length, BUFFER_SIZE - 2);
    g_buffer[0] = 'x';
    g_buffer[1] = ' ';
    memcpy(g_buffer + 2, s, length);
    parse_buffer(length + 2);
    end = tail_end(0, 2, tail);
    amark_document_destroy(g_doc);
    g_doc = NULL;
    if (end == 0) {
        return 0;
    }
    if (tail->has_destination) {
        shift_range(&tail->destination, 2);
    }
    if (tail->has_title) {
        shift_range(&tail->title, 2);
    }
    return end - 2;
}

/*
 * Checks that s (a string literal) starts with a link tail of length bytes
 * (0 for none) with the destination and the title at the given offsets in
 * s, ABSENT for an absent one.
 */
static void check_tail(const char *s, size_t s_length, size_t length,
                       size_t dest_start, size_t dest_end,
                       size_t title_start, size_t title_end)
{
    amark_link_tail_t tail;

    NAOS_CHECK_UINT_EQ(tail_in(s, s_length, &tail), length);
    if (length == 0) {
        return;
    }
    NAOS_CHECK_INT_EQ(tail.has_destination, dest_start != ABSENT);
    if (tail.has_destination && dest_start != ABSENT) {
        NAOS_CHECK_UINT_EQ(tail.destination.start, dest_start);
        NAOS_CHECK_UINT_EQ(tail.destination.end, dest_end);
    }
    NAOS_CHECK_INT_EQ(tail.has_title, title_start != ABSENT);
    if (tail.has_title && title_start != ABSENT) {
        NAOS_CHECK_UINT_EQ(tail.title.start, title_start);
        NAOS_CHECK_UINT_EQ(tail.title.end, title_end);
    }
}

#define TAIL(s, length, ds, de, ts, te) \
    check_tail((s), sizeof(s) - 1, (length), (ds), (de), (ts), (te))
#define TAIL_NONE(s) \
    check_tail((s), sizeof(s) - 1, 0, ABSENT, ABSENT, ABSENT, ABSENT)

NAOS_TEST(link_tail_empty)
{
    TAIL("()", 2, ABSENT, ABSENT, ABSENT, ABSENT);
    TAIL("( )", 3, ABSENT, ABSENT, ABSENT, ABSENT);
    TAIL("(\t)", 3, ABSENT, ABSENT, ABSENT, ABSENT);
    TAIL("(\n)", 3, ABSENT, ABSENT, ABSENT, ABSENT);
    /* "<>" is a destination, present and empty. */
    TAIL("(<>)", 4, 2, 2, ABSENT, ABSENT);
    TAIL("( <> )", 6, 3, 3, ABSENT, ABSENT);
    /* What follows the ')' does not matter. */
    TAIL("()x", 2, ABSENT, ABSENT, ABSENT, ABSENT);
    TAIL("())", 2, ABSENT, ABSENT, ABSENT, ABSENT);
    /* No ')'. */
    TAIL_NONE("(");
    TAIL_NONE("( ");
    TAIL_NONE("(<>");
}

NAOS_TEST(link_tail_destination_only)
{
    TAIL("(/uri)", 6, 1, 5, ABSENT, ABSENT);
    TAIL("( /uri )", 8, 2, 6, ABSENT, ABSENT);
    TAIL("(<my uri>)", 10, 2, 8, ABSENT, ABSENT);
    TAIL("(<b)c>)", 7, 2, 5, ABSENT, ABSENT);
    TAIL("(foo(and(bar)))", 15, 1, 14, ABSENT, ABSENT);
    TAIL("(<foo(and(bar)>)", 16, 2, 14, ABSENT, ABSENT);
    TAIL("(#fragment)", 11, 1, 10, ABSENT, ABSENT);
    TAIL("(\xC3\xA4\xFF)", 5, 1, 4, ABSENT, ABSENT);
    /* An unmatched ')' ends a raw destination. */
    TAIL("(a)b)", 3, 1, 2, ABSENT, ABSENT);
    /* A space in a raw destination. */
    TAIL_NONE("(/my uri)");
    /* A line ending or an unescaped '<' in an angle-bracketed one. */
    TAIL_NONE("(<foo\nbar>)");
    TAIL_NONE("(<a<b>)");
    TAIL_NONE("(<a)");
    /* Unbalanced parentheses. */
    TAIL_NONE("(foo(and(bar))");
    /* An ASCII control character ends a raw destination. */
    TAIL_NONE("(a\x01b)");
    TAIL_NONE("(a\x7F)");
    TAIL_NONE("(\x01)");
    /* What does not end with '>' is not angle-bracketed, and is no raw one. */
    TAIL_NONE("(<a>b)");
}

NAOS_TEST(link_tail_title_delimiters)
{
    TAIL("(/url \"title\")", 14, 1, 5, 7, 12);
    TAIL("(/url 'title')", 14, 1, 5, 7, 12);
    TAIL("(/url (title))", 14, 1, 5, 7, 12);
    TAIL("(<a> \"t\")", 9, 2, 3, 6, 7);
    TAIL("( a\t\"t\"\t)", 9, 2, 3, 5, 6);
    /* Other delimiters of the other kinds inside. */
    TAIL("(a \"t 'u' (v)\")", 15, 1, 2, 4, 13);
    TAIL("(a 't \"u\" )')", 13, 1, 2, 4, 11);
    TAIL("(a (t \"u\" 'v'))", 15, 1, 2, 4, 13);
    /* A (...) title holds no unescaped '('. */
    TAIL_NONE("(a (t (u)))");
    TAIL_NONE("(a (t(u))");
    /* An unclosed title. */
    TAIL_NONE("(a \"t)");
    TAIL_NONE("(a 't)");
    TAIL_NONE("(a (t");
    /* Only white space after the title. */
    TAIL_NONE("(a \"t\" x)");
    TAIL_NONE("(a \"t\" \"u\")");
}

NAOS_TEST(link_tail_empty_title)
{
    TAIL("(a \"\")", 6, 1, 2, 4, 4);
    TAIL("(a '')", 6, 1, 2, 4, 4);
    TAIL("(a ())", 6, 1, 2, 4, 4);
    TAIL("(<> \"\")", 7, 2, 2, 5, 5);
}

NAOS_TEST(link_tail_title_across_lines)
{
    TAIL("(a \"t\nu\")", 9, 1, 2, 4, 7);
    TAIL("(a 't\r\nu')", 10, 1, 2, 4, 8);
    TAIL("(a (t\ru))", 9, 1, 2, 4, 7);
    TAIL("(a \"t\nu\nv\")", 11, 1, 2, 4, 9);
    /* White space with a line ending around the destination and title. */
    TAIL("(\na\n\"t\"\n)", 9, 2, 3, 5, 6);
    TAIL("( \n a \n \"t\" \n )", 15, 4, 5, 9, 10);
    /* A title that is not closed before the end of the content. */
    TAIL_NONE("(a \"t\nu)");
}

NAOS_TEST(link_tail_escapes)
{
    /* Escaped parentheses do not count. */
    TAIL("(\\(foo\\))", 9, 1, 8, ABSENT, ABSENT);
    TAIL("(a\\)b)", 6, 1, 5, ABSENT, ABSENT);
    TAIL("(a\\(b)", 6, 1, 5, ABSENT, ABSENT);
    TAIL_NONE("(a(\\))");
    /* Escapes of '<' and '>' in an angle-bracketed destination. */
    TAIL("(<a\\<b>)", 8, 2, 6, ABSENT, ABSENT);
    TAIL("(<a\\>b>)", 8, 2, 6, ABSENT, ABSENT);
    TAIL_NONE("(<a\\\\<b>)");
    /* Escaped closing delimiters and '(' in a title. */
    TAIL("(a \"t\\\"u\")", 10, 1, 2, 4, 8);
    TAIL("(a 't\\'u')", 10, 1, 2, 4, 8);
    TAIL("(a (t\\)u))", 10, 1, 2, 4, 8);
    TAIL("(a (t\\(u))", 10, 1, 2, 4, 8);
    /* An escaped backslash does not escape what follows. */
    TAIL("(a \"t\\\\\")", 9, 1, 2, 4, 7);
    TAIL("(a\\\\)", 5, 1, 4, ABSENT, ABSENT);
    /* A backslash before a letter or a space escapes nothing. */
    TAIL("(a\\b)", 5, 1, 4, ABSENT, ABSENT);
    TAIL_NONE("(a\\ b)");
    /* A backslash at the end of a title's line escapes nothing. */
    TAIL("(a \"t\\\n\")", 9, 1, 2, 4, 7);
    /* Character references are not read. */
    TAIL("(&#40; \"&quot;\")", 16, 1, 6, 8, 14);
}

/*
 * Writes to g_buffer from *length: count '(' when open, ')' otherwise,
 * each preceded by 'a'.
 */
static void fill_parens(size_t *length, char paren, size_t count)
{
    size_t i;

    for (i = 0; i < count; i++) {
        fill(length, 'a', 1);
        fill(length, paren, 1);
    }
}

/*
 * Parses "x (" + levels nested parentheses + ")" and returns the length of
 * the link tail at the '(' (0 if none).
 */
static size_t nested_tail_length(size_t levels)
{
    amark_link_tail_t tail;
    size_t length = 0;
    size_t end;

    fill(&length, 'x', 1);
    fill(&length, ' ', 1);
    fill(&length, '(', 1);
    fill_parens(&length, '(', levels);
    fill_parens(&length, ')', levels);
    fill(&length, ')', 1);
    parse_buffer(length);
    end = tail_end(0, 2, &tail);
    if (end != 0) {
        NAOS_CHECK_TRUE(tail.has_destination);
        NAOS_CHECK_UINT_EQ(tail.destination.start, 3);
        NAOS_CHECK_UINT_EQ(tail.destination.end, length - 1);
        NAOS_CHECK_FALSE(tail.has_title);
    }
    amark_document_destroy(g_doc);
    g_doc = NULL;
    return end == 0 ? 0 : end - 2;
}

NAOS_TEST(link_tail_nested_parentheses)
{
    NAOS_CHECK_UINT_EQ(AMARK_LINK_DEST_PAREN_MAX, 32);
    NAOS_CHECK_UINT_EQ(nested_tail_length(1), 2 + 4);
    NAOS_CHECK_UINT_EQ(nested_tail_length(31), 2 + 31 * 4);
    NAOS_CHECK_UINT_EQ(nested_tail_length(32), 2 + 32 * 4);
    NAOS_CHECK_UINT_EQ(nested_tail_length(33), 0);
    NAOS_CHECK_UINT_EQ(nested_tail_length(1000), 0);
    /* Escaped parentheses are not nested. */
    TAIL("(\\(\\(\\(\\(\\(\\(\\(\\(\\(\\(\\(\\(\\(\\(\\(\\(\\("
         "\\(\\(\\(\\(\\(\\(\\(\\(\\(\\(\\(\\(\\(\\(\\(\\()",
         68, 1, 67, ABSENT, ABSENT);
}

NAOS_TEST(link_tail_without_space_before_the_title)
{
    /* A quote belongs to a raw destination. */
    TAIL("(/url\"title\")", 13, 1, 12, ABSENT, ABSENT);
    TAIL("(/url'title')", 13, 1, 12, ABSENT, ABSENT);
    /* A '(' nests in it. */
    TAIL("(/url(title))", 13, 1, 12, ABSENT, ABSENT);
    /* After an angle-bracketed destination, nothing may follow directly. */
    TAIL_NONE("(<a>\"t\")");
    TAIL_NONE("(<a>'t')");
    TAIL_NONE("(<a>(t))");
    TAIL_NONE("(<a>b)");
}

NAOS_TEST(link_tail_quoted_text_alone_is_a_destination)
{
    /* CommonMark 0.31.2, example 504. */
    TAIL("(\"title\")", 9, 1, 8, ABSENT, ABSENT);
    TAIL("( 'title' )", 11, 2, 9, ABSENT, ABSENT);
    /* A '(' after the opening one nests in a destination. */
    TAIL("((title))", 9, 1, 8, ABSENT, ABSENT);
    TAIL_NONE("(\"a b\")");
}

NAOS_TEST(link_tail_one_line_ending)
{
    /* White space holds at most one line ending: two need a blank line. */
    TAIL_NONE("(\n\n)");
    TAIL_NONE("(a\n\n\"t\")");
    /* A line ending in a raw destination ends it. */
    TAIL_NONE("(a\nb)");
    /* At the end of the content. */
    TAIL_NONE("(a   \n");
}

NAOS_TEST(link_tail_reads_only_the_content)
{
    amark_link_tail_t tail;

    /* A block quote: the second line's prefix is "> ". */
    PARSE("> x (a\n> \"t\")");
    NAOS_CHECK_UINT_EQ(tail_end(0, 4, &tail), 13);
    NAOS_CHECK_UINT_EQ(tail.title.start, 10);
    NAOS_CHECK_UINT_EQ(tail.title.end, 11);
    /* The prefix turned into ')' behind the parser's back is no content. */
    g_buffer[7] = ')';
    NAOS_CHECK_UINT_EQ(tail_end(0, 4, &tail), 13);
    g_doc = (amark_document_destroy(g_doc), NULL);

    /* A title across lines whose closing quote would be in the prefix. */
    PARSE("> x (a 't\n> u')");
    NAOS_CHECK_UINT_EQ(tail_end(0, 4, &tail), 15);
    NAOS_CHECK_UINT_EQ(tail.title.start, 8);
    NAOS_CHECK_UINT_EQ(tail.title.end, 13);
    g_buffer[10] = '\'';
    NAOS_CHECK_UINT_EQ(tail_end(0, 4, &tail), 15);
    g_doc = (amark_document_destroy(g_doc), NULL);

    /* A list item's indentation. */
    PARSE("- x (a\n  )");
    NAOS_CHECK_UINT_EQ(tail_end(0, 4, &tail), 10);
    g_doc = (amark_document_destroy(g_doc), NULL);

    /* A lazy continuation line. */
    PARSE("> x (a\n'b')");
    NAOS_CHECK_UINT_EQ(tail_end(0, 4, &tail), 11);
    g_doc = (amark_document_destroy(g_doc), NULL);

    /* The content ends: a blank line, another block, a heading. */
    PARSE("x (a\n\n)");
    NAOS_CHECK_UINT_EQ(tail_end(0, 2, &tail), 0);
    g_doc = (amark_document_destroy(g_doc), NULL);

    PARSE("x (a 't\n\nu')");
    NAOS_CHECK_UINT_EQ(tail_end(0, 2, &tail), 0);
    g_doc = (amark_document_destroy(g_doc), NULL);

    PARSE("# x (a\n)");
    NAOS_CHECK_UINT_EQ(tail_end(0, 4, &tail), 0);
}

/*
 * Reads "[l]: " + s (a string literal) with amark_refdef_parse and "x (" +
 * s + ")" with amark_inline_scan_link_tail, and checks that both match with
 * the same destination and title, at the same offsets in s.
 */
static void check_same_as_refdef(const char *s, size_t length)
{
    amark_refdef_t def;
    amark_link_tail_t tail;
    size_t total;

    NAOS_REQUIRE_UINT_LE(length, BUFFER_SIZE - 5);
    memcpy(g_buffer, "[l]: ", 5);
    memcpy(g_buffer + 5, s, length);
    parse_buffer(length + 5);
    NAOS_REQUIRE_TRUE(amark_refdef_parse(g_doc, 0, g_length, &def));
    amark_document_destroy(g_doc);
    g_doc = NULL;
    shift_range(&def.destination, 5);
    if (def.has_title) {
        shift_range(&def.title, 5);
    }

    total = 0;
    fill(&total, 'x', 1);
    fill(&total, ' ', 1);
    fill(&total, '(', 1);
    NAOS_REQUIRE_UINT_LE(length, BUFFER_SIZE - total - 1);
    memcpy(g_buffer + total, s, length);
    total += length;
    fill(&total, ')', 1);
    parse_buffer(total);
    NAOS_CHECK_UINT_EQ(tail_end(0, 2, &tail), total);
    amark_document_destroy(g_doc);
    g_doc = NULL;
    NAOS_REQUIRE_TRUE(tail.has_destination);
    shift_range(&tail.destination, 3);
    NAOS_CHECK_UINT_EQ(tail.destination.start, def.destination.start);
    NAOS_CHECK_UINT_EQ(tail.destination.end, def.destination.end);
    NAOS_CHECK_INT_EQ(tail.has_title, def.has_title);
    if (tail.has_title && def.has_title) {
        shift_range(&tail.title, 3);
        NAOS_CHECK_UINT_EQ(tail.title.start, def.title.start);
        NAOS_CHECK_UINT_EQ(tail.title.end, def.title.end);
    }
}

#define SAME_AS_REFDEF(s) check_same_as_refdef((s), sizeof(s) - 1)

NAOS_TEST(link_tail_agrees_with_refdef)
{
    SAME_AS_REFDEF("/url");
    SAME_AS_REFDEF("<>");
    SAME_AS_REFDEF("<my url>");
    SAME_AS_REFDEF("<a\\<b\\>c>");
    SAME_AS_REFDEF("foo(and(bar))");
    SAME_AS_REFDEF("\\(foo\\)");
    SAME_AS_REFDEF("a\\\\b");
    SAME_AS_REFDEF("\"title\"");
    SAME_AS_REFDEF("/url \"title\"");
    SAME_AS_REFDEF("/url 'title'");
    SAME_AS_REFDEF("/url (title)");
    SAME_AS_REFDEF("<a b> \"t\"");
    SAME_AS_REFDEF("/url \"\"");
    SAME_AS_REFDEF("/url \"t\\\"u\"");
    SAME_AS_REFDEF("/url (t\\(u\\))");
    SAME_AS_REFDEF("/url \"t 'u' (v)\"");
    SAME_AS_REFDEF("/url\n'title\nline'");
    SAME_AS_REFDEF("/url\r\n\"t\r\nu\"");
    SAME_AS_REFDEF("&#40; \"&quot;\"");
    SAME_AS_REFDEF("\xC3\xA4 '\xC3\xB6'");
}

/*
 * 33 levels of nested parentheses: amark_refdef_parse has no limit and
 * reads the definition; the link tail rejects it. 32 levels: both read it.
 */
NAOS_TEST(link_tail_33_levels_differ_from_refdef)
{
    amark_refdef_t def;
    size_t length;
    size_t levels;

    for (levels = 32; levels <= 33; levels++) {
        length = 0;
        fill(&length, '[', 1);
        fill(&length, 'l', 1);
        fill(&length, ']', 1);
        fill(&length, ':', 1);
        fill(&length, ' ', 1);
        fill_parens(&length, '(', levels);
        fill_parens(&length, ')', levels);
        parse_buffer(length);
        NAOS_CHECK_TRUE(amark_refdef_parse(g_doc, 0, g_length, &def));
        NAOS_CHECK_UINT_EQ(def.destination.start, 5);
        NAOS_CHECK_UINT_EQ(def.destination.end, length);
        NAOS_CHECK_FALSE(def.has_title);
        amark_document_destroy(g_doc);
        g_doc = NULL;
    }
    NAOS_CHECK_UINT_EQ(nested_tail_length(32), 2 + 32 * 4);
    NAOS_CHECK_UINT_EQ(nested_tail_length(33), 0);
}

NAOS_TEST(link_tail_does_not_change_the_content)
{
    static const char input[] = "> x (a 'b\n> c') (d";
    unsigned char copy[sizeof(input) - 1];
    amark_link_tail_t tail;

    PARSE("> x (a 'b\n> c') (d");
    memcpy(copy, g_buffer, sizeof(copy));
    NAOS_CHECK_UINT_EQ(tail_end(0, 4, &tail), 15);
    NAOS_CHECK_UINT_EQ(tail_end(0, 16, &tail), 0);
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

NAOS_SUITE(autolink,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(autolink_uri_examples),
    NAOS_ADD_TEST(autolink_scheme_lengths),
    NAOS_ADD_TEST(autolink_uri_without_space_lt_or_controls),
    NAOS_ADD_TEST(autolink_email_examples),
    NAOS_ADD_TEST(autolink_email_label_lengths),
    NAOS_ADD_TEST(autolink_stays_in_one_line),
    NAOS_ADD_TEST(autolink_does_not_change_the_content)
)

NAOS_SUITE(html,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(html_open_tags_of_the_block_tests),
    NAOS_ADD_TEST(html_closing_tags_of_the_block_tests),
    NAOS_ADD_TEST(html_tags_the_block_tests_reject),
    NAOS_ADD_TEST(html_spec_examples),
    NAOS_ADD_TEST(html_comments),
    NAOS_ADD_TEST(html_processing_instructions),
    NAOS_ADD_TEST(html_declarations),
    NAOS_ADD_TEST(html_cdata_sections),
    NAOS_ADD_TEST(html_across_lines),
    NAOS_ADD_TEST(html_across_lines_reads_only_the_content),
    NAOS_ADD_TEST(html_unclosed_kinds_set_their_bit),
    NAOS_ADD_TEST(html_bits_are_per_kind),
    NAOS_ADD_TEST(html_many_unclosed_openings),
    NAOS_ADD_TEST(html_does_not_change_the_content)
)

NAOS_SUITE(link_tail,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(link_tail_empty),
    NAOS_ADD_TEST(link_tail_destination_only),
    NAOS_ADD_TEST(link_tail_title_delimiters),
    NAOS_ADD_TEST(link_tail_empty_title),
    NAOS_ADD_TEST(link_tail_title_across_lines),
    NAOS_ADD_TEST(link_tail_escapes),
    NAOS_ADD_TEST(link_tail_nested_parentheses),
    NAOS_ADD_TEST(link_tail_without_space_before_the_title),
    NAOS_ADD_TEST(link_tail_quoted_text_alone_is_a_destination),
    NAOS_ADD_TEST(link_tail_one_line_ending),
    NAOS_ADD_TEST(link_tail_reads_only_the_content),
    NAOS_ADD_TEST(link_tail_agrees_with_refdef),
    NAOS_ADD_TEST(link_tail_33_levels_differ_from_refdef),
    NAOS_ADD_TEST(link_tail_does_not_change_the_content)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(code_span),
    NAOS_ADD_SUITE(memo),
    NAOS_ADD_SUITE(entity),
    NAOS_ADD_SUITE(autolink),
    NAOS_ADD_SUITE(html),
    NAOS_ADD_SUITE(link_tail)
)
