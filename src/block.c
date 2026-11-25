/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#include "block.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <amark/tree.h>

#include "array.h"
#include "definition.h"
#include "dialect_reply.h"
#include "line.h"
#include "parse_data.h"
#include "refdef.h"
#include "scan.h"
#include "tree_internal.h"

/*
 * The parse follows the "three phases per line" of CommonMark 0.31.2
 * (appendix "A parsing strategy"): for each line of the parsed range, (1)
 * match the open blocks from the outside in, (2) open new blocks where the
 * line starts them, (3) add the rest of the line to the innermost block, or
 * open a paragraph for it. The open blocks are an explicit stack in the
 * scratch arena; nothing recurses.
 *
 * Leaves get their nodes when they close; containers get theirs when they
 * open, since they become the parents of what opens inside them. A
 * container's range is set once, when it closes: while it is open its stack
 * element carries the end, and every child that closes reports its end to
 * its parent.
 *
 * The rules of each kind are in three functions, each one switch over the
 * kind: continue_block (does an open block continue on this line), start
 * blocks (does a new block start here) and close_block. Dialect kinds take
 * the default branches. With a dialect (run is not NULL), its block start
 * rules are called before the CommonMark starts, and a dialect container
 * takes the starts as the document does; a dialect block goes on over a
 * line by the continue rule of its kind, or over a blank line by its
 * registration. A dialect leaf records its lines as a paragraph does and
 * gets its node when it closes; a dialect container's node gets its range
 * then. Either calls the close rule of its kind once as it closes.
 */

#define TAB_STOP 4

/* The indentation, in columns, from which a line is indented code. */
#define CODE_INDENT 4

/* ------------------------------------------------------------------------ */
/* The position on a line                                                   */
/* ------------------------------------------------------------------------ */

void amark_line_pos_init(amark_line_pos_t *pos, size_t line,
                         const amark_line_bounds_t *bounds)
{
    pos->line = line;
    pos->content_end = bounds->content_end;
    pos->offset = bounds->start;
    pos->column = 0;
    pos->pad = 0;
    /* first_nonspace <= offset: the first search on the line scans. */
    pos->first_nonspace = bounds->start;
    pos->first_nonspace_column = 0;
    pos->indent = 0;
    pos->blank = bounds->start == bounds->content_end;
}

void amark_line_pos_find_nonspace(amark_line_pos_t *pos,
                                  const unsigned char *bytes)
{
    if (pos->first_nonspace <= pos->offset) {
        size_t i = pos->offset;
        size_t column = pos->column;

        while (i < pos->content_end) {
            if (bytes[i] == ' ') {
                column++;
            } else if (bytes[i] == '\t') {
                column += TAB_STOP - column % TAB_STOP;
            } else {
                break;
            }
            i++;
        }
        pos->first_nonspace = i;
        pos->first_nonspace_column = column;
    }
    /*
     * Otherwise only spaces and tabs lie between offset and first_nonspace
     * (the advances move offset forward only, and never past a byte that is
     * not a space or a tab without passing first_nonspace), so the earlier
     * result still holds; column already counts any part of a tab at offset
     * that was consumed.
     */
    pos->indent = pos->first_nonspace_column - pos->column;
    pos->blank = pos->first_nonspace == pos->content_end;
}

void amark_line_pos_advance(amark_line_pos_t *pos, const unsigned char *bytes,
                            size_t count, amark_advance_unit_t unit)
{
    while (count > 0 && pos->offset < pos->content_end) {
        if (bytes[pos->offset] == '\t') {
            size_t to_tab = TAB_STOP - pos->column % TAB_STOP;

            if (unit == AMARK_ADVANCE_COLUMNS) {
                if (to_tab > count) {
                    pos->column += count;
                    pos->pad = (unsigned)(to_tab - count);
                    return;
                }
                count -= to_tab;
            } else {
                count--;
            }
            pos->column += to_tab;
        } else {
            pos->column++;
            count--;
        }
        pos->offset++;
        pos->pad = 0;
    }
}

/* ------------------------------------------------------------------------ */
/* The stack of open blocks                                                 */
/* ------------------------------------------------------------------------ */

/*
 * An open block. Element 0 of the stack is the root (depth 0); the index of
 * an element is its nesting depth. Members that a kind does not use stay 0.
 */
typedef struct open_block {
    amark_kind_t kind;
    amark_node_id_t node; /* containers: made when opened; leaves:
                             AMARK_NODE_NONE until closed */
    size_t start;
    size_t end;           /* extended by each line added and by each child
                             that closes */

    /* Lists and list items. */
    size_t marker_offset;
    size_t padding;       /* the content indent of the item, in columns */
    amark_list_type_t list_type;
    char marker;
    uint32_t start_number;
    size_t content_indent; /* the sum of the content indents (marker_offset
                              plus padding) of the list items, and of the
                              blank_columns of the dialect containers, from
                              the root to this element, itself included;
                              every element has it, so the difference of two
                              is what the elements between them consume on
                              a blank line */

    /* Fenced code blocks. */
    char fence_char;
    size_t fence_length;
    size_t fence_offset;
    amark_opt_range_t info;

    /* HTML blocks. */
    unsigned html_type;

    /* Headings and paragraphs: the level, and the inline content. */
    unsigned level;
    amark_heading_style_t heading_style;
    size_t content_start;
    size_t content_end;

    /* Indented code blocks: the end of the last line that is not blank. */
    size_t last_nonblank_end;

    /*
     * Paragraphs: the link reference definitions at its start took every
     * line (before a setext underline), so it has no line yet. It stays
     * open as a paragraph for the rules that interrupt one; the next line
     * added becomes its first line, and closed without one it makes no
     * node.
     */
    bool emptied;

    /*
     * Containers: the number of blocks opened inside this one, leaves still
     * open and paragraphs that became definitions only included.
     */
    size_t child_count;

    /*
     * Dialect blocks: the registration of the kind (NULL for a CommonMark
     * kind), the number of the extension whose start rule opened it, and
     * the value the rules keep with it.
     */
    const amark_dialect_kind_t *dkind;
    size_t extension;
    uint64_t data;

    /* The blank line marks (tight and loose, and the blank line run). */
    bool ends_blank;       /* this block ended with a blank line */
    bool last_child_blank; /* the value its last closed child reported */
    bool blank_run;        /* lists and dialect containers that go on over
                              blank lines: on the previous line, the rest of
                              the line was blank here (see
                              add_blank_run_line) */
    bool loose;            /* lists: a blank line separates items or their
                              children */
} open_block_t;

typedef struct block_parser {
    amark_document_t *doc;
    amark_dialect_run_t *run; /* NULL: no dialect */
    const unsigned char *bytes;
    size_t max_depth;     /* 0: no limit */
    size_t max_nodes;     /* 0: no limit */
    amark_array_t stack;  /* open_block_t, in the scratch arena */
    amark_array_t definitions; /* amark_definition_entry_t, in document
                                  order, in the scratch arena */
    amark_line_pos_t pos;
    size_t line_start;    /* the first byte of the current line */
    bool all_matched;     /* every open block matched on this line */
    bool line_taken;      /* a leaf opened on this line took all of it */
    size_t break_kill;    /* no thematic break starts on this line before
                             this offset (see start_thematic_break) */
    size_t blank_mark;    /* the index of the one open element whose
                             ends_blank may be set (see note_blank_line) */
    amark_error_t failure; /* the error of a CONTINUE_FAILED result */
} block_parser_t;

static size_t stack_top(const block_parser_t *p)
{
    return amark_array_len(&p->stack) - 1;
}

static open_block_t *stack_at(const block_parser_t *p, size_t index)
{
    return (open_block_t *)amark_array_at(&p->stack, index);
}

/*
 * Creates a node of kind and class cls (AMARK_NODE_CLASS_AUTO for a
 * CommonMark kind) over [start, end) as the last child of the node of the
 * element at parent. Checks the node count limit first: the count includes
 * the root, and nothing is removed, so it is the number of nodes the
 * returned tree has.
 */
static amark_error_t append_node(block_parser_t *p, size_t parent,
                                 amark_kind_t kind, amark_node_class_t cls,
                                 size_t start, size_t end,
                                 amark_node_id_t *out)
{
    if (p->max_nodes != 0 &&
        amark_document_node_count(p->doc) >= p->max_nodes) {
        return AMARK_ERROR_LIMIT_EXCEEDED;
    }
    return amark_tree_append(p->doc, stack_at(p, parent)->node, kind, cls,
                             start, end, out);
}

/* Whether block is a dialect container. */
static bool is_dialect_container(const open_block_t *block)
{
    return block->dkind != NULL &&
           block->dkind->cls == AMARK_DIALECT_CONTAINER;
}

/*
 * Whether a block of kind child may be a child of the open block parent. A
 * dialect container, like a block quote, takes any block but a list item;
 * a dialect leaf takes none. A dialect block goes where a block quote does
 * (never into a list, which closes).
 */
static bool can_contain(const open_block_t *parent, amark_kind_t child)
{
    switch (parent->kind) {
    case AMARK_KIND_DOCUMENT:
    case AMARK_KIND_BLOCK_QUOTE:
    case AMARK_KIND_LIST_ITEM:
        return child != AMARK_KIND_LIST_ITEM;
    case AMARK_KIND_LIST:
        return child == AMARK_KIND_LIST_ITEM;
    default:
        return is_dialect_container(parent) &&
               child != AMARK_KIND_LIST_ITEM;
    }
}

