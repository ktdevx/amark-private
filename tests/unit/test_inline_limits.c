/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of the resource limits and of large inputs of the inline
 * parser, through the public entry point amark_parse and
 * amark_document_destroy only: the nesting depth and node count limits at
 * and one past their values with inline nodes (emphasis, strong emphasis,
 * links and images, alone and inside deep block quotes and lists);
 * emphasis, brackets and images nested 100,000 deep with no limits; and the
 * inputs of the table of "Performance & Scalability" in the design, which
 * make a careless inline parser take time quadratic in their length. Also:
 * a failure injected at each allocation of a parse of every inline kind,
 * and of a paragraph padded so that each step of the inline parse meets a
 * failure in turn, leaves nothing behind; and the same input gives the
 * same tree, node ID by node ID, parse after parse.
 *
 * Linear time. Each of those inputs is a pattern repeated n times, and is
 * parsed at a small n and at an n that makes it about 32 times as long
 * (100,000 repeats or more). The time of the large parse must stay within
 * 10 times the length ratio times that of the small one (320 for 32 times
 * the length), plus a fixed allowance of 50 ms: a linear parse stays near
 * 32 in a sanitizer build and below 4 times that in an optimized one (the
 * large input no longer fits the caches, and its fresh pages cost system
 * time), and a quadratic one reaches 1024. The backtick strings, whose
 * input grows as the square of n, are compared at about 1000 times the
 * length, where a parse that reads to the end of the content for each
 * string grows as the length to the power 1.5. Comparing two parses of one
 * run makes the check independent of the speed of the machine and of the
 * build (a sanitizer build is slower at both sizes alike), and where clock
 * gives the processor time it leaves out the time the process waits for
 * the processor. clock may tick only every millisecond (it does on
 * Windows, where it gives the wall time), so the small input is parsed
 * again and again for at least a twentieth of a second and the time of one
 * parse is the mean; the large one is parsed twice and the faster counts.
 * Some of the measures of the parser show in no tree (the lower bounds of
 * the delimiter stack, the link floor, the marks of the scanners for
 * constructs that do not close), so only this check finds their loss.
 *
 * Memory. The peak of the bytes a parse holds is counted by the counting
 * allocator, which is deterministic: at the large size it must stay within a
 * fixed number of bytes per byte of input, and within twice the length
 * ratio of the peak at the small size (a quadratic growth reaches its
 * square).
 *
 * The shape of each tree is checked too: the whole tree of the input with
 * n = 2, and the node count and the greatest depth, both linear in n, at
 * n = 2, 3 and both sizes. The inputs are built in memory with the counting
 * allocator, every parsed document goes through amark_tree_check, and
 * teardown checks that no block is left. The parser uses no recursion, so
 * the 100,000 deep trees need no more stack than shallow ones.
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
#include <time.h>

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
/* A second document, for the tests that compare two parses. */
static amark_document_t *g_other;

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
    g_other = NULL;
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
    if (g_other != k_sentinel) {
        amark_document_destroy(g_other);
    }
    g_other = NULL;
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
    g_input = (char *)g_allocator.malloc_fn(g_allocator.ctx,
                                            capacity > 0 ? capacity : 1);
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

/* Appends the C string s, count times. */
static void input_string(const char *s, size_t count)
{
    input_repeat(s, strlen(s), count);
}

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
    AMARK_CONFIG_INIT(&config);
    config.allocator = g_allocator;
    config.max_nesting_depth = max_depth;
    config.max_node_count = max_nodes;
    g_doc = k_sentinel;
    return amark_parse(g_input, g_length, &config, &g_doc);
}

/* Requires g_doc to be a document that passes the tree check. */
static void check_tree(void)
{
    amark_tree_check_result_t check;

    NAOS_REQUIRE_TRUE(g_doc != NULL && g_doc != k_sentinel);
    check = amark_tree_check(g_doc);
    NAOS_CHECK_INT_EQ(check.violation, AMARK_TREE_VIOLATION_NONE);
    NAOS_CHECK_UINT_EQ(check.node, AMARK_NODE_NONE);
}

/*
 * Requires the parse with the two limits to succeed with a tree that passes
 * the tree check and has nodes nodes (the root included).
 */
static void parse_ok(size_t max_depth, size_t max_nodes, size_t nodes)
{
    NAOS_REQUIRE_INT_EQ(parse_input(max_depth, max_nodes), AMARK_ERROR_NONE);
    check_tree();
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

/* Requires the whole tree of g_doc to be want. */
static void check_dump(const char *want)
{
    char buffer[1024];
    size_t length = amark_test_dump_tree(g_doc, buffer, sizeof(buffer));

    NAOS_REQUIRE_UINT_LT(length, sizeof(buffer));
    NAOS_CHECK_STR_EQ(buffer, want);
}

/*
 * Visits the whole tree of g_doc in document order without recursion and
 * stores the number of its nodes and the greatest depth of a node (the
 * root at 0).
 */
static void tree_size(size_t *nodes, size_t *max_depth)
{
    amark_node_id_t node = amark_document_root(g_doc);
    size_t depth = 0;

    *nodes = 0;
    *max_depth = 0;
    while (node != AMARK_NODE_NONE) {
        amark_node_id_t child = amark_node_first_child(g_doc, node);

        (*nodes)++;
        if (depth > *max_depth) {
            *max_depth = depth;
        }
        if (child != AMARK_NODE_NONE) {
            node = child;
            depth++;
            continue;
        }
        while (node != AMARK_NODE_NONE &&
               amark_node_next_sibling(g_doc, node) == AMARK_NODE_NONE) {
            node = amark_node_parent(g_doc, node);
            depth--;
        }
        if (node != AMARK_NODE_NONE) {
            node = amark_node_next_sibling(g_doc, node);
        }
    }
}

/*
 * Requires g_doc to have nodes nodes, counted both by the document and by a
 * walk over the tree, and its deepest node at depth depth.
 */
static void check_size(size_t nodes, size_t depth)
{
    size_t walked;
    size_t deepest;

    tree_size(&walked, &deepest);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), nodes);
    NAOS_CHECK_UINT_EQ(walked, nodes);
    NAOS_CHECK_UINT_EQ(deepest, depth);
}

/*
 * Requires the chain of first children from the paragraph (the first child
 * of the root, below depth containers of kind container) to be count nodes
 * of kind kind, each an only child but the first, ending in a text.
 */
