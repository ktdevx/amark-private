/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_TEST_DIALECT_H
#define AMARK_TEST_DIALECT_H

#include <stdint.h>

#include <amark/dialect.h>
#include <amark/tree.h>

/*
 * The dialects of the tests (amark_test_support, never installed; the public
 * API test compiles the same source). Written with the public headers only,
 * as static const data: the source has no mutable static data.
 *
 *   amark_test_dialect()        the test dialect: one extension ("test")
 *                               that registers every kind below and keeps a
 *                               per-document state (amark_test_counts_t).
 *   amark_test_dialect_plain()  a dialect without state whose rules all say
 *                               no: its start, continue and inline rules
 *                               never match, its close rules and its
 *                               paragraph close rule reply with nothing,
 *                               and its postprocessing rule makes no edit.
 *   amark_test_dialect_empty()  a dialect of 0 extensions.
 *
 * A parse with each of them gives the same tree as a parse without a
 * dialect on input that has none of the test dialect's syntax.
 */

/*
 * The kinds of the test dialect, and the syntax of those that have rules:
 *
 *   BOX   "@> " at the start of a block (after the prefixes, at most 3
 *         columns of indentation) opens a box, which interrupts a
 *         paragraph; a blank line closes it.
 */
#define AMARK_TEST_KIND_BOX (AMARK_KIND_DIALECT_MIN)        /* container */
#define AMARK_TEST_KIND_NOTE (AMARK_KIND_DIALECT_MIN + 1)   /* container */
#define AMARK_TEST_KIND_FENCE (AMARK_KIND_DIALECT_MIN + 2)  /* leaf */
#define AMARK_TEST_KIND_LINE (AMARK_KIND_DIALECT_MIN + 3)   /* leaf */
#define AMARK_TEST_KIND_GRID (AMARK_KIND_DIALECT_MIN + 4)   /* container */
#define AMARK_TEST_KIND_ROW (AMARK_KIND_DIALECT_MIN + 5)    /* container */
#define AMARK_TEST_KIND_CELL (AMARK_KIND_DIALECT_MIN + 6)   /* leaf */
#define AMARK_TEST_KIND_VAR (AMARK_KIND_DIALECT_MIN + 7)    /* inline */
#define AMARK_TEST_KIND_STRIKE (AMARK_KIND_DIALECT_MIN + 8) /* inline */
#define AMARK_TEST_KIND_CHECK (AMARK_KIND_DIALECT_MIN + 9)  /* inline */
#define AMARK_TEST_KIND_TASK (AMARK_KIND_DIALECT_MIN + 10)  /* container */

/*
 * The kinds of the dialect whose rules all say no: a container, a leaf and
 * an inline.
 */
#define AMARK_TEST_KIND_PLAIN_BLOCK (AMARK_KIND_DIALECT_MIN + 100)
#define AMARK_TEST_KIND_PLAIN_LEAF (AMARK_KIND_DIALECT_MIN + 101)
#define AMARK_TEST_KIND_PLAIN_INLINE (AMARK_KIND_DIALECT_MIN + 102)

/*
 * The per-document state of the test dialect: how many times each rule of
 * its extension was called in one parse. state_new makes it zeroed with the
 * parse's allocator and state_free releases it.
 */
typedef struct amark_test_counts {
    uint64_t block_starts;    /* block start rules */
    uint64_t block_continues; /* block continue rules */
    uint64_t block_closes;    /* close rules of the dialect's block kinds */
    uint64_t paragraphs;      /* the paragraph close rule */
    uint64_t inlines;         /* inline rules */
    uint64_t postprocesses;   /* the postprocessing rule */
} amark_test_counts_t;

/* The dialects; each is static const data and valid. */
const amark_dialect_t *amark_test_dialect(void);
const amark_dialect_t *amark_test_dialect_plain(void);
const amark_dialect_t *amark_test_dialect_empty(void);

#endif /* AMARK_TEST_DIALECT_H */
