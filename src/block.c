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
#include "line.h"
#include "parse_data.h"
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
 * the default branches, which do not continue, start nothing and close
 * doing nothing.
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
    bool is_container;
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
    size_t content_indent; /* list items: the sum of the content indents from
                              the root */

    /* Fenced code blocks. */
    char fence_char;
    size_t fence_length;
    size_t fence_offset;
    amark_opt_range_t info;

    /* HTML blocks. */
    unsigned html_type;

    /* Headings and paragraphs: the level, and the inline content. */
    unsigned level;
    size_t content_start;
    size_t content_end;

    /* Indented code blocks: the end of the last line that is not blank. */
    size_t last_nonblank_end;

    /*
     * Containers: the number of blocks opened inside this one, leaves still
     * open and paragraphs that became definitions only included.
     */
    size_t child_count;

    /* The blank line marks (tight and loose, and the blank line run). */
    bool ends_blank;       /* this block ended with a blank line */
    bool last_child_blank; /* the value its last closed child reported */
    bool blank_run;        /* lists: the previous line was blank here too */
    bool loose;            /* lists: a blank line separates items or their
                              children */
} open_block_t;

typedef struct block_parser {
    amark_document_t *doc;
    const unsigned char *bytes;
    size_t max_depth;     /* 0: no limit */
    size_t max_nodes;     /* 0: no limit */
    amark_array_t stack;  /* open_block_t, in the scratch arena */
    amark_line_pos_t pos;
    bool all_matched;     /* every open block matched on this line */
    bool line_taken;      /* a leaf opened on this line took all of it */
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
 * Creates a node of kind over [start, end) as the last child of the node of
 * the element at parent. Checks the node count limit first: the count
 * includes the root, and nothing is removed, so it is the number of nodes
 * the returned tree has.
 */
static amark_error_t append_node(block_parser_t *p, size_t parent,
                                 amark_kind_t kind, size_t start, size_t end,
                                 amark_node_id_t *out)
{
    if (p->max_nodes != 0 &&
        amark_document_node_count(p->doc) >= p->max_nodes) {
        return AMARK_ERROR_LIMIT_EXCEEDED;
    }
    return amark_tree_append(p->doc, stack_at(p, parent)->node, kind,
                             AMARK_NODE_CLASS_AUTO, start, end, out);
}

/* Whether a block of kind child may be a child of a block of kind parent. */
static bool can_contain(amark_kind_t parent, amark_kind_t child)
{
    switch (parent) {
    case AMARK_KIND_DOCUMENT:
    case AMARK_KIND_BLOCK_QUOTE:
    case AMARK_KIND_LIST_ITEM:
        return child != AMARK_KIND_LIST_ITEM;
    case AMARK_KIND_LIST:
        return child == AMARK_KIND_LIST_ITEM;
    default:
        return false;
    }
}

/* ------------------------------------------------------------------------ */
/* Closing                                                                  */
/* ------------------------------------------------------------------------ */

/*
 * Closes a paragraph: adds its node under the element at parent and its
 * inline content.
 */
static amark_error_t close_paragraph(block_parser_t *p, size_t parent,
                                     const open_block_t *block)
{
    amark_node_id_t node;
    amark_error_t err;

    err = append_node(p, parent, AMARK_KIND_PARAGRAPH, block->start,
                      block->end, &node);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    return amark_inline_content_add(p->doc, node, block->content_start,
                                    block->content_end);
}

/*
 * Closes a heading: adds its node under the element at parent with its
 * level and style, and its inline content (of length 0 when it is empty).
 */
static amark_error_t close_heading(block_parser_t *p, size_t parent,
                                   const open_block_t *block,
                                   amark_heading_style_t style)
{
    amark_node_id_t node;
    amark_error_t err;

    err = append_node(p, parent, AMARK_KIND_HEADING, block->start,
                      block->end, &node);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    err = amark_tree_set_heading(p->doc, node, block->level, style);
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
    err = append_node(p, parent, AMARK_KIND_CODE_BLOCK, block->start,
                      block->end, &node);
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
    case AMARK_KIND_LIST:
    case AMARK_KIND_LIST_ITEM:
        /* The node was made when the container opened. */
        amark_tree_set_range(p->doc, block->node, block->start, block->end);
        return AMARK_ERROR_NONE;
    case AMARK_KIND_HEADING:
        /* A heading opened as an element of its own is an ATX heading. */
        return close_heading(p, parent, block, AMARK_HEADING_ATX);
    case AMARK_KIND_THEMATIC_BREAK:
        return append_node(p, parent, AMARK_KIND_THEMATIC_BREAK, block->start,
                           block->end, &node);
    case AMARK_KIND_CODE_BLOCK:
        return close_code_block(p, parent, block);
    case AMARK_KIND_HTML_BLOCK:
        /* Raw HTML is not inline content. */
        return append_node(p, parent, AMARK_KIND_HTML_BLOCK, block->start,
                           block->end, &node);
    default:
        /* Dialect kinds close doing nothing. */
        return AMARK_ERROR_NONE;
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
 * Opens a block of kind starting at start inside the element at parent:
 * closes every element above parent, then parents that cannot contain kind
 * (never the root), checks the nesting depth limit, makes the node of a
 * container, and pushes the element. Stores its index in *index.
 */
static amark_error_t push_block(block_parser_t *p, size_t parent,
                                amark_kind_t kind, bool is_container,
                                size_t start, size_t *index)
{
    open_block_t block;
    size_t depth;
    amark_error_t err;

    err = close_above(p, parent);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    while (stack_top(p) > 0 && !can_contain(stack_at(p, stack_top(p))->kind,
                                            kind)) {
        err = close_top(p);
        if (err != AMARK_ERROR_NONE) {
            return err;
        }
    }
    parent = stack_top(p);
    depth = parent + 1;
    if (p->max_depth != 0 && depth > p->max_depth) {
        return AMARK_ERROR_LIMIT_EXCEEDED;
    }

    memset(&block, 0, sizeof(block));
    block.kind = kind;
    block.is_container = is_container;
    block.node = AMARK_NODE_NONE;
    block.start = start;
    block.end = start;
    if (is_container) {
        err = append_node(p, parent, kind, start, start, &block.node);
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
    CONTINUE_LINE_DONE  /* it took the whole line and ends with it (a
                           closing fence); the caller closes it */
} continue_result_t;

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
    case AMARK_KIND_LIST:
    case AMARK_KIND_LIST_ITEM:
        /*
         * No start rule opens these kinds yet; the rules that open them
         * give their continuation here.
         */
        return CONTINUE_NO;
    default:
        /* Dialect kinds do not continue. */
        return CONTINUE_NO;
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
    block->content_start = first + content_start;
    block->content_end = first + content_end;
    return AMARK_ERROR_NONE;
}

/*
 * Starts a thematic break if the rest of the line is one. The caller has
 * checked that the indentation is at most 3 columns. The record of the line
 * is the first character of the break.
 */
static amark_error_t start_thematic_break(block_parser_t *p,
                                          size_t *container,
                                          start_result_t *result)
{
    const amark_line_pos_t *pos = &p->pos;
    size_t first = pos->first_nonspace;

    if (!amark_scan_thematic_break(p->bytes + first,
                                   pos->content_end - first)) {
        return AMARK_ERROR_NONE;
    }
    return open_line_leaf(p, container, AMARK_KIND_THEMATIC_BREAK, first,
                          result);
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
        /*
         * The CommonMark starts are checked here in this order, each under
         * its own condition: block quote, ATX heading, opening fence, HTML
         * block, setext underline, thematic break (these at most 3 columns
         * of indentation), list item, indented code block. Those not added
         * yet leave the line to become paragraph text.
         */
        if (shallow) {
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
            err = start_thematic_break(p, container, result);
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
        /* Dialect kinds start nothing. */
        return AMARK_ERROR_NONE;
    }
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
    while (end > pos->offset &&
           (p->bytes[end - 1] == ' ' || p->bytes[end - 1] == '\t')) {
        end--;
    }
    block->end = pos->content_end;
    block->content_end = end;
}

/*
 * Adds the rest of the current line to the code block at index: the record
 * is the position the prefixes and the code indentation were consumed to,
 * with the columns left of a tab as pad, and the block ends at the end of
 * the line's content. An indented code block also keeps the end of its last
 * line that is not blank.
 */
static void code_add_line(block_parser_t *p, size_t index)
{
    const amark_line_pos_t *pos = &p->pos;
    open_block_t *block = stack_at(p, index);

    amark_line_set(p->doc, pos->line, pos->offset, pos->pad);
    block->end = pos->content_end;
    if (!pos->blank) {
        block->last_nonblank_end = pos->content_end;
    }
}

/*
 * Adds the rest of the current line to the HTML block at index: the record
 * is the position after the prefix, the line's indentation included, with
 * the columns left of a tab as pad, and the block ends at the end of the
 * line's content. Conditions 1 to 5 close the block on the line that meets
 * their end condition, its first line included.
 */
static amark_error_t html_add_line(block_parser_t *p, size_t index)
{
    const amark_line_pos_t *pos = &p->pos;
    open_block_t *block = stack_at(p, index);

    amark_line_set(p->doc, pos->line, pos->offset, pos->pad);
    block->end = pos->content_end;
    if (block->html_type <= 5 &&
        amark_scan_html_end(p->bytes + pos->offset,
                            pos->content_end - pos->offset,
                            block->html_type)) {
        return close_above(p, index - 1);
    }
    return AMARK_ERROR_NONE;
}

/*
 * The third phase: closes the elements that did not match, and adds the
 * rest of the line to the element at container, or opens a paragraph for
 * it. A blank line opens nothing and gets the record of a blank line.
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

/* Processes one line in the three phases. */
static amark_error_t process_line(block_parser_t *p, size_t line,
                                  const amark_line_bounds_t *bounds)
{
    size_t top = stack_top(p);
    size_t matched = 0;
    size_t container;
    size_t i;

    amark_line_pos_init(&p->pos, line, bounds);
    p->line_taken = false;

    /* 1. Match the open blocks from the outside in; the root always does. */
    for (i = 1; i <= top; i++) {
        continue_result_t result;

        amark_line_pos_find_nonspace(&p->pos, p->bytes);
        result = continue_block(p, i);
        if (result == CONTINUE_LINE_DONE) {
            /* The element at i is the innermost: a leaf. */
            return close_above(p, i - 1);
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

    /* 3. Add the rest of the line. */
    amark_line_pos_find_nonspace(&p->pos, p->bytes);
    return add_line(p, container);
}

/* ------------------------------------------------------------------------ */
/* The entry                                                                */
/* ------------------------------------------------------------------------ */

amark_error_t amark_block_parse(amark_document_t *doc,
                                const amark_resolved_config_t *config,
                                amark_arena_t *scratch)
{
    block_parser_t p;
    open_block_t root;
    amark_line_bounds_t bounds;
    amark_range_t range;
    size_t line;
    amark_error_t err;

    p.doc = doc;
    p.bytes = amark_document_content(doc);
    p.max_depth = config->max_nesting_depth;
    p.max_nodes = config->max_node_count;
    p.all_matched = false;
    p.line_taken = false;
    amark_array_init(&p.stack, scratch, sizeof(open_block_t));

    /*
     * The root is element 0. Its range is the parsed range and is never set
     * again; the end its children report to it is not used.
     */
    memset(&root, 0, sizeof(root));
    root.kind = AMARK_KIND_DOCUMENT;
    root.is_container = true;
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
    return close_above(&p, 0);
}
