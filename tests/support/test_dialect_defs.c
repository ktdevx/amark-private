/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#include "test_dialect_defs.h"

#include <limits.h>
#include <stdbool.h>
#include <string.h>

/* ---------------------------------------------------------------------- */
/* Rules that are never called */

static amark_error_t start_fn(const amark_dialect_line_t *line, void *state,
                              amark_dialect_block_start_result_t *out)
{
    (void)line;
    (void)state;
    (void)out;
    return AMARK_ERROR_NONE;
}

static amark_error_t continue_fn(const amark_dialect_line_t *line,
                                 const amark_dialect_open_block_t *block,
                                 void *state,
                                 amark_dialect_block_continue_result_t *out)
{
    (void)line;
    (void)block;
    (void)state;
    (void)out;
    return AMARK_ERROR_NONE;
}

static amark_error_t close_fn(const amark_dialect_closing_t *closing,
                              void *state, amark_dialect_reply_t *reply)
{
    (void)closing;
    (void)state;
    (void)reply;
    return AMARK_ERROR_NONE;
}

static amark_error_t inline_fn(const amark_dialect_inline_t *in, void *state,
                               amark_dialect_inline_result_t *out)
{
    (void)in;
    (void)state;
    (void)out;
    return AMARK_ERROR_NONE;
}

static amark_error_t state_new(const amark_allocator_t *allocator,
                               void **state)
{
    (void)allocator;
    *state = NULL;
    return AMARK_ERROR_NONE;
}

static void state_free(const amark_allocator_t *allocator, void *state)
{
    (void)allocator;
    (void)state;
}

/* ---------------------------------------------------------------------- */
/* The valid definition */

void amark_test_def_init(amark_test_def_t *d)
{
    memset(d, 0, sizeof(*d));

    d->kinds1[0].id = AMARK_TEST_DEF_BOX;
    d->kinds1[0].cls = AMARK_DIALECT_CONTAINER;
    d->kinds1[0].blank = AMARK_DIALECT_BLANK_CONTINUES;
    d->kinds1[0].continue_fn = continue_fn;
    d->kinds1[0].close_fn = close_fn;
    d->kinds1[1].id = AMARK_TEST_DEF_NOTE;
    d->kinds1[1].cls = AMARK_DIALECT_LEAF;
    d->kinds1[1].blank = AMARK_DIALECT_BLANK_CLOSES;
    d->kinds1[1].close_fn = close_fn;
    d->kinds1[2].id = AMARK_TEST_DEF_MARK;
    d->kinds1[2].cls = AMARK_DIALECT_INLINE;

    d->starts1[0].kind = AMARK_TEST_DEF_BOX;
    d->starts1[0].fn = start_fn;
    d->starts1[1].kind = AMARK_TEST_DEF_NOTE;
    d->starts1[1].interrupts_paragraph = true;
    d->starts1[1].fn = start_fn;
    d->inlines1[0].trigger = '@';
    d->inlines1[0].kind = AMARK_TEST_DEF_MARK;
    d->inlines1[0].fn = inline_fn;
    d->delims1[0].ch = '~';
    d->delims1[0].lengths = AMARK_DIALECT_LENGTH(1) | AMARK_DIALECT_LENGTH(2);
    d->delims1[0].kind = AMARK_TEST_DEF_MARK;

    d->ext1.name = "first";
    d->ext1.kinds = d->kinds1;
    d->ext1.kind_count = 3;
    d->ext1.block_starts = d->starts1;
    d->ext1.block_start_count = 2;
    d->ext1.close_paragraph = close_fn;
    d->ext1.inline_rules = d->inlines1;
    d->ext1.inline_rule_count = 1;
    d->ext1.delimiters = d->delims1;
    d->ext1.delimiter_count = 1;
    d->ext1.state_new = state_new;
    d->ext1.state_free = state_free;

    d->kinds2[0].id = AMARK_TEST_DEF_RULE;
    d->kinds2[0].cls = AMARK_DIALECT_LEAF;
    d->kinds2[1].id = AMARK_TEST_DEF_SUB;
    d->kinds2[1].cls = AMARK_DIALECT_INLINE;
    d->starts2[0].kind = AMARK_TEST_DEF_RULE;
    d->starts2[0].fn = start_fn;
    d->inlines2[0].trigger = '$';
    d->inlines2[0].kind = AMARK_TEST_DEF_SUB;
    d->inlines2[0].fn = inline_fn;
    d->delims2[0].ch = '^';
    d->delims2[0].lengths = AMARK_DIALECT_LENGTH(4);
    d->delims2[0].kind = AMARK_TEST_DEF_SUB;

    d->ext2.name = "second";
    d->ext2.kinds = d->kinds2;
    d->ext2.kind_count = 2;
    d->ext2.block_starts = d->starts2;
    d->ext2.block_start_count = 1;
    d->ext2.inline_rules = d->inlines2;
    d->ext2.inline_rule_count = 1;
    d->ext2.delimiters = d->delims2;
    d->ext2.delimiter_count = 1;

    d->kinds3[0].id = AMARK_TEST_DEF_EXTRA;
    d->kinds3[0].cls = AMARK_DIALECT_INLINE;
    d->delims3[0].ch = '|';
    d->delims3[0].lengths = AMARK_DIALECT_LENGTH(1);
    d->delims3[0].kind = AMARK_TEST_DEF_EXTRA;
    d->ext3.name = "third";
    d->ext3.kinds = d->kinds3;
    d->ext3.kind_count = 1;
    d->ext3.delimiters = d->delims3;
    d->ext3.delimiter_count = 1;

    d->exts[0] = &d->ext1;
    d->exts[1] = &d->ext2;
    d->exts[2] = &d->ext3;
    d->dialect.struct_size = sizeof(amark_dialect_t);
    d->dialect.abi = AMARK_DIALECT_ABI;
    d->dialect.extensions = d->exts;
    d->dialect.extension_count = 2;
}

