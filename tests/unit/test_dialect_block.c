/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of the block hooks of a dialect. The reply of a close rule
 * (src/dialect_reply.h): valid replies of the three modes, and for each
 * reason a reply that breaks only that condition (2.7, 2.8, 6.5, 6.6,
 * 6.10, 10.1, 10.2, 10.8, 10.9, 10.12). They run on a document and a run
 * state built with the internal functions.
 */

#include "dialect_internal.h"
#include "dialect_reply.h"
#include "document.h"
#include "line.h"
#include "parse_data.h"
#include "test_alloc.h"
#include "test_dialect_defs.h"

#include <amark/amark.h>
#include <amark/dialect.h>

#include <naos/naos.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define K_BOX AMARK_TEST_DEF_BOX   /* container of "first" */
#define K_NOTE AMARK_TEST_DEF_NOTE /* leaf of "first" */
#define K_MARK AMARK_TEST_DEF_MARK /* inline of "first" */
#define K_RULE AMARK_TEST_DEF_RULE /* leaf of "second" */
#define K_PARA AMARK_KIND_PARAGRAPH
#define TOP AMARK_DIALECT_TOP

/*
 * Three lines of a block quote. Line 1 is "> aa" (0-3, LF at 4), line 2
 * "> bb" (5-8, LF at 9), line 3 "> cc" (10-13, LF at 14). The content of
 * the lines starts at 2, 7 and 12 and ends at 4, 9 and 14. The paragraph
 * being closed is [2, 14).
 */
static const char k_content[] = "> aa\n> bb\n> cc\n";
#define CONTENT_LENGTH (sizeof(k_content) - 1)
#define PARA_START 2
#define PARA_END 14

static amark_test_alloc_t g_state;
static amark_allocator_t g_allocator;
static amark_document_t *g_doc;
static amark_test_def_t g_def;
static amark_dialect_run_t g_run;
static int g_run_begun;
static amark_dialect_reply_t *g_reply;

static amark_range_t rng(size_t start, size_t end)
{
    amark_range_t r;

    r.start = start;
    r.end = end;
    return r;
}

NAOS_SETUP(reply)
{
    amark_test_alloc_init(&g_state);
    g_allocator = amark_test_allocator(&g_state);
    g_doc = NULL;
    g_reply = NULL;
    g_run_begun = 0;
    memset(&g_run, 0, sizeof(g_run));
    amark_test_def_init(&g_def);
    NAOS_REQUIRE_TRUE(amark_dialect_validate(&g_def.dialect));
    NAOS_REQUIRE_INT_EQ(
        amark_document_create(&g_allocator,
                              (const unsigned char *)k_content,
                              CONTENT_LENGTH, 0, CONTENT_LENGTH, NULL, &g_doc),
        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_parse_data_create(g_doc), AMARK_ERROR_NONE);
    amark_line_set(g_doc, 1, 2, 0);
    amark_line_set(g_doc, 2, 7, 0);
    amark_line_set(g_doc, 3, 12, 0);
    NAOS_REQUIRE_INT_EQ(amark_dialect_run_begin(&g_run, &g_def.dialect,
                                                &g_allocator, g_doc),
                        AMARK_ERROR_NONE);
    g_run_begun = 1;
}

NAOS_TEARDOWN(reply)
{
    g_state.fail_at = 0;
    if (g_run_begun) {
        amark_dialect_run_end(&g_run);
        g_run_begun = 0;
    }
    amark_document_destroy(g_doc);
    g_doc = NULL;
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
    NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
}

