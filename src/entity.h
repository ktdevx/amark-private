/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_ENTITY_H
#define AMARK_ENTITY_H

#include <stdbool.h>
#include <stddef.h>

/*
 * HTML5 named character references, for resolving entity references such
 * as "&amp;". The data comes from the HTML Standard's entities.json in
 * tools/data/ through the generated entity_table.h; only the names that end
 * in ';' there are known.
 *
 * The function reads nothing but its arguments and constant tables,
 * allocates nothing, cannot fail, needs no initialization and may be called
 * from any number of threads at once. Each call takes time proportional to
 * the logarithm of the table size.
 */

/*
 * Looks up the name_length bytes at name: an entity name without the
 * leading '&' and the trailing ';'. The name needs no terminating NUL and
 * is compared byte by byte, so case matters ("Auml" and "auml" differ;
 * "Amp" is not found). When name_length is 0, name is not read.
 *
 * If the name is found, sets *utf8 and *utf8_length to the UTF-8 encoding
 * (1 to 6 bytes) of the one or two code points that the entity stands for
 * and returns true. *utf8 points at static read-only data that stays valid
 * for the life of the process; it is not NUL-terminated and is never freed
 * or modified by the caller. If the name is not found (including an empty
 * name and a name longer than any entity name), returns false and leaves
 * *utf8 and *utf8_length unchanged. utf8 and utf8_length must not be NULL.
 */
bool amark_entity_lookup(const unsigned char *name, size_t name_length,
                         const unsigned char **utf8, size_t *utf8_length);

#endif /* AMARK_ENTITY_H */
