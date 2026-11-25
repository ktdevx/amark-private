/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_DIALECT_EDIT_H
#define AMARK_DIALECT_EDIT_H

#include <stdbool.h>
#include <stddef.h>

#include <amark/dialect.h>
#include <amark/error.h>

#include "dialect_internal.h"

/*
 * Postprocessing (DialectEdit): the editor that a postprocessing rule
 * changes the tree with, through the public functions amark_dialect_edit_*.
 *
 * Each edit is checked before it is applied, reading only the target node
 * and the nodes linked to it (its parent, its previous and next siblings,
 * its first and last children), so a check takes a time that does not
 * depend on the size of the tree (10.9). An edit that passes is applied at
 * once and leaves a tree that keeps the invariants of the tree check. An
 * invalid edit adds a rejection diagnostic (hook POSTPROCESS, at the start
 * of the target node, or at the start of the root when the node is not a
 * node of the document), changes nothing and returns
 * AMARK_ERROR_INVALID_INPUT (9.4, 9.7); later edits are checked as usual.
 *
 * A failure that the parse cannot go on from (a failed allocation, a limit
 * exceeded) is remembered (the lasting failure): the edit and every later
 * one return it and change nothing. The caller ends the parse with it once
 * the rule returns.
 *
 * There is no edit that removes a node or reorders siblings (9.8).
 */

/*
 * The editor of one call of a postprocessing rule. It lives on the stack of
 * the caller. The members are visible in src/ but other modules use the
 * functions only.
 */
struct amark_dialect_editor {
    amark_dialect_run_t *run;
    const amark_dialect_extension_t *ext; /* the extension of the rule */
    size_t extension;                     /* its number in the dialect */
    size_t max_depth;                     /* 0: no limit */
    size_t max_nodes;                     /* 0: no limit */
    amark_error_t failure; /* the lasting failure, or AMARK_ERROR_NONE */
    bool rejected;         /* an edit was rejected with a diagnostic */
};

/*
 * Sets *editor up for the postprocessing rule of extension number
 * `extension` of run->dialect, editing run->doc, with the depth and node
 * limits of the parse (0: no limit), no lasting failure and no rejected
 * edit.
 */
void amark_dialect_editor_init(amark_dialect_editor_t *editor,
                               amark_dialect_run_t *run, size_t extension,
                               size_t max_depth, size_t max_nodes);

/*
 * Calls the postprocessing rule of each extension of run->dialect that has
 * one, in the order of the extensions, once each (9.1, 9.2). It is called
 * once per document, after the inline parse. Each rule gets the document
 * (run->doc, read with the functions of tree.h) and its content, its
 * extension's per-document state, and an editor set up for its extension
 * with the limits max_depth and max_nodes (0: no limit), on this function's
 * stack.
 *
 * After a rule returns:
 *   - if the editor has a lasting failure, returns it (10.10, 10.11);
 *   - if the rule returned AMARK_ERROR_NOMEM, returns it (10.10);
 *   - if it returned AMARK_ERROR_INVALID_INPUT after one of its edits was
 *     rejected, adds nothing more: the rule passed on what an edit
 *     returned, and that edit's diagnostic is the only one (11.1);
 *   - if it returned any other value than AMARK_ERROR_NONE, adds a
 *     rejection diagnostic (hook POSTPROCESS, reason STATUS, at the start
 *     of the root) and goes on with the next extension (10.4); the edits
 *     the rule made stay (they each passed their checks, so the tree keeps
 *     its invariants: 9.9, 10.5). A failure to add the diagnostic is
 *     returned.
 * Returns AMARK_ERROR_NONE when every rule ran.
 */
amark_error_t amark_dialect_postprocess(amark_dialect_run_t *run,
                                        size_t max_depth, size_t max_nodes);

#endif /* AMARK_DIALECT_EDIT_H */
