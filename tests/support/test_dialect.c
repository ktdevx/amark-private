/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * The dialects of the tests. Uses only the public API, so that the public
 * API test can compile this source with Amark::amark alone. Every
 * definition is static const data; the only mutable data is the
 * per-document state, made by state_new with the parse's allocator.
 */

#include "test_dialect.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* ---------------------------------------------------------------------- */
/* The test dialect */

static amark_error_t test_state_new(const amark_allocator_t *allocator,
                                    void **state)
{
    amark_test_counts_t *counts;

    *state = NULL;
    counts = (amark_test_counts_t *)allocator->malloc_fn(allocator->ctx,
                                                         sizeof(*counts));
    if (counts == NULL) {
        return AMARK_ERROR_NOMEM;
    }
    memset(counts, 0, sizeof(*counts));
    *state = counts;
    return AMARK_ERROR_NONE;
}

static void test_state_free(const amark_allocator_t *allocator, void *state)
{
    if (state != NULL) {
        allocator->free_fn(allocator->ctx, state);
    }
}

/* ---------------------------------------------------------------------- */
/* The markers of invalid results (test_dialect.h) */

/*
 * Whether "!" and name are at at, before end, followed by end or by a
 * space, a tab or a line ending.
 */
static bool has_marker(const char *content, size_t at, size_t end,
                       const char *name)
{
    size_t n = strlen(name);

    if (at >= end || end - at < n + 1 || content[at] != '!' ||
        memcmp(content + at + 1, name, n) != 0) {
        return false;
    }
    at += n + 1;
    return at == end || content[at] == ' ' || content[at] == '\t' ||
           content[at] == '\n' || content[at] == '\r';
}

/*
 * Turns the start *out just matched into the invalid result that a start
 * marker after its syntax (at out->consumed.end) selects, and returns the
 * status to return. No marker: *out stays and AMARK_ERROR_NONE.
 */
static amark_error_t start_marker(const amark_dialect_line_t *line,
                                  amark_dialect_block_start_result_t *out)
{
    const char *c = line->content;
    size_t at = out->consumed.end;
    size_t end = line->line_end;

    if (has_marker(c, at, end, "start-origin")) {
        out->consumed.start = line->position + 1;
    } else if (has_marker(c, at, end, "start-line")) {
        out->consumed.end = line->line_end + 1;
    } else if (has_marker(c, at, end, "start-empty")) {
        out->consumed.end = out->consumed.start;
    } else if (has_marker(c, at, end, "start-status")) {
        return AMARK_ERROR_INVALID_INPUT;
    } else if (has_marker(c, at, end, "start-nomem")) {
        return AMARK_ERROR_NOMEM;
    }
    return AMARK_ERROR_NONE;
}

/*
 * The same for the continue *out just matched, with a continue marker after
 * the prefix it consumes.
 */
static amark_error_t continue_marker(
    const amark_dialect_line_t *line,
    amark_dialect_block_continue_result_t *out)
{
    const char *c = line->content;
    size_t at = out->consumed.end;
    size_t end = line->line_end;

    if (has_marker(c, at, end, "cont-origin")) {
        out->consumed.start = line->position + 1;
    } else if (has_marker(c, at, end, "cont-line")) {
        out->consumed.end = line->line_end + 1;
    } else if (has_marker(c, at, end, "cont-status")) {
        return AMARK_ERROR_INVALID_INPUT;
    } else if (has_marker(c, at, end, "cont-nomem")) {
        return AMARK_ERROR_NOMEM;
    }
    return AMARK_ERROR_NONE;
}

/* What a request of a rule gives it to return: only AMARK_ERROR_NOMEM. */
static amark_error_t kept(amark_error_t err)
{
    return err == AMARK_ERROR_NOMEM ? err : AMARK_ERROR_NONE;
}

/*
 * After the valid reply of the close rule of a block kind, the invalid
 * request that a close marker at at (right after the syntax on the block's
 * first line) selects; returns the status to return.
 */
static amark_error_t close_marker(const amark_dialect_closing_t *closing,
                                  size_t at, amark_dialect_reply_t *reply)
{
    const char *c = closing->content;
    size_t end = closing->range.end;
    size_t index;
    amark_dialect_line_span_t span;
    amark_range_t range = closing->range;

    if (has_marker(c, at, end, "close-index")) {
        return kept(amark_dialect_reply_set_data(reply, 1, "x", 1));
    }
    if (has_marker(c, at, end, "close-nesting")) {
        return kept(amark_dialect_reply_add_block(
            reply, AMARK_DIALECT_TOP, closing->kind, range, &index));
    }
    if (has_marker(c, at, end, "close-inline")) {
        return kept(amark_dialect_reply_set_inline(reply, 0, range));
    }
    if (has_marker(c, at, end, "close-bounds")) {
        range.end = (size_t)-1;
        return kept(amark_dialect_reply_set_inline(reply, 0, range));
    }
    if (has_marker(c, at, end, "close-parent")) {
        range.start = closing->range.end;
        range.end = closing->range.end + 1;
        return kept(amark_dialect_reply_set_inline(reply, 0, range));
    }
    if (has_marker(c, at, end, "close-prefix")) {
        if (amark_dialect_closing_line(closing, 1, &span)) {
            range.start = span.start;
            return kept(amark_dialect_reply_set_inline(reply, 0, range));
        }
        return AMARK_ERROR_NONE;
    }
    if (has_marker(c, at, end, "close-status")) {
        return AMARK_ERROR_INVALID_INPUT;
    }
    if (has_marker(c, at, end, "close-nomem")) {
        return AMARK_ERROR_NOMEM;
    }
    return AMARK_ERROR_NONE;
}

