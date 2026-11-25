/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of the limits and failures of a parse with a dialect, and of a
 * parse without one.
 *
 * The allocation baseline: the number of calls to the allocator made by a
 * parse without a dialect, measured on fixed inputs before the dialect rules
 * were added to the parsers and written here as constants (Requirements 13.2
 * and 15.7). A parse without a dialect must keep making exactly as many
 * calls. Each input is small enough to fit with a wide margin in the first
 * block of every arena, so a small growth of a structure of the parsers does
 * not change the count.
 *
 * Dialects without effect: a parse with a dialect of 0 extensions, with a
 * dialect whose rules all say no, or with the test dialect on input that has
 * none of its syntax, gives the same tree as a parse without a dialect
 * (Requirements 13.3 and 15.6). An input with a node of every CommonMark kind
 * gives no diagnostic, and as many allocations with the dialect of 0
 * extensions as without a dialect.
 *
 * Repeated parses: the same content parsed twice with the test dialect gives
 * the same tree, attributes and diagnostics, the per-document state is not
 * carried from one parse to the next, and no parse writes to the content
 * (Requirements 11.6, 12.7, 12.10, 13.8, 13.9 and 15.9).
 *
 * Pathological inputs: dialect blocks nested 100,000 deep, blank lines after
 * them and long runs of an EQUAL delimiter parse in linear time without
 * recursion, and the rules are called no more often than the design bounds
 * (Requirements 13.4, 13.5, 13.6 and 15.10).
 */

#include "document.h"
#include "test_alloc.h"
#include "test_dialect.h"
#include "tree_check.h"
#include "tree_dump.h"

#include <amark/amark.h>

#include <naos/naos.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * Fixture. NAOS_REQUIRE_* leaves a test with longjmp, so the document a test
 * makes is kept in a static variable, and teardown destroys it and checks
 * that no block is left and that every call received its context value.
 */
static amark_test_alloc_t g_state;
static amark_config_t g_config;
static amark_document_t *g_doc;

NAOS_SETUP(counting)
{
    amark_test_alloc_init(&g_state);
    amark_config_init(&g_config);
    g_config.allocator = amark_test_allocator(&g_state);
    g_doc = NULL;
}

NAOS_TEARDOWN(counting)
{
    amark_document_destroy(g_doc);
    g_doc = NULL;
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
    NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
}

#define S(s) (s), (sizeof(s) - 1)

/* ---------------------------------------------------------------------- */
/* The allocation baseline of a parse without a dialect */

/*
 * The calls to the allocator made by a parse without a dialect, measured
 * before the dialect rules were added to the parsers. Do not change these
 * values to make a test pass: a different count means that a parse without a
 * dialect now allocates differently (Requirement 13.2).
 *
 * As measured: the document, the first block of the document arena and the
 * first block of the scratch arena of the block parser; with inline content,
 * also the first block of the scratch arena of the inline parser.
 */
#define BASELINE_EMPTY ((size_t)3)
#define BASELINE_PARAGRAPH ((size_t)4)
#define BASELINE_NESTED ((size_t)4)

/* One paragraph of one line. */
static const char k_paragraph[] = "Hello, world.\n";

/*
 * A list nested in a list item, emphasis, strong emphasis and a link: 14
 * nodes. With up to 16 nodes the node array of the document stays in the
 * first block of the document arena (measured: about 2.5 KB of its 4 KB
 * used; the scratch arenas of the block and inline parsers use less), so a
 * small growth of a structure does not move any arena to a second block.
 */
static const char k_nested[] = "- *one* [two](/url)\n"
                               "  - **three**\n";

#define DUMP_SIZE 512

/*
 * Parses content with dialect (NULL: none) into a fresh g_doc, requires
 * success, a tree that passes the check and no diagnostic, and returns the
 * number of calls made to the allocator.
 */
static size_t count_parse_with(const amark_dialect_t *dialect,
                               const char *content, size_t length)
{
    amark_tree_check_result_t check;

    amark_document_destroy(g_doc);
    g_doc = NULL;
    amark_test_alloc_init(&g_state);
    g_config.dialect = dialect;
    NAOS_REQUIRE_INT_EQ(amark_parse(content, length, &g_config, &g_doc),
                        AMARK_ERROR_NONE);
    g_config.dialect = NULL;
    NAOS_REQUIRE_NOT_NULL(g_doc);
    check = amark_tree_check(g_doc);
    NAOS_CHECK_INT_EQ(check.violation, AMARK_TREE_VIOLATION_NONE);
    NAOS_CHECK_UINT_EQ(amark_document_diagnostic_count(g_doc), 0);
    return g_state.malloc_calls;
}

/*
 * Parses content without a dialect, as count_parse_with does, and returns
 * the number of calls made to the allocator. Before that, parses it with
 * the dialect of 0 extensions and requires the same number of calls
 * (Requirements 13.2 and 15.7): such a dialect allocates nothing either.
 */
static size_t count_parse(const char *content, size_t length)
{
    size_t with_empty =
        count_parse_with(amark_test_dialect_empty(), content, length);
    size_t without = count_parse_with(NULL, content, length);

    NAOS_CHECK_UINT_EQ(with_empty, without);
    return without;
}

/* Requires the tree of g_doc to be written as want. */
static void check_dump(const char *want)
{
    char buffer[DUMP_SIZE];
    size_t length = amark_test_dump_tree(g_doc, buffer, sizeof buffer);

    NAOS_REQUIRE_UINT_LT(length, sizeof buffer);
    NAOS_CHECK_STR_EQ(buffer, want);
}

NAOS_TEST(empty_input_keeps_the_baseline)
{
    NAOS_CHECK_UINT_EQ(count_parse(S("")), BASELINE_EMPTY);
    check_dump("(doc)");
}

NAOS_TEST(one_paragraph_keeps_the_baseline)
{
    NAOS_CHECK_UINT_EQ(count_parse(S(k_paragraph)), BASELINE_PARAGRAPH);
    check_dump("(doc (p (text \"Hello, world.\")))");
}

NAOS_TEST(nested_lists_emphasis_and_links_keep_the_baseline)
{
    NAOS_CHECK_UINT_EQ(count_parse(S(k_nested)), BASELINE_NESTED);
    /* Guards the input: it must keep exercising what the name says. */
    check_dump("(doc (ul - tight (li (p (em (text \"one\")) (text \" \")"
               " (link inline dest=\"/url\" (text \"two\")))"
               " (ul - tight (li (p (strong (text \"three\"))))))))");
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), 14);
}

NAOS_SUITE(allocation_baseline,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(empty_input_keeps_the_baseline),
    NAOS_ADD_TEST(one_paragraph_keeps_the_baseline),
    NAOS_ADD_TEST(nested_lists_emphasis_and_links_keep_the_baseline)
)

/* ---------------------------------------------------------------------- */
/* Dialects without effect */

#define WIDE_DUMP_SIZE 2048

/*
 * Input with the markers of the test dialect's syntax (none of which the
 * dialects below act on), CommonMark blocks and inlines, and the trigger
 * characters of the dialect whose rules all say no.
 */
static const char k_markers[] =
    "@> box\n"
    "@>\n"
    "\n"
    "@note: a note\n"
    "  goes on\n"
    "\n"
    "@@@\n"
    "fenced\n"
    "@@@\n"
    "@line *one* line\n"
    "\n"
    "| a | b |\n"
    "| c | d |\n"
    "\n"
    "- [x] done $var$ ~one~ ~~two~~\n"
    "- [ ] open [link](/url) `code`\n"
    "\n"
    "> quote with **strong** text\n"
    "\n"
    "    indented code\n"
    "\n"
    "```c\n"
    "fenced code\n"
    "```\n"
    "# Heading\n"
    "<div>html</div>\n"
    "\n"
    "[ref]: /dest\n"
    "\n"
    "* * *\n";

/*
 * Input with a node of every CommonMark kind (check_every_core_kind
 * guards it) and none of the test dialect's syntax.
 */
static const char k_commonmark[] =
    "> quote\n"
    "\n"
    "- item\n"
    "\n"
    "1. ordered\n"
    "\n"
    "***\n"
    "# atx\n"
    "setext\n"
    "======\n"
    "\n"
    "    indented\n"
    "\n"
    "```\n"
    "fenced\n"
    "```\n"
    "<div>\n"
    "html\n"
    "</div>\n"
    "\n"
    "[ref]: /dest \"title\"\n"
    "\n"
    "*em* **strong** `code` [link](/u) ![image](/i) <http://a.b>\n"
    "<b>raw</b> \\* &amp; [ref] hard  \n"
    "break\n";