static void check_chain(amark_kind_t container, size_t depth,
                        amark_kind_t kind, size_t count)
{
    amark_node_id_t node = amark_document_root(g_doc);
    size_t i;

    for (i = 0; i < depth; i++) {
        node = amark_node_first_child(g_doc, node);
        NAOS_REQUIRE_UINT_NE(node, AMARK_NODE_NONE);
        NAOS_REQUIRE_UINT_EQ(amark_node_kind(g_doc, node), container);
    }
    node = amark_node_first_child(g_doc, node);
    NAOS_REQUIRE_UINT_NE(node, AMARK_NODE_NONE);
    NAOS_REQUIRE_UINT_EQ(amark_node_kind(g_doc, node), AMARK_KIND_PARAGRAPH);
    for (i = 0; i < count; i++) {
        node = amark_node_first_child(g_doc, node);
        NAOS_REQUIRE_UINT_NE(node, AMARK_NODE_NONE);
        NAOS_REQUIRE_UINT_EQ(amark_node_kind(g_doc, node), kind);
        if (i > 0) {
            NAOS_REQUIRE_UINT_EQ(amark_node_next_sibling(g_doc, node),
                                 AMARK_NODE_NONE);
        }
    }
    node = amark_node_first_child(g_doc, node);
    NAOS_REQUIRE_UINT_NE(node, AMARK_NODE_NONE);
    NAOS_CHECK_UINT_EQ(amark_node_kind(g_doc, node), AMARK_KIND_TEXT);
    NAOS_CHECK_UINT_EQ(amark_node_first_child(g_doc, node), AMARK_NODE_NONE);
}

/* ---------------------------------------------------------------------- */
/* The nesting depth and node count limits                                */
/* ---------------------------------------------------------------------- */

/*
 * Nesting: the root is at depth 0, its paragraph at 1, and each container
 * around the paragraph and each inline node around a text adds 1. Merged
 * texts are one node. Where a test checks two values over a limit, the
 * first is one the blocks alone are over (the block parser fails) and the
 * second one only the inline nodes are over (the inline parser fails).
 */
#define NEST 40

NAOS_TEST(depth_limit_with_deep_emphasis)
{
    /* Strong emphasis NEST deep: the text at depth NEST + 2. */
    input_begin(4 * NEST + 1);
    REPEAT("**", NEST);
    APPEND("a");
    REPEAT("**", NEST);
    parse_exceeds(NEST + 1, 0);
    parse_exceeds(2, 0);
    parse_ok(NEST + 2, 0, NEST + 3);
    check_chain(AMARK_KIND_BLOCK_QUOTE, 0, AMARK_KIND_STRONG, NEST);
    reset_doc();

    /* Emphasis and strong emphasis in turn, read whole. */
    input_begin(32);
    APPEND("*a **b *c* d** e*");
    parse_exceeds(4, 0);
    parse_ok(5, 0, 10);
    check_dump("(doc (p (em (text \"a \") (strong (text \"b \") "
               "(em (text \"c\")) (text \" d\")) (text \" e\"))))");
}

NAOS_TEST(depth_limit_with_links_and_images)
{
    /* Images nested NEST deep: the text of the innermost at NEST + 2. */
    input_begin(6 * NEST + 1);
    REPEAT("![", NEST);
    APPEND("a");
    REPEAT("](u)", NEST);
    parse_exceeds(NEST + 1, 0);
    parse_ok(NEST + 2, 0, NEST + 3);
    check_chain(AMARK_KIND_BLOCK_QUOTE, 0, AMARK_KIND_IMAGE, NEST);
    reset_doc();

    /* Strong emphasis NEST deep in a link, the link in an image. */
    input_begin(4 * NEST + 16);
    APPEND("![[");
    REPEAT("**", NEST);
    APPEND("a");
    REPEAT("**", NEST);
    APPEND("](u)](v)");
    parse_exceeds(NEST + 3, 0);
    parse_ok(NEST + 4, 0, NEST + 5);
    reset_doc();

    /* A link inside a link is the one link: the outer is text. */
    input_begin(32);
    APPEND("[*[a](u)*](v)");
    parse_exceeds(3, 0);
    parse_ok(4, 0, 7);
    check_dump("(doc (p (text \"[\") (em (link inline dest=\"u\" "
               "(text \"a\"))) (text \"](v)\")))");
}

NAOS_TEST(depth_limit_in_deep_containers)
{
    /*
     * NEST quotes, the paragraph at NEST + 1, the emphasis at NEST + 2 and
     * its text at NEST + 3.
     */
    input_begin(NEST + 3);
    REPEAT(">", NEST);
    APPEND("*a*");
    parse_exceeds(NEST, 0);
    parse_exceeds(NEST + 1, 0);
    parse_exceeds(NEST + 2, 0);
    parse_ok(NEST + 3, 0, NEST + 4);
    check_chain(AMARK_KIND_BLOCK_QUOTE, NEST, AMARK_KIND_EMPHASIS, 1);
    reset_doc();

    /* NEST lists and items, then an image in a link in the paragraph. */
    input_begin(2 * NEST + 16);
    REPEAT("- ", NEST);
    APPEND("[![a](u)](v)");
    parse_exceeds(2 * NEST, 0);
    parse_exceeds(2 * NEST + 3, 0);
    parse_ok(2 * NEST + 4, 0, 2 * NEST + 5);
    reset_doc();

    /*
     * A deep branch beside a shallow one: the shallow paragraph has the
     * deeper inline nodes, and the deepest node of all counts.
     */
    input_begin(64);
    APPEND("****a****\n\n> > b\n");
    /* "a" at depth 4, "b" at depth 4. */
    parse_exceeds(3, 0);
    parse_ok(4, 0, 9);
    reset_doc();
    input_begin(64);
    APPEND("******a******\n\n> > b\n");
    parse_exceeds(4, 0);
    parse_ok(5, 0, 10);
    check_dump("(doc (p (strong (strong (strong (text \"a\"))))) "
               "(bq (bq (p (text \"b\")))))");
}

NAOS_TEST(node_count_limit_with_inline_nodes)
{
    /* The root, the paragraph, NEST strong emphases and the text. */
    input_begin(4 * NEST + 1);
    REPEAT("**", NEST);
    APPEND("a");
    REPEAT("**", NEST);
    parse_exceeds(0, 1);
    parse_exceeds(0, NEST + 2);
    parse_ok(0, NEST + 3, NEST + 3);

    /* NEST images, each with a text, side by side. */
    input_begin(7 * NEST);
    REPEAT("![a](u)", NEST);
    parse_exceeds(0, 2 * NEST + 1);
    parse_ok(0, 2 * NEST + 2, 2 * NEST + 2);

    /*
     * Texts merged into one count once: unmatched delimiters and brackets
     * beside the texts around them.
     */
    input_begin(7 * NEST);
    REPEAT("a_b]c[d", NEST);
    parse_exceeds(0, 2);
    parse_ok(0, 3, 3);

    /*
     * NEST quotes and the paragraph, the emphasis and its text: the blocks
     * alone are over NEST + 1, the inline nodes over NEST + 3.
     */
    input_begin(NEST + 3);
    REPEAT(">", NEST);
    APPEND("*a*");
    parse_exceeds(0, NEST + 1);
    parse_exceeds(0, NEST + 3);
    parse_ok(0, NEST + 4, NEST + 4);
}

