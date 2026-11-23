/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * The public readers of the tree (include/amark/tree.h): walking, the kind,
 * the class, the range, the lines and the attributes of a node. They only
 * read the document; nothing here writes, allocates or loops over the tree.
 */

#include <amark/tree.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

/*
 * Returns the attribute element of node if the node can be found, its kind
 * is kind and its attributes are set (index 0 means none), or NULL.
 */
static const amark_node_attr_t *find_attr(const amark_document_t *doc,
                                          amark_node_id_t node,
                                          amark_kind_t kind)
{
    const amark_node_t *n = find(doc, node);

    if (n == NULL || n->kind != kind || n->attr == 0 ||
        n->attr >= amark_array_len(&doc->attrs)) {
        return NULL;
    }
    return (const amark_node_attr_t *)amark_array_at(&doc->attrs, n->attr);
}

/* The attribute element of a link or an image, or NULL. */
static const amark_node_attr_t *find_link(const amark_document_t *doc,
                                          amark_node_id_t node)
{
    const amark_node_attr_t *a = find_attr(doc, node, AMARK_KIND_LINK);

    return a != NULL ? a : find_attr(doc, node, AMARK_KIND_IMAGE);
}

/* The attribute element of a list of the given type, or NULL. */
static const amark_node_attr_t *find_list(const amark_document_t *doc,
                                          amark_node_id_t node,
                                          amark_list_type_t type)
{
    const amark_node_attr_t *a = find_attr(doc, node, AMARK_KIND_LIST);

    return a != NULL && a->list.type == type ? a : NULL;
}

/* The attribute element of a fenced code block, or NULL. */
static const amark_node_attr_t *find_fenced(const amark_document_t *doc,
                                            amark_node_id_t node)
{
    const amark_node_attr_t *a = find_attr(doc, node, AMARK_KIND_CODE_BLOCK);

    return a != NULL && a->code_block.style == AMARK_CODE_BLOCK_FENCED ? a
                                                                       : NULL;
}

/* The attribute element of a link or an image of the inline form, or NULL. */
static const amark_node_attr_t *find_inline_link(const amark_document_t *doc,
                                                 amark_node_id_t node)
{
    const amark_node_attr_t *a = find_link(doc, node);

    return a != NULL && a->link.form == AMARK_LINK_INLINE ? a : NULL;
}

/*
 * Stores the optional range r in *out and returns true if it is written;
 * returns false and leaves *out unchanged otherwise.
 */
static bool read_opt_range(const amark_opt_range_t *r, amark_range_t *out)
{
    if (!r->present || out == NULL) {
        return false;
    }
    out->start = r->start;
    out->end = r->end;
    return true;
}

unsigned amark_heading_level(const amark_document_t *doc,
                             amark_node_id_t node)
{
    const amark_node_attr_t *a = find_attr(doc, node, AMARK_KIND_HEADING);

    return a != NULL ? a->heading.level : 0;
}

amark_heading_style_t amark_heading_style(const amark_document_t *doc,
                                          amark_node_id_t node)
{
    const amark_node_attr_t *a = find_attr(doc, node, AMARK_KIND_HEADING);

    return a != NULL ? a->heading.style : AMARK_HEADING_STYLE_NONE;
}

amark_list_type_t amark_list_type(const amark_document_t *doc,
                                  amark_node_id_t node)
{
    const amark_node_attr_t *a = find_attr(doc, node, AMARK_KIND_LIST);

    return a != NULL ? a->list.type : AMARK_LIST_TYPE_NONE;
}

amark_list_spacing_t amark_list_spacing(const amark_document_t *doc,
                                        amark_node_id_t node)
{
    const amark_node_attr_t *a = find_attr(doc, node, AMARK_KIND_LIST);

    return a != NULL ? a->list.spacing : AMARK_LIST_SPACING_NONE;
}

char amark_list_bullet_char(const amark_document_t *doc, amark_node_id_t node)
{
    const amark_node_attr_t *a = find_list(doc, node, AMARK_LIST_BULLET);

    return a != NULL ? a->list.marker : (char)0;
}

char amark_list_delimiter(const amark_document_t *doc, amark_node_id_t node)
{
    const amark_node_attr_t *a = find_list(doc, node, AMARK_LIST_ORDERED);

    return a != NULL ? a->list.marker : (char)0;
}

