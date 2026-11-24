/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_INLINE_SCAN_H
#define AMARK_INLINE_SCAN_H

#include <stdbool.h>
#include <stddef.h>

#include <amark/error.h>

#include "arena.h"
#include "array.h"
#include "cursor.h"

/*
 * The inline scan: functions that read the shape of one inline construct
 * from a cursor (src/cursor.h) and tell where it ends. They make no items
 * and no nodes, and never change the document or its content.
 *
 * Each function is called with the cursor on the first byte of the
 * construct. If the construct is there, it moves the cursor right after it
 * and reports a match; if not, it leaves the cursor where it was. A
 * construct that may cross lines is read through the cursor, so only the
 * content of the lines is read, never a container prefix, the leading white
 * space of a line or a line ending. None of the functions recurses or keeps
 * global state.
 */

/*
 * What keeps the scan linear in the length of the content. Kept by the
 * inline parser for the whole parse; the backticks array lives in the
 * scratch arena.
 */
typedef struct amark_inline_scan_memo {
    amark_array_t backticks; /* size_t, indexed by the length of a backtick
                                string: the start of the last string of that
                                length seen */
    bool backticks_complete; /* this content was searched to its end */
    unsigned html_no_end;    /* comments, processing instructions,
                                declarations and CDATA sections known to have
                                no end in the rest of this content */
} amark_inline_scan_memo_t;

/*
 * Initializes memo with an empty backticks array in scratch. Allocates
 * nothing.
 */
void amark_inline_scan_memo_init(amark_inline_scan_memo_t *memo,
                                 amark_arena_t *scratch);

/*
 * Starts a new inline content: clears the marks of the previous one. The
 * backticks array is kept: its positions are offsets in the document's
 * content, so those of an earlier content are below every position of this
 * one and read as "no string there". The contents must therefore be scanned
 * in increasing order of their start.
 */
void amark_inline_scan_memo_begin(amark_inline_scan_memo_t *memo);

/*
 * Looks for the end of a code span. The cursor is on the first backtick of
 * the opening string, which is open_length backticks long (1 or more; no
 * upper bound). The closing string is the next string of exactly
 * open_length backticks, not preceded or followed by a backtick, in the
 * content of this line or the following lines up to the end of the range.
 *
 * If it is found, sets *found to true and moves the cursor right after the
 * closing string. Otherwise sets *found to false and leaves the cursor where
 * it was. Once the rest of the content has been searched to its end, a
 * length known to have no string after the cursor is answered without
 * searching again.
 *
 * Fails only when the backticks array cannot grow: returns
 * AMARK_ERROR_NOMEM or AMARK_ERROR_LIMIT_EXCEEDED, sets *found to false and
 * leaves the cursor where it was.
 */
amark_error_t amark_inline_scan_code_span(amark_cursor_t *cursor,
                                          amark_inline_scan_memo_t *memo,
                                          size_t open_length, bool *found);

/*
 * Reads an entity or numeric character reference within the current line:
 * '&', then one of
 *   - '#', 1 to 7 decimal digits,
 *   - '#', 'x' or 'X', 1 to 6 hexadecimal digits,
 *   - an ASCII alphanumeric name that amark_entity_lookup knows,
 * then ';'. The value of a numeric reference is not checked (0, surrogates
 * and values over U+10FFFF match too). The cursor is on the '&'. On a
 * match, moves the cursor right after the ';' and returns true; otherwise
 * returns false and leaves the cursor where it was. Reads at most the
 * alphanumeric run after the '&' (and the '#', the 'x' and the ';').
 */
bool amark_inline_scan_entity(amark_cursor_t *cursor);

#endif /* AMARK_INLINE_SCAN_H */
