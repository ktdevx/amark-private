/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#include "dialect_internal.h"

#include <stdbool.h>
#include <stddef.h>

/* ---------------------------------------------------------------------- */
/* Kind lookup */

const amark_dialect_kind_t *amark_dialect_find_kind(
    const amark_dialect_extension_t *ext, amark_kind_t id)
{
    size_t i;

    if (ext == NULL || ext->kinds == NULL) {
        return NULL;
    }
    for (i = 0; i < ext->kind_count; i++) {
        if (ext->kinds[i].id == id) {
            return &ext->kinds[i];
        }
    }
    return NULL;
}

const amark_dialect_kind_t *amark_dialect_find_kind_any(
    const amark_dialect_t *dialect, amark_kind_t id)
{
    const amark_dialect_kind_t *kind;
    size_t i;

    if (dialect == NULL || dialect->extensions == NULL) {
        return NULL;
    }
    for (i = 0; i < dialect->extension_count; i++) {
        kind = amark_dialect_find_kind(dialect->extensions[i], id);
        if (kind != NULL) {
            return kind;
        }
    }
    return NULL;
}

/* ---------------------------------------------------------------------- */
/* Validation */

/* The bits of amark_dialect_delimiter_t.lengths that may be set. */
#define LENGTHS_MASK ((1u << AMARK_DIALECT_DELIMITER_LENGTH_MAX) - 1u)

static bool is_block_class(int cls)
{
    return cls == AMARK_DIALECT_CONTAINER || cls == AMARK_DIALECT_LEAF;
}

/* True if ext registers id as a block kind (block is true) or an inline
 * kind (block is false). */
static bool has_kind_of_class(const amark_dialect_extension_t *ext,
                              amark_kind_t id, bool block)
{
    const amark_dialect_kind_t *kind = amark_dialect_find_kind(ext, id);

    if (kind == NULL) {
        return false;
    }
    return block ? is_block_class(kind->cls)
                 : kind->cls == AMARK_DIALECT_INLINE;
}

/* ASCII punctuation, as CommonMark defines it. */
static bool is_ascii_punctuation(unsigned char c)
{
    return (c >= 0x21 && c <= 0x2F) || (c >= 0x3A && c <= 0x40) ||
           (c >= 0x5B && c <= 0x60) || (c >= 0x7B && c <= 0x7E);
}

/* A character that starts a CommonMark inline construct. */
static bool is_commonmark_trigger(unsigned char c)
{
    switch (c) {
    case '\\':
    case '`':
    case '&':
    case '<':
    case '*':
    case '_':
    case '[':
    case ']':
    case '!':
        return true;
    default:
        return false;
    }
}

/* Rule 2: the arrays. */
static bool arrays_are_valid(const amark_dialect_t *dialect)
{
    const amark_dialect_extension_t *ext;
    size_t i;

    if (dialect->extension_count > 0 && dialect->extensions == NULL) {
        return false;
    }
    for (i = 0; i < dialect->extension_count; i++) {
        ext = dialect->extensions[i];
        if (ext == NULL) {
            return false;
        }
        if ((ext->kind_count > 0 && ext->kinds == NULL) ||
            (ext->block_start_count > 0 && ext->block_starts == NULL) ||
            (ext->inline_rule_count > 0 && ext->inline_rules == NULL) ||
            (ext->delimiter_count > 0 && ext->delimiters == NULL)) {
            return false;
        }
    }
    return true;
}

/* Rule 3: the names. */
static bool names_are_valid(const amark_dialect_t *dialect)
{
    const char *name;
    size_t i;

    for (i = 0; i < dialect->extension_count; i++) {
        name = dialect->extensions[i]->name;
        if (name == NULL || name[0] == '\0') {
            return false;
        }
    }
    return true;
}

/* True if id is registered by a kind before kinds[index] of extension e,
 * or by an earlier extension. */
static bool is_registered_before(const amark_dialect_t *dialect, size_t e,
                                 size_t index, amark_kind_t id)
{
    const amark_dialect_extension_t *ext;
    size_t i;
    size_t k;
    size_t end;

    for (i = 0; i <= e; i++) {
        ext = dialect->extensions[i];
        end = i == e ? index : ext->kind_count;
        for (k = 0; k < end; k++) {
            if (ext->kinds[k].id == id) {
                return true;
            }
        }
    }
    return false;
}

