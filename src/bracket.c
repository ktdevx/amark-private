/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#include "bracket.h"

#include <stdbool.h>
#include <stddef.h>

#include <amark/tree.h>

#include "definition.h"
#include "inline_scan.h"

void amark_brackets_init(amark_brackets_t *brackets, amark_arena_t *scratch)
{
    amark_array_init(&brackets->stack, scratch, sizeof(amark_bracket_t));
    brackets->link_floor = 0;
}

void amark_brackets_reset(amark_brackets_t *brackets)
{
    amark_array_clear(&brackets->stack);
    brackets->link_floor = 0;
}

amark_error_t amark_brackets_push(amark_brackets_t *brackets,
                                  amark_draft_id_t item,
                                  bool image, size_t open)
{
    amark_bracket_t bracket;

    bracket.item = item;
    bracket.image = image;
    bracket.open = open;
    return amark_array_push(&brackets->stack, &bracket);
}

/*
 * The inline form: right after the ']' at the position, a part in
 * parentheses (no white space between). If it matches, moves cursor to
 * right after its ')', stores the attribute in *attr and returns true;
 * otherwise leaves cursor where it was and returns false.
 */
static bool match_inline(amark_cursor_t *cursor, amark_draft_link_t *attr)
{
    amark_cursor_t at_close = *cursor;
    amark_link_tail_t tail;

    amark_cursor_seek(cursor, cursor->pos + 1);
    if (amark_cursor_at_line_end(cursor) ||
        cursor->bytes[cursor->pos] != '(' ||
        !amark_inline_scan_link_tail(cursor, &tail)) {
        *cursor = at_close;
        return false;
    }
    attr->form = AMARK_LINK_INLINE;
    attr->has_destination = tail.has_destination;
    attr->destination = tail.destination;
    attr->has_title = tail.has_title;
    attr->title = tail.title;
    attr->definition = AMARK_NODE_NONE;
    return true;
}

/*
 * The reference forms (CommonMark 0.31.2, section 6.3), tried when the
 * inline form does not match. bracket is the bracket taken off the stack and
 * cursor is at its ']'. In this order:
 *
 *   - full: right after the ']' (no white space between), a label read by
 *     amark_label_scan. If it has a definition, that is the link; if not,
 *     there is no link and no shortcut is tried (the label then goes on as
 *     brackets of its own);
 *   - collapsed: right after the ']', exactly "[]";
 *   - shortcut: anything else, including a '[' that does not start a label
 *     (such as "[ ]").
 *
 * The collapsed and shortcut forms use the text as the label: it is one if
 * amark_label_scan from the bracket's '[' ends at this ']', so a text with
 * an unescaped bracket is none. Normalizing and comparing the labels is left
 * to amark_definitions_find, which gives the first definition in the
 * document.
 *
 * If one matches, moves cursor to right after the form, stores the
 * attribute in *attr and returns true; otherwise leaves cursor at the ']'
 * and returns false.
 */
static bool match_reference(amark_cursor_t *cursor,
                            const amark_bracket_t *bracket,
                            amark_draft_link_t *attr)
{
    const amark_document_t *doc = cursor->doc;
    size_t close = cursor->pos;
    size_t after = close + 1;
    size_t label_start;
    size_t label_end;
    amark_node_id_t def;
    amark_link_form_t form = AMARK_LINK_SHORTCUT;

    if (after < cursor->line_end && cursor->bytes[after] == '[') {
        if (amark_label_scan(doc, after, cursor->limit, &label_start,
                             &label_end)) {
            form = AMARK_LINK_FULL;
            after = label_end + 1;
        } else if (after + 1 < cursor->line_end &&
                   cursor->bytes[after + 1] == ']') {
            form = AMARK_LINK_COLLAPSED;
            after += 2;
        }
    }
    if (form != AMARK_LINK_FULL &&
        (!amark_label_scan(doc, bracket->open, cursor->limit, &label_start,
                           &label_end) ||
         label_end != close)) {
        return false;
    }
    def = amark_definitions_find(doc, label_start, label_end);
    if (def == AMARK_NODE_NONE) {
        return false;
    }
    attr->form = form;
    attr->has_destination = false;
    attr->destination.start = 0;
    attr->destination.end = 0;
    attr->has_title = false;
    attr->title.start = 0;
    attr->title.end = 0;
    attr->definition = def;
    amark_cursor_seek(cursor, after);
    return true;
}

/*
 * Makes the link or image of bracket (already taken off the stack), ending
 * at the position of cursor, with the attribute *attr: matches the
 * delimiters inside its text first, so emphasis is always decided on the
 * children of the root, then wraps the items after the bracket's item and
 * removes that item. A link raises the link floor to the height of the
 * stack.
 */
static amark_error_t make_link(amark_brackets_t *brackets,
                               amark_draft_t *draft,
                               amark_delimiters_t *delims,
                               const amark_bracket_t *bracket,
                               const amark_cursor_t *cursor,
                               const amark_draft_link_t *attr)
{
    amark_kind_t kind = bracket->image ? AMARK_KIND_IMAGE : AMARK_KIND_LINK;
    size_t start = amark_draft_get(draft, bracket->item)->start;
    amark_draft_id_t id;
    amark_error_t err;

    err = amark_delimiters_process(delims, draft, bracket->open + 1);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    err = amark_draft_wrap(draft, bracket->item, AMARK_DRAFT_NONE, kind,
                           start, cursor->pos, &id);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    err = amark_draft_set_link(draft, id, attr);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    amark_draft_remove(draft, bracket->item);
    if (!bracket->image) {
        brackets->link_floor = amark_array_len(&brackets->stack);
    }
    return AMARK_ERROR_NONE;
}

amark_error_t amark_bracket_close(amark_brackets_t *brackets,
                                  amark_draft_t *draft,
                                  amark_delimiters_t *delims,
                                  amark_cursor_t *cursor,
                                  bool *made)
{
    amark_bracket_t bracket;
    amark_draft_link_t attr;
    size_t height;
    bool active;

    *made = false;
    if (!amark_array_pop(&brackets->stack, &bracket)) {
        return AMARK_ERROR_NONE;
    }
    /* The bracket's index in the stack is now the height. */
    height = amark_array_len(&brackets->stack);
    active = bracket.image || height >= brackets->link_floor;
    if (brackets->link_floor > height) {
        brackets->link_floor = height;
    }
    if (!active) {
        return AMARK_ERROR_NONE;
    }
    if (!match_inline(cursor, &attr) &&
        !match_reference(cursor, &bracket, &attr)) {
        return AMARK_ERROR_NONE;
    }
    *made = true;
    return make_link(brackets, draft, delims, &bracket, cursor, &attr);
}
