/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of the postprocessing edits (src/dialect_edit.h) that change a
 * node in place: amark_dialect_edit_set_kind, _set_data and _shrink
 * (Requirements 2.7, 2.8, 9.3, 9.4, 9.6, 9.7, 9.8, 10.1, 10.2, 10.9).
 *
 * Each test builds the same tree with the tree builder (src/tree_internal.h)
 * over k_content, begins a run of a local dialect of two extensions and
 * sets up an editor with amark_dialect_editor_init. A valid edit must leave
 * a tree that passes amark_tree_check. An invalid one must leave the tree
 * exactly as it was (every field of every node, the attributes and the
 * dump of the tree), add one diagnostic with the expected text and
 * position, and return AMARK_ERROR_INVALID_INPUT.
 */

#include "dialect_edit.h"
#include "dialect_internal.h"
#include "document.h"
#include "test_alloc.h"
#include "tree_check.h"
#include "tree_dump.h"
#include "tree_internal.h"

#include <amark/amark.h>
#include <amark/dialect.h>

#include <naos/naos.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------------- */
/* The dialect */

/* Kinds of the first extension ("first"). */
#define K_BOX (AMARK_KIND_DIALECT_MIN)      /* container */
#define K_TASK (AMARK_KIND_DIALECT_MIN + 1) /* container */
#define K_NOTE (AMARK_KIND_DIALECT_MIN + 2) /* leaf */
#define K_CELL (AMARK_KIND_DIALECT_MIN + 3) /* leaf */
#define K_MARK (AMARK_KIND_DIALECT_MIN + 4) /* inline */
#define K_SPAN (AMARK_KIND_DIALECT_MIN + 5) /* inline */
/* Kinds of the second extension ("second"). */
#define K_RULE (AMARK_KIND_DIALECT_MIN + 10) /* leaf */
#define K_SUB (AMARK_KIND_DIALECT_MIN + 11)  /* inline */
/* Registered by no extension. */
#define K_UNKNOWN (AMARK_KIND_DIALECT_MIN + 99)

static const amark_dialect_kind_t k_kinds1[] = {
    {K_BOX, AMARK_DIALECT_CONTAINER, AMARK_DIALECT_BLANK_CLOSES, NULL, NULL},
    {K_TASK, AMARK_DIALECT_CONTAINER, AMARK_DIALECT_BLANK_CLOSES, NULL,
     NULL},
    {K_NOTE, AMARK_DIALECT_LEAF, AMARK_DIALECT_BLANK_CLOSES, NULL, NULL},
    {K_CELL, AMARK_DIALECT_LEAF, AMARK_DIALECT_BLANK_CLOSES, NULL, NULL},
    {K_MARK, AMARK_DIALECT_INLINE, 0, NULL, NULL},
    {K_SPAN, AMARK_DIALECT_INLINE, 0, NULL, NULL},
};

static const amark_dialect_kind_t k_kinds2[] = {
    {K_RULE, AMARK_DIALECT_LEAF, AMARK_DIALECT_BLANK_CLOSES, NULL, NULL},
    {K_SUB, AMARK_DIALECT_INLINE, 0, NULL, NULL},
};

static const amark_dialect_extension_t k_ext1 = {
    "first", k_kinds1, sizeof(k_kinds1) / sizeof(k_kinds1[0]),
    NULL, 0, NULL, NULL, 0, NULL, 0, NULL, NULL, NULL};

static const amark_dialect_extension_t k_ext2 = {
    "second", k_kinds2, sizeof(k_kinds2) / sizeof(k_kinds2[0]),
    NULL, 0, NULL, NULL, 0, NULL, 0, NULL, NULL, NULL};

static const amark_dialect_extension_t *const k_exts[] = {&k_ext1, &k_ext2};

static const amark_dialect_t k_dialect = {
    sizeof(amark_dialect_t), AMARK_DIALECT_ABI, k_exts, 2};

/* ---------------------------------------------------------------------- */
/* The tree */

static const char k_content[] =
    "hello *world* end\n"
    "[d]: /u\n"
    "- item\n"
    "box note mark\n"
    "```\ncode\n```\n"
    "[link](/x) tail\n"
    "rule\n";
#define CONTENT_LENGTH (sizeof(k_content) - 1)

/* The IDs of the tree that every test starts from, in creation order. */
enum {
    N_ROOT = 1,
    N_P1,      /* paragraph "hello *world* end" */
    N_HELLO,   /*   text "hello " */
    N_EM,      /*   emphasis "*world*" */
    N_WORLD,   /*     text "world" */
    N_END,     /*   text " end" */
    N_DEF,     /* link reference definition "[d]: /u" */
    N_LIST,    /* list "- item" */
    N_ITEM,    /*   list item "- item" */
    N_P2,      /*     paragraph "item" */
    N_ITEMT,   /*       text "item" */
    N_BOX,     /* BOX "box note mark" */
    N_NOTE,    /*   NOTE "note mark" */
    N_NOTET,   /*     text "note " */
    N_MARK,    /*     MARK "mark" */
    N_MARKT,   /*       text "mark" */
    N_CODE,    /* code block "```\ncode\n```" */
    N_P3,      /* paragraph "[link](/x) tail" */
    N_LINK,    /*   link "[link](/x)" */
    N_LINKT,   /*     text "link" */
    N_TAIL,    /*   text " tail" */
    N_RULE,    /* RULE "rule" */
    N_COUNT = N_RULE
};

static amark_test_alloc_t g_state;
static amark_allocator_t g_allocator;
static amark_document_t *g_doc;
static amark_dialect_run_t g_run;
static int g_run_begun;
static amark_dialect_editor_t g_editor;

/* The offset of the first occurrence of needle in k_content. */
static size_t off(const char *needle)
{
    const char *at = strstr(k_content, needle);

    NAOS_REQUIRE_NOT_NULL(at);
    return (size_t)(at - k_content);
}

