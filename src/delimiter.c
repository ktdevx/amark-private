/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#include "delimiter.h"

#include <string.h>

#include <amark/dialect.h>

#include "unicode.h"

/* The kinds of CommonMark, copied into every amark_delimiters_t. */
static const amark_delimiter_kind_t commonmark_kinds[] = {
    {'*', false, AMARK_KIND_EMPHASIS, AMARK_KIND_STRONG,
     AMARK_DELIMITER_MATCH_EMPHASIS, 0},
    {'_', true, AMARK_KIND_EMPHASIS, AMARK_KIND_STRONG,
     AMARK_DELIMITER_MATCH_EMPHASIS, 0}
};

#define COMMONMARK_KIND_COUNT \
    (sizeof(commonmark_kinds) / sizeof(commonmark_kinds[0]))

/* The table of kinds holds the kinds of CommonMark and of one dialect. */
typedef char amark_delimiter_kind_max_check
    [AMARK_DELIMITER_KIND_MAX == 2 + AMARK_DIALECT_DELIMITER_MAX ? 1 : -1];

/*
 * An element is named by its index plus 1, a uint32_t, so the array holds at
 * most UINT32_MAX elements.
 */
#define MAX_STACK_LEN ((size_t)UINT32_MAX)

/* Returns the element named id (not 0). */
static amark_delimiter_t *element_at(const amark_delimiters_t *delims,
                                     uint32_t id)
{
    return (amark_delimiter_t *)amark_array_at(&delims->stack,
                                               (size_t)id - 1);
}

void amark_delimiters_init(amark_delimiters_t *delims,
                           amark_arena_t *scratch)
{
    memset(delims->kinds, 0, sizeof(delims->kinds));
    memcpy(delims->kinds, commonmark_kinds, sizeof(commonmark_kinds));
    delims->kind_count = COMMONMARK_KIND_COUNT;
    amark_array_init(&delims->stack, scratch, sizeof(amark_delimiter_t));
    delims->top = 0;
}

bool amark_delimiters_add_kind(amark_delimiters_t *delims,
                               const amark_delimiter_kind_t *kind)
{
    if (delims->kind_count >= AMARK_DELIMITER_KIND_MAX) {
        return false;
    }
    delims->kinds[delims->kind_count] = *kind;
    delims->kind_count++;
    return true;
}

void amark_delimiters_reset(amark_delimiters_t *delims)
{
    amark_array_clear(&delims->stack);
    delims->top = 0;
}

size_t amark_delimiters_kind_of(const amark_delimiters_t *delims,
                                unsigned char ch)
{
    size_t i;

    for (i = 0; i < delims->kind_count; i++) {
        if (delims->kinds[i].ch == ch) {
            return i;
        }
    }
    return delims->kind_count;
}

void amark_delimiter_classify(const amark_delimiter_kind_t *kind,
                              uint32_t before, uint32_t after,
                              bool *can_open, bool *can_close)
{
    bool before_space = amark_unicode_is_whitespace(before);
    bool before_punct = amark_unicode_is_punctuation(before);
    bool after_space = amark_unicode_is_whitespace(after);
    bool after_punct = amark_unicode_is_punctuation(after);
    /* Left-flanking: not followed by white space, and either not followed
       by punctuation or followed by punctuation and preceded by white
       space or punctuation. Right-flanking is the mirror image. */
    bool left = !after_space &&
                (!after_punct || before_space || before_punct);
    bool right = !before_space &&
                 (!before_punct || after_space || after_punct);

    if (kind->intraword_limited) {
        *can_open = left && (!right || before_punct);
        *can_close = right && (!left || after_punct);
    } else {
        *can_open = left;
        *can_close = right;
    }
}

amark_error_t amark_delimiters_push(amark_delimiters_t *delims,
                                    amark_draft_id_t item,
                                    size_t kind, bool can_open,
                                    bool can_close, size_t length)
{
    amark_delimiter_t elem;
    size_t len = amark_array_len(&delims->stack);
    amark_error_t err;

    if (len >= MAX_STACK_LEN) {
        return AMARK_ERROR_LIMIT_EXCEEDED;
    }
    memset(&elem, 0, sizeof(elem));
    elem.item = item;
    elem.kind = kind;
    elem.can_open = can_open;
    elem.can_close = can_close;
    elem.length = length;
    elem.prev = delims->top;
    elem.next = 0;
    err = amark_array_push(&delims->stack, &elem);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    if (delims->top != 0) {
        element_at(delims, delims->top)->next = (uint32_t)(len + 1);
    }
    delims->top = (uint32_t)(len + 1);
    return AMARK_ERROR_NONE;
}

