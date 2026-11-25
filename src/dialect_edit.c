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
    editor->rejected = false;
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
    editor->rejected = true;
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

/* ---------------------------------------------------------------------- */
/* Adding nodes */

/*
 * Checks the node limit before a node is added (9.10, 10.11). The count
 * includes the root, and no edit removes a node, so it is the number of
 * nodes the returned tree has. Rejected edits never get here (10.12).
 */
static bool too_many_nodes(const amark_dialect_editor_t *editor)
{
    return editor->max_nodes != 0 &&
           amark_document_node_count(editor->run->doc) >= editor->max_nodes;
}

/*
 * Checks the depth limit for a child of parent: the depth of a node is the
 * number of its ancestors. Walks up from parent with one loop; only called
 * when there is a limit.
 */
static bool too_deep(const amark_dialect_editor_t *editor,
                     const amark_node_t *parent)
{
    size_t depth = 1; /* the new child's */

    while (parent->parent != AMARK_NODE_NONE) {
        depth++;
        parent = node_at(editor, parent->parent);
    }
    return depth > editor->max_depth;
}

/*
 * Finds the place of a child over range in parent (9.5): the first child,
 * from hint (or the first child) on, that starts at or after range.start.
 * Stores it in *next (AMARK_NODE_NONE: after the last child) and the child
 * before the place in *prev. Returns false (ORDER) if the place is before
 * hint, which the search does not go back to: hint ends after range.start,
 * or hint is an empty node at range.start after a sibling that also starts
 * there (without hint, the place would be before that sibling). So a
 * search with a hint finds the same place as one without, or fails.
 */
static bool find_place(const amark_dialect_editor_t *editor,
                       const amark_node_t *parent, amark_node_id_t hint,
                       amark_range_t range, amark_node_id_t *prev,
                       amark_node_id_t *next)
{
    amark_node_id_t id = parent->first_child;

    if (hint != AMARK_NODE_NONE) {
        const amark_node_t *h = node_at(editor, hint);

        if (h->end > range.start) {
            return false;
        }
        if (h->start >= range.start && h->prev_sibling != AMARK_NODE_NONE &&
            node_at(editor, h->prev_sibling)->start >= range.start) {
            return false;
        }
        id = hint;
    }
    /* The starts of siblings do not decrease, so the first one found is
     * the first in the whole list. */
    while (id != AMARK_NODE_NONE && node_at(editor, id)->start < range.start) {
        id = node_at(editor, id)->next_sibling;
    }
    *next = id;
    *prev = id != AMARK_NODE_NONE ? node_at(editor, id)->prev_sibling
                                  : parent->last_child;
    return true;
}

/*
 * Creates a node of kind over [start, end) directly before next, or as the
 * last child of parent when next is AMARK_NODE_NONE.
 */
static amark_error_t insert(amark_dialect_editor_t *editor,
                            amark_node_id_t parent, amark_node_id_t next,
                            amark_kind_t kind, amark_node_class_t cls,
                            size_t start, size_t end, amark_node_id_t *out)
{
    if (next != AMARK_NODE_NONE) {
        return amark_tree_insert_before(editor->run->doc, next, kind, cls,
                                        start, end, out);
    }
    return amark_tree_append(editor->run->doc, parent, kind, cls, start, end,
                             out);
}

amark_error_t amark_dialect_edit_add_child(amark_dialect_editor_t *editor,
                                           amark_node_id_t parent,
                                           amark_node_id_t hint,
                                           amark_kind_t kind,
                                           amark_range_t range,
                                           const void *data, size_t size,
                                           amark_node_id_t *out)
{
    const amark_dialect_kind_t *k;
    amark_node_t *target;
    amark_node_t *h = NULL;
    amark_node_id_t prev;
    amark_node_id_t next;
    amark_node_id_t id = AMARK_NODE_NONE;
    holds_t needed;
    amark_error_t err;

    if (out != NULL) {
        *out = AMARK_NODE_NONE;
    }
    err = closed(editor);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    target = find_node(editor, parent);
    if (out == NULL || (data == NULL && size > 0)) {
        /* Values the contract does not define (10.4). */
        return reject(editor, AMARK_DIALECT_REASON_STATUS,
                      position(editor, target));
    }
    if (target == NULL) {
        return reject(editor, AMARK_DIALECT_REASON_NODE,
                      position(editor, NULL));
    }

    /* A kind of the extension (2.7, 9.6), of a class the parent can hold
     * (2.8). */
    k = amark_dialect_find_kind(editor->ext, kind);
    if (k == NULL) {
        return reject(editor, AMARK_DIALECT_REASON_KIND, target->start);
    }
    needed = k->cls == AMARK_DIALECT_INLINE ? HOLDS_INLINES : HOLDS_BLOCKS;
    if (holds(editor->run->dialect, target->kind) != needed) {
        return reject(editor, AMARK_DIALECT_REASON_NESTING, target->start);
    }

    /* The hint is none or a child of the parent. */
    if (hint != AMARK_NODE_NONE) {
        h = find_node(editor, hint);
        if (h == NULL || h->parent != parent) {
            return reject(editor, AMARK_DIALECT_REASON_NODE, target->start);
        }
    }

    /* Inside the content, inside the parent, between the siblings. */
    if (range.start > range.end ||
        range.end > editor->run->doc->content_length) {
        return reject(editor, AMARK_DIALECT_REASON_BOUNDS, target->start);
    }
    if (range.start < target->start || range.end > target->end) {
        return reject(editor, AMARK_DIALECT_REASON_PARENT, target->start);
    }
    if (!find_place(editor, target, hint, range, &prev, &next) ||
        (prev != AMARK_NODE_NONE &&
         node_at(editor, prev)->end > range.start) ||
        (next != AMARK_NODE_NONE &&
         range.end > node_at(editor, next)->start)) {
        return reject(editor, AMARK_DIALECT_REASON_ORDER, target->start);
    }

    /* The limits (9.10, 10.11): failures that last. */
    if (too_many_nodes(editor) ||
        (editor->max_depth != 0 && too_deep(editor, target))) {
        return fail(editor, AMARK_ERROR_LIMIT_EXCEEDED);
    }

    err = insert(editor, parent, next, kind,
                 k->cls == AMARK_DIALECT_INLINE ? AMARK_NODE_CLASS_INLINE
                                                : AMARK_NODE_CLASS_BLOCK,
                 range.start, range.end, &id);
    if (err != AMARK_ERROR_NONE) {
        return fail(editor, err);
    }
    if (size > 0) {
        err = amark_tree_set_dialect_data(editor->run->doc, id, data, size);
        if (err != AMARK_ERROR_NONE) {
            /* Detached: the tree has the shape it had (the parse ends with
             * the failure). */
            amark_tree_remove(editor->run->doc, id);
            return fail(editor, err);
        }
    }
    *out = id;
    return AMARK_ERROR_NONE;
}

