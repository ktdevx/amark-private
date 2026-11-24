/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#include "delimiter.h"

#include <string.h>

#include "unicode.h"

/* The kinds of CommonMark, copied into every amark_delimiters_t. */
static const amark_delimiter_kind_t commonmark_kinds[] = {
    {'*', false, AMARK_KIND_EMPHASIS, AMARK_KIND_STRONG},
    {'_', true, AMARK_KIND_EMPHASIS, AMARK_KIND_STRONG}
};

#define COMMONMARK_KIND_COUNT \
    (sizeof(commonmark_kinds) / sizeof(commonmark_kinds[0]))

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
