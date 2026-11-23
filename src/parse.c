/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * The entry point of a parse (include/amark/parse.h): checks the arguments,
 * the configuration, the input size and the UTF-8 of the parsed range in a
 * fixed order without allocating, then creates the document, attaches its
 * parse data and runs the block parser with a scratch arena. Owns the
 * lifetimes of the document and the scratch arena; knows no block rules.
 */

#include <amark/parse.h>

#include <stddef.h>

#include "arena.h"
#include "block.h"
#include "config_internal.h"
#include "document.h"
#include "parse_data.h"
#include "utf8.h"

/*
 * Runs the steps that allocate on a document made for the parsed range.
 * On failure the document is destroyed and *out is set to NULL.
 */
static amark_error_t parse_document(const unsigned char *bytes, size_t length,
                                    const amark_resolved_config_t *config,
                                    amark_document_t **out)
{
    amark_document_t *doc = NULL;
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
    if (err == AMARK_ERROR_NONE) {
        amark_arena_init(&scratch, &config->allocator);
        err = amark_block_parse(doc, config, &scratch);
        amark_arena_deinit(&scratch);
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