/* ------------------------------------------------------------------------ */
/* Closing                                                                  */
/* ------------------------------------------------------------------------ */

/*
 * Takes the link reference definitions off the start of the paragraph
 * block, whose lines are all recorded: while its content starts with '[',
 * reads a definition with amark_refdef_parse, adds its node with its label,
 * destination and title under the element at parent, adds it to the
 * definitions for the table, and moves the start of the paragraph (and of
 * its inline content) to the content_start of the line after the
 * definition. Sets *rest to whether any of the paragraph is left.
 */
static amark_error_t extract_definitions(block_parser_t *p, size_t parent,
                                         open_block_t *block, bool *rest)
{
    size_t start = block->start;

    while (p->bytes[start] == '[') {
        amark_refdef_t def;
        amark_definition_entry_t entry;
        amark_line_record_t record;
        amark_node_id_t node;
        amark_error_t err;

        if (!amark_refdef_parse(p->doc, start, block->end, &def)) {
            break;
        }
        err = append_node(p, parent, AMARK_KIND_LINK_REFERENCE_DEFINITION,
                          AMARK_NODE_CLASS_AUTO, start, def.end, &node);
        if (err != AMARK_ERROR_NONE) {
            return err;
        }
        err = amark_tree_set_definition(p->doc, node, &def.label,
                                        &def.destination,
                                        def.has_title ? &def.title : NULL);
        if (err != AMARK_ERROR_NONE) {
            return err;
        }
        entry.node = node;
        entry.label_start = def.label.start;
        entry.label_end = def.label.end;
        err = amark_array_push(&p->definitions, &entry);
        if (err != AMARK_ERROR_NONE) {
            return err;
        }
        /*
         * A definition ending where the paragraph ends took its last line.
         * Otherwise the next line belongs to the paragraph, and its record
         * is its first non-space byte.
         */
        if (def.end >= block->end ||
            !amark_line_get(p->doc, def.last_line + 1, &record)) {
            *rest = false;
            return AMARK_ERROR_NONE;
        }
        start = record.content_start;
    }
    block->start = start;
    block->content_start = start;
    *rest = true;
    return AMARK_ERROR_NONE;
}

/*
 * Closes a heading: adds its node under the element at parent with its
 * level and style, and its inline content (of length 0 when it is empty).
 */
static amark_error_t close_heading(block_parser_t *p, size_t parent,
                                   const open_block_t *block)
{
    amark_node_id_t node;
    amark_error_t err;

    err = append_node(p, parent, AMARK_KIND_HEADING, AMARK_NODE_CLASS_AUTO,
                      block->start, block->end, &node);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    err = amark_tree_set_heading(p->doc, node, block->level,
                                 block->heading_style);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    return amark_inline_content_add(p->doc, node, block->content_start,
                                    block->content_end);
}

/*
 * Closes a code block: adds its node under the element at parent with its
 * style, fence and info. An indented code block ends at the end of its last
 * line that is not blank, so the blank lines after it are left out; it
 * reports that end to its parent too. Code is not inline content.
 */
static amark_error_t close_code_block(block_parser_t *p, size_t parent,
                                      open_block_t *block)
{
    amark_node_id_t node;
    amark_range_t info;
    amark_error_t err;

    if (block->fence_length == 0) {
        block->end = block->last_nonblank_end;
    }
    err = append_node(p, parent, AMARK_KIND_CODE_BLOCK, AMARK_NODE_CLASS_AUTO,
                      block->start, block->end, &node);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    if (block->fence_length == 0) {
        return amark_tree_set_code_block(p->doc, node,
                                         AMARK_CODE_BLOCK_INDENTED, '\0', 0,
                                         NULL);
    }
    info.start = block->info.start;
    info.end = block->info.end;
    return amark_tree_set_code_block(p->doc, node, AMARK_CODE_BLOCK_FENCED,
                                     block->fence_char, block->fence_length,
                                     block->info.present ? &info : NULL);
}

/*
 * Calls close rule fn of extension number extension for the block of kind
 * over range with data, with a reply of mode (6.1, 6.3), and stores in
 * *reply the reply and in *valid whether it may be applied. A status other
 * than AMARK_ERROR_NONE rejects the reply (10.4); a rejected reply adds its
 * diagnostic (amark_dialect_reply_finish). AMARK_ERROR_NOMEM from the rule,
 * a failure that lasts in the reply (AMARK_ERROR_NOMEM, or
 * AMARK_ERROR_LIMIT_EXCEEDED for attributes too large) or a failure to
 * begin the reply or to add the diagnostic is returned: the parse ends.
 */
static amark_error_t call_close_rule(block_parser_t *p, size_t extension,
                                     amark_dialect_block_close_fn fn,
                                     amark_dialect_reply_mode_t mode,
                                     amark_kind_t kind, amark_range_t range,
                                     uint64_t data,
                                     amark_dialect_reply_t **reply,
                                     bool *valid)
{
    struct amark_dialect_lines lines;
    amark_dialect_closing_t closing;
    amark_error_t status;

    *valid = false;
    status = amark_dialect_reply_begin(p->run, extension, mode, kind, range,
                                       reply);
    if (status != AMARK_ERROR_NONE) {
        return status;
    }
    amark_dialect_closing_init(&closing, &lines, p->doc, kind, range, data);
    status = fn(&closing, p->run->states[extension], *reply);
    if (status == AMARK_ERROR_NOMEM) {
        return AMARK_ERROR_NOMEM;
    }
    amark_dialect_reply_status(*reply, status);
    return amark_dialect_reply_finish(p->run, *reply, valid);
}

/*
 * Closes the dialect block block under the element at parent. A
 * container's node was made when it opened and gets its range now; then
 * the close rule of its kind, if any, is called with a container reply,
 * which can give the node attributes only (6.5). A leaf's close rule, if
 * any, is called with a leaf reply, whose block 0 is the leaf; without the
 * rule, or with a rejected reply, the leaf is a reply of block 0 alone
 * (6.11). The reply applied makes the leaf's node, with its attributes and
 * inline content (6.8), under the parent's node; the limits are checked
 * for it as for any node (10.11).
 */
static amark_error_t close_dialect_block(block_parser_t *p, size_t parent,
                                         const open_block_t *block)
{
    const amark_dialect_kind_t *dkind = block->dkind;
    bool is_container = dkind->cls == AMARK_DIALECT_CONTAINER;
    amark_dialect_reply_mode_t mode = is_container
                                          ? AMARK_DIALECT_REPLY_CONTAINER
                                          : AMARK_DIALECT_REPLY_LEAF;
    amark_dialect_reply_t *reply = NULL;
    amark_range_t range;
    bool valid = false;
    amark_error_t err;

    range.start = block->start;
    range.end = block->end;
    if (is_container) {
        amark_tree_set_range(p->doc, block->node, block->start, block->end);
    }
    if (dkind->close_fn != NULL) {
        err = call_close_rule(p, block->extension, dkind->close_fn, mode,
                              block->kind, range, block->data, &reply,
                              &valid);
        if (err != AMARK_ERROR_NONE) {
            return err;
        }
    }
    if (!valid) {
        if (is_container) {
            return AMARK_ERROR_NONE;
        }
        /* The leaf as it is: a fresh reply holds block 0 alone. */
        err = amark_dialect_reply_begin(p->run, block->extension, mode,
                                        block->kind, range, &reply);
        if (err != AMARK_ERROR_NONE) {
            return err;
        }
    }
    return amark_dialect_reply_apply(
        p->run, reply, stack_at(p, parent)->node, parent,
        is_container ? block->node : AMARK_NODE_NONE, p->max_depth,
        p->max_nodes);
}

/*
 * Calls the paragraph close rules of the extensions of the dialect, in
 * their order, for the paragraph block whose link reference definitions
 * were taken off and of which some is left (6.2), each with an empty
 * paragraph reply. The first reply that has blocks and passes the checks
 * is applied under the element at parent, and *replaced is set; a reply
 * without blocks keeps the paragraph, and a rejected one adds its
 * diagnostic (6.11): the next extension is called. Each rule is called
 * once for the paragraph.
 */
static amark_error_t replace_paragraph(block_parser_t *p, size_t parent,
                                       const open_block_t *block,
                                       bool *replaced)
{
    const amark_dialect_t *dialect = p->run->dialect;
    amark_range_t range;
    size_t i;

    *replaced = false;
    range.start = block->start;
    range.end = block->end;
    for (i = 0; i < dialect->extension_count; i++) {
        amark_dialect_block_close_fn fn =
            dialect->extensions[i]->close_paragraph;
        amark_dialect_reply_t *reply = NULL;
        bool valid = false;
        amark_error_t err;

        if (fn == NULL) {
            continue;
        }
        err = call_close_rule(p, i, fn, AMARK_DIALECT_REPLY_PARAGRAPH,
                              AMARK_KIND_PARAGRAPH, range, 0, &reply,
                              &valid);
        if (err != AMARK_ERROR_NONE) {
            return err;
        }
        if (valid) {
            *replaced = true;
            return amark_dialect_reply_apply(
                p->run, reply, stack_at(p, parent)->node, parent,
                AMARK_NODE_NONE, p->max_depth, p->max_nodes);
        }
    }
    return AMARK_ERROR_NONE;
}

