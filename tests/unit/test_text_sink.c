/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of the output of the text extraction (src/text_sink.h).
 *
 * Each source input is copied to an allocation of exactly the given length,
 * so a sanitizer build reports any read past it. Each buffer is followed by
 * sentinel bytes, so a write past the capacity is seen.
 */

#include "text_sink.h"

#include <naos/naos.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define SENTINEL_BYTE 0xA5

/* U+FFFD REPLACEMENT CHARACTER. */
#define FFFD "\xEF\xBF\xBD"

/* Room for every expected result here plus the sentinel bytes after it. */
#define BUFFER_SIZE 256

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

/* Every byte of buffer from index from up to BUFFER_SIZE is the sentinel. */
static void require_sentinel_from(const unsigned char *buffer, size_t from)
{
    size_t i;

    for (i = from; i < BUFFER_SIZE; i++) {
        NAOS_REQUIRE_UINT_EQ(buffer[i], SENTINEL_BYTE);
    }
}

/*
 * put_source on the first length bytes of text, with a buffer large enough,
 * gives the expected_length bytes at expected.
 */
static void check_source_n(const char *text, size_t length,
                           const char *expected, size_t expected_length)
{
    const unsigned char *bytes = copy_input(text, length);
    unsigned char buffer[BUFFER_SIZE];
    amark_text_sink_t sink;

    NAOS_REQUIRE_TRUE(expected_length < BUFFER_SIZE);
    memset(buffer, SENTINEL_BYTE, sizeof(buffer));
    amark_text_sink_init(&sink, buffer, expected_length);
    amark_text_sink_put_source(&sink, bytes, length);
    NAOS_REQUIRE_UINT_EQ(sink.length, expected_length);
    NAOS_REQUIRE_TRUE(memcmp(buffer, expected, expected_length) == 0);
    require_sentinel_from(buffer, expected_length);

    /* Counting alone gives the same length. */
    amark_text_sink_init(&sink, NULL, 0);
    amark_text_sink_put_source(&sink, bytes, length);
    NAOS_REQUIRE_UINT_EQ(sink.length, expected_length);
}

/* String literals without U+0000 in the middle. */
#define CHECK_SOURCE(text, expected) \
    check_source_n((text), sizeof(text) - 1, (expected), sizeof(expected) - 1)

/* The source is emitted unchanged. */
#define CHECK_UNCHANGED(text) CHECK_SOURCE(text, text)

/*
 * A fixed sequence of all three operations. Its result is EMITTED_ALL; it
 * contains replacements, so a capacity may end inside a U+FFFD.
 */
#define EMITTED_ALL "x" "\xC3\xA9" FFFD "a" FFFD "b" "\xE2\x82\xAC" "\n" \
                    "<" FFFD
#define SOURCE_ALL "a\0b\xE2\x82\xAC"

static void emit_all(amark_text_sink_t *sink, const unsigned char *source)
{
    amark_text_sink_put_byte(sink, 'x');
    amark_text_sink_put_bytes(sink, (const unsigned char *)"\xC3\xA9", 2);
    amark_text_sink_put_bytes(sink, (const unsigned char *)FFFD, 3);
    amark_text_sink_put_source(sink, source, sizeof(SOURCE_ALL) - 1);
    amark_text_sink_put_byte(sink, '\n');
    amark_text_sink_put_bytes(sink, (const unsigned char *)"<", 1);
    /* An empty emission changes nothing. */
    amark_text_sink_put_bytes(sink, (const unsigned char *)"", 0);
    amark_text_sink_put_source(sink, source, 0);
    /* A lone continuation byte. */
    amark_text_sink_put_source(sink, source + 4, 1);
}

/*
 * With every capacity from 0 to past the result, the length is the length
 * of the result, the first min(capacity, length) bytes are the result's and
 * nothing at or past the capacity is written (10.3, 10.4).
 */
NAOS_TEST(length_is_the_same_for_every_capacity)
{
    const size_t total = sizeof(EMITTED_ALL) - 1;
    const unsigned char *source =
        copy_input(SOURCE_ALL, sizeof(SOURCE_ALL) - 1);
    unsigned char buffer[BUFFER_SIZE];
    amark_text_sink_t sink;
    size_t capacity;

    NAOS_REQUIRE_TRUE(total + 2 < BUFFER_SIZE);
    for (capacity = 0; capacity <= total + 2; capacity++) {
        size_t written = capacity < total ? capacity : total;

        memset(buffer, SENTINEL_BYTE, sizeof(buffer));
        amark_text_sink_init(&sink, buffer, capacity);
        NAOS_REQUIRE_UINT_EQ(sink.length, 0);
        emit_all(&sink, source);
        NAOS_REQUIRE_UINT_EQ(sink.length, total);
        NAOS_REQUIRE_TRUE(memcmp(buffer, EMITTED_ALL, written) == 0);
        require_sentinel_from(buffer, written);
    }
}

