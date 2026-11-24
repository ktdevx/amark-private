/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of the time and the stack of the text functions
 * (include/amark/text.h), through the public entry points only: the values
 * of nodes whose ranges hold many character references and backslashes
 * (decoded destinations and titles, plain logical text), of nodes over
 * many short lines (a fenced code block, a paragraph, a code span), and of
 * a paragraph and a code block in block quotes nested 100,000 deep.
 *
 * Linear time. Each input is built at a small size and at a size about 32
 * times larger (100,000 repeats, or 100,000 nested block quotes), and each
 * node and part is read at both, the buffer way and the alloc way. The
 * time of a read at the large size must stay within 10 times the ratio of
 * the lengths of the ranges read (the bytes amark_node_raw gives, at least
 * 1) times that at the small size, plus a fixed allowance of 50 ms: the
 * same measure as test_inline_limits.c, with the range read in place of
 * the input, as the time of a read depends on its range and not on the
 * document. A read that grows as the square of its range, or with the
 * number of the lines before it, reaches 1024 times. In the deep block
 * quotes the range of the paragraph (its first line and 10,000 lazy
 * continuation lines) does not grow with the depth (ratio 1), so a read
 * that does work in proportion to the depth for each line fails too. The
 * time is processor time from clock: the small read is repeated for at
 * least a twentieth of a second and the time of one is the mean; the large
 * one is measured twice and the faster counts.
 *
 * Stack. The text functions use no recursion, so the 100,000 deep
 * documents need no more stack than shallow ones. Every node and part of
 * the deep chain is read too, at both depths, with the same check of the
 * time against the ratio of the input lengths: a read whose time depends
 * on the depth of its node makes the walk quadratic in the depth.
 *
 * Several threads. Requirement 12.6 (reading one document from several
 * threads at once) is not tested here: there is no thread support in the
 * tests, and the test is left to spec-conformance (see the design of
 * test_text_limits.c). It rests on the text functions leaving the
 * document and the content unchanged (task 4.5) and the buffer way never
 * allocating (task 4.4), both in test_text.c, and on there being no
 * mutable static data (CheckObjects.cmake).
 */

#include "test_alloc.h"
#include "tree_check.h"

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
 * NAOS_REQUIRE_* leaves a test with longjmp, so the document, the input
 * and the buffer a test makes are kept in static variables, and teardown
 * releases them and checks that no block is left and that every call
 * received its context value.
 */
static amark_test_alloc_t g_state;
static amark_allocator_t g_allocator;
static amark_document_t *g_doc;

/* The input of a test, allocated with the counting allocator. */
static char *g_input;
static size_t g_length;
static size_t g_capacity;

/* The buffer of the buffer way, allocated with the counting allocator. */
static char *g_buffer;
static size_t g_buffer_capacity;

NAOS_SETUP(counting)
{
    amark_test_alloc_init(&g_state);
    g_allocator = amark_test_allocator(&g_state);
    g_doc = NULL;
    g_input = NULL;
    g_length = 0;
    g_capacity = 0;
    g_buffer = NULL;
    g_buffer_capacity = 0;
}

static void release_all(void)
{
    amark_document_destroy(g_doc);
    g_doc = NULL;
    if (g_input != NULL) {
        g_allocator.free_fn(g_allocator.ctx, g_input);
    }
    g_input = NULL;
    if (g_buffer != NULL) {
        g_allocator.free_fn(g_allocator.ctx, g_buffer);
    }
    g_buffer = NULL;
    g_buffer_capacity = 0;
}

NAOS_TEARDOWN(counting)
{
    release_all();
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
    NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
}

/* ---------------------------------------------------------------------- */
/* Building and parsing the input                                         */
/* ---------------------------------------------------------------------- */