/* ---------------------------------------------------------------------- */

/* Counts a call of a block start rule in the state, if there is one. */
static void count_start(void *state)
{
    if (state != NULL) {
        ((amark_test_counts_t *)state)->block_starts++;
    }
}

/*
 * BOX: "@> " at the position (the first non-space byte after the prefixes)
 * opens a box; the three bytes are its start marker.
 */
static amark_error_t box_start(const amark_dialect_line_t *line, void *state,
                               amark_dialect_block_start_result_t *out)
{
    const char *at = line->content + line->position;

    count_start(state);
    if (line->line_end - line->position >= 3 && at[0] == '@' &&
        at[1] == '>' && at[2] == ' ') {
        out->matched = true;
        out->consumed.start = line->position;
        out->consumed.end = line->position + 3;
        return start_marker(line, out);
    }
    return AMARK_ERROR_NONE;
}

/* Counts a call of a block continue rule in the state, if there is one. */
static void count_continue(void *state)
{
    if (state != NULL) {
        ((amark_test_counts_t *)state)->block_continues++;
    }
}

/*
 * BOX goes on over a line whose first non-space byte, at most 3 columns
 * from the position, starts "@>": the prefix is the white space before it,
 * the two bytes and one space after them, if there is one.
 */
static amark_error_t box_continue(const amark_dialect_line_t *line,
                                  const amark_dialect_open_block_t *block,
                                  void *state,
                                  amark_dialect_block_continue_result_t *out)
{
    const char *at = line->content + line->first_nonspace;
    size_t end = line->first_nonspace + 2;

    (void)block;
    count_continue(state);
    if (line->indent < 4 && line->line_end - line->first_nonspace >= 2 &&
        at[0] == '@' && at[1] == '>') {
        if (end < line->line_end && line->content[end] == ' ') {
            end++;
        }
        out->matched = true;
        out->consumed.start = line->position;
        out->consumed.end = end;
        return continue_marker(line, out);
    }
    return AMARK_ERROR_NONE;
}

/*
 * NOTE: "@note:" at the position opens a note; the six bytes and one space
 * after them, if there is one, are its start marker. It interrupts a
 * paragraph.
 */
static amark_error_t note_start(const amark_dialect_line_t *line, void *state,
                                amark_dialect_block_start_result_t *out)
{
    size_t end = line->position + 6;

    count_start(state);
    if (line->line_end - line->position >= 6 &&
        memcmp(line->content + line->position, "@note:", 6) == 0) {
        if (end < line->line_end && line->content[end] == ' ') {
            end++;
        }
        out->matched = true;
        out->consumed.start = line->position;
        out->consumed.end = end;
        return start_marker(line, out);
    }
    return AMARK_ERROR_NONE;
}

/*
 * NOTE goes on over a line indented at least 2 columns from the position:
 * the prefix is the bytes of the first 2 columns (a tab, whole, may give
 * more). A blank line does not reach the rule: the note goes on over it.
 */
static amark_error_t note_continue(const amark_dialect_line_t *line,
                                   const amark_dialect_open_block_t *block,
                                   void *state,
                                   amark_dialect_block_continue_result_t *out)
{
    size_t at = line->position;
    size_t column = line->column;

    (void)block;
    count_continue(state);
    if (line->indent < 2) {
        return AMARK_ERROR_NONE;
    }
    while (column < line->column + 2) {
        if (line->content[at] == '\t') {
            column += 4 - column % 4;
        } else {
            column++;
        }
        at++;
    }
    out->matched = true;
    out->consumed.start = line->position;
    out->consumed.end = at;
    return AMARK_ERROR_NONE;
}

/* Counts a call of a close rule of a block kind, if there is a state. */
static void count_close(void *state)
{
    if (state != NULL) {
        ((amark_test_counts_t *)state)->block_closes++;
    }
}

/*
 * Sets the attributes of block 0 of reply to count in decimal ASCII. A
 * rejected request is left to the reply; only AMARK_ERROR_NOMEM is
 * returned.
 */
static amark_error_t set_count(amark_dialect_reply_t *reply, size_t count)
{
    char digits[24];
    size_t n = sizeof digits;
    amark_error_t err;

    do {
        digits[--n] = (char)('0' + count % 10);
        count /= 10;
    } while (count > 0);
    err = amark_dialect_reply_set_data(reply, 0, digits + n,
                                       sizeof digits - n);
    return err == AMARK_ERROR_NOMEM ? err : AMARK_ERROR_NONE;
}

/* BOX closes with the number of its lines as its attributes. */
static amark_error_t box_close(const amark_dialect_closing_t *closing,
                               void *state, amark_dialect_reply_t *reply)
{
    amark_error_t err;

    count_close(state);
    err = set_count(reply, closing->line_count);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    return close_marker(closing, closing->range.start + 3, reply);
}

/* The bit of FENCE's data that records its closing line. */
#define FENCE_CLOSED 1u

/*
 * FENCE: "@@@" at the position opens a fence; the three bytes are its
 * start marker. It does not interrupt a paragraph.
 */