/*
 * Closes a paragraph: takes the link reference definitions off its start,
 * then, if anything is left, lets the paragraph close rules of the dialect
 * replace it, if there is a dialect; if none does, adds a node for it under
 * the element at parent, and its inline content. A paragraph underlined as
 * a setext heading became a heading before it closed (6.13).
 */
static amark_error_t close_paragraph(block_parser_t *p, size_t parent,
                                     open_block_t *block)
{
    amark_node_id_t node;
    bool rest;
    amark_error_t err;

    if (block->emptied) {
        return AMARK_ERROR_NONE;
    }
    err = extract_definitions(p, parent, block, &rest);
    if (err != AMARK_ERROR_NONE || !rest) {
        return err;
    }
    if (p->run != NULL) {
        bool replaced;

        err = replace_paragraph(p, parent, block, &replaced);
        if (err != AMARK_ERROR_NONE || replaced) {
            return err;
        }
    }
    err = append_node(p, parent, AMARK_KIND_PARAGRAPH, AMARK_NODE_CLASS_AUTO,
                      block->start, block->end, &node);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    return amark_inline_content_add(p->doc, node, block->content_start,
                                    block->content_end);
}

/*
 * The closing rules of each kind, for block, just taken off the stack;
 * parent is the index of its parent element (now the top).
 */
static amark_error_t close_block(block_parser_t *p, size_t parent,
                                 open_block_t *block)
{
    amark_node_id_t node;

    switch (block->kind) {
    case AMARK_KIND_PARAGRAPH:
        return close_paragraph(p, parent, block);
    case AMARK_KIND_BLOCK_QUOTE:
    case AMARK_KIND_LIST_ITEM:
        /* The node was made when the container opened. */
        amark_tree_set_range(p->doc, block->node, block->start, block->end);
        return AMARK_ERROR_NONE;
    case AMARK_KIND_LIST:
        /* The spacing is decided now; the list opened tight. */
        amark_tree_set_range(p->doc, block->node, block->start, block->end);
        return amark_tree_set_list(p->doc, block->node, block->list_type,
                                   block->marker, block->start_number,
                                   block->loose ? AMARK_LIST_LOOSE
                                                : AMARK_LIST_TIGHT);
    case AMARK_KIND_HEADING:
        return close_heading(p, parent, block);
    case AMARK_KIND_THEMATIC_BREAK:
        return append_node(p, parent, AMARK_KIND_THEMATIC_BREAK,
                           AMARK_NODE_CLASS_AUTO, block->start, block->end,
                           &node);
    case AMARK_KIND_CODE_BLOCK:
        return close_code_block(p, parent, block);
    case AMARK_KIND_HTML_BLOCK:
        /* Raw HTML is not inline content. */
        return append_node(p, parent, AMARK_KIND_HTML_BLOCK,
                           AMARK_NODE_CLASS_AUTO, block->start, block->end,
                           &node);
    default:
        return close_dialect_block(p, parent, block);
    }
}

/*
 * Takes the innermost element off the stack (never the root), closes it and
 * reports its end and its blank line mark to its parent.
 */
static amark_error_t close_top(block_parser_t *p)
{
    open_block_t block;
    open_block_t *parent;
    size_t parent_index;
    amark_error_t err;

    (void)amark_array_pop(&p->stack, &block);
    parent_index = stack_top(p);
    err = close_block(p, parent_index, &block);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    parent = stack_at(p, parent_index);
    if (block.end > parent->end) {
        parent->end = block.end;
    }
    if ((block.kind == AMARK_KIND_LIST || block.kind == AMARK_KIND_LIST_ITEM) &&
        block.child_count > 0) {
        parent->last_child_blank = block.last_child_blank;
    } else {
        parent->last_child_blank = block.ends_blank;
    }
    return AMARK_ERROR_NONE;
}

/* Closes elements until the element at index is the innermost. */
static amark_error_t close_above(block_parser_t *p, size_t index)
{
    while (stack_top(p) > index) {
        amark_error_t err = close_top(p);

        if (err != AMARK_ERROR_NONE) {
            return err;
        }
    }
    return AMARK_ERROR_NONE;
}

/*
 * Makes the list loose when a blank line separates the block about to be
 * added inside the element at parent from the one before it. The earlier
 * siblings have closed, so the parent holds the value its last child
 * reported. An item added to a list follows an item that ended with a blank
 * line, itself or its last child (item_blank is the item's own mark, read
 * before it closed); a second or later child of an item follows a child that
 * ended with a blank line, and the item's list is the one made loose.
 */
static void note_spacing(block_parser_t *p, size_t parent, amark_kind_t kind,
                         bool item_blank)
{
    open_block_t *block = stack_at(p, parent);

    if (block->child_count == 0) {
        return;
    }
    if (block->kind == AMARK_KIND_LIST && kind == AMARK_KIND_LIST_ITEM) {
        if (item_blank || block->last_child_blank) {
            block->loose = true;
        }
    } else if (block->kind == AMARK_KIND_LIST_ITEM) {
        if (block->last_child_blank) {
            /* An item's parent is always its list. */
            stack_at(p, parent - 1)->loose = true;
        }
    }
}

/*
 * Opens a block of kind starting at start inside the element at parent:
 * closes every element above parent, then parents that cannot contain kind
 * (never the root), checks the nesting depth limit, makes the node of a
 * container, and pushes the element. Stores its index in *index. A list
 * that gets a new item or an item that gets a new child may become loose
 * (note_spacing).
 */
static amark_error_t push_block(block_parser_t *p, size_t parent,
                                amark_kind_t kind, bool is_container,
                                size_t start, size_t *index)
{
    open_block_t block;
    size_t depth;
    bool item_blank = false;
    amark_error_t err;

    /* The open element just inside parent, if any, is its last child. */
    if (stack_top(p) > parent) {
        item_blank = stack_at(p, parent + 1)->ends_blank;
    }
    err = close_above(p, parent);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    while (stack_top(p) > 0 && !can_contain(stack_at(p, stack_top(p)),
                                            kind)) {
        err = close_top(p);
        if (err != AMARK_ERROR_NONE) {
            return err;
        }
    }
    if (stack_top(p) != parent) {
        /* parent closed: its own last child is another element. */
        parent = stack_top(p);
        item_blank = false;
    }
    note_spacing(p, parent, kind, item_blank);
    depth = parent + 1;
    if (p->max_depth != 0 && depth > p->max_depth) {
        return AMARK_ERROR_LIMIT_EXCEEDED;
    }

    memset(&block, 0, sizeof(block));
    block.kind = kind;
    block.node = AMARK_NODE_NONE;
    block.start = start;
    block.end = start;
    block.content_indent = stack_at(p, parent)->content_indent;
    if (is_container) {
        err = append_node(p, parent, kind,
                          kind >= AMARK_KIND_DIALECT_MIN
                              ? AMARK_NODE_CLASS_BLOCK
                              : AMARK_NODE_CLASS_AUTO,
                          start, start, &block.node);
        if (err != AMARK_ERROR_NONE) {
            return err;
        }
    }
    err = amark_array_push(&p->stack, &block);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    stack_at(p, parent)->child_count++;
    *index = depth;
    return AMARK_ERROR_NONE;
}

/* ------------------------------------------------------------------------ */
/* Continuing and starting                                                  */
/* ------------------------------------------------------------------------ */

typedef enum continue_result {
    CONTINUE_NO,        /* the block does not continue on this line */
    CONTINUE_YES,       /* it continues; its prefix is consumed */
    CONTINUE_LINE_DONE, /* it took the whole line and ends with it (a
                           closing fence); the caller closes it */
    CONTINUE_BLANK_RUN, /* a list, or a dialect container that goes on over
                           blank lines, where the rest of the line is
                           blank, as it was on the previous line: every
                           element inside it continues (see
                           add_blank_run_line) */
    CONTINUE_FAILED     /* the parse ends with the error in failure */
} continue_result_t;

/*
 * Whether the rest of the line starts with a block quote marker: at most 3
 * columns of indentation, then '>'.
 */
static bool at_quote_marker(const block_parser_t *p)
{
    const amark_line_pos_t *pos = &p->pos;

    return pos->indent < CODE_INDENT && !pos->blank &&
           p->bytes[pos->first_nonspace] == '>';
}

/*
 * Consumes a block quote marker (at_quote_marker is true): the indentation,
 * the '>', and one column of a following space or tab. A tab is consumed by
 * columns, so a tab wider than one column leaves the rest of its columns as
 * pad, which counts as indentation of what follows.
 */
static void consume_quote_marker(block_parser_t *p)
{
    amark_line_pos_t *pos = &p->pos;

    amark_line_pos_advance(pos, p->bytes, pos->indent,
                           AMARK_ADVANCE_COLUMNS);
    amark_line_pos_advance(pos, p->bytes, 1, AMARK_ADVANCE_BYTES);
    if (pos->offset < pos->content_end &&
        (p->bytes[pos->offset] == ' ' || p->bytes[pos->offset] == '\t')) {
        amark_line_pos_advance(pos, p->bytes, 1, AMARK_ADVANCE_COLUMNS);
    }
}

/*
 * The continuation of a code block. Indented: 4 columns of indentation, or
 * a blank line, whose white space it consumes whole. Fenced: a closing fence
 * (at most 3 columns of indentation, the same character, at least as long)
 * takes the line, which is recorded at its first character; any other line
 * continues, with up to fence_offset columns of white space consumed.
 */
