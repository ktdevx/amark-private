/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_INLINE_SCAN_H
#define AMARK_INLINE_SCAN_H

#include <stdbool.h>
#include <stddef.h>

#include <amark/error.h>
#include <amark/tree.h>

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
                                no end in the rest of this content: the
                                AMARK_INLINE_SCAN_NO_END_* bits */
} amark_inline_scan_memo_t;

/* The bits of html_no_end, one for each kind of raw HTML that has an end. */
#define AMARK_INLINE_SCAN_NO_END_COMMENT 0x1u     /* "-->" */
#define AMARK_INLINE_SCAN_NO_END_PI 0x2u          /* "?>" */
#define AMARK_INLINE_SCAN_NO_END_DECLARATION 0x4u /* ">" */
#define AMARK_INLINE_SCAN_NO_END_CDATA 0x8u       /* "]]>" */

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

/*
 * Reads an autolink within the current line (CommonMark 0.31.2, section
 * 6.5): '<', then either
 *   - an absolute URI: a scheme of 2 to 32 characters (an ASCII letter, then
 *     ASCII letters, digits, '+', '.' and '-'), ':', and any bytes but the
 *     space, '<', '>' and the ASCII control characters, or
 *   - an email address: one or more of the ASCII alphanumerics and
 *     .!#$%&'*+/=?^_`{|}~-, '@', and labels of 1 to 63 ASCII alphanumerics
 *     and '-' that neither start nor end with '-', separated by '.',
 * then '>'. The URI is tried first. The cursor is on the '<'. On a match,
 * sets *type to AMARK_AUTOLINK_URI or AMARK_AUTOLINK_EMAIL, moves the
 * cursor right after the '>' and returns true; otherwise returns false and
 * changes neither. Reads no further than the next '<', '>', space or ASCII
 * control character of the line.
 */
bool amark_inline_scan_autolink(amark_cursor_t *cursor,
                                amark_autolink_type_t *type);

/*
 * Reads raw HTML (CommonMark 0.31.2, section 6.6), one of
 *   - an open tag: '<', a tag name, attributes each after white space,
 *     optional white space, an optional '/', and '>',
 *   - a closing tag: "</", a tag name, optional white space, and '>',
 *   - a comment: "<!-->", "<!--->", or "<!--" up to the first "-->",
 *   - a processing instruction: "<?" up to the first "?>",
 *   - a declaration: "<!" and an ASCII letter up to the first '>',
 *   - a CDATA section: "<![CDATA[" up to the first "]]>".
 * A tag name is an ASCII letter followed by ASCII letters, digits and '-';
 * an attribute is a name (an ASCII letter, '_' or ':', then ASCII letters,
 * digits, '_', '.', ':' and '-') and an optional value specification
 * (optional white space, '=', optional white space, and a value). A value is
 * quoted with '"' or '\'', or a nonempty run of bytes other than white
 * space, '"', '\'', '=', '<', '>' and '`'. White space in a tag is spaces,
 * tabs and at most one line ending.
 *
 * The construct may cross lines (between attributes, in a quoted value and
 * in the four kinds with an end string); it is read through the cursor, so
 * only the content of the lines counts and an end string never spans a line
 * ending. The cursor is on the '<'. On a match, moves the cursor right after
 * the last '>' and returns true; otherwise returns false and leaves the
 * cursor where it was.
 *
 * When a search for the end of a comment, a processing instruction, a
 * declaration or a CDATA section reaches the end of the content, sets the
 * bit of that kind in memo->html_no_end; while the bit is set, that kind is
 * answered "no" without searching. The scans of one content must therefore
 * be made in increasing order of position.
 */
bool amark_inline_scan_html(amark_cursor_t *cursor,
                            amark_inline_scan_memo_t *memo);

/*
 * The part of an inline link or image after its "]": the ranges of its
 * destination and title. A range is an offset range in the document's
 * content; one that is absent (has_destination or has_title false) is
 * {0, 0}.
 */
typedef struct amark_link_tail {
    bool has_destination;
    amark_range_t destination; /* the inside of <...>, or the whole raw
                                  destination; empty for "<>" */
    bool has_title;
    amark_range_t title;       /* the inside of the delimiters; empty for an
                                  empty title */
} amark_link_tail_t;

/*
 * Reads the part of an inline link after its "]" (CommonMark 0.31.2,
 * section 6.3): '(', optional white space, an optional destination, an
 * optional title that must follow white space, optional white space, and
 * ')'. White space is spaces, tabs and at most one line ending.
 *
 *   - Right after the '(' and its white space, a ')' means neither a
 *     destination nor a title. Anything else is read as a destination, so
 *     in "(\"t\")" the "\"t\"" is a destination, not a title.
 *   - A destination is '<', bytes other than a line ending and an
 *     unescaped '<', and '>'; or a nonempty run, not starting with '<', of
 *     bytes other than the space and the ASCII control characters (line
 *     endings included) whose unescaped parentheses balance, nested at most
 *     AMARK_LINK_DEST_PAREN_MAX deep. An unmatched ')' ends it.
 *   - A title is "...", '...' or (...), up to the first unescaped closing
 *     delimiter; (...) holds no unescaped '('. It may cross lines.
 *
 * A backslash before an ASCII punctuation character only keeps that
 * character from counting as a delimiter; character references are not
 * read. The rules of the destination and the title are those of the link
 * reference definitions (read_destination and read_title in src/refdef.c);
 * when one of them changes, change both.
 *
 * The cursor is on the '('. Only the content of the lines is read. On a
 * match, writes *out, moves the cursor right after the ')' and returns
 * true; otherwise returns false and changes neither. An angle-bracketed
 * destination is read no further than the next '<' or line ending, a raw one
 * no further than its line or the AMARK_LINK_DEST_PAREN_MAX + 1st nested
 * '(', and a title no further than its next closing delimiter (or '(' for
 * a (...) title).
 */
bool amark_inline_scan_link_tail(amark_cursor_t *cursor,
                                 amark_link_tail_t *out);

/* The deepest nesting of parentheses in a raw link destination. */
#define AMARK_LINK_DEST_PAREN_MAX 32

#endif /* AMARK_INLINE_SCAN_H */
