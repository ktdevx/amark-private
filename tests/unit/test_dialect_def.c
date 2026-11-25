/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of the definition of a dialect: the rules of
 * amark_dialect_validate (Requirements 3.2 to 3.9, 3.13, 8.6 to 8.8) and the
 * kind lookups.
 *
 * Each test builds a valid definition of two extensions in local structures
 * (def_init), checks that it passes, then breaks one rule only and checks
 * that it fails. amark_dialect_validate takes no allocator, so it cannot
 * allocate (3.13).
 */

#include "dialect_internal.h"

#include <amark/amark.h>
#include <amark/dialect.h>

#include <naos/naos.h>

#include <stddef.h>
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
/* A valid definition in local structures */

/* Kinds of the first extension. */
#define K_BOX (AMARK_KIND_DIALECT_MIN)      /* container */
#define K_NOTE (AMARK_KIND_DIALECT_MIN + 1) /* leaf */
#define K_MARK (AMARK_KIND_DIALECT_MIN + 2) /* inline */
/* Kinds of the second extension. */
#define K_RULE (AMARK_KIND_DIALECT_MIN + 10) /* leaf */
#define K_SUB (AMARK_KIND_DIALECT_MIN + 11)  /* inline */

typedef struct def {
    amark_dialect_kind_t kinds1[3];
    amark_dialect_block_start_t starts1[2];
    amark_dialect_inline_rule_t inlines1[1];
    amark_dialect_delimiter_t delims1[1];
    amark_dialect_kind_t kinds2[2];
    amark_dialect_block_start_t starts2[1];
    amark_dialect_inline_rule_t inlines2[1];
    amark_dialect_delimiter_t delims2[AMARK_DIALECT_DELIMITER_MAX];
    amark_dialect_extension_t ext1;
    amark_dialect_extension_t ext2;
    const amark_dialect_extension_t *exts[2];
    amark_dialect_t dialect;
} def_t;

/*
 * Two extensions. The first has a container, a leaf and an inline kind, two
 * start rules, an inline rule on '@', a delimiter kind on '~' and state
 * functions. The second has a leaf and an inline kind, a start rule, an
 * inline rule on '$', a delimiter kind on '^' and no state functions.
 */
static void def_init(def_t *d)
{
    memset(d, 0, sizeof(*d));

    d->kinds1[0].id = K_BOX;
    d->kinds1[0].cls = AMARK_DIALECT_CONTAINER;
    d->kinds1[0].blank = AMARK_DIALECT_BLANK_CONTINUES;
    d->kinds1[0].continue_fn = continue_fn;
    d->kinds1[0].close_fn = close_fn;
    d->kinds1[1].id = K_NOTE;
    d->kinds1[1].cls = AMARK_DIALECT_LEAF;
    d->kinds1[1].blank = AMARK_DIALECT_BLANK_CLOSES;
    d->kinds1[1].close_fn = close_fn;
    d->kinds1[2].id = K_MARK;
    d->kinds1[2].cls = AMARK_DIALECT_INLINE;

    d->starts1[0].kind = K_BOX;
    d->starts1[0].fn = start_fn;
    d->starts1[1].kind = K_NOTE;
    d->starts1[1].interrupts_paragraph = true;
    d->starts1[1].fn = start_fn;
    d->inlines1[0].trigger = '@';
    d->inlines1[0].kind = K_MARK;
    d->inlines1[0].fn = inline_fn;
    d->delims1[0].ch = '~';
    d->delims1[0].lengths = AMARK_DIALECT_LENGTH(1) | AMARK_DIALECT_LENGTH(2);
    d->delims1[0].kind = K_MARK;

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

    d->kinds2[0].id = K_RULE;
    d->kinds2[0].cls = AMARK_DIALECT_LEAF;
    d->kinds2[1].id = K_SUB;
    d->kinds2[1].cls = AMARK_DIALECT_INLINE;
    d->starts2[0].kind = K_RULE;
    d->starts2[0].fn = start_fn;
    d->inlines2[0].trigger = '$';
    d->inlines2[0].kind = K_SUB;
    d->inlines2[0].fn = inline_fn;
    d->delims2[0].ch = '^';
    d->delims2[0].lengths = AMARK_DIALECT_LENGTH(4);
    d->delims2[0].kind = K_SUB;

    d->ext2.name = "second";
    d->ext2.kinds = d->kinds2;
    d->ext2.kind_count = 2;
    d->ext2.block_starts = d->starts2;
    d->ext2.block_start_count = 1;
    d->ext2.inline_rules = d->inlines2;
    d->ext2.inline_rule_count = 1;
    d->ext2.delimiters = d->delims2;
    d->ext2.delimiter_count = 1;

    d->exts[0] = &d->ext1;
    d->exts[1] = &d->ext2;
    d->dialect.struct_size = sizeof(amark_dialect_t);
    d->dialect.abi = AMARK_DIALECT_ABI;
    d->dialect.extensions = d->exts;
    d->dialect.extension_count = 2;
}