/* Begins a reply of extension ext. */
static void begin(size_t ext, amark_dialect_reply_mode_t mode,
                  amark_kind_t kind, amark_range_t range)
{
    g_reply = NULL;
    NAOS_REQUIRE_INT_EQ(amark_dialect_reply_begin(&g_run, ext, mode, kind,
                                                  range, &g_reply),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_TRUE(g_reply != NULL);
}

static void begin_paragraph(size_t ext)
{
    begin(ext, AMARK_DIALECT_REPLY_PARAGRAPH, K_PARA,
          rng(PARA_START, PARA_END));
}

static size_t block_count(void)
{
    return amark_array_len(&g_reply->blocks);
}

static const amark_dialect_reply_block_t *block_at(size_t index)
{
    return (const amark_dialect_reply_block_t *)amark_array_at(
        &g_reply->blocks, index);
}

/* Adds a block that must be accepted and returns its index. */
static size_t add_ok(size_t parent, amark_kind_t kind, size_t start,
                     size_t end)
{
    size_t index = 12345;

    NAOS_CHECK_INT_EQ(amark_dialect_reply_add_block(g_reply, parent, kind,
                                                    rng(start, end), &index),
                      AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(index, block_count() - 1);
    return index;
}

/* Finishes a reply that must be valid. */
static void finish_valid(void)
{
    bool valid = false;

    NAOS_CHECK_INT_EQ(amark_dialect_reply_finish(&g_run, g_reply, &valid),
                      AMARK_ERROR_NONE);
    NAOS_CHECK_TRUE(valid);
    NAOS_CHECK_UINT_EQ(amark_document_diagnostic_count(g_doc), 0);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 1);
}

/*
 * After the one invalid request of the test: later requests return
 * AMARK_ERROR_INVALID_INPUT and change nothing, finish adds exactly one
 * diagnostic with the text of the extension ("first" or "second") and the
 * reason, at the start of the closing block, and no node is made (10.12).
 */
static void check_rejected(const char *ext_name, const char *reason,
                           size_t offset)
{
    char expected[160];
    size_t count = block_count();
    size_t bytes = amark_array_len(&g_reply->bytes);
    size_t index = 777;
    bool valid = true;
    amark_diagnostic_t diag;

    NAOS_CHECK_TRUE(g_reply->rejected);
    if (g_reply->mode == AMARK_DIALECT_REPLY_PARAGRAPH) {
        NAOS_CHECK_INT_EQ(
            amark_dialect_reply_add_block(g_reply, TOP, K_PARA,
                                          rng(12, 14), &index),
            AMARK_ERROR_INVALID_INPUT);
        NAOS_CHECK_UINT_EQ(index, 777);
    }
    if (count > 0) {
        NAOS_CHECK_INT_EQ(amark_dialect_reply_set_data(g_reply, 0, "zz", 2),
                          AMARK_ERROR_INVALID_INPUT);
        NAOS_CHECK_INT_EQ(amark_dialect_reply_set_inline(g_reply, 0,
                                                         rng(12, 14)),
                          AMARK_ERROR_INVALID_INPUT);
    }
    NAOS_CHECK_UINT_EQ(block_count(), count);
    NAOS_CHECK_UINT_EQ(amark_array_len(&g_reply->bytes), bytes);

    NAOS_CHECK_INT_EQ(amark_dialect_reply_finish(&g_run, g_reply, &valid),
                      AMARK_ERROR_NONE);
    NAOS_CHECK_FALSE(valid);
    NAOS_REQUIRE_UINT_EQ(amark_document_diagnostic_count(g_doc), 1);
    NAOS_REQUIRE_TRUE(amark_document_diagnostic(g_doc, 0, &diag));
    (void)strcpy(expected, "dialect \"");
    (void)strcat(expected, ext_name);
    (void)strcat(expected, "\": block close: ");
    (void)strcat(expected, reason);
    NAOS_CHECK_STR_EQ(diag.message, expected);
    NAOS_CHECK_UINT_EQ(diag.offset, offset);
    NAOS_CHECK_INT_EQ(diag.severity, AMARK_SEVERITY_ERROR);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 1);
}

/* An add_block that must be the invalid request. */
static void add_bad(size_t parent, amark_kind_t kind, size_t start,
                    size_t end)
{
    size_t count = block_count();
    size_t index = 555;

    NAOS_CHECK_INT_EQ(amark_dialect_reply_add_block(g_reply, parent, kind,
                                                    rng(start, end), &index),
                      AMARK_ERROR_INVALID_INPUT);
    NAOS_CHECK_UINT_EQ(index, 555);
    NAOS_CHECK_UINT_EQ(block_count(), count);
}

/* ---------------------------------------------------------------------- */
/* Valid replies */

NAOS_TEST(paragraph_reply_with_nested_blocks_is_valid)
{
    size_t box, note, part;
    const amark_dialect_reply_block_t *b;

    begin_paragraph(0);
    NAOS_CHECK_UINT_EQ(block_count(), 0);
    box = add_ok(TOP, K_BOX, 2, 9);
    note = add_ok(box, K_NOTE, 2, 4);
    NAOS_CHECK_INT_EQ(amark_dialect_reply_set_inline(g_reply, note,
                                                     rng(2, 4)),
                      AMARK_ERROR_NONE);
    NAOS_CHECK_INT_EQ(amark_dialect_reply_set_data(g_reply, box, "xy", 2),
                      AMARK_ERROR_NONE);
    (void)add_ok(box, K_NOTE, 7, 9);
    part = add_ok(TOP, K_PARA, 12, 14);
    NAOS_CHECK_UINT_EQ(block_count(), 4);

    b = block_at(box);
    NAOS_CHECK_UINT_EQ(b->kind, K_BOX);
    NAOS_CHECK_UINT_EQ(b->parent, TOP);
    NAOS_CHECK_UINT_EQ(b->level, 0);
    NAOS_CHECK_TRUE(b->has_children);
    NAOS_CHECK_UINT_EQ(b->child_end, 9);
    NAOS_REQUIRE_UINT_EQ(b->data_size, 2);
    NAOS_CHECK_TRUE(memcmp((const unsigned char *)amark_array_at(
                               &g_reply->bytes, b->data_offset),
                           "xy", 2) == 0);
    b = block_at(note);
    NAOS_CHECK_UINT_EQ(b->parent, box);
    NAOS_CHECK_UINT_EQ(b->level, 1);
    NAOS_CHECK_TRUE(b->has_inline);
    NAOS_CHECK_UINT_EQ(b->inline_range.start, 2);
    NAOS_CHECK_UINT_EQ(b->inline_range.end, 4);
    NAOS_CHECK_UINT_EQ(b->data_size, 0);
    b = block_at(part);
    NAOS_CHECK_UINT_EQ(b->kind, K_PARA);
    NAOS_CHECK_UINT_EQ(b->range.start, 12);
    NAOS_CHECK_UINT_EQ(b->range.end, 14);
    finish_valid();
}

