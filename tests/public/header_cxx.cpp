/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Public API test in C++: the public header compiles as C++,
 * amark_version_string links (C linkage) and returns the header's version,
 * the allocator types can be filled in from C++, the tree types and
 * constants have the values the design fixes, the configuration functions
 * and amark_parse link, the kind IDs and the attribute values are
 * constant expressions that can be used as case labels, and a dialect
 * (<amark/dialect.h>) can be defined as const data from C++.
 */

#include <amark/amark.h>

#include <naos/naos.h>

#include <stddef.h>

namespace {

int allocator_ctx;

void *allocator_malloc(void *ctx, size_t size)
{
    (void)ctx;
    (void)size;
    return NULL;
}

void allocator_free(void *ctx, void *ptr)
{
    (void)ctx;
    (void)ptr;
}

// The kind IDs and the attribute values are part of the ABI (1.9); they
// are checked at compile time against the design's table.
static_assert(sizeof(amark_node_id_t) == 4, "node IDs are 32 bits");
static_assert(sizeof(amark_kind_t) == 4, "kind IDs are 32 bits");
static_assert(AMARK_NODE_NONE == 0, "AMARK_NODE_NONE");
static_assert(AMARK_KIND_NONE == 0, "AMARK_KIND_NONE");
static_assert(AMARK_KIND_DOCUMENT == 1, "AMARK_KIND_DOCUMENT");
static_assert(AMARK_KIND_BLOCK_QUOTE == 2, "AMARK_KIND_BLOCK_QUOTE");
static_assert(AMARK_KIND_LIST == 3, "AMARK_KIND_LIST");
static_assert(AMARK_KIND_LIST_ITEM == 4, "AMARK_KIND_LIST_ITEM");
static_assert(AMARK_KIND_THEMATIC_BREAK == 5, "AMARK_KIND_THEMATIC_BREAK");
static_assert(AMARK_KIND_HEADING == 6, "AMARK_KIND_HEADING");
static_assert(AMARK_KIND_CODE_BLOCK == 7, "AMARK_KIND_CODE_BLOCK");
static_assert(AMARK_KIND_HTML_BLOCK == 8, "AMARK_KIND_HTML_BLOCK");
static_assert(AMARK_KIND_PARAGRAPH == 9, "AMARK_KIND_PARAGRAPH");
static_assert(AMARK_KIND_LINK_REFERENCE_DEFINITION == 10,
              "AMARK_KIND_LINK_REFERENCE_DEFINITION");
static_assert(AMARK_KIND_TEXT == 32, "AMARK_KIND_TEXT");
static_assert(AMARK_KIND_CODE_SPAN == 33, "AMARK_KIND_CODE_SPAN");
static_assert(AMARK_KIND_EMPHASIS == 34, "AMARK_KIND_EMPHASIS");
static_assert(AMARK_KIND_STRONG == 35, "AMARK_KIND_STRONG");
static_assert(AMARK_KIND_LINK == 36, "AMARK_KIND_LINK");
static_assert(AMARK_KIND_IMAGE == 37, "AMARK_KIND_IMAGE");
static_assert(AMARK_KIND_AUTOLINK == 38, "AMARK_KIND_AUTOLINK");
static_assert(AMARK_KIND_RAW_HTML == 39, "AMARK_KIND_RAW_HTML");
static_assert(AMARK_KIND_HARD_BREAK == 40, "AMARK_KIND_HARD_BREAK");
static_assert(AMARK_KIND_SOFT_BREAK == 41, "AMARK_KIND_SOFT_BREAK");
static_assert(AMARK_KIND_BACKSLASH_ESCAPE == 42, "AMARK_KIND_BACKSLASH_ESCAPE");
static_assert(AMARK_KIND_CHARACTER_REFERENCE == 43,
              "AMARK_KIND_CHARACTER_REFERENCE");
static_assert(AMARK_KIND_DIALECT_MIN == 0x10000, "AMARK_KIND_DIALECT_MIN");

static_assert(AMARK_HEADING_STYLE_NONE == 0 && AMARK_HEADING_ATX == 1 &&
                  AMARK_HEADING_SETEXT == 2,
              "amark_heading_style_t");
static_assert(AMARK_LIST_TYPE_NONE == 0 && AMARK_LIST_BULLET == 1 &&
                  AMARK_LIST_ORDERED == 2,
              "amark_list_type_t");
static_assert(AMARK_LIST_SPACING_NONE == 0 && AMARK_LIST_TIGHT == 1 &&
                  AMARK_LIST_LOOSE == 2,
              "amark_list_spacing_t");
static_assert(AMARK_CODE_BLOCK_STYLE_NONE == 0 &&
                  AMARK_CODE_BLOCK_INDENTED == 1 &&
                  AMARK_CODE_BLOCK_FENCED == 2,
              "amark_code_block_style_t");
static_assert(AMARK_LINK_FORM_NONE == 0 && AMARK_LINK_INLINE == 1 &&
                  AMARK_LINK_FULL == 2 && AMARK_LINK_COLLAPSED == 3 &&
                  AMARK_LINK_SHORTCUT == 4,
              "amark_link_form_t");
static_assert(AMARK_AUTOLINK_TYPE_NONE == 0 && AMARK_AUTOLINK_URI == 1 &&
                  AMARK_AUTOLINK_EMAIL == 2,
              "amark_autolink_type_t");
static_assert(AMARK_SEVERITY_WARNING == 1 && AMARK_SEVERITY_ERROR == 2,
              "amark_severity_t");

// The dialect constants are constant expressions with the design's values.
static_assert(AMARK_DIALECT_ABI == 1u, "AMARK_DIALECT_ABI");
static_assert(AMARK_DIALECT_DELIMITER_MAX == 6, "AMARK_DIALECT_DELIMITER_MAX");
static_assert(AMARK_DIALECT_DELIMITER_LENGTH_MAX == 4,
              "AMARK_DIALECT_DELIMITER_LENGTH_MAX");
static_assert(AMARK_DIALECT_BLANK_COLUMNS_MAX == 8,
              "AMARK_DIALECT_BLANK_COLUMNS_MAX");
static_assert(AMARK_DIALECT_LENGTH(1) == 1u && AMARK_DIALECT_LENGTH(4) == 8u,
              "AMARK_DIALECT_LENGTH");
static_assert(AMARK_DIALECT_TOP == static_cast<size_t>(-1),
              "AMARK_DIALECT_TOP");
static_assert(AMARK_DIALECT_CONTAINER == 1 && AMARK_DIALECT_LEAF == 2 &&
                  AMARK_DIALECT_INLINE == 3,
              "amark_dialect_class_t");
static_assert(AMARK_DIALECT_BLANK_CLOSES == 0 &&
                  AMARK_DIALECT_BLANK_CONTINUES == 1,
              "amark_dialect_blank_t");

// A dialect written as const data in C++ (aggregate initialization, as
// C++11 has no designated initializers): one extension, one kind, no rules.
const amark_dialect_kind_t dialect_kinds[] = {
    {AMARK_KIND_DIALECT_MIN, AMARK_DIALECT_CONTAINER,
     AMARK_DIALECT_BLANK_CONTINUES, NULL, NULL,
     AMARK_DIALECT_BLANK_COLUMNS_MAX},
};

const amark_dialect_extension_t dialect_extension = {
    "cxx", dialect_kinds, 1, NULL, 0, NULL, NULL, 0, NULL, 0, NULL, NULL,
    NULL};

const amark_dialect_extension_t *const dialect_extensions[] = {
    &dialect_extension};

const amark_dialect_t dialect = {sizeof(amark_dialect_t), AMARK_DIALECT_ABI,
                                 dialect_extensions, 1};

// The kind ID macros as case labels: each class of kinds maps to a
// different number, so a wrong or duplicated value fails to compile or
// fails the check.
int kind_class(amark_kind_t kind)
{
    switch (kind) {
    case AMARK_KIND_NONE:
        return 0;
    case AMARK_KIND_DOCUMENT:
    case AMARK_KIND_BLOCK_QUOTE:
    case AMARK_KIND_LIST:
    case AMARK_KIND_LIST_ITEM:
    case AMARK_KIND_THEMATIC_BREAK:
    case AMARK_KIND_HEADING:
    case AMARK_KIND_CODE_BLOCK:
    case AMARK_KIND_HTML_BLOCK:
    case AMARK_KIND_PARAGRAPH:
    case AMARK_KIND_LINK_REFERENCE_DEFINITION:
        return 1;
    case AMARK_KIND_TEXT:
    case AMARK_KIND_CODE_SPAN:
    case AMARK_KIND_EMPHASIS:
    case AMARK_KIND_STRONG:
    case AMARK_KIND_LINK:
    case AMARK_KIND_IMAGE:
    case AMARK_KIND_AUTOLINK:
    case AMARK_KIND_RAW_HTML:
    case AMARK_KIND_HARD_BREAK:
    case AMARK_KIND_SOFT_BREAK:
    case AMARK_KIND_BACKSLASH_ESCAPE:
    case AMARK_KIND_CHARACTER_REFERENCE:
        return 2;
    case AMARK_KIND_DIALECT_MIN:
        return 3;
    default:
        return -1;
    }
}

// The attribute enumerations as case labels.
int heading_style_number(amark_heading_style_t style)
{
    switch (style) {
    case AMARK_HEADING_STYLE_NONE:
        return 0;
    case AMARK_HEADING_ATX:
        return 1;
    case AMARK_HEADING_SETEXT:
        return 2;
    }
    return -1;
}

int list_number(amark_list_type_t type, amark_list_spacing_t spacing)
{
    int number = 0;

    switch (type) {
    case AMARK_LIST_TYPE_NONE:
        break;
    case AMARK_LIST_BULLET:
        number += 10;
        break;
    case AMARK_LIST_ORDERED:
        number += 20;
        break;
    }
    switch (spacing) {
    case AMARK_LIST_SPACING_NONE:
        break;
    case AMARK_LIST_TIGHT:
        number += 1;
        break;
    case AMARK_LIST_LOOSE:
        number += 2;
        break;
    }
    return number;
}

int code_block_style_number(amark_code_block_style_t style)
{
    switch (style) {
    case AMARK_CODE_BLOCK_STYLE_NONE:
        return 0;
    case AMARK_CODE_BLOCK_INDENTED:
        return 1;
    case AMARK_CODE_BLOCK_FENCED:
        return 2;
    }
    return -1;
}

int link_form_number(amark_link_form_t form)
{
    switch (form) {
    case AMARK_LINK_FORM_NONE:
        return 0;
    case AMARK_LINK_INLINE:
        return 1;
    case AMARK_LINK_FULL:
        return 2;
    case AMARK_LINK_COLLAPSED:
        return 3;
    case AMARK_LINK_SHORTCUT:
        return 4;
    }
    return -1;
}

int autolink_type_number(amark_autolink_type_t type)
{
    switch (type) {
    case AMARK_AUTOLINK_TYPE_NONE:
        return 0;
    case AMARK_AUTOLINK_URI:
        return 1;
    case AMARK_AUTOLINK_EMAIL:
        return 2;
    }
    return -1;
}

} // namespace

