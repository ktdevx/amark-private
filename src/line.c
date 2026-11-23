/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#include "line.h"

#include "line_index.h"

/* Returns whether data is not NULL and its range covers line. */
static bool covers(const amark_parse_data_t *data, size_t line)
{
    return data != NULL && line >= data->first_line &&
           line - data->first_line < data->line_count;
}

bool amark_line_bounds(const amark_document_t *doc, size_t line,
                       amark_line_bounds_t *out)
{
    const amark_parse_data_t *data = amark_document_parse_data(doc);
    const amark_line_index_t *index;
    const unsigned char *bytes;
    size_t start;
    size_t end;
    size_t content_end;

    if (!covers(data, line)) {
        return false;
    }
    index = amark_document_line_index(doc);
    bytes = amark_document_content(doc);

    /* The line in the whole content, cut to the range. */
    start = index->line_starts[line - 1];
    end = line < amark_line_index_line_count(index) ? index->line_starts[line]
                                                    : index->length;
    if (start < data->range_start) {
        start = data->range_start;
    }
    if (end > data->range_end) {
        end = data->range_end;
    }

    /*
     * A line contains line ending bytes only at its end, so the line ending
     * is found from the last bytes of [start, end).
     */
    content_end = end;
    if (content_end > start && bytes[content_end - 1] == '\n') {
        content_end--;
        if (content_end > start && bytes[content_end - 1] == '\r') {
            content_end--;
        }
    } else if (content_end > start && bytes[content_end - 1] == '\r') {
        content_end--;
    }

    out->start = start;
    out->content_end = content_end;
    out->end = end;
    return true;
}

bool amark_line_get(const amark_document_t *doc, size_t line,
                    amark_line_record_t *out)
{
    const amark_parse_data_t *data = amark_document_parse_data(doc);

    if (!covers(data, line)) {
        return false;
    }
    *out = data->lines[line - data->first_line];
    return true;
}

void amark_line_set(amark_document_t *doc, size_t line,
                    size_t content_start, unsigned pad)
{
    amark_parse_data_t *data = amark_document_parse_data(doc);
    amark_line_record_t *record;

    if (!covers(data, line)) {
        return;
    }
    record = &data->lines[line - data->first_line];
    record->content_start = content_start;
    record->pad = pad;
}
