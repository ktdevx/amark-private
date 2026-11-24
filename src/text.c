/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * The public text functions (include/amark/text.h). They read the document
 * and its content; nothing here writes to the document.
 */

#include <amark/text.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "alloc.h"
#include "array.h"
#include "document.h"
#include "line_index.h"
#include "text_build.h"
#include "text_sink.h"

/*
 * Returns the node with ID id, or NULL if the call cannot be answered: doc
 * is NULL, id is AMARK_NODE_NONE or larger than the number of nodes created,
 * or the node was removed (its kind is AMARK_KIND_NONE). The same condition
 * as the tree readers (src/node.c).
 */
static const amark_node_t *find(const amark_document_t *doc,
                                amark_node_id_t id)
{
    const amark_node_t *n;

    if (doc == NULL || id == AMARK_NODE_NONE ||
        id >= amark_array_len(&doc->nodes)) {
        return NULL;
    }
    n = (const amark_node_t *)amark_array_at(&doc->nodes, id);
    if (n->kind == AMARK_KIND_NONE) {
        return NULL;
    }
    return n;
}

bool amark_node_raw(const amark_document_t *doc, amark_node_id_t node,
                    const char **bytes, size_t *length)
{
    const amark_node_t *n;

    if (bytes == NULL || length == NULL) {
        return false;
    }
    n = find(doc, node);
    if (n == NULL) {
        return false;
    }
    /* content is NULL only when its length is 0; then the range is [0, 0)
       and the bytes are NULL. */
    *bytes = doc->content != NULL ? (const char *)doc->content + n->start
                                  : NULL;
    *length = n->end - n->start;
    return true;
}

/*
 * What one call of amark_node_text reads: either the code lines of a code
 * block (code is true; attr is its attributes; [start, end) is the node's
 * range, which holds the code lines and bounds the length of the result),
 * or the range [start, end), which starts on first_line, written with rule.
 * first_line is not set for an empty range, which is not read.
 */
typedef struct text_selection {
    bool code;
    const amark_node_attr_t *attr;
    size_t start;
    size_t end;
    size_t first_line;
    amark_text_rule_t rule;
} text_selection_t;

/* Returns the attribute element of n, or NULL if it has none. */
static const amark_node_attr_t *attr_of(const amark_document_t *doc,
                                        const amark_node_t *n)
{
    if (n->attr == 0 || n->attr >= amark_array_len(&doc->attrs)) {
        return NULL;
    }
    return (const amark_node_attr_t *)amark_array_at(&doc->attrs, n->attr);
}

/* Selects [start, end) starting on first_line, with rule. */
static bool select_range(text_selection_t *sel, size_t start, size_t end,
                         size_t first_line, amark_text_rule_t rule)
{
    sel->code = false;
    sel->attr = NULL;
    sel->start = start;
    sel->end = end;
    sel->first_line = first_line;
    sel->rule = rule;
    return true;
}

/*
 * Selects the range r of an attribute (a destination or a title) if it is
 * written, decoded. Its first line is looked up from its start with the line
 * index; an empty range is not read, so no line is looked up for it.
 */
static bool select_attr_range(const amark_document_t *doc,
                              const amark_opt_range_t *r,
                              text_selection_t *sel)
{
    size_t first_line = 0;

    if (!r->present) {
        return false;
    }
    if (r->start < r->end) {
        first_line = amark_line_index_line_at(amark_document_line_index(doc),
                                              r->start);
    }
    return select_range(sel, r->start, r->end, first_line,
                        AMARK_TEXT_RULE_DECODED);
}

/* The logical text: non-container blocks and inlines (dialect included). */
static bool select_logical(const amark_node_t *n, text_selection_t *sel)
{
    if (!n->is_inline && (n->kind < AMARK_KIND_THEMATIC_BREAK ||
                          n->kind > AMARK_KIND_LINK_REFERENCE_DEFINITION)) {
        return false; /* a container or a block of a dialect kind */
    }
    return select_range(sel, n->start, n->end, n->start_line,
                        AMARK_TEXT_RULE_PLAIN);
}

