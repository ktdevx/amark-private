/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of the block hooks of a dialect. The reply of a close rule
 * (src/dialect_reply.h): valid replies of the three modes, and for each
 * reason a reply that breaks only that condition (2.7, 2.8, 6.5, 6.6,
 * 6.10, 10.1, 10.2, 10.8, 10.9, 10.12); a valid reply applied to the tree
 * (2.4, 2.5, 2.6, 6.7, 10.11, 12.2); and the line readers of the rules
 * (6.3). They run on a document and a run state built with the internal
 * functions.
 */

#include "arena.h"
#include "block.h"
#include "config_internal.h"
#include "dialect_internal.h"
#include "dialect_reply.h"
#include "document.h"
#include "line.h"
#include "parse_data.h"
#include "test_alloc.h"
#include "test_dialect.h"
#include "test_dialect_defs.h"
#include "tree_check.h"
#include "tree_internal.h"

#include <amark/amark.h>
#include <amark/dialect.h>

#include <naos/naos.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
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
    size_t nodes = amark_document_node_count(g_doc);

    NAOS_CHECK_INT_EQ(amark_dialect_reply_finish(&g_run, g_reply, &valid),
                      AMARK_ERROR_NONE);
    NAOS_CHECK_TRUE(valid);
    NAOS_CHECK_UINT_EQ(amark_document_diagnostic_count(g_doc), 0);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), nodes);
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

/* ---------------------------------------------------------------------- */
/* Apply */

/*
 * Replaces the document of the fixture with one of content (a string
 * literal of length bytes) whose line n + 1 has its content start at
 * starts[n], and begins a new run on it.
 */
static void use_content(const char *content, size_t length,
                        const size_t *starts, size_t count)
{
    size_t i;

    amark_dialect_run_end(&g_run);
    g_run_begun = 0;
    amark_document_destroy(g_doc);
    g_doc = NULL;
    memset(&g_run, 0, sizeof(g_run));
    NAOS_REQUIRE_INT_EQ(
        amark_document_create(&g_allocator, (const unsigned char *)content,
                              length, 0, length, NULL, &g_doc),
        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_parse_data_create(g_doc), AMARK_ERROR_NONE);
    for (i = 0; i < count; i++) {
        amark_line_set(g_doc, i + 1, starts[i], 0);
    }
    NAOS_REQUIRE_INT_EQ(amark_dialect_run_begin(&g_run, &g_def.dialect,
                                                &g_allocator, g_doc),
                        AMARK_ERROR_NONE);
    g_run_begun = 1;
}

/* Applies g_reply under the root (depth 0) with the given limits. */
static amark_error_t apply_at_root(amark_node_id_t existing,
                                   size_t max_depth, size_t max_nodes)
{
    return amark_dialect_reply_apply(&g_run, g_reply,
                                     amark_document_root(g_doc), 0,
                                     existing, max_depth, max_nodes);
}

static void check_tree(void)
{
    amark_tree_check_result_t check = amark_tree_check(g_doc);

    NAOS_CHECK_INT_EQ(check.violation, AMARK_TREE_VIOLATION_NONE);
    NAOS_CHECK_UINT_EQ(check.node, AMARK_NODE_NONE);
}

/*
 * Checks a node made from a reply: kind, block, range, lines and attribute
 * bytes (NULL for none).
 */
static void check_node(amark_node_id_t node, amark_kind_t kind,
                       size_t start, size_t end, size_t first_line,
                       size_t last_line, const char *data)
{
    amark_range_t range;
    const void *bytes;
    size_t size = 99;

    NAOS_REQUIRE_TRUE(node != AMARK_NODE_NONE);
    NAOS_CHECK_UINT_EQ(amark_node_kind(g_doc, node), kind);
    NAOS_CHECK_TRUE(amark_node_is_block(g_doc, node));
    NAOS_CHECK_FALSE(amark_node_is_inline(g_doc, node));
    NAOS_REQUIRE_TRUE(amark_node_range(g_doc, node, &range));
    NAOS_CHECK_UINT_EQ(range.start, start);
    NAOS_CHECK_UINT_EQ(range.end, end);
    NAOS_CHECK_UINT_EQ(amark_node_start_line(g_doc, node), first_line);
    NAOS_CHECK_UINT_EQ(amark_node_end_line(g_doc, node), last_line);
    bytes = amark_node_dialect_data(g_doc, node, &size);
    if (data == NULL) {
        NAOS_CHECK_TRUE(bytes == NULL);
        NAOS_CHECK_UINT_EQ(size, 0);
    } else {
        NAOS_REQUIRE_UINT_EQ(size, strlen(data));
        NAOS_REQUIRE_TRUE(bytes != NULL);
        NAOS_CHECK_TRUE(memcmp(bytes, data, size) == 0);
    }
}

/* Checks inline content index of the document. */
static void check_inline_content(size_t index, amark_node_id_t node,
                                 size_t start, size_t end)
{
    amark_inline_content_t ic;

    NAOS_REQUIRE_TRUE(amark_inline_content_at(g_doc, index, &ic));
    NAOS_CHECK_UINT_EQ(ic.node, node);
    NAOS_CHECK_UINT_EQ(ic.start, start);
    NAOS_CHECK_UINT_EQ(ic.end, end);
}

NAOS_TEST(apply_paragraph_reply_builds_the_tree)
{
    amark_node_id_t root;
    amark_node_id_t box, note1, note2, para;
    size_t b, n;

    begin_paragraph(0);
    b = add_ok(TOP, K_BOX, 2, 9);
    NAOS_CHECK_INT_EQ(amark_dialect_reply_set_data(g_reply, b, "xy", 2),
                      AMARK_ERROR_NONE);
    n = add_ok(b, K_NOTE, 2, 4);
    NAOS_CHECK_INT_EQ(amark_dialect_reply_set_inline(g_reply, n, rng(3, 4)),
                      AMARK_ERROR_NONE);
    n = add_ok(b, K_NOTE, 7, 9);
    NAOS_CHECK_INT_EQ(amark_dialect_reply_set_data(g_reply, n, "q", 1),
                      AMARK_ERROR_NONE);
    (void)add_ok(TOP, K_PARA, 12, 14);
    finish_valid();

    NAOS_REQUIRE_INT_EQ(apply_at_root(AMARK_NODE_NONE, 0, 0),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 5);
    NAOS_CHECK_UINT_EQ(amark_document_diagnostic_count(g_doc), 0);

    root = amark_document_root(g_doc);
    box = amark_node_first_child(g_doc, root);
    check_node(box, K_BOX, 2, 9, 1, 2, "xy");
    NAOS_CHECK_UINT_EQ(amark_node_parent(g_doc, box), root);
    note1 = amark_node_first_child(g_doc, box);
    check_node(note1, K_NOTE, 2, 4, 1, 1, NULL);
    NAOS_CHECK_UINT_EQ(amark_node_parent(g_doc, note1), box);
    NAOS_CHECK_UINT_EQ(amark_node_first_child(g_doc, note1),
                       AMARK_NODE_NONE);
    note2 = amark_node_next_sibling(g_doc, note1);
    check_node(note2, K_NOTE, 7, 9, 2, 2, "q");
    NAOS_CHECK_UINT_EQ(amark_node_parent(g_doc, note2), box);
    NAOS_CHECK_UINT_EQ(amark_node_next_sibling(g_doc, note2),
                       AMARK_NODE_NONE);
    para = amark_node_next_sibling(g_doc, box);
    check_node(para, K_PARA, 12, 14, 3, 3, NULL);
    NAOS_CHECK_UINT_EQ(amark_node_parent(g_doc, para), root);
    NAOS_CHECK_UINT_EQ(amark_node_next_sibling(g_doc, para),
                       AMARK_NODE_NONE);

    /* The inline contents, in document order. */
    NAOS_REQUIRE_UINT_EQ(amark_inline_content_count(g_doc), 2);
    check_inline_content(0, note1, 3, 4);
    check_inline_content(1, para, 12, 14);
    check_tree();
}

