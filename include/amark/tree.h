/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * The syntax tree of Amark as the public API sees it: the document handle,
 * node and kind IDs, ranges, the attribute values of each kind, and
 * diagnostics. The tree is read-only through the public API; nothing here
 * creates, removes or changes a node.
 */

#ifndef AMARK_TREE_H
#define AMARK_TREE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <amark/export.h> /* AMARK_API (generated) */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The result of one parse: the tree, its attributes and its diagnostics.
 * Its layout is not public; use it through pointers only.
 */
typedef struct amark_document amark_document_t;

/*
 * A node of a document, named by its ID within that document. The root has
 * ID 1. AMARK_NODE_NONE means "no node" and is never the ID of a node.
 */
typedef uint32_t amark_node_id_t;
#define AMARK_NODE_NONE ((amark_node_id_t)0)

/*
 * The kind of a node. The values are fixed and do not change between
 * versions of the library: 1 to 31 are blocks, 32 to 63 are inlines, 64 to
 * 0xFFFF are reserved for future CommonMark kinds, and AMARK_KIND_DIALECT_MIN
 * and above belong to dialects. Blank lines are not nodes and have no kind.
 */
typedef uint32_t amark_kind_t;
#define AMARK_KIND_NONE ((amark_kind_t)0)

/* Blocks: 1 to 31. */
#define AMARK_KIND_DOCUMENT ((amark_kind_t)1)
#define AMARK_KIND_BLOCK_QUOTE ((amark_kind_t)2)
#define AMARK_KIND_LIST ((amark_kind_t)3)
#define AMARK_KIND_LIST_ITEM ((amark_kind_t)4)
#define AMARK_KIND_THEMATIC_BREAK ((amark_kind_t)5)
#define AMARK_KIND_HEADING ((amark_kind_t)6)
#define AMARK_KIND_CODE_BLOCK ((amark_kind_t)7)
#define AMARK_KIND_HTML_BLOCK ((amark_kind_t)8)
#define AMARK_KIND_PARAGRAPH ((amark_kind_t)9)
#define AMARK_KIND_LINK_REFERENCE_DEFINITION ((amark_kind_t)10)

/* Inlines: 32 to 63. */
#define AMARK_KIND_TEXT ((amark_kind_t)32)
#define AMARK_KIND_CODE_SPAN ((amark_kind_t)33)
#define AMARK_KIND_EMPHASIS ((amark_kind_t)34)
#define AMARK_KIND_STRONG ((amark_kind_t)35)
#define AMARK_KIND_LINK ((amark_kind_t)36)
#define AMARK_KIND_IMAGE ((amark_kind_t)37)
#define AMARK_KIND_AUTOLINK ((amark_kind_t)38)
#define AMARK_KIND_RAW_HTML ((amark_kind_t)39)
#define AMARK_KIND_HARD_BREAK ((amark_kind_t)40)
#define AMARK_KIND_SOFT_BREAK ((amark_kind_t)41)
#define AMARK_KIND_BACKSLASH_ESCAPE ((amark_kind_t)42)
#define AMARK_KIND_CHARACTER_REFERENCE ((amark_kind_t)43)

/* Dialects: AMARK_KIND_DIALECT_MIN and above. */
#define AMARK_KIND_DIALECT_MIN ((amark_kind_t)0x10000)

/*
 * A range of bytes [start, end) of the content, counted from the start of
 * the content (not of the parsed range). start == end is an empty range.
 */
typedef struct amark_range {
    size_t start;
    size_t end;
} amark_range_t;

/*
 * The attribute values of the kinds. In each, 0 means "none": the node is
 * not of a kind that has the attribute.
 */

/* How a heading is written. */
typedef enum amark_heading_style {
    AMARK_HEADING_STYLE_NONE = 0,
    AMARK_HEADING_ATX = 1,   /* # Heading */
    AMARK_HEADING_SETEXT = 2 /* Heading, then a line of = or - */
} amark_heading_style_t;

/* Whether a list is a bullet list or an ordered list. */
typedef enum amark_list_type {
    AMARK_LIST_TYPE_NONE = 0,
    AMARK_LIST_BULLET = 1,
    AMARK_LIST_ORDERED = 2
} amark_list_type_t;

/* Whether a list is tight or loose. */
typedef enum amark_list_spacing {
    AMARK_LIST_SPACING_NONE = 0,
    AMARK_LIST_TIGHT = 1,
    AMARK_LIST_LOOSE = 2
} amark_list_spacing_t;

/* How a code block is written. */
typedef enum amark_code_block_style {
    AMARK_CODE_BLOCK_STYLE_NONE = 0,
    AMARK_CODE_BLOCK_INDENTED = 1,
    AMARK_CODE_BLOCK_FENCED = 2
} amark_code_block_style_t;