/* Requires g_doc to hold a node of every CommonMark kind. */
static void check_every_core_kind(void)
{
    static const amark_kind_t kinds[] = {
        AMARK_KIND_DOCUMENT,     AMARK_KIND_BLOCK_QUOTE,
        AMARK_KIND_LIST,         AMARK_KIND_LIST_ITEM,
        AMARK_KIND_THEMATIC_BREAK, AMARK_KIND_HEADING,
        AMARK_KIND_CODE_BLOCK,   AMARK_KIND_HTML_BLOCK,
        AMARK_KIND_PARAGRAPH,    AMARK_KIND_LINK_REFERENCE_DEFINITION,
        AMARK_KIND_TEXT,         AMARK_KIND_CODE_SPAN,
        AMARK_KIND_EMPHASIS,     AMARK_KIND_STRONG,
        AMARK_KIND_LINK,         AMARK_KIND_IMAGE,
        AMARK_KIND_AUTOLINK,     AMARK_KIND_RAW_HTML,
        AMARK_KIND_HARD_BREAK,   AMARK_KIND_SOFT_BREAK,
        AMARK_KIND_BACKSLASH_ESCAPE, AMARK_KIND_CHARACTER_REFERENCE};
    size_t count = amark_document_node_count(g_doc);
    size_t i;
    size_t id;
    size_t n;

    for (i = 0; i < sizeof kinds / sizeof kinds[0]; i++) {
        n = 0;
        for (id = 1; id <= count; id++) {
            if (amark_node_kind(g_doc, (amark_node_id_t)id) == kinds[i]) {
                n++;
            }
        }
        NAOS_TRACE_PUSH("kind %lu", (unsigned long)kinds[i]);
        NAOS_CHECK_UINT_GT(n, 0);
        NAOS_TRACE_POP();
    }
}

/*
 * Parses content with dialect (NULL: none) into a fresh g_doc, requires
 * success and a tree that passes the check, and writes its dump to buffer.
 */