NAOS_TEST(version_string)
{
    NAOS_CHECK_STR_EQ(AMARK_VERSION_STRING, amark_version_string());
}

NAOS_TEST(allocator_types)
{
    amark_malloc_fn_t malloc_fn = allocator_malloc;
    amark_free_fn_t free_fn = allocator_free;
    amark_allocator_t allocator = {malloc_fn, free_fn, &allocator_ctx};

    NAOS_CHECK_PTR_EQ(&allocator_ctx, allocator.ctx);
    NAOS_CHECK_TRUE(allocator.malloc_fn == allocator_malloc);
    NAOS_CHECK_TRUE(allocator.free_fn == allocator_free);
}

NAOS_TEST(tree_types)
{
    // amark_document_t is an incomplete type: only pointers to it exist.
    const amark_document_t *doc = NULL;
    amark_node_id_t node = AMARK_NODE_NONE;
    amark_kind_t kind = AMARK_KIND_DIALECT_MIN;
    amark_range_t range = {3, 7};
    amark_diagnostic_t diagnostic = {5, 2, AMARK_SEVERITY_ERROR, "message",
                                     NULL};

    NAOS_CHECK_PTR_EQ(NULL, doc);
    NAOS_CHECK_UINT_EQ(0, node);
    NAOS_CHECK_UINT_EQ(0x10000, kind);
    NAOS_CHECK_UINT_EQ(3, range.start);
    NAOS_CHECK_UINT_EQ(7, range.end);
    NAOS_CHECK_UINT_EQ(5, diagnostic.offset);
    NAOS_CHECK_UINT_EQ(2, diagnostic.line);
    NAOS_CHECK_TRUE(diagnostic.severity == AMARK_SEVERITY_ERROR);
    NAOS_CHECK_STR_EQ("message", diagnostic.message);
    NAOS_CHECK_PTR_EQ(NULL, diagnostic.filename);
}

