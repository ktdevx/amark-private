/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#include "dialect_reply.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "arena.h"
#include "document.h"
#include "line.h"
#include "line_index.h"
#include "parse_data.h"
#include "tree_internal.h"

/* ---------------------------------------------------------------------- */
/* Begin and finish */

amark_error_t amark_dialect_reply_begin(amark_dialect_run_t *run,
                                        size_t extension,
                                        amark_dialect_reply_mode_t mode,
                                        amark_kind_t kind,
                                        amark_range_t range,
                                        amark_dialect_reply_t **out)
{
    amark_dialect_reply_t *reply = run->reply;
    amark_error_t err;

    *out = NULL;
    if (reply == NULL) {
        void *place;

        err = amark_arena_alloc(&run->arena, sizeof(*reply), &place);
        if (err != AMARK_ERROR_NONE) {
            return err;
        }
        reply = (amark_dialect_reply_t *)place;
        memset(reply, 0, sizeof(*reply));
        amark_array_init(&reply->blocks, &run->arena,
                         sizeof(amark_dialect_reply_block_t));
        amark_array_init(&reply->bytes, &run->arena, 1);
        run->reply = reply;
    }

    reply->run = run;
    reply->ext = run->dialect->extensions[extension];
    reply->extension = extension;
    reply->mode = mode;
    reply->range = range;
    reply->content_length = amark_document_content_length(run->doc);
    reply->child_end = range.start;
    reply->rejected = false;
    reply->reason = AMARK_DIALECT_REASON_STATUS;
    reply->failure = AMARK_ERROR_NONE;
    amark_array_clear(&reply->blocks);
    amark_array_clear(&reply->bytes);

    if (mode != AMARK_DIALECT_REPLY_PARAGRAPH) {
        amark_dialect_reply_block_t self;

        memset(&self, 0, sizeof(self));
        self.kind = kind;
        self.cls = mode == AMARK_DIALECT_REPLY_LEAF ? AMARK_DIALECT_LEAF
                                                    : AMARK_DIALECT_CONTAINER;
        self.parent = AMARK_DIALECT_TOP;
        self.range = range;
        self.child_end = range.start;
        err = amark_array_push(&reply->blocks, &self);
        if (err != AMARK_ERROR_NONE) {
            return err;
        }
    }
    *out = reply;
    return AMARK_ERROR_NONE;
}

amark_error_t amark_dialect_reply_finish(amark_dialect_run_t *run,
                                         amark_dialect_reply_t *reply,
                                         bool *valid)
{
    *valid = false;
    if (reply->failure != AMARK_ERROR_NONE) {
        return reply->failure;
    }
    if (reply->rejected) {
        return amark_dialect_reject(run, reply->extension,
                                    AMARK_DIALECT_HOOK_BLOCK_CLOSE,
                                    reply->reason, reply->range.start);
    }
    *valid = reply->mode != AMARK_DIALECT_REPLY_PARAGRAPH ||
             amark_array_len(&reply->blocks) > 0;
    return AMARK_ERROR_NONE;
}

/* ---------------------------------------------------------------------- */
/* Checks */

/* Remembers the first invalid request (10.3). */
static amark_error_t reject(amark_dialect_reply_t *reply,
                            amark_dialect_reason_t reason)
{
    reply->rejected = true;
    reply->reason = reason;
    return AMARK_ERROR_INVALID_INPUT;
}

/* Remembers a failure that lasts (10.10, 10.11). */
static amark_error_t fail(amark_dialect_reply_t *reply, amark_error_t err)
{
    reply->failure = err;
    return err;
}

/*
 * Returns the error a request returns before it is checked: the reply is
 * NULL, has a lasting failure or is rejected already.
 */
static amark_error_t closed(const amark_dialect_reply_t *reply)
{
    if (reply == NULL) {
        return AMARK_ERROR_INVALID_INPUT;
    }
    if (reply->failure != AMARK_ERROR_NONE) {
        return reply->failure;
    }
    if (reply->rejected) {
        return AMARK_ERROR_INVALID_INPUT;
    }
    return AMARK_ERROR_NONE;
}

