/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#include "tree.h"

#include <stdint.h>
#include <string.h>

#include "array.h"

static amark_node_t *node_at(amark_document_t *doc, amark_node_id_t id)
{
    return (amark_node_t *)amark_array_at(&doc->nodes, id);
}

/* 0: block, 1: inline. A CommonMark kind is decided by its ID. */
static uint32_t is_inline_of(amark_kind_t kind, amark_node_class_t cls)
{
    if (cls == AMARK_NODE_CLASS_AUTO) {
        return kind >= AMARK_KIND_TEXT ? 1u : 0u;
    }
    return cls == AMARK_NODE_CLASS_INLINE ? 1u : 0u;
}

/*
 * Adds an unlinked node at the end of the node array and stores its ID in
 * *id. The limit is checked before the node storage is touched; on failure
 * the array is unchanged.
 */
static amark_error_t create_node(amark_document_t *doc, amark_kind_t kind,
                                 amark_node_class_t cls, size_t start,
                                 size_t end, amark_node_id_t *id)
{
    size_t len = amark_array_len(&doc->nodes);
    amark_node_t node;
    amark_error_t err;

    /* Element 0 is not a node, so len - 1 nodes exist. */
    if (len - 1 >= (size_t)UINT32_MAX - 1) {
        return AMARK_ERROR_LIMIT_EXCEEDED;
    }
    memset(&node, 0, sizeof(node));
    node.kind = kind;
    node.is_inline = is_inline_of(kind, cls);
    node.parent = AMARK_NODE_NONE;
    node.first_child = AMARK_NODE_NONE;
    node.last_child = AMARK_NODE_NONE;
    node.prev_sibling = AMARK_NODE_NONE;
    node.next_sibling = AMARK_NODE_NONE;
    node.attr = 0;
    node.start = start;
    node.end = end;
    amark_document_range_lines(doc, start, end, &node.start_line,
                               &node.end_line);
    err = amark_array_push(&doc->nodes, &node);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    *id = (amark_node_id_t)len;
    return AMARK_ERROR_NONE;
}

amark_error_t amark_tree_append(amark_document_t *doc, amark_node_id_t parent,
                                amark_kind_t kind, amark_node_class_t cls,
                                size_t start, size_t end,
                                amark_node_id_t *out)
{
    amark_node_id_t id = AMARK_NODE_NONE;
    amark_node_t *node;
    amark_node_t *p;
    amark_error_t err;

    *out = AMARK_NODE_NONE;
    err = create_node(doc, kind, cls, start, end, &id);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    /* The push may have moved the array: take the pointers after it. */
    node = node_at(doc, id);
    p = node_at(doc, parent);
    node->parent = parent;
    node->prev_sibling = p->last_child;
    if (p->last_child != AMARK_NODE_NONE) {
        node_at(doc, p->last_child)->next_sibling = id;
    } else {
        p->first_child = id;
    }
    p->last_child = id;
    *out = id;
    return AMARK_ERROR_NONE;
}

amark_error_t amark_tree_insert_before(amark_document_t *doc,
                                       amark_node_id_t sibling,
                                       amark_kind_t kind,
                                       amark_node_class_t cls, size_t start,
                                       size_t end, amark_node_id_t *out)
{
    amark_node_id_t id = AMARK_NODE_NONE;
    amark_node_id_t parent;
    amark_node_id_t prev;
    amark_node_t *node;
    amark_node_t *s;
    amark_error_t err;

    *out = AMARK_NODE_NONE;
    err = create_node(doc, kind, cls, start, end, &id);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    /* The push may have moved the array: take the pointers after it. */
    node = node_at(doc, id);
    s = node_at(doc, sibling);
    parent = s->parent;
    prev = s->prev_sibling;
    node->parent = parent;
    node->prev_sibling = prev;
    node->next_sibling = sibling;
    s->prev_sibling = id;
    if (prev != AMARK_NODE_NONE) {
        node_at(doc, prev)->next_sibling = id;
    } else {
        node_at(doc, parent)->first_child = id;
    }
    *out = id;
    return AMARK_ERROR_NONE;
}