/* Rule 4: the kinds. */
static bool kinds_are_valid(const amark_dialect_t *dialect)
{
    const amark_dialect_extension_t *ext;
    const amark_dialect_kind_t *kind;
    size_t i;
    size_t k;

    for (i = 0; i < dialect->extension_count; i++) {
        ext = dialect->extensions[i];
        for (k = 0; k < ext->kind_count; k++) {
            kind = &ext->kinds[k];
            if (kind->id < AMARK_KIND_DIALECT_MIN) {
                return false;
            }
            if (!is_block_class(kind->cls) &&
                kind->cls != AMARK_DIALECT_INLINE) {
                return false;
            }
            if (kind->blank != AMARK_DIALECT_BLANK_CLOSES &&
                kind->blank != AMARK_DIALECT_BLANK_CONTINUES) {
                return false;
            }
            if (kind->cls == AMARK_DIALECT_INLINE &&
                (kind->continue_fn != NULL || kind->close_fn != NULL)) {
                return false;
            }
            if (is_registered_before(dialect, i, k, kind->id)) {
                return false;
            }
        }
    }
    return true;
}

/* Rule 5: the block start rules. */
static bool block_starts_are_valid(const amark_dialect_t *dialect)
{
    const amark_dialect_extension_t *ext;
    const amark_dialect_block_start_t *start;
    size_t i;
    size_t k;

    for (i = 0; i < dialect->extension_count; i++) {
        ext = dialect->extensions[i];
        for (k = 0; k < ext->block_start_count; k++) {
            start = &ext->block_starts[k];
            if (start->fn == NULL ||
                !has_kind_of_class(ext, start->kind, true)) {
                return false;
            }
        }
    }
    return true;
}

/* Rule 6: the inline rules. */
static bool inline_rules_are_valid(const amark_dialect_t *dialect)
{
    const amark_dialect_extension_t *ext;
    const amark_dialect_inline_rule_t *rule;
    size_t i;
    size_t k;

    for (i = 0; i < dialect->extension_count; i++) {
        ext = dialect->extensions[i];
        for (k = 0; k < ext->inline_rule_count; k++) {
            rule = &ext->inline_rules[k];
            if (rule->fn == NULL || rule->trigger < 0x21 ||
                rule->trigger > 0x7E ||
                !has_kind_of_class(ext, rule->kind, false)) {
                return false;
            }
        }
    }
    return true;
}

/*
 * True if c is the trigger of an inline rule of dialect, or the ch of a
 * delimiter kind of dialect other than delimiters[index] of extension e.
 */
static bool is_character_taken(const amark_dialect_t *dialect, size_t e,
                               size_t index, unsigned char c)
{
    const amark_dialect_extension_t *ext;
    size_t i;
    size_t k;

    for (i = 0; i < dialect->extension_count; i++) {
        ext = dialect->extensions[i];
        for (k = 0; k < ext->inline_rule_count; k++) {
            if (ext->inline_rules[k].trigger == c) {
                return true;
            }
        }
        for (k = 0; k < ext->delimiter_count; k++) {
            if ((i != e || k != index) && ext->delimiters[k].ch == c) {
                return true;
            }
        }
    }
    return false;
}

/* Rule 7: the delimiter kinds. */
static bool delimiters_are_valid(const amark_dialect_t *dialect)
{
    const amark_dialect_extension_t *ext;
    const amark_dialect_delimiter_t *delim;
    size_t total = 0;
    size_t i;
    size_t k;

    /* The count first, so that the checks below look at a few delimiters. */
    for (i = 0; i < dialect->extension_count; i++) {
        if (dialect->extensions[i]->delimiter_count >
            AMARK_DIALECT_DELIMITER_MAX - total) {
            return false;
        }
        total += dialect->extensions[i]->delimiter_count;
    }
    for (i = 0; i < dialect->extension_count; i++) {
        ext = dialect->extensions[i];
        for (k = 0; k < ext->delimiter_count; k++) {
            delim = &ext->delimiters[k];
            if (!is_ascii_punctuation(delim->ch) ||
                is_commonmark_trigger(delim->ch) ||
                is_character_taken(dialect, i, k, delim->ch)) {
                return false;
            }
            if (delim->lengths == 0 || (delim->lengths & ~LENGTHS_MASK) != 0) {
                return false;
            }
            if (!has_kind_of_class(ext, delim->kind, false)) {
                return false;
            }
        }
    }
    return true;
}

/* Rule 8: the state functions. */
static bool state_functions_are_valid(const amark_dialect_t *dialect)
{
    const amark_dialect_extension_t *ext;
    size_t i;

    for (i = 0; i < dialect->extension_count; i++) {
        ext = dialect->extensions[i];
        if ((ext->state_new == NULL) != (ext->state_free == NULL)) {
            return false;
        }
    }
    return true;
}

bool amark_dialect_validate(const amark_dialect_t *dialect)
{
    if (dialect == NULL) {
        return false;
    }
    /* Rule 1. */
    if (dialect->struct_size != sizeof(amark_dialect_t) ||
        dialect->abi != AMARK_DIALECT_ABI) {
        return false;
    }
    return arrays_are_valid(dialect) && names_are_valid(dialect) &&
           kinds_are_valid(dialect) && block_starts_are_valid(dialect) &&
           inline_rules_are_valid(dialect) && delimiters_are_valid(dialect) &&
           state_functions_are_valid(dialect);
}
