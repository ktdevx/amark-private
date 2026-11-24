/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_TREE_DUMP_H
#define AMARK_TREE_DUMP_H

#include <stddef.h>

#include <amark/tree.h>

/*
 * Writes the block structure of doc as one line, for tests to compare with
 * an expected string (amark_test_support, never installed). Uses only the
 * public reading API of include/amark/tree.h, visits the nodes in document
 * order without recursion, and allocates nothing.
 *
 * Each block is written as "(" kind attributes children ")", with one space
 * before each attribute and each child. Inline nodes and their descendants
 * are not written, nor are ranges or lines. The kinds and their attributes,
 * in this fixed order:
 *
 *     (doc ...)                 the document
 *     (bq ...)                  a block quote
 *     (ul C S ...)              a bullet list: bullet character C ('-', '+'
 *                               or '*'), spacing S ("tight" or "loose")
 *     (ol N D S ...)            an ordered list: start number N in decimal,
 *                               delimiter D ('.' or ')'), spacing S
 *     (li ...)                  a list item
 *     (h L atx) (h L setext)    a heading of level L and its style
 *     (code indented)           an indented code block
 *     (code fenced C N[ info])  a fenced code block: fence character C, the
 *                               length N of the opening fence, and "info"
 *                               when the info string is written
 *     (html)                    an HTML block
 *     (hr)                      a thematic break
 *     (p)                       a paragraph
 *     (def)                     a link reference definition
 *
 * For example:
 *
 *     (doc (bq (p)) (ul - tight (li (p))) (ol 3 . loose (li (p))) (h 2 atx)
 *      (h 1 setext) (code indented) (code fenced ` 3 info) (html) (hr) (def))
 *
 * (on one line). An attribute the node does not have is written as "none"
 * (a list of no type is written "(list ...)"), and a block of any other kind
 * as "(?)", so that a malformed tree shows up in a comparison.
 *
 * Writes at most size bytes to buffer: the string, cut to size - 1 bytes if
 * it is longer, and a terminating NUL. Writes nothing if size is 0 (buffer
 * may then be NULL). Returns the length of the whole string, without the
 * NUL; a return value of size or more means the string was cut. A NULL doc
 * gives the empty string.
 */
size_t amark_test_dump_blocks(const amark_document_t *doc, char *buffer,
                              size_t size);

/*
 * Writes the whole tree of doc, blocks and inlines, as one line, in the
 * same way as amark_test_dump_blocks: the blocks are written in its format,
 * and the inline nodes as children of the blocks that hold them, with one
 * space before each. Visits the nodes in document order without recursion,
 * reads the tree through the public reading API of include/amark/tree.h
 * (and the content of doc, for the quoted bytes), and allocates nothing.
 * Ranges are not written as numbers.
 *
 * The inline kinds, where Q is the bytes of the range of the node, quoted:
 *
 *     (text Q)                  a text
 *     (esc Q) (ref Q)           a backslash escape, a character reference
 *     (code Q) (html Q)         a code span, a raw HTML
 *     (autolink T Q)            an autolink of type T ("uri" or "email")
 *     (em ...) (strong ...)     an emphasis, a strong emphasis
 *     (link F[ A] ...)          a link of form F ("inline", "full",
 *                               "collapsed" or "shortcut") and its
 *                               attributes A, below
 *     (image F[ A] ...)         an image, with the attributes of a link
 *     (br) (sbr)                a hard and a soft line break
 *
 * The attributes of the inline form are dest=Q and title=Q, the bytes of the
 * destination and of the title, each written only when that part is written
 * (dest="" when it is written but empty). The attribute of the reference
 * forms is def=Q, the bytes of the label of the link reference definition
 * the link refers to. For example, on one line:
 *
 *     (doc (def) (p (text "a") (em (text "b")) (sbr)
 *      (link inline dest="/u" title="t" (text "c"))
 *      (link full def="x" (code "`d`"))))
 *
 * A quoted string is written between '"'. In it, '"' and '\' are written
 * with a '\' before them, LF as "\n", CR as "\r", tab as "\t", and any other
 * byte below 0x20 or 0x7F and above as "\xHH" (two upper-case hexadecimal
 * digits); the other bytes are written as they are. Q is written as
 * "none" when the range cannot be read or does not lie within the content.
 *
 * As with the blocks, an attribute the node does not have is written as
 * "none" (a link of no form as "(link none ...)"), and an inline of any
 * other kind (a dialect inline) as "(?)" with its children, so that a
 * malformed tree shows up in a comparison.
 *
 * Writes at most size bytes to buffer and returns the length of the whole
 * string, as amark_test_dump_blocks does. A NULL doc gives the empty string.
 */
size_t amark_test_dump_tree(const amark_document_t *doc, char *buffer,
                            size_t size);

#endif /* AMARK_TREE_DUMP_H */
