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
 *
 * Then process emphasis (CommonMark 0.31.2, appendix), on items arranged by
 * hand as the inline parser arranges them: the examples of section 6.4
 * without other inline constructs (rules 9 and 10 and rules 11 to 17
 * among them), the ranges when a run is used in part, the delimiters below
 * the bottom left untouched, deep nesting, the input of cmark issue 389,
 * and an allocator failure in the middle.
 *
 * Then the kinds added for a dialect with the rule of equal lengths
 * (dialect-extension 8.3, 8.4, 8.10): adding kinds up to the maximum, runs
 * of the same length matched as one item, runs of different lengths or of
 * lengths the kind does not use left as text, nesting with emphasis and
 * with each other, and the lower bounds that keep many unmatched closers
 * from walking over the same openers again.
 *
 * The inputs of the examples of section 6.4 are taken from the CommonMark
 * Spec, version 0.31.2, by John MacFarlane,
 * https://spec.commonmark.org/0.31.2/ (spec.txt), which is licensed under
 * the Creative Commons Attribution-ShareAlike 4.0 International License
 * (CC BY-SA 4.0, https://creativecommons.org/licenses/by-sa/4.0/). The
 * expected strings are the HTML that the specification gives for each
 * example, without the enclosing <p> and </p>. Each entry carries the number
 * of its example in the specification (counted from the start of spec.txt);
 * bytes outside ASCII are written as hexadecimal escapes of their UTF-8
 * encoding.
 */

#include "arena.h"
#include "array.h"
#include "delimiter.h"
#include "draft.h"
#include "test_alloc.h"

#include <amark/amark.h>

#include <naos/naos.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static amark_test_alloc_t g_state;
static amark_allocator_t g_allocator;
static amark_arena_t g_scratch;
static amark_delimiters_t g_delims;
static amark_draft_t g_draft;

NAOS_SETUP(counting)
{
    amark_test_alloc_init(&g_state);
    g_allocator = amark_test_allocator(&g_state);
    amark_arena_init(&g_scratch, &g_allocator);
    amark_delimiters_init(&g_delims, &g_scratch);
    amark_draft_init(&g_draft, &g_scratch);
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
                                      AMARK_KIND_DIALECT_MIN + 1,
                                      AMARK_DELIMITER_MATCH_EMPHASIS, 0};
    amark_delimiter_kind_t free_kind = {'~', false, AMARK_KIND_DIALECT_MIN,
                                        AMARK_KIND_DIALECT_MIN + 1,
                                        AMARK_DELIMITER_MATCH_EMPHASIS, 0};
    bool can_open = true;
    bool can_close = true;

    amark_delimiter_classify(&limited, 'a', 'b', &can_open, &can_close);
    NAOS_CHECK_FALSE(can_open);
    NAOS_CHECK_FALSE(can_close);
    amark_delimiter_classify(&free_kind, 'a', 'b', &can_open, &can_close);
    NAOS_CHECK_TRUE(can_open);
    NAOS_CHECK_TRUE(can_close);
}

/* ---- process emphasis ---- */

/*
 * The items are arranged by hand, as the inline parser will arrange them:
 * every run of one delimiter character becomes a text item of its own and
 * is pushed with the result of amark_delimiter_classify; every stretch of
 * other bytes becomes one text item. Then amark_delimiters_process runs and
 * the draft is written out as HTML (texts as their bytes, `"` as &quot;),
 * to compare with the HTML of the specification. Only inputs without other
 * inline constructs are used, so the arrangement is the parser's.
 */
#define BUF_SIZE 65536

static const char *g_src;
static size_t g_src_len;
static char g_html[BUF_SIZE];
static size_t g_html_len;

/* Appends text to g_html; what does not fit is dropped. */
static void put(const char *text)
{
    size_t len = strlen(text);

    if (g_html_len + len < BUF_SIZE) {
        memcpy(g_html + g_html_len, text, len + 1);
        g_html_len += len;
    }
}

/*
 * Returns the code point of the UTF-8 sequence that starts at src[i]; the
 * inputs here are well-formed.
 */
