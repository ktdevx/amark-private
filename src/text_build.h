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
    AMARK_TEXT_RULE_PLAIN = 1
} amark_text_rule_t;

/*
 * Writes [start, end) of doc, which starts on first_line, to sink with rule.
 * Preconditions: those of amark_text_lines_init, and rule is one of
 * amark_text_rule_t.
 */
void amark_text_build_range(const amark_document_t *doc, size_t start,
                            size_t end, size_t first_line,
                            amark_text_rule_t rule, amark_text_sink_t *sink);

#endif /* AMARK_TEXT_BUILD_H */
