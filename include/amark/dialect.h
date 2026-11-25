/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Dialects: syntax extensions defined by the user of the library, with the
 * public headers only and without rebuilding it. A dialect is an ordered set
 * of extensions; each extension registers its own node kinds and the rules
 * that the parse calls at its hook points (block start, block continuation,
 * block close, inline trigger characters, delimiter runs and
 * postprocessing). Set it in amark_config_t.dialect.
 *
 * A dialect is plain data: write it as static const structures with
 * designated initializers. No allocation and no registration call is
 * needed. Every member of an extension other than its name may be 0 or
 * NULL. The library does not change the definition and keeps no reference
 * to it in the document, so it may be discarded once the parse returns.
 *
 * Rules an author must follow. A dialect that breaks them may make the
 * parse give different results for the same input, or break concurrent
 * parses that share the dialect:
 *   - A rule keeps no mutable state other than the per-document state that
 *     its extension's state_new creates.
 *   - A rule does no I/O and does not call abort, exit or longjmp.
 *   - A rule returns the same result for the same input.
 *   - A rule does not refer to anything it was given (inputs, the reply,
 *     the editor, the document) after it returns. The library copies what
 *     a rule writes to its result before the next call, and reads nothing
 *     a rule returned after that.
 *
 * Notes:
 *   - A reply of a close rule need not cover the whole range of the block
 *     being closed: the bytes of a gap between its top-level blocks belong
 *     to no node, and a reply that leaves out whole lines is accepted as
 *     it is. Covering the content is the author's responsibility.
 *   - amark_dialect_edit_set_kind drops the attributes of the node. Changing
 *     a CommonMark node (a link, say) to a dialect kind makes its
 *     attributes (the destination of the link) unreadable.
 *
 * The library checks every result before it uses it; an invalid result is
 * not used, a diagnostic is added to the document and the parse goes on.
 * An invalid definition is a configuration error (AMARK_ERROR_INVALID_CONFIG
 * from amark_config_validate and amark_parse).
 */

#ifndef AMARK_DIALECT_H
#define AMARK_DIALECT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <amark/allocator.h> /* amark_allocator_t */
#include <amark/config.h>    /* amark_dialect_t */
#include <amark/error.h>     /* amark_error_t */
#include <amark/export.h>    /* AMARK_API (generated) */
#include <amark/tree.h>      /* amark_document_t, amark_kind_t, amark_range_t */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The version of the dialect contract this header describes. Set
 * amark_dialect_t.abi to it. It is raised whenever a member is added.
 */
#define AMARK_DIALECT_ABI 1u

/* The most delimiter kinds one dialect may register, over all extensions. */
#define AMARK_DIALECT_DELIMITER_MAX 6

/* The longest run of a delimiter character that may be matched. */
#define AMARK_DIALECT_DELIMITER_LENGTH_MAX 4

/* The most columns amark_dialect_kind_t.blank_columns may be. */
#define AMARK_DIALECT_BLANK_COLUMNS_MAX 8

/*
 * The bit of amark_dialect_delimiter_t.lengths for runs of n characters,
 * 1 <= n <= AMARK_DIALECT_DELIMITER_LENGTH_MAX.
 */
#define AMARK_DIALECT_LENGTH(n) (1u << ((n) - 1))

/* The parent of a block of a reply that is at the top of the reply. */
#define AMARK_DIALECT_TOP ((size_t)-1)

/* The class of a dialect kind. */
typedef enum amark_dialect_class {
    AMARK_DIALECT_CONTAINER = 1, /* a block that may have block children */
    AMARK_DIALECT_LEAF = 2,      /* a block that has no block children */
    AMARK_DIALECT_INLINE = 3     /* an inline */
} amark_dialect_class_t;

/*
 * What a blank line does to an open block of a dialect kind. A blank line
 * is one whose rest (after the prefixes of the blocks that contain the
 * block) is spaces and tabs only; the continue rule is not called on it.
 * A container that goes on over it consumes its blank_columns as a list
 * item with children consumes its content indent: that many columns if the
 * line has as many columns of white space (part of a tab may be left), or
 * else all the white space. A leaf that goes on over it consumes nothing.
 */
typedef enum amark_dialect_blank {
    AMARK_DIALECT_BLANK_CLOSES = 0,   /* a blank line closes it */
    AMARK_DIALECT_BLANK_CONTINUES = 1 /* it goes on over a blank line */
} amark_dialect_blank_t;

