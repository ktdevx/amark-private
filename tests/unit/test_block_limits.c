/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of the resource limits and of large inputs, through the public
 * entry point amark_parse and amark_document_destroy only: the nesting depth
 * and node count limits at and one past their values, with block quotes,
 * lists and both; block quotes and lists nested 100,000 deep with no limits;
 * and inputs that make a careless parser take time quadratic in their length
 * (runs of blank lines and of ">" lines after deep lists, many definitions,
 * many lazy continuation lines, "- - - ... a", the continuation lines of a
 * deep list, and runs of "["). Time is not measured: these inputs end well
 * within the time limit of CI only if the parse is close to linear.
 *
 * The inputs are built in memory with the counting allocator, every parsed
 * document goes through amark_tree_check, and teardown checks that no block
 * is left.
 */

#include "document.h"
#include "test_alloc.h"
#include "tree_check.h"
#include "tree_dump.h"

#include <amark/amark.h>

#include <naos/naos.h>

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* ---------------------------------------------------------------------- */
/* Fixture                                                                */
/* ---------------------------------------------------------------------- */

/*
 * NAOS_REQUIRE_* leaves a test with longjmp, so the document and the input a
 * test makes are kept in static variables, and teardown releases them and
 * checks that no block is left and that every call received its context
 * value.
 */
static amark_test_alloc_t g_state;
static amark_allocator_t g_allocator;
static amark_document_t *g_doc;

/* The input of a test, allocated with the counting allocator. */
static char *g_input;
static size_t g_length;
static size_t g_capacity;

/* A value of *out that no successful or failed parse leaves there. */
static amark_document_t *const k_sentinel = (amark_document_t *)&g_state;

NAOS_SETUP(counting)
{
    amark_test_alloc_init(&g_state);
    g_allocator = amark_test_allocator(&g_state);
    g_doc = NULL;
    g_input = NULL;
    g_length = 0;
    g_capacity = 0;
}

NAOS_TEARDOWN(counting)
{
    g_state.fail_at = 0;
    if (g_doc != k_sentinel) {
        amark_document_destroy(g_doc);
    }
    g_doc = NULL;
    if (g_input != NULL) {
        g_allocator.free_fn(g_allocator.ctx, g_input);
    }
    g_input = NULL;
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
    NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
}

/* ---------------------------------------------------------------------- */
/* Building the input                                                     */
/* ---------------------------------------------------------------------- */

static void reset_doc(void);

/*
 * Starts a new input of at most capacity bytes, releasing the old one and
 * the document made from it.
 */
static void input_begin(size_t capacity)
{
    reset_doc();
    if (g_input != NULL) {
        g_allocator.free_fn(g_allocator.ctx, g_input);
        g_input = NULL;
    }
    g_input = (char *)g_allocator.malloc_fn(g_allocator.ctx, capacity);
    NAOS_REQUIRE_NOT_NULL(g_input);
    g_length = 0;
    g_capacity = capacity;
}

/* Appends the length bytes at bytes, count times. */
static void input_repeat(const char *bytes, size_t length, size_t count)
{
    size_t i;

    NAOS_REQUIRE_TRUE(count == 0 ||
                      length <= (g_capacity - g_length) / count);
    for (i = 0; i < count; i++) {
        memcpy(g_input + g_length, bytes, length);
        g_length += length;
    }
}

#define REPEAT(s, count) input_repeat((s), sizeof(s) - 1, (count))
#define APPEND(s) input_repeat((s), sizeof(s) - 1, 1)

/* Appends the decimal digits of value. */
static void input_number(size_t value)
{
    char digits[24];
    size_t n = 0;

    do {
        digits[sizeof(digits) - 1 - n] = (char)('0' + value % 10);
        value /= 10;
        n++;
    } while (value > 0);
    input_repeat(digits + sizeof(digits) - n, n, 1);
}

/* ---------------------------------------------------------------------- */
/* Parsing                                                                */
/* ---------------------------------------------------------------------- */

