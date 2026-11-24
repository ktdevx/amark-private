/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/* The entry point of a parse: from content and a configuration to a
 * document. */

#ifndef AMARK_PARSE_H
#define AMARK_PARSE_H

#include <stddef.h>

#include <amark/config.h> /* amark_config_t */
#include <amark/error.h>  /* amark_error_t */
#include <amark/export.h> /* AMARK_API (generated) */
#include <amark/tree.h>   /* amark_document_t */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Parses the range [config->start, config->end) of the length bytes at
 * content as CommonMark 0.31.2 and stores the document in *out.
 *
 * content may be NULL when length is 0. config is NULL (the defaults of
 * amark_config_init) or a configuration set up with amark_config_init. The
 * document refers to content without copying it: do not change or free
 * content until the document is destroyed with amark_document_destroy.
 * Ranges are byte offsets from the start of content, and lines count from 1
 * at the first line of content, also when the range does not start at 0.
 * Every allocation goes through the configured allocator.
 *
 * Returns AMARK_ERROR_NONE and the document on success: the range of its
 * root is the parsed range. Otherwise returns the first of these that
 * applies, in this order:
 *
 *   1. AMARK_ERROR_INVALID_INPUT: out is NULL.
 *   2. AMARK_ERROR_INVALID_INPUT: content is NULL and length is not 0.
 *   3. AMARK_ERROR_INVALID_CONFIG: amark_config_validate rejects config.
 *   4. AMARK_ERROR_LIMIT_EXCEEDED: max_input_size is not 0 and length is
 *      greater than it.
 *   5. AMARK_ERROR_INVALID_INPUT: utf8_mode is AMARK_UTF8_STRICT and the
 *      parsed range is not well-formed UTF-8 (bytes outside it are not
 *      checked).
 *   6. AMARK_ERROR_NOMEM (the allocator failed) or AMARK_ERROR_LIMIT_EXCEEDED
 *      (max_nesting_depth or max_node_count was exceeded, or a size did not
 *      fit).
 *
 * Nothing is allocated before step 6. With AMARK_UTF8_REPLACE each maximal
 * invalid sequence is read as one U+FFFD and the parse goes on; no copy of
 * the content is made. On failure everything allocated is released and, if
 * out is not NULL, *out is set to NULL. Invalid Markdown is never an error.
 */
AMARK_API amark_error_t amark_parse(const char *content, size_t length,
                                    const amark_config_t *config,
                                    amark_document_t **out);

#ifdef __cplusplus
}
#endif

#endif /* AMARK_PARSE_H */
