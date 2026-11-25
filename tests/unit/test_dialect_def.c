/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of the definition of a dialect: the rules of
 * amark_dialect_validate (Requirements 3.2 to 3.9, 3.13, 8.6 to 8.8), the
 * kind lookups, the per-parse run state (12.3 to 12.7) and the rejection
 * diagnostic (11.1 to 11.5).
 *
 * Each test builds a valid definition of two extensions in local structures
 * (amark_test_def_init of test_dialect_defs.h), checks that it passes, then
 * changes it and checks the result. The invalid definitions are the
 * breakers of test_dialect_defs.h, shared with the configuration tests;
 * each breaks one condition only. amark_dialect_validate takes no
 * allocator, so it cannot allocate (3.13).
 */

#include "dialect_internal.h"
#include "document.h"
#include "test_alloc.h"
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

/* ---------------------------------------------------------------------- */
/* The run state (12.3 to 12.7) */

/*
 * Fixture. NAOS_REQUIRE_* leaves a test with longjmp, so the run and the
 * document a test uses are kept in globals; teardown (which always runs)
 * ends a run that began, destroys the document, and checks that no block is
 * left and that every call received the context value it was given.
 *
 * The state functions write to g_log: +k when state_new of extension k
 * creates a state, -k when state_free receives the state of extension k
 * (the state is an int holding k, allocated with the allocator passed in),
 * and 0 when state_free receives NULL. g_fail_ext makes state_new of that
 * extension fail with g_fail_err.
 */
#define LOG_MAX 16
#define RUN_EXT_MAX 4

static amark_test_alloc_t g_state;
static amark_allocator_t g_allocator;
static amark_document_t *g_doc;
static amark_dialect_run_t g_run;
static int g_run_begun;
static int g_log[LOG_MAX];
static size_t g_log_len;
static int g_fail_ext;
static amark_error_t g_fail_err;
static const amark_allocator_t *g_seen_allocator;

static amark_dialect_extension_t g_exts[RUN_EXT_MAX];
static const amark_dialect_extension_t *g_ext_ptrs[RUN_EXT_MAX];
static amark_dialect_t g_dialect;

static void log_event(int event)
{
    if (g_log_len < LOG_MAX) {
        g_log[g_log_len] = event;
    }
    g_log_len++;
}

static amark_error_t state_new_k(const amark_allocator_t *allocator,
                                 void **state, int k)
{
    int *tag;

    g_seen_allocator = allocator;
    if (g_fail_ext == k) {
        return g_fail_err;
    }
    tag = (int *)allocator->malloc_fn(allocator->ctx, sizeof(int));
    if (tag == NULL) {
        return AMARK_ERROR_NOMEM;
    }
    *tag = k;
    *state = tag;
    log_event(k);
    return AMARK_ERROR_NONE;
}

static amark_error_t state_new_1(const amark_allocator_t *a, void **s)
{
    return state_new_k(a, s, 1);
}

static amark_error_t state_new_2(const amark_allocator_t *a, void **s)
{
    return state_new_k(a, s, 2);
}

static amark_error_t state_new_3(const amark_allocator_t *a, void **s)
{
    return state_new_k(a, s, 3);
}

/* Succeeds without writing *state. */
static amark_error_t state_new_unwritten(const amark_allocator_t *a, void **s)
{
    (void)a;
    (void)s;
    log_event(9);
    return AMARK_ERROR_NONE;
}

static void state_free_any(const amark_allocator_t *allocator, void *state)
{
    if (state == NULL) {
        log_event(0);
        return;
    }
    log_event(-*(int *)state);
    allocator->free_fn(allocator->ctx, state);
}

/* Sets g_dialect to count extensions named "ext1"... with no state. */
static void init_dialect(size_t count)
{
    static const char *const names[RUN_EXT_MAX] = {"ext1", "ext2", "ext3",
                                                   "ext4"};
    size_t i;

    memset(g_exts, 0, sizeof(g_exts));
    for (i = 0; i < RUN_EXT_MAX; i++) {
        g_exts[i].name = names[i];
        g_ext_ptrs[i] = &g_exts[i];
    }
    g_dialect.struct_size = sizeof(amark_dialect_t);
    g_dialect.abi = AMARK_DIALECT_ABI;
    g_dialect.extensions = g_ext_ptrs;
    g_dialect.extension_count = count;
}