/* The form of a link or an image. */
typedef enum amark_link_form {
    AMARK_LINK_FORM_NONE = 0,
    AMARK_LINK_INLINE = 1,    /* [text](dest "title") */
    AMARK_LINK_FULL = 2,      /* [text][label] */
    AMARK_LINK_COLLAPSED = 3, /* [label][] */
    AMARK_LINK_SHORTCUT = 4   /* [label] */
} amark_link_form_t;

/* Whether an autolink is a URI or an email address. */
typedef enum amark_autolink_type {
    AMARK_AUTOLINK_TYPE_NONE = 0,
    AMARK_AUTOLINK_URI = 1,
    AMARK_AUTOLINK_EMAIL = 2
} amark_autolink_type_t;

/* How serious a diagnostic is. */
typedef enum amark_severity {
    AMARK_SEVERITY_WARNING = 1,
    AMARK_SEVERITY_ERROR = 2
} amark_severity_t;

/* A message that a parse left in a document, about one place in the content. */
typedef struct amark_diagnostic {
    size_t offset;             /* bytes from the start of the content */
    size_t line;               /* the line of offset; the first line is 1 */
    amark_severity_t severity;
    const char *message;       /* NUL-terminated; valid until the document
                                  is destroyed */
    const char *filename;      /* NULL: no file name; otherwise valid until
                                  the document is destroyed */
} amark_diagnostic_t;

/*
 * Reading the tree.
 *
 * None of the functions below changes the document, allocates memory or
 * takes time that grows with the size of the tree, so a document that is no
 * longer changed may be read from several threads at the same time. Node IDs
 * and ranges stay valid until the document is destroyed.
 *
 * A call that cannot be answered returns a fixed value instead: doc is NULL,
 * the ID is AMARK_NODE_NONE or larger than the number of nodes created, or
 * the ID is that of a node that was removed while the tree was built.
 * Functions that return an ID then return AMARK_NODE_NONE, those that return
 * a kind AMARK_KIND_NONE, those that return a line 0 (lines start at 1),
 * and those that return bool false, leaving *out unchanged.
 *
 * The parent, the first child and the next sibling are enough to visit
 * every node in document order, without recursion or a stack:
 *
 *     amark_node_id_t root = amark_document_root(doc);
 *     amark_node_id_t node = root;
 *     while (node != AMARK_NODE_NONE) {
 *         // visit node
 *         amark_node_id_t next = amark_node_first_child(doc, node);
 *         while (next == AMARK_NODE_NONE && node != root) {
 *             next = amark_node_next_sibling(doc, node);
 *             if (next == AMARK_NODE_NONE) {
 *                 node = amark_node_parent(doc, node);
 *             }
 *         }
 *         node = next;
 *     }
 */

/*
 * amark_kind_is_core returns true if kind is a defined CommonMark kind (1 to
 * 10 and 32 to 43). amark_kind_is_dialect returns true if kind is
 * AMARK_KIND_DIALECT_MIN or above. Both return false for AMARK_KIND_NONE and
 * for the reserved values.
 */
AMARK_API bool amark_kind_is_core(amark_kind_t kind);
AMARK_API bool amark_kind_is_dialect(amark_kind_t kind);

/* Returns the root (a node of AMARK_KIND_DOCUMENT), or AMARK_NODE_NONE. */
AMARK_API amark_node_id_t amark_document_root(const amark_document_t *doc);

/*
 * Return the parent, the first child and the next sibling of node, or
 * AMARK_NODE_NONE if it has none (the root has no parent and no sibling).
 */
AMARK_API amark_node_id_t amark_node_parent(const amark_document_t *doc,
                                            amark_node_id_t node);
AMARK_API amark_node_id_t amark_node_first_child(const amark_document_t *doc,
                                                 amark_node_id_t node);
AMARK_API amark_node_id_t amark_node_next_sibling(const amark_document_t *doc,
                                                  amark_node_id_t node);

/* Returns the kind of node. */
AMARK_API amark_kind_t amark_node_kind(const amark_document_t *doc,
                                       amark_node_id_t node);

/*
 * Return whether node is a block or an inline. A CommonMark kind is decided
 * by its ID (the document is a block); a dialect kind reports the class it
 * was created with.
 */
AMARK_API bool amark_node_is_block(const amark_document_t *doc,
                                   amark_node_id_t node);
AMARK_API bool amark_node_is_inline(const amark_document_t *doc,
                                    amark_node_id_t node);

/*
 * Stores the range [start, end) of node in *out and returns true. Returns
 * false and leaves *out unchanged if the call cannot be answered or out is
 * NULL.
 */
AMARK_API bool amark_node_range(const amark_document_t *doc,
                                amark_node_id_t node, amark_range_t *out);

/*
 * Return the line of the start of node and the line of its last byte (the
 * start line for an empty range). The first line is 1.
 */
AMARK_API size_t amark_node_start_line(const amark_document_t *doc,
                                       amark_node_id_t node);
AMARK_API size_t amark_node_end_line(const amark_document_t *doc,
                                     amark_node_id_t node);

