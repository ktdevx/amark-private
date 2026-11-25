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
 * (Requirements 13.3 and 15.6).
 */

#include "document.h"
#include "test_alloc.h"
#include "test_dialect.h"
#include "tree_check.h"
#include "tree_dump.h"

#include <amark/amark.h>

#include <naos/naos.h>

#include <stddef.h>

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
 * Parses content without a dialect, requires success and a tree that passes
 * the check, and returns the number of calls made to the allocator.
 */
static size_t count_parse(const char *content, size_t length)
{
    amark_tree_check_result_t check;

    amark_test_alloc_init(&g_state);
    NAOS_REQUIRE_NULL(g_config.dialect);
    NAOS_REQUIRE_INT_EQ(amark_parse(content, length, &g_config, &g_doc),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_NOT_NULL(g_doc);
    check = amark_tree_check(g_doc);
    NAOS_CHECK_INT_EQ(check.violation, AMARK_TREE_VIOLATION_NONE);
    return g_state.malloc_calls;
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

NAOS_TEST(a_dialect_of_no_extensions_gives_the_same_tree)
{
    check_same_tree(amark_test_dialect_empty(), S(""));
    check_same_tree(amark_test_dialect_empty(), S(k_paragraph));
    check_same_tree(amark_test_dialect_empty(), S(k_nested));
    check_same_tree(amark_test_dialect_empty(), S(k_markers));
}

NAOS_TEST(a_dialect_whose_rules_say_no_gives_the_same_tree)
{
    check_same_tree(amark_test_dialect_plain(), S(""));
    check_same_tree(amark_test_dialect_plain(), S(k_paragraph));
    check_same_tree(amark_test_dialect_plain(), S(k_nested));
    check_same_tree(amark_test_dialect_plain(), S(k_markers));
}

NAOS_TEST(the_test_dialect_gives_the_same_tree_without_its_syntax)
{
    check_same_tree(amark_test_dialect(), S(""));
    check_same_tree(amark_test_dialect(), S(k_paragraph));
    check_same_tree(amark_test_dialect(), S(k_nested));
}

NAOS_SUITE(dialects_without_effect,
    NAOS_SET_SETUP(counting),
    NAOS_SET_TEARDOWN(counting),
    NAOS_ADD_TEST(the_test_dialects_pass_the_configuration_check),
    NAOS_ADD_TEST(the_test_dialect_registers_every_kind),
    NAOS_ADD_TEST(a_dialect_of_no_extensions_gives_the_same_tree),
    NAOS_ADD_TEST(a_dialect_whose_rules_say_no_gives_the_same_tree),
    NAOS_ADD_TEST(the_test_dialect_gives_the_same_tree_without_its_syntax)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(allocation_baseline),
    NAOS_ADD_SUITE(dialects_without_effect)
)
