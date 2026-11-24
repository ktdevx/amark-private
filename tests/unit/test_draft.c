/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of the draft (src/draft.h), the working tree of items of one
 * inline content: reset to the root alone, append as the last child of the
 * root, wrap a part of the root's children (one, several, none, up to the
 * end), remove from a list of children, ranges, link and autolink
 * attributes, reuse of the places after a reset, failures of the allocator
 * that leave the tree unchanged, the limit on the number of items, and
 * dialect kind IDs kept as they are.
 *
 * Then the emit step (amark_draft_emit): texts merged (adjacent, apart, a
 * break between), node IDs in document order, the attributes of links,
 * images and autolinks, dialect kinds as inline nodes, the depth and node
 * count limits at their bounds, allocator failures, and the tree check on
 * every tree written.
 */

#include "arena.h"
#include "array.h"
#include "draft.h"
#include "test_alloc.h"
#include "tree_check.h"
#include "tree_dump.h"
#include "tree_internal.h"

#include <amark/amark.h>

#include <naos/naos.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * Fixture. NAOS_REQUIRE_* leaves a test with longjmp, so the scratch arena,
 * the draft and the document of the emit tests are kept in static
 * variables, and teardown releases them and checks that no block is left.
 */
#define SHAPE_SIZE 4096

static amark_test_alloc_t g_state;
static amark_allocator_t g_allocator;
static amark_arena_t g_scratch;
static amark_draft_t g_draft;
static amark_document_t *g_doc;
static char g_shape[SHAPE_SIZE];

NAOS_SETUP(counting)
{
    amark_test_alloc_init(&g_state);
    g_allocator = amark_test_allocator(&g_state);
    amark_arena_init(&g_scratch, &g_allocator);
    amark_draft_init(&g_draft, &g_scratch);
    g_doc = NULL;
}

NAOS_TEARDOWN(counting)
{
    amark_document_destroy(g_doc);
    g_doc = NULL;
    amark_arena_deinit(&g_scratch);
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
    NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
}

/* Appends to buf (of SHAPE_SIZE bytes, holding a string). */
static void put(char *buf, const char *text)
{
    size_t len = strlen(buf);

    if (len + strlen(text) < SHAPE_SIZE) {
        memcpy(buf + len, text, strlen(text) + 1);
    }
}

/*
 * Checks the links of the children of parent: each child names parent as
 * its parent, prev and next agree, and first_child and last_child are the
 * ends of the list. Returns false at the first mismatch.
 */
static bool children_consistent(const amark_draft_t *draft,
                                amark_draft_id_t parent)
{
    const amark_draft_item_t *p = amark_draft_get(draft, parent);
    amark_draft_id_t prev = AMARK_DRAFT_NONE;
    amark_draft_id_t child = p->first_child;

    while (child != AMARK_DRAFT_NONE) {
        const amark_draft_item_t *c = amark_draft_get(draft, child);

        if (c->parent != parent || c->prev != prev) {
            return false;
        }
        prev = child;
        child = c->next;
    }
    return p->last_child == prev;
}

/*
 * Writes the shape of the tree under the root to g_shape and returns it: the
 * IDs of the items in document order, separated by spaces, with the children
 * of an item in parentheses after its ID ("2 3(4 5) 6"). Walks without
 * recursion, by first_child, next and parent. Writes "BROKEN" if a list of
 * children is not consistent.
 */
static const char *shape(const amark_draft_t *draft)
{
    amark_draft_id_t id;
    char num[16];

    g_shape[0] = '\0';
    if (!children_consistent(draft, AMARK_DRAFT_ROOT)) {
        put(g_shape, "BROKEN");
        return g_shape;
    }
    id = amark_draft_get(draft, AMARK_DRAFT_ROOT)->first_child;
    while (id != AMARK_DRAFT_NONE) {
        const amark_draft_item_t *item = amark_draft_get(draft, id);

        snprintf(num, sizeof(num), "%lu", (unsigned long)id);
        put(g_shape, num);
        if (item->first_child != AMARK_DRAFT_NONE) {
            if (!children_consistent(draft, id)) {
                g_shape[0] = '\0';
                put(g_shape, "BROKEN");
                return g_shape;
            }
            put(g_shape, "(");
            id = item->first_child;
            continue;
        }
        /* Go up until an item with a next sibling. */
        while (id != AMARK_DRAFT_NONE) {
            item = amark_draft_get(draft, id);
            if (item->next != AMARK_DRAFT_NONE) {
                put(g_shape, " ");
                id = item->next;
                break;
            }
            id = item->parent;
            if (id == AMARK_DRAFT_ROOT) {
                id = AMARK_DRAFT_NONE;
            } else {
                put(g_shape, ")");
            }
        }
    }
    return g_shape;
}

/* Appends n text items, the i-th (from 0) with the range [i, i + 1). */
static void append_texts(size_t n)
{
    size_t i;
    amark_draft_id_t id;

    for (i = 0; i < n; i++) {
        NAOS_REQUIRE_INT_EQ(amark_draft_append(&g_draft, AMARK_KIND_TEXT, i,
                                               i + 1, &id),
                            AMARK_ERROR_NONE);
    }
}

/* ---- reset and append ---- */

NAOS_TEST(init_allocates_nothing)
{
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 0);
}

NAOS_TEST(reset_leaves_the_root_alone)
{
    const amark_draft_item_t *root;

    NAOS_REQUIRE_INT_EQ(amark_draft_reset(&g_draft), AMARK_ERROR_NONE);
    root = amark_draft_get(&g_draft, AMARK_DRAFT_ROOT);
    NAOS_CHECK_UINT_EQ(root->kind, AMARK_KIND_NONE);
    NAOS_CHECK_UINT_EQ(root->parent, AMARK_DRAFT_NONE);
    NAOS_CHECK_UINT_EQ(root->first_child, AMARK_DRAFT_NONE);
    NAOS_CHECK_UINT_EQ(root->last_child, AMARK_DRAFT_NONE);
    NAOS_CHECK_UINT_EQ(root->prev, AMARK_DRAFT_NONE);
    NAOS_CHECK_UINT_EQ(root->next, AMARK_DRAFT_NONE);
    NAOS_CHECK_UINT_EQ(root->attr, 0);
    NAOS_CHECK_UINT_EQ(root->node, AMARK_NODE_NONE);
    NAOS_CHECK_STR_EQ(shape(&g_draft), "");
}

NAOS_TEST(first_reset_failure_reports_nomem)
{
    g_state.fail_at = 1;
    NAOS_CHECK_INT_EQ(amark_draft_reset(&g_draft), AMARK_ERROR_NOMEM);
    g_state.fail_at = 0;
    NAOS_REQUIRE_INT_EQ(amark_draft_reset(&g_draft), AMARK_ERROR_NONE);
    NAOS_CHECK_STR_EQ(shape(&g_draft), "");
}