/* Starts a new input of at most capacity bytes, releasing the old one. */
static void input_begin(size_t capacity)
{
    release_all();
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

/*
 * Parses the input into g_doc with no limits and requires the tree to pass
 * the tree check.
 */
static void parse_input(void)
{
    amark_config_t config;
    amark_tree_check_result_t check;

    amark_document_destroy(g_doc);
    g_doc = NULL;
    amark_config_init(&config);
    config.allocator = g_allocator;
    config.max_nesting_depth = 0;
    config.max_node_count = 0;
    NAOS_REQUIRE_INT_EQ(amark_parse(g_input, g_length, &config, &g_doc),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_NOT_NULL(g_doc);
    check = amark_tree_check(g_doc);
    NAOS_REQUIRE_INT_EQ(check.violation, AMARK_TREE_VIOLATION_NONE);
}

/* Returns the node after node in document order, or AMARK_NODE_NONE. */
static amark_node_id_t next_in_order(amark_node_id_t node)
{
    amark_node_id_t next = amark_node_first_child(g_doc, node);

    while (next == AMARK_NODE_NONE && node != AMARK_NODE_NONE) {
        next = amark_node_next_sibling(g_doc, node);
        if (next == AMARK_NODE_NONE) {
            node = amark_node_parent(g_doc, node);
        }
    }
    return next;
}

/* Returns the n-th node (from 0) of kind in document order, or none. */
static amark_node_id_t find(amark_kind_t kind, size_t n)
{
    amark_node_id_t node = amark_document_root(g_doc);

    while (node != AMARK_NODE_NONE) {
        if (amark_node_kind(g_doc, node) == kind) {
            if (n == 0) {
                return node;
            }
            n--;
        }
        node = next_in_order(node);
    }
    return AMARK_NODE_NONE;
}

/* ---------------------------------------------------------------------- */
/* The inputs                                                             */
/* ---------------------------------------------------------------------- */

/*
 * The number of repeats (or the depth) of the large input, and how many
 * times fewer the small one has.
 */
#define LARGE 100000
#define SCALE 32

/* The lazy continuation lines of the paragraph in the deep block quotes. */
#define LAZY 10000

/* A character reference and an escaped '*', and one with an escaped '"'. */
#define UNIT_STAR "&amp;\\*"
#define UNIT_QUOTE "&amp;\\\""

/*
 * A link whose destination and title hold n character references and n
 * backslash escapes, then a paragraph of n of each:
 *
 *   [a](/&amp;\*...&amp;\* "&amp;\"...&amp;\"")
 *
 *   &amp;\*...&amp;\*
 */
static void build_references(size_t n)
{
    input_begin(5 + 7 * n + 2 + 7 * n + 4 + 7 * n + 1);
    APPEND("[a](/");
    REPEAT(UNIT_STAR, n);
    APPEND(" \"");
    REPEAT(UNIT_QUOTE, n);
    APPEND("\")\n\n");
    REPEAT(UNIT_STAR, n);
    APPEND("\n");
}

/*
 * A fenced code block of n lines "a", a paragraph of n + 1 lines "b" and a
 * code span over n lines "c".
 */
static void build_short_lines(size_t n)
{
    input_begin(4 + 2 * n + 5 + 1 + 2 * n + 3 + 2 * n + 2);
    APPEND("```\n");
    REPEAT("a\n", n);
    APPEND("```\n\nb");
    REPEAT("\nb", n);
    APPEND("\n\n`");
    REPEAT("c\n", n);
    APPEND("`\n");
}

/*
 * In block quotes nested n deep, a paragraph "p" with lazy lazy
 * continuation lines "q", then a fenced code block holding "c", each of its
 * lines inside all n quotes.
 */
static void build_deep_chain(size_t n, size_t lazy)
{
    input_begin(4 * n + 3 + 2 * lazy + 5 + 3 + 5);
    REPEAT(">", n);
    APPEND(" p\n");
    REPEAT("q\n", lazy);
    REPEAT(">", n);
    APPEND(" ```\n");
    REPEAT(">", n);
    APPEND(" c\n");
    REPEAT(">", n);
    APPEND(" ```\n");
}

/* The deep chain with LAZY lazy continuation lines. */
static void build_deep_quotes(size_t n)
{
    build_deep_chain(n, LAZY);
}

/*
 * A node and part to read: the nth node of kind. With the input of n
 * repeats, the result has length_base + length_step n bytes, and, when raw
 * is true, it is the bytes amark_node_raw gives.
 */
typedef struct target {
    const char *name;
    amark_kind_t kind;
    size_t nth;
    amark_text_part_t part;
    size_t length_base;
    size_t length_step;
    bool raw;
} target_t;

typedef struct limit_case {
    const char *name;
    void (*build)(size_t n);
    const target_t *targets;
    size_t target_count;
} limit_case_t;

static const target_t k_reference_targets[] = {
    /* "/" and "&*" n times. */
    {"destination", AMARK_KIND_LINK, 0, AMARK_TEXT_DESTINATION, 1, 2, false},
    /* "&\"" n times. */
    {"title", AMARK_KIND_LINK, 0, AMARK_TEXT_TITLE, 0, 2, false},
    {"logical text of the link paragraph", AMARK_KIND_PARAGRAPH, 0,
     AMARK_TEXT_LOGICAL, 9, 14, true},
    {"logical text of the reference paragraph", AMARK_KIND_PARAGRAPH, 1,
     AMARK_TEXT_LOGICAL, 0, 7, true}};

static const target_t k_short_line_targets[] = {
    /* "a\n" n times. */
    {"value of the code block", AMARK_KIND_CODE_BLOCK, 0, AMARK_TEXT_VALUE,
     0, 2, false},
    /* The fences and the lines, without the last line ending. */
    {"logical text of the code block", AMARK_KIND_CODE_BLOCK, 0,
     AMARK_TEXT_LOGICAL, 7, 2, true},
    /* "b", n times "\nb". */
    {"logical text of the paragraph", AMARK_KIND_PARAGRAPH, 0,
     AMARK_TEXT_LOGICAL, 1, 2, true},
    /* "c " n times: each line ending is a space, none is stripped. */
    {"value of the code span", AMARK_KIND_CODE_SPAN, 0, AMARK_TEXT_VALUE, 0,
     2, false}};

static const target_t k_deep_targets[] = {
    /* "p", LAZY times "\nq". */
    {"logical text of the paragraph", AMARK_KIND_PARAGRAPH, 0,
     AMARK_TEXT_LOGICAL, 1 + 2 * LAZY, 0, true},
    {"value of the first text", AMARK_KIND_TEXT, 0, AMARK_TEXT_VALUE, 1, 0,
     true},
    /* "c\n". */
    {"value of the code block", AMARK_KIND_CODE_BLOCK, 0, AMARK_TEXT_VALUE,
     2, 0, false},
    /* "```\nc\n```": the prefixes of the quotes left out. */
    {"logical text of the code block", AMARK_KIND_CODE_BLOCK, 0,
     AMARK_TEXT_LOGICAL, 9, 0, false}};

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static const limit_case_t k_cases[] = {
    {"character references and backslashes", build_references,
     k_reference_targets, COUNT(k_reference_targets)},
    {"many short lines", build_short_lines, k_short_line_targets,
     COUNT(k_short_line_targets)},
    {"block quotes nested deep", build_deep_quotes, k_deep_targets,
     COUNT(k_deep_targets)}};

/* ---------------------------------------------------------------------- */
/* Reading and measuring                                                  */
/* ---------------------------------------------------------------------- */

/*
 * The time of a large read may be at most TIME_FACTOR times the ratio of
 * the ranges times that of a small one, plus TIME_SLACK nanoseconds.
 */
#define TIME_FACTOR 10
#define TIME_SLACK 50000000.0

/* Makes g_buffer hold at least capacity bytes. */
static void buffer_reserve(size_t capacity)
{
    if (g_buffer != NULL && g_buffer_capacity >= capacity) {
        return;
    }
    if (g_buffer != NULL) {
        g_allocator.free_fn(g_allocator.ctx, g_buffer);
    }
    g_buffer = (char *)g_allocator.malloc_fn(g_allocator.ctx,
                                             capacity > 0 ? capacity : 1);
    NAOS_REQUIRE_NOT_NULL(g_buffer);
    g_buffer_capacity = capacity;
}

/* Reads part of node the buffer way and the alloc way, once each. */
static void read_both(amark_node_id_t node, amark_text_part_t part,
                      size_t want)
{
    size_t length = 0;
    char *text = NULL;

    NAOS_REQUIRE_INT_EQ(amark_node_text(g_doc, node, part, g_buffer, want,
                                        &length),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_UINT_EQ(length, want);
    length = 0;
    NAOS_REQUIRE_INT_EQ(amark_node_text_alloc(g_doc, node, part, &text,
                                              &length),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_UINT_EQ(length, want);
    amark_text_free(g_doc, text);
}

/*
 * Reads part of node both ways again and again until the clock has
 * advanced by at least min_ticks (once at least). Returns the processor
 * time of one round in nanoseconds, or -1 where it cannot be read.
 */
static double timed_reads(amark_node_id_t node, amark_text_part_t part,
                          size_t want, clock_t min_ticks)
{
    size_t count = 0;
    clock_t start;
    clock_t end;

    start = clock();
    do {
        read_both(node, part, want);
        count++;
        end = clock();
    } while (start != (clock_t)-1 && end != (clock_t)-1 && end >= start &&
             end - start < min_ticks);
    if (start == (clock_t)-1 || end == (clock_t)-1 || end < start) {
        return -1.0;
    }
    return (double)(end - start) * 1e9 / CLOCKS_PER_SEC / (double)count;
}

/*
 * Checks the result of t in g_doc built with n repeats and returns the
 * node, storing in *range the length of its range.
 */
static amark_node_id_t check_target(const target_t *t, size_t n,
                                    size_t *range)
{
    amark_node_id_t node = find(t->kind, t->nth);
    size_t want = t->length_base + t->length_step * n;
    const char *raw = NULL;
    size_t raw_length = 0;
    char *text = NULL;
    size_t length = 0;

    NAOS_REQUIRE_TRUE(node != AMARK_NODE_NONE);
    NAOS_REQUIRE_TRUE(amark_node_raw(g_doc, node, &raw, &raw_length));
    *range = raw_length;
    buffer_reserve(want);
    read_both(node, t->part, want);
    NAOS_REQUIRE_INT_EQ(amark_node_text_alloc(g_doc, node, t->part, &text,
                                              &length),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(length, want);
    NAOS_CHECK_INT_EQ(memcmp(text, g_buffer, want), 0);
    if (t->raw) {
        NAOS_CHECK_UINT_EQ(raw_length, want);
        NAOS_CHECK_INT_EQ(memcmp(text, raw, want < raw_length ? want
                                                              : raw_length),
                          0);
    }
    amark_text_free(g_doc, text);
    return node;
}

/*
 * Checks every target of c at the small and the large size: its result,
 * and the time of a large read against that of a small one.
 */
static void check_case(const limit_case_t *c)
{
    const size_t small = LARGE / SCALE;
    size_t i;

    for (i = 0; i < c->target_count; i++) {
        const target_t *t = &c->targets[i];
        amark_node_id_t node;
        size_t small_range;
        size_t large_range;
        size_t ratio;
        double small_time;
        double large_time;
        double again;

        NAOS_TRACE_PUSH("%s", t->name);
        c->build(small);
        parse_input();
        node = check_target(t, small, &small_range);
        small_time = timed_reads(node, t->part,
                                 t->length_base + t->length_step * small,
                                 CLOCKS_PER_SEC / 20);

        c->build(LARGE);
        parse_input();
        node = check_target(t, LARGE, &large_range);
        large_time = timed_reads(node, t->part,
                                 t->length_base + t->length_step * LARGE, 0);
        again = timed_reads(node, t->part,
                            t->length_base + t->length_step * LARGE, 0);
        if (again < large_time) {
            large_time = again;
        }

        NAOS_REQUIRE_UINT_GE(large_range, small_range);
        ratio = small_range > 0
                    ? (large_range + small_range / 2) / small_range
                    : 1;
        if (ratio < 1) {
            ratio = 1;
        }
        NAOS_TRACE_PUSH("ranges %lu and %lu bytes, times %.0f ns and %.0f ns",
                        (unsigned long)small_range,
                        (unsigned long)large_range, small_time, large_time);
        if (small_time >= 0.0 && large_time >= 0.0) {
            NAOS_CHECK_TRUE(large_time <= (double)TIME_FACTOR *
                                                  (double)ratio *
                                                  small_time +
                                              TIME_SLACK);
        }
        NAOS_TRACE_POP();
        NAOS_TRACE_POP();
    }
}

/* ---------------------------------------------------------------------- */
/* Tests                                                                  */
/* ---------------------------------------------------------------------- */

NAOS_TEST(many_references_and_backslashes_take_linear_time)
{
    check_case(&k_cases[0]);
}

NAOS_TEST(many_short_lines_take_linear_time)
{
    check_case(&k_cases[1]);
}

NAOS_TEST(block_quotes_nested_100000_deep_take_time_of_the_range)
{
    check_case(&k_cases[2]);
}

/*
 * Reads every node of g_doc in document order: its range, and the length
 * of each part (the size-only call). Requires each block quote to have a
 * range and neither logical text nor value, and returns their number.
 */
static size_t read_every_node(void)
{
    amark_node_id_t node = amark_document_root(g_doc);
    size_t quotes = 0;

    while (node != AMARK_NODE_NONE) {
        const char *raw = NULL;
        size_t raw_length = 0;
        int part;

        NAOS_REQUIRE_TRUE(amark_node_raw(g_doc, node, &raw, &raw_length));
        for (part = AMARK_TEXT_LOGICAL; part <= AMARK_TEXT_INFO; part++) {
            size_t length = 0;
            amark_error_t err = amark_node_text(
                g_doc, node, (amark_text_part_t)part, NULL, 0, &length);

            NAOS_REQUIRE_TRUE(err == AMARK_ERROR_NONE ||
                              err == AMARK_ERROR_BUFFER_TOO_SMALL ||
                              err == AMARK_ERROR_NOT_AVAILABLE);
            if (amark_node_kind(g_doc, node) == AMARK_KIND_BLOCK_QUOTE &&
                (part == AMARK_TEXT_LOGICAL || part == AMARK_TEXT_VALUE)) {
                NAOS_REQUIRE_INT_EQ(err, AMARK_ERROR_NOT_AVAILABLE);
            }
        }
        if (amark_node_kind(g_doc, node) == AMARK_KIND_BLOCK_QUOTE) {
            NAOS_REQUIRE_TRUE(raw_length > 0);
            quotes++;
        }
        node = next_in_order(node);
    }
    return quotes;
}

/*
 * Reads every node of g_doc again and again until the clock has advanced
 * by at least min_ticks (once at least), requiring quotes block quotes.
 * Returns the processor time of one round in nanoseconds, or -1 where it
 * cannot be read.
 */
static double timed_walks(size_t quotes, clock_t min_ticks)
{
    size_t count = 0;
    clock_t start;
    clock_t end;

    start = clock();
    do {
        NAOS_REQUIRE_UINT_EQ(read_every_node(), quotes);
        count++;
        end = clock();
    } while (start != (clock_t)-1 && end != (clock_t)-1 && end >= start &&
             end - start < min_ticks);
    if (start == (clock_t)-1 || end == (clock_t)-1 || end < start) {
        return -1.0;
    }
    return (double)(end - start) * 1e9 / CLOCKS_PER_SEC / (double)count;
}

NAOS_TEST(every_node_in_block_quotes_nested_100000_deep_is_read)
{
    /*
     * Every node and part of the deep chain without lazy lines (so that the
     * input grows with the depth alone), at the small and the large depth:
     * the time grows with the number of nodes, so a read whose time
     * depends on the depth of its node fails, and the deepest nodes need no
     * more stack than shallow ones.
     */
    const size_t small = LARGE / SCALE;
    size_t small_length;
    size_t ratio;
    double small_time;
    double large_time;
    double again;

    build_deep_chain(small, 0);
    small_length = g_length;
    parse_input();
    small_time = timed_walks(small, CLOCKS_PER_SEC / 20);

    build_deep_chain(LARGE, 0);
    parse_input();
    ratio = (g_length + small_length / 2) / small_length;
    NAOS_REQUIRE_UINT_GE(ratio, 4);
    large_time = timed_walks(LARGE, 0);
    again = timed_walks(LARGE, 0);
    if (again < large_time) {
        large_time = again;
    }
    NAOS_TRACE_PUSH("lengths %lu and %lu bytes, times %.0f ns and %.0f ns",
                    (unsigned long)small_length, (unsigned long)g_length,
                    small_time, large_time);
    if (small_time >= 0.0 && large_time >= 0.0) {
        NAOS_CHECK_TRUE(large_time <=
                        (double)TIME_FACTOR * (double)ratio * small_time +
                            TIME_SLACK);
    }
    NAOS_TRACE_POP();
}

/* ---------------------------------------------------------------------- */

NAOS_SUITE(large_inputs,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(many_references_and_backslashes_take_linear_time),
    NAOS_ADD_TEST(many_short_lines_take_linear_time),
    NAOS_ADD_TEST(block_quotes_nested_100000_deep_take_time_of_the_range)
)

NAOS_SUITE(deep_nesting,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(every_node_in_block_quotes_nested_100000_deep_is_read)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(large_inputs),
    NAOS_ADD_SUITE(deep_nesting)
)
