/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_UTF8_H
#define AMARK_UTF8_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arena.h"
#include "error.h"

/*
 * UTF-8. Well-formedness follows Table 3-7 (Well-Formed UTF-8 Byte
 * Sequences) of the Unicode Standard:
 *
 *   U+0000..U+007F      00..7F
 *   U+0080..U+07FF      C2..DF  80..BF
 *   U+0800..U+0FFF      E0      A0..BF  80..BF
 *   U+1000..U+CFFF      E1..EC  80..BF  80..BF
 *   U+D000..U+D7FF      ED      80..9F  80..BF
 *   U+E000..U+FFFF      EE..EF  80..BF  80..BF
 *   U+10000..U+3FFFF    F0      90..BF  80..BF  80..BF
 *   U+40000..U+FFFFF    F1..F3  80..BF  80..BF  80..BF
 *   U+100000..U+10FFFF  F4      80..8F  80..BF  80..BF
 *
 * Anything else is ill-formed: non-shortest forms (C0, C1, E0 80..9F,
 * F0 80..8F), surrogates (ED A0..BF), values above U+10FFFF (F4 90..BF,
 * F5..FF), truncated sequences and continuation bytes without a lead byte.
 * U+0000 and U+FEFF are well-formed like any other code point.
 *
 * No function here changes the bytes it is given; only amark_utf8_replace
 * allocates, and only from the arena it is given.
 */

/*
 * Inspects the one sequence that starts at bytes[0]; length (at least 1) is
 * the number of bytes from there to the end of the byte sequence. If the
 * sequence is well-formed, sets *well_formed to true and returns its length
 * (1 to 4). Otherwise sets *well_formed to false and returns the length of
 * its maximal subpart (1 to 3): the longest prefix that can still begin a
 * well-formed sequence, or 1 if there is none. For example, E2 82 at the end
 * of the bytes gives 2, F0 80 80 gives 1 and C0 AF gives 1.
 *
 * Reads only bytes[0] to bytes[returned value] and never at or past
 * bytes[length]. Validation and replacement both use this function, so they
 * cannot disagree on what is ill-formed.
 */
size_t amark_utf8_inspect(const unsigned char *bytes, size_t length,
                          bool *well_formed);

/*
 * Returns true if the length bytes at bytes are well-formed UTF-8 (length 0
 * is well-formed). Otherwise returns false and, if error_offset is not NULL,
 * writes the offset at which the first ill-formed sequence starts; on true,
 * *error_offset is not changed. Takes time proportional to length.
 */
bool amark_utf8_validate(const unsigned char *bytes, size_t length,
                         size_t *error_offset);

/*
 * Decodes the character at bytes[0], writes its code point to *codepoint and
 * returns the number of its bytes (1 to 4). bytes must point at the start of
 * a character in a byte sequence that passed amark_utf8_validate, and length
 * (at least 1) must not exceed the number of bytes from there to the end of
 * that sequence. Reads only bytes[0] to bytes[returned value - 1]. The result
 * for other input is not defined.
 */
size_t amark_utf8_decode(const unsigned char *bytes, size_t length,
                         uint32_t *codepoint);

/*
 * The result of amark_utf8_replace. When replacement_count is 0, bytes and
 * length are the input itself (the same pointer); otherwise bytes points at
 * a new byte sequence in the arena.
 */
typedef struct amark_utf8_replaced {
    const unsigned char *bytes; /* the result; the input if nothing replaced */
    size_t length;
    size_t replacement_count;   /* the number of U+FFFD written */
} amark_utf8_replaced_t;

/*
 * Replaces each maximal subpart of an ill-formed sequence in the length bytes
 * at bytes with one U+FFFD (EF BF BD), following "U+FFFD Substitution of
 * Maximal Subparts" in the Unicode Standard (section 3.9), and copies the
 * well-formed sequences unchanged and in order. The result is always
 * well-formed. The input is never changed.
 *
 * If the input is well-formed, allocates nothing and stores the input
 * pointer and length with replacement_count 0, so the caller must keep the
 * input alive for as long as it uses the result. Otherwise allocates the
 * result from arena exactly once.
 *
 * Returns AMARK_ERROR_NONE on success. Returns AMARK_ERROR_OVERFLOW if the
 * result length does not fit in size_t, or AMARK_ERROR_NOMEM (or
 * AMARK_ERROR_OVERFLOW) if the arena fails; on failure nothing is allocated
 * and *out is not changed. Takes time proportional to length.
 */
amark_error_t amark_utf8_replace(amark_arena_t *arena,
                                 const unsigned char *bytes, size_t length,
                                 amark_utf8_replaced_t *out);

#endif /* AMARK_UTF8_H */
