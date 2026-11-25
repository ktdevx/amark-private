/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_DIALECT_INTERNAL_H
#define AMARK_DIALECT_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>

#include <amark/allocator.h>
#include <amark/dialect.h>
#include <amark/error.h>
#include <amark/tree.h>

#include "arena.h"

/*
 * The definition of a dialect, as the library reads it. The public header
 * <amark/dialect.h> has the same name, so this one is dialect_internal.h.
 */

/*
 * Returns true if *dialect is a valid definition. The rules are checked in
 * this order, each over every extension, and the first one broken returns
 * false:
 *
 *   1. struct_size equals sizeof(amark_dialect_t) and abi equals
 *      AMARK_DIALECT_ABI.
 *   2. With extension_count > 0, extensions and each of its elements are
 *      not NULL. In each extension, an array with a count other than 0 is
 *      not NULL.
 *   3. The name of each extension is not NULL and not empty.
 *   4. Each kind: id >= AMARK_KIND_DIALECT_MIN; cls and blank are defined
 *      values; an inline kind has no continue_fn and no close_fn; no id is
 *      registered twice in the whole dialect.
 *   5. Each block start rule: fn is not NULL and kind is a block kind of
 *      its extension.
 *   6. Each inline rule: fn is not NULL, trigger is 0x21 to 0x7E and kind
 *      is an inline kind of its extension.
 *   7. The dialect has at most AMARK_DIALECT_DELIMITER_MAX delimiter kinds.
 *      Each one: ch is ASCII punctuation, not a CommonMark trigger
 *      (\ ` & < * _ [ ] !), and neither the ch of another delimiter kind nor
 *      the trigger of an inline rule of the dialect; lengths is not 0 and
 *      has no bit above the lowest AMARK_DIALECT_DELIMITER_LENGTH_MAX; kind
 *      is an inline kind of its extension.
 *   8. state_new and state_free of each extension are both set or both
 *      NULL.
 *
 * A NULL dialect is not valid. Calls no rule, allocates nothing, changes
 * nothing, and returns the same result for the same definition.
 */
bool amark_dialect_validate(const amark_dialect_t *dialect);

/*
 * Returns the kind id registered by ext, searching its kinds in order, or
 * NULL if ext does not register it. ext must have passed
 * amark_dialect_validate as part of a dialect.
 */
const amark_dialect_kind_t *amark_dialect_find_kind(
    const amark_dialect_extension_t *ext, amark_kind_t id);

/*
 * Returns the kind id registered by any extension of dialect, or NULL.
 * dialect must have passed amark_dialect_validate.
 */
const amark_dialect_kind_t *amark_dialect_find_kind_any(
    const amark_dialect_t *dialect, amark_kind_t id);

/* ---------------------------------------------------------------------- */
/* The run state of one parse */

/* The closing reply buffer, made by DialectReply. */
struct amark_dialect_reply;

/*
 * The state of one parse with a dialect. It lives on the stack of the parse
 * entry, from amark_dialect_run_begin to amark_dialect_run_end, and is
 * passed to the parsers; a parse without a dialect has no run (NULL).
 */
typedef struct amark_dialect_run {
    const amark_dialect_t *dialect;    /* has at least one extension */
    const amark_allocator_t *allocator; /* the parse's allocator */
    amark_document_t *doc;
    void **states;      /* one per extension, from arena; NULL for an
                           extension without state functions */
    size_t states_made; /* extensions, from the first, whose state_new was
                           called and succeeded (or that have none) */
    amark_arena_t arena; /* the state table and the reply buffer */
    struct amark_dialect_reply *reply; /* made by DialectReply; NULL until
                                          used */
} amark_dialect_run_t;