static void parse_and_dump(const amark_dialect_t *dialect, const char *content,
                           size_t length, char *buffer, size_t size)
{
    amark_tree_check_result_t check;
    size_t dump_length;

    amark_document_destroy(g_doc);
    g_doc = NULL;
    g_config.dialect = dialect;
    NAOS_REQUIRE_INT_EQ(amark_parse(content, length, &g_config, &g_doc),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_NOT_NULL(g_doc);
    check = amark_tree_check(g_doc);
    NAOS_CHECK_INT_EQ(check.violation, AMARK_TREE_VIOLATION_NONE);
    NAOS_CHECK_UINT_EQ(amark_document_diagnostic_count(g_doc), 0);
    dump_length = amark_test_dump_tree(g_doc, buffer, size);
    NAOS_REQUIRE_UINT_LT(dump_length, size);
}

/* Requires the dump with dialect to equal the dump without a dialect. */
static void check_same_tree(const amark_dialect_t *dialect,
                            const char *content, size_t length)
{
    char without[WIDE_DUMP_SIZE];
    char with[WIDE_DUMP_SIZE];

    parse_and_dump(NULL, content, length, without, sizeof without);
    parse_and_dump(dialect, content, length, with, sizeof with);
    NAOS_CHECK_STR_EQ(with, without);
    g_config.dialect = NULL;
}

NAOS_TEST(the_test_dialects_pass_the_configuration_check)
{
    NAOS_REQUIRE_NOT_NULL(amark_test_dialect());
    NAOS_REQUIRE_NOT_NULL(amark_test_dialect_plain());
    NAOS_REQUIRE_NOT_NULL(amark_test_dialect_empty());
    NAOS_CHECK_UINT_EQ(amark_test_dialect()->extension_count, 1);
    NAOS_CHECK_UINT_EQ(amark_test_dialect_plain()->extension_count, 1);
    NAOS_CHECK_UINT_EQ(amark_test_dialect_empty()->extension_count, 0);

    g_config.dialect = amark_test_dialect();
    NAOS_CHECK_INT_EQ(amark_config_validate(&g_config, 0), AMARK_ERROR_NONE);
    g_config.dialect = amark_test_dialect_plain();
    NAOS_CHECK_INT_EQ(amark_config_validate(&g_config, 0), AMARK_ERROR_NONE);
    g_config.dialect = amark_test_dialect_empty();
    NAOS_CHECK_INT_EQ(amark_config_validate(&g_config, 0), AMARK_ERROR_NONE);
    g_config.dialect = NULL;
    NAOS_CHECK_UINT_EQ(g_state.malloc_calls, 0);
}

NAOS_TEST(the_test_dialect_registers_every_kind)
{
    const amark_dialect_extension_t *ext = amark_test_dialect()->extensions[0];
    size_t i;
    size_t k;
    const amark_kind_t want[] = {
        AMARK_TEST_KIND_BOX,   AMARK_TEST_KIND_NOTE,   AMARK_TEST_KIND_FENCE,
        AMARK_TEST_KIND_LINE,  AMARK_TEST_KIND_GRID,   AMARK_TEST_KIND_ROW,
        AMARK_TEST_KIND_CELL,  AMARK_TEST_KIND_VAR,    AMARK_TEST_KIND_STRIKE,
        AMARK_TEST_KIND_CHECK, AMARK_TEST_KIND_TASK};
    const int cls[] = {
        AMARK_DIALECT_CONTAINER, AMARK_DIALECT_CONTAINER,
        AMARK_DIALECT_LEAF,      AMARK_DIALECT_LEAF,
        AMARK_DIALECT_CONTAINER, AMARK_DIALECT_CONTAINER,
        AMARK_DIALECT_LEAF,      AMARK_DIALECT_INLINE,
        AMARK_DIALECT_INLINE,    AMARK_DIALECT_INLINE,
        AMARK_DIALECT_CONTAINER};

    NAOS_REQUIRE_UINT_EQ(ext->kind_count, sizeof want / sizeof want[0]);
    for (i = 0; i < sizeof want / sizeof want[0]; i++) {
        for (k = 0; k < ext->kind_count; k++) {
            if (ext->kinds[k].id == want[i]) {
                break;
            }
        }
        NAOS_REQUIRE_UINT_LT(k, ext->kind_count);
        NAOS_CHECK_INT_EQ(ext->kinds[k].cls, cls[i]);
    }
    /* A per-document state. */
    NAOS_CHECK_TRUE(ext->state_new != NULL);
    NAOS_CHECK_TRUE(ext->state_free != NULL);
}

/*
 * Every CommonMark kind is in k_commonmark, and a parse of it without a
 * dialect and with the dialect of 0 extensions makes as many calls to the
 * allocator (Requirements 13.2 and 15.7).
 */
NAOS_TEST(the_commonmark_input_has_every_kind_and_the_same_allocations)
{
    char dump[WIDE_DUMP_SIZE];

    (void)count_parse(S(k_commonmark));
    check_every_core_kind();
    /* Guards the input: one paragraph holds every inline kind. */
    NAOS_REQUIRE_UINT_LT(amark_test_dump_tree(g_doc, dump, sizeof dump),
                         sizeof dump);
    NAOS_CHECK_STR_EQ(dump,
        "(doc (bq (p (text \"quote\"))) (ul - tight (li (p (text \"item\"))))"
        " (ol 1 . tight (li (p (text \"ordered\")))) (hr) (h 1 atx"
        " (text \"atx\")) (h 1 setext (text \"setext\")) (code indented)"
        " (code fenced ` 3) (html) (def) (p (em (text \"em\")) (text \" \")"
        " (strong (text \"strong\")) (text \" \") (code \"`code`\")"
        " (text \" \") (link inline dest=\"/u\" (text \"link\"))"
        " (text \" \") (image inline dest=\"/i\" (text \"image\"))"
        " (text \" \") (autolink uri \"<http://a.b>\") (sbr)"
        " (html \"<b>\") (text \"raw\") (html \"</b>\") (text \" \")"
        " (esc \"\\\\*\") (text \" \") (ref \"&amp;\") (text \" \")"
        " (link shortcut def=\"ref\" (text \"ref\")) (text \" hard\")"
        " (br) (text \"break\")))");
}

NAOS_TEST(a_dialect_of_no_extensions_gives_the_same_tree)
{
    check_same_tree(amark_test_dialect_empty(), S(k_commonmark));
    check_same_tree(amark_test_dialect_empty(), S(""));
    check_same_tree(amark_test_dialect_empty(), S(k_paragraph));
    check_same_tree(amark_test_dialect_empty(), S(k_nested));
    check_same_tree(amark_test_dialect_empty(), S(k_markers));
}

NAOS_TEST(a_dialect_whose_rules_say_no_gives_the_same_tree)
{
    check_same_tree(amark_test_dialect_plain(), S(k_commonmark));
    check_same_tree(amark_test_dialect_plain(), S(""));
    check_same_tree(amark_test_dialect_plain(), S(k_paragraph));
    check_same_tree(amark_test_dialect_plain(), S(k_nested));
    check_same_tree(amark_test_dialect_plain(), S(k_markers));
}

NAOS_TEST(the_test_dialect_gives_the_same_tree_without_its_syntax)
{
    check_same_tree(amark_test_dialect(), S(k_commonmark));
    check_same_tree(amark_test_dialect(), S(""));
    check_same_tree(amark_test_dialect(), S(k_paragraph));
    check_same_tree(amark_test_dialect(), S(k_nested));
}

NAOS_SUITE(dialects_without_effect,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(the_test_dialects_pass_the_configuration_check),
    NAOS_ADD_TEST(the_test_dialect_registers_every_kind),
    NAOS_ADD_TEST(the_commonmark_input_has_every_kind_and_the_same_allocations),
    NAOS_ADD_TEST(a_dialect_of_no_extensions_gives_the_same_tree),
    NAOS_ADD_TEST(a_dialect_whose_rules_say_no_gives_the_same_tree),
    NAOS_ADD_TEST(the_test_dialect_gives_the_same_tree_without_its_syntax)
)

/* ---------------------------------------------------------------------- */
/* Failed allocations */

/* The number of nodes of kind in g_doc. */
static size_t count_kind(amark_kind_t kind)
{
    size_t count = amark_document_node_count(g_doc);
    size_t n = 0;
    size_t id;

    for (id = 1; id <= count; id++) {
        if (amark_node_kind(g_doc, (amark_node_id_t)id) == kind) {
            n++;
        }
    }
    return n;
}

/*
 * The test dialect with its state functions wrapped to count the states made
 * and released (Requirement 15.8). The copy is made in setup from the test
 * dialect; only the two state functions differ.
 */
static amark_dialect_extension_t g_counted_extension;
static const amark_dialect_extension_t *g_counted_extensions[1];
static amark_dialect_t g_counted_dialect;
static amark_dialect_state_new_fn g_inner_state_new;
static amark_dialect_state_free_fn g_inner_state_free;
static size_t g_states_made;   /* state_new returned AMARK_ERROR_NONE */
static size_t g_states_failed; /* state_new returned another value */
static size_t g_states_freed;  /* calls to state_free */
static amark_test_counts_t g_last_counts; /* the counts of the last state
                                             released */

static amark_error_t counted_state_new(const amark_allocator_t *allocator,
                                       void **state)
{
    amark_error_t err = g_inner_state_new(allocator, state);

    if (err == AMARK_ERROR_NONE) {
        g_states_made++;
    } else {
        g_states_failed++;
    }
    return err;
}

static void counted_state_free(const amark_allocator_t *allocator,
                               void *state)
{
    g_states_freed++;
    if (state != NULL) {
        g_last_counts = *(const amark_test_counts_t *)state;
    }
    g_inner_state_free(allocator, state);
}

NAOS_SETUP(counted_states)
{
    const amark_dialect_t *inner = amark_test_dialect();

    amark_test_alloc_init(&g_state);
    amark_config_init(&g_config);
    g_config.allocator = amark_test_allocator(&g_state);
    g_doc = NULL;

    g_counted_extension = *inner->extensions[0];
    g_inner_state_new = g_counted_extension.state_new;
    g_inner_state_free = g_counted_extension.state_free;
    g_counted_extension.state_new = counted_state_new;
    g_counted_extension.state_free = counted_state_free;
    g_counted_extensions[0] = &g_counted_extension;
    g_counted_dialect = *inner;
    g_counted_dialect.extensions = g_counted_extensions;
    g_counted_dialect.extension_count = 1;
    g_states_made = 0;
    g_states_failed = 0;
    g_states_freed = 0;
    memset(&g_last_counts, 0, sizeof g_last_counts);
}

/*
 * Input that calls every hook of the test dialect: the block start rules
 * (BOX, NOTE, FENCE, LINE), the continue rules (BOX, NOTE), the close rules
 * (BOX, FENCE, LINE), the paragraph close rule (GRID), the inline rule (VAR),
 * the delimiter kind (STRIKE) and the postprocessing rule (CHECK, TASK). It
 * ends with results each hook rejects, repeated; g_every_hook adds a
 * paragraph of rejected VARs, so that the allocations of the diagnostics
 * reach the allocator too (Requirement 11.7).
 */
#define REJECTED_LINES \
    "@> !start-origin\n" \
    "@> box\n" \
    "@> !cont-line\n" \
    "\n" \
    "@> !close-index\n" \
    "\n" \
    "@@@!close-bounds\n" \
    "@@@\n" \
    "text\n" \
    "|!close-order\n" \
    "\n" \
    "a $!inline-origin$ b\n" \
    "\n" \
    "- [x] !post-order\n" \
    "\n"

#define REJECTED_LINE_DIAGNOSTICS 7

#define REJECTED_LINES_4 REJECTED_LINES REJECTED_LINES REJECTED_LINES \
    REJECTED_LINES

/*
 * A rejected VAR (the closing '$' and a space after it are no VAR), and the
 * number of them on each line and of lines in the paragraph of rejected VARs
 * that ends the input below.
 */
static const char k_rejected_var[] = "$!inline-origin$ ";
#define VARS_PER_LINE 100
#define VAR_LINES 10

static const char k_every_hook[] =
    "@> box *one*\n"
    "@> two\n"
    "\n"
    "@note: a note\n"
    "  goes on\n"
    "\n"
    "  and on\n"
    "@@@\n"
    "fenced\n"
    "@@@\n"
    "@line *one* $ab$ line\n"
    "\n"
    "before the grid\n"
    "| a | *b* |\n"
    "| c | ~d~ |\n"
    "\n"
    "- [x] done $var$ ~one~ ~~two~~\n"
    "- [ ] open [link](/url) `code`\n"
    "\n"
    REJECTED_LINES_4 REJECTED_LINES_4;

/* k_every_hook, then the paragraph of rejected VARs. */
#define EVERY_HOOK_SIZE \
    (sizeof k_every_hook - 1 + \
     VAR_LINES * (VARS_PER_LINE * (sizeof k_rejected_var - 1) + 1))
static char g_every_hook[EVERY_HOOK_SIZE];

/*
 * Writes the input to g_every_hook. The paragraph of rejected VARs makes
 * one rejection after another while the inline parser adds no node to the
 * document, and their diagnostics take most of the document arena, so they
 * make it grow.
 */
static void write_every_hook(void)
{
    size_t used = sizeof k_every_hook - 1;
    size_t line;
    size_t var;

    memcpy(g_every_hook, k_every_hook, used);
    for (line = 0; line < VAR_LINES; line++) {
        for (var = 0; var < VARS_PER_LINE; var++) {
            memcpy(g_every_hook + used, k_rejected_var,
                   sizeof k_rejected_var - 1);
            used += sizeof k_rejected_var - 1;
        }
        g_every_hook[used++] = '\n';
    }
    NAOS_REQUIRE_UINT_EQ(used, sizeof g_every_hook);
}

/* Requires g_doc to hold a node of every kind of the test dialect. */
static void check_every_kind(void)
{
    static const amark_kind_t kinds[] = {
        AMARK_TEST_KIND_BOX,   AMARK_TEST_KIND_NOTE,   AMARK_TEST_KIND_FENCE,
        AMARK_TEST_KIND_LINE,  AMARK_TEST_KIND_GRID,   AMARK_TEST_KIND_ROW,
        AMARK_TEST_KIND_CELL,  AMARK_TEST_KIND_VAR,    AMARK_TEST_KIND_STRIKE,
        AMARK_TEST_KIND_CHECK, AMARK_TEST_KIND_TASK};
    size_t i;

    for (i = 0; i < sizeof kinds / sizeof kinds[0]; i++) {
        NAOS_CHECK_UINT_GT(count_kind(kinds[i]), 0);
    }
}

/* The diagnostics of g_every_hook. */
#define EVERY_HOOK_DIAGNOSTICS \
    ((size_t)(8 * REJECTED_LINE_DIAGNOSTICS + VAR_LINES * VARS_PER_LINE))

/* The highest fail_at tried; the loop must end by a success before it. */
#define FAIL_AT_MAX ((size_t)2000)

/*
 * Parses g_every_hook with the counted dialect, failing the fail_at-th call
 * to the allocator, for fail_at = 1, 2, ... until the parse succeeds
 * without reaching fail_at. At each index: a failed parse returns
 * AMARK_ERROR_NOMEM, sets the document to NULL and has made exactly fail_at
 * calls; whatever the result, no block is left, every free received the
 * context value, and state_free was called as many times as state_new
 * succeeded (Requirements 10.10, 11.7, 13.7, 13.10 and 15.8).
 */
NAOS_TEST(every_failed_allocation_releases_everything)
{
    size_t fail_at;
    size_t failures = 0;
    size_t state_failures = 0;
    bool succeeded = false;
    amark_document_t *doc;
    amark_error_t err;
    amark_tree_check_result_t check;

    write_every_hook();
    g_config.dialect = &g_counted_dialect;
    for (fail_at = 1; fail_at < FAIL_AT_MAX && !succeeded; fail_at++) {
        amark_test_alloc_init(&g_state);
        g_state.fail_at = fail_at;
        g_states_made = 0;
        g_states_failed = 0;
        g_states_freed = 0;
        doc = (amark_document_t *)&g_state; /* any value but NULL */
        err = amark_parse(g_every_hook, sizeof g_every_hook, &g_config,
                          &doc);
        if (err == AMARK_ERROR_NONE) {
            succeeded = true;
            /* fail_at is past the last allocation. */
            NAOS_CHECK_UINT_LT(g_state.malloc_calls, fail_at);
            NAOS_REQUIRE_NOT_NULL(doc);
            check = amark_tree_check(doc);
            NAOS_CHECK_INT_EQ(check.violation, AMARK_TREE_VIOLATION_NONE);
            NAOS_CHECK_UINT_EQ(amark_document_diagnostic_count(doc),
                               EVERY_HOOK_DIAGNOSTICS);
            NAOS_CHECK_UINT_EQ(g_states_made, 1);
            /* The state is released before the parse returns. */
            NAOS_CHECK_UINT_EQ(g_states_freed, 1);
            g_doc = doc;
            check_every_kind();
            amark_document_destroy(g_doc);
            g_doc = NULL;
        } else {
            failures++;
            NAOS_CHECK_INT_EQ(err, AMARK_ERROR_NOMEM);
            NAOS_CHECK_NULL(doc);
            NAOS_CHECK_UINT_EQ(g_state.malloc_calls, fail_at);
            NAOS_CHECK_UINT_EQ(g_states_freed, g_states_made);
            state_failures += g_states_failed;
        }
        NAOS_CHECK_UINT_EQ(g_state.live, 0);
        NAOS_CHECK_UINT_EQ(g_state.live_bytes, 0);
        NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
        if (g_state.live != 0 || g_states_freed != g_states_made) {
            NAOS_NOTE("first failure at fail_at %u", (unsigned)fail_at);
            break;
        }
    }
    NAOS_CHECK_TRUE(succeeded);
    /*
     * One failure is that of the state; the others go well past the
     * allocations every parse makes.
     */
    NAOS_CHECK_UINT_EQ(state_failures, 1);
    NAOS_CHECK_UINT_GT(failures, 2 * BASELINE_PARAGRAPH);
    g_config.dialect = NULL;
}

NAOS_SUITE(failed_allocations,
    NAOS_SET_SETUP(counted_states),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(every_failed_allocation_releases_everything)
)

/* ---------------------------------------------------------------------- */
/* Repeated parses and the content */

/*
 * A parse with a dialect is a function of the content, the configuration
 * and the dialect: parsing the same content twice with the test dialect
 * gives the same tree, the same attributes and the same diagnostics, in the
 * same order (Requirements 11.6, 12.10, 13.8 and 15.9). The state of one
 * parse is not that of another: the TASK attributes, numbered from 1 by the
 * per-document state, are the same in the second parse, and each parse
 * makes and releases a state of its own (Requirements 12.7 and 12.8). The
 * parse reads the content and never writes to it (Requirement 13.9).
 */

/* Room for the description of the tree of k_every_hook. */
#define DESCRIPTION_SIZE ((size_t)65536)

static char g_description[2][DESCRIPTION_SIZE];

/* A document kept while another is parsed; teardown destroys it. */
static amark_document_t *g_kept;

NAOS_TEARDOWN(repeated)
{
    amark_document_destroy(g_kept);
    g_kept = NULL;
    amark_document_destroy(g_doc);
    g_doc = NULL;
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
    NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
}
static char g_full_dump[2][DESCRIPTION_SIZE];

/*
 * The content is parsed from g_content, whose bytes past the content are
 * set to CANARY, and compared with g_reference after each parse.
 */
#define CANARY 0x5A
#define CANARY_SIZE ((size_t)64)

static char g_content[EVERY_HOOK_SIZE + CANARY_SIZE];
static char g_reference[EVERY_HOOK_SIZE + CANARY_SIZE];
static size_t g_content_length;

/* Writes content to g_content and to g_reference, then the canary. */
static void content_set(const char *content, size_t length)
{
    NAOS_REQUIRE_UINT_LE(length, EVERY_HOOK_SIZE);
    memset(g_content, CANARY, sizeof g_content);
    memcpy(g_content, content, length);
    memcpy(g_reference, g_content, sizeof g_reference);
    g_content_length = length;
}

/* Requires g_content, the content and the canary, to be as written. */
static void check_content_unchanged(void)
{
    NAOS_CHECK_TRUE(memcmp(g_content, g_reference, sizeof g_content) == 0);
}

/* Appends to the description in out (of size bytes, used *used). */
static void describe_bytes(char *out, size_t size, size_t *used,
                           const char *bytes, size_t length)
{
    NAOS_REQUIRE_UINT_LT(length, size - *used);
    memcpy(out + *used, bytes, length);
    *used += length;
    out[*used] = '\0';
}

static void describe_number(char *out, size_t size, size_t *used,
                            const char *label, unsigned long value)
{
    char buffer[48];
    int length = snprintf(buffer, sizeof buffer, " %s%lu", label, value);

    NAOS_REQUIRE_TRUE(length > 0 && (size_t)length < sizeof buffer);
    describe_bytes(out, size, used, buffer, (size_t)length);
}

/*
 * Writes g_doc to out as text: for each node in the order of its ID, its
 * kind, parent, first child, next sibling, range, lines and dialect
 * attribute bytes (in hexadecimal); then each diagnostic in order: its
 * offset, line, severity, message and file name. Two documents with the
 * same description are the same.
 */
static void describe_document(char *out, size_t size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t count = amark_document_node_count(g_doc);
    size_t diagnostics = amark_document_diagnostic_count(g_doc);
    size_t used = 0;
    size_t id;
    size_t i;
    amark_node_id_t node;
    amark_range_t range;
    const unsigned char *data;
    size_t data_size;
    amark_diagnostic_t diagnostic;
    char pair[2];

    out[0] = '\0';
    describe_number(out, size, &used, "nodes ", (unsigned long)count);
    for (id = 1; id <= count; id++) {
        node = (amark_node_id_t)id;
        describe_bytes(out, size, &used, S("\n"));
        describe_number(out, size, &used, "kind ",
                        (unsigned long)amark_node_kind(g_doc, node));
        describe_number(out, size, &used, "parent ",
                        (unsigned long)amark_node_parent(g_doc, node));
        describe_number(out, size, &used, "child ",
                        (unsigned long)amark_node_first_child(g_doc, node));
        describe_number(out, size, &used, "next ",
                        (unsigned long)amark_node_next_sibling(g_doc, node));
        NAOS_REQUIRE_TRUE(amark_node_range(g_doc, node, &range));
        describe_number(out, size, &used, "start ",
                        (unsigned long)range.start);
        describe_number(out, size, &used, "end ", (unsigned long)range.end);
        describe_number(out, size, &used, "lines ",
                        (unsigned long)amark_node_start_line(g_doc, node));
        describe_number(out, size, &used, "to ",
                        (unsigned long)amark_node_end_line(g_doc, node));
        data = (const unsigned char *)amark_node_dialect_data(g_doc, node,
                                                              &data_size);
        describe_number(out, size, &used, "data ", (unsigned long)data_size);
        describe_bytes(out, size, &used, S(" "));
        for (i = 0; i < data_size; i++) {
            pair[0] = hex[data[i] >> 4];
            pair[1] = hex[data[i] & 0x0F];
            describe_bytes(out, size, &used, pair, 2);
        }
    }
    describe_bytes(out, size, &used, S("\n"));
    describe_number(out, size, &used, "diagnostics ",
                    (unsigned long)diagnostics);
    for (i = 0; i < diagnostics; i++) {
        NAOS_REQUIRE_TRUE(amark_document_diagnostic(g_doc, i, &diagnostic));
        describe_bytes(out, size, &used, S("\n"));
        describe_number(out, size, &used, "offset ",
                        (unsigned long)diagnostic.offset);
        describe_number(out, size, &used, "line ",
                        (unsigned long)diagnostic.line);
        describe_number(out, size, &used, "severity ",
                        (unsigned long)diagnostic.severity);
        NAOS_REQUIRE_NOT_NULL(diagnostic.message);
        describe_bytes(out, size, &used, S(" "));
        describe_bytes(out, size, &used, diagnostic.message,
                       strlen(diagnostic.message));
        describe_bytes(out, size, &used, S(" file "));
        if (diagnostic.filename != NULL) {
            describe_bytes(out, size, &used, diagnostic.filename,
                           strlen(diagnostic.filename));
        }
    }
}

/* The largest number of TASKs check_task_numbers reads. */
#define MAX_TASKS 64

/*
 * Requires the TASKs of g_doc to be numbered 1 to their number, each once,
 * and returns their number: numbers that go on from an earlier parse would
 * show a state carried over.
 */
static size_t check_task_numbers(void)
{
    bool seen[MAX_TASKS + 1];
    size_t count = amark_document_node_count(g_doc);
    size_t tasks = count_kind(AMARK_TEST_KIND_TASK);
    size_t id;
    size_t i;
    size_t size;
    size_t number;
    const char *data;

    NAOS_REQUIRE_UINT_LE(tasks, MAX_TASKS);
    memset(seen, 0, sizeof seen);
    for (id = 1; id <= count; id++) {
        if (amark_node_kind(g_doc, (amark_node_id_t)id) !=
            AMARK_TEST_KIND_TASK) {
            continue;
        }
        data = (const char *)amark_node_dialect_data(
            g_doc, (amark_node_id_t)id, &size);
        NAOS_REQUIRE_NOT_NULL(data);
        NAOS_REQUIRE_UINT_GT(size, 0);
        number = 0;
        for (i = 0; i < size; i++) {
            NAOS_REQUIRE_TRUE(data[i] >= '0' && data[i] <= '9');
            number = number * 10 + (size_t)(data[i] - '0');
        }
        NAOS_TRACE_PUSH("TASK %lu numbered %lu", (unsigned long)id,
                        (unsigned long)number);
        NAOS_CHECK_UINT_GE(number, 1);
        NAOS_CHECK_UINT_LE(number, tasks);
        if (number >= 1 && number <= tasks) {
            NAOS_CHECK_FALSE(seen[number]);
            seen[number] = true;
        }
        NAOS_TRACE_POP();
    }
    return tasks;
}

/*
 * Parses g_content with the counted dialect into a fresh g_doc, requires
 * success, a tree that passes the check, one state made and released, and
 * the content unchanged; writes the description and the dump of the tree
 * to the slot-th buffers.
 */
static void parse_described(size_t slot)
{
    amark_tree_check_result_t check;

    amark_document_destroy(g_doc);
    g_doc = NULL;
    g_config.dialect = &g_counted_dialect;
    g_states_made = 0;
    g_states_failed = 0;
    g_states_freed = 0;
    memset(&g_last_counts, 0, sizeof g_last_counts);
    NAOS_REQUIRE_INT_EQ(amark_parse(g_content, g_content_length, &g_config,
                                    &g_doc),
                        AMARK_ERROR_NONE);
    g_config.dialect = NULL;
    NAOS_REQUIRE_NOT_NULL(g_doc);
    check = amark_tree_check(g_doc);
    NAOS_CHECK_INT_EQ(check.violation, AMARK_TREE_VIOLATION_NONE);
    NAOS_CHECK_UINT_EQ(g_states_made, 1);
    NAOS_CHECK_UINT_EQ(g_states_freed, 1);
    check_content_unchanged();
    describe_document(g_description[slot], DESCRIPTION_SIZE);
    NAOS_REQUIRE_UINT_LT(amark_test_dump_tree(g_doc, g_full_dump[slot],
                                              DESCRIPTION_SIZE),
                         DESCRIPTION_SIZE);
}

NAOS_TEST(two_parses_with_the_test_dialect_give_the_same_document)
{
    amark_test_counts_t first;
    size_t tasks;

    content_set(S(k_every_hook));
    parse_described(0);
    check_every_kind();
    /* The rejected lines, 8 times: there are diagnostics to compare. */
    NAOS_CHECK_UINT_EQ(amark_document_diagnostic_count(g_doc),
                       8 * REJECTED_LINE_DIAGNOSTICS);
    tasks = check_task_numbers();
    NAOS_CHECK_UINT_GT(tasks, 1);
    NAOS_CHECK_UINT_EQ(g_last_counts.tasks, tasks);
    first = g_last_counts;

    parse_described(1);
    NAOS_CHECK_STR_EQ(g_description[1], g_description[0]);
    NAOS_CHECK_STR_EQ(g_full_dump[1], g_full_dump[0]);
    /* A fresh state: the numbers start again from 1. */
    NAOS_CHECK_UINT_EQ(check_task_numbers(), tasks);
    NAOS_CHECK_UINT_EQ(g_last_counts.tasks, tasks);
    /* The rules were called as often, in each parse (Requirement 12.10). */
    NAOS_CHECK_UINT_EQ(g_last_counts.block_starts, first.block_starts);
    NAOS_CHECK_UINT_EQ(g_last_counts.block_continues, first.block_continues);
    NAOS_CHECK_UINT_EQ(g_last_counts.block_closes, first.block_closes);
    NAOS_CHECK_UINT_EQ(g_last_counts.paragraphs, first.paragraphs);
    NAOS_CHECK_UINT_EQ(g_last_counts.inlines, first.inlines);
    NAOS_CHECK_UINT_EQ(g_last_counts.postprocesses, first.postprocesses);
}

/*
 * A document kept while another parse runs with the same dialect is not
 * changed by it: the two do not share a state or anything else.
 */
NAOS_TEST(a_parse_does_not_change_the_document_of_another)
{
    static const char other[] = "- [x] a\n- [ ] b\n- [x] c\n";
    content_set(S(k_every_hook));
    parse_described(0);
    g_kept = g_doc;
    g_doc = NULL;
    g_config.dialect = &g_counted_dialect;
    NAOS_REQUIRE_INT_EQ(amark_parse(S(other), &g_config, &g_doc),
                        AMARK_ERROR_NONE);
    g_config.dialect = NULL;
    /* The other document numbers its own TASKs from 1. */
    NAOS_CHECK_UINT_EQ(check_task_numbers(), 3);
    amark_document_destroy(g_doc);
    g_doc = g_kept;
    g_kept = NULL;
    describe_document(g_description[1], DESCRIPTION_SIZE);
    NAOS_CHECK_STR_EQ(g_description[1], g_description[0]);
    check_content_unchanged();
}

/*
 * The content is unchanged, its bytes and the bytes after it, by a parse
 * with each dialect, of input with every hook of the test dialect, its
 * rejected results and their diagnostics, and of input of every CommonMark
 * kind (Requirement 13.9).
 */
NAOS_TEST(a_parse_with_a_dialect_does_not_change_the_content)
{
    const amark_dialect_t *dialects[4];
    size_t d;

    dialects[0] = NULL;
    dialects[1] = amark_test_dialect();
    dialects[2] = amark_test_dialect_plain();
    dialects[3] = amark_test_dialect_empty();
    write_every_hook();
    for (d = 0; d < sizeof dialects / sizeof dialects[0]; d++) {
        NAOS_TRACE_PUSH("dialect %lu", (unsigned long)d);
        g_config.dialect = dialects[d];
        content_set(g_every_hook, sizeof g_every_hook);
        amark_document_destroy(g_doc);
        g_doc = NULL;
        NAOS_CHECK_INT_EQ(amark_parse(g_content, g_content_length, &g_config,
                                      &g_doc),
                          AMARK_ERROR_NONE);
        check_content_unchanged();
        content_set(S(k_commonmark));
        amark_document_destroy(g_doc);
        g_doc = NULL;
        NAOS_CHECK_INT_EQ(amark_parse(g_content, g_content_length, &g_config,
                                      &g_doc),
                          AMARK_ERROR_NONE);
        check_content_unchanged();
        NAOS_TRACE_POP();
    }
    g_config.dialect = NULL;
}

NAOS_SUITE(repeated_parses,
    NAOS_SET_SETUP(counted_states),
    NAOS_SET_TEARDOWN(repeated),
    NAOS_ADD_TEST(two_parses_with_the_test_dialect_give_the_same_document),
    NAOS_ADD_TEST(a_parse_does_not_change_the_document_of_another),
    NAOS_ADD_TEST(a_parse_with_a_dialect_does_not_change_the_content)
)

/* ---------------------------------------------------------------------- */
/* Resource limits */

/*
 * The depth of the deepest node of g_doc (the root is at depth 0), and the
 * number of nodes reached by walking the tree from the root.
 */
static void measure_tree(size_t *depth_out, size_t *reached_out)
{
    amark_node_id_t root = amark_document_root(g_doc);
    amark_node_id_t node = root;
    amark_node_id_t next;
    size_t depth = 0;
    size_t deepest = 0;
    size_t reached = 0;

    while (node != AMARK_NODE_NONE) {
        reached++;
        if (depth > deepest) {
            deepest = depth;
        }
        next = amark_node_first_child(g_doc, node);
        if (next != AMARK_NODE_NONE) {
            depth++;
            node = next;
            continue;
        }
        next = AMARK_NODE_NONE;
        while (node != root) {
            next = amark_node_next_sibling(g_doc, node);
            if (next != AMARK_NODE_NONE) {
                break;
            }
            node = amark_node_parent(g_doc, node);
            depth--;
        }
        node = next;
    }
    *depth_out = deepest;
    *reached_out = reached;
}

/*
 * Parses content with the test dialect and the limits in g_config into a
 * fresh g_doc, and requires the result want (on failure: no document and
 * no block left).
 */
static void parse_limited(const char *content, size_t length,
                          amark_error_t want)
{
    amark_tree_check_result_t check;

    amark_document_destroy(g_doc);
    g_doc = NULL;
    g_config.dialect = amark_test_dialect();
    NAOS_REQUIRE_INT_EQ(amark_parse(content, length, &g_config, &g_doc),
                        want);
    if (want == AMARK_ERROR_NONE) {
        NAOS_REQUIRE_NOT_NULL(g_doc);
        check = amark_tree_check(g_doc);
        NAOS_CHECK_INT_EQ(check.violation, AMARK_TREE_VIOLATION_NONE);
    } else {
        NAOS_CHECK_NULL(g_doc);
        NAOS_CHECK_UINT_EQ(g_state.live, 0);
    }
}

/*
 * Parses content without limits, then with max_node_count and with
 * max_nesting_depth at the values of its tree (success, the same tree) and
 * one below them, when that is not 0 (AMARK_ERROR_LIMIT_EXCEEDED). Leaves the tree parsed
 * without limits in g_doc, returns its node count and writes its depth to
 * depth_out.
 */
static size_t check_tight_limits(const char *content, size_t length,
                                 size_t *depth_out)
{
    size_t nodes;
    size_t depth;
    size_t reached;
    size_t diagnostics;

    g_config.max_node_count = 0;
    g_config.max_nesting_depth = 0;
    parse_limited(content, length, AMARK_ERROR_NONE);
    nodes = amark_document_node_count(g_doc);
    diagnostics = amark_document_diagnostic_count(g_doc);
    measure_tree(&depth, &reached);
    /* Every node made is in the tree. */
    NAOS_CHECK_UINT_EQ(reached, nodes);

    g_config.max_node_count = nodes;
    parse_limited(content, length, AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), nodes);
    NAOS_CHECK_UINT_EQ(amark_document_diagnostic_count(g_doc), diagnostics);
    g_config.max_node_count = nodes - 1;
    parse_limited(content, length, AMARK_ERROR_LIMIT_EXCEEDED);
    g_config.max_node_count = 0;

    g_config.max_nesting_depth = depth;
    parse_limited(content, length, AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), nodes);
    NAOS_CHECK_UINT_EQ(amark_document_diagnostic_count(g_doc), diagnostics);
    /* A limit of 0 is no limit. */
    if (depth > 1) {
        g_config.max_nesting_depth = depth - 1;
        parse_limited(content, length, AMARK_ERROR_LIMIT_EXCEEDED);
    }
    g_config.max_nesting_depth = 0;

    /* Leaves the tree without limits in g_doc. */
    parse_limited(content, length, AMARK_ERROR_NONE);
    *depth_out = depth;
    return nodes;
}

