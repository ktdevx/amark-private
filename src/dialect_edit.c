/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#include "dialect_edit.h"

#include <stdbool.h>
#include <stddef.h>

#include "array.h"
#include "document.h"
#include "tree_internal.h"

/* ---------------------------------------------------------------------- */
/* The editor */

void amark_dialect_editor_init(amark_dialect_editor_t *editor,
                               amark_dialect_run_t *run, size_t extension,
                               size_t max_depth, size_t max_nodes)
{
    editor->run = run;
    editor->ext = run->dialect->extensions[extension];
    editor->extension = extension;
    editor->max_depth = max_depth;
    editor->max_nodes = max_nodes;
    editor->failure = AMARK_ERROR_NONE;
}

/*
 * Returns the error an edit returns before it is checked: the editor is
 * NULL or has a lasting failure.
 */
static amark_error_t closed(const amark_dialect_editor_t *editor)
{
    if (editor == NULL) {
        return AMARK_ERROR_INVALID_INPUT;
    }
    return editor->failure;
}

/* Remembers a failure that lasts (10.10, 10.11). */
static amark_error_t fail(amark_dialect_editor_t *editor, amark_error_t err)
{
    editor->failure = err;
    return err;
}

/*
 * Rejects an edit (9.4, 9.7): adds the diagnostic at offset and returns
 * AMARK_ERROR_INVALID_INPUT, or the failure of adding it, which lasts
 * (11.7).
 */
static amark_error_t reject(amark_dialect_editor_t *editor,
                            amark_dialect_reason_t reason, size_t offset)
{
    amark_error_t err = amark_dialect_reject(editor->run, editor->extension,
                                             AMARK_DIALECT_HOOK_POSTPROCESS,
                                             reason, offset);

    if (err != AMARK_ERROR_NONE) {
        return fail(editor, err);
    }
    return AMARK_ERROR_INVALID_INPUT;
}

/* ---------------------------------------------------------------------- */
/* Nodes */

static amark_node_t *node_at(const amark_dialect_editor_t *editor,
                             amark_node_id_t id)
{
    return (amark_node_t *)amark_array_at(&editor->run->doc->nodes, id);
}

/*
 * Returns node if id is a node of the document that was not removed, or
 * NULL (NODE). Every ID is compared with the number of nodes created before
 * it is used (10.8). The tree builder removes a node by detaching it, so a
 * descendant of a removed node keeps its kind; the parsers remove no node,
 * so a postprocessing rule never sees one.
 */
static amark_node_t *find_node(const amark_dialect_editor_t *editor,
                               amark_node_id_t id)
{
    amark_node_t *node;

    if (id == AMARK_NODE_NONE ||
        id > amark_document_node_count(editor->run->doc)) {
        return NULL;
    }
    node = node_at(editor, id);
    return node->kind != AMARK_KIND_NONE ? node : NULL;
}

/*
 * The position of a diagnostic (11.2): the start of the node, or the start
 * of the parsed range (the root) when there is no such node.
 */
static size_t position(const amark_dialect_editor_t *editor,
                       const amark_node_t *node)
{
    if (node == NULL) {
        node = node_at(editor, amark_document_root(editor->run->doc));
    }
    return node->start;
}

/* The class of children a kind may hold (2.8). */
typedef enum holds {
    HOLDS_NOTHING,
    HOLDS_BLOCKS,
    HOLDS_INLINES
} holds_t;

/*
 * Returns the class of children a node of kind may hold, by the table of
 * the design; the class of a dialect kind is looked up in the whole
 * dialect, so a kind of another extension is answered too.
 */
static holds_t holds(const amark_dialect_t *dialect, amark_kind_t kind)
{
    const amark_dialect_kind_t *k;

    switch (kind) {
    case AMARK_KIND_DOCUMENT:
    case AMARK_KIND_BLOCK_QUOTE:
    case AMARK_KIND_LIST_ITEM:
        return HOLDS_BLOCKS;
    case AMARK_KIND_PARAGRAPH:
    case AMARK_KIND_HEADING:
    case AMARK_KIND_EMPHASIS:
    case AMARK_KIND_STRONG:
    case AMARK_KIND_LINK:
    case AMARK_KIND_IMAGE:
        return HOLDS_INLINES;
    default:
        break;
    }
    if (kind < AMARK_KIND_DIALECT_MIN) {
        /* A list (its items cannot be added) and the other kinds. */
        return HOLDS_NOTHING;
    }
    k = amark_dialect_find_kind_any(dialect, kind);
    if (k == NULL) {
        return HOLDS_NOTHING;
    }
    switch (k->cls) {
    case AMARK_DIALECT_CONTAINER:
        return HOLDS_BLOCKS;
    case AMARK_DIALECT_LEAF:
    case AMARK_DIALECT_INLINE:
        return HOLDS_INLINES;
    default:
        return HOLDS_NOTHING;
    }
}

