/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_SCAN_H
#define AMARK_SCAN_H

#include <amark/tree.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The block scanners. Each function looks at the bytes of one line and tells
 * whether they have the shape of the start or the end of a block, following
 * CommonMark 0.31.2. They read only the bytes they are given, know nothing
 * of the document or the tree, allocate nothing and cannot fail.
 *
 * p is the first byte of the line that is not white space, and len is the
 * number of bytes from there to the end of the line's content (the line
 * ending is not included). Whether the indentation is at most 3 columns is
 * checked by the caller. When len is 0, p is not read.
 *
 * White space is the space (U+0020) and the tab (U+0009) only. U+0000, other
 * control characters and Unicode white space (for example U+00A0) are not
 * white space.
 *
 * Positions written through output parameters are offsets from p. When a
 * function returns false or 0, it leaves its output parameters unchanged.
 */

/*
 * Returns true when the line is a thematic break: three or more of the same
 * character, '-', '_' or '*', with any number of spaces and tabs between and
 * after them, and nothing else.
 */
bool amark_scan_thematic_break(const unsigned char *p, size_t len);

/*
 * Returns true when the line is an ATX heading: 1 to 6 '#' followed by a
 * space, a tab or the end of the line. Stores the number of '#' in *level.
 * *content_start and *content_end bound the content: the opening '#' run and
 * the white space after it, an optional closing '#' run with the white space
 * before it, and trailing white space are left out. A closing run counts
 * only when white space precedes it (or it is the whole rest of the line).
 * When the heading has no content, *content_start == *content_end.
 */
bool amark_scan_atx_heading(const unsigned char *p, size_t len,
                            unsigned *level, size_t *content_start,
                            size_t *content_end);

/*
 * Returns 1 when the line is a setext heading underline of '=', 2 when it is
 * one of '-', and 0 otherwise. An underline is one or more of the same
 * character followed only by white space.
 */
unsigned amark_scan_setext_underline(const unsigned char *p, size_t len);

/*
 * Returns true when the line is an opening code fence: three or more of the
 * same character, '`' or '~', followed by an optional info string. Stores
 * the character in *fence_char and the length of the run in *fence_length.
 * *info_start and *info_end bound the info string with leading and trailing
 * white space left out; when there is no info string they are equal. A
 * backtick fence whose rest of the line contains a backtick is not a fence.
 */
bool amark_scan_open_fence(const unsigned char *p, size_t len,
                           char *fence_char, size_t *fence_length,
                           size_t *info_start, size_t *info_end);

/*
 * Returns the length of the run when the line is a closing code fence of
 * fence_char: three or more fence_char followed only by white space.
 * Returns 0 otherwise. Whether the run is at least as long as the opening
 * fence is compared by the caller.
 */
size_t amark_scan_close_fence(const unsigned char *p, size_t len,
                              char fence_char);

/* A list item marker, as found by amark_scan_list_marker. */
typedef struct amark_list_marker {
    amark_list_type_t type;
    char marker;           /* the bullet character, or the ordered delimiter */
    uint32_t start_number; /* the start number; ordered markers only */
    size_t width;          /* bytes of the marker: the digits and delimiter,
                              or the one bullet character */
    bool blank_after;      /* only white space follows the marker */
} amark_list_marker_t;

/*
 * Returns true when the line starts with a list item marker followed by a
 * space, a tab or the end of the line. A bullet marker is '-', '+' or '*';
 * an ordered marker is 1 to 9 ASCII digits followed by '.' or ')'. Stores
 * the type, the character, the start number (ordered markers only; for a
 * bullet marker it is 0), the width of the marker and whether only white
 * space follows it in *out.
 *
 * Only the shape is checked. Whether the marker may interrupt a paragraph
 * (start number 1 and not blank_after), whether the line is a thematic break
 * instead, and the columns of the white space after the marker are decided
 * by the caller.
 */
bool amark_scan_list_marker(const unsigned char *p, size_t len,
                            amark_list_marker_t *out);

#endif /* AMARK_SCAN_H */
