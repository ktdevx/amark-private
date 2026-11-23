/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_TREE_CHECK_H
#define AMARK_TREE_CHECK_H

#include <amark/tree.h>

/*
 * The tree check: checks the invariants of the tree of a document and
 * returns the first violation. Internal: not declared in a public header and
 * not exported from the shared library; the parsers' unit tests call it to
 * find mistakes in the trees they build.
 *
 * The check does not change the document, allocates nothing and does not
 * recurse. It walks the tree in document order through the first child, next
 * sibling and parent indices only. Every index is compared with the number
 * of nodes created before it is used, and a walk that reaches more nodes
 * than were created reports a cycle (AMARK_TREE_VIOLATION_LINK), so a broken
 * tree never makes it read outside the node array or loop forever.
 *
 * The first violation in document order is returned. The conditions of one
 * node are checked in the order of the enumeration below (ROOT, LINK,
 * BOUNDS, LINES, PARENT_RANGE, SIBLING_ORDER, NESTING, ATTRIBUTE). A link
 * condition that involves another node (the parent of the first child, the
 * previous sibling of the next sibling, ...) is reported on the node being
 * checked, which comes first in document order.
 *
 * Removed nodes (kind AMARK_KIND_NONE) are not reachable from the root;
 * their descendants keep their kinds but are not reachable either, so
 * neither is checked by amark_tree_check. A removed node that is reachable
 * is a LINK violation.
 */

typedef enum amark_tree_violation {
    AMARK_TREE_VIOLATION_NONE = 0,
    /* The root is not a document, or has a parent or a sibling. */
    AMARK_TREE_VIOLATION_ROOT,
    /* The links disagree, an index is out of range, or there is a cycle. */
    AMARK_TREE_VIOLATION_LINK,
    /* start > end, or end > the content length. */
    AMARK_TREE_VIOLATION_BOUNDS,
    /* The lines do not match the range (amark_document_range_lines). */
    AMARK_TREE_VIOLATION_LINES,
    /* The range of a child is not inside the range of its parent. */
    AMARK_TREE_VIOLATION_PARENT_RANGE,
    /* A node overlaps its previous sibling or comes before it. */
    AMARK_TREE_VIOLATION_SIBLING_ORDER,
    /* A child of the document is an inline, a child of an inline is a block,
     * or the class of a CommonMark kind does not match its ID. */
    AMARK_TREE_VIOLATION_NESTING,
    /* An attribute value, range or definition is not correct. */
    AMARK_TREE_VIOLATION_ATTRIBUTE
} amark_tree_violation_t;

typedef struct amark_tree_check_result {
    amark_tree_violation_t violation;
    amark_node_id_t node; /* the node that violates; AMARK_NODE_NONE if none */
} amark_tree_check_result_t;

/*
 * Checks the whole tree of doc from the root.
 *
 * The root (ID 1) must be a document without a parent or siblings (ROOT).
 * For each node reached, in document order:
 *   LINK: the five indices are at most the number of nodes created; the
 *     kind is not AMARK_KIND_NONE; the first child has no previous sibling
 *     and names the node as its parent; the last child has no next sibling;
 *     the first and the last child are both absent or both present; the next
 *     sibling names the node as its previous sibling and has the same parent;
 *     a node without a next sibling is the last child of its parent.
 *   BOUNDS: start <= end <= the content length.
 *   LINES: the lines are those amark_document_range_lines derives.
 *   PARENT_RANGE: (not the root) parent start <= start and end <= parent end.
 *   SIBLING_ORDER: previous sibling end <= start.
 *   NESTING: a child of the document is a block; a child of an inline is an
 *     inline; a CommonMark kind is an inline exactly when its ID is at least
 *     AMARK_KIND_TEXT.
 *
 * If doc is NULL or has no root element, returns AMARK_TREE_VIOLATION_ROOT
 * with AMARK_NODE_NONE.
 */
amark_tree_check_result_t amark_tree_check(const amark_document_t *doc);

/*
 * Checks node and its descendants with the conditions of amark_tree_check
 * from LINK on (not ROOT). Besides, the relations of node with the nodes
 * around it are checked: its parent exists (unless node is the root), is not
 * removed, and has node as its first child when node has no previous
 * sibling; the previous sibling names node as its next sibling and has the
 * same parent (LINK); node is inside its parent's range (PARENT_RANGE); it
 * does not overlap its previous sibling, and node end <= next sibling start
 * (SIBLING_ORDER); it nests correctly under its parent (NESTING). Nodes
 * outside the subtree are not checked otherwise.
 *
 * If doc is NULL, node is AMARK_NODE_NONE or node is larger than the number
 * of nodes created, returns AMARK_TREE_VIOLATION_LINK with node.
 */
amark_tree_check_result_t amark_tree_check_subtree(const amark_document_t *doc,
                                                   amark_node_id_t node);

#endif /* AMARK_TREE_CHECK_H */
