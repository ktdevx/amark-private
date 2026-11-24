/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_TEXT_LINES_H
#define AMARK_TEXT_LINES_H

#include <stdbool.h>
#include <stddef.h>

#include "document.h"

/*
 * The lines of a range: splits a range [start, end) of the content of a
 * parsed document into one segment for each line it covers. A segment is the
 * part of the line's content that is inside the range, so container prefixes
 * and the indentation a block rule removes, which lie before the
 * content_start of the line record (src/line.h), are never part of it.
 *
 * On the first line the segment starts at start; on each later line it
 * starts at the content_start of the line record. It ends at end or at the
 * end of the line's content, whichever comes first, and never before it
 * starts. Lazy continuation lines follow the same rule, as their records are
 * written the same way.
 *
 * Nothing here allocates, fails, recurses or changes the document; each call
 * ends in a time that does not depend on the number of lines.
 */

typedef struct amark_text_segment {
    size_t start;  /* the segment [start, end), inside the line's content */
    size_t end;
    unsigned pad;  /* 0 to 3; if not 0, the byte at start is a tab, to be
                      read as this many spaces */
    bool line_end; /* the line ending of this line is inside the range */
} amark_text_segment_t;

typedef struct amark_text_lines {
    const amark_document_t *doc;
    size_t start; /* the range */
    size_t end;
    size_t line;  /* the line of the next segment */
    bool done;
} amark_text_lines_t;

/*
 * Starts reading [start, end) of doc from first_line, the line that holds
 * start. Preconditions: start <= end; the range is inside the parsed range of
 * doc, which has parse data; first_line is the line that holds start; and
 * the range neither starts nor ends between the CR and the LF of a line
 * ending. An empty range (start == end) gives no segment.
 */
void amark_text_lines_init(amark_text_lines_t *lines,
                           const amark_document_t *doc,
                           size_t start, size_t end, size_t first_line);

/*
 * Stores the next segment in *out and returns true, or returns false and
 * leaves *out unchanged when there is none left. For a line L with bounds
 * (ls, ce, le) (start, end of content, start of the next line) and record
 * (cs, pad):
 *   - the segment starts at a = start on the first line, cs on later lines;
 *   - it ends at b = min(end, ce), but not before a;
 *   - pad is the record's pad only when a == cs and a < b, 0 otherwise;
 *   - line_end is true when end > ce;
 *   - the range ends on L when end <= le; otherwise the next call reads the
 *     next line.
 */
bool amark_text_lines_next(amark_text_lines_t *lines,
                           amark_text_segment_t *out);

#endif /* AMARK_TEXT_LINES_H */