bool amark_list_start(const amark_document_t *doc, amark_node_id_t node,
                      uint32_t *out)
{
    const amark_node_attr_t *a = find_list(doc, node, AMARK_LIST_ORDERED);

    if (a == NULL || out == NULL) {
        return false;
    }
    *out = a->list.start_number;
    return true;
}

amark_code_block_style_t amark_code_block_style(const amark_document_t *doc,
                                                amark_node_id_t node)
{
    const amark_node_attr_t *a = find_attr(doc, node, AMARK_KIND_CODE_BLOCK);

    return a != NULL ? a->code_block.style : AMARK_CODE_BLOCK_STYLE_NONE;
}

char amark_code_block_fence_char(const amark_document_t *doc,
                                 amark_node_id_t node)
{
    const amark_node_attr_t *a = find_fenced(doc, node);

    return a != NULL ? a->code_block.fence_char : (char)0;
}

size_t amark_code_block_fence_length(const amark_document_t *doc,
                                     amark_node_id_t node)
{
    const amark_node_attr_t *a = find_fenced(doc, node);

    return a != NULL ? a->code_block.fence_length : 0;
}

bool amark_code_block_info(const amark_document_t *doc, amark_node_id_t node,
                           amark_range_t *out)
{
    const amark_node_attr_t *a = find_fenced(doc, node);

    return a != NULL && read_opt_range(&a->code_block.info, out);
}

amark_link_form_t amark_link_form(const amark_document_t *doc,
                                  amark_node_id_t node)
{
    const amark_node_attr_t *a = find_link(doc, node);

    return a != NULL ? a->link.form : AMARK_LINK_FORM_NONE;
}

bool amark_link_destination(const amark_document_t *doc, amark_node_id_t node,
                            amark_range_t *out)
{
    const amark_node_attr_t *a = find_inline_link(doc, node);

    return a != NULL && read_opt_range(&a->link.destination, out);
}

bool amark_link_title(const amark_document_t *doc, amark_node_id_t node,
                      amark_range_t *out)
{
    const amark_node_attr_t *a = find_inline_link(doc, node);

    return a != NULL && read_opt_range(&a->link.title, out);
}

amark_node_id_t amark_link_definition(const amark_document_t *doc,
                                      amark_node_id_t node)
{
    const amark_node_attr_t *a = find_link(doc, node);

    if (a == NULL || (a->link.form != AMARK_LINK_FULL &&
                      a->link.form != AMARK_LINK_COLLAPSED &&
                      a->link.form != AMARK_LINK_SHORTCUT)) {
        return AMARK_NODE_NONE;
    }
    return a->link.definition;
}

bool amark_definition_label(const amark_document_t *doc, amark_node_id_t node,
                            amark_range_t *out)
{
    const amark_node_attr_t *a =
        find_attr(doc, node, AMARK_KIND_LINK_REFERENCE_DEFINITION);

    return a != NULL && read_opt_range(&a->definition.label, out);
}

bool amark_definition_destination(const amark_document_t *doc,
                                  amark_node_id_t node, amark_range_t *out)
{
    const amark_node_attr_t *a =
        find_attr(doc, node, AMARK_KIND_LINK_REFERENCE_DEFINITION);

    return a != NULL && read_opt_range(&a->definition.destination, out);
}

bool amark_definition_title(const amark_document_t *doc, amark_node_id_t node,
                            amark_range_t *out)
{
    const amark_node_attr_t *a =
        find_attr(doc, node, AMARK_KIND_LINK_REFERENCE_DEFINITION);

    return a != NULL && read_opt_range(&a->definition.title, out);
}

amark_autolink_type_t amark_autolink_type(const amark_document_t *doc,
                                          amark_node_id_t node)
{
    const amark_node_attr_t *a = find_attr(doc, node, AMARK_KIND_AUTOLINK);

    return a != NULL ? a->autolink.type : AMARK_AUTOLINK_TYPE_NONE;
}

const void *amark_node_dialect_data(const amark_document_t *doc,
                                    amark_node_id_t node, size_t *size)
{
    const amark_node_t *n = find(doc, node);
    const amark_node_attr_t *a = NULL;

    if (n != NULL && amark_kind_is_dialect(n->kind)) {
        a = find_attr(doc, node, n->kind);
    }
    if (size != NULL) {
        *size = a != NULL ? a->dialect.size : 0;
    }
    return a != NULL ? a->dialect.data : NULL;
}
