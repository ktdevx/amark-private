/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of the delimiters (src/delimiter.h): the table of kinds (`*`
 * and `_`) and the lookup of a kind by its character, the empty stack after
 * init and reset, pushing runs (the elements linked from the bottom to the
 * top, the reuse of the place after a reset, allocator failures that leave
 * the stack unchanged), and whether a delimiter run can open or close emphasis
 * (CommonMark 0.31.2, section 6.2), checked as a table of the characters
 * before and after a run: the examples of left-flanking and right-flanking
 * runs, the rule of `_` inside a word, Unicode white space and punctuation
 * (U+00A0, U+3000, full-width punctuation, currency symbols), and U+FFFD and
 * U+000A as the caller passes them.
 */

#include "arena.h"
#include "array.h"
#include "delimiter.h"
#include "test_alloc.h"

#include <amark/amark.h>

#include <naos/naos.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static amark_test_alloc_t g_state;
static amark_allocator_t g_allocator;
static amark_arena_t g_scratch;
static amark_delimiters_t g_delims;

NAOS_SETUP(counting)
{
    amark_test_alloc_init(&g_state);
    g_allocator = amark_test_allocator(&g_state);
    amark_arena_init(&g_scratch, &g_allocator);
    amark_delimiters_init(&g_delims, &g_scratch);
}

NAOS_TEARDOWN(counting)
{
    amark_arena_deinit(&g_scratch);
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
    NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
}

/* ---- the kinds ---- */

NAOS_TEST(init_allocates_nothing_and_leaves_the_stack_empty)
{
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 0);
    NAOS_CHECK_UINT_EQ(g_delims.top, 0);
    NAOS_CHECK_UINT_EQ(amark_array_len(&g_delims.stack), 0);
}

NAOS_TEST(init_makes_the_two_kinds_of_commonmark)
{
    NAOS_REQUIRE_UINT_EQ(g_delims.kind_count, 2);
    NAOS_CHECK_UINT_EQ(g_delims.kinds[0].ch, '*');
    NAOS_CHECK_FALSE(g_delims.kinds[0].intraword_limited);
    NAOS_CHECK_UINT_EQ(g_delims.kinds[0].single, AMARK_KIND_EMPHASIS);
    NAOS_CHECK_UINT_EQ(g_delims.kinds[0].pair, AMARK_KIND_STRONG);
    NAOS_CHECK_UINT_EQ(g_delims.kinds[1].ch, '_');
    NAOS_CHECK_TRUE(g_delims.kinds[1].intraword_limited);
    NAOS_CHECK_UINT_EQ(g_delims.kinds[1].single, AMARK_KIND_EMPHASIS);
    NAOS_CHECK_UINT_EQ(g_delims.kinds[1].pair, AMARK_KIND_STRONG);
}

NAOS_TEST(kind_of_finds_the_kind_of_a_character)
{
    NAOS_CHECK_UINT_EQ(amark_delimiters_kind_of(&g_delims, '*'), 0);
    NAOS_CHECK_UINT_EQ(amark_delimiters_kind_of(&g_delims, '_'), 1);
}

NAOS_TEST(kind_of_returns_kind_count_for_other_characters)
{
    static const unsigned char others[] = {
        'a', '~', '[', '`', ' ', '\n', 0x00, 0x80, 0xFF
    };
    size_t i;

    NAOS_REQUIRE_UINT_EQ(g_delims.kind_count, 2);
    for (i = 0; i < sizeof(others); i++) {
        NAOS_CHECK_UINT_EQ(amark_delimiters_kind_of(&g_delims, others[i]),
                           2);
    }
}

NAOS_TEST(kind_of_finds_a_kind_added_to_the_table)
{
    /* The way dialect-extension adds a kind (17.2). */
    NAOS_REQUIRE_UINT_EQ(g_delims.kind_count, 2);
    g_delims.kinds[2].ch = '~';
    g_delims.kinds[2].intraword_limited = false;
    g_delims.kinds[2].single = AMARK_KIND_DIALECT_MIN;
    g_delims.kinds[2].pair = AMARK_KIND_DIALECT_MIN + 1;
    g_delims.kind_count = 3;
    NAOS_CHECK_UINT_EQ(amark_delimiters_kind_of(&g_delims, '~'), 2);
    NAOS_CHECK_UINT_EQ(amark_delimiters_kind_of(&g_delims, '*'), 0);
    NAOS_CHECK_UINT_EQ(amark_delimiters_kind_of(&g_delims, 'a'), 3);
}