static amark_error_t fence_start(const amark_dialect_line_t *line,
                                 void *state,
                                 amark_dialect_block_start_result_t *out)
{
    count_start(state);
    if (line->line_end - line->position >= 3 &&
        memcmp(line->content + line->position, "@@@", 3) == 0) {
        out->matched = true;
        out->consumed.start = line->position;
        out->consumed.end = line->position + 3;
        return start_marker(line, out);
    }
    return AMARK_ERROR_NONE;
}

/*
 * FENCE goes on over every line, consuming nothing, until the line after
 * its closing line: one whose first non-space byte, at most 3 columns from
 * the position, starts "@@@". The data records that line was seen.
 */
static amark_error_t fence_continue(
    const amark_dialect_line_t *line, const amark_dialect_open_block_t *block,
    void *state, amark_dialect_block_continue_result_t *out)
{
    count_continue(state);
    if ((block->data & FENCE_CLOSED) != 0) {
        return AMARK_ERROR_NONE;
    }
    out->matched = true;
    out->consumed.start = line->position;
    out->consumed.end = line->position;
    if (line->indent < 4 && line->line_end - line->first_nonspace >= 3 &&
        memcmp(line->content + line->first_nonspace, "@@@", 3) == 0) {
        out->data = block->data | FENCE_CLOSED;
    }
    return AMARK_ERROR_NONE;
}

/* FENCE closes with the number of its lines as its attributes. */
static amark_error_t fence_close(const amark_dialect_closing_t *closing,
                                 void *state, amark_dialect_reply_t *reply)
{
    amark_error_t err;

    count_close(state);
    err = set_count(reply, closing->line_count);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    return close_marker(closing, closing->range.start + 3, reply);
}

/*
 * LINE: "@line " at the position opens a one-line leaf; the six bytes are
 * its start marker. It interrupts a paragraph.
 */
static amark_error_t line_start(const amark_dialect_line_t *line,
                                void *state,
                                amark_dialect_block_start_result_t *out)
{
    count_start(state);
    if (line->line_end - line->position >= 6 &&
        memcmp(line->content + line->position, "@line ", 6) == 0) {
        out->matched = true;
        out->consumed.start = line->position;
        out->consumed.end = line->position + 6;
        return start_marker(line, out);
    }
    return AMARK_ERROR_NONE;
}

/*
 * LINE closes with its content, from where its line's content starts (the
 * first non-space byte after the marker) to its end, as inline content.
 */
static amark_error_t line_close(const amark_dialect_closing_t *closing,
                                void *state, amark_dialect_reply_t *reply)
{
    amark_dialect_line_span_t span;
    amark_range_t range;
    amark_error_t err;

    count_close(state);
    if (!amark_dialect_closing_line(closing, 0, &span)) {
        return AMARK_ERROR_NONE;
    }
    range.start = span.content_start;
    range.end = closing->range.end;
    err = amark_dialect_reply_set_inline(reply, 0, range);
    if (err == AMARK_ERROR_NOMEM) {
        return err;
    }
    return close_marker(closing, closing->range.start + 6, reply);
}

/* Counts a call of the paragraph close rule, if there is a state. */
static void count_paragraph(void *state)
{
    if (state != NULL) {
        ((amark_test_counts_t *)state)->paragraphs++;
    }
}

/* Whether the line span starts a row of a grid: its content starts '|'. */
static bool is_row_line(const amark_dialect_closing_t *closing,
                       const amark_dialect_line_span_t *span)
{
    return span->content_start < span->end &&
           closing->content[span->content_start] == '|';
}

/*
 * The result of a request made by a rule of the grid: AMARK_ERROR_NOMEM is
 * returned, any other failure is left to the reply (which rejects it).
 */
#define GRID_TRY(call)                                                     \
    do {                                                                   \
        amark_error_t grid_err_ = (call);                                  \
        if (grid_err_ != AMARK_ERROR_NONE) {                               \
            return grid_err_ == AMARK_ERROR_NOMEM ? grid_err_                \
                                                  : AMARK_ERROR_NONE;      \
        }                                                                  \
    } while (0)

/*
 * Adds a cell over [start, end) under row, with the inline content
 * [inline_start, inline_end) less the spaces and tabs at its two ends; an
 * inline content left empty is not set.
 */
static amark_error_t add_cell(const amark_dialect_closing_t *closing,
                              amark_dialect_reply_t *reply, size_t row,
                              size_t start, size_t end, size_t inline_start,
                              size_t inline_end)
{
    const char *c = closing->content;
    size_t cell;
    amark_range_t range;

    range.start = start;
    range.end = end;
    GRID_TRY(amark_dialect_reply_add_block(reply, row, AMARK_TEST_KIND_CELL,
                                           range, &cell));
    while (inline_start < inline_end &&
           (c[inline_start] == ' ' || c[inline_start] == '\t')) {
        inline_start++;
    }
    while (inline_end > inline_start &&
           (c[inline_end - 1] == ' ' || c[inline_end - 1] == '\t')) {
        inline_end--;
    }
    if (inline_start == inline_end) {
        return AMARK_ERROR_NONE;
    }
    range.start = inline_start;
    range.end = inline_end;
    GRID_TRY(amark_dialect_reply_set_inline(reply, cell, range));
    return AMARK_ERROR_NONE;
}

/*
 * Adds the row whose first line is line (a row line) and whose last line
 * is last (the lines after the first are its continuation lines), and its
 * cells: the bytes between two '|' of the first line, and the bytes after
 * its last '|' up to the end of the row when they are not blank or the row
 * has continuation lines. The inline content of that last cell starts on
 * the first line, or, when the first line has nothing left, at the content
 * of the first continuation line.
 */