NAOS_TEST(append_adds_last_children_of_the_root_in_order)
{
    amark_draft_id_t a;
    amark_draft_id_t b;
    amark_draft_id_t c;
    const amark_draft_item_t *item;

    NAOS_REQUIRE_INT_EQ(amark_draft_reset(&g_draft), AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_draft_append(&g_draft, AMARK_KIND_TEXT, 0, 3,
                                           &a),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_draft_append(&g_draft, AMARK_KIND_SOFT_BREAK,
                                           3, 4, &b),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_draft_append(&g_draft, AMARK_KIND_TEXT, 6, 9,
                                           &c),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(a, 2);
    NAOS_CHECK_UINT_EQ(b, 3);
    NAOS_CHECK_UINT_EQ(c, 4);
    NAOS_CHECK_STR_EQ(shape(&g_draft), "2 3 4");

    item = amark_draft_get(&g_draft, b);
    NAOS_CHECK_UINT_EQ(item->kind, AMARK_KIND_SOFT_BREAK);
    NAOS_CHECK_UINT_EQ(item->start, 3);
    NAOS_CHECK_UINT_EQ(item->end, 4);
    NAOS_CHECK_UINT_EQ(item->parent, AMARK_DRAFT_ROOT);
    NAOS_CHECK_UINT_EQ(item->first_child, AMARK_DRAFT_NONE);
    NAOS_CHECK_UINT_EQ(item->last_child, AMARK_DRAFT_NONE);
    NAOS_CHECK_UINT_EQ(item->attr, 0);
    NAOS_CHECK_UINT_EQ(item->node, AMARK_NODE_NONE);
    NAOS_CHECK_UINT_EQ(amark_draft_get(&g_draft, AMARK_DRAFT_ROOT)
                           ->first_child,
                       a);
    NAOS_CHECK_UINT_EQ(amark_draft_get(&g_draft, AMARK_DRAFT_ROOT)
                           ->last_child,
                       c);
}

NAOS_TEST(set_range_changes_only_the_range)
{
    const amark_draft_item_t *item;

    NAOS_REQUIRE_INT_EQ(amark_draft_reset(&g_draft), AMARK_ERROR_NONE);
    append_texts(3);
    amark_draft_set_range(&g_draft, 3, 1, 1);
    item = amark_draft_get(&g_draft, 3);
    NAOS_CHECK_UINT_EQ(item->start, 1);
    NAOS_CHECK_UINT_EQ(item->end, 1);
    NAOS_CHECK_UINT_EQ(item->kind, AMARK_KIND_TEXT);
    NAOS_CHECK_STR_EQ(shape(&g_draft), "2 3 4");
}

/* ---- wrap ---- */

NAOS_TEST(wrap_one_sibling)
{
    amark_draft_id_t id;
    const amark_draft_item_t *item;

    NAOS_REQUIRE_INT_EQ(amark_draft_reset(&g_draft), AMARK_ERROR_NONE);
    append_texts(3); /* 2 3 4 */
    NAOS_REQUIRE_INT_EQ(amark_draft_wrap(&g_draft, 2, 4, AMARK_KIND_EMPHASIS,
                                         0, 3, &id),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(id, 5);
    NAOS_CHECK_STR_EQ(shape(&g_draft), "2 5(3) 4");
    item = amark_draft_get(&g_draft, id);
    NAOS_CHECK_UINT_EQ(item->kind, AMARK_KIND_EMPHASIS);
    NAOS_CHECK_UINT_EQ(item->start, 0);
    NAOS_CHECK_UINT_EQ(item->end, 3);
    NAOS_CHECK_UINT_EQ(item->parent, AMARK_DRAFT_ROOT);
    NAOS_CHECK_UINT_EQ(amark_draft_get(&g_draft, 3)->prev, AMARK_DRAFT_NONE);
    NAOS_CHECK_UINT_EQ(amark_draft_get(&g_draft, 3)->next, AMARK_DRAFT_NONE);
}

NAOS_TEST(wrap_several_siblings)
{
    amark_draft_id_t id;

    NAOS_REQUIRE_INT_EQ(amark_draft_reset(&g_draft), AMARK_ERROR_NONE);
    append_texts(6); /* 2 3 4 5 6 7 */
    NAOS_REQUIRE_INT_EQ(amark_draft_wrap(&g_draft, 3, 7, AMARK_KIND_STRONG,
                                         1, 6, &id),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_STR_EQ(shape(&g_draft), "2 3 8(4 5 6) 7");
}

NAOS_TEST(wrap_nothing_between)
{
    amark_draft_id_t id;
    const amark_draft_item_t *item;

    NAOS_REQUIRE_INT_EQ(amark_draft_reset(&g_draft), AMARK_ERROR_NONE);
    append_texts(3); /* 2 3 4 */
    NAOS_REQUIRE_INT_EQ(amark_draft_wrap(&g_draft, 2, 3, AMARK_KIND_LINK,
                                         0, 2, &id),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_STR_EQ(shape(&g_draft), "2 5 3 4");
    item = amark_draft_get(&g_draft, id);
    NAOS_CHECK_UINT_EQ(item->first_child, AMARK_DRAFT_NONE);
    NAOS_CHECK_UINT_EQ(item->last_child, AMARK_DRAFT_NONE);
}

NAOS_TEST(wrap_up_to_the_end)
{
    amark_draft_id_t id;

    NAOS_REQUIRE_INT_EQ(amark_draft_reset(&g_draft), AMARK_ERROR_NONE);
    append_texts(4); /* 2 3 4 5 */
    NAOS_REQUIRE_INT_EQ(amark_draft_wrap(&g_draft, 3, AMARK_DRAFT_NONE,
                                         AMARK_KIND_LINK, 1, 4, &id),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_STR_EQ(shape(&g_draft), "2 3 6(4 5)");
    NAOS_CHECK_UINT_EQ(amark_draft_get(&g_draft, AMARK_DRAFT_ROOT)
                           ->last_child,
                       id);
}

NAOS_TEST(wrap_nothing_at_the_end)
{
    amark_draft_id_t id;

    NAOS_REQUIRE_INT_EQ(amark_draft_reset(&g_draft), AMARK_ERROR_NONE);
    append_texts(2); /* 2 3 */
    NAOS_REQUIRE_INT_EQ(amark_draft_wrap(&g_draft, 3, AMARK_DRAFT_NONE,
                                         AMARK_KIND_LINK, 2, 2, &id),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_STR_EQ(shape(&g_draft), "2 3 4");
    NAOS_CHECK_UINT_EQ(amark_draft_get(&g_draft, AMARK_DRAFT_ROOT)
                           ->last_child,
                       id);
}

/* Wrapping inside out: emphasis inside a link, then the wrapper wrapped. */
NAOS_TEST(wrap_nests)
{
    amark_draft_id_t inner;
    amark_draft_id_t outer;
    amark_draft_id_t text;

    NAOS_REQUIRE_INT_EQ(amark_draft_reset(&g_draft), AMARK_ERROR_NONE);
    append_texts(5); /* 2 3 4 5 6 */
    NAOS_REQUIRE_INT_EQ(amark_draft_wrap(&g_draft, 3, 5, AMARK_KIND_EMPHASIS,
                                         1, 4, &inner),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_STR_EQ(shape(&g_draft), "2 3 7(4) 5 6");
    NAOS_REQUIRE_INT_EQ(amark_draft_wrap(&g_draft, 2, 6, AMARK_KIND_LINK,
                                         0, 6, &outer),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_STR_EQ(shape(&g_draft), "2 8(3 7(4) 5) 6");
    NAOS_CHECK_UINT_EQ(amark_draft_get(&g_draft, inner)->parent, outer);
    /* Appending still adds to the root, after the wrapped items. */
    NAOS_REQUIRE_INT_EQ(amark_draft_append(&g_draft, AMARK_KIND_TEXT, 6, 7,
                                           &text),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_STR_EQ(shape(&g_draft), "2 8(3 7(4) 5) 6 9");
}

/* ---- remove ---- */

NAOS_TEST(remove_first_middle_and_last)
{
    NAOS_REQUIRE_INT_EQ(amark_draft_reset(&g_draft), AMARK_ERROR_NONE);
    append_texts(5); /* 2 3 4 5 6 */
    amark_draft_remove(&g_draft, 4);
    NAOS_CHECK_STR_EQ(shape(&g_draft), "2 3 5 6");
    amark_draft_remove(&g_draft, 2);
    NAOS_CHECK_STR_EQ(shape(&g_draft), "3 5 6");
    amark_draft_remove(&g_draft, 6);
    NAOS_CHECK_STR_EQ(shape(&g_draft), "3 5");
    amark_draft_remove(&g_draft, 3);
    amark_draft_remove(&g_draft, 5);
    NAOS_CHECK_STR_EQ(shape(&g_draft), "");
    NAOS_CHECK_UINT_EQ(amark_draft_get(&g_draft, AMARK_DRAFT_ROOT)
                           ->first_child,
                       AMARK_DRAFT_NONE);
    NAOS_CHECK_UINT_EQ(amark_draft_get(&g_draft, AMARK_DRAFT_ROOT)
                           ->last_child,
                       AMARK_DRAFT_NONE);
    /* The removed elements stay; new items get new IDs. */
    append_texts(1);
    NAOS_CHECK_STR_EQ(shape(&g_draft), "7");
}

NAOS_TEST(remove_a_child_of_a_wrapper)
{
    amark_draft_id_t id;

    NAOS_REQUIRE_INT_EQ(amark_draft_reset(&g_draft), AMARK_ERROR_NONE);
    append_texts(5); /* 2 3 4 5 6 */
    NAOS_REQUIRE_INT_EQ(amark_draft_wrap(&g_draft, 2, 6, AMARK_KIND_STRONG,
                                         0, 5, &id),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_STR_EQ(shape(&g_draft), "2 7(3 4 5) 6");
    amark_draft_remove(&g_draft, 3);
    NAOS_CHECK_STR_EQ(shape(&g_draft), "2 7(4 5) 6");
    amark_draft_remove(&g_draft, 5);
    NAOS_CHECK_STR_EQ(shape(&g_draft), "2 7(4) 6");
    amark_draft_remove(&g_draft, 4);
    NAOS_CHECK_STR_EQ(shape(&g_draft), "2 7 6");
    /* Removing the delimiter items around the wrapper, as emphasis does. */
    amark_draft_remove(&g_draft, 2);
    amark_draft_remove(&g_draft, 6);
    NAOS_CHECK_STR_EQ(shape(&g_draft), "7");
}

NAOS_TEST(remove_a_wrapper_with_its_children)
{
    amark_draft_id_t id;

    NAOS_REQUIRE_INT_EQ(amark_draft_reset(&g_draft), AMARK_ERROR_NONE);
    append_texts(3); /* 2 3 4 */
    NAOS_REQUIRE_INT_EQ(amark_draft_wrap(&g_draft, 2, AMARK_DRAFT_NONE,
                                         AMARK_KIND_LINK, 0, 3, &id),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_STR_EQ(shape(&g_draft), "2 5(3 4)");
    amark_draft_remove(&g_draft, id);
    NAOS_CHECK_STR_EQ(shape(&g_draft), "2");
}

/* Wrapping next to a removed item uses the current links only. */
NAOS_TEST(wrap_after_remove)
{
    amark_draft_id_t id;

    NAOS_REQUIRE_INT_EQ(amark_draft_reset(&g_draft), AMARK_ERROR_NONE);
    append_texts(5); /* 2 3 4 5 6 */
    amark_draft_remove(&g_draft, 4);
    NAOS_REQUIRE_INT_EQ(amark_draft_wrap(&g_draft, 3, 6, AMARK_KIND_EMPHASIS,
                                         1, 5, &id),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_STR_EQ(shape(&g_draft), "2 3 7(5) 6");
}

/* ---- attributes ---- */

NAOS_TEST(set_link_keeps_a_copy)
{
    amark_draft_id_t link;
    amark_draft_id_t image;
    amark_draft_link_t attr;
    const amark_draft_item_t *item;
    const amark_draft_link_t *stored;

    NAOS_REQUIRE_INT_EQ(amark_draft_reset(&g_draft), AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_draft_append(&g_draft, AMARK_KIND_LINK, 0, 10,
                                           &link),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_draft_append(&g_draft, AMARK_KIND_IMAGE, 10,
                                           20, &image),
                        AMARK_ERROR_NONE);

    memset(&attr, 0, sizeof(attr));
    attr.form = AMARK_LINK_INLINE;
    attr.has_destination = true;
    attr.destination.start = 4;
    attr.destination.end = 4;
    attr.has_title = false;
    attr.definition = AMARK_NODE_NONE;
    NAOS_REQUIRE_INT_EQ(amark_draft_set_link(&g_draft, link, &attr),
                        AMARK_ERROR_NONE);

    attr.form = AMARK_LINK_FULL;
    attr.has_destination = false;
    attr.destination.start = 0;
    attr.destination.end = 0;
    attr.definition = 7;
    NAOS_REQUIRE_INT_EQ(amark_draft_set_link(&g_draft, image, &attr),
                        AMARK_ERROR_NONE);
    attr.form = AMARK_LINK_SHORTCUT; /* the draft keeps its own copy */

    item = amark_draft_get(&g_draft, link);
    NAOS_REQUIRE_UINT_EQ(item->attr, 1);
    stored = (const amark_draft_link_t *)amark_array_at(&g_draft.links,
                                                        item->attr - 1);
    NAOS_CHECK_INT_EQ(stored->form, AMARK_LINK_INLINE);
    NAOS_CHECK_TRUE(stored->has_destination);
    NAOS_CHECK_UINT_EQ(stored->destination.start, 4);
    NAOS_CHECK_UINT_EQ(stored->destination.end, 4);
    NAOS_CHECK_FALSE(stored->has_title);
    NAOS_CHECK_UINT_EQ(stored->definition, AMARK_NODE_NONE);

    item = amark_draft_get(&g_draft, image);
    NAOS_REQUIRE_UINT_EQ(item->attr, 2);
    stored = (const amark_draft_link_t *)amark_array_at(&g_draft.links,
                                                        item->attr - 1);
    NAOS_CHECK_INT_EQ(stored->form, AMARK_LINK_FULL);
    NAOS_CHECK_FALSE(stored->has_destination);
    NAOS_CHECK_UINT_EQ(stored->definition, 7);
}

NAOS_TEST(set_autolink_stores_the_type_plus_one)
{
    amark_draft_id_t uri;
    amark_draft_id_t email;

    NAOS_REQUIRE_INT_EQ(amark_draft_reset(&g_draft), AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_draft_append(&g_draft, AMARK_KIND_AUTOLINK, 0,
                                           5, &uri),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_draft_append(&g_draft, AMARK_KIND_AUTOLINK, 5,
                                           9, &email),
                        AMARK_ERROR_NONE);
    amark_draft_set_autolink(&g_draft, uri, AMARK_AUTOLINK_URI);
    amark_draft_set_autolink(&g_draft, email, AMARK_AUTOLINK_EMAIL);
    NAOS_CHECK_UINT_EQ(amark_draft_get(&g_draft, uri)->attr,
                       (uint32_t)AMARK_AUTOLINK_URI + 1);
    NAOS_CHECK_UINT_EQ(amark_draft_get(&g_draft, email)->attr,
                       (uint32_t)AMARK_AUTOLINK_EMAIL + 1);
    NAOS_CHECK_UINT_EQ(amark_array_len(&g_draft.links), 0);
}

/* ---- dialect kinds ---- */

NAOS_TEST(dialect_kinds_are_kept_as_they_are)
{
    amark_draft_id_t a;
    amark_draft_id_t b;
    amark_draft_id_t w;

    NAOS_REQUIRE_INT_EQ(amark_draft_reset(&g_draft), AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_draft_append(&g_draft, AMARK_KIND_DIALECT_MIN,
                                           0, 2, &a),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_draft_append(&g_draft, UINT32_MAX, 2, 4, &b),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_draft_wrap(&g_draft, a, AMARK_DRAFT_NONE,
                                         AMARK_KIND_DIALECT_MIN + 12345u, 2,
                                         4, &w),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(amark_draft_get(&g_draft, a)->kind,
                       AMARK_KIND_DIALECT_MIN);
    NAOS_CHECK_UINT_EQ(amark_draft_get(&g_draft, b)->kind, UINT32_MAX);
    NAOS_CHECK_UINT_EQ(amark_draft_get(&g_draft, w)->kind,
                       AMARK_KIND_DIALECT_MIN + 12345u);
    NAOS_CHECK_STR_EQ(shape(&g_draft), "2 4(3)");
}

/* ---- reuse after reset ---- */

NAOS_TEST(reset_reuses_the_places)
{
    amark_draft_link_t attr;
    amark_draft_id_t id;
    size_t calls;

    memset(&attr, 0, sizeof(attr));
    attr.form = AMARK_LINK_SHORTCUT;
    NAOS_REQUIRE_INT_EQ(amark_draft_reset(&g_draft), AMARK_ERROR_NONE);
    append_texts(100);
    NAOS_REQUIRE_INT_EQ(amark_draft_wrap(&g_draft, 2, AMARK_DRAFT_NONE,
                                         AMARK_KIND_LINK, 0, 100, &id),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_draft_set_link(&g_draft, id, &attr),
                        AMARK_ERROR_NONE);
    calls = g_state.malloc_calls;

    NAOS_REQUIRE_INT_EQ(amark_draft_reset(&g_draft), AMARK_ERROR_NONE);
    NAOS_CHECK_STR_EQ(shape(&g_draft), "");
    NAOS_CHECK_UINT_EQ(amark_array_len(&g_draft.links), 0);
    NAOS_CHECK_UINT_EQ(amark_draft_get(&g_draft, AMARK_DRAFT_ROOT)
                           ->first_child,
                       AMARK_DRAFT_NONE);

    /* The same number of items and links again allocates nothing, and the
       IDs start over at 2. */
    append_texts(100);
    NAOS_REQUIRE_INT_EQ(amark_draft_wrap(&g_draft, 2, AMARK_DRAFT_NONE,
                                         AMARK_KIND_LINK, 0, 100, &id),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_draft_set_link(&g_draft, id, &attr),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(id, 102);
    NAOS_CHECK_UINT_EQ(amark_draft_get(&g_draft, id)->attr, 1);
    NAOS_CHECK_UINT_EQ(amark_draft_get(&g_draft, 2)->next, id);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, calls);
}

/* ---- failures ---- */

/*
 * Appends until a growth needs a new block and makes that call fail: the
 * error is AMARK_ERROR_NOMEM, *out is not written and the tree is as it was.
 */
NAOS_TEST(append_failure_changes_nothing)
{
    char before[SHAPE_SIZE];
    size_t len;
    size_t i;
    int failed = 0;

    NAOS_REQUIRE_INT_EQ(amark_draft_reset(&g_draft), AMARK_ERROR_NONE);
    for (i = 0; i < 1000 && !failed; i++) {
        amark_draft_id_t id = 12345;
        amark_error_t err;

        strcpy(before, shape(&g_draft));
        len = amark_array_len(&g_draft.items);
        g_state.fail_at = g_state.malloc_calls + 1;
        err = amark_draft_append(&g_draft, AMARK_KIND_TEXT, i, i + 1, &id);
        g_state.fail_at = 0;
        if (err != AMARK_ERROR_NONE) {
            NAOS_CHECK_INT_EQ(err, AMARK_ERROR_NOMEM);
            NAOS_CHECK_UINT_EQ(id, 12345);
            NAOS_CHECK_UINT_EQ(amark_array_len(&g_draft.items), len);
            NAOS_CHECK_STR_EQ(shape(&g_draft), before);
            failed = 1;
        }
    }
    NAOS_REQUIRE_TRUE(failed);
    append_texts(1);
}

/*
 * Wraps again and again (each time the root's children after the first one)
 * until a growth needs a new block and makes that call fail: nothing has
 * moved.
 */
NAOS_TEST(wrap_failure_changes_nothing)
{
    char before[SHAPE_SIZE];
    size_t len;
    size_t i;
    int failed = 0;

    NAOS_REQUIRE_INT_EQ(amark_draft_reset(&g_draft), AMARK_ERROR_NONE);
    append_texts(3); /* 2 3 4 */
    for (i = 0; i < 200 && !failed; i++) {
        amark_draft_id_t id = 12345;
        amark_error_t err;

        strcpy(before, shape(&g_draft));
        len = amark_array_len(&g_draft.items);
        g_state.fail_at = g_state.malloc_calls + 1;
        err = amark_draft_wrap(&g_draft, 2, AMARK_DRAFT_NONE,
                               AMARK_KIND_EMPHASIS, 1, 3, &id);
        g_state.fail_at = 0;
        if (err != AMARK_ERROR_NONE) {
            NAOS_CHECK_INT_EQ(err, AMARK_ERROR_NOMEM);
            NAOS_CHECK_UINT_EQ(id, 12345);
            NAOS_CHECK_UINT_EQ(amark_array_len(&g_draft.items), len);
            NAOS_CHECK_STR_EQ(shape(&g_draft), before);
            failed = 1;
        }
    }
    NAOS_REQUIRE_TRUE(failed);
}

/* Sets links until a growth fails: the item keeps attr 0. */
NAOS_TEST(set_link_failure_changes_nothing)
{
    amark_draft_link_t attr;
    size_t i;
    int failed = 0;

    memset(&attr, 0, sizeof(attr));
    attr.form = AMARK_LINK_INLINE;
    NAOS_REQUIRE_INT_EQ(amark_draft_reset(&g_draft), AMARK_ERROR_NONE);
    for (i = 0; i < 1000 && !failed; i++) {
        amark_draft_id_t id;
        amark_error_t err;
        size_t len;

        NAOS_REQUIRE_INT_EQ(amark_draft_append(&g_draft, AMARK_KIND_LINK, i,
                                               i + 1, &id),
                            AMARK_ERROR_NONE);
        len = amark_array_len(&g_draft.links);
        g_state.fail_at = g_state.malloc_calls + 1;
        err = amark_draft_set_link(&g_draft, id, &attr);
        g_state.fail_at = 0;
        if (err != AMARK_ERROR_NONE) {
            NAOS_CHECK_INT_EQ(err, AMARK_ERROR_NOMEM);
            NAOS_CHECK_UINT_EQ(amark_draft_get(&g_draft, id)->attr, 0);
            NAOS_CHECK_UINT_EQ(amark_array_len(&g_draft.links), len);
            failed = 1;
        } else {
            NAOS_CHECK_UINT_EQ(amark_draft_get(&g_draft, id)->attr, len + 1);
        }
    }
    NAOS_REQUIRE_TRUE(failed);
}

/*
 * The number of items (the root included, the unused element 0 not) is
 * checked before anything is allocated or written, so the limit is tested by
 * setting the length of the array (members are visible in src/) to the value
 * it has with UINT32_MAX - 1 items, and putting it back.
 */
NAOS_TEST(too_many_items_is_limit_exceeded)
{
    amark_draft_id_t id = 12345;
    size_t len;
    size_t calls;

    NAOS_REQUIRE_INT_EQ(amark_draft_reset(&g_draft), AMARK_ERROR_NONE);
    append_texts(3); /* 2 3 4 */
    len = g_draft.items.len;
    calls = g_state.malloc_calls;
    g_draft.items.len = (size_t)UINT32_MAX; /* UINT32_MAX - 1 items */
    NAOS_CHECK_INT_EQ(amark_draft_append(&g_draft, AMARK_KIND_TEXT, 0, 1,
                                         &id),
                      AMARK_ERROR_LIMIT_EXCEEDED);
    NAOS_CHECK_INT_EQ(amark_draft_wrap(&g_draft, 2, 4, AMARK_KIND_EMPHASIS,
                                       0, 3, &id),
                      AMARK_ERROR_LIMIT_EXCEEDED);
    g_draft.items.len = len;
    NAOS_CHECK_UINT_EQ(id, 12345);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, calls);
    NAOS_CHECK_STR_EQ(shape(&g_draft), "2 3 4");

    /* With the length put back, appending works again. */
    NAOS_REQUIRE_INT_EQ(amark_draft_append(&g_draft, AMARK_KIND_TEXT, 3, 4,
                                           &id),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(id, 5);
}

/* The same for the links: attr (the index plus 1) must fit in uint32_t. */
NAOS_TEST(too_many_links_is_limit_exceeded)
{
    amark_draft_link_t attr;
    amark_draft_id_t id;
    size_t calls;

    memset(&attr, 0, sizeof(attr));
    attr.form = AMARK_LINK_INLINE;
    NAOS_REQUIRE_INT_EQ(amark_draft_reset(&g_draft), AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_draft_append(&g_draft, AMARK_KIND_LINK, 0, 4,
                                           &id),
                        AMARK_ERROR_NONE);
    calls = g_state.malloc_calls;
    g_draft.links.len = (size_t)UINT32_MAX;
    NAOS_CHECK_INT_EQ(amark_draft_set_link(&g_draft, id, &attr),
                      AMARK_ERROR_LIMIT_EXCEEDED);
    g_draft.links.len = 0;
    NAOS_CHECK_UINT_EQ(amark_draft_get(&g_draft, id)->attr, 0);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, calls);
}

/* ---- emit ---- */

#define DUMP_SIZE 1024

static char g_dump[DUMP_SIZE];

/*
 * Creates g_doc over content (a string) with one paragraph, the last child
 * of the root, over [start, strlen(content)), resets the draft and returns
 * the paragraph. The paragraph is at depth 1 (the root is at depth 0).
 */
static amark_node_id_t make_paragraph(const char *content, size_t start)
{
    size_t len = strlen(content);
    amark_node_id_t para = AMARK_NODE_NONE;

    NAOS_REQUIRE_INT_EQ(amark_document_create(&g_allocator,
                                              (const unsigned char *)content,
                                              len, 0, len, NULL, &g_doc),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_append(g_doc, amark_document_root(g_doc),
                                          AMARK_KIND_PARAGRAPH,
                                          AMARK_NODE_CLASS_AUTO, start, len,
                                          &para),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_draft_reset(&g_draft), AMARK_ERROR_NONE);
    return para;
}

/* Appends an item to the root of g_draft and returns its ID. */
static amark_draft_id_t add(amark_kind_t kind, size_t start, size_t end)
{
    amark_draft_id_t id = AMARK_DRAFT_NONE;

    NAOS_REQUIRE_INT_EQ(amark_draft_append(&g_draft, kind, start, end, &id),
                        AMARK_ERROR_NONE);
    return id;
}

/* Wraps the root's children between after and before; returns the wrapper. */
static amark_draft_id_t wrap(amark_draft_id_t after, amark_draft_id_t before,
                             amark_kind_t kind, size_t start, size_t end)
{
    amark_draft_id_t id = AMARK_DRAFT_NONE;

    NAOS_REQUIRE_INT_EQ(amark_draft_wrap(&g_draft, after, before, kind, start,
                                         end, &id),
                        AMARK_ERROR_NONE);
    return id;
}

/* Emits g_draft under para (at depth 1) without limits. */
static amark_error_t emit(amark_node_id_t para)
{
    return amark_draft_emit(g_doc, &g_draft, para, 1, 0, 0);
}

/* Returns the dump of the whole tree of g_doc. */
static const char *dump(void)
{
    amark_test_dump_tree(g_doc, g_dump, sizeof(g_dump));
    return g_dump;
}

/* Whether the tree of g_doc has no violation. */
static bool tree_ok(void)
{
    return amark_tree_check(g_doc).violation == AMARK_TREE_VIOLATION_NONE;
}

/* Returns the range of node in g_doc ([0, 0) if it cannot be read). */
static amark_range_t node_range(amark_node_id_t node)
{
    amark_range_t r;

    r.start = 0;
    r.end = 0;
    (void)amark_node_range(g_doc, node, &r);
    return r;
}

NAOS_TEST(emit_of_an_empty_draft_adds_nothing)
{
    amark_node_id_t para = make_paragraph("", 0);

    NAOS_REQUIRE_INT_EQ(emit(para), AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 2);
    NAOS_CHECK_STR_EQ(dump(), "(doc (p))");
    NAOS_CHECK_TRUE(tree_ok());
}

NAOS_TEST(emit_merges_adjacent_texts)
{
    amark_node_id_t para = make_paragraph("abcde", 0);
    amark_draft_id_t a;
    amark_draft_id_t b;
    amark_draft_id_t c;

    a = add(AMARK_KIND_TEXT, 0, 2);
    b = add(AMARK_KIND_TEXT, 2, 3);
    c = add(AMARK_KIND_TEXT, 3, 5);
    NAOS_REQUIRE_INT_EQ(emit(para), AMARK_ERROR_NONE);
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (text \"abcde\")))");
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 3);
    NAOS_CHECK_UINT_EQ(node_range(3).start, 0);
    NAOS_CHECK_UINT_EQ(node_range(3).end, 5);
    /* Every merged item names the one node. */
    NAOS_CHECK_UINT_EQ(amark_draft_get(&g_draft, a)->node, 3);
    NAOS_CHECK_UINT_EQ(amark_draft_get(&g_draft, b)->node, 3);
    NAOS_CHECK_UINT_EQ(amark_draft_get(&g_draft, c)->node, 3);
    NAOS_CHECK_TRUE(tree_ok());
}

NAOS_TEST(emit_keeps_texts_apart_when_their_ranges_do_not_meet)
{
    /* The '*' between was taken out of the draft (an unused delimiter would
     * be text; this checks only the rule of the ranges). */
    amark_node_id_t para = make_paragraph("a*b", 0);

    add(AMARK_KIND_TEXT, 0, 1);
    add(AMARK_KIND_TEXT, 2, 3);
    NAOS_REQUIRE_INT_EQ(emit(para), AMARK_ERROR_NONE);
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (text \"a\") (text \"b\")))");
    NAOS_CHECK_TRUE(tree_ok());
}

NAOS_TEST(emit_does_not_merge_texts_across_a_line_break)
{
    amark_node_id_t para = make_paragraph("ab\ncd  \nef", 0);

    add(AMARK_KIND_TEXT, 0, 2);
    add(AMARK_KIND_SOFT_BREAK, 2, 3);
    add(AMARK_KIND_TEXT, 3, 5);
    add(AMARK_KIND_HARD_BREAK, 5, 8);
    add(AMARK_KIND_TEXT, 8, 10);
    NAOS_REQUIRE_INT_EQ(emit(para), AMARK_ERROR_NONE);
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (text \"ab\") (sbr) (text \"cd\") (br)"
                              " (text \"ef\")))");
    NAOS_CHECK_UINT_EQ(amark_node_start_line(g_doc, 7), 3);
    NAOS_CHECK_TRUE(tree_ok());
}

NAOS_TEST(emit_merges_texts_only_among_siblings)
{
    /* "a*bc*d": the texts inside the emphasis merge with each other but not
     * with the texts outside it, whose ranges do not meet them anyway. */
    amark_node_id_t para = make_paragraph("a*bc*d", 0);
    amark_draft_id_t a;
    amark_draft_id_t open;
    amark_draft_id_t close;

    a = add(AMARK_KIND_TEXT, 0, 1);
    open = add(AMARK_KIND_TEXT, 1, 2);
    add(AMARK_KIND_TEXT, 2, 3);
    add(AMARK_KIND_TEXT, 3, 4);
    close = add(AMARK_KIND_TEXT, 4, 5);
    add(AMARK_KIND_TEXT, 5, 6);
    amark_draft_remove(&g_draft, open);
    wrap(a, close, AMARK_KIND_EMPHASIS, 1, 5);
    amark_draft_remove(&g_draft, close);
    NAOS_REQUIRE_INT_EQ(emit(para), AMARK_ERROR_NONE);
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (text \"a\") (em (text \"bc\"))"
                              " (text \"d\")))");
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 6);
    NAOS_CHECK_TRUE(tree_ok());
}

NAOS_TEST(emit_gives_node_ids_in_document_order)
{
    /* "**a*b*c**d": the wrappers are made after their children, with higher
     * item IDs, but the nodes come in document order. */
    amark_node_id_t para = make_paragraph("**a*b*c**d", 0);
    amark_draft_id_t o2;
    amark_draft_id_t o1;
    amark_draft_id_t c1;
    amark_draft_id_t c2;
    amark_node_id_t node;
    amark_node_id_t expected;
    amark_node_id_t next;
    static const amark_kind_t kinds[] = {
        AMARK_KIND_STRONG, AMARK_KIND_TEXT, AMARK_KIND_EMPHASIS,
        AMARK_KIND_TEXT, AMARK_KIND_TEXT, AMARK_KIND_TEXT
    };
    static const size_t starts[] = {0, 2, 3, 4, 6, 9};
    size_t i;

    o2 = add(AMARK_KIND_TEXT, 0, 2);
    add(AMARK_KIND_TEXT, 2, 3);
    o1 = add(AMARK_KIND_TEXT, 3, 4);
    add(AMARK_KIND_TEXT, 4, 5);
    c1 = add(AMARK_KIND_TEXT, 5, 6);
    add(AMARK_KIND_TEXT, 6, 7);
    c2 = add(AMARK_KIND_TEXT, 7, 9);
    add(AMARK_KIND_TEXT, 9, 10);
    wrap(o1, c1, AMARK_KIND_EMPHASIS, 3, 6);
    amark_draft_remove(&g_draft, o1);
    amark_draft_remove(&g_draft, c1);
    wrap(o2, c2, AMARK_KIND_STRONG, 0, 9);
    amark_draft_remove(&g_draft, o2);
    amark_draft_remove(&g_draft, c2);
    NAOS_REQUIRE_INT_EQ(emit(para), AMARK_ERROR_NONE);
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (strong (text \"a\") (em (text \"b\"))"
                              " (text \"c\")) (text \"d\")))");
    NAOS_CHECK_TRUE(tree_ok());

    /* Walk in document order: the IDs are para + 1, para + 2, ... */
    node = amark_node_first_child(g_doc, para);
    expected = para + 1;
    for (i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
        NAOS_REQUIRE_UINT_NE(node, AMARK_NODE_NONE);
        NAOS_CHECK_UINT_EQ(node, expected);
        NAOS_CHECK_UINT_EQ(amark_node_kind(g_doc, node), kinds[i]);
        NAOS_CHECK_UINT_EQ(node_range(node).start, starts[i]);
        expected++;
        next = amark_node_first_child(g_doc, node);
        while (next == AMARK_NODE_NONE && node != para) {
            next = amark_node_next_sibling(g_doc, node);
            if (next == AMARK_NODE_NONE) {
                node = amark_node_parent(g_doc, node);
            }
        }
        node = next;
    }
    NAOS_CHECK_UINT_EQ(node, AMARK_NODE_NONE);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), para + 6);
}

NAOS_TEST(emit_sets_the_attributes_of_links_images_and_autolinks)
{
    /* After the definition (0 to 9): "[a](/u \"t\")" at 9, " ![b][x]" at
     * 20, " [c]()" at 28, " <http://y>" at 34; 45 bytes in all. */
    static const char content[] =
        "[x]: /v\n\n[a](/u \"t\") ![b][x] [c]() <http://y>";
    amark_node_id_t def = AMARK_NODE_NONE;
    amark_node_id_t para;
    amark_range_t label;
    amark_range_t dest;
    amark_draft_link_t attr;
    amark_draft_id_t link;
    amark_draft_id_t image;
    amark_draft_id_t empty;
    amark_draft_id_t autolink;
    amark_draft_id_t t;

    para = make_paragraph(content, 9);
    /* The definition, before the paragraph. */
    label.start = 1;
    label.end = 2;
    dest.start = 5;
    dest.end = 7;
    NAOS_REQUIRE_INT_EQ(amark_tree_insert_before(
                            g_doc, para,
                            AMARK_KIND_LINK_REFERENCE_DEFINITION,
                            AMARK_NODE_CLASS_AUTO, 0, 7, &def),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_definition(g_doc, def, &label, &dest,
                                                  NULL),
                        AMARK_ERROR_NONE);

    /* [a](/u "t") */
    t = add(AMARK_KIND_TEXT, 9, 10);
    add(AMARK_KIND_TEXT, 10, 11);
    add(AMARK_KIND_TEXT, 11, 20);
    amark_draft_remove(&g_draft, t + 2);
    link = wrap(t, AMARK_DRAFT_NONE, AMARK_KIND_LINK, 9, 20);
    amark_draft_remove(&g_draft, t);
    memset(&attr, 0, sizeof(attr));
    attr.form = AMARK_LINK_INLINE;
    attr.has_destination = true;
    attr.destination.start = 13;
    attr.destination.end = 15;
    attr.has_title = true;
    attr.title.start = 17;
    attr.title.end = 18;
    attr.definition = AMARK_NODE_NONE;
    NAOS_REQUIRE_INT_EQ(amark_draft_set_link(&g_draft, link, &attr),
                        AMARK_ERROR_NONE);

    /* " " then ![b][x] */
    add(AMARK_KIND_TEXT, 20, 21);
    t = add(AMARK_KIND_TEXT, 21, 23);
    add(AMARK_KIND_TEXT, 23, 24);
    add(AMARK_KIND_TEXT, 24, 28);
    amark_draft_remove(&g_draft, t + 2);
    image = wrap(t, AMARK_DRAFT_NONE, AMARK_KIND_IMAGE, 21, 28);
    amark_draft_remove(&g_draft, t);
    memset(&attr, 0, sizeof(attr));
    attr.form = AMARK_LINK_FULL;
    attr.definition = def;
    NAOS_REQUIRE_INT_EQ(amark_draft_set_link(&g_draft, image, &attr),
                        AMARK_ERROR_NONE);

    /* " " then [c]() */
    add(AMARK_KIND_TEXT, 28, 29);
    t = add(AMARK_KIND_TEXT, 29, 30);
    add(AMARK_KIND_TEXT, 30, 31);
    add(AMARK_KIND_TEXT, 31, 34);
    amark_draft_remove(&g_draft, t + 2);
    empty = wrap(t, AMARK_DRAFT_NONE, AMARK_KIND_LINK, 29, 34);
    amark_draft_remove(&g_draft, t);
    memset(&attr, 0, sizeof(attr));
    attr.form = AMARK_LINK_INLINE;
    attr.definition = AMARK_NODE_NONE;
    NAOS_REQUIRE_INT_EQ(amark_draft_set_link(&g_draft, empty, &attr),
                        AMARK_ERROR_NONE);

    /* " " then <http://y> */
    add(AMARK_KIND_TEXT, 34, 35);
    autolink = add(AMARK_KIND_AUTOLINK, 35, 45);
    amark_draft_set_autolink(&g_draft, autolink, AMARK_AUTOLINK_URI);

    NAOS_REQUIRE_INT_EQ(emit(para), AMARK_ERROR_NONE);
    NAOS_CHECK_STR_EQ(dump(),
                      "(doc (def) (p (link inline dest=\"/u\" title=\"t\""
                      " (text \"a\")) (text \" \") (image full def=\"x\""
                      " (text \"b\")) (text \" \") (link inline (text \"c\"))"
                      " (text \" \") (autolink uri \"<http://y>\")))");
    NAOS_CHECK_UINT_EQ(amark_link_definition(
                           g_doc, amark_draft_get(&g_draft, image)->node),
                       def);
    NAOS_CHECK_FALSE(amark_link_destination(
        g_doc, amark_draft_get(&g_draft, empty)->node, &dest));
    NAOS_CHECK_TRUE(tree_ok());
}

NAOS_TEST(emit_writes_dialect_kinds_as_inline_nodes)
{
    amark_node_id_t para = make_paragraph("a~b~c", 0);
    amark_draft_id_t a;
    amark_draft_id_t w;
    amark_draft_id_t leaf;
    amark_node_id_t node;

    a = add(AMARK_KIND_TEXT, 0, 1);
    add(AMARK_KIND_TEXT, 2, 3);
    w = wrap(a, AMARK_DRAFT_NONE, AMARK_KIND_DIALECT_MIN + 7u, 1, 4);
    leaf = add(AMARK_KIND_DIALECT_MIN, 4, 5);
    NAOS_REQUIRE_INT_EQ(emit(para), AMARK_ERROR_NONE);
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (text \"a\") (? (text \"b\")) (?)))");

    node = amark_draft_get(&g_draft, w)->node;
    NAOS_CHECK_UINT_EQ(amark_node_kind(g_doc, node),
                       AMARK_KIND_DIALECT_MIN + 7u);
    NAOS_CHECK_TRUE(amark_node_is_inline(g_doc, node));
    NAOS_CHECK_FALSE(amark_node_is_block(g_doc, node));
    node = amark_draft_get(&g_draft, leaf)->node;
    NAOS_CHECK_UINT_EQ(amark_node_kind(g_doc, node), AMARK_KIND_DIALECT_MIN);
    NAOS_CHECK_TRUE(amark_node_is_inline(g_doc, node));
    NAOS_CHECK_TRUE(tree_ok());
}

NAOS_TEST(emit_writes_a_content_that_starts_later)
{
    /* The paragraph (as a heading would) starts after a prefix. */
    amark_node_id_t para = make_paragraph("> ab", 2);

    add(AMARK_KIND_TEXT, 2, 4);
    NAOS_REQUIRE_INT_EQ(emit(para), AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(node_range(3).start, 2);
    NAOS_CHECK_UINT_EQ(node_range(3).end, 4);
    NAOS_CHECK_UINT_EQ(amark_node_parent(g_doc, 3), para);
    NAOS_CHECK_TRUE(tree_ok());
}

/*
 * Builds "*a*b" under a new paragraph: (em (text "a")) (text "b"), three
 * nodes, the deepest (the text in the emphasis) at depth 3.
 */
static amark_node_id_t make_emphasis(void)
{
    amark_node_id_t para = make_paragraph("*a*b", 0);
    amark_draft_id_t open;
    amark_draft_id_t close;

    open = add(AMARK_KIND_TEXT, 0, 1);
    add(AMARK_KIND_TEXT, 1, 2);
    close = add(AMARK_KIND_TEXT, 2, 3);
    add(AMARK_KIND_TEXT, 3, 4);
    wrap(open, close, AMARK_KIND_EMPHASIS, 0, 3);
    amark_draft_remove(&g_draft, open);
    amark_draft_remove(&g_draft, close);
    return para;
}

NAOS_TEST(emit_allows_the_depth_limit_exactly)
{
    amark_node_id_t para = make_emphasis();

    NAOS_REQUIRE_INT_EQ(amark_draft_emit(g_doc, &g_draft, para, 1, 3, 0),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (em (text \"a\")) (text \"b\")))");
    NAOS_CHECK_TRUE(tree_ok());
}

NAOS_TEST(emit_fails_one_past_the_depth_limit)
{
    amark_node_id_t para = make_emphasis();

    NAOS_CHECK_INT_EQ(amark_draft_emit(g_doc, &g_draft, para, 1, 2, 0),
                      AMARK_ERROR_LIMIT_EXCEEDED);
    /* The emphasis (depth 2) was written before the text failed. */
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 3);
}

NAOS_TEST(emit_counts_depth_from_parent_depth)
{
    amark_node_id_t para = make_emphasis();

    /* As if the paragraph were in a block quote: depth 2, the text 4. */
    NAOS_CHECK_INT_EQ(amark_draft_emit(g_doc, &g_draft, para, 2, 3, 0),
                      AMARK_ERROR_LIMIT_EXCEEDED);
}

NAOS_TEST(emit_ignores_parent_depth_without_a_depth_limit)
{
    amark_node_id_t para = make_emphasis();

    NAOS_REQUIRE_INT_EQ(amark_draft_emit(g_doc, &g_draft, para, SIZE_MAX, 0,
                                         0),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 5);
}

NAOS_TEST(emit_allows_the_node_limit_exactly)
{
    amark_node_id_t para = make_emphasis();

    /* The root, the paragraph and three inline nodes. */
    NAOS_REQUIRE_INT_EQ(amark_draft_emit(g_doc, &g_draft, para, 1, 0, 5),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 5);
    NAOS_CHECK_TRUE(tree_ok());
}

NAOS_TEST(emit_fails_one_past_the_node_limit)
{
    amark_node_id_t para = make_emphasis();

    NAOS_CHECK_INT_EQ(amark_draft_emit(g_doc, &g_draft, para, 1, 0, 4),
                      AMARK_ERROR_LIMIT_EXCEEDED);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 4);
}

NAOS_TEST(emit_counts_removed_nodes_against_the_node_limit)
{
    amark_node_id_t para = make_emphasis();
    amark_node_id_t gone = AMARK_NODE_NONE;

    /* A block made and removed before: 6 nodes created in all. */
    NAOS_REQUIRE_INT_EQ(amark_tree_append(g_doc, amark_document_root(g_doc),
                                          AMARK_KIND_THEMATIC_BREAK,
                                          AMARK_NODE_CLASS_AUTO, 4, 4, &gone),
                        AMARK_ERROR_NONE);
    amark_tree_remove(g_doc, gone);
    NAOS_CHECK_INT_EQ(amark_draft_emit(g_doc, &g_draft, para, 1, 0, 5),
                      AMARK_ERROR_LIMIT_EXCEEDED);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 5);
}

NAOS_TEST(emit_counts_merged_texts_as_one_node)
{
    amark_node_id_t para = make_paragraph("abc", 0);

    add(AMARK_KIND_TEXT, 0, 1);
    add(AMARK_KIND_TEXT, 1, 2);
    add(AMARK_KIND_TEXT, 2, 3);
    NAOS_REQUIRE_INT_EQ(amark_draft_emit(g_doc, &g_draft, para, 1, 2, 3),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (text \"abc\")))");
}

NAOS_TEST(emit_reports_an_allocator_failure)
{
    static char content[4001];
    amark_node_id_t para;
    size_t i;

    /* 2,000 texts that do not merge: the node array must grow. */
    memset(content, 'a', sizeof(content) - 1);
    content[sizeof(content) - 1] = '\0';
    para = make_paragraph(content, 0);
    for (i = 0; i < 2000; i++) {
        add(AMARK_KIND_TEXT, 2 * i, 2 * i + 1);
    }
    g_state.fail_at = g_state.malloc_calls + 1;
    NAOS_CHECK_INT_EQ(emit(para), AMARK_ERROR_NOMEM);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, g_state.fail_at);
    g_state.fail_at = 0;
}

NAOS_TEST(emit_after_reset_writes_the_next_content)
{
    amark_node_id_t para = make_paragraph("ab\n\ncd", 0);
    amark_node_id_t second = AMARK_NODE_NONE;

    /* Two paragraphs, one draft: the first content, then the second. */
    amark_tree_set_range(g_doc, para, 0, 2);
    NAOS_REQUIRE_INT_EQ(amark_tree_append(g_doc, amark_document_root(g_doc),
                                          AMARK_KIND_PARAGRAPH,
                                          AMARK_NODE_CLASS_AUTO, 4, 6,
                                          &second),
                        AMARK_ERROR_NONE);
    add(AMARK_KIND_TEXT, 0, 2);
    NAOS_REQUIRE_INT_EQ(emit(para), AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_draft_reset(&g_draft), AMARK_ERROR_NONE);
    add(AMARK_KIND_TEXT, 4, 6);
    NAOS_REQUIRE_INT_EQ(emit(second), AMARK_ERROR_NONE);
    NAOS_CHECK_STR_EQ(dump(), "(doc (p (text \"ab\")) (p (text \"cd\")))");
    NAOS_CHECK_TRUE(tree_ok());
}

NAOS_SUITE(draft,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(init_allocates_nothing),
    NAOS_ADD_TEST(reset_leaves_the_root_alone),
    NAOS_ADD_TEST(first_reset_failure_reports_nomem),
    NAOS_ADD_TEST(append_adds_last_children_of_the_root_in_order),
    NAOS_ADD_TEST(set_range_changes_only_the_range),
    NAOS_ADD_TEST(wrap_one_sibling),
    NAOS_ADD_TEST(wrap_several_siblings),
    NAOS_ADD_TEST(wrap_nothing_between),
    NAOS_ADD_TEST(wrap_up_to_the_end),
    NAOS_ADD_TEST(wrap_nothing_at_the_end),
    NAOS_ADD_TEST(wrap_nests),
    NAOS_ADD_TEST(remove_first_middle_and_last),
    NAOS_ADD_TEST(remove_a_child_of_a_wrapper),
    NAOS_ADD_TEST(remove_a_wrapper_with_its_children),
    NAOS_ADD_TEST(wrap_after_remove),
    NAOS_ADD_TEST(set_link_keeps_a_copy),
    NAOS_ADD_TEST(set_autolink_stores_the_type_plus_one),
    NAOS_ADD_TEST(dialect_kinds_are_kept_as_they_are),
    NAOS_ADD_TEST(reset_reuses_the_places),
    NAOS_ADD_TEST(append_failure_changes_nothing),
    NAOS_ADD_TEST(wrap_failure_changes_nothing),
    NAOS_ADD_TEST(set_link_failure_changes_nothing),
    NAOS_ADD_TEST(too_many_items_is_limit_exceeded),
    NAOS_ADD_TEST(too_many_links_is_limit_exceeded),
    NAOS_ADD_TEST(emit_of_an_empty_draft_adds_nothing),
    NAOS_ADD_TEST(emit_merges_adjacent_texts),
    NAOS_ADD_TEST(emit_keeps_texts_apart_when_their_ranges_do_not_meet),
    NAOS_ADD_TEST(emit_does_not_merge_texts_across_a_line_break),
    NAOS_ADD_TEST(emit_merges_texts_only_among_siblings),
    NAOS_ADD_TEST(emit_gives_node_ids_in_document_order),
    NAOS_ADD_TEST(emit_sets_the_attributes_of_links_images_and_autolinks),
    NAOS_ADD_TEST(emit_writes_dialect_kinds_as_inline_nodes),
    NAOS_ADD_TEST(emit_writes_a_content_that_starts_later),
    NAOS_ADD_TEST(emit_allows_the_depth_limit_exactly),
    NAOS_ADD_TEST(emit_fails_one_past_the_depth_limit),
    NAOS_ADD_TEST(emit_counts_depth_from_parent_depth),
    NAOS_ADD_TEST(emit_ignores_parent_depth_without_a_depth_limit),
    NAOS_ADD_TEST(emit_allows_the_node_limit_exactly),
    NAOS_ADD_TEST(emit_fails_one_past_the_node_limit),
    NAOS_ADD_TEST(emit_counts_removed_nodes_against_the_node_limit),
    NAOS_ADD_TEST(emit_counts_merged_texts_as_one_node),
    NAOS_ADD_TEST(emit_reports_an_allocator_failure),
    NAOS_ADD_TEST(emit_after_reset_writes_the_next_content)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(draft)
)
