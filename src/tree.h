/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_TREE_INTERNAL_H
#define AMARK_TREE_INTERNAL_H

#include <stddef.h>

#include <amark/error.h>
#include <amark/tree.h>

#include "document.h"

/*
 * The tree builder: the operations the parsers and the dialects use to build
 * the tree of a document. Internal: not declared in a public header and not
 * exported from the shared library.
 *
 * The operations keep the five links (parent, first child, last child,
 * previous sibling, next sibling) and the lines of every node correct. They
 * do not check ranges, nesting or attributes; the tree check does. They take
 * and return node IDs, never pointers to nodes. IDs are given in the order
 * the nodes are added, so the same operations in the same order give the
 * same tree with the same IDs.
 *
 * Common preconditions: doc is not NULL; every node ID was created in doc
 * and has not been removed; start <= end <= the content length; if kind is
 * below AMARK_KIND_DIALECT_MIN, cls is AMARK_NODE_CLASS_AUTO, otherwise
 * AMARK_NODE_CLASS_BLOCK or AMARK_NODE_CLASS_INLINE; out is not NULL. The
 * result of a call that breaks them is undefined.
 *
 * The lines of a node are derived from its range with
 * amark_document_range_lines; callers never pass lines.
 */

/* Whether a new node is a block or an inline. */
typedef enum amark_node_class {
    AMARK_NODE_CLASS_AUTO = 0,  /* a CommonMark kind: decided by its ID */
    AMARK_NODE_CLASS_BLOCK = 1,
    AMARK_NODE_CLASS_INLINE = 2
} amark_node_class_t;

/*
 * Creates a node of kind over [start, end) and makes it the last child of
 * parent. Stores its ID in *out.
 *
 * If the document already has UINT32_MAX - 1 nodes, returns
 * AMARK_ERROR_LIMIT_EXCEEDED before touching the node storage. If the
 * allocator fails, returns AMARK_ERROR_NOMEM. On failure the tree is as it
 * was before the call and *out is AMARK_NODE_NONE.
 */
amark_error_t amark_tree_append(amark_document_t *doc, amark_node_id_t parent,
                                amark_kind_t kind, amark_node_class_t cls,
                                size_t start, size_t end,
                                amark_node_id_t *out);

/*
 * Creates a node as amark_tree_append does and makes it the sibling directly
 * before sibling, which is not the root. Fails as amark_tree_append does.
 */
amark_error_t amark_tree_insert_before(amark_document_t *doc,
                                       amark_node_id_t sibling,
                                       amark_kind_t kind,
                                       amark_node_class_t cls, size_t start,
                                       size_t end, amark_node_id_t *out);

/*
 * Creates a node as amark_tree_append does and moves the consecutive
 * siblings from first to last under it, keeping their order. first and last
 * have the same parent and first is last or comes before it. The new node
 * takes the place of first. Takes time in proportion to the number of
 * siblings moved (each one's parent is changed), with one loop. Fails as
 * amark_tree_append does; on failure no sibling has moved.
 */
amark_error_t amark_tree_wrap(amark_document_t *doc, amark_node_id_t first,
                              amark_node_id_t last, amark_kind_t kind,
                              amark_node_class_t cls, size_t start, size_t end,
                              amark_node_id_t *out);

/* Changes the range of node and derives its lines again. Cannot fail. */
void amark_tree_set_range(amark_document_t *doc, amark_node_id_t node,
                          size_t start, size_t end);

/*
 * Changes the kind of node, which is not the root. Its attributes become
 * "none" (the old attribute element stays in the array, unreferenced); its
 * children stay. Cannot fail.
 */
void amark_tree_set_kind(amark_document_t *doc, amark_node_id_t node,
                         amark_kind_t kind, amark_node_class_t cls);

/*
 * Detaches node, which is not the root, together with its descendants, in
 * constant time. Its kind becomes AMARK_KIND_NONE and its ID must not be
 * used again; the element is not reused, so later nodes still get new IDs.
 * The descendants are left as they are, out of the tree. Cannot fail.
 */
void amark_tree_remove(amark_document_t *doc, amark_node_id_t node);

/*
 * The attribute setters. The kind of node must be one that has the
 * attribute (a heading, a list, a code block, a link or an image, a link
 * reference definition, an autolink, a dialect kind).
 *
 * If node has no attribute element yet, one is added to the attribute array;
 * this is the only step that can fail, with AMARK_ERROR_NOMEM (the allocator
 * failed) or AMARK_ERROR_LIMIT_EXCEEDED (the attribute array is full), and
 * then node is unchanged. Otherwise the element is overwritten as a whole,
 * which cannot fail.
 *
 * A range argument that is NULL means "not written in the source"; a range
 * with start == end means "written but empty". Ranges are byte offsets from
 * the start of the content, start <= end <= the content length.
 */

amark_error_t amark_tree_set_heading(amark_document_t *doc,
                                     amark_node_id_t node, unsigned level,
                                     amark_heading_style_t style);

/*
 * marker is the bullet character of a bullet list and the delimiter of an
 * ordered list. start_number has a meaning for an ordered list only. Call
 * again when the spacing is decided later.
 */
amark_error_t amark_tree_set_list(amark_document_t *doc, amark_node_id_t node,
                                  amark_list_type_t type, char marker,
                                  uint32_t start_number,
                                  amark_list_spacing_t spacing);

/* An indented code block takes fence_char 0, fence_length 0, info NULL. */
amark_error_t amark_tree_set_code_block(amark_document_t *doc,
                                        amark_node_id_t node,
                                        amark_code_block_style_t style,
                                        char fence_char, size_t fence_length,
                                        const amark_range_t *info);

/*
 * For links and images. The inline form takes definition AMARK_NODE_NONE;
 * the reference forms take destination and title NULL.
 */
amark_error_t amark_tree_set_link(amark_document_t *doc, amark_node_id_t node,
                                  amark_link_form_t form,
                                  const amark_range_t *destination,
                                  const amark_range_t *title,
                                  amark_node_id_t definition);

amark_error_t amark_tree_set_definition(amark_document_t *doc,
                                        amark_node_id_t node,
                                        const amark_range_t *label,
                                        const amark_range_t *destination,
                                        const amark_range_t *title);

amark_error_t amark_tree_set_autolink(amark_document_t *doc,
                                      amark_node_id_t node,
                                      amark_autolink_type_t type);

/*
 * Copies size bytes from data into the document, aligned for any type, and
 * makes them the attribute of node, a dialect kind. data may be NULL only
 * when size is 0. The copy is made before the attribute element is added;
 * if that fails, the copy stays in the arena, referred to by nothing. A size
 * of 0 makes the attribute "none" (node->attr becomes 0, as
 * amark_tree_set_kind does) and cannot fail.
 */
amark_error_t amark_tree_set_dialect_data(amark_document_t *doc,
                                          amark_node_id_t node,
                                          const void *data, size_t size);

#endif /* AMARK_TREE_INTERNAL_H */
