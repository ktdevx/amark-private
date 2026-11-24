/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_DELIMITER_H
#define AMARK_DELIMITER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <amark/error.h>
#include <amark/tree.h>

#include "arena.h"
#include "array.h"
#include "draft.h"

/*
 * The delimiters of emphasis: the kinds of delimiter characters, whether a
 * delimiter run can open or close (CommonMark 0.31.2, section 6.2), and the
 * stack of delimiter runs of one inline content.
 *
 * This module knows neither the cursor nor the inline scan: the caller
 * passes the characters around a run as code points. None of the functions
 * recurses or keeps global state.
 */

/* The largest number of delimiter kinds. */
#define AMARK_DELIMITER_KIND_MAX 8

/*
 * A kind of delimiter. Holds no pointer, so a table of kinds can be copied
 * as it is.
 */
typedef struct amark_delimiter_kind {
    unsigned char ch;       /* the delimiter character */
    bool intraword_limited; /* true: does not open or close inside a word
                               (the rule of `_`) */
    amark_kind_t single;    /* the kind made of one delimiter on each side */
    amark_kind_t pair;      /* the kind made of two delimiters on each side */
} amark_delimiter_kind_t;

/*
 * An element of the delimiter stack: one delimiter run. The elements form a
 * doubly linked list over the array; an element is named by its index in
 * the array plus 1, and 0 means "none".
 */
typedef struct amark_delimiter {
    amark_draft_id_t item; /* the text item of the run */
    size_t kind;           /* the index of the kind in kinds */
    bool can_open;
    bool can_close;
    size_t length;         /* the length of the run when it was pushed; does
                              not shrink when delimiters are used */
    uint32_t prev;         /* the element below, or 0 */
    uint32_t next;         /* the element above, or 0 */
} amark_delimiter_t;

/*
 * The members are visible in src/ but other modules use the functions only.
 */
typedef struct amark_delimiters {
    amark_delimiter_kind_t kinds[AMARK_DELIMITER_KIND_MAX];
    size_t kind_count;   /* 2 in this specification (`*` and `_`) */
    amark_array_t stack; /* amark_delimiter_t */
    uint32_t top;        /* the element on top; 0 when the stack is empty */
} amark_delimiters_t;

/*
 * Initializes delims with the two kinds of CommonMark (`*` and `_`, both
 * making AMARK_KIND_EMPHASIS of one delimiter and AMARK_KIND_STRONG of two;
 * only `_` is limited inside a word) and an empty stack in scratch.
 * Allocates nothing.
 */
void amark_delimiters_init(amark_delimiters_t *delims,
                           amark_arena_t *scratch);

/*
 * Empties the stack. The kinds and the place of the stack are kept, so the
 * delimiters of the next content reuse it. Never fails and never allocates.
 */
void amark_delimiters_reset(amark_delimiters_t *delims);

/*
 * Returns the index in kinds of the kind whose character is ch, or
 * kind_count if there is none.
 */
size_t amark_delimiters_kind_of(const amark_delimiters_t *delims,
                                unsigned char ch);

/*
 * Decides whether a run of delimiters of kind, with the character before at
 * its left and the character after at its right, can open and can close
 * emphasis (CommonMark 0.31.2, section 6.2). before and after are code
 * points; the start and the end of a line's content are passed as U+000A,
 * and an ill-formed sequence and U+0000 as U+FFFD. White space and
 * punctuation are those of amark_unicode_is_whitespace and
 * amark_unicode_is_punctuation.
 *
 * Without the intraword limit, a run can open if it is left-flanking and
 * can close if it is right-flanking. With it, a run can open if it is
 * left-flanking and either not right-flanking or preceded by punctuation,
 * and can close if it is right-flanking and either not left-flanking or
 * followed by punctuation.
 */
void amark_delimiter_classify(const amark_delimiter_kind_t *kind,
                              uint32_t before, uint32_t after,
                              bool *can_open, bool *can_close);

/*
 * Pushes the run whose text item is item, of the kind at index kind in
 * kinds, onto the top of the stack. length is the original length of the
 * run: the length of the item's range when it is pushed. A run that can
 * neither open nor close need not be pushed. On failure (AMARK_ERROR_NOMEM,
 * or AMARK_ERROR_LIMIT_EXCEEDED when the number of elements has reached
 * UINT32_MAX), the stack is unchanged.
 */
amark_error_t amark_delimiters_push(amark_delimiters_t *delims,
                                    amark_draft_id_t item,
                                    size_t kind, bool can_open,
                                    bool can_close, size_t length);

/*
 * Matches the delimiters on the stack whose items start at bottom or later
 * (the stack bottom of CommonMark 0.31.2, appendix "process emphasis"),
 * makes emphasis of them in draft, and takes all of them off the stack. The
 * elements below are left as they are.
 *
 * Every closer, from the bottom up, looks down the stack for the nearest
 * opener of its kind. A pair is skipped when either run can both open and
 * close, the sum of the original lengths is a multiple of 3 and the closer's
 * original length is not (rules 9 and 10). The pair uses two delimiters on
 * each side (the kind's pair) when both runs have two or more left, and one
 * (the kind's single) otherwise. The items between the opener's item and the
 * closer's item are wrapped in a new item whose range is
 * [opener's end - used, closer's start + used); the opener's end and the
 * closer's start then shrink by the number used, and an item left empty is
 * removed from draft and from the stack. The delimiters between the pair
 * are taken off the stack.
 *
 * A closer without an opener sets the lower bound of the search for its
 * kind, whether it can open, and its original length modulo 3, so later
 * searches of the same class do not look below it; the bound is a position
 * on the stack, so taking elements off does not loosen it. A closer that
 * cannot open is then taken off the stack.
 *
 * The items of the delimiters on the stack must be children of the root of
 * draft. On failure (amark_draft_wrap's AMARK_ERROR_NOMEM or
 * AMARK_ERROR_LIMIT_EXCEEDED) returns at once; the draft is still a tree
 * and the stack still a list, but the content is to be abandoned.
 */
amark_error_t amark_delimiters_process(amark_delimiters_t *delims,
                                       amark_draft_t *draft,
                                       size_t bottom);

#endif /* AMARK_DELIMITER_H */
