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
 * (amark_test_def_init of test_dialect_defs.h), checks that it passes, then
 * changes it and checks the result. The invalid definitions are the
 * breakers of test_dialect_defs.h, shared with the configuration tests;
 * each breaks one condition only. amark_dialect_validate takes no
 * allocator, so it cannot allocate (3.13).
 */

#include "dialect_internal.h"
#include "test_dialect_defs.h"

#include <amark/amark.h>
#include <amark/dialect.h>

#include <naos/naos.h>

#include <stddef.h>
#include <string.h>

#define K_BOX AMARK_TEST_DEF_BOX
#define K_NOTE AMARK_TEST_DEF_NOTE
#define K_MARK AMARK_TEST_DEF_MARK
#define K_RULE AMARK_TEST_DEF_RULE
#define K_SUB AMARK_TEST_DEF_SUB

typedef amark_test_def_t def_t;

/* Every test starts from a valid definition. */
#define VALID_DEF(d)                                                       \
    def_t d;                                                               \
    amark_test_def_init(&d);                                               \
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
/* Invalid definitions: every breaker, each on its own (3.2 to 3.9, 8.6 to
   8.8) */

NAOS_TEST(every_breaker_is_invalid)
{
    size_t i;

    NAOS_REQUIRE_UINT_GT(amark_test_def_breaker_count, 0);
    for (i = 0; i < amark_test_def_breaker_count; i++) {
        const amark_test_def_breaker_t *b = &amark_test_def_breakers[i];
        def_t copy;
        bool first;
        VALID_DEF(d);

        b->apply(&d, b->arg);
        memcpy(&copy, &d, sizeof(d));
        first = amark_dialect_validate(&d.dialect);
        /* The breaker's name is the message of a failed check. */
        (void)naos_check(!first, b->name, __FILE__, __LINE__);
        /* The same result again, and the definition is not changed. */
        (void)naos_check(amark_dialect_validate(&d.dialect) == first,
                         b->name, __FILE__, __LINE__);
        (void)naos_check(memcmp(&copy, &d, sizeof(d)) == 0, b->name,
                         __FILE__, __LINE__);
    }
}

/* ---------------------------------------------------------------------- */
/* Rule 1: the version (3.2) */

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

NAOS_TEST(rule3_same_names_are_valid)
{
    VALID_DEF(d);
    d.ext2.name = "first";
    NAOS_CHECK_TRUE(amark_dialect_validate(&d.dialect));
}

/* ---------------------------------------------------------------------- */
/* Rule 4: the kinds (3.4, 3.5, 3.7) */

NAOS_TEST(rule4_lowest_dialect_id_is_valid)
{
    VALID_DEF(d);
    NAOS_CHECK_UINT_EQ(d.kinds1[0].id, AMARK_KIND_DIALECT_MIN);
    NAOS_CHECK_TRUE(amark_dialect_validate(&d.dialect));
}

/* ---------------------------------------------------------------------- */
/* Rule 6: the inline rules (3.6, 3.7) */

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

NAOS_TEST(rule7_two_characters_in_one_extension_are_valid)
{
    VALID_DEF(d);
    d.delims2[1] = d.delims2[0];
    d.delims2[1].ch = '=';
    d.ext2.delimiter_count = 2;
    NAOS_CHECK_TRUE(amark_dialect_validate(&d.dialect));
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

NAOS_TEST(rule7_the_most_delimiters_are_valid)
{
    VALID_DEF(d);
    amark_test_def_set_delimiter_total(&d, AMARK_DIALECT_DELIMITER_MAX);
    NAOS_CHECK_TRUE(amark_dialect_validate(&d.dialect));

    /* A third extension without a delimiter kind keeps it valid. */
    d.ext3.delimiter_count = 0;
    d.dialect.extension_count = 3;
    NAOS_CHECK_TRUE(amark_dialect_validate(&d.dialect));
}

/* ---------------------------------------------------------------------- */
/* Rule 8: the state functions (3.9) */

NAOS_TEST(rule8_both_or_neither_are_valid)
{
    VALID_DEF(d);
    d.ext2.state_new = d.ext1.state_new;
    d.ext2.state_free = d.ext1.state_free;
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
    NAOS_ADD_TEST(every_breaker_is_invalid),
    NAOS_ADD_TEST(rule1_is_checked_before_the_extensions_are_read),
    NAOS_ADD_TEST(rule2_null_arrays_with_no_count_are_valid),
    NAOS_ADD_TEST(rule3_same_names_are_valid),
    NAOS_ADD_TEST(rule4_lowest_dialect_id_is_valid),
    NAOS_ADD_TEST(rule6_trigger_bounds_are_valid),
    NAOS_ADD_TEST(rule6_same_trigger_in_two_rules_is_valid),
    NAOS_ADD_TEST(rule7_every_allowed_character_is_valid),
    NAOS_ADD_TEST(rule7_two_characters_in_one_extension_are_valid),
    NAOS_ADD_TEST(rule7_every_length_set_is_valid),
    NAOS_ADD_TEST(rule7_the_most_delimiters_are_valid),
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
