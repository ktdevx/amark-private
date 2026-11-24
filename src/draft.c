/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#include "draft.h"

#include <string.h>

#include "document.h"
#include "tree_internal.h"

/*
 * The array holds at most UINT32_MAX elements: the unused element 0 and
 * UINT32_MAX - 1 items, so every ID fits in amark_draft_id_t.
 */
#define MAX_ITEMS_LEN ((size_t)UINT32_MAX)

/* The links are named by attr, their index plus 1, a uint32_t. */
#define MAX_LINKS_LEN ((size_t)UINT32_MAX)

static amark_draft_item_t *item_at(const amark_draft_t *draft,
                                   amark_draft_id_t id)
{
    return (amark_draft_item_t *)amark_array_at(&draft->items, id);
}

/*
 * Pushes a new item of kind with the range [start, end) and no links, and
 * stores its ID in *out. On failure nothing changes.
 */
static amark_error_t push_item(amark_draft_t *draft, amark_kind_t kind,
                               size_t start, size_t end,
                               amark_draft_id_t *out)
{
    amark_draft_item_t item;
    size_t len = amark_array_len(&draft->items);
    amark_error_t err;

    if (len >= MAX_ITEMS_LEN) {
        return AMARK_ERROR_LIMIT_EXCEEDED;
    }
    memset(&item, 0, sizeof(item));
    item.kind = kind;
    item.parent = AMARK_DRAFT_NONE;
    item.first_child = AMARK_DRAFT_NONE;
    item.last_child = AMARK_DRAFT_NONE;
    item.prev = AMARK_DRAFT_NONE;
    item.next = AMARK_DRAFT_NONE;
    item.start = start;
    item.end = end;
    item.attr = 0;
    item.node = AMARK_NODE_NONE;
    err = amark_array_push(&draft->items, &item);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    *out = (amark_draft_id_t)len;
    return AMARK_ERROR_NONE;
}

void amark_draft_init(amark_draft_t *draft, amark_arena_t *scratch)
{
    amark_array_init(&draft->items, scratch, sizeof(amark_draft_item_t));
    amark_array_init(&draft->links, scratch, sizeof(amark_draft_link_t));
}

amark_error_t amark_draft_reset(amark_draft_t *draft)
{
    amark_draft_id_t id;
    amark_error_t err;

    amark_array_clear(&draft->items);
    amark_array_clear(&draft->links);
    /* Element 0 (AMARK_DRAFT_NONE), never used, then the root. */
    err = push_item(draft, AMARK_KIND_NONE, 0, 0, &id);
    if (err == AMARK_ERROR_NONE) {
        err = push_item(draft, AMARK_KIND_NONE, 0, 0, &id);
    }
    if (err != AMARK_ERROR_NONE) {
        amark_array_clear(&draft->items);
    }
    return err;
}

amark_error_t amark_draft_append(amark_draft_t *draft,
                                 amark_kind_t kind, size_t start,
                                 size_t end,
                                 amark_draft_id_t *out)
{
    amark_draft_id_t id;
    amark_draft_item_t *root;
    amark_draft_item_t *item;
    amark_error_t err;

    err = push_item(draft, kind, start, end, &id);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    root = item_at(draft, AMARK_DRAFT_ROOT);
    item = item_at(draft, id);
    item->parent = AMARK_DRAFT_ROOT;
    item->prev = root->last_child;
    if (root->last_child == AMARK_DRAFT_NONE) {
        root->first_child = id;
    } else {
        item_at(draft, root->last_child)->next = id;
    }
    root->last_child = id;
    *out = id;
    return AMARK_ERROR_NONE;
}

amark_error_t amark_draft_wrap(amark_draft_t *draft,
                               amark_draft_id_t after,
                               amark_draft_id_t before,
                               amark_kind_t kind, size_t start,
                               size_t end,
                               amark_draft_id_t *out)
{
    amark_draft_id_t id;
    amark_draft_id_t first;
    amark_draft_id_t last;
    amark_draft_id_t child;
    amark_draft_item_t *wrapper;
    amark_draft_item_t *root;
    amark_error_t err;

    /* The only step that can fail comes first: nothing has moved yet. */
    err = push_item(draft, kind, start, end, &id);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    root = item_at(draft, AMARK_DRAFT_ROOT);
    wrapper = item_at(draft, id);
    first = item_at(draft, after)->next;
    last = before == AMARK_DRAFT_NONE ? root->last_child
                                      : item_at(draft, before)->prev;

    /* Move the siblings between (if any) under the wrapper. */
    if (first != before) {
        item_at(draft, first)->prev = AMARK_DRAFT_NONE;
        item_at(draft, last)->next = AMARK_DRAFT_NONE;
        wrapper->first_child = first;
        wrapper->last_child = last;
        for (child = first; child != AMARK_DRAFT_NONE;
             child = item_at(draft, child)->next) {
            item_at(draft, child)->parent = id;
        }
    }

    /* Put the wrapper between after and before. */
    wrapper->parent = AMARK_DRAFT_ROOT;
    wrapper->prev = after;
    wrapper->next = before;
    item_at(draft, after)->next = id;
    if (before == AMARK_DRAFT_NONE) {
        root->last_child = id;
    } else {
        item_at(draft, before)->prev = id;
    }
    *out = id;
    return AMARK_ERROR_NONE;
}

