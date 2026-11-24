/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_REFDEF_H
#define AMARK_REFDEF_H

#include <stdbool.h>
#include <stddef.h>

#include <amark/tree.h>

#include "document.h"

/*
 * Link reference definitions (CommonMark 0.31.2, section 4.7): reading one
 * definition at the start of a paragraph. Nothing here builds a tree,
 * allocates, recurses or fails; "not a definition" is not a failure.
 *
 * Positions are offsets into the content of the document. A definition may
 * cross lines; at a line ending the reader steps to the content_start of the
 * line record of the next line (past container prefixes), so the records of
 * every line the definition may span must be written before it is read.
 */

/* The ranges of one definition. */
typedef struct amark_refdef {
    amark_range_t label;       /* the inside of the brackets */
    amark_range_t destination; /* the inside of <...>, or the whole raw
                                  destination */
    amark_range_t title;       /* the inside of the delimiters; unused when
                                  has_title is false */
    bool has_title;
    size_t last_line;          /* the number of the last line of the
                                  definition */
    size_t end;                /* the end of the content of that line (the
                                  end of the definition node) */
} amark_refdef_t;

/*
 * Reads a link reference definition whose '[' is at start, reading no byte
 * at or past limit.
 *
 * Preconditions: start is the content_start of a line; limit is the end of
 * the content of the last line of the paragraph; the line records of the
 * lines from start to limit are written.
 *
 * The definition is: the label, ':', optional spaces and tabs with up to one
 * line ending, the destination, then optionally spaces and tabs with up to
 * one line ending (at least one of them) and a title, then only spaces and
 * tabs up to the end of the line. When what follows the destination is no
 * such title, the definition still ends at the destination if only spaces
 * and tabs follow it on its line; the title's line is then left to the
 * paragraph. An empty <> destination and an empty title are empty ranges.
 *
 * Returns true and writes *out when there is a definition; otherwise returns
 * false and leaves *out unchanged. Reads at most AMARK_LABEL_MAX_CHARS + 1
 * characters of the label and at most up to limit for the rest, in time
 * proportional to the bytes read.
 */
bool amark_refdef_parse(const amark_document_t *doc, size_t start,
                        size_t limit, amark_refdef_t *out);

#endif /* AMARK_REFDEF_H */
