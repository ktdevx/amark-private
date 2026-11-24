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
 */

#include "arena.h"
#include "array.h"
#include "draft.h"
#include "test_alloc.h"

#include <amark/amark.h>

#include <naos/naos.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * Fixture. NAOS_REQUIRE_* leaves a test with longjmp, so the scratch arena
 * and the draft are kept in static variables, and teardown releases the
 * arena and checks that no block is left.
 */
#define SHAPE_SIZE 4096

static amark_test_alloc_t g_state;
static amark_allocator_t g_allocator;
static amark_arena_t g_scratch;
static amark_draft_t g_draft;
static char g_shape[SHAPE_SIZE];

NAOS_SETUP(counting)
{
    amark_test_alloc_init(&g_state);
    g_allocator = amark_test_allocator(&g_state);
    amark_arena_init(&g_scratch, &g_allocator);
    amark_draft_init(&g_draft, &g_scratch);
}

NAOS_TEARDOWN(counting)
{
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
    NAOS_ADD_TEST(too_many_links_is_limit_exceeded)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(draft)
)
