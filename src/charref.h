/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_CHARREF_H
#define AMARK_CHARREF_H

#include <stddef.h>

/*
 * Character references: the one owner of their syntax. The inline parser
 * uses it to recognize a reference and the text extraction uses it to get
 * the value, so every reference that becomes a node has a value.
 *
 * The syntax (CommonMark 0.31.2, "Entity and numeric character
 * references"):
 *
 *   '&' name ';'                 name: ASCII letters and digits that
 *                                amark_entity_lookup finds (case matters)
 *   '&#' 1 to 7 decimal digits ';'
 *   '&#x' or '&#X' 1 to 6 hexadecimal digits ';'
 *
 * The function reads nothing but its arguments and the constant entity
 * table, allocates nothing, cannot fail and may be called from any number
 * of threads at once.
 */

/* The value of a character reference. */
typedef struct amark_charref {
    unsigned char utf8[8]; /* the UTF-8 of the value (1 to 6 bytes) */
    size_t utf8_length;
} amark_charref_t;

/*
 * bytes[0] is '&'; length (at least 1) is the number of bytes that may be
 * read from there (up to the end of the line's content).
 *
 * If a character reference starts at bytes[0] and ends within length, returns
 * its length from the '&' to the ';' and, when out is not NULL, sets *out to
 * its value: for a name, the one or two code points of the entity; for a
 * number, the code point of that value, except that 0, the surrogates
 * (U+D800 to U+DFFF) and values above U+10FFFF give U+FFFD.
 *
 * Otherwise returns 0 and leaves *out unchanged.
 *
 * Reads only the bytes up to the end of the name or the digits and the byte
 * after them, so a call takes time proportional to that length.
 */
size_t amark_charref_scan(const unsigned char *bytes, size_t length,
                          amark_charref_t *out);

#endif /* AMARK_CHARREF_H */