static void set_state_fns(size_t index, amark_dialect_state_new_fn fn)
{
    g_exts[index].state_new = fn;
    g_exts[index].state_free = state_free_any;
}

/* "ab\n" is line 1 (offsets 0-2), "cd\n" line 2 (3-5), "ef" line 3 (6-7);
 * offset 8 (the length) is on line 3. */
static const char k_content[] = "ab\ncd\nef";
#define CONTENT_LENGTH (sizeof(k_content) - 1)

NAOS_SETUP(run)
{
    amark_test_alloc_init(&g_state);
    g_allocator = amark_test_allocator(&g_state);
    g_doc = NULL;
    memset(&g_run, 0, sizeof(g_run));
    g_run_begun = 0;
    g_log_len = 0;
    g_fail_ext = 0;
    g_fail_err = AMARK_ERROR_NOMEM;
    g_seen_allocator = NULL;
    init_dialect(3);
    NAOS_REQUIRE_INT_EQ(
        amark_document_create(&g_allocator,
                              (const unsigned char *)k_content,
                              CONTENT_LENGTH, 0, CONTENT_LENGTH, NULL, &g_doc),
        AMARK_ERROR_NONE);
}

NAOS_TEARDOWN(run)
{
    g_state.fail_at = 0;
    if (g_run_begun) {
        amark_dialect_run_end(&g_run);
    }
    amark_document_destroy(g_doc);
    g_doc = NULL;
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
    NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
}

static amark_error_t begin(void)
{
    amark_error_t err;

    NAOS_REQUIRE_TRUE(amark_dialect_validate(&g_dialect));
    err = amark_dialect_run_begin(&g_run, &g_dialect, &g_allocator, g_doc);
    g_run_begun = err == AMARK_ERROR_NONE;
    return err;
}

static void end(void)
{
    NAOS_REQUIRE_TRUE(g_run_begun);
    g_run_begun = 0;
    amark_dialect_run_end(&g_run);
}

static void check_log(const int *expected, size_t count)
{
    size_t i;

    NAOS_REQUIRE_UINT_EQ(g_log_len, count);
    for (i = 0; i < count; i++) {
        NAOS_CHECK_INT_EQ(g_log[i], expected[i]);
    }
}

NAOS_TEST(run_begin_creates_each_state_once_in_order)
{
    static const int created[] = {1, 2, 3};
    static const int freed[] = {1, 2, 3, -1, -2, -3};

    set_state_fns(0, state_new_1);
    set_state_fns(1, state_new_2);
    set_state_fns(2, state_new_3);
    NAOS_REQUIRE_INT_EQ(begin(), AMARK_ERROR_NONE);
    check_log(created, 3);

    NAOS_CHECK_PTR_EQ(g_run.dialect, &g_dialect);
    NAOS_CHECK_PTR_EQ(g_run.doc, g_doc);
    NAOS_CHECK_NULL(g_run.reply);
    NAOS_CHECK_UINT_EQ(g_run.states_made, 3);
    NAOS_REQUIRE_NOT_NULL(g_run.states);
    NAOS_REQUIRE_NOT_NULL(g_run.states[0]);
    NAOS_REQUIRE_NOT_NULL(g_run.states[1]);
    NAOS_REQUIRE_NOT_NULL(g_run.states[2]);
    NAOS_CHECK_INT_EQ(*(int *)g_run.states[0], 1);
    NAOS_CHECK_INT_EQ(*(int *)g_run.states[1], 2);
    NAOS_CHECK_INT_EQ(*(int *)g_run.states[2], 3);

    /* The parse's allocator is passed in (12.5). */
    NAOS_REQUIRE_NOT_NULL(g_seen_allocator);
    NAOS_CHECK_PTR_EQ(g_seen_allocator->ctx, &g_state);
    NAOS_CHECK_TRUE(g_seen_allocator->malloc_fn == g_allocator.malloc_fn);
    NAOS_CHECK_TRUE(g_seen_allocator->free_fn == g_allocator.free_fn);

    end();
    check_log(freed, 6);
}