NAOS_TEST(config_functions)
{
    amark_config_t config;

    amark_config_init(&config);
    NAOS_CHECK_UINT_EQ(sizeof(amark_config_t), config.struct_size);
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_config_validate(&config, 10));

    config.utf8_mode = AMARK_UTF8_REPLACE;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_NONE, amark_config_validate(&config, 10));

    config.start = 5;
    config.end = 4;
    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_CONFIG,
                      amark_config_validate(&config, 10));
}

NAOS_TEST(parse_function)
{
    static const char content[] = "a";
    amark_document_t *doc = NULL;
    amark_range_t range = {0, 0};

    NAOS_REQUIRE_INT_EQ(AMARK_ERROR_NONE,
                        amark_parse(content, sizeof(content) - 1, NULL, &doc));
    NAOS_REQUIRE_NOT_NULL(doc);
    NAOS_CHECK_TRUE(amark_node_range(doc, amark_document_root(doc), &range));
    NAOS_CHECK_UINT_EQ(1, range.end);
    amark_document_destroy(doc);

    NAOS_CHECK_INT_EQ(AMARK_ERROR_INVALID_INPUT,
                      amark_parse(NULL, 1, NULL, &doc));
    NAOS_CHECK_NULL(doc);
}

NAOS_TEST(dialect_types)
{
    amark_config_t config;

    amark_config_init(&config);
    config.dialect = &dialect;
    NAOS_CHECK_PTR_EQ(&dialect, config.dialect);
    NAOS_CHECK_UINT_EQ(AMARK_DIALECT_ABI, dialect.abi);
    NAOS_REQUIRE_UINT_EQ(1, dialect.extension_count);
    NAOS_CHECK_STR_EQ("cxx", dialect.extensions[0]->name);
    NAOS_CHECK_UINT_EQ(AMARK_KIND_DIALECT_MIN,
                       dialect.extensions[0]->kinds[0].id);
}