NAOS_TEST(node_count_limit_with_every_paragraph)
{
    /*
     * NEST paragraphs, each with an emphasis and its text: the limit is
     * reached in the last paragraph.
     */
    input_begin(5 * NEST);
    REPEAT("*a*\n\n", NEST);
    parse_exceeds(0, NEST);
    parse_exceeds(0, 3 * NEST);
    parse_ok(0, 3 * NEST + 1, 3 * NEST + 1);
    reset_doc();

    /* Both limits exactly met, and each one past its value. */
    parse_ok(3, 3 * NEST + 1, 3 * NEST + 1);
    reset_doc();
    parse_exceeds(2, 3 * NEST + 1);
    parse_exceeds(3, 3 * NEST);
}

/* ---------------------------------------------------------------------- */
/* Nesting 100,000 deep                                                   */
/* ---------------------------------------------------------------------- */

#define DEEP 100000

NAOS_TEST(emphasis_nested_100000_deep)
{
    /* DEEP strong emphases, the text at depth DEEP + 2. */
    input_begin(4 * DEEP + 1);
    REPEAT("**", DEEP);
    APPEND("a");
    REPEAT("**", DEEP);
    parse_ok(0, 0, DEEP + 3);
    check_chain(AMARK_KIND_BLOCK_QUOTE, 0, AMARK_KIND_STRONG, DEEP);
    reset_doc();
    parse_exceeds(DEEP + 1, 0);
    parse_ok(DEEP + 2, DEEP + 3, DEEP + 3);
    reset_doc();
    parse_exceeds(0, DEEP + 2);

    /*
     * Emphasis and strong emphasis in turn, DEEP of each: "*a **a " DEEP / 2
     * times, "b", then " a** a*" as many times. Each level holds a text
     * before and after the next one.
     */
    input_begin(14 * (DEEP / 2) + 1);
    REPEAT("*a **a ", DEEP / 2);
    APPEND("b");
    REPEAT(" a** a*", DEEP / 2);
    parse_ok(0, 0, 3 * DEEP + 1);
    check_size(3 * DEEP + 1, DEEP + 2);
}

NAOS_TEST(brackets_nested_100000_deep)
{
    /* DEEP "[", "a" and DEEP "]": no link, one text. */
    input_begin(2 * DEEP + 1);
    REPEAT("[", DEEP);
    APPEND("a");
    REPEAT("]", DEEP);
    parse_ok(0, 0, 3);
    check_size(3, 2);
    reset_doc();

    /*
     * DEEP "[", "a" and DEEP "](u)": the innermost is a link, and the link
     * in it turns the others to text.
     */
    input_begin(5 * DEEP + 1);
    REPEAT("[", DEEP);
    APPEND("a");
    REPEAT("](u)", DEEP);
    parse_ok(0, 0, 6);
    check_size(6, 3);
}

NAOS_TEST(images_nested_100000_deep)
{
    /* DEEP images, the text of the innermost at depth DEEP + 2. */
    input_begin(6 * DEEP + 1);
    REPEAT("![", DEEP);
    APPEND("a");
    REPEAT("](u)", DEEP);
    parse_ok(0, 0, DEEP + 3);
    check_chain(AMARK_KIND_BLOCK_QUOTE, 0, AMARK_KIND_IMAGE, DEEP);
    reset_doc();
    parse_exceeds(DEEP + 1, 0);
    parse_ok(DEEP + 2, DEEP + 3, DEEP + 3);
    reset_doc();
    parse_exceeds(0, DEEP + 2);

    /* Images in a link: the link holds the outermost image. */
    input_begin(6 * DEEP + 16);
    APPEND("[");
    REPEAT("![", DEEP);
    APPEND("a");
    REPEAT("](u)", DEEP);
    APPEND("](v)");
    parse_ok(0, 0, DEEP + 4);
    check_size(DEEP + 4, DEEP + 3);
}

/* ---------------------------------------------------------------------- */
/* Inputs that take quadratic time in a careless parser                   */
/* ---------------------------------------------------------------------- */

/*
 * The number of repeats of the large input of a pattern, and how many times
 * fewer the small one has. PATH_N1 gives both for a pattern of one repeated
 * unit, PATH_N2 for one of two, which repeats each half as many times.
 */
#define PATH_REPEATS 100000
#define PATH_SCALE 32
#define PATH_N1 PATH_REPEATS / PATH_SCALE, PATH_REPEATS
#define PATH_N2 PATH_REPEATS / 2 / PATH_SCALE, PATH_REPEATS / 2

/*
 * The processor time of the large parse may be at most PATH_TIME_FACTOR
 * times the length ratio times that of the small one, plus PATH_TIME_SLACK
 * microseconds.
 */
#define PATH_TIME_FACTOR 10
#define PATH_TIME_SLACK 50000

/*
 * The peak of the bytes a parse holds may be at most PATH_PEAK_PER_BYTE
 * bytes per byte of the input, plus PATH_PEAK_FIXED.
 */
#define PATH_PEAK_PER_BYTE 512
#define PATH_PEAK_FIXED ((size_t)64 * 1024)

/*
 * A pattern: head, unit n times, middle, unit2 n times, tail. When build is
 * not NULL, it builds the input of n instead. It is parsed with n = small
 * and n = large. The tree with n = 2 is dump;
 * with n repeats it has nodes_base + nodes_step n nodes and its deepest node
 * at depth_base + depth_step n.
 */
typedef struct path_case {
    const char *name;
    const char *head;
    const char *unit;
    const char *middle;
    const char *unit2;
    const char *tail;
    void (*build)(size_t n);
    size_t small; /* n of the small input */
    size_t large; /* n of the large input */
    const char *dump;
    size_t nodes_base;
    size_t nodes_step;
    size_t depth_base;
    size_t depth_step;
} path_case_t;

/* Builds the input of c with n repeats. */
static void path_build(const path_case_t *c, size_t n)
{
    if (c->build != NULL) {
        c->build(n);
        return;
    }
    input_begin(strlen(c->head) + n * strlen(c->unit) + strlen(c->middle) +
                n * strlen(c->unit2) + strlen(c->tail));
    input_string(c->head, 1);
    input_string(c->unit, n);
    input_string(c->middle, 1);
    input_string(c->unit2, n);
    input_string(c->tail, 1);
}

/*
 * "e" and a run of i backticks, for i from 1 to n: no two runs have the same
 * length, so none closes, and the input is about n * n / 2 bytes long.
 */
static void build_backticks(size_t n)
{
    size_t i;

    input_begin(n * (n + 3) / 2);
    for (i = 1; i <= n; i++) {
        APPEND("e");
        REPEAT("`", i);
    }
}

/*
 * n references "[0] " that match no definition, then n definitions of other
 * labels.
 */
static void build_undefined_references(size_t n)
{
    size_t i;

    input_begin(4 * n + 2 + n * 16);
    REPEAT("[0] ", n);
    APPEND("\n\n");
    for (i = 0; i < n; i++) {
        APPEND("[d");
        input_number(i);
        APPEND("]: /u\n");
    }
}

/* The longest label a reference may have, and one byte too many. */
#define LABEL_MAX 999

