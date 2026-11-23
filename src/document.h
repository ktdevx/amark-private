/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_DOCUMENT_H
#define AMARK_DOCUMENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <amark/allocator.h>
#include <amark/error.h>
#include <amark/tree.h>

#include "arena.h"
#include "array.h"
#include "line_index.h"

/*
 * The document: the owner of the result of one parse (the tree, its
 * attributes and its diagnostics). The body is one block from the allocator;
 * everything else (nodes, attributes, diagnostics, the line index and the
 * copies of strings) comes from the document's arena.
 *
 * The layouts below are visible in src/, but the modules of other specs use
 * the functions only. One document must not be changed from several threads
 * at the same time; a document that is no longer changed may be read from
 * several threads at the same time.
 */

/*
 * A node. Its ID is its index in the node array; element 0 is not used and
 * element 1 is the root.
 */
typedef struct amark_node {
    amark_kind_t kind;   /* AMARK_KIND_NONE: unused element or removed node */
    uint32_t is_inline;  /* 0: block, 1: inline */
    amark_node_id_t parent;
    amark_node_id_t first_child;
    amark_node_id_t last_child;
    amark_node_id_t prev_sibling;
    amark_node_id_t next_sibling;
    uint32_t attr;       /* index in the attribute array; 0: no attributes */
    size_t start;
    size_t end;
    size_t start_line;
    size_t end_line;
} amark_node_t;

/* A range that may be absent (present false: not written in the source). */
typedef struct amark_opt_range {
    size_t start;
    size_t end;
    bool present;
} amark_opt_range_t;

/* The attributes of a node. The kind of the node selects the member. */
typedef union amark_node_attr {
    struct {
        unsigned level;
        amark_heading_style_t style;
    } heading;
    struct {
        amark_list_type_t type;
        amark_list_spacing_t spacing;
        char marker;
        uint32_t start_number;
    } list;
    struct {
        amark_code_block_style_t style;
        char fence_char;
        size_t fence_length;
        amark_opt_range_t info;
    } code_block;
    struct {
        amark_link_form_t form;
        amark_opt_range_t destination;
        amark_opt_range_t title;
        amark_node_id_t definition;
    } link;
    struct {
        amark_opt_range_t label;
        amark_opt_range_t destination;
        amark_opt_range_t title;
    } definition;
    struct {
        amark_autolink_type_t type;
    } autolink;
    struct {
        const void *data;
        size_t size;
    } dialect;
} amark_node_attr_t;

/* A diagnostic as the document stores it. */
typedef struct amark_stored_diagnostic {
    size_t offset;
    size_t line;
    amark_severity_t severity;
    const char *message; /* a copy in the arena */
} amark_stored_diagnostic_t;

struct amark_document {
    amark_allocator_t allocator;  /* the allocator the body came from */
    amark_arena_t arena;
    const unsigned char *content; /* not owned */
    size_t content_length;
    amark_line_index_t lines;     /* of the whole content */
    amark_array_t nodes;          /* amark_node_t; 0 unused, 1 is the root */
    amark_array_t attrs;          /* amark_node_attr_t; 0 unused */
    amark_array_t diagnostics;    /* amark_stored_diagnostic_t */
    const char *filename;         /* a copy in the arena, or NULL */
};

/*
 * Creates a document whose only node is the root (ID 1, AMARK_KIND_DOCUMENT,
 * range [start, end), lines derived from the range as
 * amark_document_range_lines does). The line index covers the whole content
 * and filename is copied into the document. The body is allocated once from
 * *allocator; everything else comes from the document's arena.
 *
 * allocator must have both functions set. start <= end <= content_length.
 * content may be NULL when content_length is 0; it is neither changed nor
 * owned, and must stay valid until the document is destroyed. filename is
 * NULL or a NUL-terminated string. out must not be NULL.
 *
 * On failure, releases everything it allocated, stores NULL in *out and
 * returns AMARK_ERROR_NOMEM (the allocator failed) or
 * AMARK_ERROR_LIMIT_EXCEEDED (a size computation overflowed).
 */
amark_error_t amark_document_create(const amark_allocator_t *allocator,
                                    const unsigned char *content,
                                    size_t content_length,
                                    size_t start, size_t end,
                                    const char *filename,
                                    amark_document_t **out);

/* Return the content and its length as given to amark_document_create. */
const unsigned char *amark_document_content(const amark_document_t *doc);
size_t amark_document_content_length(const amark_document_t *doc);

/* Returns the line index of the whole content. */
const amark_line_index_t *amark_document_line_index(const amark_document_t *doc);

/*
 * Returns the number of nodes created so far, the root and removed nodes
 * included.
 */
size_t amark_document_node_count(const amark_document_t *doc);

/*
 * Derives the lines of the range [start, end) of the content of doc: the
 * start line is the line that contains start, and the end line is the line
 * that contains the last byte (end - 1), or the start line when start ==
 * end. start <= end <= the content length. This is the one place where the
 * rule lives; every operation that sets a range uses it.
 */
void amark_document_range_lines(const amark_document_t *doc, size_t start,
                                size_t end, size_t *start_line,
                                size_t *end_line);

#endif /* AMARK_DOCUMENT_H */