/* Takes the element id off the stack. Its place is not reused. */
static void take_off(amark_delimiters_t *delims, uint32_t id)
{
    amark_delimiter_t *elem = element_at(delims, id);

    if (elem->prev != 0) {
        element_at(delims, elem->prev)->next = elem->next;
    }
    if (elem->next != 0) {
        element_at(delims, elem->next)->prev = elem->prev;
    } else {
        delims->top = elem->prev;
    }
    elem->prev = 0;
    elem->next = 0;
}

/* Returns the number of delimiters left in the run of the element. */
static size_t current_length(const amark_draft_t *draft,
                             const amark_delimiter_t *elem)
{
    const amark_draft_item_t *item = amark_draft_get(draft, elem->item);

    return item->end - item->start;
}

/*
 * Whether opener and closer may not match by the rule of 3 (rules 9 and
 * 10): either run can both open and close, the sum of the runs' lengths is
 * a multiple of 3, and the closer's is not.
 */
static bool fails_rule_of_3(const amark_delimiter_t *opener,
                            const amark_delimiter_t *closer)
{
    return (opener->can_close || closer->can_open) &&
           closer->length % 3 != 0 &&
           (opener->length + closer->length) % 3 == 0;
}

/*
 * Whether the run of elem may match by the rule of its kind: always for
 * EMPHASIS; for EQUAL, when its length is one of the kind's lengths.
 */
static bool is_usable(const amark_delimiters_t *delims,
                      const amark_delimiter_t *elem)
{
    const amark_delimiter_kind_t *kind = &delims->kinds[elem->kind];

    if (kind->match != AMARK_DELIMITER_MATCH_EQUAL) {
        return true;
    }
    return elem->length >= 1 &&
           elem->length <= AMARK_DIALECT_DELIMITER_LENGTH_MAX &&
           (kind->lengths & AMARK_DIALECT_LENGTH(elem->length)) != 0;
}

/*
 * Whether opener (of the closer's kind, and able to open) and closer may
 * match: for EQUAL, when their lengths are equal; for EMPHASIS, unless the
 * rule of 3 forbids it.
 */
static bool may_match(const amark_delimiters_t *delims,
                      const amark_delimiter_t *opener,
                      const amark_delimiter_t *closer)
{
    if (delims->kinds[closer->kind].match == AMARK_DELIMITER_MATCH_EQUAL) {
        return opener->length == closer->length;
    }
    return !fails_rule_of_3(opener, closer);
}

/*
 * The class of a usable closer in the table of lower bounds: its kind,
 * whether it can open, and for EMPHASIS its original length modulo 3 (6 of
 * the 8 classes), for EQUAL its length, 1 to 4 (all 8).
 */
#define BOUND_CLASSES 8

static size_t bound_class(const amark_delimiters_t *delims,
                          const amark_delimiter_t *closer)
{
    size_t base = closer->kind * BOUND_CLASSES;

    if (delims->kinds[closer->kind].match == AMARK_DELIMITER_MATCH_EQUAL) {
        return base + (closer->can_open ? 4u : 0u) + (closer->length - 1);
    }
    return base + (closer->can_open ? 3u : 0u) + closer->length % 3;
}

/*
 * Matches opener and closer of an EQUAL kind: wraps the items from the
 * opener's to the closer's in one item of the kind's single, ranging from
 * the opener's start to the closer's end, removes the two runs' items, and
 * takes the two runs and the delimiters between off the stack. Stores in
 * *next the element to look at as the closer next.
 */
static amark_error_t match_equal(amark_delimiters_t *delims,
                                 amark_draft_t *draft, uint32_t opener_id,
                                 uint32_t closer_id, uint32_t *next)
{
    amark_delimiter_t *opener = element_at(delims, opener_id);
    amark_delimiter_t *closer = element_at(delims, closer_id);
    amark_draft_id_t wrapped;
    amark_error_t err;

    err = amark_draft_wrap(draft, opener->item, closer->item,
                           delims->kinds[closer->kind].single,
                           amark_draft_get(draft, opener->item)->start,
                           amark_draft_get(draft, closer->item)->end,
                           &wrapped);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    amark_draft_remove(draft, opener->item);
    amark_draft_remove(draft, closer->item);

    /* The delimiters between are off the stack. */
    opener->next = closer_id;
    closer->prev = opener_id;
    *next = closer->next;
    take_off(delims, opener_id);
    take_off(delims, closer_id);
    return AMARK_ERROR_NONE;
}

