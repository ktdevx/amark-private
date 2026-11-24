/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_PARSE_DATA_H
#define AMARK_PARSE_DATA_H

#include <stdbool.h>
#include <stddef.h>

#include <amark/error.h>
#include <amark/tree.h>

#include "array.h"
#include "document.h"

/*
 * The parse data. The block parser attaches one to each document it parses;
 * it holds what the later stages read after the block parse: the line
 * records, the definition table and the inline contents. Everything lives in
 * the document's arena, so it stays valid until the document is destroyed
 * and is released with it. No byte of the content is copied.
 *
 * The block parser writes the data while it parses; after amark_block_parse
 * returns successfully it is read-only. None of this appears in a public
 * header.
 */

/*
 * The record of one line of the parsed range (see src/line.h for its
 * meaning). content_start is an offset from the start of the content.
 */
typedef struct amark_line_record {
    size_t content_start; /* where the content of the line starts */
    unsigned pad;         /* 0 to 3; if not 0, the byte at content_start is a
                             tab read as this many spaces */
} amark_line_record_t;

/* An entry of the definition table. */
typedef struct amark_definition_entry {
    amark_node_id_t node; /* the link reference definition */
    size_t label_start;   /* the label, inside the brackets */
    size_t label_end;
} amark_definition_entry_t;

/* A range to parse as inlines, and the paragraph or heading it belongs to. */
typedef struct amark_inline_content {
    amark_node_id_t node; /* a paragraph or a heading */
    size_t start;         /* the range [start, end) to parse as inlines */
    size_t end;
} amark_inline_content_t;

/*
 * The members are visible in src/ but other modules use the functions only.
 */
typedef struct amark_parse_data {
    size_t range_start;                 /* the parsed range */
    size_t range_end;
    size_t first_line;                  /* the first line of the range; 0 if
                                           the range is empty */
    size_t line_count;                  /* lines the range covers; 0 if the
                                           range is empty */
    amark_line_record_t *lines;         /* line_count records, or NULL */
    const amark_definition_entry_t *definitions; /* in normalized label
                                                    order, or NULL */
    size_t definition_count;
    amark_array_t inline_contents;      /* amark_inline_content_t, in
                                           document order */
} amark_parse_data_t;

/*
 * Creates the parse data of doc, which must be a document just made by
 * amark_document_create, and attaches it to doc. The parsed range is the
 * range of the root. The structure and the line records are allocated from
 * the document's arena.
 *
 * The number of lines is 0 if the range is empty (start == end). Otherwise
 * first_line is the line that contains start, the last line is the line that
 * contains end - 1, and line_count is their difference plus 1. Every record
 * starts as (the end of the line's content, pad 0); the end of the content
 * is the byte before the line ending, found only from the bytes inside the
 * range. The definition table and the inline contents are empty.
 *
 * On failure, returns AMARK_ERROR_NOMEM (the arena could not allocate) or
 * AMARK_ERROR_LIMIT_EXCEEDED (a size does not fit in size_t), attaches
 * nothing, and doc can still be destroyed.
 */
amark_error_t amark_parse_data_create(amark_document_t *doc);

/*
 * Appends the inline content (node, [start, end)) to the parse data of doc,
 * which must have parse data. On failure, returns AMARK_ERROR_NOMEM or
 * AMARK_ERROR_LIMIT_EXCEEDED and the list is unchanged.
 */
amark_error_t amark_inline_content_add(amark_document_t *doc,
                                       amark_node_id_t node,
                                       size_t start, size_t end);

/* Returns the number of inline contents of doc (0 without parse data). */
size_t amark_inline_content_count(const amark_document_t *doc);

/*
 * Copies the inline content at index (in the order added) to *out and
 * returns true. Returns false and leaves *out unchanged if index is at least
 * the count.
 */
bool amark_inline_content_at(const amark_document_t *doc, size_t index,
                             amark_inline_content_t *out);

#endif /* AMARK_PARSE_DATA_H */
