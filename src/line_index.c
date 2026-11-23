/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#include "line_index.h"

#include "alloc.h"

/*
 * Returns 1 if bytes[i] is the last byte of a line ending (LF, or CR not
 * followed by LF), 0 otherwise. The CR of CR LF returns 0 and its LF returns
 * 1, so each line ending is counted once, at its last byte.
 */
static int ends_line(const unsigned char *bytes, size_t length, size_t i)
{
    if (bytes[i] == '\n') {
        return 1;
    }
    if (bytes[i] == '\r') {
        return i + 1 == length || bytes[i + 1] != '\n';
    }
    return 0;
}

amark_error_t amark_line_index_build(amark_line_index_t *index,
                                     amark_arena_t *arena,
                                     const unsigned char *bytes,
                                     size_t length)
{
    size_t line_count = 1;
    size_t table_bytes;
    size_t *starts;
    void *mem = NULL;
    size_t line;
    size_t i;
    amark_error_t err;

    /* First pass: count the lines (line endings + 1). */
    for (i = 0; i < length; i++) {
        if (ends_line(bytes, length, i)) {
            if (!amark_size_add(line_count, 1, &line_count)) {
                return AMARK_ERROR_LIMIT_EXCEEDED;
            }
        }
    }

    if (!amark_size_mul(line_count, sizeof(size_t), &table_bytes)) {
        return AMARK_ERROR_LIMIT_EXCEEDED;
    }
    err = amark_arena_alloc(arena, table_bytes, &mem);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    starts = (size_t *)mem;

    /* Second pass: each line after the first starts right after the last
       byte of a line ending. */
    starts[0] = 0;
    line = 1;
    for (i = 0; i < length; i++) {
        if (ends_line(bytes, length, i)) {
            starts[line] = i + 1;
            line++;
        }
    }

    index->line_starts = starts;
    index->line_count = line_count;
    index->length = length;
    return AMARK_ERROR_NONE;
}

size_t amark_line_index_line_at(const amark_line_index_t *index,
                                size_t offset)
{
    /*
     * Find the largest i with line_starts[i] <= offset. line_starts[0] is 0,
     * so such an i exists. Invariant: line_starts[lo] <= offset, and every
     * entry at hi or later is greater than offset.
     */
    size_t lo = 0;
    size_t hi = index->line_count;

    while (hi - lo > 1) {
        size_t mid = lo + (hi - lo) / 2;

        if (index->line_starts[mid] <= offset) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return lo + 1;
}

size_t amark_line_index_line_count(const amark_line_index_t *index)
{
    return index->line_count;
}