NAOS_TEST(kind_of_ignores_the_unused_part_of_the_table)
{
    /* The unused elements are zero; ch 0 must not match them. */
    NAOS_REQUIRE_UINT_EQ(g_delims.kind_count, 2);
    NAOS_CHECK_UINT_EQ(g_delims.kinds[2].ch, 0);
    NAOS_CHECK_UINT_EQ(amark_delimiters_kind_of(&g_delims, 0), 2);
}

NAOS_TEST(reset_empties_the_stack_and_keeps_the_kinds)
{
    amark_delimiter_t elem = {0};

    NAOS_REQUIRE_INT_EQ(amark_array_push(&g_delims.stack, &elem),
                        AMARK_ERROR_NONE);
    g_delims.top = 1;
    amark_delimiters_reset(&g_delims);
    NAOS_CHECK_UINT_EQ(g_delims.top, 0);
    NAOS_CHECK_UINT_EQ(amark_array_len(&g_delims.stack), 0);
    NAOS_CHECK_UINT_EQ(g_delims.kind_count, 2);
    NAOS_CHECK_UINT_EQ(amark_delimiters_kind_of(&g_delims, '_'), 1);
}

/* ---- the stack ---- */

/* Returns the element named id (not 0). */
static const amark_delimiter_t *element(uint32_t id)
{
    return (const amark_delimiter_t *)amark_array_at(&g_delims.stack,
                                                     (size_t)id - 1);
}

/*
 * Checks that the stack holds n elements linked from 1 (the bottom) to n
 * (the top), the i-th (from 1) with the item i + 1, the kind i % 2, can_open
 * true for odd i, can_close true for even i, and the length i.
 */
static void check_stack(uint32_t n)
{
    uint32_t i;

    NAOS_REQUIRE_UINT_EQ(amark_array_len(&g_delims.stack), n);
    NAOS_CHECK_UINT_EQ(g_delims.top, n);
    for (i = 1; i <= n; i++) {
        const amark_delimiter_t *e = element(i);

        NAOS_TRACE_PUSH("element %lu", (unsigned long)i);
        NAOS_CHECK_UINT_EQ(e->item, i + 1);
        NAOS_CHECK_UINT_EQ(e->kind, i % 2);
        NAOS_CHECK_INT_EQ(e->can_open, i % 2 == 1);
        NAOS_CHECK_INT_EQ(e->can_close, i % 2 == 0);
        NAOS_CHECK_UINT_EQ(e->length, i);
        NAOS_CHECK_UINT_EQ(e->prev, i - 1);
        NAOS_CHECK_UINT_EQ(e->next, i == n ? 0 : i + 1);
        NAOS_TRACE_POP();
    }
}

/* Pushes the element i (from 1) as check_stack expects it. */
static amark_error_t push_nth(uint32_t i)
{
    return amark_delimiters_push(&g_delims, (amark_draft_id_t)(i + 1),
                                 i % 2, i % 2 == 1, i % 2 == 0, i);
}

