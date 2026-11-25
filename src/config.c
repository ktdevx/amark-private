/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#include "config_internal.h"

#include "alloc.h"
#include "dialect_internal.h"

#include <stdbool.h>

amark_error_t amark_config_init(amark_config_t *config, size_t struct_size,
                                unsigned abi)
{
    /* Write nothing into a struct of a size or version this library does
     * not know: it may be smaller than amark_config_t. */
    if (config == NULL || struct_size != sizeof(amark_config_t) ||
        abi != AMARK_CONFIG_ABI) {
        return AMARK_ERROR_INVALID_CONFIG;
    }
    config->struct_size = struct_size;
    config->abi = abi;
    config->dialect = NULL;
    config->start = 0;
    config->end = AMARK_END_OF_CONTENT;
    config->allocator.malloc_fn = NULL;
    config->allocator.free_fn = NULL;
    config->allocator.ctx = NULL;
    config->filename = NULL;
    config->utf8_mode = AMARK_UTF8_STRICT;
    config->max_input_size = 0;
    config->max_nesting_depth = 0;
    config->max_node_count = 0;
    return AMARK_ERROR_NONE;
}

amark_error_t amark_config_resolve(const amark_config_t *config,
                                   size_t content_length,
                                   amark_resolved_config_t *out)
{
    amark_config_t defaults;
    bool has_malloc;
    bool has_free;
    size_t end;

    if (config == NULL) {
        (void)AMARK_CONFIG_INIT(&defaults);
        config = &defaults;
    }

    /* Rule 1: the caller's struct is the size and version this library
     * knows. */
    if (config->struct_size != sizeof(amark_config_t) ||
        config->abi != AMARK_CONFIG_ABI) {
        return AMARK_ERROR_INVALID_CONFIG;
    }

    /* Rule 2: both allocator functions, or neither. */
    has_malloc = config->allocator.malloc_fn != NULL;
    has_free = config->allocator.free_fn != NULL;
    if (has_malloc != has_free) {
        return AMARK_ERROR_INVALID_CONFIG;
    }

    /* Rule 3: start <= end <= content_length. */
    end = config->end;
    if (end == AMARK_END_OF_CONTENT) {
        end = content_length;
    }
    if (config->start > end || end > content_length) {
        return AMARK_ERROR_INVALID_CONFIG;
    }

    /* Rule 4: a defined UTF-8 mode. */
    if (config->utf8_mode != AMARK_UTF8_STRICT &&
        config->utf8_mode != AMARK_UTF8_REPLACE) {
        return AMARK_ERROR_INVALID_CONFIG;
    }

    /* Rule 5: a valid dialect definition. */
    if (config->dialect != NULL && !amark_dialect_validate(config->dialect)) {
        return AMARK_ERROR_INVALID_CONFIG;
    }

    /* A dialect of no extension is no dialect. */
    out->dialect = config->dialect;
    if (out->dialect != NULL && out->dialect->extension_count == 0) {
        out->dialect = NULL;
    }
    out->start = config->start;
    out->end = end;
    out->allocator = has_malloc ? config->allocator
                                : amark_allocator_default();
    out->filename = config->filename;
    out->utf8_mode = (amark_utf8_mode_t)config->utf8_mode;
    out->max_input_size = config->max_input_size;
    out->max_nesting_depth = config->max_nesting_depth;
    out->max_node_count = config->max_node_count;
    return AMARK_ERROR_NONE;
}

amark_error_t amark_config_validate(const amark_config_t *config,
                                    size_t content_length)
{
    amark_resolved_config_t resolved;

    return amark_config_resolve(config, content_length, &resolved);
}
