/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_LINE_H
#define AMARK_LINE_H

#include "document.h"
#include "parse_data.h"

/*
 * The line records. The parse data of a document holds one record
 * (amark_line_record_t, src/parse_data.h) for each line the parsed range
 * covers, from first_line to first_line + line_count - 1; lines are numbered
 * from 1 at the first line of the content. amark_parse_data_create sets
 * every record to (the end of the line's content, pad 0); the block parser
 * then writes where the content of the innermost block starts on each line.
 *
 * When pad is not 0, the byte at content_start is a tab, and the logical
 * content of the line is pad spaces followed by the bytes from
 * content_start + 1 to the end of the line's content. When pad is 0, it is
 * the bytes from content_start to the end of the line's content.
 */

#endif /* AMARK_LINE_H */
