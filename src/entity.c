/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#include "entity.h"

#include <stdint.h>
#include <string.h>

/* The type of the table in entity_table.h, which defines no types. */

/* An entity: its name in amark_entity_names and its UTF-8 value. */
typedef struct amark_entity_entry {
    uint16_t name_offset;   /* the position of the name in amark_entity_names */
    uint8_t name_length;    /* at least 1 */
    uint8_t utf8_length;    /* 1 to 6 */
    unsigned char utf8[6];  /* unused bytes are 0 */
} amark_entity_entry_t;

#include "entity_table.h"

/*
 * Compares the name_length bytes at name with the name of entry in the
 * order of the table: byte by byte (as unsigned char) up to the shorter
 * length, then the shorter name first. Returns <0, 0 or >0 as memcmp does.
 */
static int compare_name(const unsigned char *name, size_t name_length,
                        const amark_entity_entry_t *entry)
{
    size_t entry_length = entry->name_length;
    size_t common = name_length < entry_length ? name_length : entry_length;
    int result = memcmp(name, &amark_entity_names[entry->name_offset], common);

    if (result != 0) {
        return result;
    }
    if (name_length < entry_length) {
        return -1;
    }
    return name_length > entry_length ? 1 : 0;
}

bool amark_entity_lookup(const unsigned char *name, size_t name_length,
                         const unsigned char **utf8, size_t *utf8_length)
{
    size_t low = 0;
    size_t high = AMARK_ENTITY_COUNT;  /* the entries left are [low, high) */

    if (name_length == 0) {
        return false;
    }

    /* A loop (no recursion) halves the search. */
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        const amark_entity_entry_t *entry = &amark_entity_entries[mid];
        int result = compare_name(name, name_length, entry);

        if (result < 0) {
            high = mid;
        } else if (result > 0) {
            low = mid + 1;
        } else {
            *utf8 = entry->utf8;
            *utf8_length = entry->utf8_length;
            return true;
        }
    }
    return false;
}