/*
 * Destroys g_doc so that the input can be parsed again or replaced, and
 * checks that only the input is left.
 */
static void reset_doc(void)
{
    size_t input_blocks = g_input != NULL ? 1 : 0;

    if (g_doc != k_sentinel) {
        amark_document_destroy(g_doc);
    }
    g_doc = NULL;
    NAOS_CHECK_UINT_EQ(g_state.live, input_blocks);
}

/*
 * Parses the input with the two limits into g_doc through amark_parse,
 * destroying the document of an earlier parse first.
 */
static amark_error_t parse_input(size_t max_depth, size_t max_nodes)
{
    amark_config_t config;

    reset_doc();
    amark_config_init(&config);
    config.allocator = g_allocator;
    config.max_nesting_depth = max_depth;
    config.max_node_count = max_nodes;
    g_doc = k_sentinel;
    return amark_parse(g_input, g_length, &config, &g_doc);
}

/*
 * Requires the parse with the two limits to succeed with a tree that passes
 * the tree check and has nodes nodes (the root included).
 */
static void parse_ok(size_t max_depth, size_t max_nodes, size_t nodes)
{
    amark_tree_check_result_t check;

    NAOS_REQUIRE_INT_EQ(parse_input(max_depth, max_nodes), AMARK_ERROR_NONE);
    NAOS_REQUIRE_TRUE(g_doc != NULL && g_doc != k_sentinel);
    check = amark_tree_check(g_doc);
    NAOS_CHECK_INT_EQ(check.violation, AMARK_TREE_VIOLATION_NONE);
    NAOS_CHECK_UINT_EQ(check.node, AMARK_NODE_NONE);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), nodes);
}

/*
 * Requires the parse with the two limits to fail with
 * AMARK_ERROR_LIMIT_EXCEEDED, store NULL in *out and leave no block but the
 * input.
 */
static void parse_exceeds(size_t max_depth, size_t max_nodes)
{
    NAOS_CHECK_INT_EQ(parse_input(max_depth, max_nodes),
                      AMARK_ERROR_LIMIT_EXCEEDED);
    NAOS_CHECK_NULL(g_doc);
    NAOS_CHECK_UINT_EQ(g_state.live, 1);
}

/* Requires the block structure of g_doc to be want. */
static void check_dump(const char *want)
{
    char buffer[512];
    size_t length = amark_test_dump_blocks(g_doc, buffer, sizeof(buffer));

    NAOS_REQUIRE_UINT_LT(length, sizeof(buffer));
    NAOS_CHECK_STR_EQ(buffer, want);
}

/*
 * Requires the chain of first children from the root to be depth containers
 * whose kinds repeat the pattern of period kinds, followed by a node of kind
 * last. Below the root's first child, each node of the chain is an only
 * child.
 */
static void check_chain(const amark_kind_t *pattern, size_t period,
                        size_t depth, amark_kind_t last)
{
    amark_node_id_t node = amark_document_root(g_doc);
    size_t i;

    for (i = 0; i < depth; i++) {
        node = amark_node_first_child(g_doc, node);
        NAOS_REQUIRE_UINT_NE(node, AMARK_NODE_NONE);
        NAOS_REQUIRE_UINT_EQ(amark_node_kind(g_doc, node),
                             pattern[i % period]);
        if (i > 0) {
            NAOS_REQUIRE_UINT_EQ(amark_node_next_sibling(g_doc, node),
                                 AMARK_NODE_NONE);
        }
    }
    node = amark_node_first_child(g_doc, node);
    NAOS_REQUIRE_UINT_NE(node, AMARK_NODE_NONE);
    NAOS_CHECK_UINT_EQ(amark_node_kind(g_doc, node), last);
}

