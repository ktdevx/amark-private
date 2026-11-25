/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of the entry point of a parse (amark_parse, include/amark/parse.h):
 * the order of the checks and the cases where several apply, that the checks
 * allocate nothing, empty ranges and NULL content, the input size limit, the
 * two UTF-8 modes, ranges that do not start at 0, the file name, the
 * allocator, the release of everything on failure and of the scratch arena on
 * success, that the same input gives the same tree, and that a dialect
 * changes nothing. Every parsed document
 * goes through amark_tree_check.
 */

#include "arena.h"
#include "block.h"
#include "config_internal.h"
#include "document.h"
#include "line.h"
#include "parse_data.h"
#include "test_alloc.h"
#include "tree_check.h"
#include "tree_dump.h"

#include <amark/amark.h>

#include <naos/naos.h>

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/*
 * Fixture. NAOS_REQUIRE_* leaves a test with longjmp, so the documents a test
 * makes are kept in static variables, and teardown destroys them and checks
 * that no block is left and that every call received its context value.
 */
#define DUMP_SIZE 1024

static amark_test_alloc_t g_state;
static amark_config_t g_config;
static amark_document_t *g_doc;
static amark_document_t *g_other;
static amark_arena_t g_scratch;
static char g_dump[DUMP_SIZE];
static char g_other_dump[DUMP_SIZE];

/* A value of *out that no successful or failed parse leaves there. */
static amark_document_t *const k_sentinel = (amark_document_t *)&g_state;

NAOS_SETUP(counting)
{
    amark_test_alloc_init(&g_state);
    amark_config_init(&g_config);
    g_config.allocator = amark_test_allocator(&g_state);
    amark_arena_init(&g_scratch, &g_config.allocator);
    g_doc = NULL;
    g_other = NULL;
}

NAOS_TEARDOWN(counting)
{
    g_state.fail_at = 0;
    amark_arena_deinit(&g_scratch);
    if (g_doc != k_sentinel) {
        amark_document_destroy(g_doc);
    }
    if (g_other != k_sentinel) {
        amark_document_destroy(g_other);
    }
    g_doc = NULL;
    g_other = NULL;
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
    NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
}

/* Requires doc to pass the tree check. */
static void check_tree(const amark_document_t *doc)
{
    amark_tree_check_result_t check = amark_tree_check(doc);

    NAOS_CHECK_INT_EQ(check.violation, AMARK_TREE_VIOLATION_NONE);
    NAOS_CHECK_UINT_EQ(check.node, AMARK_NODE_NONE);
}

/* Writes the block structure of doc to buffer (DUMP_SIZE bytes). */
static void dump(const amark_document_t *doc, char *buffer)
{
    size_t length = amark_test_dump_blocks(doc, buffer, DUMP_SIZE);

    NAOS_REQUIRE_UINT_LT(length, DUMP_SIZE);
}

/* Checks the kind, range and lines of node of g_doc. */
static void check_node(amark_node_id_t node, amark_kind_t kind, size_t start,
                       size_t end, size_t start_line, size_t end_line)
{
    amark_range_t range;

    NAOS_CHECK_UINT_EQ(amark_node_kind(g_doc, node), kind);
    NAOS_REQUIRE_TRUE(amark_node_range(g_doc, node, &range));
    NAOS_CHECK_UINT_EQ(range.start, start);
    NAOS_CHECK_UINT_EQ(range.end, end);
    NAOS_CHECK_UINT_EQ(amark_node_start_line(g_doc, node), start_line);
    NAOS_CHECK_UINT_EQ(amark_node_end_line(g_doc, node), end_line);
}

/*
 * Requires amark_parse to fail with want, store NULL in *out, call the
 * allocator not at all and leave no block.
 */
