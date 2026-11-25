/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_DIALECT_REPLY_H
#define AMARK_DIALECT_REPLY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <amark/dialect.h>
#include <amark/error.h>
#include <amark/tree.h>

#include "array.h"
#include "dialect_internal.h"

/*
 * The reply of a close rule (DialectReply). The block parser begins a reply
 * before it calls a close rule, the rule fills it in with the public
 * functions amark_dialect_reply_add_block, _set_data and _set_inline, and
 * the parser finishes it after the rule returns.
 *
 * Each request is checked when it is made, in a time that does not depend
 * on the size of the reply (a line is found with the line index). The
 * first invalid request is remembered and makes the whole reply invalid:
 * later requests return AMARK_ERROR_INVALID_INPUT and change nothing. A
 * failed allocation is remembered too (the lasting failure): later requests
 * return it, and so does amark_dialect_reply_finish.
 *
 * The reply lives in the run's arena (run->reply) and is reused by every
 * reply of the run: amark_dialect_reply_begin empties it and keeps its
 * capacity, so its memory is set by the largest reply.
 */

/* What is being closed. */
typedef enum amark_dialect_reply_mode {
    AMARK_DIALECT_REPLY_PARAGRAPH, /* a paragraph; the reply starts empty */
    AMARK_DIALECT_REPLY_LEAF,      /* a dialect leaf; block 0 is itself */
    AMARK_DIALECT_REPLY_CONTAINER  /* a dialect container; block 0 is
                                      itself */
} amark_dialect_reply_mode_t;

/*
 * A block of a reply. The members are visible in src/ but other modules
 * use the functions only.
 */
typedef struct amark_dialect_reply_block {
    amark_kind_t kind;     /* AMARK_KIND_PARAGRAPH for a paragraph part */
    int cls;               /* an amark_dialect_class_t value; 0 for a
                              paragraph part */
    size_t parent;         /* AMARK_DIALECT_TOP or an earlier block */
    size_t level;          /* depth in the reply; 0 at the top */
    amark_range_t range;
    size_t data_offset;    /* the attributes, in the reply's bytes */
    size_t data_size;      /* 0: none */
    bool has_inline;       /* an inline content was set */
    amark_range_t inline_range;
    size_t child_end;      /* end of the last child added (start of the
                              range while there is none) */
    bool has_children;
    amark_node_id_t node;  /* the node made by amark_dialect_reply_apply */
} amark_dialect_reply_block_t;

/*
 * The reply. The members are visible in src/ but other modules use the
 * functions only.
 */
struct amark_dialect_reply {
    amark_dialect_run_t *run;
    const amark_dialect_extension_t *ext; /* the extension of the rule */
    size_t extension;                     /* its number in the dialect */
    amark_dialect_reply_mode_t mode;
    amark_range_t range;      /* the block being closed */
    size_t content_length;
    size_t child_end;         /* end of the last top block added */
    amark_array_t blocks;     /* amark_dialect_reply_block_t */
    amark_array_t bytes;      /* unsigned char: the attributes */
    bool rejected;            /* an invalid request was made */
    amark_dialect_reason_t reason; /* why the first one was invalid */
    amark_error_t failure;    /* the lasting failure, or AMARK_ERROR_NONE */
};

/*
 * Begins a reply of extension number `extension` of run->dialect for the
 * block of kind over range being closed, and stores it in *out. The reply
 * of run is made in the run's arena on the first call and emptied on each
 * later one. With AMARK_DIALECT_REPLY_LEAF or _CONTAINER, block 0 is the
 * closing block itself (kind, range, no parent); kind is then a block kind
 * of the extension of that class. With AMARK_DIALECT_REPLY_PARAGRAPH, kind
 * is AMARK_KIND_PARAGRAPH and the reply has no block.
 *
 * range is inside the parsed range of run->doc, whose line records the
 * block parser has written for range's lines.
 *
 * On failure, returns AMARK_ERROR_NOMEM and stores NULL in *out.
 */