void amark_test_def_set_delimiter_total(amark_test_def_t *d, size_t total)
{
    static const char chars[] = "^=+%#:";
    size_t i;

    for (i = 0; i + 1 < total; i++) {
        d->delims2[i].ch = (unsigned char)chars[i];
        d->delims2[i].lengths = AMARK_DIALECT_LENGTH(2);
        d->delims2[i].kind = AMARK_TEST_DEF_SUB;
    }
    d->ext2.delimiter_count = total - 1;
}

/* ---------------------------------------------------------------------- */
/* The breakers. arg is unused where the change has no value. */

/* Rule 1: the version (3.2). */

static void struct_size_set(amark_test_def_t *d, unsigned long arg)
{
    d->dialect.struct_size = (size_t)arg;
}

static void struct_size_short(amark_test_def_t *d, unsigned long arg)
{
    (void)arg;
    d->dialect.struct_size = sizeof(amark_dialect_t) - 1;
}

static void struct_size_long(amark_test_def_t *d, unsigned long arg)
{
    (void)arg;
    d->dialect.struct_size = sizeof(amark_dialect_t) + 1;
}

static void abi_set(amark_test_def_t *d, unsigned long arg)
{
    d->dialect.abi = (unsigned)arg;
}

/* Rule 2: the arrays. */

static void extensions_null(amark_test_def_t *d, unsigned long arg)
{
    (void)arg;
    d->dialect.extensions = NULL;
}

static void extension_null(amark_test_def_t *d, unsigned long arg)
{
    (void)arg;
    d->exts[1] = NULL;
}

static void kinds_null(amark_test_def_t *d, unsigned long arg)
{
    (void)arg;
    d->ext2.kinds = NULL;
}

static void block_starts_null(amark_test_def_t *d, unsigned long arg)
{
    (void)arg;
    d->ext1.block_starts = NULL;
}

static void inline_rules_null(amark_test_def_t *d, unsigned long arg)
{
    (void)arg;
    d->ext1.inline_rules = NULL;
}

static void delimiters_null(amark_test_def_t *d, unsigned long arg)
{
    (void)arg;
    d->ext2.delimiters = NULL;
}

/* Rule 3: the names (3.3). */

static void name_null(amark_test_def_t *d, unsigned long arg)
{
    (void)arg;
    d->ext2.name = NULL;
}

static void name_empty(amark_test_def_t *d, unsigned long arg)
{
    (void)arg;
    d->ext1.name = "";
}

/* Rule 4: the kinds (3.4, 3.5, 3.7). */

/* The id of BOX and of its start rule. */
static void box_id_set(amark_test_def_t *d, unsigned long arg)
{
    d->kinds1[0].id = (amark_kind_t)arg;
    d->starts1[0].kind = (amark_kind_t)arg;
}

/* The id of RULE and of its start rule. */
static void rule_id_set(amark_test_def_t *d, unsigned long arg)
{
    d->kinds2[0].id = (amark_kind_t)arg;
    d->starts2[0].kind = (amark_kind_t)arg;
}

/* The id of SUB and of its inline rule and delimiter kind. */
static void sub_id_set(amark_test_def_t *d, unsigned long arg)
{
    d->kinds2[1].id = (amark_kind_t)arg;
    d->inlines2[0].kind = (amark_kind_t)arg;
    d->delims2[0].kind = (amark_kind_t)arg;
}

/* The id of NOTE and of its start rule. */
static void note_id_set(amark_test_def_t *d, unsigned long arg)
{
    d->kinds1[1].id = (amark_kind_t)arg;
    d->starts1[1].kind = (amark_kind_t)arg;
}

