/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of the inline hooks of a dialect: the inline rules called at
 * their trigger bytes (7.2 to 7.11). The suite var parses with the VAR rule
 * of the test dialect: its kind, range and attributes, where it is made
 * (inside link text, emphasis and headings) and where it is not (code
 * spans, autolinks, raw HTML, link destinations and titles, escaped
 * bytes), a VAR that ends on the next line, and each invalid result
 * (ORIGIN, EMPTY, PARENT, PREFIX, STATUS), which is not in the tree, adds
 * one diagnostic and leaves the bytes as a parse without the dialect has
 * them (10.4, 15.3, 15.4); a rule that returns AMARK_ERROR_NOMEM ends the
 * parse (10.10). The suite strike parses with the STRIKE delimiter kind
 * of the test dialect: runs of a registered length match runs of the same
 * length, other runs are text, and the runs open, close and nest with
 * emphasis and links as runs of emphasis do (8.2 to 8.5, 15.11). The suite
 * order parses with dialects of this file: the order of the extensions and
 * of their rules, and a rejected rule followed by the next one. The suite
 * count runs the parsers with a run state built here, to read the test dialect's count of the calls of its inline rule:
 * at most one per trigger position (7.11).
 */

#include "arena.h"
#include "block.h"
#include "config_internal.h"
#include "dialect_edit.h"
#include "dialect_internal.h"
#include "document.h"
#include "inline.h"
#include "parse_data.h"
#include "test_alloc.h"
#include "test_dialect.h"
#include "tree_check.h"
#include "tree_dump.h"

#include <amark/amark.h>
#include <amark/dialect.h>

#include <naos/naos.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define K_VAR AMARK_TEST_KIND_VAR
#define K_STRIKE AMARK_TEST_KIND_STRIKE
#define DUMP_SIZE 2048

static amark_test_alloc_t g_state;
static amark_config_t g_config;
static amark_document_t *g_doc;
static char g_dump[DUMP_SIZE];
static char g_plain_dump[DUMP_SIZE];

NAOS_SETUP(parse)
{
    amark_test_alloc_init(&g_state);
    amark_config_init(&g_config);
    g_config.allocator = amark_test_allocator(&g_state);
    g_doc = NULL;
}

NAOS_TEARDOWN(parse)
{
    g_state.fail_at = 0;
    amark_document_destroy(g_doc);
    g_doc = NULL;
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
    NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
}

static void check_tree(void)
{
    amark_tree_check_result_t check = amark_tree_check(g_doc);

    NAOS_CHECK_INT_EQ(check.violation, AMARK_TREE_VIOLATION_NONE);
    NAOS_CHECK_UINT_EQ(check.node, AMARK_NODE_NONE);
}

/*
 * Parses length bytes of content with dialect, requires success and a tree
 * that passes the check, requires diagnostics diagnostics, and writes the
 * dump of the whole tree to g_dump.
 */