/* Returns the nth (from 0) child of the root; it must exist. */
static amark_node_id_t root_child(size_t nth)
{
    amark_node_id_t child = amark_node_first_child(g_doc,
                                                   amark_document_root(g_doc));

    while (nth > 0) {
        NAOS_REQUIRE_UINT_NE(child, AMARK_NODE_NONE);
        child = amark_node_next_sibling(g_doc, child);
        nth--;
    }
    NAOS_REQUIRE_UINT_NE(child, AMARK_NODE_NONE);
    return child;
}

static const amark_kind_t k_quotes[] = {AMARK_KIND_BLOCK_QUOTE};
static const amark_kind_t k_lists[] = {AMARK_KIND_LIST, AMARK_KIND_LIST_ITEM};

/* ---------------------------------------------------------------------- */
/* The nesting depth and node count limits                                */
/* ---------------------------------------------------------------------- */

/* Nesting: the root is at depth 0 and each container adds 1. */
#define NEST 40

NAOS_TEST(depth_limit_with_block_quotes)
{
    /* NEST quotes, the paragraph at depth NEST + 1. */
    input_begin(NEST + 1);
    REPEAT(">", NEST);
    APPEND("a");
    parse_exceeds(NEST, 0);
    parse_ok(NEST + 1, 0, NEST + 2);
    check_chain(k_quotes, 1, NEST, AMARK_KIND_PARAGRAPH);
    reset_doc();

    /* Without the paragraph, depth NEST is enough. */
    input_begin(NEST);
    REPEAT("> ", NEST / 2);
    parse_exceeds(NEST / 2 - 1, 0);
    parse_ok(NEST / 2, 0, NEST / 2 + 1);
}

NAOS_TEST(depth_limit_with_lists)
{
    /* NEST lists and NEST items, the paragraph at depth 2 NEST + 1. */
    input_begin(2 * NEST + 1);
    REPEAT("- ", NEST);
    APPEND("a");
    parse_exceeds(2 * NEST, 0);
    parse_ok(2 * NEST + 1, 0, 2 * NEST + 2);
    check_chain(k_lists, 2, 2 * NEST, AMARK_KIND_PARAGRAPH);
    reset_doc();

    /* Ordered lists count the same. */
    input_begin(3 * NEST + 1);
    REPEAT("1. ", NEST);
    APPEND("a");
    parse_exceeds(2 * NEST, 0);
    parse_ok(2 * NEST + 1, 0, 2 * NEST + 2);
}

NAOS_TEST(depth_limit_with_block_quotes_and_lists)
{
    static const amark_kind_t mixed[] = {
        AMARK_KIND_BLOCK_QUOTE, AMARK_KIND_LIST, AMARK_KIND_LIST_ITEM};

    /* "> - " NEST times: 3 NEST containers, the paragraph below them. */
    input_begin(4 * NEST + 1);
    REPEAT("> - ", NEST);
    APPEND("a");
    parse_exceeds(3 * NEST, 0);
    parse_ok(3 * NEST + 1, 0, 3 * NEST + 2);
    check_chain(mixed, 3, 3 * NEST, AMARK_KIND_PARAGRAPH);
    reset_doc();

    /* A short one, read whole. */
    input_begin(16);
    APPEND("> - > 1. a\n");
    parse_exceeds(6, 0);
    parse_ok(7, 0, 8);
    check_dump("(doc (bq (ul - tight (li (bq (ol 1 . tight (li (p))))))))");
}

NAOS_TEST(depth_limit_counts_the_deepest_node_only)
{
    /* A deep branch beside shallow ones: only the deepest counts. */
    input_begin(64);
    APPEND("a\n\n> > > b\n\n- c\n\n> d\n");
    parse_exceeds(3, 0);
    parse_ok(4, 0, 11);
    check_dump("(doc (p) (bq (bq (bq (p)))) (ul - tight (li (p))) (bq (p)))");
    reset_doc();

    /* A leaf counts the same: a code block in two quotes is at depth 3. */
    input_begin(32);
    APPEND("> >     code\n");
    parse_exceeds(2, 0);
    parse_ok(3, 0, 4);
    check_dump("(doc (bq (bq (code indented))))");
}

