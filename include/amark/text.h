/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * The text of a node: the source bytes of its range, and strings built from
 * the range (the logical text of a node, its value, the destination, title
 * and info). None of these functions changes the document or the content,
 * so a document that is no longer changed may be read from several threads
 * at the same time.
 */

#ifndef AMARK_TEXT_H
#define AMARK_TEXT_H

#include <stdbool.h>
#include <stddef.h>

#include <amark/error.h>  /* amark_error_t */
#include <amark/export.h> /* AMARK_API (generated) */
#include <amark/tree.h>   /* amark_document_t, amark_node_id_t */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Which part of a node to turn into a string. The values are fixed and do
 * not change between versions of the library.
 */
typedef enum amark_text_part {
    AMARK_TEXT_LOGICAL = 1,     /* the text as if written outside the
                                   containers, at the start of a line */
    AMARK_TEXT_VALUE = 2,       /* the value of a node (text, escape,
                                   character reference, autolink, code span,
                                   code block) */
    AMARK_TEXT_DESTINATION = 3, /* the destination of a link, an image or a
                                   link reference definition */
    AMARK_TEXT_TITLE = 4,       /* the title of the same */
    AMARK_TEXT_INFO = 5         /* the info string of a fenced code block */
} amark_text_part_t;

/*
 * Stores in *bytes the position in the content of the bytes of the range of
 * node and in *length their number, and returns true. The bytes are those
 * of the content, unchanged (prefixes, tabs, line endings, U+0000 and
 * invalid UTF-8 included); nothing is copied or allocated, and the time
 * does not depend on the length of the range. The root gives the whole
 * parsed range. An empty range gives true and *length 0; *bytes may then be
 * NULL if the content is. The bytes stay valid while the document exists
 * and the content is kept.
 *
 * Returns false and leaves *bytes and *length unchanged if doc, bytes or
 * length is NULL, or if node is not a node of doc (AMARK_NODE_NONE, larger
 * than the number of nodes created, or removed while the tree was built).
 */
AMARK_API bool amark_node_raw(const amark_document_t *doc,
                              amark_node_id_t node,
                              const char **bytes, size_t *length);

/*
 * Builds the part of node and writes it to buffer, without a terminating
 * NUL. Stores the length L of the result in *length; returns
 * AMARK_ERROR_NONE (a result of length 0 included) if L <= capacity, or
 * AMARK_ERROR_BUFFER_TOO_SMALL if not (the first capacity bytes of buffer
 * may then have been written; nothing past them is). A NULL buffer is a
 * capacity of 0. Calling again with a buffer of L bytes succeeds. Never
 * allocates.
 *
 * Returns AMARK_ERROR_NOT_AVAILABLE and leaves buffer and *length unchanged
 * if doc or length is NULL, node is not a node of doc, part is not one of
 * the values above, the node has no such part (for example the logical text
 * of a container, or a destination that is not written), or the document
 * was not made by amark_parse. Returns AMARK_ERROR_LIMIT_EXCEEDED, leaving
 * buffer and *length unchanged, if the range read is longer than
 * (SIZE_MAX - 2) / 3 bytes (the result of such a range and its terminating
 * NUL might not fit in a size_t).
 */
AMARK_API amark_error_t amark_node_text(const amark_document_t *doc,
                                        amark_node_id_t node,
                                        amark_text_part_t part,
                                        char *buffer, size_t capacity,
                                        size_t *length);

/*
 * Builds the part of node as amark_node_text does and stores in *out a
 * NUL-terminated string of L + 1 bytes made with the allocator of the
 * document, and in *length (if length is not NULL) its length L. A result
 * of length 0 is a string of one byte. The result never contains U+0000.
 * Free it with amark_text_free before destroying the document; destroying
 * the document does not free it.
 *
 * Returns AMARK_ERROR_NOT_AVAILABLE as amark_node_text does, and also when
 * out is NULL; AMARK_ERROR_NOMEM if the allocator fails; and
 * AMARK_ERROR_LIMIT_EXCEEDED as amark_node_text does. On every result but
 * AMARK_ERROR_NONE, *out and *length are unchanged and nothing allocated is
 * left behind.
 */
AMARK_API amark_error_t amark_node_text_alloc(const amark_document_t *doc,
                                              amark_node_id_t node,
                                              amark_text_part_t part,
                                              char **out, size_t *length);

/*
 * Frees text, a string that amark_node_text_alloc made for doc, with the
 * allocator of doc. Does nothing if doc or text is NULL.
 */
AMARK_API void amark_text_free(const amark_document_t *doc, char *text);

#ifdef __cplusplus
}
#endif

#endif /* AMARK_TEXT_H */