NAOS_TEST(push_puts_one_element_on_the_empty_stack)
{
    NAOS_REQUIRE_INT_EQ(amark_delimiters_push(&g_delims, 7, 1, true, false,
                                              3),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_UINT_EQ(amark_array_len(&g_delims.stack), 1);
    NAOS_CHECK_UINT_EQ(g_delims.top, 1);
    NAOS_CHECK_UINT_EQ(element(1)->item, 7);
    NAOS_CHECK_UINT_EQ(element(1)->kind, 1);
    NAOS_CHECK_TRUE(element(1)->can_open);
    NAOS_CHECK_FALSE(element(1)->can_close);
    NAOS_CHECK_UINT_EQ(element(1)->length, 3);
    NAOS_CHECK_UINT_EQ(element(1)->prev, 0);
    NAOS_CHECK_UINT_EQ(element(1)->next, 0);
}

NAOS_TEST(push_links_the_elements_in_order)
{
    uint32_t i;

    /* 20 elements: past the first capacity of the array (8 and 16). */
    for (i = 1; i <= 20; i++) {
        NAOS_REQUIRE_INT_EQ(push_nth(i), AMARK_ERROR_NONE);
    }
    check_stack(20);
}

NAOS_TEST(push_keeps_both_open_and_close)
{
    NAOS_REQUIRE_INT_EQ(amark_delimiters_push(&g_delims, 2, 0, true, true,
                                              1000),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_TRUE(element(1)->can_open);
    NAOS_CHECK_TRUE(element(1)->can_close);
    NAOS_CHECK_UINT_EQ(element(1)->length, 1000);
}

NAOS_TEST(push_after_reset_starts_a_new_stack_without_allocating)
{
    uint32_t i;
    size_t calls;

    for (i = 1; i <= 5; i++) {
        NAOS_REQUIRE_INT_EQ(push_nth(i), AMARK_ERROR_NONE);
    }
    amark_delimiters_reset(&g_delims);
    NAOS_CHECK_UINT_EQ(g_delims.top, 0);
    calls = g_state.malloc_calls;
    for (i = 1; i <= 5; i++) {
        NAOS_REQUIRE_INT_EQ(push_nth(i), AMARK_ERROR_NONE);
    }
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, calls);
    check_stack(5);
}

NAOS_TEST(push_failure_on_the_empty_stack_changes_nothing)
{
    /* The first push takes the arena's first block from the allocator. */
    g_state.fail_at = 1;
    NAOS_CHECK_INT_EQ(push_nth(1), AMARK_ERROR_NOMEM);
    g_state.fail_at = 0;
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 1);
    NAOS_CHECK_UINT_EQ(g_delims.top, 0);
    NAOS_CHECK_UINT_EQ(amark_array_len(&g_delims.stack), 0);
    NAOS_REQUIRE_INT_EQ(push_nth(1), AMARK_ERROR_NONE);
    check_stack(1);
}

NAOS_TEST(push_failure_when_the_stack_grows_changes_nothing)
{
    uint32_t i;
    size_t failures = 0;

    /*
     * Makes every push's first call to the allocator fail, then pushes again.
     * The array grows past the arena's first block within 4096 elements, so
     * pushes that need a new block fail at least twice (the first push and
     * a later growth).
     */
    for (i = 1; i <= 4096; i++) {
        amark_error_t err;

        g_state.fail_at = g_state.malloc_calls + 1;
        err = push_nth(i);
        g_state.fail_at = 0;
        if (err != AMARK_ERROR_NONE) {
            NAOS_TRACE_PUSH("push %lu", (unsigned long)i);
            NAOS_CHECK_INT_EQ(err, AMARK_ERROR_NOMEM);
            NAOS_CHECK_UINT_EQ(g_delims.top, i - 1);
            NAOS_REQUIRE_UINT_EQ(amark_array_len(&g_delims.stack), i - 1);
            if (i > 1) {
                NAOS_CHECK_UINT_EQ(element(i - 1)->next, 0);
            }
            NAOS_TRACE_POP();
            failures++;
            NAOS_REQUIRE_INT_EQ(push_nth(i), AMARK_ERROR_NONE);
        }
    }
    NAOS_CHECK_UINT_GE(failures, 2);
    check_stack(4096);
}

/* ---- can open, can close ---- */

#define LF 0x000Au
#define SP 0x0020u

typedef struct classify_case {
    const char *name;      /* the example, the run between the brackets */
    unsigned char ch;      /* '*' or '_' */
    uint32_t before;
    uint32_t after;
    bool can_open;
    bool can_close;
} classify_case_t;