static size_t end_of(const char *needle)
{
    return off(needle) + strlen(needle);
}

static amark_range_t rng(size_t start, size_t end)
{
    amark_range_t r;

    r.start = start;
    r.end = end;
    return r;
}

static amark_node_t *node_at(amark_node_id_t id)
{
    return (amark_node_t *)amark_array_at(&g_doc->nodes, id);
}

/* Appends a node over needle; leaves the test on failure. */
static amark_node_id_t add(amark_node_id_t parent, amark_kind_t kind,
                           amark_node_class_t cls, const char *needle)
{
    amark_node_id_t id = AMARK_NODE_NONE;

    NAOS_REQUIRE_INT_EQ(amark_tree_append(g_doc, parent, kind, cls,
                                          off(needle), end_of(needle), &id),
                        AMARK_ERROR_NONE);
    return id;
}

static void build_tree(void)
{
    amark_range_t label;
    amark_range_t dest;
    size_t start;

    NAOS_REQUIRE_INT_EQ(amark_document_create(&g_allocator,
                                              (const unsigned char *)k_content,
                                              CONTENT_LENGTH, 0,
                                              CONTENT_LENGTH, NULL, &g_doc),
                        AMARK_ERROR_NONE);
    NAOS_REQUIRE_UINT_EQ(add(N_ROOT, AMARK_KIND_PARAGRAPH,
                             AMARK_NODE_CLASS_AUTO, "hello *world* end"),
                         N_P1);
    add(N_P1, AMARK_KIND_TEXT, AMARK_NODE_CLASS_AUTO, "hello ");
    add(N_P1, AMARK_KIND_EMPHASIS, AMARK_NODE_CLASS_AUTO, "*world*");
    add(N_EM, AMARK_KIND_TEXT, AMARK_NODE_CLASS_AUTO, "world");
    add(N_P1, AMARK_KIND_TEXT, AMARK_NODE_CLASS_AUTO, " end");
    add(N_ROOT, AMARK_KIND_LINK_REFERENCE_DEFINITION, AMARK_NODE_CLASS_AUTO,
        "[d]: /u");
    start = off("[d]: /u");
    label = rng(start + 1, start + 2);
    dest = rng(start + 5, start + 7);
    NAOS_REQUIRE_INT_EQ(amark_tree_set_definition(g_doc, N_DEF, &label,
                                                  &dest, NULL),
                        AMARK_ERROR_NONE);
    add(N_ROOT, AMARK_KIND_LIST, AMARK_NODE_CLASS_AUTO, "- item");
    NAOS_REQUIRE_INT_EQ(amark_tree_set_list(g_doc, N_LIST, AMARK_LIST_BULLET,
                                            '-', 0, AMARK_LIST_TIGHT),
                        AMARK_ERROR_NONE);
    add(N_LIST, AMARK_KIND_LIST_ITEM, AMARK_NODE_CLASS_AUTO, "- item");
    add(N_ITEM, AMARK_KIND_PARAGRAPH, AMARK_NODE_CLASS_AUTO, "item\n");
    amark_tree_set_range(g_doc, N_P2, off("item\n"), off("item\n") + 4);
    add(N_P2, AMARK_KIND_TEXT, AMARK_NODE_CLASS_AUTO, "item\n");
    amark_tree_set_range(g_doc, N_ITEMT, off("item\n"), off("item\n") + 4);
    add(N_ROOT, K_BOX, AMARK_NODE_CLASS_BLOCK, "box note mark");
    add(N_BOX, K_NOTE, AMARK_NODE_CLASS_BLOCK, "note mark");
    add(N_NOTE, AMARK_KIND_TEXT, AMARK_NODE_CLASS_AUTO, "note ");
    add(N_NOTE, K_MARK, AMARK_NODE_CLASS_INLINE, "mark");
    add(N_MARK, AMARK_KIND_TEXT, AMARK_NODE_CLASS_AUTO, "mark");
    add(N_ROOT, AMARK_KIND_CODE_BLOCK, AMARK_NODE_CLASS_AUTO,
        "```\ncode\n```");
    NAOS_REQUIRE_INT_EQ(amark_tree_set_code_block(g_doc, N_CODE,
                                                  AMARK_CODE_BLOCK_FENCED,
                                                  '`', 3, NULL),
                        AMARK_ERROR_NONE);
    add(N_ROOT, AMARK_KIND_PARAGRAPH, AMARK_NODE_CLASS_AUTO,
        "[link](/x) tail");
    add(N_P3, AMARK_KIND_LINK, AMARK_NODE_CLASS_AUTO, "[link](/x)");
    dest = rng(off("/x"), end_of("/x"));
    NAOS_REQUIRE_INT_EQ(amark_tree_set_link(g_doc, N_LINK, AMARK_LINK_INLINE,
                                            &dest, NULL, AMARK_NODE_NONE),
                        AMARK_ERROR_NONE);
    add(N_LINK, AMARK_KIND_TEXT, AMARK_NODE_CLASS_AUTO, "link]");
    amark_tree_set_range(g_doc, N_LINKT, off("link]"), off("link]") + 4);
    add(N_P3, AMARK_KIND_TEXT, AMARK_NODE_CLASS_AUTO, " tail");
    NAOS_REQUIRE_UINT_EQ(add(N_ROOT, K_RULE, AMARK_NODE_CLASS_BLOCK,
                             "rule\n"),
                         N_RULE);
    amark_tree_set_range(g_doc, N_RULE, off("rule\n"), off("rule\n") + 4);
    NAOS_REQUIRE_UINT_EQ(amark_document_node_count(g_doc), N_COUNT);
}