static amark_error_t add_row(const amark_dialect_closing_t *closing,
                             amark_dialect_reply_t *reply, size_t grid,
                             const amark_dialect_line_span_t *line,
                             const amark_dialect_line_span_t *next,
                             const amark_dialect_line_span_t *last)
{
    const char *c = closing->content;
    size_t row;
    size_t at;
    size_t cell_start = line->content_start + 1;
    amark_range_t range;

    range.start = line->content_start;
    range.end = last->end;
    GRID_TRY(amark_dialect_reply_add_block(reply, grid, AMARK_TEST_KIND_ROW,
                                           range, &row));
    for (at = cell_start; at < line->end; at++) {
        if (c[at] == '|') {
            GRID_TRY(add_cell(closing, reply, row, cell_start, at,
                              cell_start, at));
            cell_start = at + 1;
        }
    }
    at = cell_start;
    while (at < line->end && (c[at] == ' ' || c[at] == '\t')) {
        at++;
    }
    if (next != NULL) {
        return add_cell(closing, reply, row, cell_start, last->end,
                        at < line->end ? at : next->content_start,
                        last->end);
    }
    if (at < line->end) {
        return add_cell(closing, reply, row, cell_start, line->end,
                        cell_start, line->end);
    }
    return AMARK_ERROR_NONE;
}

/*
 * The invalid reply that a close marker right after the '|' of the first
 * row line (first, line g of the paragraph) selects. Returns false if there
 * is no marker; otherwise stores in *err the status to return.
 */
static bool grid_marker(const amark_dialect_closing_t *closing,
                        amark_dialect_reply_t *reply, size_t g,
                        const amark_dialect_line_span_t *first,
                        amark_error_t *err)
{
    const char *c = closing->content;
    size_t at = first->content_start + 1;
    size_t end = first->end;
    size_t grid;
    size_t i;
    amark_dialect_line_span_t span;
    amark_range_t whole;
    amark_range_t range;

    whole.start = first->content_start;
    whole.end = closing->range.end;
    range = whole;
    *err = AMARK_ERROR_NONE;
    if (has_marker(c, at, end, "close-index")) {
        /* No block 0 yet. */
        *err = kept(amark_dialect_reply_add_block(
            reply, 0, AMARK_TEST_KIND_GRID, whole, &i));
    } else if (has_marker(c, at, end, "close-kind")) {
        *err = kept(amark_dialect_reply_add_block(
            reply, AMARK_DIALECT_TOP, AMARK_KIND_HEADING, whole, &i));
    } else if (has_marker(c, at, end, "close-nesting")) {
        *err = kept(amark_dialect_reply_add_block(
            reply, AMARK_DIALECT_TOP, AMARK_TEST_KIND_VAR, whole, &i));
    } else if (has_marker(c, at, end, "close-bounds")) {
        range.end = (size_t)-1;
        *err = kept(amark_dialect_reply_add_block(
            reply, AMARK_DIALECT_TOP, AMARK_TEST_KIND_GRID, range, &i));
    } else if (has_marker(c, at, end, "close-parent")) {
        /* A grid of the '|' alone, and a row over the whole of it. */
        range.end = at;
        *err = amark_dialect_reply_add_block(reply, AMARK_DIALECT_TOP,
                                             AMARK_TEST_KIND_GRID, range,
                                             &grid);
        if (*err == AMARK_ERROR_NONE) {
            *err = amark_dialect_reply_add_block(reply, grid,
                                                 AMARK_TEST_KIND_ROW, whole,
                                                 &i);
        }
        *err = kept(*err);
    } else if (has_marker(c, at, end, "close-order")) {
        /* Two grids over the same bytes. */
        *err = amark_dialect_reply_add_block(reply, AMARK_DIALECT_TOP,
                                             AMARK_TEST_KIND_GRID, whole,
                                             &grid);
        if (*err == AMARK_ERROR_NONE) {
            *err = amark_dialect_reply_add_block(reply, AMARK_DIALECT_TOP,
                                                 AMARK_TEST_KIND_GRID, whole,
                                                 &i);
        }
        *err = kept(*err);
    } else if (has_marker(c, at, end, "close-inline")) {
        /* Inline content on the grid, a container. */
        *err = amark_dialect_reply_add_block(reply, AMARK_DIALECT_TOP,
                                             AMARK_TEST_KIND_GRID, whole,
                                             &grid);
        if (*err == AMARK_ERROR_NONE) {
            *err = amark_dialect_reply_set_inline(reply, grid, whole);
        }
        *err = kept(*err);
    } else if (has_marker(c, at, end, "close-prefix")) {
        /*
         * A cell over the grid, whose inline content starts where the line
         * after the first row line starts (in its prefix or the white space
         * before its content, if it has one).
         */
        *err = amark_dialect_reply_add_block(reply, AMARK_DIALECT_TOP,
                                             AMARK_TEST_KIND_GRID, whole,
                                             &grid);
        if (*err == AMARK_ERROR_NONE) {
            *err = amark_dialect_reply_add_block(reply, grid,
                                                 AMARK_TEST_KIND_CELL, whole,
                                                 &i);
        }
        if (*err == AMARK_ERROR_NONE &&
            amark_dialect_closing_line(closing, g + 1, &span)) {
            range.start = span.start;
            *err = amark_dialect_reply_set_inline(reply, i, range);
        }
        *err = kept(*err);
    } else if (has_marker(c, at, end, "close-paragraph")) {
        /* A paragraph part that starts inside the first line. */
        range.start = closing->range.start + 1;
        *err = kept(amark_dialect_reply_add_block(
            reply, AMARK_DIALECT_TOP, AMARK_KIND_PARAGRAPH, range, &i));
    } else if (has_marker(c, at, end, "close-status")) {
        *err = AMARK_ERROR_INVALID_INPUT;
    } else if (has_marker(c, at, end, "close-nomem")) {
        *err = AMARK_ERROR_NOMEM;
    } else {
        return false;
    }
    return true;
}

