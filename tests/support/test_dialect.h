/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_TEST_DIALECT_H
#define AMARK_TEST_DIALECT_H

#include <stdint.h>

#include <amark/dialect.h>
#include <amark/tree.h>

/*
 * The dialects of the tests (amark_test_support, never installed; the public
 * API test compiles the same source). Written with the public headers only,
 * as static const data: the source has no mutable static data.
 *
 *   amark_test_dialect()        the test dialect: one extension ("test")
 *                               that registers every kind below and keeps a
 *                               per-document state (amark_test_counts_t).
 *   amark_test_dialect_plain()  a dialect without state whose rules all say
 *                               no: its start, continue and inline rules
 *                               never match, its close rules and its
 *                               paragraph close rule reply with nothing,
 *                               and its postprocessing rule makes no edit.
 *   amark_test_dialect_empty()  a dialect of 0 extensions.
 *
 * A parse with each of them gives the same tree as a parse without a
 * dialect on input that has none of the test dialect's syntax.
 */

/*
 * The kinds of the test dialect, and the syntax of those that have rules:
 *
 *   BOX   "@> " at the start of a block (after the prefixes, at most 3
 *         columns of indentation) opens a box, which interrupts a
 *         paragraph. It goes on over a line that starts with "@>" (at
 *         most 3 columns of indentation; the marker, and one space after
 *         it, are its prefix); a blank line closes it.
 *   NOTE  "@note:" at the start of a block opens a note, which interrupts
 *         a paragraph. It goes on over a line indented at least 2 columns
 *         (the first 2 columns are its prefix) and over blank lines.
 *   FENCE "@@@" at the start of a block opens a fence, which does not
 *         interrupt a paragraph. It goes on over every line (blank ones
 *         included), consuming nothing, up to and with the next line whose
 *         first non-space byte (at most 3 columns of indentation) starts
 *         "@@@"; its data records that line. Its lines are recorded at
 *         their first non-space byte.
 *   LINE  "@line " at the start of a block opens a one-line leaf, which
 *         interrupts a paragraph. Its content after the marker is parsed
 *         as inlines.
 *   GRID  the paragraph close rule: from the first line of a paragraph
 *         whose content starts with '|' to the paragraph's end is a grid
 *         (a container), and the lines before it stay a paragraph. Each
 *         line that starts with '|' is a row (a container) of cells
 *         (leaves): the bytes between two '|', and the bytes after the
 *         last '|' when they are not blank. A line that does not start
 *         with '|' continues the row before it: its last cell runs to the
 *         end of that line. The inline content of a cell is its bytes
 *         less the spaces and tabs at its ends (none when that is empty).
 *         A paragraph with no such line stays as it is.
 *   VAR   the inline rule of '$': "$name$" on one line, the name being one
 *         or more bytes other than '$', a space, a tab and a line ending,
 *         is a VAR over the whole "$name$". Its attributes are the length
 *         of the name in decimal ASCII ("$ab$": "2").
 *   STRIKE the delimiter kind of '~': runs of 1 and 2 match runs of the
 *         same length, as amark_dialect_delimiter_t defines ("~a~" and
 *         "~~a~~"; "~a~~" and "~~~a~~~" are text).
 *
 * Close rules: BOX and FENCE set their attributes to the number of their
 * lines, in decimal ASCII; LINE sets its inline content, from the first
 * non-space byte after its marker to its end. NOTE has none.
 *
 * Markers of invalid results. As the dialect has no state to switch on, a
 * test selects an invalid result by the input: "!" and a name right after
 * the syntax, followed by the end of the line, a space or a tab. A name
 * that is no marker below is content as any other.
 *
 *   Start markers, right after the start marker a start rule consumes
 *   ("@> !start-origin", "@note: !...", "@@@!...", "@line !..."): the rule
 *   matches with
 *     start-origin  a range that starts 1 byte after the position;
 *     start-line    a range that ends 1 byte past the end of the line;
 *     start-empty   a range of length 0 (invalid for BOX and NOTE only);
 *     start-status  the status AMARK_ERROR_INVALID_INPUT, not defined;
 *     start-nomem   the status AMARK_ERROR_NOMEM.
 *
 *   Continue markers, on a line BOX goes on over, right after the prefix
 *   it consumes ("@> !cont-origin"): cont-origin, cont-line, cont-status
 *   and cont-nomem, as the start markers. When BOX does not go on, "@> "
 *   on that line opens a new box.
 *
 *   Close markers of a dialect block, right after its start marker on its
 *   first line ("@> !close-index", "@@@!close-bounds", "@line !close-x"):
 *   after its valid reply, the close rule makes the request named, so that
 *   none of the reply is kept:
 *     close-index    attributes for block 1, which is not there;
 *     close-nesting  a block added to the reply of a block;
 *     close-inline   inline content on block 0 (invalid for BOX only);
 *     close-bounds   inline content that ends past the content (leaves);
 *     close-parent   inline content over the byte after the block (leaves;
 *                    needs that byte);
 *     close-prefix   inline content from the start of the block's second
 *                    line (leaves; needs a prefix there, as in a box);
 *     close-status   the status AMARK_ERROR_INVALID_INPUT;
 *     close-nomem    the status AMARK_ERROR_NOMEM.
 *
 *   Close markers of a paragraph, right after the '|' of its first row line
 *   ("|!close-order"): instead of the grid, the paragraph close rule replies
 *     close-index      a block under block 0, which is not there;
 *     close-kind       a heading, a kind not registered by the extension;
 *     close-nesting    a block of the inline kind VAR;
 *     close-bounds     a grid that ends past the content;
 *     close-parent     a grid of the '|' alone, and a row over the grid's
 *                      bytes up to the paragraph's end under it;
 *     close-order      two grids over the same bytes;
 *     close-inline     a grid with inline content;
 *     close-prefix     a grid and a cell under it, whose inline content
 *                      starts where the next line starts (needs a next line
 *                      with a prefix or white space before its content);
 *     close-paragraph  a paragraph part that starts 1 byte into the
 *                      paragraph;
 *     close-status     the status AMARK_ERROR_INVALID_INPUT;
 *     close-nomem      the status AMARK_ERROR_NOMEM.
 *
 *   Inline markers, as the whole name of a VAR ("$!inline-origin$"): the
 *   rule matches "$name$" but with
 *     inline-origin  a range that starts 1 byte after the position;
 *     inline-empty   a range of length 0;
 *     inline-parent  a range that ends 1 byte past the end of the inline
 *                    content;
 *     inline-status  the status AMARK_ERROR_INVALID_INPUT, not defined;
 *     inline-nomem   the status AMARK_ERROR_NOMEM.
 *
 *   VARs that end on the next line: a "$" and a name that runs to the end
 *   of the line's content, with no closing '$', where a line of the inline
 *   content follows (amark_dialect_inline_next_line):
 *     "$!inline-next"    a VAR up to and with the first byte of the content
 *                        of the next line (which must not be empty): valid;
 *     "$!inline-prefix"  a VAR up to the byte right before the content of
 *                        the next line, which ends inside its prefix or
 *                        leading white space (there must be one): invalid.
 *   Any other name without a closing '$' is no VAR.
 */
