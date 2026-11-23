/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_UTF8_H
#define AMARK_UTF8_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
 * No function here allocates or changes the bytes it is given.
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

#endif /* AMARK_UTF8_H */