/* Sets g_editor up for extension number extension, without limits. */
static void use_extension(size_t extension)
{
    amark_dialect_editor_init(&g_editor, &g_run, extension, 0, 0);
}

NAOS_SETUP(edit)
{
    amark_test_alloc_init(&g_state);
    g_allocator = amark_test_allocator(&g_state);
    g_doc = NULL;
    memset(&g_run, 0, sizeof(g_run));
    g_run_begun = 0;
    build_tree();
    NAOS_REQUIRE_INT_EQ(amark_tree_check(g_doc).violation,
                        AMARK_TREE_VIOLATION_NONE);
    NAOS_REQUIRE_TRUE(amark_dialect_validate(&k_dialect));
    NAOS_REQUIRE_INT_EQ(amark_dialect_run_begin(&g_run, &k_dialect,
                                                &g_allocator, g_doc),
                        AMARK_ERROR_NONE);
    g_run_begun = 1;
    use_extension(0);
}

NAOS_TEARDOWN(edit)
{
    g_state.fail_at = 0;
    if (g_run_begun) {
        amark_dialect_run_end(&g_run);
        g_run_begun = 0;
    }
    amark_document_destroy(g_doc);
    g_doc = NULL;
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
    NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
}

/* ---------------------------------------------------------------------- */
/* Snapshots and checks */

#define SNAPSHOT_SIZE 8192

static char g_before[SNAPSHOT_SIZE];
static char g_after[SNAPSHOT_SIZE];

/*
 * Writes every field of every node created (removed ones included), the
 * number of attribute elements, the dialect attributes and the dump of the
 * tree to buffer.
 */
static void snapshot(char *buffer)
{
    size_t count = amark_document_node_count(g_doc);
    size_t used = 0;
    size_t id;
    int n;

    buffer[0] = '\0';
    for (id = 1; id <= count; id++) {
        const amark_node_t *node = node_at((amark_node_id_t)id);
        size_t size = 0;
        const void *data = amark_node_dialect_data(g_doc,
                                                   (amark_node_id_t)id,
                                                   &size);

        n = snprintf(buffer + used, SNAPSHOT_SIZE - used,
                     "%zu:%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%zu,%zu,%zu,%zu,"
                     "%p,%zu;",
                     id, (unsigned long)node->kind,
                     (unsigned long)node->is_inline,
                     (unsigned long)node->parent,
                     (unsigned long)node->first_child,
                     (unsigned long)node->last_child,
                     (unsigned long)node->prev_sibling,
                     (unsigned long)node->next_sibling,
                     (unsigned long)node->attr, node->start, node->end,
                     node->start_line, node->end_line, data, size);
        NAOS_REQUIRE_TRUE(n > 0 && (size_t)n < SNAPSHOT_SIZE - used);
        used += (size_t)n;
    }
    n = snprintf(buffer + used, SNAPSHOT_SIZE - used, "attrs=%zu;",
                 amark_array_len(&g_doc->attrs));
    NAOS_REQUIRE_TRUE(n > 0 && (size_t)n < SNAPSHOT_SIZE - used);
    used += (size_t)n;
    NAOS_REQUIRE_UINT_LT(amark_test_dump_tree(g_doc, buffer + used,
                                              SNAPSHOT_SIZE - used),
                         SNAPSHOT_SIZE - used);
}

static void check_tree(void)
{
    amark_tree_check_result_t result = amark_tree_check(g_doc);

    NAOS_CHECK_INT_EQ(result.violation, AMARK_TREE_VIOLATION_NONE);
    NAOS_CHECK_UINT_EQ(result.node, AMARK_NODE_NONE);
}

/* Checks diagnostic index of g_doc. */
static void check_diagnostic(size_t index, size_t offset,
                             const char *message)
{
    amark_diagnostic_t diag;
    size_t line = 1;
    size_t i;

    for (i = 0; i < offset; i++) {
        if (k_content[i] == '\n') {
            line++;
        }
    }
    NAOS_REQUIRE_TRUE(amark_document_diagnostic(g_doc, index, &diag));
    NAOS_CHECK_UINT_EQ(diag.offset, offset);
    NAOS_CHECK_UINT_EQ(diag.line, line);
    NAOS_CHECK_INT_EQ(diag.severity, AMARK_SEVERITY_ERROR);
    NAOS_CHECK_STR_EQ(diag.message, message);
}

#define MSG_FIRST "dialect \"first\": postprocess: "
#define MSG_SECOND "dialect \"second\": postprocess: "
#define R_STATUS "unknown return value"
#define R_KIND "kind not registered by the extension"
#define R_NESTING "node not allowed at this place"
#define R_PARENT "range leaves the parent"
#define R_NODE "not a node of this document"
#define R_PROTECTED "node cannot be changed"

/* The kinds of edit, to run one by its arguments. */
typedef enum op {
    OP_SET_KIND,
    OP_SET_DATA,
    OP_SHRINK
} op_t;

typedef struct edit {
    op_t op;
    amark_node_id_t node;
    amark_kind_t kind;   /* set_kind */
    const void *data;    /* set_data */
    size_t size;         /* set_data */
    amark_range_t range; /* shrink */
} edit_t;

static amark_error_t run_edit(amark_dialect_editor_t *editor,
                              const edit_t *e)
{
    switch (e->op) {
    case OP_SET_KIND:
        return amark_dialect_edit_set_kind(editor, e->node, e->kind);
    case OP_SET_DATA:
        return amark_dialect_edit_set_data(editor, e->node, e->data,
                                           e->size);
    case OP_SHRINK:
        return amark_dialect_edit_shrink(editor, e->node, e->range);
    }
    return AMARK_ERROR_NONE;
}

static edit_t set_kind(amark_node_id_t node, amark_kind_t kind)
{
    edit_t e;

    memset(&e, 0, sizeof(e));
    e.op = OP_SET_KIND;
    e.node = node;
    e.kind = kind;
    return e;
}

