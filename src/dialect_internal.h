/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_DIALECT_INTERNAL_H
#define AMARK_DIALECT_INTERNAL_H

#include <stdbool.h>

#include <amark/dialect.h>
#include <amark/tree.h>

/*
 * The definition of a dialect, as the library reads it. The public header
 * <amark/dialect.h> has the same name, so this one is dialect_internal.h.
 */

/*
 * Returns true if *dialect is a valid definition. The rules are checked in
 * this order, each over every extension, and the first one broken returns
 * false:
 *
 *   1. struct_size equals sizeof(amark_dialect_t) and abi equals
 *      AMARK_DIALECT_ABI.
 *   2. With extension_count > 0, extensions and each of its elements are
 *      not NULL. In each extension, an array with a count other than 0 is
 *      not NULL.
 *   3. The name of each extension is not NULL and not empty.
 *   4. Each kind: id >= AMARK_KIND_DIALECT_MIN; cls and blank are defined
 *      values; an inline kind has no continue_fn and no close_fn; no id is
 *      registered twice in the whole dialect.
 *   5. Each block start rule: fn is not NULL and kind is a block kind of
 *      its extension.
 *   6. Each inline rule: fn is not NULL, trigger is 0x21 to 0x7E and kind
 *      is an inline kind of its extension.
 *   7. The dialect has at most AMARK_DIALECT_DELIMITER_MAX delimiter kinds.
 *      Each one: ch is ASCII punctuation, not a CommonMark trigger
 *      (\ ` & < * _ [ ] !), and neither the ch of another delimiter kind nor
 *      the trigger of an inline rule of the dialect; lengths is not 0 and
 *      has no bit above the lowest AMARK_DIALECT_DELIMITER_LENGTH_MAX; kind
 *      is an inline kind of its extension.
 *   8. state_new and state_free of each extension are both set or both
 *      NULL.
 *
 * A NULL dialect is not valid. Calls no rule, allocates nothing, changes
 * nothing, and returns the same result for the same definition.
 */
bool amark_dialect_validate(const amark_dialect_t *dialect);

/*
 * Returns the kind id registered by ext, searching its kinds in order, or
 * NULL if ext does not register it. ext must have passed
 * amark_dialect_validate as part of a dialect.
 */
const amark_dialect_kind_t *amark_dialect_find_kind(
    const amark_dialect_extension_t *ext, amark_kind_t id);

/*
 * Returns the kind id registered by any extension of dialect, or NULL.
 * dialect must have passed amark_dialect_validate.
 */
const amark_dialect_kind_t *amark_dialect_find_kind_any(
    const amark_dialect_t *dialect, amark_kind_t id);

#endif /* AMARK_DIALECT_INTERNAL_H */
