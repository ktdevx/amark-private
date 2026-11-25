/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#include "inline.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <amark/tree.h>

#include "bracket.h"
#include "cursor.h"
#include "delimiter.h"
#include "draft.h"
#include "inline_scan.h"
#include "parse_data.h"

/* The number of entries of a table indexed by a byte. */
#define BYTE_TABLE_SIZE 256

/*
 * The special bytes of CommonMark 0.31.2 that start an inline structure,
 * other than the delimiter characters of emphasis, which are marked from the
 * kinds of the delimiter stack (amark_delimiters_init). A byte that is not
 * special never starts a structure, so a run of them is text. The parser
 * copies this table into its state, where a dialect can mark its own
 * special bytes. It holds no pointer.
 */
static const unsigned char commonmark_special[BYTE_TABLE_SIZE] = {
    ['!'] = 1, ['&'] = 1, ['<'] = 1, ['['] = 1, ['\\'] = 1, [']'] = 1,
    ['`'] = 1
};

/*
 * The state of the inline parse: one local structure of amark_inline_parse,
 * and nothing outside it.
 */
typedef struct inline_parser {
    amark_document_t *doc;
    size_t max_depth; /* 0: no limit */
    size_t max_nodes; /* 0: no limit */
    unsigned char special[BYTE_TABLE_SIZE]; /* not 0: a special byte */
    amark_draft_t draft;
    amark_delimiters_t delimiters;
    amark_brackets_t brackets;
    amark_inline_scan_memo_t memo;
    amark_cursor_t cursor;
    /* The start of the white space left out of the text before the line
       ending of the current line, where its line break starts. */
    bool has_break_start;
    size_t break_start;
} inline_parser_t;

static void parser_init(inline_parser_t *parser, amark_document_t *doc,
                        const amark_resolved_config_t *config,
                        amark_arena_t *scratch)
{
    size_t i;

    parser->doc = doc;
    parser->max_depth = config->max_nesting_depth;
    parser->max_nodes = config->max_node_count;
    for (i = 0; i < BYTE_TABLE_SIZE; i++) {
        parser->special[i] = commonmark_special[i];
    }
    amark_draft_init(&parser->draft, scratch);
    amark_delimiters_init(&parser->delimiters, scratch);
    for (i = 0; i < BYTE_TABLE_SIZE; i++) {
        if (amark_delimiters_kind_of(&parser->delimiters, (unsigned char)i) !=
            parser->delimiters.kind_count) {
            parser->special[i] = 1;
        }
    }
    amark_brackets_init(&parser->brackets, scratch);
    amark_inline_scan_memo_init(&parser->memo, scratch);
    parser->has_break_start = false;
    parser->break_start = 0;
}

/* Returns the depth of node: the number of its ancestors. */
static size_t node_depth(const amark_document_t *doc, amark_node_id_t node)
{
    size_t depth = 0;

    node = amark_node_parent(doc, node);
    while (node != AMARK_NODE_NONE) {
        depth++;
        node = amark_node_parent(doc, node);
    }
    return depth;
}

static bool is_space_or_tab(unsigned char c)
{
    return c == ' ' || c == '\t';
}

/*
 * Text: from the position up to the next special byte or the end of the
 * line's content. The position is not at the end of the line's content and
 * not on a special byte, so it moves forward by one byte or more. If the
 * text reaches the end of the line's content and a line ending follows, the
 * spaces and tabs at its end are left out and their first byte becomes the
 * start of the line break; a text left empty is not added.
 */
static amark_error_t scan_text(inline_parser_t *parser)
{
    amark_cursor_t *cursor = &parser->cursor;
    const unsigned char *bytes = cursor->bytes;
    size_t start = cursor->pos;
    size_t end = start;
    amark_draft_id_t unused;

    while (end < cursor->line_end && parser->special[bytes[end]] == 0) {
        end++;
    }
    amark_cursor_seek(cursor, end);
    if (amark_cursor_at_line_end(cursor) && !amark_cursor_at_end(cursor)) {
        while (end > start && is_space_or_tab(bytes[end - 1])) {
            end--;
        }
        if (end < cursor->pos) {
            parser->has_break_start = true;
            parser->break_start = end;
        }
    }
    if (end == start) {
        return AMARK_ERROR_NONE;
    }
    return amark_draft_append(&parser->draft, AMARK_KIND_TEXT, start, end,
                              &unused);
}