NAOS_TEST(paragraph_part_may_span_lines_and_end_at_the_paragraph)
{
    begin_paragraph(0);
    (void)add_ok(TOP, K_PARA, 2, 4);
    (void)add_ok(TOP, K_PARA, 7, 14);
    finish_valid();

    /* A paragraph that ends before the end of its last line. */
    begin(0, AMARK_DIALECT_REPLY_PARAGRAPH, K_PARA, rng(2, 13));
    (void)add_ok(TOP, K_PARA, 7, 13);
    finish_valid();
}

NAOS_TEST(blocks_may_leave_gaps_and_be_empty)
{
    begin_paragraph(1);
    (void)add_ok(TOP, K_RULE, 3, 3);
    (void)add_ok(TOP, K_RULE, 3, 5);
    (void)add_ok(TOP, K_RULE, 10, 14);
    finish_valid();
}

NAOS_TEST(empty_paragraph_reply_keeps_the_paragraph_without_diagnostic)
{
    bool valid = true;

    begin_paragraph(0);
    NAOS_CHECK_INT_EQ(amark_dialect_reply_finish(&g_run, g_reply, &valid),
                      AMARK_ERROR_NONE);
    NAOS_CHECK_FALSE(valid);
    NAOS_CHECK_UINT_EQ(amark_document_diagnostic_count(g_doc), 0);
}

NAOS_TEST(leaf_reply_sets_data_and_inline_content_of_block_0)
{
    const amark_dialect_reply_block_t *b;

    begin(0, AMARK_DIALECT_REPLY_LEAF, K_NOTE, rng(2, 14));
    NAOS_REQUIRE_UINT_EQ(block_count(), 1);
    NAOS_CHECK_INT_EQ(amark_dialect_reply_set_data(g_reply, 0, "abc", 3),
                      AMARK_ERROR_NONE);
    NAOS_CHECK_INT_EQ(amark_dialect_reply_set_inline(g_reply, 0,
                                                     rng(7, 14)),
                      AMARK_ERROR_NONE);
    b = block_at(0);
    NAOS_CHECK_UINT_EQ(b->kind, K_NOTE);
    NAOS_CHECK_INT_EQ(b->cls, AMARK_DIALECT_LEAF);
    NAOS_CHECK_UINT_EQ(b->range.start, 2);
    NAOS_CHECK_UINT_EQ(b->range.end, 14);
    NAOS_CHECK_UINT_EQ(b->data_size, 3);
    NAOS_CHECK_TRUE(b->has_inline);
    NAOS_CHECK_UINT_EQ(b->inline_range.start, 7);
    NAOS_CHECK_UINT_EQ(b->inline_range.end, 14);
    finish_valid();
}

NAOS_TEST(leaf_reply_with_nothing_set_is_valid)
{
    begin(1, AMARK_DIALECT_REPLY_LEAF, K_RULE, rng(7, 9));
    finish_valid();
}

