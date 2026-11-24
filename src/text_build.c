/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#include "text_build.h"

#include <stdbool.h>

#include "charref.h"
#include "text_lines.h"

/* ASCII punctuation as CommonMark defines it: what a backslash escapes. */
static bool is_ascii_punctuation(unsigned char c)
{
    return (c >= 0x21 && c <= 0x2F) || (c >= 0x3A && c <= 0x40) ||
           (c >= 0x5B && c <= 0x60) || (c >= 0x7B && c <= 0x7E);
}

/*
 * Emits the pad spaces of segment and returns where its bytes start: past
 * the tab the pad stands for, if there is a pad.
 */
static size_t put_pad(const amark_text_segment_t *segment,
                      amark_text_sink_t *sink)
{
    unsigned i;

    if (segment->pad == 0) {
        return segment->start;
    }
    for (i = 0; i < segment->pad; i++) {
        amark_text_sink_put_byte(sink, ' ');
    }
    return segment->start + 1;
}

/*
 * PLAIN: pad spaces in place of the tab at the start of the segment, the
 * rest of its bytes as written, and '\n' for a line ending in the range.
 */
static void build_plain(const unsigned char *content,
                        const amark_text_segment_t *segment,
                        amark_text_sink_t *sink)
{
    size_t start = put_pad(segment, sink);

    amark_text_sink_put_source(sink, content + start, segment->end - start);
    if (segment->line_end) {
        amark_text_sink_put_byte(sink, '\n');
    }
}

/*
 * DECODED: as PLAIN, reading the bytes of the segment once from the front.
 * A backslash before an ASCII punctuation character in the segment gives
 * that character; a character reference that ends in the segment gives its
 * value; any other backslash or '&' is emitted as written. The bytes between
 * them go out as source bytes.
 */
static void build_decoded(const unsigned char *content,
                          const amark_text_segment_t *segment,
                          amark_text_sink_t *sink)
{
    size_t end = segment->end;
    size_t pos = put_pad(segment, sink);
    size_t run = pos; /* start of the bytes not emitted yet */

    while (pos < end) {
        unsigned char c = content[pos];
        amark_charref_t ref;
        size_t length;

        if (c != '\\' && c != '&') {
            pos++;
            continue;
        }
        amark_text_sink_put_source(sink, content + run, pos - run);
        if (c == '\\') {
            if (pos + 1 < end && is_ascii_punctuation(content[pos + 1])) {
                amark_text_sink_put_byte(sink, content[pos + 1]);
                pos += 2;
            } else {
                amark_text_sink_put_byte(sink, '\\');
                pos++;
            }
        } else {
            length = amark_charref_scan(content + pos, end - pos, &ref);
            if (length != 0) {
                amark_text_sink_put_bytes(sink, ref.utf8, ref.utf8_length);
                pos += length;
            } else {
                amark_text_sink_put_byte(sink, '&');
                pos++;
            }
        }
        run = pos;
    }
    amark_text_sink_put_source(sink, content + run, end - run);
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
        case AMARK_TEXT_RULE_DECODED:
            build_decoded(content, &segment, sink);
            break;
        }
    }
}
