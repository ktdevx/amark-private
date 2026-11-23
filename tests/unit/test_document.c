/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of the document (src/document.h): create and destroy, and
 * adding and reading diagnostics. Nodes are added with the tree builder
 * (src/tree_internal.h) where a test needs nodes and diagnostics together.
 */

#include "document.h"
#include "test_alloc.h"
#include "tree_internal.h"
#include "tree_check.h"

#include <naos/naos.h>

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/*
 * Fixture. NAOS_REQUIRE_* leaves a test with longjmp, so the document a test
 * creates is kept in g_doc, and teardown (which always runs) destroys it and
 * then checks that no block is left and that every call received the
 * context value it was given.
 */
#define LARGE_INPUT 100000

static amark_test_alloc_t g_state;
static amark_allocator_t g_allocator;
static amark_document_t *g_doc;
static unsigned char g_large[LARGE_INPUT];

NAOS_SETUP(counting)
{
    amark_test_alloc_init(&g_state);
    g_allocator = amark_test_allocator(&g_state);
    g_doc = NULL;
}

NAOS_TEARDOWN(counting)
{
    g_state.fail_at = 0;
    amark_document_destroy(g_doc);
    g_doc = NULL;
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
    NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
}

/*
 * "ab\n" is line 1 (offsets 0-2), "cd\r\n" line 2 (3-6), "ef\r" line 3
 * (7-9) and "gh" line 4 (10-11); offset 12 (the length) is on line 4.
 */
static const char k_mixed[] = "ab\ncd\r\nef\rgh";
#define MIXED_LENGTH (sizeof(k_mixed) - 1)

static const amark_node_t *node_at(const amark_document_t *doc,
                                   amark_node_id_t id)
{
    return (const amark_node_t *)amark_array_at(&doc->nodes, id);
}

/* Creates g_doc over k_mixed with the range [start, end). */
static amark_error_t create_mixed(size_t start, size_t end,
                                  const char *filename)
{
    return amark_document_create(&g_allocator,
                                 (const unsigned char *)k_mixed, MIXED_LENGTH,
                                 start, end, filename, &g_doc);
}

/* Checks that g_doc holds only the root, with the given range and lines. */
static void check_only_root(size_t start, size_t end, size_t start_line,
                            size_t end_line)
{
    const amark_node_t *root;
    const amark_node_t *unused;

    NAOS_REQUIRE_TRUE(g_doc != NULL);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 1);
    NAOS_REQUIRE_UINT_EQ(amark_array_len(&g_doc->nodes), 2);

    unused = node_at(g_doc, AMARK_NODE_NONE);
    NAOS_CHECK_UINT_EQ(unused->kind, AMARK_KIND_NONE);

    root = node_at(g_doc, 1);
    NAOS_CHECK_UINT_EQ(root->kind, AMARK_KIND_DOCUMENT);
    NAOS_CHECK_UINT_EQ(root->is_inline, 0);
    NAOS_CHECK_UINT_EQ(root->parent, AMARK_NODE_NONE);
    NAOS_CHECK_UINT_EQ(root->first_child, AMARK_NODE_NONE);
    NAOS_CHECK_UINT_EQ(root->last_child, AMARK_NODE_NONE);
    NAOS_CHECK_UINT_EQ(root->prev_sibling, AMARK_NODE_NONE);
    NAOS_CHECK_UINT_EQ(root->next_sibling, AMARK_NODE_NONE);
    NAOS_CHECK_UINT_EQ(root->attr, 0);
    NAOS_CHECK_UINT_EQ(root->start, start);
    NAOS_CHECK_UINT_EQ(root->end, end);
    NAOS_CHECK_UINT_EQ(root->start_line, start_line);
    NAOS_CHECK_UINT_EQ(root->end_line, end_line);

    NAOS_CHECK_UINT_EQ(amark_array_len(&g_doc->diagnostics), 0);
}

/* ---- the root (2.2, 2.4, 2.7, 7.1) ---- */

NAOS_TEST(whole_content_root)
{
    NAOS_REQUIRE_INT_EQ(create_mixed(0, MIXED_LENGTH, NULL), AMARK_ERROR_NONE);
    check_only_root(0, MIXED_LENGTH, 1, 4);
}

NAOS_TEST(root_range_is_the_parsed_range_from_the_content_start)
{
    /* [3, 11): starts at "cd" (line 2), last byte 'g' at 10 (line 4). */
    NAOS_REQUIRE_INT_EQ(create_mixed(3, 11, NULL), AMARK_ERROR_NONE);
    check_only_root(3, 11, 2, 4);
}

NAOS_TEST(root_ending_with_a_line_ending)
{
    /* [3, 7): the last byte is the LF of CR LF at 6, on line 2. */
    NAOS_REQUIRE_INT_EQ(create_mixed(3, 7, NULL), AMARK_ERROR_NONE);
    check_only_root(3, 7, 2, 2);
}