/*
 * The reply of a close rule: the blocks that replace a closing paragraph,
 * or the attributes and the inline content of a closing dialect block. Its
 * layout is not public; fill it in with amark_dialect_reply_*.
 */
typedef struct amark_dialect_reply amark_dialect_reply_t;

/*
 * The editor of a postprocessing rule: the only way a rule changes the
 * tree. Its layout is not public; use it with amark_dialect_edit_*.
 */
typedef struct amark_dialect_editor amark_dialect_editor_t;

/* The lines of a block or of an inline content. Not public. */
struct amark_dialect_lines;

/*
 * A line as a block rule sees it. Offsets count from the start of the
 * content.
 *
 * For a block continue rule, position is where the prefixes of the blocks
 * that contain the open block end, first_nonspace the first non-space byte
 * from there, and indent the columns between the two. For a block start
 * rule, position and first_nonspace are both the first non-space byte after
 * the prefixes of the open blocks (and of any block opened before on the
 * line), column is its column, and indent is the columns from the end of
 * those prefixes to it: 0 to 3, as a start rule is called only there.
 */
typedef struct amark_dialect_line {
    const char *content;         /* the whole content */
    size_t content_length;
    size_t line;                 /* the line number, from 1 */
    size_t line_start;           /* the first byte of the line (in range) */
    size_t line_end;             /* the end of the line, before its ending */
    size_t position;             /* the position to decide on */
    size_t column;               /* its column (a tab is 4 wide) */
    size_t first_nonspace;       /* the first non-space byte from position */
    size_t indent;               /* columns of indentation before
                                    first_nonspace (see above) */
    amark_kind_t container_kind; /* start only: kind of the new block's parent */
    bool in_paragraph;           /* start only: innermost open block is a
                                    paragraph */
} amark_dialect_line_t;

/* The result of a block start rule. */
typedef struct amark_dialect_block_start_result {
    bool matched;           /* true: start the block */
    amark_range_t consumed; /* the start marker; start is line->position */
    uint64_t data;          /* a value kept with the block, given to the
                               continue and close rules */
} amark_dialect_block_start_result_t;

/* An open block of a dialect kind, as its continue rule sees it. */
typedef struct amark_dialect_open_block {
    amark_kind_t kind;
    amark_range_t range; /* from its start to the end of its last line */
    uint64_t data;
} amark_dialect_open_block_t;

/* The result of a block continue rule. */
typedef struct amark_dialect_block_continue_result {
    bool matched;           /* true: the block goes on */
    amark_range_t consumed; /* the prefix; start is line->position */
    uint64_t data;          /* the new value; left as is, it is unchanged */
} amark_dialect_block_continue_result_t;

/* A closing block, as a close rule sees it. */
typedef struct amark_dialect_closing {
    const char *content; /* the whole content */
    size_t content_length;
    amark_kind_t kind;   /* AMARK_KIND_PARAGRAPH or a block kind of the
                            extension */
    amark_range_t range;
    size_t line_count;   /* the number of lines of the block */
    uint64_t data;       /* 0 for a paragraph */
    const struct amark_dialect_lines *lines; /* for amark_dialect_closing_line */
} amark_dialect_closing_t;

/* A line of a closing block, from amark_dialect_closing_line. */
typedef struct amark_dialect_line_span {
    size_t start;         /* the first byte of the line */
    size_t content_start; /* where the content of the line starts */
    size_t end;           /* the end of the content of the line */
} amark_dialect_line_span_t;

/* The position of a trigger character, as an inline rule sees it. */
typedef struct amark_dialect_inline {
    const char *content; /* the whole content */
    size_t content_length;
    size_t position;               /* the trigger character */
    size_t line_end;               /* the end of the content of its line */
    amark_range_t inline_content;  /* the inline content being parsed */
    const struct amark_dialect_lines *lines; /* for
                                                amark_dialect_inline_next_line */
} amark_dialect_inline_t;

/* The result of an inline rule. */
typedef struct amark_dialect_inline_result {
    bool matched;        /* true: make the inline */
    amark_range_t range; /* start is in->position */
    const void *data;    /* the attribute bytes; NULL and 0 for none */
    size_t data_size;
} amark_dialect_inline_result_t;

/* The document, as a postprocessing rule sees it. */
typedef struct amark_dialect_post {
    const amark_document_t *doc; /* read it with the functions of tree.h */
    const char *content;
    size_t content_length;
} amark_dialect_post_t;

