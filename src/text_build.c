/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#include "text_build.h"

#include "text_lines.h"

/*
 * PLAIN: pad spaces in place of the tab at the start of the segment, the
 * rest of its bytes as written, and '\n' for a line ending in the range.
 */
static void build_plain(const unsigned char *content,
                        const amark_text_segment_t *segment,
                        amark_text_sink_t *sink)
{
    size_t start = segment->start;
    unsigned i;

    if (segment->pad != 0) {
        /* The byte at start is the tab the pad stands for. */
        for (i = 0; i < segment->pad; i++) {
            amark_text_sink_put_byte(sink, ' ');
        }
        start++;
    }
    amark_text_sink_put_source(sink, content + start, segment->end - start);
    if (segment->line_end) {
        amark_text_sink_put_byte(sink, '\n');
    }
}

void amark_text_build_range(const amark_document_t *doc, size_t start,
                            size_t end, size_t first_line,
                            amark_text_rule_t rule, amark_text_sink_t *sink)
{
    const unsigned char *content = amark_document_content(doc);
    amark_text_lines_t lines;
    amark_text_segment_t segment;

    amark_text_lines_init(&lines, doc, start, end, first_line);
    while (amark_text_lines_next(&lines, &segment)) {
        switch (rule) {
        case AMARK_TEXT_RULE_PLAIN:
            build_plain(content, &segment, sink);
            break;
        }
    }
}
