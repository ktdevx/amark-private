/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of the Unicode properties and case folding (src/unicode.h).
 *
 * The expected values come from the Unicode Character Database files in
 * AMARK_TEST_DATA_DIR (tools/data/), which this test reads itself, line by
 * line. It uses neither the generated tables nor the generator.
 */

/* fopen is standard C99; keep MSVC from warning (C4996) about it. */
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "unicode.h"

#include <naos/naos.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef AMARK_TEST_DATA_DIR
#error "AMARK_TEST_DATA_DIR must name the directory of the input data"
#endif

#define CODEPOINT_LIMIT 0x110000u  /* one past U+10FFFF */
#define LINE_MAX_LENGTH 1024
#define PATH_MAX_LENGTH 4096
#define SENTINEL 0xA5A5A5A5u

/* Bits of g_props[codepoint]. */
#define PROP_PUNCTUATION 0x01u  /* General Category P* or S* */
#define PROP_SPACE 0x02u        /* General Category Zs */
#define PROP_LISTED 0x04u       /* the code point has a line in the file */

/* A C or F mapping of CaseFolding.txt. */
typedef struct expected_fold {
    uint32_t to[AMARK_UNICODE_FOLD_MAX];
    size_t count;
} expected_fold_t;

/*
 * Fixture. NAOS_REQUIRE_* leaves a test with longjmp, so what reading the
 * files allocates lives in these static variables, and teardown (which
 * always runs) frees it. Setup reads both files; a test that needs them
 * fails with the reason when reading did not succeed.
 */
static unsigned char *g_props;        /* CODEPOINT_LIMIT elements */
static uint16_t *g_fold_index;        /* 0: no mapping; else 1 + index */
static expected_fold_t *g_folds;      /* the C and F mappings */
static size_t g_fold_count;
static size_t g_fold_capacity;
static size_t g_simple_count;         /* status C lines */
static size_t g_full_count;           /* status F lines */
static size_t g_full3_count;          /* status F lines with 3 code points */
static size_t g_ignored_count;        /* status S and T lines */
static bool g_loaded;
static char g_error[PATH_MAX_LENGTH + 128];