/*
 * Rejected results add no node and are not counted against the node limit:
 * a parse whose rules keep returning invalid results succeeds when the tree
 * it returns fits the limit (Requirements 10.12 and 15.12). The rejected
 * results here would add nodes: replies of a dialect block and of a
 * paragraph with blocks in them, a VAR, and a postprocessing CHECK, the
 * last one made once every other node is in the tree.
 */
NAOS_TEST(rejected_results_do_not_count_toward_the_node_limit)
{
    static const char content[] = REJECTED_LINES_4;
    size_t depth;
    size_t nodes = check_tight_limits(S(content), &depth);

    NAOS_CHECK_UINT_EQ(amark_document_diagnostic_count(g_doc),
                       4 * REJECTED_LINE_DIAGNOSTICS);
    /* None of the rejected nodes is in the tree. */
    NAOS_CHECK_UINT_EQ(count_kind(AMARK_TEST_KIND_VAR), 0);
    NAOS_CHECK_UINT_EQ(count_kind(AMARK_TEST_KIND_GRID), 0);
    NAOS_CHECK_UINT_EQ(count_kind(AMARK_TEST_KIND_CHECK), 4);
    NAOS_CHECK_UINT_GT(nodes, 1);
}

/*
 * A dialect node that makes the tree exceed a limit ends the parse with
 * AMARK_ERROR_LIMIT_EXCEEDED, as a CommonMark node does (Requirement 10.11):
 * the node beyond the limit is, in turn, a dialect block, a block of a
 * paragraph's replacement, a dialect inline and a node the postprocessing
 * rule adds.
 */
