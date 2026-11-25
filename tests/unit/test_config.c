/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of the parse configuration (src/config_internal.h) and the error
 * type, and of the dialect definition check as rule 5 of the resolution
 * (dialect-extension Requirements 3.1, 3.10, 3.11, 3.12, 15.5).
 */

#include "config_internal.h"
#include "test_alloc.h"
#include "test_dialect_defs.h"
#include "tree_dump.h"

#include <amark/parse.h>

#include <naos/naos.h>

#include <limits.h>
#include <stdint.h>
#include <string.h>

/*
 * Fixture. The configuration never allocates, so teardown checks that the
 * counting allocator given to a configuration was never called.
 */
static amark_test_alloc_t g_state;
static amark_allocator_t g_allocator;

NAOS_SETUP(counting)
{
    amark_test_alloc_init(&g_state);
    g_allocator = amark_test_allocator(&g_state);
}

NAOS_TEARDOWN(counting)
{
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 0);
    NAOS_CHECK_UINT_EQ(g_state.free_calls, 0);
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
}

/* Fills *out with a byte pattern that no resolution produces. */
static void poison(amark_resolved_config_t *out)
{
    memset(out, 0xA5, sizeof(*out));
}

/*
 * Returns 1 if resolving config for content_length fails with
 * AMARK_ERROR_INVALID_CONFIG, leaves the output and the configuration byte
 * for byte unchanged, and amark_config_validate agrees.
 */
static int rejected(const amark_config_t *config, size_t content_length)
{
    amark_resolved_config_t out;
    amark_resolved_config_t before;
    amark_config_t config_before;
    amark_error_t err;

    poison(&out);
    memcpy(&before, &out, sizeof(out));
    memcpy(&config_before, config, sizeof(*config));
    err = amark_config_resolve(config, content_length, &out);
    return err == AMARK_ERROR_INVALID_CONFIG &&
           memcmp(&before, &out, sizeof(out)) == 0 &&
           memcmp(&config_before, config, sizeof(*config)) == 0 &&
           amark_config_validate(config, content_length) ==
               AMARK_ERROR_INVALID_CONFIG;
}

/* Returns 1 if out holds the defaults resolved for content_length. */
static int is_default_resolution(const amark_resolved_config_t *out,
                                 size_t content_length)
{
    amark_allocator_t def = amark_allocator_default();

    return out->dialect == NULL && out->start == 0 &&
           out->end == content_length &&
           out->allocator.malloc_fn == def.malloc_fn &&
           out->allocator.free_fn == def.free_fn &&
           out->allocator.ctx == NULL && out->filename == NULL &&
           out->utf8_mode == AMARK_UTF8_STRICT && out->max_input_size == 0 &&
           out->max_nesting_depth == 0 && out->max_node_count == 0;
}

/* ---- the error type (10.1, 10.3) ---- */

NAOS_TEST(error_values_are_distinct_and_fixed)
{
    const int values[7] = {AMARK_ERROR_NONE,
                           AMARK_ERROR_INVALID_INPUT,
                           AMARK_ERROR_INVALID_CONFIG,
                           AMARK_ERROR_LIMIT_EXCEEDED,
                           AMARK_ERROR_NOMEM,
                           AMARK_ERROR_NOT_AVAILABLE,
                           AMARK_ERROR_BUFFER_TOO_SMALL};
    int i;
    int j;

    for (i = 0; i < 7; i++) {
        NAOS_CHECK_INT_EQ(values[i], i);
        for (j = i + 1; j < 7; j++) {
            NAOS_CHECK_INT_NE(values[i], values[j]);
        }
    }
}

/* ---- init (8.3, 8.4) ---- */