/* range is ordered and inside the content (10.8). */
static bool in_content(const amark_dialect_reply_t *reply,
                       amark_range_t range)
{
    return range.start <= range.end && range.end <= reply->content_length;
}

static bool inside(amark_range_t inner, amark_range_t outer)
{
    return inner.start >= outer.start && inner.end <= outer.end;
}

/*
 * Finds the line of offset (<= the content length) with the line index and
 * stores where its content starts (the line record) and ends. Returns false
 * if the parsed range does not cover that line.
 */
static bool line_content(const amark_document_t *doc, size_t offset,
                         size_t *content_start, size_t *content_end)
{
    size_t line = amark_line_index_line_at(amark_document_line_index(doc),
                                           offset);
    amark_line_bounds_t bounds;
    amark_line_record_t record;

    if (!amark_line_bounds(doc, line, &bounds) ||
        !amark_line_get(doc, line, &record)) {
        return false;
    }
    *content_start = record.content_start;
    *content_end = bounds.content_end;
    return true;
}

/*
 * A paragraph part starts where the content of its line starts (cut to the
 * start of the closing paragraph) and ends where the content of its line
 * ends or where the closing paragraph ends (6.10).
 */
static bool covers_lines(const amark_dialect_reply_t *reply,
                         amark_range_t range)
{
    const amark_document_t *doc = reply->run->doc;
    size_t start;
    size_t end;

    if (!line_content(doc, range.start, &start, &end)) {
        return false;
    }
    if (start < reply->range.start) {
        start = reply->range.start;
    }
    if (range.start != start) {
        return false;
    }
    if (range.end == reply->range.end) {
        return true;
    }
    if (!line_content(doc, range.end, &start, &end)) {
        return false;
    }
    return range.end == end;
}

/* offset is inside the content of its line, prefix excluded. */
static bool in_line_content(const amark_dialect_reply_t *reply,
                            size_t offset)
{
    size_t start;
    size_t end;

    return line_content(reply->run->doc, offset, &start, &end) &&
           start <= offset && offset <= end;
}

static amark_dialect_reply_block_t *block_at(
    const amark_dialect_reply_t *reply, size_t index)
{
    return (amark_dialect_reply_block_t *)amark_array_at(&reply->blocks,
                                                         index);
}

/* ---------------------------------------------------------------------- */
/* The public functions */

