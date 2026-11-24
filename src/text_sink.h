/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_TEXT_SINK_H
#define AMARK_TEXT_SINK_H

#include <stddef.h>

/*
 * The output of the text extraction: counts the length of the result while
 * writing as much of it as fits in a buffer, in one pass.
 *
 * Every function writes buffer[length] only while length is less than
 * capacity, advances length whether it wrote or not, and never writes at or
 * past buffer[capacity]. A NULL buffer counts with capacity 0. Nothing here
 * allocates, checks length for overflow (the caller bounds the result before
 * writing) or fails.
 */

typedef struct amark_text_sink {
    unsigned char *buffer; /* may be NULL (capacity is then 0) */
    size_t capacity;
    size_t length;         /* bytes emitted so far, including any not written */
} amark_text_sink_t;

/*
 * Starts an empty output into the capacity bytes at buffer. If buffer is
 * NULL, capacity is taken as 0 and the output is only counted.
 */
void amark_text_sink_init(amark_text_sink_t *sink, unsigned char *buffer,
                          size_t capacity);

/* Emits one byte unchanged (a space, '\n' or an ASCII punctuation mark). */
void amark_text_sink_put_byte(amark_text_sink_t *sink, unsigned char byte);

/*
 * Emits the count bytes at bytes unchanged. They must already be
 * well-formed UTF-8 without U+0000 (the value of a character reference).
 */
void amark_text_sink_put_bytes(amark_text_sink_t *sink,
                               const unsigned char *bytes, size_t count);

/*
 * Emits the count bytes at bytes, taken from the source, one character at a
 * time as amark_utf8_decode_lossy splits them (reading only within count):
 * a well-formed character other than U+0000 is emitted unchanged, while
 * U+0000 and each maximal subpart of an ill-formed sequence become one
 * U+FFFD (EF BF BD). Nothing else is replaced, so characters outside ASCII,
 * control characters and U+FEFF come out as written.
 */
void amark_text_sink_put_source(amark_text_sink_t *sink,
                                const unsigned char *bytes, size_t count);

#endif /* AMARK_TEXT_SINK_H */
