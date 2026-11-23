/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Public API test of the parse configuration: links Amark::amark and uses
 * only <amark/amark.h>.
 */

#include <amark/amark.h>

#include <naos/naos.h>

#include <stddef.h>
#include <string.h>

/* Allocator functions used only as non-NULL pointers; never called. */
static void *unused_malloc(void *ctx, size_t size)
{
    (void)ctx;
    (void)size;
    return NULL;
}

static void unused_free(void *ctx, void *ptr)
{
    (void)ctx;
    (void)ptr;
}

NAOS_TEST(init_writes_defaults)
{
    amark_config_t config;

    memset(&config, 0xA5, sizeof(config));
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
    NAOS_CHECK_TRUE(1);
}

NAOS_TEST(defaults_are_valid)
{
    amark_config_t config;

    amark_config_init(&config);
    NAOS_CHECK_INT_EQ(amark_config_validate(&config, 0), AMARK_ERROR_NONE);
    NAOS_CHECK_INT_EQ(amark_config_validate(&config, 10), AMARK_ERROR_NONE);
    NAOS_CHECK_INT_EQ(amark_config_validate(NULL, 10), AMARK_ERROR_NONE);
}

NAOS_TEST(replace_mode_is_valid)
{
    amark_config_t config;

    amark_config_init(&config);
    config.utf8_mode = AMARK_UTF8_REPLACE;
    NAOS_CHECK_INT_EQ(amark_config_validate(&config, 10), AMARK_ERROR_NONE);
}

NAOS_TEST(zero_filled_is_invalid)
{
    amark_config_t config;

    memset(&config, 0, sizeof(config));
    NAOS_CHECK_INT_EQ(amark_config_validate(&config, 10),
                      AMARK_ERROR_INVALID_CONFIG);
}

NAOS_TEST(one_allocator_function_is_invalid)
{
    amark_config_t config;

    amark_config_init(&config);
    config.allocator.malloc_fn = unused_malloc;
    NAOS_CHECK_INT_EQ(amark_config_validate(&config, 10),
                      AMARK_ERROR_INVALID_CONFIG);

    amark_config_init(&config);
    config.allocator.free_fn = unused_free;
    NAOS_CHECK_INT_EQ(amark_config_validate(&config, 10),
                      AMARK_ERROR_INVALID_CONFIG);

    amark_config_init(&config);
    config.allocator.malloc_fn = unused_malloc;
    config.allocator.free_fn = unused_free;
    NAOS_CHECK_INT_EQ(amark_config_validate(&config, 10), AMARK_ERROR_NONE);
}

NAOS_TEST(reversed_range_is_invalid)
{
    amark_config_t config;

    amark_config_init(&config);
    config.start = 5;
    config.end = 4;
    NAOS_CHECK_INT_EQ(amark_config_validate(&config, 10),
                      AMARK_ERROR_INVALID_CONFIG);

    config.end = 5;
    NAOS_CHECK_INT_EQ(amark_config_validate(&config, 10), AMARK_ERROR_NONE);
}

NAOS_TEST(undefined_utf8_mode_is_invalid)
{
    amark_config_t config;

    amark_config_init(&config);
    config.utf8_mode = 2;
    NAOS_CHECK_INT_EQ(amark_config_validate(&config, 10),
                      AMARK_ERROR_INVALID_CONFIG);

    config.utf8_mode = -1;
    NAOS_CHECK_INT_EQ(amark_config_validate(&config, 10),
                      AMARK_ERROR_INVALID_CONFIG);
}

NAOS_SUITE(config,
    NAOS_ADD_TEST(init_writes_defaults),
    NAOS_ADD_TEST(init_null_does_nothing),
    NAOS_ADD_TEST(defaults_are_valid),
    NAOS_ADD_TEST(replace_mode_is_valid),
    NAOS_ADD_TEST(zero_filled_is_invalid),
    NAOS_ADD_TEST(one_allocator_function_is_invalid),
    NAOS_ADD_TEST(reversed_range_is_invalid),
    NAOS_ADD_TEST(undefined_utf8_mode_is_invalid)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(config)
)