NAOS_TEST(node_count_limit_with_block_quotes)
{
    /* The root, NEST quotes and the paragraph. */
    input_begin(NEST + 1);
    REPEAT(">", NEST);
    APPEND("a");
    parse_exceeds(0, NEST + 1);
    parse_ok(0, NEST + 2, NEST + 2);

    /* Siblings count the same as the nested. */
    input_begin(4 * NEST);
    REPEAT("> a\n\n", NEST / 2);
    parse_exceeds(0, NEST);
    parse_ok(0, NEST + 1, NEST + 1);
}

NAOS_TEST(node_count_limit_with_lists)
{
    /* The root, NEST lists, NEST items and the paragraph. */
    input_begin(2 * NEST + 1);
    REPEAT("- ", NEST);
    APPEND("a");
    parse_exceeds(0, 2 * NEST + 1);
    parse_ok(0, 2 * NEST + 2, 2 * NEST + 2);

    /* One list of NEST items, each with a paragraph. */
    input_begin(4 * NEST);
    REPEAT("- a\n", NEST);
    parse_exceeds(0, 2 * NEST + 1);
    parse_ok(0, 2 * NEST + 2, 2 * NEST + 2);
    NAOS_CHECK_UINT_EQ(amark_node_next_sibling(g_doc, root_child(0)),
                       AMARK_NODE_NONE);
}

NAOS_TEST(node_count_limit_with_block_quotes_and_lists)
{
    /* The root, 3 NEST containers and the paragraph. */
    input_begin(4 * NEST + 1);
    REPEAT("> - ", NEST);
    APPEND("a");
    parse_exceeds(0, 3 * NEST + 1);
    parse_ok(0, 3 * NEST + 2, 3 * NEST + 2);
}

NAOS_TEST(both_limits_exactly_met)
{
    input_begin(4 * NEST + 1);
    REPEAT("> - ", NEST);
    APPEND("a");
    parse_ok(3 * NEST + 1, 3 * NEST + 2, 3 * NEST + 2);
    reset_doc();
    parse_exceeds(3 * NEST, 3 * NEST + 2);
    parse_exceeds(3 * NEST + 1, 3 * NEST + 1);
}

NAOS_TEST(node_count_limit_counts_definitions)
{
    /* The definitions are nodes; the paragraph they came from is not. */
    input_begin(32);
    APPEND("[a]: /u\n[b]: /v\nc\n");
    parse_exceeds(0, 3);
    parse_ok(0, 4, 4);
    check_dump("(doc (def) (def) (p))");
}

/* ---------------------------------------------------------------------- */
/* Nesting 100,000 deep                                                   */
/* ---------------------------------------------------------------------- */

#define DEEP 100000

NAOS_TEST(block_quotes_nested_100000_deep)
{
    input_begin(DEEP + 4);
    REPEAT(">", DEEP);
    APPEND("a\nb");
    parse_ok(0, 0, DEEP + 2);
    check_chain(k_quotes, 1, DEEP, AMARK_KIND_PARAGRAPH);
    reset_doc();

    /* The limits at their bounds, at this depth. */
    parse_exceeds(DEEP, 0);
    parse_ok(DEEP + 1, DEEP + 2, DEEP + 2);
    reset_doc();
    parse_exceeds(0, DEEP + 1);
}

NAOS_TEST(lists_nested_100000_deep)
{
    input_begin(2 * DEEP + 1);
    REPEAT("- ", DEEP);
    APPEND("a");
    parse_ok(0, 0, 2 * DEEP + 2);
    check_chain(k_lists, 2, 2 * DEEP, AMARK_KIND_PARAGRAPH);
    reset_doc();

    parse_exceeds(2 * DEEP, 0);
    parse_ok(2 * DEEP + 1, 2 * DEEP + 2, 2 * DEEP + 2);
    reset_doc();
    parse_exceeds(0, 2 * DEEP + 1);
}