static continue_result_t continue_code_block(block_parser_t *p,
                                             open_block_t *block)
{
    amark_line_pos_t *pos = &p->pos;

    if (block->fence_length == 0) {
        if (pos->indent >= CODE_INDENT) {
            amark_line_pos_advance(pos, p->bytes, CODE_INDENT,
                                   AMARK_ADVANCE_COLUMNS);
            return CONTINUE_YES;
        }
        if (pos->blank) {
            amark_line_pos_advance(pos, p->bytes,
                                   pos->first_nonspace - pos->offset,
                                   AMARK_ADVANCE_BYTES);
            return CONTINUE_YES;
        }
        return CONTINUE_NO;
    }
    if (pos->indent < CODE_INDENT &&
        amark_scan_close_fence(p->bytes + pos->first_nonspace,
                               pos->content_end - pos->first_nonspace,
                               block->fence_char) >= block->fence_length) {
        amark_line_set(p->doc, pos->line, pos->first_nonspace, 0);
        block->end = pos->content_end;
        return CONTINUE_LINE_DONE;
    }
    amark_line_pos_advance(pos, p->bytes,
                           pos->indent < block->fence_offset
                               ? pos->indent
                               : block->fence_offset,
                           AMARK_ADVANCE_COLUMNS);
    return CONTINUE_YES;
}

/*
 * The continuation of a list item: indentation of at least its content
 * indent, which is consumed by columns (a tab crossing it keeps the rest of
 * its columns as pad), or a blank line, whose white space it consumes whole,
 * once something has opened inside the item. An item whose marker line was
 * blank and that has nothing in it yet does not go on over a blank line: it
 * can begin with one blank line at most.
 */
static continue_result_t continue_list_item(block_parser_t *p,
                                            const open_block_t *block)
{
    amark_line_pos_t *pos = &p->pos;
    size_t content_indent = block->marker_offset + block->padding;

    if (pos->indent >= content_indent) {
        amark_line_pos_advance(pos, p->bytes, content_indent,
                               AMARK_ADVANCE_COLUMNS);
        return CONTINUE_YES;
    }
    if (pos->blank && block->child_count > 0) {
        amark_line_pos_advance(pos, p->bytes,
                               pos->first_nonspace - pos->offset,
                               AMARK_ADVANCE_BYTES);
        return CONTINUE_YES;
    }
    return CONTINUE_NO;
}

/*
 * Whether every element inside the list at index (or the dialect container
 * that goes on over blank lines) continues on the current line, the rest of
 * which is blank here as it was on the previous line (its blank_run). That
 * line closed every element inside it that a blank line does not continue,
 * and opened nothing, so what is left inside it continues on any blank
 * line: lists, items with children, dialect blocks registered to go on over
 * blank lines, and at the top an indented or fenced code block or an HTML
 * block of conditions 1 to 5 (a paragraph, a block quote, an HTML block of
 * conditions 6 and 7 and a dialect block registered to close do not go on
 * over a blank line). The one exception is an item with nothing
 * in it, which can only be the top: it went on over the previous line by
 * its indentation (a line of white space), and goes on over this one only
 * with as many columns as the elements from the one at index to it consume
 * (the dialect container at index consumes its blank_columns, a list
 * nothing), the difference of the sums from the root of it and of the
 * parent of the one at index; otherwise it closes, and the elements are
 * matched one by one. That happens once for the item.
 */
static bool blank_run_continues(const block_parser_t *p, size_t index)
{
    const open_block_t *top = stack_at(p, stack_top(p));

    if (top->kind == AMARK_KIND_LIST_ITEM && top->child_count == 0) {
        return p->pos.indent >=
               top->content_indent - stack_at(p, index - 1)->content_indent;
    }
    return true;
}

/*
 * The continuation of the dialect block at index, block (5.1 to 5.9).
 *
 * When the rest of the line is blank, the rule is not called: the
 * registration of the kind closes the block or continues it (5.6). A
 * container that continues consumes its blank_columns as a list item with
 * children consumes its content indent: by columns (a tab crossing it keeps
 * the rest of its columns as pad) when the line has as many, otherwise all
 * of the white space; a leaf consumes nothing. A container that goes on
 * over blank lines keeps the blank
 * line run mark as a list does, and from the second blank line of a run the
 * elements inside it are not matched (CONTINUE_BLANK_RUN): the previous
 * line closed every element inside it that a blank line does not continue,
 * the dialect blocks among them, which close on a blank line by their
 * registration, so the rest continue on this one. A run of blank lines
 * after deeply nested containers thus costs no more per line than one
 * element.
 *
 * On a line that is not blank, a kind without a continue rule does not
 * continue (5.7); otherwise its rule is called once, at the current
 * position, which may be on white space, with *out filled with "no" and the
 * block's data (5.1, 5.2). A result is valid when its range starts at the
 * position (ORIGIN) and does not end before it starts or after the end of
 * the line's content (LINE) (5.8); a status other than AMARK_ERROR_NONE and
 * AMARK_ERROR_NOMEM is rejected too (STATUS, 10.4). An accepted result
 * consumes its range by bytes (a tab is taken whole), keeps the data it
 * gives, and a container ends at least at the end of the line's content
 * (5.4, 5.11). "No", or a rejected result with its diagnostic at the
 * position, is a block that does not continue (5.5, 5.9).
 * AMARK_ERROR_NOMEM from the rule, or from adding the diagnostic, ends the
 * parse (10.10).
 */
static continue_result_t continue_dialect_block(block_parser_t *p,
                                                size_t index,
                                                open_block_t *block)
{
    amark_line_pos_t *pos = &p->pos;
    const amark_dialect_kind_t *dkind = block->dkind;
    amark_dialect_line_t line;
    amark_dialect_open_block_t open;
    amark_dialect_block_continue_result_t out;
    amark_dialect_reason_t reason;
    amark_error_t status;

    if (pos->blank) {
        if (dkind->blank != AMARK_DIALECT_BLANK_CONTINUES) {
            return CONTINUE_NO;
        }
        if (dkind->cls == AMARK_DIALECT_CONTAINER) {
            if (block->blank_run && blank_run_continues(p, index)) {
                return CONTINUE_BLANK_RUN;
            }
            block->blank_run = true;
            if (pos->indent >= dkind->blank_columns) {
                amark_line_pos_advance(pos, p->bytes, dkind->blank_columns,
                                       AMARK_ADVANCE_COLUMNS);
            } else {
                amark_line_pos_advance(pos, p->bytes,
                                       pos->first_nonspace - pos->offset,
                                       AMARK_ADVANCE_BYTES);
            }
        }
        return CONTINUE_YES;
    }
    block->blank_run = false;
    if (dkind->continue_fn == NULL) {
        return CONTINUE_NO;
    }

    memset(&line, 0, sizeof(line));
    line.content = (const char *)p->bytes;
    line.content_length = amark_document_content_length(p->doc);
    line.line = pos->line;
    line.line_start = p->line_start;
    line.line_end = pos->content_end;
    line.position = pos->offset;
    line.column = pos->column;
    line.first_nonspace = pos->first_nonspace;
    line.indent = pos->indent;
    open.kind = block->kind;
    open.range.start = block->start;
    open.range.end = block->end;
    open.data = block->data;
    memset(&out, 0, sizeof(out));
    out.data = block->data;

    status = dkind->continue_fn(&line, &open,
                                p->run->states[block->extension], &out);
    if (status == AMARK_ERROR_NOMEM) {
        p->failure = AMARK_ERROR_NOMEM;
        return CONTINUE_FAILED;
    }
    if (status != AMARK_ERROR_NONE) {
        reason = AMARK_DIALECT_REASON_STATUS;
    } else if (!out.matched) {
        return CONTINUE_NO;
    } else if (out.consumed.start != pos->offset) {
        reason = AMARK_DIALECT_REASON_ORIGIN;
    } else if (out.consumed.end < out.consumed.start ||
               out.consumed.end > pos->content_end) {
        reason = AMARK_DIALECT_REASON_LINE;
    } else {
        amark_line_pos_advance(pos, p->bytes,
                               out.consumed.end - pos->offset,
                               AMARK_ADVANCE_BYTES);
        block->data = out.data;
        if (dkind->cls == AMARK_DIALECT_CONTAINER) {
            block->end = pos->content_end;
        }
        return CONTINUE_YES;
    }
    status = amark_dialect_reject(p->run, block->extension,
                                  AMARK_DIALECT_HOOK_BLOCK_CONTINUE, reason,
                                  pos->offset);
    if (status != AMARK_ERROR_NONE) {
        p->failure = status;
        return CONTINUE_FAILED;
    }
    return CONTINUE_NO;
}

/*
 * The continuation rules of each kind: whether the element at index
 * continues on the current line, consuming its prefix if it does. The
 * position is at the first byte the element may look at, with
 * amark_line_pos_find_nonspace done.
 */