static edit_t set_data(amark_node_id_t node, const void *data, size_t size)
{
    edit_t e;

    memset(&e, 0, sizeof(e));
    e.op = OP_SET_DATA;
    e.node = node;
    e.data = data;
    e.size = size;
    return e;
}

static edit_t shrink(amark_node_id_t node, size_t start, size_t end)
{
    edit_t e;

    memset(&e, 0, sizeof(e));
    e.op = OP_SHRINK;
    e.node = node;
    e.range = rng(start, end);
    return e;
}

/*
 * Runs e, which must be invalid: checks that it returns INVALID_INPUT,
 * leaves the tree exactly as it was, and adds one diagnostic at offset
 * with message.
 */
static void check_invalid(edit_t e, size_t offset, const char *message)
{
    size_t diagnostics = amark_document_diagnostic_count(g_doc);

    snapshot(g_before);
    NAOS_CHECK_INT_EQ(run_edit(&g_editor, &e), AMARK_ERROR_INVALID_INPUT);
    snapshot(g_after);
    NAOS_CHECK_STR_EQ(g_after, g_before);
    NAOS_REQUIRE_UINT_EQ(amark_document_diagnostic_count(g_doc),
                         diagnostics + 1);
    check_diagnostic(diagnostics, offset, message);
    NAOS_CHECK_INT_EQ(g_editor.failure, AMARK_ERROR_NONE);
    check_tree();
}

/* Runs e, which must be valid: no diagnostic, and the tree passes. */
static void check_valid(edit_t e)
{
    size_t diagnostics = amark_document_diagnostic_count(g_doc);

    NAOS_CHECK_INT_EQ(run_edit(&g_editor, &e), AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_EQ(amark_document_diagnostic_count(g_doc), diagnostics);
    check_tree();
}

static size_t start_of(amark_node_id_t id)
{
    return node_at(id)->start;
}

/* ---------------------------------------------------------------------- */
/* Every edit */

NAOS_TEST(null_editor_changes_nothing)
{
    static const char bytes[] = "x";

    snapshot(g_before);
    NAOS_CHECK_INT_EQ(amark_dialect_edit_set_kind(NULL, N_P1, K_NOTE),
                      AMARK_ERROR_INVALID_INPUT);
    NAOS_CHECK_INT_EQ(amark_dialect_edit_set_data(NULL, N_BOX, bytes, 1),
                      AMARK_ERROR_INVALID_INPUT);
    NAOS_CHECK_INT_EQ(amark_dialect_edit_shrink(NULL, N_HELLO,
                                                rng(start_of(N_HELLO),
                                                    start_of(N_HELLO))),
                      AMARK_ERROR_INVALID_INPUT);
    snapshot(g_after);
    NAOS_CHECK_STR_EQ(g_after, g_before);
    NAOS_CHECK_UINT_EQ(amark_document_diagnostic_count(g_doc), 0);
}

NAOS_TEST(every_edit_rejects_a_node_not_in_the_document)
{
    static const char bytes[] = "x";
    static const amark_node_id_t bad[] = {
        AMARK_NODE_NONE, N_COUNT + 1, N_COUNT + 1000,
        (amark_node_id_t)0xFFFFFFFFu};
    size_t i;

    /* The position is the start of the parsed range (the root). */
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        check_invalid(set_kind(bad[i], K_NOTE), 0, MSG_FIRST R_NODE);
        check_invalid(set_data(bad[i], bytes, 1), 0, MSG_FIRST R_NODE);
        check_invalid(shrink(bad[i], 0, 0), 0, MSG_FIRST R_NODE);
    }
}

NAOS_TEST(every_edit_rejects_a_removed_node)
{
    static const char bytes[] = "x";
    amark_node_id_t extra = AMARK_NODE_NONE;

    NAOS_REQUIRE_INT_EQ(amark_tree_append(g_doc, N_BOX, K_NOTE,
                                          AMARK_NODE_CLASS_BLOCK,
                                          end_of("box note mark"),
                                          end_of("box note mark"), &extra),
                        AMARK_ERROR_NONE);
    amark_tree_remove(g_doc, extra);
    check_tree();
    check_invalid(set_kind(extra, K_CELL), 0, MSG_FIRST R_NODE);
    check_invalid(set_data(extra, bytes, 1), 0, MSG_FIRST R_NODE);
    check_invalid(shrink(extra, 0, 0), 0, MSG_FIRST R_NODE);
}

NAOS_TEST(an_invalid_edit_keeps_earlier_edits_and_allows_later_ones)
{
    static const char bytes[] = "ab";
    size_t size = 0;

    check_valid(set_kind(N_P1, K_NOTE));
    check_invalid(set_kind(N_P1, K_BOX), start_of(N_P1),
                  MSG_FIRST R_NESTING);
    NAOS_CHECK_UINT_EQ(amark_node_kind(g_doc, N_P1), K_NOTE);
    check_valid(set_data(N_P1, bytes, 2));
    NAOS_CHECK_MEM_EQ(amark_node_dialect_data(g_doc, N_P1, &size), 2, bytes,
                      2);
    NAOS_CHECK_UINT_EQ(size, 2);
}

NAOS_TEST(diagnostics_are_added_in_the_order_found)
{
    check_invalid(set_kind(N_CODE, K_MARK), start_of(N_CODE),
                  MSG_FIRST R_NESTING);
    check_invalid(shrink(N_P1, 0, 1), start_of(N_P1),
                  MSG_FIRST R_PROTECTED);
    check_invalid(set_data(N_P3, "x", 1), start_of(N_P3), MSG_FIRST R_KIND);
    NAOS_CHECK_UINT_EQ(amark_document_diagnostic_count(g_doc), 3);
}

