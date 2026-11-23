/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/* The error type of Amark, shared by the public API and the internals. */

#ifndef AMARK_ERROR_H
#define AMARK_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The kinds of failure that Amark's functions return. A function that can
 * fail returns one of these; AMARK_ERROR_NONE means success. The values are
 * fixed and do not change between versions of the library.
 */
typedef enum amark_error {
    AMARK_ERROR_NONE = 0,
    AMARK_ERROR_INVALID_INPUT = 1,  /* the input violates a constraint */
    AMARK_ERROR_INVALID_CONFIG = 2, /* the configuration is invalid */
    AMARK_ERROR_LIMIT_EXCEEDED = 3, /* a resource limit was exceeded,
                                       including a size computation that
                                       overflowed */
    AMARK_ERROR_NOMEM = 4           /* the allocator failed */
} amark_error_t;

#ifdef __cplusplus
}
#endif

#endif /* AMARK_ERROR_H */
