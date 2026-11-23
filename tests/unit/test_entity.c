/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of the HTML5 named character references (src/entity.h).
 *
 * The expected values come from entities.json in AMARK_TEST_DATA_DIR
 * (tools/data/), which this test reads itself, assuming the layout of that
 * file: one entry per line. A line of any other shape fails the test. It
 * uses neither the generated table nor the generator.
 */

/* fopen is standard C99; keep MSVC from warning (C4996) about it. */
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "entity.h"

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

#define LINE_MAX_LENGTH 1024
#define PATH_MAX_LENGTH 4096
#define NAME_CAPACITY 64        /* longer names in the file are an error */
#define CODEPOINTS_MAX 2        /* an entity stands for 1 or 2 code points */
#define UTF8_CAPACITY 8         /* 2 code points of at most 4 bytes */
#define EXPECTED_COUNT 2125     /* the names that end in ';' */
#define LONG_NAME_LENGTH 1000
#define SENTINEL_LENGTH ((size_t)0xA5A5A5A5u)

/* An entity of entities.json whose name ends in ';'. */
typedef struct expected_entity {
    char name[NAME_CAPACITY];   /* without '&' and ';'; not NUL-terminated */
    size_t name_length;
    unsigned char utf8[UTF8_CAPACITY];
    size_t utf8_length;
} expected_entity_t;

/*
 * Fixture. NAOS_REQUIRE_* leaves a test with longjmp, so what reading the
 * file allocates lives in these static variables, and teardown (which
 * always runs) frees it. Setup reads the file; a test that needs it fails
 * with the reason when reading did not succeed.
 */
static expected_entity_t *g_entities;   /* sorted by name after reading */
static size_t g_count;                  /* names that end in ';' */
static size_t g_capacity;
static size_t g_other_count;            /* names without ';' */
static size_t g_longest;                /* the longest name_length */
static size_t g_longest_index;
static bool g_loaded;
static char g_error[PATH_MAX_LENGTH + 128];

/*
 * Where a lookup that does not find the name must leave its outputs: a
 * pointer that the function cannot return and an unlikely length.
 */
static const unsigned char g_sentinel_bytes[1] = {0};

/* Skips spaces and tabs. */
static const char *skip_blanks(const char *p)
{
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    return p;
}

/* Moves *p past blanks and the text literal. Returns false if no literal. */
static bool expect_text(const char **p, const char *literal)
{
    const char *s = skip_blanks(*p);
    size_t length = strlen(literal);

    if (strncmp(s, literal, length) != 0) {
        return false;
    }
    *p = s + length;
    return true;
}

static bool is_name_char(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9');
}

/*
 * Reads 1 to 7 decimal digits at *p (after blanks) as a Unicode scalar
 * value (at most U+10FFFF, no surrogate) and moves *p past them.
 */
static bool parse_codepoint(const char **p, uint32_t *out)
{
    const char *s = skip_blanks(*p);
    uint32_t value = 0;
    size_t digits = 0;

    while (s[digits] >= '0' && s[digits] <= '9') {
        if (digits == 7) {
            return false;
        }
        value = value * 10u + (uint32_t)(s[digits] - '0');
        digits++;
    }
    if (digits == 0 || value > 0x10FFFFu ||
        (value >= 0xD800u && value <= 0xDFFFu)) {
        return false;
    }
    *p = s + digits;
    *out = value;
    return true;
}

