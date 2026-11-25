/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/* The parse configuration of Amark: its type, defaults and validation. */

#ifndef AMARK_CONFIG_H
#define AMARK_CONFIG_H

#include <stddef.h>

#include <amark/allocator.h> /* amark_allocator_t */
#include <amark/error.h>     /* amark_error_t */
#include <amark/export.h>    /* AMARK_API (generated) */

#ifdef __cplusplus
extern "C" {
#endif

/* A dialect (syntax extension). Its contents are defined elsewhere. */
typedef struct amark_dialect amark_dialect_t;

/* How a parse treats invalid UTF-8 in the content. */
typedef enum amark_utf8_mode {
    AMARK_UTF8_STRICT = 0, /* invalid UTF-8 is an error */
    AMARK_UTF8_REPLACE = 1 /* replace it with U+FFFD and continue */
} amark_utf8_mode_t;

/*
 * The value of amark_config_t.end that means "to the end of the content",
 * whatever its length.
 */
#define AMARK_END_OF_CONTENT ((size_t)-1)

/*
 * The configuration of a parse. Set it up with amark_config_init and then
 * change only the members you need. It has no member that changes the
 * syntax rules.
 */
typedef struct amark_config {
    size_t struct_size;             /* written by amark_config_init */
    const amark_dialect_t *dialect; /* NULL: CommonMark only */
    size_t start;                   /* the range to parse is [start, end) */
    size_t end;                     /* AMARK_END_OF_CONTENT: to the end */
    amark_allocator_t allocator;    /* both functions NULL: malloc and free */
    const char *filename;           /* NULL: no file name */
    int utf8_mode;                  /* an amark_utf8_mode_t value */
    size_t max_input_size;          /* 0: no limit */
    size_t max_nesting_depth;       /* 0: no limit */
    size_t max_node_count;          /* 0: no limit */
} amark_config_t;

/*
 * Sets *config to the defaults: struct_size is sizeof(amark_config_t), end
 * is AMARK_END_OF_CONTENT, and every other member is 0 or NULL (no dialect,
 * the whole content, malloc and free, AMARK_UTF8_STRICT, no limits). Does
 * nothing if config is NULL.
 */
AMARK_API void amark_config_init(amark_config_t *config);

/*
 * Returns AMARK_ERROR_NONE if *config is valid for content of
 * content_length bytes, and AMARK_ERROR_INVALID_CONFIG otherwise: when
 * struct_size is not sizeof(amark_config_t), when only one of the two
 * allocator functions is set, when start > end or end > content_length
 * (after AMARK_END_OF_CONTENT is read as content_length), when utf8_mode
 * is not an amark_utf8_mode_t value, or when dialect is not NULL and its
 * definition is invalid (see <amark/dialect.h>; for example a struct_size
 * or abi this library does not know, a NULL array with a count other than
 * 0, an extension without a name, a kind id outside the dialect space or
 * registered twice, a rule or delimiter kind naming a kind its extension
 * did not register or of the wrong class, an invalid delimiter character or
 * length set, too many delimiter kinds, blank_columns out of range or set
 * on a kind that is not a container going on over blank lines, or only one
 * of the two state functions). A dialect with no extension is valid and
 * parses as no dialect. amark_parse makes the same check before it parses
 * and returns the same error. The resource limits are not compared with the
 * content. A NULL config is treated as the defaults. Calls no rule of the dialect,
 * allocates nothing, and changes neither *config nor the dialect.
 */
AMARK_API amark_error_t amark_config_validate(const amark_config_t *config,
                                              size_t content_length);

#ifdef __cplusplus
}
#endif

#endif /* AMARK_CONFIG_H */