NAOS_TEST(run_end_frees_everything_it_allocated)
{
    size_t live_doc;

    live_doc = g_state.live;
    set_state_fns(0, state_new_1);
    NAOS_REQUIRE_INT_EQ(begin(), AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_GT(g_state.live, live_doc);
    end();
    NAOS_CHECK_UINT_EQ(g_state.live, live_doc);
}

NAOS_TEST(run_without_state_functions_calls_none)
{
    NAOS_REQUIRE_INT_EQ(begin(), AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(g_log_len, 0);
    NAOS_CHECK_UINT_EQ(g_run.states_made, 3);
    NAOS_REQUIRE_NOT_NULL(g_run.states);
    NAOS_CHECK_NULL(g_run.states[0]);
    NAOS_CHECK_NULL(g_run.states[1]);
    NAOS_CHECK_NULL(g_run.states[2]);
    end();
    NAOS_CHECK_UINT_EQ(g_log_len, 0);
}

NAOS_TEST(run_mixes_extensions_with_and_without_state)
{
    static const int created[] = {1, 3};
    static const int freed[] = {1, 3, -1, -3};

    set_state_fns(0, state_new_1);
    set_state_fns(2, state_new_3);
    NAOS_REQUIRE_INT_EQ(begin(), AMARK_ERROR_NONE);
    check_log(created, 2);
    NAOS_CHECK_NULL(g_run.states[1]);
    end();
    check_log(freed, 4);
}

NAOS_TEST(run_state_left_unwritten_is_null_and_still_freed)
{
    static const int log[] = {1, 9, -1, 0};

    set_state_fns(0, state_new_1);
    set_state_fns(1, state_new_unwritten);
    NAOS_REQUIRE_INT_EQ(begin(), AMARK_ERROR_NONE);
    NAOS_CHECK_NULL(g_run.states[1]);
    end();
    check_log(log, 4);
}

NAOS_TEST(run_failure_at_the_second_extension_frees_the_first)
{
    static const int log[] = {1, -1};

    set_state_fns(0, state_new_1);
    set_state_fns(1, state_new_2);
    set_state_fns(2, state_new_3);
    g_fail_ext = 2;
    NAOS_CHECK_INT_EQ(begin(), AMARK_ERROR_NOMEM);
    check_log(log, 2);
}

NAOS_TEST(run_failure_with_any_status_is_nomem)
{
    static const amark_error_t statuses[] = {
        AMARK_ERROR_NOMEM, AMARK_ERROR_INVALID_INPUT,
        AMARK_ERROR_LIMIT_EXCEEDED, (amark_error_t)12345};
    static const int log[] = {1, -1};
    size_t i;

    set_state_fns(0, state_new_1);
    set_state_fns(1, state_new_2);
    g_fail_ext = 2;
    for (i = 0; i < sizeof(statuses) / sizeof(statuses[0]); i++) {
        g_log_len = 0;
        g_fail_err = statuses[i];
        NAOS_CHECK_INT_EQ(begin(), AMARK_ERROR_NOMEM);
        check_log(log, 2);
    }
}

NAOS_TEST(run_failure_skips_extensions_without_state)
{
    static const int log[] = {1, -1};

    set_state_fns(0, state_new_1);
    set_state_fns(2, state_new_3);
    g_fail_ext = 3;
    NAOS_CHECK_INT_EQ(begin(), AMARK_ERROR_NOMEM);
    check_log(log, 2);
}

NAOS_TEST(run_failure_at_the_first_extension_frees_nothing)
{
    set_state_fns(0, state_new_1);
    set_state_fns(1, state_new_2);
    g_fail_ext = 1;
    NAOS_CHECK_INT_EQ(begin(), AMARK_ERROR_NOMEM);
    NAOS_CHECK_UINT_EQ(g_log_len, 0);
}

NAOS_TEST(run_failure_of_the_table_calls_no_state_function)
{
    set_state_fns(0, state_new_1);
    g_state.fail_at = g_state.malloc_calls + 1;
    NAOS_CHECK_INT_EQ(begin(), AMARK_ERROR_NOMEM);
    NAOS_CHECK_UINT_EQ(g_log_len, 0);
}

NAOS_TEST(run_failure_of_a_state_allocation_frees_the_others)
{
    static const int log[] = {1, -1};

    /* The table, then the state of ext1, then the state of ext2 fails. */
    set_state_fns(0, state_new_1);
    set_state_fns(1, state_new_2);
    g_state.fail_at = g_state.malloc_calls + 3;
    NAOS_CHECK_INT_EQ(begin(), AMARK_ERROR_NOMEM);
    check_log(log, 2);
}

NAOS_TEST(runs_keep_separate_states)
{
    amark_dialect_run_t other;
    void *first;

    set_state_fns(0, state_new_1);
    NAOS_REQUIRE_INT_EQ(begin(), AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(
        amark_dialect_run_begin(&other, &g_dialect, &g_allocator, g_doc),
        AMARK_ERROR_NONE);
    first = g_run.states[0];
    NAOS_CHECK_TRUE(first != other.states[0]);
    amark_dialect_run_end(&other);
    NAOS_CHECK_PTR_EQ(g_run.states[0], first);
    end();
}

NAOS_SUITE(run,
    NAOS_SET_SETUP(run),
    NAOS_SET_TEARDOWN(run),
    NAOS_ADD_TEST(run_begin_creates_each_state_once_in_order),
    NAOS_ADD_TEST(run_end_frees_everything_it_allocated),
    NAOS_ADD_TEST(run_without_state_functions_calls_none),
    NAOS_ADD_TEST(run_mixes_extensions_with_and_without_state),
    NAOS_ADD_TEST(run_state_left_unwritten_is_null_and_still_freed),
    NAOS_ADD_TEST(run_failure_at_the_second_extension_frees_the_first),
    NAOS_ADD_TEST(run_failure_with_any_status_is_nomem),
    NAOS_ADD_TEST(run_failure_skips_extensions_without_state),
    NAOS_ADD_TEST(run_failure_at_the_first_extension_frees_nothing),
    NAOS_ADD_TEST(run_failure_of_the_table_calls_no_state_function),
    NAOS_ADD_TEST(run_failure_of_a_state_allocation_frees_the_others),
    NAOS_ADD_TEST(runs_keep_separate_states)
)

/* ---------------------------------------------------------------------- */
/* The rejection diagnostic (11.1 to 11.5) */

/* Checks diagnostic index of g_doc. */
static void check_diagnostic(size_t index, size_t offset, size_t line,
                             const char *message)
{
    amark_diagnostic_t diag;

    NAOS_REQUIRE_TRUE(amark_document_diagnostic(g_doc, index, &diag));
    NAOS_CHECK_UINT_EQ(diag.offset, offset);
    NAOS_CHECK_UINT_EQ(diag.line, line);
    NAOS_CHECK_INT_EQ(diag.severity, AMARK_SEVERITY_ERROR);
    NAOS_CHECK_STR_EQ(diag.message, message);
}

NAOS_TEST(reject_names_every_hook)
{
    static const struct {
        amark_dialect_hook_t hook;
        const char *message;
    } cases[] = {
        {AMARK_DIALECT_HOOK_BLOCK_START,
         "dialect \"ext1\": block start: unknown return value"},
        {AMARK_DIALECT_HOOK_BLOCK_CONTINUE,
         "dialect \"ext1\": block continue: unknown return value"},
        {AMARK_DIALECT_HOOK_BLOCK_CLOSE,
         "dialect \"ext1\": block close: unknown return value"},
        {AMARK_DIALECT_HOOK_INLINE,
         "dialect \"ext1\": inline: unknown return value"},
        {AMARK_DIALECT_HOOK_POSTPROCESS,
         "dialect \"ext1\": postprocess: unknown return value"}};
    size_t i;

    NAOS_REQUIRE_INT_EQ(begin(), AMARK_ERROR_NONE);
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        NAOS_CHECK_INT_EQ(
            amark_dialect_reject(&g_run, 0, cases[i].hook,
                                 AMARK_DIALECT_REASON_STATUS, 0),
            AMARK_ERROR_NONE);
    }
    NAOS_REQUIRE_UINT_EQ(amark_document_diagnostic_count(g_doc),
                         sizeof(cases) / sizeof(cases[0]));
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        check_diagnostic(i, 0, 1, cases[i].message);
    }
}

NAOS_TEST(reject_names_every_reason)
{
    static const struct {
        amark_dialect_reason_t reason;
        const char *message;
    } cases[] = {
        {AMARK_DIALECT_REASON_STATUS,
         "dialect \"ext2\": inline: unknown return value"},
        {AMARK_DIALECT_REASON_ORIGIN,
         "dialect \"ext2\": inline: range does not start at the position"},
        {AMARK_DIALECT_REASON_LINE,
         "dialect \"ext2\": inline: range leaves the line"},
        {AMARK_DIALECT_REASON_EMPTY, "dialect \"ext2\": inline: empty range"},
        {AMARK_DIALECT_REASON_BOUNDS,
         "dialect \"ext2\": inline: range leaves the content"},
        {AMARK_DIALECT_REASON_KIND,
         "dialect \"ext2\": inline: kind not registered by the extension"},
        {AMARK_DIALECT_REASON_NESTING,
         "dialect \"ext2\": inline: node not allowed at this place"},
        {AMARK_DIALECT_REASON_INDEX,
         "dialect \"ext2\": inline: parent is not an earlier block"},
        {AMARK_DIALECT_REASON_PARENT,
         "dialect \"ext2\": inline: range leaves the parent"},
        {AMARK_DIALECT_REASON_ORDER,
         "dialect \"ext2\": inline: siblings overlap or are out of order"},
        {AMARK_DIALECT_REASON_INLINE,
         "dialect \"ext2\": inline: inline content not allowed here"},
        {AMARK_DIALECT_REASON_PREFIX,
         "dialect \"ext2\": inline: range ends inside a line prefix"},
        {AMARK_DIALECT_REASON_PARAGRAPH,
         "dialect \"ext2\": inline: paragraph part does not cover whole "
         "lines"},
        {AMARK_DIALECT_REASON_NODE,
         "dialect \"ext2\": inline: not a node of this document"},
        {AMARK_DIALECT_REASON_PROTECTED,
         "dialect \"ext2\": inline: node cannot be changed"},
        {AMARK_DIALECT_REASON_TEXT,
         "dialect \"ext2\": inline: not a text node, or a bad split "
         "position"}};
    size_t i;

    NAOS_REQUIRE_INT_EQ(begin(), AMARK_ERROR_NONE);
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        NAOS_CHECK_INT_EQ(amark_dialect_reject(&g_run, 1,
                                               AMARK_DIALECT_HOOK_INLINE,
                                               cases[i].reason, 4),
                          AMARK_ERROR_NONE);
    }
    NAOS_REQUIRE_UINT_EQ(amark_document_diagnostic_count(g_doc),
                         sizeof(cases) / sizeof(cases[0]));
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        check_diagnostic(i, 4, 2, cases[i].message);
    }
}

