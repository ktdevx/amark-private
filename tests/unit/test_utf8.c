/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/* Unit tests of UTF-8 validation, decoding and replacement (src/utf8.h). */

#include "utf8.h"

#include "test_alloc.h"

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


/* --- Replacement --------------------------------------------------------- */

/*
 * Fixture. NAOS_REQUIRE_* leaves a test with longjmp, so the arena and the
 * heap copies a test uses are static variables, and teardown (which always
 * runs) releases them and then checks that no block is left and that every
 * call received the context value it was given.
 */
static amark_test_alloc_t g_state;
static amark_allocator_t g_allocator;
static amark_arena_t g_arena;
static unsigned char *g_input;    /* an exact-sized copy of the input */
static unsigned char *g_expected; /* the reference result */

NAOS_SETUP(counting)
{
    amark_test_alloc_init(&g_state);
    g_allocator = amark_test_allocator(&g_state);
    amark_arena_init(&g_arena, &g_allocator);
    g_input = NULL;
    g_expected = NULL;
}

NAOS_TEARDOWN(counting)
{
    free(g_input);
    g_input = NULL;
    free(g_expected);
    g_expected = NULL;
    amark_arena_deinit(&g_arena);
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
    NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
}

/* U+FFFD in UTF-8, as a separate literal so that no hex escape runs on. */
#define R "\xEF\xBF\xBD"

/*
 * The reference replacement, built on ref_inspect (which knows nothing of
 * Table 3-7). Writes the result to out (at least 3 * length bytes) and the
 * number of U+FFFD to *count, and returns the result length.
 */
static size_t ref_replace(const unsigned char *bytes, size_t length,
                          unsigned char *out, size_t *count)
{
    size_t i = 0;
    size_t o = 0;
    size_t n;
    bool ok;

    *count = 0;
    while (i < length) {
        n = ref_inspect(bytes + i, length - i, &ok);
        if (ok) {
            memcpy(out + o, bytes + i, n);
            o += n;
        } else {
            memcpy(out + o, R, 3);
            o += 3;
            (*count)++;
        }
        i += n;
    }
    return o;
}

/*
 * Replaces the n bytes of text, held in a block of exactly n bytes, and
 * checks the result against expected (en bytes) and count: the bytes, the
 * count, that the result is well-formed, that the input is unchanged, and
 * that the result is the input itself exactly when nothing was replaced.
 */
