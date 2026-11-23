/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/* Unit tests of UTF-8 validation and decoding (src/utf8.h). */

#include "utf8.h"

#include <naos/naos.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * The reference. It knows nothing of Table 3-7: a byte sequence is a
 * well-formed sequence when it is the encoding that ref_encode gives to some
 * scalar value, and a prefix can begin a well-formed sequence when some such
 * encoding starts with it. The prefixes of length 1 to 3 are marked in
 * bitsets by encoding every scalar value once.
 */
#define BIT_GET(set, i) (((set)[(i) >> 3] >> ((i) & 7u)) & 1u)
#define BIT_SET(set, i) ((set)[(i) >> 3] |= (unsigned char)(1u << ((i) & 7u)))

static unsigned char g_prefix1[256 / 8];
static unsigned char g_prefix2[65536 / 8];
static unsigned char g_prefix3[16777216 / 8];
static int g_prefixes_ready;

/* Encodes the scalar value cp (not a surrogate, at most U+10FFFF). */
static size_t ref_encode(uint32_t cp, unsigned char out[4])
{
    if (cp < 0x80) {
        out[0] = (unsigned char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (unsigned char)(0xC0 | (cp >> 6));
        out[1] = (unsigned char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (unsigned char)(0xE0 | (cp >> 12));
        out[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (unsigned char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (unsigned char)(0xF0 | (cp >> 18));
    out[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (unsigned char)(0x80 | (cp & 0x3F));
    return 4;
}

static int is_scalar(uint32_t cp)
{
    return cp <= 0x10FFFF && (cp < 0xD800 || cp > 0xDFFF);
}

static void ref_prepare(void)
{
    uint32_t cp;
    unsigned char e[4];
    size_t n;

    if (g_prefixes_ready) {
        return;
    }
    for (cp = 0; cp <= 0x10FFFF; cp++) {
        if (!is_scalar(cp)) {
            continue;
        }
        n = ref_encode(cp, e);
        BIT_SET(g_prefix1, (uint32_t)e[0]);
        if (n >= 2) {
            BIT_SET(g_prefix2, ((uint32_t)e[0] << 8) | e[1]);
        }
        if (n >= 3) {
            BIT_SET(g_prefix3,
                    ((uint32_t)e[0] << 16) | ((uint32_t)e[1] << 8) | e[2]);
        }
    }
    g_prefixes_ready = 1;
}

/*
 * If bytes[0..n) (n 1 to 4) is the encoding of a scalar value, returns 1 and
 * writes the value. The value is read from the bit patterns and accepted
 * only if encoding it gives back the same n bytes.
 */
static int ref_is_encoding(const unsigned char *bytes, size_t n, uint32_t *cp)
{
    uint32_t v;
    size_t i;
    unsigned char e[4];

    if (n == 1) {
        v = bytes[0];
    } else if (n == 2) {
        v = bytes[0] & 0x1Fu;
    } else if (n == 3) {
        v = bytes[0] & 0x0Fu;
    } else {
        v = bytes[0] & 0x07u;
    }
    for (i = 1; i < n; i++) {
        v = (v << 6) | (bytes[i] & 0x3Fu);
    }
    if (!is_scalar(v) || ref_encode(v, e) != n || memcmp(e, bytes, n) != 0) {
        return 0;
    }
    *cp = v;
    return 1;
}

/* Returns 1 if bytes[0..m) (m 1 to 3) can begin a well-formed sequence. */
static int ref_is_prefix(const unsigned char *bytes, size_t m)
{
    if (m == 1) {
        return (int)BIT_GET(g_prefix1, (uint32_t)bytes[0]);
    }
    if (m == 2) {
        return (int)BIT_GET(g_prefix2,
                            ((uint32_t)bytes[0] << 8) | bytes[1]);
    }
    return (int)BIT_GET(g_prefix3, ((uint32_t)bytes[0] << 16) |
                                       ((uint32_t)bytes[1] << 8) | bytes[2]);
}

/* The reference for amark_utf8_inspect (length at least 1). */
static size_t ref_inspect(const unsigned char *bytes, size_t length,
                          bool *well_formed)
{
    size_t n;
    size_t m;
    uint32_t cp;

    for (n = 1; n <= 4 && n <= length; n++) {
        if (ref_is_encoding(bytes, n, &cp)) {
            *well_formed = true;
            return n;
        }
    }
    *well_formed = false;
    for (m = 3; m >= 2; m--) {
        if (m <= length && ref_is_prefix(bytes, m)) {
            return m;
        }
    }
    return 1;
}

/* The reference for amark_utf8_validate. */
static bool ref_validate(const unsigned char *bytes, size_t length,
                         size_t *error_offset)
{
    size_t i = 0;
    bool ok;
    size_t n;

    while (i < length) {
        n = ref_inspect(bytes + i, length - i, &ok);
        if (!ok) {
            *error_offset = i;
            return false;
        }
        i += n;
    }
    return true;
}

/*
 * Copies n bytes into a heap block of exactly n bytes, so that the sanitizer
 * reports any read past the end. Returns NULL for n == 0 or on failure.
 */
static unsigned char *exact_copy(const void *bytes, size_t n)
{
    unsigned char *p;

    if (n == 0) {
        return NULL;
    }
    p = (unsigned char *)malloc(n);
    if (p != NULL) {
        memcpy(p, bytes, n);
    }
    return p;
}

/*
 * Validates the n bytes of text in an exact-sized copy. Returns the result
 * and writes the offset (SIZE_MAX when valid, so that the offset is checked
 * not to be written).
 */
static bool validate_exact(const char *text, size_t n, size_t *offset)
{
    unsigned char *copy = exact_copy(text, n);
    bool result;

    *offset = SIZE_MAX;
    if (n != 0 && copy == NULL) {
        return false;
    }
    result = amark_utf8_validate(copy, n, offset);
    free(copy);
    return result;
}

/* The inspection of the n bytes of text, in an exact-sized copy. */
static size_t inspect_exact(const char *text, size_t n, bool *well_formed)
{
    unsigned char *copy = exact_copy(text, n);
    size_t result;

    *well_formed = true;
    if (copy == NULL) {
        return 0;
    }
    result = amark_utf8_inspect(copy, n, well_formed);
    free(copy);
    return result;
}

/* Checks that literal s is valid UTF-8. */
#define CHECK_VALID(s)                                                       \
    do {                                                                     \
        size_t off_;                                                         \
        NAOS_CHECK_TRUE(validate_exact((s), sizeof(s) - 1, &off_));          \
        NAOS_CHECK_UINT_EQ(off_, SIZE_MAX);                                  \
    } while (0)

/* Checks that literal s is invalid and that its first error is at off. */
#define CHECK_INVALID_AT(s, off)                                             \
    do {                                                                     \
        size_t off_;                                                         \
        NAOS_CHECK_FALSE(validate_exact((s), sizeof(s) - 1, &off_));         \
        NAOS_CHECK_UINT_EQ(off_, (off));                                     \
    } while (0)

/* Checks the inspection of literal s: its well-formedness and length. */
#define CHECK_INSPECT(s, ok, len)                                            \
    do {                                                                     \
        bool ok_;                                                            \
        NAOS_CHECK_UINT_EQ(inspect_exact((s), sizeof(s) - 1, &ok_), (len));  \
        NAOS_CHECK_INT_EQ(ok_, (ok));                                        \
    } while (0)

/* Checks that literal s decodes to cp with len bytes. */
#define CHECK_DECODE(s, cp, len)                                             \
    do {                                                                     \
        unsigned char *copy_ = exact_copy((s), sizeof(s) - 1);               \
        uint32_t cp_ = 0xFFFFFFFFu;                                          \
        NAOS_REQUIRE_NOT_NULL(copy_);                                        \
        NAOS_CHECK_UINT_EQ(amark_utf8_decode(copy_, sizeof(s) - 1, &cp_),    \
                           (len));                                           \
        NAOS_CHECK_UINT_EQ(cp_, (cp));                                       \
        free(copy_);                                                         \
    } while (0)

/* --- Validation: well-formed input -------------------------------------- */

NAOS_TEST(empty_input_is_valid)
{
    size_t off = 77;

    NAOS_CHECK_TRUE(amark_utf8_validate(NULL, 0, &off));
    NAOS_CHECK_UINT_EQ(off, 77);
    NAOS_CHECK_TRUE(amark_utf8_validate((const unsigned char *)"x", 0, &off));
    NAOS_CHECK_UINT_EQ(off, 77);
    NAOS_CHECK_TRUE(amark_utf8_validate(NULL, 0, NULL));
}

NAOS_TEST(nul_and_bom_are_valid)
{
    CHECK_VALID("\x00");
    CHECK_VALID("a\x00" "b");
    CHECK_VALID("\xEF\xBB\xBF");
    CHECK_VALID("\xEF\xBB\xBF" "abc");
    CHECK_VALID("a\xEF\xBB\xBF" "b");
}

NAOS_TEST(boundaries_of_each_range_are_valid)
{
    CHECK_VALID("\x00");
    CHECK_VALID("\x7F");
    CHECK_VALID("\xC2\x80");
    CHECK_VALID("\xDF\xBF");
    CHECK_VALID("\xE0\xA0\x80");
    CHECK_VALID("\xE0\xBF\xBF");
    CHECK_VALID("\xE1\x80\x80");
    CHECK_VALID("\xEC\xBF\xBF");
    CHECK_VALID("\xED\x80\x80");
    CHECK_VALID("\xED\x9F\xBF");
    CHECK_VALID("\xEE\x80\x80");
    CHECK_VALID("\xEF\xBF\xBF");
    CHECK_VALID("\xF0\x90\x80\x80");
    CHECK_VALID("\xF0\xBF\xBF\xBF");
    CHECK_VALID("\xF1\x80\x80\x80");
    CHECK_VALID("\xF3\xBF\xBF\xBF");
    CHECK_VALID("\xF4\x80\x80\x80");
    CHECK_VALID("\xF4\x8F\xBF\xBF");
}

NAOS_TEST(mixed_text_is_valid)
{
    CHECK_VALID("plain ASCII text\n");
    CHECK_VALID("caf\xC3\xA9 \xE2\x82\xAC 5 \xF0\x9F\x98\x80!");
    CHECK_VALID("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E");
}

/* --- Validation: each kind of ill-formed input ---------------------------- */

NAOS_TEST(non_shortest_forms_are_invalid)
{
    CHECK_INVALID_AT("\xC0\x80", 0);
    CHECK_INVALID_AT("\xC0\xAF", 0);
    CHECK_INVALID_AT("\xC1\xBF", 0);
    CHECK_INVALID_AT("\xE0\x80\x80", 0);
    CHECK_INVALID_AT("\xE0\x9F\xBF", 0);
    CHECK_INVALID_AT("\xF0\x80\x80\x80", 0);
    CHECK_INVALID_AT("\xF0\x8F\xBF\xBF", 0);
    CHECK_INVALID_AT("ab\xC0\xAF", 2);
    CHECK_INVALID_AT("\xC3\xA9\xE0\x9F\xBF", 2);
}

NAOS_TEST(surrogates_are_invalid)
{
    CHECK_INVALID_AT("\xED\xA0\x80", 0);
    CHECK_INVALID_AT("\xED\xAF\xBF", 0);
    CHECK_INVALID_AT("\xED\xB0\x80", 0);
    CHECK_INVALID_AT("\xED\xBF\xBF", 0);
    CHECK_INVALID_AT("x\xED\xA0\x80\xED\xB0\x80", 1);
}

NAOS_TEST(values_above_10ffff_are_invalid)
{
    CHECK_INVALID_AT("\xF4\x90\x80\x80", 0);
    CHECK_INVALID_AT("\xF4\xBF\xBF\xBF", 0);
    CHECK_INVALID_AT("\xF5\x80\x80\x80", 0);
    CHECK_INVALID_AT("\xF7\xBF\xBF\xBF", 0);
    CHECK_INVALID_AT("\xF8\x88\x80\x80\x80", 0);
    CHECK_INVALID_AT("\xFC\x84\x80\x80\x80\x80", 0);
    CHECK_INVALID_AT("\xF4\x8F\xBF\xBF\xF4\x90\x80\x80", 4);
}

NAOS_TEST(truncated_sequences_are_invalid)
{
    /* At the end of the input. */
    CHECK_INVALID_AT("\xC2", 0);
    CHECK_INVALID_AT("\xE2", 0);
    CHECK_INVALID_AT("\xE2\x82", 0);
    CHECK_INVALID_AT("\xF0", 0);
    CHECK_INVALID_AT("\xF0\x9F", 0);
    CHECK_INVALID_AT("\xF0\x9F\x98", 0);
    CHECK_INVALID_AT("abc\xE2\x82", 3);
    CHECK_INVALID_AT("\xE2\x82\xAC\xF0\x9F\x98", 3);
    /* Followed by a byte that is not a continuation byte. */
    CHECK_INVALID_AT("\xC2" "A", 0);
    CHECK_INVALID_AT("\xE2\x82" "A", 0);
    CHECK_INVALID_AT("\xF0\x9F\x98" "A", 0);
    CHECK_INVALID_AT("\xE2\x82\xE2\x82\xAC", 0);
    CHECK_INVALID_AT("a\xF0\x9F\x98\xF0\x9F\x98\x80", 1);
}

NAOS_TEST(lone_continuation_bytes_are_invalid)
{
    CHECK_INVALID_AT("\x80", 0);
    CHECK_INVALID_AT("\xBF", 0);
    CHECK_INVALID_AT("a\x80", 1);
    CHECK_INVALID_AT("\xC3\xA9\xA9", 2);
    CHECK_INVALID_AT("\xE2\x82\xAC\x80", 3);
    CHECK_INVALID_AT("\xF0\x9F\x98\x80\xBF", 4);
}

NAOS_TEST(bytes_never_in_utf8_are_invalid)
{
    unsigned int b;
    unsigned char buf[3];
    size_t off;

    CHECK_INVALID_AT("\xC0", 0);
    CHECK_INVALID_AT("\xC1", 0);
    for (b = 0xF5; b <= 0xFF; b++) {
        buf[0] = 'a';
        buf[1] = (unsigned char)b;
        buf[2] = 0x80;
        off = SIZE_MAX;
        NAOS_CHECK_FALSE(amark_utf8_validate(buf + 1, 1, &off));
        NAOS_CHECK_UINT_EQ(off, 0);
        off = SIZE_MAX;
        NAOS_CHECK_FALSE(amark_utf8_validate(buf, 3, &off));
        NAOS_CHECK_UINT_EQ(off, 1);
    }
}

NAOS_TEST(just_outside_each_range_is_invalid)
{
    CHECK_INVALID_AT("\xC1\xBF", 0);
    CHECK_INVALID_AT("\xC2\x7F", 0);
    CHECK_INVALID_AT("\xC2\xC0", 0);
    CHECK_INVALID_AT("\xDF\xC0", 0);
    CHECK_INVALID_AT("\xE0\x9F\x80", 0);
    CHECK_INVALID_AT("\xE0\xC0\x80", 0);
    CHECK_INVALID_AT("\xE0\xA0\x7F", 0);
    CHECK_INVALID_AT("\xE0\xA0\xC0", 0);
    CHECK_INVALID_AT("\xE1\x7F\x80", 0);
    CHECK_INVALID_AT("\xEC\xC0\x80", 0);
    CHECK_INVALID_AT("\xEC\xBF\xC0", 0);
    CHECK_INVALID_AT("\xED\x7F\x80", 0);
    CHECK_INVALID_AT("\xED\xA0\x80", 0);
    CHECK_INVALID_AT("\xED\x9F\xC0", 0);
    CHECK_INVALID_AT("\xEE\x7F\x80", 0);
    CHECK_INVALID_AT("\xEF\xC0\x80", 0);
    CHECK_INVALID_AT("\xEF\xBF\x7F", 0);
    CHECK_INVALID_AT("\xF0\x8F\x80\x80", 0);
    CHECK_INVALID_AT("\xF0\xC0\x80\x80", 0);
    CHECK_INVALID_AT("\xF0\x90\x80\x7F", 0);
    CHECK_INVALID_AT("\xF0\x90\x7F\x80", 0);
    CHECK_INVALID_AT("\xF1\x7F\x80\x80", 0);
    CHECK_INVALID_AT("\xF3\xC0\x80\x80", 0);
    CHECK_INVALID_AT("\xF3\xBF\xBF\xC0", 0);
    CHECK_INVALID_AT("\xF4\x7F\x80\x80", 0);
    CHECK_INVALID_AT("\xF4\x90\x80\x80", 0);
    CHECK_INVALID_AT("\xF4\x8F\xBF\xC0", 0);
    CHECK_INVALID_AT("\xF5\x80\x80\x80", 0);
}

NAOS_TEST(first_invalid_offset_is_reported)
{
    CHECK_INVALID_AT("abc\x80" "def\xC0", 3);
    CHECK_INVALID_AT("\xC3\xA9\xE2\x82\xAC\xED\xA0\x80", 5);
    CHECK_INVALID_AT("\xF0\x9F\x98\x80" "x\xFF", 5);
}

NAOS_TEST(null_error_offset_is_allowed)
{
    NAOS_CHECK_FALSE(
        amark_utf8_validate((const unsigned char *)"a\x80", 2, NULL));
    NAOS_CHECK_TRUE(
        amark_utf8_validate((const unsigned char *)"a\xC3\xA9", 3, NULL));
}

NAOS_TEST(validation_does_not_change_the_bytes)
{
    static const unsigned char original[] = "a\xC3\xA9\xE2\x82\xAC\xED\xA0\x80";
    unsigned char bytes[sizeof(original)];
    size_t off;

    memcpy(bytes, original, sizeof(original));
    NAOS_CHECK_FALSE(amark_utf8_validate(bytes, sizeof(bytes) - 1, &off));
    NAOS_CHECK_MEM_EQ(bytes, sizeof(bytes), original, sizeof(original));
}

/* --- Inspection: maximal subparts ---------------------------------------- */

NAOS_TEST(inspect_returns_the_length_of_a_well_formed_sequence)
{
    CHECK_INSPECT("\x00", true, 1);
    CHECK_INSPECT("A\x80", true, 1);
    CHECK_INSPECT("\xC2\x80", true, 2);
    CHECK_INSPECT("\xDF\xBF" "A", true, 2);
    CHECK_INSPECT("\xE0\xA0\x80", true, 3);
    CHECK_INSPECT("\xED\x9F\xBF\x80", true, 3);
    CHECK_INSPECT("\xEF\xBB\xBF", true, 3);
    CHECK_INSPECT("\xF0\x90\x80\x80", true, 4);
    CHECK_INSPECT("\xF4\x8F\xBF\xBF\xBF", true, 4);
}

NAOS_TEST(inspect_returns_the_maximal_subpart)
{
    /* No prefix can begin a well-formed sequence: one byte. */
    CHECK_INSPECT("\x80", false, 1);
    CHECK_INSPECT("\xBF\xBF", false, 1);
    CHECK_INSPECT("\xC0\xAF", false, 1);
    CHECK_INSPECT("\xC1\xBF", false, 1);
    CHECK_INSPECT("\xE0\x80\x80", false, 1);
    CHECK_INSPECT("\xE0\x9F\xBF", false, 1);
    CHECK_INSPECT("\xED\xA0\x80", false, 1);
    CHECK_INSPECT("\xF0\x80\x80", false, 1);
    CHECK_INSPECT("\xF0\x8F\xBF\xBF", false, 1);
    CHECK_INSPECT("\xF4\x90\x80\x80", false, 1);
    CHECK_INSPECT("\xF5\x80\x80\x80", false, 1);
    CHECK_INSPECT("\xFF", false, 1);
    /* Truncated at the end of the input. */
    CHECK_INSPECT("\xC2", false, 1);
    CHECK_INSPECT("\xE2", false, 1);
    CHECK_INSPECT("\xE2\x82", false, 2);
    CHECK_INSPECT("\xE0\xA0", false, 2);
    CHECK_INSPECT("\xED\x9F", false, 2);
    CHECK_INSPECT("\xF0", false, 1);
    CHECK_INSPECT("\xF0\x90", false, 2);
    CHECK_INSPECT("\xF0\x9F\x98", false, 3);
    CHECK_INSPECT("\xF4\x8F\xBF", false, 3);
    /* Truncated by a byte that is not a continuation byte. */
    CHECK_INSPECT("\xC2" "A", false, 1);
    CHECK_INSPECT("\xE2\x82" "A", false, 2);
    CHECK_INSPECT("\xE2\x82\xC0", false, 2);
    CHECK_INSPECT("\xE1\x80\x7F", false, 2);
    CHECK_INSPECT("\xF0\x9F\x98" "A", false, 3);
    CHECK_INSPECT("\xF1\x80\x80\xC0", false, 3);
    CHECK_INSPECT("\xF4\x8F" "A", false, 2);
}

/* --- Decoding ------------------------------------------------------------ */

NAOS_TEST(decode_boundaries)
{
    CHECK_DECODE("\x00", 0x0, 1);
    CHECK_DECODE("\x7F", 0x7F, 1);
    CHECK_DECODE("\xC2\x80", 0x80, 2);
    CHECK_DECODE("\xDF\xBF", 0x7FF, 2);
    CHECK_DECODE("\xE0\xA0\x80", 0x800, 3);
    CHECK_DECODE("\xE0\xBF\xBF", 0xFFF, 3);
    CHECK_DECODE("\xE1\x80\x80", 0x1000, 3);
    CHECK_DECODE("\xEC\xBF\xBF", 0xCFFF, 3);
    CHECK_DECODE("\xED\x80\x80", 0xD000, 3);
    CHECK_DECODE("\xED\x9F\xBF", 0xD7FF, 3);
    CHECK_DECODE("\xEE\x80\x80", 0xE000, 3);
    CHECK_DECODE("\xEF\xBB\xBF", 0xFEFF, 3);
    CHECK_DECODE("\xEF\xBF\xBF", 0xFFFF, 3);
    CHECK_DECODE("\xF0\x90\x80\x80", 0x10000, 4);
    CHECK_DECODE("\xF0\xBF\xBF\xBF", 0x3FFFF, 4);
    CHECK_DECODE("\xF1\x80\x80\x80", 0x40000, 4);
    CHECK_DECODE("\xF3\xBF\xBF\xBF", 0xFFFFF, 4);
    CHECK_DECODE("\xF4\x80\x80\x80", 0x100000, 4);
    CHECK_DECODE("\xF4\x8F\xBF\xBF", 0x10FFFF, 4);
}

NAOS_TEST(decode_reads_one_character_of_a_longer_range)
{
    CHECK_DECODE("A\xC3\xA9", 0x41, 1);
    CHECK_DECODE("\xC3\xA9" "A", 0xE9, 2);
    CHECK_DECODE("\xE2\x82\xAC\xE2\x82\xAC", 0x20AC, 3);
    CHECK_DECODE("\xF0\x9F\x98\x80\x00", 0x1F600, 4);
}

NAOS_TEST(decode_walks_a_validated_sequence)
{
    static const char text[] = "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80\x00z";
    static const uint32_t expected[] = {0x61, 0xE9, 0x20AC, 0x1F600, 0x0,
                                        0x7A};
    const size_t length = sizeof(text) - 1;
    unsigned char *bytes = exact_copy(text, length);
    size_t i = 0;
    size_t k = 0;
    size_t n;
    uint32_t cp;

    NAOS_REQUIRE_NOT_NULL(bytes);
    NAOS_CHECK_TRUE(amark_utf8_validate(bytes, length, NULL));
    while (i < length && k < sizeof(expected) / sizeof(expected[0])) {
        n = amark_utf8_decode(bytes + i, length - i, &cp);
        NAOS_CHECK_UINT_EQ(cp, expected[k]);
        if (n < 1 || n > 4) {
            break;
        }
        i += n;
        k++;
    }
    NAOS_CHECK_UINT_EQ(i, length);
    NAOS_CHECK_UINT_EQ(k, sizeof(expected) / sizeof(expected[0]));
    free(bytes);
}

/* --- Cross-checks against the reference ---------------------------------- */

/*
 * Every scalar value: its encoding validates, inspects as well-formed with
 * its length, and decodes to the value with its length, in a block of
 * exactly that many bytes (decode and inspect must not read past it).
 */
NAOS_TEST(every_scalar_value_round_trips)
{
    unsigned char *exact[5];
    unsigned char e[4];
    uint32_t cp;
    uint32_t got;
    size_t n;
    size_t i;
    size_t bad = 0;
    bool ok;

    for (i = 1; i <= 4; i++) {
        exact[i] = (unsigned char *)malloc(i);
        NAOS_REQUIRE_NOT_NULL(exact[i]);
    }
    for (cp = 0; cp <= 0x10FFFF; cp++) {
        if (!is_scalar(cp)) {
            continue;
        }
        n = ref_encode(cp, e);
        memcpy(exact[n], e, n);
        got = 0xFFFFFFFFu;
        if (amark_utf8_decode(exact[n], n, &got) != n || got != cp ||
            !amark_utf8_validate(exact[n], n, NULL) ||
            amark_utf8_inspect(exact[n], n, &ok) != n || !ok) {
            bad++;
        }
    }
    NAOS_CHECK_UINT_EQ(bad, 0);
    for (i = 1; i <= 4; i++) {
        free(exact[i]);
    }
}

/*
 * Compares inspection and validation with the reference for the bytes
 * bytes[0..n), held in a block of exactly n bytes. Returns 1 on agreement.
 */
static int agrees(unsigned char *exact, size_t n)
{
    bool ok;
    bool ref_ok;
    size_t len;
    size_t off = SIZE_MAX;
    size_t ref_off = SIZE_MAX;
    bool valid;

    len = amark_utf8_inspect(exact, n, &ok);
    if (len != ref_inspect(exact, n, &ref_ok) || ok != ref_ok) {
        return 0;
    }
    valid = amark_utf8_validate(exact, n, &off);
    if (valid != ref_validate(exact, n, &ref_off) || off != ref_off) {
        return 0;
    }
    return 1;
}

/* Every byte sequence of length 1 to 3. */
NAOS_TEST(all_short_sequences_agree_with_the_reference)
{
    unsigned char *exact[4];
    uint32_t v;
    size_t i;
    size_t bad = 0;

    ref_prepare();
    for (i = 1; i <= 3; i++) {
        exact[i] = (unsigned char *)malloc(i);
        NAOS_REQUIRE_NOT_NULL(exact[i]);
    }
    for (v = 0; v < 0x100; v++) {
        exact[1][0] = (unsigned char)v;
        bad += !agrees(exact[1], 1);
    }
    for (v = 0; v < 0x10000; v++) {
        exact[2][0] = (unsigned char)(v >> 8);
        exact[2][1] = (unsigned char)v;
        bad += !agrees(exact[2], 2);
    }
    for (v = 0; v < 0x1000000; v++) {
        exact[3][0] = (unsigned char)(v >> 16);
        exact[3][1] = (unsigned char)(v >> 8);
        exact[3][2] = (unsigned char)v;
        bad += !agrees(exact[3], 3);
    }
    NAOS_CHECK_UINT_EQ(bad, 0);
    for (i = 1; i <= 3; i++) {
        free(exact[i]);
    }
}

/*
 * Four-byte sequences: every first and second byte, with the third and
 * fourth bytes taken from values on and around each range boundary.
 */
NAOS_TEST(four_byte_sequences_agree_with_the_reference)
{
    static const unsigned char tails[] = {0x00, 0x41, 0x7F, 0x80, 0x81,
                                          0x8F, 0x90, 0x9F, 0xA0, 0xBE,
                                          0xBF, 0xC0, 0xC2, 0xE0, 0xF0,
                                          0xF4, 0xFF};
    const size_t count = sizeof(tails) / sizeof(tails[0]);
    unsigned char *exact = (unsigned char *)malloc(4);
    uint32_t v;
    size_t a;
    size_t b;
    size_t bad = 0;

    ref_prepare();
    NAOS_REQUIRE_NOT_NULL(exact);
    for (v = 0; v < 0x10000; v++) {
        exact[0] = (unsigned char)(v >> 8);
        exact[1] = (unsigned char)v;
        for (a = 0; a < count; a++) {
            exact[2] = tails[a];
            for (b = 0; b < count; b++) {
                exact[3] = tails[b];
                bad += !agrees(exact, 4);
            }
        }
    }
    NAOS_CHECK_UINT_EQ(bad, 0);
    free(exact);
}

/*
 * Random sequences of 1 to 16 bytes, biased toward lead and continuation
 * bytes so that long well-formed runs and errors deep inside them occur.
 */
NAOS_TEST(random_sequences_agree_with_the_reference)
{
    static const unsigned char bytes_pool[] = {
        0x00, 0x41, 0x7F, 0x80, 0x8F, 0x90, 0x9F, 0xA0, 0xBF, 0xC0, 0xC1,
        0xC2, 0xDF, 0xE0, 0xE1, 0xEC, 0xED, 0xEE, 0xEF, 0xF0, 0xF1, 0xF3,
        0xF4, 0xF5, 0xFF};
    const size_t pool = sizeof(bytes_pool) / sizeof(bytes_pool[0]);
    unsigned char buf[16];
    unsigned char *exact;
    uint32_t state = 12345u;
    size_t round;
    size_t n;
    size_t i;
    size_t bad = 0;

    ref_prepare();
    for (round = 0; round < 200000; round++) {
        state = state * 1103515245u + 12345u;
        n = 1 + (state >> 16) % sizeof(buf);
        for (i = 0; i < n; i++) {
            state = state * 1103515245u + 12345u;
            if ((state >> 28) < 4) {
                buf[i] = (unsigned char)(state >> 16);
            } else {
                buf[i] = bytes_pool[(state >> 16) % pool];
            }
        }
        exact = exact_copy(buf, n);
        NAOS_REQUIRE_NOT_NULL(exact);
        bad += !agrees(exact, n);
        free(exact);
    }
    NAOS_CHECK_UINT_EQ(bad, 0);
}

NAOS_SUITE(utf8,
    NAOS_ADD_TEST(empty_input_is_valid),
    NAOS_ADD_TEST(nul_and_bom_are_valid),
    NAOS_ADD_TEST(boundaries_of_each_range_are_valid),
    NAOS_ADD_TEST(mixed_text_is_valid),
    NAOS_ADD_TEST(non_shortest_forms_are_invalid),
    NAOS_ADD_TEST(surrogates_are_invalid),
    NAOS_ADD_TEST(values_above_10ffff_are_invalid),
    NAOS_ADD_TEST(truncated_sequences_are_invalid),
    NAOS_ADD_TEST(lone_continuation_bytes_are_invalid),
    NAOS_ADD_TEST(bytes_never_in_utf8_are_invalid),
    NAOS_ADD_TEST(just_outside_each_range_is_invalid),
    NAOS_ADD_TEST(first_invalid_offset_is_reported),
    NAOS_ADD_TEST(null_error_offset_is_allowed),
    NAOS_ADD_TEST(validation_does_not_change_the_bytes),
    NAOS_ADD_TEST(inspect_returns_the_length_of_a_well_formed_sequence),
    NAOS_ADD_TEST(inspect_returns_the_maximal_subpart),
    NAOS_ADD_TEST(decode_boundaries),
    NAOS_ADD_TEST(decode_reads_one_character_of_a_longer_range),
    NAOS_ADD_TEST(decode_walks_a_validated_sequence),
    NAOS_ADD_TEST(every_scalar_value_round_trips),
    NAOS_ADD_TEST(all_short_sequences_agree_with_the_reference),
    NAOS_ADD_TEST(four_byte_sequences_agree_with_the_reference),
    NAOS_ADD_TEST(random_sequences_agree_with_the_reference)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(utf8)
)