/*
 * One definition of a label of LABEL_MAX bytes, then n times a reference to
 * it with its last byte changed (no definition), a reference to it, and a
 * label one byte too long (no label).
 */
static void build_long_labels(size_t n)
{
    input_begin(LABEL_MAX + 8 + n * (3 * LABEL_MAX + 12));
    APPEND("[");
    REPEAT("x", LABEL_MAX);
    APPEND("]: /u\n\n");
    {
        size_t i;

        for (i = 0; i < n; i++) {
            APPEND("[");
            REPEAT("x", LABEL_MAX - 1);
            APPEND("y] [");
            REPEAT("x", LABEL_MAX);
            APPEND("] [");
            REPEAT("x", LABEL_MAX + 1);
            APPEND("] ");
        }
    }
}

static const path_case_t k_path_cases[] = {
    /* Delimiters that match nothing. */
    {"closers with no opener", "", "a_ ", "", "", "", NULL, PATH_N1,
     "(doc (p (text \"a_ a_\")))", 3, 0, 2, 0},
    {"openers with no closer", "", "_a ", "", "", "", NULL, PATH_N1,
     "(doc (p (text \"_a _a\")))", 3, 0, 2, 0},
    {"openers and closers of other kinds", "", "*a_ ", "", "", "", NULL,
     PATH_N1, "(doc (p (text \"*a_ *a_\")))", 3, 0, 2, 0},
    {"openers, then closers of the other kind", "", "_a ", "", "a* ", "",
     NULL, PATH_N2, "(doc (p (text \"_a _a a* a*\")))", 3, 0, 2, 0},
    /* Matches that are hard to decide. */
    {"openers, then closers of both kinds", "", "*a ", "", "_a*_ ", "", NULL,
     PATH_N2,
     "(doc (p (em (text \"a \") (em (text \"a _a\")) (text \"_ _a\")) "
     "(text \"_\")))",
     2, 3, 2, 1},
    {"closers that fail the rule of 3", "a**b", "c* ", "", "", "", NULL,
     PATH_N1, "(doc (p (text \"a**bc* c*\")))", 3, 0, 2, 0},
    /* Deep emphasis. */
    {"emphasis in strong emphasis, deep", "", "*a **a ", "b", " a** a*", "",
     NULL, PATH_N2,
     "(doc (p (em (text \"a \") (strong (text \"a \") (em (text \"a \") "
     "(strong (text \"a b a\")) (text \" a\")) (text \" a\")) "
     "(text \" a\"))))",
     1, 6, 2, 2},
    {"strong emphasis in emphasis, deep", "", "**a *a ", "b", " a* a**",
     "", NULL, PATH_N2,
     "(doc (p (strong (text \"a \") (em (text \"a \") (strong (text \"a \") "
     "(em (text \"a b a\")) (text \" a\")) (text \" a\")) "
     "(text \" a\"))))",
     1, 6, 2, 2},
    {"one long run of each side", "", "**", "a", "**", "", NULL, PATH_N2,
     "(doc (p (strong (strong (text \"a\")))))", 3, 1, 2, 1},
    /* Deep and unclosed brackets. */
    {"deep brackets", "", "[", "a", "]", "", NULL, PATH_N2,
     "(doc (p (text \"[[a]]\")))", 3, 0, 2, 0},
    {"deep brackets around a link", "", "[", "a", "](u)", "", NULL,
     PATH_N2,
     "(doc (p (text \"[\") (link inline dest=\"u\" (text \"a\")) "
     "(text \"](u)\")))",
     6, 0, 3, 0},
    {"brackets that do not close", "", "[a", "", "", "", NULL, PATH_N1,
     "(doc (p (text \"[a[a\")))", 3, 0, 2, 0},
    {"brackets that do not open", "", "a]", "", "", "", NULL, PATH_N1,
     "(doc (p (text \"a]a]\")))", 3, 0, 2, 0},
    {"empty links after image openers", "", "![[]()", "", "", "", NULL,
     PATH_N1,
     "(doc (p (text \"![\") (link inline) (text \"![\") (link inline)))", 2,
     2, 2, 0},
    /* Parentheses in a destination, inline links that do not close. */
    {"destinations that do not close", "", "[a](b", "", "", "", NULL,
     PATH_N1, "(doc (p (text \"[a](b[a](b\")))", 3, 0, 2, 0},
    {"pointy destinations that do not close", "", "[a](<b", "", "", "",
     NULL, PATH_N1, "(doc (p (text \"[a](<b[a](<b\")))", 3, 0, 2, 0},
    {"pointy destinations over lines", "", "[a](<b\n", "", "", "", NULL,
     PATH_N1,
     "(doc (p (text \"[a](<b\") (sbr) (text \"[a](<b\")))", 1, 2, 2, 0},
    {"parentheses after brackets", "", "[ (](", "", "", "", NULL,
     PATH_N1, "(doc (p (text \"[ (]([ (](\")))", 3, 0, 2, 0},
    {"parentheses nested deep in a destination", "[a](", "(", "", "", "",
     NULL, PATH_N1, "(doc (p (text \"[a](((\")))", 3, 0, 2, 0},
    {"titles that do not close", "", "[a](x '", "", "", "", NULL,
     PATH_N1, "(doc (p (text \"[a](x '[a](x '\")))", 3, 0, 2, 0},
    /* Backtick strings that do not close. */
    {"backtick strings of every length", "", "", "", "", "",
     build_backticks, 64, 4096, "(doc (p (text \"e`e``\")))", 3, 0, 2, 0},
    /* Raw HTML that does not close. */
    {"comments that do not close", "a ", "<!--", "", "", "", NULL,
     PATH_N1, "(doc (p (text \"a <!--<!--\")))", 3, 0, 2, 0},
    {"processing instructions that do not close", "a ", "<?", "", "", "",
     NULL, PATH_N1, "(doc (p (text \"a <?<?\")))", 3, 0, 2, 0},
    {"declarations that do not close", "a ", "<!A", "", "", "", NULL,
     PATH_N1, "(doc (p (text \"a <!A<!A\")))", 3, 0, 2, 0},
    {"CDATA sections that do not close", "a ", "<![CDATA[", "", "", "",
     NULL, PATH_N1, "(doc (p (text \"a <![CDATA[<![CDATA[\")))", 3, 0,
     2, 0},
    {"attribute values that do not close", "a ", "<a b=\"", "", "", "",
     NULL, PATH_N1, "(doc (p (text \"a <a b=\\\"<a b=\\\"\")))", 3, 0, 2,
     0},
    {"attribute values of both quotes", "a <a b=\"<a c='", "\" b=\"' c='",
     "", "", "", NULL, PATH_N1,
     "(doc (p (text \"a <a b=\\\"<a c='\\\" b=\\\"' c='\\\" b=\\\"' "
     "c='\")))",
     3, 0, 2, 0},
    {"less-than signs", "a ", "<", "", "", "", NULL, PATH_N1,
     "(doc (p (text \"a <<\")))", 3, 0, 2, 0},
    {"autolinks that do not close", "", "<a:", "", "<b@c", "", NULL,
     PATH_N2, "(doc (p (text \"<a:<a:<b@c<b@c\")))", 3, 0, 2, 0},
    /* Many "&" and "\". */
    {"ampersands", "", "&a", "", "&#", "", NULL, PATH_N2,
     "(doc (p (text \"&a&a&#&#\")))", 3, 0, 2, 0},
    {"backslashes", "", "\\a", "", "", "", NULL, PATH_N1,
     "(doc (p (text \"\\\\a\\\\a\")))", 3, 0, 2, 0},
    /* References that match no definition, and long labels. */
    {"references with no definition", "", "", "", "", "",
     build_undefined_references, PATH_N2,
     "(doc (p (text \"[0] [0]\")) (def) (def))", 3, 1, 2, 0},
    {"long labels", "", "", "", "", "", build_long_labels, 25, 400, NULL,
     4, 3, 3, 0},
    /* Emphasis in deep block quotes. */
    {"emphasis in deep block quotes", "", ">", "", "a*a*", "", NULL,
     PATH_N2,
     "(doc (bq (bq (p (text \"a\") (em (text \"a\")) (text \"a\") "
     "(em (text \"a\"))))))",
     2, 4, 3, 1},
};

