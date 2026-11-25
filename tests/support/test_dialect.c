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
    count_close(state);
    return set_count(reply, closing->line_count);
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
    count_close(state);
    return set_count(reply, closing->line_count);
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
    return err == AMARK_ERROR_NOMEM ? err : AMARK_ERROR_NONE;
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
    NULL, 0,                                        /* inline_rules */
    NULL, 0,                                        /* delimiters */
    NULL,                                           /* postprocess */
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