static void cls_set(amark_test_def_t *d, unsigned long arg)
{
    d->kinds1[1].cls = (int)arg;
}

static void cls_negative(amark_test_def_t *d, unsigned long arg)
{
    (void)arg;
    d->kinds1[1].cls = -1;
}

static void blank_set(amark_test_def_t *d, unsigned long arg)
{
    d->kinds1[0].blank = (int)arg;
}

static void blank_negative(amark_test_def_t *d, unsigned long arg)
{
    (void)arg;
    d->kinds1[0].blank = -1;
}

static void inline_kind_continue_fn(amark_test_def_t *d, unsigned long arg)
{
    (void)arg;
    d->kinds1[2].continue_fn = continue_fn;
}

static void inline_kind_close_fn(amark_test_def_t *d, unsigned long arg)
{
    (void)arg;
    d->kinds2[1].close_fn = close_fn;
}

/* Rule 5: the block start rules (3.6, 3.7). */

static void start_fn_null(amark_test_def_t *d, unsigned long arg)
{
    (void)arg;
    d->starts1[1].fn = NULL;
}

static void start_kind_set(amark_test_def_t *d, unsigned long arg)
{
    d->starts2[0].kind = (amark_kind_t)arg;
}

/* Rule 6: the inline rules (3.6, 3.7). */

static void inline_fn_null(amark_test_def_t *d, unsigned long arg)
{
    (void)arg;
    d->inlines2[0].fn = NULL;
}

static void trigger_set(amark_test_def_t *d, unsigned long arg)
{
    d->inlines1[0].trigger = (unsigned char)arg;
}

static void inline_kind_set(amark_test_def_t *d, unsigned long arg)
{
    d->inlines1[0].kind = (amark_kind_t)arg;
}

/* Rule 7: the delimiter kinds (3.8, 8.6, 8.7, 8.8). */

static void delimiter_ch_set(amark_test_def_t *d, unsigned long arg)
{
    d->delims1[0].ch = (unsigned char)arg;
}

static void delimiter_ch_twice(amark_test_def_t *d, unsigned long arg)
{
    (void)arg;
    d->delims2[1] = d->delims2[0];
    d->ext2.delimiter_count = 2;
}

static void delimiter_lengths_set(amark_test_def_t *d, unsigned long arg)
{
    d->delims1[0].lengths = (unsigned)arg;
}

static void delimiter_kind_set(amark_test_def_t *d, unsigned long arg)
{
    d->delims1[0].kind = (amark_kind_t)arg;
}

static void delimiters_too_many(amark_test_def_t *d, unsigned long arg)
{
    (void)arg;
    /* The third extension adds one delimiter kind over the limit. */
    amark_test_def_set_delimiter_total(d, AMARK_DIALECT_DELIMITER_MAX);
    d->dialect.extension_count = 3;
}

/* Rule 8: the state functions (3.9). */

static void state_new_only(amark_test_def_t *d, unsigned long arg)
{
    (void)arg;
    d->ext1.state_free = NULL;
}

static void state_free_only(amark_test_def_t *d, unsigned long arg)
{
    (void)arg;
    d->ext2.state_free = state_free;
}

/* ---------------------------------------------------------------------- */
/* The table */

#define MIN_ AMARK_KIND_DIALECT_MIN