/* Every test starts from a valid definition. */
#define VALID_DEF(d)                                                       \
    def_t d;                                                               \
    def_init(&d);                                                          \
    NAOS_REQUIRE_TRUE(amark_dialect_validate(&d.dialect))

/* ---------------------------------------------------------------------- */
/* Valid definitions */

NAOS_TEST(two_extensions_are_valid)
{
    VALID_DEF(d);
    NAOS_CHECK_TRUE(amark_dialect_validate(&d.dialect));
}

NAOS_TEST(zero_extensions_are_valid)
{
    amark_dialect_t dialect;

    dialect.struct_size = sizeof(amark_dialect_t);
    dialect.abi = AMARK_DIALECT_ABI;
    dialect.extensions = NULL;
    dialect.extension_count = 0;
    NAOS_CHECK_TRUE(amark_dialect_validate(&dialect));
}

NAOS_TEST(zero_extensions_with_an_array_are_valid)
{
    VALID_DEF(d);
    d.dialect.extension_count = 0;
    NAOS_CHECK_TRUE(amark_dialect_validate(&d.dialect));
}

NAOS_TEST(extension_with_a_name_only_is_valid)
{
    amark_dialect_extension_t ext;
    const amark_dialect_extension_t *exts[1];
    amark_dialect_t dialect;

    memset(&ext, 0, sizeof(ext));
    ext.name = "x";
    exts[0] = &ext;
    dialect.struct_size = sizeof(amark_dialect_t);
    dialect.abi = AMARK_DIALECT_ABI;
    dialect.extensions = exts;
    dialect.extension_count = 1;
    NAOS_CHECK_TRUE(amark_dialect_validate(&dialect));
}

NAOS_TEST(null_dialect_is_invalid)
{
    NAOS_CHECK_FALSE(amark_dialect_validate(NULL));
}

NAOS_TEST(validate_changes_nothing_and_repeats)
{
    def_t copy;
    bool first;
    VALID_DEF(d);

    memcpy(&copy, &d, sizeof(d));
    NAOS_CHECK_TRUE(amark_dialect_validate(&d.dialect));
    NAOS_CHECK_TRUE(amark_dialect_validate(&d.dialect));
    NAOS_CHECK_TRUE(memcmp(&copy, &d, sizeof(d)) == 0);

    d.ext2.name = "";
    memcpy(&copy, &d, sizeof(d));
    first = amark_dialect_validate(&d.dialect);
    NAOS_CHECK_FALSE(first);
    NAOS_CHECK_TRUE(amark_dialect_validate(&d.dialect) == first);
    NAOS_CHECK_TRUE(memcmp(&copy, &d, sizeof(d)) == 0);
}

/* ---------------------------------------------------------------------- */
/* Rule 1: the version (3.2) */

NAOS_TEST(rule1_wrong_struct_size_is_invalid)
{
    VALID_DEF(d);
    d.dialect.struct_size = sizeof(amark_dialect_t) - 1;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
    d.dialect.struct_size = sizeof(amark_dialect_t) + 1;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
    d.dialect.struct_size = 0;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
}

