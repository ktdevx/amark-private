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

#endif /* AMARK_TREE_DUMP_H */