NAOS_SUITE(common,
    NAOS_SET_SETUP(edit),
    NAOS_SET_TEARDOWN(edit),
    NAOS_ADD_TEST(null_editor_changes_nothing),
    NAOS_ADD_TEST(every_edit_rejects_a_node_not_in_the_document),
    NAOS_ADD_TEST(every_edit_rejects_a_removed_node),
    NAOS_ADD_TEST(an_invalid_edit_keeps_earlier_edits_and_allows_later_ones),
    NAOS_ADD_TEST(diagnostics_are_added_in_the_order_found)
)

/* ---------------------------------------------------------------------- */
/* set_kind */

static void check_kind(amark_node_id_t id, amark_kind_t kind, bool is_inline)
{
    NAOS_CHECK_UINT_EQ(amark_node_kind(g_doc, id), kind);
    NAOS_CHECK_TRUE(amark_node_is_inline(g_doc, id) == is_inline);
}

NAOS_TEST(set_kind_changes_a_block_with_inline_children_to_a_leaf)
{
    check_valid(set_kind(N_P1, K_NOTE));
    check_kind(N_P1, K_NOTE, false);
    /* The children and the range stay. */
    NAOS_CHECK_UINT_EQ(amark_node_first_child(g_doc, N_P1), N_HELLO);
    NAOS_CHECK_UINT_EQ(node_at(N_P1)->start, off("hello"));
    NAOS_CHECK_UINT_EQ(node_at(N_P1)->end, end_of("end"));
}

NAOS_TEST(set_kind_changes_a_block_with_block_children_to_a_container)
{
    check_valid(set_kind(N_ITEM, K_TASK));
    check_kind(N_ITEM, K_TASK, false);
    NAOS_CHECK_UINT_EQ(amark_node_parent(g_doc, N_ITEM), N_LIST);
    NAOS_CHECK_UINT_EQ(amark_node_first_child(g_doc, N_ITEM), N_P2);
    check_valid(set_kind(N_BOX, K_TASK));
    check_kind(N_BOX, K_TASK, false);
}

NAOS_TEST(set_kind_changes_a_block_without_children_to_any_block_kind)
{
    check_valid(set_kind(N_CODE, K_BOX));
    check_kind(N_CODE, K_BOX, false);
    check_valid(set_kind(N_CODE, K_CELL));
    check_kind(N_CODE, K_CELL, false);
    /* A node of another extension's kind may be changed too. */
    check_valid(set_kind(N_RULE, K_TASK));
    check_kind(N_RULE, K_TASK, false);
}

NAOS_TEST(set_kind_changes_inlines_to_inline_kinds)
{
    /* Without children, with inline children, and a dialect inline. */
    check_valid(set_kind(N_HELLO, K_MARK));
    check_kind(N_HELLO, K_MARK, true);
    check_valid(set_kind(N_EM, K_SPAN));
    check_kind(N_EM, K_SPAN, true);
    check_valid(set_kind(N_MARK, K_SPAN));
    check_kind(N_MARK, K_SPAN, true);
    NAOS_CHECK_UINT_EQ(amark_node_first_child(g_doc, N_MARK), N_MARKT);
}

NAOS_TEST(set_kind_drops_the_attributes)
{
    static const char bytes[] = "abc";
    amark_range_t dest;
    size_t size = 1;

    /* A link loses its destination (the header's note). */
    NAOS_REQUIRE_TRUE(amark_link_destination(g_doc, N_LINK, &dest));
    check_valid(set_kind(N_LINK, K_SPAN));
    check_kind(N_LINK, K_SPAN, true);
    NAOS_CHECK_UINT_EQ(node_at(N_LINK)->attr, 0);
    NAOS_CHECK_FALSE(amark_link_destination(g_doc, N_LINK, &dest));
    NAOS_CHECK_NULL(amark_node_dialect_data(g_doc, N_LINK, &size));
    NAOS_CHECK_UINT_EQ(size, 0);

    /* A list loses its type; a dialect node loses its data. */
    check_valid(set_kind(N_LIST, K_BOX));
    NAOS_CHECK_UINT_EQ(node_at(N_LIST)->attr, 0);
    check_valid(set_data(N_BOX, bytes, 3));
    NAOS_REQUIRE_NOT_NULL(amark_node_dialect_data(g_doc, N_BOX, NULL));
    check_valid(set_kind(N_BOX, K_TASK));
    size = 1;
    NAOS_CHECK_NULL(amark_node_dialect_data(g_doc, N_BOX, &size));
    NAOS_CHECK_UINT_EQ(size, 0);
}

NAOS_TEST(set_kind_rejects_the_document_and_definitions)
{
    check_invalid(set_kind(N_ROOT, K_BOX), start_of(N_ROOT),
                  MSG_FIRST R_PROTECTED);
    check_invalid(set_kind(N_DEF, K_NOTE), start_of(N_DEF),
                  MSG_FIRST R_PROTECTED);
}

NAOS_TEST(set_kind_rejects_kinds_the_extension_did_not_register)
{
    /* Another extension's, a CommonMark one, an unknown one, none. */
    check_invalid(set_kind(N_P1, K_RULE), start_of(N_P1), MSG_FIRST R_KIND);
    check_invalid(set_kind(N_P1, AMARK_KIND_HEADING), start_of(N_P1),
                  MSG_FIRST R_KIND);
    check_invalid(set_kind(N_HELLO, AMARK_KIND_TEXT), start_of(N_HELLO),
                  MSG_FIRST R_KIND);
    check_invalid(set_kind(N_P1, K_UNKNOWN), start_of(N_P1),
                  MSG_FIRST R_KIND);
    check_invalid(set_kind(N_P1, AMARK_KIND_NONE), start_of(N_P1),
                  MSG_FIRST R_KIND);
    /* The second extension may use its own kinds only. */
    use_extension(1);
    check_invalid(set_kind(N_CODE, K_BOX), start_of(N_CODE),
                  MSG_SECOND R_KIND);
    check_valid(set_kind(N_CODE, K_RULE));
    check_valid(set_kind(N_HELLO, K_SUB));
}