/*
 * GRID: the paragraph close rule. From the first line of the paragraph
 * whose content starts with '|' to its end, the paragraph is a grid; the
 * lines before it stay a paragraph. Each line that starts with '|' starts a
 * row; a line that does not continues the row before it (its last cell).
 * A paragraph without such a line stays as it is.
 */
static amark_error_t grid_close(const amark_dialect_closing_t *closing,
                                void *state, amark_dialect_reply_t *reply)
{
    amark_dialect_line_span_t first = {0, 0, 0};
    amark_dialect_line_span_t span;
    amark_range_t range;
    amark_error_t err;
    size_t grid;
    size_t g;
    size_t i;

    count_paragraph(state);
    for (g = 0; g < closing->line_count; g++) {
        if (!amark_dialect_closing_line(closing, g, &first)) {
            return AMARK_ERROR_NONE;
        }
        if (is_row_line(closing, &first)) {
            break;
        }
    }
    if (g == closing->line_count) {
        return AMARK_ERROR_NONE;
    }
    if (grid_marker(closing, reply, g, &first, &err)) {
        return err;
    }
    if (g > 0) {
        /* The lines before the grid stay a paragraph. */
        if (!amark_dialect_closing_line(closing, 0, &span)) {
            return AMARK_ERROR_NONE;
        }
        range.start = span.content_start;
        if (!amark_dialect_closing_line(closing, g - 1, &span)) {
            return AMARK_ERROR_NONE;
        }
        range.end = span.end;
        GRID_TRY(amark_dialect_reply_add_block(reply, AMARK_DIALECT_TOP,
                                               AMARK_KIND_PARAGRAPH, range,
                                               &i));
    }
    range.start = first.content_start;
    range.end = closing->range.end;
    GRID_TRY(amark_dialect_reply_add_block(reply, AMARK_DIALECT_TOP,
                                           AMARK_TEST_KIND_GRID, range,
                                           &grid));
    i = g;
    while (i < closing->line_count) {
        amark_dialect_line_span_t line;
        amark_dialect_line_span_t next = {0, 0, 0};
        size_t j = i + 1;

        if (!amark_dialect_closing_line(closing, i, &line)) {
            return AMARK_ERROR_NONE;
        }
        while (j < closing->line_count &&
               amark_dialect_closing_line(closing, j, &span) &&
               !is_row_line(closing, &span)) {
            if (j == i + 1) {
                next = span;
            }
            j++;
        }
        if (j - 1 != i) {
            (void)amark_dialect_closing_line(closing, j - 1, &span);
        } else {
            span = line;
        }
        GRID_TRY(add_row(closing, reply, grid, &line,
                         j - 1 != i ? &next : NULL, &span));
        i = j;
    }
    return AMARK_ERROR_NONE;
}

#undef GRID_TRY

/* ---------------------------------------------------------------------- */
/* VAR */

/* Whether c may be a byte of the name of a VAR. */
static bool is_var_name_byte(char c)
{
    return c != '$' && c != ' ' && c != '\t' && c != '\n' && c != '\r';
}

/*
 * Whether the name [start, end) of a VAR is "!" and marker, as the markers
 * of the other rules: the whole name.
 */
static bool is_var_marker(const char *content, size_t start, size_t end,
                          const char *marker)
{
    size_t n = strlen(marker);

    return end - start == n + 1 && content[start] == '!' &&
           memcmp(content + start + 1, marker, n) == 0;
}

/*
 * The forms of a VAR that end on the next line, from a name that ends the
 * line's content: "!inline-next" ends 1 byte into the content of the next
 * line, "!inline-prefix" 1 byte before it (inside its prefix or leading
 * white space). Leaves *out unmatched for any other name, or when no line
 * follows or the content of the next line is empty (inline-next) or starts
 * where the line does (inline-prefix: no prefix and no white space).
 */
static void var_next_line(const amark_dialect_inline_t *in, size_t name_end,
                          amark_dialect_inline_result_t *out)
{
    const char *c = in->content;
    size_t start = in->position + 1;
    amark_range_t next;

    if (name_end != in->line_end ||
        !amark_dialect_inline_next_line(in, in->line_end, &next)) {
        return;
    }
    if (is_var_marker(c, start, name_end, "inline-next") &&
        next.start < next.end) {
        out->matched = true;
        out->range.end = next.start + 1;
    } else if (is_var_marker(c, start, name_end, "inline-prefix") &&
               next.start > in->line_end + 1) {
        out->matched = true;
        out->range.end = next.start - 1;
    }
}

/*
 * VAR, at '$': "$name$" on one line, the name being one or more bytes other
 * than '$', a space, a tab and a line ending, is a VAR whose attributes are
 * the length of its name in decimal ASCII, written in the state. The
 * markers of invalid results and the forms that end on the next line are
 * names (test_dialect.h).
 */