void amark_draft_remove(amark_draft_t *draft,
                        amark_draft_id_t id)
{
    amark_draft_item_t *item = item_at(draft, id);
    amark_draft_item_t *parent = item_at(draft, item->parent);

    if (item->prev == AMARK_DRAFT_NONE) {
        parent->first_child = item->next;
    } else {
        item_at(draft, item->prev)->next = item->next;
    }
    if (item->next == AMARK_DRAFT_NONE) {
        parent->last_child = item->prev;
    } else {
        item_at(draft, item->next)->prev = item->prev;
    }
    item->parent = AMARK_DRAFT_NONE;
    item->prev = AMARK_DRAFT_NONE;
    item->next = AMARK_DRAFT_NONE;
}

void amark_draft_set_range(amark_draft_t *draft,
                           amark_draft_id_t id,
                           size_t start, size_t end)
{
    amark_draft_item_t *item = item_at(draft, id);

    item->start = start;
    item->end = end;
}

amark_error_t amark_draft_set_link(amark_draft_t *draft,
                                   amark_draft_id_t id,
                                   const amark_draft_link_t *attr)
{
    size_t len = amark_array_len(&draft->links);
    amark_error_t err;

    if (len >= MAX_LINKS_LEN) {
        return AMARK_ERROR_LIMIT_EXCEEDED;
    }
    err = amark_array_push(&draft->links, attr);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    item_at(draft, id)->attr = (uint32_t)(len + 1);
    return AMARK_ERROR_NONE;
}

void amark_draft_set_autolink(amark_draft_t *draft,
                              amark_draft_id_t id,
                              amark_autolink_type_t type)
{
    item_at(draft, id)->attr = (uint32_t)type + 1;
}

const amark_draft_item_t *amark_draft_get(const amark_draft_t *draft,
                                          amark_draft_id_t id)
{
    return item_at(draft, id);
}

/*
 * Adds the node of item (whose range ends at end, after the texts merged
 * into it) under parent at depth, with its attribute, after checking the
 * limits. Stores the node in *out.
 */
static amark_error_t emit_item(amark_document_t *doc,
                               const amark_draft_t *draft,
                               const amark_draft_item_t *item, size_t end,
                               amark_node_id_t parent, size_t depth,
                               size_t max_depth, size_t max_nodes,
                               amark_node_id_t *out)
{
    amark_node_class_t cls = item->kind < AMARK_KIND_DIALECT_MIN
                                 ? AMARK_NODE_CLASS_AUTO
                                 : AMARK_NODE_CLASS_INLINE;
    amark_error_t err;

    if (max_depth != 0 && depth > max_depth) {
        return AMARK_ERROR_LIMIT_EXCEEDED;
    }
    if (max_nodes != 0 && amark_document_node_count(doc) >= max_nodes) {
        return AMARK_ERROR_LIMIT_EXCEEDED;
    }
    err = amark_tree_append(doc, parent, item->kind, cls, item->start, end,
                            out);
    if (err != AMARK_ERROR_NONE || item->attr == 0) {
        return err;
    }
    if (item->kind == AMARK_KIND_LINK || item->kind == AMARK_KIND_IMAGE) {
        const amark_draft_link_t *link = (const amark_draft_link_t *)
            amark_array_at(&draft->links, (size_t)item->attr - 1);

        return amark_tree_set_link(
            doc, *out, link->form,
            link->has_destination ? &link->destination : NULL,
            link->has_title ? &link->title : NULL, link->definition);
    }
    if (item->kind == AMARK_KIND_AUTOLINK) {
        return amark_tree_set_autolink(
            doc, *out, (amark_autolink_type_t)(item->attr - 1));
    }
    return AMARK_ERROR_NONE;
}

amark_error_t amark_draft_emit(amark_document_t *doc,
                               amark_draft_t *draft,
                               amark_node_id_t parent, size_t parent_depth,
                               size_t max_depth, size_t max_nodes)
{
    amark_draft_id_t id = item_at(draft, AMARK_DRAFT_ROOT)->first_child;
    size_t depth = parent_depth + 1;
    amark_error_t err;

    while (id != AMARK_DRAFT_NONE) {
        amark_draft_item_t *item = item_at(draft, id);
        amark_draft_id_t last = id;
        size_t end = item->end;
        amark_node_id_t under = item->parent == AMARK_DRAFT_ROOT
                                    ? parent
                                    : item_at(draft, item->parent)->node;
        amark_node_id_t node = AMARK_NODE_NONE;

        /* Merge the following texts whose ranges meet this one. */
        if (item->kind == AMARK_KIND_TEXT) {
            amark_draft_id_t next = item->next;

            while (next != AMARK_DRAFT_NONE &&
                   item_at(draft, next)->kind == AMARK_KIND_TEXT &&
                   item_at(draft, next)->start == end) {
                last = next;
                end = item_at(draft, next)->end;
                next = item_at(draft, next)->next;
            }
        }
        err = emit_item(doc, draft, item, end, under, depth, max_depth,
                        max_nodes, &node);
        if (err != AMARK_ERROR_NONE) {
            return err;
        }
        /* Every merged text names the one node. */
        for (;;) {
            item_at(draft, id)->node = node;
            if (id == last) {
                break;
            }
            id = item_at(draft, id)->next;
        }

        /* Down to the first child, else on to the next item in document
         * order, going up while there is no next sibling. */
        if (item->first_child != AMARK_DRAFT_NONE) {
            id = item->first_child;
            depth++;
            continue;
        }
        while (id != AMARK_DRAFT_ROOT &&
               item_at(draft, id)->next == AMARK_DRAFT_NONE) {
            id = item_at(draft, id)->parent;
            depth--;
        }
        id = id == AMARK_DRAFT_ROOT ? AMARK_DRAFT_NONE
                                    : item_at(draft, id)->next;
    }
    return AMARK_ERROR_NONE;
}