amark_error_t amark_dialect_reply_begin(amark_dialect_run_t *run,
                                        size_t extension,
                                        amark_dialect_reply_mode_t mode,
                                        amark_kind_t kind,
                                        amark_range_t range,
                                        amark_dialect_reply_t **out);

/*
 * Records the status the close rule returned, before
 * amark_dialect_reply_finish: a status other than AMARK_ERROR_NONE rejects
 * the reply (STATUS, 10.4) unless an earlier request was invalid, whose
 * reason is kept. The caller ends the parse on AMARK_ERROR_NOMEM instead of
 * calling this.
 */
void amark_dialect_reply_status(amark_dialect_reply_t *reply,
                                amark_error_t status);

/*
 * Checks reply after the close rule returned and stores in *valid whether
 * it may be applied:
 *   - With a lasting failure, stores false and returns that failure.
 *   - With an invalid request, adds the rejection diagnostic of the first
 *     one (hook BLOCK_CLOSE, at the start of the closing block) with
 *     amark_dialect_reject, stores false and returns what it returned.
 *   - A paragraph reply with no block means "keep it as it is": stores
 *     false and adds nothing.
 *   - Otherwise stores true.
 */
amark_error_t amark_dialect_reply_finish(amark_dialect_run_t *run,
                                         amark_dialect_reply_t *reply,
                                         bool *valid);

/*
 * Applies reply, which amark_dialect_reply_finish found valid, to run->doc
 * in one pass over its blocks, in the order they were added (a parent comes
 * before its children, siblings in the order of the content):
 *   - Each block becomes a node, the last child of the node of its parent
 *     block, or of parent for a block without one: a dialect kind as a
 *     block (AMARK_NODE_CLASS_BLOCK), a paragraph part as a paragraph.
 *     Block 0 of a container reply is not made: it is existing, the node
 *     the block parser made when the container opened.
 *   - The attributes of a block are copied into the document (2.5, 12.2).
 *   - The inline content of a leaf is added to the inline contents. A
 *     paragraph part gets the inline content of a paragraph: its range
 *     without the spaces and tabs at its end (6.7).
 *
 * parent is a node at depth parent_depth (the root is at depth 0); a block
 * at level n of the reply is at depth parent_depth + 1 + n. Before adding
 * anything, checks that no new node is deeper than max_depth and that the
 * document will have at most max_nodes nodes (0: no limit), and returns
 * AMARK_ERROR_LIMIT_EXCEEDED if not (10.11). A failed allocation returns
 * AMARK_ERROR_NOMEM; the parse then fails, so the nodes already added are
 * not removed.
 */
amark_error_t amark_dialect_reply_apply(amark_dialect_run_t *run,
                                        amark_dialect_reply_t *reply,
                                        amark_node_id_t parent,
                                        size_t parent_depth,
                                        amark_node_id_t existing,
                                        size_t max_depth, size_t max_nodes);

/* ---------------------------------------------------------------------- */
/* The lines of a closing block or of an inline content */

/*
 * The lines a rule reads with amark_dialect_closing_line or
 * amark_dialect_inline_next_line: those of a range of doc, as the lines of
 * a node over that range (amark_document_range_lines). The line records of
 * the document are the ones the block parser wrote.
 */
struct amark_dialect_lines {
    const amark_document_t *doc;
    size_t first_line; /* the line of the start of the range */
    size_t line_count; /* lines from first_line to the line of its end */
};

/*
 * Sets *lines to the lines of range of doc, which has parse data;
 * range.start <= range.end <= the content length.
 */
void amark_dialect_lines_init(struct amark_dialect_lines *lines,
                              const amark_document_t *doc,
                              amark_range_t range);

/*
 * Fills in *closing, the input of a close rule, for the block of kind over
 * range of doc with data (0 for a paragraph), and *lines, the lines of that
 * range, which closing refers to; both stay valid while doc does and lines
 * is not changed.
 */
void amark_dialect_closing_init(amark_dialect_closing_t *closing,
                                struct amark_dialect_lines *lines,
                                const amark_document_t *doc,
                                amark_kind_t kind, amark_range_t range,
                                uint64_t data);

#endif /* AMARK_DIALECT_REPLY_H */
