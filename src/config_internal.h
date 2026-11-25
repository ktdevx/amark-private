/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_CONFIG_INTERNAL_H
#define AMARK_CONFIG_INTERNAL_H

#include <stddef.h>

#include <amark/allocator.h>
#include <amark/config.h>
#include <amark/error.h>

/*
 * A configuration checked against a content length, with its defaults
 * filled in. The parser reads this instead of amark_config_t.
 */
typedef struct amark_resolved_config {
    const amark_dialect_t *dialect; /* NULL: CommonMark only; never a
                                       dialect of no extension */
    size_t start;
    size_t end;                  /* never AMARK_END_OF_CONTENT */
    amark_allocator_t allocator; /* both functions set */
    const char *filename;        /* NULL: no file name */
    amark_utf8_mode_t utf8_mode;
    size_t max_input_size;    /* 0: no limit */
    size_t max_nesting_depth; /* 0: no limit */
    size_t max_node_count;    /* 0: no limit */
} amark_resolved_config_t;

/*
 * Checks config against content_length and, if it is valid, stores the
 * resolved configuration in *out and returns AMARK_ERROR_NONE. A NULL config
 * is treated as the defaults of AMARK_CONFIG_INIT. The rules are checked in
 * this order, and the first one broken returns AMARK_ERROR_INVALID_CONFIG
 * with *out unchanged:
 *
 *   1. struct_size equals sizeof(amark_config_t) and abi equals
 *      AMARK_CONFIG_ABI.
 *   2. allocator.malloc_fn and allocator.free_fn are both NULL or both set.
 *   3. With end AMARK_END_OF_CONTENT read as content_length,
 *      start <= end and end <= content_length.
 *   4. utf8_mode is AMARK_UTF8_STRICT or AMARK_UTF8_REPLACE.
 *   5. dialect is NULL or passes amark_dialect_validate (dialect_internal.h).
 *
 * On success, end is the resolved end, an allocator with both functions
 * NULL becomes the default allocator (its ctx is ignored), and a dialect
 * with extension_count 0 becomes NULL (no dialect). Calls no rule of the
 * dialect, allocates nothing and changes neither *config nor the dialect.
 * out must not be NULL.
 */
amark_error_t amark_config_resolve(const amark_config_t *config,
                                   size_t content_length,
                                   amark_resolved_config_t *out);

#endif /* AMARK_CONFIG_INTERNAL_H */