NAOS_TEST(set_data_replaces_and_clears)
{
    const amark_dialect_reply_block_t *b;

    begin(0, AMARK_DIALECT_REPLY_CONTAINER, K_BOX, rng(2, 14));
    NAOS_CHECK_INT_EQ(amark_dialect_reply_set_data(g_reply, 0, "abc", 3),
                      AMARK_ERROR_NONE);
    NAOS_CHECK_INT_EQ(amark_dialect_reply_set_data(g_reply, 0, "de", 2),
                      AMARK_ERROR_NONE);
    b = block_at(0);
    NAOS_REQUIRE_UINT_EQ(b->data_size, 2);
    NAOS_CHECK_TRUE(memcmp((const unsigned char *)amark_array_at(
                               &g_reply->bytes, b->data_offset),
                           "de", 2) == 0);
    NAOS_CHECK_INT_EQ(amark_dialect_reply_set_data(g_reply, 0, NULL, 0),
                      AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(block_at(0)->data_size, 0);
    finish_valid();
}

NAOS_TEST(begin_empties_the_reply_and_reuses_it)
{
    amark_dialect_reply_t *first;

    begin_paragraph(0);
    first = g_reply;
    add_bad(TOP, K_RULE, 2, 4);
    NAOS_CHECK_TRUE(g_reply->rejected);

    begin_paragraph(0);
    NAOS_CHECK_TRUE(g_reply == first);
    NAOS_CHECK_TRUE(g_run.reply == first);
    NAOS_CHECK_FALSE(g_reply->rejected);
    NAOS_CHECK_UINT_EQ(block_count(), 0);
    (void)add_ok(TOP, K_BOX, 2, 14);
    finish_valid();

    begin(0, AMARK_DIALECT_REPLY_LEAF, K_NOTE, rng(2, 4));
    NAOS_CHECK_TRUE(g_reply == first);
    NAOS_CHECK_UINT_EQ(block_count(), 1);
    finish_valid();
}

NAOS_TEST(null_reply_is_invalid)
{
    size_t index = 9;

    NAOS_CHECK_INT_EQ(amark_dialect_reply_add_block(NULL, TOP, K_BOX,
                                                    rng(2, 4), &index),
                      AMARK_ERROR_INVALID_INPUT);
    NAOS_CHECK_INT_EQ(amark_dialect_reply_set_data(NULL, 0, "a", 1),
                      AMARK_ERROR_INVALID_INPUT);
    NAOS_CHECK_INT_EQ(amark_dialect_reply_set_inline(NULL, 0, rng(2, 4)),
                      AMARK_ERROR_INVALID_INPUT);
    NAOS_CHECK_UINT_EQ(index, 9);
    NAOS_CHECK_UINT_EQ(amark_document_diagnostic_count(g_doc), 0);
}

/* ---------------------------------------------------------------------- */
/* STATUS: a NULL argument the contract does not define (10.4) */

NAOS_TEST(status_add_block_without_index)
{
    begin_paragraph(0);
    NAOS_CHECK_INT_EQ(amark_dialect_reply_add_block(g_reply, TOP, K_BOX,
                                                    rng(2, 4), NULL),
                      AMARK_ERROR_INVALID_INPUT);
    NAOS_CHECK_UINT_EQ(block_count(), 0);
    check_rejected("first", "unknown return value", PARA_START);
}

NAOS_TEST(status_set_data_without_data)
{
    begin_paragraph(0);
    (void)add_ok(TOP, K_BOX, 2, 4);
    NAOS_CHECK_INT_EQ(amark_dialect_reply_set_data(g_reply, 0, NULL, 3),
                      AMARK_ERROR_INVALID_INPUT);
    NAOS_CHECK_UINT_EQ(block_at(0)->data_size, 0);
    NAOS_CHECK_UINT_EQ(amark_array_len(&g_reply->bytes), 0);
    check_rejected("first", "unknown return value", PARA_START);
}

/* ---------------------------------------------------------------------- */
/* INDEX: parent is not an earlier block */

NAOS_TEST(index_parent_not_added_yet)
{
    begin_paragraph(0);
    add_bad(0, K_NOTE, 2, 4);
    check_rejected("first", "parent is not an earlier block", PARA_START);
}

NAOS_TEST(index_parent_after_the_last_block)
{
    begin_paragraph(0);
    (void)add_ok(TOP, K_BOX, 2, 9);
    add_bad(1, K_NOTE, 2, 4);
    check_rejected("first", "parent is not an earlier block", PARA_START);
}

NAOS_TEST(index_set_data_of_a_missing_block)
{
    begin_paragraph(0);
    (void)add_ok(TOP, K_BOX, 2, 9);
    NAOS_CHECK_INT_EQ(amark_dialect_reply_set_data(g_reply, 1, "a", 1),
                      AMARK_ERROR_INVALID_INPUT);
    NAOS_CHECK_UINT_EQ(amark_array_len(&g_reply->bytes), 0);
    check_rejected("first", "parent is not an earlier block", PARA_START);
}

NAOS_TEST(index_set_inline_beyond_block_0_of_a_leaf)
{
    begin(0, AMARK_DIALECT_REPLY_LEAF, K_NOTE, rng(2, 14));
    NAOS_CHECK_INT_EQ(amark_dialect_reply_set_inline(g_reply, 1, rng(2, 4)),
                      AMARK_ERROR_INVALID_INPUT);
    NAOS_CHECK_FALSE(block_at(0)->has_inline);
    check_rejected("first", "parent is not an earlier block", PARA_START);
}

NAOS_TEST(index_set_inline_on_an_empty_paragraph_reply)
{
    begin_paragraph(0);
    NAOS_CHECK_INT_EQ(amark_dialect_reply_set_inline(g_reply, 0, rng(2, 4)),
                      AMARK_ERROR_INVALID_INPUT);
    check_rejected("first", "parent is not an earlier block", PARA_START);
}

/* ---------------------------------------------------------------------- */
/* KIND: kind not registered by the extension (2.7) */

NAOS_TEST(kind_of_another_extension)
{
    begin_paragraph(0);
    (void)add_ok(TOP, K_BOX, 2, 9);
    add_bad(TOP, K_RULE, 12, 14);
    check_rejected("first", "kind not registered by the extension",
                   PARA_START);
}

NAOS_TEST(kind_registered_by_no_extension)
{
    begin_paragraph(1);
    add_bad(TOP, AMARK_KIND_DIALECT_MIN + 99, 2, 4);
    check_rejected("second", "kind not registered by the extension",
                   PARA_START);
}

NAOS_TEST(kind_of_commonmark_other_than_paragraph)
{
    begin_paragraph(1);
    add_bad(TOP, AMARK_KIND_DOCUMENT, 2, 4);
    check_rejected("second", "kind not registered by the extension",
                   PARA_START);
}

/* ---------------------------------------------------------------------- */
/* NESTING: node not allowed at this place (2.8) */

#define NESTING_TEXT "node not allowed at this place"

NAOS_TEST(nesting_inline_kind_as_a_block)
{
    begin_paragraph(0);
    add_bad(TOP, K_MARK, 2, 4);
    check_rejected("first", NESTING_TEXT, PARA_START);
}

NAOS_TEST(nesting_child_of_a_leaf)
{
    begin_paragraph(0);
    (void)add_ok(TOP, K_NOTE, 2, 9);
    add_bad(0, K_NOTE, 2, 4);
    check_rejected("first", NESTING_TEXT, PARA_START);
}

NAOS_TEST(nesting_child_of_a_paragraph_part)
{
    begin_paragraph(0);
    (void)add_ok(TOP, K_PARA, 2, 9);
    add_bad(0, K_NOTE, 2, 4);
    check_rejected("first", NESTING_TEXT, PARA_START);
}

NAOS_TEST(nesting_paragraph_part_inside_a_block)
{
    begin_paragraph(0);
    (void)add_ok(TOP, K_BOX, 2, 9);
    add_bad(0, K_PARA, 2, 4);
    check_rejected("first", NESTING_TEXT, PARA_START);
}

NAOS_TEST(nesting_block_added_to_a_leaf_reply)
{
    begin(0, AMARK_DIALECT_REPLY_LEAF, K_NOTE, rng(7, 14));
    add_bad(TOP, K_NOTE, 7, 9);
    check_rejected("first", NESTING_TEXT, 7);
}

NAOS_TEST(nesting_block_added_to_a_container_reply)
{
    begin(0, AMARK_DIALECT_REPLY_CONTAINER, K_BOX, rng(2, 14));
    add_bad(0, K_NOTE, 2, 4);
    check_rejected("first", NESTING_TEXT, PARA_START);
}

NAOS_TEST(nesting_data_of_a_paragraph_part)
{
    begin_paragraph(0);
    (void)add_ok(TOP, K_PARA, 2, 4);
    NAOS_CHECK_INT_EQ(amark_dialect_reply_set_data(g_reply, 0, "a", 1),
                      AMARK_ERROR_INVALID_INPUT);
    NAOS_CHECK_UINT_EQ(block_at(0)->data_size, 0);
    check_rejected("first", NESTING_TEXT, PARA_START);
}

/* ---------------------------------------------------------------------- */
/* BOUNDS: range leaves the content (10.8) */

NAOS_TEST(bounds_start_after_end)
{
    begin_paragraph(0);
    add_bad(TOP, K_BOX, 9, 7);
    check_rejected("first", "range leaves the content", PARA_START);
}

NAOS_TEST(bounds_end_after_the_content)
{
    begin_paragraph(0);
    add_bad(TOP, K_BOX, 2, CONTENT_LENGTH + 1);
    check_rejected("first", "range leaves the content", PARA_START);
}

NAOS_TEST(bounds_huge_values)
{
    begin_paragraph(0);
    add_bad(TOP, K_BOX, SIZE_MAX, SIZE_MAX);
    check_rejected("first", "range leaves the content", PARA_START);
}

NAOS_TEST(bounds_inline_range_after_the_content)
{
    begin(0, AMARK_DIALECT_REPLY_LEAF, K_NOTE, rng(2, 14));
    NAOS_CHECK_INT_EQ(amark_dialect_reply_set_inline(g_reply, 0,
                                                     rng(SIZE_MAX - 1,
                                                         SIZE_MAX)),
                      AMARK_ERROR_INVALID_INPUT);
    NAOS_CHECK_FALSE(block_at(0)->has_inline);
    check_rejected("first", "range leaves the content", PARA_START);
}

/* ---------------------------------------------------------------------- */
/* PARENT: range leaves the parent (6.10) */

#define PARENT_TEXT "range leaves the parent"

NAOS_TEST(parent_top_block_before_the_paragraph)
{
    begin_paragraph(0);
    add_bad(TOP, K_BOX, 0, 4);
    check_rejected("first", PARENT_TEXT, PARA_START);
}

NAOS_TEST(parent_top_block_after_the_paragraph)
{
    begin_paragraph(0);
    add_bad(TOP, K_BOX, 12, 15);
    check_rejected("first", PARENT_TEXT, PARA_START);
}

NAOS_TEST(parent_child_outside_its_parent)
{
    begin_paragraph(0);
    (void)add_ok(TOP, K_BOX, 2, 9);
    add_bad(0, K_NOTE, 7, 12);
    check_rejected("first", PARENT_TEXT, PARA_START);
}

NAOS_TEST(parent_inline_range_outside_the_block)
{
    begin_paragraph(0);
    (void)add_ok(TOP, K_NOTE, 7, 9);
    NAOS_CHECK_INT_EQ(amark_dialect_reply_set_inline(g_reply, 0,
                                                     rng(7, 12)),
                      AMARK_ERROR_INVALID_INPUT);
    NAOS_CHECK_FALSE(block_at(0)->has_inline);
    check_rejected("first", PARENT_TEXT, PARA_START);
}

/* ---------------------------------------------------------------------- */
/* ORDER: siblings overlap or are out of order (6.10) */

#define ORDER_TEXT "siblings overlap or are out of order"

NAOS_TEST(order_top_siblings_overlap)
{
    begin_paragraph(0);
    (void)add_ok(TOP, K_BOX, 2, 9);
    add_bad(TOP, K_NOTE, 7, 14);
    check_rejected("first", ORDER_TEXT, PARA_START);
}

NAOS_TEST(order_top_siblings_out_of_order)
{
    begin_paragraph(0);
    (void)add_ok(TOP, K_NOTE, 7, 9);
    add_bad(TOP, K_NOTE, 2, 4);
    check_rejected("first", ORDER_TEXT, PARA_START);
}

NAOS_TEST(order_children_out_of_order)
{
    begin_paragraph(0);
    (void)add_ok(TOP, K_BOX, 2, 14);
    (void)add_ok(0, K_NOTE, 7, 9);
    add_bad(0, K_NOTE, 2, 4);
    check_rejected("first", ORDER_TEXT, PARA_START);
}

/* ---------------------------------------------------------------------- */
/* INLINE: inline content not allowed here (6.10) */

#define INLINE_TEXT "inline content not allowed here"

NAOS_TEST(inline_on_a_container)
{
    begin_paragraph(0);
    (void)add_ok(TOP, K_BOX, 2, 9);
    NAOS_CHECK_INT_EQ(amark_dialect_reply_set_inline(g_reply, 0, rng(2, 4)),
                      AMARK_ERROR_INVALID_INPUT);
    NAOS_CHECK_FALSE(block_at(0)->has_inline);
    check_rejected("first", INLINE_TEXT, PARA_START);
}

NAOS_TEST(inline_on_a_paragraph_part)
{
    begin_paragraph(0);
    (void)add_ok(TOP, K_PARA, 2, 4);
    NAOS_CHECK_INT_EQ(amark_dialect_reply_set_inline(g_reply, 0, rng(2, 4)),
                      AMARK_ERROR_INVALID_INPUT);
    check_rejected("first", INLINE_TEXT, PARA_START);
}

NAOS_TEST(inline_on_block_0_of_a_container_reply)
{
    begin(0, AMARK_DIALECT_REPLY_CONTAINER, K_BOX, rng(2, 14));
    NAOS_CHECK_INT_EQ(amark_dialect_reply_set_inline(g_reply, 0, rng(2, 4)),
                      AMARK_ERROR_INVALID_INPUT);
    check_rejected("first", INLINE_TEXT, PARA_START);
}

/* ---------------------------------------------------------------------- */
/* PREFIX: range ends inside a line prefix */

#define PREFIX_TEXT "range ends inside a line prefix"

NAOS_TEST(prefix_inline_range_starts_in_a_prefix)
{
    begin(0, AMARK_DIALECT_REPLY_LEAF, K_NOTE, rng(2, 14));
    NAOS_CHECK_INT_EQ(amark_dialect_reply_set_inline(g_reply, 0, rng(6, 9)),
                      AMARK_ERROR_INVALID_INPUT);
    NAOS_CHECK_FALSE(block_at(0)->has_inline);
    check_rejected("first", PREFIX_TEXT, PARA_START);
}

NAOS_TEST(prefix_inline_range_ends_in_a_prefix)
{
    begin(0, AMARK_DIALECT_REPLY_LEAF, K_NOTE, rng(2, 14));
    NAOS_CHECK_INT_EQ(amark_dialect_reply_set_inline(g_reply, 0,
                                                     rng(2, 11)),
                      AMARK_ERROR_INVALID_INPUT);
    check_rejected("first", PREFIX_TEXT, PARA_START);
}

NAOS_TEST(prefix_inline_range_starts_on_a_line_ending)
{
    /* Offset 4 is the end of line 1's content and is allowed; 5 (the
       first byte of line 2) is inside its prefix. */
    begin(0, AMARK_DIALECT_REPLY_LEAF, K_NOTE, rng(2, 14));
    NAOS_CHECK_INT_EQ(amark_dialect_reply_set_inline(g_reply, 0, rng(4, 9)),
                      AMARK_ERROR_NONE);
    NAOS_CHECK_INT_EQ(amark_dialect_reply_set_inline(g_reply, 0, rng(5, 9)),
                      AMARK_ERROR_INVALID_INPUT);
    check_rejected("first", PREFIX_TEXT, PARA_START);
}

/* ---------------------------------------------------------------------- */
/* PARAGRAPH: paragraph part does not cover whole lines (6.10) */

#define PARAGRAPH_TEXT "paragraph part does not cover whole lines"

NAOS_TEST(paragraph_part_starts_inside_a_line)
{
    begin_paragraph(0);
    add_bad(TOP, K_PARA, 3, 4);
    check_rejected("first", PARAGRAPH_TEXT, PARA_START);
}

NAOS_TEST(paragraph_part_ends_inside_a_line)
{
    begin_paragraph(0);
    add_bad(TOP, K_PARA, 7, 8);
    check_rejected("first", PARAGRAPH_TEXT, PARA_START);
}

NAOS_TEST(paragraph_part_starts_in_a_prefix)
{
    begin_paragraph(0);
    add_bad(TOP, K_PARA, 5, 9);
    check_rejected("first", PARAGRAPH_TEXT, PARA_START);
}

/* ---------------------------------------------------------------------- */
/* The first violation is the one reported */

NAOS_TEST(only_the_first_violation_is_reported)
{
    begin_paragraph(0);
    (void)add_ok(TOP, K_BOX, 2, 9);
    add_bad(TOP, K_BOX, 0, 4);          /* PARENT */
    add_bad(TOP, K_RULE, 12, 14);       /* would be KIND */
    NAOS_CHECK_INT_EQ(g_reply->reason, AMARK_DIALECT_REASON_PARENT);
    check_rejected("first", PARENT_TEXT, PARA_START);
}

/* ---------------------------------------------------------------------- */
/* Lasting failures */

NAOS_TEST(failed_allocation_lasts_and_is_returned_by_finish)
{
    static const unsigned char big[8192];
    bool valid = true;
    size_t index = 3;

    begin(0, AMARK_DIALECT_REPLY_CONTAINER, K_BOX, rng(2, 14));
    g_state.fail_at = g_state.malloc_calls + 1;
    NAOS_CHECK_INT_EQ(amark_dialect_reply_set_data(g_reply, 0, big,
                                                   sizeof(big)),
                      AMARK_ERROR_NOMEM);
    g_state.fail_at = 0;
    NAOS_CHECK_UINT_EQ(block_at(0)->data_size, 0);
    NAOS_CHECK_INT_EQ(amark_dialect_reply_set_data(g_reply, 0, "a", 1),
                      AMARK_ERROR_NOMEM);
    NAOS_CHECK_INT_EQ(amark_dialect_reply_add_block(g_reply, TOP, K_NOTE,
                                                    rng(2, 4), &index),
                      AMARK_ERROR_NOMEM);
    NAOS_CHECK_UINT_EQ(index, 3);
    NAOS_CHECK_UINT_EQ(block_at(0)->data_size, 0);
    NAOS_CHECK_INT_EQ(amark_dialect_reply_finish(&g_run, g_reply, &valid),
                      AMARK_ERROR_NOMEM);
    NAOS_CHECK_FALSE(valid);
    NAOS_CHECK_UINT_EQ(amark_document_diagnostic_count(g_doc), 0);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 1);

    /* The next reply starts without it. */
    begin(0, AMARK_DIALECT_REPLY_CONTAINER, K_BOX, rng(2, 14));
    NAOS_CHECK_INT_EQ(amark_dialect_reply_set_data(g_reply, 0, "a", 1),
                      AMARK_ERROR_NONE);
    finish_valid();
}

