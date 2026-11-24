/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_TEXT_DUMP_H
#define AMARK_TEXT_DUMP_H

#include <stdbool.h>
#include <stddef.h>

#include <amark/tree.h>

/*
 * Writes the values of the nodes of doc as one line, for tests to compare
 * with an expected string (amark_test_support, never installed). Uses only
 * the public API (the reading API of include/amark/tree.h and the text
 * functions of include/amark/text.h) and visits the nodes in document order
 * without recursion.
 *
 * For each node, the parts AMARK_TEXT_VALUE, AMARK_TEXT_DESTINATION,
 * AMARK_TEXT_TITLE and AMARK_TEXT_INFO are asked for, in this order, and
 * each that amark_node_text_alloc gives (AMARK_ERROR_NONE) is written as
 *
 *     kind:part="value"
 *
 * with one space between entries. The kind is named as amark_test_dump_tree
 * names it ("text", "esc", "ref", "code" for a code span and a code block,
 * "autolink", "link", "image", "def", ...; "?" for a dialect kind), and the
 * part as "value", "dest", "title" or "info". A part the node does not have
 * (AMARK_ERROR_NOT_AVAILABLE) is not written; a part that fails otherwise
 * (no memory, a range past the limit) is written as kind:part=none, so that
 * it shows up in a comparison. A value of length 0 is written "".
 *
 * The value is quoted as amark_test_dump_tree quotes bytes: between '"',
 * with '"' and '\' written with a '\' before them, LF as "\n", CR as "\r",
 * tab as "\t", and any other byte below 0x20 and 0x7F as "\xHH" (two
 * upper-case hexadecimal digits). The bytes of 0x80 and above (the values
 * are UTF-8) and the other bytes are written as they are.
 *
 * For example, "```ruby\nx\n```\n\n[a]: /u 'b\"'\n" gives (on one line)
 *
 *     code:value="x\n" code:info="ruby" def:dest="/u" def:title="b\""
 *
 * Writes at most size bytes to buffer: the string and a terminating NUL if
 * they fit, and returns true; if not, the string cut to size - 1 bytes and
 * a NUL (nothing if size is 0, when buffer may be NULL), and returns false.
 * A NULL doc gives the empty string.
 */
bool amark_test_dump_text(const amark_document_t *doc, char *buffer,
                          size_t size);

#endif /* AMARK_TEXT_DUMP_H */
