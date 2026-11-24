/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_INLINE_H
#define AMARK_INLINE_H

#include <amark/error.h>

#include "arena.h"
#include "config_internal.h"
#include "document.h"

/*
 * The inline parser: parses the inline content of every paragraph and
 * heading (the list amark_inline_content_at gives, in document order) and
 * adds the inline nodes as their children.
 *
 * For each content of nonzero length, the draft is emptied, the content is
 * scanned from the start to the end with a cursor (src/cursor.h) that reads
 * only the content of the lines, and the items are then written to the
 * document with amark_draft_emit. A byte that may start an inline structure
 * (a special byte) is looked up in a table and handled in one function, the
 * place where the rules of each structure, and those of a dialect, are
 * called. Everything else, up to the next special byte or the end of the
 * line's content, is text. At the end of a line's content that is not the
 * end of the content, the white space (spaces and tabs) before the line
 * ending is left out of the text and goes into a line break, from the first
 * byte of that white space (or the line ending) to right after the line
 * ending: a hard break if two or more spaces come right before the line
 * ending, otherwise a soft break. The prefix and the leading white space of
 * the next line belong to no item.
 *
 * Preconditions: amark_block_parse succeeded on doc with config; scratch is
 * an initialized arena that the caller releases. Of config, only
 * max_nesting_depth and max_node_count are read (not the dialect). The
 * state of the parse lives in a local structure; nothing is kept between
 * calls. No recursion.
 *
 * On success, every paragraph and heading holds its inline nodes and the
 * tree passes amark_tree_check; the block nodes are unchanged. On failure,
 * returns AMARK_ERROR_NOMEM or AMARK_ERROR_LIMIT_EXCEEDED (the depth or node
 * count limit, or a size that cannot be represented); the tree may then hold
 * part of the inline nodes, and doc is to be destroyed.
 */
amark_error_t amark_inline_parse(amark_document_t *doc,
                                 const amark_resolved_config_t *config,
                                 amark_arena_t *scratch);

#endif /* AMARK_INLINE_H */
