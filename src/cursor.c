/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#include "cursor.h"

#include "line.h"
#include "parse_data.h"
#include "utf8.h"

/* The most continuation bytes a well-formed sequence has after its lead. */
#define MAX_CONTINUATION 3

static bool is_continuation(unsigned char c)
{
    return c >= 0x80 && c <= 0xBF;
}

/*
 * Sets line_end for the current line: the end of the line's content, or
 * limit if that comes first (on the last line of the range). Never before
 * pos.
 */
static void set_line_end(amark_cursor_t *cursor)
{
    amark_line_bounds_t bounds;
    size_t end = cursor->limit;

    if (amark_line_bounds(cursor->doc, cursor->line, &bounds) &&
        bounds.content_end < end) {
        end = bounds.content_end;
    }
    cursor->line_end = end < cursor->pos ? cursor->pos : end;
}

void amark_cursor_init(amark_cursor_t *cursor, const amark_document_t *doc,
                       size_t start, size_t limit)
{
    size_t unused;

    cursor->doc = doc;
    cursor->bytes = amark_document_content(doc);
    cursor->limit = limit;
    cursor->pos = start;
    amark_document_range_lines(doc, start, start, &cursor->line, &unused);
    cursor->line_start = start;
    set_line_end(cursor);
}

bool amark_cursor_at_end(const amark_cursor_t *cursor)
{
    return cursor->pos == cursor->limit;
}

bool amark_cursor_at_line_end(const amark_cursor_t *cursor)
{
    return cursor->pos == cursor->line_end;
}

size_t amark_cursor_line_break_end(const amark_cursor_t *cursor)
{
    amark_line_bounds_t bounds;

    if (amark_line_bounds(cursor->doc, cursor->line, &bounds) &&
        bounds.end > cursor->line_end) {
        return bounds.end;
    }
    return cursor->line_end;
}

void amark_cursor_next_line(amark_cursor_t *cursor)
{
    amark_line_record_t record;
    size_t pos = amark_cursor_line_break_end(cursor);

    cursor->line++;
    /*
     * The record of the next line tells where its content starts, past the
     * container prefixes and the leading white space.
     */
    if (amark_line_get(cursor->doc, cursor->line, &record) &&
        record.content_start > pos) {
        pos = record.content_start;
    }
    if (pos > cursor->limit) {
        pos = cursor->limit;
    }
    cursor->pos = pos;
    cursor->line_start = pos;
    set_line_end(cursor);
}

void amark_cursor_seek(amark_cursor_t *cursor, size_t pos)
{
    while (pos > cursor->line_end && cursor->line_end < cursor->limit) {
        cursor->pos = cursor->line_end;
        amark_cursor_next_line(cursor);
    }
    cursor->pos = pos < cursor->line_end ? pos : cursor->line_end;
}

/* Returns codepoint, with U+0000 turned into U+FFFD. */
static uint32_t no_nul(uint32_t codepoint)
{
    return codepoint == 0 ? 0xFFFD : codepoint;
}

uint32_t amark_cursor_char_before(const amark_cursor_t *cursor, size_t pos)
{
    size_t start = pos - 1;
    size_t length;
    uint32_t codepoint;

    if (pos <= cursor->line_start) {
        return 0x0A;
    }
    /*
     * Step back over at most MAX_CONTINUATION continuation bytes to where
     * the character may start, without leaving the line's content.
     */
    while (start > cursor->line_start && pos - start <= MAX_CONTINUATION &&
           is_continuation(cursor->bytes[start])) {
        start--;
    }
    length = amark_utf8_decode_lossy(cursor->bytes + start,
                                     cursor->line_end - start, &codepoint);
    if (start + length != pos) {
        return 0xFFFD;
    }
    return no_nul(codepoint);
}

uint32_t amark_cursor_char_at(const amark_cursor_t *cursor, size_t pos)
{
    uint32_t codepoint;

    if (pos >= cursor->line_end) {
        return 0x0A;
    }
    (void)amark_utf8_decode_lossy(cursor->bytes + pos,
                                  cursor->line_end - pos, &codepoint);
    return no_nul(codepoint);
}