NAOS_TEST(set_kind_rejects_a_change_of_class)
{
    /* Block to inline, inline to block. */
    check_invalid(set_kind(N_CODE, K_MARK), start_of(N_CODE),
                  MSG_FIRST R_NESTING);
    check_invalid(set_kind(N_BOX, K_SPAN), start_of(N_BOX),
                  MSG_FIRST R_NESTING);
    check_invalid(set_kind(N_HELLO, K_NOTE), start_of(N_HELLO),
                  MSG_FIRST R_NESTING);
    check_invalid(set_kind(N_MARK, K_BOX), start_of(N_MARK),
                  MSG_FIRST R_NESTING);
}

NAOS_TEST(set_kind_rejects_a_kind_that_cannot_hold_the_children)
{
    /* Inline children: not a container. */
    check_invalid(set_kind(N_P1, K_BOX), start_of(N_P1),
                  MSG_FIRST R_NESTING);
    check_invalid(set_kind(N_NOTE, K_TASK), start_of(N_NOTE),
                  MSG_FIRST R_NESTING);
    /* Block children: not a leaf. */
    check_invalid(set_kind(N_ITEM, K_NOTE), start_of(N_ITEM),
                  MSG_FIRST R_NESTING);
    check_invalid(set_kind(N_LIST, K_CELL), start_of(N_LIST),
                  MSG_FIRST R_NESTING);
    check_invalid(set_kind(N_BOX, K_NOTE), start_of(N_BOX),
                  MSG_FIRST R_NESTING);
}

NAOS_SUITE(set_kind,
    NAOS_SET_SETUP(edit),
    NAOS_SET_TEARDOWN(edit),
    NAOS_ADD_TEST(set_kind_changes_a_block_with_inline_children_to_a_leaf),
    NAOS_ADD_TEST(set_kind_changes_a_block_with_block_children_to_a_container),
    NAOS_ADD_TEST(set_kind_changes_a_block_without_children_to_any_block_kind),
    NAOS_ADD_TEST(set_kind_changes_inlines_to_inline_kinds),
    NAOS_ADD_TEST(set_kind_drops_the_attributes),
    NAOS_ADD_TEST(set_kind_rejects_the_document_and_definitions),
    NAOS_ADD_TEST(set_kind_rejects_kinds_the_extension_did_not_register),
    NAOS_ADD_TEST(set_kind_rejects_a_change_of_class),
    NAOS_ADD_TEST(set_kind_rejects_a_kind_that_cannot_hold_the_children)
)

/* ---------------------------------------------------------------------- */
/* set_data */

NAOS_TEST(set_data_copies_the_bytes)
{
    char bytes[4] = {'a', 'b', '\0', 'd'};
    const void *data;
    size_t size = 0;

    check_valid(set_data(N_BOX, bytes, 4));
    check_valid(set_data(N_MARK, bytes, 1));
    bytes[0] = 'z'; /* the copy does not change */
    data = amark_node_dialect_data(g_doc, N_BOX, &size);
    NAOS_REQUIRE_NOT_NULL(data);
    NAOS_CHECK_UINT_EQ(size, 4);
    NAOS_CHECK_MEM_EQ(data, 4, "ab\0d", 4);
    data = amark_node_dialect_data(g_doc, N_MARK, &size);
    NAOS_REQUIRE_NOT_NULL(data);
    NAOS_CHECK_UINT_EQ(size, 1);
    NAOS_CHECK_MEM_EQ(data, 1, "a", 1);
}

NAOS_TEST(set_data_replaces_and_clears_the_bytes)
{
    const void *data;
    size_t size = 0;

    check_valid(set_data(N_NOTE, "one", 3));
    check_valid(set_data(N_NOTE, "second", 6));
    data = amark_node_dialect_data(g_doc, N_NOTE, &size);
    NAOS_REQUIRE_NOT_NULL(data);
    NAOS_CHECK_UINT_EQ(size, 6);
    NAOS_CHECK_MEM_EQ(data, 6, "second", 6);
    /* Size 0 is "none"; data may then be NULL. */
    check_valid(set_data(N_NOTE, NULL, 0));
    size = 1;
    NAOS_CHECK_NULL(amark_node_dialect_data(g_doc, N_NOTE, &size));
    NAOS_CHECK_UINT_EQ(size, 0);
}

NAOS_TEST(set_data_on_a_node_changed_to_a_kind_of_the_extension)
{
    size_t size = 0;

    check_valid(set_kind(N_ITEM, K_TASK));
    check_valid(set_data(N_ITEM, "7", 1));
    NAOS_CHECK_MEM_EQ(amark_node_dialect_data(g_doc, N_ITEM, &size), 1, "7",
                      1);
    NAOS_CHECK_UINT_EQ(size, 1);
}

NAOS_TEST(set_data_rejects_kinds_the_extension_did_not_register)
{
    /* CommonMark kinds (the document and a definition among them). */
    check_invalid(set_data(N_ROOT, "x", 1), start_of(N_ROOT),
                  MSG_FIRST R_KIND);
    check_invalid(set_data(N_DEF, "x", 1), start_of(N_DEF),
                  MSG_FIRST R_KIND);
    check_invalid(set_data(N_P1, "x", 1), start_of(N_P1), MSG_FIRST R_KIND);
    check_invalid(set_data(N_HELLO, "x", 1), start_of(N_HELLO),
                  MSG_FIRST R_KIND);
    check_invalid(set_data(N_LINK, NULL, 0), start_of(N_LINK),
                  MSG_FIRST R_KIND);
    /* Another extension's kind, both ways. */
    check_invalid(set_data(N_RULE, "x", 1), start_of(N_RULE),
                  MSG_FIRST R_KIND);
    use_extension(1);
    check_invalid(set_data(N_BOX, "x", 1), start_of(N_BOX),
                  MSG_SECOND R_KIND);
    check_valid(set_data(N_RULE, "x", 1));
}