amark_error_t amark_dialect_edit_split_text(amark_dialect_editor_t *editor,
                                            amark_node_id_t node, size_t at,
                                            amark_node_id_t *second)
{
    amark_node_t *target;
    amark_node_id_t id = AMARK_NODE_NONE;
    size_t start;
    size_t end;
    amark_error_t err;

    if (second != NULL) {
        *second = AMARK_NODE_NONE;
    }
    err = closed(editor);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    target = find_node(editor, node);
    if (second == NULL) {
        /* A value the contract does not define (10.4). */
        return reject(editor, AMARK_DIALECT_REASON_STATUS,
                      position(editor, target));
    }
    if (target == NULL) {
        return reject(editor, AMARK_DIALECT_REASON_NODE,
                      position(editor, NULL));
    }

    /* A text, split strictly inside, at the start of a UTF-8 character
     * (9.6): at < end <= the content length, so content[at] is read. */
    if (target->kind != AMARK_KIND_TEXT || at <= target->start ||
        at >= target->end ||
        (editor->run->doc->content[at] & 0xC0) == 0x80) {
        return reject(editor, AMARK_DIALECT_REASON_TEXT, target->start);
    }

    /* The node limit; the depth of no node changes. */
    if (too_many_nodes(editor)) {
        return fail(editor, AMARK_ERROR_LIMIT_EXCEEDED);
    }

    start = target->start;
    end = target->end;
    err = insert(editor, target->parent, target->next_sibling,
                 AMARK_KIND_TEXT, AMARK_NODE_CLASS_AUTO, at, end, &id);
    if (err != AMARK_ERROR_NONE) {
        return fail(editor, err);
    }
    /* target may have moved with the node array: use the ID. */
    amark_tree_set_range(editor->run->doc, node, start, at);
    *second = id;
    return AMARK_ERROR_NONE;
}

/* ---------------------------------------------------------------------- */
/* Calling the postprocessing rules */

amark_error_t amark_dialect_postprocess(amark_dialect_run_t *run,
                                        size_t max_depth, size_t max_nodes)
{
    const amark_dialect_t *dialect = run->dialect;
    amark_dialect_editor_t editor;
    amark_dialect_post_t post;
    size_t i;
    amark_error_t err;

    for (i = 0; i < dialect->extension_count; i++) {
        const amark_dialect_extension_t *ext = dialect->extensions[i];

        if (ext->postprocess == NULL) {
            continue;
        }
        /* Set again for each rule: a rule may not change what it got. */
        post.doc = run->doc;
        post.content = (const char *)run->doc->content;
        post.content_length = run->doc->content_length;
        amark_dialect_editor_init(&editor, run, i, max_depth, max_nodes);
        err = ext->postprocess(&post, run->states[i], &editor);
        if (editor.failure != AMARK_ERROR_NONE) {
            return editor.failure;
        }
        if (err == AMARK_ERROR_NOMEM) {
            return err;
        }
        if (err == AMARK_ERROR_INVALID_INPUT && editor.rejected) {
            /* What a rejected edit returned, passed on: its diagnostic is
             * the only one (11.1). */
            continue;
        }
        if (err != AMARK_ERROR_NONE) {
            /* A value the contract does not define (10.4). */
            err = amark_dialect_reject(run, i, AMARK_DIALECT_HOOK_POSTPROCESS,
                                       AMARK_DIALECT_REASON_STATUS,
                                       position(&editor, NULL));
            if (err != AMARK_ERROR_NONE) {
                return err;
            }
        }
    }
    return AMARK_ERROR_NONE;
}
