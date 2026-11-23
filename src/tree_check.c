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
 * A range an attribute holds: absent, or start <= end <= the content
 * length. inside: a present range must also lie inside the range of n.
 */
static bool range_holds(const amark_document_t *doc,
                        const amark_opt_range_t *r, const amark_node_t *n,
                        bool inside)
{
    if (!r->present) {
        return true;
    }
    if (r->start > r->end || r->end > doc->content_length) {
        return false;
    }
    return !inside || (n->start <= r->start && r->end <= n->end);
}

/*
 * The ATTRIBUTE conditions of n (count nodes created). The attribute index
 * is compared with the length of the attribute array before the element is
 * read, and a link target with the number of nodes created before its kind
 * is read. The attributes of dialect kinds, and of kinds without
 * attributes, are not checked.
 */
static bool attribute_holds(const amark_document_t *doc, size_t count,
                            const amark_node_t *n)
{
    const amark_node_attr_t *a;

    switch (n->kind) {
    case AMARK_KIND_HEADING:
    case AMARK_KIND_LIST:
    case AMARK_KIND_CODE_BLOCK:
    case AMARK_KIND_LINK:
    case AMARK_KIND_IMAGE:
    case AMARK_KIND_LINK_REFERENCE_DEFINITION:
    case AMARK_KIND_AUTOLINK:
        break;
    default:
        return true;
    }
    if (n->attr == 0 || n->attr >= amark_array_len(&doc->attrs)) {
        return false;
    }
    a = (const amark_node_attr_t *)amark_array_at(&doc->attrs, n->attr);

    switch (n->kind) {
    case AMARK_KIND_HEADING:
        if (a->heading.style == AMARK_HEADING_ATX) {
            return a->heading.level >= 1 && a->heading.level <= 6;
        }
        if (a->heading.style == AMARK_HEADING_SETEXT) {
            return a->heading.level >= 1 && a->heading.level <= 2;
        }
        return false;
    case AMARK_KIND_LIST:
        if (a->list.spacing != AMARK_LIST_TIGHT &&
            a->list.spacing != AMARK_LIST_LOOSE) {
            return false;
        }
        if (a->list.type == AMARK_LIST_BULLET) {
            return a->list.marker == '-' || a->list.marker == '+' ||
                   a->list.marker == '*';
        }
        if (a->list.type == AMARK_LIST_ORDERED) {
            return a->list.marker == '.' || a->list.marker == ')';
        }
        return false;
    case AMARK_KIND_CODE_BLOCK:
        if (a->code_block.style == AMARK_CODE_BLOCK_INDENTED) {
            return range_holds(doc, &a->code_block.info, n, false);
        }
        if (a->code_block.style == AMARK_CODE_BLOCK_FENCED) {
            return (a->code_block.fence_char == '`' ||
                    a->code_block.fence_char == '~') &&
                   a->code_block.fence_length >= 3 &&
                   range_holds(doc, &a->code_block.info, n, true);
        }
        return false;
    case AMARK_KIND_LINK:
    case AMARK_KIND_IMAGE:
        if (a->link.form == AMARK_LINK_INLINE) {
            return a->link.definition == AMARK_NODE_NONE &&
                   range_holds(doc, &a->link.destination, n, true) &&
                   range_holds(doc, &a->link.title, n, true);
        }
        if (a->link.form == AMARK_LINK_FULL ||
            a->link.form == AMARK_LINK_COLLAPSED ||
            a->link.form == AMARK_LINK_SHORTCUT) {
            /* A removed definition has kind AMARK_KIND_NONE. */
            return a->link.definition != AMARK_NODE_NONE &&
                   a->link.definition <= count &&
                   at(doc, a->link.definition)->kind ==
                       AMARK_KIND_LINK_REFERENCE_DEFINITION &&
                   !a->link.destination.present && !a->link.title.present;
        }
        return false;
    case AMARK_KIND_LINK_REFERENCE_DEFINITION:
        return a->definition.label.present &&
               a->definition.destination.present &&
               range_holds(doc, &a->definition.label, n, true) &&
               range_holds(doc, &a->definition.destination, n, true) &&
               range_holds(doc, &a->definition.title, n, true);
    default: /* AMARK_KIND_AUTOLINK */
        return a->autolink.type == AMARK_AUTOLINK_URI ||
               a->autolink.type == AMARK_AUTOLINK_EMAIL;
    }
}

/*
 * Checks node id from LINK to ATTRIBUTE. The ranges and indices of the
 * nodes read after LINK (parent, previous and next sibling) are known to be
 * in range by then.
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

    if (!attribute_holds(doc, count, n)) {
        return AMARK_TREE_VIOLATION_ATTRIBUTE;
    }
    return AMARK_TREE_VIOLATION_NONE;
}

/*
 * Walks the subtree of top in document order and checks every node. A node
 * is reached from its parent (as the first child) or from its previous
 * sibling, and that node has already checked the link back, so the walk
 * climbs back along the path it came down; the counts still bound both the
 * nodes reached and the steps up by the number of nodes created.
 *
 * The visit count guard is not only a backstop: in a subtree check, the
 * relations of top with nodes outside the subtree are checked only locally,
 * so a cycle can pass every link condition. For example, two nodes that
 * name each other as parent and as only child make the walk go round them
 * until the visit count passes the number of nodes created.
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
