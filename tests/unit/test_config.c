/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/* Unit tests of the parse configuration (src/config.h) and the error type. */

#include "config.h"
#include "test_alloc.h"

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

/* Only its address is used, as an opaque dialect pointer. */
static const int g_dialect_object = 1;

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

static const amark_dialect_t *test_dialect(void)
{
    return (const amark_dialect_t *)(const void *)&g_dialect_object;
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
    const int values[5] = {AMARK_ERROR_NONE, AMARK_ERROR_INVALID_INPUT,
                           AMARK_ERROR_INVALID_CONFIG,
                           AMARK_ERROR_LIMIT_EXCEEDED, AMARK_ERROR_NOMEM};
    int i;
    int j;

    for (i = 0; i < 5; i++) {
        NAOS_CHECK_INT_EQ(values[i], i);
        for (j = i + 1; j < 5; j++) {
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

    amark_config_init(&config);
    config.allocator = g_allocator;
    config.filename = filename;
    config.dialect = test_dialect();
    poison(&out);
    NAOS_REQUIRE_INT_EQ(amark_config_resolve(&config, 6, &out),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_TRUE(out.allocator.malloc_fn == g_allocator.malloc_fn);
    NAOS_CHECK_TRUE(out.allocator.free_fn == g_allocator.free_fn);
    NAOS_CHECK_PTR_EQ(out.allocator.ctx, &g_state);
    NAOS_CHECK_PTR_EQ(out.filename, filename);
    NAOS_CHECK_TRUE(out.dialect == test_dialect());
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

NAOS_MAIN(
    NAOS_ADD_SUITE(error),
    NAOS_ADD_SUITE(config)
)