/*
 * Parses the input with the two limits and destroys the document, again and
 * again until the clock has advanced by at least min_ticks (once at least),
 * requiring each parse to succeed. Returns the processor time of one parse
 * and its destruction, in microseconds: the time of them all divided by
 * their number, so that inputs that parse in less than a tick of the clock
 * (1 ms on some systems) are measured all the same. Stores in *peak the
 * peak of the bytes a parse held (the input left out; the same for every
 * parse). Returns 0 where the processor time cannot be read.
 */
static unsigned long timed_parses(size_t max_depth, size_t max_nodes,
                                  clock_t min_ticks, size_t *peak)
{
    size_t before;
    size_t count = 0;
    clock_t start;
    clock_t end;

    reset_doc();
    before = g_state.live_bytes;
    g_state.peak_bytes = before;
    start = clock();
    do {
        NAOS_REQUIRE_INT_EQ(parse_input(max_depth, max_nodes),
                            AMARK_ERROR_NONE);
        reset_doc();
        count++;
        end = clock();
    } while (start != (clock_t)-1 && end != (clock_t)-1 && end >= start &&
             end - start < min_ticks);
    *peak = g_state.peak_bytes - before;
    if (start == (clock_t)-1 || end == (clock_t)-1 || end < start) {
        return 0;
    }
    return (unsigned long)((double)(end - start) * 1e6 / CLOCKS_PER_SEC /
                           (double)count);
}

/* Requires the size of g_doc to be that of c with n repeats. */
static void check_case_size(const path_case_t *c, size_t n)
{
    NAOS_TRACE_PUSH("n = %lu", (unsigned long)n);
    check_size(c->nodes_base + c->nodes_step * n,
               c->depth_base + c->depth_step * n);
    NAOS_TRACE_POP();
}

/*
 * Checks c: its tree with 2 and 3 repeats, then the time and the memory of
 * its parse at the small and the large size, and the size of both trees.
 * With limits, the large input is parsed with both limits at the values of
 * its tree.
 */
static void check_path_case(const path_case_t *c, bool limits)
{
    size_t small_length;
    size_t large_length;
    size_t small_peak;
    size_t large_peak;
    size_t ratio;
    size_t max_depth = 0;
    size_t max_nodes = 0;
    unsigned long small_time;
    unsigned long large_time;

    /* The tree of a few repeats. */
    path_build(c, 2);
    NAOS_REQUIRE_INT_EQ(parse_input(0, 0), AMARK_ERROR_NONE);
    check_tree();
    if (c->dump != NULL) {
        check_dump(c->dump);
    }
    check_case_size(c, 2);
    path_build(c, 3);
    NAOS_REQUIRE_INT_EQ(parse_input(0, 0), AMARK_ERROR_NONE);
    check_tree();
    check_case_size(c, 3);

    /*
     * The small and the large input. The small one is parsed for at least a
     * twentieth of a second, the large one twice, keeping the faster. Each
     * tree is then made once more to check its size.
     */
    path_build(c, c->small);
    small_length = g_length;
    small_time = timed_parses(0, 0, CLOCKS_PER_SEC / 20, &small_peak);
    NAOS_REQUIRE_INT_EQ(parse_input(0, 0), AMARK_ERROR_NONE);
    check_tree();
    check_case_size(c, c->small);
    path_build(c, c->large);
    large_length = g_length;
    NAOS_REQUIRE_UINT_GE(large_length, small_length);
    ratio = (large_length + small_length / 2) / small_length;
    NAOS_REQUIRE_UINT_GE(ratio, 4);
    if (limits) {
        max_depth = c->depth_base + c->depth_step * c->large;
        max_nodes = c->nodes_base + c->nodes_step * c->large;
    }
    large_time = timed_parses(max_depth, max_nodes, 0, &large_peak);
    {
        unsigned long again = timed_parses(max_depth, max_nodes, 0,
                                           &large_peak);

        if (again < large_time) {
            large_time = again;
        }
    }
    NAOS_REQUIRE_INT_EQ(parse_input(max_depth, max_nodes), AMARK_ERROR_NONE);
    check_tree();
    check_case_size(c, c->large);

    NAOS_TRACE_PUSH("lengths %lu and %lu, times %lu us and %lu us, "
                    "peaks %lu and %lu bytes",
                    (unsigned long)small_length, (unsigned long)large_length,
                    small_time, large_time, (unsigned long)small_peak,
                    (unsigned long)large_peak);
    /* Linear time. */
    NAOS_CHECK_UINT_LE(large_time,
                       PATH_TIME_FACTOR * ratio * small_time +
                           PATH_TIME_SLACK);
    /* Memory in proportion to the input. */
    NAOS_CHECK_UINT_LE(large_peak,
                       PATH_PEAK_PER_BYTE * large_length + PATH_PEAK_FIXED);
    NAOS_CHECK_UINT_LE(large_peak, 2 * ratio * small_peak + PATH_PEAK_FIXED);
    NAOS_TRACE_POP();
    reset_doc();
}

NAOS_TEST(pathological_inputs_take_linear_time_and_memory)
{
    size_t i;

    for (i = 0; i < sizeof(k_path_cases) / sizeof(k_path_cases[0]); i++) {
        NAOS_TRACE_PUSH("%s", k_path_cases[i].name);
        check_path_case(&k_path_cases[i], false);
        NAOS_TRACE_POP();
    }
}

