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

static const amark_dialect_kind_t k_test_kinds[] = {
    {AMARK_TEST_KIND_BOX, AMARK_DIALECT_CONTAINER, AMARK_DIALECT_BLANK_CLOSES,
     NULL, NULL},
    {AMARK_TEST_KIND_NOTE, AMARK_DIALECT_CONTAINER,
     AMARK_DIALECT_BLANK_CONTINUES, NULL, NULL},
    {AMARK_TEST_KIND_FENCE, AMARK_DIALECT_LEAF, AMARK_DIALECT_BLANK_CONTINUES,
     NULL, NULL},
    {AMARK_TEST_KIND_LINE, AMARK_DIALECT_LEAF, AMARK_DIALECT_BLANK_CLOSES,
     NULL, NULL},
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
};

static const amark_dialect_extension_t k_test_extension = {
    "test",                                         /* name */
    k_test_kinds,                                   /* kinds */
    sizeof k_test_kinds / sizeof k_test_kinds[0],   /* kind_count */
    k_test_starts,                                  /* block_starts */
    sizeof k_test_starts / sizeof k_test_starts[0],
    NULL,                                           /* close_paragraph */
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