static uint32_t code_point_at(size_t i)
{
    const unsigned char *s = (const unsigned char *)g_src;

    if (s[i] < 0x80) {
        return s[i];
    }
    if (s[i] < 0xE0) {
        return ((uint32_t)(s[i] & 0x1F) << 6) | (s[i + 1] & 0x3F);
    }
    if (s[i] < 0xF0) {
        return ((uint32_t)(s[i] & 0x0F) << 12) |
               ((uint32_t)(s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F);
    }
    return ((uint32_t)(s[i] & 0x07) << 18) |
           ((uint32_t)(s[i + 1] & 0x3F) << 12) |
           ((uint32_t)(s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F);
}

/* Returns the code point that ends right before src[i], or U+000A at 0. */
static uint32_t code_point_before(size_t i)
{
    const unsigned char *s = (const unsigned char *)g_src;

    if (i == 0) {
        return LF;
    }
    i--;
    while (i > 0 && (s[i] & 0xC0) == 0x80) {
        i--;
    }
    return code_point_at(i);
}

/* Empties the draft and the stack and arranges the items of src. */
static void arrange(const char *src)
{
    size_t i = 0;

    g_src = src;
    g_src_len = strlen(src);
    amark_delimiters_reset(&g_delims);
    NAOS_REQUIRE_INT_EQ(amark_draft_reset(&g_draft), AMARK_ERROR_NONE);
    while (i < g_src_len) {
        size_t kind = amark_delimiters_kind_of(&g_delims,
                                               (unsigned char)src[i]);
        size_t start = i;
        amark_draft_id_t item;

        if (kind < g_delims.kind_count) {
            bool can_open;
            bool can_close;

            while (i < g_src_len && src[i] == src[start]) {
                i++;
            }
            amark_delimiter_classify(&g_delims.kinds[kind],
                                     code_point_before(start),
                                     i < g_src_len ? code_point_at(i) : LF,
                                     &can_open, &can_close);
            NAOS_REQUIRE_INT_EQ(amark_draft_append(&g_draft,
                                                   AMARK_KIND_TEXT, start,
                                                   i, &item),
                                AMARK_ERROR_NONE);
            if (can_open || can_close) {
                NAOS_REQUIRE_INT_EQ(amark_delimiters_push(&g_delims, item,
                                                          kind, can_open,
                                                          can_close,
                                                          i - start),
                                    AMARK_ERROR_NONE);
            }
        } else {
            while (i < g_src_len &&
                   amark_delimiters_kind_of(&g_delims,
                                            (unsigned char)src[i]) ==
                       g_delims.kind_count) {
                i++;
            }
            NAOS_REQUIRE_INT_EQ(amark_draft_append(&g_draft,
                                                   AMARK_KIND_TEXT, start,
                                                   i, &item),
                                AMARK_ERROR_NONE);
        }
    }
}

/*
 * Returns the number of delimiters on each side of item: 2 for a strong, 1
 * for an emphasis, and for an item of a kind added with the rule of equal
 * lengths, the length of the run of its first character.
 */
static size_t delimiters_of(const amark_draft_item_t *item)
{
    size_t n = 0;

    if (item->kind == AMARK_KIND_STRONG) {
        return 2;
    }
    if (item->kind == AMARK_KIND_EMPHASIS) {
        return 1;
    }
    while (item->start + n < item->end &&
           g_src[item->start + n] == g_src[item->start]) {
        n++;
    }
    return n;
}

/*
 * Writes the opening (or, if closing, the closing) tag of item: <em>,
 * <strong>, or <dN> for the kind AMARK_KIND_DIALECT_MIN + N.
 */
static void put_tag(const amark_draft_item_t *item, bool closing)
{
    char tag[16];

    if (item->kind == AMARK_KIND_STRONG) {
        put(closing ? "</strong>" : "<strong>");
    } else if (item->kind == AMARK_KIND_EMPHASIS) {
        put(closing ? "</em>" : "<em>");
    } else {
        NAOS_REQUIRE_UINT_GE(item->kind, AMARK_KIND_DIALECT_MIN);
        NAOS_REQUIRE_UINT_LT(item->kind, AMARK_KIND_DIALECT_MIN + 10);
        tag[0] = '<';
        tag[1] = '/';
        tag[2] = 'd';
        tag[3] = (char)('0' + (item->kind - AMARK_KIND_DIALECT_MIN));
        tag[4] = '>';
        tag[5] = '\0';
        if (closing) {
            put(tag);
        } else {
            tag[1] = '<';
            put(tag + 1);
        }
    }
}

/* Writes the closing tag of the emphasis item and checks its end. */
static void close_item(const amark_draft_item_t *item, size_t *expected)
{
    NAOS_CHECK_UINT_EQ(*expected + delimiters_of(item), item->end);
    put_tag(item, true);
    *expected = item->end;
}

/*
 * Writes the draft to g_html and checks its ranges, without recursion: no
 * item is empty, the children of the root cover [0, length of src) one
 * after another, and the children of an emphasis cover its range without
 * the delimiters at both ends (7.7, 3.6). Also checks the parent links and
 * returns the depth of the deepest item (the root's children are at 1).
 */
static size_t render(void)
{
    amark_draft_id_t parent = AMARK_DRAFT_ROOT;
    amark_draft_id_t id = amark_draft_get(&g_draft, parent)->first_child;
    size_t expected = 0; /* where the next item must start */
    size_t depth = 1;
    size_t max_depth = 0;

    g_html[0] = '\0';
    g_html_len = 0;
    while (id != AMARK_DRAFT_NONE) {
        const amark_draft_item_t *item = amark_draft_get(&g_draft, id);

        NAOS_TRACE_PUSH("item %lu [%lu, %lu)", (unsigned long)id,
                        (unsigned long)item->start,
                        (unsigned long)item->end);
        NAOS_CHECK_UINT_LT(item->start, item->end);
        NAOS_CHECK_UINT_EQ(item->start, expected);
        NAOS_CHECK_UINT_EQ(item->parent, parent);
        NAOS_TRACE_POP();
        if (depth > max_depth) {
            max_depth = depth;
        }
        if (item->kind == AMARK_KIND_TEXT) {
            size_t i;

            NAOS_CHECK_UINT_EQ(item->first_child, AMARK_DRAFT_NONE);
            for (i = item->start; i < item->end; i++) {
                char byte[2];

                byte[0] = g_src[i];
                byte[1] = '\0';
                put(g_src[i] == '"' ? "&quot;" : byte);
            }
            expected = item->end;
        } else {
            put_tag(item, false);
            NAOS_CHECK_INT_EQ(g_src[item->start], g_src[item->end - 1]);
            expected = item->start + delimiters_of(item);
            if (item->first_child != AMARK_DRAFT_NONE) {
                parent = id;
                id = item->first_child;
                depth++;
                continue;
            }
            close_item(item, &expected);
        }
        /* Go to the next item, closing the emphases left on the way up. */
        while (id != AMARK_DRAFT_NONE &&
               amark_draft_get(&g_draft, id)->next == AMARK_DRAFT_NONE) {
            if (parent == AMARK_DRAFT_ROOT) {
                id = AMARK_DRAFT_NONE;
            } else {
                id = parent;
                parent = amark_draft_get(&g_draft, id)->parent;
                depth--;
                close_item(amark_draft_get(&g_draft, id), &expected);
            }
        }
        if (id != AMARK_DRAFT_NONE) {
            id = amark_draft_get(&g_draft, id)->next;
        }
    }
    NAOS_CHECK_UINT_EQ(expected, g_src_len);
    return max_depth;
}

/* Arranges src, processes the whole stack, and writes the draft out. */
static size_t process_all(const char *src)
{
    arrange(src);
    NAOS_REQUIRE_INT_EQ(amark_delimiters_process(&g_delims, &g_draft, 0),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(g_delims.top, 0);
    return render();
}

/*
 * The examples of section 6.4 (Emphasis and strong emphasis) of
 * CommonMark 0.31.2 whose input has no other inline construct and is one
 * paragraph, with the HTML of the specification inside <p> and </p>
 * (taken from spec.txt; see the top of this file). Among
 * them: rules 9 and 10 (examples 411, 412, 415 to 417, 442 to 447), and
 * rules 11 to 17 (examples 436 to 472).
 */
typedef struct emphasis_example {
    int number;
    const char *markdown;
    const char *html;
} emphasis_example_t;

#define PRISTANYAM "\xd0\xbf\xd1\x80\xd0\xb8\xd1\x81\xd1\x82" \
                   "\xd0\xb0\xd0\xbd\xd1\x8f\xd0\xbc"
#define STREMYATSYA "\xd1\x81\xd1\x82\xd1\x80\xd0\xb5\xd0\xbc" \
                    "\xd1\x8f\xd1\x82\xd1\x81\xd1\x8f"

static const emphasis_example_t emphasis_examples[] = {
    {350, "*foo bar*",
     "<em>foo bar</em>"},
    {351, "a * foo bar*",
     "a * foo bar*"},
    {352, "a*\"foo\"*",
     "a*&quot;foo&quot;*"},
    {353, "*\xc2\xa0" "a\xc2\xa0*",
     "*\xc2\xa0" "a\xc2\xa0*"},
    {355, "foo*bar*",
     "foo<em>bar</em>"},
    {356, "5*6*78",
     "5<em>6</em>78"},
    {357, "_foo bar_",
     "<em>foo bar</em>"},
    {358, "_ foo bar_",
     "_ foo bar_"},
    {359, "a_\"foo\"_",
     "a_&quot;foo&quot;_"},
    {360, "foo_bar_",
     "foo_bar_"},
    {361, "5_6_78",
     "5_6_78"},
    {362, PRISTANYAM "_" STREMYATSYA "_",
     PRISTANYAM "_" STREMYATSYA "_"},
    {363, "aa_\"bb\"_cc",
     "aa_&quot;bb&quot;_cc"},
    {364, "foo-_(bar)_",
     "foo-<em>(bar)</em>"},
    {365, "_foo*",
     "_foo*"},
    {366, "*foo bar *",
     "*foo bar *"},
    {367, "*foo bar\n*",
     "*foo bar\n*"},
    {368, "*(*foo)",
     "*(*foo)"},
    {369, "*(*foo*)*",
     "<em>(<em>foo</em>)</em>"},
    {370, "*foo*bar",
     "<em>foo</em>bar"},
    {371, "_foo bar _",
     "_foo bar _"},
    {372, "_(_foo)",
     "_(_foo)"},
    {373, "_(_foo_)_",
     "<em>(<em>foo</em>)</em>"},
    {374, "_foo_bar",
     "_foo_bar"},
    {375, "_" PRISTANYAM "_" STREMYATSYA,
     "_" PRISTANYAM "_" STREMYATSYA},
    {376, "_foo_bar_baz_",
     "<em>foo_bar_baz</em>"},
    {377, "_(bar)_.",
     "<em>(bar)</em>."},
    {378, "**foo bar**",
     "<strong>foo bar</strong>"},
    {379, "** foo bar**",
     "** foo bar**"},
    {380, "a**\"foo\"**",
     "a**&quot;foo&quot;**"},
    {381, "foo**bar**",
     "foo<strong>bar</strong>"},
    {382, "__foo bar__",
     "<strong>foo bar</strong>"},
    {383, "__ foo bar__",
     "__ foo bar__"},
    {384, "__\nfoo bar__",
     "__\nfoo bar__"},
    {385, "a__\"foo\"__",
     "a__&quot;foo&quot;__"},
    {386, "foo__bar__",
     "foo__bar__"},
    {387, "5__6__78",
     "5__6__78"},
    {388, PRISTANYAM "__" STREMYATSYA "__",
     PRISTANYAM "__" STREMYATSYA "__"},
    {389, "__foo, __bar__, baz__",
     "<strong>foo, <strong>bar</strong>, baz</strong>"},
    {390, "foo-__(bar)__",
     "foo-<strong>(bar)</strong>"},
    {391, "**foo bar **",
     "**foo bar **"},
    {392, "**(**foo)",
     "**(**foo)"},
    {393, "*(**foo**)*",
     "<em>(<strong>foo</strong>)</em>"},
    {394, "**Gomphocarpus (*Gomphocarpus physocarpus*, syn.\n"
          "*Asclepias physocarpa*)**",
     "<strong>Gomphocarpus (<em>Gomphocarpus physocarpus</em>, syn.\n"
     "<em>Asclepias physocarpa</em>)</strong>"},
    {395, "**foo \"*bar*\" foo**",
     "<strong>foo &quot;<em>bar</em>&quot; foo</strong>"},
    {396, "**foo**bar",
     "<strong>foo</strong>bar"},
    {397, "__foo bar __",
     "__foo bar __"},
    {398, "__(__foo)",
     "__(__foo)"},
    {399, "_(__foo__)_",
     "<em>(<strong>foo</strong>)</em>"},
    {400, "__foo__bar",
     "__foo__bar"},
    {401, "__" PRISTANYAM "__" STREMYATSYA,
     "__" PRISTANYAM "__" STREMYATSYA},
    {402, "__foo__bar__baz__",
     "<strong>foo__bar__baz</strong>"},
    {403, "__(bar)__.",
     "<strong>(bar)</strong>."},
    {405, "*foo\nbar*",
     "<em>foo\nbar</em>"},
    {406, "_foo __bar__ baz_",
     "<em>foo <strong>bar</strong> baz</em>"},
    {407, "_foo _bar_ baz_",
     "<em>foo <em>bar</em> baz</em>"},
    {408, "__foo_ bar_",
     "<em><em>foo</em> bar</em>"},
    {409, "*foo *bar**",
     "<em>foo <em>bar</em></em>"},
    {410, "*foo **bar** baz*",
     "<em>foo <strong>bar</strong> baz</em>"},
    {411, "*foo**bar**baz*",
     "<em>foo<strong>bar</strong>baz</em>"},
    {412, "*foo**bar*",
     "<em>foo**bar</em>"},
    {413, "***foo** bar*",
     "<em><strong>foo</strong> bar</em>"},
    {414, "*foo **bar***",
     "<em>foo <strong>bar</strong></em>"},
    {415, "*foo**bar***",
     "<em>foo<strong>bar</strong></em>"},
    {416, "foo***bar***baz",
     "foo<em><strong>bar</strong></em>baz"},
    {417, "foo******bar*********baz",
     "foo<strong><strong><strong>bar</strong></strong></strong>***baz"},
    {418, "*foo **bar *baz* bim** bop*",
     "<em>foo <strong>bar <em>baz</em> bim</strong> bop</em>"},
    {420, "** is not an empty emphasis",
     "** is not an empty emphasis"},
    {421, "**** is not an empty strong emphasis",
     "**** is not an empty strong emphasis"},
    {423, "**foo\nbar**",
     "<strong>foo\nbar</strong>"},
    {424, "__foo _bar_ baz__",
     "<strong>foo <em>bar</em> baz</strong>"},
    {425, "__foo __bar__ baz__",
     "<strong>foo <strong>bar</strong> baz</strong>"},
    {426, "____foo__ bar__",
     "<strong><strong>foo</strong> bar</strong>"},
    {427, "**foo **bar****",
     "<strong>foo <strong>bar</strong></strong>"},
    {428, "**foo *bar* baz**",
     "<strong>foo <em>bar</em> baz</strong>"},
    {429, "**foo*bar*baz**",
     "<strong>foo<em>bar</em>baz</strong>"},
    {430, "***foo* bar**",
     "<strong><em>foo</em> bar</strong>"},
    {431, "**foo *bar***",
     "<strong>foo <em>bar</em></strong>"},
    {432, "**foo *bar **baz**\nbim* bop**",
     "<strong>foo <em>bar <strong>baz</strong>\nbim</em> bop</strong>"},
    {434, "__ is not an empty emphasis",
     "__ is not an empty emphasis"},
    {435, "____ is not an empty strong emphasis",
     "____ is not an empty strong emphasis"},
    {436, "foo ***",
     "foo ***"},
    {438, "foo *_*",
     "foo <em>_</em>"},
    {439, "foo *****",
     "foo *****"},
    {441, "foo **_**",
     "foo <strong>_</strong>"},
    {442, "**foo*",
     "*<em>foo</em>"},
    {443, "*foo**",
     "<em>foo</em>*"},
    {444, "***foo**",
     "*<strong>foo</strong>"},
    {445, "****foo*",
     "***<em>foo</em>"},
    {446, "**foo***",
     "<strong>foo</strong>*"},
    {447, "*foo****",
     "<em>foo</em>***"},
    {448, "foo ___",
     "foo ___"},
    {450, "foo _*_",
     "foo <em>*</em>"},
    {451, "foo _____",
     "foo _____"},
    {453, "foo __*__",
     "foo <strong>*</strong>"},
    {454, "__foo_",
     "_<em>foo</em>"},
    {455, "_foo__",
     "<em>foo</em>_"},
    {456, "___foo__",
     "_<strong>foo</strong>"},
    {457, "____foo_",
     "___<em>foo</em>"},
    {458, "__foo___",
     "<strong>foo</strong>_"},
    {459, "_foo____",
     "<em>foo</em>___"},
    {460, "**foo**",
     "<strong>foo</strong>"},
    {461, "*_foo_*",
     "<em><em>foo</em></em>"},
    {462, "__foo__",
     "<strong>foo</strong>"},
    {463, "_*foo*_",
     "<em><em>foo</em></em>"},
    {464, "****foo****",
     "<strong><strong>foo</strong></strong>"},
    {465, "____foo____",
     "<strong><strong>foo</strong></strong>"},
    {466, "******foo******",
     "<strong><strong><strong>foo</strong></strong></strong>"},
    {467, "***foo***",
     "<em><strong>foo</strong></em>"},
    {468, "_____foo_____",
     "<em><strong><strong>foo</strong></strong></em>"},
    {469, "*foo _bar* baz_",
     "<em>foo _bar</em> baz_"},
    {470, "*foo __bar *baz bim__ bam*",
     "<em>foo <strong>bar *baz bim</strong> bam</em>"},
    {471, "**foo **bar baz**",
     "**foo <strong>bar baz</strong>"},
    {472, "*foo *bar baz*",
     "*foo <em>bar baz</em>"}
};

NAOS_TEST(process_follows_the_examples_of_section_6_4)
{
    size_t i;

    for (i = 0; i < sizeof(emphasis_examples) / sizeof(emphasis_examples[0]);
         i++) {
        const emphasis_example_t *e = &emphasis_examples[i];

        NAOS_TRACE_PUSH("example %d", e->number);
        process_all(e->markdown);
        NAOS_CHECK_STR_EQ(g_html, e->html);
        NAOS_TRACE_POP();
    }
}

/* Returns the item id. */
static const amark_draft_item_t *item_of(amark_draft_id_t id)
{
    return amark_draft_get(&g_draft, id);
}

NAOS_TEST(process_on_an_empty_stack_does_nothing)
{
    arrange("foo");
    NAOS_CHECK_INT_EQ(amark_delimiters_process(&g_delims, &g_draft, 0),
                      AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(g_delims.top, 0);
    render();
    NAOS_CHECK_STR_EQ(g_html, "foo");
}

NAOS_TEST(process_shrinks_a_run_used_in_part)
{
    /* Example 444: items 2 `***`, 3 `foo`, 4 `**`; the strong is 5. */
    process_all("***foo**");
    NAOS_CHECK_STR_EQ(g_html, "*<strong>foo</strong>");
    NAOS_CHECK_UINT_EQ(item_of(2)->start, 0);
    NAOS_CHECK_UINT_EQ(item_of(2)->end, 1);
    NAOS_CHECK_UINT_EQ(item_of(2)->parent, AMARK_DRAFT_ROOT);
    /* The closer is used up: removed from the tree. */
    NAOS_CHECK_UINT_EQ(item_of(4)->parent, AMARK_DRAFT_NONE);
    NAOS_CHECK_UINT_EQ(item_of(4)->start, 8);
    NAOS_CHECK_UINT_EQ(item_of(4)->end, 8);
    NAOS_CHECK_UINT_EQ(item_of(5)->kind, AMARK_KIND_STRONG);
    NAOS_CHECK_UINT_EQ(item_of(5)->start, 1);
    NAOS_CHECK_UINT_EQ(item_of(5)->end, 8);
    NAOS_CHECK_UINT_EQ(item_of(5)->first_child, 3);
    NAOS_CHECK_UINT_EQ(item_of(5)->last_child, 3);
    NAOS_CHECK_UINT_EQ(item_of(AMARK_DRAFT_ROOT)->first_child, 2);
    NAOS_CHECK_UINT_EQ(item_of(AMARK_DRAFT_ROOT)->last_child, 5);
}

NAOS_TEST(process_uses_one_run_for_several_emphases)
{
    /*
     * Example 417: items 2 `foo`, 3 `******`, 4 `bar`, 5 `*********`,
     * 6 `baz`. Three strongs (7, 8, 9) from the inside out; three
     * delimiters of the closer are left as text.
     */
    process_all("foo******bar*********baz");
    NAOS_CHECK_STR_EQ(g_html, "foo<strong><strong><strong>bar</strong>"
                              "</strong></strong>***baz");
    NAOS_CHECK_UINT_EQ(item_of(7)->start, 7);
    NAOS_CHECK_UINT_EQ(item_of(7)->end, 14);
    NAOS_CHECK_UINT_EQ(item_of(8)->start, 5);
    NAOS_CHECK_UINT_EQ(item_of(8)->end, 16);
    NAOS_CHECK_UINT_EQ(item_of(9)->start, 3);
    NAOS_CHECK_UINT_EQ(item_of(9)->end, 18);
    NAOS_CHECK_UINT_EQ(item_of(3)->parent, AMARK_DRAFT_NONE);
    NAOS_CHECK_UINT_EQ(item_of(5)->start, 18);
    NAOS_CHECK_UINT_EQ(item_of(5)->end, 21);
    NAOS_CHECK_UINT_EQ(item_of(5)->parent, AMARK_DRAFT_ROOT);
}

NAOS_TEST(process_uses_one_delimiter_when_a_run_has_one_left)
{
    /* Example 467: `***foo***` is an emphasis around a strong. */
    process_all("***foo***");
    NAOS_CHECK_STR_EQ(g_html, "<em><strong>foo</strong></em>");
    NAOS_CHECK_UINT_EQ(item_of(5)->kind, AMARK_KIND_STRONG);
    NAOS_CHECK_UINT_EQ(item_of(5)->start, 1);
    NAOS_CHECK_UINT_EQ(item_of(5)->end, 8);
    NAOS_CHECK_UINT_EQ(item_of(6)->kind, AMARK_KIND_EMPHASIS);
    NAOS_CHECK_UINT_EQ(item_of(6)->start, 0);
    NAOS_CHECK_UINT_EQ(item_of(6)->end, 9);
}

NAOS_TEST(process_uses_the_original_length_for_the_rule_of_3)
{
    /*
     * `*foo***bar*`: the run `***` closes the first emphasis with one of its
     * delimiters and keeps two. With the last `*`, the lengths now left
     * (2 + 1) are a multiple of 3, but the rule looks at the lengths of the
     * runs (3 + 1), as cmark and commonmark.js do, so they match.
     */
    process_all("*foo***bar*");
    NAOS_CHECK_STR_EQ(g_html, "<em>foo</em>*<em>bar</em>");
    /*
     * Example 412 `*foo**bar*`: `**` finds no opener (1 + 2) and sets the
     * lower bound of its class (can open, length 2 modulo 3); the last `*`
     * differs in both (cannot open, length 1), so it still looks below
     * `**`. This tells a table split by class from one bound per kind, but
     * not which of the two splits matters; the next two tests do.
     */
    process_all("*foo**bar*");
    NAOS_CHECK_STR_EQ(g_html, "<em>foo**bar</em>");
}

NAOS_TEST(process_bounds_the_search_by_the_length_modulo_3)
{
    /*
     * `*.**.*.`: `**` (can open and close, length 2) finds no opener
     * (1 + 2) and sets the bound of (can open, 2). The second `*` (can open
     * and close, length 1) is of the class (can open, 1), so it still
     * reaches the first `*` (1 + 1).
     */
    process_all("*.**.*.");
    NAOS_CHECK_STR_EQ(g_html, "<em>.**.</em>.");
    process_all("_.__._*");
    NAOS_CHECK_STR_EQ(g_html, "<em>.__.</em>*");
}

NAOS_TEST(process_bounds_the_search_by_whether_the_closer_can_open)
{
    /*
     * `**_*_*`: the first `*` (can open and close, length 1) finds no
     * opener (2 + 1) and sets the bound of (can open, 1). The last `*`
     * (cannot open, length 1) is of another class, so it still reaches
     * `**` and uses one of its delimiters.
     */
    process_all("**_*_*");
    NAOS_CHECK_STR_EQ(g_html, "*<em><em>*</em></em>");
}

NAOS_TEST(process_uses_the_current_length_of_the_closer)
{
    /*
     * `**x **a***`: items 2 `**`, 3 `x `, 4 `**`, 5 `a`, 6 `***`. The
     * closer gives two delimiters to the strong (7) and has one left, so
     * the emphasis (8) with the first `**` uses one, although the run was
     * three long.
     */
    process_all("**x **a***");
    NAOS_CHECK_STR_EQ(g_html, "*<em>x <strong>a</strong></em>");
    NAOS_CHECK_UINT_EQ(item_of(2)->start, 0);
    NAOS_CHECK_UINT_EQ(item_of(2)->end, 1);
    NAOS_CHECK_UINT_EQ(item_of(2)->parent, AMARK_DRAFT_ROOT);
    NAOS_CHECK_UINT_EQ(item_of(4)->parent, AMARK_DRAFT_NONE);
    NAOS_CHECK_UINT_EQ(item_of(6)->parent, AMARK_DRAFT_NONE);
    NAOS_CHECK_UINT_EQ(item_of(7)->kind, AMARK_KIND_STRONG);
    NAOS_CHECK_UINT_EQ(item_of(7)->start, 4);
    NAOS_CHECK_UINT_EQ(item_of(7)->end, 9);
    NAOS_CHECK_UINT_EQ(item_of(7)->parent, 8);
    NAOS_CHECK_UINT_EQ(item_of(8)->kind, AMARK_KIND_EMPHASIS);
    NAOS_CHECK_UINT_EQ(item_of(8)->start, 1);
    NAOS_CHECK_UINT_EQ(item_of(8)->end, 10);
    NAOS_CHECK_UINT_EQ(item_of(8)->first_child, 3);
    NAOS_CHECK_UINT_EQ(item_of(8)->last_child, 7);
}

NAOS_TEST(process_uses_the_current_length_of_the_opener)
{
    /*
     * `**a* b**`: items 2 `**`, 3 `a`, 4 `*`, 5 ` b`, 6 `**`. The closer
     * `*` takes one delimiter of the opener `**` for the inner emphasis
     * (7), so the opener has one left and the emphasis (8) with the last
     * `**` uses one, although the run was two long; one `*` of the closer
     * is left as text.
     */
    process_all("**a* b**");
    NAOS_CHECK_STR_EQ(g_html, "<em><em>a</em> b</em>*");
    NAOS_CHECK_UINT_EQ(item_of(7)->kind, AMARK_KIND_EMPHASIS);
    NAOS_CHECK_UINT_EQ(item_of(7)->start, 1);
    NAOS_CHECK_UINT_EQ(item_of(7)->end, 4);
    NAOS_CHECK_UINT_EQ(item_of(7)->parent, 8);
    NAOS_CHECK_UINT_EQ(item_of(8)->kind, AMARK_KIND_EMPHASIS);
    NAOS_CHECK_UINT_EQ(item_of(8)->start, 0);
    NAOS_CHECK_UINT_EQ(item_of(8)->end, 7);
    NAOS_CHECK_UINT_EQ(item_of(8)->parent, AMARK_DRAFT_ROOT);
    NAOS_CHECK_UINT_EQ(item_of(8)->first_child, 7);
    NAOS_CHECK_UINT_EQ(item_of(8)->last_child, 5);
    /* The opener is used up; the closer keeps [7, 8) under the root. */
    NAOS_CHECK_UINT_EQ(item_of(2)->parent, AMARK_DRAFT_NONE);
    NAOS_CHECK_UINT_EQ(item_of(4)->parent, AMARK_DRAFT_NONE);
    NAOS_CHECK_UINT_EQ(item_of(6)->start, 7);
    NAOS_CHECK_UINT_EQ(item_of(6)->end, 8);
    NAOS_CHECK_UINT_EQ(item_of(6)->parent, AMARK_DRAFT_ROOT);
    NAOS_CHECK_UINT_EQ(item_of(AMARK_DRAFT_ROOT)->first_child, 8);
    NAOS_CHECK_UINT_EQ(item_of(AMARK_DRAFT_ROOT)->last_child, 6);
}

NAOS_TEST(process_leaves_the_delimiters_below_bottom)
{
    /* Items: 2 `*`, 3 `a `, 4 `*`, 5 `b`, 6 `*`. */
    arrange("*a *b*");
    NAOS_REQUIRE_UINT_EQ(g_delims.top, 3);
    NAOS_REQUIRE_INT_EQ(amark_delimiters_process(&g_delims, &g_draft, 3),
                        AMARK_ERROR_NONE);
    /* Only the element of item 2 is left, as it was. */
    NAOS_CHECK_UINT_EQ(g_delims.top, 1);
    NAOS_CHECK_UINT_EQ(element(1)->item, 2);
    NAOS_CHECK_UINT_EQ(element(1)->next, 0);
    NAOS_CHECK_UINT_EQ(item_of(2)->start, 0);
    NAOS_CHECK_UINT_EQ(item_of(2)->end, 1);
    render();
    NAOS_CHECK_STR_EQ(g_html, "*a <em>b</em>");
    NAOS_REQUIRE_INT_EQ(amark_delimiters_process(&g_delims, &g_draft, 0),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(g_delims.top, 0);
    render();
    NAOS_CHECK_STR_EQ(g_html, "*a <em>b</em>");
}

NAOS_TEST(process_does_not_match_a_closer_with_an_opener_below_bottom)
{
    /* As in `*a [b*](u)`: the closer at 4 must not reach the opener at 0
       (7.11). Items: 2 `*`, 3 `a b`, 4 `*`. */
    arrange("*a b*");
    NAOS_REQUIRE_INT_EQ(amark_delimiters_process(&g_delims, &g_draft, 2),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(g_delims.top, 1);
    NAOS_CHECK_UINT_EQ(element(1)->item, 2);
    NAOS_CHECK_UINT_EQ(element(1)->next, 0);
    render();
    NAOS_CHECK_STR_EQ(g_html, "*a b*");
    /* The closer is off the stack, so the opener is left alone. */
    NAOS_REQUIRE_INT_EQ(amark_delimiters_process(&g_delims, &g_draft, 0),
                        AMARK_ERROR_NONE);
    render();
    NAOS_CHECK_STR_EQ(g_html, "*a b*");
}

NAOS_TEST(process_with_bottom_past_every_item_changes_nothing)
{
    arrange("*a*");
    NAOS_REQUIRE_INT_EQ(amark_delimiters_process(&g_delims, &g_draft, 3),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(g_delims.top, 2);
    render();
    NAOS_CHECK_STR_EQ(g_html, "*a*");
}

/* Builds in buf (of BUF_SIZE bytes) n copies of a, then n copies of b. */
static void repeat(char *buf, const char *a, const char *b, size_t n)
{
    size_t i;
    size_t len = 0;

    for (i = 0; i < 2 * n; i++) {
        const char *part = i < n ? a : b;

        NAOS_REQUIRE_UINT_LT(len + strlen(part), BUF_SIZE);
        memcpy(buf + len, part, strlen(part));
        len += strlen(part);
    }
    buf[len] = '\0';
}

static char g_input[BUF_SIZE];

NAOS_TEST(process_nests_deeply)
{
    /* 5000 emphases inside one another: `*a *a ... b* b*` (7.9). */
    repeat(g_input, "*a ", "b* ", 5000);
    g_input[strlen(g_input) - 1] = '\0';
    NAOS_CHECK_UINT_EQ(process_all(g_input), 5001);
    /* 2000 strongs in emphases in turn, `**a *a **a ... b** b* b**`. */
    {
        size_t i;
        size_t len = 0;

        for (i = 0; i < 4000; i++) {
            const char *part = i < 2000 ? (i % 2 == 0 ? "**a " : "*a ")
                                        : (i % 2 == 0 ? "b* " : "b** ");

            memcpy(g_input + len, part, strlen(part));
            len += strlen(part);
        }
        g_input[len - 1] = '\0';
    }
    NAOS_CHECK_UINT_EQ(process_all(g_input), 2001);
    NAOS_CHECK_UINT_EQ(item_of(item_of(AMARK_DRAFT_ROOT)->first_child)->kind,
                       AMARK_KIND_STRONG);
    NAOS_CHECK_UINT_EQ(item_of(AMARK_DRAFT_ROOT)->first_child,
                       item_of(AMARK_DRAFT_ROOT)->last_child);
}

NAOS_TEST(process_matches_the_input_of_cmark_issue_389)
{
    /*
     * cmark issue 389: `*t ` n times, then `_t*_ ` n times. Each `_`
     * closer finds no opener; the lower bound it sets must hold after the
     * delimiters it was set from are taken off the stack, or every later
     * search walks over all the `*` openers again.
     */
    process_all("*t *t *t *t _t*_ _t*_ _t*_ _t*_");
    NAOS_CHECK_STR_EQ(g_html, "<em>t <em>t <em>t <em>t _t</em>_ _t</em>_ "
                              "_t</em>_ _t</em>_");
    repeat(g_input, "*t ", "_t*_ ", 6000);
    g_input[strlen(g_input) - 1] = '\0';
    NAOS_CHECK_UINT_EQ(process_all(g_input), 6001);
}

NAOS_TEST(process_failure_leaves_a_tree)
{
    /*
     * 2000 emphases `*a* `: arranging takes 8002 items, and the wraps grow
     * the array past 8192, which takes a new block from the allocator.
     */
    size_t calls;
    size_t i;
    size_t ems = 0;

    repeat(g_input, "*a* ", "", 2000);
    arrange(g_input);
    calls = g_state.malloc_calls;
    g_state.fail_at = calls + 1;
    NAOS_CHECK_INT_EQ(amark_delimiters_process(&g_delims, &g_draft, 0),
                      AMARK_ERROR_NOMEM);
    g_state.fail_at = 0;
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, calls + 1);
    /* The pairs matched before the failure are emphases; the rest text. */
    render();
    for (i = 0; g_html[i] != '\0'; i++) {
        if (strncmp(g_html + i, "<em>a</em>", 10) == 0) {
            ems++;
        }
    }
    NAOS_CHECK_UINT_GT(ems, 0);
    NAOS_CHECK_UINT_LT(ems, 2000);
}

/* ---- the rule of equal lengths (dialect-extension 8.3, 8.4, 8.10) ---- */

#define ALL_LENGTHS                                                    \
    (AMARK_DIALECT_LENGTH(1) | AMARK_DIALECT_LENGTH(2) |                \
     AMARK_DIALECT_LENGTH(3) | AMARK_DIALECT_LENGTH(4))

/*
 * Adds the kind of ch with the rule of equal lengths, the lengths, and the
 * kind AMARK_KIND_DIALECT_MIN + n, as the inline parser adds a delimiter of
 * a dialect.
 */
static void add_equal(unsigned char ch, unsigned lengths, unsigned n)
{
    amark_delimiter_kind_t kind;

    memset(&kind, 0, sizeof(kind));
    kind.ch = ch;
    kind.intraword_limited = false;
    kind.single = AMARK_KIND_DIALECT_MIN + n;
    kind.pair = AMARK_KIND_DIALECT_MIN + n;
    kind.match = AMARK_DELIMITER_MATCH_EQUAL;
    kind.lengths = (unsigned char)lengths;
    NAOS_REQUIRE_TRUE(amark_delimiters_add_kind(&g_delims, &kind));
}

NAOS_TEST(init_gives_the_kinds_of_commonmark_the_rule_of_emphasis)
{
    NAOS_CHECK_UINT_EQ(g_delims.kinds[0].match,
                       AMARK_DELIMITER_MATCH_EMPHASIS);
    NAOS_CHECK_UINT_EQ(g_delims.kinds[1].match,
                       AMARK_DELIMITER_MATCH_EMPHASIS);
}

NAOS_TEST(add_kind_appends_kinds_up_to_the_maximum)
{
    static const unsigned char chars[] = {'~', '=', '+', '^', '%', ':'};
    amark_delimiter_kind_t kind;
    size_t i;

    NAOS_CHECK_UINT_EQ(AMARK_DELIMITER_KIND_MAX,
                       2 + AMARK_DIALECT_DELIMITER_MAX);
    for (i = 0; i < sizeof(chars); i++) {
        add_equal(chars[i], AMARK_DIALECT_LENGTH(i % 4 + 1), (unsigned)i);
        NAOS_CHECK_UINT_EQ(g_delims.kind_count, 3 + i);
    }
    for (i = 0; i < sizeof(chars); i++) {
        const amark_delimiter_kind_t *k = &g_delims.kinds[2 + i];

        NAOS_TRACE_PUSH("kind %lu", (unsigned long)i);
        NAOS_CHECK_UINT_EQ(amark_delimiters_kind_of(&g_delims, chars[i]),
                           2 + i);
        NAOS_CHECK_UINT_EQ(k->ch, chars[i]);
        NAOS_CHECK_FALSE(k->intraword_limited);
        NAOS_CHECK_UINT_EQ(k->single, AMARK_KIND_DIALECT_MIN + i);
        NAOS_CHECK_UINT_EQ(k->pair, AMARK_KIND_DIALECT_MIN + i);
        NAOS_CHECK_UINT_EQ(k->match, AMARK_DELIMITER_MATCH_EQUAL);
        NAOS_CHECK_UINT_EQ(k->lengths, AMARK_DIALECT_LENGTH(i % 4 + 1));
        NAOS_TRACE_POP();
    }
    /* The table is full: nothing changes. */
    memset(&kind, 0, sizeof(kind));
    kind.ch = '!';
    kind.match = AMARK_DELIMITER_MATCH_EQUAL;
    kind.lengths = AMARK_DIALECT_LENGTH(1);
    NAOS_CHECK_FALSE(amark_delimiters_add_kind(&g_delims, &kind));
    NAOS_CHECK_UINT_EQ(g_delims.kind_count, AMARK_DELIMITER_KIND_MAX);
    NAOS_CHECK_UINT_EQ(amark_delimiters_kind_of(&g_delims, '!'),
                       AMARK_DELIMITER_KIND_MAX);
    /* The kinds of CommonMark are kept, and reset keeps the added ones. */
    NAOS_CHECK_UINT_EQ(amark_delimiters_kind_of(&g_delims, '*'), 0);
    NAOS_CHECK_UINT_EQ(amark_delimiters_kind_of(&g_delims, '_'), 1);
    amark_delimiters_reset(&g_delims);
    NAOS_CHECK_UINT_EQ(g_delims.kind_count, AMARK_DELIMITER_KIND_MAX);
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 0);
}

NAOS_TEST(process_equal_matches_runs_of_the_same_length)
{
    add_equal('~', AMARK_DIALECT_LENGTH(1) | AMARK_DIALECT_LENGTH(2), 0);
    /* Items: 2 `~`, 3 `a`, 4 `~`; the wrapping item is 5. */
    process_all("~a~");
    NAOS_CHECK_STR_EQ(g_html, "<d0>a</d0>");
    NAOS_CHECK_UINT_EQ(item_of(5)->kind, AMARK_KIND_DIALECT_MIN);
    NAOS_CHECK_UINT_EQ(item_of(5)->start, 0);
    NAOS_CHECK_UINT_EQ(item_of(5)->end, 3);
    NAOS_CHECK_UINT_EQ(item_of(5)->first_child, 3);
    NAOS_CHECK_UINT_EQ(item_of(5)->last_child, 3);
    NAOS_CHECK_UINT_EQ(item_of(2)->parent, AMARK_DRAFT_NONE);
    NAOS_CHECK_UINT_EQ(item_of(4)->parent, AMARK_DRAFT_NONE);
    NAOS_CHECK_UINT_EQ(item_of(AMARK_DRAFT_ROOT)->first_child, 5);
    NAOS_CHECK_UINT_EQ(item_of(AMARK_DRAFT_ROOT)->last_child, 5);
    /*
     * A run of two makes one item from the opener's start to the closer's
     * end, not two nested ones. Items: 2 `x`, 3 `~~`, 4 `a b`, 5 `~~`,
     * 6 `y`; the wrapping item is 7.
     */
    process_all("x~~a b~~y");
    NAOS_CHECK_STR_EQ(g_html, "x<d0>a b</d0>y");
    NAOS_CHECK_UINT_EQ(item_of(7)->kind, AMARK_KIND_DIALECT_MIN);
    NAOS_CHECK_UINT_EQ(item_of(7)->start, 1);
    NAOS_CHECK_UINT_EQ(item_of(7)->end, 8);
    NAOS_CHECK_UINT_EQ(item_of(7)->parent, AMARK_DRAFT_ROOT);
    NAOS_CHECK_UINT_EQ(item_of(7)->first_child, 4);
    NAOS_CHECK_UINT_EQ(item_of(7)->last_child, 4);
    NAOS_CHECK_UINT_EQ(item_of(3)->parent, AMARK_DRAFT_NONE);
    NAOS_CHECK_UINT_EQ(item_of(5)->parent, AMARK_DRAFT_NONE);
    NAOS_CHECK_UINT_EQ(item_of(2)->next, 7);
    NAOS_CHECK_UINT_EQ(item_of(7)->next, 6);
    /* The same kind nests in itself, and an empty content is matched. */
    process_all("~a ~~b~~ c~");
    NAOS_CHECK_STR_EQ(g_html, "<d0>a <d0>b</d0> c</d0>");
    process_all("~~~~");
    NAOS_CHECK_STR_EQ(g_html, "~~~~");
    process_all("~a~~b~");
    NAOS_CHECK_STR_EQ(g_html, "<d0>a~~b</d0>");
}

NAOS_TEST(process_equal_does_not_match_runs_of_different_lengths)
{
    add_equal('~', AMARK_DIALECT_LENGTH(1) | AMARK_DIALECT_LENGTH(2), 0);
    process_all("~a~~");
    NAOS_CHECK_STR_EQ(g_html, "~a~~");
    process_all("~~a~");
    NAOS_CHECK_STR_EQ(g_html, "~~a~");
    /* The closer `~` skips the opener `~~` and finds none; the closer
       `~~` then finds the opener `~~`, the nearest of its length. */
    process_all("~~a~ b~~");
    NAOS_CHECK_STR_EQ(g_html, "<d0>a~ b</d0>");
    process_all("~a ~~b~");
    NAOS_CHECK_STR_EQ(g_html, "<d0>a ~~b</d0>");
    /* Runs that can both open and close match as well. */
    process_all("a~~b~~c");
    NAOS_CHECK_STR_EQ(g_html, "a<d0>b</d0>c");
    /* Lengths outside the set are never matched, nor used in part. */
    process_all("~~~a~~~");
    NAOS_CHECK_STR_EQ(g_html, "~~~a~~~");
    process_all("~~~~~a~~~~~");
    NAOS_CHECK_STR_EQ(g_html, "~~~~~a~~~~~");
}

NAOS_TEST(process_equal_uses_only_the_lengths_of_the_kind)
{
    add_equal('~', AMARK_DIALECT_LENGTH(1) | AMARK_DIALECT_LENGTH(4), 0);
    add_equal('=', AMARK_DIALECT_LENGTH(3), 1);
    process_all("~a~ ~~b~~ ~~~~c~~~~ ==d== ===e=== =f=");
    NAOS_CHECK_STR_EQ(g_html,
                      "<d0>a</d0> ~~b~~ <d0>c</d0> ==d== <d1>e</d1> =f=");
}

NAOS_TEST(process_equal_nests_with_emphasis)
{
    add_equal('~', AMARK_DIALECT_LENGTH(1) | AMARK_DIALECT_LENGTH(2), 0);
    add_equal('=', AMARK_DIALECT_LENGTH(2), 1);
    process_all("*~a~*");
    NAOS_CHECK_STR_EQ(g_html, "<em><d0>a</d0></em>");
    process_all("~*a*~");
    NAOS_CHECK_STR_EQ(g_html, "<d0><em>a</em></d0>");
    process_all("**a ~~b~~ c**");
    NAOS_CHECK_STR_EQ(g_html, "<strong>a <d0>b</d0> c</strong>");
    process_all("~~a **b** c~~");
    NAOS_CHECK_STR_EQ(g_html, "<d0>a <strong>b</strong> c</d0>");
    /* An emphasis closed over an opener takes it off the stack (8.4), and
       the other way round, as between emphases (example 469). */
    process_all("*a ~b* c~");
    NAOS_CHECK_STR_EQ(g_html, "<em>a ~b</em> c~");
    process_all("~a *b~ c*");
    NAOS_CHECK_STR_EQ(g_html, "<d0>a *b</d0> c*");
    /* Between two kinds of equal lengths. */
    process_all("~a ==b~ c==");
    NAOS_CHECK_STR_EQ(g_html, "<d0>a ==b</d0> c==");
    process_all("==a ~b~ *c*==");
    NAOS_CHECK_STR_EQ(g_html, "<d1>a <d0>b</d0> <em>c</em></d1>");
    /* A run of three `*` around: the emphasis rules are unchanged. */
    process_all("***~a~***");
    NAOS_CHECK_STR_EQ(g_html, "<em><strong><d0>a</d0></strong></em>");
}

NAOS_TEST(process_equal_leaves_the_delimiters_below_bottom)
{
    add_equal('~', AMARK_DIALECT_LENGTH(1), 0);
    /* Items: 2 `~`, 3 `a `, 4 `~`, 5 `b`, 6 `~`. */
    arrange("~a ~b~");
    NAOS_REQUIRE_INT_EQ(amark_delimiters_process(&g_delims, &g_draft, 3),
                        AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(g_delims.top, 1);
    NAOS_CHECK_UINT_EQ(element(1)->item, 2);
    NAOS_CHECK_UINT_EQ(element(1)->next, 0);
    render();
    NAOS_CHECK_STR_EQ(g_html, "~a <d0>b</d0>");
}

static char g_big[1u << 20];

NAOS_TEST(process_equal_bounds_the_search_by_class)
{
    /*
     * n openers `~t ` of length 1, then n closers of lengths 2, 3 and 4 in
     * turn that cannot open, `t~~ `, `t~~~ `, `t~~~~ `. No closer finds an
     * opener; each sets the lower bound of its class (cannot open, its
     * length), so the later closers of the class do not walk over the
     * openers again. Without the bounds this takes about n * n steps.
     */
    static const char *const closers[] = {"t~~ ", "t~~~ ", "t~~~~ "};
    const size_t n = 60000;
    size_t len = 0;
    size_t i;

    add_equal('~', ALL_LENGTHS, 0);
    for (i = 0; i < 2 * n; i++) {
        const char *part = i < n ? "~t " : closers[i % 3];

        NAOS_REQUIRE_UINT_LT(len + strlen(part), sizeof(g_big));
        memcpy(g_big + len, part, strlen(part));
        len += strlen(part);
    }
    g_big[len] = '\0';
    NAOS_CHECK_UINT_EQ(process_all(g_big), 1);
    /* The same with closers that can open: `t~~t`, `t~~~t`, `t~~~~t`.
       Each stays on the stack and is matched by the next of its length. */
    process_all("~t t~~t t~~~t t~~~~t t~~t t~~~t t~~~~t");
    NAOS_CHECK_STR_EQ(g_html, "~t t<d0>t t~~~t t~~~~t t</d0>t t"
                              "~~~t t~~~~t");
}

NAOS_TEST(process_equal_bounds_the_search_past_closers_that_can_open)
{
    /*
     * n openers `~t `, then n runs `t~~t ` that can open and close: the
     * first finds no opener and sets the bound of its class, the second
     * matches it, the third finds no opener above the bound and sets it
     * again, the fourth matches the third, and so on; the openers `~t `
     * are walked over once. Then the closers `t~~ ` that cannot open walk
     * over them once more for their own class. Without the bounds this
     * takes about n * n steps.
     */
    const size_t n = 60000;
    size_t len = 0;
    size_t i;

    add_equal('~', ALL_LENGTHS, 0);
    for (i = 0; i < 3 * n; i++) {
        const char *part = i < n ? "~t " : i < 2 * n ? "t~~t " : "t~~ ";

        NAOS_REQUIRE_UINT_LT(len + strlen(part), sizeof(g_big));
        memcpy(g_big + len, part, strlen(part));
        len += strlen(part);
    }
    g_big[len] = '\0';
    NAOS_CHECK_UINT_EQ(process_all(g_big), 2);
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
    NAOS_ADD_TEST(classify_uses_only_the_limit_of_the_kind),
    NAOS_ADD_TEST(process_follows_the_examples_of_section_6_4),
    NAOS_ADD_TEST(process_on_an_empty_stack_does_nothing),
    NAOS_ADD_TEST(process_shrinks_a_run_used_in_part),
    NAOS_ADD_TEST(process_uses_one_run_for_several_emphases),
    NAOS_ADD_TEST(process_uses_one_delimiter_when_a_run_has_one_left),
    NAOS_ADD_TEST(process_uses_the_original_length_for_the_rule_of_3),
    NAOS_ADD_TEST(process_bounds_the_search_by_the_length_modulo_3),
    NAOS_ADD_TEST(process_bounds_the_search_by_whether_the_closer_can_open),
    NAOS_ADD_TEST(process_uses_the_current_length_of_the_closer),
    NAOS_ADD_TEST(process_uses_the_current_length_of_the_opener),
    NAOS_ADD_TEST(process_leaves_the_delimiters_below_bottom),
    NAOS_ADD_TEST(process_does_not_match_a_closer_with_an_opener_below_bottom),
    NAOS_ADD_TEST(process_with_bottom_past_every_item_changes_nothing),
    NAOS_ADD_TEST(process_nests_deeply),
    NAOS_ADD_TEST(process_matches_the_input_of_cmark_issue_389),
    NAOS_ADD_TEST(process_failure_leaves_a_tree),
    NAOS_ADD_TEST(init_gives_the_kinds_of_commonmark_the_rule_of_emphasis),
    NAOS_ADD_TEST(add_kind_appends_kinds_up_to_the_maximum),
    NAOS_ADD_TEST(process_equal_matches_runs_of_the_same_length),
    NAOS_ADD_TEST(process_equal_does_not_match_runs_of_different_lengths),
    NAOS_ADD_TEST(process_equal_uses_only_the_lengths_of_the_kind),
    NAOS_ADD_TEST(process_equal_nests_with_emphasis),
    NAOS_ADD_TEST(process_equal_leaves_the_delimiters_below_bottom),
    NAOS_ADD_TEST(process_equal_bounds_the_search_by_class),
    NAOS_ADD_TEST(process_equal_bounds_the_search_past_closers_that_can_open)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(delimiter)
)
