/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#include "utf8.h"

#include "alloc.h"

#include <string.h>

/* Returns 1 if b is a continuation byte (80..BF). */
static int is_continuation(unsigned char b)
{
    return b >= 0x80 && b <= 0xBF;
}

size_t amark_utf8_inspect(const unsigned char *bytes, size_t length,
                          bool *well_formed)
{
    unsigned char lead = bytes[0];
    unsigned char low = 0x80;  /* the range of the second byte (Table 3-7) */
    unsigned char high = 0xBF;
    size_t size;               /* the length of the sequence the lead starts */
    size_t i;

    if (lead <= 0x7F) {
        *well_formed = true;
        return 1;
    }
    if (lead >= 0xC2 && lead <= 0xDF) {
        size = 2;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        size = 3;
        if (lead == 0xE0) {
            low = 0xA0;
        } else if (lead == 0xED) {
            high = 0x9F;
        }
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        size = 4;
        if (lead == 0xF0) {
            low = 0x90;
        } else if (lead == 0xF4) {
            high = 0x8F;
        }
    } else {
        /* 80..BF (no lead byte), C0, C1 and F5..FF. */
        *well_formed = false;
        return 1;
    }

    /*
     * Each byte read so far can begin a well-formed sequence, so on the first
     * byte that does not fit (or at the end of the bytes), the maximal subpart
     * is everything before it.
     */
    if (length < 2 || bytes[1] < low || bytes[1] > high) {
        *well_formed = false;
        return 1;
    }
    for (i = 2; i < size; i++) {
        if (i >= length || !is_continuation(bytes[i])) {
            *well_formed = false;
            return i;
        }
    }
    *well_formed = true;
    return size;
}

bool amark_utf8_validate(const unsigned char *bytes, size_t length,
                         size_t *error_offset)
{
    size_t i = 0;
    bool well_formed;
    size_t size;

    while (i < length) {
        size = amark_utf8_inspect(bytes + i, length - i, &well_formed);
        if (!well_formed) {
            if (error_offset != NULL) {
                *error_offset = i;
            }
            return false;
        }
        i += size;
    }
    return true;
}

size_t amark_utf8_decode(const unsigned char *bytes, size_t length,
                         uint32_t *codepoint)
{
    unsigned char lead = bytes[0];
    uint32_t value;
    size_t size;
    size_t i;

    if (lead < 0x80) {
        *codepoint = lead;
        return 1;
    }
    if (lead < 0xE0) {
        size = 2;
        value = lead & 0x1Fu;
    } else if (lead < 0xF0) {
        size = 3;
        value = lead & 0x0Fu;
    } else {
        size = 4;
        value = lead & 0x07u;
    }
    /* i < length keeps the reads inside the range even if it is too short. */
    for (i = 1; i < size && i < length; i++) {
        value = (value << 6) | (uint32_t)(bytes[i] & 0x3Fu);
    }
    *codepoint = value;
    return size;
}

size_t amark_utf8_decode_lossy(const unsigned char *bytes, size_t length,
                               uint32_t *codepoint)
{
    bool well_formed;
    size_t size = amark_utf8_inspect(bytes, length, &well_formed);

    if (well_formed) {
        /* bytes[0..size) is one well-formed sequence, as decode requires. */
        return amark_utf8_decode(bytes, size, codepoint);
    }
    *codepoint = 0xFFFDu;
    return size;
}

amark_error_t amark_utf8_replace(amark_arena_t *arena,
                                 const unsigned char *bytes, size_t length,
                                 amark_utf8_replaced_t *out)
{
    /* U+FFFD in UTF-8. */
    static const unsigned char replacement[3] = {0xEF, 0xBF, 0xBD};
    size_t result_length = 0;
    size_t count = 0;
    size_t i;
    size_t o;
    size_t size;
    bool well_formed;
    unsigned char *result;
    void *block;
    amark_error_t err;

    /* Pass 1: the result length and the number of replacements. */
    for (i = 0; i < length; i += size) {
        size = amark_utf8_inspect(bytes + i, length - i, &well_formed);
        if (well_formed) {
            if (!amark_size_add(result_length, size, &result_length)) {
                return AMARK_ERROR_LIMIT_EXCEEDED;
            }
        } else {
            if (!amark_size_add(result_length, sizeof(replacement),
                                &result_length)) {
                return AMARK_ERROR_LIMIT_EXCEEDED;
            }
            count++; /* at most length, so it cannot overflow */
        }
    }

    if (count == 0) {
        out->bytes = bytes;
        out->length = length;
        out->replacement_count = 0;
        return AMARK_ERROR_NONE;
    }

    err = amark_arena_alloc(arena, result_length, &block);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    result = (unsigned char *)block;

    /* Pass 2: one U+FFFD per maximal subpart; well-formed parts as they are. */
    o = 0;
    for (i = 0; i < length; i += size) {
        size = amark_utf8_inspect(bytes + i, length - i, &well_formed);
        if (well_formed) {
            memcpy(result + o, bytes + i, size);
            o += size;
        } else {
            memcpy(result + o, replacement, sizeof(replacement));
            o += sizeof(replacement);
        }
    }

    out->bytes = result;
    out->length = result_length;
    out->replacement_count = count;
    return AMARK_ERROR_NONE;
}