static const classify_case_t classify_cases[] = {
    /* Section 6.2: left-flanking but not right-flanking. */
    {"[***]abc", '*', LF, 'a', true, false},
    {"[***]abc (_)", '_', LF, 'a', true, false},
    {"  [_]abc", '_', SP, 'a', true, false},
    {"  [*]abc", '*', SP, 'a', true, false},
    {"[**]\"abc\"", '*', LF, '"', true, false},
    {"[__]\"abc\"", '_', LF, '"', true, false},
    {" [_]\"abc\"", '_', SP, '"', true, false},
    /* Right-flanking but not left-flanking. */
    {" abc[***]", '*', 'c', LF, false, true},
    {" abc[_]", '_', 'c', LF, false, true},
    {"\"abc\"[**]", '*', '"', LF, false, true},
    {"\"abc\"[_]", '_', '"', LF, false, true},
    {"abc[*] x", '*', 'c', SP, false, true},
    /* Both left-flanking and right-flanking. */
    {" abc[***]def", '*', 'c', 'd', true, true},
    {"\"abc\"[_]\"def\"", '_', '"', '"', true, true},
    {"\"abc\"[*]\"def\"", '*', '"', '"', true, true},
    /* Neither. */
    {"abc [***] def", '*', SP, SP, false, false},
    {"a [_] b", '_', SP, SP, false, false},
    {"[*] at the end of a line", '*', LF, LF, false, false},
    {" [*]\\n", '*', SP, LF, false, false},
    /* `_` inside a word: neither opens nor closes; `*` does both. */
    {"foo[_]bar_", '_', 'o', 'b', false, false},
    {"foo[*]bar*", '*', 'o', 'b', true, true},
    {"5[_]6_78", '_', '5', '6', false, false},
    {"\xd0\xbf\xd1\x80\xd0\xb8[_]\xd1\x81", '_', 0x0438u, 0x0441u, false,
     false},
    /* `_` both flanking with punctuation on one side. */
    {"foo-[_](bar)_ (opens)", '_', '-', '(', true, true},
    {"(bar)[_]. (closes)", '_', ')', '.', true, true},
    {"foo[_](bar)", '_', 'o', '(', false, true},
    {"(bar)[_]foo", '_', ')', 'f', true, false},
    {"aa[_]\"bb\"_cc (example 361)", '_', 'a', '"', false, true},
    /* Punctuation (P and S): ASCII symbols. */
    {"a[*]$b", '*', 'a', '$', false, true},
    {"$[*]alpha (example 354)", '*', '$', 'a', true, false},
    {"[*]$*alpha (example 354)", '*', LF, '$', true, false},
    /* Unicode punctuation and symbols. */
    {"\xc2\xa3[*]bravo (U+00A3)", '*', 0x00A3u, 'b', true, false},
    {"\xe2\x82\xac[*]charlie (U+20AC)", '*', 0x20ACu, 'c', true, false},
    {"a[_]\xe2\x82\xac", '_', 'a', 0x20ACu, false, true},
    {"\xe3\x80\x82[_]a (U+3002)", '_', 0x3002u, 'a', true, false},
    {"a[_]\xe3\x80\x81 (U+3001)", '_', 'a', 0x3001u, false, true},
    {"\xef\xbc\x81[*]\xef\xbc\x88 (U+FF01, U+FF08)", '*', 0xFF01u, 0xFF08u,
     true, true},
    {"\xef\xbc\x88[_]\xef\xbc\x89 (U+FF08, U+FF09)", '_', 0xFF08u, 0xFF09u,
     true, true},
    {"\xe3\x80\x8c[*]a (U+300C)", '*', 0x300Cu, 'a', true, false},
    /* Unicode white space. */
    {"[*]U+00A0 a (example 353)", '*', LF, 0x00A0u, false, false},
    {"a U+00A0[*]", '*', 0x00A0u, LF, false, false},
    {"U+00A0[_]a", '_', 0x00A0u, 'a', true, false},
    {"a[_]U+00A0", '_', 'a', 0x00A0u, false, true},
    {"U+3000[*]a", '*', 0x3000u, 'a', true, false},
    {"a[*]U+3000", '*', 'a', 0x3000u, false, true},
    {"U+3000[*]U+3000", '*', 0x3000u, 0x3000u, false, false},
    {"tab[*]a", '*', 0x0009u, 'a', true, false},
    /* Letters outside ASCII are neither white space nor punctuation. */
    {"\xe3\x81\x82[*]\xe3\x81\x84", '*', 0x3042u, 0x3044u, true, true},
    {"\xe3\x81\x82[_]\xe3\x81\x84", '_', 0x3042u, 0x3044u, false, false},
    /* U+FFFD (an ill-formed sequence or U+0000) is punctuation (So). */
    {"U+FFFD[_]a", '_', 0xFFFDu, 'a', true, false},
    {"a[_]U+FFFD", '_', 'a', 0xFFFDu, false, true},
    {"U+FFFD[*]U+FFFD", '*', 0xFFFDu, 0xFFFDu, true, true},
    {"U+FFFD[_]U+FFFD", '_', 0xFFFDu, 0xFFFDu, true, true},
    {"U+FFFD[*] (end)", '*', 0xFFFDu, LF, false, true},
    {"[*]U+FFFD (start)", '*', LF, 0xFFFDu, true, false},
    /* U+000A (the start and the end of a line's content) is white space. */
    {"[_]a (start)", '_', LF, 'a', true, false},
    {"a[_] (end)", '_', 'a', LF, false, true},
    {"(start)[*]. ", '*', LF, '.', true, false},
    {".[*](end)", '*', '.', LF, false, true}
};

