/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#include "definition.h"

#include <stdint.h>
#include <string.h>

#include "line.h"
#include "unicode.h"
#include "utf8.h"

/*
 * A position in a label that may cross lines: the next byte to read, the
 * end of what may be read, and the line that holds the next byte (so that
 * the record of the next line is found without a search).
 */
typedef struct label_cursor {
    const amark_document_t *doc;
    const unsigned char *bytes;
    size_t pos;
    size_t end;
    size_t line;
} label_cursor_t;

static void cursor_init(label_cursor_t *cursor, const amark_document_t *doc,
                        size_t start, size_t end)
{
    size_t unused;

    cursor->doc = doc;
    cursor->bytes = amark_document_content(doc);
    cursor->pos = start;
    cursor->end = end;
    amark_document_range_lines(doc, start, start, &cursor->line, &unused);
}

/*
 * Steps over the line ending at cursor->pos (CR LF, LF or CR) and then up to
 * the content_start of the next line's record, if it has one past the line
 * ending. Never moves past cursor->end.
 */
static void cursor_skip_line_ending(label_cursor_t *cursor)
{
    amark_line_record_t record;

    if (cursor->bytes[cursor->pos] == '\r' &&
        cursor->pos + 1 < cursor->end &&
        cursor->bytes[cursor->pos + 1] == '\n') {
        cursor->pos += 2;
    } else {
        cursor->pos++;
    }
    cursor->line++;
    if (amark_line_get(cursor->doc, cursor->line, &record) &&
        record.content_start > cursor->pos) {
        cursor->pos = record.content_start < cursor->end ? record.content_start
                                                         : cursor->end;
    }
}

static bool is_line_ending(unsigned char c)
{
    return c == '\n' || c == '\r';
}

static bool is_blank(unsigned char c)
{
    return c == ' ' || c == '\t';
}

/* ASCII punctuation as CommonMark defines it: what a backslash escapes. */
static bool is_ascii_punctuation(unsigned char c)
{
    return (c >= 0x21 && c <= 0x2F) || (c >= 0x3A && c <= 0x40) ||
           (c >= 0x5B && c <= 0x60) || (c >= 0x7B && c <= 0x7E);
}

bool amark_label_scan(const amark_document_t *doc, size_t open, size_t limit,
                      size_t *label_start, size_t *label_end)
{
    label_cursor_t cursor;
    size_t chars = 0;
    bool visible = false;

    if (open >= limit) {
        return false;
    }
    cursor_init(&cursor, doc, open + 1, limit);
    while (cursor.pos < limit) {
        unsigned char c = cursor.bytes[cursor.pos];
        uint32_t unused;

        if (c == ']') {
            if (!visible) {
                return false;
            }
            *label_start = open + 1;
            *label_end = cursor.pos;
            return true;
        }
        if (c == '[' || chars == AMARK_LABEL_MAX_CHARS) {
            return false;
        }
        chars++;
        if (is_line_ending(c)) {
            cursor_skip_line_ending(&cursor);
        } else if (is_blank(c)) {
            cursor.pos++;
        } else if (c == '\\') {
            visible = true;
            cursor.pos++;
            if (cursor.pos < limit &&
                is_ascii_punctuation(cursor.bytes[cursor.pos])) {
                if (chars == AMARK_LABEL_MAX_CHARS) {
                    return false;
                }
                chars++;
                cursor.pos++;
            }
        } else {
            visible = true;
            cursor.pos += amark_utf8_decode_lossy(cursor.bytes + cursor.pos,
                                                  limit - cursor.pos,
                                                  &unused);
        }
    }
    return false;
}

/*
 * Produces the normalized code points of a label one at a time. The code
 * points that folding the last character gave and that were not returned yet
 * are folded[next] to folded[count - 1]. space tells that white space was
 * read since the last character, and started that a character was read.
 */
typedef struct label_reader {
    label_cursor_t cursor;
    uint32_t folded[AMARK_UNICODE_FOLD_MAX];
    size_t next;
    size_t count;
    bool space;
    bool started;
} label_reader_t;

static void reader_init(label_reader_t *reader, const amark_document_t *doc,
                        size_t start, size_t end)
{
    cursor_init(&reader->cursor, doc, start, end);
    reader->next = 0;
    reader->count = 0;
    reader->space = false;
    reader->started = false;
}

/*
 * Writes the next normalized code point to *codepoint and returns true, or
 * returns false at the end of the label. Each call reads a bounded number of
 * code points past the white space it skips, so reading a whole label takes
 * time proportional to its length.
 */
static bool reader_next(label_reader_t *reader, uint32_t *codepoint)
{
    label_cursor_t *cursor = &reader->cursor;

    if (reader->next < reader->count) {
        *codepoint = reader->folded[reader->next++];
        return true;
    }
    while (cursor->pos < cursor->end) {
        unsigned char c = cursor->bytes[cursor->pos];
        uint32_t decoded;

        if (is_blank(c)) {
            cursor->pos++;
            reader->space = true;
            continue;
        }
        if (is_line_ending(c)) {
            cursor_skip_line_ending(cursor);
            reader->space = true;
            continue;
        }
        cursor->pos += amark_utf8_decode_lossy(cursor->bytes + cursor->pos,
                                               cursor->end - cursor->pos,
                                               &decoded);
        if (decoded == 0) {
            decoded = 0xFFFD;
        }
        reader->count = amark_unicode_fold(decoded, reader->folded);
        reader->next = 0;
        if (reader->space && reader->started) {
            /* One space between words, then the folded character. */
            reader->space = false;
            *codepoint = ' ';
            return true;
        }
        reader->space = false;
        reader->started = true;
        *codepoint = reader->folded[reader->next++];
        return true;
    }
    return false;
}