/*
 * The rules. state is the per-document state of the extension (NULL if it
 * has no state functions). A rule returns AMARK_ERROR_NONE on success;
 * AMARK_ERROR_NOMEM stops the parse, and any other error rejects the
 * rule's result.
 *
 * The postprocessing rule of each extension that has one is called once
 * per document, after the inline parse, in the order of the extensions.
 * Its edits are applied as they are made, so any other error it returns
 * adds a diagnostic but does not undo them; the next extension's rule is
 * called as usual. A rule may pass on the AMARK_ERROR_INVALID_INPUT that one
 * of its rejected edits returned: that adds no diagnostic besides the
 * edit's own. When an edit fails in a way the parse cannot go on from
 * (a failed allocation, a limit exceeded), the parse ends with that error
 * once the rule returns, whatever the rule returns.
 *
 * state_new returns AMARK_ERROR_NONE and may leave *state NULL; any other
 * value ends the parse with AMARK_ERROR_NOMEM, before any rule is called.
 * The allocator given to state_new and state_free is the parse's allocator,
 * at an address that is valid during the parse only: a state may keep the
 * pointer and use it until state_free returns, but not after amark_parse
 * returns.
 */
typedef amark_error_t (*amark_dialect_block_start_fn)(
    const amark_dialect_line_t *line, void *state,
    amark_dialect_block_start_result_t *out);
typedef amark_error_t (*amark_dialect_block_continue_fn)(
    const amark_dialect_line_t *line, const amark_dialect_open_block_t *block,
    void *state, amark_dialect_block_continue_result_t *out);
typedef amark_error_t (*amark_dialect_block_close_fn)(
    const amark_dialect_closing_t *closing, void *state,
    amark_dialect_reply_t *reply);
typedef amark_error_t (*amark_dialect_inline_fn)(
    const amark_dialect_inline_t *in, void *state,
    amark_dialect_inline_result_t *out);
typedef amark_error_t (*amark_dialect_post_fn)(
    const amark_dialect_post_t *post, void *state,
    amark_dialect_editor_t *editor);

/* Create and free the per-document state with the parse's allocator. */
typedef amark_error_t (*amark_dialect_state_new_fn)(
    const amark_allocator_t *allocator, void **state);
typedef void (*amark_dialect_state_free_fn)(const amark_allocator_t *allocator,
                                            void *state);

/* A node kind of an extension. */
typedef struct amark_dialect_kind {
    amark_kind_t id; /* AMARK_KIND_DIALECT_MIN or above */
    int cls;         /* an amark_dialect_class_t value */
    int blank;       /* an amark_dialect_blank_t value; blocks only */
    amark_dialect_block_continue_fn continue_fn; /* blocks only; NULL: a
                                                    non-blank line ends it */
    amark_dialect_block_close_fn close_fn;       /* blocks only; NULL: as it
                                                    is */
    unsigned blank_columns; /* containers that go on over a blank line
                               only: the columns they consume on one, like
                               a list item's content indent (see
                               amark_dialect_blank_t); 0 for other kinds; at
                               most AMARK_DIALECT_BLANK_COLUMNS_MAX */
} amark_dialect_kind_t;

/* A block start rule and the block kind of the extension it opens. */
typedef struct amark_dialect_block_start {
    amark_kind_t kind;
    bool interrupts_paragraph;
    amark_dialect_block_start_fn fn;
} amark_dialect_block_start_t;

/* An inline rule, its trigger and the inline kind of the extension it
 * makes. */
typedef struct amark_dialect_inline_rule {
    unsigned char trigger; /* 0x21 to 0x7E */
    amark_kind_t kind;
    amark_dialect_inline_fn fn;
} amark_dialect_inline_rule_t;

/* A delimiter kind: matched runs make an inline kind of the extension. */
typedef struct amark_dialect_delimiter {
    unsigned char ch; /* an ASCII punctuation character */
    unsigned lengths; /* a sum of AMARK_DIALECT_LENGTH(1) to (4) */
    amark_kind_t kind;
} amark_dialect_delimiter_t;

/* An extension. Every member other than name may be 0 or NULL. */
typedef struct amark_dialect_extension {
    const char *name;
    const amark_dialect_kind_t *kinds;
    size_t kind_count;
    const amark_dialect_block_start_t *block_starts;
    size_t block_start_count;
    amark_dialect_block_close_fn close_paragraph; /* NULL: none */
    const amark_dialect_inline_rule_t *inline_rules;
    size_t inline_rule_count;
    const amark_dialect_delimiter_t *delimiters;
    size_t delimiter_count;
    amark_dialect_post_fn postprocess;        /* NULL: none */
    amark_dialect_state_new_fn state_new;     /* both or neither */
    amark_dialect_state_free_fn state_free;
} amark_dialect_extension_t;