NAOS_TEST(a_dialect_block_past_the_node_limit_ends_the_parse)
{
    static const char content[] = "@@@\n@@@\n";
    size_t depth;

    /* The root and the fence. */
    NAOS_CHECK_UINT_EQ(check_tight_limits(S(content), &depth), 2);
    NAOS_CHECK_UINT_EQ(depth, 1);
    NAOS_CHECK_UINT_EQ(count_kind(AMARK_TEST_KIND_FENCE), 1);
}

NAOS_TEST(a_replacement_block_past_the_node_limit_ends_the_parse)
{
    static const char content[] = "|\n";
    size_t depth;

    /* The root, the grid and the row (the cell is blank: there is none). */
    NAOS_CHECK_UINT_EQ(check_tight_limits(S(content), &depth), 3);
    NAOS_CHECK_UINT_EQ(depth, 2);
    NAOS_CHECK_UINT_EQ(count_kind(AMARK_TEST_KIND_ROW), 1);
}

NAOS_TEST(a_dialect_inline_past_the_node_limit_ends_the_parse)
{
    static const char content[] = "$a$\n";
    size_t depth;

    /* The root, the paragraph and the VAR. */
    NAOS_CHECK_UINT_EQ(check_tight_limits(S(content), &depth), 3);
    NAOS_CHECK_UINT_EQ(depth, 2);
    NAOS_CHECK_UINT_EQ(count_kind(AMARK_TEST_KIND_VAR), 1);
}