NAOS_TEST(root_ending_between_cr_and_lf)
{
    /* [0, 6): the last byte is the CR of CR LF at 5, on line 2. */
    NAOS_REQUIRE_INT_EQ(create_mixed(0, 6, NULL), AMARK_ERROR_NONE);
    check_only_root(0, 6, 1, 2);
}

NAOS_TEST(empty_root_range)
{
    /* [7, 7): start line and end line are both the line of 7. */
    NAOS_REQUIRE_INT_EQ(create_mixed(7, 7, NULL), AMARK_ERROR_NONE);
    check_only_root(7, 7, 3, 3);
}

NAOS_TEST(empty_range_at_the_end_of_the_content)
{
    NAOS_REQUIRE_INT_EQ(create_mixed(MIXED_LENGTH, MIXED_LENGTH, NULL),
                        AMARK_ERROR_NONE);
    check_only_root(MIXED_LENGTH, MIXED_LENGTH, 4, 4);
}

NAOS_TEST(empty_content_with_null_pointer)
{
    NAOS_REQUIRE_INT_EQ(amark_document_create(&g_allocator, NULL, 0, 0, 0,
                                              NULL, &g_doc),
                        AMARK_ERROR_NONE);
    check_only_root(0, 0, 1, 1);
    NAOS_CHECK_NULL(amark_document_content(g_doc));
    NAOS_CHECK_UINT_EQ(amark_document_content_length(g_doc), 0);
    NAOS_CHECK_UINT_EQ(
        amark_line_index_line_count(amark_document_line_index(g_doc)), 1);
}

NAOS_TEST(empty_content_with_a_pointer)
{
    static const unsigned char empty[1] = {0};

    NAOS_REQUIRE_INT_EQ(amark_document_create(&g_allocator, empty, 0, 0, 0,
                                              "e.md", &g_doc),
                        AMARK_ERROR_NONE);
    check_only_root(0, 0, 1, 1);
    NAOS_CHECK_PTR_EQ(amark_document_content(g_doc), empty);
}

/* ---- accessors ---- */

NAOS_TEST(content_and_line_index_accessors)
{
    const amark_line_index_t *lines;

    NAOS_REQUIRE_INT_EQ(create_mixed(3, 11, NULL), AMARK_ERROR_NONE);
    NAOS_CHECK_PTR_EQ(amark_document_content(g_doc), k_mixed);
    NAOS_CHECK_UINT_EQ(amark_document_content_length(g_doc), MIXED_LENGTH);

    /* The index covers the whole content, not only the parsed range. */
    lines = amark_document_line_index(g_doc);
    NAOS_REQUIRE_TRUE(lines != NULL);
    NAOS_CHECK_UINT_EQ(amark_line_index_line_count(lines), 4);
    NAOS_CHECK_UINT_EQ(amark_line_index_line_at(lines, 0), 1);
    NAOS_CHECK_UINT_EQ(amark_line_index_line_at(lines, 12), 4);
}

NAOS_TEST(range_lines_follow_the_rule)
{
    size_t first = 0;
    size_t last = 0;

    NAOS_REQUIRE_INT_EQ(create_mixed(0, MIXED_LENGTH, NULL), AMARK_ERROR_NONE);

    amark_document_range_lines(g_doc, 0, 3, &first, &last); /* ends at LF */
    NAOS_CHECK_UINT_EQ(first, 1);
    NAOS_CHECK_UINT_EQ(last, 1);
    amark_document_range_lines(g_doc, 2, 4, &first, &last);
    NAOS_CHECK_UINT_EQ(first, 1);
    NAOS_CHECK_UINT_EQ(last, 2);
    amark_document_range_lines(g_doc, 4, 4, &first, &last); /* empty */
    NAOS_CHECK_UINT_EQ(first, 2);
    NAOS_CHECK_UINT_EQ(last, 2);
    amark_document_range_lines(g_doc, 7, 10, &first, &last); /* ends at CR */
    NAOS_CHECK_UINT_EQ(first, 3);
    NAOS_CHECK_UINT_EQ(last, 3);
    amark_document_range_lines(g_doc, 10, 12, &first, &last);
    NAOS_CHECK_UINT_EQ(first, 4);
    NAOS_CHECK_UINT_EQ(last, 4);
}

NAOS_TEST(filename_is_copied)
{
    char name[] = "notes.md";

    NAOS_REQUIRE_INT_EQ(create_mixed(0, MIXED_LENGTH, name), AMARK_ERROR_NONE);
    NAOS_REQUIRE_TRUE(g_doc->filename != NULL);
    NAOS_CHECK_TRUE(g_doc->filename != name);
    name[0] = 'X';
    NAOS_CHECK_STR_EQ(g_doc->filename, "notes.md");
}

NAOS_TEST(empty_filename_is_copied)
{
    NAOS_REQUIRE_INT_EQ(create_mixed(0, MIXED_LENGTH, ""), AMARK_ERROR_NONE);
    NAOS_REQUIRE_TRUE(g_doc->filename != NULL);
    NAOS_CHECK_STR_EQ(g_doc->filename, "");
}

