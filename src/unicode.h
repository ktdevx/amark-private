/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_UNICODE_H
#define AMARK_UNICODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Unicode character properties used by CommonMark: punctuation and
 * whitespace (for emphasis flanking) and full case folding (for link label
 * matching). The data comes from the Unicode Character Database in
 * tools/data/ through the generated unicode_tables.h.
 *
 * The functions take a code point (any uint32_t value, including surrogates
 * and values above U+10FFFF), read nothing but their arguments and constant
 * tables, allocate nothing, cannot fail, need no initialization and may be
 * called from any number of threads at once. Each call takes time
 * proportional to the logarithm of the table size.
 */

/* The largest number of code points that folding one code point gives. */
#define AMARK_UNICODE_FOLD_MAX 3

/*
 * Returns true if the General Category of codepoint is P (Pc, Pd, Ps, Pe,
 * Pi, Pf, Po) or S (Sm, Sc, Sk, So). Returns false for anything else,
 * including unassigned code points, surrogates and values above U+10FFFF.
 */
bool amark_unicode_is_punctuation(uint32_t codepoint);

/*
 * Returns true if the General Category of codepoint is Zs, or if codepoint
 * is U+0009, U+000A, U+000C or U+000D. Returns false for anything else,
 * including U+000B, U+0085, U+2028, U+2029 and values above U+10FFFF. No
 * code point is both whitespace and punctuation.
 */
bool amark_unicode_is_whitespace(uint32_t codepoint);

/*
 * Folds codepoint with full case folding (the C and F mappings of
 * CaseFolding.txt; never S or T), writes the result to out[0], out[1], ...
 * in order and returns the number written (1 to AMARK_UNICODE_FOLD_MAX).
 * Writes nothing past out[returned value - 1]. A code point with no mapping,
 * and any value above U+10FFFF, folds to itself: out[0] = codepoint and the
 * result is 1. out must point at AMARK_UNICODE_FOLD_MAX writable elements.
 */
size_t amark_unicode_fold(uint32_t codepoint,
                          uint32_t out[AMARK_UNICODE_FOLD_MAX]);

#endif /* AMARK_UNICODE_H */