#define AMARK_TEST_KIND_BOX (AMARK_KIND_DIALECT_MIN)        /* container */
#define AMARK_TEST_KIND_NOTE (AMARK_KIND_DIALECT_MIN + 1)   /* container */
#define AMARK_TEST_KIND_FENCE (AMARK_KIND_DIALECT_MIN + 2)  /* leaf */
#define AMARK_TEST_KIND_LINE (AMARK_KIND_DIALECT_MIN + 3)   /* leaf */
#define AMARK_TEST_KIND_GRID (AMARK_KIND_DIALECT_MIN + 4)   /* container */
#define AMARK_TEST_KIND_ROW (AMARK_KIND_DIALECT_MIN + 5)    /* container */
#define AMARK_TEST_KIND_CELL (AMARK_KIND_DIALECT_MIN + 6)   /* leaf */
#define AMARK_TEST_KIND_VAR (AMARK_KIND_DIALECT_MIN + 7)    /* inline */
#define AMARK_TEST_KIND_STRIKE (AMARK_KIND_DIALECT_MIN + 8) /* inline */
#define AMARK_TEST_KIND_CHECK (AMARK_KIND_DIALECT_MIN + 9)  /* inline */
#define AMARK_TEST_KIND_TASK (AMARK_KIND_DIALECT_MIN + 10)  /* container */

/*
 * The kinds of the dialect whose rules all say no: a container, a leaf and
 * an inline.
 */
#define AMARK_TEST_KIND_PLAIN_BLOCK (AMARK_KIND_DIALECT_MIN + 100)
#define AMARK_TEST_KIND_PLAIN_LEAF (AMARK_KIND_DIALECT_MIN + 101)
#define AMARK_TEST_KIND_PLAIN_INLINE (AMARK_KIND_DIALECT_MIN + 102)

/*
 * The per-document state of the test dialect: how many times each rule of
 * its extension was called in one parse, and the place where the VAR rule
 * writes the attributes it returns (they must outlive the call). state_new
 * makes it zeroed with the parse's allocator and state_free releases it.
 */
typedef struct amark_test_counts {
    uint64_t block_starts;    /* block start rules */
    uint64_t block_continues; /* block continue rules */
    uint64_t block_closes;    /* close rules of the dialect's block kinds */
    uint64_t paragraphs;      /* the paragraph close rule */
    uint64_t inlines;         /* inline rules */
    uint64_t postprocesses;   /* the postprocessing rule */
    char var_data[24];        /* the attributes of the last VAR matched,
                                 at the end; the parse copies them */
} amark_test_counts_t;

/* The dialects; each is static const data and valid. */
const amark_dialect_t *amark_test_dialect(void);
const amark_dialect_t *amark_test_dialect_plain(void);
const amark_dialect_t *amark_test_dialect_empty(void);

#endif /* AMARK_TEST_DIALECT_H */
