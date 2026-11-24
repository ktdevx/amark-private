/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#include "charref.h"

#include "entity.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* The most digits of a decimal and a hexadecimal character reference. */
#define MAX_DECIMAL_DIGITS 7
#define MAX_HEX_DIGITS 6

/* The largest code point and U+FFFD REPLACEMENT CHARACTER. */
#define MAX_CODE_POINT 0x10FFFFu
#define REPLACEMENT_CHARACTER 0xFFFDu

static bool is_digit(unsigned char c)
{
    return c >= '0' && c <= '9';
}

static bool is_letter(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool is_alnum(unsigned char c)
{
    return is_digit(c) || is_letter(c);
}

/* The value of the hexadecimal digit c, or -1 if c is not one. */
static int hex_value(unsigned char c)
{
    if (is_digit(c)) {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/*
 * Sets *out to the UTF-8 of the code point cp, with U+FFFD for 0, the
 * surrogates and values above U+10FFFF.
 */
static void encode(uint32_t cp, amark_charref_t *out)
{
    unsigned char *u = out->utf8;

    if (cp == 0 || (cp >= 0xD800u && cp <= 0xDFFFu) || cp > MAX_CODE_POINT) {
        cp = REPLACEMENT_CHARACTER;
    }
    if (cp < 0x80u) {
        u[0] = (unsigned char)cp;
        out->utf8_length = 1;
    } else if (cp < 0x800u) {
        u[0] = (unsigned char)(0xC0u | (cp >> 6));
        u[1] = (unsigned char)(0x80u | (cp & 0x3Fu));
        out->utf8_length = 2;
    } else if (cp < 0x10000u) {
        u[0] = (unsigned char)(0xE0u | (cp >> 12));
        u[1] = (unsigned char)(0x80u | ((cp >> 6) & 0x3Fu));
        u[2] = (unsigned char)(0x80u | (cp & 0x3Fu));
        out->utf8_length = 3;
    } else {
        u[0] = (unsigned char)(0xF0u | (cp >> 18));
        u[1] = (unsigned char)(0x80u | ((cp >> 12) & 0x3Fu));
        u[2] = (unsigned char)(0x80u | ((cp >> 6) & 0x3Fu));
        u[3] = (unsigned char)(0x80u | (cp & 0x3Fu));
        out->utf8_length = 4;
    }
}

/*
 * A numeric reference; pos is the index right after "&#". Returns the
 * length of the reference or 0.
 */
static size_t scan_numeric(const unsigned char *p, size_t length, size_t pos,
                           amark_charref_t *out)
{
    size_t max = MAX_DECIMAL_DIGITS;
    uint32_t base = 10;
    uint32_t value = 0;
    size_t start;

    if (pos < length && (p[pos] == 'x' || p[pos] == 'X')) {
        max = MAX_HEX_DIGITS;
        base = 16;
        pos++;
    }
    start = pos;
    /* 7 decimal or 6 hexadecimal digits stay below 2^32. */
    while (pos < length && pos - start < max) {
        int digit = base == 16 ? hex_value(p[pos])
                               : (is_digit(p[pos]) ? p[pos] - '0' : -1);

        if (digit < 0) {
            break;
        }
        value = value * base + (uint32_t)digit;
        pos++;
    }
    if (pos == start || pos >= length || p[pos] != ';') {
        return 0;
    }
    if (out != NULL) {
        encode(value, out);
    }
    return pos + 1;
}

/*
 * An entity reference; the name starts at index 1. Returns the length of
 * the reference or 0.
 */
static size_t scan_named(const unsigned char *p, size_t length,
                         amark_charref_t *out)
{
    const unsigned char *utf8;
    size_t utf8_length;
    size_t pos = 1;

    while (pos < length && is_alnum(p[pos])) {
        pos++;
    }
    if (pos == 1 || pos >= length || p[pos] != ';' ||
        !amark_entity_lookup(p + 1, pos - 1, &utf8, &utf8_length)) {
        return 0;
    }
    if (out != NULL) {
        /* An entity stands for at most 6 bytes of UTF-8. */
        memcpy(out->utf8, utf8, utf8_length);
        out->utf8_length = utf8_length;
    }
    return pos + 1;
}

size_t amark_charref_scan(const unsigned char *bytes, size_t length,
                          amark_charref_t *out)
{
    if (length < 2 || bytes[0] != '&') {
        return 0;
    }
    if (bytes[1] == '#') {
        return scan_numeric(bytes, length, 2, out);
    }
    return scan_named(bytes, length, out);
}