NAOS_TEST(init_writes_the_defaults)
{
    amark_config_t config;

    memset(&config, 0x5A, sizeof(config));
    amark_config_init(&config);
    NAOS_CHECK_UINT_EQ(config.struct_size, sizeof(amark_config_t));
    NAOS_CHECK_NULL(config.dialect);
    NAOS_CHECK_UINT_EQ(config.start, 0);
    NAOS_CHECK_UINT_EQ(config.end, AMARK_END_OF_CONTENT);
    NAOS_CHECK_TRUE(config.allocator.malloc_fn == NULL);
    NAOS_CHECK_TRUE(config.allocator.free_fn == NULL);
    NAOS_CHECK_NULL(config.allocator.ctx);
    NAOS_CHECK_NULL(config.filename);
    NAOS_CHECK_INT_EQ(config.utf8_mode, AMARK_UTF8_STRICT);
    NAOS_CHECK_UINT_EQ(config.max_input_size, 0);
    NAOS_CHECK_UINT_EQ(config.max_nesting_depth, 0);
    NAOS_CHECK_UINT_EQ(config.max_node_count, 0);
}

NAOS_TEST(init_null_does_nothing)
{
    amark_config_init(NULL);
    NAOS_CHECK_INT_EQ(amark_config_validate(NULL, 0), AMARK_ERROR_NONE);
}

NAOS_TEST(end_of_content_is_size_max)
{
    NAOS_CHECK_UINT_EQ(AMARK_END_OF_CONTENT, SIZE_MAX);
}

/* ---- resolving the defaults (8.3, 8.4, 9.2) ---- */