/* The value: text, escapes, references, autolinks, code spans and blocks. */
static bool select_value(const amark_document_t *doc, const amark_node_t *n,
                         text_selection_t *sel)
{
    size_t ticks = 0;

    switch (n->kind) {
    case AMARK_KIND_TEXT:
        return select_range(sel, n->start, n->end, n->start_line,
                            AMARK_TEXT_RULE_PLAIN);
    case AMARK_KIND_BACKSLASH_ESCAPE:
    case AMARK_KIND_CHARACTER_REFERENCE:
        return select_range(sel, n->start, n->end, n->start_line,
                            AMARK_TEXT_RULE_DECODED);
    case AMARK_KIND_AUTOLINK:
        /* Without the '<' and the '>' that enclose it. */
        if (n->end - n->start < 2) {
            return false;
        }
        return select_range(sel, n->start + 1, n->end - 1, n->start_line,
                            AMARK_TEXT_RULE_PLAIN);
    case AMARK_KIND_CODE_SPAN:
        /* The range starts with the opening backtick string, and the
           closing one at its end has the same length. */
        while (n->start + ticks < n->end &&
               doc->content[n->start + ticks] == '`') {
            ticks++;
        }
        if (ticks > (n->end - n->start) / 2) {
            return false;
        }
        return select_range(sel, n->start + ticks, n->end - ticks,
                            n->start_line, AMARK_TEXT_RULE_CODE_SPAN);
    case AMARK_KIND_CODE_BLOCK:
        sel->attr = attr_of(doc, n);
        sel->code = sel->attr != NULL;
        sel->start = n->start;
        sel->end = n->end;
        return sel->code;
    default:
        return false;
    }
}

/* The destination or the title of an inline link, image or definition. */
static bool select_link_part(const amark_document_t *doc,
                             const amark_node_t *n, bool title,
                             text_selection_t *sel)
{
    const amark_node_attr_t *a = attr_of(doc, n);

    if (a == NULL) {
        return false;
    }
    if (n->kind == AMARK_KIND_LINK || n->kind == AMARK_KIND_IMAGE) {
        if (a->link.form != AMARK_LINK_INLINE) {
            return false; /* a reference form: ask the definition */
        }
        return select_attr_range(doc, title ? &a->link.title
                                            : &a->link.destination, sel);
    }
    if (n->kind == AMARK_KIND_LINK_REFERENCE_DEFINITION) {
        return select_attr_range(doc, title ? &a->definition.title
                                            : &a->definition.destination,
                                 sel);
    }
    return false;
}

/* The info string of a fenced code block, when it is written. */
static bool select_info(const amark_document_t *doc, const amark_node_t *n,
                        text_selection_t *sel)
{
    const amark_node_attr_t *a;

    if (n->kind != AMARK_KIND_CODE_BLOCK) {
        return false;
    }
    a = attr_of(doc, n);
    if (a == NULL || a->code_block.style != AMARK_CODE_BLOCK_FENCED ||
        !a->code_block.info.present) {
        return false;
    }
    /* The info string is on the opening fence line, the node's first. */
    return select_range(sel, a->code_block.info.start,
                        a->code_block.info.end, n->start_line,
                        AMARK_TEXT_RULE_DECODED);
}

/*
 * The part and kind table: selects what part of n is read. Returns false if
 * the pair is not in the table or the part is not written.
 */
static bool select_part(const amark_document_t *doc, const amark_node_t *n,
                        amark_text_part_t part, text_selection_t *sel)
{
    switch ((int)part) {
    case AMARK_TEXT_LOGICAL:
        return select_logical(n, sel);
    case AMARK_TEXT_VALUE:
        return select_value(doc, n, sel);
    case AMARK_TEXT_DESTINATION:
        return select_link_part(doc, n, false, sel);
    case AMARK_TEXT_TITLE:
        return select_link_part(doc, n, true, sel);
    case AMARK_TEXT_INFO:
        return select_info(doc, n, sel);
    default:
        return false; /* none of the five parts */
    }
}

