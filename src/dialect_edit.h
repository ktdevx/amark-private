/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_DIALECT_EDIT_H
#define AMARK_DIALECT_EDIT_H

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
};

/*
 * Sets *editor up for the postprocessing rule of extension number
 * `extension` of run->dialect, editing run->doc, with the depth and node
 * limits of the parse (0: no limit) and no lasting failure.
 */
void amark_dialect_editor_init(amark_dialect_editor_t *editor,
                               amark_dialect_run_t *run, size_t extension,
                               size_t max_depth, size_t max_nodes);

#endif /* AMARK_DIALECT_EDIT_H */