const amark_test_def_breaker_t amark_test_def_breakers[] = {
    /* Rule 1 (3.2). */
    {"struct_size 0", struct_size_set, 0},
    {"struct_size one short", struct_size_short, 0},
    {"struct_size one long", struct_size_long, 0},
    {"abi 0", abi_set, 0},
    {"abi of the next version", abi_set, AMARK_DIALECT_ABI + 1},
    /* Rule 2. */
    {"extensions NULL", extensions_null, 0},
    {"an extension NULL", extension_null, 0},
    {"kinds NULL with a count", kinds_null, 0},
    {"block_starts NULL with a count", block_starts_null, 0},
    {"inline_rules NULL with a count", inline_rules_null, 0},
    {"delimiters NULL with a count", delimiters_null, 0},
    /* Rule 3 (3.3). */
    {"name NULL", name_null, 0},
    {"name empty", name_empty, 0},
    /* Rule 4 (3.4, 3.5, 3.7). */
    {"kind id below the dialect space", box_id_set, MIN_ - 1},
    {"kind id of CommonMark", rule_id_set, AMARK_KIND_PARAGRAPH},
    {"kind id NONE", sub_id_set, AMARK_KIND_NONE},
    {"class 0", cls_set, 0},
    {"class above INLINE", cls_set, AMARK_DIALECT_INLINE + 1},
    {"class negative", cls_negative, 0},
    {"blank above CONTINUES", blank_set, AMARK_DIALECT_BLANK_CONTINUES + 1},
    {"blank negative", blank_negative, 0},
    {"inline kind with a continue rule", inline_kind_continue_fn, 0},
    {"inline kind with a close rule", inline_kind_close_fn, 0},
    {"same kind id in one extension", note_id_set, AMARK_TEST_DEF_BOX},
    {"same kind id in two extensions", rule_id_set, AMARK_TEST_DEF_NOTE},
    /* Rule 5 (3.6, 3.7). */
    {"start rule without fn", start_fn_null, 0},
    {"start rule of an unregistered kind", start_kind_set, MIN_ + 100},
    {"start rule of a CommonMark kind", start_kind_set, AMARK_KIND_PARAGRAPH},
    {"start rule of another extension's kind", start_kind_set,
     AMARK_TEST_DEF_NOTE},
    {"start rule of an inline kind", start_kind_set, AMARK_TEST_DEF_SUB},
    /* Rule 6 (3.6, 3.7). */
    {"inline rule without fn", inline_fn_null, 0},
    {"trigger 0x00", trigger_set, 0x00},
    {"trigger 0x20", trigger_set, 0x20},
    {"trigger 0x7F", trigger_set, 0x7F},
    {"trigger 0x80", trigger_set, 0x80},
    {"inline rule of an unregistered kind", inline_kind_set, MIN_ + 100},
    {"inline rule of a CommonMark kind", inline_kind_set,
     AMARK_KIND_EMPHASIS},
    {"inline rule of another extension's kind", inline_kind_set,
     AMARK_TEST_DEF_SUB},
    {"inline rule of a block kind", inline_kind_set, AMARK_TEST_DEF_NOTE},
    /* Rule 7, the character (3.8, 8.6). */
    {"delimiter 0x00", delimiter_ch_set, 0x00},
    {"delimiter space", delimiter_ch_set, ' '},
    {"delimiter tab", delimiter_ch_set, '\t'},
    {"delimiter digit 0", delimiter_ch_set, '0'},
    {"delimiter digit 9", delimiter_ch_set, '9'},
    {"delimiter letter a", delimiter_ch_set, 'a'},
    {"delimiter letter Z", delimiter_ch_set, 'Z'},
    {"delimiter 0x7F", delimiter_ch_set, 0x7F},
    {"delimiter 0x80", delimiter_ch_set, 0x80},
    {"delimiter 0xA1", delimiter_ch_set, 0xA1},
    {"delimiter 0xFF", delimiter_ch_set, 0xFF},
    {"delimiter CommonMark trigger \\", delimiter_ch_set, '\\'},
    {"delimiter CommonMark trigger `", delimiter_ch_set, '`'},
    {"delimiter CommonMark trigger &", delimiter_ch_set, '&'},
    {"delimiter CommonMark trigger <", delimiter_ch_set, '<'},
    {"delimiter CommonMark trigger *", delimiter_ch_set, '*'},
    {"delimiter CommonMark trigger _", delimiter_ch_set, '_'},
    {"delimiter CommonMark trigger [", delimiter_ch_set, '['},
    {"delimiter CommonMark trigger ]", delimiter_ch_set, ']'},
    {"delimiter CommonMark trigger !", delimiter_ch_set, '!'},
    {"delimiter twice in one extension", delimiter_ch_twice, 0},
    {"delimiter of another extension", delimiter_ch_set, '^'},
    {"delimiter on the trigger of another extension", delimiter_ch_set,
     '$'},
    {"delimiter on the trigger of its extension", delimiter_ch_set, '@'},
    /* Rule 7, the lengths (3.8, 8.7). */
    {"delimiter without lengths", delimiter_lengths_set, 0},
    {"delimiter lengths 1 and 5", delimiter_lengths_set,
     AMARK_DIALECT_LENGTH(1) | (1u << 4)},
    {"delimiter length 5", delimiter_lengths_set, 1u << 4},
    {"delimiter lengths all bits", delimiter_lengths_set, UINT_MAX},
    /* Rule 7, the kind (3.6, 3.7). */
    {"delimiter of an unregistered kind", delimiter_kind_set, MIN_ + 100},
    {"delimiter of a CommonMark kind", delimiter_kind_set, AMARK_KIND_STRONG},
    {"delimiter of another extension's kind", delimiter_kind_set,
     AMARK_TEST_DEF_SUB},
    {"delimiter of a block kind", delimiter_kind_set, AMARK_TEST_DEF_BOX},
    /* Rule 7, the number (8.8). */
    {"one delimiter kind over the limit", delimiters_too_many, 0},
    /* Rule 8 (3.9). */
    {"state_new only", state_new_only, 0},
    {"state_free only", state_free_only, 0},
};

const size_t amark_test_def_breaker_count =
    sizeof(amark_test_def_breakers) / sizeof(amark_test_def_breakers[0]);
