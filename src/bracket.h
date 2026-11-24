/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_BRACKET_H
#define AMARK_BRACKET_H

#include <stdbool.h>
#include <stddef.h>

#include <amark/error.h>

#include "arena.h"
#include "array.h"
#include "cursor.h"
#include "delimiter.h"
#include "draft.h"

/*
 * The brackets of links and images: the bracket stack of one inline content
 * and the handling of a ']' (CommonMark 0.31.2, sections 6.3 and 6.4, and
 * the appendix "look for link or image").
 *
 * The inline parser appends a "[" or "![" as a text item and pushes it here;
 * at a ']' it calls amark_bracket_close, which decides whether a link or an
 * image is made. A link made here deactivates the link brackets still on
 * the stack: rather than walking the stack, the link floor says that a link
 * bracket below it is inactive. Image brackets are never deactivated.
 *
 * None of the functions recurses or keeps global state.
 */

/* An element of the bracket stack: one "[" or "![". */
typedef struct amark_bracket {
    amark_draft_id_t item; /* the text item of "[" or "![" */
    bool image;            /* true: "![" */
    size_t open;           /* the position of the '[' */
} amark_bracket_t;

/*
 * The members are visible in src/ but other modules use the functions only.
 */
typedef struct amark_brackets {
    amark_array_t stack; /* amark_bracket_t; the top is the last element */
    size_t link_floor;   /* a link bracket whose index in the stack is below
                            this is inactive */
} amark_brackets_t;

/* Initializes brackets with an empty stack in scratch. Allocates nothing. */
void amark_brackets_init(amark_brackets_t *brackets, amark_arena_t *scratch);

/*
 * Empties the stack and sets the link floor to 0. The place of the stack is
 * kept, so the brackets of the next content reuse it. Never fails and never
 * allocates.
 */
void amark_brackets_reset(amark_brackets_t *brackets);

/*
 * Pushes the bracket whose text item is item ("[", or "![" if image is
 * true) onto the top of the stack. open is the position of its '['. On
 * failure (AMARK_ERROR_NOMEM or AMARK_ERROR_LIMIT_EXCEEDED), the stack is
 * unchanged.
 */
amark_error_t amark_brackets_push(amark_brackets_t *brackets,
                                  amark_draft_id_t item,
                                  bool image, size_t open);

/*
 * Handles the ']' at the position of cursor. If the stack is empty, does
 * nothing. Otherwise takes the bracket on top off the stack and, if it is
 * active and the bytes after the ']' make a link or an image, makes it:
 *
 *   1. matches the delimiters inside the text with amark_delimiters_process,
 *      the bottom being right after the '[', so they never match one
 *      outside;
 *   2. wraps the items after the bracket's item in a new link or image item
 *      from the bracket's item's start (the '[' of a link, the '!' of an
 *      image) to the end of the form, gives it its attribute with
 *      amark_draft_set_link, and removes the bracket's item;
 *   3. for a link, sets the link floor to the height of the stack, so every
 *      link bracket left on it is inactive.
 *
 * The form tried is the inline form: a part in parentheses read by
 * amark_inline_scan_link_tail right after the ']'.
 *
 * If a link or an image is made, sets *made to true and moves cursor to
 * right after the form. Otherwise sets *made to false and leaves cursor at
 * the ']', which the caller then adds as a text. Taking a bracket off lowers
 * the link floor to the height of the stack if it was above it.
 *
 * Preconditions: the byte at the position is ']'; the items of the brackets
 * and of the delimiters on the stacks are children of the root of draft.
 * On failure (AMARK_ERROR_NOMEM or AMARK_ERROR_LIMIT_EXCEEDED from the
 * delimiters or the draft) returns at once; the content is to be
 * abandoned.
 */
amark_error_t amark_bracket_close(amark_brackets_t *brackets,
                                  amark_draft_t *draft,
                                  amark_delimiters_t *delims,
                                  amark_cursor_t *cursor,
                                  bool *made);

#endif /* AMARK_BRACKET_H */