/* Appends the UTF-8 encoding of the scalar value cp at out[*length]. */
static void encode_utf8(uint32_t cp, unsigned char *out, size_t *length)
{
    size_t n = *length;

    if (cp < 0x80u) {
        out[n++] = (unsigned char)cp;
    } else if (cp < 0x800u) {
        out[n++] = (unsigned char)(0xC0u | (cp >> 6));
        out[n++] = (unsigned char)(0x80u | (cp & 0x3Fu));
    } else if (cp < 0x10000u) {
        out[n++] = (unsigned char)(0xE0u | (cp >> 12));
        out[n++] = (unsigned char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[n++] = (unsigned char)(0x80u | (cp & 0x3Fu));
    } else {
        out[n++] = (unsigned char)(0xF0u | (cp >> 18));
        out[n++] = (unsigned char)(0x80u | ((cp >> 12) & 0x3Fu));
        out[n++] = (unsigned char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[n++] = (unsigned char)(0x80u | (cp & 0x3Fu));
    }
    *length = n;
}

/* Orders names byte by byte (as unsigned char), then the shorter first. */
static int compare_names(const char *a, size_t a_length, const char *b,
                         size_t b_length)
{
    size_t common = a_length < b_length ? a_length : b_length;
    int result = memcmp(a, b, common);

    if (result != 0) {
        return result;
    }
    if (a_length != b_length) {
        return a_length < b_length ? -1 : 1;
    }
    return 0;
}

static int compare_entities(const void *a, const void *b)
{
    const expected_entity_t *x = (const expected_entity_t *)a;
    const expected_entity_t *y = (const expected_entity_t *)b;

    return compare_names(x->name, x->name_length, y->name, y->name_length);
}

/* Whether the name is one of the names that end in ';' in the file. */
static bool in_table(const char *name, size_t name_length)
{
    size_t low = 0;
    size_t high = g_count;

    while (low < high) {
        size_t mid = low + (high - low) / 2;
        int result = compare_names(name, name_length, g_entities[mid].name,
                                   g_entities[mid].name_length);

        if (result < 0) {
            high = mid;
        } else if (result > 0) {
            low = mid + 1;
        } else {
            return true;
        }
    }
    return false;
}

/* Adds an entity that ends in ';'. Returns false when out of memory. */
static bool add_entity(const char *name, size_t name_length,
                       const uint32_t *codepoints, size_t count)
{
    expected_entity_t *entry;
    size_t i;

    if (g_count == g_capacity) {
        size_t capacity = g_capacity == 0 ? 1024 : g_capacity * 2;
        expected_entity_t *grown = (expected_entity_t *)realloc(
            g_entities, capacity * sizeof(*grown));

        if (grown == NULL) {
            return false;
        }
        g_entities = grown;
        g_capacity = capacity;
    }
    entry = &g_entities[g_count];
    memcpy(entry->name, name, name_length);
    entry->name_length = name_length;
    entry->utf8_length = 0;
    for (i = 0; i < count; i++) {
        encode_utf8(codepoints[i], entry->utf8, &entry->utf8_length);
    }
    g_count++;
    return true;
}

/*
 * Parses an entry line
 *   "&NAME": { "codepoints": [N, ...], "characters": "..." }[,]
 * and adds the entity if NAME ends in ';'. Sets *last when the line has no
 * ',' (the last entry). Returns false if the line has another shape.
 */
static bool parse_entry(const char *line, bool *last)
{
    const char *p = line;
    const char *name;
    size_t name_length = 0;
    bool semicolon = false;
    uint32_t codepoints[CODEPOINTS_MAX];
    size_t count = 0;

    if (!expect_text(&p, "\"&")) {
        return false;
    }
    name = p;
    while (is_name_char(name[name_length])) {
        name_length++;
    }
    p = name + name_length;
    if (*p == ';') {
        semicolon = true;
        p++;
    }
    if (name_length == 0 || name_length >= NAME_CAPACITY ||
        !expect_text(&p, "\":") || !expect_text(&p, "{") ||
        !expect_text(&p, "\"codepoints\":") || !expect_text(&p, "[")) {
        return false;
    }
    for (;;) {
        if (count == CODEPOINTS_MAX ||
            !parse_codepoint(&p, &codepoints[count])) {
            return false;
        }
        count++;
        if (expect_text(&p, "]")) {
            break;
        }
        if (!expect_text(&p, ",")) {
            return false;
        }
    }
    if (!expect_text(&p, ",") || !expect_text(&p, "\"characters\":") ||
        !expect_text(&p, "\"")) {
        return false;
    }
    /* The JSON string: skip its escapes ("\u00C6", "\"") up to the '"'. */
    while (*p != '"') {
        if (*p == '\0') {
            return false;
        }
        if (*p == '\\') {
            p++;
            if (*p == '\0') {
                return false;
            }
        }
        p++;
    }
    p++;
    if (!expect_text(&p, "}")) {
        return false;
    }
    *last = !expect_text(&p, ",");
    if (*skip_blanks(p) != '\0') {
        return false;
    }
    if (!semicolon) {
        g_other_count++;
        return true;
    }
    if (!add_entity(name, name_length, codepoints, count)) {
        snprintf(g_error, sizeof(g_error), "out of memory");
        return false;
    }
    if (name_length > g_longest) {
        g_longest = name_length;
    }
    return true;
}

/*
 * Reads the next line of file into line (without the line ending; a CR
 * before the LF is removed). Returns 1 for a line, 0 at the end of the file
 * and -1 (with g_error set) for a read error or a line too long.
 */
static int read_line(FILE *file, const char *name, unsigned long number,
                     char line[LINE_MAX_LENGTH])
{
    size_t length;

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
 * Reads entities.json: a line "{", one line per entry (each but the last
 * ending in ','), a line "}" and the end of the file.
 */
static bool read_entities(void)
{
    static const char name[] = "entities.json";
    char line[LINE_MAX_LENGTH];
    unsigned long number = 0;
    bool ok = true;
    bool closed = false;
    bool last = false;
    FILE *file = open_data_file(name);
    int status;

    if (file == NULL) {
        return false;
    }
    status = read_line(file, name, ++number, line);
    if (status == 1 && strcmp(line, "{") != 0) {
        ok = false;
    }
    while (ok && status == 1 &&
           (status = read_line(file, name, ++number, line)) == 1) {
        if (closed) {
            ok = false;  /* something after the closing "}" */
        } else if (last) {
            ok = strcmp(line, "}") == 0;
            closed = true;
        } else {
            ok = parse_entry(line, &last);
        }
    }
    fclose(file);
    if (status < 0) {
        return false;
    }
    if (!ok) {
        if (g_error[0] == '\0') {
            snprintf(g_error, sizeof(g_error), "%s:%lu: malformed line",
                     name, number);
        }
        return false;
    }
    if (!closed) {
        snprintf(g_error, sizeof(g_error), "%s: no closing \"}\" line", name);
        return false;
    }
    return true;
}

/* Sorts the entities and finds the longest. A name listed twice fails. */
static bool sort_entities(void)
{
    size_t i;

    if (g_count == 0) {
        snprintf(g_error, sizeof(g_error), "entities.json: no entity");
        return false;
    }
    qsort(g_entities, g_count, sizeof(*g_entities), compare_entities);
    for (i = 0; i < g_count; i++) {
        if (i > 0 &&
            compare_entities(&g_entities[i - 1], &g_entities[i]) == 0) {
            snprintf(g_error, sizeof(g_error),
                     "entities.json: \"&%.*s;\" is listed twice",
                     (int)g_entities[i].name_length, g_entities[i].name);
            return false;
        }
        if (g_entities[i].name_length == g_longest) {
            g_longest_index = i;
        }
    }
    return true;
}

NAOS_SETUP(data)
{
    g_loaded = false;
    g_error[0] = '\0';
    g_entities = NULL;
    g_count = 0;
    g_capacity = 0;
    g_other_count = 0;
    g_longest = 0;
    g_longest_index = 0;
    g_loaded = read_entities() && sort_entities();
}

NAOS_TEARDOWN(data)
{
    free(g_entities);
    g_entities = NULL;
    g_count = 0;
    g_capacity = 0;
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

/*
 * Looks up the name_length bytes at name from a heap copy of exactly that
 * size, so that the sanitizers catch a read past the name. The outputs
 * start as the sentinels. *copied is false (and nothing is looked up) when
 * out of memory.
 */
static bool lookup_copy(const char *name, size_t name_length,
                        const unsigned char **utf8, size_t *utf8_length,
                        bool *copied)
{
    unsigned char *copy = (unsigned char *)malloc(name_length + 1);
    bool found;

    *utf8 = g_sentinel_bytes;
    *utf8_length = SENTINEL_LENGTH;
    *copied = copy != NULL;
    if (copy == NULL) {
        return false;
    }
    /* Move the name to the end of the block; the extra byte is before it. */
    memcpy(copy + 1, name, name_length);
    found = amark_entity_lookup(copy + 1, name_length, utf8, utf8_length);
    free(copy);
    return found;
}

/*
 * Tries a name that must not be found and must leave both outputs
 * unchanged. Counts the names tried in *tried and the others in *wrong, keeping the
 * first of them in first (NUL-terminated, cut to fit).
 */
static void try_not_found(const char *name, size_t name_length,
                          size_t *tried, size_t *wrong,
                          char first[NAME_CAPACITY + 2])
{
    const unsigned char *utf8;
    size_t utf8_length;
    bool copied;
    bool found = lookup_copy(name, name_length, &utf8, &utf8_length, &copied);

    (*tried)++;
    if (!copied || found || utf8 != g_sentinel_bytes ||
        utf8_length != SENTINEL_LENGTH) {
        if (*wrong == 0) {
            size_t n = name_length < NAME_CAPACITY + 1 ? name_length
                                                       : NAME_CAPACITY + 1;
            memcpy(first, name, n);
            first[n] = '\0';
        }
        (*wrong)++;
    }
}

/* Checks that the name is not found and the outputs stay unchanged. */
static void check_not_found(const char *name, size_t name_length)
{
    const unsigned char *utf8;
    size_t utf8_length;
    bool copied;
    bool found = lookup_copy(name, name_length, &utf8, &utf8_length, &copied);
    int shown = (int)(name_length < 40 ? name_length : 40);

    NAOS_REQUIRE_TRUE(copied);
    NAOS_NOTE("\"%.*s\" (length %lu)", shown, name,
              (unsigned long)name_length);
    NAOS_CHECK_FALSE(found);
    NAOS_NOTE("\"%.*s\" (length %lu)", shown, name,
              (unsigned long)name_length);
    NAOS_CHECK_PTR_EQ(utf8, g_sentinel_bytes);
    NAOS_NOTE("\"%.*s\" (length %lu)", shown, name,
              (unsigned long)name_length);
    NAOS_CHECK_UINT_EQ(utf8_length, SENTINEL_LENGTH);
}

/* Checks that the NUL-terminated name is found with exactly the bytes. */
static void check_found(const char *name, const char *bytes)
{
    const unsigned char *utf8;
    size_t utf8_length;
    bool copied;
    size_t bytes_length = strlen(bytes);
    bool found = lookup_copy(name, strlen(name), &utf8, &utf8_length, &copied);

    NAOS_REQUIRE_TRUE(copied);
    NAOS_NOTE("%s", name);
    NAOS_REQUIRE_TRUE(found);
    NAOS_NOTE("%s", name);
    NAOS_REQUIRE_UINT_EQ(utf8_length, bytes_length);
    NAOS_NOTE("%s", name);
    NAOS_CHECK_MEM_EQ(utf8, utf8_length, bytes, bytes_length);
}

/* The input file was read; the names that end in ';' number 2,125. */
NAOS_TEST(input_data_is_read)
{
    require_data();
    NAOS_CHECK_UINT_EQ(g_count, EXPECTED_COUNT);
    NAOS_CHECK_UINT_GT(g_other_count, 0);
    /* The long names of the tests below are longer than every entity. */
    NAOS_CHECK_UINT_LT(g_longest, 32);
}

/* 7.1, 7.4, 7.7: every name that ends in ';' gives its code points. */
NAOS_TEST(every_entity_is_found)
{
    size_t missing = 0;
    size_t mismatches = 0;
    size_t first_missing = 0;
    size_t first_mismatch = 0;
    size_t i;

    require_data();
    for (i = 0; i < g_count; i++) {
        const expected_entity_t *entry = &g_entities[i];
        const unsigned char *utf8;
        size_t utf8_length;
        bool copied;
        bool found = lookup_copy(entry->name, entry->name_length, &utf8,
                                 &utf8_length, &copied);

        NAOS_REQUIRE_TRUE(copied);
        if (!found) {
            if (missing == 0) {
                first_missing = i;
            }
            missing++;
        } else if (utf8_length != entry->utf8_length ||
                   memcmp(utf8, entry->utf8, utf8_length) != 0) {
            if (mismatches == 0) {
                first_mismatch = i;
            }
            mismatches++;
        }
    }
    NAOS_NOTE("first not found: \"%.*s\"",
              (int)g_entities[first_missing].name_length,
              g_entities[first_missing].name);
    NAOS_CHECK_UINT_EQ(missing, 0);
    NAOS_NOTE("first with other bytes: \"%.*s\"",
              (int)g_entities[first_mismatch].name_length,
              g_entities[first_mismatch].name);
    NAOS_CHECK_UINT_EQ(mismatches, 0);
}

/*
 * 7.2, 7.3: for every name in the table, the name without its first byte,
 * the name with one more byte (a letter, a digit, ';' or '&') and the name
 * after '&' are not found, unless they are themselves in the table.
 */
NAOS_TEST(names_near_the_table_are_not_found)
{
    static const char extra[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789;&";
    char name[NAME_CAPACITY + 2];
    char first[NAME_CAPACITY + 2] = "";
    size_t tried = 0;
    size_t wrong = 0;
    size_t i;
    size_t k;

    require_data();
    for (i = 0; i < g_count; i++) {
        const expected_entity_t *entry = &g_entities[i];
        size_t length = entry->name_length;

        /* Without the first byte (the empty name is tested on its own). */
        if (length > 1 && !in_table(entry->name + 1, length - 1)) {
            try_not_found(entry->name + 1, length - 1, &tried, &wrong, first);
        }
        /* With one more byte at the end. */
        memcpy(name, entry->name, length);
        for (k = 0; k < sizeof(extra) - 1; k++) {
            name[length] = extra[k];
            if (!in_table(name, length + 1)) {
                try_not_found(name, length + 1, &tried, &wrong, first);
            }
        }
        /* With the '&' in front. */
        name[0] = '&';
        memcpy(name + 1, entry->name, length);
        try_not_found(name, length + 1, &tried, &wrong, first);
    }
    NAOS_CHECK_UINT_GT(tried, g_count);
    NAOS_NOTE("first found or changed the outputs: \"%s\"", first);
    NAOS_CHECK_UINT_EQ(wrong, 0);
}

/*
 * 7.5: a 32-byte and a 1,000-byte name that start with the longest entity
 * name (and the same lengths in bytes of a name in the table) are not found.
 */
NAOS_TEST(names_longer_than_the_longest_are_not_found)
{
    static const size_t lengths[] = {32, LONG_NAME_LENGTH};
    const expected_entity_t *longest;
    char name[LONG_NAME_LENGTH];
    size_t i;
    size_t j;

    require_data();
    longest = &g_entities[g_longest_index];
    for (i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
        memcpy(name, longest->name, longest->name_length);
        memset(name + longest->name_length, 'a',
               lengths[i] - longest->name_length);
        check_not_found(name, lengths[i]);

        /* The longest name over and over again. */
        for (j = 0; j < lengths[i]; j++) {
            name[j] = longest->name[j % longest->name_length];
        }
        check_not_found(name, lengths[i]);
    }
}

/* 7.6: the examples of the requirements. */
NAOS_TEST(fixed_examples)
{
    check_found("amp", "\x26");
    check_found("nbsp", "\xC2\xA0");
    check_found("NotEqualTilde", "\xE2\x89\x82\xCC\xB8");
}

/* 7.3: case matters. */
NAOS_TEST(case_matters)
{
    const unsigned char *upper = NULL;
    const unsigned char *lower = NULL;
    size_t upper_length = 0;
    size_t lower_length = 0;

    check_found("Auml", "\xC3\x84");
    check_found("auml", "\xC3\xA4");
    check_found("AMP", "\x26");
    check_not_found("Amp", 3);
    check_not_found("aMP", 3);

    NAOS_REQUIRE_TRUE(amark_entity_lookup((const unsigned char *)"Auml", 4,
                                          &upper, &upper_length));
    NAOS_REQUIRE_TRUE(amark_entity_lookup((const unsigned char *)"auml", 4,
                                          &lower, &lower_length));
    NAOS_CHECK_FALSE(upper_length == lower_length &&
                     memcmp(upper, lower, upper_length) == 0);
}

/* 7.4: the name is given by its start and length, with no NUL after it. */
NAOS_TEST(name_is_taken_by_length)
{
    static const unsigned char ampx[4] = {'a', 'm', 'p', 'x'};
    const unsigned char *utf8 = g_sentinel_bytes;
    size_t utf8_length = SENTINEL_LENGTH;

    NAOS_REQUIRE_TRUE(amark_entity_lookup(ampx, 3, &utf8, &utf8_length));
    NAOS_CHECK_MEM_EQ(utf8, utf8_length, "\x26", 1);

    utf8 = g_sentinel_bytes;
    utf8_length = SENTINEL_LENGTH;
    NAOS_CHECK_FALSE(amark_entity_lookup(ampx, 4, &utf8, &utf8_length));
    NAOS_CHECK_PTR_EQ(utf8, g_sentinel_bytes);
    NAOS_CHECK_UINT_EQ(utf8_length, SENTINEL_LENGTH);

    /* "nbsp" inside a longer text. */
    check_found("nbsp", "\xC2\xA0");
    utf8 = g_sentinel_bytes;
    utf8_length = SENTINEL_LENGTH;
    NAOS_REQUIRE_TRUE(amark_entity_lookup(
        (const unsigned char *)"x&nbsp;y" + 2, 4, &utf8, &utf8_length));
    NAOS_CHECK_MEM_EQ(utf8, utf8_length, "\xC2\xA0", 2);
}

/* 7.5: the empty name is not found, also where a name starts. */
NAOS_TEST(empty_name_is_not_found)
{
    const unsigned char *utf8 = g_sentinel_bytes;
    size_t utf8_length = SENTINEL_LENGTH;

    NAOS_CHECK_FALSE(amark_entity_lookup((const unsigned char *)"amp", 0,
                                         &utf8, &utf8_length));
    NAOS_CHECK_PTR_EQ(utf8, g_sentinel_bytes);
    NAOS_CHECK_UINT_EQ(utf8_length, SENTINEL_LENGTH);
    check_not_found("", 0);
}

/* 7.2, 7.5: names with ';' or '&', and names longer than any entity. */
NAOS_TEST(other_names_are_not_found)
{
    /* A name and its length in bytes (one of them holds a NUL). */
#define NAME_(text) {text, sizeof(text) - 1}
    static const struct {
        const char *text;
        size_t length;
    } names[] = {
        NAME_("amp;"), NAME_("&amp"), NAME_("&amp;"), NAME_(";"), NAME_("&"),
        NAME_(";;"), NAME_("&&"), NAME_("am;p"), NAME_("a&mp"),
        NAME_("nbsp;"), NAME_("&nbsp"), NAME_("NotEqualTilde;"),
        NAME_("Amp"), NAME_("AMp"), NAME_("nbs"), NAME_("bsp"),
        NAME_("ampx"), NAME_("x"), NAME_("\xC3\xA4"), NAME_("a\0mp"),
    };
#undef NAME_
    char name[LONG_NAME_LENGTH];
    size_t i;

    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        check_not_found(names[i].text, names[i].length);
    }
    memset(name, 'a', sizeof(name));
    check_not_found(name, 32);
    check_not_found(name, LONG_NAME_LENGTH);
    /* The longest entity name (31 bytes) with one more byte. */
    check_not_found("CounterClockwiseContourIntegrals", 32);
}

NAOS_SUITE(entity,
    NAOS_SET_SETUP(data),
    NAOS_SET_TEARDOWN(data),
    NAOS_ADD_TEST(input_data_is_read),
    NAOS_ADD_TEST(every_entity_is_found),
    NAOS_ADD_TEST(names_near_the_table_are_not_found),
    NAOS_ADD_TEST(names_longer_than_the_longest_are_not_found)
)

NAOS_SUITE(examples,
    NAOS_ADD_TEST(fixed_examples),
    NAOS_ADD_TEST(case_matters),
    NAOS_ADD_TEST(name_is_taken_by_length),
    NAOS_ADD_TEST(empty_name_is_not_found),
    NAOS_ADD_TEST(other_names_are_not_found)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(entity),
    NAOS_ADD_SUITE(examples)
)
