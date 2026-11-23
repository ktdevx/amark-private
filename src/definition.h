/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_DEFINITION_H
#define AMARK_DEFINITION_H

#include <stdbool.h>
#include <stddef.h>

#include "document.h"
#include "parse_data.h"

/*
 * Link labels: scanning a label and comparing two labels after normalizing
 * them (CommonMark 0.31.2, section 4.7).
 *
 * Positions are offsets into the content of the document. A label may cross
 * lines; the bytes from the end of a line's content to the content_start of
 * the line record of the next line (the line ending, container prefixes and
 * leading white space) are read as one line ending. When the document has no
 * parse data, or the next line has no record, only the line ending is
 * skipped.
 *
 * A label is normalized as a sequence of code points, produced one at a time
 * and never stored:
 *   1. the bytes are decoded with amark_utf8_decode_lossy (an ill-formed
 *      sequence is U+FFFD) and U+0000 becomes U+FFFD;
 *   2. runs of spaces, tabs and line endings become one space, and those at
 *      the start and the end are dropped;
 *   3. each code point is folded with amark_unicode_fold.
 * Backslash escapes and character references are compared as written.
 *
 * None of these functions allocates, recurses or fails.
 */

/* The largest number of characters (code points) inside the brackets. */
#define AMARK_LABEL_MAX_CHARS 999

/*
 * Scans the label whose '[' is at open, reading no byte at or past limit
 * (the end of the paragraph content). Reads from open + 1 to the first
 * unescaped ']'. Returns false, leaving the outputs unchanged, when there is
 * no ']' before limit, when an unescaped '[' comes first, when the inside
 * has more than AMARK_LABEL_MAX_CHARS characters, or when the inside has no
 * character other than a space, a tab or a line ending. A backslash followed
 * by an ASCII punctuation character escapes it. Each line ending counts as
 * one character; the bytes skipped after it (see above) count as none.
 * Otherwise writes the inside of the brackets to [*label_start, *label_end)
 * (*label_end is the position of ']') and returns true. Reads no more than
 * AMARK_LABEL_MAX_CHARS + 1 characters.
 */
bool amark_label_scan(const amark_document_t *doc, size_t open, size_t limit,
                      size_t *label_start, size_t *label_end);

/*
 * Compares the normalized labels [a_start, a_end) and [b_start, b_end) code
 * point by code point and returns a negative value, 0 or a positive value
 * when a sorts before, equal to or after b (a proper prefix sorts first).
 * Takes time proportional to the sum of the two lengths.
 */
int amark_label_compare(const amark_document_t *doc,
                        size_t a_start, size_t a_end,
                        size_t b_start, size_t b_end);

#endif /* AMARK_DEFINITION_H */
