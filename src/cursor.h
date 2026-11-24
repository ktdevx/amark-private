/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_CURSOR_H
#define AMARK_CURSOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "document.h"

/*
 * The cursor: a position in an inline content (a range the block parser
 * prepared, see amark_inline_content_t in src/parse_data.h) that walks the
 * content of the lines only. The position is always inside the content of a
 * line or at its end; it never stops in a line ending, a container prefix or
 * the leading white space of a line. On the first line the content starts
 * at the start of the range; on each later line it starts at the
 * content_start of the line record (src/line.h). On the last line it ends
 * at the end of the range; on the others, right before the line ending.
 *
 * This is the rule amark_label_scan (src/definition.c) and the link
 * reference definitions (src/refdef.c) follow: at the end of a line's
 * content, go on at the content_start of the next line.
 *
 * A cursor is a value: copy it with an assignment to keep a position, and
 * assign the copy back to return to it. No function here allocates, fails,
 * recurses or changes the document or its content.
 */
typedef struct amark_cursor {
    const amark_document_t *doc;
    const unsigned char *bytes; /* the content */
    size_t limit;      /* the end of the range */
    size_t pos;        /* the position; line_start <= pos <= line_end */
    size_t line;       /* the line that holds pos */
    size_t line_start; /* the start of the line's content (the start of the
                          range on the first line) */
    size_t line_end;   /* the end of the line's content; limit on the last
                          line */
} amark_cursor_t;

/*
 * Places cursor at start, in the range [start, limit) of the content of
 * doc. Preconditions: start <= limit, the range is an inline content the
 * block parser prepared, and doc has parse data with the line records of the
 * range written.
 */
void amark_cursor_init(amark_cursor_t *cursor, const amark_document_t *doc,
                       size_t start, size_t limit);

/* Returns true if the position is the end of the range (pos == limit). */
bool amark_cursor_at_end(const amark_cursor_t *cursor);

/*
 * Returns true if the position is the end of the line's content
 * (pos == line_end). If the cursor is not also at the end of the range, a
 * line ending follows.
 */
bool amark_cursor_at_line_end(const amark_cursor_t *cursor);

/*
 * Returns the position right after the line ending of the current line (the
 * first byte of the next line), whether it is LF, CR or CR LF. On the last
 * line of the parsed range without a line ending, returns the end of that
 * line's content.
 */
size_t amark_cursor_line_break_end(const amark_cursor_t *cursor);

/*
 * Moves to the start of the content of the next line (the content_start of
 * its record). Precondition: amark_cursor_at_line_end is true and
 * amark_cursor_at_end is false.
 */
void amark_cursor_next_line(amark_cursor_t *cursor);

/*
 * Moves forward to pos, going through the lines in order, so it takes time
 * proportional to the number of lines passed. Precondition: pos is at or
 * after the current position, at most limit, and inside the content of a
 * line or at its end.
 */
void amark_cursor_seek(amark_cursor_t *cursor, size_t pos);

/*
 * Returns the character right before pos, or U+000A if pos is the start of
 * the line's content. Steps back over at most 3 continuation bytes and
 * decodes from there with amark_utf8_decode_lossy; if what it decodes does
 * not end exactly at pos, returns U+FFFD. Precondition: line_start <= pos <=
 * line_end.
 */
uint32_t amark_cursor_char_before(const amark_cursor_t *cursor, size_t pos);

/*
 * Returns the character at pos, decoded with amark_utf8_decode_lossy up to
 * line_end, or U+000A if pos is the end of the line's content.
 * Precondition: line_start <= pos <= line_end.
 *
 * Both functions return an ill-formed sequence and U+0000 as U+FFFD. As the
 * start and the end of the range are the start and the end of a line's
 * content, they read as U+000A there, which is white space.
 */
uint32_t amark_cursor_char_at(const amark_cursor_t *cursor, size_t pos);

#endif /* AMARK_CURSOR_H */
