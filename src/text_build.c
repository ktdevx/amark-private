/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#include "text_build.h"

#include <stdbool.h>

#include "charref.h"
#include "line.h"
#include "scan.h"
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

/*
 * What the first pass of CODE_SPAN learns about the content read as one
 * sequence: the bytes of the segments, with one space for each line ending.
 */
typedef struct code_span_scan {
    size_t count;     /* the length of the sequence */
    bool first_space; /* its first element is a space */
    bool last_space;  /* its last element is a space */
    bool non_space;   /* it holds an element that is not a space */
} code_span_scan_t;

/* Adds element c to the sequence of scan. */
static void scan_element(code_span_scan_t *scan, unsigned char c)
{
    if (scan->count == 0) {
        scan->first_space = c == ' ';
    }
    scan->last_space = c == ' ';
    if (c != ' ') {
        scan->non_space = true;
    }
    scan->count++;
}

/*
 * The second pass of CODE_SPAN: emits one space as element *index of the
 * sequence when it lies in [low, high), and moves to the next element.
 */
static void put_space_in(size_t *index, size_t low, size_t high,
                         amark_text_sink_t *sink)
{
    if (*index >= low && *index < high) {
        amark_text_sink_put_byte(sink, ' ');
    }
    (*index)++;
}

/*
 * CODE_SPAN: reads the range twice. The first pass finds whether the
 * sequence (segment bytes, a space for each line ending; pad spaces, which a
 * paragraph line never has, count as spaces) starts and ends with a space
 * and holds a byte that is not a space; if all three hold, one element is
 * dropped at each end. The second pass emits the elements in [low, high):
 * the segment bytes as source bytes, the line endings as spaces.
 */
static void build_code_span(const amark_document_t *doc, size_t start,
                            size_t end, size_t first_line,
                            amark_text_sink_t *sink)
{
    const unsigned char *content = amark_document_content(doc);
    amark_text_lines_t lines;
    amark_text_segment_t segment;
    code_span_scan_t scan = {0, false, false, false};
    size_t low = 0;
    size_t high;
    size_t index = 0;
    size_t pos;
    unsigned i;

    amark_text_lines_init(&lines, doc, start, end, first_line);
    while (amark_text_lines_next(&lines, &segment)) {
        for (i = 0; i < segment.pad; i++) {
            scan_element(&scan, ' ');
        }
        pos = segment.pad != 0 ? segment.start + 1 : segment.start;
        for (; pos < segment.end; pos++) {
            scan_element(&scan, content[pos]);
        }
        if (segment.line_end) {
            scan_element(&scan, ' ');
        }
    }

    high = scan.count;
    if (scan.first_space && scan.last_space && scan.non_space) {
        low = 1;
        high--;
    }

    amark_text_lines_init(&lines, doc, start, end, first_line);
    while (amark_text_lines_next(&lines, &segment)) {
        size_t from;
        size_t to;

        for (i = 0; i < segment.pad; i++) {
            put_space_in(&index, low, high, sink);
        }
        pos = segment.pad != 0 ? segment.start + 1 : segment.start;
        /* The bytes [pos, end) are the elements [index, index + count). */
        from = pos + (low > index ? low - index : 0);
        to = segment.end;
        if (high < index + (segment.end - pos)) {
            to = pos + (high > index ? high - index : 0);
        }
        if (from < to) {
            amark_text_sink_put_source(sink, content + from, to - from);
        }
        index += segment.end - pos;
        if (segment.line_end) {
            put_space_in(&index, low, high, sink);
        }
    }
}

/*
 * PLAIN and DECODED: each segment is written on its own, with the line
 * ending inside the range as '\n'.
 */
static void build_segments(const amark_document_t *doc, size_t start,
                           size_t end, size_t first_line,
                           amark_text_rule_t rule, amark_text_sink_t *sink)
{
    const unsigned char *content = amark_document_content(doc);
    amark_text_lines_t lines;
    amark_text_segment_t segment;

    amark_text_lines_init(&lines, doc, start, end, first_line);
    while (amark_text_lines_next(&lines, &segment)) {
        if (rule == AMARK_TEXT_RULE_PLAIN) {
            build_plain(content, &segment, sink);
        } else {
            build_decoded(content, &segment, sink);
        }
    }
}

void amark_text_build_range(const amark_document_t *doc, size_t start,
                            size_t end, size_t first_line,
                            amark_text_rule_t rule, amark_text_sink_t *sink)
{
    switch (rule) {
    case AMARK_TEXT_RULE_PLAIN:
    case AMARK_TEXT_RULE_DECODED:
        build_segments(doc, start, end, first_line, rule, sink);
        break;
    case AMARK_TEXT_RULE_CODE_SPAN:
        build_code_span(doc, start, end, first_line, sink);
        break;
    }
}

/*
 * Whether the end line of the fenced code block node is its closing fence
 * line: a line other than the start line whose record has pad 0, whose
 * content starts with the fence character, and which amark_scan_close_fence
 * reads as a run at least as long as the opening fence. The block parser
 * records a closing fence line at its first character with pad 0; a code
 * line is recorded after up to the indentation of the opening fence, so its
 * record is at the fence character only when its indentation was 3 columns
 * or less, and such a line of the closing form would have closed the block.
 */
static bool ends_with_closing_fence(const amark_document_t *doc,
                                    const amark_node_t *node,
                                    const amark_node_attr_t *attr)
{
    const unsigned char *content = amark_document_content(doc);
    amark_line_bounds_t bounds;
    amark_line_record_t record;
    size_t start;

    if (node->start_line == node->end_line ||
        !amark_line_bounds(doc, node->end_line, &bounds) ||
        !amark_line_get(doc, node->end_line, &record)) {
        return false;
    }
    start = record.content_start;
    if (record.pad != 0 || start >= bounds.content_end ||
        content[start] != (unsigned char)attr->code_block.fence_char) {
        return false;
    }
    return amark_scan_close_fence(content + start, bounds.content_end - start,
                                  attr->code_block.fence_char) >=
           attr->code_block.fence_length;
}

/*
 * Writes code line line of doc: from the content_start of its record to the
 * end of its content as PLAIN writes a segment, then '\n' in any case.
 */
static void build_code_line(const amark_document_t *doc, size_t line,
                            amark_text_sink_t *sink)
{
    amark_line_bounds_t bounds;
    amark_line_record_t record;
    amark_text_segment_t segment;

    if (!amark_line_bounds(doc, line, &bounds) ||
        !amark_line_get(doc, line, &record)) {
        /* Not reached when the preconditions hold. */
        return;
    }
    segment.start = record.content_start;
    segment.end = bounds.content_end;
    segment.pad = record.pad;
    segment.line_end = true;
    build_plain(amark_document_content(doc), &segment, sink);
}

void amark_text_build_code(const amark_document_t *doc,
                           const amark_node_t *node,
                           const amark_node_attr_t *attr,
                           amark_text_sink_t *sink)
{
    size_t first = node->start_line;
    size_t last = node->end_line;
    size_t line;

    if (attr->code_block.style == AMARK_CODE_BLOCK_FENCED) {
        if (first == last) {
            return;
        }
        if (ends_with_closing_fence(doc, node, attr)) {
            last--;
        }
        first++;
    }
    for (line = first; line <= last; line++) {
        build_code_line(doc, line, sink);
    }
}
