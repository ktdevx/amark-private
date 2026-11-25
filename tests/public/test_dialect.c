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
 *
 * The test dialect of the tests (tests/support/test_dialect.c, written with
 * the public headers only) is compiled into this test, with the support
 * directory and no internal header directory on its include path. A parse
 * with it is read with the traversal of <amark/tree.h>: the kind, the class
 * (block or inline), the range, the lines and the attributes of its dialect
 * nodes and its diagnostics (Requirements 1.7, 2.4, 2.5, 15.2). The parse
 * is repeated with a local copy of the definition (the dialect, its
 * extension, its tables and its name), which is overwritten once the parse
 * returns: the document reads the same before and after (Requirement 1.5).
 */

#include <amark/dialect.h>

#include <amark/amark.h>

#include <naos/naos.h>

#include <stddef.h>
#include <string.h>

#include "test_dialect.h"

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

/* ---------------------------------------------------------------------- */
/* A parse with the test dialect, read through the public API              */
/* ---------------------------------------------------------------------- */

/*
 * Offsets:   0 "@> a $ab$"  10 "@> b"  15 ""  16 "@line *x*"
 *           26 "$!inline-empty$", 42 bytes. A box of two lines (its close
 * rule sets its attributes to "2") holding a paragraph with a VAR (its
 * attributes, the length of its name: "2"), a LINE whose inline content is
 * "*x*", and a paragraph with an invalid VAR: the inline rule returns an
 * empty range, which is not used, and a diagnostic is added at its
 * position.
 */
static const char k_dialect_content[] =
    "@> a $ab$\n"
    "@> b\n"
    "\n"
    "@line *x*\n"
    "$!inline-empty$\n";

/* A node as the traversal meets it, in document order. */
typedef struct expected_node {
    amark_kind_t kind;
    int is_block; /* 1 for a block, 0 for an inline node */
    size_t depth; /* the root is at depth 0 */
    size_t start;
    size_t end;
    size_t start_line;
    size_t end_line;
    const char *data; /* the attributes; NULL for none */
} expected_node_t;

static const expected_node_t k_dialect_nodes[] = {
    {AMARK_KIND_DOCUMENT, 1, 0, 0, 42, 1, 5, NULL},
    {AMARK_TEST_KIND_BOX, 1, 1, 0, 14, 1, 2, "2"},
    {AMARK_KIND_PARAGRAPH, 1, 2, 3, 14, 1, 2, NULL},
    {AMARK_KIND_TEXT, 0, 3, 3, 5, 1, 1, NULL},
    {AMARK_TEST_KIND_VAR, 0, 3, 5, 9, 1, 1, "2"},
    {AMARK_KIND_SOFT_BREAK, 0, 3, 9, 10, 1, 1, NULL},
    {AMARK_KIND_TEXT, 0, 3, 13, 14, 2, 2, NULL},
    {AMARK_TEST_KIND_LINE, 1, 1, 16, 25, 4, 4, NULL},
    {AMARK_KIND_EMPHASIS, 0, 2, 22, 25, 4, 4, NULL},
    {AMARK_KIND_TEXT, 0, 3, 23, 24, 4, 4, NULL},
    {AMARK_KIND_PARAGRAPH, 1, 1, 26, 41, 5, 5, NULL},
    {AMARK_KIND_TEXT, 0, 2, 26, 41, 5, 5, NULL}};

#define DIALECT_NODE_COUNT (sizeof(k_dialect_nodes) / sizeof(k_dialect_nodes[0]))

/* The one diagnostic: the invalid VAR, at its '$'. */
#define DIAGNOSTIC_MESSAGE "dialect \"test\": inline: empty range"
#define DIAGNOSTIC_OFFSET 26
#define DIAGNOSTIC_LINE 5

/*
 * NAOS_REQUIRE_* leaves a test with longjmp, so the document is kept in a
 * static variable and teardown destroys it.
 */
static amark_document_t *g_doc;

NAOS_SETUP(document)
{
    g_doc = NULL;
}

NAOS_TEARDOWN(document)
{
    amark_document_destroy(g_doc);
    g_doc = NULL;
}

