/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/* The entry point of Amark's public API. */

#ifndef AMARK_AMARK_H
#define AMARK_AMARK_H

#include <amark/error.h>   /* amark_error_t */
#include <amark/export.h>  /* AMARK_API (generated) */
#include <amark/version.h> /* AMARK_VERSION_* (generated) */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Returns the version of the library as it was built, in the form "0.1.0".
 * The returned string is a static constant; do not free or modify it.
 * It is equal to AMARK_VERSION_STRING when the header matches the library.
 */
AMARK_API const char *amark_version_string(void);

#ifdef __cplusplus
}
#endif

#endif /* AMARK_AMARK_H */
