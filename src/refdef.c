/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#include "refdef.h"

#include "definition.h"
#include "line.h"

/*
 * A position in a definition: the next byte to read, the end of what may be
 * read, and the line that holds the next byte.
 */
typedef struct refdef_cursor {
    const amark_document_t *doc;
    const unsigned char *bytes;
    size_t pos;
    size_t limit;
    size_t line;
} refdef_cursor_t;

static bool is_line_ending(unsigned char c)
{
    return c == '\n' || c == '\r';
}

static bool is_blank(unsigned char c)
{
    return c == ' ' || c == '\t';
}

/* ASCII punctuation as CommonMark defines it: what a backslash escapes. */
static bool is_ascii_punctuation(unsigned char c)
{
    return (c >= 0x21 && c <= 0x2F) || (c >= 0x3A && c <= 0x40) ||
           (c >= 0x5B && c <= 0x60) || (c >= 0x7B && c <= 0x7E);
}

/* Whether the cursor is at the end of its line (a line ending or limit). */
static bool at_line_end(const refdef_cursor_t *cursor)
{
    return cursor->pos >= cursor->limit ||
           is_line_ending(cursor->bytes[cursor->pos]);
}

/* Whether a backslash at the cursor escapes the byte after it. */
static bool at_escape(const refdef_cursor_t *cursor)
{
    return cursor->bytes[cursor->pos] == '\\' &&
           cursor->pos + 1 < cursor->limit &&
           is_ascii_punctuation(cursor->bytes[cursor->pos + 1]);
}

static void skip_blanks(refdef_cursor_t *cursor)
{
    while (cursor->pos < cursor->limit &&
           is_blank(cursor->bytes[cursor->pos])) {
        cursor->pos++;
    }
}

/*
 * Steps over the line ending at the cursor (CR LF, LF or CR) and then up to
 * the content_start of the next line's record, if it is past the line
 * ending. Never moves past the limit.
 */
static void next_line(refdef_cursor_t *cursor)
{
    amark_line_record_t record;

    if (cursor->bytes[cursor->pos] == '\r' &&
        cursor->pos + 1 < cursor->limit &&
        cursor->bytes[cursor->pos + 1] == '\n') {
        cursor->pos += 2;
    } else {
        cursor->pos++;
    }
    cursor->line++;
    if (amark_line_get(cursor->doc, cursor->line, &record) &&
        record.content_start > cursor->pos) {
        cursor->pos = record.content_start < cursor->limit
                          ? record.content_start
                          : cursor->limit;
    }
}

/*
 * Skips spaces and tabs including up to one line ending. Returns whether it
 * skipped anything.
 */
static bool skip_space(refdef_cursor_t *cursor)
{
    size_t from = cursor->pos;

    skip_blanks(cursor);
    if (cursor->pos < cursor->limit &&
        is_line_ending(cursor->bytes[cursor->pos])) {
        next_line(cursor);
        skip_blanks(cursor);
    }
    return cursor->pos != from;
}

/*
 * Reads a link destination at the cursor: <...> without line endings and
 * unescaped '<' or '>', or a nonempty run of bytes that are neither spaces
 * nor ASCII control characters and whose unescaped parentheses balance.
 * Parentheses are counted, not nested. Writes the range and leaves the
 * cursor after the destination.
 */
static bool read_destination(refdef_cursor_t *cursor, amark_range_t *out)
{
    const unsigned char *bytes = cursor->bytes;
    size_t start = cursor->pos;
    size_t depth = 0;

    if (bytes[cursor->pos] == '<') {
        cursor->pos++;
        while (cursor->pos < cursor->limit) {
            unsigned char c = bytes[cursor->pos];

            if (c == '>') {
                out->start = start + 1;
                out->end = cursor->pos;
                cursor->pos++;
                return true;
            }
            if (c == '<' || is_line_ending(c)) {
                return false;
            }
            cursor->pos += at_escape(cursor) ? 2 : 1;
        }
        return false;
    }
    while (cursor->pos < cursor->limit) {
        unsigned char c = bytes[cursor->pos];

        if (c <= 0x20 || c == 0x7F) {
            break;
        }
        if (at_escape(cursor)) {
            cursor->pos += 2;
            continue;
        }
        if (c == '(') {
            depth++;
        } else if (c == ')') {
            if (depth == 0) {
                break;
            }
            depth--;
        }
        cursor->pos++;
    }
    if (cursor->pos == start || depth != 0) {
        return false;
    }
    out->start = start;
    out->end = cursor->pos;
    return true;
}

