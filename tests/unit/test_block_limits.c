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
 * deep list, runs of "[", fences and HTML blocks that never close, and one
 * line of several megabytes). Time is not measured: these inputs end well
 * within the time limit of CI only if the parse is close to linear. The
 * peak of the bytes a parse holds is compared at two sizes of an input, to
 * check that memory grows in proportion to it. Also: runs of blank lines
 * give the tree of one, and a failure injected at each allocation of a
 * parse of every kind of block leaves nothing behind.
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

/* Checks the kind, the range and the line span of node. */
static void check_span(amark_node_id_t node, amark_kind_t kind, size_t start,
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
 * Blocks that never close and take every line to the end of the input: a
 * careless parser that looks back over the lines of an open block (for its
 * closing line, or its end condition) takes time quadratic in their number.
 * Each block ends at the end of the content of its last line.
 */
NAOS_TEST(unclosed_blocks_of_many_lines)
{
    /* A fence that is never closed, PATH_LINES lines of code. */
    input_begin(4 + 4 * PATH_LINES);
    APPEND("```\n");
    REPEAT("x y\n", PATH_LINES);
    parse_ok(0, 0, 2);
    check_dump("(doc (code fenced ` 3))");
    check_span(root_child(0), AMARK_KIND_CODE_BLOCK, 0, g_length - 1, 1,
               PATH_LINES + 1);
    reset_doc();

    /* The same in a list item: each line is continued by its indentation. */
    input_begin(6 + 6 * PATH_LINES);
    APPEND("- ```\n");
    REPEAT("  x y\n", PATH_LINES);
    parse_ok(0, 0, 4);
    check_dump("(doc (ul - tight (li (code fenced ` 3))))");
    check_span(root_child(0), AMARK_KIND_LIST, 0, g_length - 1, 1,
               PATH_LINES + 1);
    reset_doc();

    /*
     * A "<script>" block (condition 1) and a "<!--" block (condition 2) whose
     * end condition never comes, over PATH_LINES lines that hold none of it,
     * blank lines included.
     */
    input_begin(9 + 6 * PATH_LINES);
    APPEND("<script>\n");
    REPEAT("a < b\n\n", PATH_LINES / 2);
    parse_ok(0, 0, 2);
    check_dump("(doc (html))");
    /* The last line is blank: the block ends where its content does. */
    check_span(root_child(0), AMARK_KIND_HTML_BLOCK, 0, g_length - 1, 1,
               PATH_LINES);
    reset_doc();

    input_begin(5 + 5 * PATH_LINES);
    APPEND("<!--\n");
    REPEAT("- -\n", PATH_LINES);
    parse_ok(0, 0, 2);
    check_dump("(doc (html))");
    check_span(root_child(0), AMARK_KIND_HTML_BLOCK, 0, g_length - 1, 1,
               PATH_LINES + 1);
}

/* One line of LONG_LINE bytes. */
#define LONG_LINE ((size_t)4 * 1024 * 1024)

/*
 * One very long line: the line is scanned for its first non-space byte and
 * its kind a fixed number of times, never once per byte.
 */
NAOS_TEST(one_very_long_line)
{
    /* A paragraph of one line. */
    input_begin(LONG_LINE);
    REPEAT("ab c", LONG_LINE / 4);
    parse_ok(0, 0, 2);
    check_dump("(doc (p))");
    check_span(root_child(0), AMARK_KIND_PARAGRAPH, 0, LONG_LINE, 1, 1);
    reset_doc();

    /* The same line indented by three spaces, after a paragraph line. */
    input_begin(5 + LONG_LINE);
    APPEND("a\n   ");
    REPEAT("ab c", LONG_LINE / 4);
    parse_ok(0, 0, 2);
    check_span(root_child(0), AMARK_KIND_PARAGRAPH, 0, g_length, 1, 2);
    reset_doc();

    /* The content of a fenced code block, closed after it. */
    input_begin(4 + LONG_LINE + 5);
    APPEND("```\n");
    REPEAT("`a~ ", LONG_LINE / 4);
    APPEND("\n```\n");
    parse_ok(0, 0, 2);
    check_dump("(doc (code fenced ` 3))");
    check_span(root_child(0), AMARK_KIND_CODE_BLOCK, 0, g_length - 1, 1, 3);
    reset_doc();

    /* A run of "-" that is one thematic break, and of "#" that is none. */
    input_begin(LONG_LINE);
    REPEAT("- ", LONG_LINE / 2);
    parse_ok(0, 0, 2);
    check_dump("(doc (hr))");
    check_span(root_child(0), AMARK_KIND_THEMATIC_BREAK, 0, LONG_LINE, 1, 1);
    reset_doc();

    input_begin(LONG_LINE);
    REPEAT("#", LONG_LINE);
    parse_ok(0, 0, 2);
    check_dump("(doc (p))");
}

/* ---------------------------------------------------------------------- */
/* The blank line cutoff leaves the tree as it is                         */
/* ---------------------------------------------------------------------- */

/*
 * The cutoff skips matching the elements inside a list on a line whose rest
 * is blank there, when it was blank there on the line before too. One blank
 * line never reaches it (the list's mark is set on that line and used from
 * the next), so a run of blank lines is checked against one blank line at
 * the same place: with no code or HTML block open, CommonMark gives them the
 * same tree, tight and loose included, and so must the parser with the
 * cutoff.
 *
 * Each input below is a pattern whose one line holding '@' stands for the
 * blank lines: the bytes before the '@' (a prefix of ">" markers, say) start
 * each of them, and a filler of white space follows. The strings of
 * k_blank_fillers give the fillers of the lines, separated by ','; the first
 * one, a single empty filler, is the one blank line the others are compared
 * with. In every input but the last three of the table a list is open on
 * the blank lines and goes on over them (a blank line closes no list), with
 * its blank line mark set by the first of them, so each blank line after
 * the first reaches the cutoff at that list or at one outside it.
 *
 * The exception to the rule is an item with nothing in it, which a line of
 * white space as wide as its content indent continues (as in cmark), so
 * that what follows can still go into it. For such an item (empty_item) the
 * fillers are followed by one more empty line, which closes the item as the
 * one blank line does. When a narrow line has closed the item first, the
 * lines after it reach the cutoff at its list; when wide lines keep it
 * open, the second of them reaches the cutoff with the item at the top, and
 * the empty line after them must not: it closes the item.
 */
typedef struct blank_case {
    const char *input; /* the pattern, one line holding '@' */
    bool empty_item;   /* an empty item is open on the blank lines */
    const char *want;  /* the tree with one empty blank line */
} blank_case_t;

static const blank_case_t k_blank_cases[] = {
    /* Lists, the blank line between items, after them, and in an item. */
    {"- a\n@\n- b\n", false, "(doc (ul - loose (li (p)) (li (p))))"},
    {"- a\n- b\n@\nc\n", false,
     "(doc (ul - tight (li (p)) (li (p))) (p))"},
    {"- a\n- b\n@\n", false, "(doc (ul - tight (li (p)) (li (p))))"},
    {"- a\n@\n  b\n", false, "(doc (ul - loose (li (p) (p))))"},
    {"3. a\n@\n4. b\n", false, "(doc (ol 3 . loose (li (p)) (li (p))))"},
    {"- a\n\n- b\n- c\n@\n- d\n", false,
     "(doc (ul - loose (li (p)) (li (p)) (li (p)) (li (p))))"},
    /* Two lists side by side, one tight and one loose. */
    {"1. a\n@\n2) b\n", false,
     "(doc (ol 1 . tight (li (p))) (ol 2 ) tight (li (p))))"},
    {"- a\n@\n- b\n+ c\n+ d\n", false,
     "(doc (ul - loose (li (p)) (li (p))) (ul + tight (li (p)) (li (p))))"},
    {"- a\n+ b\n@\n+ c\n", false,
     "(doc (ul - tight (li (p))) (ul + loose (li (p)) (li (p))))"},
    /* Nested lists, the outer and the inner one loose in turn. */
    {"- a\n  - b\n  - c\n@\n- d\n", false,
     "(doc (ul - loose (li (p) (ul - tight (li (p)) (li (p)))) (li (p))))"},
    {"- a\n  - b\n@\n    c\n- d\n", false,
     "(doc (ul - tight (li (p) (ul - loose (li (p) (p)))) (li (p))))"},
    {"- a\n  - b\n@\n  c\n", false,
     "(doc (ul - loose (li (p) (ul - tight (li (p))) (p))))"},
    {"- a\n  - b\n    - c\n@\n      d\n", false,
     "(doc (ul - tight (li (p) (ul - tight (li (p) (ul - loose (li (p) "
     "(p))))))))"},
    {"- a\n  - b\n    - c\n@\n    d\n", false,
     "(doc (ul - tight (li (p) (ul - loose (li (p) (ul - tight (li (p))) "
     "(p))))))"},
    {"- - - a\n@\n      b\n", false,
     "(doc (ul - tight (li (ul - tight (li (ul - loose (li (p) "
     "(p))))))))"},
    {"- - - a\n@\n- b\n", false,
     "(doc (ul - loose (li (ul - tight (li (ul - tight (li (p)))))) "
     "(li (p))))"},
    {"1. - a\n@\n   - b\n", false,
     "(doc (ol 1 . tight (li (ul - loose (li (p)) (li (p))))))"},
    /* Lists in block quotes, and block quotes in lists. */
    {"> - a\n>@\n> - b\n", false,
     "(doc (bq (ul - loose (li (p)) (li (p)))))"},
    {"> - a\n>@\n>   b\n", false, "(doc (bq (ul - loose (li (p) (p)))))"},
    {"> - a\n> - b\n>@\n> c\n", false,
     "(doc (bq (ul - tight (li (p)) (li (p))) (p)))"},
    {"> 1. a\n>    - b\n>@\n>    c\n", false,
     "(doc (bq (ol 1 . loose (li (p) (ul - tight (li (p))) (p)))))"},
    {"> > - a\n> >@\n> >   - b\n", false,
     "(doc (bq (bq (ul - loose (li (p) (ul - tight (li (p))))))))"},
    {"- > a\n@\n  > b\n", false, "(doc (ul - loose (li (bq (p)) (bq (p)))))"},
    {"- > - a\n  >@\n  >   b\n", false,
     "(doc (ul - tight (li (bq (ul - loose (li (p) (p)))))))"},
    /* Empty items, at the top, after an item, nested and in a quote. */
    {"-\n@\n  a\n", true, "(doc (ul - tight (li)) (p))"},
    {"1.\n@\n   a\n", true, "(doc (ol 1 . tight (li)) (p))"},
    {"- a\n-\n@\n  b\n", true, "(doc (ul - tight (li (p)) (li)) (p))"},
    {"- a\n  - b\n  -\n@\n    c\n", true,
     "(doc (ul - loose (li (p) (ul - tight (li (p)) (li)) (p))))"},
    {"> -\n>@\n>   a\n", true, "(doc (bq (ul - tight (li)) (p)))"},
    /* No list on the blank lines: the cutoff is not reached. */
    {"> - a\n@\n- b\n", false,
     "(doc (bq (ul - tight (li (p)))) (ul - tight (li (p))))"},
    {"a\n@\nb\n", false, "(doc (p) (p))"},
    {"> a\n>@\n> b\n", false, "(doc (bq (p) (p)))"}};

/*
 * The fillers of the blank lines, separated by ','. The first is the one
 * empty blank line; then runs of 2 and 3 empty lines, single lines of white
 * space and runs that mix spaces, tabs and empty lines.
 */
static const char *const k_blank_fillers[] = {
    "",      ",",        ",,",      " ",          "\t",       "  ,\t",
    ", \t ,", "\t,,   ", " ,  ,\t, \t", "        ", "\t\t,\t", ",,,,,,,"};

#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

/*
 * Builds the input of the pattern with the blank lines of fillers, and one
 * more empty blank line if extra is true.
 */
static void build_blank_input(const char *pattern, const char *fillers,
                              bool extra)
{
    size_t length = strlen(pattern);
    size_t mark = (size_t)(strchr(pattern, '@') - pattern);
    size_t line = mark;
    const char *filler = fillers;

    /* At most strlen(fillers) + 2 lines, each of prefix, filler and LF. */
    input_begin(length + (strlen(fillers) + 2) * (mark + 1) +
                strlen(fillers));
    while (line > 0 && pattern[line - 1] != '\n') {
        line--;
    }
    input_repeat(pattern, line, 1);
    for (;;) {
        size_t width = strcspn(filler, ",");

        input_repeat(pattern + line, mark - line, 1);
        input_repeat(filler, width, 1);
        if (filler[width] == '\0') {
            break;
        }
        APPEND("\n");
        filler += width + 1;
    }
    if (extra) {
        APPEND("\n");
        input_repeat(pattern + line, mark - line, 1);
    }
    input_repeat(pattern + mark + 1, length - mark - 1, 1);
}

/*
 * Parses the input with no limits, requires the tree check to pass, and
 * writes the block structure into buffer.
 */
static void parse_and_dump(char *buffer, size_t size)
{
    amark_tree_check_result_t check;

    NAOS_REQUIRE_INT_EQ(parse_input(0, 0), AMARK_ERROR_NONE);
    NAOS_REQUIRE_TRUE(g_doc != NULL && g_doc != k_sentinel);
    check = amark_tree_check(g_doc);
    NAOS_CHECK_INT_EQ(check.violation, AMARK_TREE_VIOLATION_NONE);
    NAOS_CHECK_UINT_EQ(check.node, AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_LT(amark_test_dump_blocks(g_doc, buffer, size), size);
}

NAOS_TEST(runs_of_blank_lines_give_the_tree_of_one)
{
    char one[512];
    char run[512];
    size_t c;
    size_t f;

    for (c = 0; c < COUNT_OF(k_blank_cases); c++) {
        const blank_case_t *test = &k_blank_cases[c];

        /* One empty blank line, with no line added for an empty item. */
        NAOS_TRACE_PUSH("case %lu", (unsigned long)c);
        build_blank_input(test->input, k_blank_fillers[0], false);
        parse_and_dump(one, sizeof(one));
        NAOS_CHECK_STR_EQ(one, test->want);
        for (f = 0; f < COUNT_OF(k_blank_fillers); f++) {
            NAOS_TRACE_PUSH("fillers %lu", (unsigned long)f);
            build_blank_input(test->input, k_blank_fillers[f],
                              test->empty_item);
            parse_and_dump(run, sizeof(run));
            NAOS_CHECK_STR_EQ(run, one);
            NAOS_TRACE_POP();
        }
        NAOS_TRACE_POP();
    }
}

/* ---------------------------------------------------------------------- */
/* Allocation failures                                                    */
/* ---------------------------------------------------------------------- */

/*
 * A document with every kind of block: ATX and setext headings, a thematic
 * break, indented and fenced code (with an info string), an HTML block,
 * link reference definitions (one over two lines, and duplicates, also
 * across the repeats below), a block quote with a lazy continuation line
 * and a list in it, a tight bullet list with an ordered list nested in it
 * and a lazy line in an item, a loose ordered list with a bullet list
 * nested in it, followed by a run of blank lines that reaches the cutoff,
 * and paragraphs.
 */
static const char k_every_block[] =
    "# ATX heading\n"
    "Setext heading\n"
    "--------------\n"
    "\n"
    "***\n"
    "\n"
    "    indented code\n"
    "      more code\n"
    "\n"
    "```c info\n"
    "fenced code\n"
    "```\n"
    "\n"
    "<div>\n"
    "html block\n"
    "</div>\n"
    "\n"
    "[foo]: /url \"title\"\n"
    "[Foo]: /other\n"
    "[bar]:\n"
    "  /bar\n"
    "\n"
    "> quoted line\n"
    "lazy continuation\n"
    "> - item in a quote\n"
    "\n"
    "- tight a\n"
    "lazy line of an item\n"
    "- tight b\n"
    "  1. nested ordered\n"
    "  2. nested ordered\n"
    "- tight c\n"
    "\n"
    "1) loose one\n"
    "\n"
    "2) loose two\n"
    "   - nested bullet\n"
    "\n"
    "\n"
    "   \n"
    "\n"
    "paragraph after the blank lines\n"
    "with a second line\n";

/* The tree of one copy of k_every_block (the root left out). */
#define EVERY_BLOCK_TREE                                                     \
    " (h 1 atx) (h 2 setext) (hr) (code indented) (code fenced ` 3 info)"     \
    " (html) (def)"                                                         \
    " (def) (def) (bq (p) (ul - tight (li (p))))"                            \
    " (ul - tight (li (p)) (li (p) (ol 1 . tight (li (p)) (li (p))))"         \
    " (li (p))) (ol 1 ) loose (li (p)) (li (p) (ul - tight (li (p)))))"     \
    " (p)"

/* Copies of k_every_block in the input: enough to grow the arena of the
 * document several times and the scratch arrays of the parser. */
#define EVERY_BLOCK_COPIES 64

/* Large enough for the dump of the input of EVERY_BLOCK_COPIES copies. */
#define EVERY_BLOCK_DUMP_SIZE ((size_t)64 * 1024)

static char g_dump_want[EVERY_BLOCK_DUMP_SIZE];
static char g_dump_got[EVERY_BLOCK_DUMP_SIZE];

NAOS_TEST(every_block_kind_parses_with_one_copy)
{
    input_begin(sizeof(k_every_block) - 1);
    APPEND(k_every_block);
    parse_and_dump(g_dump_want, sizeof(g_dump_want));
    NAOS_CHECK_STR_EQ(g_dump_want, "(doc" EVERY_BLOCK_TREE ")");
}

/* Requires dump to be the tree of EVERY_BLOCK_COPIES copies. */
static void check_every_block_copies(const char *dump)
{
    static const char tree[] = EVERY_BLOCK_TREE;
    size_t i;

    NAOS_REQUIRE_TRUE(strncmp(dump, "(doc", 4) == 0);
    dump += 4;
    for (i = 0; i < EVERY_BLOCK_COPIES; i++) {
        NAOS_TRACE_PUSH("copy %lu", (unsigned long)i);
        NAOS_REQUIRE_TRUE(strncmp(dump, tree, sizeof(tree) - 1) == 0);
        dump += sizeof(tree) - 1;
        NAOS_TRACE_POP();
    }
    NAOS_CHECK_STR_EQ(dump, ")");
}

/*
 * Makes the n-th allocation of the parse fail, for n = 1, 2, 3, ... until
 * the parse succeeds. Every failed parse must return AMARK_ERROR_NOMEM, set
 * *out to NULL and leave no block but the input; the parse that succeeds
 * must give the tree of a parse with no failure, and leave no block but the
 * input once the document is destroyed.
 */
NAOS_TEST(allocation_failure_at_every_call_with_every_block_kind)
{
    size_t unconstrained_calls;
    size_t before;
    size_t n;
    bool succeeded = false;

    input_begin((sizeof(k_every_block) - 1) * EVERY_BLOCK_COPIES);
    REPEAT(k_every_block, EVERY_BLOCK_COPIES);

    /* The tree and the number of allocations with no failure. */
    before = g_state.malloc_calls;
    parse_and_dump(g_dump_want, sizeof(g_dump_want));
    check_every_block_copies(g_dump_want);
    unconstrained_calls = g_state.malloc_calls - before;
    /* The arena of the document and the scratch arrays grow several
     * times, so there are many allocations to fail. */
    NAOS_CHECK_UINT_GE(unconstrained_calls, 10);
    reset_doc();

    for (n = 1; !succeeded && n <= unconstrained_calls + 1; n++) {
        amark_error_t err;

        NAOS_TRACE_PUSH("allocation %lu", (unsigned long)n);
        g_state.fail_at = g_state.malloc_calls + n;
        err = parse_input(0, 0);
        g_state.fail_at = 0;
        if (err == AMARK_ERROR_NONE) {
            amark_tree_check_result_t check;

            succeeded = true;
            NAOS_REQUIRE_TRUE(g_doc != NULL && g_doc != k_sentinel);
            check = amark_tree_check(g_doc);
            NAOS_CHECK_INT_EQ(check.violation, AMARK_TREE_VIOLATION_NONE);
            NAOS_CHECK_UINT_EQ(check.node, AMARK_NODE_NONE);
            NAOS_REQUIRE_UINT_LT(amark_test_dump_blocks(g_doc, g_dump_got,
                                                        sizeof(g_dump_got)),
                                 sizeof(g_dump_got));
            NAOS_CHECK_STR_EQ(g_dump_got, g_dump_want);
            reset_doc();
        } else {
            NAOS_CHECK_INT_EQ(err, AMARK_ERROR_NOMEM);
            NAOS_CHECK_NULL(g_doc);
            /* Only the input is left. */
            NAOS_CHECK_UINT_EQ(g_state.live, 1);
        }
        NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
        NAOS_TRACE_POP();
    }
    /* The parse succeeds once no allocation fails, and not before. */
    NAOS_CHECK_TRUE(succeeded);
    NAOS_CHECK_UINT_EQ(n, unconstrained_calls + 2);
}

/* ---------------------------------------------------------------------- */
/* Memory in proportion to the input                                      */
/* ---------------------------------------------------------------------- */

/*
 * Parses the input with no limits and returns the peak of the bytes the
 * parse held at once (the document and its scratch arena), the input left
 * out. Allocation is deterministic, so the value is the same on every run.
 */
static size_t parse_peak_bytes(void)
{
    amark_tree_check_result_t check;
    size_t before;

    reset_doc();
    before = g_state.live_bytes;
    g_state.peak_bytes = before;
    NAOS_REQUIRE_INT_EQ(parse_input(0, 0), AMARK_ERROR_NONE);
    NAOS_REQUIRE_TRUE(g_doc != NULL && g_doc != k_sentinel);
    check = amark_tree_check(g_doc);
    NAOS_CHECK_INT_EQ(check.violation, AMARK_TREE_VIOLATION_NONE);
    NAOS_CHECK_UINT_EQ(check.node, AMARK_NODE_NONE);
    return g_state.peak_bytes - before;
}

/*
 * Requires the peak of an input four times as large to stay within a
 * linear bound of the peak of the small one: at most 1.5 times four times
 * it, plus a constant for the fixed blocks of a document. A parse whose
 * memory is quadratic in the input reaches sixteen times it. Allocation is
 * deterministic, so the check gives the same result on every run.
 */
static void check_linear_peak(size_t small, size_t large)
{
    NAOS_CHECK_UINT_GT(small, 0);
    NAOS_CHECK_UINT_GE(large, small);
    NAOS_CHECK_UINT_LE(large, 6 * small + 4096);
}

/* The size of the small input of each shape below; the large one is 4x. */
#define MEMORY_UNITS 2500

NAOS_TEST(memory_grows_in_proportion_to_the_input)
{
    size_t small;
    size_t large;

    /* Paragraphs of two lines. */
    input_begin(20 * MEMORY_UNITS);
    REPEAT("para one\nline two\n\n", MEMORY_UNITS);
    small = parse_peak_bytes();
    input_begin(20 * 4 * MEMORY_UNITS);
    REPEAT("para one\nline two\n\n", 4 * MEMORY_UNITS);
    large = parse_peak_bytes();
    check_linear_peak(small, large);

    /* Every kind of block, in copies of k_every_block. */
    input_begin((sizeof(k_every_block) - 1) * (MEMORY_UNITS / 100));
    REPEAT(k_every_block, MEMORY_UNITS / 100);
    small = parse_peak_bytes();
    input_begin((sizeof(k_every_block) - 1) * (MEMORY_UNITS / 100) * 4);
    REPEAT(k_every_block, (MEMORY_UNITS / 100) * 4);
    large = parse_peak_bytes();
    check_linear_peak(small, large);

    /* Block quotes nested deep: the stack of open blocks grows too. */
    input_begin(MEMORY_UNITS + 1);
    REPEAT(">", MEMORY_UNITS);
    APPEND("a");
    small = parse_peak_bytes();
    input_begin(4 * MEMORY_UNITS + 1);
    REPEAT(">", 4 * MEMORY_UNITS);
    APPEND("a");
    large = parse_peak_bytes();
    check_linear_peak(small, large);
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
    NAOS_ADD_TEST(runs_of_open_brackets),
    NAOS_ADD_TEST(unclosed_blocks_of_many_lines),
    NAOS_ADD_TEST(one_very_long_line),
    NAOS_ADD_TEST(memory_grows_in_proportion_to_the_input)
)

NAOS_SUITE(blank_line_cutoff,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(runs_of_blank_lines_give_the_tree_of_one)
)

NAOS_SUITE(allocation_failures,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(every_block_kind_parses_with_one_copy),
    NAOS_ADD_TEST(allocation_failure_at_every_call_with_every_block_kind)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(limits),
    NAOS_ADD_SUITE(deep_nesting),
    NAOS_ADD_SUITE(large_inputs),
    NAOS_ADD_SUITE(blank_line_cutoff),
    NAOS_ADD_SUITE(allocation_failures)
)