NAOS_TEST(lists_nested_on_their_own_lines)
{
    size_t i;

    /*
     * Each item on its own line, indented under the one before: a line of
     * 2 i spaces and "- a" for i from 0. The line lengths grow with the
     * depth, so this one is only 1,000 deep.
     */
    input_begin(2 * 1000 * 1000 + 3000 + 1);
    for (i = 0; i < 1000; i++) {
        input_repeat("  ", 2, i);
        APPEND("- a\n");
    }
    parse_ok(0, 0, 3 * 1000 + 1);
}

/* ---------------------------------------------------------------------- */
/* Inputs that take quadratic time in a careless parser                   */
/* ---------------------------------------------------------------------- */

/* The depth of the deep lists below, and the length of the runs of lines. */
#define PATH_DEPTH 10000
#define PATH_LINES 40000

NAOS_TEST(blank_lines_after_a_deep_list)
{
    size_t i;

    /*
     * "- " PATH_DEPTH times and "a", then PATH_LINES blank lines and lines of
     * spaces and tabs only, then "b" after the list.
     */
    input_begin(2 * PATH_DEPTH + 2 + PATH_LINES * 4 + 1);
    REPEAT("- ", PATH_DEPTH);
    APPEND("a\n");
    for (i = 0; i < PATH_LINES / 4; i++) {
        APPEND("\n");
        APPEND("  \n");
        APPEND("\t\n");
        APPEND(" \n");
    }
    APPEND("b");
    parse_ok(0, 0, 2 * PATH_DEPTH + 3);
    check_chain(k_lists, 2, 2 * PATH_DEPTH, AMARK_KIND_PARAGRAPH);
    NAOS_CHECK_UINT_EQ(amark_node_kind(g_doc, root_child(1)),
                       AMARK_KIND_PARAGRAPH);
    NAOS_CHECK_UINT_EQ(amark_node_next_sibling(g_doc, root_child(1)),
                       AMARK_NODE_NONE);
    reset_doc();

    /*
     * Lines of spaces only, as long as the indentation of the innermost
     * item's content.
     */
    input_begin(2 * PATH_DEPTH + 2 + PATH_LINES * (2 * PATH_DEPTH + 1) / 1000);
    REPEAT("- ", PATH_DEPTH);
    APPEND("a\n");
    for (i = 0; i < PATH_LINES / 1000; i++) {
        REPEAT("  ", PATH_DEPTH);
        APPEND("\n");
    }
    parse_ok(0, 0, 2 * PATH_DEPTH + 2);
}

NAOS_TEST(marker_lines_after_a_deep_list_in_a_block_quote)
{
    /* "> " and "- " PATH_DEPTH times and "a", then PATH_LINES ">" lines. */
    input_begin(2 + 2 * PATH_DEPTH + 2 + 3 * PATH_LINES + 3);
    APPEND("> ");
    REPEAT("- ", PATH_DEPTH);
    APPEND("a\n");
    REPEAT(">\n", PATH_LINES / 2);
    REPEAT("> \n", PATH_LINES / 2);
    APPEND("> b");
    /* The quote, the lists and items, the paragraph and "b" in the quote. */
    parse_ok(0, 0, 2 * PATH_DEPTH + 4);
    check_chain(k_quotes, 1, 1, AMARK_KIND_LIST);
}

NAOS_TEST(many_definitions)
{
    size_t i;

    /* PATH_LINES definitions with distinct labels, in one paragraph. */
    input_begin(PATH_LINES * 24);
    for (i = 0; i < PATH_LINES; i++) {
        APPEND("[l");
        input_number(i);
        APPEND("]: /u\n");
    }
    parse_ok(0, 0, PATH_LINES + 1);
    reset_doc();

    /* The same label each time. */
    input_begin(PATH_LINES * 8);
    REPEAT("[a]: /u\n", PATH_LINES);
    parse_ok(0, 0, PATH_LINES + 1);
    reset_doc();

    /* Labels in reverse order, separated by blank lines, with titles. */
    input_begin(PATH_LINES * 32);
    for (i = PATH_LINES; i > 0; i--) {
        APPEND("[");
        input_number(i);
        APPEND("]: /u 't'\n\n");
    }
    parse_ok(0, 0, PATH_LINES + 1);
}