amark_error_t amark_dialect_reply_add_block(
    amark_dialect_reply_t *reply, size_t parent, amark_kind_t kind,
    amark_range_t range, size_t *index)
{
    amark_dialect_reply_block_t block;
    amark_dialect_reply_block_t *up = NULL;
    amark_range_t outer;
    size_t child_end;
    size_t count;
    int cls;
    amark_error_t err = closed(reply);

    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    if (index == NULL) {
        /* A value the contract does not define (10.4). */
        return reject(reply, AMARK_DIALECT_REASON_STATUS);
    }

    /* 1. Only a paragraph reply has blocks to add; parent is earlier. */
    if (reply->mode != AMARK_DIALECT_REPLY_PARAGRAPH) {
        return reject(reply, AMARK_DIALECT_REASON_NESTING);
    }
    count = amark_array_len(&reply->blocks);
    if (parent != AMARK_DIALECT_TOP && parent >= count) {
        return reject(reply, AMARK_DIALECT_REASON_INDEX);
    }

    /* 2. A block kind of the extension, or a paragraph part at the top. */
    if (kind == AMARK_KIND_PARAGRAPH) {
        if (parent != AMARK_DIALECT_TOP) {
            return reject(reply, AMARK_DIALECT_REASON_NESTING);
        }
        cls = 0;
    } else {
        const amark_dialect_kind_t *k = amark_dialect_find_kind(reply->ext,
                                                                kind);

        if (k == NULL) {
            return reject(reply, AMARK_DIALECT_REASON_KIND);
        }
        if (k->cls == AMARK_DIALECT_INLINE) {
            return reject(reply, AMARK_DIALECT_REASON_NESTING);
        }
        cls = k->cls;
    }

    /* 3. The parent is a container. */
    if (parent != AMARK_DIALECT_TOP) {
        up = block_at(reply, parent);
        if (up->cls != AMARK_DIALECT_CONTAINER) {
            return reject(reply, AMARK_DIALECT_REASON_NESTING);
        }
        outer = up->range;
        child_end = up->child_end;
    } else {
        outer = reply->range;
        child_end = reply->child_end;
    }

    /* 4. Inside the content, and inside the parent or the closing block. */
    if (!in_content(reply, range)) {
        return reject(reply, AMARK_DIALECT_REASON_BOUNDS);
    }
    if (!inside(range, outer)) {
        return reject(reply, AMARK_DIALECT_REASON_PARENT);
    }

    /* 5. After the previous sibling. */
    if (range.start < child_end) {
        return reject(reply, AMARK_DIALECT_REASON_ORDER);
    }

    /* 6. A paragraph part covers whole lines. */
    if (cls == 0 && !covers_lines(reply, range)) {
        return reject(reply, AMARK_DIALECT_REASON_PARAGRAPH);
    }

    memset(&block, 0, sizeof(block));
    block.kind = kind;
    block.cls = cls;
    block.parent = parent;
    block.level = up != NULL ? up->level + 1 : 0;
    block.range = range;
    block.child_end = range.start;
    err = amark_array_push(&reply->blocks, &block);
    if (err != AMARK_ERROR_NONE) {
        return fail(reply, err);
    }
    if (parent != AMARK_DIALECT_TOP) {
        up = block_at(reply, parent); /* the push may have moved it */
        up->child_end = range.end;
        up->has_children = true;
    } else {
        reply->child_end = range.end;
    }
    *index = count;
    return AMARK_ERROR_NONE;
}

amark_error_t amark_dialect_reply_set_data(
    amark_dialect_reply_t *reply, size_t index, const void *data,
    size_t size)
{
    amark_dialect_reply_block_t *block;
    const unsigned char *bytes = (const unsigned char *)data;
    size_t offset;
    size_t i;
    amark_error_t err = closed(reply);

    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    if (data == NULL && size > 0) {
        /* A value the contract does not define (10.4). */
        return reject(reply, AMARK_DIALECT_REASON_STATUS);
    }
    if (index >= amark_array_len(&reply->blocks)) {
        return reject(reply, AMARK_DIALECT_REASON_INDEX);
    }
    block = block_at(reply, index);
    if (block->cls == 0) {
        return reject(reply, AMARK_DIALECT_REASON_NESTING);
    }
    if (size == 0) {
        block->data_offset = 0;
        block->data_size = 0;
        return AMARK_ERROR_NONE;
    }

    offset = amark_array_len(&reply->bytes);
    if (size > SIZE_MAX - offset) {
        return fail(reply, AMARK_ERROR_LIMIT_EXCEEDED);
    }
    err = amark_array_reserve(&reply->bytes, offset + size);
    if (err != AMARK_ERROR_NONE) {
        return fail(reply, err);
    }
    for (i = 0; i < size; i++) {
        /* Within the capacity just reserved: cannot fail. */
        (void)amark_array_push(&reply->bytes, &bytes[i]);
    }
    block->data_offset = offset;
    block->data_size = size;
    return AMARK_ERROR_NONE;
}

amark_error_t amark_dialect_reply_set_inline(
    amark_dialect_reply_t *reply, size_t index, amark_range_t range)
{
    amark_dialect_reply_block_t *block;
    amark_error_t err = closed(reply);

    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    if (index >= amark_array_len(&reply->blocks)) {
        return reject(reply, AMARK_DIALECT_REASON_INDEX);
    }
    block = block_at(reply, index);
    if (block->cls != AMARK_DIALECT_LEAF) {
        return reject(reply, AMARK_DIALECT_REASON_INLINE);
    }
    if (!in_content(reply, range)) {
        return reject(reply, AMARK_DIALECT_REASON_BOUNDS);
    }
    if (!inside(range, block->range)) {
        return reject(reply, AMARK_DIALECT_REASON_PARENT);
    }
    if (!in_line_content(reply, range.start) ||
        !in_line_content(reply, range.end)) {
        return reject(reply, AMARK_DIALECT_REASON_PREFIX);
    }
    block->has_inline = true;
    block->inline_range = range;
    return AMARK_ERROR_NONE;
}