static void check_fails_without_allocating(const char *content, size_t length,
                                           const amark_config_t *config,
                                           amark_error_t want)
{
    g_doc = k_sentinel;
    NAOS_CHECK_INT_EQ(amark_parse(content, length, config, &g_doc), want);
    NAOS_CHECK_NULL(g_doc);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 0);
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
}

#define S(s) (s), (sizeof(s) - 1)

/* ---------------------------------------------------------------------- */
/* The order of the checks */

NAOS_TEST(null_out_is_invalid_input)
{
    NAOS_CHECK_INT_EQ(amark_parse(S("a"), &g_config, NULL),
                      AMARK_ERROR_INVALID_INPUT);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 0);
}

NAOS_TEST(null_out_comes_before_every_other_check)
{
    g_config.struct_size = 0;
    g_config.max_input_size = 1;
    NAOS_CHECK_INT_EQ(amark_parse(NULL, 5, &g_config, NULL),
                      AMARK_ERROR_INVALID_INPUT);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 0);
}

NAOS_TEST(null_content_with_a_length_is_invalid_input)
{
    check_fails_without_allocating(NULL, 3, &g_config,
                                   AMARK_ERROR_INVALID_INPUT);
}

NAOS_TEST(null_content_comes_before_the_config)
{
    g_config.struct_size = 0;
    g_config.max_input_size = 1;
    check_fails_without_allocating(NULL, 3, &g_config,
                                   AMARK_ERROR_INVALID_INPUT);
}

NAOS_TEST(invalid_config_allocates_nothing)
{
    g_config.struct_size = 0;
    check_fails_without_allocating(S("a"), &g_config,
                                   AMARK_ERROR_INVALID_CONFIG);
}

NAOS_TEST(range_past_the_content_is_invalid_config)
{
    g_config.start = 0;
    g_config.end = 2;
    check_fails_without_allocating(S("a"), &g_config,
                                   AMARK_ERROR_INVALID_CONFIG);
}

NAOS_TEST(invalid_utf8_mode_is_invalid_config)
{
    g_config.utf8_mode = 2;
    check_fails_without_allocating(S("a"), &g_config,
                                   AMARK_ERROR_INVALID_CONFIG);
}

NAOS_TEST(config_comes_before_the_size_limit_and_utf8)
{
    g_config.allocator.free_fn = NULL;
    g_config.max_input_size = 1;
    check_fails_without_allocating(S("a\xff"), &g_config,
                                   AMARK_ERROR_INVALID_CONFIG);
}

NAOS_TEST(input_over_the_size_limit_allocates_nothing)
{
    g_config.max_input_size = 3;
    check_fails_without_allocating(S("abcd"), &g_config,
                                   AMARK_ERROR_LIMIT_EXCEEDED);
}

NAOS_TEST(size_limit_counts_the_whole_content_not_the_range)
{
    g_config.max_input_size = 3;
    g_config.start = 0;
    g_config.end = 1;
    check_fails_without_allocating(S("abcd"), &g_config,
                                   AMARK_ERROR_LIMIT_EXCEEDED);
}

NAOS_TEST(size_limit_comes_before_utf8)
{
    g_config.max_input_size = 1;
    check_fails_without_allocating(S("a\xff"), &g_config,
                                   AMARK_ERROR_LIMIT_EXCEEDED);
}

NAOS_TEST(invalid_utf8_in_strict_mode_allocates_nothing)
{
    check_fails_without_allocating(S("a\xc3("), &g_config,
                                   AMARK_ERROR_INVALID_INPUT);
}

NAOS_TEST(allocation_failure_is_nomem)
{
    g_state.fail_at = 1;
    g_doc = k_sentinel;
    NAOS_CHECK_INT_EQ(amark_parse(S("a"), &g_config, &g_doc),
                      AMARK_ERROR_NOMEM);
    NAOS_CHECK_NULL(g_doc);
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
}

