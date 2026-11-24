/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_LINE_INDEX_H
#define AMARK_LINE_INDEX_H

#include <stddef.h>

#include <amark/error.h>

#include "arena.h"

/*
 * The line index. It records where each line of a byte sequence starts and
 * maps a byte offset to a line number (the first line is 1).
 *
 * A line ends with LF, with CR not followed by LF, or with CR LF (one line
 * ending). The bytes of a line ending belong to the line they end; the next
 * line starts at the byte right after them. The number of lines is therefore
 * the number of line endings plus 1: an empty sequence has one line, and a
 * sequence that ends with a line ending has an empty last line that starts
 * at its length. Only byte values are looked at, so invalid UTF-8 follows the
 * same rules.
 *
 * The index does not keep a pointer to the bytes. Its table lives in the
 * arena it was built with and is released with that arena.
 */

/*
 * The members are visible in src/ but other modules use the functions only.
 */
typedef struct amark_line_index {
    const size_t *line_starts; /* offset of each line's first byte; ascending;
                                  line_starts[0] is 0 */
    size_t line_count;         /* at least 1 */
    size_t length;             /* the length of the byte sequence */
} amark_line_index_t;

/*
 * Builds *index for the length bytes at bytes, which are not changed. The
 * bytes are scanned twice (count the lines, then fill the table) and the
 * table is allocated from arena once. On failure, returns AMARK_ERROR_NOMEM
 * (the arena could not allocate) or AMARK_ERROR_LIMIT_EXCEEDED (the table size
 * in bytes does not fit in size_t); nothing is allocated and *index is
 * unchanged.
 */
amark_error_t amark_line_index_build(amark_line_index_t *index,
                                     amark_arena_t *arena,
                                     const unsigned char *bytes,
                                     size_t length);

/*
 * Returns the number of the line that contains offset (the first line is 1),
 * by binary search over the table. offset must be at most the length; the
 * offset equal to the length belongs to the last line.
 */
size_t amark_line_index_line_at(const amark_line_index_t *index,
                                size_t offset);

/* Returns the number of lines (at least 1). */
size_t amark_line_index_line_count(const amark_line_index_t *index);

/*
 * Returns the offset of the first byte of line (the first line is 1), in
 * constant time by one read of the table. line must be at least 1 and at most
 * the number of lines plus 1; callers check this, as it is not checked here.
 * For the number of lines plus 1, returns the length, so every line runs from
 * its start to the start of the next line. An empty last line starts at the
 * length.
 */
size_t amark_line_index_line_start(const amark_line_index_t *index,
                                   size_t line);

#endif /* AMARK_LINE_INDEX_H */