NAOS_TEST(limits_met_exactly_on_deep_inputs_take_linear_time)
{
    /*
     * With both limits at the values of the tree (the parse just fits), the
     * depth of the paragraph is counted, once for its content and not once
     * per inline node, and the limits are checked at every node: in deep
     * block quotes and in deep emphasis.
     */
    size_t i;

    for (i = 0; i < sizeof(k_path_cases) / sizeof(k_path_cases[0]); i++) {
        if (strcmp(k_path_cases[i].name, "emphasis in deep block quotes") ==
                0 ||
            strcmp(k_path_cases[i].name, "emphasis in strong emphasis, deep") ==
                0) {
            NAOS_TRACE_PUSH("%s", k_path_cases[i].name);
            check_path_case(&k_path_cases[i], true);
            NAOS_TRACE_POP();
        }
    }
}

/*
 * "[a](x" 32, 33 and 34 times, then a title that never closes: the
 * destination of the first link holds 31, 32 and 33 unclosed parentheses,
 * below, at and one past the deepest nesting the parser accepts
 * (AMARK_LINK_DEST_PAREN_MAX, 32). Each bracket reads the link after it up
 * to the title, so the long title is read at most once per bracket: 34
 * times its length at most, whatever its length.
 */
NAOS_TEST(unclosed_title_after_nested_destinations)
{
    size_t count;

    for (count = 32; count <= 34; count++) {
        NAOS_TRACE_PUSH("%lu links", (unsigned long)count);
        input_begin(5 * count + 2 + 2 * PATH_REPEATS);
        input_string("[a](x", count);
        APPEND(" \"");
        REPEAT("t ", PATH_REPEATS);
        parse_ok(0, 0, 3);
        check_size(3, 2);
        NAOS_TRACE_POP();
    }

    /*
     * The same with the parentheses closed: one link holds them all in its
     * destination while they nest at most 32 deep. One more, and the first
     * bracket is text and the second one the link.
     */
    input_begin(6 * 33);
    input_string("[a](x", 33);
    REPEAT(")", 33);
    parse_ok(0, 0, 4);
    check_size(4, 3);
    input_begin(6 * 34);
    input_string("[a](x", 34);
    REPEAT(")", 34);
    parse_ok(0, 0, 6);
    check_size(6, 3);
}

/* ---------------------------------------------------------------------- */
/* Allocation failures                                                    */
/* ---------------------------------------------------------------------- */

/* The whole tree of an input, as amark_test_dump_tree writes it. */
#define DUMP_SIZE ((size_t)64 * 1024)
static char g_dump_want[DUMP_SIZE];
static char g_dump_got[DUMP_SIZE];

/* Writes the whole tree of doc to buffer (DUMP_SIZE bytes). */
static void dump_whole(const amark_document_t *doc, char *buffer)
{
    NAOS_REQUIRE_UINT_LT(amark_test_dump_tree(doc, buffer, DUMP_SIZE),
                         DUMP_SIZE);
}

/*
 * Every inline construct, in a paragraph, a heading, a block quote and a
 * list item: texts, emphasis and strong emphasis (with "*" and "_", nested,
 * and runs used in part), links and images of the four forms (inline with
 * and without a title and with an empty destination and title, full,
 * collapsed and shortcut, a label of other case), links in images and
 * images in links, a bracket under the link floor, delimiters inside and
 * outside a link that do not match, autolinks of both types, raw HTML of
 * the six forms, code spans, backslash escapes, character references, hard
 * and soft line breaks, and constructs that do not close (a backtick
 * string, a comment, a link). In the block quote a link, its title, a code
 * span and raw HTML run over several lines, with the prefixes in their
 * ranges. A blank line ends it, so copies of it follow one another; the
 * repeated definition is ignored, and every reference refers to the first.
 */
static const char k_every_inline[] =
    "[ref]: /url \"title\"\n"
    "\n"
    "# Heading *em* `code` [ref]\n"
    "\n"
    "Text *em* **strong** ***both*** _u_ __uu__ *a **b* c** x\\\n"
    "hard  \n"
    "soft [inline](/dest \"title\") [empty](<> \"\") "
    "![image *alt*](/img 'img title')\n"
    "[full][ref] [collapsed][] [ref] ![ref] [Ref][] "
    "<https://example.com> <a@b.co>\n"
    "<span class=\"x\"> <!-- comment --> <?pi?> <!DECL x> "
    "<![CDATA[ c ]]> </span>\n"
    "`code span` `` a ` b `` \\* \\_ &amp; &#35; &#x41; &nope; "
    "[nested ![img](/i) **in** link](/u)\n"
    "[a [b](c) [d] e](f) **x [a*b**c*](d) ` open `` <!-- open [no] *a\n"
    "> quoted *emphasis\n"
    "> over lines* and [a link\n"
    "> over lines](/u\n"
    "> \"title\n"
    "> over lines\") with `code\n"
    "> span` and <span\n"
    "> attr=\"v\">\n"
    "- item [x][ref] *list* <x@y.z>\n"
    "  lazy **strong** &#0;\n"
    "\n";

/*
 * Requires the tree of g_doc to hold a node of every inline kind, a link of
 * each of the four forms and an autolink of each type, so that a test with
 * k_every_inline reaches every construct.
 */
static void check_every_inline_kind(void)
{
    amark_node_id_t root = amark_document_root(g_doc);
    amark_node_id_t node = root;
    bool kinds[AMARK_KIND_CHARACTER_REFERENCE - AMARK_KIND_TEXT + 1];
    bool forms[AMARK_LINK_SHORTCUT + 1];
    bool types[AMARK_AUTOLINK_EMAIL + 1];
    size_t i;

    memset(kinds, 0, sizeof(kinds));
    memset(forms, 0, sizeof(forms));
    memset(types, 0, sizeof(types));
    while (node != AMARK_NODE_NONE) {
        amark_kind_t kind = amark_node_kind(g_doc, node);
        amark_node_id_t next = amark_node_first_child(g_doc, node);

        if (kind >= AMARK_KIND_TEXT &&
            kind <= AMARK_KIND_CHARACTER_REFERENCE) {
            kinds[kind - AMARK_KIND_TEXT] = true;
        }
        if (kind == AMARK_KIND_LINK || kind == AMARK_KIND_IMAGE) {
            forms[amark_link_form(g_doc, node)] = true;
        }
        if (kind == AMARK_KIND_AUTOLINK) {
            types[amark_autolink_type(g_doc, node)] = true;
        }
        while (next == AMARK_NODE_NONE && node != root) {
            next = amark_node_next_sibling(g_doc, node);
            if (next == AMARK_NODE_NONE) {
                node = amark_node_parent(g_doc, node);
            }
        }
        node = next;
    }
    for (i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
        NAOS_TRACE_PUSH("kind %lu", (unsigned long)(AMARK_KIND_TEXT + i));
        NAOS_CHECK_TRUE(kinds[i]);
        NAOS_TRACE_POP();
    }
    for (i = AMARK_LINK_INLINE; i <= AMARK_LINK_SHORTCUT; i++) {
        NAOS_TRACE_PUSH("link form %lu", (unsigned long)i);
        NAOS_CHECK_TRUE(forms[i]);
        NAOS_TRACE_POP();
    }
    NAOS_CHECK_TRUE(types[AMARK_AUTOLINK_URI]);
    NAOS_CHECK_TRUE(types[AMARK_AUTOLINK_EMAIL]);
}