NAOS_TEST(set_data_rejects_null_data_with_a_size)
{
    /* Not defined by the contract: a STATUS violation (10.4). */
    check_invalid(set_data(N_BOX, NULL, 1), start_of(N_BOX),
                  MSG_FIRST R_STATUS);
}

NAOS_TEST(set_data_failure_lasts)
{
    static char big[100000];
    size_t diagnostics;

    memset(big, 'b', sizeof(big));
    snapshot(g_before);
    g_state.fail_at = g_state.malloc_calls + 1;
    NAOS_CHECK_INT_EQ(amark_dialect_edit_set_data(&g_editor, N_BOX, big,
                                                  sizeof(big)),
                      AMARK_ERROR_NOMEM);
    g_state.fail_at = 0;
    snapshot(g_after);
    NAOS_CHECK_STR_EQ(g_after, g_before);
    NAOS_CHECK_INT_EQ(g_editor.failure, AMARK_ERROR_NOMEM);

    /* Every later edit, valid or not, returns it and changes nothing. */
    diagnostics = amark_document_diagnostic_count(g_doc);
    NAOS_CHECK_INT_EQ(amark_dialect_edit_set_data(&g_editor, N_BOX, "x", 1),
                      AMARK_ERROR_NOMEM);
    NAOS_CHECK_INT_EQ(amark_dialect_edit_set_kind(&g_editor, N_P1, K_NOTE),
                      AMARK_ERROR_NOMEM);
    NAOS_CHECK_INT_EQ(amark_dialect_edit_set_kind(&g_editor, N_P1, K_BOX),
                      AMARK_ERROR_NOMEM);
    NAOS_CHECK_INT_EQ(amark_dialect_edit_shrink(&g_editor, N_HELLO,
                                                rng(start_of(N_HELLO),
                                                    start_of(N_HELLO))),
                      AMARK_ERROR_NOMEM);
    NAOS_CHECK_INT_EQ(amark_dialect_edit_shrink(&g_editor, 0, rng(0, 0)),
                      AMARK_ERROR_NOMEM);
    snapshot(g_after);
    NAOS_CHECK_STR_EQ(g_after, g_before);
    NAOS_CHECK_UINT_EQ(amark_document_diagnostic_count(g_doc), diagnostics);
    check_tree();
}

NAOS_TEST(diagnostic_failure_lasts)
{
    size_t i;
    amark_error_t err = AMARK_ERROR_INVALID_INPUT;

    /* Fail the next allocation until adding a diagnostic needs one. */
    for (i = 0; i < 10000 && err == AMARK_ERROR_INVALID_INPUT; i++) {
        g_state.fail_at = g_state.malloc_calls + 1;
        err = amark_dialect_edit_set_kind(&g_editor, N_P1, K_BOX);
    }
    g_state.fail_at = 0;
    NAOS_REQUIRE_INT_EQ(err, AMARK_ERROR_NOMEM);
    NAOS_CHECK_INT_EQ(g_editor.failure, AMARK_ERROR_NOMEM);
    snapshot(g_before);
    NAOS_CHECK_INT_EQ(amark_dialect_edit_set_kind(&g_editor, N_P1, K_NOTE),
                      AMARK_ERROR_NOMEM);
    NAOS_CHECK_UINT_EQ(amark_node_kind(g_doc, N_P1), AMARK_KIND_PARAGRAPH);
    snapshot(g_after);
    NAOS_CHECK_STR_EQ(g_after, g_before);
    check_tree();
}

NAOS_SUITE(set_data,
    NAOS_SET_SETUP(edit),
    NAOS_SET_TEARDOWN(edit),
    NAOS_ADD_TEST(set_data_copies_the_bytes),
    NAOS_ADD_TEST(set_data_replaces_and_clears_the_bytes),
    NAOS_ADD_TEST(set_data_on_a_node_changed_to_a_kind_of_the_extension),
    NAOS_ADD_TEST(set_data_rejects_kinds_the_extension_did_not_register),
    NAOS_ADD_TEST(set_data_rejects_null_data_with_a_size),
    NAOS_ADD_TEST(set_data_failure_lasts),
    NAOS_ADD_TEST(diagnostic_failure_lasts)
)

/* ---------------------------------------------------------------------- */
/* shrink */

static void check_range(amark_node_id_t id, size_t start, size_t end)
{
    size_t start_line = 0;
    size_t end_line = 0;

    NAOS_CHECK_UINT_EQ(node_at(id)->start, start);
    NAOS_CHECK_UINT_EQ(node_at(id)->end, end);
    amark_document_range_lines(g_doc, start, end, &start_line, &end_line);
    NAOS_CHECK_UINT_EQ(node_at(id)->start_line, start_line);
    NAOS_CHECK_UINT_EQ(node_at(id)->end_line, end_line);
}

NAOS_TEST(shrink_narrows_a_text)
{
    size_t s = off("hello ");

    check_valid(shrink(N_HELLO, s, s + 5));
    check_range(N_HELLO, s, s + 5);
    check_valid(shrink(N_HELLO, s + 1, s + 5));
    check_range(N_HELLO, s + 1, s + 5);
    /* The same range, then an empty one. */
    check_valid(shrink(N_HELLO, s + 1, s + 5));
    check_range(N_HELLO, s + 1, s + 5);
    check_valid(shrink(N_HELLO, s + 3, s + 3));
    check_range(N_HELLO, s + 3, s + 3);
}