static continue_result_t continue_block(block_parser_t *p, size_t index)
{
    open_block_t *block = stack_at(p, index);

    switch (block->kind) {
    case AMARK_KIND_PARAGRAPH:
        return p->pos.blank ? CONTINUE_NO : CONTINUE_YES;
    case AMARK_KIND_THEMATIC_BREAK:
    case AMARK_KIND_HEADING:
        /* One-line leaves never continue. */
        return CONTINUE_NO;
    case AMARK_KIND_CODE_BLOCK:
        return continue_code_block(p, block);
    case AMARK_KIND_HTML_BLOCK:
        /*
         * Conditions 1 to 5 end on the line with their end condition (seen
         * when the line is added); 6 and 7 end before a blank line, which
         * is not part of the block. Nothing is consumed.
         */
        if (block->html_type >= 6 && p->pos.blank) {
            return CONTINUE_NO;
        }
        return CONTINUE_YES;
    case AMARK_KIND_BLOCK_QUOTE:
        /*
         * A line with the marker continues the quote, which then ends at
         * least at the end of that line's content.
         */
        if (!at_quote_marker(p)) {
            return CONTINUE_NO;
        }
        consume_quote_marker(p);
        block->end = p->pos.content_end;
        return CONTINUE_YES;
    case AMARK_KIND_LIST:
        /*
         * A list goes on while its items do, or a new item can join it. It
         * keeps whether the rest of the line was blank here, for the cutoff
         * on the next line.
         */
        if (!p->pos.blank) {
            block->blank_run = false;
        } else if (block->blank_run && blank_run_continues(p, index)) {
            return CONTINUE_BLANK_RUN;
        } else {
            block->blank_run = true;
        }
        return CONTINUE_YES;
    case AMARK_KIND_LIST_ITEM:
        return continue_list_item(p, block);
    default:
        return continue_dialect_block(p, index, block);
    }
}

typedef enum start_result {
    START_NONE,      /* nothing starts here */
    START_CONTAINER, /* a container opened; look for more inside it */
    START_LEAF       /* a leaf opened; the rest of the line is its own */
} start_result_t;

/*
 * Opens a leaf of kind whose first line is the rest of the current line,
 * from its first non-space byte to the end of the line's content, inside the
 * element at *container, and makes *container its index. The record of the
 * line is record_start. The leaf takes the whole line: nothing of it is
 * added in the third phase.
 */
static amark_error_t open_line_leaf(block_parser_t *p, size_t *container,
                                    amark_kind_t kind, size_t record_start,
                                    start_result_t *result)
{
    const amark_line_pos_t *pos = &p->pos;
    size_t index;
    amark_error_t err;

    err = push_block(p, *container, kind, false, pos->first_nonspace,
                     &index);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    stack_at(p, index)->end = pos->content_end;
    amark_line_set(p->doc, pos->line, record_start, 0);
    p->line_taken = true;
    *container = index;
    *result = START_LEAF;
    return AMARK_ERROR_NONE;
}

/*
 * Starts a block quote if the rest of the line starts with its marker. The
 * caller has checked that the indentation is at most 3 columns. The quote
 * starts at '>' and its node is made now; the marker is consumed, and the
 * starts are looked for again inside the quote. The quote ends at least at
 * the end of the line's content.
 */
static amark_error_t start_block_quote(block_parser_t *p, size_t *container,
                                       start_result_t *result)
{
    size_t index;
    amark_error_t err;

    if (!at_quote_marker(p)) {
        return AMARK_ERROR_NONE;
    }
    err = push_block(p, *container, AMARK_KIND_BLOCK_QUOTE, true,
                     p->pos.first_nonspace, &index);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    consume_quote_marker(p);
    stack_at(p, index)->end = p->pos.content_end;
    *container = index;
    *result = START_CONTAINER;
    return AMARK_ERROR_NONE;
}

/*
 * Starts an ATX heading if the rest of the line is one. The caller has
 * checked that the indentation is at most 3 columns. The record of the line
 * is the start of the heading's content.
 */
static amark_error_t start_atx_heading(block_parser_t *p, size_t *container,
                                       start_result_t *result)
{
    const amark_line_pos_t *pos = &p->pos;
    size_t first = pos->first_nonspace;
    unsigned level;
    size_t content_start;
    size_t content_end;
    open_block_t *block;
    amark_error_t err;

    if (!amark_scan_atx_heading(p->bytes + first, pos->content_end - first,
                                &level, &content_start, &content_end)) {
        return AMARK_ERROR_NONE;
    }
    err = open_line_leaf(p, container, AMARK_KIND_HEADING,
                         first + content_start, result);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    block = stack_at(p, *container);
    block->level = level;
    block->heading_style = AMARK_HEADING_ATX;
    block->content_start = first + content_start;
    block->content_end = first + content_end;
    return AMARK_ERROR_NONE;
}

/*
 * Handles a setext heading underline. The caller has checked that the
 * indentation is at most 3 columns. Only a paragraph that is the innermost
 * open block, with every open block matched (so not on a lazy continuation
 * line), is underlined. The link reference definitions at the start of the
 * paragraph are taken off first. If some of the paragraph is left, it
 * becomes a heading over its lines and the underline, with the paragraph's
 * inline content, and closes; the underline line is recorded at its first
 * character. If nothing is left, the paragraph stays open with no line, and
 * the line is tried as the start of other blocks; if none starts, it
 * becomes the paragraph's first line.
 */
static amark_error_t start_setext_heading(block_parser_t *p,
                                          size_t *container,
                                          start_result_t *result)
{
    const amark_line_pos_t *pos = &p->pos;
    size_t first = pos->first_nonspace;
    open_block_t *block = stack_at(p, *container);
    unsigned level;
    bool rest;
    amark_error_t err;

    if (block->kind != AMARK_KIND_PARAGRAPH || block->emptied ||
        *container != stack_top(p) || !p->all_matched) {
        return AMARK_ERROR_NONE;
    }
    level = amark_scan_setext_underline(p->bytes + first,
                                        pos->content_end - first);
    if (level == 0) {
        return AMARK_ERROR_NONE;
    }
    err = extract_definitions(p, *container - 1, block, &rest);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    if (!rest) {
        block->emptied = true;
        return AMARK_ERROR_NONE;
    }
    block->kind = AMARK_KIND_HEADING;
    block->level = level;
    block->heading_style = AMARK_HEADING_SETEXT;
    block->end = pos->content_end;
    amark_line_set(p->doc, pos->line, first, 0);
    p->line_taken = true;
    *container -= 1;
    *result = START_LEAF;
    return close_above(p, *container);
}

/*
 * Starts a thematic break if the rest of the line is one. The caller has
 * checked that the indentation is at most 3 columns. The record of the line
 * is the first character of the break.
 *
 * A line of nested list items ("- - - ... a") is tried at each marker, and
 * each scan reads to the end of the line, which would make the line
 * quadratic. So when a scan fails, the run of its first character and white
 * space after it is skipped by the later tries on the same line: a break
 * starting inside the run would read the same characters up to the same
 * byte that ended the scan (or the same end of line with fewer characters),
 * and fail too. A try at or after the end of the run scans again (as
 * "- * * *", where the break starts at the '*' that ended the run).
 */
static amark_error_t start_thematic_break(block_parser_t *p,
                                          size_t *container,
                                          start_result_t *result)
{
    const amark_line_pos_t *pos = &p->pos;
    size_t first = pos->first_nonspace;

    if (first < p->break_kill) {
        return AMARK_ERROR_NONE;
    }
    if (!amark_scan_thematic_break(p->bytes + first,
                                   pos->content_end - first)) {
        unsigned char c = p->bytes[first];
        size_t end = first;

        while (end < pos->content_end &&
               (p->bytes[end] == c || p->bytes[end] == ' ' ||
                p->bytes[end] == '\t')) {
            end++;
        }
        p->break_kill = end;
        return AMARK_ERROR_NONE;
    }
    return open_line_leaf(p, container, AMARK_KIND_THEMATIC_BREAK, first,
                          result);
}

/*
 * Starts a list item if the rest of the line starts with a list item marker.
 * The caller has checked that the indentation is at most 3 columns. When the
 * element at *container is a paragraph, only a bullet item or an ordered
 * item numbered 1, either with content after the marker, interrupts it
 * (amark_scan_list_marker leaves this to its caller, and a thematic break
 * was tried before).
 *
 * The item joins the list at *container if that list has the same type and
 * the same bullet character or delimiter; otherwise a new list opens, tight
 * until its spacing is decided. Both start at the marker, and their nodes
 * are made now. The marker is consumed with the white space after it that
 * belongs to the content indent: the marker's width plus 1 to 4 columns of
 * white space, or the width plus 1 when 5 columns or more follow (the
 * content is then indented code) or nothing does. Tabs count by columns, so
 * the 1 column of a wide tab leaves the rest as pad. The item ends at least
 * at the end of the marker line's content; the starts are looked for again
 * inside it.
 */
