/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#include "text_lines.h"

#include "line.h"
#include "parse_data.h"

void amark_text_lines_init(amark_text_lines_t *lines,
                           const amark_document_t *doc,
                           size_t start, size_t end, size_t first_line)
{
    lines->doc = doc;
    lines->start = start;
    lines->end = end;
    lines->line = first_line;
    lines->done = start >= end;
}

bool amark_text_lines_next(amark_text_lines_t *lines,
                           amark_text_segment_t *out)
{
    amark_line_bounds_t bounds;
    amark_line_record_t record;
    size_t a;
    size_t b;

    if (lines->done) {
        return false;
    }
    if (!amark_line_bounds(lines->doc, lines->line, &bounds)
        || !amark_line_get(lines->doc, lines->line, &record)) {
        /* Not reached when the preconditions hold. */
        lines->done = true;
        return false;
    }

    /*
     * The range starts on the first line, so start is at or after the start
     * of the first line and before the start of every later one. On the
     * first line the segment starts at start, not at the record: the record
     * of an ATX heading's line points after the '#' run, and the range of
     * the heading starts at the '#'.
     */
    a = lines->start >= bounds.start ? lines->start : record.content_start;
    b = lines->end < bounds.content_end ? lines->end : bounds.content_end;
    if (b < a) {
        b = a;
    }

    out->start = a;
    out->end = b;
    out->pad = (a == record.content_start && a < b) ? record.pad : 0;
    out->line_end = lines->end > bounds.content_end;

    if (lines->end <= bounds.end) {
        lines->done = true;
    } else {
        lines->line++;
    }
    return true;
}
