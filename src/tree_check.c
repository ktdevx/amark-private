/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * The tree check (src/tree_check.h). One walk in document order through the
 * first child, next sibling and parent indices; every node reached is
 * checked in the order of the violation enumeration before the walk moves
 * on, so the first violation found is the first in document order.
 *
 * Nothing here writes to the document, allocates or recurses. Every index is
 * compared with the number of nodes created before the node it names is
 * read, so a broken tree never makes the check read outside the node array.
 */

#include "tree_check.h"

#include <stdbool.h>
#include <stddef.h>

#include "array.h"
#include "document.h"

/* The node with ID id, which is 1 to the number of nodes created. */
static const amark_node_t *at(const amark_document_t *doc,
                              amark_node_id_t id)
{
    return (const amark_node_t *)amark_array_at(&doc->nodes, id);
}

static amark_tree_check_result_t result(amark_tree_violation_t violation,
                                        amark_node_id_t node)
{
    amark_tree_check_result_t r;

    r.violation = violation;
    r.node = violation == AMARK_TREE_VIOLATION_NONE ? AMARK_NODE_NONE : node;
    return r;
}

/*
 * The LINK conditions of node id (count nodes created). top: id is the
 * root of the walk, whose relations with its parent and previous sibling
 * were not checked by the node before it.
 */
static bool links_hold(const amark_document_t *doc, size_t count,
                       amark_node_id_t id, bool top)
{
    const amark_node_t *n = at(doc, id);

    if (n->parent > count || n->first_child > count ||
        n->last_child > count || n->prev_sibling > count ||
        n->next_sibling > count) {
        return false;
    }
    if (n->kind == AMARK_KIND_NONE) {
        return false;
    }
    if ((n->first_child == AMARK_NODE_NONE) !=
        (n->last_child == AMARK_NODE_NONE)) {
        return false;
    }
    if (n->first_child != AMARK_NODE_NONE) {
        const amark_node_t *c = at(doc, n->first_child);

        if (c->prev_sibling != AMARK_NODE_NONE || c->parent != id) {
            return false;
        }
        if (at(doc, n->last_child)->next_sibling != AMARK_NODE_NONE) {
            return false;
        }
    }
    if (n->next_sibling != AMARK_NODE_NONE) {
        const amark_node_t *s = at(doc, n->next_sibling);

        if (s->prev_sibling != id || s->parent != n->parent) {
            return false;
        }
    }
    if (n->parent == AMARK_NODE_NONE) {
        /* Only the root has no parent. */
        return id == 1;
    }
    {
        const amark_node_t *p = at(doc, n->parent);

        if (n->next_sibling == AMARK_NODE_NONE && p->last_child != id) {
            return false;
        }
        if (top) {
            if (p->kind == AMARK_KIND_NONE) {
                return false;
            }
            if (n->prev_sibling == AMARK_NODE_NONE) {
                if (p->first_child != id) {
                    return false;
                }
            } else {
                const amark_node_t *q = at(doc, n->prev_sibling);

                if (q->next_sibling != id || q->parent != n->parent) {
                    return false;
                }
            }
        }
    }
    return true;
}

/*
 * Checks node id from LINK to NESTING. The ranges and indices of the nodes
 * read after LINK (parent, previous and next sibling) are known to be in
 * range by then.
 */
