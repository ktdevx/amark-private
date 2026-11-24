/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#include "inline_scan.h"

#include "entity.h"

#include <string.h>

/* The most digits of a decimal and a hexadecimal character reference. */
#define MAX_DECIMAL_DIGITS 7
#define MAX_HEX_DIGITS 6

/* The shortest and the longest scheme of an absolute URI. */
#define MIN_SCHEME_LENGTH 2
#define MAX_SCHEME_LENGTH 32

/* The longest label of the domain of an email address. */
#define MAX_LABEL_LENGTH 63

static bool is_digit(unsigned char c)
{
    return c >= '0' && c <= '9';
}

static bool is_hex_digit(unsigned char c)
{
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static bool is_letter(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool is_alnum(unsigned char c)
{
    return is_digit(c) || is_letter(c);
}

void amark_inline_scan_memo_init(amark_inline_scan_memo_t *memo,
                                 amark_arena_t *scratch)
{
    amark_array_init(&memo->backticks, scratch, sizeof(size_t));
    memo->backticks_complete = false;
    memo->html_no_end = 0;
}

void amark_inline_scan_memo_begin(amark_inline_scan_memo_t *memo)
{
    memo->backticks_complete = false;
    memo->html_no_end = 0;
}

/*
 * Records that a string of length backticks starts at pos: grows the array
 * to hold the entry (the new entries are 0, below every position) and keeps
 * the larger of the entry and pos, so the entry stays the last string of
 * that length seen even when a later scan passes strings an earlier one has
 * already seen. On failure nothing changes.
 */
static amark_error_t record_backticks(amark_inline_scan_memo_t *memo,
                                      size_t length, size_t pos)
{
    size_t *entry;

    if (length >= amark_array_len(&memo->backticks)) {
        size_t zero = 0;
        amark_error_t err = amark_array_reserve(&memo->backticks,
                                                length + 1);

        if (err != AMARK_ERROR_NONE) {
            return err;
        }
        /* Within the capacity just reserved, so these cannot fail. */
        while (amark_array_len(&memo->backticks) <= length) {
            (void)amark_array_push(&memo->backticks, &zero);
        }
    }
    entry = (size_t *)amark_array_at(&memo->backticks, length);
    if (*entry < pos) {
        *entry = pos;
    }
    return AMARK_ERROR_NONE;
}

/*
 * The entry for length, or 0 (below every position) if the array does not
 * reach it.
 */
static size_t last_backticks(const amark_inline_scan_memo_t *memo,
                             size_t length)
{
    if (length >= amark_array_len(&memo->backticks)) {
        return 0;
    }
    return *(const size_t *)amark_array_at(&memo->backticks, length);
}

amark_error_t amark_inline_scan_code_span(amark_cursor_t *cursor,
                                          amark_inline_scan_memo_t *memo,
                                          size_t open_length, bool *found)
{
    amark_cursor_t scan = *cursor;

    *found = false;
    /*
     * Once a search has reached the end of this content, every string from
     * there on is in the memo, so an entry at or before the cursor means no
     * string of this length follows.
     */
    if (memo->backticks_complete &&
        last_backticks(memo, open_length) <= cursor->pos) {
        return AMARK_ERROR_NONE;
    }
    /* The opening string lies within this line. */
    scan.pos += open_length;
    for (;;) {
        size_t start;
        size_t length;
        amark_error_t err;

        while (scan.pos < scan.line_end && scan.bytes[scan.pos] != '`') {
            scan.pos++;
        }
        if (amark_cursor_at_line_end(&scan)) {
            if (amark_cursor_at_end(&scan)) {
                break;
            }
            amark_cursor_next_line(&scan);
            continue;
        }
        start = scan.pos;
        while (scan.pos < scan.line_end && scan.bytes[scan.pos] == '`') {
            scan.pos++;
        }
        length = scan.pos - start;
        err = record_backticks(memo, length, start);
        if (err != AMARK_ERROR_NONE) {
            return err;
        }
        if (length == open_length) {
            *cursor = scan;
            *found = true;
            return AMARK_ERROR_NONE;
        }
    }
    memo->backticks_complete = true;
    return AMARK_ERROR_NONE;
}

bool amark_inline_scan_entity(amark_cursor_t *cursor)
{
    const unsigned char *p = cursor->bytes;
    size_t end = cursor->line_end;
    size_t pos = cursor->pos + 1;
    size_t start;

    if (pos < end && p[pos] == '#') {
        size_t max = MAX_DECIMAL_DIGITS;
        bool hex = false;

        pos++;
        if (pos < end && (p[pos] == 'x' || p[pos] == 'X')) {
            hex = true;
            max = MAX_HEX_DIGITS;
            pos++;
        }
        start = pos;
        while (pos < end && pos - start < max &&
               (hex ? is_hex_digit(p[pos]) : is_digit(p[pos]))) {
            pos++;
        }
        if (pos == start || pos >= end || p[pos] != ';') {
            return false;
        }
    } else {
        const unsigned char *utf8;
        size_t utf8_length;

        start = pos;
        while (pos < end && is_alnum(p[pos])) {
            pos++;
        }
        if (pos == start || pos >= end || p[pos] != ';' ||
            !amark_entity_lookup(p + start, pos - start, &utf8,
                                 &utf8_length)) {
            return false;
        }
    }
    /* Right after the ';', still within this line. */
    cursor->pos = pos + 1;
    return true;
}

/* ---- autolinks ---------------------------------------------------------- */

/* The bytes of the scheme of an absolute URI after its first letter. */
static bool is_scheme_byte(unsigned char c)
{
    return is_alnum(c) || c == '+' || c == '.' || c == '-';
}

/* The bytes of the local part of an email address. */
static bool is_email_local_byte(unsigned char c)
{
    if (is_alnum(c)) {
        return true;
    }
    switch (c) {
    case '.': case '!': case '#': case '$': case '%': case '&': case '\'':
    case '*': case '+': case '/': case '=': case '?': case '^': case '_':
    case '`': case '{': case '|': case '}': case '~': case '-':
        return true;
    default:
        return false;
    }
}

/*
 * Returns the offset right after the '>' of an absolute URI autolink whose
 * '<' is at p[pos], or 0 if there is none before end.
 */
static size_t scan_uri_autolink(const unsigned char *p, size_t pos,
                                size_t end)
{
    size_t start = pos + 1;
    size_t i = start;

    if (i >= end || !is_letter(p[i])) {
        return 0;
    }
    i++;
    while (i < end && i - start < MAX_SCHEME_LENGTH && is_scheme_byte(p[i])) {
        i++;
    }
    if (i - start < MIN_SCHEME_LENGTH || i >= end || p[i] != ':') {
        return 0;
    }
    for (i++; i < end; i++) {
        unsigned char c = p[i];

        if (c == '>') {
            return i + 1;
        }
        if (c == ' ' || c == '<' || c < 0x20 || c == 0x7F) {
            return 0;
        }
    }
    return 0;
}

/*
 * Returns the offset right after the '>' of an email autolink whose '<' is
 * at p[pos], or 0 if there is none before end. A label is the whole run of
 * alphanumerics and '-' up to the next '.' or '>', since nothing else may
 * follow it.
 */
static size_t scan_email_autolink(const unsigned char *p, size_t pos,
                                  size_t end)
{
    size_t i = pos + 1;

    while (i < end && is_email_local_byte(p[i])) {
        i++;
    }
    if (i == pos + 1 || i >= end || p[i] != '@') {
        return 0;
    }
    for (;;) {
        size_t label = ++i;

        while (i < end && (is_alnum(p[i]) || p[i] == '-')) {
            i++;
        }
        if (i == label || i - label > MAX_LABEL_LENGTH ||
            p[label] == '-' || p[i - 1] == '-' || i >= end) {
            return 0;
        }
        if (p[i] == '>') {
            return i + 1;
        }
        if (p[i] != '.') {
            return 0;
        }
    }
}

bool amark_inline_scan_autolink(amark_cursor_t *cursor,
                                amark_autolink_type_t *type)
{
    size_t end = scan_uri_autolink(cursor->bytes, cursor->pos,
                                   cursor->line_end);

    if (end != 0) {
        *type = AMARK_AUTOLINK_URI;
    } else {
        end = scan_email_autolink(cursor->bytes, cursor->pos,
                                  cursor->line_end);
        if (end == 0) {
            return false;
        }
        *type = AMARK_AUTOLINK_EMAIL;
    }
    /* Within this line. */
    cursor->pos = end;
    return true;
}

/* ---- raw HTML ----------------------------------------------------------- */

/*
 * Returns the byte at the cursor, or -1 at the end of the line's content.
 */
static int byte_at(const amark_cursor_t *cursor)
{
    return cursor->pos < cursor->line_end ? cursor->bytes[cursor->pos] : -1;
}

/*
 * Returns true if the line's content holds the len bytes of s at the
 * cursor.
 */
static bool looking_at(const amark_cursor_t *cursor, const char *s,
                       size_t len)
{
    return cursor->line_end - cursor->pos >= len &&
           memcmp(cursor->bytes + cursor->pos, s, len) == 0;
}

/*
 * Skips the white space of a tag or a link tail: spaces, tabs and at most
 * one line ending. Returns true if it skipped any.
 */
static bool skip_white_space(amark_cursor_t *cursor)
{
    size_t pos = cursor->pos;
    size_t line = cursor->line;
    bool line_ending = false;

    for (;;) {
        while (cursor->pos < cursor->line_end &&
               (cursor->bytes[cursor->pos] == ' ' ||
                cursor->bytes[cursor->pos] == '\t')) {
            cursor->pos++;
        }
        if (line_ending || !amark_cursor_at_line_end(cursor) ||
            amark_cursor_at_end(cursor)) {
            break;
        }
        amark_cursor_next_line(cursor);
        line_ending = true;
    }
    return cursor->pos != pos || cursor->line != line;
}

/*
 * Skips a tag name: an ASCII letter followed by ASCII letters, digits and
 * '-'. Returns false, without moving, if none is there.
 */
static bool skip_tag_name(amark_cursor_t *cursor)
{
    const unsigned char *p = cursor->bytes;

    if (cursor->pos >= cursor->line_end || !is_letter(p[cursor->pos])) {
        return false;
    }
    cursor->pos++;
    while (cursor->pos < cursor->line_end &&
           (is_alnum(p[cursor->pos]) || p[cursor->pos] == '-')) {
        cursor->pos++;
    }
    return true;
}

/*
 * Skips an attribute name: an ASCII letter, '_' or ':', followed by ASCII
 * letters, digits, '_', '.', ':' and '-'. Returns false, without moving, if
 * none is there.
 */
static bool skip_attribute_name(amark_cursor_t *cursor)
{
    const unsigned char *p = cursor->bytes;
    int c = byte_at(cursor);

    if (!(c >= 0 && (is_letter((unsigned char)c) || c == '_' || c == ':'))) {
        return false;
    }
    cursor->pos++;
    while (cursor->pos < cursor->line_end &&
           (is_alnum(p[cursor->pos]) || p[cursor->pos] == '_' ||
            p[cursor->pos] == '.' || p[cursor->pos] == ':' ||
            p[cursor->pos] == '-')) {
        cursor->pos++;
    }
    return true;
}

/*
 * Skips an attribute value: a string quoted with '"' or '\'', which may
 * cross lines, or a nonempty run, within the line, of bytes other than
 * white space, '"', '\'', '=', '<', '>' and '`'. Returns false if none is
 * there; the cursor may then have moved.
 */
static bool skip_attribute_value(amark_cursor_t *cursor)
{
    const unsigned char *p = cursor->bytes;
    int quote = byte_at(cursor);
    size_t start = cursor->pos;

    if (quote == '"' || quote == '\'') {
        cursor->pos++;
        for (;;) {
            const void *found = memchr(p + cursor->pos, quote,
                                       cursor->line_end - cursor->pos);

            if (found != NULL) {
                cursor->pos = (size_t)((const unsigned char *)found - p) + 1;
                return true;
            }
            cursor->pos = cursor->line_end;
            if (amark_cursor_at_end(cursor)) {
                return false;
            }
            amark_cursor_next_line(cursor);
        }
    }
    while (cursor->pos < cursor->line_end) {
        unsigned char c = p[cursor->pos];

        if (c == ' ' || c == '\t' || c == '"' || c == '\'' || c == '=' ||
            c == '<' || c == '>' || c == '`') {
            break;
        }
        cursor->pos++;
    }
    return cursor->pos != start;
}

/*
 * The open tag and the closing tag. The grammar of the tag name, the
 * attribute name and the attribute value is the one of start condition 7 of
 * the HTML blocks, scan_open_tag and scan_closing_tag in src/scan.c; when
 * one of them changes, change both. Here the white space may also hold one
 * line ending, a quoted value may cross lines, and the tag may be followed
 * by anything.
 *
 * Each reads from a copy of the cursor at the '<' and, on a match, leaves
 * the copy right after the '>'. Returns false otherwise.
 */
static bool scan_open_tag(amark_cursor_t *scan)
{
    scan->pos++;
    if (!skip_tag_name(scan)) {
        return false;
    }
    for (;;) {
        amark_cursor_t attribute = *scan;
        amark_cursor_t value;

        if (!skip_white_space(&attribute) || !skip_attribute_name(&attribute)) {
            break;
        }
        *scan = attribute;
        value = attribute;
        skip_white_space(&value);
        if (byte_at(&value) == '=') {
            value.pos++;
            skip_white_space(&value);
            if (!skip_attribute_value(&value)) {
                return false;
            }
            *scan = value;
        }
    }
    skip_white_space(scan);
    if (byte_at(scan) == '/') {
        scan->pos++;
    }
    if (byte_at(scan) != '>') {
        return false;
    }
    scan->pos++;
    return true;
}

static bool scan_closing_tag(amark_cursor_t *scan)
{
    scan->pos += 2;
    if (!skip_tag_name(scan)) {
        return false;
    }
    skip_white_space(scan);
    if (byte_at(scan) != '>') {
        return false;
    }
    scan->pos++;
    return true;
}

/*
 * Searches the content of this line and the following lines, from the
 * cursor, for the len bytes of s, which never span a line ending. If found,
 * moves the cursor right after them and returns true. Otherwise, sets the
 * bit no_end in memo->html_no_end and returns false; the cursor may then
 * have moved.
 */
static bool search_end(amark_cursor_t *scan, amark_inline_scan_memo_t *memo,
                       const char *s, size_t len, unsigned no_end)
{
    const unsigned char *p = scan->bytes;

    for (;;) {
        size_t i = scan->pos;

        while (scan->line_end - i >= len) {
            const unsigned char *found = (const unsigned char *)memchr(
                p + i, s[0], scan->line_end - i - (len - 1));

            if (found == NULL) {
                break;
            }
            i = (size_t)(found - p);
            if (memcmp(p + i, s, len) == 0) {
                scan->pos = i + len;
                return true;
            }
            i++;
        }
        scan->pos = scan->line_end;
        if (amark_cursor_at_end(scan)) {
            memo->html_no_end |= no_end;
            return false;
        }
        amark_cursor_next_line(scan);
    }
}

/*
 * The four kinds with an end string. Each is called on a copy of the cursor
 * right after the opening string and answers "no" without searching once
 * memo knows there is no end in the rest of the content.
 */
static bool scan_to_end(amark_cursor_t *scan, amark_inline_scan_memo_t *memo,
                        const char *s, size_t len, unsigned no_end)
{
    if ((memo->html_no_end & no_end) != 0) {
        return false;
    }
    return search_end(scan, memo, s, len, no_end);
}

bool amark_inline_scan_html(amark_cursor_t *cursor,
                            amark_inline_scan_memo_t *memo)
{
    amark_cursor_t scan = *cursor;
    bool found;

    scan.pos++;
    if (looking_at(&scan, "!--", 3)) {
        scan.pos += 3;
        /* "<!-->" and "<!--->" are comments of their own. */
        if (looking_at(&scan, ">", 1)) {
            scan.pos += 1;
            found = true;
        } else if (looking_at(&scan, "->", 2)) {
            scan.pos += 2;
            found = true;
        } else {
            found = scan_to_end(&scan, memo, "-->", 3,
                                AMARK_INLINE_SCAN_NO_END_COMMENT);
        }
    } else if (looking_at(&scan, "?", 1)) {
        scan.pos += 1;
        found = scan_to_end(&scan, memo, "?>", 2,
                            AMARK_INLINE_SCAN_NO_END_PI);
    } else if (looking_at(&scan, "![CDATA[", 8)) {
        scan.pos += 8;
        found = scan_to_end(&scan, memo, "]]>", 3,
                            AMARK_INLINE_SCAN_NO_END_CDATA);
    } else if (looking_at(&scan, "!", 1) && scan.pos + 1 < scan.line_end &&
               is_letter(scan.bytes[scan.pos + 1])) {
        scan.pos += 2;
        found = scan_to_end(&scan, memo, ">", 1,
                            AMARK_INLINE_SCAN_NO_END_DECLARATION);
    } else if (looking_at(&scan, "/", 1)) {
        scan = *cursor;
        found = scan_closing_tag(&scan);
    } else {
        scan = *cursor;
        found = scan_open_tag(&scan);
    }
    if (found) {
        *cursor = scan;
    }
    return found;
}

/* ---- link tails --------------------------------------------------------- */

/* ASCII punctuation as CommonMark defines it: what a backslash escapes. */
static bool is_ascii_punctuation(unsigned char c)
{
    return (c >= 0x21 && c <= 0x2F) || (c >= 0x3A && c <= 0x40) ||
           (c >= 0x5B && c <= 0x60) || (c >= 0x7B && c <= 0x7E);
}

/*
 * Whether the byte at the cursor, which is within the line, is a backslash
 * escaping the next byte of the line.
 */
static bool at_escape(const amark_cursor_t *cursor)
{
    return cursor->bytes[cursor->pos] == '\\' &&
           cursor->pos + 1 < cursor->line_end &&
           is_ascii_punctuation(cursor->bytes[cursor->pos + 1]);
}

/*
 * The destination and the title of a link tail. Their rules are those of
 * the link reference definitions, read_destination and read_title in
 * src/refdef.c; when one of them changes, change both. Here a raw
 * destination nests its parentheses at most AMARK_LINK_DEST_PAREN_MAX deep,
 * and a title needs no check for a blank line (an inline content has
 * none).
 *
 * Each reads from a copy of the cursor at its first byte and, on a match,
 * writes the range and leaves the copy right after it. Returns false
 * otherwise; the copy may then have moved.
 */
static bool read_link_destination(amark_cursor_t *scan, amark_range_t *out)
{
    const unsigned char *p = scan->bytes;
    size_t start = scan->pos;
    size_t depth = 0;

    if (byte_at(scan) == '<') {
        scan->pos++;
        while (scan->pos < scan->line_end) {
            unsigned char c = p[scan->pos];

            if (c == '>') {
                out->start = start + 1;
                out->end = scan->pos;
                scan->pos++;
                return true;
            }
            if (c == '<') {
                return false;
            }
            scan->pos += at_escape(scan) ? 2 : 1;
        }
        return false; /* a line ending or the end of the content */
    }
    while (scan->pos < scan->line_end) {
        unsigned char c = p[scan->pos];

        if (c <= 0x20 || c == 0x7F) {
            break;
        }
        if (at_escape(scan)) {
            scan->pos += 2;
            continue;
        }
        if (c == '(') {
            if (++depth > AMARK_LINK_DEST_PAREN_MAX) {
                return false;
            }
        } else if (c == ')') {
            if (depth == 0) {
                break;
            }
            depth--;
        }
        scan->pos++;
    }
    if (scan->pos == start || depth != 0) {
        return false;
    }
    out->start = start;
    out->end = scan->pos;
    return true;
}

static bool read_link_title(amark_cursor_t *scan, amark_range_t *out)
{
    const unsigned char *p = scan->bytes;
    unsigned char open = p[scan->pos];
    unsigned char close = open == '(' ? ')' : open;
    size_t start = scan->pos + 1;

    scan->pos++;
    for (;;) {
        while (scan->pos < scan->line_end) {
            unsigned char c = p[scan->pos];

            if (c == close) {
                out->start = start;
                out->end = scan->pos;
                scan->pos++;
                return true;
            }
            if (open == '(' && c == '(') {
                return false;
            }
            scan->pos += at_escape(scan) ? 2 : 1;
        }
        if (amark_cursor_at_end(scan)) {
            return false;
        }
        amark_cursor_next_line(scan);
    }
}

bool amark_inline_scan_link_tail(amark_cursor_t *cursor,
                                 amark_link_tail_t *out)
{
    amark_cursor_t scan = *cursor;
    amark_link_tail_t tail;
    int c;

    tail.has_destination = false;
    tail.destination.start = 0;
    tail.destination.end = 0;
    tail.has_title = false;
    tail.title.start = 0;
    tail.title.end = 0;

    scan.pos++; /* the '(' */
    skip_white_space(&scan);
    if (byte_at(&scan) != ')') {
        if (!read_link_destination(&scan, &tail.destination)) {
            return false;
        }
        tail.has_destination = true;
        /* A title only follows white space. */
        if (skip_white_space(&scan)) {
            c = byte_at(&scan);
            if (c == '"' || c == '\'' || c == '(') {
                if (!read_link_title(&scan, &tail.title)) {
                    return false;
                }
                tail.has_title = true;
                skip_white_space(&scan);
            }
        }
        if (byte_at(&scan) != ')') {
            return false;
        }
    }
    scan.pos++; /* the ')', within this line */
    *cursor = scan;
    *out = tail;
    return true;
}