/*
 * Makes emphasis of opener and closer: wraps the items between, shrinks the
 * two runs, takes the delimiters between off the stack, and removes a run
 * used up. Stores in *next the element to look at as the closer next.
 */
static amark_error_t match(amark_delimiters_t *delims, amark_draft_t *draft,
                           uint32_t opener_id, uint32_t closer_id,
                           uint32_t *next)
{
    amark_delimiter_t *opener = element_at(delims, opener_id);
    amark_delimiter_t *closer = element_at(delims, closer_id);
    const amark_delimiter_kind_t *kind = &delims->kinds[closer->kind];
    size_t used = current_length(draft, opener) >= 2 &&
                          current_length(draft, closer) >= 2
                      ? 2
                      : 1;
    size_t open_start = amark_draft_get(draft, opener->item)->start;
    size_t open_end = amark_draft_get(draft, opener->item)->end;
    size_t close_start = amark_draft_get(draft, closer->item)->start;
    size_t close_end = amark_draft_get(draft, closer->item)->end;
    amark_draft_id_t wrapped;
    amark_error_t err;

    err = amark_draft_wrap(draft, opener->item, closer->item,
                           used == 2 ? kind->pair : kind->single,
                           open_end - used, close_start + used, &wrapped);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    amark_draft_set_range(draft, opener->item, open_start, open_end - used);
    amark_draft_set_range(draft, closer->item, close_start + used,
                          close_end);

    /* The delimiters between are off the stack. */
    opener->next = closer_id;
    closer->prev = opener_id;

    if (open_end - used == open_start) {
        amark_draft_remove(draft, opener->item);
        take_off(delims, opener_id);
    }
    if (close_start + used == close_end) {
        *next = closer->next;
        amark_draft_remove(draft, closer->item);
        take_off(delims, closer_id);
    } else {
        *next = closer_id;
    }
    return AMARK_ERROR_NONE;
}

amark_error_t amark_delimiters_process(amark_delimiters_t *delims,
                                       amark_draft_t *draft,
                                       size_t bottom)
{
    /*
     * The lower bounds: an opener of a closer of class c must be at
     * lower[c] or above. Elements are named in the order they were pushed,
     * so a bound stays right after elements are taken off the stack.
     */
    uint32_t lower[AMARK_DELIMITER_KIND_MAX * BOUND_CLASSES];
    uint32_t stack_bottom = delims->top; /* the element below, or 0 */
    uint32_t closer_id = 0;
    size_t i;

    while (stack_bottom != 0 &&
           amark_draft_get(draft, element_at(delims, stack_bottom)->item)
                   ->start >= bottom) {
        closer_id = stack_bottom;
        stack_bottom = element_at(delims, stack_bottom)->prev;
    }
    for (i = 0; i < sizeof(lower) / sizeof(lower[0]); i++) {
        lower[i] = stack_bottom + 1;
    }

    while (closer_id != 0) {
        amark_delimiter_t *closer = element_at(delims, closer_id);
        size_t cls;
        uint32_t opener_id = 0;
        uint32_t id;

        if (!closer->can_close) {
            closer_id = closer->next;
            continue;
        }
        if (!is_usable(delims, closer)) {
            /* An EQUAL run of a length the kind does not use matches
               nothing; as an opener it matches no usable closer either. */
            uint32_t next = closer->next;

            if (!closer->can_open) {
                take_off(delims, closer_id);
            }
            closer_id = next;
            continue;
        }
        cls = bound_class(delims, closer);
        for (id = closer->prev; id != 0 && id >= lower[cls];
             id = element_at(delims, id)->prev) {
            const amark_delimiter_t *opener = element_at(delims, id);

            if (opener->can_open && opener->kind == closer->kind &&
                may_match(delims, opener, closer)) {
                opener_id = id;
                break;
            }
        }
        if (opener_id != 0) {
            amark_error_t err =
                delims->kinds[closer->kind].match ==
                        AMARK_DELIMITER_MATCH_EQUAL
                    ? match_equal(delims, draft, opener_id, closer_id,
                                  &closer_id)
                    : match(delims, draft, opener_id, closer_id,
                            &closer_id);

            if (err != AMARK_ERROR_NONE) {
                return err;
            }
        } else {
            uint32_t next = closer->next;

            lower[cls] = closer_id;
            if (!closer->can_open) {
                take_off(delims, closer_id);
            }
            closer_id = next;
        }
    }

    /* Every delimiter above the bottom is off the stack. */
    if (stack_bottom != 0) {
        element_at(delims, stack_bottom)->next = 0;
    }
    delims->top = stack_bottom;
    return AMARK_ERROR_NONE;
}