/* ASCII punctuation as CommonMark defines it: what a backslash escapes. */
static bool is_ascii_punctuation(unsigned char c)
{
    return (c >= 0x21 && c <= 0x2F) || (c >= 0x3A && c <= 0x40) ||
           (c >= 0x5B && c <= 0x60) || (c >= 0x7B && c <= 0x7E);
}

/* Appends an item of kind for [start, the position). */
static amark_error_t append_to_cursor(inline_parser_t *parser,
                                      amark_kind_t kind, size_t start,
                                      amark_draft_id_t *out)
{
    return amark_draft_append(&parser->draft, kind, start, parser->cursor.pos,
                              out);
}

/*
 * '\': before an ASCII punctuation character of the line, an escape of the
 * two bytes; right before a line ending (not the end of the content), a
 * hard break from the backslash to right after the line ending, and then
 * the content of the next line; otherwise (the end of the content
 * included) a text of the backslash.
 */
static amark_error_t scan_backslash(inline_parser_t *parser)
{
    amark_cursor_t *cursor = &parser->cursor;
    size_t start = cursor->pos;
    amark_draft_id_t unused;
    amark_error_t err;

    if (start + 1 < cursor->line_end &&
        is_ascii_punctuation(cursor->bytes[start + 1])) {
        amark_cursor_seek(cursor, start + 2);
        return append_to_cursor(parser, AMARK_KIND_BACKSLASH_ESCAPE, start,
                                &unused);
    }
    amark_cursor_seek(cursor, start + 1);
    if (!amark_cursor_at_line_end(cursor) || amark_cursor_at_end(cursor)) {
        return append_to_cursor(parser, AMARK_KIND_TEXT, start, &unused);
    }
    err = amark_draft_append(&parser->draft, AMARK_KIND_HARD_BREAK, start,
                             amark_cursor_line_break_end(cursor), &unused);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    amark_cursor_next_line(cursor);
    parser->has_break_start = false;
    return AMARK_ERROR_NONE;
}

/*
 * '`': the whole backtick string, which no backtick precedes (a string
 * before it was taken as a whole, and an escaped backtick is its own item),
 * opens a code span up to the next string of the same length, read through
 * the lines by the scan; if there is none, the whole string is a text.
 */
static amark_error_t scan_backticks(inline_parser_t *parser)
{
    amark_cursor_t *cursor = &parser->cursor;
    size_t start = cursor->pos;
    size_t end = start;
    bool found;
    amark_draft_id_t unused;
    amark_error_t err;

    while (end < cursor->line_end && cursor->bytes[end] == '`') {
        end++;
    }
    err = amark_inline_scan_code_span(cursor, &parser->memo, end - start,
                                      &found);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    if (found) {
        return append_to_cursor(parser, AMARK_KIND_CODE_SPAN, start,
                                &unused);
    }
    amark_cursor_seek(cursor, end);
    return append_to_cursor(parser, AMARK_KIND_TEXT, start, &unused);
}

/* '&': a character reference, or a text of the '&'. */
static amark_error_t scan_ampersand(inline_parser_t *parser)
{
    amark_cursor_t *cursor = &parser->cursor;
    size_t start = cursor->pos;
    amark_draft_id_t unused;

    if (amark_inline_scan_entity(cursor)) {
        return append_to_cursor(parser, AMARK_KIND_CHARACTER_REFERENCE, start,
                                &unused);
    }
    amark_cursor_seek(cursor, start + 1);
    return append_to_cursor(parser, AMARK_KIND_TEXT, start, &unused);
}

/*
 * '<': an autolink, tried first, then raw HTML, which the scan may read
 * through the lines; otherwise a text of the '<'.
 */
static amark_error_t scan_less_than(inline_parser_t *parser)
{
    amark_cursor_t *cursor = &parser->cursor;
    size_t start = cursor->pos;
    amark_autolink_type_t type;
    amark_draft_id_t id;
    amark_error_t err;

    if (amark_inline_scan_autolink(cursor, &type)) {
        err = append_to_cursor(parser, AMARK_KIND_AUTOLINK, start, &id);
        if (err == AMARK_ERROR_NONE) {
            amark_draft_set_autolink(&parser->draft, id, type);
        }
        return err;
    }
    if (amark_inline_scan_html(cursor, &parser->memo)) {
        return append_to_cursor(parser, AMARK_KIND_RAW_HTML, start, &id);
    }
    amark_cursor_seek(cursor, start + 1);
    return append_to_cursor(parser, AMARK_KIND_TEXT, start, &id);
}