NAOS_TEST(rule1_wrong_abi_is_invalid)
{
    VALID_DEF(d);
    d.dialect.abi = 0;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
    d.dialect.abi = AMARK_DIALECT_ABI + 1;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
}

NAOS_TEST(rule1_is_checked_before_the_extensions_are_read)
{
    VALID_DEF(d);
    /* With rule 1 broken, the NULL array is never read. */
    d.dialect.abi = 0;
    d.dialect.extensions = NULL;
    d.dialect.extension_count = 1000;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
}

/* ---------------------------------------------------------------------- */
/* Rule 2: the arrays */

NAOS_TEST(rule2_null_extensions_are_invalid)
{
    VALID_DEF(d);
    d.dialect.extensions = NULL;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
}

NAOS_TEST(rule2_null_extension_is_invalid)
{
    VALID_DEF(d);
    d.exts[1] = NULL;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
}

NAOS_TEST(rule2_null_kinds_with_a_count_are_invalid)
{
    VALID_DEF(d);
    d.ext2.kinds = NULL;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
}

NAOS_TEST(rule2_null_block_starts_with_a_count_are_invalid)
{
    VALID_DEF(d);
    d.ext1.block_starts = NULL;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
}

NAOS_TEST(rule2_null_inline_rules_with_a_count_are_invalid)
{
    VALID_DEF(d);
    d.ext1.inline_rules = NULL;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
}

NAOS_TEST(rule2_null_delimiters_with_a_count_are_invalid)
{
    VALID_DEF(d);
    d.ext2.delimiters = NULL;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
}

NAOS_TEST(rule2_null_arrays_with_no_count_are_valid)
{
    VALID_DEF(d);
    d.ext2.block_starts = NULL;
    d.ext2.block_start_count = 0;
    d.ext2.inline_rules = NULL;
    d.ext2.inline_rule_count = 0;
    d.ext2.delimiters = NULL;
    d.ext2.delimiter_count = 0;
    NAOS_CHECK_TRUE(amark_dialect_validate(&d.dialect));
    d.ext2.kinds = NULL;
    d.ext2.kind_count = 0;
    NAOS_CHECK_TRUE(amark_dialect_validate(&d.dialect));
}

/* ---------------------------------------------------------------------- */
/* Rule 3: the names (3.3) */

NAOS_TEST(rule3_null_name_is_invalid)
{
    VALID_DEF(d);
    d.ext2.name = NULL;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
}

NAOS_TEST(rule3_empty_name_is_invalid)
{
    VALID_DEF(d);
    d.ext1.name = "";
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
}

NAOS_TEST(rule3_same_names_are_valid)
{
    VALID_DEF(d);
    d.ext2.name = "first";
    NAOS_CHECK_TRUE(amark_dialect_validate(&d.dialect));
}

/* ---------------------------------------------------------------------- */
/* Rule 4: the kinds (3.4, 3.5, 3.7) */

NAOS_TEST(rule4_id_below_the_dialect_space_is_invalid)
{
    VALID_DEF(d);
    /* K_BOX is AMARK_KIND_DIALECT_MIN itself, which is valid. */
    d.kinds1[0].id = AMARK_KIND_DIALECT_MIN - 1;
    d.starts1[0].kind = AMARK_KIND_DIALECT_MIN - 1;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
}

NAOS_TEST(rule4_commonmark_id_is_invalid)
{
    VALID_DEF(d);
    d.kinds2[0].id = AMARK_KIND_PARAGRAPH;
    d.starts2[0].kind = AMARK_KIND_PARAGRAPH;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
}

NAOS_TEST(rule4_none_id_is_invalid)
{
    VALID_DEF(d);
    d.kinds2[1].id = AMARK_KIND_NONE;
    d.inlines2[0].kind = AMARK_KIND_NONE;
    d.delims2[0].kind = AMARK_KIND_NONE;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
}

NAOS_TEST(rule4_undefined_class_is_invalid)
{
    VALID_DEF(d);
    d.kinds1[1].cls = 0;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
    d.kinds1[1].cls = AMARK_DIALECT_INLINE + 1;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
    d.kinds1[1].cls = -1;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
}

