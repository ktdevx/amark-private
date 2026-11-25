/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Public API test of the dialect definition: links Amark::amark and uses
 * only the public headers. <amark/dialect.h> is included first, on its own,
 * to show that it compiles by itself. A dialect is written as static const
 * data with designated initializers only (no allocation and no registration
 * call): one extension with one kind and no rules. The public
 * configuration check accepts it, and rejects a copy of it with another
 * abi (Requirements 3.1, 3.2, 3.12).
 */

#include <amark/dialect.h>

#include <amark/amark.h>

#include <naos/naos.h>

#include <stddef.h>
#include <string.h>

/* The kind of the test extension: a leaf block that a blank line closes. */
#define TEST_KIND_NOTE (AMARK_KIND_DIALECT_MIN + 1)

static const amark_dialect_kind_t test_kinds[] = {
    {.id = TEST_KIND_NOTE,
     .cls = AMARK_DIALECT_LEAF,
     .blank = AMARK_DIALECT_BLANK_CLOSES},
};

static const amark_dialect_extension_t test_extension = {
    .name = "note",
    .kinds = test_kinds,
    .kind_count = sizeof(test_kinds) / sizeof(test_kinds[0]),
};

static const amark_dialect_extension_t *const test_extensions[] = {
    &test_extension,
};

static const amark_dialect_t test_dialect = {
    .struct_size = sizeof(amark_dialect_t),
    .abi = AMARK_DIALECT_ABI,
    .extensions = test_extensions,
    .extension_count = sizeof(test_extensions) / sizeof(test_extensions[0]),
};

NAOS_TEST(static_definition)
{
    const amark_dialect_extension_t *extension = test_dialect.extensions[0];

    NAOS_CHECK_UINT_EQ(sizeof(amark_dialect_t), test_dialect.struct_size);
    NAOS_CHECK_UINT_EQ(AMARK_DIALECT_ABI, test_dialect.abi);
    NAOS_REQUIRE_UINT_EQ(1, test_dialect.extension_count);
    NAOS_CHECK_STR_EQ("note", extension->name);
    NAOS_REQUIRE_UINT_EQ(1, extension->kind_count);
    NAOS_CHECK_UINT_EQ(TEST_KIND_NOTE, extension->kinds[0].id);
    NAOS_CHECK_INT_EQ(AMARK_DIALECT_LEAF, extension->kinds[0].cls);
    NAOS_CHECK_INT_EQ(AMARK_DIALECT_BLANK_CLOSES, extension->kinds[0].blank);
    NAOS_CHECK_TRUE(extension->kinds[0].continue_fn == NULL);
    NAOS_CHECK_TRUE(extension->kinds[0].close_fn == NULL);
    NAOS_CHECK_NULL(extension->block_starts);
    NAOS_CHECK_UINT_EQ(0, extension->block_start_count);
    NAOS_CHECK_TRUE(extension->close_paragraph == NULL);
    NAOS_CHECK_NULL(extension->inline_rules);
    NAOS_CHECK_UINT_EQ(0, extension->inline_rule_count);
    NAOS_CHECK_NULL(extension->delimiters);
    NAOS_CHECK_UINT_EQ(0, extension->delimiter_count);
    NAOS_CHECK_TRUE(extension->postprocess == NULL);
    NAOS_CHECK_TRUE(extension->state_new == NULL);
    NAOS_CHECK_TRUE(extension->state_free == NULL);
}

NAOS_TEST(constants)
{
    NAOS_CHECK_UINT_EQ(1, AMARK_DIALECT_ABI);
    NAOS_CHECK_INT_EQ(6, AMARK_DIALECT_DELIMITER_MAX);
    NAOS_CHECK_INT_EQ(4, AMARK_DIALECT_DELIMITER_LENGTH_MAX);
    NAOS_CHECK_UINT_EQ(1, AMARK_DIALECT_LENGTH(1));
    NAOS_CHECK_UINT_EQ(8, AMARK_DIALECT_LENGTH(4));
    NAOS_CHECK_TRUE(AMARK_DIALECT_TOP == (size_t)-1);
    NAOS_CHECK_INT_EQ(1, AMARK_DIALECT_CONTAINER);
    NAOS_CHECK_INT_EQ(2, AMARK_DIALECT_LEAF);
    NAOS_CHECK_INT_EQ(3, AMARK_DIALECT_INLINE);
    NAOS_CHECK_INT_EQ(0, AMARK_DIALECT_BLANK_CLOSES);
    NAOS_CHECK_INT_EQ(1, AMARK_DIALECT_BLANK_CONTINUES);
}

NAOS_TEST(config_check_accepts_the_definition)
{
    static const char content[] = "text\n";
    amark_config_t config;
    amark_document_t *doc = NULL;

    amark_config_init(&config);
    config.dialect = &test_dialect;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE,
                      amark_config_validate(&config, sizeof(content) - 1));
    NAOS_REQUIRE_INT_EQ(
        AMARK_ERROR_NONE,
        amark_parse(content, sizeof(content) - 1, &config, &doc));
    NAOS_CHECK_NOT_NULL(doc);
    amark_document_destroy(doc);
}

NAOS_TEST(config_check_rejects_a_copy_with_another_abi)
{
    static const char content[] = "text\n";
    amark_dialect_t copy;
    amark_config_t config;
    amark_document_t *doc;

    memcpy(&copy, &test_dialect, sizeof(copy));
    copy.abi = AMARK_DIALECT_ABI + 1;
    amark_config_init(&config);
    config.dialect = &copy;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CONFIG,
                      amark_config_validate(&config, sizeof(content) - 1));
    /* The parse entry returns the same error and no document. */
    doc = (amark_document_t *)(void *)&copy; /* any non-NULL value */
    NAOS_CHECK_INT_EQ(
        AMARK_ERROR_INVALID_CONFIG,
        amark_parse(content, sizeof(content) - 1, &config, &doc));
    NAOS_CHECK_NULL(doc);
}

NAOS_SUITE(dialect,
    NAOS_ADD_TEST(static_definition),
    NAOS_ADD_TEST(constants),
    NAOS_ADD_TEST(config_check_accepts_the_definition),
    NAOS_ADD_TEST(config_check_rejects_a_copy_with_another_abi)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(dialect)
)