int amark_label_compare(const amark_document_t *doc,
                        size_t a_start, size_t a_end,
                        size_t b_start, size_t b_end)
{
    label_reader_t a;
    label_reader_t b;

    reader_init(&a, doc, a_start, a_end);
    reader_init(&b, doc, b_start, b_end);
    for (;;) {
        uint32_t ca = 0;
        uint32_t cb = 0;
        bool has_a = reader_next(&a, &ca);
        bool has_b = reader_next(&b, &cb);

        if (!has_a || !has_b) {
            return has_a ? 1 : (has_b ? -1 : 0);
        }
        if (ca != cb) {
            return ca < cb ? -1 : 1;
        }
    }
}

/* Orders entries by normalized label, then by node ID. */
static int entry_compare(const amark_document_t *doc,
                         const amark_definition_entry_t *a,
                         const amark_definition_entry_t *b)
{
    int order = amark_label_compare(doc, a->label_start, a->label_end,
                                    b->label_start, b->label_end);

    if (order != 0) {
        return order;
    }
    return a->node < b->node ? -1 : (a->node > b->node ? 1 : 0);
}

/*
 * Merges the sorted runs from[start, middle) and from[middle, end) into
 * to[start, end). Takes from the left run when the entries are equal.
 */
static void merge_runs(const amark_document_t *doc,
                       const amark_definition_entry_t *from,
                       amark_definition_entry_t *to,
                       size_t start, size_t middle, size_t end)
{
    size_t left = start;
    size_t right = middle;
    size_t out = start;

    while (left < middle && right < end) {
        if (entry_compare(doc, &from[right], &from[left]) < 0) {
            to[out++] = from[right++];
        } else {
            to[out++] = from[left++];
        }
    }
    while (left < middle) {
        to[out++] = from[left++];
    }
    while (right < end) {
        to[out++] = from[right++];
    }
}

/*
 * Sorts the count entries of a with a bottom-up merge sort, using b (count
 * entries) as the other buffer. Returns the buffer that holds the result.
 */
static amark_definition_entry_t *sort_entries(const amark_document_t *doc,
                                              amark_definition_entry_t *a,
                                              amark_definition_entry_t *b,
                                              size_t count)
{
    amark_definition_entry_t *from = a;
    amark_definition_entry_t *to = b;
    size_t width;

    /* width doubles, without overflowing, until one run holds everything. */
    for (width = 1; width < count;
         width = width > count / 2 ? count : width * 2) {
        size_t start;

        for (start = 0; start < count; start += 2 * width) {
            size_t middle = count - start > width ? start + width : count;
            size_t end = count - middle > width ? middle + width : count;

            merge_runs(doc, from, to, start, middle, end);
            if (end == count) {
                break;
            }
        }
        {
            amark_definition_entry_t *swap = from;

            from = to;
            to = swap;
        }
    }
    return from;
}

amark_error_t amark_definitions_build(amark_document_t *doc,
                                      amark_arena_t *scratch,
                                      const amark_definition_entry_t *entries,
                                      size_t count)
{
    amark_parse_data_t *data = amark_document_parse_data(doc);
    amark_definition_entry_t *a;
    amark_definition_entry_t *b;
    amark_definition_entry_t *sorted;
    amark_definition_entry_t *table;
    void *mem;
    size_t unique;
    size_t i;
    amark_error_t err;

    if (count == 0) {
        data->definitions = NULL;
        data->definition_count = 0;
        return AMARK_ERROR_NONE;
    }
    err = amark_arena_alloc_array(scratch, count, sizeof(*a), &mem);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    a = (amark_definition_entry_t *)mem;
    err = amark_arena_alloc_array(scratch, count, sizeof(*b), &mem);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    b = (amark_definition_entry_t *)mem;
    memcpy(a, entries, count * sizeof(*a));
    sorted = sort_entries(doc, a, b, count);

    /* Keep the first entry of each label (the one with the smallest ID). */
    unique = 1;
    for (i = 1; i < count; i++) {
        const amark_definition_entry_t *kept = &sorted[unique - 1];

        if (amark_label_compare(doc, kept->label_start, kept->label_end,
                                sorted[i].label_start,
                                sorted[i].label_end) != 0) {
            sorted[unique++] = sorted[i];
        }
    }

    err = amark_arena_alloc_array(amark_document_arena(doc), unique,
                                  sizeof(*table), &mem);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    table = (amark_definition_entry_t *)mem;
    memcpy(table, sorted, unique * sizeof(*table));
    data->definitions = table;
    data->definition_count = unique;
    return AMARK_ERROR_NONE;
}

amark_node_id_t amark_definitions_find(const amark_document_t *doc,
                                       size_t label_start, size_t label_end)
{
    const amark_parse_data_t *data = amark_document_parse_data(doc);
    size_t low = 0;
    size_t high;

    if (data == NULL) {
        return AMARK_NODE_NONE;
    }
    /* The entry, if any, is in definitions[low, high). */
    high = data->definition_count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        const amark_definition_entry_t *entry = &data->definitions[middle];
        int order = amark_label_compare(doc, label_start, label_end,
                                        entry->label_start, entry->label_end);

        if (order == 0) {
            return entry->node;
        }
        if (order < 0) {
            high = middle;
        } else {
            low = middle + 1;
        }
    }
    return AMARK_NODE_NONE;
}