NAOS_TEST(rule4_undefined_blank_is_invalid)
{
    VALID_DEF(d);
    d.kinds1[0].blank = AMARK_DIALECT_BLANK_CONTINUES + 1;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
    d.kinds1[0].blank = -1;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
}

NAOS_TEST(rule4_inline_kind_with_a_continue_rule_is_invalid)
{
    VALID_DEF(d);
    d.kinds1[2].continue_fn = continue_fn;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
}

NAOS_TEST(rule4_inline_kind_with_a_close_rule_is_invalid)
{
    VALID_DEF(d);
    d.kinds2[1].close_fn = close_fn;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
}

NAOS_TEST(rule4_same_id_in_one_extension_is_invalid)
{
    VALID_DEF(d);
    d.kinds1[1].id = K_BOX;
    d.starts1[1].kind = K_BOX;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
}

NAOS_TEST(rule4_same_id_in_two_extensions_is_invalid)
{
    VALID_DEF(d);
    d.kinds2[0].id = K_NOTE;
    d.starts2[0].kind = K_NOTE;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
}

/* ---------------------------------------------------------------------- */
/* Rule 5: the block start rules (3.6, 3.7) */

NAOS_TEST(rule5_null_start_fn_is_invalid)
{
    VALID_DEF(d);
    d.starts1[1].fn = NULL;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
}

NAOS_TEST(rule5_unregistered_kind_is_invalid)
{
    VALID_DEF(d);
    d.starts1[0].kind = AMARK_KIND_DIALECT_MIN + 100;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
}

NAOS_TEST(rule5_commonmark_kind_is_invalid)
{
    VALID_DEF(d);
    d.starts1[0].kind = AMARK_KIND_PARAGRAPH;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
}

NAOS_TEST(rule5_kind_of_another_extension_is_invalid)
{
    VALID_DEF(d);
    d.starts2[0].kind = K_NOTE;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
}

NAOS_TEST(rule5_inline_kind_is_invalid)
{
    VALID_DEF(d);
    d.starts1[0].kind = K_MARK;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
}

/* ---------------------------------------------------------------------- */
/* Rule 6: the inline rules (3.6, 3.7) */

NAOS_TEST(rule6_null_inline_fn_is_invalid)
{
    VALID_DEF(d);
    d.inlines2[0].fn = NULL;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
}

NAOS_TEST(rule6_trigger_out_of_range_is_invalid)
{
    VALID_DEF(d);
    d.inlines1[0].trigger = 0x20;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
    d.inlines1[0].trigger = 0x7F;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
    d.inlines1[0].trigger = 0;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
    d.inlines1[0].trigger = 0x80;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
}

NAOS_TEST(rule6_trigger_bounds_are_valid)
{
    VALID_DEF(d);
    d.inlines1[0].trigger = 0x21;
    NAOS_CHECK_TRUE(amark_dialect_validate(&d.dialect));
    d.inlines1[0].trigger = 0x7E;
    d.delims1[0].ch = '=';
    NAOS_CHECK_TRUE(amark_dialect_validate(&d.dialect));
    /* A letter and a CommonMark trigger are allowed for inline rules. */
    d.inlines1[0].trigger = 'a';
    NAOS_CHECK_TRUE(amark_dialect_validate(&d.dialect));
    d.inlines2[0].trigger = '*';
    NAOS_CHECK_TRUE(amark_dialect_validate(&d.dialect));
}

NAOS_TEST(rule6_same_trigger_in_two_rules_is_valid)
{
    VALID_DEF(d);
    d.inlines2[0].trigger = '@';
    NAOS_CHECK_TRUE(amark_dialect_validate(&d.dialect));
}

NAOS_TEST(rule6_unregistered_kind_is_invalid)
{
    VALID_DEF(d);
    d.inlines1[0].kind = AMARK_KIND_DIALECT_MIN + 100;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
}

NAOS_TEST(rule6_commonmark_kind_is_invalid)
{
    VALID_DEF(d);
    d.inlines1[0].kind = AMARK_KIND_EMPHASIS;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
}

NAOS_TEST(rule6_kind_of_another_extension_is_invalid)
{
    VALID_DEF(d);
    d.inlines1[0].kind = K_SUB;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
}

