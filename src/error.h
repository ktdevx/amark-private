/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_ERROR_H
#define AMARK_ERROR_H

/*
 * The kinds of failure that the internal components return. A function that
 * can fail returns one of these; AMARK_ERROR_NONE means success.
 */
typedef enum amark_error {
    AMARK_ERROR_NONE = 0,
    AMARK_ERROR_NOMEM,   /* the allocator failed */
    AMARK_ERROR_OVERFLOW /* a size computation overflowed */
} amark_error_t;

#endif /* AMARK_ERROR_H */
