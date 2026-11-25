/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#include "dialect_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "document.h"

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
            /* blank_columns: containers that go on over blank lines only
               (3.14). */
            if (kind->blank_columns > AMARK_DIALECT_BLANK_COLUMNS_MAX ||
                (kind->blank_columns != 0 &&
                 (kind->cls != AMARK_DIALECT_CONTAINER ||
                  kind->blank != AMARK_DIALECT_BLANK_CONTINUES))) {
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

/* ---------------------------------------------------------------------- */
/* The run state */

/* Frees the states of the first `count` extensions, in order. */
static void free_states(amark_dialect_run_t *run, size_t count)
{
    const amark_dialect_extension_t *ext;
    size_t i;

    for (i = 0; i < count; i++) {
        ext = run->dialect->extensions[i];
        if (ext->state_free != NULL) {
            ext->state_free(run->allocator, run->states[i]);
        }
    }
}

amark_error_t amark_dialect_run_begin(amark_dialect_run_t *run,
                                      const amark_dialect_t *dialect,
                                      const amark_allocator_t *allocator,
                                      amark_document_t *doc)
{
    const amark_dialect_extension_t *ext;
    void *table;
    amark_error_t err;
    size_t count = dialect->extension_count;
    size_t i;

    run->dialect = dialect;
    run->allocator = allocator;
    run->doc = doc;
    run->states = NULL;
    run->states_made = 0;
    run->reply = NULL;
    amark_arena_init(&run->arena, allocator);

    if (count > 0) {
        err = amark_arena_alloc_array(&run->arena, count, sizeof(void *),
                                      &table);
        if (err != AMARK_ERROR_NONE) {
            amark_arena_deinit(&run->arena);
            return AMARK_ERROR_NOMEM;
        }
        run->states = (void **)table;
    }
    for (i = 0; i < count; i++) {
        ext = dialect->extensions[i];
        run->states[i] = NULL;
        if (ext->state_new != NULL &&
            ext->state_new(allocator, &run->states[i]) != AMARK_ERROR_NONE) {
            free_states(run, run->states_made);
            run->states_made = 0;
            run->states = NULL;
            amark_arena_deinit(&run->arena);
            return AMARK_ERROR_NOMEM;
        }
        run->states_made = i + 1;
    }
    return AMARK_ERROR_NONE;
}

void amark_dialect_run_end(amark_dialect_run_t *run)
{
    free_states(run, run->states_made);
    run->states_made = 0;
    run->states = NULL;
    run->reply = NULL;
    amark_arena_deinit(&run->arena);
}

/* ---------------------------------------------------------------------- */
/* The rejection diagnostic */

/* The longest message: the 64-byte name, "block continue" and the
 * PARAGRAPH reason make 134 bytes with the NUL. */
#define REJECT_MESSAGE_SIZE 160

static const char *hook_text(amark_dialect_hook_t hook)
{
    switch (hook) {
    case AMARK_DIALECT_HOOK_BLOCK_START:
        return "block start";
    case AMARK_DIALECT_HOOK_BLOCK_CONTINUE:
        return "block continue";
    case AMARK_DIALECT_HOOK_BLOCK_CLOSE:
        return "block close";
    case AMARK_DIALECT_HOOK_INLINE:
        return "inline";
    case AMARK_DIALECT_HOOK_POSTPROCESS:
        return "postprocess";
    }
    return "unknown hook";
}

static const char *reason_text(amark_dialect_reason_t reason)
{
    switch (reason) {
    case AMARK_DIALECT_REASON_STATUS:
        return "unknown return value";
    case AMARK_DIALECT_REASON_ORIGIN:
        return "range does not start at the position";
    case AMARK_DIALECT_REASON_LINE:
        return "range leaves the line";
    case AMARK_DIALECT_REASON_EMPTY:
        return "empty range";
    case AMARK_DIALECT_REASON_BOUNDS:
        return "range leaves the content";
    case AMARK_DIALECT_REASON_KIND:
        return "kind not registered by the extension";
    case AMARK_DIALECT_REASON_NESTING:
        return "node not allowed at this place";
    case AMARK_DIALECT_REASON_INDEX:
        return "parent is not an earlier block";
    case AMARK_DIALECT_REASON_PARENT:
        return "range leaves the parent";
    case AMARK_DIALECT_REASON_ORDER:
        return "siblings overlap or are out of order";
    case AMARK_DIALECT_REASON_INLINE:
        return "inline content not allowed here";
    case AMARK_DIALECT_REASON_PREFIX:
        return "range ends inside a line prefix";
    case AMARK_DIALECT_REASON_PARAGRAPH:
        return "paragraph part does not cover whole lines";
    case AMARK_DIALECT_REASON_NODE:
        return "not a node of this document";
    case AMARK_DIALECT_REASON_PROTECTED:
        return "node cannot be changed";
    case AMARK_DIALECT_REASON_TEXT:
        return "not a text node, or a bad split position";
    }
    return "unknown reason";
}

/* Appends the first `length` bytes of text to buffer at *used. The caller
 * makes sure they fit. */
static void append(char *buffer, size_t *used, const char *text,
                   size_t length)
{
    memcpy(buffer + *used, text, length);
    *used += length;
}

static void append_string(char *buffer, size_t *used, const char *text)
{
    append(buffer, used, text, strlen(text));
}

amark_error_t amark_dialect_reject(amark_dialect_run_t *run, size_t extension,
                                   amark_dialect_hook_t hook,
                                   amark_dialect_reason_t reason,
                                   size_t offset)
{
    char message[REJECT_MESSAGE_SIZE];
    const char *name = run->dialect->extensions[extension]->name;
    size_t name_length = 0;
    size_t used = 0;

    /* strnlen is not C99. */
    while (name_length < AMARK_DIALECT_REJECT_NAME_MAX &&
           name[name_length] != '\0') {
        name_length++;
    }
    append_string(message, &used, "dialect \"");
    append(message, &used, name, name_length);
    append_string(message, &used, "\": ");
    append_string(message, &used, hook_text(hook));
    append_string(message, &used, ": ");
    append_string(message, &used, reason_text(reason));
    message[used] = '\0';
    return amark_document_add_diagnostic(run->doc, offset,
                                         AMARK_SEVERITY_ERROR, message);
}