static amark_error_t var_inline(const amark_dialect_inline_t *in,
                                void *state,
                                amark_dialect_inline_result_t *out)
{
    amark_test_counts_t *counts = (amark_test_counts_t *)state;
    const char *c = in->content;
    size_t start = in->position + 1;
    size_t end = start;
    size_t length;
    size_t n;

    if (counts != NULL) {
        counts->inlines++;
    }
    while (end < in->line_end && is_var_name_byte(c[end])) {
        end++;
    }
    out->range.start = in->position;
    if (end == start) {
        return AMARK_ERROR_NONE;
    }
    if (end == in->line_end || c[end] != '$') {
        var_next_line(in, end, out);
        length = end - start;
    } else {
        out->matched = true;
        out->range.end = end + 1;
        length = end - start;
        if (is_var_marker(c, start, end, "inline-origin")) {
            out->range.start = in->position + 1;
        } else if (is_var_marker(c, start, end, "inline-empty")) {
            out->range.end = in->position;
        } else if (is_var_marker(c, start, end, "inline-parent")) {
            out->range.end = in->inline_content.end + 1;
        } else if (is_var_marker(c, start, end, "inline-status")) {
            return AMARK_ERROR_INVALID_INPUT;
        } else if (is_var_marker(c, start, end, "inline-nomem")) {
            return AMARK_ERROR_NOMEM;
        }
    }
    if (out->matched && counts != NULL) {
        n = sizeof counts->var_data;
        do {
            counts->var_data[--n] = (char)('0' + length % 10);
            length /= 10;
        } while (length > 0);
        out->data = counts->var_data + n;
        out->data_size = sizeof counts->var_data - n;
    }
    return AMARK_ERROR_NONE;
}

/* ---------------------------------------------------------------------- */
/* CHECK and TASK: the postprocessing rule */

/* Whether the bytes [at, end) start "[x] " or "[ ] ". */
static bool is_check_mark(const char *content, size_t at, size_t end)
{
    return end - at >= 4 && content[at] == '[' &&
           (content[at + 1] == 'x' || content[at + 1] == ' ') &&
           content[at + 2] == ']' && content[at + 3] == ' ';
}

/*
 * What an edit that should pass gives the rule to return: nothing for a
 * rejected edit (the diagnostic is added; the rule goes on), and the error
 * of a failure that lasts.
 */
static amark_error_t edit_status(amark_error_t err)
{
    return err == AMARK_ERROR_INVALID_INPUT ? AMARK_ERROR_NONE : err;
}

#define POST_TRY(call)                                                     \
    do {                                                                   \
        amark_error_t post_err_ = (call);                                  \
        if (post_err_ != AMARK_ERROR_NONE) {                               \
            return edit_status(post_err_);                                 \
        }                                                                  \
    } while (0)

/*
 * Makes the CHECK of paragraph para if its first child is a text that
 * starts where it starts with "[x] " or "[ ] " (at s): splits the text after
 * the mark (when more follows), shrinks the first part to the space after
 * "]", adds a CHECK over "[x]" before it, and, when para is the first child
 * of a list item, turns the item into a TASK whose attributes are the
 * number of TASKs made in this document so far, in decimal ASCII. Then
 * applies the postprocessing marker after the mark, if any. Returns the
 * status the rule returns: AMARK_ERROR_NONE to go on with the next
 * paragraph.
 */
static amark_error_t check_paragraph(const amark_dialect_post_t *post,
                                     amark_test_counts_t *counts,
                                     amark_dialect_editor_t *editor,
                                     amark_node_id_t para)
{
    const amark_document_t *doc = post->doc;
    const char *c = post->content;
    amark_node_id_t text = amark_node_first_child(doc, para);
    amark_node_id_t item = amark_node_parent(doc, para);
    amark_node_id_t node = AMARK_NODE_NONE;
    amark_range_t p;
    amark_range_t t;
    amark_range_t r;
    char data[24];
    uint64_t number;
    size_t n;
    size_t s;

    if (text == AMARK_NODE_NONE ||
        amark_node_kind(doc, text) != AMARK_KIND_TEXT ||
        !amark_node_range(doc, para, &p) || !amark_node_range(doc, text, &t) ||
        t.start != p.start || t.end - t.start < 4 ||
        !is_check_mark(c, p.start, p.end)) {
        return AMARK_ERROR_NONE;
    }
    s = p.start;
    if (t.end > s + 4) {
        POST_TRY(amark_dialect_edit_split_text(editor, text, s + 4, &node));
    }
    r.start = s + 3;
    r.end = s + 4;
    POST_TRY(amark_dialect_edit_shrink(editor, text, r));
    r.start = s;
    r.end = s + 3;
    POST_TRY(amark_dialect_edit_add_child(editor, para, AMARK_NODE_NONE,
                                          AMARK_TEST_KIND_CHECK, r, NULL, 0,
                                          &node));
    if (amark_node_kind(doc, item) == AMARK_KIND_LIST_ITEM &&
        amark_node_first_child(doc, item) == para) {
        POST_TRY(amark_dialect_edit_set_kind(editor, item,
                                             AMARK_TEST_KIND_TASK));
        number = counts != NULL ? ++counts->tasks : 0;
        n = sizeof data;
        do {
            data[--n] = (char)('0' + number % 10);
            number /= 10;
        } while (number > 0);
        POST_TRY(amark_dialect_edit_set_data(editor, item, data + n,
                                             sizeof data - n));
    }

    if (has_marker(c, s + 4, p.end, "post-order")) {
        /* A second CHECK over the first: rejected (ORDER). */
        POST_TRY(amark_dialect_edit_add_child(editor, para, AMARK_NODE_NONE,
                                              AMARK_TEST_KIND_CHECK, r, NULL,
                                              0, &node));
    } else if (has_marker(c, s + 4, p.end, "post-status")) {
        return AMARK_ERROR_INVALID_INPUT;
    } else if (has_marker(c, s + 4, p.end, "post-nomem")) {
        return AMARK_ERROR_NOMEM;
    }
    return AMARK_ERROR_NONE;
}