NAOS_TEST(a_postprocessing_node_past_the_node_limit_ends_the_parse)
{
    static const char content[] = "[x] a\n";
    size_t depth;

    /*
     * The root, the paragraph, the CHECK, the text " " and the text "a";
     * before the postprocessing, the root, the paragraph and one text.
     */
    NAOS_CHECK_UINT_EQ(check_tight_limits(S(content), &depth), 5);
    NAOS_CHECK_UINT_EQ(count_kind(AMARK_TEST_KIND_CHECK), 1);
    g_config.max_node_count = 3;
    parse_limited(S(content), AMARK_ERROR_LIMIT_EXCEEDED);
    g_config.max_node_count = 0;
}

/*
 * The deepest node is a dialect block (a fence in two boxes) or a dialect
 * inline (a VAR in a STRIKE in emphasis): one level less than its depth
 * ends the parse (Requirement 10.11).
 */
NAOS_TEST(a_dialect_block_past_the_depth_limit_ends_the_parse)
{
    static const char content[] = "@> @> @@@\n";
    size_t depth;

    /* The root, two boxes and the fence. */
    NAOS_CHECK_UINT_EQ(check_tight_limits(S(content), &depth), 4);
    NAOS_CHECK_UINT_EQ(depth, 3);
    NAOS_CHECK_UINT_EQ(count_kind(AMARK_TEST_KIND_BOX), 2);
}

NAOS_TEST(a_dialect_inline_past_the_depth_limit_ends_the_parse)
{
    static const char content[] = "*~$a$~*\n";
    size_t depth;

    /* The root, the paragraph, the emphasis, the STRIKE and the VAR. */
    NAOS_CHECK_UINT_EQ(check_tight_limits(S(content), &depth), 5);
    NAOS_CHECK_UINT_EQ(depth, 4);
    NAOS_CHECK_UINT_EQ(count_kind(AMARK_TEST_KIND_STRIKE), 1);
    NAOS_CHECK_UINT_EQ(count_kind(AMARK_TEST_KIND_VAR), 1);
}