/*
 * A delimiter character of kind (`*`, `_`, or one a dialect adds): the
 * whole run of that character in the line is a text item. Whether it can
 * open or close is decided from the characters right before and right
 * after the run, the ends of a line's content reading as U+000A; a run that
 * can do either is pushed onto the delimiter stack with its length, to be
 * matched when the content ends. The position moves forward by the length
 * of the run.
 */
static amark_error_t scan_delimiter_run(inline_parser_t *parser, size_t kind)
{
    amark_cursor_t *cursor = &parser->cursor;
    size_t start = cursor->pos;
    size_t end = start;
    unsigned char ch = cursor->bytes[start];
    uint32_t before;
    uint32_t after;
    bool can_open;
    bool can_close;
    amark_draft_id_t id;
    amark_error_t err;

    while (end < cursor->line_end && cursor->bytes[end] == ch) {
        end++;
    }
    before = amark_cursor_char_before(cursor, start);
    after = amark_cursor_char_at(cursor, end);
    amark_delimiter_classify(&parser->delimiters.kinds[kind], before, after,
                             &can_open, &can_close);
    amark_cursor_seek(cursor, end);
    err = append_to_cursor(parser, AMARK_KIND_TEXT, start, &id);
    if (err != AMARK_ERROR_NONE || (!can_open && !can_close)) {
        return err;
    }
    return amark_delimiters_push(&parser->delimiters, id, kind, can_open,
                                 can_close, end - start);
}

/*
 * '[' and '!': a "[", or a "![" (a '!' right before a '[' of the line), is
 * added as a text item and pushed onto the bracket stack, an image bracket
 * for "![". A '!' not followed by '[' is a text of the '!'.
 */
static amark_error_t scan_open_bracket(inline_parser_t *parser)
{
    amark_cursor_t *cursor = &parser->cursor;
    size_t start = cursor->pos;
    bool image = cursor->bytes[start] == '!';
    size_t open = image ? start + 1 : start;
    amark_draft_id_t id;
    amark_error_t err;

    if (image && (open >= cursor->line_end || cursor->bytes[open] != '[')) {
        amark_cursor_seek(cursor, start + 1);
        return append_to_cursor(parser, AMARK_KIND_TEXT, start, &id);
    }
    amark_cursor_seek(cursor, open + 1);
    err = append_to_cursor(parser, AMARK_KIND_TEXT, start, &id);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    return amark_brackets_push(&parser->brackets, id, image, open);
}

/*
 * ']': the bracket module decides whether a link or an image ends here (and
 * then moves past it); if not, a text of the ']'.
 */
static amark_error_t scan_close_bracket(inline_parser_t *parser)
{
    amark_cursor_t *cursor = &parser->cursor;
    size_t start = cursor->pos;
    amark_draft_id_t unused;
    bool made;
    amark_error_t err;

    err = amark_bracket_close(&parser->brackets, &parser->draft,
                              &parser->delimiters, cursor, &made);
    if (err != AMARK_ERROR_NONE || made) {
        return err;
    }
    amark_cursor_seek(cursor, start + 1);
    return append_to_cursor(parser, AMARK_KIND_TEXT, start, &unused);
}

/*
 * A special byte at the position: the one place where the rules of the
 * inline structures (and those a dialect adds) are called, chosen by the
 * byte. A structure read here is read to its end at once, so the special
 * bytes inside it are never scanned. A special byte that starts no
 * structure is a text of that byte. The position moves forward by one byte
 * or more.
 */
