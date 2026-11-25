/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of the limits and failures of a parse with a dialect, and of a
 * parse without one.
 *
 * The allocation baseline: the number of calls to the allocator made by a
 * parse without a dialect, measured on fixed inputs before the dialect rules
 * were added to the parsers and written here as constants (Requirements 13.2
 * and 15.7). A parse without a dialect must keep making exactly as many
 * calls. Each input is small enough to fit with a wide margin in the first
 * block of every arena, so a small growth of a structure of the parsers does
 * not change the count.
 */

#include "document.h"
#include "test_alloc.h"
#include "tree_check.h"
#include "tree_dump.h"

#include <amark/amark.h>

#include <naos/naos.h>

#include <stddef.h>

/*
 * Fixture. NAOS_REQUIRE_* leaves a test with longjmp, so the document a test
 * makes is kept in a static variable, and teardown destroys it and checks
 * that no block is left and that every call received its context value.
 */
static amark_test_alloc_t g_state;
static amark_config_t g_config;
static amark_document_t *g_doc;

NAOS_SETUP(counting)
{
    amark_test_alloc_init(&g_state);
    amark_config_init(&g_config);
    g_config.allocator = amark_test_allocator(&g_state);
    g_doc = NULL;
}

NAOS_TEARDOWN(counting)
{
    amark_document_destroy(g_doc);
    g_doc = NULL;
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
    NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
}

#define S(s) (s), (sizeof(s) - 1)

/* ---------------------------------------------------------------------- */
/* The allocation baseline of a parse without a dialect */

/*
 * The calls to the allocator made by a parse without a dialect, measured
 * before the dialect rules were added to the parsers. Do not change these
 * values to make a test pass: a different count means that a parse without a
 * dialect now allocates differently (Requirement 13.2).
 *
 * As measured: the document, the first block of the document arena and the
 * first block of the scratch arena of the block parser; with inline content,
 * also the first block of the scratch arena of the inline parser.
 */
#define BASELINE_EMPTY ((size_t)3)
#define BASELINE_PARAGRAPH ((size_t)4)
#define BASELINE_NESTED ((size_t)4)

/* One paragraph of one line. */
static const char k_paragraph[] = "Hello, world.\n";

/*
 * A list nested in a list item, emphasis, strong emphasis and a link: 14
 * nodes. With up to 16 nodes the node array of the document stays in the
 * first block of the document arena (measured: about 2.5 KB of its 4 KB
 * used; the scratch arenas of the block and inline parsers use less), so a
 * small growth of a structure does not move any arena to a second block.
 */
static const char k_nested[] = "- *one* [two](/url)\n"
                               "  - **three**\n";

#define DUMP_SIZE 512

/*
 * Parses content without a dialect, requires success and a tree that passes
 * the check, and returns the number of calls made to the allocator.
 */
static size_t count_parse(const char *content, size_t length)
{
    amark_tree_check_result_t check;

    amark_test_alloc_init(&g_state);
    NAOS_REQUIRE_NULL(g_config.dialect);
    NAOS_REQUIRE_INT_EQ(amark_parse(content, length, &g_config, &g_doc),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_NOT_NULL(g_doc);
    check = amark_tree_check(g_doc);
    NAOS_CHECK_INT_EQ(check.violation, AMARK_TREE_VIOLATION_NONE);
    return g_state.malloc_calls;
}

/* Requires the tree of g_doc to be written as want. */
static void check_dump(const char *want)
{
    char buffer[DUMP_SIZE];
    size_t length = amark_test_dump_tree(g_doc, buffer, sizeof buffer);

    NAOS_REQUIRE_UINT_LT(length, sizeof buffer);
    NAOS_CHECK_STR_EQ(buffer, want);
}

NAOS_TEST(empty_input_keeps_the_baseline)
{
    NAOS_CHECK_UINT_EQ(count_parse(S("")), BASELINE_EMPTY);
    check_dump("(doc)");
}

NAOS_TEST(one_paragraph_keeps_the_baseline)
{
    NAOS_CHECK_UINT_EQ(count_parse(S(k_paragraph)), BASELINE_PARAGRAPH);
    check_dump("(doc (p (text \"Hello, world.\")))");
}

NAOS_TEST(nested_lists_emphasis_and_links_keep_the_baseline)
{
    NAOS_CHECK_UINT_EQ(count_parse(S(k_nested)), BASELINE_NESTED);
    /* Guards the input: it must keep exercising what the name says. */
    check_dump("(doc (ul - tight (li (p (em (text \"one\")) (text \" \")"
               " (link inline dest=\"/url\" (text \"two\")))"
               " (ul - tight (li (p (strong (text \"three\"))))))))");
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 14);
}

NAOS_SUITE(allocation_baseline,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(empty_input_keeps_the_baseline),
    NAOS_ADD_TEST(one_paragraph_keeps_the_baseline),
    NAOS_ADD_TEST(nested_lists_emphasis_and_links_keep_the_baseline)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(allocation_baseline)
)