/* The three capacities named by the tests of the delivery (13.8). */
NAOS_TEST(capacity_zero_one_less_and_exact)
{
    const size_t total = sizeof(EMITTED_ALL) - 1;
    const size_t capacities[] = { 0, total - 1, total };
    const unsigned char *source =
        copy_input(SOURCE_ALL, sizeof(SOURCE_ALL) - 1);
    unsigned char buffer[BUFFER_SIZE];
    amark_text_sink_t sink;
    size_t i;

    for (i = 0; i < sizeof(capacities) / sizeof(capacities[0]); i++) {
        memset(buffer, SENTINEL_BYTE, sizeof(buffer));
        amark_text_sink_init(&sink, buffer, capacities[i]);
        emit_all(&sink, source);
        NAOS_REQUIRE_UINT_EQ(sink.length, total);
        NAOS_REQUIRE_TRUE(memcmp(buffer, EMITTED_ALL, capacities[i]) == 0);
        require_sentinel_from(buffer, capacities[i]);
    }
}

/* A NULL buffer counts with capacity 0, whatever capacity is given. */
NAOS_TEST(null_buffer_only_counts)
{
    const unsigned char *source =
        copy_input(SOURCE_ALL, sizeof(SOURCE_ALL) - 1);
    amark_text_sink_t sink;

    amark_text_sink_init(&sink, NULL, 0);
    emit_all(&sink, source);
    NAOS_REQUIRE_UINT_EQ(sink.length, sizeof(EMITTED_ALL) - 1);

    amark_text_sink_init(&sink, NULL, 64);
    NAOS_REQUIRE_UINT_EQ(sink.capacity, 0);
    emit_all(&sink, source);
    NAOS_REQUIRE_UINT_EQ(sink.length, sizeof(EMITTED_ALL) - 1);
}

/* U+0000 becomes U+FFFD, one per U+0000 (9.1, 9.3). */
NAOS_TEST(nul_becomes_replacement)
{
    check_source_n("\0", 1, FFFD, 3);
    check_source_n("\0\0", 2, FFFD FFFD, 6);
    check_source_n("a\0b", 3, "a" FFFD "b", 5);
    check_source_n("\xC3\xA9\0", 3, "\xC3\xA9" FFFD, 5);
}

/* A truncated sequence is one maximal subpart (9.2). */
NAOS_TEST(truncated_sequence_is_one_replacement)
{
    CHECK_SOURCE("\xC3", FFFD);
    CHECK_SOURCE("\xE2\x82", FFFD);
    CHECK_SOURCE("\xE2", FFFD);
    CHECK_SOURCE("\xF0\x9F\x98", FFFD);
    CHECK_SOURCE("\xF0\x9F", FFFD);
    CHECK_SOURCE("\xE2\x82" "A", FFFD "A");
    CHECK_SOURCE("\xF0\x9F\x98" "A", FFFD "A");
    CHECK_SOURCE("\xC3" "\xC3\xA9", FFFD "\xC3\xA9");
    CHECK_SOURCE("\xE2\x82" "\xE2\x82\xAC", FFFD "\xE2\x82\xAC");
    /* Two truncated sequences in a row are two maximal subparts. */
    CHECK_SOURCE("\xE2\x82\xF0\x9F\x98", FFFD FFFD);

    /* count ends the sequence even when the bytes go on (9.2). */
    check_source_n("\xE2\x82\xAC", 2, FFFD, 3);
    check_source_n("\xF0\x9F\x98\x80", 3, FFFD, 3);
}

/* Each continuation byte without a lead byte is its own subpart (9.2). */
NAOS_TEST(lone_continuation_bytes)
{
    CHECK_SOURCE("\x80", FFFD);
    CHECK_SOURCE("\xBF", FFFD);
    CHECK_SOURCE("\x80\xBF\x80", FFFD FFFD FFFD);
    CHECK_SOURCE("a\x80" "b", "a" FFFD "b");
    /* After a complete sequence, an extra continuation byte. */
    CHECK_SOURCE("\xC3\xA9\xA9", "\xC3\xA9" FFFD);
}

/* Non-shortest forms: each byte is its own subpart (9.2). */
NAOS_TEST(overlong_forms)
{
    CHECK_SOURCE("\xC0\xAF", FFFD FFFD);
    CHECK_SOURCE("\xC1\xBF", FFFD FFFD);
    CHECK_SOURCE("\xC0\x80", FFFD FFFD);
    CHECK_SOURCE("\xE0\x80\xAF", FFFD FFFD FFFD);
    CHECK_SOURCE("\xE0\x9F\xBF", FFFD FFFD FFFD);
    CHECK_SOURCE("\xF0\x80\x80\xAF", FFFD FFFD FFFD FFFD);
    CHECK_SOURCE("\xF0\x8F\xBF\xBF", FFFD FFFD FFFD FFFD);
}