NAOS_SUITE(resource_limits,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(rejected_results_do_not_count_toward_the_node_limit),
    NAOS_ADD_TEST(a_dialect_block_past_the_node_limit_ends_the_parse),
    NAOS_ADD_TEST(a_replacement_block_past_the_node_limit_ends_the_parse),
    NAOS_ADD_TEST(a_dialect_inline_past_the_node_limit_ends_the_parse),
    NAOS_ADD_TEST(a_postprocessing_node_past_the_node_limit_ends_the_parse),
    NAOS_ADD_TEST(a_dialect_block_past_the_depth_limit_ends_the_parse),
    NAOS_ADD_TEST(a_dialect_inline_past_the_depth_limit_ends_the_parse)
)

/* ---------------------------------------------------------------------- */
/* Pathological inputs and the number of rule calls */

/*
 * Inputs that would exhaust the stack of a recursive parser or take time
 * quadratic in their length in a careless one, parsed with the test dialect
 * (the counted copy of the failed allocations above, whose state_free keeps
 * the per-document counts in g_last_counts): boxes nested 100,000 deep,
 * notes nested 100,000 deep followed by 100,000 blank lines (from the second
 * blank line of a run, the blank line cutoff matches none of the notes), and
 * long runs of '~' that the lower bounds of the delimiter stack must keep
 * linear for an EQUAL kind (Requirements 13.5, 13.6 and 15.10). Every
 * document goes through amark_tree_check and amark_document_destroy; none
 * of them is recursive either. The default limits are 0 (none), so the
 * whole depth is parsed; the boxes are also parsed with max_nesting_depth at
 * the depth of their tree and one below it.
 *
 * The number of rule calls (Requirement 13.4, design "Performance &
 * Scalability"): the block start rules are called at most once per rule at
 * each position tried on a line, and a line has as many as the containers
 * opened on it plus 1; the continue rules once per pair of an open dialect
 * block and a line that is not blank; the close rules once per closed
 * dialect block of a kind that has one; the paragraph close rule once per
 * paragraph; the inline rule once per trigger character visited; and the
 * postprocessing rule once per document.
 *
 * Linear time, as test_inline_limits measures it: each input is a pattern
 * of size n, parsed at a small n and at an n about 32 times as large. The
 * processor time of the large parse must stay within TIME_FACTOR times the
 * length ratio times that of the small one, plus TIME_SLACK; a parse whose
 * time grows as the square of n reaches 32 times more. The small input is
 * parsed again and again for a twentieth of a second at least and the large
 * one twice, keeping the faster.
 */
#define DEEP 100000
#define START_RULES 4 /* the block start rules of the test dialect */

#define TIME_FACTOR 10
#define TIME_SLACK 50000 /* microseconds */

/* The input, allocated with malloc (outside the counting allocator). */
static char *g_input;
static size_t g_length;
static size_t g_capacity;

static void input_free(void)
{
    free(g_input);
    g_input = NULL;
    g_length = 0;
    g_capacity = 0;
}

/* Replaces the input with an empty one of capacity bytes. */
static void input_begin(size_t capacity)
{
    input_free();
    g_input = (char *)malloc(capacity > 0 ? capacity : 1);
    NAOS_REQUIRE_NOT_NULL(g_input);
    g_capacity = capacity;
}

/* Appends the length bytes at bytes, count times. */
static void input_repeat(const char *bytes, size_t length, size_t count)
{
    size_t i;

    NAOS_REQUIRE_TRUE(count == 0 ||
                      length <= (g_capacity - g_length) / count);
    for (i = 0; i < count; i++) {
        memcpy(g_input + g_length, bytes, length);
        g_length += length;
    }
}

#define REPEAT(s, count) input_repeat((s), sizeof(s) - 1, (count))

NAOS_TEARDOWN(measured)
{
    input_free();
    amark_document_destroy(g_doc);
    g_doc = NULL;
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
    NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
}

/*
 * Parses the input with the counted dialect and the two limits into a
 * fresh g_doc. One state is made and released, whatever the result, and
 * its counts are left in g_last_counts.
 */
static amark_error_t parse_measured(size_t max_depth, size_t max_nodes)
{
    amark_error_t err;

    amark_document_destroy(g_doc);
    g_doc = NULL;
    g_config.dialect = &g_counted_dialect;
    g_config.max_nesting_depth = max_depth;
    g_config.max_node_count = max_nodes;
    g_states_made = 0;
    g_states_failed = 0;
    g_states_freed = 0;
    memset(&g_last_counts, 0, sizeof g_last_counts);
    err = amark_parse(g_input, g_length, &g_config, &g_doc);
    NAOS_CHECK_UINT_EQ(g_states_made, 1);
    NAOS_CHECK_UINT_EQ(g_states_freed, 1);
    return err;
}

/*
 * Requires the parse of the input without limits to succeed with a tree
 * that passes the check and whose deepest node is at depth; every node made
 * is in the tree.
 */
static void parse_deep(size_t depth)
{
    amark_tree_check_result_t check;
    size_t deepest;
    size_t reached;

    NAOS_REQUIRE_INT_EQ(parse_measured(0, 0), AMARK_ERROR_NONE);
    NAOS_REQUIRE_NOT_NULL(g_doc);
    check = amark_tree_check(g_doc);
    NAOS_CHECK_INT_EQ(check.violation, AMARK_TREE_VIOLATION_NONE);
    NAOS_CHECK_UINT_EQ(check.node, AMARK_NODE_NONE);
    measure_tree(&deepest, &reached);
    NAOS_CHECK_UINT_EQ(deepest, depth);
    NAOS_CHECK_UINT_EQ(reached, amark_document_node_count(g_doc));
    NAOS_CHECK_UINT_EQ(amark_document_diagnostic_count(g_doc), 0);
}

/*
 * The design's bounds on the rule calls of one parse, from the input and
 * its tree.
 */
typedef struct call_bounds {
    size_t lines;      /* lines of the input */
    size_t opened;     /* containers opened, CommonMark's and the dialect's */
    size_t pairs;      /* pairs of an open dialect block and a line that is
                          not blank, the block's first line left out */
    size_t closed;     /* closed dialect blocks of kinds with a close rule */
    size_t paragraphs; /* paragraphs closed */
    size_t triggers;   /* trigger characters of the inline rule ('$') */
} call_bounds_t;

/* Requires the counts of the last parse to be within bounds. */
static void check_calls(const call_bounds_t *bounds)
{
    NAOS_CHECK_UINT_LE(g_last_counts.block_starts,
                       START_RULES * (bounds->lines + bounds->opened));
    NAOS_CHECK_UINT_LE(g_last_counts.block_continues, bounds->pairs);
    NAOS_CHECK_UINT_LE(g_last_counts.block_closes, bounds->closed);
    NAOS_CHECK_UINT_LE(g_last_counts.paragraphs, bounds->paragraphs);
    NAOS_CHECK_UINT_LE(g_last_counts.inlines, bounds->triggers);
    NAOS_CHECK_UINT_EQ(g_last_counts.postprocesses, 1);
}

/* "@> " depth times and text, twice: the second line goes on in each box. */
static void build_boxes(size_t depth)
{
    input_begin(2 * (3 * depth + 2));
    REPEAT("@> ", depth);
    REPEAT("a\n", 1);
    REPEAT("@> ", depth);
    REPEAT("b\n", 1);
}

NAOS_TEST(boxes_nested_100000_deep)
{
    call_bounds_t bounds = {2, DEEP, DEEP, DEEP, 1, 0};

    /* The root, DEEP boxes, the paragraph and its text. */
    build_boxes(DEEP);
    parse_deep(DEEP + 2);
    NAOS_CHECK_UINT_EQ(count_kind(AMARK_TEST_KIND_BOX), DEEP);
    NAOS_CHECK_UINT_EQ(count_kind(AMARK_KIND_PARAGRAPH), 1);
    check_calls(&bounds);
    /* The rules were called: the counts are no bounds of 0 calls. */
    NAOS_CHECK_UINT_GE(g_last_counts.block_starts, DEEP);
    NAOS_CHECK_UINT_EQ(g_last_counts.block_continues, DEEP);
    NAOS_CHECK_UINT_EQ(g_last_counts.block_closes, DEEP);
    NAOS_CHECK_UINT_EQ(g_last_counts.paragraphs, 1);

    /* The depth limit at the depth of the tree, and one below it. */
    NAOS_CHECK_INT_EQ(parse_measured(DEEP + 2, 0), AMARK_ERROR_NONE);
    NAOS_CHECK_NOT_NULL(g_doc);
    NAOS_CHECK_INT_EQ(parse_measured(DEEP + 1, 0),
                      AMARK_ERROR_LIMIT_EXCEEDED);
    NAOS_CHECK_NULL(g_doc);
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
}