NAOS_TEST(rule6_block_kind_is_invalid)
{
    VALID_DEF(d);
    d.inlines1[0].kind = K_NOTE;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
}

/* ---------------------------------------------------------------------- */
/* Rule 7: the delimiter kinds (3.6, 3.7, 8.6, 8.7, 8.8) */

NAOS_TEST(rule7_every_allowed_character_is_valid)
{
    static const char allowed[] = "\"#$%'()+,-./:;=>?@^{|}~";
    size_t i;
    VALID_DEF(d);

    /* The other characters of the dialect are moved out of the way. */
    d.ext1.inline_rule_count = 0;
    d.ext2.inline_rule_count = 0;
    d.ext2.delimiter_count = 0;
    for (i = 0; i < sizeof(allowed) - 1; i++) {
        d.delims1[0].ch = (unsigned char)allowed[i];
        NAOS_CHECK_TRUE(amark_dialect_validate(&d.dialect));
    }
}

NAOS_TEST(rule7_not_punctuation_is_invalid)
{
    static const unsigned char bad[] = {0, ' ', '\t', '0', '9', 'a', 'Z',
                                        0x7F, 0x80, 0xA1, 0xFF};
    size_t i;
    VALID_DEF(d);

    for (i = 0; i < sizeof(bad); i++) {
        d.delims1[0].ch = bad[i];
        NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
    }
}

NAOS_TEST(rule7_commonmark_trigger_is_invalid)
{
    static const char triggers[] = "\\`&<*_[]!";
    size_t i;
    VALID_DEF(d);

    for (i = 0; i < sizeof(triggers) - 1; i++) {
        d.delims2[0].ch = (unsigned char)triggers[i];
        NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
    }
}

NAOS_TEST(rule7_same_character_in_one_extension_is_invalid)
{
    VALID_DEF(d);
    d.delims2[1] = d.delims2[0];
    d.ext2.delimiter_count = 2;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
    d.delims2[1].ch = '=';
    NAOS_CHECK_TRUE(amark_dialect_validate(&d.dialect));
}

NAOS_TEST(rule7_same_character_in_two_extensions_is_invalid)
{
    VALID_DEF(d);
    d.delims2[0].ch = '~';
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
}

NAOS_TEST(rule7_trigger_of_an_inline_rule_is_invalid)
{
    VALID_DEF(d);
    /* The inline rule of the other extension. */
    d.delims1[0].ch = '$';
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
    /* The inline rule of the same extension. */
    d.delims1[0].ch = '@';
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
}

NAOS_TEST(rule7_no_length_is_invalid)
{
    VALID_DEF(d);
    d.delims1[0].lengths = 0;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
}

NAOS_TEST(rule7_length_above_four_is_invalid)
{
    VALID_DEF(d);
    d.delims1[0].lengths = AMARK_DIALECT_LENGTH(1) | (1u << 4);
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
    d.delims1[0].lengths = 1u << 4;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
    d.delims1[0].lengths = ~0u;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
}

NAOS_TEST(rule7_every_length_set_is_valid)
{
    unsigned lengths;
    VALID_DEF(d);

    for (lengths = 1; lengths <= 0xFu; lengths++) {
        d.delims1[0].lengths = lengths;
        NAOS_CHECK_TRUE(amark_dialect_validate(&d.dialect));
    }
}

NAOS_TEST(rule7_unregistered_kind_is_invalid)
{
    VALID_DEF(d);
    d.delims1[0].kind = AMARK_KIND_DIALECT_MIN + 100;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
}

NAOS_TEST(rule7_commonmark_kind_is_invalid)
{
    VALID_DEF(d);
    d.delims1[0].kind = AMARK_KIND_STRONG;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
}

NAOS_TEST(rule7_kind_of_another_extension_is_invalid)
{
    VALID_DEF(d);
    d.delims2[0].kind = K_MARK;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
}

NAOS_TEST(rule7_block_kind_is_invalid)
{
    VALID_DEF(d);
    d.delims1[0].kind = K_BOX;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
}