static amark_error_t start_list_item(block_parser_t *p, size_t *container,
                                     start_result_t *result)
{
    amark_line_pos_t *pos = &p->pos;
    size_t first = pos->first_nonspace;
    open_block_t *parent = stack_at(p, *container);
    amark_list_marker_t marker;
    size_t marker_offset;
    size_t list;
    size_t index;
    open_block_t *block;
    amark_error_t err;

    if (!amark_scan_list_marker(p->bytes + first, pos->content_end - first,
                                &marker)) {
        return AMARK_ERROR_NONE;
    }
    if (parent->kind == AMARK_KIND_PARAGRAPH &&
        (marker.blank_after || (marker.type == AMARK_LIST_ORDERED &&
                                marker.start_number != 1))) {
        return AMARK_ERROR_NONE;
    }

    if (parent->kind == AMARK_KIND_LIST && parent->list_type == marker.type &&
        parent->marker == marker.marker) {
        list = *container;
    } else {
        err = push_block(p, *container, AMARK_KIND_LIST, true, first, &list);
        if (err != AMARK_ERROR_NONE) {
            return err;
        }
        block = stack_at(p, list);
        block->list_type = marker.type;
        block->marker = marker.marker;
        block->start_number =
            marker.type == AMARK_LIST_ORDERED ? marker.start_number : 0;
        err = amark_tree_set_list(p->doc, block->node, block->list_type,
                                  block->marker, block->start_number,
                                  AMARK_LIST_TIGHT);
        if (err != AMARK_ERROR_NONE) {
            return err;
        }
    }
    err = push_block(p, list, AMARK_KIND_LIST_ITEM, true, first, &index);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }

    /* The marker, then the white space of the content indent. */
    marker_offset = pos->indent;
    amark_line_pos_advance(pos, p->bytes, pos->indent, AMARK_ADVANCE_COLUMNS);
    amark_line_pos_advance(pos, p->bytes, marker.width, AMARK_ADVANCE_BYTES);
    amark_line_pos_find_nonspace(pos, p->bytes);
    block = stack_at(p, index);
    if (pos->blank || pos->indent > CODE_INDENT) {
        block->padding = marker.width + 1;
        if (pos->first_nonspace > pos->offset) {
            amark_line_pos_advance(pos, p->bytes, 1, AMARK_ADVANCE_COLUMNS);
        }
    } else {
        block->padding = marker.width + pos->indent;
        amark_line_pos_advance(pos, p->bytes, pos->indent,
                               AMARK_ADVANCE_COLUMNS);
    }
    block->marker_offset = marker_offset;
    block->content_indent += marker_offset + block->padding;
    block->end = pos->content_end;
    *container = index;
    *result = START_CONTAINER;
    return AMARK_ERROR_NONE;
}

/*
 * Starts a fenced code block if the rest of the line is an opening fence.
 * The caller has checked that the indentation is at most 3 columns, which
 * becomes the fence offset. The record of the line is the first character
 * of the fence. The info string is only located here: it is unescaped by
 * whoever reads it.
 */
static amark_error_t start_fenced_code(block_parser_t *p, size_t *container,
                                       start_result_t *result)
{
    const amark_line_pos_t *pos = &p->pos;
    size_t first = pos->first_nonspace;
    char fence_char;
    size_t fence_length;
    size_t info_start;
    size_t info_end;
    open_block_t *block;
    amark_error_t err;

    if (!amark_scan_open_fence(p->bytes + first, pos->content_end - first,
                               &fence_char, &fence_length, &info_start,
                               &info_end)) {
        return AMARK_ERROR_NONE;
    }
    err = open_line_leaf(p, container, AMARK_KIND_CODE_BLOCK, first, result);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    block = stack_at(p, *container);
    block->fence_char = fence_char;
    block->fence_length = fence_length;
    block->fence_offset = pos->indent;
    block->info.start = first + info_start;
    block->info.end = first + info_end;
    block->info.present = info_start != info_end;
    return AMARK_ERROR_NONE;
}

/*
 * Starts an HTML block if the rest of the line meets one of the 7 start
 * conditions. The caller has checked that the indentation is at most 3
 * columns. Condition 7 does not interrupt a paragraph, so it is not tried
 * when the innermost open block is a paragraph: the line either continues
 * it or could be a lazy continuation line of it. The block starts after the
 * prefix, its indentation included; the line itself is added in the third
 * phase, which also closes the block if the line meets the end condition.
 */
static amark_error_t start_html_block(block_parser_t *p, size_t *container,
                                      start_result_t *result)
{
    const amark_line_pos_t *pos = &p->pos;
    size_t first = pos->first_nonspace;
    unsigned type;
    size_t index;
    amark_error_t err;

    type = amark_scan_html_start(p->bytes + first, pos->content_end - first);
    if (type == 0 ||
        (type == 7 &&
         stack_at(p, stack_top(p))->kind == AMARK_KIND_PARAGRAPH)) {
        return AMARK_ERROR_NONE;
    }
    err = push_block(p, *container, AMARK_KIND_HTML_BLOCK, false,
                     pos->offset, &index);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    stack_at(p, index)->html_type = type;
    *container = index;
    *result = START_LEAF;
    return AMARK_ERROR_NONE;
}

/*
 * Starts an indented code block. The caller has checked that the
 * indentation is at least 4 columns, that the line is not blank and that it
 * cannot be a lazy continuation line. The 4 columns are consumed; the block
 * and the record of the line start after them, at a tab with the columns it
 * has left as pad if the tab crosses the 4th column. The line itself is
 * added in the third phase.
 */
static amark_error_t start_indented_code(block_parser_t *p,
                                         size_t *container,
                                         start_result_t *result)
{
    amark_line_pos_t *pos = &p->pos;
    size_t index;
    amark_error_t err;

    amark_line_pos_advance(pos, p->bytes, CODE_INDENT, AMARK_ADVANCE_COLUMNS);
    err = push_block(p, *container, AMARK_KIND_CODE_BLOCK, false,
                     pos->offset, &index);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    *container = index;
    *result = START_LEAF;
    return AMARK_ERROR_NONE;
}

/*
 * Opens the block of dkind that the start rule of extension number
 * extension accepted with *out at the current position (its first non-space
 * byte), inside the element at *container: push_block closes what cannot
 * contain it, checks the depth and makes a container's node. The block
 * keeps its registration, extension and data, and the position moves past
 * the start marker, by bytes. The block ends at least at the end of the
 * line's content. The starts are looked for again inside a container; the
 * rest of the line is a leaf's first line, which add_line records (4.9).
 */
static amark_error_t open_dialect_block(
    block_parser_t *p, size_t *container, size_t extension,
    const amark_dialect_kind_t *dkind,
    const amark_dialect_block_start_result_t *out, start_result_t *result)
{
    amark_line_pos_t *pos = &p->pos;
    bool is_container = dkind->cls == AMARK_DIALECT_CONTAINER;
    open_block_t *block;
    size_t index;
    amark_error_t err;

    err = push_block(p, *container, dkind->id, is_container,
                     pos->first_nonspace, &index);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    block = stack_at(p, index);
    block->dkind = dkind;
    block->extension = extension;
    block->data = out->data;
    /* 0 unless a container that goes on over blank lines (3.14). */
    block->content_indent += dkind->blank_columns;
    amark_line_pos_advance(pos, p->bytes, out->consumed.end - pos->offset,
                           AMARK_ADVANCE_BYTES);
    *container = index;
    block->end = pos->content_end;
    *result = is_container ? START_CONTAINER : START_LEAF;
    return AMARK_ERROR_NONE;
}

/*
 * The block start rules of the dialect, before the CommonMark starts
 * (4.1): the caller has checked that the rest of the line is not blank and
 * indented at most 3 columns. The rules are called in the order of the
 * extensions, and of the rules in each; when the innermost open block is a
 * paragraph, the rules that cannot interrupt one are skipped (4.7). The
 * first valid start opens its block (open_dialect_block).
 *
 * A result is valid when its range starts at the position (ORIGIN), does
 * not end before it starts or after the end of the line's content (LINE),
 * and, for a container, is not empty (EMPTY) (4.10). A status other than
 * AMARK_ERROR_NONE and AMARK_ERROR_NOMEM is rejected too (STATUS, 10.4). A
 * rejected result adds a diagnostic at the position and counts as "no"
 * (4.11); AMARK_ERROR_NOMEM from a rule, or from adding the diagnostic,
 * ends the parse (10.10, 11.7).
 */
static amark_error_t start_dialect_block(block_parser_t *p,
                                         size_t *container,
                                         start_result_t *result)
{
    const amark_line_pos_t *pos = &p->pos;
    const amark_dialect_t *dialect = p->run->dialect;
    size_t position = pos->first_nonspace;
    size_t parent = *container;
    amark_dialect_line_t line;
    size_t e;

    /*
     * The parent the new block would have: a paragraph and a list cannot
     * contain a dialect block, and their parents can.
     */
    while (parent > 0 &&
           !can_contain(stack_at(p, parent), AMARK_KIND_DIALECT_MIN)) {
        parent--;
    }
    line.content = (const char *)p->bytes;
    line.content_length = amark_document_content_length(p->doc);
    line.line = pos->line;
    line.line_start = p->line_start;
    line.line_end = pos->content_end;
    line.position = position;
    line.column = pos->first_nonspace_column;
    line.first_nonspace = position;
    line.indent = pos->indent;
    line.container_kind = stack_at(p, parent)->kind;
    line.in_paragraph =
        stack_at(p, stack_top(p))->kind == AMARK_KIND_PARAGRAPH;

    for (e = 0; e < dialect->extension_count; e++) {
        const amark_dialect_extension_t *ext = dialect->extensions[e];
        size_t r;

        for (r = 0; r < ext->block_start_count; r++) {
            const amark_dialect_block_start_t *rule = &ext->block_starts[r];
            const amark_dialect_kind_t *dkind;
            amark_dialect_block_start_result_t out;
            amark_dialect_reason_t reason;
            amark_error_t status;

            if (line.in_paragraph && !rule->interrupts_paragraph) {
                continue;
            }
            memset(&out, 0, sizeof(out));
            status = rule->fn(&line, p->run->states[e], &out);
            if (status == AMARK_ERROR_NOMEM) {
                return AMARK_ERROR_NOMEM;
            }
            /* Validation made sure the kind is a block kind of ext. */
            dkind = amark_dialect_find_kind(ext, rule->kind);
            if (status != AMARK_ERROR_NONE) {
                reason = AMARK_DIALECT_REASON_STATUS;
            } else if (!out.matched) {
                continue;
            } else if (out.consumed.start != position) {
                reason = AMARK_DIALECT_REASON_ORIGIN;
            } else if (out.consumed.end < out.consumed.start ||
                       out.consumed.end > pos->content_end) {
                reason = AMARK_DIALECT_REASON_LINE;
            } else if (dkind->cls == AMARK_DIALECT_CONTAINER &&
                       out.consumed.end == out.consumed.start) {
                reason = AMARK_DIALECT_REASON_EMPTY;
            } else {
                return open_dialect_block(p, container, e, dkind, &out,
                                          result);
            }
            status = amark_dialect_reject(p->run, e,
                                          AMARK_DIALECT_HOOK_BLOCK_START,
                                          reason, position);
            if (status != AMARK_ERROR_NONE) {
                return status;
            }
        }
    }
    return AMARK_ERROR_NONE;
}