/* Encoded surrogates: each byte is its own subpart (9.2). */
NAOS_TEST(surrogate_encodings)
{
    CHECK_SOURCE("\xED\xA0\x80", FFFD FFFD FFFD);
    CHECK_SOURCE("\xED\xAD\xBF", FFFD FFFD FFFD);
    CHECK_SOURCE("\xED\xB0\x80", FFFD FFFD FFFD);
    CHECK_SOURCE("\xED\xBF\xBF", FFFD FFFD FFFD);
    /* A surrogate pair encoded as two sequences. */
    CHECK_SOURCE("\xED\xA0\xBD\xED\xB8\x80", FFFD FFFD FFFD FFFD FFFD FFFD);
    /* The last code point before the surrogates is well-formed. */
    CHECK_UNCHANGED("\xED\x9F\xBF");
}

/* Bytes that can never begin a sequence and values above U+10FFFF (9.2). */
NAOS_TEST(bytes_and_values_out_of_range)
{
    CHECK_SOURCE("\xF5", FFFD);
    CHECK_SOURCE("\xFF", FFFD);
    CHECK_SOURCE("\xFE\xFF", FFFD FFFD);
    CHECK_SOURCE("\xF4\x90\x80\x80", FFFD FFFD FFFD FFFD);
    CHECK_SOURCE("\xF8\x88\x80\x80\x80", FFFD FFFD FFFD FFFD FFFD);
}

/* Well-formed characters other than U+0000 are unchanged (9.6, 9.7). */
NAOS_TEST(well_formed_characters_are_unchanged)
{
    CHECK_UNCHANGED("plain ASCII text");
    /* Two bytes: U+0080, U+00E9, U+07FF. */
    CHECK_UNCHANGED("\xC2\x80\xC3\xA9\xDF\xBF");
    /* Three bytes: U+0800, U+20AC, U+FFFD itself, U+FFFF. */
    CHECK_UNCHANGED("\xE0\xA0\x80\xE2\x82\xAC\xEF\xBF\xBD\xEF\xBF\xBF");
    /* Four bytes: U+10000, U+1F600, U+10FFFF. */
    CHECK_UNCHANGED("\xF0\x90\x80\x80\xF0\x9F\x98\x80\xF4\x8F\xBF\xBF");
    /* No normalization: e followed by U+0301 stays two characters. */
    CHECK_UNCHANGED("e\xCC\x81");
    /* No case change and no whitespace folding. */
    CHECK_UNCHANGED("MiXeD  \t  case\r\n");
}

/* Control characters are unchanged (9.6). */
NAOS_TEST(control_characters_are_unchanged)
{
    CHECK_UNCHANGED("\x01\x07\x08\t\n\x0B\x0C\r\x1B\x1F\x7F");
    /* C1 controls U+0080 to U+009F. */
    CHECK_UNCHANGED("\xC2\x80\xC2\x85\xC2\x9F");
}

/* U+FEFF is unchanged wherever it is (9.6). */
NAOS_TEST(byte_order_mark_is_unchanged)
{
    CHECK_UNCHANGED("\xEF\xBB\xBF");
    CHECK_UNCHANGED("\xEF\xBB\xBF" "a");
    CHECK_UNCHANGED("a\xEF\xBB\xBF" "b");
    CHECK_UNCHANGED("\xEF\xBB\xBF\xEF\xBB\xBF");
}

/* put_bytes and put_byte emit exactly what they are given. */
NAOS_TEST(put_byte_and_put_bytes_emit_unchanged)
{
    unsigned char buffer[BUFFER_SIZE];
    amark_text_sink_t sink;

    memset(buffer, SENTINEL_BYTE, sizeof(buffer));
    amark_text_sink_init(&sink, buffer, 9);
    amark_text_sink_put_byte(&sink, ' ');
    amark_text_sink_put_byte(&sink, '*');
    amark_text_sink_put_bytes(&sink,
                              (const unsigned char *)"\xE2\x89\x82\xCC\xB8",
                              5);
    amark_text_sink_put_byte(&sink, '\n');
    amark_text_sink_put_byte(&sink, 0x7F);
    NAOS_REQUIRE_UINT_EQ(sink.length, 9);
    NAOS_REQUIRE_TRUE(
        memcmp(buffer, " *\xE2\x89\x82\xCC\xB8\n\x7F", 9) == 0);
    require_sentinel_from(buffer, 9);
}

NAOS_SUITE(text_sink,
    NAOS_SET_SETUP(copy),
    NAOS_SET_TEARDOWN(copy),
    NAOS_ADD_TEST(length_is_the_same_for_every_capacity),
    NAOS_ADD_TEST(capacity_zero_one_less_and_exact),
    NAOS_ADD_TEST(null_buffer_only_counts),
    NAOS_ADD_TEST(nul_becomes_replacement),
    NAOS_ADD_TEST(truncated_sequence_is_one_replacement),
    NAOS_ADD_TEST(lone_continuation_bytes),
    NAOS_ADD_TEST(overlong_forms),
    NAOS_ADD_TEST(surrogate_encodings),
    NAOS_ADD_TEST(bytes_and_values_out_of_range),
    NAOS_ADD_TEST(well_formed_characters_are_unchanged),
    NAOS_ADD_TEST(control_characters_are_unchanged),
    NAOS_ADD_TEST(byte_order_mark_is_unchanged),
    NAOS_ADD_TEST(put_byte_and_put_bytes_emit_unchanged)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(text_sink)
)