NAOS_TEST(no_filename_stays_null)
{
    NAOS_REQUIRE_INT_EQ(create_mixed(0, MIXED_LENGTH, NULL), AMARK_ERROR_NONE);
    NAOS_CHECK_NULL(g_doc->filename);
}

/* ---- memory (5.1, 5.2, 5.3, 5.5, 2.9, 7.7) ---- */

NAOS_TEST(create_and_destroy_release_everything)
{
    NAOS_REQUIRE_INT_EQ(create_mixed(0, MIXED_LENGTH, "a.md"),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_GT(g_state.live, 0);
    /* The body keeps a copy of the allocator it came from. */
    NAOS_CHECK_PTR_EQ(g_doc->allocator.ctx, &g_state);

    amark_document_destroy(g_doc);
    g_doc = NULL;
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
    NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
    NAOS_CHECK_UINT_EQ(g_state.free_calls, g_state.malloc_calls);
}

NAOS_TEST(destroy_null_calls_nothing)
{
    amark_document_destroy(NULL);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 0);
    NAOS_CHECK_UINT_EQ(g_state.free_calls, 0);
}

NAOS_TEST(content_bytes_are_unchanged)
{
    unsigned char content[sizeof(k_mixed)];
    unsigned char before[sizeof(k_mixed)];

    memcpy(content, k_mixed, sizeof(k_mixed));
    memcpy(before, content, sizeof(content));
    NAOS_REQUIRE_INT_EQ(amark_document_create(&g_allocator, content,
                                              MIXED_LENGTH, 0, MIXED_LENGTH,
                                              "c.md", &g_doc),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_TRUE(memcmp(content, before, sizeof(content)) == 0);
    amark_document_destroy(g_doc);
    g_doc = NULL;
    NAOS_CHECK_TRUE(memcmp(content, before, sizeof(content)) == 0);
}

/* Fills g_large with text whose lines end at random with LF, CR or CR LF. */
static size_t fill_large(void)
{
    size_t i = 0;
    size_t lines = 1;
    unsigned seed = 12345u;

    while (i < LARGE_INPUT) {
        unsigned r;

        seed = seed * 1103515245u + 12345u;
        r = (seed >> 16) % 30u;
        if (r == 0) {
            g_large[i++] = '\n';
            lines++;
        } else if (r == 1) {
            g_large[i++] = '\r';
            if (i < LARGE_INPUT) {
                g_large[i++] = '\n';
            }
            lines++;
        } else if (r == 2) {
            g_large[i++] = '\r';
            if (i < LARGE_INPUT && g_large[i - 1] == '\r') {
                g_large[i++] = 'x';
            }
            lines++;
        } else {
            g_large[i++] = (unsigned char)('a' + r);
        }
    }
    return lines;
}

NAOS_TEST(large_content)
{
    size_t lines = fill_large();
    const amark_node_t *root;

    NAOS_REQUIRE_INT_EQ(amark_document_create(&g_allocator, g_large,
                                              LARGE_INPUT, 0, LARGE_INPUT,
                                              "big.md", &g_doc),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(
        amark_line_index_line_count(amark_document_line_index(g_doc)), lines);
    root = node_at(g_doc, 1);
    NAOS_CHECK_UINT_EQ(root->start_line, 1);
    NAOS_CHECK_UINT_EQ(root->end_line,
                       amark_line_index_line_at(&g_doc->lines,
                                                LARGE_INPUT - 1));
}

/* ---- allocation failures (7.8, 7.10) ---- */

/*
 * Makes the 1st, 2nd, 3rd, ... call to the allocator fail until create
 * succeeds. Each failure returns NOMEM, stores NULL and leaves no block.
 */
static size_t failure_loop(const unsigned char *content, size_t length,
                           size_t start, size_t end, const char *filename)
{
    size_t fail_at;
    size_t failures = 0;

    for (fail_at = 1; fail_at < 1000; fail_at++) {
        amark_document_t sentinel_storage;
        amark_document_t *doc = &sentinel_storage;
        amark_error_t err;

        amark_test_alloc_init(&g_state);
        g_state.fail_at = fail_at;
        err = amark_document_create(&g_allocator, content, length, start, end,
                                    filename, &doc);
        if (err == AMARK_ERROR_NONE) {
            g_doc = doc;
            break;
        }
        failures++;
        NAOS_CHECK_INT_EQ(err, AMARK_ERROR_NOMEM);
        NAOS_CHECK_NULL(doc);
        NAOS_CHECK_UINT_EQ(g_state.malloc_calls, fail_at);
        NAOS_CHECK_UINT_EQ(g_state.live, 0);
        NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
    }
    g_state.fail_at = 0;
    return failures;
}

NAOS_TEST(failure_injection_loop)
{
    size_t failures = failure_loop((const unsigned char *)k_mixed,
                                   MIXED_LENGTH, 3, 11, "loop.md");

    /* At least the body and one arena block can fail. */
    NAOS_CHECK_UINT_GE(failures, 2);
    check_only_root(3, 11, 2, 4);
    NAOS_CHECK_STR_EQ(g_doc->filename, "loop.md");
}

NAOS_TEST(failure_injection_loop_large_content)
{
    size_t failures;

    (void)fill_large();
    failures = failure_loop(g_large, LARGE_INPUT, 0, LARGE_INPUT, "big.md");
    NAOS_CHECK_UINT_GE(failures, 2);
    NAOS_REQUIRE_TRUE(g_doc != NULL);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 1);
}

NAOS_TEST(failure_injection_loop_empty_content)
{
    size_t failures = failure_loop(NULL, 0, 0, 0, NULL);

    NAOS_CHECK_UINT_GE(failures, 2);
    check_only_root(0, 0, 1, 1);
}

/* ---- diagnostics (11.3 to 11.11) ---- */

/* Large enough for a message longer than any normal arena block here. */
#define BIG_MESSAGE 65536
static char g_big[BIG_MESSAGE + 1];

/* Fills g_big with a message of length bytes ('m' repeated). */
static const char *big_message(size_t length)
{
    memset(g_big, 'm', length);
    g_big[length] = '\0';
    return g_big;
}

/* A diagnostic with every member set to a value no call returns. */
static amark_diagnostic_t poisoned_diagnostic(void)
{
    amark_diagnostic_t d;

    memset(&d, 0, sizeof(d));
    d.offset = 777;
    d.line = 888;
    d.severity = AMARK_SEVERITY_ERROR;
    d.message = "poison";
    d.filename = "poison.md";
    return d;
}

static void check_poisoned(const amark_diagnostic_t *d)
{
    NAOS_CHECK_UINT_EQ(d->offset, 777);
    NAOS_CHECK_UINT_EQ(d->line, 888);
    NAOS_CHECK_INT_EQ(d->severity, AMARK_SEVERITY_ERROR);
    NAOS_CHECK_STR_EQ(d->message, "poison");
    NAOS_CHECK_STR_EQ(d->filename, "poison.md");
}

NAOS_TEST(no_diagnostics_after_create)
{
    amark_diagnostic_t d = poisoned_diagnostic();

    NAOS_REQUIRE_INT_EQ(create_mixed(0, MIXED_LENGTH, "a.md"),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(amark_document_diagnostic_count(g_doc), 0);
    NAOS_CHECK_FALSE(amark_document_diagnostic(g_doc, 0, &d));
    check_poisoned(&d);
}

NAOS_TEST(three_diagnostics_in_the_order_added)
{
    char name[] = "notes.md";
    char m1[] = "first";
    char m2[] = "second";
    char m3[] = "third";
    amark_diagnostic_t d;

    NAOS_REQUIRE_INT_EQ(create_mixed(0, MIXED_LENGTH, name), AMARK_ERROR_NONE);
    /* Offsets 4 (line 2), 0 (line 1) and 12, the content length (line 4). */
    NAOS_REQUIRE_INT_EQ(amark_document_add_diagnostic(
                            g_doc, 4, AMARK_SEVERITY_WARNING, m1),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_document_add_diagnostic(
                            g_doc, 0, AMARK_SEVERITY_ERROR, m2),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_document_add_diagnostic(
                            g_doc, MIXED_LENGTH, AMARK_SEVERITY_WARNING, m3),
                        AMARK_ERROR_NONE);

    /* The strings passed in are changed; the document holds copies. */
    name[0] = 'X';
    m1[0] = 'X';
    m2[0] = 'X';
    m3[0] = 'X';

    NAOS_REQUIRE_UINT_EQ(amark_document_diagnostic_count(g_doc), 3);

    NAOS_REQUIRE_TRUE(amark_document_diagnostic(g_doc, 0, &d));
    NAOS_CHECK_UINT_EQ(d.offset, 4);
    NAOS_CHECK_UINT_EQ(d.line, 2);
    NAOS_CHECK_INT_EQ(d.severity, AMARK_SEVERITY_WARNING);
    NAOS_CHECK_STR_EQ(d.message, "first");
    NAOS_CHECK_TRUE(d.message != m1);
    NAOS_CHECK_STR_EQ(d.filename, "notes.md");
    NAOS_CHECK_PTR_EQ(d.filename, g_doc->filename);

    NAOS_REQUIRE_TRUE(amark_document_diagnostic(g_doc, 1, &d));
    NAOS_CHECK_UINT_EQ(d.offset, 0);
    NAOS_CHECK_UINT_EQ(d.line, 1);
    NAOS_CHECK_INT_EQ(d.severity, AMARK_SEVERITY_ERROR);
    NAOS_CHECK_STR_EQ(d.message, "second");
    NAOS_CHECK_STR_EQ(d.filename, "notes.md");

    NAOS_REQUIRE_TRUE(amark_document_diagnostic(g_doc, 2, &d));
    NAOS_CHECK_UINT_EQ(d.offset, MIXED_LENGTH);
    NAOS_CHECK_UINT_EQ(d.line, 4);
    NAOS_CHECK_INT_EQ(d.severity, AMARK_SEVERITY_WARNING);
    NAOS_CHECK_STR_EQ(d.message, "third");
    NAOS_CHECK_STR_EQ(d.filename, "notes.md");
}

NAOS_TEST(diagnostic_line_follows_the_offset)
{
    /* Offset and line of each byte of k_mixed, and of its end. */
    static const size_t lines[MIXED_LENGTH + 1] = {
        1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4
    };
    size_t offset;
    amark_diagnostic_t d;

    NAOS_REQUIRE_INT_EQ(create_mixed(3, 11, NULL), AMARK_ERROR_NONE);
    for (offset = 0; offset <= MIXED_LENGTH; offset++) {
        NAOS_REQUIRE_INT_EQ(amark_document_add_diagnostic(
                                g_doc, offset, AMARK_SEVERITY_WARNING, "x"),
                            AMARK_ERROR_NONE);
    }
    NAOS_REQUIRE_UINT_EQ(amark_document_diagnostic_count(g_doc),
                         MIXED_LENGTH + 1);
    for (offset = 0; offset <= MIXED_LENGTH; offset++) {
        NAOS_REQUIRE_TRUE(amark_document_diagnostic(g_doc, offset, &d));
        NAOS_CHECK_UINT_EQ(d.offset, offset);
        NAOS_CHECK_UINT_EQ(d.line, lines[offset]);
    }
}

NAOS_TEST(empty_message_is_copied)
{
    amark_diagnostic_t d;

    NAOS_REQUIRE_INT_EQ(create_mixed(0, MIXED_LENGTH, NULL), AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_document_add_diagnostic(
                            g_doc, 1, AMARK_SEVERITY_ERROR, ""),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_TRUE(amark_document_diagnostic(g_doc, 0, &d));
    NAOS_REQUIRE_TRUE(d.message != NULL);
    NAOS_CHECK_STR_EQ(d.message, "");
}

NAOS_TEST(diagnostic_without_filename)
{
    amark_diagnostic_t d = poisoned_diagnostic();

    NAOS_REQUIRE_INT_EQ(create_mixed(0, MIXED_LENGTH, NULL), AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_document_add_diagnostic(
                            g_doc, 7, AMARK_SEVERITY_WARNING, "w"),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_TRUE(amark_document_diagnostic(g_doc, 0, &d));
    NAOS_CHECK_NULL(d.filename);
    NAOS_CHECK_STR_EQ(d.message, "w");
    NAOS_CHECK_UINT_EQ(d.line, 3);
}

NAOS_TEST(diagnostic_with_empty_content)
{
    amark_diagnostic_t d;

    NAOS_REQUIRE_INT_EQ(amark_document_create(&g_allocator, NULL, 0, 0, 0,
                                              "e.md", &g_doc),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_document_add_diagnostic(
                            g_doc, 0, AMARK_SEVERITY_ERROR, "empty"),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_TRUE(amark_document_diagnostic(g_doc, 0, &d));
    NAOS_CHECK_UINT_EQ(d.offset, 0);
    NAOS_CHECK_UINT_EQ(d.line, 1);
    NAOS_CHECK_STR_EQ(d.filename, "e.md");
}

NAOS_TEST(no_diagnostic_for_unanswerable_calls)
{
    amark_diagnostic_t d = poisoned_diagnostic();

    NAOS_CHECK_UINT_EQ(amark_document_diagnostic_count(NULL), 0);
    NAOS_CHECK_FALSE(amark_document_diagnostic(NULL, 0, &d));
    check_poisoned(&d);

    NAOS_REQUIRE_INT_EQ(create_mixed(0, MIXED_LENGTH, "a.md"),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_document_add_diagnostic(
                            g_doc, 0, AMARK_SEVERITY_WARNING, "one"),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_document_add_diagnostic(
                            g_doc, 3, AMARK_SEVERITY_ERROR, "two"),
                        AMARK_ERROR_NONE);

    NAOS_CHECK_FALSE(amark_document_diagnostic(g_doc, 2, &d));
    check_poisoned(&d);
    NAOS_CHECK_FALSE(amark_document_diagnostic(g_doc, 3, &d));
    check_poisoned(&d);
    NAOS_CHECK_FALSE(amark_document_diagnostic(g_doc, (size_t)-1, &d));
    check_poisoned(&d);
    NAOS_CHECK_FALSE(amark_document_diagnostic(g_doc, 0, NULL));
    NAOS_CHECK_FALSE(amark_document_diagnostic(NULL, 0, NULL));

    /* The answerable calls still answer. */
    NAOS_CHECK_TRUE(amark_document_diagnostic(g_doc, 1, &d));
    NAOS_CHECK_STR_EQ(d.message, "two");
}

/* A copy of what amark_document_diagnostic returned for each index. */
#define SNAPSHOT_MAX 300
typedef struct {
    size_t count;
    amark_diagnostic_t items[SNAPSHOT_MAX];
} snapshot_t;

static snapshot_t g_before;

static void take_snapshot(snapshot_t *s)
{
    size_t i;

    s->count = amark_document_diagnostic_count(g_doc);
    NAOS_REQUIRE_UINT_LE(s->count, SNAPSHOT_MAX);
    for (i = 0; i < s->count; i++) {
        NAOS_REQUIRE_TRUE(amark_document_diagnostic(g_doc, i, &s->items[i]));
    }
}

/*
 * Checks that the diagnostics of g_doc are those of s: the same count, and
 * the same members, and the same message pointers for each (arena copies
 * are never rewritten).
 */
static void check_snapshot(const snapshot_t *s)
{
    size_t i;
    amark_diagnostic_t d;

    NAOS_REQUIRE_UINT_EQ(amark_document_diagnostic_count(g_doc), s->count);
    NAOS_CHECK_FALSE(amark_document_diagnostic(g_doc, s->count, &d));
    for (i = 0; i < s->count; i++) {
        NAOS_REQUIRE_TRUE(amark_document_diagnostic(g_doc, i, &d));
        NAOS_CHECK_UINT_EQ(d.offset, s->items[i].offset);
        NAOS_CHECK_UINT_EQ(d.line, s->items[i].line);
        NAOS_CHECK_INT_EQ(d.severity, s->items[i].severity);
        NAOS_CHECK_PTR_EQ(d.message, s->items[i].message);
        NAOS_CHECK_PTR_EQ(d.filename, s->items[i].filename);
    }
}

/* Adds count diagnostics whose messages are "d0", "d1", ... */
static void add_numbered(size_t count)
{
    size_t i;

    for (i = 0; i < count; i++) {
        char message[24];
        size_t n = amark_document_diagnostic_count(g_doc);

        message[0] = 'd';
        message[1] = (char)('0' + (char)(n % 10u));
        message[2] = '\0';
        NAOS_REQUIRE_INT_EQ(amark_document_add_diagnostic(
                                g_doc, n % (MIXED_LENGTH + 1),
                                AMARK_SEVERITY_WARNING, message),
                            AMARK_ERROR_NONE);
    }
}

NAOS_TEST(failed_message_copy_keeps_diagnostics)
{
    size_t calls;
    size_t length;
    amark_error_t err;

    NAOS_REQUIRE_INT_EQ(create_mixed(0, MIXED_LENGTH, "f.md"),
                        AMARK_ERROR_NONE);
    add_numbered(3);
    take_snapshot(&g_before);

    /*
     * A message longer than the next arena block gets a block of its own,
     * so copying it is the first call to the allocator; make it fail.
     */
    length = g_doc->arena.next_block_size;
    NAOS_REQUIRE_UINT_LE(length, BIG_MESSAGE);
    calls = g_state.malloc_calls;
    g_state.fail_at = calls + 1;
    err = amark_document_add_diagnostic(g_doc, 5, AMARK_SEVERITY_ERROR,
                                        big_message(length));
    g_state.fail_at = 0;
    NAOS_CHECK_INT_EQ(err, AMARK_ERROR_NOMEM);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, calls + 1);
    check_snapshot(&g_before);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 1);

    /* The document still works afterwards. */
    NAOS_REQUIRE_INT_EQ(amark_document_add_diagnostic(
                            g_doc, 5, AMARK_SEVERITY_ERROR, "after"),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(amark_document_diagnostic_count(g_doc), 4);
}

NAOS_TEST(failed_append_keeps_diagnostics)
{
    size_t i;
    bool failed = false;

    NAOS_REQUIRE_INT_EQ(create_mixed(0, MIXED_LENGTH, "f.md"),
                        AMARK_ERROR_NONE);
    add_numbered(2);

    /*
     * Each message gets an arena block of its own (the 1st allocator call
     * of the add, which succeeds); the 2nd call fails. The array grows from
     * the current block until a growth no longer fits there, so some add
     * needs that 2nd call: the copy succeeded and the append failed.
     */
    for (i = 0; i < SNAPSHOT_MAX - 3 && !failed; i++) {
        size_t calls;
        size_t length = g_doc->arena.next_block_size;
        amark_error_t err;

        NAOS_REQUIRE_UINT_LE(length, BIG_MESSAGE);
        take_snapshot(&g_before);
        calls = g_state.malloc_calls;
        g_state.fail_at = calls + 2;
        err = amark_document_add_diagnostic(g_doc, 2, AMARK_SEVERITY_WARNING,
                                            big_message(length));
        g_state.fail_at = 0;
        if (err == AMARK_ERROR_NONE) {
            NAOS_REQUIRE_UINT_EQ(g_state.malloc_calls, calls + 1);
            continue;
        }
        failed = true;
        NAOS_CHECK_INT_EQ(err, AMARK_ERROR_NOMEM);
        NAOS_CHECK_UINT_EQ(g_state.malloc_calls, calls + 2);
        check_snapshot(&g_before);
    }
    NAOS_CHECK_TRUE(failed);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 1);

    NAOS_REQUIRE_INT_EQ(amark_document_add_diagnostic(
                            g_doc, 0, AMARK_SEVERITY_ERROR, "after"),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(amark_document_diagnostic_count(g_doc),
                       g_before.count + 1);
}

NAOS_TEST(failure_injection_loop_add_diagnostics)
{
    size_t fail_at;
    size_t failures = 0;
    bool completed = false;

    /*
     * For each n, create a document, add diagnostics with the n-th
     * allocator call made to fail, and check that the failing call (of
     * create or of add) returns NOMEM and leaves what was there before.
     */
    for (fail_at = 1; fail_at < 200; fail_at++) {
        amark_error_t err;
        size_t i;
        bool add_failed = false;

        amark_test_alloc_init(&g_state);
        g_state.fail_at = fail_at;
        err = create_mixed(0, MIXED_LENGTH, "loop.md");
        if (err != AMARK_ERROR_NONE) {
            NAOS_CHECK_INT_EQ(err, AMARK_ERROR_NOMEM);
            NAOS_CHECK_NULL(g_doc);
            NAOS_CHECK_UINT_EQ(g_state.live, 0);
            failures++;
            continue;
        }
        for (i = 0; i < 40 && !add_failed; i++) {
            take_snapshot(&g_before);
            err = amark_document_add_diagnostic(
                g_doc, i % (MIXED_LENGTH + 1), AMARK_SEVERITY_ERROR,
                big_message(200 + 37 * i));
            if (err != AMARK_ERROR_NONE) {
                add_failed = true;
                failures++;
                NAOS_CHECK_INT_EQ(err, AMARK_ERROR_NOMEM);
                check_snapshot(&g_before);
            }
        }
        g_state.fail_at = 0;
        if (!add_failed) {
            /* No call failed: every diagnostic was added. */
            NAOS_CHECK_UINT_EQ(amark_document_diagnostic_count(g_doc), 40);
            completed = true;
        }
        amark_document_destroy(g_doc);
        g_doc = NULL;
        NAOS_CHECK_UINT_EQ(g_state.live, 0);
        NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
        if (completed) {
            break;
        }
    }
    NAOS_CHECK_TRUE(completed);
    NAOS_CHECK_UINT_GE(failures, 4);
}

#define STEP_NODES 100
#define STEP_DIAGNOSTICS 10

/*
 * Adds STEP_NODES nodes (thematic breaks, children of the root) and, after
 * every tenth node, one diagnostic, to g_doc. Stops at the first failure:
 * checks that the failed call left the node count and the diagnostics (count
 * and contents) as they were and the tree whole, and returns its error.
 * *nodes and *diagnostics receive the numbers added.
 */
static amark_error_t add_nodes_and_diagnostics(size_t *nodes,
                                               size_t *diagnostics,
                                               bool *diagnostic_failed)
{
    size_t i;

    *nodes = 0;
    *diagnostics = 0;
    *diagnostic_failed = false;
    for (i = 0; i < STEP_NODES; i++) {
        size_t count = amark_document_node_count(g_doc);
        amark_node_id_t out = 999;
        amark_error_t err;

        take_snapshot(&g_before);
        err = amark_tree_append(g_doc, 1, AMARK_KIND_THEMATIC_BREAK,
                                AMARK_NODE_CLASS_AUTO, MIXED_LENGTH,
                                MIXED_LENGTH, &out);
        if (err == AMARK_ERROR_NONE && i % 10 == 9) {
            (*nodes)++;
            count++;
            /*
             * A message longer than the next arena block gets a block of
             * its own, so every add calls the allocator at least once.
             */
            NAOS_REQUIRE_UINT_LE(g_doc->arena.next_block_size, BIG_MESSAGE);
            err = amark_document_add_diagnostic(
                g_doc, i % (MIXED_LENGTH + 1), AMARK_SEVERITY_WARNING,
                big_message(g_doc->arena.next_block_size));
            if (err == AMARK_ERROR_NONE) {
                (*diagnostics)++;
                continue;
            }
            *diagnostic_failed = true;
        } else if (err == AMARK_ERROR_NONE) {
            (*nodes)++;
            continue;
        } else {
            NAOS_CHECK_UINT_EQ(out, AMARK_NODE_NONE);
        }
        NAOS_CHECK_INT_EQ(err, AMARK_ERROR_NOMEM);
        NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), count);
        check_snapshot(&g_before);
        NAOS_CHECK_INT_EQ(amark_tree_check(g_doc).violation,
                          AMARK_TREE_VIOLATION_NONE);
        return err;
    }
    return AMARK_ERROR_NONE;
}

NAOS_TEST(failure_injection_loop_nodes_and_diagnostics)
{
    size_t fail_at;
    size_t create_failures = 0;
    size_t node_failures = 0;
    size_t diagnostic_failures = 0;
    bool completed = false;

    /*
     * For each n, create a document and add 100 nodes and 10 diagnostics
     * with the n-th allocator call made to fail. The failing call returns
     * NOMEM and changes nothing that was there, and destroying the document
     * leaves no block.
     */
    for (fail_at = 1; fail_at < 1000; fail_at++) {
        amark_error_t err;
        size_t nodes = 0;
        size_t diagnostics = 0;
        bool diagnostic_failed = false;

        amark_test_alloc_init(&g_state);
        g_state.fail_at = fail_at;
        err = create_mixed(0, MIXED_LENGTH, "steps.md");
        if (err != AMARK_ERROR_NONE) {
            NAOS_CHECK_INT_EQ(err, AMARK_ERROR_NOMEM);
            NAOS_CHECK_NULL(g_doc);
            NAOS_CHECK_UINT_EQ(g_state.live, 0);
            create_failures++;
            continue;
        }
        err = add_nodes_and_diagnostics(&nodes, &diagnostics,
                                        &diagnostic_failed);
        if (err == AMARK_ERROR_NONE) {
            NAOS_CHECK_UINT_EQ(nodes, STEP_NODES);
            NAOS_CHECK_UINT_EQ(diagnostics, STEP_DIAGNOSTICS);
            completed = true;
        } else {
            /* The failed call made the injected allocation the last one. */
            NAOS_CHECK_UINT_EQ(g_state.malloc_calls, fail_at);
            if (diagnostic_failed) {
                diagnostic_failures++;
            } else {
                node_failures++;
            }
        }
        NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), nodes + 1);
        NAOS_CHECK_UINT_EQ(amark_document_diagnostic_count(g_doc),
                           diagnostics);
        g_state.fail_at = 0;
        amark_document_destroy(g_doc);
        g_doc = NULL;
        NAOS_CHECK_UINT_EQ(g_state.live, 0);
        NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
        if (completed) {
            break;
        }
    }
    NAOS_CHECK_TRUE(completed);
    NAOS_CHECK_UINT_GE(create_failures, 2);
    /* 100 nodes and 10 copied messages outgrow the first arena block, so
     * both kinds of call meet a failing allocation. */
    NAOS_CHECK_UINT_GE(node_failures, 1);
    NAOS_CHECK_UINT_GE(diagnostic_failures, 1);
}