NAOS_TEST(classify_follows_section_6_2)
{
    size_t i;

    NAOS_REQUIRE_UINT_EQ(g_delims.kind_count, 2);
    for (i = 0; i < sizeof(classify_cases) / sizeof(classify_cases[0]);
         i++) {
        const classify_case_t *c = &classify_cases[i];
        size_t kind = amark_delimiters_kind_of(&g_delims, c->ch);
        bool can_open = !c->can_open;
        bool can_close = !c->can_close;

        NAOS_REQUIRE_UINT_LT(kind, g_delims.kind_count);
        amark_delimiter_classify(&g_delims.kinds[kind], c->before, c->after,
                                 &can_open, &can_close);
        NAOS_TRACE_PUSH("case %lu (%c, %s)", (unsigned long)i, c->ch,
                        c->name);
        NAOS_CHECK_INT_EQ(can_open, c->can_open);
        NAOS_CHECK_INT_EQ(can_close, c->can_close);
        NAOS_TRACE_POP();
    }
}

NAOS_TEST(classify_uses_only_the_limit_of_the_kind)
{
    /* A kind added with the limit behaves as `_`, without it as `*`. */
    amark_delimiter_kind_t limited = {'~', true, AMARK_KIND_DIALECT_MIN,
                                      AMARK_KIND_DIALECT_MIN + 1};
    amark_delimiter_kind_t free_kind = {'~', false, AMARK_KIND_DIALECT_MIN,
                                        AMARK_KIND_DIALECT_MIN + 1};
    bool can_open = true;
    bool can_close = true;

    amark_delimiter_classify(&limited, 'a', 'b', &can_open, &can_close);
    NAOS_CHECK_FALSE(can_open);
    NAOS_CHECK_FALSE(can_close);
    amark_delimiter_classify(&free_kind, 'a', 'b', &can_open, &can_close);
    NAOS_CHECK_TRUE(can_open);
    NAOS_CHECK_TRUE(can_close);
}

NAOS_SUITE(delimiter,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(init_allocates_nothing_and_leaves_the_stack_empty),
    NAOS_ADD_TEST(init_makes_the_two_kinds_of_commonmark),
    NAOS_ADD_TEST(kind_of_finds_the_kind_of_a_character),
    NAOS_ADD_TEST(kind_of_returns_kind_count_for_other_characters),
    NAOS_ADD_TEST(kind_of_finds_a_kind_added_to_the_table),
    NAOS_ADD_TEST(kind_of_ignores_the_unused_part_of_the_table),
    NAOS_ADD_TEST(reset_empties_the_stack_and_keeps_the_kinds),
    NAOS_ADD_TEST(push_puts_one_element_on_the_empty_stack),
    NAOS_ADD_TEST(push_links_the_elements_in_order),
    NAOS_ADD_TEST(push_keeps_both_open_and_close),
    NAOS_ADD_TEST(push_after_reset_starts_a_new_stack_without_allocating),
    NAOS_ADD_TEST(push_failure_on_the_empty_stack_changes_nothing),
    NAOS_ADD_TEST(push_failure_when_the_stack_grows_changes_nothing),
    NAOS_ADD_TEST(classify_follows_section_6_2),
    NAOS_ADD_TEST(classify_uses_only_the_limit_of_the_kind)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(delimiter)
)