NAOS_TEST(oversized_data_is_a_lasting_limit)
{
    bool valid = true;

    begin(0, AMARK_DIALECT_REPLY_LEAF, K_NOTE, rng(2, 14));
    NAOS_CHECK_INT_EQ(amark_dialect_reply_set_data(g_reply, 0, "a", 1),
                      AMARK_ERROR_NONE);
    NAOS_CHECK_INT_EQ(amark_dialect_reply_set_data(g_reply, 0, "b",
                                                   SIZE_MAX),
                      AMARK_ERROR_LIMIT_EXCEEDED);
    NAOS_CHECK_INT_EQ(amark_dialect_reply_set_inline(g_reply, 0, rng(2, 4)),
                      AMARK_ERROR_LIMIT_EXCEEDED);
    NAOS_CHECK_FALSE(block_at(0)->has_inline);
    NAOS_CHECK_UINT_EQ(block_at(0)->data_size, 1);
    NAOS_CHECK_INT_EQ(amark_dialect_reply_finish(&g_run, g_reply, &valid),
                      AMARK_ERROR_LIMIT_EXCEEDED);
    NAOS_CHECK_FALSE(valid);
    NAOS_CHECK_UINT_EQ(amark_document_diagnostic_count(g_doc), 0);
}

NAOS_TEST(begin_fails_when_the_reply_cannot_be_made)
{
    amark_dialect_reply_t *reply = NULL;
    amark_error_t err = AMARK_ERROR_NOMEM;
    size_t n;

    /* Fail each allocation in turn until begin succeeds. */
    for (n = 1; n < 100; n++) {
        g_state.fail_at = g_state.malloc_calls + n;
        reply = (amark_dialect_reply_t *)&g_run;
        err = amark_dialect_reply_begin(&g_run, 0, AMARK_DIALECT_REPLY_LEAF,
                                        K_NOTE, rng(2, 14), &reply);
        g_state.fail_at = 0;
        if (err == AMARK_ERROR_NONE) {
            break;
        }
        NAOS_CHECK_INT_EQ(err, AMARK_ERROR_NOMEM);
        NAOS_CHECK_TRUE(reply == NULL);
    }
    NAOS_CHECK_INT_EQ(err, AMARK_ERROR_NONE);
    NAOS_CHECK_TRUE(reply != NULL);
    g_reply = reply;
    NAOS_CHECK_UINT_EQ(block_count(), 1);
    finish_valid();
}