NAOS_TEST(node_count_limit_fails_and_releases_everything)
{
    /*
     * The root, two paragraphs and their texts: five nodes. A limit of 2
     * stops the block parser; a limit of 4 stops the inline parser, after
     * the blocks were made, and those are released too.
     */
    g_config.max_node_count = 2;
    g_doc = k_sentinel;
    NAOS_CHECK_INT_EQ(amark_parse(S("a\n\nb"), &g_config, &g_doc),
                      AMARK_ERROR_LIMIT_EXCEEDED);
    NAOS_CHECK_NULL(g_doc);
    NAOS_CHECK_UINT_GT(g_state.malloc_calls, 0);
    NAOS_CHECK_UINT_EQ(g_state.live, 0);

    g_config.max_node_count = 4;
    g_doc = k_sentinel;
    NAOS_CHECK_INT_EQ(amark_parse(S("a\n\nb"), &g_config, &g_doc),
                      AMARK_ERROR_LIMIT_EXCEEDED);
    NAOS_CHECK_NULL(g_doc);
    NAOS_CHECK_UINT_EQ(g_state.live, 0);

    g_config.max_node_count = 5;
    NAOS_REQUIRE_INT_EQ(amark_parse(S("a\n\nb"), &g_config, &g_doc),
                        AMARK_ERROR_NONE);
    check_tree(g_doc);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 5);
}

NAOS_TEST(allocation_failure_at_every_call)
{
    static const char content[] = "a\nb\n\n  c\n\nd";
    size_t fail_at;
    bool succeeded = false;

    for (fail_at = 1; fail_at < 1000 && !succeeded; fail_at++) {
        amark_error_t err;

        amark_test_alloc_init(&g_state);
        g_state.fail_at = fail_at;
        g_doc = k_sentinel;
        err = amark_parse(S(content), &g_config, &g_doc);
        if (err == AMARK_ERROR_NONE) {
            succeeded = true;
            check_tree(g_doc);
            amark_document_destroy(g_doc);
            g_doc = NULL;
        } else {
            NAOS_CHECK_INT_EQ(err, AMARK_ERROR_NOMEM);
            NAOS_CHECK_NULL(g_doc);
        }
        NAOS_CHECK_UINT_EQ(g_state.live, 0);
        NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
    }
    NAOS_CHECK_TRUE(succeeded);
    NAOS_CHECK_UINT_GT(fail_at, 2);
}

/* ---------------------------------------------------------------------- */
/* Empty input and the size limit */