/* ---------------------------------------------------------------------- */
/* The public functions */

amark_error_t amark_dialect_edit_set_kind(amark_dialect_editor_t *editor,
                                          amark_node_id_t node,
                                          amark_kind_t kind)
{
    const amark_dialect_kind_t *k;
    amark_node_t *target;
    bool to_inline;
    amark_error_t err = closed(editor);

    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    target = find_node(editor, node);
    if (target == NULL) {
        return reject(editor, AMARK_DIALECT_REASON_NODE,
                      position(editor, NULL));
    }
    if (target->kind == AMARK_KIND_DOCUMENT ||
        target->kind == AMARK_KIND_LINK_REFERENCE_DEFINITION) {
        return reject(editor, AMARK_DIALECT_REASON_PROTECTED, target->start);
    }

    /* A kind of the extension (2.7, 9.6). */
    k = amark_dialect_find_kind(editor->ext, kind);
    if (k == NULL) {
        return reject(editor, AMARK_DIALECT_REASON_KIND, target->start);
    }

    /* The same class, and able to hold the children (2.8, 9.6). */
    to_inline = k->cls == AMARK_DIALECT_INLINE;
    if (to_inline != (target->is_inline != 0)) {
        return reject(editor, AMARK_DIALECT_REASON_NESTING, target->start);
    }
    if (target->first_child != AMARK_NODE_NONE) {
        holds_t needed = node_at(editor, target->first_child)->is_inline
                             ? HOLDS_INLINES
                             : HOLDS_BLOCKS;

        if (holds(editor->run->dialect, kind) != needed) {
            return reject(editor, AMARK_DIALECT_REASON_NESTING,
                          target->start);
        }
    }

    amark_tree_set_kind(editor->run->doc, node, kind,
                        to_inline ? AMARK_NODE_CLASS_INLINE
                                  : AMARK_NODE_CLASS_BLOCK);
    return AMARK_ERROR_NONE;
}

amark_error_t amark_dialect_edit_set_data(amark_dialect_editor_t *editor,
                                          amark_node_id_t node,
                                          const void *data, size_t size)
{
    amark_node_t *target;
    amark_error_t err = closed(editor);

    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    target = find_node(editor, node);
    if (data == NULL && size > 0) {
        /* A value the contract does not define (10.4). */
        return reject(editor, AMARK_DIALECT_REASON_STATUS,
                      position(editor, target));
    }
    if (target == NULL) {
        return reject(editor, AMARK_DIALECT_REASON_NODE,
                      position(editor, NULL));
    }
    if (amark_dialect_find_kind(editor->ext, target->kind) == NULL) {
        return reject(editor, AMARK_DIALECT_REASON_KIND, target->start);
    }

    /* On failure the node is unchanged. */
    err = amark_tree_set_dialect_data(editor->run->doc, node, data, size);
    if (err != AMARK_ERROR_NONE) {
        return fail(editor, err);
    }
    return AMARK_ERROR_NONE;
}

amark_error_t amark_dialect_edit_shrink(amark_dialect_editor_t *editor,
                                        amark_node_id_t node,
                                        amark_range_t range)
{
    amark_node_t *target;
    amark_error_t err = closed(editor);

    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    target = find_node(editor, node);
    if (target == NULL) {
        return reject(editor, AMARK_DIALECT_REASON_NODE,
                      position(editor, NULL));
    }

    /*
     * A text or a kind of the extension. The CommonMark blocks and the
     * nodes with ranges in their attributes keep their ranges, so the
     * marks the source text reads stay inside them.
     */
    if (target->kind != AMARK_KIND_TEXT &&
        amark_dialect_find_kind(editor->ext, target->kind) == NULL) {
        return reject(editor, AMARK_DIALECT_REASON_PROTECTED, target->start);
    }

    /*
     * Inside the current range (so inside the parent and between the
     * siblings), and around the children (9.6).
     */
    if (range.start > range.end || range.start < target->start ||
        range.end > target->end) {
        return reject(editor, AMARK_DIALECT_REASON_PARENT, target->start);
    }
    if (target->first_child != AMARK_NODE_NONE &&
        (node_at(editor, target->first_child)->start < range.start ||
         node_at(editor, target->last_child)->end > range.end)) {
        return reject(editor, AMARK_DIALECT_REASON_PARENT, target->start);
    }

    amark_tree_set_range(editor->run->doc, node, range.start, range.end);
    return AMARK_ERROR_NONE;
}