NAOS_SUITE(reply,
    NAOS_SET_SETUP(reply),
    NAOS_SET_TEARDOWN(reply),
    NAOS_ADD_TEST(paragraph_reply_with_nested_blocks_is_valid),
    NAOS_ADD_TEST(paragraph_part_may_span_lines_and_end_at_the_paragraph),
    NAOS_ADD_TEST(blocks_may_leave_gaps_and_be_empty),
    NAOS_ADD_TEST(empty_paragraph_reply_keeps_the_paragraph_without_diagnostic),
    NAOS_ADD_TEST(leaf_reply_sets_data_and_inline_content_of_block_0),
    NAOS_ADD_TEST(leaf_reply_with_nothing_set_is_valid),
    NAOS_ADD_TEST(set_data_replaces_and_clears),
    NAOS_ADD_TEST(begin_empties_the_reply_and_reuses_it),
    NAOS_ADD_TEST(null_reply_is_invalid),
    NAOS_ADD_TEST(status_add_block_without_index),
    NAOS_ADD_TEST(status_set_data_without_data),
    NAOS_ADD_TEST(index_parent_not_added_yet),
    NAOS_ADD_TEST(index_parent_after_the_last_block),
    NAOS_ADD_TEST(index_set_data_of_a_missing_block),
    NAOS_ADD_TEST(index_set_inline_beyond_block_0_of_a_leaf),
    NAOS_ADD_TEST(index_set_inline_on_an_empty_paragraph_reply),
    NAOS_ADD_TEST(kind_of_another_extension),
    NAOS_ADD_TEST(kind_registered_by_no_extension),
    NAOS_ADD_TEST(kind_of_commonmark_other_than_paragraph),
    NAOS_ADD_TEST(nesting_inline_kind_as_a_block),
    NAOS_ADD_TEST(nesting_child_of_a_leaf),
    NAOS_ADD_TEST(nesting_child_of_a_paragraph_part),
    NAOS_ADD_TEST(nesting_paragraph_part_inside_a_block),
    NAOS_ADD_TEST(nesting_block_added_to_a_leaf_reply),
    NAOS_ADD_TEST(nesting_block_added_to_a_container_reply),
    NAOS_ADD_TEST(nesting_data_of_a_paragraph_part),
    NAOS_ADD_TEST(bounds_start_after_end),
    NAOS_ADD_TEST(bounds_end_after_the_content),
    NAOS_ADD_TEST(bounds_huge_values),
    NAOS_ADD_TEST(bounds_inline_range_after_the_content),
    NAOS_ADD_TEST(parent_top_block_before_the_paragraph),
    NAOS_ADD_TEST(parent_top_block_after_the_paragraph),
    NAOS_ADD_TEST(parent_child_outside_its_parent),
    NAOS_ADD_TEST(parent_inline_range_outside_the_block),
    NAOS_ADD_TEST(order_top_siblings_overlap),
    NAOS_ADD_TEST(order_top_siblings_out_of_order),
    NAOS_ADD_TEST(order_children_out_of_order),
    NAOS_ADD_TEST(inline_on_a_container),
    NAOS_ADD_TEST(inline_on_a_paragraph_part),
    NAOS_ADD_TEST(inline_on_block_0_of_a_container_reply),
    NAOS_ADD_TEST(prefix_inline_range_starts_in_a_prefix),
    NAOS_ADD_TEST(prefix_inline_range_ends_in_a_prefix),
    NAOS_ADD_TEST(prefix_inline_range_starts_on_a_line_ending),
    NAOS_ADD_TEST(paragraph_part_starts_inside_a_line),
    NAOS_ADD_TEST(paragraph_part_ends_inside_a_line),
    NAOS_ADD_TEST(paragraph_part_starts_in_a_prefix),
    NAOS_ADD_TEST(only_the_first_violation_is_reported),
    NAOS_ADD_TEST(failed_allocation_lasts_and_is_returned_by_finish),
    NAOS_ADD_TEST(oversized_data_is_a_lasting_limit),
    NAOS_ADD_TEST(begin_fails_when_the_reply_cannot_be_made)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(reply)
)