static void check_replace(const char *text, size_t n, const char *expected,
                          size_t en, size_t count)
{
    amark_utf8_replaced_t r;

    free(g_input);
    g_input = exact_copy(text, n);
    if (n != 0) {
        NAOS_REQUIRE_NOT_NULL(g_input);
    }
    r.bytes = NULL;
    r.length = SIZE_MAX;
    r.replacement_count = SIZE_MAX;
    NAOS_REQUIRE_INT_EQ(amark_utf8_replace(&g_arena, g_input, n, &r),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_MEM_EQ(r.bytes, r.length, expected, en);
    NAOS_CHECK_UINT_EQ(r.replacement_count, count);
    NAOS_CHECK_TRUE(amark_utf8_validate(r.bytes, r.length, NULL));
    NAOS_CHECK_MEM_EQ(g_input, n, text, n);
    if (count == 0) {
        NAOS_CHECK_PTR_EQ(r.bytes, g_input);
    } else {
        NAOS_CHECK_TRUE(r.bytes != g_input);
    }
    free(g_input);
    g_input = NULL;
}

/* Checks the replacement of literal in: literal out with count U+FFFD. */
#define CHECK_REPLACE(in, out, count)                                        \
    check_replace((in), sizeof(in) - 1, (out), sizeof(out) - 1, (count))

/*
 * The examples of section 3.9 of the Unicode Standard ("U+FFFD Substitution
 * of Maximal Subparts"), byte for byte.
 */
NAOS_TEST(replace_matches_the_standard_example)
{
    /* 61 F1 80 80 E1 80 C2 62 80 63 80 BF 64 */
    CHECK_REPLACE("a" "\xF1\x80\x80" "\xE1\x80" "\xC2" "b" "\x80" "c"
                  "\x80\xBF" "d",
                  "a" R R R "b" R "c" R R "d", 6);
}

/* Table 3-8: non-shortest forms. C0 AF E0 80 BF F0 81 82 41 */
NAOS_TEST(replace_matches_table_3_8)
{
    CHECK_REPLACE("\xC0\xAF\xE0\x80\xBF\xF0\x81\x82" "A",
                  R R R R R R R R "A", 8);
}

/* Table 3-9: surrogates. ED A0 80 ED BF BF ED AF 41 */
NAOS_TEST(replace_matches_table_3_9)
{
    CHECK_REPLACE("\xED\xA0\x80\xED\xBF\xBF\xED\xAF" "A",
                  R R R R R R R R "A", 8);
}

/* Table 3-10: other ill-formed sequences. F4 91 92 93 FF 41 80 BF 42 */
NAOS_TEST(replace_matches_table_3_10)
{
    CHECK_REPLACE("\xF4\x91\x92\x93\xFF" "A" "\x80\xBF" "B",
                  R R R R R "A" R R "B", 7);
}

/* Table 3-11: truncated sequences. E1 80 E2 F0 91 92 F1 BF 41 */
NAOS_TEST(replace_matches_table_3_11)
{
    CHECK_REPLACE("\xE1\x80" "\xE2" "\xF0\x91\x92" "\xF1\xBF" "A",
                  R R R R "A", 4);
}

/* The examples in design.md and single sequences of each kind. */
NAOS_TEST(replace_uses_one_fffd_per_maximal_subpart)
{
    CHECK_REPLACE("\xE2\x82", R, 1);
    CHECK_REPLACE("\xF0\x80\x80", R R R, 3);
    CHECK_REPLACE("\xC0\xAF", R R, 2);
    CHECK_REPLACE("\xF0\x9F\x98", R, 1);         /* truncated 4 bytes */
    CHECK_REPLACE("\xF4\x8F\xBF", R, 1);         /* truncated U+10FFFF */
    CHECK_REPLACE("\xF4\x90\x80\x80", R R R R, 4); /* above U+10FFFF */
    CHECK_REPLACE("\xED\xA0\x80", R R R, 3);     /* a surrogate */
    CHECK_REPLACE("\xE0\x80\x80", R R R, 3);     /* non-shortest */
    CHECK_REPLACE("\x80", R, 1);
    CHECK_REPLACE("\xC1", R, 1);
    CHECK_REPLACE("\xF5", R, 1);
    CHECK_REPLACE("\xFF\xFE", R R, 2);
    /* A truncated sequence followed by a lead byte: two subparts. */
    CHECK_REPLACE("\xE2\x82\xE2\x82\xAC", R "\xE2\x82\xAC", 1);
    CHECK_REPLACE("\xC3\xC3\xA9", R "\xC3\xA9", 1);
    /* A U+FFFD already in the input is copied, not counted. */
    CHECK_REPLACE(R "\x80", R R, 1);
}

NAOS_TEST(replace_copies_well_formed_parts_unchanged)
{
    CHECK_REPLACE("\x00" "a" "\xC3\xA9" "\x80" "\xEF\xBB\xBF"
                  "\xE2\x82\xAC" "\xFF" "\xF0\x9F\x98\x80" "\xF4\x8F\xBF\xBF"
                  "\xC0",
                  "\x00" "a" "\xC3\xA9" R "\xEF\xBB\xBF" "\xE2\x82\xAC" R
                  "\xF0\x9F\x98\x80" "\xF4\x8F\xBF\xBF" R,
                  3);
    /* Errors at the very start and the very end. */
    CHECK_REPLACE("\x80" "hello" "\xE2\x82", R "hello" R, 2);
}

NAOS_TEST(replace_returns_well_formed_input_without_allocating)
{
    static const char *const inputs[] = {
        "a", "hello, world", "\xC2\x80\xDF\xBF", "\xE0\xA0\x80\xEF\xBF\xBF",
        "\xF0\x90\x80\x80\xF4\x8F\xBF\xBF", "\xEF\xBB\xBF-bom",
        "\xEF\xBF\xBD"};
    size_t i;
    amark_utf8_replaced_t r;

    for (i = 0; i < sizeof(inputs) / sizeof(inputs[0]); i++) {
        check_replace(inputs[i], strlen(inputs[i]), inputs[i],
                      strlen(inputs[i]), 0);
    }
    CHECK_REPLACE("\x00", "\x00", 0); /* U+0000 */

    /* The empty input, also with a NULL pointer. */
    r.bytes = (const unsigned char *)"x";
    r.length = 99;
    r.replacement_count = 99;
    NAOS_CHECK_INT_EQ(amark_utf8_replace(&g_arena, NULL, 0, &r),
                      AMARK_ERROR_NONE);
    NAOS_CHECK_NULL(r.bytes);
    NAOS_CHECK_UINT_EQ(r.length, 0);
    NAOS_CHECK_UINT_EQ(r.replacement_count, 0);

    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 0);
}