NAOS_TEST(many_lazy_continuation_lines)
{
    /* A paragraph in PATH_DEPTH quotes, continued by PATH_LINES lazy lines. */
    input_begin(PATH_DEPTH + 2 + 2 * PATH_LINES);
    REPEAT(">", PATH_DEPTH);
    APPEND("a\n");
    REPEAT("b\n", PATH_LINES);
    parse_ok(0, 0, PATH_DEPTH + 2);
    check_chain(k_quotes, 1, PATH_DEPTH, AMARK_KIND_PARAGRAPH);
    reset_doc();

    /* The same in PATH_DEPTH list items. */
    input_begin(2 * PATH_DEPTH + 2 + 2 * PATH_LINES);
    REPEAT("- ", PATH_DEPTH);
    APPEND("a\n");
    REPEAT("b\n", PATH_LINES);
    parse_ok(0, 0, 2 * PATH_DEPTH + 2);
    check_chain(k_lists, 2, 2 * PATH_DEPTH, AMARK_KIND_PARAGRAPH);
    reset_doc();

    /* Lazy lines that each match some of the quotes. */
    input_begin(PATH_DEPTH + 2 + 5 * PATH_LINES);
    REPEAT(">", PATH_DEPTH);
    APPEND("a\n");
    REPEAT(">>> b\n", PATH_LINES / 2);
    REPEAT("> b\n", PATH_LINES / 2);
    parse_ok(0, 0, PATH_DEPTH + 2);
}

NAOS_TEST(markers_that_almost_make_a_thematic_break)
{
    /*
     * "- - - ... - a": at each item the rest of the line looks like a
     * thematic break until the "a".
     */
    input_begin(2 * DEEP + 1);
    REPEAT("- ", DEEP);
    APPEND("a");
    parse_ok(0, 0, 2 * DEEP + 2);
    reset_doc();

    /* With "*" and with no spaces after the first two. */
    input_begin(2 * DEEP + 1);
    REPEAT("* ", DEEP);
    APPEND("a");
    parse_ok(0, 0, 2 * DEEP + 2);
    reset_doc();
    input_begin(2 * DEEP + 4);
    APPEND("- - ");
    REPEAT("-", 2 * DEEP - 4);
    APPEND("a");
    parse_ok(0, 0, 6);
    check_dump("(doc (ul - tight (li (ul - tight (li (p))))))");
    reset_doc();

    /*
     * PATH_LINES / 40 lines of a hundred markers and "a": one list, each of
     * its items holding 99 nested lists and items and a paragraph.
     */
    input_begin(PATH_LINES / 40 * 203);
    {
        size_t i;

        for (i = 0; i < PATH_LINES / 40; i++) {
            REPEAT("- ", 100);
            APPEND("a\n");
        }
    }
    parse_ok(0, 0, 2 + PATH_LINES / 40 * 200);
}

NAOS_TEST(continuation_lines_of_a_deep_list)
{
    /*
     * A paragraph in PATH_DEPTH / 2 items, continued by lines indented to
     * the content of the innermost item: each such line matches every item.
     */
    input_begin(PATH_DEPTH + 2 + 100 * (PATH_DEPTH + 2));
    REPEAT("- ", PATH_DEPTH / 2);
    APPEND("a\n");
    {
        size_t i;

        for (i = 0; i < 100; i++) {
            REPEAT("  ", PATH_DEPTH / 2);
            APPEND("b\n");
        }
    }
    parse_ok(0, 0, PATH_DEPTH + 2);
    check_chain(k_lists, 2, PATH_DEPTH, AMARK_KIND_PARAGRAPH);
    reset_doc();

    /* The same with tabs: each tab covers two items. */
    input_begin(PATH_DEPTH + 2 + 100 * (PATH_DEPTH / 4 + 2));
    REPEAT("- ", PATH_DEPTH / 2);
    APPEND("a\n");
    {
        size_t i;

        for (i = 0; i < 100; i++) {
            REPEAT("\t", PATH_DEPTH / 4);
            APPEND("b\n");
        }
    }
    parse_ok(0, 0, PATH_DEPTH + 2);
    check_chain(k_lists, 2, PATH_DEPTH, AMARK_KIND_PARAGRAPH);
}

