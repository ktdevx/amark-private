/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#include "text_dump.h"

#include <amark/error.h>
#include <amark/text.h>

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

/* Writes the length bytes of value between '"', escaped. */
static void put_quoted(dump_out_t *out, const char *value, size_t length)
{
    static const char k_hex[] = "0123456789ABCDEF";
    size_t i;
    unsigned char c;

    put_char(out, '"');
    for (i = 0; i < length; i++) {
        c = (unsigned char)value[i];
        if (c == '"' || c == '\\') {
            put_char(out, '\\');
            put_char(out, (char)c);
        } else if (c == '\n') {
            put_str(out, "\\n");
        } else if (c == '\r') {
            put_str(out, "\\r");
        } else if (c == '\t') {
            put_str(out, "\\t");
        } else if (c < 0x20u || c == 0x7Fu) {
            put_str(out, "\\x");
            put_char(out, k_hex[c >> 4]);
            put_char(out, k_hex[c & 0x0Fu]);
        } else {
            put_char(out, (char)c);
        }
    }
    put_char(out, '"');
}

/* The name of a kind, as amark_test_dump_tree writes it. */
static const char *kind_name(amark_kind_t kind)
{
    switch (kind) {
    case AMARK_KIND_DOCUMENT:
        return "doc";
    case AMARK_KIND_BLOCK_QUOTE:
        return "bq";
    case AMARK_KIND_LIST:
        return "list";
    case AMARK_KIND_LIST_ITEM:
        return "li";
    case AMARK_KIND_THEMATIC_BREAK:
        return "hr";
    case AMARK_KIND_HEADING:
        return "h";
    case AMARK_KIND_CODE_BLOCK:
    case AMARK_KIND_CODE_SPAN:
        return "code";
    case AMARK_KIND_HTML_BLOCK:
    case AMARK_KIND_RAW_HTML:
        return "html";
    case AMARK_KIND_PARAGRAPH:
        return "p";
    case AMARK_KIND_LINK_REFERENCE_DEFINITION:
        return "def";
    case AMARK_KIND_TEXT:
        return "text";
    case AMARK_KIND_EMPHASIS:
        return "em";
    case AMARK_KIND_STRONG:
        return "strong";
    case AMARK_KIND_LINK:
        return "link";
    case AMARK_KIND_IMAGE:
        return "image";
    case AMARK_KIND_AUTOLINK:
        return "autolink";
    case AMARK_KIND_HARD_BREAK:
        return "br";
    case AMARK_KIND_SOFT_BREAK:
        return "sbr";
    case AMARK_KIND_BACKSLASH_ESCAPE:
        return "esc";
    case AMARK_KIND_CHARACTER_REFERENCE:
        return "ref";
    default:
        return "?";
    }
}

/* The parts asked for, in the order they are written, and their names. */
typedef struct dump_part {
    amark_text_part_t part;
    const char *name;
} dump_part_t;

static const dump_part_t k_parts[] = {
    {AMARK_TEXT_VALUE, "value"},
    {AMARK_TEXT_DESTINATION, "dest"},
    {AMARK_TEXT_TITLE, "title"},
    {AMARK_TEXT_INFO, "info"},
};

/* Writes the entries of node, each with a space before it but the first. */
static void put_node(dump_out_t *out, const amark_document_t *doc,
                     amark_node_id_t node)
{
    const char *kind = kind_name(amark_node_kind(doc, node));
    size_t i;
    char *value;
    size_t length;
    amark_error_t err;

    for (i = 0; i < sizeof(k_parts) / sizeof(k_parts[0]); i++) {
        value = NULL;
        length = 0;
        err = amark_node_text_alloc(doc, node, k_parts[i].part, &value,
                                    &length);
        if (err == AMARK_ERROR_NOT_AVAILABLE) {
            continue;
        }
        if (out->len > 0) {
            put_char(out, ' ');
        }
        put_str(out, kind);
        put_char(out, ':');
        put_str(out, k_parts[i].name);
        put_char(out, '=');
        if (err == AMARK_ERROR_NONE) {
            put_quoted(out, value, length);
            amark_text_free(doc, value);
        } else {
            put_str(out, "none");
        }
    }
}

bool amark_test_dump_text(const amark_document_t *doc, char *buffer,
                          size_t size)
{
    dump_out_t out;
    amark_node_id_t root = amark_document_root(doc);
    amark_node_id_t node = root;
    amark_node_id_t next;

    out.buffer = buffer;
    out.size = size;
    out.len = 0;

    /* Preorder: a node, then its children, then its next sibling. */
    while (node != AMARK_NODE_NONE) {
        put_node(&out, doc, node);
        next = amark_node_first_child(doc, node);
        while (next == AMARK_NODE_NONE && node != root) {
            next = amark_node_next_sibling(doc, node);
            if (next == AMARK_NODE_NONE) {
                node = amark_node_parent(doc, node);
                if (node == AMARK_NODE_NONE) {
                    break; /* a malformed tree: stop */
                }
            }
        }
        node = next;
    }

    if (size > 0) {
        buffer[out.len < size ? out.len : size - 1] = '\0';
    }
    return out.len < size;
}
