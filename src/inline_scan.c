/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#include "inline_scan.h"

#include "entity.h"

/* The most digits of a decimal and a hexadecimal character reference. */
#define MAX_DECIMAL_DIGITS 7
#define MAX_HEX_DIGITS 6

static bool is_digit(unsigned char c)
{
    return c >= '0' && c <= '9';
}

static bool is_hex_digit(unsigned char c)
{
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static bool is_alnum(unsigned char c)
{
    return is_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
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
