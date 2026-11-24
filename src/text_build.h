/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_TEXT_BUILD_H
#define AMARK_TEXT_BUILD_H

#include <stddef.h>

#include "document.h"
#include "text_sink.h"

/*
 * The rules of the text extraction: writes a range of the content of a
 * parsed document to a TextSink, one segment of src/text_lines.h at a time,
 * following a rule. Every rule about the bytes of a string lives here; the
 * kind of the node the range came from is not known.
 *
 * Nothing here allocates, fails, recurses or changes the document.
 */

typedef enum amark_text_rule {
    /*
     * As written: for each segment, pad spaces in place of the tab at its
     * start (when pad is not 0), then the rest of its bytes through
     * amark_text_sink_put_source, then '\n' when its line ending (LF, CR or
     * CR LF) is inside the range. Trailing spaces, other tabs, backslash
     * escapes, character references and syntax delimiters stay as written.
     */
    AMARK_TEXT_RULE_PLAIN = 1,
    /*
     * Decoded: as PLAIN, except that within each segment a backslash before
     * an ASCII punctuation character gives that character alone and a
     * character reference (amark_charref_scan) gives its value. Any other
     * backslash or '&' stays as written. The output is not read again, and
     * neither an escape nor a reference runs past the end of its segment.
     */
    AMARK_TEXT_RULE_DECODED = 2,
    /*
     * Code span: the content is read as the bytes of its segments with one
     * space for each line ending in the range (container prefixes and the
     * leading whitespace of continuation lines are left out). When that
     * sequence starts with a space, ends with a space and holds a byte that
     * is not a space, one space is dropped at each end. Tabs, other spaces,
     * backslashes and '&' stay as written.
     */
    AMARK_TEXT_RULE_CODE_SPAN = 3
} amark_text_rule_t;

/*
 * Writes [start, end) of doc, which starts on first_line, to sink with rule.
 * Preconditions: those of amark_text_lines_init, and rule is one of
 * amark_text_rule_t.
 */
void amark_text_build_range(const amark_document_t *doc, size_t start,
                            size_t end, size_t first_line,
                            amark_text_rule_t rule, amark_text_sink_t *sink);

/*
 * Writes the code lines of the code block node of doc, whose attributes are
 * attr, to sink. The code lines of an indented code block are its start line
 * through its end line; those of a fenced code block are the line after its
 * start line through its end line, without the end line when it is the
 * closing fence line, and there are none when the start line is the end
 * line. The end line is the closing fence line when it is not the start
 * line, its record has pad 0, the byte at its content_start is the fence
 * character, and amark_scan_close_fence from there to the end of the line's
 * content returns at least the length of the opening fence.
 *
 * Each code line is written from the content_start of its record to the end
 * of its content as PLAIN writes a segment (pad spaces in place of the tab at
 * the record, the rest as written), followed by '\n' whether or not the line
 * has a line ending. Backslashes and '&' stay as written.
 *
 * Preconditions: doc has parse data, node is a code block node of doc and
 * attr is its attributes.
 */
void amark_text_build_code(const amark_document_t *doc,
                           const amark_node_t *node,
                           const amark_node_attr_t *attr,
                           amark_text_sink_t *sink);

#endif /* AMARK_TEXT_BUILD_H */