NAOS_TEST(default_resolves_for_empty_content)
{
    amark_config_t config;
    amark_resolved_config_t out;

    amark_config_init(&config);
    poison(&out);
    NAOS_REQUIRE_INT_EQ(amark_config_resolve(&config, 0, &out),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_TRUE(is_default_resolution(&out, 0));
}

NAOS_TEST(default_resolves_for_some_content)
{
    amark_config_t config;
    amark_resolved_config_t out;

    amark_config_init(&config);
    poison(&out);
    NAOS_REQUIRE_INT_EQ(amark_config_resolve(&config, 12, &out),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_TRUE(is_default_resolution(&out, 12));
}

NAOS_TEST(default_resolves_for_the_largest_length)
{
    amark_config_t config;
    amark_resolved_config_t out;

    amark_config_init(&config);
    poison(&out);
    NAOS_REQUIRE_INT_EQ(amark_config_resolve(&config, SIZE_MAX, &out),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_TRUE(is_default_resolution(&out, SIZE_MAX));
}

NAOS_TEST(default_validates_for_any_length)
{
    amark_config_t config;

    amark_config_init(&config);
    NAOS_CHECK_INT_EQ(amark_config_validate(&config, 0), AMARK_ERROR_NONE);
    NAOS_CHECK_INT_EQ(amark_config_validate(&config, 1), AMARK_ERROR_NONE);
    NAOS_CHECK_INT_EQ(amark_config_validate(&config, SIZE_MAX - 1),
                      AMARK_ERROR_NONE);
    NAOS_CHECK_INT_EQ(amark_config_validate(&config, SIZE_MAX),
                      AMARK_ERROR_NONE);
}

/* ---- a NULL configuration is the defaults ---- */

NAOS_TEST(null_config_resolves_as_the_defaults)
{
    const size_t lengths[3] = {0, 7, SIZE_MAX};
    amark_resolved_config_t out;
    size_t i;

    for (i = 0; i < 3; i++) {
        poison(&out);
        NAOS_REQUIRE_INT_EQ(amark_config_resolve(NULL, lengths[i], &out),
                            AMARK_ERROR_NONE);
        NAOS_CHECK_TRUE(is_default_resolution(&out, lengths[i]));
        NAOS_CHECK_INT_EQ(amark_config_validate(NULL, lengths[i]),
                          AMARK_ERROR_NONE);
    }
}

/* ---- rule 1: struct_size ---- */

NAOS_TEST(rule1_wrong_struct_size_is_rejected)
{
    amark_config_t config;

    amark_config_init(&config);
    config.struct_size = 0;
    NAOS_CHECK_TRUE(rejected(&config, 10));
    config.struct_size = sizeof(amark_config_t) - 1;
    NAOS_CHECK_TRUE(rejected(&config, 10));
    config.struct_size = sizeof(amark_config_t) + 1;
    NAOS_CHECK_TRUE(rejected(&config, 10));
    config.struct_size = SIZE_MAX;
    NAOS_CHECK_TRUE(rejected(&config, 10));
}

NAOS_TEST(zeroed_config_is_rejected)
{
    amark_config_t config;

    memset(&config, 0, sizeof(config));
    NAOS_CHECK_TRUE(rejected(&config, 0));
}

/* ---- rule 2: the allocator pair (9.3) ---- */

NAOS_TEST(rule2_only_malloc_fn_is_rejected)
{
    amark_config_t config;

    amark_config_init(&config);
    config.allocator.malloc_fn = g_allocator.malloc_fn;
    config.allocator.ctx = &g_state;
    NAOS_CHECK_TRUE(rejected(&config, 10));
}

NAOS_TEST(rule2_only_free_fn_is_rejected)
{
    amark_config_t config;

    amark_config_init(&config);
    config.allocator.free_fn = g_allocator.free_fn;
    config.allocator.ctx = &g_state;
    NAOS_CHECK_TRUE(rejected(&config, 10));
}

/* ---- rule 3: the range (9.4, 9.5, 9.6) ---- */

NAOS_TEST(rule3_start_after_end_is_rejected)
{
    amark_config_t config;

    amark_config_init(&config);
    config.start = 5;
    config.end = 4;
    NAOS_CHECK_TRUE(rejected(&config, 10));
    config.start = 1;
    config.end = 0;
    NAOS_CHECK_TRUE(rejected(&config, 10));
}

NAOS_TEST(rule3_end_after_length_is_rejected)
{
    amark_config_t config;

    amark_config_init(&config);
    config.end = 11;
    NAOS_CHECK_TRUE(rejected(&config, 10));
    config.start = 0;
    config.end = 1;
    NAOS_CHECK_TRUE(rejected(&config, 0));
    config.end = SIZE_MAX - 1;
    NAOS_CHECK_TRUE(rejected(&config, SIZE_MAX - 2));
}

NAOS_TEST(rule3_start_after_length_to_the_end_is_rejected)
{
    amark_config_t config;

    amark_config_init(&config);
    config.start = 11;
    NAOS_CHECK_TRUE(rejected(&config, 10));
    config.start = 1;
    NAOS_CHECK_TRUE(rejected(&config, 0));
}

NAOS_TEST(rule3_boundaries_are_valid)
{
    amark_config_t config;
    amark_resolved_config_t out;

    amark_config_init(&config);

    /* An empty range in the middle. */
    config.start = 4;
    config.end = 4;
    poison(&out);
    NAOS_REQUIRE_INT_EQ(amark_config_resolve(&config, 10, &out),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(out.start, 4);
    NAOS_CHECK_UINT_EQ(out.end, 4);

    /* end equal to the length. */
    config.start = 3;
    config.end = 10;
    poison(&out);
    NAOS_REQUIRE_INT_EQ(amark_config_resolve(&config, 10, &out),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(out.start, 3);
    NAOS_CHECK_UINT_EQ(out.end, 10);

    /* start equal to the length, to the end of the content. */
    config.start = 10;
    config.end = AMARK_END_OF_CONTENT;
    poison(&out);
    NAOS_REQUIRE_INT_EQ(amark_config_resolve(&config, 10, &out),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(out.start, 10);
    NAOS_CHECK_UINT_EQ(out.end, 10);

    /* The empty range of empty content. */
    config.start = 0;
    config.end = 0;
    poison(&out);
    NAOS_REQUIRE_INT_EQ(amark_config_resolve(&config, 0, &out),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(out.start, 0);
    NAOS_CHECK_UINT_EQ(out.end, 0);

    /* A start partway, to the end of the content. */
    config.start = 2;
    config.end = AMARK_END_OF_CONTENT;
    poison(&out);
    NAOS_REQUIRE_INT_EQ(amark_config_resolve(&config, 10, &out),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(out.start, 2);
    NAOS_CHECK_UINT_EQ(out.end, 10);
}

/* ---- rule 4: utf8_mode (8.5, 9.7) ---- */

NAOS_TEST(rule4_undefined_utf8_mode_is_rejected)
{
    amark_config_t config;

    amark_config_init(&config);
    config.utf8_mode = 2;
    NAOS_CHECK_TRUE(rejected(&config, 10));
    config.utf8_mode = -1;
    NAOS_CHECK_TRUE(rejected(&config, 10));
    config.utf8_mode = INT_MAX;
    NAOS_CHECK_TRUE(rejected(&config, 10));
    config.utf8_mode = INT_MIN;
    NAOS_CHECK_TRUE(rejected(&config, 10));
}

NAOS_TEST(rule4_both_utf8_modes_are_valid)
{
    amark_config_t config;
    amark_resolved_config_t out;

    amark_config_init(&config);
    config.utf8_mode = AMARK_UTF8_REPLACE;
    poison(&out);
    NAOS_REQUIRE_INT_EQ(amark_config_resolve(&config, 10, &out),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_INT_EQ(out.utf8_mode, AMARK_UTF8_REPLACE);

    config.utf8_mode = AMARK_UTF8_STRICT;
    poison(&out);
    NAOS_REQUIRE_INT_EQ(amark_config_resolve(&config, 10, &out),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_INT_EQ(out.utf8_mode, AMARK_UTF8_STRICT);
}

/* A configuration breaking every rule at once is still only rejected. */
NAOS_TEST(every_rule_broken_is_rejected)
{
    amark_config_t config;

    amark_config_init(&config);
    config.struct_size = 1;
    config.allocator.malloc_fn = g_allocator.malloc_fn;
    config.start = 9;
    config.end = 8;
    config.utf8_mode = 7;
    NAOS_CHECK_TRUE(rejected(&config, 5));
}

/* ---- values passed through unchanged (8.6, 8.7, 8.8, 8.9, 9.8) ---- */

NAOS_TEST(limits_pass_through_and_are_not_compared)
{
    amark_config_t config;
    amark_resolved_config_t out;

    amark_config_init(&config);
    config.max_input_size = 1;
    config.max_nesting_depth = 2;
    config.max_node_count = 3;
    poison(&out);
    NAOS_REQUIRE_INT_EQ(amark_config_resolve(&config, 1000, &out),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(out.max_input_size, 1);
    NAOS_CHECK_UINT_EQ(out.max_nesting_depth, 2);
    NAOS_CHECK_UINT_EQ(out.max_node_count, 3);
    NAOS_CHECK_UINT_EQ(out.end, 1000);

    config.max_input_size = SIZE_MAX;
    config.max_nesting_depth = SIZE_MAX;
    config.max_node_count = SIZE_MAX;
    poison(&out);
    NAOS_REQUIRE_INT_EQ(amark_config_resolve(&config, 0, &out),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(out.max_input_size, SIZE_MAX);
    NAOS_CHECK_UINT_EQ(out.max_nesting_depth, SIZE_MAX);
    NAOS_CHECK_UINT_EQ(out.max_node_count, SIZE_MAX);
}

NAOS_TEST(allocator_filename_and_dialect_pass_through)
{
    amark_config_t config;
    amark_resolved_config_t out;
    const char *filename = "doc.md";
    amark_test_def_t def;

    amark_test_def_init(&def);
    amark_config_init(&config);
    config.allocator = g_allocator;
    config.filename = filename;
    config.dialect = &def.dialect;
    poison(&out);
    NAOS_REQUIRE_INT_EQ(amark_config_resolve(&config, 6, &out),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_TRUE(out.allocator.malloc_fn == g_allocator.malloc_fn);
    NAOS_CHECK_TRUE(out.allocator.free_fn == g_allocator.free_fn);
    NAOS_CHECK_PTR_EQ(out.allocator.ctx, &g_state);
    NAOS_CHECK_PTR_EQ(out.filename, filename);
    NAOS_CHECK_TRUE(out.dialect == &def.dialect);
    NAOS_CHECK_UINT_EQ(out.start, 0);
    NAOS_CHECK_UINT_EQ(out.end, 6);
}

NAOS_TEST(ctx_without_functions_is_ignored)
{
    amark_config_t config;
    amark_resolved_config_t out;

    amark_config_init(&config);
    config.allocator.ctx = &g_state;
    poison(&out);
    NAOS_REQUIRE_INT_EQ(amark_config_resolve(&config, 3, &out),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_TRUE(is_default_resolution(&out, 3));
}

/* ---- validate (9.1, 9.9) ---- */

NAOS_TEST(validate_changes_nothing_and_repeats)
{
    amark_config_t config;
    amark_config_t before;
    int i;

    amark_config_init(&config);
    config.allocator = g_allocator;
    config.start = 2;
    config.end = 5;
    memcpy(&before, &config, sizeof(config));
    for (i = 0; i < 3; i++) {
        NAOS_CHECK_INT_EQ(amark_config_validate(&config, 5), AMARK_ERROR_NONE);
        NAOS_CHECK_INT_EQ(amark_config_validate(&config, 4),
                          AMARK_ERROR_INVALID_CONFIG);
    }
    NAOS_CHECK_INT_EQ(memcmp(&before, &config, sizeof(config)), 0);
}

/* ---- rule 5: the dialect definition (dialect-extension 3.1, 3.10 to 3.12,
   15.5) ---- */

/*
 * Fixture of the dialect tests: the counting allocator. A parse may
 * allocate here, so teardown checks only that nothing is left.
 */
NAOS_SETUP(dialect)
{
    amark_test_alloc_init(&g_state);
    g_allocator = amark_test_allocator(&g_state);
}

NAOS_TEARDOWN(dialect)
{
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
    NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
}

/* The content parsed with each dialect. */
static const char g_content[] = "# Title\n\n> *quote*\n\n- item\n";
#define CONTENT_LENGTH (sizeof(g_content) - 1)
#define DUMP_SIZE 512

/* Any non-NULL value: the parse entry must overwrite it. */
static char g_not_a_document;
#define NOT_A_DOCUMENT ((amark_document_t *)(void *)&g_not_a_document)

/*
 * Returns 1 if the parse entry returns AMARK_ERROR_INVALID_CONFIG for
 * config, sets *out to NULL and allocates nothing.
 */
static int parse_rejected(const amark_config_t *config)
{
    amark_document_t *doc = NOT_A_DOCUMENT;
    size_t calls = g_state.malloc_calls;
    amark_error_t err;

    err = amark_parse(g_content, CONTENT_LENGTH, config, &doc);
    return err == AMARK_ERROR_INVALID_CONFIG && doc == NULL &&
           g_state.malloc_calls == calls;
}

/*
 * Parses g_content with config into buffer (a tree dump), requiring
 * success, and destroys the document.
 */
static void parse_dump(const amark_config_t *config, char *buffer)
{
    amark_document_t *doc = NOT_A_DOCUMENT;

    NAOS_REQUIRE_INT_EQ(amark_parse(g_content, CONTENT_LENGTH, config, &doc),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_NOT_NULL(doc);
    NAOS_CHECK_UINT_LT(amark_test_dump_tree(doc, buffer, DUMP_SIZE),
                       DUMP_SIZE);
    amark_document_destroy(doc);
}

NAOS_TEST(invalid_dialect_definitions_are_config_errors)
{
    size_t i;

    NAOS_REQUIRE_UINT_GT(amark_test_def_breaker_count, 0);
    for (i = 0; i < amark_test_def_breaker_count; i++) {
        const amark_test_def_breaker_t *b = &amark_test_def_breakers[i];
        amark_test_def_t def;
        amark_config_t config;

        amark_test_def_init(&def);
        b->apply(&def, b->arg);
        amark_config_init(&config);
        config.allocator = g_allocator;
        config.dialect = &def.dialect;
        /* The breaker's name is the message of a failed check. */
        (void)naos_check(rejected(&config, CONTENT_LENGTH), b->name,
                         __FILE__, __LINE__);
        (void)naos_check(parse_rejected(&config), b->name, __FILE__,
                         __LINE__);
        /* The default allocator: the same error. */
        config.allocator = amark_allocator_default();
        (void)naos_check(parse_rejected(&config), b->name, __FILE__,
                         __LINE__);
    }
}

NAOS_TEST(valid_dialect_resolves_and_parses)
{
    amark_test_def_t def;
    amark_config_t config;
    amark_resolved_config_t out;
    char want[DUMP_SIZE];
    char got[DUMP_SIZE];

    amark_test_def_init(&def);
    amark_config_init(&config);
    config.allocator = g_allocator;
    parse_dump(&config, want);

    config.dialect = &def.dialect;
    poison(&out);
    NAOS_REQUIRE_INT_EQ(amark_config_resolve(&config, CONTENT_LENGTH, &out),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_TRUE(out.dialect == &def.dialect);
    NAOS_CHECK_INT_EQ(amark_config_validate(&config, CONTENT_LENGTH),
                      AMARK_ERROR_NONE);
    parse_dump(&config, got);
    NAOS_CHECK_STR_EQ(got, want);

    /* The three extensions of the most delimiter kinds. */
    amark_test_def_set_delimiter_total(&def, AMARK_DIALECT_DELIMITER_MAX);
    def.ext3.delimiter_count = 0;
    def.dialect.extension_count = 3;
    NAOS_CHECK_INT_EQ(amark_config_validate(&config, CONTENT_LENGTH),
                      AMARK_ERROR_NONE);
}

NAOS_TEST(zero_extension_dialects_resolve_to_no_dialect)
{
    amark_test_def_t def;
    amark_dialect_t empty;
    const amark_dialect_t *dialects[2];
    amark_config_t config;
    amark_resolved_config_t out;
    char want[DUMP_SIZE];
    char got[DUMP_SIZE];
    size_t i;

    /* With no array, and with an array but a count of 0. */
    memset(&empty, 0, sizeof(empty));
    empty.struct_size = sizeof(amark_dialect_t);
    empty.abi = AMARK_DIALECT_ABI;
    amark_test_def_init(&def);
    def.dialect.extension_count = 0;
    dialects[0] = &empty;
    dialects[1] = &def.dialect;

    amark_config_init(&config);
    config.allocator = g_allocator;
    parse_dump(&config, want);
    for (i = 0; i < 2; i++) {
        config.dialect = dialects[i];
        poison(&out);
        NAOS_REQUIRE_INT_EQ(
            amark_config_resolve(&config, CONTENT_LENGTH, &out),
            AMARK_ERROR_NONE);
        NAOS_CHECK_NULL(out.dialect);
        NAOS_CHECK_TRUE(out.allocator.malloc_fn == g_allocator.malloc_fn);
        NAOS_CHECK_UINT_EQ(out.end, CONTENT_LENGTH);
        NAOS_CHECK_INT_EQ(amark_config_validate(&config, CONTENT_LENGTH),
                          AMARK_ERROR_NONE);
        parse_dump(&config, got);
        NAOS_CHECK_STR_EQ(got, want);
    }

    /* The defaults with a 0-extension dialect are the default resolution. */
    amark_config_init(&config);
    config.dialect = &empty;
    poison(&out);
    NAOS_REQUIRE_INT_EQ(amark_config_resolve(&config, CONTENT_LENGTH, &out),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_TRUE(is_default_resolution(&out, CONTENT_LENGTH));
}

NAOS_TEST(zero_extension_dialect_is_still_checked)
{
    amark_dialect_t empty;
    amark_config_t config;

    memset(&empty, 0, sizeof(empty));
    empty.struct_size = sizeof(amark_dialect_t);
    empty.abi = AMARK_DIALECT_ABI + 1;
    amark_config_init(&config);
    config.allocator = g_allocator;
    config.dialect = &empty;
    NAOS_CHECK_TRUE(rejected(&config, CONTENT_LENGTH));
    NAOS_CHECK_TRUE(parse_rejected(&config));

    empty.abi = AMARK_DIALECT_ABI;
    empty.struct_size = 0;
    NAOS_CHECK_TRUE(rejected(&config, CONTENT_LENGTH));
    NAOS_CHECK_TRUE(parse_rejected(&config));
}

/* Rule 5 comes after rules 1 to 4: a valid dialect does not hide them. */
NAOS_TEST(valid_dialect_does_not_hide_other_rules)
{
    amark_test_def_t def;
    amark_config_t config;

    amark_test_def_init(&def);
    amark_config_init(&config);
    config.dialect = &def.dialect;
    config.start = CONTENT_LENGTH + 1;
    NAOS_CHECK_TRUE(rejected(&config, CONTENT_LENGTH));
    NAOS_CHECK_TRUE(parse_rejected(&config));
}

/* The check reads the definition only: no rule is called, nothing changes. */
NAOS_TEST(dialect_check_changes_nothing_and_repeats)
{
    amark_test_def_t def;
    amark_test_def_t before;
    amark_config_t config;
    int i;

    amark_test_def_init(&def);
    amark_config_init(&config);
    config.allocator = g_allocator;
    config.dialect = &def.dialect;
    memcpy(&before, &def, sizeof(def));
    for (i = 0; i < 3; i++) {
        NAOS_CHECK_INT_EQ(amark_config_validate(&config, CONTENT_LENGTH),
                          AMARK_ERROR_NONE);
    }
    NAOS_CHECK_INT_EQ(memcmp(&before, &def, sizeof(def)), 0);
    def.ext1.name = "";
    memcpy(&before, &def, sizeof(def));
    for (i = 0; i < 3; i++) {
        NAOS_CHECK_INT_EQ(amark_config_validate(&config, CONTENT_LENGTH),
                          AMARK_ERROR_INVALID_CONFIG);
    }
    NAOS_CHECK_INT_EQ(memcmp(&before, &def, sizeof(def)), 0);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 0);
}

NAOS_SUITE(error,
    NAOS_ADD_TEST(error_values_are_distinct_and_fixed)
)

NAOS_SUITE(config,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(init_writes_the_defaults),
    NAOS_ADD_TEST(init_null_does_nothing),
    NAOS_ADD_TEST(end_of_content_is_size_max),
    NAOS_ADD_TEST(default_resolves_for_empty_content),
    NAOS_ADD_TEST(default_resolves_for_some_content),
    NAOS_ADD_TEST(default_resolves_for_the_largest_length),
    NAOS_ADD_TEST(default_validates_for_any_length),
    NAOS_ADD_TEST(null_config_resolves_as_the_defaults),
    NAOS_ADD_TEST(rule1_wrong_struct_size_is_rejected),
    NAOS_ADD_TEST(zeroed_config_is_rejected),
    NAOS_ADD_TEST(rule2_only_malloc_fn_is_rejected),
    NAOS_ADD_TEST(rule2_only_free_fn_is_rejected),
    NAOS_ADD_TEST(rule3_start_after_end_is_rejected),
    NAOS_ADD_TEST(rule3_end_after_length_is_rejected),
    NAOS_ADD_TEST(rule3_start_after_length_to_the_end_is_rejected),
    NAOS_ADD_TEST(rule3_boundaries_are_valid),
    NAOS_ADD_TEST(rule4_undefined_utf8_mode_is_rejected),
    NAOS_ADD_TEST(rule4_both_utf8_modes_are_valid),
    NAOS_ADD_TEST(every_rule_broken_is_rejected),
    NAOS_ADD_TEST(limits_pass_through_and_are_not_compared),
    NAOS_ADD_TEST(allocator_filename_and_dialect_pass_through),
    NAOS_ADD_TEST(ctx_without_functions_is_ignored),
    NAOS_ADD_TEST(validate_changes_nothing_and_repeats)
)

NAOS_SUITE(dialect,
    NAOS_SET_SETUP(dialect),
    NAOS_SET_TEARDOWN(dialect),
    NAOS_ADD_TEST(invalid_dialect_definitions_are_config_errors),
    NAOS_ADD_TEST(valid_dialect_resolves_and_parses),
    NAOS_ADD_TEST(zero_extension_dialects_resolve_to_no_dialect),
    NAOS_ADD_TEST(zero_extension_dialect_is_still_checked),
    NAOS_ADD_TEST(valid_dialect_does_not_hide_other_rules),
    NAOS_ADD_TEST(dialect_check_changes_nothing_and_repeats)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(error),
    NAOS_ADD_SUITE(config),
    NAOS_ADD_SUITE(dialect)
)