/* Fills the delimiters of the second extension up to a total of total. */
static void set_delimiter_total(def_t *d, size_t total)
{
    static const char chars[] = "^=+%#:";
    size_t i;

    for (i = 0; i + 1 < total; i++) {
        d->delims2[i].ch = (unsigned char)chars[i];
        d->delims2[i].lengths = AMARK_DIALECT_LENGTH(2);
        d->delims2[i].kind = K_SUB;
    }
    d->ext2.delimiter_count = total - 1;
}

NAOS_TEST(rule7_the_most_delimiters_are_valid)
{
    VALID_DEF(d);
    set_delimiter_total(&d, AMARK_DIALECT_DELIMITER_MAX);
    NAOS_CHECK_TRUE(amark_dialect_validate(&d.dialect));
}

NAOS_TEST(rule7_too_many_delimiters_are_invalid)
{
    amark_dialect_delimiter_t delims3[1];
    amark_dialect_kind_t kinds3[1];
    amark_dialect_extension_t ext3;
    const amark_dialect_extension_t *exts[3];
    VALID_DEF(d);

    /* A third extension adds one delimiter kind over the limit. */
    set_delimiter_total(&d, AMARK_DIALECT_DELIMITER_MAX);
    memset(&ext3, 0, sizeof(ext3));
    memset(kinds3, 0, sizeof(kinds3));
    memset(delims3, 0, sizeof(delims3));
    kinds3[0].id = AMARK_KIND_DIALECT_MIN + 20;
    kinds3[0].cls = AMARK_DIALECT_INLINE;
    delims3[0].ch = '|';
    delims3[0].lengths = AMARK_DIALECT_LENGTH(1);
    delims3[0].kind = AMARK_KIND_DIALECT_MIN + 20;
    ext3.name = "third";
    ext3.kinds = kinds3;
    ext3.kind_count = 1;
    ext3.delimiters = delims3;
    ext3.delimiter_count = 1;
    exts[0] = &d.ext1;
    exts[1] = &d.ext2;
    exts[2] = &ext3;
    d.dialect.extensions = exts;
    d.dialect.extension_count = 3;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));

    /* Without the extra one, the same three extensions are valid. */
    ext3.delimiter_count = 0;
    NAOS_CHECK_TRUE(amark_dialect_validate(&d.dialect));
}

/* ---------------------------------------------------------------------- */
/* Rule 8: the state functions (3.9) */

NAOS_TEST(rule8_state_new_only_is_invalid)
{
    VALID_DEF(d);
    d.ext1.state_free = NULL;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
}

NAOS_TEST(rule8_state_free_only_is_invalid)
{
    VALID_DEF(d);
    d.ext2.state_free = state_free;
    NAOS_CHECK_FALSE(amark_dialect_validate(&d.dialect));
}

NAOS_TEST(rule8_both_or_neither_are_valid)
{
    VALID_DEF(d);
    d.ext2.state_new = state_new;
    d.ext2.state_free = state_free;
    NAOS_CHECK_TRUE(amark_dialect_validate(&d.dialect));
    d.ext1.state_new = NULL;
    d.ext1.state_free = NULL;
    NAOS_CHECK_TRUE(amark_dialect_validate(&d.dialect));
}

