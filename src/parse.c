/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * The entry point of a parse (include/amark/parse.h): checks the arguments,
 * the configuration, the input size and the UTF-8 of the parsed range in a
 * fixed order without allocating, then creates the document, attaches its
 * parse data, begins the run state of the dialect (only when there is one),
 * runs the block parser with a scratch arena, and then the inline parser
 * with the scratch arena made again (nothing of the block parse is kept in
 * it), and then, with a dialect, its postprocessing rules. Owns the
 * lifetimes of the document, the run state and the scratch arena; knows no
 * block or inline rules.
 */

#include <amark/parse.h>

#include <stddef.h>

#include "arena.h"
#include "block.h"
#include "config_internal.h"
#include "dialect_edit.h"
#include "dialect_internal.h"
#include "document.h"
#include "inline.h"
#include "parse_data.h"
#include "utf8.h"

/*
 * Runs the steps that allocate on a document made for the parsed range.
 * With a dialect, the run state lives from right after the parse data is
 * made until right before the return, and is ended exactly once on success
 * and on failure (12.4); a failure of amark_dialect_run_begin ends it
 * itself (12.6). On failure the document is destroyed after the run ended
 * and *out is set to NULL (13.7).
 */
static amark_error_t parse_document(const unsigned char *bytes, size_t length,
                                    const amark_resolved_config_t *config,
                                    amark_document_t **out)
{
    amark_document_t *doc = NULL;
    amark_dialect_run_t run_state;
    amark_dialect_run_t *run = NULL;
    amark_arena_t scratch;
    amark_error_t err;

    err = amark_document_create(&config->allocator, bytes, length,
                                config->start, config->end, config->filename,
                                &doc);
    if (err != AMARK_ERROR_NONE) {
        *out = NULL;
        return err;
    }
    err = amark_parse_data_create(doc);
    if (err == AMARK_ERROR_NONE && config->dialect != NULL) {
        err = amark_dialect_run_begin(&run_state, config->dialect,
                                      &config->allocator, doc);
        if (err == AMARK_ERROR_NONE) {
            run = &run_state;
        }
    }
    if (err == AMARK_ERROR_NONE) {
        amark_arena_init(&scratch, &config->allocator);
        err = amark_block_parse(doc, config, &scratch, run);
        amark_arena_deinit(&scratch);
        if (err == AMARK_ERROR_NONE) {
            amark_arena_init(&scratch, &config->allocator);
            err = amark_inline_parse(doc, config, &scratch, run);
            amark_arena_deinit(&scratch);
        }
        if (err == AMARK_ERROR_NONE && run != NULL) {
            err = amark_dialect_postprocess(run, config->max_nesting_depth,
                                            config->max_node_count);
        }
    }
    if (run != NULL) {
        amark_dialect_run_end(run);
    }
    if (err != AMARK_ERROR_NONE) {
        amark_document_destroy(doc);
        *out = NULL;
        return err;
    }
    *out = doc;
    return AMARK_ERROR_NONE;
}

amark_error_t amark_parse(const char *content, size_t length,
                          const amark_config_t *config,
                          amark_document_t **out)
{
    /* The one conversion between char and unsigned char. */
    const unsigned char *bytes = (const unsigned char *)content;
    amark_resolved_config_t resolved;

    if (out == NULL) {
        return AMARK_ERROR_INVALID_INPUT;
    }
    *out = NULL;
    if (content == NULL && length != 0) {
        return AMARK_ERROR_INVALID_INPUT;
    }
    if (amark_config_resolve(config, length, &resolved) != AMARK_ERROR_NONE) {
        return AMARK_ERROR_INVALID_CONFIG;
    }
    if (resolved.max_input_size != 0 && length > resolved.max_input_size) {
        return AMARK_ERROR_LIMIT_EXCEEDED;
    }
    if (resolved.utf8_mode == AMARK_UTF8_STRICT && resolved.end > resolved.start
        && !amark_utf8_validate(bytes + resolved.start,
                                resolved.end - resolved.start, NULL)) {
        return AMARK_ERROR_INVALID_INPUT;
    }
    return parse_document(bytes, length, &resolved, out);
}