NAOS_TEST(null_content_of_length_zero)
{
    NAOS_REQUIRE_INT_EQ(amark_parse(NULL, 0, &g_config, &g_doc),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_NOT_NULL(g_doc);
    check_tree(g_doc);
    check_node(amark_document_root(g_doc), AMARK_KIND_DOCUMENT, 0, 0, 1, 1);
    NAOS_CHECK_UINT_EQ(amark_node_first_child(g_doc, amark_document_root(g_doc)),
                       AMARK_NODE_NONE);
}

NAOS_TEST(empty_range_inside_the_content)
{
    g_config.start = 2;
    g_config.end = 2;
    NAOS_REQUIRE_INT_EQ(amark_parse(S("a\nb\n"), &g_config, &g_doc),
                        AMARK_ERROR_NONE);
    check_tree(g_doc);
    check_node(amark_document_root(g_doc), AMARK_KIND_DOCUMENT, 2, 2, 2, 2);
    dump(g_doc, g_dump);
    NAOS_CHECK_STR_EQ(g_dump, "(doc)");
}

NAOS_TEST(input_at_the_size_limit_succeeds)
{
    g_config.max_input_size = 4;
    NAOS_REQUIRE_INT_EQ(amark_parse(S("abcd"), &g_config, &g_doc),
                        AMARK_ERROR_NONE);
    check_tree(g_doc);
    dump(g_doc, g_dump);
    NAOS_CHECK_STR_EQ(g_dump, "(doc (p))");
}

NAOS_TEST(size_limit_of_zero_is_no_limit)
{
    g_config.max_input_size = 0;
    NAOS_REQUIRE_INT_EQ(amark_parse(S("abcd"), &g_config, &g_doc),
                        AMARK_ERROR_NONE);
    check_tree(g_doc);
}

NAOS_TEST(null_config_uses_the_defaults)
{
    static const char content[] = "a\n\nb";
    amark_document_t *doc = NULL;

    NAOS_REQUIRE_INT_EQ(amark_parse(S(content), NULL, &doc),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_NOT_NULL(doc);
    check_tree(doc);
    dump(doc, g_dump);
    NAOS_CHECK_STR_EQ(g_dump, "(doc (p) (p))");
    amark_document_destroy(doc);
}

/* ---------------------------------------------------------------------- */
/* UTF-8 */

NAOS_TEST(invalid_utf8_outside_the_range_is_not_checked)
{
    g_config.start = 1;
    g_config.end = 4;
    NAOS_REQUIRE_INT_EQ(amark_parse(S("\xff" "abc" "\xc3"), &g_config, &g_doc),
                        AMARK_ERROR_NONE);
    check_tree(g_doc);
    check_node(amark_document_root(g_doc), AMARK_KIND_DOCUMENT, 1, 4, 1, 1);
}

NAOS_TEST(invalid_utf8_at_the_edge_of_the_range_fails_in_strict_mode)
{
    g_config.start = 1;
    g_config.end = 5;
    check_fails_without_allocating(S("\xff" "abc" "\xc3"), &g_config,
                                   AMARK_ERROR_INVALID_INPUT);
}

NAOS_TEST(invalid_utf8_in_replace_mode_succeeds)
{
    static const char content[] = "a\xff" "b\n\n\xc3(";
    amark_node_id_t first;
    amark_node_id_t second;

    g_config.utf8_mode = AMARK_UTF8_REPLACE;
    NAOS_REQUIRE_INT_EQ(amark_parse(S(content), &g_config, &g_doc),
                        AMARK_ERROR_NONE);
    check_tree(g_doc);
    dump(g_doc, g_dump);
    NAOS_CHECK_STR_EQ(g_dump, "(doc (p) (p))");
    /* The ranges are offsets in the content as given: no copy was made. */
    check_node(amark_document_root(g_doc), AMARK_KIND_DOCUMENT, 0, 7, 1, 3);
    first = amark_node_first_child(g_doc, amark_document_root(g_doc));
    check_node(first, AMARK_KIND_PARAGRAPH, 0, 3, 1, 1);
    second = amark_node_next_sibling(g_doc, first);
    check_node(second, AMARK_KIND_PARAGRAPH, 5, 7, 3, 3);
}

NAOS_TEST(valid_utf8_gives_the_same_tree_in_both_modes)
{
    static const char content[] =
        "caf\xc3\xa9\n\xe6\x97\xa5\xe6\x9c\xac\n\n\xf0\x9f\x98\x80 x\n";
    size_t count;
    amark_node_id_t node;

    g_config.utf8_mode = AMARK_UTF8_STRICT;
    NAOS_REQUIRE_INT_EQ(amark_parse(S(content), &g_config, &g_doc),
                        AMARK_ERROR_NONE);
    g_config.utf8_mode = AMARK_UTF8_REPLACE;
    NAOS_REQUIRE_INT_EQ(amark_parse(S(content), &g_config, &g_other),
                        AMARK_ERROR_NONE);
    check_tree(g_doc);
    check_tree(g_other);

    dump(g_doc, g_dump);
    dump(g_other, g_other_dump);
    NAOS_CHECK_STR_EQ(g_dump, "(doc (p) (p))");
    NAOS_CHECK_STR_EQ(g_other_dump, g_dump);

    count = amark_document_node_count(g_doc);
    NAOS_REQUIRE_UINT_EQ(amark_document_node_count(g_other), count);
    for (node = 1; node < count; node++) {
        amark_range_t a;
        amark_range_t b;

        NAOS_CHECK_UINT_EQ(amark_node_kind(g_other, node),
                           amark_node_kind(g_doc, node));
        NAOS_REQUIRE_TRUE(amark_node_range(g_doc, node, &a));
        NAOS_REQUIRE_TRUE(amark_node_range(g_other, node, &b));
        NAOS_CHECK_UINT_EQ(b.start, a.start);
        NAOS_CHECK_UINT_EQ(b.end, a.end);
    }
}

/* ---------------------------------------------------------------------- */
/* The range, the file name and the allocator */

NAOS_TEST(range_not_starting_at_zero_counts_from_the_content)
{
    /* Line 1 "skip", line 2 "foo" (5-7), line 3 "bar" (9-11), line 4 empty,
     * line 5 "baz" (14-16), line 6 "tail". The range is [5, 17). */
    static const char content[] = "skip\nfoo\nbar\n\nbaz\ntail";
    amark_node_id_t first;
    amark_node_id_t second;

    g_config.start = 5;
    g_config.end = 17;
    NAOS_REQUIRE_INT_EQ(amark_parse(S(content), &g_config, &g_doc),
                        AMARK_ERROR_NONE);
    check_tree(g_doc);
    dump(g_doc, g_dump);
    NAOS_CHECK_STR_EQ(g_dump, "(doc (p) (p))");
    check_node(amark_document_root(g_doc), AMARK_KIND_DOCUMENT, 5, 17, 2, 5);
    first = amark_node_first_child(g_doc, amark_document_root(g_doc));
    check_node(first, AMARK_KIND_PARAGRAPH, 5, 12, 2, 3);
    second = amark_node_next_sibling(g_doc, first);
    check_node(second, AMARK_KIND_PARAGRAPH, 14, 17, 5, 5);
    NAOS_CHECK_UINT_EQ(amark_node_next_sibling(g_doc, second),
                       AMARK_NODE_NONE);
}

NAOS_TEST(file_name_reaches_the_diagnostics)
{
    char filename[] = "notes.md";
    amark_diagnostic_t diagnostic;

    g_config.filename = filename;
    NAOS_REQUIRE_INT_EQ(amark_parse(S("a"), &g_config, &g_doc),
                        AMARK_ERROR_NONE);
    check_tree(g_doc);
    /* The block parser adds no diagnostics; add one to read the name. */
    NAOS_CHECK_UINT_EQ(amark_document_diagnostic_count(g_doc), 0);
    NAOS_REQUIRE_INT_EQ(amark_document_add_diagnostic(
                            g_doc, 0, AMARK_SEVERITY_WARNING, "m"),
                        AMARK_ERROR_NONE);
    /* The document holds a copy of the name. */
    filename[0] = 'X';
    NAOS_REQUIRE_TRUE(amark_document_diagnostic(g_doc, 0, &diagnostic));
    NAOS_REQUIRE_NOT_NULL(diagnostic.filename);
    NAOS_CHECK_STR_EQ(diagnostic.filename, "notes.md");
}

NAOS_TEST(no_file_name_by_default)
{
    amark_diagnostic_t diagnostic;

    NAOS_REQUIRE_INT_EQ(amark_parse(S("a"), &g_config, &g_doc),
                        AMARK_ERROR_NONE);
    check_tree(g_doc);
    NAOS_REQUIRE_INT_EQ(amark_document_add_diagnostic(
                            g_doc, 0, AMARK_SEVERITY_WARNING, "m"),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_TRUE(amark_document_diagnostic(g_doc, 0, &diagnostic));
    NAOS_CHECK_NULL(diagnostic.filename);
}

NAOS_TEST(only_the_configured_allocator_is_used)
{
    NAOS_REQUIRE_INT_EQ(amark_parse(S("a\nb\n\nc"), &g_config, &g_doc),
                        AMARK_ERROR_NONE);
    check_tree(g_doc);
    /* Every block the document holds came from the counting allocator, and
     * destroying the document returns every one of them. */
    NAOS_CHECK_UINT_GT(g_state.malloc_calls, 0);
    NAOS_CHECK_UINT_GT(g_state.live, 0);
    amark_document_destroy(g_doc);
    g_doc = NULL;
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
    NAOS_CHECK_UINT_EQ(g_state.free_calls, g_state.malloc_calls);
}

NAOS_TEST(scratch_arena_is_released_on_success)
{
    static const char content[] = "a\nb\n\nc";
    amark_resolved_config_t resolved;
    amark_test_alloc_t direct;
    size_t document_live;

    /* The blocks the document alone holds: the same steps done by hand with
     * a second counting allocator, the scratch arena released before
     * counting. */
    amark_test_alloc_init(&direct);
    g_config.allocator = amark_test_allocator(&direct);
    NAOS_REQUIRE_INT_EQ(amark_config_resolve(&g_config, sizeof(content) - 1,
                                             &resolved),
                        AMARK_ERROR_NONE);
    amark_arena_deinit(&g_scratch);
    amark_arena_init(&g_scratch, &resolved.allocator);
    NAOS_REQUIRE_INT_EQ(amark_document_create(
                            &resolved.allocator,
                            (const unsigned char *)content,
                            sizeof(content) - 1, resolved.start, resolved.end,
                            NULL, &g_other),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_parse_data_create(g_other), AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_block_parse(g_other, &resolved, &g_scratch),
                        AMARK_ERROR_NONE);
    check_tree(g_other);
    NAOS_REQUIRE_NOT_NULL(g_scratch.current);
    amark_arena_deinit(&g_scratch);
    document_live = direct.live;
    amark_document_destroy(g_other);
    g_other = NULL;
    NAOS_CHECK_UINT_EQ(direct.live, 0);

    /* amark_parse leaves exactly the document's blocks. */
    g_config.allocator = amark_test_allocator(&g_state);
    amark_arena_init(&g_scratch, &g_config.allocator);
    NAOS_REQUIRE_INT_EQ(amark_parse(S(content), &g_config, &g_doc),
                        AMARK_ERROR_NONE);
    check_tree(g_doc);
    NAOS_CHECK_UINT_EQ(g_state.live, document_live);
}

NAOS_TEST(same_input_gives_the_same_tree)
{
    static const char content[] = "one\ntwo\n\n\nthree\n  four\n\nfive";
    size_t count;
    amark_node_id_t node;

    NAOS_REQUIRE_INT_EQ(amark_parse(S(content), &g_config, &g_doc),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_parse(S(content), &g_config, &g_other),
                        AMARK_ERROR_NONE);
    check_tree(g_doc);
    check_tree(g_other);
    dump(g_doc, g_dump);
    dump(g_other, g_other_dump);
    NAOS_CHECK_STR_EQ(g_dump, "(doc (p) (p) (p))");
    NAOS_CHECK_STR_EQ(g_other_dump, g_dump);

    count = amark_document_node_count(g_doc);
    NAOS_REQUIRE_UINT_EQ(amark_document_node_count(g_other), count);
    for (node = 1; node < count; node++) {
        amark_range_t a;
        amark_range_t b;

        NAOS_CHECK_UINT_EQ(amark_node_kind(g_other, node),
                           amark_node_kind(g_doc, node));
        NAOS_CHECK_UINT_EQ(amark_node_parent(g_other, node),
                           amark_node_parent(g_doc, node));
        NAOS_CHECK_UINT_EQ(amark_node_next_sibling(g_other, node),
                           amark_node_next_sibling(g_doc, node));
        NAOS_REQUIRE_TRUE(amark_node_range(g_doc, node, &a));
        NAOS_REQUIRE_TRUE(amark_node_range(g_other, node, &b));
        NAOS_CHECK_UINT_EQ(b.start, a.start);
        NAOS_CHECK_UINT_EQ(b.end, a.end);
        NAOS_CHECK_UINT_EQ(amark_node_start_line(g_other, node),
                           amark_node_start_line(g_doc, node));
        NAOS_CHECK_UINT_EQ(amark_node_end_line(g_other, node),
                           amark_node_end_line(g_doc, node));
    }
}

/* A valid dialect of one extension with a name only: it adds nothing. */
static const amark_dialect_extension_t g_name_only = {.name = "name-only"};
static const amark_dialect_extension_t *const g_name_only_list[] = {
    &g_name_only,
};
static const amark_dialect_t g_dialect = {
    .struct_size = sizeof(amark_dialect_t),
    .abi = AMARK_DIALECT_ABI,
    .extensions = g_name_only_list,
    .extension_count = 1,
};

NAOS_TEST(dialect_does_not_change_the_result)
{
    /*
     * In this spec the dialect member is passed through and never read
     * (Requirement 17.4): a non-NULL dialect gives the same tree, line
     * records and inline contents as NULL. The dialect is a valid one that
     * adds nothing, as the configuration check reads its definition.
     */
    static const char content[] =
        "# Title\n\n> quote\nlazy\n\n- a\n\n  b\n-\tc\n\n"
        "```js\ncode\n```\n\n    indented\n\n[x]: /u\nSetext\n---\n"
        "<div>\nhtml\n</div>\n";
    amark_line_bounds_t bounds;
    size_t count;
    size_t line;
    size_t index;
    amark_node_id_t node;

    NAOS_REQUIRE_INT_EQ(amark_parse(S(content), &g_config, &g_doc),
                        AMARK_ERROR_NONE);
    g_config.dialect = &g_dialect;
    NAOS_REQUIRE_INT_EQ(amark_parse(S(content), &g_config, &g_other),
                        AMARK_ERROR_NONE);
    check_tree(g_doc);
    check_tree(g_other);
    dump(g_doc, g_dump);
    dump(g_other, g_other_dump);
    NAOS_CHECK_STR_EQ(g_dump,
                      "(doc (h 1 atx) (bq (p)) (ul - loose (li (p) (p))"
                      " (li (p))) (code fenced ` 3 info) (code indented)"
                      " (def) (h 2 setext) (html))");
    NAOS_CHECK_STR_EQ(g_other_dump, g_dump);

    count = amark_document_node_count(g_doc);
    NAOS_REQUIRE_UINT_EQ(amark_document_node_count(g_other), count);
    for (node = 1; node < count; node++) {
        amark_range_t a;
        amark_range_t b;

        NAOS_CHECK_UINT_EQ(amark_node_kind(g_other, node),
                           amark_node_kind(g_doc, node));
        NAOS_CHECK_UINT_EQ(amark_node_parent(g_other, node),
                           amark_node_parent(g_doc, node));
        NAOS_REQUIRE_TRUE(amark_node_range(g_doc, node, &a));
        NAOS_REQUIRE_TRUE(amark_node_range(g_other, node, &b));
        NAOS_CHECK_UINT_EQ(b.start, a.start);
        NAOS_CHECK_UINT_EQ(b.end, a.end);
        NAOS_CHECK_UINT_EQ(amark_node_start_line(g_other, node),
                           amark_node_start_line(g_doc, node));
        NAOS_CHECK_UINT_EQ(amark_node_end_line(g_other, node),
                           amark_node_end_line(g_doc, node));
    }

    for (line = 1; amark_line_bounds(g_doc, line, &bounds); line++) {
        amark_line_record_t a;
        amark_line_record_t b;

        NAOS_REQUIRE_TRUE(amark_line_get(g_doc, line, &a));
        NAOS_REQUIRE_TRUE(amark_line_get(g_other, line, &b));
        NAOS_CHECK_UINT_EQ(b.content_start, a.content_start);
        NAOS_CHECK_UINT_EQ(b.pad, a.pad);
    }
    /* 22 lines, the last ending with a line ending. */
    NAOS_CHECK_UINT_EQ(line, 23);
    NAOS_CHECK_FALSE(amark_line_bounds(g_other, line, &bounds));

    count = amark_inline_content_count(g_doc);
    /* The heading, the quote's paragraph, three in the list, the setext
     * heading. */
    NAOS_CHECK_UINT_EQ(count, 6);
    NAOS_REQUIRE_UINT_EQ(amark_inline_content_count(g_other), count);
    for (index = 0; index < count; index++) {
        amark_inline_content_t a;
        amark_inline_content_t b;

        NAOS_REQUIRE_TRUE(amark_inline_content_at(g_doc, index, &a));
        NAOS_REQUIRE_TRUE(amark_inline_content_at(g_other, index, &b));
        NAOS_CHECK_UINT_EQ(b.node, a.node);
        NAOS_CHECK_UINT_EQ(b.start, a.start);
        NAOS_CHECK_UINT_EQ(b.end, a.end);
    }
}

NAOS_SUITE(parse_checks,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(null_out_is_invalid_input),
    NAOS_ADD_TEST(null_out_comes_before_every_other_check),
    NAOS_ADD_TEST(null_content_with_a_length_is_invalid_input),
    NAOS_ADD_TEST(null_content_comes_before_the_config),
    NAOS_ADD_TEST(invalid_config_allocates_nothing),
    NAOS_ADD_TEST(range_past_the_content_is_invalid_config),
    NAOS_ADD_TEST(invalid_utf8_mode_is_invalid_config),
    NAOS_ADD_TEST(config_comes_before_the_size_limit_and_utf8),
    NAOS_ADD_TEST(input_over_the_size_limit_allocates_nothing),
    NAOS_ADD_TEST(size_limit_counts_the_whole_content_not_the_range),
    NAOS_ADD_TEST(size_limit_comes_before_utf8),
    NAOS_ADD_TEST(invalid_utf8_in_strict_mode_allocates_nothing),
    NAOS_ADD_TEST(allocation_failure_is_nomem),
    NAOS_ADD_TEST(node_count_limit_fails_and_releases_everything),
    NAOS_ADD_TEST(allocation_failure_at_every_call)
)

NAOS_SUITE(parse_input,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(null_content_of_length_zero),
    NAOS_ADD_TEST(empty_range_inside_the_content),
    NAOS_ADD_TEST(input_at_the_size_limit_succeeds),
    NAOS_ADD_TEST(size_limit_of_zero_is_no_limit),
    NAOS_ADD_TEST(null_config_uses_the_defaults)
)

NAOS_SUITE(parse_utf8,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(invalid_utf8_outside_the_range_is_not_checked),
    NAOS_ADD_TEST(invalid_utf8_at_the_edge_of_the_range_fails_in_strict_mode),
    NAOS_ADD_TEST(invalid_utf8_in_replace_mode_succeeds),
    NAOS_ADD_TEST(valid_utf8_gives_the_same_tree_in_both_modes)
)

NAOS_SUITE(parse_document,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(range_not_starting_at_zero_counts_from_the_content),
    NAOS_ADD_TEST(file_name_reaches_the_diagnostics),
    NAOS_ADD_TEST(no_file_name_by_default),
    NAOS_ADD_TEST(only_the_configured_allocator_is_used),
    NAOS_ADD_TEST(scratch_arena_is_released_on_success),
    NAOS_ADD_TEST(same_input_gives_the_same_tree),
    NAOS_ADD_TEST(dialect_does_not_change_the_result)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(parse_checks),
    NAOS_ADD_SUITE(parse_input),
    NAOS_ADD_SUITE(parse_utf8),
    NAOS_ADD_SUITE(parse_document)
)