NAOS_SUITE(validate,
    NAOS_ADD_TEST(two_extensions_are_valid),
    NAOS_ADD_TEST(zero_extensions_are_valid),
    NAOS_ADD_TEST(zero_extensions_with_an_array_are_valid),
    NAOS_ADD_TEST(extension_with_a_name_only_is_valid),
    NAOS_ADD_TEST(null_dialect_is_invalid),
    NAOS_ADD_TEST(validate_changes_nothing_and_repeats),
    NAOS_ADD_TEST(rule1_wrong_struct_size_is_invalid),
    NAOS_ADD_TEST(rule1_wrong_abi_is_invalid),
    NAOS_ADD_TEST(rule1_is_checked_before_the_extensions_are_read),
    NAOS_ADD_TEST(rule2_null_extensions_are_invalid),
    NAOS_ADD_TEST(rule2_null_extension_is_invalid),
    NAOS_ADD_TEST(rule2_null_kinds_with_a_count_are_invalid),
    NAOS_ADD_TEST(rule2_null_block_starts_with_a_count_are_invalid),
    NAOS_ADD_TEST(rule2_null_inline_rules_with_a_count_are_invalid),
    NAOS_ADD_TEST(rule2_null_delimiters_with_a_count_are_invalid),
    NAOS_ADD_TEST(rule2_null_arrays_with_no_count_are_valid),
    NAOS_ADD_TEST(rule3_null_name_is_invalid),
    NAOS_ADD_TEST(rule3_empty_name_is_invalid),
    NAOS_ADD_TEST(rule3_same_names_are_valid),
    NAOS_ADD_TEST(rule4_id_below_the_dialect_space_is_invalid),
    NAOS_ADD_TEST(rule4_commonmark_id_is_invalid),
    NAOS_ADD_TEST(rule4_none_id_is_invalid),
    NAOS_ADD_TEST(rule4_undefined_class_is_invalid),
    NAOS_ADD_TEST(rule4_undefined_blank_is_invalid),
    NAOS_ADD_TEST(rule4_inline_kind_with_a_continue_rule_is_invalid),
    NAOS_ADD_TEST(rule4_inline_kind_with_a_close_rule_is_invalid),
    NAOS_ADD_TEST(rule4_same_id_in_one_extension_is_invalid),
    NAOS_ADD_TEST(rule4_same_id_in_two_extensions_is_invalid),
    NAOS_ADD_TEST(rule5_null_start_fn_is_invalid),
    NAOS_ADD_TEST(rule5_unregistered_kind_is_invalid),
    NAOS_ADD_TEST(rule5_commonmark_kind_is_invalid),
    NAOS_ADD_TEST(rule5_kind_of_another_extension_is_invalid),
    NAOS_ADD_TEST(rule5_inline_kind_is_invalid),
    NAOS_ADD_TEST(rule6_null_inline_fn_is_invalid),
    NAOS_ADD_TEST(rule6_trigger_out_of_range_is_invalid),
    NAOS_ADD_TEST(rule6_trigger_bounds_are_valid),
    NAOS_ADD_TEST(rule6_same_trigger_in_two_rules_is_valid),
    NAOS_ADD_TEST(rule6_unregistered_kind_is_invalid),
    NAOS_ADD_TEST(rule6_commonmark_kind_is_invalid),
    NAOS_ADD_TEST(rule6_kind_of_another_extension_is_invalid),
    NAOS_ADD_TEST(rule6_block_kind_is_invalid),
    NAOS_ADD_TEST(rule7_every_allowed_character_is_valid),
    NAOS_ADD_TEST(rule7_not_punctuation_is_invalid),
    NAOS_ADD_TEST(rule7_commonmark_trigger_is_invalid),
    NAOS_ADD_TEST(rule7_same_character_in_one_extension_is_invalid),
    NAOS_ADD_TEST(rule7_same_character_in_two_extensions_is_invalid),
    NAOS_ADD_TEST(rule7_trigger_of_an_inline_rule_is_invalid),
    NAOS_ADD_TEST(rule7_no_length_is_invalid),
    NAOS_ADD_TEST(rule7_length_above_four_is_invalid),
    NAOS_ADD_TEST(rule7_every_length_set_is_valid),
    NAOS_ADD_TEST(rule7_unregistered_kind_is_invalid),
    NAOS_ADD_TEST(rule7_commonmark_kind_is_invalid),
    NAOS_ADD_TEST(rule7_kind_of_another_extension_is_invalid),
    NAOS_ADD_TEST(rule7_block_kind_is_invalid),
    NAOS_ADD_TEST(rule7_the_most_delimiters_are_valid),
    NAOS_ADD_TEST(rule7_too_many_delimiters_are_invalid),
    NAOS_ADD_TEST(rule8_state_new_only_is_invalid),
    NAOS_ADD_TEST(rule8_state_free_only_is_invalid),
    NAOS_ADD_TEST(rule8_both_or_neither_are_valid)
)

/* ---------------------------------------------------------------------- */
/* Kind lookup */

