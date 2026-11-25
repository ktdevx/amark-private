/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_DIALECT_REPLY_H
#define AMARK_DIALECT_REPLY_H

#include <stdbool.h>
#include <stddef.h>

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

#endif /* AMARK_DIALECT_REPLY_H */