NAOS_TEST(replace_of_well_formed_input_succeeds_when_allocation_fails)
{
    static const unsigned char text[] = "caf\xC3\xA9";
    amark_utf8_replaced_t r;

    g_state.fail_at = 1;
    NAOS_CHECK_INT_EQ(amark_utf8_replace(&g_arena, text, sizeof(text) - 1, &r),
                      AMARK_ERROR_NONE);
    NAOS_CHECK_PTR_EQ(r.bytes, text);
    NAOS_CHECK_UINT_EQ(r.length, sizeof(text) - 1);
    NAOS_CHECK_UINT_EQ(r.replacement_count, 0);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 0);
}

NAOS_TEST(replace_does_not_change_the_input)
{
    static const unsigned char original[] =
        "a\x80\xC3\xA9\xE2\x82\xED\xA0\x80\xF0\x9F\x98\x80\xFF";
    unsigned char bytes[sizeof(original)];
    amark_utf8_replaced_t r;

    memcpy(bytes, original, sizeof(original));
    NAOS_REQUIRE_INT_EQ(
        amark_utf8_replace(&g_arena, bytes, sizeof(bytes) - 1, &r),
        AMARK_ERROR_NONE);
    NAOS_CHECK_MEM_EQ(bytes, sizeof(bytes), original, sizeof(original));
    NAOS_CHECK_UINT_EQ(r.replacement_count, 6);
    NAOS_CHECK_TRUE(amark_utf8_validate(r.bytes, r.length, NULL));
}

/* One allocation from the arena: one block from a fresh arena (11.7). */
NAOS_TEST(replace_allocates_once_from_the_arena)
{
    amark_utf8_replaced_t r;
    size_t i;

    NAOS_REQUIRE_INT_EQ(
        amark_utf8_replace(&g_arena, (const unsigned char *)"a\x80" "b", 3,
                           &r),
        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 1);
    NAOS_CHECK_MEM_EQ(r.bytes, r.length, "a" R "b", 5);

    /* A result larger than any block so far takes one more block. */
    g_input = (unsigned char *)malloc(1u << 20);
    NAOS_REQUIRE_NOT_NULL(g_input);
    for (i = 0; i < (1u << 20); i++) {
        g_input[i] = (unsigned char)(i % 3 == 0 ? 0x80 : 'x');
    }
    NAOS_REQUIRE_INT_EQ(amark_utf8_replace(&g_arena, g_input, 1u << 20, &r),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 2);
    NAOS_CHECK_UINT_EQ(r.replacement_count, ((1u << 20) + 2) / 3);
    NAOS_CHECK_UINT_EQ(r.length, (1u << 20) + 2 * r.replacement_count);
    NAOS_CHECK_TRUE(amark_utf8_validate(r.bytes, r.length, NULL));
}

/*
 * One allocation of exactly the result length: between two 1-byte
 * allocations from the same block, replace takes the result length rounded
 * up to AMARK_ARENA_ALIGN and nothing more. This relies on the arena carving
 * consecutive allocations from the current block in order.
 */