/*
 * The start rules: whether a new block starts at the current position
 * inside the element at *container (the innermost matched or opened one).
 * If one does, it is opened and *container becomes its index. The position
 * has amark_line_pos_find_nonspace done.
 */
static amark_error_t start_block(block_parser_t *p, size_t *container,
                                 start_result_t *result)
{
    const amark_line_pos_t *pos = &p->pos;
    bool shallow = pos->indent < CODE_INDENT && !pos->blank;
    amark_error_t err;

    *result = START_NONE;
    switch (stack_at(p, *container)->kind) {
    case AMARK_KIND_DOCUMENT:
    case AMARK_KIND_BLOCK_QUOTE:
    case AMARK_KIND_LIST:
    case AMARK_KIND_LIST_ITEM:
    case AMARK_KIND_PARAGRAPH:
        break;
    case AMARK_KIND_CODE_BLOCK:
    case AMARK_KIND_HTML_BLOCK:
    case AMARK_KIND_THEMATIC_BREAK:
    case AMARK_KIND_HEADING:
        /*
         * Nothing starts inside a code or HTML block (their lines are raw)
         * or inside a leaf that takes the whole line.
         */
        return AMARK_ERROR_NONE;
    default:
        /*
         * A dialect container takes the starts as the document does; a
         * dialect leaf starts nothing.
         */
        if (!is_dialect_container(stack_at(p, *container))) {
            return AMARK_ERROR_NONE;
        }
        break;
    }

    /*
     * The dialect's start rules come first (start_dialect_block). Then the
     * CommonMark starts are checked in this order, each under its own
     * condition: block quote, ATX heading, opening fence, HTML block, setext
     * underline, thematic break, list item (these at most 3 columns of
     * indentation), indented code block. A list item needs at most 3
     * columns even inside an open list (CommonMark example 312): the
     * indentation counts from the list's container, so a marker indented 4
     * columns from it is not an item.
     */
    if (shallow) {
        if (p->run != NULL) {
            err = start_dialect_block(p, container, result);
            if (err != AMARK_ERROR_NONE || *result != START_NONE) {
                return err;
            }
        }
        err = start_block_quote(p, container, result);
        if (err != AMARK_ERROR_NONE || *result != START_NONE) {
            return err;
        }
        err = start_atx_heading(p, container, result);
        if (err != AMARK_ERROR_NONE || *result != START_NONE) {
            return err;
        }
        err = start_fenced_code(p, container, result);
        if (err != AMARK_ERROR_NONE || *result != START_NONE) {
            return err;
        }
        err = start_html_block(p, container, result);
        if (err != AMARK_ERROR_NONE || *result != START_NONE) {
            return err;
        }
        err = start_setext_heading(p, container, result);
        if (err != AMARK_ERROR_NONE || *result != START_NONE) {
            return err;
        }
        err = start_thematic_break(p, container, result);
        if (err != AMARK_ERROR_NONE || *result != START_NONE) {
            return err;
        }
        err = start_list_item(p, container, result);
        if (err != AMARK_ERROR_NONE || *result != START_NONE) {
            return err;
        }
    }
    /*
     * Indented code: not on a blank line, and not on a line that could
     * be a lazy continuation of a paragraph (the innermost open block),
     * since indented code cannot interrupt one.
     */
    if (pos->indent >= CODE_INDENT && !pos->blank &&
        stack_at(p, stack_top(p))->kind != AMARK_KIND_PARAGRAPH) {
        return start_indented_code(p, container, result);
    }
    return AMARK_ERROR_NONE;
}

/* ------------------------------------------------------------------------ */
/* Adding lines                                                             */
/* ------------------------------------------------------------------------ */

/*
 * Adds the current line to the paragraph at index: its content starts at the
 * first non-space byte (the record), the node ends at the end of the line's
 * content, and the inline content after the last non-space byte.
 */
static void paragraph_add_line(block_parser_t *p, size_t index)
{
    amark_line_pos_t *pos = &p->pos;
    open_block_t *block = stack_at(p, index);
    size_t end = pos->content_end;

    amark_line_pos_advance(pos, p->bytes, pos->first_nonspace - pos->offset,
                           AMARK_ADVANCE_BYTES);
    amark_line_set(p->doc, pos->line, pos->offset, pos->pad);
    if (block->emptied) {
        /* The first line of a paragraph emptied by its definitions. */
        block->emptied = false;
        block->start = pos->offset;
        block->content_start = pos->offset;
    }
    while (end > pos->offset &&
           (p->bytes[end - 1] == ' ' || p->bytes[end - 1] == '\t')) {
        end--;
    }
    block->end = pos->content_end;
    block->content_end = end;
}

/*
 * The end of a code or HTML block whose last line is the current line: the
 * end of the line's content, or, when the line has no content at all (its
 * content ends where it starts), the end of its line ending, which is never
 * past the end of the range. The end line of a node is the line of its
 * end - 1, so an empty last line stays the block's end line.
 */
static size_t leaf_line_end(const block_parser_t *p)
{
    amark_line_bounds_t bounds;

    if (amark_line_bounds(p->doc, p->pos.line, &bounds) &&
        bounds.content_end == bounds.start) {
        return bounds.end;
    }
    return p->pos.content_end;
}

/*
 * Adds the rest of the current line to the code block at index: the record
 * is the position the prefixes and the code indentation were consumed to,
 * with the columns left of a tab as pad, and the block ends at the end of
 * the line (leaf_line_end). An indented code block also keeps the end of
 * its last line that is not blank, where it ends when it closes.
 */
static void code_add_line(block_parser_t *p, size_t index)
{
    const amark_line_pos_t *pos = &p->pos;
    open_block_t *block = stack_at(p, index);

    amark_line_set(p->doc, pos->line, pos->offset, pos->pad);
    block->end = leaf_line_end(p);
    if (!pos->blank) {
        block->last_nonblank_end = pos->content_end;
    }
}

/*
 * Adds the rest of the current line to the HTML block at index: the record
 * is the position after the prefix, the line's indentation included, with
 * the columns left of a tab as pad, and the block ends at the end of the
 * line (leaf_line_end). Conditions 1 to 5 close the block on the line that
 * meets their end condition, its first line included; that line is never
 * empty.
 */
static amark_error_t html_add_line(block_parser_t *p, size_t index)
{
    const amark_line_pos_t *pos = &p->pos;
    open_block_t *block = stack_at(p, index);

    amark_line_set(p->doc, pos->line, pos->offset, pos->pad);
    block->end = leaf_line_end(p);
    if (block->html_type <= 5 &&
        amark_scan_html_end(p->bytes + pos->offset,
                            pos->content_end - pos->offset,
                            block->html_type)) {
        return close_above(p, index - 1);
    }
    return AMARK_ERROR_NONE;
}

/*
 * Adds the rest of the current line to the dialect leaf at index (4.9):
 * as a paragraph's, its record is its first non-space byte and the leaf
 * ends at the end of the line's content (5.11). A blank line, which a leaf
 * registered to go on over blank lines gets, has the record of a blank
 * line and leaves the end, so the leaf ends at its last line that is not
 * blank. Its first line was not blank (the start marker is on it), and
 * the leaf already ends at its end.
 */
static void dialect_leaf_add_line(block_parser_t *p, size_t index)
{
    amark_line_pos_t *pos = &p->pos;

    if (pos->blank) {
        amark_line_set(p->doc, pos->line, pos->content_end, 0);
        return;
    }
    amark_line_pos_advance(pos, p->bytes, pos->first_nonspace - pos->offset,
                           AMARK_ADVANCE_BYTES);
    amark_line_set(p->doc, pos->line, pos->offset, pos->pad);
    stack_at(p, index)->end = pos->content_end;
}

/*
 * The third phase: closes the elements that did not match, and adds the
 * rest of the line to the element at container (a leaf, or a dialect
 * leaf), or opens a paragraph for it. A blank line opens nothing and gets
 * the record of a blank line.
 */