static amark_tree_violation_t check_node(const amark_document_t *doc,
                                         size_t count, amark_node_id_t id,
                                         bool top)
{
    const amark_node_t *n;
    const amark_node_t *p = NULL;
    size_t start_line;
    size_t end_line;

    if (!links_hold(doc, count, id, top)) {
        return AMARK_TREE_VIOLATION_LINK;
    }
    n = at(doc, id);
    if (n->parent != AMARK_NODE_NONE) {
        p = at(doc, n->parent);
    }

    if (n->start > n->end || n->end > doc->content_length) {
        return AMARK_TREE_VIOLATION_BOUNDS;
    }

    amark_document_range_lines(doc, n->start, n->end, &start_line,
                               &end_line);
    if (n->start_line != start_line || n->end_line != end_line) {
        return AMARK_TREE_VIOLATION_LINES;
    }

    if (p != NULL && (n->start < p->start || n->end > p->end)) {
        return AMARK_TREE_VIOLATION_PARENT_RANGE;
    }

    if (n->prev_sibling != AMARK_NODE_NONE &&
        at(doc, n->prev_sibling)->end > n->start) {
        return AMARK_TREE_VIOLATION_SIBLING_ORDER;
    }
    if (top && n->next_sibling != AMARK_NODE_NONE &&
        n->end > at(doc, n->next_sibling)->start) {
        return AMARK_TREE_VIOLATION_SIBLING_ORDER;
    }

    if (p != NULL) {
        if (p->kind == AMARK_KIND_DOCUMENT && n->is_inline != 0) {
            return AMARK_TREE_VIOLATION_NESTING;
        }
        if (p->is_inline != 0 && n->is_inline == 0) {
            return AMARK_TREE_VIOLATION_NESTING;
        }
    }
    if (amark_kind_is_core(n->kind) &&
        (n->kind >= AMARK_KIND_TEXT) != (n->is_inline != 0)) {
        return AMARK_TREE_VIOLATION_NESTING;
    }

    /* The ATTRIBUTE conditions come here, last. */
    return AMARK_TREE_VIOLATION_NONE;
}

/*
 * Walks the subtree of top in document order and checks every node. A node
 * is reached from its parent (as the first child) or from its previous
 * sibling, and that node has already checked the link back, so the walk
 * climbs back along the path it came down; the counts still bound both the
 * nodes reached and the steps up by the number of nodes created.
 */
static amark_tree_check_result_t walk(const amark_document_t *doc,
                                      size_t count, amark_node_id_t top)
{
    amark_node_id_t id = top;
    size_t visited = 0;
    size_t climbed = 0;

    for (;;) {
        amark_tree_violation_t v;

        visited++;
        if (visited > count) {
            return result(AMARK_TREE_VIOLATION_LINK, id);
        }
        v = check_node(doc, count, id, id == top);
        if (v != AMARK_TREE_VIOLATION_NONE) {
            return result(v, id);
        }

        /* The next node in document order inside the subtree. */
        if (at(doc, id)->first_child != AMARK_NODE_NONE) {
            id = at(doc, id)->first_child;
            continue;
        }
        while (id != top && at(doc, id)->next_sibling == AMARK_NODE_NONE) {
            climbed++;
            if (climbed > count) {
                return result(AMARK_TREE_VIOLATION_LINK, id);
            }
            id = at(doc, id)->parent;
        }
        if (id == top) {
            return result(AMARK_TREE_VIOLATION_NONE, AMARK_NODE_NONE);
        }
        id = at(doc, id)->next_sibling;
    }
}

amark_tree_check_result_t amark_tree_check(const amark_document_t *doc)
{
    const amark_node_t *root;
    size_t count;

    if (doc == NULL || amark_array_len(&doc->nodes) < 2) {
        return result(AMARK_TREE_VIOLATION_ROOT, AMARK_NODE_NONE);
    }
    count = amark_array_len(&doc->nodes) - 1;
    root = at(doc, 1);
    if (root->kind != AMARK_KIND_DOCUMENT ||
        root->parent != AMARK_NODE_NONE ||
        root->prev_sibling != AMARK_NODE_NONE ||
        root->next_sibling != AMARK_NODE_NONE) {
        return result(AMARK_TREE_VIOLATION_ROOT, 1);
    }
    return walk(doc, count, 1);
}

amark_tree_check_result_t amark_tree_check_subtree(const amark_document_t *doc,
                                                   amark_node_id_t node)
{
    size_t len;

    if (doc == NULL || node == AMARK_NODE_NONE) {
        return result(AMARK_TREE_VIOLATION_LINK, node);
    }
    len = amark_array_len(&doc->nodes);
    if (node >= len) {
        return result(AMARK_TREE_VIOLATION_LINK, node);
    }
    return walk(doc, len - 1, node);
}