/* ---------------------------------------------------------------------- */
/* Apply */

/*
 * Checks the limits for the nodes reply adds under a parent at
 * parent_depth, before any is added (10.11).
 */
static amark_error_t check_limits(const amark_dialect_reply_t *reply,
                                  const amark_document_t *doc,
                                  size_t parent_depth, size_t max_depth,
                                  size_t max_nodes)
{
    size_t count = amark_array_len(&reply->blocks);
    size_t first = reply->mode == AMARK_DIALECT_REPLY_CONTAINER ? 1 : 0;
    size_t added;
    size_t deepest = 0;
    size_t nodes;
    size_t i;

    if (count <= first) {
        return AMARK_ERROR_NONE;
    }
    added = count - first;
    for (i = first; i < count; i++) {
        size_t level = block_at(reply, i)->level;

        if (level > deepest) {
            deepest = level;
        }
    }
    /* parent_depth + 1 + deepest > max_depth, without overflow. */
    if (max_depth != 0 &&
        (parent_depth >= max_depth ||
         deepest > max_depth - parent_depth - 1)) {
        return AMARK_ERROR_LIMIT_EXCEEDED;
    }
    /* nodes + added > max_nodes, without overflow. */
    nodes = amark_document_node_count(doc);
    if (max_nodes != 0 && (nodes > max_nodes || added > max_nodes - nodes)) {
        return AMARK_ERROR_LIMIT_EXCEEDED;
    }
    return AMARK_ERROR_NONE;
}

/*
 * The end of the inline content of a paragraph part over [start, end): end
 * without the spaces and tabs before it (as the block parser cuts the last
 * line of a paragraph).
 */
static size_t trim_end(const unsigned char *bytes, size_t start, size_t end)
{
    while (end > start && (bytes[end - 1] == ' ' || bytes[end - 1] == '\t')) {
        end--;
    }
    return end;
}

amark_error_t amark_dialect_reply_apply(amark_dialect_run_t *run,
                                        amark_dialect_reply_t *reply,
                                        amark_node_id_t parent,
                                        size_t parent_depth,
                                        amark_node_id_t existing,
                                        size_t max_depth, size_t max_nodes)
{
    amark_document_t *doc = run->doc;
    const unsigned char *bytes = amark_document_content(doc);
    size_t count = amark_array_len(&reply->blocks);
    size_t i;
    amark_error_t err;

    err = check_limits(reply, doc, parent_depth, max_depth, max_nodes);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }

    for (i = 0; i < count; i++) {
        amark_dialect_reply_block_t *block = block_at(reply, i);
        amark_node_id_t node;

        if (i == 0 && reply->mode == AMARK_DIALECT_REPLY_CONTAINER) {
            node = existing;
        } else {
            amark_node_id_t up = block->parent == AMARK_DIALECT_TOP
                                     ? parent
                                     : block_at(reply, block->parent)->node;

            err = amark_tree_append(doc, up, block->kind,
                                    block->cls == 0 ? AMARK_NODE_CLASS_AUTO
                                                    : AMARK_NODE_CLASS_BLOCK,
                                    block->range.start, block->range.end,
                                    &node);
            if (err != AMARK_ERROR_NONE) {
                return err;
            }
        }
        block->node = node;

        if (block->data_size > 0) {
            err = amark_tree_set_dialect_data(
                doc, node, amark_array_at(&reply->bytes, block->data_offset),
                block->data_size);
            if (err != AMARK_ERROR_NONE) {
                return err;
            }
        }
        if (block->cls == 0) {
            err = amark_inline_content_add(
                doc, node, block->range.start,
                trim_end(bytes, block->range.start, block->range.end));
        } else if (block->has_inline) {
            err = amark_inline_content_add(doc, node,
                                           block->inline_range.start,
                                           block->inline_range.end);
        }
        if (err != AMARK_ERROR_NONE) {
            return err;
        }
    }
    return AMARK_ERROR_NONE;
}