static amark_error_t add_line(block_parser_t *p, size_t container)
{
    amark_line_pos_t *pos = &p->pos;
    size_t index;
    amark_error_t err;

    err = close_above(p, container);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    if (p->line_taken) {
        /* A leaf opened on this line took it whole; recorded already. */
        return AMARK_ERROR_NONE;
    }
    switch (stack_at(p, container)->kind) {
    case AMARK_KIND_PARAGRAPH:
        paragraph_add_line(p, container);
        return AMARK_ERROR_NONE;
    case AMARK_KIND_CODE_BLOCK:
        code_add_line(p, container);
        return AMARK_ERROR_NONE;
    case AMARK_KIND_HTML_BLOCK:
        return html_add_line(p, container);
    default:
        if (stack_at(p, container)->dkind != NULL &&
            !is_dialect_container(stack_at(p, container))) {
            dialect_leaf_add_line(p, container);
            return AMARK_ERROR_NONE;
        }
        break;
    }
    if (pos->blank) {
        amark_line_set(p->doc, pos->line, pos->content_end, 0);
        return AMARK_ERROR_NONE;
    }
    err = push_block(p, container, AMARK_KIND_PARAGRAPH, false,
                     pos->first_nonspace, &index);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    stack_at(p, index)->content_start = pos->first_nonspace;
    paragraph_add_line(p, index);
    return AMARK_ERROR_NONE;
}

/*
 * The blank line cutoff: the first phase came to the list at index list (or
 * to a dialect container that goes on over blank lines, which consumes its
 * blank_columns on them), the rest of the line is blank, it was blank there on
 * the previous line too, and every element inside it continues
 * (blank_run_continues). The line changes nothing else: nothing opens or
 * closes on it, and the blank line marks stay as the previous line left
 * them (note_blank_line would set the same ones), so the elements inside
 * the list are not matched one by one, which would cost the depth on every
 * line of a run of blank lines.
 *
 * The line gets the record of a blank line, or is added to the code or HTML
 * block at the top. Then the elements from the one at index list to the top
 * consume their content indents (the items) and blank_columns (the dialect
 * containers, that at index list included): all of their columns, which
 * are the difference of the sums from the root of the top and of the parent
 * of the one at index list, when the line has as many; otherwise the
 * element where the columns run out takes all of the white space, and so
 * do the elements after it. The leaf then consumes its own prefix, as on
 * any blank line.
 */
static amark_error_t add_blank_run_line(block_parser_t *p, size_t list)
{
    amark_line_pos_t *pos = &p->pos;
    size_t top = stack_top(p);
    open_block_t *leaf = stack_at(p, top);
    size_t items =
        leaf->content_indent - stack_at(p, list - 1)->content_indent;

    p->all_matched = true;
    if (leaf->kind != AMARK_KIND_CODE_BLOCK &&
        leaf->kind != AMARK_KIND_HTML_BLOCK) {
        amark_line_set(p->doc, pos->line, pos->content_end, 0);
        return AMARK_ERROR_NONE;
    }
    if (pos->indent >= items) {
        amark_line_pos_advance(pos, p->bytes, items, AMARK_ADVANCE_COLUMNS);
    } else {
        amark_line_pos_advance(pos, p->bytes,
                               pos->first_nonspace - pos->offset,
                               AMARK_ADVANCE_BYTES);
    }
    amark_line_pos_find_nonspace(pos, p->bytes);
    if (leaf->kind == AMARK_KIND_HTML_BLOCK) {
        /* No end condition is met by white space. */
        return html_add_line(p, top);
    }
    /* A blank line is not a closing fence: the code continues. */
    (void)continue_code_block(p, leaf);
    code_add_line(p, top);
    return AMARK_ERROR_NONE;
}

/*
 * Updates the blank line marks for the current line, before the third phase
 * closes the elements that did not match. container is the innermost element
 * matched or opened on the line (C below); line_start is the start of the
 * line.
 *
 * On a blank line, the last child of C ended with a blank line: that is the
 * element just inside C if one did not match (it closes on this line and
 * reports the mark to C), otherwise the value C keeps for its last closed
 * child. C itself ends with a blank line, unless it is a block quote, a
 * fenced code block (blank lines in a fence do not count), or an item whose
 * marker line is blank and is this line. Every element outside C, and on a
 * line that is not blank every element matched or opened, loses its own
 * mark; the elements that did not match keep theirs until they close.
 *
 * So after each line at most one open element has its own mark set (C, if
 * the line was blank), and blank_mark keeps its index: clearing that one
 * element clears every element up to C without walking the stack, which
 * would cost the depth on every line. An index past C is an element that
 * did not match, which keeps its mark until it closes, or one left from a
 * block that closed; it is not touched.
 */
static void note_blank_line(block_parser_t *p, size_t container,
                            size_t line_start)
{
    open_block_t *block = stack_at(p, container);

    if (p->blank_mark <= container) {
        stack_at(p, p->blank_mark)->ends_blank = false;
    }
    if (!p->pos.blank) {
        return;
    }
    if (stack_top(p) > container) {
        stack_at(p, container + 1)->ends_blank = true;
    } else if (block->child_count > 0) {
        block->last_child_blank = true;
    }
    switch (block->kind) {
    case AMARK_KIND_BLOCK_QUOTE:
        return;
    case AMARK_KIND_CODE_BLOCK:
        if (block->fence_length > 0) {
            return;
        }
        break;
    case AMARK_KIND_LIST_ITEM:
        if (block->child_count == 0 && block->start >= line_start) {
            return;
        }
        break;
    default:
        break;
    }
    block->ends_blank = true;
    p->blank_mark = container;
}

/* Processes one line in the three phases. */
static amark_error_t process_line(block_parser_t *p, size_t line,
                                  const amark_line_bounds_t *bounds)
{
    size_t top = stack_top(p);
    size_t matched = 0;
    size_t container;
    size_t i;

    amark_line_pos_init(&p->pos, line, bounds);
    p->line_start = bounds->start;
    p->line_taken = false;
    p->break_kill = bounds->start;

    /* 1. Match the open blocks from the outside in; the root always does. */
    for (i = 1; i <= top; i++) {
        continue_result_t result;

        amark_line_pos_find_nonspace(&p->pos, p->bytes);
        result = continue_block(p, i);
        if (result == CONTINUE_FAILED) {
            return p->failure;
        }
        if (result == CONTINUE_LINE_DONE) {
            /* The element at i is the innermost: a leaf. */
            return close_above(p, i - 1);
        }
        if (result == CONTINUE_BLANK_RUN) {
            return add_blank_run_line(p, i);
        }
        if (result == CONTINUE_NO) {
            break;
        }
        matched = i;
    }
    p->all_matched = matched == top;

    /* 2. Open new blocks until a leaf opens or nothing starts. */
    container = matched;
    for (;;) {
        start_result_t result;
        amark_error_t err;

        amark_line_pos_find_nonspace(&p->pos, p->bytes);
        err = start_block(p, &container, &result);
        if (err != AMARK_ERROR_NONE) {
            return err;
        }
        if (result != START_CONTAINER) {
            break;
        }
    }

    /*
     * 3. Add the rest of the line. A line on which some open block did not
     * match, which opened nothing, and which is not blank while the
     * innermost open block is a paragraph, is a lazy continuation line: it
     * is paragraph text (the starts that cannot interrupt a paragraph were
     * not tried, and a setext underline needs every block matched), so it
     * goes to the paragraph and nothing closes. Its record is its first
     * non-space byte, and the paragraph's end reaches its containers when
     * the paragraph closes.
     */
    amark_line_pos_find_nonspace(&p->pos, p->bytes);
    note_blank_line(p, container, bounds->start);
    if (!p->all_matched && container == matched && !p->pos.blank &&
        stack_at(p, stack_top(p))->kind == AMARK_KIND_PARAGRAPH) {
        paragraph_add_line(p, stack_top(p));
        return AMARK_ERROR_NONE;
    }
    return add_line(p, container);
}

/* ------------------------------------------------------------------------ */
/* The entry                                                                */
/* ------------------------------------------------------------------------ */

amark_error_t amark_block_parse(amark_document_t *doc,
                                const amark_resolved_config_t *config,
                                amark_arena_t *scratch,
                                amark_dialect_run_t *run)
{
    block_parser_t p;
    open_block_t root;
    amark_line_bounds_t bounds;
    amark_range_t range;
    size_t line;
    size_t count;
    amark_error_t err;

    p.doc = doc;
    p.run = run;
    p.bytes = amark_document_content(doc);
    p.max_depth = config->max_nesting_depth;
    p.max_nodes = config->max_node_count;
    p.line_start = 0;
    p.all_matched = false;
    p.line_taken = false;
    p.break_kill = 0;
    p.blank_mark = 0;
    p.failure = AMARK_ERROR_NONE;
    amark_array_init(&p.stack, scratch, sizeof(open_block_t));
    amark_array_init(&p.definitions, scratch,
                     sizeof(amark_definition_entry_t));

    /*
     * The root is element 0. Its range is the parsed range and is never set
     * again; the end its children report to it is not used.
     */
    memset(&root, 0, sizeof(root));
    root.kind = AMARK_KIND_DOCUMENT;
    root.node = amark_document_root(doc);
    (void)amark_node_range(doc, root.node, &range);
    root.start = range.start;
    root.end = range.start;
    err = amark_array_push(&p.stack, &root);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }

    /* The lines the range covers; an empty range covers none. */
    for (line = amark_node_start_line(doc, root.node);
         amark_line_bounds(doc, line, &bounds); line++) {
        err = process_line(&p, line, &bounds);
        if (err != AMARK_ERROR_NONE) {
            return err;
        }
    }

    /* Close what is still open, from the inside out. */
    err = close_above(&p, 0);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }

    /* The definition table, from the definitions in document order. */
    count = amark_array_len(&p.definitions);
    return amark_definitions_build(
        doc, scratch,
        count > 0 ? (const amark_definition_entry_t *)amark_array_at(
                        &p.definitions, 0)
                  : NULL,
        count);
}