NAOS_TEST(replace_takes_only_the_result_from_the_arena)
{
    static const unsigned char text[] = "ab\x80" "cd\xE2\x82" "e";
    amark_utf8_replaced_t r;
    void *before;
    void *after;
    size_t rounded;

    NAOS_REQUIRE_INT_EQ(amark_arena_alloc(&g_arena, 1, &before),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(
        amark_utf8_replace(&g_arena, text, sizeof(text) - 1, &r),
        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_arena_alloc(&g_arena, 1, &after),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_UINT_EQ(g_state.malloc_calls, 1); /* all in one block */
    NAOS_CHECK_MEM_EQ(r.bytes, r.length, "ab" R "cd" R "e", 11);
    rounded = (r.length + AMARK_ARENA_ALIGN - 1) / AMARK_ARENA_ALIGN *
              AMARK_ARENA_ALIGN;
    NAOS_CHECK_PTR_EQ(r.bytes, (unsigned char *)before + AMARK_ARENA_ALIGN);
    NAOS_CHECK_PTR_EQ(after, r.bytes + rounded);
}

static void poison(amark_utf8_replaced_t *r, const unsigned char *marker)
{
    r->bytes = marker;
    r->length = 12345;
    r->replacement_count = 67890;
}

static int still_poisoned(const amark_utf8_replaced_t *r,
                          const unsigned char *marker)
{
    return r->bytes == marker && r->length == 12345 &&
           r->replacement_count == 67890;
}

NAOS_TEST(allocation_failure_is_nomem_and_changes_nothing)
{
    static const unsigned char marker = 0;
    static const unsigned char text[] = "a\x80" "b";
    amark_utf8_replaced_t r;

    poison(&r, &marker);
    g_state.fail_at = 1;
    NAOS_CHECK_INT_EQ(amark_utf8_replace(&g_arena, text, sizeof(text) - 1, &r),
                      AMARK_ERROR_NOMEM);
    NAOS_CHECK_TRUE(still_poisoned(&r, &marker));
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 1);
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
}

/* Fills buf with n random bytes, biased toward lead and continuation bytes. */
static void fill_mixed(unsigned char *buf, size_t n, uint32_t *state)
{
    static const unsigned char bytes_pool[] = {
        0x00, 0x41, 0x7F, 0x80, 0x8F, 0x90, 0x9F, 0xA0, 0xBF, 0xC0, 0xC1,
        0xC2, 0xDF, 0xE0, 0xE1, 0xEC, 0xED, 0xEE, 0xEF, 0xF0, 0xF1, 0xF3,
        0xF4, 0xF5, 0xFF};
    const size_t pool = sizeof(bytes_pool) / sizeof(bytes_pool[0]);
    size_t i;

    for (i = 0; i < n; i++) {
        *state = *state * 1103515245u + 12345u;
        if ((*state >> 28) < 4) {
            buf[i] = (unsigned char)(*state >> 16);
        } else if ((*state >> 28) < 8) {
            buf[i] = (unsigned char)'a';
        } else {
            buf[i] = bytes_pool[(*state >> 16) % pool];
        }
    }
}

/*
 * Every possible failure point of a sequence of replacements: each failing
 * replacement returns NOMEM and leaves its result unchanged, and releasing
 * the arena leaves nothing behind (12.9, 12.10).
 */
NAOS_TEST(failure_injection_loop)
{
    static const unsigned char marker = 0;
    size_t fail_at;
    size_t failures = 0;
    size_t calls_without_failure = 0;
    int succeeded = 0;

    g_input = (unsigned char *)malloc((size_t)1 << 17);
    NAOS_REQUIRE_TRUE(g_input != NULL);
    for (fail_at = 1; fail_at <= 100 && !succeeded; fail_at++) {
        amark_error_t err = AMARK_ERROR_NONE;
        size_t round;
        uint32_t seed = 99;

        amark_test_alloc_init(&g_state);
        g_state.fail_at = fail_at;
        amark_arena_init(&g_arena, &g_allocator);

        /* Growing inputs, so that later results need new blocks. */
        for (round = 0; round < 8; round++) {
            size_t length = (size_t)1 << (round * 2 + 3);
            amark_utf8_replaced_t r;

            fill_mixed(g_input, length, &seed);
            g_input[0] = 0x80; /* at least one replacement */
            poison(&r, &marker);
            err = amark_utf8_replace(&g_arena, g_input, length, &r);
            if (err != AMARK_ERROR_NONE) {
                NAOS_CHECK_TRUE(still_poisoned(&r, &marker));
                break;
            }
            NAOS_CHECK_UINT_GT(r.replacement_count, 0);
            NAOS_CHECK_TRUE(amark_utf8_validate(r.bytes, r.length, NULL));
        }
        if (err == AMARK_ERROR_NONE) {
            NAOS_CHECK_UINT_LT(g_state.malloc_calls, fail_at);
            calls_without_failure = g_state.malloc_calls;
            succeeded = 1;
        } else {
            NAOS_CHECK_INT_EQ(err, AMARK_ERROR_NOMEM);
            NAOS_CHECK_UINT_EQ(g_state.malloc_calls, fail_at);
            failures++;
        }

        amark_arena_deinit(&g_arena);
        NAOS_REQUIRE_UINT_EQ(g_state.live, 0);
        NAOS_REQUIRE_UINT_EQ(g_state.ctx_mismatch, 0);
    }
    NAOS_CHECK_TRUE(succeeded);
    /* One failure per allocator call the replacements make. */
    NAOS_CHECK_UINT_EQ(failures, calls_without_failure);
    NAOS_CHECK_UINT_GT(failures, 2);
}

/*
 * Replaces bytes[0..n) and compares with the reference: the bytes, the
 * count, the well-formedness of the result, the unchanged input and the
 * pointer identity for well-formed input. g_expected holds at least 3 * n
 * bytes. Returns 1 on agreement.
 */
static int replace_agrees(const unsigned char *bytes, size_t n)
{
    amark_utf8_replaced_t r;
    size_t ref_count;
    size_t ref_length = ref_replace(bytes, n, g_expected, &ref_count);

    if (amark_utf8_replace(&g_arena, bytes, n, &r) != AMARK_ERROR_NONE) {
        return 0;
    }
    if (r.length != ref_length || r.replacement_count != ref_count ||
        (n != 0 && memcmp(r.bytes, g_expected, ref_length) != 0) ||
        !amark_utf8_validate(r.bytes, r.length, NULL)) {
        return 0;
    }
    if ((ref_count == 0) != (r.bytes == bytes)) {
        return 0;
    }
    return 1;
}

/* Restarts the arena so that the long runs below do not grow it forever. */
static void reset_arena(void)
{
    amark_arena_deinit(&g_arena);
    amark_arena_init(&g_arena, &g_allocator);
}

/* Every byte sequence of length 1 and 2. */
NAOS_TEST(replace_of_all_short_sequences_agrees_with_the_reference)
{
    uint32_t v;
    size_t bad = 0;

    ref_prepare();
    g_expected = (unsigned char *)malloc(6);
    NAOS_REQUIRE_NOT_NULL(g_expected);
    g_input = (unsigned char *)malloc(2);
    NAOS_REQUIRE_NOT_NULL(g_input);
    for (v = 0; v < 0x100; v++) {
        g_input[0] = (unsigned char)v;
        bad += !replace_agrees(g_input, 1);
    }
    for (v = 0; v < 0x10000; v++) {
        g_input[0] = (unsigned char)(v >> 8);
        g_input[1] = (unsigned char)v;
        bad += !replace_agrees(g_input, 2);
        if ((v & 0xFFF) == 0xFFF) {
            reset_arena();
        }
    }
    NAOS_CHECK_UINT_EQ(bad, 0);
}

/* Random sequences of 0 to 64 bytes, each in a block of its exact size. */
NAOS_TEST(replace_of_random_sequences_agrees_with_the_reference)
{
    unsigned char buf[64];
    uint32_t state = 4242u;
    size_t round;
    size_t n;
    size_t bad = 0;

    ref_prepare();
    g_expected = (unsigned char *)malloc(3 * sizeof(buf));
    NAOS_REQUIRE_NOT_NULL(g_expected);
    for (round = 0; round < 100000; round++) {
        state = state * 1103515245u + 12345u;
        n = (state >> 16) % (sizeof(buf) + 1);
        fill_mixed(buf, n, &state);
        free(g_input);
        g_input = exact_copy(buf, n);
        if (n != 0) {
            NAOS_REQUIRE_NOT_NULL(g_input);
        }
        bad += !replace_agrees(g_input, n);
        if (n != 0 && memcmp(g_input, buf, n) != 0) {
            bad++;
        }
        if (round % 1000 == 999) {
            reset_arena();
        }
    }
    NAOS_CHECK_UINT_EQ(bad, 0);
}

/* One large random input (1 MiB), compared with the reference. */
NAOS_TEST(replace_of_a_large_input_agrees_with_the_reference)
{
    const size_t n = (size_t)1 << 20;
    uint32_t state = 7u;

    ref_prepare();
    g_input = (unsigned char *)malloc(n);
    NAOS_REQUIRE_NOT_NULL(g_input);
    g_expected = (unsigned char *)malloc(3 * n);
    NAOS_REQUIRE_NOT_NULL(g_expected);
    fill_mixed(g_input, n, &state);
    NAOS_CHECK_TRUE(replace_agrees(g_input, n));
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

NAOS_SUITE(replace,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(replace_matches_the_standard_example),
    NAOS_ADD_TEST(replace_matches_table_3_8),
    NAOS_ADD_TEST(replace_matches_table_3_9),
    NAOS_ADD_TEST(replace_matches_table_3_10),
    NAOS_ADD_TEST(replace_matches_table_3_11),
    NAOS_ADD_TEST(replace_uses_one_fffd_per_maximal_subpart),
    NAOS_ADD_TEST(replace_copies_well_formed_parts_unchanged),
    NAOS_ADD_TEST(replace_returns_well_formed_input_without_allocating),
    NAOS_ADD_TEST(replace_of_well_formed_input_succeeds_when_allocation_fails),
    NAOS_ADD_TEST(replace_does_not_change_the_input),
    NAOS_ADD_TEST(replace_allocates_once_from_the_arena),
    NAOS_ADD_TEST(replace_takes_only_the_result_from_the_arena),
    NAOS_ADD_TEST(allocation_failure_is_nomem_and_changes_nothing),
    NAOS_ADD_TEST(failure_injection_loop),
    NAOS_ADD_TEST(replace_of_all_short_sequences_agrees_with_the_reference),
    NAOS_ADD_TEST(replace_of_random_sequences_agrees_with_the_reference),
    NAOS_ADD_TEST(replace_of_a_large_input_agrees_with_the_reference)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(utf8),
    NAOS_ADD_SUITE(replace)
)