NAOS_TEST(kind_ids_as_case_labels)
{
    NAOS_CHECK_INT_EQ(0, kind_class(AMARK_KIND_NONE));
    NAOS_CHECK_INT_EQ(1, kind_class(AMARK_KIND_DOCUMENT));
    NAOS_CHECK_INT_EQ(1, kind_class(AMARK_KIND_LINK_REFERENCE_DEFINITION));
    NAOS_CHECK_INT_EQ(2, kind_class(AMARK_KIND_TEXT));
    NAOS_CHECK_INT_EQ(2, kind_class(AMARK_KIND_CHARACTER_REFERENCE));
    NAOS_CHECK_INT_EQ(3, kind_class(AMARK_KIND_DIALECT_MIN));
    NAOS_CHECK_INT_EQ(-1, kind_class(11));
    NAOS_CHECK_INT_EQ(-1, kind_class(44));
}

NAOS_TEST(attribute_values_as_case_labels)
{
    NAOS_CHECK_INT_EQ(0, heading_style_number(AMARK_HEADING_STYLE_NONE));
    NAOS_CHECK_INT_EQ(1, heading_style_number(AMARK_HEADING_ATX));
    NAOS_CHECK_INT_EQ(2, heading_style_number(AMARK_HEADING_SETEXT));
    NAOS_CHECK_INT_EQ(0, list_number(AMARK_LIST_TYPE_NONE,
                                     AMARK_LIST_SPACING_NONE));
    NAOS_CHECK_INT_EQ(11, list_number(AMARK_LIST_BULLET, AMARK_LIST_TIGHT));
    NAOS_CHECK_INT_EQ(22, list_number(AMARK_LIST_ORDERED, AMARK_LIST_LOOSE));
    NAOS_CHECK_INT_EQ(0, code_block_style_number(AMARK_CODE_BLOCK_STYLE_NONE));
    NAOS_CHECK_INT_EQ(1, code_block_style_number(AMARK_CODE_BLOCK_INDENTED));
    NAOS_CHECK_INT_EQ(2, code_block_style_number(AMARK_CODE_BLOCK_FENCED));
    NAOS_CHECK_INT_EQ(0, link_form_number(AMARK_LINK_FORM_NONE));
    NAOS_CHECK_INT_EQ(1, link_form_number(AMARK_LINK_INLINE));
    NAOS_CHECK_INT_EQ(2, link_form_number(AMARK_LINK_FULL));
    NAOS_CHECK_INT_EQ(3, link_form_number(AMARK_LINK_COLLAPSED));
    NAOS_CHECK_INT_EQ(4, link_form_number(AMARK_LINK_SHORTCUT));
    NAOS_CHECK_INT_EQ(0, autolink_type_number(AMARK_AUTOLINK_TYPE_NONE));
    NAOS_CHECK_INT_EQ(1, autolink_type_number(AMARK_AUTOLINK_URI));
    NAOS_CHECK_INT_EQ(2, autolink_type_number(AMARK_AUTOLINK_EMAIL));
}

NAOS_SUITE(header_cxx,
    NAOS_ADD_TEST(version_string),
    NAOS_ADD_TEST(allocator_types),
    NAOS_ADD_TEST(tree_types),
    NAOS_ADD_TEST(config_functions),
    NAOS_ADD_TEST(parse_function),
    NAOS_ADD_TEST(dialect_types),
    NAOS_ADD_TEST(kind_ids_as_case_labels),
    NAOS_ADD_TEST(attribute_values_as_case_labels)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(header_cxx)
)