NAOS_TEST(apply_paragraph_parts_trim_trailing_whitespace_of_content)
{
    /*
     * Line 1 "> aa " (0-4, LF at 5), line 2 "> bb\t " (6-11, LF at 12).
     * Content starts at 2 and 8; the paragraph is [2, 12).
     */
    static const char content[] = "> aa \n> bb\t \n";
    static const size_t starts[] = {2, 8};
    amark_node_id_t root;
    amark_node_id_t first, second;

    use_content(content, sizeof(content) - 1, starts, 2);
    begin(0, AMARK_DIALECT_REPLY_PARAGRAPH, K_PARA, rng(2, 12));
    (void)add_ok(TOP, K_PARA, 2, 5);
    (void)add_ok(TOP, K_PARA, 8, 12);
    finish_valid();
    NAOS_REQUIRE_INT_EQ(apply_at_root(AMARK_NODE_NONE, 0, 0),
                        AMARK_ERROR_NONE);

    root = amark_document_root(g_doc);
    first = amark_node_first_child(g_doc, root);
    check_node(first, K_PARA, 2, 5, 1, 1, NULL);
    second = amark_node_next_sibling(g_doc, first);
    check_node(second, K_PARA, 8, 12, 2, 2, NULL);
    NAOS_REQUIRE_UINT_EQ(amark_inline_content_count(g_doc), 2);
    check_inline_content(0, first, 2, 4);
    check_inline_content(1, second, 8, 10);
    check_tree();

    /* One part over both lines: only the end of the last line is cut. */
    use_content(content, sizeof(content) - 1, starts, 2);
    begin(0, AMARK_DIALECT_REPLY_PARAGRAPH, K_PARA, rng(2, 12));
    (void)add_ok(TOP, K_PARA, 2, 12);
    finish_valid();
    NAOS_REQUIRE_INT_EQ(apply_at_root(AMARK_NODE_NONE, 0, 0),
                        AMARK_ERROR_NONE);
    first = amark_node_first_child(g_doc, amark_document_root(g_doc));
    check_node(first, K_PARA, 2, 12, 1, 2, NULL);
    NAOS_REQUIRE_UINT_EQ(amark_inline_content_count(g_doc), 1);
    check_inline_content(0, first, 2, 10);
    check_tree();
}

NAOS_TEST(apply_leaf_reply_makes_block_0)
{
    amark_node_id_t note;

    begin(0, AMARK_DIALECT_REPLY_LEAF, K_NOTE, rng(2, 14));
    NAOS_CHECK_INT_EQ(amark_dialect_reply_set_data(g_reply, 0, "abc", 3),
                      AMARK_ERROR_NONE);
    NAOS_CHECK_INT_EQ(amark_dialect_reply_set_inline(g_reply, 0,
                                                     rng(7, 14)),
                      AMARK_ERROR_NONE);
    finish_valid();
    NAOS_REQUIRE_INT_EQ(apply_at_root(AMARK_NODE_NONE, 0, 0),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 2);
    note = amark_node_first_child(g_doc, amark_document_root(g_doc));
    check_node(note, K_NOTE, 2, 14, 1, 3, "abc");
    NAOS_REQUIRE_UINT_EQ(amark_inline_content_count(g_doc), 1);
    check_inline_content(0, note, 7, 14);
    check_tree();
}

NAOS_TEST(apply_leaf_reply_without_anything_set)
{
    amark_node_id_t rule;

    begin(1, AMARK_DIALECT_REPLY_LEAF, K_RULE, rng(7, 9));
    finish_valid();
    NAOS_REQUIRE_INT_EQ(apply_at_root(AMARK_NODE_NONE, 0, 0),
                        AMARK_ERROR_NONE);
    rule = amark_node_first_child(g_doc, amark_document_root(g_doc));
    check_node(rule, K_RULE, 7, 9, 2, 2, NULL);
    NAOS_CHECK_UINT_EQ(amark_inline_content_count(g_doc), 0);
    check_tree();
}

NAOS_TEST(apply_container_reply_sets_data_of_the_existing_node)
{
    amark_node_id_t box = AMARK_NODE_NONE;

    NAOS_REQUIRE_INT_EQ(amark_tree_append(g_doc, amark_document_root(g_doc),
                                          K_BOX, AMARK_NODE_CLASS_BLOCK, 2,
                                          14, &box),
                        AMARK_ERROR_NONE);
    begin(0, AMARK_DIALECT_REPLY_CONTAINER, K_BOX, rng(2, 14));
    NAOS_CHECK_INT_EQ(amark_dialect_reply_set_data(g_reply, 0, "zz", 2),
                      AMARK_ERROR_NONE);
    finish_valid();
    /* No node is added, so the node count limit is not reached. */
    NAOS_REQUIRE_INT_EQ(apply_at_root(box, 1, 2), AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 2);
    check_node(box, K_BOX, 2, 14, 1, 3, "zz");
    NAOS_CHECK_UINT_EQ(amark_inline_content_count(g_doc), 0);
    check_tree();
}

/* A reply of three nested blocks: BOX > BOX > NOTE. */
static void begin_nested(void)
{
    size_t outer, inner;

    begin_paragraph(0);
    outer = add_ok(TOP, K_BOX, 2, 14);
    inner = add_ok(outer, K_BOX, 7, 14);
    (void)add_ok(inner, K_NOTE, 12, 14);
    finish_valid();
}

NAOS_TEST(apply_checks_the_node_count_before_adding)
{
    begin_nested();
    /* The root and 3 new nodes need a limit of 4. */
    NAOS_CHECK_INT_EQ(apply_at_root(AMARK_NODE_NONE, 0, 3),
                      AMARK_ERROR_LIMIT_EXCEEDED);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 1);
    NAOS_CHECK_UINT_EQ(amark_inline_content_count(g_doc), 0);
    NAOS_CHECK_INT_EQ(apply_at_root(AMARK_NODE_NONE, 0, 1),
                      AMARK_ERROR_LIMIT_EXCEEDED);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 1);
    NAOS_CHECK_INT_EQ(apply_at_root(AMARK_NODE_NONE, 0, 4),
                      AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 4);
    check_tree();
}