/*
 * Initializes *run for one parse of doc with dialect, which has passed
 * amark_dialect_validate: takes the state table from the run's arena (on
 * allocator) and calls state_new of each extension that has one, in the
 * order of the extensions, passing allocator (12.3, 12.5). A state_new that
 * returns AMARK_ERROR_NONE without writing *state leaves the state NULL.
 *
 * If the table cannot be allocated or a state_new returns anything other
 * than AMARK_ERROR_NONE, calls state_free on the states already made, in the
 * order they were made, releases the arena and returns AMARK_ERROR_NOMEM
 * (12.6); amark_dialect_run_end must not be called then. Otherwise returns
 * AMARK_ERROR_NONE.
 *
 * allocator and doc must stay valid until amark_dialect_run_end.
 */
amark_error_t amark_dialect_run_begin(amark_dialect_run_t *run,
                                      const amark_dialect_t *dialect,
                                      const amark_allocator_t *allocator,
                                      amark_document_t *doc);

/*
 * Calls state_free once for each state that amark_dialect_run_begin made, in
 * the order they were made, and releases the run's arena (12.4). Calling it
 * again on the same run frees nothing.
 */
void amark_dialect_run_end(amark_dialect_run_t *run);

/* ---------------------------------------------------------------------- */
/* The rejection diagnostic */

/* The hook whose result was rejected. */
typedef enum amark_dialect_hook {
    AMARK_DIALECT_HOOK_BLOCK_START,
    AMARK_DIALECT_HOOK_BLOCK_CONTINUE,
    AMARK_DIALECT_HOOK_BLOCK_CLOSE,
    AMARK_DIALECT_HOOK_INLINE,
    AMARK_DIALECT_HOOK_POSTPROCESS
} amark_dialect_hook_t;

/* Why a result was rejected. The comment is the text of the diagnostic. */
typedef enum amark_dialect_reason {
    AMARK_DIALECT_REASON_STATUS,    /* unknown return value */
    AMARK_DIALECT_REASON_ORIGIN,    /* range does not start at the position */
    AMARK_DIALECT_REASON_LINE,      /* range leaves the line */
    AMARK_DIALECT_REASON_EMPTY,     /* empty range */
    AMARK_DIALECT_REASON_BOUNDS,    /* range leaves the content */
    AMARK_DIALECT_REASON_KIND,      /* kind not registered by the extension */
    AMARK_DIALECT_REASON_NESTING,   /* node not allowed at this place */
    AMARK_DIALECT_REASON_INDEX,     /* parent is not an earlier block */
    AMARK_DIALECT_REASON_PARENT,    /* range leaves the parent */
    AMARK_DIALECT_REASON_ORDER,     /* siblings overlap or are out of order */
    AMARK_DIALECT_REASON_INLINE,    /* inline content not allowed here */
    AMARK_DIALECT_REASON_PREFIX,    /* range ends inside a line prefix */
    AMARK_DIALECT_REASON_PARAGRAPH, /* paragraph part does not cover whole
                                       lines */
    AMARK_DIALECT_REASON_NODE,      /* not a node of this document */
    AMARK_DIALECT_REASON_PROTECTED, /* node cannot be changed */
    AMARK_DIALECT_REASON_TEXT       /* not a text node, or a bad split
                                       position */
} amark_dialect_reason_t;

/* The longest extension name a diagnostic copies, in bytes. */
#define AMARK_DIALECT_REJECT_NAME_MAX 64

/*
 * Adds to run->doc a diagnostic of severity error at offset (11.1, 11.2,
 * 11.3) whose message is
 *
 *   dialect "<name>": <hook>: <reason>
 *
 * with the name of extension number `extension` of run->dialect (its first
 * AMARK_DIALECT_REJECT_NAME_MAX bytes) and the texts of hook and reason
 * (11.4). The message is built on the stack; the only allocation is the one
 * of amark_document_add_diagnostic, whose error is returned (11.7).
 *
 * extension < the number of extensions, and offset <= the content length.
 */
amark_error_t amark_dialect_reject(amark_dialect_run_t *run, size_t extension,
                                   amark_dialect_hook_t hook,
                                   amark_dialect_reason_t reason,
                                   size_t offset);

#endif /* AMARK_DIALECT_INTERNAL_H */
