/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#include "unicode.h"

/* The types of the tables in unicode_tables.h, which defines no types. */

/* An inclusive range of code points. */
typedef struct amark_unicode_range {
    uint32_t first;
    uint32_t last;  /* at least first; this value is in the range */
} amark_unicode_range_t;

/* A C (common) mapping of CaseFolding.txt: one code point to one. */
typedef struct amark_unicode_fold_simple {
    uint32_t from;
    uint32_t to;
} amark_unicode_fold_simple_t;

/* An F (full) mapping of CaseFolding.txt: one code point to two or three. */
typedef struct amark_unicode_fold_full {
    uint32_t from;
    uint32_t to[3];  /* to[2] is 0 when the mapping has two code points */
} amark_unicode_fold_full_t;

#include "unicode_tables.h"

/*
 * Returns true if codepoint is in one of the count ranges, which are sorted
 * by first and do not overlap. A loop (no recursion) halves the search.
 */
static bool in_ranges(const amark_unicode_range_t *ranges, size_t count,
                      uint32_t codepoint)
{
    size_t low = 0;
    size_t high = count;  /* the ranges left to search are [low, high) */

    while (low < high) {
        size_t mid = low + (high - low) / 2;

        if (codepoint < ranges[mid].first) {
            high = mid;
        } else if (codepoint > ranges[mid].last) {
            low = mid + 1;
        } else {
            return true;
        }
    }
    return false;
}

bool amark_unicode_is_punctuation(uint32_t codepoint)
{
    return in_ranges(amark_unicode_punctuation,
                     AMARK_UNICODE_PUNCTUATION_COUNT, codepoint);
}

bool amark_unicode_is_whitespace(uint32_t codepoint)
{
    /* Tab, LF, FF and CR come from CommonMark, not from the UCD (they are
       Cc), so they are not in the table. */
    if (codepoint == 0x09 || codepoint == 0x0A || codepoint == 0x0C ||
        codepoint == 0x0D) {
        return true;
    }
    return in_ranges(amark_unicode_space, AMARK_UNICODE_SPACE_COUNT,
                     codepoint);
}

size_t amark_unicode_fold(uint32_t codepoint,
                          uint32_t out[AMARK_UNICODE_FOLD_MAX])
{
    size_t low = 0;
    size_t high = AMARK_UNICODE_FOLD_SIMPLE_COUNT;

    while (low < high) {
        size_t mid = low + (high - low) / 2;
        uint32_t from = amark_unicode_fold_simple_table[mid].from;

        if (codepoint < from) {
            high = mid;
        } else if (codepoint > from) {
            low = mid + 1;
        } else {
            out[0] = amark_unicode_fold_simple_table[mid].to;
            return 1;
        }
    }

    low = 0;
    high = AMARK_UNICODE_FOLD_FULL_COUNT;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        const amark_unicode_fold_full_t *entry =
            &amark_unicode_fold_full_table[mid];

        if (codepoint < entry->from) {
            high = mid;
        } else if (codepoint > entry->from) {
            low = mid + 1;
        } else {
            out[0] = entry->to[0];
            out[1] = entry->to[1];
            if (entry->to[2] == 0) {
                return 2;
            }
            out[2] = entry->to[2];
            return 3;
        }
    }

    out[0] = codepoint;
    return 1;
}