NAOS_TEST(apply_checks_the_depth_before_adding)
{
    begin_nested();
    /* Under the root (depth 0) the blocks are at depths 1, 2 and 3. */
    NAOS_CHECK_INT_EQ(apply_at_root(AMARK_NODE_NONE, 2, 0),
                      AMARK_ERROR_LIMIT_EXCEEDED);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 1);
    NAOS_CHECK_INT_EQ(apply_at_root(AMARK_NODE_NONE, 3, 0),
                      AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 4);
    check_tree();
}

NAOS_TEST(apply_checks_the_depth_of_the_parent)
{
    begin(1, AMARK_DIALECT_REPLY_LEAF, K_RULE, rng(7, 9));
    finish_valid();
    /* A parent at depth 5 puts the leaf at depth 6. */
    NAOS_CHECK_INT_EQ(amark_dialect_reply_apply(&g_run, g_reply,
                                                amark_document_root(g_doc),
                                                5, AMARK_NODE_NONE, 5, 0),
                      AMARK_ERROR_LIMIT_EXCEEDED);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 1);
    NAOS_CHECK_INT_EQ(amark_dialect_reply_apply(&g_run, g_reply,
                                                amark_document_root(g_doc),
                                                5, AMARK_NODE_NONE, 6, 0),
                      AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 2);
}