#undef POST_TRY

/*
 * The postprocessing rule: walks the blocks of the document in document
 * order with the functions of tree.h (without recursion) and makes the
 * CHECK of each paragraph (check_paragraph). Stops at the first paragraph
 * whose status is not AMARK_ERROR_NONE and returns it.
 */
static amark_error_t test_post(const amark_dialect_post_t *post, void *state,
                               amark_dialect_editor_t *editor)
{
    amark_test_counts_t *counts = (amark_test_counts_t *)state;
    const amark_document_t *doc = post->doc;
    amark_node_id_t root = amark_document_root(doc);
    amark_node_id_t node = root;
    amark_node_id_t child;
    amark_error_t err;

    if (counts != NULL) {
        counts->postprocesses++;
    }
    while (node != AMARK_NODE_NONE) {
        child = AMARK_NODE_NONE;
        if (amark_node_kind(doc, node) == AMARK_KIND_PARAGRAPH) {
            err = check_paragraph(post, counts, editor, node);
            if (err != AMARK_ERROR_NONE) {
                return err;
            }
        } else {
            child = amark_node_first_child(doc, node);
        }
        /* The children of a block are all blocks or all inlines. */
        if (child != AMARK_NODE_NONE && amark_node_is_block(doc, child)) {
            node = child;
            continue;
        }
        while (node != root &&
               amark_node_next_sibling(doc, node) == AMARK_NODE_NONE) {
            node = amark_node_parent(doc, node);
        }
        node = node == root ? AMARK_NODE_NONE
                            : amark_node_next_sibling(doc, node);
    }
    return AMARK_ERROR_NONE;
}

/* ---------------------------------------------------------------------- */
/* The definition */

/* The inline rules, in the order they are tried. */
static const amark_dialect_inline_rule_t k_test_inlines[] = {
    {'$', AMARK_TEST_KIND_VAR, var_inline},
};

/* The delimiter kinds. */
static const amark_dialect_delimiter_t k_test_delimiters[] = {
    {'~', AMARK_DIALECT_LENGTH(1) | AMARK_DIALECT_LENGTH(2),
     AMARK_TEST_KIND_STRIKE},
};

static const amark_dialect_kind_t k_test_kinds[] = {
    {AMARK_TEST_KIND_BOX, AMARK_DIALECT_CONTAINER, AMARK_DIALECT_BLANK_CLOSES,
     box_continue, box_close},
    {AMARK_TEST_KIND_NOTE, AMARK_DIALECT_CONTAINER,
     AMARK_DIALECT_BLANK_CONTINUES, note_continue, NULL},
    {AMARK_TEST_KIND_FENCE, AMARK_DIALECT_LEAF, AMARK_DIALECT_BLANK_CONTINUES,
     fence_continue, fence_close},
    {AMARK_TEST_KIND_LINE, AMARK_DIALECT_LEAF, AMARK_DIALECT_BLANK_CLOSES,
     NULL, line_close},
    {AMARK_TEST_KIND_GRID, AMARK_DIALECT_CONTAINER, AMARK_DIALECT_BLANK_CLOSES,
     NULL, NULL},
    {AMARK_TEST_KIND_ROW, AMARK_DIALECT_CONTAINER, AMARK_DIALECT_BLANK_CLOSES,
     NULL, NULL},
    {AMARK_TEST_KIND_CELL, AMARK_DIALECT_LEAF, AMARK_DIALECT_BLANK_CLOSES,
     NULL, NULL},
    {AMARK_TEST_KIND_VAR, AMARK_DIALECT_INLINE, AMARK_DIALECT_BLANK_CLOSES,
     NULL, NULL},
    {AMARK_TEST_KIND_STRIKE, AMARK_DIALECT_INLINE, AMARK_DIALECT_BLANK_CLOSES,
     NULL, NULL},
    {AMARK_TEST_KIND_CHECK, AMARK_DIALECT_INLINE, AMARK_DIALECT_BLANK_CLOSES,
     NULL, NULL},
    {AMARK_TEST_KIND_TASK, AMARK_DIALECT_CONTAINER, AMARK_DIALECT_BLANK_CLOSES,
     NULL, NULL},
};

/* The block start rules, in the order they are tried. */
static const amark_dialect_block_start_t k_test_starts[] = {
    {AMARK_TEST_KIND_BOX, true, box_start},
    {AMARK_TEST_KIND_NOTE, true, note_start},
    {AMARK_TEST_KIND_FENCE, false, fence_start},
    {AMARK_TEST_KIND_LINE, true, line_start},
};

