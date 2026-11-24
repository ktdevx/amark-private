/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of the syntax and the value of character references
 * (src/charref.h).
 *
 * Each input is copied to an allocation of exactly the given length, so a
 * sanitizer build reports any read past it.
 */

#include "charref.h"

#include <naos/naos.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define SENTINEL_BYTE 0xA5
#define SENTINEL_LENGTH ((size_t)0xA5A5A5A5u)

/* U+FFFD REPLACEMENT CHARACTER. */
#define FFFD "\xEF\xBF\xBD"

/*
 * Fixture. NAOS_REQUIRE_* leaves a test with longjmp, so the copy of the
 * input lives here and teardown (which always runs) frees it.
 */
static unsigned char *g_copy;

NAOS_SETUP(copy)
{
    g_copy = NULL;
}

NAOS_TEARDOWN(copy)
{
    free(g_copy);
    g_copy = NULL;
}

/* Copies the first length bytes of text to g_copy (length at least 1). */
static const unsigned char *copy_input(const char *text, size_t length)
{
    free(g_copy);
    g_copy = (unsigned char *)malloc(length);
    NAOS_REQUIRE_NOT_NULL(g_copy);
    memcpy(g_copy, text, length);
    return g_copy;
}

static void fill_sentinel(amark_charref_t *out)
{
    memset(out->utf8, SENTINEL_BYTE, sizeof(out->utf8));
    out->utf8_length = SENTINEL_LENGTH;
}

/*
 * The first length bytes of text are a character reference of
 * expected_length bytes whose value is the utf8_length bytes at utf8.
 */
static void check_ref_n(const char *text, size_t length,
                        size_t expected_length, const char *utf8,
                        size_t utf8_length)
{
    const unsigned char *bytes = copy_input(text, length);
    amark_charref_t out;

    fill_sentinel(&out);
    NAOS_REQUIRE_UINT_EQ(amark_charref_scan(bytes, length, &out),
                         expected_length);
    NAOS_REQUIRE_UINT_EQ(out.utf8_length, utf8_length);
    NAOS_REQUIRE_TRUE(memcmp(out.utf8, utf8, utf8_length) == 0);

    /* The length comes back without asking for the value. */
    NAOS_REQUIRE_UINT_EQ(amark_charref_scan(bytes, length, NULL),
                         expected_length);
}

/* The whole of text is a character reference with the given value. */
#define CHECK_REF(text, value) \
    check_ref_n((text), sizeof(text) - 1, sizeof(text) - 1, (value), \
                sizeof(value) - 1)

/* The first length bytes of text are no character reference. */
static void check_no_ref_n(const char *text, size_t length)
{
    const unsigned char *bytes = copy_input(text, length);
    amark_charref_t out;
    size_t i;

    fill_sentinel(&out);
    NAOS_REQUIRE_UINT_EQ(amark_charref_scan(bytes, length, &out), 0);
    NAOS_REQUIRE_UINT_EQ(out.utf8_length, SENTINEL_LENGTH);
    for (i = 0; i < sizeof(out.utf8); i++) {
        NAOS_REQUIRE_UINT_EQ(out.utf8[i], SENTINEL_BYTE);
    }
    NAOS_REQUIRE_UINT_EQ(amark_charref_scan(bytes, length, NULL), 0);
}

#define CHECK_NO_REF(text) check_no_ref_n((text), sizeof(text) - 1)

/* 5.3: names in the table give their one or two code points. */
NAOS_TEST(names_in_the_table)
{
    CHECK_REF("&amp;", "&");
    CHECK_REF("&nbsp;", "\xC2\xA0");
    CHECK_REF("&copy;", "\xC2\xA9");
    /* Two code points: U+2242 U+0338. */
    CHECK_REF("&NotEqualTilde;", "\xE2\x89\x82\xCC\xB8");
    /* Longer than the reference: U+226B U+20D2 (6 bytes for 5). */
    CHECK_REF("&nGt;", "\xE2\x89\xAB\xE2\x83\x92");
    /* Digits in the name. */
    CHECK_REF("&frac34;", "\xC2\xBE");
}

/* 5.8: case matters. */
NAOS_TEST(names_are_case_sensitive)
{
    CHECK_REF("&Auml;", "\xC3\x84");
    CHECK_REF("&auml;", "\xC3\xA4");
    CHECK_REF("&AMP;", "&");
    CHECK_NO_REF("&Amp;");
    CHECK_NO_REF("&aMP;");
}

/* 5.7: names that are not entities, or not followed by ';'. */
NAOS_TEST(other_names_are_not_references)
{
    CHECK_NO_REF("&foo;");
    CHECK_NO_REF("&ampx;");
    CHECK_NO_REF("&hi?;");
    CHECK_NO_REF("&amp");
    CHECK_NO_REF("&amp ;");
    CHECK_NO_REF("&am-p;");
    CHECK_NO_REF("& amp;");
    /* The empty name. */
    CHECK_NO_REF("&;");
    CHECK_NO_REF("&");
    CHECK_NO_REF("&&amp;");
    /* Not even the start of a reference. */
    CHECK_NO_REF("&x");
}