NAOS_TEST(apply_returns_a_failed_allocation)
{
    /* Larger than a chunk of the document's arena: copying it allocates. */
    static const unsigned char big[8192] = {'a', 'b'};
    amark_error_t err = AMARK_ERROR_NOMEM;
    size_t n;

    /* Fail each allocation in turn until apply succeeds. */
    for (n = 1; n < 100; n++) {
        size_t b;

        use_content(k_content, CONTENT_LENGTH, NULL, 0);
        amark_line_set(g_doc, 1, 2, 0);
        amark_line_set(g_doc, 2, 7, 0);
        amark_line_set(g_doc, 3, 12, 0);
        begin_paragraph(0);
        b = add_ok(TOP, K_NOTE, 2, 9);
        NAOS_CHECK_INT_EQ(amark_dialect_reply_set_data(g_reply, b, big,
                                                       sizeof(big)),
                          AMARK_ERROR_NONE);
        NAOS_CHECK_INT_EQ(amark_dialect_reply_set_inline(g_reply, b,
                                                         rng(2, 9)),
                          AMARK_ERROR_NONE);
        (void)add_ok(TOP, K_PARA, 12, 14);
        g_state.fail_at = g_state.malloc_calls + n;
        err = apply_at_root(AMARK_NODE_NONE, 0, 0);
        g_state.fail_at = 0;
        if (err == AMARK_ERROR_NONE) {
            break;
        }
        NAOS_CHECK_INT_EQ(err, AMARK_ERROR_NOMEM);
    }
    NAOS_CHECK_INT_EQ(err, AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_GT(n, 1); /* at least one allocation failed */
    NAOS_CHECK_UINT_EQ(amark_inline_content_count(g_doc), 2);
    check_tree();
}

NAOS_TEST(apply_copies_the_data_of_the_rule)
{
    char buffer[4];
    amark_node_id_t first, second;
    size_t b;

    memcpy(buffer, "abc", 4);
    begin_paragraph(0);
    b = add_ok(TOP, K_NOTE, 2, 4);
    NAOS_CHECK_INT_EQ(amark_dialect_reply_set_data(g_reply, b, buffer, 3),
                      AMARK_ERROR_NONE);
    buffer[0] = 'X'; /* the rule reuses its buffer before returning */
    finish_valid();
    NAOS_REQUIRE_INT_EQ(apply_at_root(AMARK_NODE_NONE, 0, 0),
                        AMARK_ERROR_NONE);
    buffer[1] = 'Y'; /* and after apply */
    first = amark_node_first_child(g_doc, amark_document_root(g_doc));
    check_node(first, K_NOTE, 2, 4, 1, 1, "abc");

    /* The next reply reuses the reply's bytes; the document keeps its. */
    memcpy(buffer, "QRS", 4);
    begin_paragraph(0);
    b = add_ok(TOP, K_NOTE, 7, 9);
    NAOS_CHECK_INT_EQ(amark_dialect_reply_set_data(g_reply, b, buffer, 3),
                      AMARK_ERROR_NONE);
    finish_valid();
    NAOS_REQUIRE_INT_EQ(apply_at_root(AMARK_NODE_NONE, 0, 0),
                        AMARK_ERROR_NONE);
    memset(buffer, 0, sizeof(buffer));
    second = amark_node_next_sibling(g_doc, first);
    check_node(first, K_NOTE, 2, 4, 1, 1, "abc");
    check_node(second, K_NOTE, 7, 9, 2, 2, "QRS");
    check_tree();
}

/* ---------------------------------------------------------------------- */
/* The closing block and its lines (6.3) */

static void check_span(const amark_dialect_closing_t *closing, size_t index,
                       size_t start, size_t content_start, size_t end)
{
    amark_dialect_line_span_t span;

    NAOS_REQUIRE_TRUE(amark_dialect_closing_line(closing, index, &span));
    NAOS_CHECK_UINT_EQ(span.start, start);
    NAOS_CHECK_UINT_EQ(span.content_start, content_start);
    NAOS_CHECK_UINT_EQ(span.end, end);
}

NAOS_TEST(closing_init_fills_the_input_of_a_close_rule)
{
    struct amark_dialect_lines lines;
    amark_dialect_closing_t closing;

    memset(&closing, 0, sizeof(closing));
    amark_dialect_closing_init(&closing, &lines, g_doc, K_NOTE, rng(3, 13),
                               42);
    NAOS_CHECK_TRUE(closing.content == k_content);
    NAOS_CHECK_UINT_EQ(closing.content_length, CONTENT_LENGTH);
    NAOS_CHECK_UINT_EQ(closing.kind, K_NOTE);
    NAOS_CHECK_UINT_EQ(closing.range.start, 3);
    NAOS_CHECK_UINT_EQ(closing.range.end, 13);
    NAOS_CHECK_UINT_EQ(closing.line_count, 3);
    NAOS_CHECK_TRUE(closing.data == 42);
    NAOS_CHECK_TRUE(closing.lines == &lines);

    amark_dialect_closing_init(&closing, &lines, g_doc, K_PARA,
                               rng(PARA_START, PARA_END), 0);
    NAOS_CHECK_UINT_EQ(closing.kind, K_PARA);
    NAOS_CHECK_UINT_EQ(closing.line_count, 3);
    NAOS_CHECK_TRUE(closing.data == 0);

    amark_dialect_closing_init(&closing, &lines, g_doc, K_RULE, rng(7, 9),
                               7);
    NAOS_CHECK_UINT_EQ(closing.line_count, 1);
}

NAOS_TEST(closing_line_cuts_the_first_and_last_lines_to_the_block)
{
    struct amark_dialect_lines lines;
    amark_dialect_closing_t closing;
    amark_dialect_line_span_t span;

    /* [3, 13): from inside line 1's content to inside line 3's. */
    amark_dialect_closing_init(&closing, &lines, g_doc, K_NOTE, rng(3, 13),
                               0);
    check_span(&closing, 0, 3, 3, 4);
    check_span(&closing, 1, 5, 7, 9);
    check_span(&closing, 2, 10, 12, 13);
    span.start = 77;
    NAOS_CHECK_FALSE(amark_dialect_closing_line(&closing, 3, &span));
    NAOS_CHECK_FALSE(amark_dialect_closing_line(&closing, SIZE_MAX, &span));
    NAOS_CHECK_UINT_EQ(span.start, 77);

    /* The paragraph [2, 14): the record of line 1 is its start. */
    amark_dialect_closing_init(&closing, &lines, g_doc, K_PARA,
                               rng(PARA_START, PARA_END), 0);
    check_span(&closing, 0, 2, 2, 4);
    check_span(&closing, 2, 10, 12, 14);

    /* One line [7, 9): both ends cut on the same line. */
    amark_dialect_closing_init(&closing, &lines, g_doc, K_RULE, rng(7, 9),
                               0);
    check_span(&closing, 0, 7, 7, 9);
    NAOS_CHECK_FALSE(amark_dialect_closing_line(&closing, 1, &span));
}

NAOS_TEST(closing_line_rejects_null_arguments)
{
    struct amark_dialect_lines lines;
    amark_dialect_closing_t closing;
    amark_dialect_line_span_t span;

    amark_dialect_closing_init(&closing, &lines, g_doc, K_NOTE, rng(2, 14),
                               0);
    NAOS_CHECK_FALSE(amark_dialect_closing_line(NULL, 0, &span));
    NAOS_CHECK_FALSE(amark_dialect_closing_line(&closing, 0, NULL));
    closing.lines = NULL;
    NAOS_CHECK_FALSE(amark_dialect_closing_line(&closing, 0, &span));
}

/*
 * Parses "> aa\n>  bb\n> cc  \n" without a dialect into a separate
 * document: a paragraph in a block quote, whose inline content is
 * [2, 15). Line 2 ">  bb" is 5-9 (content from 8, LF at 10); line 3
 * "> cc  " is 11-16 (content from 13, ends at 17, LF at 17).
 */
static const char k_quote[] = "> aa\n>  bb\n> cc  \n";

NAOS_TEST(inline_next_line_reads_continuation_lines_after_the_prefix)
{
    amark_config_t config;
    amark_resolved_config_t resolved;
    amark_arena_t scratch;
    amark_document_t *doc = NULL;
    struct amark_dialect_lines lines;
    amark_dialect_inline_t in;
    amark_inline_content_t ic;
    amark_range_t out;

    amark_config_init(&config);
    config.allocator = g_allocator;
    NAOS_REQUIRE_INT_EQ(amark_config_resolve(&config, sizeof(k_quote) - 1,
                                             &resolved),
                        AMARK_ERROR_NONE);
    amark_arena_init(&scratch, &g_allocator);
    NAOS_REQUIRE_INT_EQ(
        amark_document_create(&resolved.allocator,
                              (const unsigned char *)k_quote,
                              sizeof(k_quote) - 1, 0, sizeof(k_quote) - 1,
                              NULL, &doc),
        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_parse_data_create(doc), AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_block_parse(doc, &resolved, &scratch, NULL),
                        AMARK_ERROR_NONE);
    amark_arena_deinit(&scratch);
    NAOS_REQUIRE_UINT_EQ(amark_inline_content_count(doc), 1);
    NAOS_REQUIRE_TRUE(amark_inline_content_at(doc, 0, &ic));
    NAOS_CHECK_UINT_EQ(ic.start, 2);
    NAOS_CHECK_UINT_EQ(ic.end, 15);

    memset(&in, 0, sizeof(in));
    in.content = k_quote;
    in.content_length = sizeof(k_quote) - 1;
    in.position = 2;
    in.line_end = 4;
    in.inline_content = rng(ic.start, ic.end);
    amark_dialect_lines_init(&lines, doc, in.inline_content);
    in.lines = &lines;

    /* Line 2 starts after the prefix and its extra space. */
    NAOS_CHECK_TRUE(amark_dialect_inline_next_line(&in, 4, &out));
    NAOS_CHECK_UINT_EQ(out.start, 8);
    NAOS_CHECK_UINT_EQ(out.end, 10);
    /* The last line ends where the inline content ends. */
    NAOS_CHECK_TRUE(amark_dialect_inline_next_line(&in, 10, &out));
    NAOS_CHECK_UINT_EQ(out.start, 13);
    NAOS_CHECK_UINT_EQ(out.end, 15);

    /* No line after the last one, and no line end elsewhere. */
    out = rng(77, 77);
    NAOS_CHECK_FALSE(amark_dialect_inline_next_line(&in, 15, &out));
    NAOS_CHECK_FALSE(amark_dialect_inline_next_line(&in, 17, &out));
    NAOS_CHECK_FALSE(amark_dialect_inline_next_line(&in, 3, &out));
    NAOS_CHECK_FALSE(amark_dialect_inline_next_line(&in, 9, &out));
    NAOS_CHECK_FALSE(amark_dialect_inline_next_line(&in, SIZE_MAX, &out));
    NAOS_CHECK_UINT_EQ(out.start, 77);

    /* An inline content of one line has no next line. */
    in.inline_content = rng(8, 10);
    amark_dialect_lines_init(&lines, doc, in.inline_content);
    NAOS_CHECK_FALSE(amark_dialect_inline_next_line(&in, 10, &out));

    /* NULL arguments. */
    NAOS_CHECK_FALSE(amark_dialect_inline_next_line(NULL, 4, &out));
    in.inline_content = rng(ic.start, ic.end);
    amark_dialect_lines_init(&lines, doc, in.inline_content);
    NAOS_CHECK_FALSE(amark_dialect_inline_next_line(&in, 4, NULL));
    in.lines = NULL;
    NAOS_CHECK_FALSE(amark_dialect_inline_next_line(&in, 4, &out));

    amark_document_destroy(doc);
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
    NAOS_ADD_TEST(begin_fails_when_the_reply_cannot_be_made),
    NAOS_ADD_TEST(apply_paragraph_reply_builds_the_tree),
    NAOS_ADD_TEST(apply_paragraph_parts_trim_trailing_whitespace_of_content),
    NAOS_ADD_TEST(apply_leaf_reply_makes_block_0),
    NAOS_ADD_TEST(apply_leaf_reply_without_anything_set),
    NAOS_ADD_TEST(apply_container_reply_sets_data_of_the_existing_node),
    NAOS_ADD_TEST(apply_checks_the_node_count_before_adding),
    NAOS_ADD_TEST(apply_checks_the_depth_before_adding),
    NAOS_ADD_TEST(apply_checks_the_depth_of_the_parent),
    NAOS_ADD_TEST(apply_returns_a_failed_allocation),
    NAOS_ADD_TEST(apply_copies_the_data_of_the_rule),
    NAOS_ADD_TEST(closing_init_fills_the_input_of_a_close_rule),
    NAOS_ADD_TEST(closing_line_cuts_the_first_and_last_lines_to_the_block),
    NAOS_ADD_TEST(closing_line_rejects_null_arguments),
    NAOS_ADD_TEST(inline_next_line_reads_continuation_lines_after_the_prefix)
)

/* ---------------------------------------------------------------------- */
/* Start rules */

/*
 * The parses below go through amark_parse with a dialect, on the
 * allocator of the fixture, and look at the blocks of the tree. The local
 * dialects are test data: "probe" records what its start rules see, "bad"
 * returns the invalid results that the input selects, and "good" opens a
 * container on any "!" so that the rule after a rejected one can be seen.
 */

#define K_GOOD (AMARK_KIND_DIALECT_MIN + 200) /* container of "good" */
#define K_BAD (AMARK_KIND_DIALECT_MIN + 201)  /* container of "bad" */
#define K_PROBE (AMARK_KIND_DIALECT_MIN + 202) /* container of "probe" */
#define K_PROBE_LEAF (AMARK_KIND_DIALECT_MIN + 203) /* leaf of "probe" */

static amark_config_t g_config;

/* What the rules of "probe" saw. */
static struct {
    size_t interrupting; /* calls of the rule that interrupts a paragraph */
    size_t other;        /* calls of the rule that does not */
    amark_dialect_line_t last; /* the input of the last call */
} g_probe;

static amark_error_t probe_interrupting(
    const amark_dialect_line_t *line, void *state,
    amark_dialect_block_start_result_t *out)
{
    (void)state;
    (void)out;
    g_probe.interrupting++;
    g_probe.last = *line;
    return AMARK_ERROR_NONE;
}

static amark_error_t probe_other(const amark_dialect_line_t *line,
                                 void *state,
                                 amark_dialect_block_start_result_t *out)
{
    (void)state;
    (void)out;
    g_probe.other++;
    g_probe.last = *line;
    return AMARK_ERROR_NONE;
}

/*
 * At "!" followed by a letter: 'o' a range not starting at the position,
 * 'l' a range past the end of the line, 'r' a range that ends before it
 * starts, 'e' an empty range for a container, 's' and 'x' a status the
 * contract does not define, 'n' AMARK_ERROR_NOMEM, and 'k' a valid start
 * of two bytes. Anything else: no.
 */
static amark_error_t bad_start(const amark_dialect_line_t *line, void *state,
                               amark_dialect_block_start_result_t *out)
{
    size_t at = line->position;

    (void)state;
    if (line->line_end - at < 2 || line->content[at] != '!') {
        return AMARK_ERROR_NONE;
    }
    out->matched = true;
    out->consumed.start = at;
    out->consumed.end = at + 2;
    switch (line->content[at + 1]) {
    case 'o':
        out->consumed.start = at + 1;
        break;
    case 'l':
        out->consumed.end = line->line_end + 1;
        break;
    case 'r':
        out->consumed.end = at - 1;
        break;
    case 'e':
        out->consumed.end = at;
        break;
    case 's':
        return AMARK_ERROR_INVALID_INPUT;
    case 'x':
        return AMARK_ERROR_LIMIT_EXCEEDED;
    case 'n':
        return AMARK_ERROR_NOMEM;
    case 'k':
        break;
    default:
        out->matched = false;
        break;
    }
    return AMARK_ERROR_NONE;
}

/* Opens a container on "!" and the byte after it. */
static amark_error_t good_start(const amark_dialect_line_t *line, void *state,
                                amark_dialect_block_start_result_t *out)
{
    size_t at = line->position;

    (void)state;
    if (line->line_end - at >= 2 && line->content[at] == '!') {
        out->matched = true;
        out->consumed.start = at;
        out->consumed.end = at + 2;
        out->data = 7;
    }
    return AMARK_ERROR_NONE;
}

static const amark_dialect_kind_t k_probe_kinds[] = {
    {K_PROBE, AMARK_DIALECT_CONTAINER, AMARK_DIALECT_BLANK_CLOSES, NULL,
     NULL},
    {K_PROBE_LEAF, AMARK_DIALECT_LEAF, AMARK_DIALECT_BLANK_CLOSES, NULL,
     NULL},
};
static const amark_dialect_block_start_t k_probe_starts[] = {
    {K_PROBE_LEAF, false, probe_other},
    {K_PROBE, true, probe_interrupting},
};
static const amark_dialect_extension_t k_probe_ext = {
    "probe", k_probe_kinds, 2, k_probe_starts, 2, NULL, NULL, 0, NULL, 0,
    NULL, NULL, NULL};

static const amark_dialect_kind_t k_bad_kinds[] = {
    {K_BAD, AMARK_DIALECT_CONTAINER, AMARK_DIALECT_BLANK_CLOSES, NULL, NULL},
};
static const amark_dialect_block_start_t k_bad_starts[] = {
    {K_BAD, true, bad_start},
};
static const amark_dialect_extension_t k_bad_ext = {
    "bad", k_bad_kinds, 1, k_bad_starts, 1, NULL, NULL, 0, NULL, 0,
    NULL, NULL, NULL};

static const amark_dialect_kind_t k_good_kinds[] = {
    {K_GOOD, AMARK_DIALECT_CONTAINER, AMARK_DIALECT_BLANK_CLOSES, NULL,
     NULL},
};
static const amark_dialect_block_start_t k_good_starts[] = {
    {K_GOOD, true, good_start},
};
static const amark_dialect_extension_t k_good_ext = {
    "good", k_good_kinds, 1, k_good_starts, 1, NULL, NULL, 0, NULL, 0,
    NULL, NULL, NULL};

static const amark_dialect_extension_t *const k_probe_exts[] = {&k_probe_ext};
static const amark_dialect_t k_probe_dialect = {
    sizeof(amark_dialect_t), AMARK_DIALECT_ABI, k_probe_exts, 1};

/* "bad" first, then "good" and the test dialect. */
static const amark_dialect_extension_t *const k_bad_exts[] = {&k_bad_ext,
                                                              &k_good_ext};
static const amark_dialect_t k_bad_dialect = {
    sizeof(amark_dialect_t), AMARK_DIALECT_ABI, k_bad_exts, 2};

NAOS_SETUP(start)
{
    amark_test_alloc_init(&g_state);
    amark_config_init(&g_config);
    g_config.allocator = amark_test_allocator(&g_state);
    g_doc = NULL;
    memset(&g_probe, 0, sizeof(g_probe));
}

NAOS_TEARDOWN(start)
{
    amark_document_destroy(g_doc);
    g_doc = NULL;
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
    NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
}

/*
 * Parses content (a string literal) with dialect, requires success and a
 * tree that passes the check, and requires diagnostics diagnostics.
 */
static void parse_with(const amark_dialect_t *dialect, const char *content,
                       size_t length, size_t diagnostics)
{
    amark_document_destroy(g_doc);
    g_doc = NULL;
    g_config.dialect = dialect;
    NAOS_REQUIRE_INT_EQ(amark_parse(content, length, &g_config, &g_doc),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_NOT_NULL(g_doc);
    check_tree();
    NAOS_CHECK_UINT_EQ(amark_document_diagnostic_count(g_doc), diagnostics);
}

#define PARSE(dialect, s, diagnostics) \
    parse_with((dialect), (s), sizeof(s) - 1, (diagnostics))

static const char *kind_name(amark_kind_t kind)
{
    switch (kind) {
    case AMARK_KIND_DOCUMENT:
        return "doc";
    case AMARK_KIND_BLOCK_QUOTE:
        return "bq";
    case AMARK_KIND_LIST:
        return "list";
    case AMARK_KIND_LIST_ITEM:
        return "li";
    case AMARK_KIND_THEMATIC_BREAK:
        return "hr";
    case AMARK_KIND_HEADING:
        return "h";
    case AMARK_KIND_CODE_BLOCK:
        return "code";
    case AMARK_KIND_HTML_BLOCK:
        return "html";
    case AMARK_KIND_PARAGRAPH:
        return "p";
    case AMARK_KIND_LINK_REFERENCE_DEFINITION:
        return "def";
    case AMARK_TEST_KIND_BOX:
        return "box";
    case K_GOOD:
        return "good";
    case K_BAD:
        return "bad";
    case K_PROBE:
        return "probe";
    default:
        return "?";
    }
}

#define SHAPE_SIZE 512

static void shape_put(char *buffer, size_t *length, const char *s)
{
    size_t n = strlen(s);

    NAOS_REQUIRE_UINT_LT(*length + n, SHAPE_SIZE);
    memcpy(buffer + *length, s, n + 1);
    *length += n;
}

/*
 * Requires the blocks of g_doc to be written as want: each block as its
 * kind name and range, "name[start,end]", followed by its block children
 * in parentheses, separated by spaces. Inlines are left out.
 */
static void check_shape(const char *want)
{
    char buffer[SHAPE_SIZE];
    char item[64];
    size_t length = 0;
    amark_node_id_t root = amark_document_root(g_doc);
    amark_node_id_t node = root;

    buffer[0] = '\0';
    for (;;) {
        amark_node_id_t child;
        amark_range_t range;

        NAOS_REQUIRE_TRUE(amark_node_range(g_doc, node, &range));
        (void)sprintf(item, "%s[%lu,%lu]",
                      kind_name(amark_node_kind(g_doc, node)),
                      (unsigned long)range.start, (unsigned long)range.end);
        shape_put(buffer, &length, item);
        child = amark_node_first_child(g_doc, node);
        if (child != AMARK_NODE_NONE && amark_node_is_block(g_doc, child)) {
            shape_put(buffer, &length, "(");
            node = child;
            continue;
        }
        while (node != root &&
               amark_node_next_sibling(g_doc, node) == AMARK_NODE_NONE) {
            node = amark_node_parent(g_doc, node);
            shape_put(buffer, &length, ")");
        }
        if (node == root) {
            break;
        }
        shape_put(buffer, &length, " ");
        node = amark_node_next_sibling(g_doc, node);
    }
    NAOS_CHECK_STR_EQ(buffer, want);
}

/* The diagnostic number index of g_doc is a block start one of name. */
static void check_start_diagnostic(size_t index, const char *name,
                                   const char *reason, size_t offset,
                                   size_t line)
{
    char expected[160];
    amark_diagnostic_t diag;

    NAOS_REQUIRE_TRUE(amark_document_diagnostic(g_doc, index, &diag));
    (void)strcpy(expected, "dialect \"");
    (void)strcat(expected, name);
    (void)strcat(expected, "\": block start: ");
    (void)strcat(expected, reason);
    NAOS_CHECK_STR_EQ(diag.message, expected);
    NAOS_CHECK_UINT_EQ(diag.offset, offset);
    NAOS_CHECK_UINT_EQ(diag.line, line);
    NAOS_CHECK_INT_EQ(diag.severity, AMARK_SEVERITY_ERROR);
}

NAOS_TEST(one_line_box_is_a_container_block)
{
    amark_node_id_t root;
    amark_node_id_t box;
    amark_node_id_t para;

    PARSE(amark_test_dialect(), "@> a\n", 0);
    check_shape("doc[0,5](box[0,4](p[3,4]))");
    root = amark_document_root(g_doc);
    box = amark_node_first_child(g_doc, root);
    check_node(box, AMARK_TEST_KIND_BOX, 0, 4, 1, 1, NULL);
    NAOS_CHECK_TRUE(amark_kind_is_dialect(amark_node_kind(g_doc, box)));
    NAOS_CHECK_UINT_EQ(amark_node_parent(g_doc, box), root);
    NAOS_CHECK_UINT_EQ(amark_node_next_sibling(g_doc, box), AMARK_NODE_NONE);
    para = amark_node_first_child(g_doc, box);
    NAOS_CHECK_UINT_EQ(amark_node_kind(g_doc, para), AMARK_KIND_PARAGRAPH);
    NAOS_CHECK_UINT_EQ(amark_node_parent(g_doc, para), box);
    NAOS_CHECK_UINT_EQ(amark_node_start_line(g_doc, para), 1);
    NAOS_CHECK_UINT_EQ(amark_node_end_line(g_doc, para), 1);
    NAOS_CHECK_UINT_EQ(amark_node_kind(g_doc,
                                       amark_node_first_child(g_doc, para)),
                       AMARK_KIND_TEXT);
}

NAOS_TEST(box_does_not_continue_on_the_next_line)
{
    /* Line 3 is a sibling; a blank line ends the box. */
    PARSE(amark_test_dialect(), "@> a\n\nb\n", 0);
    check_shape("doc[0,8](box[0,4](p[3,4]) p[6,7])");
    /* Line 2 is a box of its own, after the first one. */
    PARSE(amark_test_dialect(), "@> a\n@> b\n", 0);
    check_shape("doc[0,10](box[0,4](p[3,4]) box[5,9](p[8,9]))");
    /* The box ends at the end of the content too. */
    PARSE(amark_test_dialect(), "x\n\n@> last", 0);
    check_shape("doc[0,10](p[0,1] box[3,10](p[6,10]))");
    /* An empty box: the marker alone, with the line ending after it. */
    PARSE(amark_test_dialect(), "@> \n", 0);
    check_shape("doc[0,4](box[0,3])");
}

NAOS_TEST(box_starts_at_its_first_non_space_byte)
{
    PARSE(amark_test_dialect(), "   @> a\n", 0);
    check_shape("doc[0,8](box[3,7](p[6,7]))");
    /* Without the space after the marker, it is not a box. */
    PARSE(amark_test_dialect(), "@>a\n", 0);
    check_shape("doc[0,4](p[0,3])");
}

NAOS_TEST(boxes_nest_on_one_line)
{
    PARSE(amark_test_dialect(), "@> @> a\n", 0);
    check_shape("doc[0,8](box[0,7](box[3,7](p[6,7])))");
    PARSE(amark_test_dialect(), "@> @> @> a\n", 0);
    check_shape("doc[0,11](box[0,10](box[3,10](box[6,10](p[9,10]))))");
}

NAOS_TEST(commonmark_blocks_start_inside_a_box)
{
    PARSE(amark_test_dialect(), "@> - a\n", 0);
    check_shape("doc[0,7](box[0,6](list[3,6](li[3,6](p[5,6]))))");
    PARSE(amark_test_dialect(), "@> # h\n", 0);
    check_shape("doc[0,7](box[0,6](h[3,6]))");
    PARSE(amark_test_dialect(), "@> > q\n", 0);
    check_shape("doc[0,7](box[0,6](bq[3,6](p[5,6])))");
    PARSE(amark_test_dialect(), "@> ---\n", 0);
    check_shape("doc[0,7](box[0,6](hr[3,6]))");
    /* Code indented 4 columns from the end of the marker. */
    PARSE(amark_test_dialect(), "@>     x\n", 0);
    check_shape("doc[0,9](box[0,8](code[7,8]))");
}

NAOS_TEST(box_starts_inside_commonmark_containers)
{
    PARSE(amark_test_dialect(), "- @> a\n", 0);
    check_shape("doc[0,7](list[0,6](li[0,6](box[2,6](p[5,6]))))");
    PARSE(amark_test_dialect(), "> @> a\n", 0);
    check_shape("doc[0,7](bq[0,6](box[2,6](p[5,6])))");
    PARSE(amark_test_dialect(), "- @> - a\n", 0);
    check_shape("doc[0,9](list[0,8](li[0,8](box[2,8](list[5,8](li[5,8]"
                "(p[7,8]))))))");
}

NAOS_TEST(box_closes_a_list_whose_item_does_not_continue)
{
    /* A box does not go into a list: the list closes. */
    PARSE(amark_test_dialect(), "- a\n@> b\n", 0);
    check_shape("doc[0,9](list[0,3](li[0,3](p[2,3])) box[4,8](p[7,8]))");
}

NAOS_TEST(box_interrupts_a_paragraph)
{
    PARSE(amark_test_dialect(), "a\n@> b\n", 0);
    check_shape("doc[0,7](p[0,1] box[2,6](p[5,6]))");
    /* Inside a block quote, on a lazy line: the quote closes. */
    PARSE(amark_test_dialect(), "> a\n@> b\n", 0);
    check_shape("doc[0,9](bq[0,3](p[2,3]) box[4,8](p[7,8]))");
    /* Inside a list item that continues: the box joins the item. */
    PARSE(amark_test_dialect(), "- a\n  @> b\n", 0);
    check_shape("doc[0,11](list[0,10](li[0,10](p[2,3] box[6,10](p[9,10]))))");
}

NAOS_TEST(a_rule_that_cannot_interrupt_a_paragraph_is_not_called_in_one)
{
    PARSE(&k_probe_dialect, "a\nb\nc\n", 0);
    /* Line 1 calls both; lines 2 and 3 continue the paragraph. */
    NAOS_CHECK_UINT_EQ(g_probe.interrupting, 3);
    NAOS_CHECK_UINT_EQ(g_probe.other, 1);
    NAOS_CHECK_TRUE(g_probe.last.in_paragraph);
    NAOS_CHECK_UINT_EQ(g_probe.last.container_kind, AMARK_KIND_DOCUMENT);
    check_shape("doc[0,6](p[0,5])");
}

NAOS_TEST(start_rules_see_the_line_and_the_position)
{
    static const char content[] = "x\n\n > \t b\n";

    PARSE(&k_probe_dialect, content, 0);
    /*
     * Line 3: the rules are called at 4 (">", column 1), then inside the
     * quote at 8 ("b"), after the marker and its space (column 3), a tab
     * and a space: 2 columns of indentation.
     */
    NAOS_CHECK_UINT_EQ(g_probe.interrupting, 3);
    NAOS_CHECK_UINT_EQ(g_probe.other, 3);
    NAOS_REQUIRE_NOT_NULL(g_probe.last.content);
    NAOS_CHECK_TRUE(memcmp(g_probe.last.content, content,
                           sizeof(content) - 1) == 0);
    NAOS_CHECK_UINT_EQ(g_probe.last.content_length, sizeof(content) - 1);
    NAOS_CHECK_UINT_EQ(g_probe.last.line, 3);
    NAOS_CHECK_UINT_EQ(g_probe.last.line_start, 3);
    NAOS_CHECK_UINT_EQ(g_probe.last.line_end, 9);
    NAOS_CHECK_UINT_EQ(g_probe.last.position, 8);
    NAOS_CHECK_UINT_EQ(g_probe.last.first_nonspace, 8);
    NAOS_CHECK_UINT_EQ(g_probe.last.column, 5);
    NAOS_CHECK_UINT_EQ(g_probe.last.indent, 2);
    NAOS_CHECK_UINT_EQ(g_probe.last.container_kind,
                       AMARK_KIND_BLOCK_QUOTE);
    NAOS_CHECK_FALSE(g_probe.last.in_paragraph);
    check_shape("doc[0,10](p[0,1] bq[4,9](p[8,9]))");
}

NAOS_TEST(start_rules_see_the_parent_a_new_block_would_have)
{
    /* The list matches on line 2 but its item does not: the list closes. */
    PARSE(&k_probe_dialect, "- a\n\nb\n", 0);
    NAOS_CHECK_UINT_EQ(g_probe.last.container_kind, AMARK_KIND_DOCUMENT);
    NAOS_CHECK_UINT_EQ(g_probe.last.position, 5);
    PARSE(&k_probe_dialect, "- a\n", 0);
    NAOS_CHECK_UINT_EQ(g_probe.last.container_kind, AMARK_KIND_LIST_ITEM);
    NAOS_CHECK_UINT_EQ(g_probe.last.position, 2);
}

NAOS_TEST(no_start_rule_is_called_on_blank_or_indented_lines)
{
    PARSE(&k_probe_dialect, "\n  \n\t\n", 0);
    NAOS_CHECK_UINT_EQ(g_probe.interrupting + g_probe.other, 0);
    PARSE(&k_probe_dialect, "    @> a\n", 0);
    NAOS_CHECK_UINT_EQ(g_probe.interrupting + g_probe.other, 0);
    /* A box indented 4 columns is code, or a paragraph's text. */
    PARSE(amark_test_dialect(), "    @> a\n", 0);
    check_shape("doc[0,9](code[4,8])");
    PARSE(amark_test_dialect(), "a\n    @> b\n", 0);
    check_shape("doc[0,11](p[0,10])");
    PARSE(amark_test_dialect(), "@>     @> a\n", 0);
    check_shape("doc[0,12](box[0,11](code[7,11]))");
}

NAOS_TEST(no_start_rule_is_called_inside_code_and_html_blocks)
{
    /* Only on the opening lines; the closing fence ends the line. */
    PARSE(&k_probe_dialect, "```\n@> a\n```\n", 0);
    NAOS_CHECK_UINT_EQ(g_probe.interrupting, 1);
    memset(&g_probe, 0, sizeof(g_probe));
    PARSE(&k_probe_dialect, "<div>\n@> a\n", 0);
    NAOS_CHECK_UINT_EQ(g_probe.interrupting, 1);
    PARSE(amark_test_dialect(), "```\n@> a\n```\n", 0);
    check_shape("doc[0,13](code[0,12])");
    PARSE(amark_test_dialect(), "<div>\n@> a\n", 0);
    check_shape("doc[0,11](html[0,10])");
    PARSE(amark_test_dialect(), "    x\n    @> a\n", 0);
    check_shape("doc[0,15](code[4,14])");
}

NAOS_TEST(start_rules_are_called_before_the_commonmark_starts)
{
    /* "!-" would be nothing in CommonMark; "!k" is taken before "- ". */
    PARSE(&k_bad_dialect, "!k- a\n", 0);
    check_shape("doc[0,6](bad[0,5](list[2,5](li[2,5](p[4,5]))))");
    /* The extensions are tried in order: "bad" before "good". */
    PARSE(&k_bad_dialect, "!k a\n!q b\n", 0);
    check_shape("doc[0,10](bad[0,4](p[3,4]) good[5,9](p[8,9]))");
}

NAOS_TEST(invalid_start_results_are_rejected_and_the_next_rule_is_called)
{
    PARSE(&k_bad_dialect, "!o a\n", 1);
    check_start_diagnostic(0, "bad", "range does not start at the position",
                           0, 1);
    check_shape("doc[0,5](good[0,4](p[3,4]))");

    PARSE(&k_bad_dialect, "x\n\n  !l a\n", 1);
    check_start_diagnostic(0, "bad", "range leaves the line", 5, 3);
    check_shape("doc[0,10](p[0,1] good[5,9](p[8,9]))");

    PARSE(&k_bad_dialect, " !r a\n", 1);
    check_start_diagnostic(0, "bad", "range leaves the line", 1, 1);
    check_shape("doc[0,6](good[1,5](p[4,5]))");

    PARSE(&k_bad_dialect, "> !e a\n", 1);
    check_start_diagnostic(0, "bad", "empty range", 2, 1);
    check_shape("doc[0,7](bq[0,6](good[2,6](p[5,6])))");
}

NAOS_TEST(a_status_outside_the_contract_is_rejected_as_no)
{
    PARSE(&k_bad_dialect, "!s a\n\n!x b\n", 2);
    check_start_diagnostic(0, "bad", "unknown return value", 0, 1);
    check_start_diagnostic(1, "bad", "unknown return value", 6, 3);
    check_shape("doc[0,11](good[0,4](p[3,4]) good[6,10](p[9,10]))");
}

NAOS_TEST(a_rejected_rule_is_still_called_and_accepted_later)
{
    PARSE(&k_bad_dialect, "!o a\n\n!k b\n", 1);
    check_start_diagnostic(0, "bad", "range does not start at the position",
                           0, 1);
    check_shape("doc[0,11](good[0,4](p[3,4]) bad[6,10](p[9,10]))");
}

NAOS_TEST(nomem_from_a_start_rule_ends_the_parse)
{
    static const char content[] = "a\n\n!n b\n";

    const amark_dialect_extension_t *exts[2];
    amark_dialect_t dialect;

    g_config.dialect = &k_bad_dialect;
    NAOS_CHECK_INT_EQ(amark_parse(content, sizeof(content) - 1, &g_config,
                                  &g_doc),
                      AMARK_ERROR_NOMEM);
    NAOS_CHECK_NULL(g_doc);

    /* With the test extension first, its state is released too (teardown
     * checks that no block is left). */
    exts[0] = amark_test_dialect()->extensions[0];
    exts[1] = &k_bad_ext;
    dialect.struct_size = sizeof(dialect);
    dialect.abi = AMARK_DIALECT_ABI;
    dialect.extensions = exts;
    dialect.extension_count = 2;
    g_config.dialect = &dialect;
    NAOS_CHECK_INT_EQ(amark_parse(content, sizeof(content) - 1, &g_config,
                                  &g_doc),
                      AMARK_ERROR_NOMEM);
    NAOS_CHECK_NULL(g_doc);
    NAOS_CHECK_UINT_GT(g_state.free_calls, 0);
    g_config.dialect = NULL;
}

NAOS_TEST(a_failed_diagnostic_ends_the_parse_with_nomem)
{
    static const char content[] = "!o a\n";
    size_t calls;
    size_t i;

    /* Find the calls of a successful parse, then fail each one. */
    PARSE(&k_bad_dialect, "!o a\n", 1);
    calls = g_state.malloc_calls;
    for (i = 1; i <= calls; i++) {
        amark_document_destroy(g_doc);
        g_doc = NULL;
        amark_test_alloc_init(&g_state);
        g_state.fail_at = i;
        NAOS_CHECK_INT_EQ(amark_parse(content, sizeof(content) - 1,
                                      &g_config, &g_doc),
                          AMARK_ERROR_NOMEM);
        NAOS_CHECK_NULL(g_doc);
        NAOS_CHECK_UINT_EQ(g_state.live, 0);
    }
    g_state.fail_at = 0;
}

NAOS_TEST(without_a_dialect_the_markers_are_text)
{
    PARSE(NULL, "@> a\n", 0);
    check_shape("doc[0,5](p[0,4])");
    PARSE(amark_test_dialect_plain(), "@> a\n!k b\n", 0);
    check_shape("doc[0,10](p[0,9])");
}

NAOS_SUITE(start,
    NAOS_SET_SETUP(start),
    NAOS_SET_TEARDOWN(start),
    NAOS_ADD_TEST(one_line_box_is_a_container_block),
    NAOS_ADD_TEST(box_does_not_continue_on_the_next_line),
    NAOS_ADD_TEST(box_starts_at_its_first_non_space_byte),
    NAOS_ADD_TEST(boxes_nest_on_one_line),
    NAOS_ADD_TEST(commonmark_blocks_start_inside_a_box),
    NAOS_ADD_TEST(box_starts_inside_commonmark_containers),
    NAOS_ADD_TEST(box_closes_a_list_whose_item_does_not_continue),
    NAOS_ADD_TEST(box_interrupts_a_paragraph),
    NAOS_ADD_TEST(a_rule_that_cannot_interrupt_a_paragraph_is_not_called_in_one),
    NAOS_ADD_TEST(start_rules_see_the_line_and_the_position),
    NAOS_ADD_TEST(start_rules_see_the_parent_a_new_block_would_have),
    NAOS_ADD_TEST(no_start_rule_is_called_on_blank_or_indented_lines),
    NAOS_ADD_TEST(no_start_rule_is_called_inside_code_and_html_blocks),
    NAOS_ADD_TEST(start_rules_are_called_before_the_commonmark_starts),
    NAOS_ADD_TEST(invalid_start_results_are_rejected_and_the_next_rule_is_called),
    NAOS_ADD_TEST(a_status_outside_the_contract_is_rejected_as_no),
    NAOS_ADD_TEST(a_rejected_rule_is_still_called_and_accepted_later),
    NAOS_ADD_TEST(nomem_from_a_start_rule_ends_the_parse),
    NAOS_ADD_TEST(a_failed_diagnostic_ends_the_parse_with_nomem),
    NAOS_ADD_TEST(without_a_dialect_the_markers_are_text)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(reply),
    NAOS_ADD_SUITE(start)
)