NAOS_TEST(shrink_narrows_a_kind_of_the_extension_around_its_children)
{
    size_t s = off("box note mark");

    /* A container to its child, a leaf to its children, an inline. */
    check_valid(shrink(N_BOX, s + 4, end_of("box note mark")));
    check_range(N_BOX, s + 4, end_of("box note mark"));
    check_valid(shrink(N_NOTE, s + 4, end_of("box note mark")));
    check_valid(shrink(N_MARK, off("mark"), end_of("mark")));
    check_valid(shrink(N_MARKT, off("mark") + 1, end_of("mark")));
    check_valid(shrink(N_MARK, off("mark") + 1, end_of("mark")));
    check_range(N_MARK, off("mark") + 1, end_of("mark"));
    /* A leaf without children, to nothing. */
    use_extension(1);
    check_valid(shrink(N_RULE, off("rule\n") + 2, off("rule\n") + 2));
    check_range(N_RULE, off("rule\n") + 2, off("rule\n") + 2);
}

NAOS_TEST(shrink_rederives_the_lines)
{
    size_t s = off("```\ncode");

    check_valid(set_kind(N_CODE, K_CELL));
    NAOS_CHECK_UINT_EQ(node_at(N_CODE)->end_line,
                       node_at(N_CODE)->start_line + 2);
    check_valid(shrink(N_CODE, s + 4, s + 8));
    check_range(N_CODE, s + 4, s + 8);
    NAOS_CHECK_UINT_EQ(node_at(N_CODE)->start_line,
                       node_at(N_CODE)->end_line);
}

NAOS_TEST(shrink_rejects_kinds_other_than_text_and_the_extensions)
{
    size_t s;

    /* The document, a definition, CommonMark blocks and inlines with
     * children or with ranges in their attributes. */
    check_invalid(shrink(N_ROOT, 0, 1), 0, MSG_FIRST R_PROTECTED);
    s = start_of(N_DEF);
    check_invalid(shrink(N_DEF, s, s + 7), s, MSG_FIRST R_PROTECTED);
    s = start_of(N_P1);
    check_invalid(shrink(N_P1, s, s + 16), s, MSG_FIRST R_PROTECTED);
    s = start_of(N_CODE);
    check_invalid(shrink(N_CODE, s, s + 3), s, MSG_FIRST R_PROTECTED);
    s = start_of(N_LIST);
    check_invalid(shrink(N_LIST, s, node_at(N_LIST)->end), s,
                  MSG_FIRST R_PROTECTED);
    s = start_of(N_EM);
    check_invalid(shrink(N_EM, s + 1, s + 6), s, MSG_FIRST R_PROTECTED);
    s = start_of(N_LINK);
    check_invalid(shrink(N_LINK, s, s + 6), s, MSG_FIRST R_PROTECTED);
    /* Another extension's kind, both ways. */
    s = start_of(N_RULE);
    check_invalid(shrink(N_RULE, s, s + 1), s, MSG_FIRST R_PROTECTED);
    use_extension(1);
    s = start_of(N_MARK);
    check_invalid(shrink(N_MARK, s, s + 4), s, MSG_SECOND R_PROTECTED);
}

NAOS_TEST(shrink_rejects_a_range_outside_the_node)
{
    size_t s = start_of(N_WORLD);
    size_t e = node_at(N_WORLD)->end;

    check_invalid(shrink(N_WORLD, s - 1, e), s, MSG_FIRST R_PARENT);
    check_invalid(shrink(N_WORLD, s, e + 1), s, MSG_FIRST R_PARENT);
    check_invalid(shrink(N_WORLD, s + 2, s + 1), s, MSG_FIRST R_PARENT);
    check_invalid(shrink(N_WORLD, e + 1, e + 2), s, MSG_FIRST R_PARENT);
    check_invalid(shrink(N_WORLD, 0, CONTENT_LENGTH), s, MSG_FIRST R_PARENT);
    check_invalid(shrink(N_WORLD, CONTENT_LENGTH + 5, (size_t)-1), s,
                  MSG_FIRST R_PARENT);
}

NAOS_TEST(shrink_rejects_a_range_that_leaves_out_a_child)
{
    size_t s = start_of(N_NOTE);
    size_t e = node_at(N_NOTE)->end;

    /* The first child's start and the last child's end. */
    check_invalid(shrink(N_NOTE, s + 1, e), s, MSG_FIRST R_PARENT);
    check_invalid(shrink(N_NOTE, s, e - 1), s, MSG_FIRST R_PARENT);
    s = start_of(N_BOX);
    check_invalid(shrink(N_BOX, start_of(N_NOTE) + 1,
                         node_at(N_BOX)->end),
                  s, MSG_FIRST R_PARENT);
    s = start_of(N_MARK);
    check_invalid(shrink(N_MARK, s, s + 3), s, MSG_FIRST R_PARENT);
}

NAOS_SUITE(shrink,
    NAOS_SET_SETUP(edit),
    NAOS_SET_TEARDOWN(edit),
    NAOS_ADD_TEST(shrink_narrows_a_text),
    NAOS_ADD_TEST(shrink_narrows_a_kind_of_the_extension_around_its_children),
    NAOS_ADD_TEST(shrink_rederives_the_lines),
    NAOS_ADD_TEST(shrink_rejects_kinds_other_than_text_and_the_extensions),
    NAOS_ADD_TEST(shrink_rejects_a_range_outside_the_node),
    NAOS_ADD_TEST(shrink_rejects_a_range_that_leaves_out_a_child)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(common),
    NAOS_ADD_SUITE(set_kind),
    NAOS_ADD_SUITE(set_data),
    NAOS_ADD_SUITE(shrink)
)