static void parse_with(const amark_dialect_t *dialect, const char *content,
                       size_t length, size_t diagnostics)
{
    amark_document_destroy(g_doc);
    g_doc = NULL;
    g_config.dialect = dialect;
    NAOS_REQUIRE_INT_EQ(amark_parse(content, length, &g_config, &g_doc),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_NOT_NULL(g_doc);
    check_tree();
    NAOS_CHECK_UINT_EQ(amark_document_diagnostic_count(g_doc), diagnostics);
    NAOS_REQUIRE_UINT_LT(amark_test_dump_tree(g_doc, g_dump, DUMP_SIZE),
                         DUMP_SIZE);
}

#define PARSE(dialect, s, diagnostics) \
    parse_with((dialect), (s), sizeof(s) - 1, (diagnostics))

/*
 * Parses content without a dialect and then with the test dialect, which
 * must add diagnostics diagnostics, and requires both trees to be dumped
 * the same: the test dialect made no node, and its trigger bytes are what
 * they are without it. g_doc is the parse with the dialect.
 */
static void same_as_without(const char *content, size_t length,
                            size_t diagnostics)
{
    parse_with(NULL, content, length, 0);
    memcpy(g_plain_dump, g_dump, DUMP_SIZE);
    parse_with(amark_test_dialect(), content, length, diagnostics);
    NAOS_CHECK_STR_EQ(g_dump, g_plain_dump);
}

#define SAME_AS_WITHOUT(s, diagnostics) \
    same_as_without((s), sizeof(s) - 1, (diagnostics))

/*
 * Returns the node after node in document order (its first child, or the
 * next sibling of it or of its nearest ancestor that has one), or
 * AMARK_NODE_NONE after the last.
 */
static amark_node_id_t next_in_order(amark_node_id_t node)
{
    amark_node_id_t child = amark_node_first_child(g_doc, node);

    if (child != AMARK_NODE_NONE) {
        return child;
    }
    while (node != AMARK_NODE_NONE &&
           amark_node_next_sibling(g_doc, node) == AMARK_NODE_NONE) {
        node = amark_node_parent(g_doc, node);
    }
    return node == AMARK_NODE_NONE ? AMARK_NODE_NONE
                                   : amark_node_next_sibling(g_doc, node);
}

/* Returns how many nodes of kind g_doc has. */
static size_t count_kind(amark_kind_t kind)
{
    size_t count = 0;
    amark_node_id_t node = amark_document_root(g_doc);

    while (node != AMARK_NODE_NONE) {
        if (amark_node_kind(g_doc, node) == kind) {
            count++;
        }
        node = next_in_order(node);
    }
    return count;
}

/* Returns node number index (from 0) of kind in document order. */
static amark_node_id_t find_kind(amark_kind_t kind, size_t index)
{
    amark_node_id_t node = amark_document_root(g_doc);

    while (node != AMARK_NODE_NONE) {
        if (amark_node_kind(g_doc, node) == kind) {
            if (index == 0) {
                return node;
            }
            index--;
        }
        node = next_in_order(node);
    }
    NAOS_REQUIRE_TRUE(false);
    return AMARK_NODE_NONE;
}

/*
 * Checks node: an inline of kind with no children over [start, end), whose
 * parent is of parent_kind and whose attributes are data (NULL: none).
 */
static void check_inline(amark_node_id_t node, amark_kind_t kind,
                         size_t start, size_t end, amark_kind_t parent_kind,
                         const char *data)
{
    amark_range_t range;
    const void *bytes;
    size_t size = 99;

    NAOS_CHECK_UINT_EQ(amark_node_kind(g_doc, node), kind);
    NAOS_CHECK_FALSE(amark_node_is_block(g_doc, node));
    NAOS_REQUIRE_TRUE(amark_node_range(g_doc, node, &range));
    NAOS_CHECK_UINT_EQ(range.start, start);
    NAOS_CHECK_UINT_EQ(range.end, end);
    NAOS_CHECK_UINT_EQ(amark_node_first_child(g_doc, node), AMARK_NODE_NONE);
    NAOS_CHECK_UINT_EQ(amark_node_kind(g_doc, amark_node_parent(g_doc, node)),
                       parent_kind);
    bytes = amark_node_dialect_data(g_doc, node, &size);
    if (data == NULL) {
        NAOS_CHECK_NULL(bytes);
        NAOS_CHECK_UINT_EQ(size, 0);
    } else {
        NAOS_REQUIRE_NOT_NULL(bytes);
        NAOS_REQUIRE_UINT_EQ(size, strlen(data));
        NAOS_CHECK_TRUE(memcmp(bytes, data, size) == 0);
    }
}

/* Checks VAR number index of g_doc, as check_inline. */
static void check_var(size_t index, size_t start, size_t end,
                      amark_kind_t parent_kind, const char *data)
{
    check_inline(find_kind(K_VAR, index), K_VAR, start, end, parent_kind,
                 data);
}

/* The diagnostic number index of g_doc is an inline one of name. */
static void check_inline_diagnostic(size_t index, const char *name,
                                    const char *reason, size_t offset,
                                    size_t line)
{
    char expected[160];
    amark_diagnostic_t diag;

    NAOS_REQUIRE_TRUE(amark_document_diagnostic(g_doc, index, &diag));
    (void)strcpy(expected, "dialect \"");
    (void)strcat(expected, name);
    (void)strcat(expected, "\": inline: ");
    (void)strcat(expected, reason);
    NAOS_CHECK_STR_EQ(diag.message, expected);
    NAOS_CHECK_UINT_EQ(diag.offset, offset);
    NAOS_CHECK_UINT_EQ(diag.line, line);
    NAOS_CHECK_INT_EQ(diag.severity, AMARK_SEVERITY_ERROR);
}

/* ---------------------------------------------------------------------- */
/* VAR */

NAOS_TEST(var_is_a_childless_inline_with_its_name_length)
{
    PARSE(amark_test_dialect(), "a $ab$ b\n", 0);
    NAOS_CHECK_STR_EQ(g_dump, "(doc (p (text \"a \") (?) (text \" b\")))");
    NAOS_CHECK_UINT_EQ(count_kind(K_VAR), 1);
    check_var(0, 2, 6, AMARK_KIND_PARAGRAPH, "2");
    NAOS_CHECK_TRUE(amark_kind_is_dialect(K_VAR));
}

NAOS_TEST(var_attributes_are_copied_for_each_var)
{
    PARSE(amark_test_dialect(), "$abcdefghijkl$ $x$$yz$\n", 0);
    NAOS_CHECK_STR_EQ(g_dump, "(doc (p (?) (text \" \") (?) (?)))");
    NAOS_CHECK_UINT_EQ(count_kind(K_VAR), 3);
    check_var(0, 0, 14, AMARK_KIND_PARAGRAPH, "12");
    check_var(1, 15, 18, AMARK_KIND_PARAGRAPH, "1");
    check_var(2, 18, 22, AMARK_KIND_PARAGRAPH, "2");
}

NAOS_TEST(var_is_made_in_headings_and_emphasis)
{
    PARSE(amark_test_dialect(), "# $a$\n\n*$b$*\n", 0);
    NAOS_CHECK_STR_EQ(g_dump, "(doc (h 1 atx (?)) (p (em (?))))");
    check_var(0, 2, 5, AMARK_KIND_HEADING, "1");
    check_var(1, 8, 11, AMARK_KIND_EMPHASIS, "1");
}

NAOS_TEST(var_inside_link_text_is_a_child_of_the_link)
{
    PARSE(amark_test_dialect(), "[x $a$](u)\n", 0);
    NAOS_CHECK_STR_EQ(g_dump,
                      "(doc (p (link inline dest=\"u\" (text \"x \") (?))))");
    check_var(0, 3, 6, AMARK_KIND_LINK, "1");
    PARSE(amark_test_dialect(), "[$a$]\n\n[$a$]: /u\n", 0);
    NAOS_CHECK_STR_EQ(g_dump, "(doc (p (link shortcut def=\"$a$\" (?))) "
                              "(def))");
    check_var(0, 1, 4, AMARK_KIND_LINK, "1");
}

NAOS_TEST(var_is_not_made_inside_code_spans_autolinks_or_raw_html)
{
    SAME_AS_WITHOUT("`$a$` <http://x/$a$> <span title=\"$a$\">\n", 0);
    NAOS_CHECK_UINT_EQ(count_kind(K_VAR), 0);
}

NAOS_TEST(var_is_not_made_inside_link_destinations_or_titles)
{
    SAME_AS_WITHOUT("[x]($a$ \"$b$\") [y](<$c$>)\n", 0);
    NAOS_CHECK_UINT_EQ(count_kind(K_VAR), 0);
}

NAOS_TEST(var_is_not_made_at_an_escaped_trigger)
{
    SAME_AS_WITHOUT("\\$a$\n", 0);
    NAOS_CHECK_UINT_EQ(count_kind(K_VAR), 0);
    /* The escape is "\$"; the '$' after "a" starts "$b$". */
    PARSE(amark_test_dialect(), "\\$a$b$\n", 0);
    NAOS_CHECK_STR_EQ(g_dump, "(doc (p (esc \"\\\\$\") (text \"a\") (?)))");
    check_var(0, 3, 6, AMARK_KIND_PARAGRAPH, "1");
}

NAOS_TEST(trigger_without_a_var_is_text_as_without_the_dialect)
{
    SAME_AS_WITHOUT("$ $$ a$ $b c$\n$\n", 0);
    NAOS_CHECK_UINT_EQ(count_kind(K_VAR), 0);
}

NAOS_TEST(var_may_end_inside_the_content_of_the_next_line)
{
    /*
     * "> $!inline-next" is 0-14 (LF at 15), "> bc" 16-19: the content of
     * line 2 starts at 18, so the VAR is [2, 19) and "c" follows it.
     */
    PARSE(amark_test_dialect(), "> $!inline-next\n> bc\n", 0);
    NAOS_CHECK_STR_EQ(g_dump, "(doc (bq (p (?) (text \"c\"))))");
    check_var(0, 2, 19, AMARK_KIND_PARAGRAPH, "12");
    /* Without a prefix: the line ending and the leading spaces. */
    PARSE(amark_test_dialect(), "a $!inline-next\n  bc $d$\n", 0);
    NAOS_CHECK_STR_EQ(g_dump,
                      "(doc (p (text \"a \") (?) (text \"c \") (?)))");
    check_var(0, 2, 19, AMARK_KIND_PARAGRAPH, "12");
    check_var(1, 21, 24, AMARK_KIND_PARAGRAPH, "1");
}

NAOS_TEST(var_on_the_last_line_has_no_next_line)
{
    SAME_AS_WITHOUT("a $!inline-next\n\nb\n", 0);
    NAOS_CHECK_UINT_EQ(count_kind(K_VAR), 0);
}

/* ---------------------------------------------------------------------- */
/* Invalid results */

NAOS_TEST(invalid_result_origin)
{
    SAME_AS_WITHOUT("a $!inline-origin$ b\n", 1);
    NAOS_CHECK_UINT_EQ(count_kind(K_VAR), 0);
    check_inline_diagnostic(0, "test", "range does not start at the position",
                            2, 1);
}

NAOS_TEST(invalid_result_empty)
{
    SAME_AS_WITHOUT("a\n$!inline-empty$ b\n", 1);
    NAOS_CHECK_UINT_EQ(count_kind(K_VAR), 0);
    check_inline_diagnostic(0, "test", "empty range", 2, 2);
}

NAOS_TEST(invalid_result_outside_the_inline_content)
{
    /* Past the inline content, inside the content (its line ending). */
    SAME_AS_WITHOUT("a $!inline-parent$\n", 1);
    NAOS_CHECK_UINT_EQ(count_kind(K_VAR), 0);
    check_inline_diagnostic(0, "test", "range leaves the parent", 2, 1);
    /* Past the content itself. */
    SAME_AS_WITHOUT("a $!inline-parent$", 1);
    check_inline_diagnostic(0, "test", "range leaves the parent", 2, 1);
    /* Past a heading's content, into the trailing closing sequence. */
    SAME_AS_WITHOUT("# $!inline-parent$ #\n", 1);
    check_inline_diagnostic(0, "test", "range leaves the parent", 2, 1);
}

NAOS_TEST(invalid_result_ends_inside_a_prefix)
{
    /*
     * "> $!inline-prefix" is 0-16 (LF at 17); the content of "> b" starts
     * at 20, so the range ends at 19, after the '>' of the prefix.
     */
    SAME_AS_WITHOUT("> $!inline-prefix\n> b\n", 1);
    NAOS_CHECK_UINT_EQ(count_kind(K_VAR), 0);
    check_inline_diagnostic(0, "test", "range ends inside a line prefix", 2,
                            1);
    /* Inside the leading white space of a continuation line. */
    SAME_AS_WITHOUT("a\n$!inline-prefix\n   b\n", 1);
    check_inline_diagnostic(0, "test", "range ends inside a line prefix", 2,
                            2);
}

NAOS_TEST(invalid_result_undefined_status)
{
    SAME_AS_WITHOUT("a $!inline-status$ b\n", 1);
    NAOS_CHECK_UINT_EQ(count_kind(K_VAR), 0);
    check_inline_diagnostic(0, "test", "unknown return value", 2, 1);
}

NAOS_TEST(invalid_results_leave_later_vars_alone)
{
    PARSE(amark_test_dialect(),
          "$!inline-empty$ $a$\n> $!inline-origin$ $!inline-prefix\n> b\n",
          3);
    NAOS_CHECK_UINT_EQ(count_kind(K_VAR), 1);
    check_var(0, 16, 19, AMARK_KIND_PARAGRAPH, "1");
    check_inline_diagnostic(0, "test", "empty range", 0, 1);
    check_inline_diagnostic(1, "test", "range does not start at the position",
                            22, 2);
    check_inline_diagnostic(2, "test", "range ends inside a line prefix", 39,
                            2);
}

NAOS_TEST(rule_nomem_ends_the_parse)
{
    static const char content[] = "a $b$ $!inline-nomem$ c\n";

    g_config.dialect = amark_test_dialect();
    NAOS_CHECK_INT_EQ(amark_parse(content, sizeof(content) - 1, &g_config,
                                  &g_doc),
                      AMARK_ERROR_NOMEM);
    NAOS_CHECK_NULL(g_doc);
}

NAOS_TEST(failed_allocations_end_the_parse_without_leaks)
{
    static const char content[] =
        "# $a$\n\n> [$bc$](u) *$d$*\n> $!inline-next\n> x $!inline-empty$\n";
    size_t calls;
    size_t i;

    PARSE(amark_test_dialect(), content, 1);
    NAOS_CHECK_UINT_EQ(count_kind(K_VAR), 4);
    calls = g_state.malloc_calls;
    amark_document_destroy(g_doc);
    g_doc = NULL;
    for (i = 1; i <= calls; i++) {
        amark_test_alloc_init(&g_state);
        g_state.fail_at = i;
        NAOS_CHECK_INT_EQ(amark_parse(content, sizeof(content) - 1,
                                      &g_config, &g_doc),
                          AMARK_ERROR_NOMEM);
        NAOS_CHECK_NULL(g_doc);
        NAOS_CHECK_UINT_EQ(g_state.live, 0);
    }
    g_state.fail_at = 0;
}

NAOS_TEST(rules_that_say_no_leave_the_tree_as_without_a_dialect)
{
    PARSE(amark_test_dialect_plain(), "a $b$ *c* @d ~e~\n", 0);
    memcpy(g_plain_dump, g_dump, DUMP_SIZE);
    PARSE(NULL, "a $b$ *c* @d ~e~\n", 0);
    NAOS_CHECK_STR_EQ(g_dump, g_plain_dump);
    NAOS_CHECK_STR_EQ(g_dump, "(doc (p (text \"a $b$ \") (em (text \"c\")) "
                              "(text \" @d ~e~\")))");
}

NAOS_SUITE(var,
    NAOS_SET_SETUP(parse),
    NAOS_SET_TEARDOWN(parse),
    NAOS_ADD_TEST(var_is_a_childless_inline_with_its_name_length),
    NAOS_ADD_TEST(var_attributes_are_copied_for_each_var),
    NAOS_ADD_TEST(var_is_made_in_headings_and_emphasis),
    NAOS_ADD_TEST(var_inside_link_text_is_a_child_of_the_link),
    NAOS_ADD_TEST(var_is_not_made_inside_code_spans_autolinks_or_raw_html),
    NAOS_ADD_TEST(var_is_not_made_inside_link_destinations_or_titles),
    NAOS_ADD_TEST(var_is_not_made_at_an_escaped_trigger),
    NAOS_ADD_TEST(trigger_without_a_var_is_text_as_without_the_dialect),
    NAOS_ADD_TEST(var_may_end_inside_the_content_of_the_next_line),
    NAOS_ADD_TEST(var_on_the_last_line_has_no_next_line),
    NAOS_ADD_TEST(invalid_result_origin),
    NAOS_ADD_TEST(invalid_result_empty),
    NAOS_ADD_TEST(invalid_result_outside_the_inline_content),
    NAOS_ADD_TEST(invalid_result_ends_inside_a_prefix),
    NAOS_ADD_TEST(invalid_result_undefined_status),
    NAOS_ADD_TEST(invalid_results_leave_later_vars_alone),
    NAOS_ADD_TEST(rule_nomem_ends_the_parse),
    NAOS_ADD_TEST(failed_allocations_end_the_parse_without_leaks),
    NAOS_ADD_TEST(rules_that_say_no_leave_the_tree_as_without_a_dialect)
)

/* ---------------------------------------------------------------------- */
/* STRIKE: the delimiter kind '~' of lengths 1 and 2 */

/*
 * Checks STRIKE number index of g_doc: over [start, end), with children,
 * whose parent is of parent_kind and which has no attributes.
 */
static void check_strike(size_t index, size_t start, size_t end,
                         amark_kind_t parent_kind)
{
    amark_node_id_t node = find_kind(K_STRIKE, index);
    amark_range_t range;
    size_t size = 99;

    NAOS_CHECK_FALSE(amark_node_is_block(g_doc, node));
    NAOS_REQUIRE_TRUE(amark_node_range(g_doc, node, &range));
    NAOS_CHECK_UINT_EQ(range.start, start);
    NAOS_CHECK_UINT_EQ(range.end, end);
    NAOS_CHECK_UINT_NE(amark_node_first_child(g_doc, node), AMARK_NODE_NONE);
    NAOS_CHECK_UINT_EQ(amark_node_kind(g_doc, amark_node_parent(g_doc, node)),
                       parent_kind);
    NAOS_CHECK_NULL(amark_node_dialect_data(g_doc, node, &size));
    NAOS_CHECK_UINT_EQ(size, 0);
}

NAOS_TEST(strike_runs_of_a_registered_length_match)
{
    PARSE(amark_test_dialect(), "~a~\n", 0);
    NAOS_CHECK_STR_EQ(g_dump, "(doc (p (? (text \"a\"))))");
    NAOS_CHECK_UINT_EQ(count_kind(K_STRIKE), 1);
    check_strike(0, 0, 3, AMARK_KIND_PARAGRAPH);
    PARSE(amark_test_dialect(), "~~a~~\n", 0);
    NAOS_CHECK_STR_EQ(g_dump, "(doc (p (? (text \"a\"))))");
    NAOS_CHECK_UINT_EQ(count_kind(K_STRIKE), 1);
    check_strike(0, 0, 5, AMARK_KIND_PARAGRAPH);
    PARSE(amark_test_dialect(), "x ~a b~ y ~~c~~\n", 0);
    NAOS_CHECK_STR_EQ(g_dump, "(doc (p (text \"x \") (? (text \"a b\")) "
                              "(text \" y \") (? (text \"c\"))))");
    check_strike(0, 2, 7, AMARK_KIND_PARAGRAPH);
    check_strike(1, 10, 15, AMARK_KIND_PARAGRAPH);
}

NAOS_TEST(strike_opens_and_closes_as_a_star_does)
{
    /* Inside a word, as '*' and unlike '_'. */
    PARSE(amark_test_dialect(), "a~b~c\n", 0);
    NAOS_CHECK_STR_EQ(g_dump,
                      "(doc (p (text \"a\") (? (text \"b\")) (text \"c\")))");
    check_strike(0, 1, 4, AMARK_KIND_PARAGRAPH);
    /* Not left-flanking, not right-flanking. */
    SAME_AS_WITHOUT("~ a~ ~a ~ a ~ b\n", 0);
    NAOS_CHECK_UINT_EQ(count_kind(K_STRIKE), 0);
}

NAOS_TEST(strike_runs_of_other_lengths_are_text)
{
    SAME_AS_WITHOUT("~a~~\n", 0);
    NAOS_CHECK_UINT_EQ(count_kind(K_STRIKE), 0);
    SAME_AS_WITHOUT("~~~a~~~\n", 0);
    NAOS_CHECK_UINT_EQ(count_kind(K_STRIKE), 0);
    SAME_AS_WITHOUT("~~a~ b~ ~~~~c~~~~\n", 0);
    NAOS_CHECK_UINT_EQ(count_kind(K_STRIKE), 0);
    /* A run of length 3 in between is text and does not stop a match. */
    PARSE(amark_test_dialect(), "~a~~~b~\n", 0);
    NAOS_CHECK_STR_EQ(g_dump, "(doc (p (? (text \"a~~~b\"))))");
    check_strike(0, 0, 7, AMARK_KIND_PARAGRAPH);
}

NAOS_TEST(strike_nests_with_emphasis_and_links)
{
    PARSE(amark_test_dialect(), "*~a~*\n", 0);
    NAOS_CHECK_STR_EQ(g_dump, "(doc (p (em (? (text \"a\")))))");
    check_strike(0, 1, 4, AMARK_KIND_EMPHASIS);
    PARSE(amark_test_dialect(), "~*a*~\n", 0);
    NAOS_CHECK_STR_EQ(g_dump, "(doc (p (? (em (text \"a\")))))");
    check_strike(0, 0, 5, AMARK_KIND_PARAGRAPH);
    PARSE(amark_test_dialect(), "[~a~](u)\n", 0);
    NAOS_CHECK_STR_EQ(g_dump,
                      "(doc (p (link inline dest=\"u\" (? (text \"a\")))))");
    check_strike(0, 1, 4, AMARK_KIND_LINK);
    PARSE(amark_test_dialect(), "~~a ~$b$~ **c**~~\n", 0);
    NAOS_CHECK_STR_EQ(g_dump, "(doc (p (? (text \"a \") (? (?)) (text \" \") "
                              "(strong (text \"c\")))))");
    check_strike(0, 0, 17, AMARK_KIND_PARAGRAPH);
    check_strike(1, 4, 9, K_STRIKE);
    check_var(0, 5, 8, K_STRIKE, "1");
}

NAOS_TEST(strike_overlapping_emphasis_follows_the_rules_of_emphasis)
{
    /*
     * As "*a _b* c_": the closer that comes first wins, and the runs
     * between its pair are text.
     */
    PARSE(NULL, "*a _b* c_\n", 0);
    NAOS_CHECK_STR_EQ(g_dump,
                      "(doc (p (em (text \"a _b\")) (text \" c_\")))");
    SAME_AS_WITHOUT("*a ~b* c~\n", 0);
    NAOS_CHECK_STR_EQ(g_dump,
                      "(doc (p (em (text \"a ~b\")) (text \" c~\")))");
    NAOS_CHECK_UINT_EQ(count_kind(K_STRIKE), 0);
    PARSE(NULL, "_a *b_ c*\n", 0);
    NAOS_CHECK_STR_EQ(g_dump,
                      "(doc (p (em (text \"a *b\")) (text \" c*\")))");
    PARSE(amark_test_dialect(), "~a *b~ c*\n", 0);
    NAOS_CHECK_STR_EQ(g_dump,
                      "(doc (p (? (text \"a *b\")) (text \" c*\")))");
    check_strike(0, 0, 6, AMARK_KIND_PARAGRAPH);
    /* A link closes first, and the strike run inside it is text. */
    PARSE(amark_test_dialect(), "~a [b~](u)\n", 0);
    NAOS_CHECK_STR_EQ(g_dump, "(doc (p (text \"~a \") "
                              "(link inline dest=\"u\" (text \"b~\"))))");
    NAOS_CHECK_UINT_EQ(count_kind(K_STRIKE), 0);
}

NAOS_TEST(strike_is_not_made_inside_code_spans_or_at_escapes)
{
    SAME_AS_WITHOUT("`~a~` \\~a~ <http://x/~a~>\n", 0);
    NAOS_CHECK_UINT_EQ(count_kind(K_STRIKE), 0);
}

NAOS_TEST(strike_failed_allocations_end_the_parse_without_leaks)
{
    static const char content[] = "~a~ *~~b~~* [~c~](u) ~d~~ ~~~e~~~\n";
    size_t calls;
    size_t i;

    PARSE(amark_test_dialect(), content, 0);
    NAOS_CHECK_UINT_EQ(count_kind(K_STRIKE), 3);
    calls = g_state.malloc_calls;
    amark_document_destroy(g_doc);
    g_doc = NULL;
    for (i = 1; i <= calls; i++) {
        amark_test_alloc_init(&g_state);
        g_state.fail_at = i;
        NAOS_CHECK_INT_EQ(amark_parse(content, sizeof(content) - 1,
                                      &g_config, &g_doc),
                          AMARK_ERROR_NOMEM);
        NAOS_CHECK_NULL(g_doc);
        NAOS_CHECK_UINT_EQ(g_state.live, 0);
    }
    g_state.fail_at = 0;
}

NAOS_SUITE(strike,
    NAOS_SET_SETUP(parse),
    NAOS_SET_TEARDOWN(parse),
    NAOS_ADD_TEST(strike_runs_of_a_registered_length_match),
    NAOS_ADD_TEST(strike_opens_and_closes_as_a_star_does),
    NAOS_ADD_TEST(strike_runs_of_other_lengths_are_text),
    NAOS_ADD_TEST(strike_nests_with_emphasis_and_links),
    NAOS_ADD_TEST(strike_overlapping_emphasis_follows_the_rules_of_emphasis),
    NAOS_ADD_TEST(strike_is_not_made_inside_code_spans_or_at_escapes),
    NAOS_ADD_TEST(strike_failed_allocations_end_the_parse_without_leaks)
)

/* ---------------------------------------------------------------------- */
/* The order of the rules */

#define K_A (AMARK_KIND_DIALECT_MIN + 200)
#define K_B (AMARK_KIND_DIALECT_MIN + 201)
#define K_C (AMARK_KIND_DIALECT_MIN + 202)
#define K_STAR (AMARK_KIND_DIALECT_MIN + 203)

/* Whether the byte after the trigger is ch, on the trigger's line. */
static bool next_is(const amark_dialect_inline_t *in, char ch)
{
    return in->position + 1 < in->line_end &&
           in->content[in->position + 1] == ch;
}

/* Makes the trigger and the byte after it, if it is ch. */
static amark_error_t make_two_if(const amark_dialect_inline_t *in, char ch,
                                 amark_dialect_inline_result_t *out)
{
    if (next_is(in, ch)) {
        out->matched = true;
        out->range.start = in->position;
        out->range.end = in->position + 2;
    }
    return AMARK_ERROR_NONE;
}

static amark_error_t rule_a(const amark_dialect_inline_t *in, void *state,
                            amark_dialect_inline_result_t *out)
{
    (void)state;
    return make_two_if(in, 'a', out);
}

static amark_error_t rule_b(const amark_dialect_inline_t *in, void *state,
                            amark_dialect_inline_result_t *out)
{
    (void)state;
    return make_two_if(in, 'b', out);
}

/* Makes the trigger and the byte after it whatever it is ("%x", "^x"). */
static amark_error_t rule_any(const amark_dialect_inline_t *in, void *state,
                              amark_dialect_inline_result_t *out)
{
    (void)state;
    if (in->position + 1 < in->line_end) {
        out->matched = true;
        out->range.start = in->position;
        out->range.end = in->position + 2;
        out->data = "any";
        out->data_size = 3;
    }
    return AMARK_ERROR_NONE;
}

/* Matches with an empty range: always rejected. */
static amark_error_t rule_empty(const amark_dialect_inline_t *in,
                                void *state,
                                amark_dialect_inline_result_t *out)
{
    (void)state;
    out->matched = true;
    out->range.start = in->position;
    out->range.end = in->position;
    return AMARK_ERROR_NONE;
}

/* Matches with attributes of 3 bytes at NULL: a broken contract. */
static amark_error_t rule_null_data(const amark_dialect_inline_t *in,
                                    void *state,
                                    amark_dialect_inline_result_t *out)
{
    (void)state;
    out->matched = true;
    out->range.start = in->position;
    out->range.end = in->position + 1;
    out->data = NULL;
    out->data_size = 3;
    return AMARK_ERROR_NONE;
}

/* "*" and the bytes up to the next "*" of the line, if there is one. */
static amark_error_t rule_star(const amark_dialect_inline_t *in, void *state,
                               amark_dialect_inline_result_t *out)
{
    size_t end = in->position + 1;

    (void)state;
    while (end < in->line_end && in->content[end] != '*') {
        end++;
    }
    if (end < in->line_end) {
        out->matched = true;
        out->range.start = in->position;
        out->range.end = end + 1;
    }
    return AMARK_ERROR_NONE;
}

static const amark_dialect_kind_t k_first_kinds[] = {
    {K_A, AMARK_DIALECT_INLINE, AMARK_DIALECT_BLANK_CLOSES, NULL, NULL, 0},
    {K_B, AMARK_DIALECT_INLINE, AMARK_DIALECT_BLANK_CLOSES, NULL, NULL, 0},
};
/* "%a" and "%b"; '^' and '=' are always rejected. */
static const amark_dialect_inline_rule_t k_first_inlines[] = {
    {'%', K_A, rule_a},
    {'^', K_A, rule_empty},
    {'%', K_B, rule_b},
    {'=', K_A, rule_null_data},
};
static const amark_dialect_extension_t k_first_ext = {
    "first", k_first_kinds, 2, NULL, 0, NULL, k_first_inlines, 4, NULL, 0,
    NULL, NULL, NULL};

static const amark_dialect_kind_t k_second_kinds[] = {
    {K_C, AMARK_DIALECT_INLINE, AMARK_DIALECT_BLANK_CLOSES, NULL, NULL, 0},
    {K_STAR, AMARK_DIALECT_INLINE, AMARK_DIALECT_BLANK_CLOSES, NULL, NULL, 0},
};
/* Any "%x" and "^x" that "first" did not make; "*...*" on one line. */
static const amark_dialect_inline_rule_t k_second_inlines[] = {
    {'%', K_C, rule_any},
    {'^', K_C, rule_any},
    {'*', K_STAR, rule_star},
};
static const amark_dialect_extension_t k_second_ext = {
    "second", k_second_kinds, 2, NULL, 0, NULL, k_second_inlines, 3, NULL,
    0, NULL, NULL, NULL};

static const amark_dialect_extension_t *const k_order_exts[] = {
    &k_first_ext, &k_second_ext};
static const amark_dialect_t k_order_dialect = {
    sizeof(amark_dialect_t), AMARK_DIALECT_ABI, k_order_exts, 2};

NAOS_TEST(rules_are_called_in_extension_order_then_rule_order)
{
    PARSE(&k_order_dialect, "%a %b %c\n", 0);
    NAOS_CHECK_STR_EQ(g_dump,
                      "(doc (p (?) (text \" \") (?) (text \" \") (?)))");
    check_inline(find_kind(K_A, 0), K_A, 0, 2, AMARK_KIND_PARAGRAPH, NULL);
    check_inline(find_kind(K_B, 0), K_B, 3, 5, AMARK_KIND_PARAGRAPH, NULL);
    check_inline(find_kind(K_C, 0), K_C, 6, 8, AMARK_KIND_PARAGRAPH, "any");
}

NAOS_TEST(rejected_rule_is_followed_by_the_next_one)
{
    PARSE(&k_order_dialect, "x ^y\n", 1);
    NAOS_CHECK_UINT_EQ(count_kind(K_A), 0);
    check_inline(find_kind(K_C, 0), K_C, 2, 4, AMARK_KIND_PARAGRAPH, "any");
    check_inline_diagnostic(0, "first", "empty range", 2, 1);
}

NAOS_TEST(attributes_at_null_are_an_unknown_return_value)
{
    PARSE(&k_order_dialect, "a=b\n", 1);
    NAOS_CHECK_UINT_EQ(count_kind(K_A), 0);
    NAOS_CHECK_STR_EQ(g_dump, "(doc (p (text \"a=b\")))");
    check_inline_diagnostic(0, "first", "unknown return value", 1, 1);
}

NAOS_TEST(rule_on_a_commonmark_trigger_comes_before_it)
{
    PARSE(&k_order_dialect, "*a* *b\n", 0);
    NAOS_CHECK_STR_EQ(g_dump, "(doc (p (?) (text \" *b\")))");
    check_inline(find_kind(K_STAR, 0), K_STAR, 0, 3, AMARK_KIND_PARAGRAPH,
                 NULL);
    /* Not matched: the run is a delimiter run as without the dialect. */
    PARSE(&k_order_dialect, "*b\nc*\n", 0);
    NAOS_CHECK_STR_EQ(g_dump,
                      "(doc (p (em (text \"b\") (sbr) (text \"c\"))))");
}

NAOS_SUITE(order,
    NAOS_SET_SETUP(parse),
    NAOS_SET_TEARDOWN(parse),
    NAOS_ADD_TEST(rules_are_called_in_extension_order_then_rule_order),
    NAOS_ADD_TEST(rejected_rule_is_followed_by_the_next_one),
    NAOS_ADD_TEST(attributes_at_null_are_an_unknown_return_value),
    NAOS_ADD_TEST(rule_on_a_commonmark_trigger_comes_before_it)
)

/* ---------------------------------------------------------------------- */
/* The number of calls */

/*
 * Parses length bytes of content with the test dialect as amark_parse does
 * (the block parse, the inline parse and the postprocessing rules, in the
 * order of parse_document in src/parse.c), with a run state of this test,
 * and returns the number of calls of the inline rule that the dialect's
 * state counted, read before the run ends. It also requires the
 * postprocessing rule to have been called once (13.4).
 * Requires the parse to succeed and its tree to pass the check; g_doc is
 * the document.
 */
static uint64_t count_inline_calls(const char *content, size_t length)
{
    amark_resolved_config_t resolved;
    amark_dialect_run_t run;
    amark_arena_t scratch;
    amark_error_t err;
    uint64_t calls;

    amark_document_destroy(g_doc);
    g_doc = NULL;
    g_config.dialect = amark_test_dialect();
    NAOS_REQUIRE_INT_EQ(amark_config_resolve(&g_config, length, &resolved),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(
        amark_document_create(&resolved.allocator,
                              (const unsigned char *)content, length,
                              resolved.start, resolved.end, NULL, &g_doc),
        AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_parse_data_create(g_doc), AMARK_ERROR_NONE);
    NAOS_REQUIRE_INT_EQ(amark_dialect_run_begin(&run, resolved.dialect,
                                                &resolved.allocator, g_doc),
                        AMARK_ERROR_NONE);
    amark_arena_init(&scratch, &resolved.allocator);
    err = amark_block_parse(g_doc, &resolved, &scratch, &run);
    amark_arena_deinit(&scratch);
    if (err == AMARK_ERROR_NONE) {
        amark_arena_init(&scratch, &resolved.allocator);
        err = amark_inline_parse(g_doc, &resolved, &scratch, &run);
        amark_arena_deinit(&scratch);
    }
    if (err == AMARK_ERROR_NONE) {
        err = amark_dialect_postprocess(&run, resolved.max_nesting_depth,
                                        resolved.max_node_count);
    }
    calls = ((const amark_test_counts_t *)run.states[0])->inlines;
    NAOS_CHECK_UINT_EQ(
        ((const amark_test_counts_t *)run.states[0])->postprocesses,
        err == AMARK_ERROR_NONE ? 1 : 0);
    amark_dialect_run_end(&run);
    NAOS_REQUIRE_INT_EQ(err, AMARK_ERROR_NONE);
    check_tree();
    return calls;
}

#define COUNT_CALLS(s) count_inline_calls((s), sizeof(s) - 1)

NAOS_TEST(rule_is_called_once_at_each_trigger_it_visits)
{
    /* Each '$' is visited once; none starts a VAR. */
    NAOS_CHECK_UINT_EQ(COUNT_CALLS("$$$$$ $ $\n"), 7);
    /* The closing '$' of a VAR is inside it. */
    NAOS_CHECK_UINT_EQ(COUNT_CALLS("$a$ $b$\n$c$\n"), 3);
    NAOS_CHECK_UINT_EQ(count_kind(K_VAR), 3);
    /* Rejected: each '$' still once, the closing one as text. */
    NAOS_CHECK_UINT_EQ(COUNT_CALLS("$!inline-empty$\n"), 2);
    /* A VAR over a line ending: its end is not visited again. */
    NAOS_CHECK_UINT_EQ(COUNT_CALLS("> $!inline-next\n> $\n"), 1);
}

NAOS_TEST(rule_is_not_called_inside_structures_or_at_escapes)
{
    NAOS_CHECK_UINT_EQ(
        COUNT_CALLS("`$a$` \\$ <http://x/$> <b c=\"$\"> [x]($y \"$\")\n"),
        0);
    /* The link text is scanned; its destination is not. */
    NAOS_CHECK_UINT_EQ(COUNT_CALLS("[$a$]($b$)\n"), 1);
}

/* The content of many_triggers_take_one_call_each. */
#define MANY 20000
static char g_many[2 * MANY + 1];

NAOS_TEST(many_triggers_take_one_call_each)
{
    size_t i;

    /*
     * "$a" MANY times: "$a$" is a VAR, so every other '$' is visited (the
     * others close a VAR), and each of those once.
     */
    for (i = 0; i < MANY; i++) {
        g_many[2 * i] = '$';
        g_many[2 * i + 1] = 'a';
    }
    g_many[2 * MANY] = '\n';
    NAOS_CHECK_UINT_EQ(count_inline_calls(g_many, sizeof g_many), MANY / 2);
    NAOS_CHECK_UINT_EQ(count_kind(K_VAR), MANY / 2);
    /* Only '$': every one is visited, once, and none starts a VAR. */
    for (i = 0; i < 2 * MANY; i++) {
        g_many[i] = '$';
    }
    NAOS_CHECK_UINT_EQ(count_inline_calls(g_many, sizeof g_many), 2 * MANY);
    NAOS_CHECK_UINT_EQ(count_kind(K_VAR), 0);
}

NAOS_SUITE(count,
    NAOS_SET_SETUP(parse),
    NAOS_SET_TEARDOWN(parse),
    NAOS_ADD_TEST(rule_is_called_once_at_each_trigger_it_visits),
    NAOS_ADD_TEST(rule_is_not_called_inside_structures_or_at_escapes),
    NAOS_ADD_TEST(many_triggers_take_one_call_each)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(var),
    NAOS_ADD_SUITE(strike),
    NAOS_ADD_SUITE(order),
    NAOS_ADD_SUITE(count)
)