/* Only the reference counts; what follows the ';' is not read into it. */
NAOS_TEST(reference_ends_at_the_semicolon)
{
    check_ref_n("&amp;amp;", 9, 5, "&", 1);
    check_ref_n("&#35;;", 6, 5, "#", 1);
    check_ref_n("&#x41;B", 7, 6, "A", 1);
    check_ref_n("&nbsp;x", 7, 6, "\xC2\xA0", 2);
}

/* 5.4: 1 to 7 decimal digits. */
NAOS_TEST(decimal_references)
{
    CHECK_REF("&#35;", "#");
    CHECK_REF("&#1;", "\x01");
    CHECK_REF("&#1234;", "\xD3\x92");
    CHECK_REF("&#0000035;", "#");
    CHECK_REF("&#9999999;", FFFD);
    CHECK_REF("&#1114111;", "\xF4\x8F\xBF\xBF");
    /* 8 digits. */
    CHECK_NO_REF("&#00000035;");
    CHECK_NO_REF("&#12345678;");
    /* No digits. */
    CHECK_NO_REF("&#;");
    CHECK_NO_REF("&#");
    CHECK_NO_REF("&#a;");
    CHECK_NO_REF("&#-1;");
    CHECK_NO_REF("&#35");
    CHECK_NO_REF("&#35 ;");
    CHECK_NO_REF("&#3a;");
}

/* 5.5: 1 to 6 hexadecimal digits after 'x' or 'X'. */
NAOS_TEST(hexadecimal_references)
{
    CHECK_REF("&#x22;", "\"");
    CHECK_REF("&#X22;", "\"");
    CHECK_REF("&#XD06;", "\xE0\xB4\x86");
    CHECK_REF("&#xcab;", "\xE0\xB2\xAB");
    CHECK_REF("&#xCAB;", "\xE0\xB2\xAB");
    CHECK_REF("&#x00002F;", "/");
    CHECK_REF("&#xFFFFFF;", FFFD);
    /* 7 digits. */
    CHECK_NO_REF("&#x000002F;");
    CHECK_NO_REF("&#x1000000;");
    /* No digits. */
    CHECK_NO_REF("&#x;");
    CHECK_NO_REF("&#X;");
    CHECK_NO_REF("&#x");
    CHECK_NO_REF("&#xg;");
    CHECK_NO_REF("&#x22");
    CHECK_NO_REF("&#x2g;");
    CHECK_NO_REF("&#xx22;");
}

/* 5.6: 0, the surrogates and values above U+10FFFF give U+FFFD. */
NAOS_TEST(numeric_values_at_the_boundaries)
{
    CHECK_REF("&#0;", FFFD);
    CHECK_REF("&#x0;", FFFD);
    CHECK_REF("&#0000000;", FFFD);
    CHECK_REF("&#x7F;", "\x7F");
    CHECK_REF("&#x80;", "\xC2\x80");
    CHECK_REF("&#x7FF;", "\xDF\xBF");
    CHECK_REF("&#x800;", "\xE0\xA0\x80");
    CHECK_REF("&#xD7FF;", "\xED\x9F\xBF");
    CHECK_REF("&#55295;", "\xED\x9F\xBF");
    CHECK_REF("&#xD800;", FFFD);
    CHECK_REF("&#55296;", FFFD);
    CHECK_REF("&#xDFFF;", FFFD);
    CHECK_REF("&#57343;", FFFD);
    CHECK_REF("&#xE000;", "\xEE\x80\x80");
    CHECK_REF("&#57344;", "\xEE\x80\x80");
    CHECK_REF("&#xFFFF;", "\xEF\xBF\xBF");
    CHECK_REF("&#x10000;", "\xF0\x90\x80\x80");
    CHECK_REF("&#x10FFFF;", "\xF4\x8F\xBF\xBF");
    CHECK_REF("&#x110000;", FFFD);
    CHECK_REF("&#1114112;", FFFD);
}

/* A reference cut by length (the end of the line's content) is none. */
NAOS_TEST(references_cut_by_length)
{
    static const char *const texts[] = {
        "&amp;", "&nbsp;", "&#35;", "&#1234567;", "&#x22;", "&#X10FFFF;",
        "&NotEqualTilde;"
    };
    size_t i;
    size_t n;

    for (i = 0; i < sizeof(texts) / sizeof(texts[0]); i++) {
        size_t full = strlen(texts[i]);

        for (n = 1; n < full; n++) {
            check_no_ref_n(texts[i], n);
        }
    }
    /* A shorter name in the table is not taken from a cut longer one. */
    check_no_ref_n("&amp;", 4);
    check_no_ref_n("&notin;", 4);
}

NAOS_SUITE(charref,
    NAOS_SET_SETUP(copy),
    NAOS_SET_TEARDOWN(copy),
    NAOS_ADD_TEST(names_in_the_table),
    NAOS_ADD_TEST(names_are_case_sensitive),
    NAOS_ADD_TEST(other_names_are_not_references),
    NAOS_ADD_TEST(reference_ends_at_the_semicolon),
    NAOS_ADD_TEST(decimal_references),
    NAOS_ADD_TEST(hexadecimal_references),
    NAOS_ADD_TEST(numeric_values_at_the_boundaries),
    NAOS_ADD_TEST(references_cut_by_length)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(charref)
)
