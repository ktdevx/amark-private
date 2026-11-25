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
 * The version of the layout of amark_config_t this header describes. Store
 * it in amark_config_t.abi, together with sizeof(amark_config_t) in
 * struct_size; AMARK_CONFIG_INIT does both. It is raised whenever a member
 * is added. 0 is never a version.
 */
#define AMARK_CONFIG_ABI 1u

/*
 * The configuration of a parse. Set it up with AMARK_CONFIG_INIT and then
 * change only the members you need. It has no member that changes the
 * syntax rules.
 */
typedef struct amark_config {
    size_t struct_size;             /* sizeof(amark_config_t) */
    unsigned abi;                   /* AMARK_CONFIG_ABI */
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
 * Sets *config to the defaults and returns AMARK_ERROR_NONE when struct_size
 * is sizeof(amark_config_t) and abi is AMARK_CONFIG_ABI as this library knows
 * them. The defaults are struct_size and abi as given, end
 * AMARK_END_OF_CONTENT, and every other member 0 or NULL (no dialect, the
 * whole content, malloc and free, AMARK_UTF8_STRICT, no limits). Returns
 * AMARK_ERROR_INVALID_CONFIG and writes nothing when config is NULL or when
 * struct_size or abi is another value, for example because the program was
 * compiled with the header of another version of this library.
 *
 * Call it through AMARK_CONFIG_INIT, which passes the values of the header
 * the program is compiled with. Call it directly only where the macro cannot
 * be used, such as from another language.
 */
AMARK_API amark_error_t amark_config_init(amark_config_t *config,
                                          size_t struct_size, unsigned abi);

/*
 * Sets *config to the defaults with amark_config_init, passing the size and
 * the version of amark_config_t of this header. Evaluates config once and
 * gives the result of amark_config_init.
 */
#define AMARK_CONFIG_INIT(config) \
    amark_config_init((config), sizeof(amark_config_t), AMARK_CONFIG_ABI)

/*
 * Returns AMARK_ERROR_NONE if *config is valid for content of
 * content_length bytes, and AMARK_ERROR_INVALID_CONFIG otherwise: when
 * struct_size is not sizeof(amark_config_t) or abi is not
 * AMARK_CONFIG_ABI, when only one of the two
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
