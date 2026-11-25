/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_BLOCK_H
#define AMARK_BLOCK_H

#include <stdbool.h>
#include <stddef.h>

#include <amark/error.h>

#include "arena.h"
#include "config_internal.h"
#include "dialect_internal.h"
#include "document.h"
#include "line.h"

/*
 * The block parser: reads the lines of the parsed range once, in order, and
 * builds the block tree of the document with a stack of open blocks (the
 * "three phases per line" of CommonMark 0.31.2, appendix "A parsing
 * strategy"). While it parses it writes the line records and the inline
 * contents of the parse data, and at the end it builds the definition table
 * from the link reference definitions taken off the starts of paragraphs.
 * It recurses nowhere and keeps no state outside the call.
 */

/*
 * The position on the line being processed. Columns count from the start of
 * the line (the start of the range on the first line of the range) and a
 * tab advances to the next multiple of 4. When an advance by columns stops
 * inside a tab, offset stays on the tab and pad holds the columns of that
 * tab not consumed yet. Only the block parser and its tests use these.
 */
typedef struct amark_line_pos {
    size_t line;           /* the line number */
    size_t content_end;    /* the end of the line's content */
    size_t offset;         /* the byte being looked at */
    size_t column;         /* its column */
    unsigned pad;          /* columns of the tab at offset not consumed yet;
                              0 to 3 */
    size_t first_nonspace; /* the first byte from offset that is not a space
                              or a tab; content_end if none */
    size_t first_nonspace_column;
    size_t indent;         /* first_nonspace_column - column */
    bool blank;            /* first_nonspace == content_end */
} amark_line_pos_t;

/* What amark_line_pos_advance counts. */
typedef enum amark_advance_unit {
    AMARK_ADVANCE_BYTES,
    AMARK_ADVANCE_COLUMNS
} amark_advance_unit_t;

/* Puts pos at column 0 of the start of line, whose bounds are *bounds. */
void amark_line_pos_init(amark_line_pos_t *pos, size_t line,
                         const amark_line_bounds_t *bounds);

/*
 * Sets first_nonspace, first_nonspace_column, indent and blank for the
 * current offset. Only spaces and tabs are spaces; every other byte (U+0000,
 * Unicode spaces, bytes of invalid UTF-8) is content. A tab partly consumed
 * counts only its remaining columns.
 *
 * Scans only when first_nonspace <= offset (the first call on a line, or
 * after an advance reached or passed it). Otherwise the earlier result
 * still holds, and only indent and blank are computed again, so the calls
 * for all the open blocks of a line together scan each byte once.
 */
void amark_line_pos_find_nonspace(amark_line_pos_t *pos,
                                  const unsigned char *bytes);

/*
 * Advances pos by count bytes or count columns, stopping at the end of the
 * line's content. By bytes, a tab is consumed whole. By columns, a tab wider
 * than what is left to consume keeps offset on the tab and leaves its
 * remaining columns in pad; otherwise pad becomes 0.
 */
void amark_line_pos_advance(amark_line_pos_t *pos, const unsigned char *bytes,
                            size_t count, amark_advance_unit_t unit);

/*
 * Parses the parsed range of doc into its block tree.
 *
 * Preconditions: doc has only its root (whose range is the parsed range) and
 * has parse data (amark_parse_data_create). config is the resolved
 * configuration doc was made with; only max_nesting_depth and max_node_count
 * are read from it (the dialect is not read). scratch is an initialized
 * arena for the working state of the parse (the stack of open blocks, the
 * definitions for the table and the table's working arrays); the
 * caller releases it. With AMARK_UTF8_STRICT the caller has checked that the
 * range is valid UTF-8. run is the run state of the parse's dialect, or
 * NULL without a dialect; with NULL the parse is the CommonMark one (13.1).
 * No rule of a dialect is called yet, so run is not read.
 *
 * On success, returns AMARK_ERROR_NONE: the tree satisfies every invariant
 * of the tree check, the range of the root is still the parsed range, and
 * the line records, the inline contents and the definition table are
 * written.
 *
 * On failure, returns AMARK_ERROR_NOMEM (an allocation failed) or
 * AMARK_ERROR_LIMIT_EXCEEDED (a block would be deeper than max_nesting_depth,
 * a node would make more than max_node_count nodes, the tree is full, or a
 * size computation overflowed). The tree may then be partly built; doc can
 * still be destroyed.
 */
amark_error_t amark_block_parse(amark_document_t *doc,
                                const amark_resolved_config_t *config,
                                amark_arena_t *scratch,
                                amark_dialect_run_t *run);

#endif /* AMARK_BLOCK_H */