NAOS_TEST(reject_keeps_the_position_line_and_order)
{
    NAOS_REQUIRE_INT_EQ(begin(), AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(
        amark_dialect_reject(&g_run, 2, AMARK_DIALECT_HOOK_BLOCK_CLOSE,
                             AMARK_DIALECT_REASON_ORDER, 7),
        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(
        amark_dialect_reject(&g_run, 0, AMARK_DIALECT_HOOK_BLOCK_START,
                             AMARK_DIALECT_REASON_EMPTY, 2),
        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(
        amark_dialect_reject(&g_run, 1, AMARK_DIALECT_HOOK_POSTPROCESS,
                             AMARK_DIALECT_REASON_NODE, CONTENT_LENGTH),
        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(
        amark_dialect_reject(&g_run, 1, AMARK_DIALECT_HOOK_BLOCK_CONTINUE,
                             AMARK_DIALECT_REASON_LINE, 3),
        AMARK_ERROR_NONE);
    NAOS_REQUIRE_UINT_EQ(amark_document_diagnostic_count(g_doc), 4);
    check_diagnostic(0, 7, 3,
                     "dialect \"ext3\": block close: siblings overlap or "
                     "are out of order");
    check_diagnostic(1, 2, 1, "dialect \"ext1\": block start: empty range");
    check_diagnostic(2, CONTENT_LENGTH, 3,
                     "dialect \"ext2\": postprocess: not a node of this "
                     "document");
    check_diagnostic(3, 3, 2,
                     "dialect \"ext2\": block continue: range leaves the "
                     "line");
}

NAOS_TEST(reject_cuts_a_long_name_to_64_bytes)
{
    char name64[65];
    char name65[66];
    char name200[201];
    char expected[160];

    memset(name64, 'a', 64);
    name64[64] = '\0';
    memset(name65, 'b', 65);
    name65[65] = '\0';
    memset(name200, 'c', 200);
    name200[200] = '\0';
    g_exts[0].name = name64;
    g_exts[1].name = name65;
    g_exts[2].name = name200;
    NAOS_REQUIRE_INT_EQ(begin(), AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(
        amark_dialect_reject(&g_run, 0, AMARK_DIALECT_HOOK_BLOCK_CONTINUE,
                             AMARK_DIALECT_REASON_PARAGRAPH, 0),
        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(
        amark_dialect_reject(&g_run, 1, AMARK_DIALECT_HOOK_BLOCK_CONTINUE,
                             AMARK_DIALECT_REASON_PARAGRAPH, 0),
        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(
        amark_dialect_reject(&g_run, 2, AMARK_DIALECT_HOOK_BLOCK_CONTINUE,
                             AMARK_DIALECT_REASON_PARAGRAPH, 0),
        AMARK_ERROR_NONE);

    /* The longest message: a 64-byte name, the longest hook and reason. */
    strcpy(expected, "dialect \"");
    strcat(expected, name64);
    strcat(expected, "\": block continue: paragraph part does not cover "
                     "whole lines");
    check_diagnostic(0, 0, 1, expected);

    memset(name65, 'b', 64);
    name65[64] = '\0';
    strcpy(expected, "dialect \"");
    strcat(expected, name65);
    strcat(expected, "\": block continue: paragraph part does not cover "
                     "whole lines");
    check_diagnostic(1, 0, 1, expected);

    memset(name200, 'c', 64);
    name200[64] = '\0';
    strcpy(expected, "dialect \"");
    strcat(expected, name200);
    strcat(expected, "\": block continue: paragraph part does not cover "
                     "whole lines");
    check_diagnostic(2, 0, 1, expected);
}

NAOS_TEST(reject_allocates_only_for_the_diagnostic)
{
    size_t calls;

    NAOS_REQUIRE_INT_EQ(begin(), AMARK_ERROR_NONE);
    calls = g_state.malloc_calls;
    NAOS_REQUIRE_INT_EQ(
        amark_dialect_reject(&g_run, 0, AMARK_DIALECT_HOOK_INLINE,
                             AMARK_DIALECT_REASON_KIND, 1),
        AMARK_ERROR_NONE);
    /* The document's arena and diagnostics array may grow; nothing else. */
    NAOS_CHECK_UINT_LE(g_state.malloc_calls - calls, 2);
}

NAOS_TEST(reject_returns_the_failure_to_add)
{
    size_t added = 0;
    amark_error_t err = AMARK_ERROR_NONE;

    NAOS_REQUIRE_INT_EQ(begin(), AMARK_ERROR_NONE);
    /*
     * The next allocation fails. The diagnostics fit in the memory the
     * document has until one needs more; that one fails and adds nothing.
     */
    g_state.fail_at = g_state.malloc_calls + 1;
    while (added < 100000) {
        err = amark_dialect_reject(&g_run, 0, AMARK_DIALECT_HOOK_INLINE,
                                   AMARK_DIALECT_REASON_KIND, 1);
        if (err != AMARK_ERROR_NONE) {
            break;
        }
        added++;
    }
    g_state.fail_at = 0;
    NAOS_CHECK_INT_EQ(err, AMARK_ERROR_NOMEM);
    NAOS_CHECK_UINT_EQ(amark_document_diagnostic_count(g_doc), added);

    /* Without the failure, the same call adds it. */
    NAOS_REQUIRE_INT_EQ(
        amark_dialect_reject(&g_run, 0, AMARK_DIALECT_HOOK_INLINE,
                             AMARK_DIALECT_REASON_KIND, 1),
        AMARK_ERROR_NONE);
    NAOS_REQUIRE_UINT_EQ(amark_document_diagnostic_count(g_doc), added + 1);
    check_diagnostic(added, 1, 1,
                     "dialect \"ext1\": inline: kind not registered by the "
                     "extension");
}

NAOS_SUITE(reject,
    NAOS_SET_SETUP(run),
    NAOS_SET_TEARDOWN(run),
    NAOS_ADD_TEST(reject_names_every_hook),
    NAOS_ADD_TEST(reject_names_every_reason),
    NAOS_ADD_TEST(reject_keeps_the_position_line_and_order),
    NAOS_ADD_TEST(reject_cuts_a_long_name_to_64_bytes),
    NAOS_ADD_TEST(reject_allocates_only_for_the_diagnostic),
    NAOS_ADD_TEST(reject_returns_the_failure_to_add)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(validate),
    NAOS_ADD_SUITE(find_kind),
    NAOS_ADD_SUITE(run),
    NAOS_ADD_SUITE(reject)
)
