/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#include "parse_data.h"

#include <string.h>

#include "arena.h"

/*
 * Sets each of the count records of lines to (the end of the line's
 * content, pad 0), by one pass over the bytes of [start, end). A line ends
 * with LF, with CR not followed by LF, or with CR LF; only bytes inside the
 * range are looked at, so a CR at end - 1 ends its line even if an LF
 * follows outside the range.
 */
static void init_records(const unsigned char *bytes, size_t start,
                         size_t end, amark_line_record_t *lines, size_t count)
{
    size_t pos = start;
    size_t i;

    for (i = 0; i < count; i++) {
        size_t content_end = pos;

        while (content_end < end && bytes[content_end] != '\n' &&
               bytes[content_end] != '\r') {
            content_end++;
        }
        lines[i].content_start = content_end;
        lines[i].pad = 0;

        pos = content_end;
        if (pos < end) {
            if (bytes[pos] == '\r' && pos + 1 < end && bytes[pos + 1] == '\n') {
                pos += 2;
            } else {
                pos += 1;
            }
        }
    }
}

amark_error_t amark_parse_data_create(amark_document_t *doc)
{
    amark_arena_t *arena = amark_document_arena(doc);
    amark_node_id_t root = amark_document_root(doc);
    amark_parse_data_t *data;
    amark_range_t range;
    size_t first_line = 0;
    size_t last_line = 0;
    size_t line_count = 0;
    void *mem = NULL;
    amark_error_t err;

    range.start = 0;
    range.end = 0;
    (void)amark_node_range(doc, root, &range);
    if (range.start < range.end) {
        amark_document_range_lines(doc, range.start, range.end, &first_line,
                                   &last_line);
        line_count = last_line - first_line + 1;
    }

    err = amark_arena_alloc(arena, sizeof(amark_parse_data_t), &mem);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    data = (amark_parse_data_t *)mem;
    memset(data, 0, sizeof(*data));
    data->range_start = range.start;
    data->range_end = range.end;
    data->first_line = first_line;
    data->line_count = line_count;
    data->lines = NULL;
    data->definitions = NULL;
    data->definition_count = 0;
    amark_array_init(&data->inline_contents, arena,
                     sizeof(amark_inline_content_t));

    if (line_count > 0) {
        err = amark_arena_alloc_array(arena, line_count,
                                      sizeof(amark_line_record_t), &mem);
        if (err != AMARK_ERROR_NONE) {
            return err;
        }
        data->lines = (amark_line_record_t *)mem;
        init_records(amark_document_content(doc), range.start, range.end,
                     data->lines, line_count);
    }

    amark_document_set_parse_data(doc, data);
    return AMARK_ERROR_NONE;
}

amark_error_t amark_inline_content_add(amark_document_t *doc,
                                       amark_node_id_t node,
                                       size_t start, size_t end)
{
    amark_parse_data_t *data =
        amark_document_parse_data(doc);
    amark_inline_content_t content;

    content.node = node;
    content.start = start;
    content.end = end;
    return amark_array_push(&data->inline_contents, &content);
}

size_t amark_inline_content_count(const amark_document_t *doc)
{
    const amark_parse_data_t *data =
        amark_document_parse_data(doc);

    if (data == NULL) {
        return 0;
    }
    return amark_array_len(&data->inline_contents);
}

bool amark_inline_content_at(const amark_document_t *doc, size_t index,
                             amark_inline_content_t *out)
{
    const amark_parse_data_t *data =
        amark_document_parse_data(doc);

    if (data == NULL || index >= amark_array_len(&data->inline_contents)) {
        return false;
    }
    memcpy(out, amark_array_at(&data->inline_contents, index), sizeof(*out));
    return true;
}