/*
 * "@note: " depth times and text, blanks blank lines, then 2 spaces depth
 * times and text: the last line goes on in each note.
 */
static void build_notes(size_t depth, size_t blanks)
{
    input_begin(7 * depth + 2 + blanks + 2 * depth + 2);
    REPEAT("@note: ", depth);
    REPEAT("a\n", 1);
    REPEAT("\n", blanks);
    REPEAT("  ", depth);
    REPEAT("b\n", 1);
}

NAOS_TEST(notes_nested_100000_deep_then_100000_blank_lines)
{
    call_bounds_t bounds = {DEEP + 2, DEEP, DEEP, 0, 2, 0};

    /* The root, DEEP notes, and in the last two paragraphs and texts. */
    build_notes(DEEP, DEEP);
    parse_deep(DEEP + 2);
    NAOS_CHECK_UINT_EQ(count_kind(AMARK_TEST_KIND_NOTE), DEEP);
    NAOS_CHECK_UINT_EQ(count_kind(AMARK_KIND_PARAGRAPH), 2);
    check_calls(&bounds);
    /* A blank line calls no continue rule; the last line calls each. */
    NAOS_CHECK_UINT_EQ(g_last_counts.block_continues, DEEP);
    NAOS_CHECK_UINT_EQ(g_last_counts.paragraphs, 2);
    NAOS_CHECK_UINT_GE(g_last_counts.block_starts, DEEP);
}

/*
 * A paragraph of the input: requires the parse to succeed with a tree whose
 * deepest node is at depth and that holds strikes STRIKE nodes, and the
 * rule calls of one paragraph of one line with triggers '$'.
 */
static void check_tilde_paragraph(size_t depth, size_t strikes,
                                  size_t triggers)
{
    call_bounds_t bounds = {1, 0, 0, 0, 1, 0};

    bounds.triggers = triggers;
    parse_deep(depth);
    NAOS_CHECK_UINT_EQ(count_kind(AMARK_KIND_PARAGRAPH), 1);
    NAOS_CHECK_UINT_EQ(count_kind(AMARK_TEST_KIND_STRIKE), strikes);
    NAOS_CHECK_UINT_EQ(count_kind(AMARK_TEST_KIND_VAR), 0);
    check_calls(&bounds);
    NAOS_CHECK_UINT_EQ(g_last_counts.inlines, triggers);
    NAOS_CHECK_UINT_EQ(g_last_counts.paragraphs, 1);
}

/* The patterns of '~', each with n repeats of its units. */
static void build_tilde_run(size_t n)
{
    /*
     * One run of n, which can open and close: a length the kind does not
     * use. The "a" before it keeps it from opening a fenced code block.
     */
    input_begin(n + 3);
    REPEAT("a", 1);
    REPEAT("~", n);
    REPEAT("a\n", 1);
}

static void build_tilde_lengths(size_t n)
{
    /* n openers of length 1, then n closers of length 2. */
    input_begin(7 * n + 1);
    REPEAT("~a ", n);
    REPEAT("a~~ ", n);
    REPEAT("\n", 1);
}

static void build_tilde_unused(size_t n)
{
    /* n openers of length 3, which the kind does not use, then n closers
       of length 1; the "a " first keeps the first from opening a fenced
       code block. */
    input_begin(8 * n + 3);
    REPEAT("a ", 1);
    REPEAT("~~~a ", n);
    REPEAT("a~ ", n);
    REPEAT("\n", 1);
}

static void build_tilde_mixed(size_t n)
{
    /*
     * n pairs of openers of lengths 1 and 3, then n pairs of closers of
     * lengths 2 and 1: each closer of length 2 finds no opener, each of
     * length 1 the nearest opener of length 1 past one of length 3.
     */
    input_begin(15 * n + 1);
    REPEAT("~a ~~~a ", n);
    REPEAT("a~~ a~ ", n);
    REPEAT("\n", 1);
}

static void build_tilde_triggers(size_t n)
{
    /* n triggers of the inline rule that start no VAR, and n openers. */
    input_begin(6 * n + 1);
    REPEAT("$a ~a ", n);
    REPEAT("\n", 1);
}

NAOS_TEST(long_runs_of_tildes)
{
    /* The root, the paragraph and its text. */
    build_tilde_run(10 * DEEP);
    check_tilde_paragraph(2, 0, 0);
    build_tilde_lengths(DEEP);
    check_tilde_paragraph(2, 0, 0);
    build_tilde_unused(DEEP);
    check_tilde_paragraph(2, 0, 0);
    /* DEEP strikes, each in the one after it, and their texts. */
    build_tilde_mixed(DEEP);
    check_tilde_paragraph(DEEP + 2, DEEP, 0);
    build_tilde_triggers(DEEP);
    check_tilde_paragraph(2, 0, DEEP);
}

/* A pattern of size n and its sizes for the timing. */
typedef struct timed_case {
    const char *name;
    void (*build)(size_t n);
    size_t small;
    size_t large;
} timed_case_t;

static void build_boxes_square(size_t n)
{
    build_boxes(n);
}

static void build_notes_square(size_t n)
{
    /* n notes and n blank lines: n times n without the cutoff. */
    build_notes(n, n);
}

static const timed_case_t k_timed_cases[] = {
    {"boxes", build_boxes_square, DEEP / 32, DEEP},
    {"notes and blank lines", build_notes_square, 1000, 32000},
    {"a run of tildes", build_tilde_run, DEEP / 32, DEEP},
    {"tildes of lengths that differ", build_tilde_lengths, DEEP / 32, DEEP},
    {"tildes of an unused length", build_tilde_unused, DEEP / 32, DEEP},
    {"tildes of mixed lengths", build_tilde_mixed, DEEP / 32, DEEP},
    {"triggers and tildes", build_tilde_triggers, DEEP / 32, DEEP},
};

/*
 * Parses the input and destroys the document, again and again until the
 * clock has advanced by at least min_ticks (once at least). Returns the
 * processor time of one parse, in microseconds, or 0 where it cannot be
 * read.
 */
static unsigned long timed_parses(clock_t min_ticks)
{
    size_t count = 0;
    clock_t start;
    clock_t end;

    start = clock();
    do {
        NAOS_REQUIRE_INT_EQ(parse_measured(0, 0), AMARK_ERROR_NONE);
        amark_document_destroy(g_doc);
        g_doc = NULL;
        count++;
        end = clock();
    } while (start != (clock_t)-1 && end != (clock_t)-1 && end >= start &&
             end - start < min_ticks);
    if (start == (clock_t)-1 || end == (clock_t)-1 || end < start) {
        return 0;
    }
    return (unsigned long)((double)(end - start) * 1e6 / CLOCKS_PER_SEC /
                           (double)count);
}

NAOS_TEST(pathological_inputs_take_linear_time)
{
    size_t i;

    for (i = 0; i < sizeof k_timed_cases / sizeof k_timed_cases[0]; i++) {
        const timed_case_t *c = &k_timed_cases[i];
        size_t small_length;
        size_t ratio;
        unsigned long small_time;
        unsigned long large_time;
        unsigned long again;

        NAOS_TRACE_PUSH("%s", c->name);
        c->build(c->small);
        small_length = g_length;
        small_time = timed_parses(CLOCKS_PER_SEC / 20);
        c->build(c->large);
        ratio = (g_length + small_length / 2) / small_length;
        NAOS_REQUIRE_UINT_GE(ratio, 16);
        large_time = timed_parses(0);
        again = timed_parses(0);
        if (again < large_time) {
            large_time = again;
        }
        NAOS_TRACE_PUSH("lengths %lu and %lu, times %lu us and %lu us",
                        (unsigned long)small_length,
                        (unsigned long)g_length, small_time, large_time);
        NAOS_CHECK_UINT_LE(large_time,
                           TIME_FACTOR * ratio * small_time + TIME_SLACK);
        NAOS_TRACE_POP();
        NAOS_TRACE_POP();
    }
}

NAOS_SUITE(pathological_inputs,
    NAOS_SET_SETUP(counted_states),
    NAOS_SET_TEARDOWN(measured),
    NAOS_ADD_TEST(boxes_nested_100000_deep),
    NAOS_ADD_TEST(notes_nested_100000_deep_then_100000_blank_lines),
    NAOS_ADD_TEST(long_runs_of_tildes),
    NAOS_ADD_TEST(pathological_inputs_take_linear_time)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(allocation_baseline),
    NAOS_ADD_SUITE(dialects_without_effect),
    NAOS_ADD_SUITE(failed_allocations),
    NAOS_ADD_SUITE(repeated_parses),
    NAOS_ADD_SUITE(resource_limits),
    NAOS_ADD_SUITE(pathological_inputs)
)