/*
 * Reads a link title at the cursor, delimited by "...", '...' or (...),
 * without a blank line and, for (...), without an unescaped '('. Writes the
 * inside of the delimiters and leaves the cursor after the closing one.
 */
static bool read_title(refdef_cursor_t *cursor, amark_range_t *out)
{
    const unsigned char *bytes = cursor->bytes;
    unsigned char open = bytes[cursor->pos];
    unsigned char close = open == '(' ? ')' : open;
    size_t start = cursor->pos + 1;
    bool line_has_content = true; /* the opening delimiter */

    cursor->pos++;
    while (cursor->pos < cursor->limit) {
        unsigned char c = bytes[cursor->pos];

        if (c == close) {
            out->start = start;
            out->end = cursor->pos;
            cursor->pos++;
            return true;
        }
        if (open == '(' && c == '(') {
            return false;
        }
        if (is_line_ending(c)) {
            if (!line_has_content) {
                return false; /* a blank line */
            }
            next_line(cursor);
            line_has_content = false;
            continue;
        }
        if (!is_blank(c)) {
            line_has_content = true;
        }
        cursor->pos += at_escape(cursor) ? 2 : 1;
    }
    return false;
}

bool amark_refdef_parse(const amark_document_t *doc, size_t start,
                        size_t limit, amark_refdef_t *out)
{
    refdef_cursor_t cursor;
    amark_refdef_t result;
    size_t unused;
    size_t after_dest;
    size_t dest_line;
    size_t dest_line_end;
    bool dest_ends_line;

    if (start >= limit || amark_document_content(doc)[start] != '[') {
        return false;
    }
    if (!amark_label_scan(doc, start, limit, &result.label.start,
                          &result.label.end)) {
        return false;
    }
    cursor.doc = doc;
    cursor.bytes = amark_document_content(doc);
    cursor.pos = result.label.end + 1;
    cursor.limit = limit;
    amark_document_range_lines(doc, result.label.end, result.label.end,
                               &cursor.line, &unused);
    if (cursor.pos >= limit || cursor.bytes[cursor.pos] != ':') {
        return false;
    }
    cursor.pos++;

    skip_space(&cursor);
    if (at_line_end(&cursor) || !read_destination(&cursor,
                                                  &result.destination)) {
        return false;
    }

    /* Where the definition ends if no title follows. */
    after_dest = cursor.pos;
    dest_line = cursor.line;
    skip_blanks(&cursor);
    dest_ends_line = at_line_end(&cursor);
    dest_line_end = cursor.pos;
    cursor.pos = after_dest;

    if (skip_space(&cursor) && cursor.pos < limit &&
        (cursor.bytes[cursor.pos] == '"' || cursor.bytes[cursor.pos] == '\'' ||
         cursor.bytes[cursor.pos] == '(') &&
        read_title(&cursor, &result.title)) {
        skip_blanks(&cursor);
        if (at_line_end(&cursor)) {
            result.has_title = true;
            result.last_line = cursor.line;
            result.end = cursor.pos;
            *out = result;
            return true;
        }
    }
    if (!dest_ends_line) {
        return false;
    }
    result.has_title = false;
    result.title.start = 0;
    result.title.end = 0;
    result.last_line = dest_line;
    result.end = dest_line_end;
    *out = result;
    return true;
}
