/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * The public readers of the tree (include/amark/tree.h): walking, the kind,
 * the class, the range and the lines of a node. They only read the document;
 * nothing here writes, allocates or loops over the tree.
 */

#include <amark/tree.h>

#include <stdbool.h>
#include <stddef.h>

#include "array.h"
#include "document.h"

/*
 * Returns the node with ID id, or NULL if the call cannot be answered: doc
 * is NULL, id is AMARK_NODE_NONE or larger than the number of nodes created,
 * or the node was removed (its kind is AMARK_KIND_NONE).
 */
static const amark_node_t *find(const amark_document_t *doc,
                                amark_node_id_t id)
{
    const amark_node_t *n;

    if (doc == NULL || id == AMARK_NODE_NONE ||
        id >= amark_array_len(&doc->nodes)) {
        return NULL;
    }
    n = (const amark_node_t *)amark_array_at(&doc->nodes, id);
    if (n->kind == AMARK_KIND_NONE) {
        return NULL;
    }
    return n;
}

bool amark_kind_is_core(amark_kind_t kind)
{
    return (kind >= AMARK_KIND_DOCUMENT &&
            kind <= AMARK_KIND_LINK_REFERENCE_DEFINITION) ||
           (kind >= AMARK_KIND_TEXT &&
            kind <= AMARK_KIND_CHARACTER_REFERENCE);
}

bool amark_kind_is_dialect(amark_kind_t kind)
{
    return kind >= AMARK_KIND_DIALECT_MIN;
}

amark_node_id_t amark_document_root(const amark_document_t *doc)
{
    /* Element 1 is the root; it is never removed. */
    return find(doc, 1) != NULL ? (amark_node_id_t)1 : AMARK_NODE_NONE;
}

amark_node_id_t amark_node_parent(const amark_document_t *doc,
                                  amark_node_id_t node)
{
    const amark_node_t *n = find(doc, node);

    return n != NULL ? n->parent : AMARK_NODE_NONE;
}

amark_node_id_t amark_node_first_child(const amark_document_t *doc,
                                       amark_node_id_t node)
{
    const amark_node_t *n = find(doc, node);

    return n != NULL ? n->first_child : AMARK_NODE_NONE;
}

amark_node_id_t amark_node_next_sibling(const amark_document_t *doc,
                                        amark_node_id_t node)
{
    const amark_node_t *n = find(doc, node);

    return n != NULL ? n->next_sibling : AMARK_NODE_NONE;
}

amark_kind_t amark_node_kind(const amark_document_t *doc,
                             amark_node_id_t node)
{
    const amark_node_t *n = find(doc, node);

    return n != NULL ? n->kind : AMARK_KIND_NONE;
}

bool amark_node_is_block(const amark_document_t *doc, amark_node_id_t node)
{
    const amark_node_t *n = find(doc, node);

    return n != NULL && n->is_inline == 0;
}

bool amark_node_is_inline(const amark_document_t *doc, amark_node_id_t node)
{
    const amark_node_t *n = find(doc, node);

    return n != NULL && n->is_inline != 0;
}

bool amark_node_range(const amark_document_t *doc, amark_node_id_t node,
                      amark_range_t *out)
{
    const amark_node_t *n = find(doc, node);

    if (n == NULL || out == NULL) {
        return false;
    }
    out->start = n->start;
    out->end = n->end;
    return true;
}

size_t amark_node_start_line(const amark_document_t *doc,
                             amark_node_id_t node)
{
    const amark_node_t *n = find(doc, node);

    return n != NULL ? n->start_line : 0;
}

size_t amark_node_end_line(const amark_document_t *doc, amark_node_id_t node)
{
    const amark_node_t *n = find(doc, node);

    return n != NULL ? n->end_line : 0;
}
