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


amark_error_t amark_tree_wrap(amark_document_t *doc, amark_node_id_t first,
                              amark_node_id_t last, amark_kind_t kind,
                              amark_node_class_t cls, size_t start, size_t end,
                              amark_node_id_t *out)
{
    amark_node_id_t id = AMARK_NODE_NONE;
    amark_node_id_t parent;
    amark_node_id_t prev;
    amark_node_id_t next;
    amark_node_id_t child;
    amark_node_t *node;
    amark_node_t *f;
    amark_node_t *l;
    amark_error_t err;

    *out = AMARK_NODE_NONE;
    err = create_node(doc, kind, cls, start, end, &id);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    /* The push may have moved the array: take the pointers after it. */
    node = node_at(doc, id);
    f = node_at(doc, first);
    l = node_at(doc, last);
    parent = f->parent;
    prev = f->prev_sibling;
    next = l->next_sibling;

    /* The new node takes the place of first..last among the siblings. */
    node->parent = parent;
    node->prev_sibling = prev;
    node->next_sibling = next;
    node->first_child = first;
    node->last_child = last;
    if (prev != AMARK_NODE_NONE) {
        node_at(doc, prev)->next_sibling = id;
    } else {
        node_at(doc, parent)->first_child = id;
    }
    if (next != AMARK_NODE_NONE) {
        node_at(doc, next)->prev_sibling = id;
    } else {
        node_at(doc, parent)->last_child = id;
    }
    f->prev_sibling = AMARK_NODE_NONE;
    l->next_sibling = AMARK_NODE_NONE;

    /* The moved siblings get the new parent; last now ends the chain. */
    for (child = first; child != AMARK_NODE_NONE;
         child = node_at(doc, child)->next_sibling) {
        node_at(doc, child)->parent = id;
    }
    *out = id;
    return AMARK_ERROR_NONE;
}

void amark_tree_set_range(amark_document_t *doc, amark_node_id_t node,
                          size_t start, size_t end)
{
    amark_node_t *n = node_at(doc, node);

    n->start = start;
    n->end = end;
    amark_document_range_lines(doc, start, end, &n->start_line, &n->end_line);
}

void amark_tree_set_kind(amark_document_t *doc, amark_node_id_t node,
                         amark_kind_t kind, amark_node_class_t cls)
{
    amark_node_t *n = node_at(doc, node);

    n->kind = kind;
    n->is_inline = is_inline_of(kind, cls);
    n->attr = 0;
}

void amark_tree_remove(amark_document_t *doc, amark_node_id_t node)
{
    amark_node_t *n = node_at(doc, node);
    amark_node_t *p = node_at(doc, n->parent);

    if (n->prev_sibling != AMARK_NODE_NONE) {
        node_at(doc, n->prev_sibling)->next_sibling = n->next_sibling;
    } else {
        p->first_child = n->next_sibling;
    }
    if (n->next_sibling != AMARK_NODE_NONE) {
        node_at(doc, n->next_sibling)->prev_sibling = n->prev_sibling;
    } else {
        p->last_child = n->prev_sibling;
    }
    /* The element stays in the array, unused; the subtree is not walked. */
    n->kind = AMARK_KIND_NONE;
    n->parent = AMARK_NODE_NONE;
    n->prev_sibling = AMARK_NODE_NONE;
    n->next_sibling = AMARK_NODE_NONE;
}
