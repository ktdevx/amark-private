/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#include "tree_dump.h"

#include <stdint.h>

/*
 * The output: bytes are counted in len whether or not they fit, and stored
 * while len < size - 1, so that one byte is always left for the NUL.
 */
typedef struct dump_out {
    char *buffer;
    size_t size;
    size_t len;
} dump_out_t;

static void put_char(dump_out_t *out, char c)
{
    if (out->size > 0 && out->len < out->size - 1) {
        out->buffer[out->len] = c;
    }
    out->len++;
}

static void put_str(dump_out_t *out, const char *s)
{
    while (*s != '\0') {
        put_char(out, *s);
        s++;
    }
}

/* Writes a space and then value in decimal. */
static void put_number(dump_out_t *out, uint32_t value)
{
    char digits[10]; /* UINT32_MAX has 10 digits */
    size_t count = 0;

    do {
        digits[count] = (char)('0' + (int)(value % 10u));
        count++;
        value /= 10u;
    } while (value != 0);

    put_char(out, ' ');
    while (count > 0) {
        count--;
        put_char(out, digits[count]);
    }
}

/* Writes a space and then c, or " none" if c is 0. */
static void put_attr_char(dump_out_t *out, char c)
{
    if (c == '\0') {
        put_str(out, " none");
        return;
    }
    put_char(out, ' ');
    put_char(out, c);
}

static void put_spacing(dump_out_t *out, amark_list_spacing_t spacing)
{
    switch (spacing) {
    case AMARK_LIST_TIGHT:
        put_str(out, " tight");
        break;
    case AMARK_LIST_LOOSE:
        put_str(out, " loose");
        break;
    default:
        put_str(out, " none");
        break;
    }
}

static void put_list(dump_out_t *out, const amark_document_t *doc,
                     amark_node_id_t node)
{
    uint32_t start = 0;

    switch (amark_list_type(doc, node)) {
    case AMARK_LIST_BULLET:
        put_str(out, "ul");
        put_attr_char(out, amark_list_bullet_char(doc, node));
        break;
    case AMARK_LIST_ORDERED:
        put_str(out, "ol");
        (void)amark_list_start(doc, node, &start);
        put_number(out, start);
        put_attr_char(out, amark_list_delimiter(doc, node));
        break;
    default:
        put_str(out, "list");
        return;
    }
    put_spacing(out, amark_list_spacing(doc, node));
}

static void put_heading(dump_out_t *out, const amark_document_t *doc,
                        amark_node_id_t node)
{
    put_str(out, "h");
    put_number(out, (uint32_t)amark_heading_level(doc, node));
    switch (amark_heading_style(doc, node)) {
    case AMARK_HEADING_ATX:
        put_str(out, " atx");
        break;
    case AMARK_HEADING_SETEXT:
        put_str(out, " setext");
        break;
    default:
        put_str(out, " none");
        break;
    }
}

static void put_code_block(dump_out_t *out, const amark_document_t *doc,
                           amark_node_id_t node)
{
    amark_range_t info;
    size_t length;

    put_str(out, "code");
    switch (amark_code_block_style(doc, node)) {
    case AMARK_CODE_BLOCK_INDENTED:
        put_str(out, " indented");
        break;
    case AMARK_CODE_BLOCK_FENCED:
        put_str(out, " fenced");
        put_attr_char(out, amark_code_block_fence_char(doc, node));
        length = amark_code_block_fence_length(doc, node);
        put_number(out, length > UINT32_MAX ? UINT32_MAX : (uint32_t)length);
        if (amark_code_block_info(doc, node, &info)) {
            put_str(out, " info");
        }
        break;
    default:
        put_str(out, " none");
        break;
    }
}

/* Writes "(" and the kind and attributes of node, a block. */
static void put_open(dump_out_t *out, const amark_document_t *doc,
                     amark_node_id_t node)
{
    put_char(out, '(');
    switch (amark_node_kind(doc, node)) {
    case AMARK_KIND_DOCUMENT:
        put_str(out, "doc");
        break;
    case AMARK_KIND_BLOCK_QUOTE:
        put_str(out, "bq");
        break;
    case AMARK_KIND_LIST:
        put_list(out, doc, node);
        break;
    case AMARK_KIND_LIST_ITEM:
        put_str(out, "li");
        break;
    case AMARK_KIND_THEMATIC_BREAK:
        put_str(out, "hr");
        break;
    case AMARK_KIND_HEADING:
        put_heading(out, doc, node);
        break;
    case AMARK_KIND_CODE_BLOCK:
        put_code_block(out, doc, node);
        break;
    case AMARK_KIND_HTML_BLOCK:
        put_str(out, "html");
        break;
    case AMARK_KIND_PARAGRAPH:
        put_str(out, "p");
        break;
    case AMARK_KIND_LINK_REFERENCE_DEFINITION:
        put_str(out, "def");
        break;
    default:
        put_char(out, '?');
        break;
    }
}

/* Returns node or the first sibling after it that is not an inline. */
static amark_node_id_t skip_inlines(const amark_document_t *doc,
                                    amark_node_id_t node)
{
    while (node != AMARK_NODE_NONE && amark_node_is_inline(doc, node)) {
        node = amark_node_next_sibling(doc, node);
    }
    return node;
}

size_t amark_test_dump_blocks(const amark_document_t *doc, char *buffer,
                              size_t size)
{
    dump_out_t out;
    amark_node_id_t root = amark_document_root(doc);
    amark_node_id_t node = root;
    amark_node_id_t next;

    out.buffer = buffer;
    out.size = size;
    out.len = 0;

    if (root != AMARK_NODE_NONE) {
        put_open(&out, doc, root);
        for (;;) {
            next = skip_inlines(doc, amark_node_first_child(doc, node));
            if (next == AMARK_NODE_NONE) {
                /*
                 * node has no block children: close it, and the ancestors
                 * it was the last block child of, up to one with a next
                 * block sibling.
                 */
                for (;;) {
                    put_char(&out, ')');
                    if (node == root) {
                        break;
                    }
                    next = skip_inlines(doc,
                                        amark_node_next_sibling(doc, node));
                    if (next != AMARK_NODE_NONE) {
                        break;
                    }
                    node = amark_node_parent(doc, node);
                }
                if (next == AMARK_NODE_NONE) {
                    break; /* the root is closed */
                }
            }
            put_char(&out, ' ');
            put_open(&out, doc, next);
            node = next;
        }
    }

    if (size > 0) {
        buffer[out.len < size ? out.len : size - 1] = '\0';
    }
    return out.len;
}