NAOS_SUITE(document,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(whole_content_root),
    NAOS_ADD_TEST(root_range_is_the_parsed_range_from_the_content_start),
    NAOS_ADD_TEST(root_ending_with_a_line_ending),
    NAOS_ADD_TEST(root_ending_between_cr_and_lf),
    NAOS_ADD_TEST(empty_root_range),
    NAOS_ADD_TEST(empty_range_at_the_end_of_the_content),
    NAOS_ADD_TEST(empty_content_with_null_pointer),
    NAOS_ADD_TEST(empty_content_with_a_pointer),
    NAOS_ADD_TEST(content_and_line_index_accessors),
    NAOS_ADD_TEST(range_lines_follow_the_rule),
    NAOS_ADD_TEST(filename_is_copied),
    NAOS_ADD_TEST(empty_filename_is_copied),
    NAOS_ADD_TEST(no_filename_stays_null),
    NAOS_ADD_TEST(create_and_destroy_release_everything),
    NAOS_ADD_TEST(destroy_null_calls_nothing),
    NAOS_ADD_TEST(content_bytes_are_unchanged),
    NAOS_ADD_TEST(large_content),
    NAOS_ADD_TEST(failure_injection_loop),
    NAOS_ADD_TEST(failure_injection_loop_large_content),
    NAOS_ADD_TEST(failure_injection_loop_empty_content),
    NAOS_ADD_TEST(no_diagnostics_after_create),
    NAOS_ADD_TEST(three_diagnostics_in_the_order_added),
    NAOS_ADD_TEST(diagnostic_line_follows_the_offset),
    NAOS_ADD_TEST(empty_message_is_copied),
    NAOS_ADD_TEST(diagnostic_without_filename),
    NAOS_ADD_TEST(diagnostic_with_empty_content),
    NAOS_ADD_TEST(no_diagnostic_for_unanswerable_calls),
    NAOS_ADD_TEST(failed_message_copy_keeps_diagnostics),
    NAOS_ADD_TEST(failed_append_keeps_diagnostics),
    NAOS_ADD_TEST(failure_injection_loop_add_diagnostics),
    NAOS_ADD_TEST(failure_injection_loop_nodes_and_diagnostics)
)

/* The default allocator (malloc and free) works too. */
NAOS_TEST(default_allocator_works)
{
    amark_allocator_t allocator = amark_allocator_default();
    amark_document_t *doc = NULL;

    NAOS_REQUIRE_INT_EQ(amark_document_create(&allocator,
                                              (const unsigned char *)k_mixed,
                                              MIXED_LENGTH, 0, MIXED_LENGTH,
                                              "d.md", &doc),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(doc), 1);
    amark_document_destroy(doc);
}

NAOS_SUITE(default_allocator,
    NAOS_ADD_TEST(default_allocator_works)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(document),
    NAOS_ADD_SUITE(default_allocator)
)