/*
 * The longest range that is read (design, "the length limit"). Each byte read
 * gives at most 3 bytes and code lines may gain one '\n', so the result of a
 * range of at most this length is at most SIZE_MAX - 1 bytes, and that plus
 * the terminating NUL of amark_node_text_alloc fits in size_t.
 */
#define AMARK_TEXT_RANGE_LIMIT ((SIZE_MAX - 2) / 3)

amark_error_t amark_node_text(const amark_document_t *doc,
                              amark_node_id_t node, amark_text_part_t part,
                              char *buffer, size_t capacity, size_t *length)
{
    const amark_node_t *n;
    text_selection_t sel;
    amark_text_sink_t sink;

    if (length == NULL) {
        return AMARK_ERROR_NOT_AVAILABLE;
    }
    n = find(doc, node);
    /* Without parse data (a document not made by amark_parse) there are no
       line records to read the range by. */
    if (n == NULL || amark_document_parse_data(doc) == NULL ||
        !select_part(doc, n, part, &sel)) {
        return AMARK_ERROR_NOT_AVAILABLE;
    }
    /* Before anything is read or written. */
    if (sel.end - sel.start > AMARK_TEXT_RANGE_LIMIT) {
        return AMARK_ERROR_LIMIT_EXCEEDED;
    }

    amark_text_sink_init(&sink, (unsigned char *)buffer, capacity);
    if (sel.code) {
        amark_text_build_code(doc, n, sel.attr, &sink);
    } else if (sel.start < sel.end) {
        amark_text_build_range(doc, sel.start, sel.end, sel.first_line,
                               sel.rule, &sink);
    }
    /* The sink wrote nothing past its capacity (0 for a NULL buffer), and
       counted the whole length: the size needed, without a NUL. */
    *length = sink.length;
    if (sink.length > sink.capacity) {
        return AMARK_ERROR_BUFFER_TOO_SMALL;
    }
    return AMARK_ERROR_NONE;
}

amark_error_t amark_node_text_alloc(const amark_document_t *doc,
                                    amark_node_id_t node,
                                    amark_text_part_t part, char **out,
                                    size_t *length)
{
    amark_error_t err;
    size_t needed = 0;
    size_t written = 0;
    void *block = NULL;

    if (out == NULL) {
        return AMARK_ERROR_NOT_AVAILABLE;
    }
    /* The first pass only measures. A result of length 0 succeeds here. */
    err = amark_node_text(doc, node, part, NULL, 0, &needed);
    if (err != AMARK_ERROR_NONE && err != AMARK_ERROR_BUFFER_TOO_SMALL) {
        return err; /* NOT_AVAILABLE or LIMIT_EXCEEDED */
    }
    /* The range is within the limit, so needed + 1 does not overflow
       (design, "the length limit"). The string comes from the allocator of
       the document, not its arena, so the document is not changed. */
    err = amark_malloc(&doc->allocator, needed + 1, &block);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    /* The second pass writes the same bytes: nothing changes the document
       or the content in between, so the result is again needed bytes and
       fits. */
    err = amark_node_text(doc, node, part, (char *)block, needed, &written);
    if (err != AMARK_ERROR_NONE) {
        amark_free(&doc->allocator, block);
        return err;
    }
    ((char *)block)[needed] = '\0';
    *out = (char *)block;
    if (length != NULL) {
        *length = needed;
    }
    return AMARK_ERROR_NONE;
}

void amark_text_free(const amark_document_t *doc, char *text)
{
    if (doc == NULL || text == NULL) {
        return;
    }
    amark_free(&doc->allocator, text);
}
