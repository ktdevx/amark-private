/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#include "text_sink.h"

#include <stdint.h>

#include "utf8.h"

/* U+FFFD REPLACEMENT CHARACTER in UTF-8. */
static const unsigned char replacement[3] = { 0xEFu, 0xBFu, 0xBDu };

void amark_text_sink_init(amark_text_sink_t *sink, unsigned char *buffer,
                          size_t capacity)
{
    sink->buffer = buffer;
    sink->capacity = buffer != NULL ? capacity : 0;
    sink->length = 0;
}

void amark_text_sink_put_byte(amark_text_sink_t *sink, unsigned char byte)
{
    if (sink->length < sink->capacity) {
        sink->buffer[sink->length] = byte;
    }
    sink->length++;
}

void amark_text_sink_put_bytes(amark_text_sink_t *sink,
                               const unsigned char *bytes, size_t count)
{
    size_t i;

    for (i = 0; i < count; i++) {
        amark_text_sink_put_byte(sink, bytes[i]);
    }
}

void amark_text_sink_put_source(amark_text_sink_t *sink,
                                const unsigned char *bytes, size_t count)
{
    size_t pos = 0;

    while (pos < count) {
        uint32_t codepoint;
        size_t size =
            amark_utf8_decode_lossy(bytes + pos, count - pos, &codepoint);

        /*
         * decode_lossy gives U+FFFD for a maximal subpart and for a
         * well-formed U+FFFD alike; both come out as EF BF BD.
         */
        if (codepoint == 0 || codepoint == 0xFFFDu) {
            amark_text_sink_put_bytes(sink, replacement, sizeof(replacement));
        } else {
            amark_text_sink_put_bytes(sink, bytes + pos, size);
        }
        pos += size;
    }
}
