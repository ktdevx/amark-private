/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#include "tree.h"

#include <stdint.h>
#include <string.h>

#include "arena.h"
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

static amark_opt_range_t opt_range_of(const amark_range_t *range)
{
    amark_opt_range_t r;

    memset(&r, 0, sizeof(r));
    if (range != NULL) {
        r.start = range->start;
        r.end = range->end;
        r.present = true;
    }
    return r;
}

/*
 * Stores attr as the attribute element of node: overwrites the element node
 * already has, or adds one. Only adding can fail, and then node is
 * unchanged.
 */
static amark_error_t store_attr(amark_document_t *doc, amark_node_id_t node,
                                const amark_node_attr_t *attr)
{
    amark_node_t *n = node_at(doc, node);
    size_t len;
    amark_error_t err;

    if (n->attr != 0) {
        *(amark_node_attr_t *)amark_array_at(&doc->attrs, n->attr) = *attr;
        return AMARK_ERROR_NONE;
    }
    /* The new element's index must fit in the 32-bit attr field. */
    len = amark_array_len(&doc->attrs);
    if (len > (size_t)UINT32_MAX) {
        return AMARK_ERROR_LIMIT_EXCEEDED;
    }
    err = amark_array_push(&doc->attrs, attr);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    /* The push may move the attribute array, never the node array. */
    n->attr = (uint32_t)len;
    return AMARK_ERROR_NONE;
}

amark_error_t amark_tree_set_heading(amark_document_t *doc,
                                     amark_node_id_t node, unsigned level,
                                     amark_heading_style_t style)
{
    amark_node_attr_t attr;

    memset(&attr, 0, sizeof(attr));
    attr.heading.level = level;
    attr.heading.style = style;
    return store_attr(doc, node, &attr);
}

amark_error_t amark_tree_set_list(amark_document_t *doc, amark_node_id_t node,
                                  amark_list_type_t type, char marker,
                                  uint32_t start_number,
                                  amark_list_spacing_t spacing)
{
    amark_node_attr_t attr;

    memset(&attr, 0, sizeof(attr));
    attr.list.type = type;
    attr.list.spacing = spacing;
    attr.list.marker = marker;
    attr.list.start_number = start_number;
    return store_attr(doc, node, &attr);
}

amark_error_t amark_tree_set_code_block(amark_document_t *doc,
                                        amark_node_id_t node,
                                        amark_code_block_style_t style,
                                        char fence_char, size_t fence_length,
                                        const amark_range_t *info)
{
    amark_node_attr_t attr;

    memset(&attr, 0, sizeof(attr));
    attr.code_block.style = style;
    attr.code_block.fence_char = fence_char;
    attr.code_block.fence_length = fence_length;
    attr.code_block.info = opt_range_of(info);
    return store_attr(doc, node, &attr);
}

amark_error_t amark_tree_set_link(amark_document_t *doc, amark_node_id_t node,
                                  amark_link_form_t form,
                                  const amark_range_t *destination,
                                  const amark_range_t *title,
                                  amark_node_id_t definition)
{
    amark_node_attr_t attr;

    memset(&attr, 0, sizeof(attr));
    attr.link.form = form;
    attr.link.destination = opt_range_of(destination);
    attr.link.title = opt_range_of(title);
    attr.link.definition = definition;
    return store_attr(doc, node, &attr);
}

amark_error_t amark_tree_set_definition(amark_document_t *doc,
                                        amark_node_id_t node,
                                        const amark_range_t *label,
                                        const amark_range_t *destination,
                                        const amark_range_t *title)
{
    amark_node_attr_t attr;

    memset(&attr, 0, sizeof(attr));
    attr.definition.label = opt_range_of(label);
    attr.definition.destination = opt_range_of(destination);
    attr.definition.title = opt_range_of(title);
    return store_attr(doc, node, &attr);
}

amark_error_t amark_tree_set_autolink(amark_document_t *doc,
                                      amark_node_id_t node,
                                      amark_autolink_type_t type)
{
    amark_node_attr_t attr;

    memset(&attr, 0, sizeof(attr));
    attr.autolink.type = type;
    return store_attr(doc, node, &attr);
}

amark_error_t amark_tree_set_dialect_data(amark_document_t *doc,
                                          amark_node_id_t node,
                                          const void *data, size_t size)
{
    amark_node_attr_t attr;
    void *copy = NULL;
    amark_error_t err;

    if (size == 0) {
        node_at(doc, node)->attr = 0;
        return AMARK_ERROR_NONE;
    }
    /* The arena aligns every allocation to AMARK_ARENA_ALIGN. */
    err = amark_arena_alloc(&doc->arena, size, &copy);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    memcpy(copy, data, size);
    memset(&attr, 0, sizeof(attr));
    attr.dialect.data = copy;
    attr.dialect.size = size;
    return store_attr(doc, node, &attr);
}
