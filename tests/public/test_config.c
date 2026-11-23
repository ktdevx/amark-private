/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Public API test of the parse configuration and of the calls that take no
 * document: links Amark::amark and uses only <amark/amark.h>. The public
 * API cannot create a document yet, so the reading functions are checked
 * here with a NULL document only; their results on a real tree are checked
 * by the unit tests of the tree.
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

NAOS_TEST(null_document_destroy_does_nothing)
{
    amark_document_destroy(NULL);
    NAOS_CHECK_TRUE(1);
}

NAOS_TEST(null_document_has_no_root)
{
    NAOS_CHECK_UINT_EQ(amark_document_root(NULL), AMARK_NODE_NONE);
}

NAOS_TEST(null_document_has_no_diagnostics)
{
    amark_diagnostic_t diagnostic = {5, 2, AMARK_SEVERITY_WARNING, "kept",
                                     NULL};

    NAOS_CHECK_UINT_EQ(amark_document_diagnostic_count(NULL), 0);
    NAOS_CHECK_FALSE(amark_document_diagnostic(NULL, 0, &diagnostic));
    NAOS_CHECK_UINT_EQ(diagnostic.offset, 5);
    NAOS_CHECK_UINT_EQ(diagnostic.line, 2);
    NAOS_CHECK_STR_EQ(diagnostic.message, "kept");
}

NAOS_TEST(null_document_navigation_returns_none)
{
    NAOS_CHECK_UINT_EQ(amark_node_parent(NULL, 1), AMARK_NODE_NONE);
    NAOS_CHECK_UINT_EQ(amark_node_first_child(NULL, 1), AMARK_NODE_NONE);
    NAOS_CHECK_UINT_EQ(amark_node_next_sibling(NULL, 1), AMARK_NODE_NONE);
    NAOS_CHECK_UINT_EQ(amark_node_parent(NULL, AMARK_NODE_NONE),
                       AMARK_NODE_NONE);
    NAOS_CHECK_UINT_EQ(amark_node_first_child(NULL, AMARK_NODE_NONE),
                       AMARK_NODE_NONE);
    NAOS_CHECK_UINT_EQ(amark_node_next_sibling(NULL, AMARK_NODE_NONE),
                       AMARK_NODE_NONE);
    NAOS_CHECK_UINT_EQ(amark_node_kind(NULL, 1), AMARK_KIND_NONE);
}

/* The traversal of the comment in <amark/tree.h> visits nothing and ends. */
NAOS_TEST(null_document_traversal_ends)
{
    const amark_document_t *doc = NULL;
    size_t visits = 0;
    amark_node_id_t root = amark_document_root(doc);
    amark_node_id_t node = root;

    while (node != AMARK_NODE_NONE) {
        amark_node_id_t next = amark_node_first_child(doc, node);

        visits++;
        while (next == AMARK_NODE_NONE && node != root) {
            next = amark_node_next_sibling(doc, node);
            if (next == AMARK_NODE_NONE) {
                node = amark_node_parent(doc, node);
            }
        }
        node = next;
    }
    NAOS_CHECK_UINT_EQ(visits, 0);
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

NAOS_SUITE(null_document,
    NAOS_ADD_TEST(null_document_destroy_does_nothing),
    NAOS_ADD_TEST(null_document_has_no_root),
    NAOS_ADD_TEST(null_document_has_no_diagnostics),
    NAOS_ADD_TEST(null_document_navigation_returns_none),
    NAOS_ADD_TEST(null_document_traversal_ends)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(config),
    NAOS_ADD_SUITE(null_document)
)
