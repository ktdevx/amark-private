/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_TEST_DIALECT_DEFS_H
#define AMARK_TEST_DIALECT_DEFS_H

#include <stddef.h>

#include <amark/dialect.h>

/*
 * Valid and invalid dialect definitions for the unit tests of the
 * definition check and of the configuration (amark_test_support, never
 * installed). Uses only the public header <amark/dialect.h>. The rules of
 * the definition are never called.
 *
 * amark_test_def_init builds a valid definition of two extensions in a
 * caller's amark_test_def_t; each breaker then changes that definition so
 * that exactly one condition of a configuration error (Requirements 3.2 to
 * 3.9, 8.6 to 8.8) holds.
 */

/* Kinds of the first extension. */
#define AMARK_TEST_DEF_BOX (AMARK_KIND_DIALECT_MIN)      /* container */
#define AMARK_TEST_DEF_NOTE (AMARK_KIND_DIALECT_MIN + 1) /* leaf */
#define AMARK_TEST_DEF_MARK (AMARK_KIND_DIALECT_MIN + 2) /* inline */
/* Kinds of the second extension. */
#define AMARK_TEST_DEF_RULE (AMARK_KIND_DIALECT_MIN + 10) /* leaf */
#define AMARK_TEST_DEF_SUB (AMARK_KIND_DIALECT_MIN + 11)  /* inline */
/* The kind of the third extension (used only by some breakers). */
#define AMARK_TEST_DEF_EXTRA (AMARK_KIND_DIALECT_MIN + 20) /* inline */

/*
 * A dialect definition in one structure. It holds pointers into itself, so
 * it is not copied after amark_test_def_init.
 */
typedef struct amark_test_def {
    amark_dialect_kind_t kinds1[3];
    amark_dialect_block_start_t starts1[2];
    amark_dialect_inline_rule_t inlines1[1];
    amark_dialect_delimiter_t delims1[1];
    amark_dialect_kind_t kinds2[2];
    amark_dialect_block_start_t starts2[1];
    amark_dialect_inline_rule_t inlines2[1];
    amark_dialect_delimiter_t delims2[AMARK_DIALECT_DELIMITER_MAX];
    amark_dialect_kind_t kinds3[1];
    amark_dialect_delimiter_t delims3[1];
    amark_dialect_extension_t ext1;
    amark_dialect_extension_t ext2;
    amark_dialect_extension_t ext3; /* not in the dialect after init */
    const amark_dialect_extension_t *exts[3];
    amark_dialect_t dialect;
} amark_test_def_t;

/*
 * Sets *d to a valid dialect of two extensions. The first ("first") has a
 * container (BOX), a leaf (NOTE) and an inline kind (MARK), two start
 * rules, a paragraph close rule, an inline rule on '@', a delimiter kind on
 * '~' (lengths 1 and 2) and state functions. The second ("second") has a
 * leaf (RULE) and an inline kind (SUB), a start rule, an inline rule on '$',
 * a delimiter kind on '^' (length 4) and no state functions. ext3 ("third",
 * one inline kind EXTRA and a delimiter kind on '|') is set up but not in
 * the dialect, whose extension_count is 2.
 */
void amark_test_def_init(amark_test_def_t *d);

/*
 * Gives the second extension delimiter kinds until the dialect has total of
 * them (the first extension has 1): '^', '=', '+', '%', '#', ':' in order,
 * each of length 2 and kind SUB. 1 <= total <= AMARK_DIALECT_DELIMITER_MAX.
 */
void amark_test_def_set_delimiter_total(amark_test_def_t *d, size_t total);

/*
 * One invalid definition: apply(d, arg) changes a valid *d (from
 * amark_test_def_init) so that one configuration error holds. name says
 * which, for the test output.
 */
typedef struct amark_test_def_breaker {
    const char *name;
    void (*apply)(amark_test_def_t *d, unsigned long arg);
    unsigned long arg;
} amark_test_def_breaker_t;

/* Every invalid definition, and how many there are. */
extern const amark_test_def_breaker_t amark_test_def_breakers[];
extern const size_t amark_test_def_breaker_count;

#endif /* AMARK_TEST_DIALECT_DEFS_H */