/* ---------------------------------------------------------------------- */
/* The line readers */

void amark_dialect_lines_init(struct amark_dialect_lines *lines,
                              const amark_document_t *doc,
                              amark_range_t range)
{
    size_t first;
    size_t last;

    amark_document_range_lines(doc, range.start, range.end, &first, &last);
    lines->doc = doc;
    lines->first_line = first;
    lines->line_count = last - first + 1;
}

void amark_dialect_closing_init(amark_dialect_closing_t *closing,
                                struct amark_dialect_lines *lines,
                                const amark_document_t *doc,
                                amark_kind_t kind, amark_range_t range,
                                uint64_t data)
{
    amark_dialect_lines_init(lines, doc, range);
    closing->content = (const char *)amark_document_content(doc);
    closing->content_length = amark_document_content_length(doc);
    closing->kind = kind;
    closing->range = range;
    closing->line_count = lines->line_count;
    closing->data = data;
    closing->lines = lines;
}

/*
 * Reads line (a line number) of doc: its first byte, where its content
 * starts (the line record) and where its content ends. Returns false if the
 * parsed range does not cover it.
 */
static bool read_line(const amark_document_t *doc, size_t line,
                      size_t *start, size_t *content_start, size_t *end)
{
    amark_line_bounds_t bounds;
    amark_line_record_t record;

    if (!amark_line_bounds(doc, line, &bounds) ||
        !amark_line_get(doc, line, &record)) {
        return false;
    }
    *start = bounds.start;
    *content_start = record.content_start;
    *end = bounds.content_end;
    return true;
}

bool amark_dialect_closing_line(const amark_dialect_closing_t *closing,
                                size_t index, amark_dialect_line_span_t *out)
{
    const struct amark_dialect_lines *lines;
    size_t start;
    size_t content_start;
    size_t end;

    if (closing == NULL || out == NULL || closing->lines == NULL) {
        return false;
    }
    lines = closing->lines;
    if (index >= lines->line_count ||
        !read_line(lines->doc, lines->first_line + index, &start,
                   &content_start, &end)) {
        return false;
    }
    /* Cut the first line to the start of the block, the last to its end. */
    if (index == 0) {
        if (start < closing->range.start) {
            start = closing->range.start;
        }
        if (content_start < closing->range.start) {
            content_start = closing->range.start;
        }
    }
    if (index == lines->line_count - 1 && end > closing->range.end) {
        end = closing->range.end;
    }
    if (content_start > end) {
        content_start = end;
    }
    if (start > content_start) {
        start = content_start;
    }
    out->start = start;
    out->content_start = content_start;
    out->end = end;
    return true;
}

bool amark_dialect_inline_next_line(const amark_dialect_inline_t *in,
                                    size_t line_end, amark_range_t *out)
{
    const struct amark_dialect_lines *lines;
    size_t line;
    size_t start;
    size_t content_start;
    size_t end;

    if (in == NULL || out == NULL || in->lines == NULL) {
        return false;
    }
    lines = in->lines;
    /* A line ends inside the inline content, and a line follows it. */
    if (line_end < in->inline_content.start ||
        line_end >= in->inline_content.end ||
        in->inline_content.end > amark_document_content_length(lines->doc)) {
        return false;
    }
    line = amark_line_index_line_at(amark_document_line_index(lines->doc),
                                    line_end);
    if (line < lines->first_line ||
        line - lines->first_line + 1 >= lines->line_count ||
        !read_line(lines->doc, line, &start, &content_start, &end) ||
        end != line_end) {
        return false;
    }
    line++;
    if (!read_line(lines->doc, line, &start, &content_start, &end)) {
        return false;
    }
    /* The last line ends where the inline content ends. */
    if (line - lines->first_line + 1 == lines->line_count ||
        end > in->inline_content.end) {
        end = in->inline_content.end;
    }
    if (content_start > end) {
        content_start = end;
    }
    out->start = content_start;
    out->end = end;
    return true;
}
