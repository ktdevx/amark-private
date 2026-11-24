/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_LINE_H
#define AMARK_LINE_H

#include <stdbool.h>
#include <stddef.h>

#include "document.h"
#include "parse_data.h"

/*
 * The line records. The parse data of a document holds one record
 * (amark_line_record_t, src/parse_data.h) for each line the parsed range
 * covers, from first_line to first_line + line_count - 1; lines are numbered
 * from 1 at the first line of the content. amark_parse_data_create sets
 * every record to (the end of the line's content, pad 0); the block parser
 * then writes where the content of the innermost block starts on each line.
 *
 * When pad is not 0, the byte at content_start is a tab, and the logical
 * content of the line is pad spaces followed by the bytes from
 * content_start + 1 to the end of the line's content. When pad is 0, it is
 * the bytes from content_start to the end of the line's content.
 *
 * Every function here takes a line number, ends in a time that does not
 * depend on the number of lines, and allocates nothing.
 */

/*
 * The bounds of a line of the parsed range, as offsets from the start of the
 * content. start <= content_end <= end.
 */
typedef struct amark_line_bounds {
    size_t start;       /* the first byte of the line; not before the start
                           of the range */
    size_t content_end; /* right before the line ending; end if the line has
                           none */
    size_t end;         /* the first byte of the next line; not after the end
                           of the range */
} amark_line_bounds_t;

/*
 * Stores in *out the bounds of line of doc and returns true. The line comes
 * from the line index of the document and is cut to the parsed range; the
 * line ending is found by looking only at bytes inside the range, so a CR
 * that is the last byte of the range is a line ending even if an LF follows
 * outside it, and an LF that is the first byte of the range is a line ending
 * of its own.
 *
 * Returns false and leaves *out unchanged when doc has no parse data or the
 * range does not cover line.
 */
bool amark_line_bounds(const amark_document_t *doc, size_t line,
                       amark_line_bounds_t *out);

/*
 * Stores in *out the record of line of doc and returns true. Returns false
 * and leaves *out unchanged when doc has no parse data or the range does not
 * cover line.
 */
bool amark_line_get(const amark_document_t *doc, size_t line,
                    amark_line_record_t *out);

/*
 * Sets the record of line of doc to (content_start, pad). Only the block
 * parser writes records. Preconditions: doc has parse data, the range covers
 * line, start <= content_start <= content_end of the line's bounds, and pad
 * is 0 to 3. A line the range does not cover is left alone.
 */
void amark_line_set(amark_document_t *doc, size_t line,
                    size_t content_start, unsigned pad);

#endif /* AMARK_LINE_H */