NAOS_TEST(runs_of_open_brackets)
{
    size_t i;

    /* One paragraph of PATH_LINES lines of "[". */
    input_begin(2 * PATH_LINES);
    REPEAT("[\n", PATH_LINES);
    parse_ok(0, 0, 2);
    check_dump("(doc (p))");
    reset_doc();

    /* One line of DEEP "[". */
    input_begin(DEEP);
    REPEAT("[", DEEP);
    parse_ok(0, 0, 2);
    reset_doc();

    /* Labels that never close, one per line. */
    input_begin(3 * PATH_LINES);
    REPEAT("[a\n", PATH_LINES);
    parse_ok(0, 0, 2);
    reset_doc();

    /*
     * Lines of "[a]:": each definition takes the next line as its
     * destination.
     */
    input_begin(5 * PATH_LINES);
    REPEAT("[a]:\n", PATH_LINES);
    parse_ok(0, 0, PATH_LINES / 2 + 1);
    reset_doc();

    /*
     * A title that starts on the next line and never closes, over
     * PATH_LINES lines: a definition with no title, then a paragraph.
     */
    input_begin(10 + 2 * PATH_LINES);
    APPEND("[a]: /u\n'\n");
    REPEAT("x\n", PATH_LINES);
    parse_ok(0, 0, 3);
    check_dump("(doc (def) (p))");
    reset_doc();

    /* Runs of "[" and a setext underline after each. */
    input_begin(PATH_LINES / 100 * 103);
    for (i = 0; i < PATH_LINES / 100; i++) {
        REPEAT("[", 100);
        APPEND("\n=\n");
    }
    parse_ok(0, 0, PATH_LINES / 100 + 1);
}

/* ---------------------------------------------------------------------- */

NAOS_SUITE(limits,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(depth_limit_with_block_quotes),
    NAOS_ADD_TEST(depth_limit_with_lists),
    NAOS_ADD_TEST(depth_limit_with_block_quotes_and_lists),
    NAOS_ADD_TEST(depth_limit_counts_the_deepest_node_only),
    NAOS_ADD_TEST(node_count_limit_with_block_quotes),
    NAOS_ADD_TEST(node_count_limit_with_lists),
    NAOS_ADD_TEST(node_count_limit_with_block_quotes_and_lists),
    NAOS_ADD_TEST(both_limits_exactly_met),
    NAOS_ADD_TEST(node_count_limit_counts_definitions)
)

NAOS_SUITE(deep_nesting,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(block_quotes_nested_100000_deep),
    NAOS_ADD_TEST(lists_nested_100000_deep),
    NAOS_ADD_TEST(lists_nested_on_their_own_lines)
)

NAOS_SUITE(large_inputs,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(blank_lines_after_a_deep_list),
    NAOS_ADD_TEST(marker_lines_after_a_deep_list_in_a_block_quote),
    NAOS_ADD_TEST(many_definitions),
    NAOS_ADD_TEST(many_lazy_continuation_lines),
    NAOS_ADD_TEST(markers_that_almost_make_a_thematic_break),
    NAOS_ADD_TEST(continuation_lines_of_a_deep_list),
    NAOS_ADD_TEST(runs_of_open_brackets)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(limits),
    NAOS_ADD_SUITE(deep_nesting),
    NAOS_ADD_SUITE(large_inputs)
)