/* Parses k_dialect_content with dialect into g_doc. */
static void parse_with(const amark_dialect_t *dialect)
{
    amark_config_t config;

    amark_config_init(&config);
    config.dialect = dialect;
    NAOS_REQUIRE_INT_EQ(amark_parse(k_dialect_content,
                                    sizeof(k_dialect_content) - 1, &config,
                                    &g_doc),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_NOT_NULL(g_doc);
}

/* Requires node of g_doc to be the nth node of k_dialect_nodes, at depth. */
static void check_dialect_node(amark_node_id_t node, size_t nth, size_t depth)
{
    const expected_node_t *want = &k_dialect_nodes[nth];
    amark_range_t range = {0, 0};
    const void *data;
    size_t size = 1;

    NAOS_TRACE_PUSH("node %lu", (unsigned long)nth);
    NAOS_CHECK_UINT_EQ(amark_node_kind(g_doc, node), want->kind);
    NAOS_CHECK_INT_EQ(amark_kind_is_dialect(want->kind) ? 1 : 0,
                      want->kind >= AMARK_KIND_DIALECT_MIN);
    NAOS_CHECK_INT_EQ(amark_node_is_block(g_doc, node) ? 1 : 0,
                      want->is_block);
    NAOS_CHECK_INT_EQ(amark_node_is_inline(g_doc, node) ? 1 : 0,
                      !want->is_block);
    NAOS_CHECK_UINT_EQ(depth, want->depth);
    NAOS_CHECK_TRUE(amark_node_range(g_doc, node, &range));
    NAOS_CHECK_UINT_EQ(range.start, want->start);
    NAOS_CHECK_UINT_EQ(range.end, want->end);
    NAOS_CHECK_UINT_EQ(amark_node_start_line(g_doc, node), want->start_line);
    NAOS_CHECK_UINT_EQ(amark_node_end_line(g_doc, node), want->end_line);
    data = amark_node_dialect_data(g_doc, node, &size);
    if (want->data == NULL) {
        NAOS_CHECK_NULL(data);
        NAOS_CHECK_UINT_EQ(size, 0);
    } else {
        NAOS_CHECK_NOT_NULL(data);
        NAOS_CHECK_UINT_EQ(size, strlen(want->data));
        if (data != NULL && size == strlen(want->data)) {
            NAOS_CHECK_INT_EQ(memcmp(data, want->data, size), 0);
        }
    }
    NAOS_TRACE_POP();
}

/*
 * Requires g_doc to be the tree of k_dialect_nodes, visited with the
 * traversal of <amark/tree.h>, with the one diagnostic of the content.
 */
static void check_dialect_document(void)
{
    amark_node_id_t root = amark_document_root(g_doc);
    amark_node_id_t node = root;
    amark_diagnostic_t diag;
    size_t depth = 0;
    size_t nth = 0;

    NAOS_REQUIRE_UINT_NE(root, AMARK_NODE_NONE);
    while (node != AMARK_NODE_NONE) {
        amark_node_id_t next;

        NAOS_REQUIRE_UINT_LT(nth, DIALECT_NODE_COUNT);
        check_dialect_node(node, nth, depth);
        nth++;
        next = amark_node_first_child(g_doc, node);
        if (next != AMARK_NODE_NONE) {
            depth++;
        }
        while (next == AMARK_NODE_NONE && node != root) {
            next = amark_node_next_sibling(g_doc, node);
            if (next == AMARK_NODE_NONE) {
                node = amark_node_parent(g_doc, node);
                depth--;
            }
        }
        node = next;
    }
    NAOS_CHECK_UINT_EQ(nth, DIALECT_NODE_COUNT);

    NAOS_REQUIRE_UINT_EQ(amark_document_diagnostic_count(g_doc), 1);
    NAOS_REQUIRE_TRUE(amark_document_diagnostic(g_doc, 0, &diag));
    NAOS_CHECK_STR_EQ(diag.message, DIAGNOSTIC_MESSAGE);
    NAOS_CHECK_UINT_EQ(diag.offset, DIAGNOSTIC_OFFSET);
    NAOS_CHECK_UINT_EQ(diag.line, DIAGNOSTIC_LINE);
    NAOS_CHECK_INT_EQ(diag.severity, AMARK_SEVERITY_ERROR);
    NAOS_CHECK_NULL(diag.filename);
}

NAOS_TEST(test_dialect_nodes_are_read_through_the_public_api)
{
    parse_with(amark_test_dialect());
    check_dialect_document();
}

/* The capacities of the local copy; the test dialect fits. */
#define COPY_KINDS_MAX 16
#define COPY_STARTS_MAX 8
#define COPY_INLINE_RULES_MAX 4
#define COPY_DELIMITERS_MAX 4
#define COPY_NAME_MAX 16

/*
 * A copy of a dialect of one extension in local storage: the dialect, its
 * extension pointer, the extension, its tables and its name.
 */
typedef struct dialect_copy {
    amark_dialect_t dialect;
    const amark_dialect_extension_t *extensions[1];
    amark_dialect_extension_t extension;
    amark_dialect_kind_t kinds[COPY_KINDS_MAX];
    amark_dialect_block_start_t starts[COPY_STARTS_MAX];
    amark_dialect_inline_rule_t inline_rules[COPY_INLINE_RULES_MAX];
    amark_dialect_delimiter_t delimiters[COPY_DELIMITERS_MAX];
    char name[COPY_NAME_MAX];
} dialect_copy_t;

/* Copies source, a dialect of one extension that fits, into *copy. */
static void copy_dialect(dialect_copy_t *copy, const amark_dialect_t *source)
{
    const amark_dialect_extension_t *ext;

    NAOS_REQUIRE_UINT_EQ(source->extension_count, 1);
    ext = source->extensions[0];
    NAOS_REQUIRE_UINT_LE(ext->kind_count, COPY_KINDS_MAX);
    NAOS_REQUIRE_UINT_LE(ext->block_start_count, COPY_STARTS_MAX);
    NAOS_REQUIRE_UINT_LE(ext->inline_rule_count, COPY_INLINE_RULES_MAX);
    NAOS_REQUIRE_UINT_LE(ext->delimiter_count, COPY_DELIMITERS_MAX);
    NAOS_REQUIRE_UINT_LT(strlen(ext->name), COPY_NAME_MAX);

    memset(copy, 0, sizeof(*copy));
    copy->extension = *ext;
    if (ext->kind_count > 0) {
        memcpy(copy->kinds, ext->kinds, ext->kind_count * sizeof(*ext->kinds));
        copy->extension.kinds = copy->kinds;
    }
    if (ext->block_start_count > 0) {
        memcpy(copy->starts, ext->block_starts,
               ext->block_start_count * sizeof(*ext->block_starts));
        copy->extension.block_starts = copy->starts;
    }
    if (ext->inline_rule_count > 0) {
        memcpy(copy->inline_rules, ext->inline_rules,
               ext->inline_rule_count * sizeof(*ext->inline_rules));
        copy->extension.inline_rules = copy->inline_rules;
    }
    if (ext->delimiter_count > 0) {
        memcpy(copy->delimiters, ext->delimiters,
               ext->delimiter_count * sizeof(*ext->delimiters));
        copy->extension.delimiters = copy->delimiters;
    }
    (void)strcpy(copy->name, ext->name);
    copy->extension.name = copy->name;
    copy->extensions[0] = &copy->extension;
    copy->dialect = *source;
    copy->dialect.extensions = copy->extensions;
}

NAOS_TEST(document_does_not_refer_to_the_definition)
{
    dialect_copy_t copy;

    copy_dialect(&copy, amark_test_dialect());
    parse_with(&copy.dialect);
    check_dialect_document();

    /*
     * Overwrite the whole definition: its version, its counts, its
     * pointers (tables, rules, name) and the bytes of its tables and name.
     * The document reads the same, so it keeps no reference to it.
     */
    memset(&copy, 0xA5, sizeof(copy));
    check_dialect_document();
}

NAOS_TEST(dialect_kinds_are_dialect_kinds)
{
    NAOS_CHECK_TRUE(amark_kind_is_dialect(AMARK_TEST_KIND_BOX));
    NAOS_CHECK_TRUE(amark_kind_is_dialect(AMARK_TEST_KIND_VAR));
    NAOS_CHECK_TRUE(amark_kind_is_dialect(AMARK_TEST_KIND_LINE));
    NAOS_CHECK_FALSE(amark_kind_is_core(AMARK_TEST_KIND_BOX));
}

NAOS_SUITE(dialect,
    NAOS_ADD_TEST(static_definition),
    NAOS_ADD_TEST(constants),
    NAOS_ADD_TEST(config_check_accepts_the_definition),
    NAOS_ADD_TEST(config_check_rejects_a_copy_with_another_abi)
)

NAOS_SUITE(test_dialect,
    NAOS_SET_SETUP(document),
    NAOS_SET_TEARDOWN(document),
    NAOS_ADD_TEST(test_dialect_nodes_are_read_through_the_public_api),
    NAOS_ADD_TEST(document_does_not_refer_to_the_definition),
    NAOS_ADD_TEST(dialect_kinds_are_dialect_kinds)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(dialect),
    NAOS_ADD_SUITE(test_dialect)
)