NAOS_TEST(find_kind_finds_the_kinds_of_the_extension)
{
    VALID_DEF(d);
    NAOS_CHECK_TRUE(amark_dialect_find_kind(&d.ext1, K_BOX) == &d.kinds1[0]);
    NAOS_CHECK_TRUE(amark_dialect_find_kind(&d.ext1, K_NOTE) == &d.kinds1[1]);
    NAOS_CHECK_TRUE(amark_dialect_find_kind(&d.ext1, K_MARK) == &d.kinds1[2]);
    NAOS_CHECK_TRUE(amark_dialect_find_kind(&d.ext2, K_SUB) == &d.kinds2[1]);
}

NAOS_TEST(find_kind_does_not_find_other_kinds)
{
    VALID_DEF(d);
    NAOS_CHECK_NULL(amark_dialect_find_kind(&d.ext1, K_RULE));
    NAOS_CHECK_NULL(amark_dialect_find_kind(&d.ext2, K_BOX));
    NAOS_CHECK_NULL(amark_dialect_find_kind(&d.ext1, AMARK_KIND_PARAGRAPH));
    NAOS_CHECK_NULL(amark_dialect_find_kind(&d.ext1, AMARK_KIND_NONE));
    NAOS_CHECK_NULL(
        amark_dialect_find_kind(&d.ext1, AMARK_KIND_DIALECT_MIN + 100));
}

NAOS_TEST(find_kind_in_an_extension_without_kinds)
{
    amark_dialect_extension_t ext;

    memset(&ext, 0, sizeof(ext));
    ext.name = "x";
    NAOS_CHECK_NULL(amark_dialect_find_kind(&ext, AMARK_KIND_DIALECT_MIN));
}

NAOS_TEST(find_kind_any_finds_the_kinds_of_every_extension)
{
    VALID_DEF(d);
    NAOS_CHECK_TRUE(amark_dialect_find_kind_any(&d.dialect, K_BOX) ==
                    &d.kinds1[0]);
    NAOS_CHECK_TRUE(amark_dialect_find_kind_any(&d.dialect, K_MARK) ==
                    &d.kinds1[2]);
    NAOS_CHECK_TRUE(amark_dialect_find_kind_any(&d.dialect, K_RULE) ==
                    &d.kinds2[0]);
    NAOS_CHECK_TRUE(amark_dialect_find_kind_any(&d.dialect, K_SUB) ==
                    &d.kinds2[1]);
}

NAOS_TEST(find_kind_any_does_not_find_other_kinds)
{
    VALID_DEF(d);
    NAOS_CHECK_NULL(
        amark_dialect_find_kind_any(&d.dialect, AMARK_KIND_PARAGRAPH));
    NAOS_CHECK_NULL(amark_dialect_find_kind_any(&d.dialect, AMARK_KIND_NONE));
    NAOS_CHECK_NULL(amark_dialect_find_kind_any(
        &d.dialect, AMARK_KIND_DIALECT_MIN + 100));
}

NAOS_TEST(find_kind_any_in_a_dialect_without_extensions)
{
    amark_dialect_t dialect;

    dialect.struct_size = sizeof(amark_dialect_t);
    dialect.abi = AMARK_DIALECT_ABI;
    dialect.extensions = NULL;
    dialect.extension_count = 0;
    NAOS_CHECK_NULL(
        amark_dialect_find_kind_any(&dialect, AMARK_KIND_DIALECT_MIN));
}

NAOS_SUITE(find_kind,
    NAOS_ADD_TEST(find_kind_finds_the_kinds_of_the_extension),
    NAOS_ADD_TEST(find_kind_does_not_find_other_kinds),
    NAOS_ADD_TEST(find_kind_in_an_extension_without_kinds),
    NAOS_ADD_TEST(find_kind_any_finds_the_kinds_of_every_extension),
    NAOS_ADD_TEST(find_kind_any_does_not_find_other_kinds),
    NAOS_ADD_TEST(find_kind_any_in_a_dialect_without_extensions)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(validate),
    NAOS_ADD_SUITE(find_kind)
)
