/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * The public text functions (include/amark/text.h). They read the document
 * and its content; nothing here writes to the document.
 */

#include <amark/text.h>

#include <stdbool.h>
#include <stddef.h>

#include "array.h"
#include "document.h"

/*
 * Returns the node with ID id, or NULL if the call cannot be answered: doc
 * is NULL, id is AMARK_NODE_NONE or larger than the number of nodes created,
 * or the node was removed (its kind is AMARK_KIND_NONE). The same condition
 * as the tree readers (src/node.c).
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

bool amark_node_raw(const amark_document_t *doc, amark_node_id_t node,
                    const char **bytes, size_t *length)
{
    const amark_node_t *n;

    if (bytes == NULL || length == NULL) {
        return false;
    }
    n = find(doc, node);
    if (n == NULL) {
        return false;
    }
    /* content is NULL only when its length is 0; then the range is [0, 0)
       and the bytes are NULL. */
    *bytes = doc->content != NULL ? (const char *)doc->content + n->start
                                  : NULL;
    *length = n->end - n->start;
    return true;
}
