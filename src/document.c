/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#include "document.h"

#include <string.h>

#include "alloc.h"

/*
 * Copies the NUL-terminated string str (with its NUL) into the arena of doc
 * and stores the copy in *out. On failure, *out is unchanged.
 */
static amark_error_t copy_string(amark_document_t *doc, const char *str,
                                 const char **out)
{
    size_t size;
    void *mem = NULL;
    amark_error_t err;

    if (!amark_size_add(strlen(str), 1, &size)) {
        return AMARK_ERROR_LIMIT_EXCEEDED;
    }
    err = amark_arena_alloc(&doc->arena, size, &mem);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    memcpy(mem, str, size);
    *out = (const char *)mem;
    return AMARK_ERROR_NONE;
}

/*
 * Copies filename into the arena of doc and stores the copy in
 * doc->filename. A NULL filename leaves doc->filename NULL.
 */
static amark_error_t copy_filename(amark_document_t *doc, const char *filename)
{
    doc->filename = NULL;
    if (filename == NULL) {
        return AMARK_ERROR_NONE;
    }
    return copy_string(doc, filename, &doc->filename);
}

/*
 * Fills the parts of doc that come from the arena: the line index, the copy
 * of the file name, the unused element 0 of the node and attribute arrays,
 * and the root. On failure, the arena may hold blocks; the caller releases
 * them.
 */
static amark_error_t fill_document(amark_document_t *doc, size_t start,
                                   size_t end, const char *filename)
{
    amark_node_t node;
    amark_node_attr_t attr;
    amark_error_t err;

    err = amark_line_index_build(&doc->lines, &doc->arena, doc->content,
                                 doc->content_length);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    err = copy_filename(doc, filename);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }

    amark_array_init(&doc->nodes, &doc->arena, sizeof(amark_node_t));
    amark_array_init(&doc->attrs, &doc->arena, sizeof(amark_node_attr_t));
    amark_array_init(&doc->diagnostics, &doc->arena,
                     sizeof(amark_stored_diagnostic_t));

    /* Element 0 of each array is not used: index 0 means "none". */
    memset(&node, 0, sizeof(node));
    node.kind = AMARK_KIND_NONE;
    err = amark_array_push(&doc->nodes, &node);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    memset(&attr, 0, sizeof(attr));
    err = amark_array_push(&doc->attrs, &attr);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }

    /* The root: ID 1, a block of kind document over the parsed range. */
    node.kind = AMARK_KIND_DOCUMENT;
    node.is_inline = 0;
    node.parent = AMARK_NODE_NONE;
    node.first_child = AMARK_NODE_NONE;
    node.last_child = AMARK_NODE_NONE;
    node.prev_sibling = AMARK_NODE_NONE;
    node.next_sibling = AMARK_NODE_NONE;
    node.attr = 0;
    node.start = start;
    node.end = end;
    amark_document_range_lines(doc, start, end, &node.start_line,
                               &node.end_line);
    return amark_array_push(&doc->nodes, &node);
}

amark_error_t amark_document_create(const amark_allocator_t *allocator,
                                    const unsigned char *content,
                                    size_t content_length,
                                    size_t start, size_t end,
                                    const char *filename,
                                    amark_document_t **out)
{
    void *mem = NULL;
    amark_document_t *doc;
    amark_error_t err;

    *out = NULL;
    err = amark_malloc(allocator, sizeof(amark_document_t), &mem);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    doc = (amark_document_t *)mem;
    memset(doc, 0, sizeof(*doc));
    doc->allocator = *allocator;
    amark_arena_init(&doc->arena, &doc->allocator);
    doc->content = content;
    doc->content_length = content_length;

    err = fill_document(doc, start, end, filename);
    if (err != AMARK_ERROR_NONE) {
        amark_document_destroy(doc);
        return err;
    }
    *out = doc;
    return AMARK_ERROR_NONE;
}

amark_error_t amark_document_add_diagnostic(amark_document_t *doc,
                                            size_t offset,
                                            amark_severity_t severity,
                                            const char *message)
{
    amark_stored_diagnostic_t diag;
    amark_error_t err;

    diag.offset = offset;
    diag.line = amark_line_index_line_at(&doc->lines, offset);
    diag.severity = severity;
    diag.message = NULL;
    /* Copy first: a failed append then leaves only an unused copy. */
    err = copy_string(doc, message, &diag.message);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    return amark_array_push(&doc->diagnostics, &diag);
}

size_t amark_document_diagnostic_count(const amark_document_t *doc)
{
    if (doc == NULL) {
        return 0;
    }
    return amark_array_len(&doc->diagnostics);
}

bool amark_document_diagnostic(const amark_document_t *doc, size_t index,
                               amark_diagnostic_t *out)
{
    const amark_stored_diagnostic_t *diag;

    if (doc == NULL || out == NULL ||
        index >= amark_array_len(&doc->diagnostics)) {
        return false;
    }
    diag = (const amark_stored_diagnostic_t *)amark_array_at(
        &doc->diagnostics, index);
    out->offset = diag->offset;
    out->line = diag->line;
    out->severity = diag->severity;
    out->message = diag->message;
    out->filename = doc->filename;
    return true;
}

const unsigned char *amark_document_content(const amark_document_t *doc)
{
    return doc->content;
}

size_t amark_document_content_length(const amark_document_t *doc)
{
    return doc->content_length;
}

const amark_line_index_t *amark_document_line_index(const amark_document_t *doc)
{
    return &doc->lines;
}

size_t amark_document_node_count(const amark_document_t *doc)
{
    /* Element 0 of the node array is not a node. */
    return amark_array_len(&doc->nodes) - 1;
}

void amark_document_range_lines(const amark_document_t *doc, size_t start,
                                size_t end, size_t *start_line,
                                size_t *end_line)
{
    *start_line = amark_line_index_line_at(&doc->lines, start);
    if (start < end) {
        *end_line = amark_line_index_line_at(&doc->lines, end - 1);
    } else {
        *end_line = *start_line;
    }
}

void amark_document_destroy(amark_document_t *doc)
{
    amark_allocator_t allocator;

    if (doc == NULL) {
        return;
    }
    /* The body holds the allocator, so copy it before the body is gone. */
    allocator = doc->allocator;
    amark_arena_deinit(&doc->arena);
    amark_free(&allocator, doc);
}