/*
 * The attributes of each kind.
 *
 * Each reader below answers only for a node of the kind (and style or form)
 * it names, with its attributes set; otherwise it returns the fixed value as
 * above: 0 for an enumeration (its *_NONE), a number or a character, and
 * false for bool, leaving *out unchanged. A reader of a range returns false
 * when the part is not written, and true with start == end when it is
 * written but empty. Ranges count bytes from the start of the content.
 */

/* The level (1 to 6) and the style of a heading. */
AMARK_API unsigned amark_heading_level(const amark_document_t *doc,
                                       amark_node_id_t node);
AMARK_API amark_heading_style_t amark_heading_style(
    const amark_document_t *doc, amark_node_id_t node);

/*
 * The type and the spacing of a list. amark_list_bullet_char answers for a
 * bullet list only ('-', '+' or '*'); amark_list_delimiter ('.' or ')') and
 * amark_list_start (the start number, stored in *out) answer for an ordered
 * list only. amark_list_start returns false if out is NULL.
 */
AMARK_API amark_list_type_t amark_list_type(const amark_document_t *doc,
                                            amark_node_id_t node);
AMARK_API amark_list_spacing_t amark_list_spacing(const amark_document_t *doc,
                                                  amark_node_id_t node);
AMARK_API char amark_list_bullet_char(const amark_document_t *doc,
                                      amark_node_id_t node);
AMARK_API char amark_list_delimiter(const amark_document_t *doc,
                                    amark_node_id_t node);
AMARK_API bool amark_list_start(const amark_document_t *doc,
                                amark_node_id_t node, uint32_t *out);

/*
 * The style of a code block. The fence character ('`' or '~'), the length
 * of the opening fence and the range of the info string answer for a fenced
 * code block only. amark_code_block_info returns false if the info string is
 * not written or out is NULL.
 */
AMARK_API amark_code_block_style_t amark_code_block_style(
    const amark_document_t *doc, amark_node_id_t node);
AMARK_API char amark_code_block_fence_char(const amark_document_t *doc,
                                           amark_node_id_t node);
AMARK_API size_t amark_code_block_fence_length(const amark_document_t *doc,
                                               amark_node_id_t node);
AMARK_API bool amark_code_block_info(const amark_document_t *doc,
                                     amark_node_id_t node, amark_range_t *out);

/*
 * The attributes of a link or an image. The destination and the title
 * answer for the inline form only, when that part is written.
 * amark_link_definition answers for the reference forms (full, collapsed,
 * shortcut) only: it returns the link reference definition the link refers
 * to, whose destination and title are read with amark_definition_*.
 */
AMARK_API amark_link_form_t amark_link_form(const amark_document_t *doc,
                                            amark_node_id_t node);
AMARK_API bool amark_link_destination(const amark_document_t *doc,
                                      amark_node_id_t node,
                                      amark_range_t *out);
AMARK_API bool amark_link_title(const amark_document_t *doc,
                                amark_node_id_t node, amark_range_t *out);
AMARK_API amark_node_id_t amark_link_definition(const amark_document_t *doc,
                                                amark_node_id_t node);

/* The label, the destination and the title of a link reference definition. */
AMARK_API bool amark_definition_label(const amark_document_t *doc,
                                      amark_node_id_t node,
                                      amark_range_t *out);
AMARK_API bool amark_definition_destination(const amark_document_t *doc,
                                            amark_node_id_t node,
                                            amark_range_t *out);
AMARK_API bool amark_definition_title(const amark_document_t *doc,
                                      amark_node_id_t node,
                                      amark_range_t *out);

/* Whether an autolink is a URI or an email address. */
AMARK_API amark_autolink_type_t amark_autolink_type(
    const amark_document_t *doc, amark_node_id_t node);

/*
 * Returns the attribute bytes a dialect put on node, a node of a dialect
 * kind, and stores their number in *size if size is not NULL. Returns NULL
 * and stores 0 if there are none. The bytes are valid until the document is
 * destroyed and aligned for any type.
 */
AMARK_API const void *amark_node_dialect_data(const amark_document_t *doc,
                                              amark_node_id_t node,
                                              size_t *size);

/*
 * Releases doc and everything it holds (nodes, attributes, diagnostics)
 * through the allocator it was created with. The content is neither changed
 * nor released. Does nothing if doc is NULL.
 */
AMARK_API void amark_document_destroy(amark_document_t *doc);

/*
 * Returns the number of diagnostics of doc, or 0 if doc is NULL. The
 * diagnostics are numbered from 0 in the order they were added.
 */
AMARK_API size_t amark_document_diagnostic_count(const amark_document_t *doc);

/*
 * Stores the diagnostic number index of doc in *out and returns true. The
 * message and the file name in *out are copies the document holds, valid
 * until doc is destroyed. Returns false and leaves *out unchanged if doc or
 * out is NULL, or if index is not less than the number of diagnostics.
 */
AMARK_API bool amark_document_diagnostic(const amark_document_t *doc,
                                         size_t index,
                                         amark_diagnostic_t *out);

#ifdef __cplusplus
}
#endif

#endif /* AMARK_TREE_H */