/*
 * Makes the n-th allocation of a parse of the input with the two limits
 * fail, for n = 1, 2, 3, ... until the parse succeeds. Every failed parse
 * must return AMARK_ERROR_NOMEM, store NULL in *out and leave no block but
 * the input (the block parse, the inline parse and the document released
 * alike); the parse that succeeds must give the whole tree of a parse with
 * no failure, and leave no block but the input once the document is
 * destroyed. Returns the number of allocations of a parse with no failure.
 */
static size_t inject_failures(size_t max_depth, size_t max_nodes)
{
    size_t calls;
    size_t before;
    size_t n;
    bool succeeded = false;

    /* The tree and the number of allocations with no failure. */
    before = g_state.malloc_calls;
    NAOS_REQUIRE_INT_EQ(parse_input(max_depth, max_nodes), AMARK_ERROR_NONE);
    calls = g_state.malloc_calls - before;
    check_tree();
    dump_whole(g_doc, g_dump_want);
    reset_doc();
    /* The document and the two scratch arenas. */
    NAOS_REQUIRE_UINT_GE(calls, 3);

    for (n = 1; !succeeded && n <= calls + 1; n++) {
        amark_error_t err;

        NAOS_TRACE_PUSH("allocation %lu of %lu", (unsigned long)n,
                        (unsigned long)calls);
        g_state.fail_at = g_state.malloc_calls + n;
        err = parse_input(max_depth, max_nodes);
        g_state.fail_at = 0;
        if (err == AMARK_ERROR_NONE) {
            succeeded = true;
            check_tree();
            dump_whole(g_doc, g_dump_got);
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
    NAOS_CHECK_UINT_EQ(n, calls + 2);
    return calls;
}

/* The largest number of copies of k_every_inline in one input. */
#define EVERY_INLINE_COPIES 6

/*
 * A failure at each allocation of a parse of 1 to EVERY_INLINE_COPIES
 * copies of k_every_inline: the arenas and the arrays grow at other points
 * of the parse with each number of copies. Also with both limits set to
 * the values of the tree, so that the depth of each content is counted.
 */
NAOS_TEST(allocation_failure_at_every_call_with_every_inline_kind)
{
    size_t copies;
    size_t nodes;
    size_t walked;
    size_t depth;

    for (copies = 1; copies <= EVERY_INLINE_COPIES; copies++) {
        NAOS_TRACE_PUSH("%lu copies", (unsigned long)copies);
        input_begin((sizeof(k_every_inline) - 1) * copies);
        REPEAT(k_every_inline, copies);
        NAOS_REQUIRE_INT_EQ(parse_input(0, 0), AMARK_ERROR_NONE);
        check_tree();
        check_every_inline_kind();
        nodes = amark_document_node_count(g_doc);
        tree_size(&walked, &depth);
        reset_doc();
        /* Several blocks of the document arena and of each scratch arena,
         * and a growth of each array. */
        NAOS_CHECK_UINT_GE(inject_failures(0, 0), 3 + copies);
        inject_failures(depth, nodes);
        NAOS_TRACE_POP();
    }
}

/*
 * A paragraph that closes links, images and emphasis on the last bytes of
 * the content: a reference image in an inline link with emphasis inside
 * and outside, a full reference, a code span, an autolink and a character
 * reference. Before it, 0 to PAD_MAX copies of a padding: with each number
 * of copies, the arrays of the scratch arena and the nodes of the document
 * arena grow at a later step of the paragraph, so a failure reaches each
 * step of the inline parse in turn (an item appended, a link or an
 * emphasis wrapped, the attributes of a link stored, a delimiter and a
 * bracket pushed, the record of the backtick strings grown, a node
 * written).
 */
static const char k_padded_definition[] = "[ref]: /url\n\n";
static const char k_padded_tail[] =
    " *a [b *c* ![d][ref] **e**](f \"t\") `g` <h@i.j> &amp; [k][ref]*";
#define PAD_MAX 300

/*
 * The paddings: a prefix, then the unit repeated, each unit one to three
 * items (one or three, so that the items that grow the arrays fall on
 * every step in turn), and what each one makes grow.
 */
typedef struct padding {
    const char *prefix;
    const char *unit;
} padding_t;

static const padding_t k_paddings[] = {
    {"", "\\*"},         /* the items and the nodes, one by one */
    {"", "[a](b)"},      /* the attributes of the links */
    {"", "*a*"},         /* the delimiters and the wraps of emphasis */
    {"", "\\\n"},        /* hard line breaks after a backslash */
    {"", "x\\*  \n"},    /* hard line breaks after spaces */
    {"", "x\\*\n"},      /* soft line breaks */
    {"` x ", "``"},      /* a backtick string as long as the padding,
                            which the string before it records: the
                            record of the strings */
};

NAOS_TEST(allocation_failure_at_every_call_at_every_position)
{
    size_t i;
    size_t pad;

    for (i = 0; i < sizeof(k_paddings) / sizeof(k_paddings[0]); i++) {
        const padding_t *padding = &k_paddings[i];
        size_t prefix = strlen(padding->prefix);
        size_t unit = strlen(padding->unit);

        for (pad = 0; pad <= PAD_MAX; pad++) {
            NAOS_TRACE_PUSH("\"%s\" and %lu times \"%s\"", padding->prefix,
                            (unsigned long)pad, padding->unit);
            input_begin(sizeof(k_padded_definition) - 1 + prefix +
                        unit * pad + sizeof(k_padded_tail) - 1);
            APPEND(k_padded_definition);
            input_string(padding->prefix, 1);
            input_string(padding->unit, pad);
            APPEND(k_padded_tail);
            inject_failures(0, 0);
            NAOS_TRACE_POP();
        }
    }
}

/* ---------------------------------------------------------------------- */
/* The same input gives the same tree                                     */
/* ---------------------------------------------------------------------- */

/* Requires a reader of a range to give the same answer in both documents. */
#define CHECK_SAME_RANGE(reader, node)                                      \
    do {                                                                    \
        amark_range_t a_ = {0, 0};                                          \
        amark_range_t b_ = {0, 0};                                          \
        bool has_a_ = reader(g_doc, (node), &a_);                           \
        bool has_b_ = reader(g_other, (node), &b_);                         \
        NAOS_CHECK_INT_EQ(has_b_ ? 1 : 0, has_a_ ? 1 : 0);                  \
        NAOS_CHECK_UINT_EQ(b_.start, a_.start);                             \
        NAOS_CHECK_UINT_EQ(b_.end, a_.end);                                 \
    } while (0)

/*
 * Requires g_other to have the same nodes as g_doc, ID by ID: the same
 * kind, class, links, range, lines and attributes. The IDs of removed nodes
 * are compared too: both documents must answer them alike.
 */
static void check_same_nodes(void)
{
    size_t count;
    amark_node_id_t node;

    NAOS_REQUIRE_TRUE(g_other != NULL && g_other != k_sentinel);
    count = amark_document_node_count(g_doc);
    NAOS_REQUIRE_UINT_EQ(amark_document_node_count(g_other), count);
    NAOS_CHECK_UINT_EQ(amark_document_root(g_other),
                       amark_document_root(g_doc));
    for (node = 1; node <= count; node++) {
        NAOS_TRACE_PUSH("node %lu", (unsigned long)node);
        NAOS_CHECK_UINT_EQ(amark_node_kind(g_other, node),
                           amark_node_kind(g_doc, node));
        NAOS_CHECK_INT_EQ(amark_node_is_inline(g_other, node) ? 1 : 0,
                          amark_node_is_inline(g_doc, node) ? 1 : 0);
        NAOS_CHECK_UINT_EQ(amark_node_parent(g_other, node),
                           amark_node_parent(g_doc, node));
        NAOS_CHECK_UINT_EQ(amark_node_first_child(g_other, node),
                           amark_node_first_child(g_doc, node));
        NAOS_CHECK_UINT_EQ(amark_node_next_sibling(g_other, node),
                           amark_node_next_sibling(g_doc, node));
        CHECK_SAME_RANGE(amark_node_range, node);
        NAOS_CHECK_UINT_EQ(amark_node_start_line(g_other, node),
                           amark_node_start_line(g_doc, node));
        NAOS_CHECK_UINT_EQ(amark_node_end_line(g_other, node),
                           amark_node_end_line(g_doc, node));
        NAOS_CHECK_INT_EQ(amark_link_form(g_other, node),
                          amark_link_form(g_doc, node));
        CHECK_SAME_RANGE(amark_link_destination, node);
        CHECK_SAME_RANGE(amark_link_title, node);
        NAOS_CHECK_UINT_EQ(amark_link_definition(g_other, node),
                           amark_link_definition(g_doc, node));
        NAOS_CHECK_INT_EQ(amark_autolink_type(g_other, node),
                          amark_autolink_type(g_doc, node));
        CHECK_SAME_RANGE(amark_definition_label, node);
        CHECK_SAME_RANGE(amark_definition_destination, node);
        CHECK_SAME_RANGE(amark_definition_title, node);
        NAOS_TRACE_POP();
    }
}

/*
 * Parses length bytes at content with the two limits through amark_parse
 * into *out, with the counting allocator; *out must hold no document.
 */
static amark_error_t parse_into(const char *content, size_t length,
                                size_t max_depth, size_t max_nodes,
                                amark_document_t **out)
{
    amark_config_t config;

    AMARK_CONFIG_INIT(&config);
    config.allocator = g_allocator;
    config.max_nesting_depth = max_depth;
    config.max_node_count = max_nodes;
    *out = k_sentinel;
    return amark_parse(content, length, &config, out);
}

/* Destroys g_doc, keeping g_other. */
static void destroy_doc(void)
{
    if (g_doc != k_sentinel) {
        amark_document_destroy(g_doc);
    }
    g_doc = NULL;
}

/*
 * Parses EVERY_INLINE_COPIES copies of k_every_inline into g_other, then
 * into g_doc again and again: right after, after a parse of another input
 * that grew the arenas otherwise, after a parse that failed at its last
 * allocation (in the inline parse), and with the limits set to the values
 * of the tree. Each time the two trees must be byte for byte the same, and
 * the same node ID by node ID.
 */
NAOS_TEST(same_input_gives_the_same_tree)
{
    static const char other[] = "*a* [b](c)\n\n`d` <e@f.g>";
    size_t nodes;
    size_t walked;
    size_t depth;
    size_t calls;
    size_t before;
    int round;

    input_begin((sizeof(k_every_inline) - 1) * EVERY_INLINE_COPIES);
    REPEAT(k_every_inline, EVERY_INLINE_COPIES);
    before = g_state.malloc_calls;
    NAOS_REQUIRE_INT_EQ(parse_input(0, 0), AMARK_ERROR_NONE);
    calls = g_state.malloc_calls - before;
    check_tree();
    check_every_inline_kind();
    dump_whole(g_doc, g_dump_want);
    nodes = amark_document_node_count(g_doc);
    tree_size(&walked, &depth);
    /* The first document is kept in g_other. */
    g_other = g_doc;
    g_doc = NULL;

    for (round = 0; round < 4; round++) {
        NAOS_TRACE_PUSH("round %d", round);
        if (round == 1) {
            /* Another input, whose document is destroyed first. */
            NAOS_REQUIRE_INT_EQ(parse_into(other, sizeof(other) - 1, 0, 0,
                                           &g_doc),
                                AMARK_ERROR_NONE);
            destroy_doc();
        } else if (round == 2) {
            /* A parse that fails at its last allocation. */
            g_state.fail_at = g_state.malloc_calls + calls;
            NAOS_CHECK_INT_EQ(parse_into(g_input, g_length, 0, 0, &g_doc),
                              AMARK_ERROR_NOMEM);
            g_state.fail_at = 0;
            NAOS_CHECK_NULL(g_doc);
        }
        NAOS_REQUIRE_INT_EQ(parse_into(g_input, g_length,
                                       round == 3 ? depth : 0,
                                       round == 3 ? nodes : 0, &g_doc),
                            AMARK_ERROR_NONE);
        check_tree();
        dump_whole(g_doc, g_dump_got);
        NAOS_CHECK_STR_EQ(g_dump_got, g_dump_want);
        check_same_nodes();
        destroy_doc();
        NAOS_TRACE_POP();
    }
}

/* ---------------------------------------------------------------------- */

NAOS_SUITE(limits,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(depth_limit_with_deep_emphasis),
    NAOS_ADD_TEST(depth_limit_with_links_and_images),
    NAOS_ADD_TEST(depth_limit_in_deep_containers),
    NAOS_ADD_TEST(node_count_limit_with_inline_nodes),
    NAOS_ADD_TEST(node_count_limit_with_every_paragraph)
)

NAOS_SUITE(deep_nesting,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(emphasis_nested_100000_deep),
    NAOS_ADD_TEST(brackets_nested_100000_deep),
    NAOS_ADD_TEST(images_nested_100000_deep)
)

NAOS_SUITE(large_inputs,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(pathological_inputs_take_linear_time_and_memory),
    NAOS_ADD_TEST(limits_met_exactly_on_deep_inputs_take_linear_time),
    NAOS_ADD_TEST(unclosed_title_after_nested_destinations)
)

NAOS_SUITE(failures,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(allocation_failure_at_every_call_with_every_inline_kind),
    NAOS_ADD_TEST(allocation_failure_at_every_call_at_every_position)
)

NAOS_SUITE(determinism,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(same_input_gives_the_same_tree)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(limits),
    NAOS_ADD_SUITE(deep_nesting),
    NAOS_ADD_SUITE(large_inputs),
    NAOS_ADD_SUITE(failures),
    NAOS_ADD_SUITE(determinism)
)