static const amark_dialect_extension_t k_test_extension = {
    "test",                                         /* name */
    k_test_kinds,                                   /* kinds */
    sizeof k_test_kinds / sizeof k_test_kinds[0],   /* kind_count */
    k_test_starts,                                  /* block_starts */
    sizeof k_test_starts / sizeof k_test_starts[0],
    grid_close,                                     /* close_paragraph */
    k_test_inlines,                                 /* inline_rules */
    sizeof k_test_inlines / sizeof k_test_inlines[0],
    k_test_delimiters,                              /* delimiters */
    sizeof k_test_delimiters / sizeof k_test_delimiters[0],
    test_post,                                      /* postprocess */
    test_state_new,                                 /* state_new */
    test_state_free                                 /* state_free */
};

static const amark_dialect_extension_t *const k_test_extensions[] = {
    &k_test_extension};

static const amark_dialect_t k_test_dialect = {
    sizeof(amark_dialect_t), AMARK_DIALECT_ABI, k_test_extensions,
    sizeof k_test_extensions / sizeof k_test_extensions[0]};

const amark_dialect_t *amark_test_dialect(void)
{
    return &k_test_dialect;
}

/* ---------------------------------------------------------------------- */
/* The dialect whose rules all say no */

/* Leaves *out as the parse filled it: not matched. */
static amark_error_t plain_start(const amark_dialect_line_t *line, void *state,
                                 amark_dialect_block_start_result_t *out)
{
    (void)line;
    (void)state;
    out->matched = false;
    return AMARK_ERROR_NONE;
}

static amark_error_t plain_continue(const amark_dialect_line_t *line,
                                    const amark_dialect_open_block_t *block,
                                    void *state,
                                    amark_dialect_block_continue_result_t *out)
{
    (void)line;
    (void)block;
    (void)state;
    out->matched = false;
    return AMARK_ERROR_NONE;
}

/* Adds nothing to the reply: the block or paragraph stays as it is. */
static amark_error_t plain_close(const amark_dialect_closing_t *closing,
                                 void *state, amark_dialect_reply_t *reply)
{
    (void)closing;
    (void)state;
    (void)reply;
    return AMARK_ERROR_NONE;
}

static amark_error_t plain_inline(const amark_dialect_inline_t *in,
                                  void *state,
                                  amark_dialect_inline_result_t *out)
{
    (void)in;
    (void)state;
    out->matched = false;
    return AMARK_ERROR_NONE;
}

/* Makes no edit. */
static amark_error_t plain_post(const amark_dialect_post_t *post, void *state,
                                amark_dialect_editor_t *editor)
{
    (void)post;
    (void)state;
    (void)editor;
    return AMARK_ERROR_NONE;
}

static const amark_dialect_kind_t k_plain_kinds[] = {
    {AMARK_TEST_KIND_PLAIN_BLOCK, AMARK_DIALECT_CONTAINER,
     AMARK_DIALECT_BLANK_CONTINUES, plain_continue, plain_close},
    {AMARK_TEST_KIND_PLAIN_LEAF, AMARK_DIALECT_LEAF,
     AMARK_DIALECT_BLANK_CLOSES, plain_continue, plain_close},
    {AMARK_TEST_KIND_PLAIN_INLINE, AMARK_DIALECT_INLINE,
     AMARK_DIALECT_BLANK_CLOSES, NULL, NULL},
};

static const amark_dialect_block_start_t k_plain_starts[] = {
    {AMARK_TEST_KIND_PLAIN_BLOCK, true, plain_start},
    {AMARK_TEST_KIND_PLAIN_LEAF, false, plain_start},
};

/* Triggers that appear in the tests' inputs, and one CommonMark trigger. */
static const amark_dialect_inline_rule_t k_plain_inlines[] = {
    {'$', AMARK_TEST_KIND_PLAIN_INLINE, plain_inline},
    {'@', AMARK_TEST_KIND_PLAIN_INLINE, plain_inline},
    {'~', AMARK_TEST_KIND_PLAIN_INLINE, plain_inline},
    {'*', AMARK_TEST_KIND_PLAIN_INLINE, plain_inline},
};

static const amark_dialect_extension_t k_plain_extension = {
    "plain",                                        /* name */
    k_plain_kinds,                                  /* kinds */
    sizeof k_plain_kinds / sizeof k_plain_kinds[0], /* kind_count */
    k_plain_starts,                                 /* block_starts */
    sizeof k_plain_starts / sizeof k_plain_starts[0],
    plain_close,                                    /* close_paragraph */
    k_plain_inlines,                                /* inline_rules */
    sizeof k_plain_inlines / sizeof k_plain_inlines[0],
    NULL, 0,                                        /* delimiters */
    plain_post,                                     /* postprocess */
    NULL,                                           /* state_new */
    NULL                                            /* state_free */
};

static const amark_dialect_extension_t *const k_plain_extensions[] = {
    &k_plain_extension};

static const amark_dialect_t k_plain_dialect = {
    sizeof(amark_dialect_t), AMARK_DIALECT_ABI, k_plain_extensions,
    sizeof k_plain_extensions / sizeof k_plain_extensions[0]};

const amark_dialect_t *amark_test_dialect_plain(void)
{
    return &k_plain_dialect;
}

/* ---------------------------------------------------------------------- */
/* The dialect of 0 extensions */

static const amark_dialect_t k_empty_dialect = {
    sizeof(amark_dialect_t), AMARK_DIALECT_ABI, NULL, 0};

const amark_dialect_t *amark_test_dialect_empty(void)
{
    return &k_empty_dialect;
}