/* A dialect: its contract version and its extensions, in order. */
struct amark_dialect {
    size_t struct_size; /* sizeof(amark_dialect_t) */
    unsigned abi;       /* AMARK_DIALECT_ABI */
    const amark_dialect_extension_t *const *extensions;
    size_t extension_count;
};

/*
 * Line readers. Each returns false if an argument is NULL or there is no
 * such line.
 */

/*
 * Stores in *out line index (from 0) of the closing block. The start of the
 * first line and the end of the last line are cut to the block's range.
 */
AMARK_API bool amark_dialect_closing_line(const amark_dialect_closing_t *closing,
                                          size_t index,
                                          amark_dialect_line_span_t *out);

/*
 * If line_end is the end of the content of a line in the inline content and
 * a line follows it there, stores that line's [content start, content end)
 * in *out.
 */
AMARK_API bool amark_dialect_inline_next_line(const amark_dialect_inline_t *in,
                                              size_t line_end,
                                              amark_range_t *out);

/*
 * The reply of a close rule. Each function returns AMARK_ERROR_INVALID_INPUT
 * and changes nothing if reply is NULL or the request is invalid; once a
 * request is invalid the whole reply is rejected.
 */

/*
 * Adds a block of kind over range under parent (AMARK_DIALECT_TOP or the
 * index of a block added before) and stores its index in *index. Only the
 * reply of a closing paragraph has blocks to add.
 */
AMARK_API amark_error_t amark_dialect_reply_add_block(
    amark_dialect_reply_t *reply, size_t parent, amark_kind_t kind,
    amark_range_t range, size_t *index);

/* Copies size bytes at data as the attributes of block index (0: none). */
AMARK_API amark_error_t amark_dialect_reply_set_data(
    amark_dialect_reply_t *reply, size_t index, const void *data,
    size_t size);

/* Sets the range of leaf block index that is parsed as inlines. */
AMARK_API amark_error_t amark_dialect_reply_set_inline(
    amark_dialect_reply_t *reply, size_t index, amark_range_t range);

/*
 * The edits of a postprocessing rule. Each function returns
 * AMARK_ERROR_INVALID_INPUT and changes nothing if editor is NULL or the
 * edit is invalid (a diagnostic is added for the latter); a valid edit is
 * applied at once.
 */

/* Changes the kind of node; its attributes are dropped. */
AMARK_API amark_error_t amark_dialect_edit_set_kind(
    amark_dialect_editor_t *editor, amark_node_id_t node, amark_kind_t kind);

/* Copies size bytes at data as the attributes of node (0: none). */
AMARK_API amark_error_t amark_dialect_edit_set_data(
    amark_dialect_editor_t *editor, amark_node_id_t node, const void *data,
    size_t size);

/* Shrinks the range of node to range, within its current range. */
AMARK_API amark_error_t amark_dialect_edit_shrink(
    amark_dialect_editor_t *editor, amark_node_id_t node, amark_range_t range);

/*
 * Adds a child of kind over range to parent, in source order, searching
 * for its place from hint (AMARK_NODE_NONE or a child of parent), and
 * stores its ID in *out. The search does not go back before hint: the edit
 * is invalid (the siblings would be out of order) if hint ends after
 * range.start, or if hint is an empty node at range.start whose previous
 * sibling also starts at range.start or later. So a hint never changes the
 * place of the new child; it only shortens the search.
 */
AMARK_API amark_error_t amark_dialect_edit_add_child(
    amark_dialect_editor_t *editor, amark_node_id_t parent,
    amark_node_id_t hint, amark_kind_t kind, amark_range_t range,
    const void *data, size_t size, amark_node_id_t *out);

/*
 * Splits text node at byte at: node keeps [start, at) and a new text node,
 * its next sibling, takes [at, end). Stores the new node's ID in *second.
 */
AMARK_API amark_error_t amark_dialect_edit_split_text(
    amark_dialect_editor_t *editor, amark_node_id_t node, size_t at,
    amark_node_id_t *second);

#ifdef __cplusplus
}
#endif

#endif /* AMARK_DIALECT_H */