/* Skips spaces and tabs. */
static const char *skip_blanks(const char *p)
{
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    return p;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

/*
 * Reads 4 to 6 hexadecimal digits at *p (after blanks) as a code point of
 * at most U+10FFFF and moves *p past them. Returns false on anything else.
 */
static bool parse_codepoint(const char **p, uint32_t *out)
{
    const char *s = skip_blanks(*p);
    uint32_t value = 0;
    size_t digits = 0;
    int d;

    while ((d = hex_value(s[digits])) >= 0) {
        if (digits == 6) {
            return false;
        }
        value = value * 16u + (uint32_t)d;
        digits++;
    }
    if (digits < 4 || value >= CODEPOINT_LIMIT) {
        return false;
    }
    *p = s + digits;
    *out = value;
    return true;
}

/* Moves *p past blanks and one c, then blanks. Returns false if no c. */
static bool expect_char(const char **p, char c)
{
    const char *s = skip_blanks(*p);

    if (*s != c) {
        return false;
    }
    *p = skip_blanks(s + 1);
    return true;
}

/*
 * Reads the next line of file into line (without the line ending) and
 * cuts it at '#'. Returns 1 for a line, 0 at the end of the file and -1
 * (with g_error set) for a read error or a line too long for the buffer.
 */
static int read_line(FILE *file, const char *name, unsigned long number,
                     char line[LINE_MAX_LENGTH])
{
    size_t length;
    char *hash;

    if (fgets(line, LINE_MAX_LENGTH, file) == NULL) {
        if (ferror(file)) {
            snprintf(g_error, sizeof(g_error), "%s: read error", name);
            return -1;
        }
        return 0;
    }
    length = strlen(line);
    if (length > 0 && line[length - 1] == '\n') {
        line[--length] = '\0';
    } else if (!feof(file)) {
        snprintf(g_error, sizeof(g_error), "%s:%lu: line too long", name,
                 number);
        return -1;
    }
    if (length > 0 && line[length - 1] == '\r') {
        line[--length] = '\0';
    }
    hash = strchr(line, '#');
    if (hash != NULL) {
        *hash = '\0';
    }
    return 1;
}

static FILE *open_data_file(const char *name)
{
    char path[PATH_MAX_LENGTH];
    FILE *file;
    int written = snprintf(path, sizeof(path), "%s/%s", AMARK_TEST_DATA_DIR,
                           name);

    if (written < 0 || (size_t)written >= sizeof(path)) {
        snprintf(g_error, sizeof(g_error), "%s: path too long", name);
        return NULL;
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        snprintf(g_error, sizeof(g_error), "cannot open %s", path);
    }
    return file;
}

/*
 * Reads DerivedGeneralCategory.txt: lines "XXXX[..YYYY] ; Gc". Marks every
 * code point of a P* or S* range as punctuation and of a Zs range as space.
 * A code point listed twice is an error.
 */
static bool read_general_category(void)
{
    static const char name[] = "DerivedGeneralCategory.txt";
    char line[LINE_MAX_LENGTH];
    unsigned long number = 0;
    bool ok = true;
    FILE *file = open_data_file(name);
    int status = 0;

    if (file == NULL) {
        return false;
    }
    while (ok && (status = read_line(file, name, ++number, line)) == 1) {
        const char *p = skip_blanks(line);
        uint32_t first;
        uint32_t last;
        uint32_t cp;
        unsigned char bits = PROP_LISTED;
        char category[3];

        if (*p == '\0') {
            continue;
        }
        if (!parse_codepoint(&p, &first)) {
            ok = false;
            break;
        }
        last = first;
        if (p[0] == '.' && p[1] == '.') {
            p += 2;
            if (!parse_codepoint(&p, &last)) {
                ok = false;
                break;
            }
        }
        if (last < first || !expect_char(&p, ';')) {
            ok = false;
            break;
        }
        if (!((p[0] >= 'A' && p[0] <= 'Z') && (p[1] >= 'a' && p[1] <= 'z'))) {
            ok = false;
            break;
        }
        category[0] = p[0];
        category[1] = p[1];
        category[2] = '\0';
        if (*skip_blanks(p + 2) != '\0') {
            ok = false;
            break;
        }
        if (category[0] == 'P' || category[0] == 'S') {
            bits |= PROP_PUNCTUATION;
        } else if (strcmp(category, "Zs") == 0) {
            bits |= PROP_SPACE;
        }
        for (cp = first; cp <= last; cp++) {
            if (g_props[cp] & PROP_LISTED) {
                snprintf(g_error, sizeof(g_error),
                         "%s:%lu: U+%04lX is listed twice", name, number,
                         (unsigned long)cp);
                fclose(file);
                return false;
            }
            g_props[cp] = bits;
        }
    }
    if (ok && status < 0) {
        fclose(file);
        return false;
    }
    fclose(file);
    if (!ok) {
        snprintf(g_error, sizeof(g_error), "%s:%lu: malformed line", name,
                 number);
    }
    return ok;
}

/* Adds a C or F mapping for from. A second C or F mapping is an error. */
static bool add_fold(uint32_t from, const uint32_t *to, size_t count)
{
    expected_fold_t *entry;
    size_t i;

    if (g_fold_index[from] != 0) {
        return false;
    }
    if (g_fold_count == g_fold_capacity) {
        size_t capacity = g_fold_capacity == 0 ? 256 : g_fold_capacity * 2;
        expected_fold_t *grown;

        if (capacity > UINT16_MAX) {
            return false;
        }
        grown = (expected_fold_t *)realloc(g_folds,
                                           capacity * sizeof(*grown));
        if (grown == NULL) {
            return false;
        }
        g_folds = grown;
        g_fold_capacity = capacity;
    }
    entry = &g_folds[g_fold_count];
    for (i = 0; i < count; i++) {
        entry->to[i] = to[i];
    }
    entry->count = count;
    g_fold_count++;
    g_fold_index[from] = (uint16_t)g_fold_count;
    return true;
}

/*
 * Reads CaseFolding.txt: lines "XXXX; status; YYYY [ZZZZ ...];". Keeps the
 * C (one code point) and F (two or three) mappings and skips S and T.
 */
static bool read_case_folding(void)
{
    static const char name[] = "CaseFolding.txt";
    char line[LINE_MAX_LENGTH];
    unsigned long number = 0;
    bool ok = true;
    FILE *file = open_data_file(name);
    int status = 0;

    if (file == NULL) {
        return false;
    }
    while (ok && (status = read_line(file, name, ++number, line)) == 1) {
        const char *p = skip_blanks(line);
        uint32_t from;
        uint32_t to[AMARK_UNICODE_FOLD_MAX];
        size_t count = 0;
        char kind;

        if (*p == '\0') {
            continue;
        }
        if (!parse_codepoint(&p, &from) || !expect_char(&p, ';')) {
            ok = false;
            break;
        }
        kind = *p;
        p++;
        if (!expect_char(&p, ';')) {
            ok = false;
            break;
        }
        while (*p != ';' && *p != '\0') {
            if (count == AMARK_UNICODE_FOLD_MAX ||
                !parse_codepoint(&p, &to[count])) {
                ok = false;
                break;
            }
            count++;
            p = skip_blanks(p);
        }
        if (!ok || !expect_char(&p, ';') || *p != '\0' || count == 0) {
            ok = false;
            break;
        }
        if (kind == 'C' && count == 1) {
            g_simple_count++;
        } else if (kind == 'F' && count >= 2) {
            g_full_count++;
            if (count == 3) {
                g_full3_count++;
            }
        } else if ((kind == 'S' || kind == 'T') && count == 1) {
            g_ignored_count++;
            continue;
        } else {
            ok = false;
            break;
        }
        if (!add_fold(from, to, count)) {
            snprintf(g_error, sizeof(g_error),
                     "%s:%lu: second C or F mapping of U+%04lX, or no memory",
                     name, number, (unsigned long)from);
            fclose(file);
            return false;
        }
    }
    if (ok && status < 0) {
        fclose(file);
        return false;
    }
    fclose(file);
    if (!ok) {
        snprintf(g_error, sizeof(g_error), "%s:%lu: malformed line", name,
                 number);
    }
    return ok;
}

NAOS_SETUP(data)
{
    g_loaded = false;
    g_error[0] = '\0';
    g_fold_count = 0;
    g_fold_capacity = 0;
    g_simple_count = 0;
    g_full_count = 0;
    g_full3_count = 0;
    g_ignored_count = 0;
    g_folds = NULL;
    g_props = (unsigned char *)calloc(CODEPOINT_LIMIT, sizeof(*g_props));
    g_fold_index = (uint16_t *)calloc(CODEPOINT_LIMIT, sizeof(*g_fold_index));
    if (g_props == NULL || g_fold_index == NULL) {
        snprintf(g_error, sizeof(g_error), "out of memory");
        return;
    }
    g_loaded = read_general_category() && read_case_folding();
}

NAOS_TEARDOWN(data)
{
    free(g_props);
    free(g_fold_index);
    free(g_folds);
    g_props = NULL;
    g_fold_index = NULL;
    g_folds = NULL;
    g_fold_count = 0;
    g_fold_capacity = 0;
    g_loaded = false;
}

/* Stops the test, with the reason, if the input data was not read. */
static void require_data(void)
{
    if (!g_loaded) {
        NAOS_NOTE("%s", g_error);
        NAOS_FAIL("the input data in " AMARK_TEST_DATA_DIR
                  " could not be read");
    }
}

static bool expected_punctuation(uint32_t cp)
{
    return cp < CODEPOINT_LIMIT && (g_props[cp] & PROP_PUNCTUATION) != 0;
}

static bool expected_whitespace(uint32_t cp)
{
    if (cp == 0x09 || cp == 0x0A || cp == 0x0C || cp == 0x0D) {
        return true;
    }
    return cp < CODEPOINT_LIMIT && (g_props[cp] & PROP_SPACE) != 0;
}

/* The expected folding of cp: the C or F mapping, or else cp itself. */
static size_t expected_fold(uint32_t cp, uint32_t to[AMARK_UNICODE_FOLD_MAX])
{
    size_t i;
    const expected_fold_t *entry;

    if (cp >= CODEPOINT_LIMIT || g_fold_index[cp] == 0) {
        to[0] = cp;
        return 1;
    }
    entry = &g_folds[g_fold_index[cp] - 1];
    for (i = 0; i < entry->count; i++) {
        to[i] = entry->to[i];
    }
    return entry->count;
}

/*
 * Folds cp into a buffer with a sentinel in every element and returns the
 * count. *intact is false if an element past the count was written.
 */
static size_t fold_with_sentinel(uint32_t cp,
                                 uint32_t out[AMARK_UNICODE_FOLD_MAX + 1],
                                 bool *intact)
{
    size_t i;
    size_t count;

    for (i = 0; i <= AMARK_UNICODE_FOLD_MAX; i++) {
        out[i] = SENTINEL;
    }
    count = amark_unicode_fold(cp, out);
    *intact = true;
    for (i = count; i <= AMARK_UNICODE_FOLD_MAX; i++) {
        if (out[i] != SENTINEL) {
            *intact = false;
        }
    }
    return count;
}

/* The input files were read and hold mappings of every kind. */
NAOS_TEST(input_data_is_read)
{
    size_t punctuation = 0;
    size_t space = 0;
    size_t listed = 0;
    uint32_t cp;

    require_data();
    for (cp = 0; cp < CODEPOINT_LIMIT; cp++) {
        punctuation += (g_props[cp] & PROP_PUNCTUATION) != 0;
        space += (g_props[cp] & PROP_SPACE) != 0;
        listed += (g_props[cp] & PROP_LISTED) != 0;
    }
    NAOS_CHECK_UINT_GT(listed, 0);
    NAOS_CHECK_UINT_GE(punctuation, 32);
    NAOS_CHECK_UINT_GE(space, 2);
    NAOS_CHECK_UINT_GT(g_simple_count, 0);
    NAOS_CHECK_UINT_GT(g_full_count, 0);
    NAOS_CHECK_UINT_GT(g_full3_count, 0);
    NAOS_CHECK_UINT_GT(g_ignored_count, 0);
    NAOS_CHECK_UINT_EQ(g_fold_count, g_simple_count + g_full_count);
}

/* 4.1, 4.2, 4.4, 4.5: punctuation is General Category P* or S*. */
NAOS_TEST(punctuation_matches_the_data_for_every_code_point)
{
    size_t mismatches = 0;
    uint32_t first = 0;
    uint32_t cp;

    require_data();
    for (cp = 0; cp < CODEPOINT_LIMIT; cp++) {
        if (amark_unicode_is_punctuation(cp) != expected_punctuation(cp)) {
            if (mismatches == 0) {
                first = cp;
            }
            mismatches++;
        }
    }
    NAOS_NOTE("first mismatch U+%04lX: expected %d", (unsigned long)first,
              (int)expected_punctuation(first));
    NAOS_CHECK_UINT_EQ(mismatches, 0);
}

/* 5.1 to 5.3: whitespace is Zs, or tab, LF, FF and CR. */
NAOS_TEST(whitespace_matches_the_data_for_every_code_point)
{
    size_t mismatches = 0;
    uint32_t first = 0;
    uint32_t cp;

    require_data();
    for (cp = 0; cp < CODEPOINT_LIMIT; cp++) {
        if (amark_unicode_is_whitespace(cp) != expected_whitespace(cp)) {
            if (mismatches == 0) {
                first = cp;
            }
            mismatches++;
        }
    }
    NAOS_NOTE("first mismatch U+%04lX: expected %d", (unsigned long)first,
              (int)expected_whitespace(first));
    NAOS_CHECK_UINT_EQ(mismatches, 0);
}

/* 5.5: no code point is both whitespace and punctuation. */
NAOS_TEST(no_code_point_is_both_whitespace_and_punctuation)
{
    size_t both = 0;
    uint32_t first = 0;
    uint32_t cp;

    for (cp = 0; cp < CODEPOINT_LIMIT; cp++) {
        if (amark_unicode_is_whitespace(cp) &&
            amark_unicode_is_punctuation(cp)) {
            if (both == 0) {
                first = cp;
            }
            both++;
        }
    }
    NAOS_NOTE("first such code point U+%04lX", (unsigned long)first);
    NAOS_CHECK_UINT_EQ(both, 0);
}

/*
 * 6.1 to 6.4, 6.6, 6.8: folding gives the C or F mapping (never S or T),
 * or else the code point itself, and writes nothing past the count.
 */
NAOS_TEST(folding_matches_the_data_for_every_code_point)
{
    size_t mismatches = 0;
    size_t overwrites = 0;
    uint32_t first = 0;
    uint32_t first_overwrite = 0;
    uint32_t cp;

    require_data();
    for (cp = 0; cp < CODEPOINT_LIMIT; cp++) {
        uint32_t out[AMARK_UNICODE_FOLD_MAX + 1] = {0};
        uint32_t expected[AMARK_UNICODE_FOLD_MAX] = {0};
        size_t expected_count = expected_fold(cp, expected);
        bool intact;
        size_t count = fold_with_sentinel(cp, out, &intact);

        if (count != expected_count ||
            memcmp(out, expected, count * sizeof(out[0])) != 0) {
            if (mismatches == 0) {
                first = cp;
            }
            mismatches++;
        }
        if (!intact) {
            if (overwrites == 0) {
                first_overwrite = cp;
            }
            overwrites++;
        }
    }
    if (mismatches > 0) {
        uint32_t out[AMARK_UNICODE_FOLD_MAX + 1] = {0};
        uint32_t expected[AMARK_UNICODE_FOLD_MAX] = {0};
        size_t expected_count = expected_fold(first, expected);
        bool intact;
        size_t count = fold_with_sentinel(first, out, &intact);

        NAOS_NOTE("first mismatch U+%04lX: got %lu (%04lX %04lX %04lX), "
                  "expected %lu (%04lX %04lX %04lX)",
                  (unsigned long)first, (unsigned long)count,
                  (unsigned long)out[0],
                  count > 1 ? (unsigned long)out[1] : 0ul,
                  count > 2 ? (unsigned long)out[2] : 0ul,
                  (unsigned long)expected_count, (unsigned long)expected[0],
                  expected_count > 1 ? (unsigned long)expected[1] : 0ul,
                  expected_count > 2 ? (unsigned long)expected[2] : 0ul);
    }
    NAOS_CHECK_UINT_EQ(mismatches, 0);
    NAOS_NOTE("first write past the count at U+%04lX",
              (unsigned long)first_overwrite);
    NAOS_CHECK_UINT_EQ(overwrites, 0);
}

/* 4.3: the 32 ASCII punctuation characters, and no other ASCII. */
NAOS_TEST(ascii_punctuation_characters_are_punctuation)
{
    static const char punctuation[] = "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
    uint32_t cp;

    NAOS_REQUIRE_UINT_EQ(sizeof(punctuation) - 1, 32);
    for (cp = 0; cp < 0x80; cp++) {
        bool listed = cp != 0 && strchr(punctuation, (int)cp) != NULL;

        NAOS_NOTE("U+%04lX", (unsigned long)cp);
        NAOS_CHECK_INT_EQ(amark_unicode_is_punctuation(cp), listed);
    }
}

/* 5.1, 5.2, 5.4: which line and space characters are whitespace. */
NAOS_TEST(whitespace_examples)
{
    NAOS_CHECK_TRUE(amark_unicode_is_whitespace(0x0009));
    NAOS_CHECK_TRUE(amark_unicode_is_whitespace(0x000A));
    NAOS_CHECK_TRUE(amark_unicode_is_whitespace(0x000C));
    NAOS_CHECK_TRUE(amark_unicode_is_whitespace(0x000D));
    NAOS_CHECK_TRUE(amark_unicode_is_whitespace(0x0020));
    NAOS_CHECK_TRUE(amark_unicode_is_whitespace(0x00A0));
    NAOS_CHECK_TRUE(amark_unicode_is_whitespace(0x3000));

    NAOS_CHECK_FALSE(amark_unicode_is_whitespace(0x000B));
    NAOS_CHECK_FALSE(amark_unicode_is_whitespace(0x0085));
    NAOS_CHECK_FALSE(amark_unicode_is_whitespace(0x2028));
    NAOS_CHECK_FALSE(amark_unicode_is_whitespace(0x2029));
    NAOS_CHECK_FALSE(amark_unicode_is_whitespace(0x0000));
    NAOS_CHECK_FALSE(amark_unicode_is_whitespace(0x0041));
}

/* Checks that folding cp gives exactly the n code points that follow. */
#define CHECK_FOLD(cp, n, ...)                                               \
    do {                                                                     \
        static const uint32_t expected_[] = {__VA_ARGS__};                   \
        uint32_t out_[AMARK_UNICODE_FOLD_MAX + 1];                           \
        bool intact_;                                                        \
        size_t count_ = fold_with_sentinel((cp), out_, &intact_);            \
        NAOS_NOTE("U+%04lX", (unsigned long)(cp));                           \
        NAOS_REQUIRE_UINT_EQ(count_, (n));                                   \
        NAOS_NOTE("U+%04lX", (unsigned long)(cp));                           \
        NAOS_CHECK_MEM_EQ(out_, (n) * sizeof(uint32_t), expected_,           \
                          sizeof(expected_));                                \
        NAOS_NOTE("U+%04lX", (unsigned long)(cp));                           \
        NAOS_CHECK_TRUE(intact_);                                            \
    } while (0)

/* Checks that cp folds to itself alone. */
static void check_folds_to_itself(uint32_t cp)
{
    uint32_t out[AMARK_UNICODE_FOLD_MAX + 1];
    bool intact;
    size_t count = fold_with_sentinel(cp, out, &intact);

    NAOS_NOTE("0x%08lX", (unsigned long)cp);
    NAOS_CHECK_UINT_EQ(count, 1);
    NAOS_NOTE("0x%08lX", (unsigned long)cp);
    NAOS_CHECK_UINT_EQ(out[0], cp);
    NAOS_NOTE("0x%08lX", (unsigned long)cp);
    NAOS_CHECK_TRUE(intact);
}

/* 6.7: the examples of the requirements. */
NAOS_TEST(folding_examples)
{
    uint32_t out[AMARK_UNICODE_FOLD_MAX + 1];
    bool intact;

    CHECK_FOLD(0x1E9E, 2, 0x0073, 0x0073);
    CHECK_FOLD(0x00DF, 2, 0x0073, 0x0073);
    CHECK_FOLD(0x0041, 1, 0x0061);
    CHECK_FOLD(0x0061, 1, 0x0061);
    CHECK_FOLD(0x0390, 3, 0x03B9, 0x0308, 0x0301);
    CHECK_FOLD(0x0030, 1, 0x0030);

    /* U+1E9E has the status S mapping 1E9E; S; 00DF, which is not used. */
    fold_with_sentinel(0x1E9E, out, &intact);
    NAOS_CHECK_UINT_NE(out[0], 0x00DF);
}

/* 4.4, 8.6: values above U+10FFFF. */
NAOS_TEST(values_above_10ffff)
{
    static const uint32_t values[] = {0x110000u, 0x7FFFFFFFu, 0xFFFFFFFFu};
    size_t i;

    for (i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        NAOS_NOTE("0x%08lX", (unsigned long)values[i]);
        NAOS_CHECK_FALSE(amark_unicode_is_punctuation(values[i]));
        NAOS_NOTE("0x%08lX", (unsigned long)values[i]);
        NAOS_CHECK_FALSE(amark_unicode_is_whitespace(values[i]));
        check_folds_to_itself(values[i]);
    }
}

/* 4.4, 8.6: surrogates. */
NAOS_TEST(surrogates)
{
    static const uint32_t values[] = {0xD800u, 0xDBFFu, 0xDC00u, 0xDFFFu};
    size_t i;

    for (i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        NAOS_NOTE("U+%04lX", (unsigned long)values[i]);
        NAOS_CHECK_FALSE(amark_unicode_is_punctuation(values[i]));
        NAOS_NOTE("U+%04lX", (unsigned long)values[i]);
        NAOS_CHECK_FALSE(amark_unicode_is_whitespace(values[i]));
        check_folds_to_itself(values[i]);
    }
}

NAOS_SUITE(unicode,
    NAOS_SET_SETUP(data),
    NAOS_SET_TEARDOWN(data),
    NAOS_ADD_TEST(input_data_is_read),
    NAOS_ADD_TEST(punctuation_matches_the_data_for_every_code_point),
    NAOS_ADD_TEST(whitespace_matches_the_data_for_every_code_point),
    NAOS_ADD_TEST(no_code_point_is_both_whitespace_and_punctuation),
    NAOS_ADD_TEST(folding_matches_the_data_for_every_code_point)
)

NAOS_SUITE(examples,
    NAOS_ADD_TEST(ascii_punctuation_characters_are_punctuation),
    NAOS_ADD_TEST(whitespace_examples),
    NAOS_ADD_TEST(folding_examples),
    NAOS_ADD_TEST(values_above_10ffff),
    NAOS_ADD_TEST(surrogates)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(unicode),
    NAOS_ADD_SUITE(examples)
)