static amark_error_t scan_special(inline_parser_t *parser)
{
    amark_cursor_t *cursor = &parser->cursor;
    size_t start = cursor->pos;
    amark_draft_id_t unused;
    size_t kind = amark_delimiters_kind_of(&parser->delimiters,
                                           cursor->bytes[start]);

    if (kind != parser->delimiters.kind_count) {
        return scan_delimiter_run(parser, kind);
    }
    switch (cursor->bytes[start]) {
    case '\\':
        return scan_backslash(parser);
    case '`':
        return scan_backticks(parser);
    case '&':
        return scan_ampersand(parser);
    case '<':
        return scan_less_than(parser);
    case '[':
    case '!':
        return scan_open_bracket(parser);
    case ']':
        return scan_close_bracket(parser);
    default:
        break;
    }
    amark_cursor_seek(cursor, start + 1);
    return amark_draft_append(&parser->draft, AMARK_KIND_TEXT, start,
                              start + 1, &unused);
}

/*
 * The end of a line's content that is not the end of the content: a line
 * break from the start of the white space left out of the text (or the
 * line ending) to right after the line ending, hard if two or more spaces
 * come right before the line ending and soft otherwise. Then moves to the
 * content of the next line, past its prefix and leading white space, which
 * belong to no item. The position moves forward by the length of the line
 * ending or more.
 */
static amark_error_t scan_line_end(inline_parser_t *parser)
{
    amark_cursor_t *cursor = &parser->cursor;
    size_t line_end = cursor->line_end;
    size_t start = parser->has_break_start ? parser->break_start : line_end;
    size_t spaces = line_end;
    amark_kind_t kind = AMARK_KIND_SOFT_BREAK;
    amark_draft_id_t unused;
    amark_error_t err;

    while (spaces > start && cursor->bytes[spaces - 1] == ' ') {
        spaces--;
    }
    if (line_end - spaces >= 2) {
        kind = AMARK_KIND_HARD_BREAK;
    }
    err = amark_draft_append(&parser->draft, kind, start,
                             amark_cursor_line_break_end(cursor), &unused);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    amark_cursor_next_line(cursor);
    parser->has_break_start = false;
    return AMARK_ERROR_NONE;
}

/* Scans the content [start, end) into the items of the draft. */
static amark_error_t scan_content(inline_parser_t *parser, size_t start,
                                  size_t end)
{
    amark_cursor_t *cursor = &parser->cursor;
    amark_error_t err = AMARK_ERROR_NONE;

    amark_cursor_init(cursor, parser->doc, start, end);
    parser->has_break_start = false;
    while (err == AMARK_ERROR_NONE && !amark_cursor_at_end(cursor)) {
        if (amark_cursor_at_line_end(cursor)) {
            err = scan_line_end(parser);
        } else if (parser->special[cursor->bytes[cursor->pos]] != 0) {
            err = scan_special(parser);
        } else {
            err = scan_text(parser);
        }
    }
    return err;
}

/*
 * Parses one inline content, matches its delimiters from the start of the
 * content up, and writes its nodes under its block. On failure the content
 * is abandoned: the draft is not written.
 */
static amark_error_t parse_content(inline_parser_t *parser,
                                   const amark_inline_content_t *content)
{
    size_t depth = 0;
    amark_error_t err;

    err = amark_draft_reset(&parser->draft);
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    amark_delimiters_reset(&parser->delimiters);
    amark_brackets_reset(&parser->brackets);
    amark_inline_scan_memo_begin(&parser->memo);
    err = scan_content(parser, content->start, content->end);
    if (err == AMARK_ERROR_NONE) {
        err = amark_delimiters_process(&parser->delimiters, &parser->draft,
                                       content->start);
    }
    if (err != AMARK_ERROR_NONE) {
        return err;
    }
    if (parser->max_depth != 0) {
        depth = node_depth(parser->doc, content->node);
    }
    return amark_draft_emit(parser->doc, &parser->draft, content->node,
                            depth, parser->max_depth, parser->max_nodes);
}

amark_error_t amark_inline_parse(amark_document_t *doc,
                                 const amark_resolved_config_t *config,
                                 amark_arena_t *scratch,
                                 amark_dialect_run_t *run)
{
    inline_parser_t parser;
    amark_inline_content_t content;
    size_t index;
    amark_error_t err;

    (void)run; /* read once a dialect's rules are called */
    parser_init(&parser, doc, config, scratch);
    for (index = 0; amark_inline_content_at(doc, index, &content); index++) {
        if (content.start == content.end) {
            continue;
        }
        err = parse_content(&parser, &content);
        if (err != AMARK_ERROR_NONE) {
            return err;
        }
    }
    return AMARK_ERROR_NONE;
}
