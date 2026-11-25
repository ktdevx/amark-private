/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Unit tests of the postprocessing edits (src/dialect_edit.h): those that
 * change a node in place, amark_dialect_edit_set_kind, _set_data and
 * _shrink, and those that add nodes, _add_child and _split_text
 * (Requirements 2.4, 2.5, 2.7, 2.8, 9.3, 9.4, 9.5, 9.6, 9.7, 9.8, 9.10,
 * 10.1, 10.2, 10.9, 10.11, 10.12).
 *
 * Each test builds the same tree with the tree builder (src/tree_internal.h)
 * over k_content, begins a run of a local dialect of two extensions and
 * sets up an editor with amark_dialect_editor_init. A valid edit must leave
 * a tree that passes amark_tree_check. An invalid one must leave the tree
 * exactly as it was (every field of every node, the attributes and the
 * dump of the tree), add one diagnostic with the expected text and
 * position, and return AMARK_ERROR_INVALID_INPUT.
 *
 * The suite postprocess parses with amark_parse, which calls the
 * postprocessing rules after the inline parse, once per document, in the
 * order of the extensions (amark_dialect_postprocess; 1.4, 9.1, 9.2): the
 * CHECK and TASK of the test dialect, its invalid edit, its undefined
 * return value and its AMARK_ERROR_NOMEM (10.4, 10.5, 10.10, 15.1, 15.3,
 * 15.4), and local dialects of two postprocessing extensions. Every tree
 * returned passes amark_tree_check (9.9).
 */

#include "dialect_edit.h"
#include "dialect_internal.h"
#include "document.h"
#include "test_alloc.h"
#include "test_dialect.h"
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
    {K_BOX, AMARK_DIALECT_CONTAINER, AMARK_DIALECT_BLANK_CLOSES, NULL,
     NULL, 0},
    {K_TASK, AMARK_DIALECT_CONTAINER, AMARK_DIALECT_BLANK_CLOSES, NULL,
     NULL, 0},
    {K_NOTE, AMARK_DIALECT_LEAF, AMARK_DIALECT_BLANK_CLOSES, NULL, NULL, 0},
    {K_CELL, AMARK_DIALECT_LEAF, AMARK_DIALECT_BLANK_CLOSES, NULL, NULL, 0},
    {K_MARK, AMARK_DIALECT_INLINE, 0, NULL, NULL, 0},
    {K_SPAN, AMARK_DIALECT_INLINE, 0, NULL, NULL, 0},
};

static const amark_dialect_kind_t k_kinds2[] = {
    {K_RULE, AMARK_DIALECT_LEAF, AMARK_DIALECT_BLANK_CLOSES, NULL, NULL, 0},
    {K_SUB, AMARK_DIALECT_INLINE, 0, NULL, NULL, 0},
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
    "rule\n"
    "caf\xc3\xa9\n";
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

#define SNAPSHOT_SIZE 16384

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
#define R_BOUNDS "range leaves the content"
#define R_ORDER "siblings overlap or are out of order"
#define R_TEXT "not a text node, or a bad split position"

/* The kinds of edit, to run one by its arguments. */
typedef enum op {
    OP_SET_KIND,
    OP_SET_DATA,
    OP_SHRINK,
    OP_ADD_CHILD,
    OP_SPLIT_TEXT
} op_t;

typedef struct edit {
    op_t op;
    amark_node_id_t node; /* the parent for add_child */
    amark_node_id_t hint; /* add_child */
    amark_kind_t kind;    /* set_kind, add_child */
    const void *data;     /* set_data, add_child */
    size_t size;          /* set_data, add_child */
    amark_range_t range;  /* shrink, add_child */
    size_t at;            /* split_text */
    amark_node_id_t *out; /* add_child, split_text */
} edit_t;

/* Where add_child and split_text store the new node's ID. */
static amark_node_id_t g_out;

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
    case OP_ADD_CHILD:
        return amark_dialect_edit_add_child(editor, e->node, e->hint,
                                            e->kind, e->range, e->data,
                                            e->size, e->out);
    case OP_SPLIT_TEXT:
        return amark_dialect_edit_split_text(editor, e->node, e->at, e->out);
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

static edit_t add_child(amark_node_id_t parent, amark_node_id_t hint,
                        amark_kind_t kind, size_t start, size_t end)
{
    edit_t e;

    memset(&e, 0, sizeof(e));
    e.op = OP_ADD_CHILD;
    e.node = parent;
    e.hint = hint;
    e.kind = kind;
    e.range = rng(start, end);
    e.out = &g_out;
    return e;
}

static edit_t split_text(amark_node_id_t node, size_t at)
{
    edit_t e;

    memset(&e, 0, sizeof(e));
    e.op = OP_SPLIT_TEXT;
    e.node = node;
    e.at = at;
    e.out = &g_out;
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
    g_out = (amark_node_id_t)12345;
    NAOS_CHECK_INT_EQ(run_edit(&g_editor, &e), AMARK_ERROR_INVALID_INPUT);
    if (e.out != NULL) {
        NAOS_CHECK_UINT_EQ(g_out, AMARK_NODE_NONE);
    }
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
    g_out = (amark_node_id_t)12345;
    NAOS_CHECK_INT_EQ(amark_dialect_edit_add_child(NULL, N_BOX,
                                                   AMARK_NODE_NONE, K_NOTE,
                                                   rng(start_of(N_BOX),
                                                       start_of(N_BOX)),
                                                   NULL, 0, &g_out),
                      AMARK_ERROR_INVALID_INPUT);
    NAOS_CHECK_UINT_EQ(g_out, AMARK_NODE_NONE);
    g_out = (amark_node_id_t)12345;
    NAOS_CHECK_INT_EQ(amark_dialect_edit_split_text(NULL, N_HELLO,
                                                    start_of(N_HELLO) + 1,
                                                    &g_out),
                      AMARK_ERROR_INVALID_INPUT);
    NAOS_CHECK_UINT_EQ(g_out, AMARK_NODE_NONE);
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
        check_invalid(add_child(bad[i], AMARK_NODE_NONE, K_NOTE, 0, 0), 0,
                      MSG_FIRST R_NODE);
        check_invalid(split_text(bad[i], 1), 0, MSG_FIRST R_NODE);
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
    check_invalid(add_child(extra, AMARK_NODE_NONE, K_MARK, 0, 0), 0,
                  MSG_FIRST R_NODE);
    check_invalid(split_text(extra, 1), 0, MSG_FIRST R_NODE);
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

/* ---------------------------------------------------------------------- */
/* add_child */

/*
 * Appends an empty node of kind at the end of parent, with the attributes
 * the tree check needs, as a node the tests can add children to.
 */
static amark_node_id_t extra(amark_node_id_t parent, amark_kind_t kind)
{
    size_t at = node_at(parent)->end;
    amark_node_id_t id = AMARK_NODE_NONE;

    NAOS_REQUIRE_INT_EQ(amark_tree_append(g_doc, parent, kind,
                                          AMARK_NODE_CLASS_AUTO, at, at, &id),
                        AMARK_ERROR_NONE);
    switch (kind) {
    case AMARK_KIND_HEADING:
        NAOS_REQUIRE_INT_EQ(amark_tree_set_heading(g_doc, id, 1,
                                                   AMARK_HEADING_ATX),
                            AMARK_ERROR_NONE);
        break;
    case AMARK_KIND_IMAGE:
        NAOS_REQUIRE_INT_EQ(amark_tree_set_link(g_doc, id, AMARK_LINK_INLINE,
                                                NULL, NULL, AMARK_NODE_NONE),
                            AMARK_ERROR_NONE);
        break;
    case AMARK_KIND_AUTOLINK:
        NAOS_REQUIRE_INT_EQ(amark_tree_set_autolink(g_doc, id,
                                                    AMARK_AUTOLINK_URI),
                            AMARK_ERROR_NONE);
        break;
    default:
        break;
    }
    return id;
}

/* Checks the links and the range of a node add_child or split_text made. */
static void check_added(amark_node_id_t id, amark_kind_t kind,
                        amark_node_id_t parent, amark_node_id_t prev,
                        amark_node_id_t next, size_t start, size_t end)
{
    NAOS_CHECK_UINT_EQ(amark_node_kind(g_doc, id), kind);
    NAOS_CHECK_UINT_EQ(amark_node_parent(g_doc, id), parent);
    NAOS_CHECK_UINT_EQ(node_at(id)->prev_sibling, prev);
    NAOS_CHECK_UINT_EQ(amark_node_next_sibling(g_doc, id), next);
    NAOS_CHECK_UINT_EQ(amark_node_first_child(g_doc, id), AMARK_NODE_NONE);
    check_range(id, start, end);
}

/* The rows of the table of the child classes each kind can hold (2.8). */
typedef enum held {
    HELD_NOTHING,
    HELD_BLOCKS,
    HELD_INLINES
} held_t;

NAOS_TEST(add_child_follows_the_table_of_child_classes)
{
    struct row {
        amark_node_id_t parent;
        held_t held;
    } rows[32];
    size_t count = 0;
    size_t i;

    /* Blocks: the document, a block quote, a list item, a container. */
    rows[count].parent = N_ROOT;
    rows[count++].held = HELD_BLOCKS;
    rows[count].parent = extra(N_ROOT, AMARK_KIND_BLOCK_QUOTE);
    rows[count++].held = HELD_BLOCKS;
    rows[count].parent = N_ITEM;
    rows[count++].held = HELD_BLOCKS;
    rows[count].parent = N_BOX;
    rows[count++].held = HELD_BLOCKS;
    /* Inlines: a paragraph, a heading, a leaf (of either extension), an
     * emphasis, a strong emphasis, a link, an image, a dialect inline. */
    rows[count].parent = N_P1;
    rows[count++].held = HELD_INLINES;
    rows[count].parent = extra(N_ROOT, AMARK_KIND_HEADING);
    rows[count++].held = HELD_INLINES;
    rows[count].parent = N_NOTE;
    rows[count++].held = HELD_INLINES;
    rows[count].parent = N_RULE;
    rows[count++].held = HELD_INLINES;
    rows[count].parent = N_EM;
    rows[count++].held = HELD_INLINES;
    rows[count].parent = extra(N_P3, AMARK_KIND_STRONG);
    rows[count++].held = HELD_INLINES;
    rows[count].parent = N_LINK;
    rows[count++].held = HELD_INLINES;
    rows[count].parent = extra(N_P3, AMARK_KIND_IMAGE);
    rows[count++].held = HELD_INLINES;
    rows[count].parent = N_MARK;
    rows[count++].held = HELD_INLINES;
    /* Nothing: a list and the other kinds. */
    rows[count].parent = N_LIST;
    rows[count++].held = HELD_NOTHING;
    rows[count].parent = extra(N_ROOT, AMARK_KIND_THEMATIC_BREAK);
    rows[count++].held = HELD_NOTHING;
    rows[count].parent = N_CODE;
    rows[count++].held = HELD_NOTHING;
    rows[count].parent = extra(N_ROOT, AMARK_KIND_HTML_BLOCK);
    rows[count++].held = HELD_NOTHING;
    rows[count].parent = N_DEF;
    rows[count++].held = HELD_NOTHING;
    rows[count].parent = N_HELLO;
    rows[count++].held = HELD_NOTHING;
    rows[count].parent = extra(N_P3, AMARK_KIND_CODE_SPAN);
    rows[count++].held = HELD_NOTHING;
    rows[count].parent = extra(N_P3, AMARK_KIND_AUTOLINK);
    rows[count++].held = HELD_NOTHING;
    rows[count].parent = extra(N_P3, AMARK_KIND_RAW_HTML);
    rows[count++].held = HELD_NOTHING;
    rows[count].parent = extra(N_P3, AMARK_KIND_HARD_BREAK);
    rows[count++].held = HELD_NOTHING;
    rows[count].parent = extra(N_P3, AMARK_KIND_SOFT_BREAK);
    rows[count++].held = HELD_NOTHING;
    rows[count].parent = extra(N_P3, AMARK_KIND_BACKSLASH_ESCAPE);
    rows[count++].held = HELD_NOTHING;
    rows[count].parent = extra(N_P3, AMARK_KIND_CHARACTER_REFERENCE);
    rows[count++].held = HELD_NOTHING;
    check_tree();

    /* Each row with a container, a leaf and an inline kind, placed as the
     * first child (an empty range at the start of the parent). */
    for (i = 0; i < count; i++) {
        amark_node_id_t parent = rows[i].parent;
        size_t s = start_of(parent);

        if (rows[i].held == HELD_BLOCKS) {
            amark_node_id_t first = amark_node_first_child(g_doc, parent);

            check_valid(add_child(parent, AMARK_NODE_NONE, K_TASK, s, s));
            check_added(g_out, K_TASK, parent, AMARK_NODE_NONE, first, s, s);
            NAOS_CHECK_FALSE(amark_node_is_inline(g_doc, g_out));
            first = g_out;
            check_valid(add_child(parent, AMARK_NODE_NONE, K_CELL, s, s));
            check_added(g_out, K_CELL, parent, AMARK_NODE_NONE, first, s, s);
            NAOS_CHECK_FALSE(amark_node_is_inline(g_doc, g_out));
        } else {
            check_invalid(add_child(parent, AMARK_NODE_NONE, K_TASK, s, s), s,
                          MSG_FIRST R_NESTING);
            check_invalid(add_child(parent, AMARK_NODE_NONE, K_CELL, s, s), s,
                          MSG_FIRST R_NESTING);
        }
        if (rows[i].held == HELD_INLINES) {
            amark_node_id_t first = amark_node_first_child(g_doc, parent);

            check_valid(add_child(parent, AMARK_NODE_NONE, K_SPAN, s, s));
            check_added(g_out, K_SPAN, parent, AMARK_NODE_NONE, first, s, s);
            NAOS_CHECK_TRUE(amark_node_is_inline(g_doc, g_out));
        } else {
            check_invalid(add_child(parent, AMARK_NODE_NONE, K_SPAN, s, s), s,
                          MSG_FIRST R_NESTING);
        }
    }
    /* The second extension uses the same table. */
    use_extension(1);
    check_valid(add_child(N_BOX, AMARK_NODE_NONE, K_RULE, start_of(N_BOX),
                          start_of(N_BOX)));
    check_invalid(add_child(N_BOX, AMARK_NODE_NONE, K_SUB, start_of(N_BOX),
                            start_of(N_BOX)),
                  start_of(N_BOX), MSG_SECOND R_NESTING);
}

NAOS_TEST(add_child_places_the_node_between_its_siblings)
{
    size_t s = start_of(N_BOX);
    amark_node_id_t first;
    amark_node_id_t second;

    /* Before the first child, between two, after the last. */
    check_valid(add_child(N_P1, AMARK_NODE_NONE, K_MARK, 0, 0));
    first = g_out;
    check_added(g_out, K_MARK, N_P1, AMARK_NODE_NONE, N_HELLO, 0, 0);
    check_valid(add_child(N_P1, AMARK_NODE_NONE, K_SPAN, 6, 6));
    check_added(g_out, K_SPAN, N_P1, N_HELLO, N_EM, 6, 6);
    check_valid(add_child(N_P1, AMARK_NODE_NONE, K_MARK, 17, 17));
    check_added(g_out, K_MARK, N_P1, N_END, AMARK_NODE_NONE, 17, 17);
    NAOS_CHECK_UINT_EQ(amark_node_first_child(g_doc, N_P1), first);
    NAOS_CHECK_UINT_EQ(node_at(N_P1)->last_child, g_out);

    /* A range with bytes in it: "box" before the NOTE, "\n" after the
     * paragraph; the lines are derived from the range. */
    check_valid(add_child(N_BOX, AMARK_NODE_NONE, K_CELL, s, s + 3));
    check_added(g_out, K_CELL, N_BOX, AMARK_NODE_NONE, N_NOTE, s, s + 3);
    check_valid(add_child(N_ROOT, AMARK_NODE_NONE, K_BOX, 17, 18));
    second = g_out;
    check_added(g_out, K_BOX, N_ROOT, N_P1, N_DEF, 17, 18);
    /* A child of a node added before. */
    check_valid(add_child(second, AMARK_NODE_NONE, K_NOTE, 17, 18));
    check_added(g_out, K_NOTE, second, AMARK_NODE_NONE, AMARK_NODE_NONE, 17,
                18);
    /* The whole range of a parent without children. */
    check_valid(add_child(N_RULE, AMARK_NODE_NONE, K_MARK, start_of(N_RULE),
                          node_at(N_RULE)->end));
    check_added(g_out, K_MARK, N_RULE, AMARK_NODE_NONE, AMARK_NODE_NONE,
                start_of(N_RULE), node_at(N_RULE)->end);
}

NAOS_TEST(add_child_keeps_siblings_in_source_order)
{
    static const size_t starts[] = {17, 0, 13, 6, 6, 0, 17};
    amark_node_id_t id;
    size_t last = 0;
    size_t count = 0;
    size_t i;

    /* Added out of order, the children are still in source order. */
    for (i = 0; i < sizeof(starts) / sizeof(starts[0]); i++) {
        check_valid(add_child(N_P1, AMARK_NODE_NONE, K_MARK, starts[i],
                              starts[i]));
    }
    for (id = amark_node_first_child(g_doc, N_P1); id != AMARK_NODE_NONE;
         id = amark_node_next_sibling(g_doc, id)) {
        NAOS_CHECK_UINT_GE(start_of(id), last);
        last = node_at(id)->end;
        count++;
    }
    NAOS_CHECK_UINT_EQ(count, 3 + sizeof(starts) / sizeof(starts[0]));
    /* An empty node goes before a sibling that starts at its offset. */
    NAOS_CHECK_UINT_EQ(amark_node_next_sibling(g_doc, N_COUNT + 6),
                       N_COUNT + 2);
    NAOS_CHECK_UINT_EQ(amark_node_next_sibling(g_doc, N_COUNT + 2), N_HELLO);
    NAOS_CHECK_UINT_EQ(amark_node_next_sibling(g_doc, N_COUNT + 5),
                       N_COUNT + 4);
    NAOS_CHECK_UINT_EQ(amark_node_next_sibling(g_doc, N_COUNT + 4), N_EM);
    NAOS_CHECK_UINT_EQ(amark_node_next_sibling(g_doc, N_COUNT + 3), N_END);
    NAOS_CHECK_UINT_EQ(amark_node_next_sibling(g_doc, N_END), N_COUNT + 7);
    NAOS_CHECK_UINT_EQ(amark_node_next_sibling(g_doc, N_COUNT + 7),
                       N_COUNT + 1);
}

/* Starts a new run over a new tree, as the setup does. */
static void rebuild(void)
{
    amark_dialect_run_end(&g_run);
    g_run_begun = 0;
    amark_document_destroy(g_doc);
    g_doc = NULL;
    memset(&g_run, 0, sizeof(g_run));
    build_tree();
    NAOS_REQUIRE_INT_EQ(amark_dialect_run_begin(&g_run, &k_dialect,
                                                &g_allocator, g_doc),
                        AMARK_ERROR_NONE);
    g_run_begun = 1;
    use_extension(0);
}

NAOS_TEST(add_child_gives_the_same_tree_with_or_without_a_hint)
{
    static char without[SNAPSHOT_SIZE];
    size_t b = start_of(N_BOX);
    amark_node_id_t prev;

    /* Without hints. */
    check_valid(add_child(N_P1, AMARK_NODE_NONE, K_MARK, 0, 0));
    check_valid(add_child(N_P1, AMARK_NODE_NONE, K_MARK, 6, 6));
    check_valid(add_child(N_P1, AMARK_NODE_NONE, K_MARK, 6, 6));
    check_valid(add_child(N_P1, AMARK_NODE_NONE, K_MARK, 13, 13));
    check_valid(add_child(N_P1, AMARK_NODE_NONE, K_MARK, 17, 17));
    check_valid(add_child(N_P1, AMARK_NODE_NONE, K_SPAN, 17, 17));
    check_valid(add_child(N_BOX, AMARK_NODE_NONE, K_CELL, b, b + 3));
    check_valid(add_child(N_ROOT, AMARK_NODE_NONE, K_NOTE, 17, 18));
    snapshot(without);

    /* With the first child, the next sibling, or the node added last as
     * the hint (left to right, each search is O(1)). */
    rebuild();
    check_valid(add_child(N_P1, AMARK_NODE_NONE, K_MARK, 0, 0));
    prev = g_out;
    check_valid(add_child(N_P1, prev, K_MARK, 6, 6));
    prev = g_out;
    check_valid(add_child(N_P1, N_HELLO, K_MARK, 6, 6));
    check_valid(add_child(N_P1, prev, K_MARK, 13, 13));
    check_valid(add_child(N_P1, N_END, K_MARK, 17, 17));
    /* As the hint, the empty sibling the node goes before. */
    check_valid(add_child(N_P1, g_out, K_SPAN, 17, 17));
    check_valid(add_child(N_BOX, AMARK_NODE_NONE, K_CELL, b, b + 3));
    check_valid(add_child(N_ROOT, N_P1, K_NOTE, 17, 18));
    snapshot(g_after);
    NAOS_CHECK_STR_EQ(g_after, without);

    /* With the first child as the hint each time. */
    rebuild();
    check_valid(add_child(N_P1, AMARK_NODE_NONE, K_MARK, 0, 0));
    prev = g_out;
    check_valid(add_child(N_P1, prev, K_MARK, 6, 6));
    check_valid(add_child(N_P1, prev, K_MARK, 6, 6));
    check_valid(add_child(N_P1, prev, K_MARK, 13, 13));
    check_valid(add_child(N_P1, prev, K_MARK, 17, 17));
    check_valid(add_child(N_P1, prev, K_SPAN, 17, 17));
    check_valid(add_child(N_BOX, AMARK_NODE_NONE, K_CELL, b, b + 3));
    check_valid(add_child(N_ROOT, N_P1, K_NOTE, 17, 18));
    snapshot(g_after);
    NAOS_CHECK_STR_EQ(g_after, without);
}

NAOS_TEST(add_child_copies_the_data)
{
    char bytes[3] = {'a', '\0', 'c'};
    const void *data;
    size_t size = 0;

    edit_t e = add_child(N_P1, AMARK_NODE_NONE, K_MARK, 6, 6);
    e.data = bytes;
    e.size = 3;
    check_valid(e);
    bytes[0] = 'z'; /* the copy does not change */
    data = amark_node_dialect_data(g_doc, g_out, &size);
    NAOS_REQUIRE_NOT_NULL(data);
    NAOS_CHECK_UINT_EQ(size, 3);
    NAOS_CHECK_MEM_EQ(data, 3, "a\0c", 3);
    /* Size 0: no data, and data may be NULL. */
    check_valid(add_child(N_P1, AMARK_NODE_NONE, K_MARK, 13, 13));
    size = 1;
    NAOS_CHECK_NULL(amark_node_dialect_data(g_doc, g_out, &size));
    NAOS_CHECK_UINT_EQ(size, 0);
}

NAOS_TEST(add_child_rejects_kinds_the_extension_did_not_register)
{
    size_t s = start_of(N_P1);

    check_invalid(add_child(N_P1, AMARK_NODE_NONE, K_SUB, 6, 6), s,
                  MSG_FIRST R_KIND);
    check_invalid(add_child(N_P1, AMARK_NODE_NONE, AMARK_KIND_TEXT, 6, 6), s,
                  MSG_FIRST R_KIND);
    check_invalid(add_child(N_ROOT, AMARK_NODE_NONE, AMARK_KIND_PARAGRAPH,
                            17, 17),
                  0, MSG_FIRST R_KIND);
    check_invalid(add_child(N_P1, AMARK_NODE_NONE, K_UNKNOWN, 6, 6), s,
                  MSG_FIRST R_KIND);
    check_invalid(add_child(N_P1, AMARK_NODE_NONE, AMARK_KIND_NONE, 6, 6), s,
                  MSG_FIRST R_KIND);
    use_extension(1);
    check_invalid(add_child(N_P1, AMARK_NODE_NONE, K_MARK, 6, 6), s,
                  MSG_SECOND R_KIND);
    check_valid(add_child(N_P1, AMARK_NODE_NONE, K_SUB, 6, 6));
}

NAOS_TEST(add_child_rejects_a_hint_that_is_not_a_child_of_the_parent)
{
    size_t s = start_of(N_P1);
    amark_node_id_t removed = AMARK_NODE_NONE;

    /* A grandchild, a node elsewhere, the parent itself, no node. */
    check_invalid(add_child(N_P1, N_WORLD, K_MARK, 6, 6), s,
                  MSG_FIRST R_NODE);
    check_invalid(add_child(N_P1, N_TAIL, K_MARK, 6, 6), s, MSG_FIRST R_NODE);
    check_invalid(add_child(N_P1, N_P1, K_MARK, 6, 6), s, MSG_FIRST R_NODE);
    check_invalid(add_child(N_P1, N_COUNT + 1, K_MARK, 6, 6), s,
                  MSG_FIRST R_NODE);
    check_invalid(add_child(N_P1, (amark_node_id_t)0xFFFFFFFFu, K_MARK, 6,
                            6),
                  s, MSG_FIRST R_NODE);
    /* A removed child. */
    NAOS_REQUIRE_INT_EQ(amark_tree_append(g_doc, N_P1, K_MARK,
                                          AMARK_NODE_CLASS_INLINE, 17, 17,
                                          &removed),
                        AMARK_ERROR_NONE);
    amark_tree_remove(g_doc, removed);
    check_invalid(add_child(N_P1, removed, K_MARK, 17, 17), s,
                  MSG_FIRST R_NODE);
}

NAOS_TEST(add_child_rejects_a_range_outside_the_content)
{
    size_t s = start_of(N_ROOT);

    check_invalid(add_child(N_ROOT, AMARK_NODE_NONE, K_NOTE, 18, 17), s,
                  MSG_FIRST R_BOUNDS);
    check_invalid(add_child(N_ROOT, AMARK_NODE_NONE, K_NOTE, CONTENT_LENGTH,
                            CONTENT_LENGTH + 1),
                  s, MSG_FIRST R_BOUNDS);
    check_invalid(add_child(N_ROOT, AMARK_NODE_NONE, K_NOTE, (size_t)-1,
                            (size_t)-1),
                  s, MSG_FIRST R_BOUNDS);
    s = start_of(N_EM);
    check_invalid(add_child(N_EM, AMARK_NODE_NONE, K_MARK, 8, 7), s,
                  MSG_FIRST R_BOUNDS);
}

NAOS_TEST(add_child_rejects_a_range_outside_the_parent)
{
    size_t s = start_of(N_EM);
    size_t e = node_at(N_EM)->end;

    check_invalid(add_child(N_EM, AMARK_NODE_NONE, K_MARK, s - 1, s), s,
                  MSG_FIRST R_PARENT);
    check_invalid(add_child(N_EM, AMARK_NODE_NONE, K_MARK, e, e + 1), s,
                  MSG_FIRST R_PARENT);
    check_invalid(add_child(N_EM, AMARK_NODE_NONE, K_MARK, 0, CONTENT_LENGTH),
                  s, MSG_FIRST R_PARENT);
    s = start_of(N_BOX);
    check_invalid(add_child(N_BOX, AMARK_NODE_NONE, K_CELL, 0, 0), s,
                  MSG_FIRST R_PARENT);
}

NAOS_TEST(add_child_rejects_a_range_that_overlaps_a_sibling)
{
    size_t s = start_of(N_P1);
    amark_node_id_t later;
    amark_node_id_t earlier;
    edit_t e;

    /* The previous sibling, the next one, both, a child covered. */
    check_invalid(add_child(N_P1, AMARK_NODE_NONE, K_MARK, 5, 6), s,
                  MSG_FIRST R_ORDER);
    check_invalid(add_child(N_P1, AMARK_NODE_NONE, K_MARK, 6, 7), s,
                  MSG_FIRST R_ORDER);
    check_invalid(add_child(N_P1, AMARK_NODE_NONE, K_MARK, 5, 7), s,
                  MSG_FIRST R_ORDER);
    check_invalid(add_child(N_P1, AMARK_NODE_NONE, K_MARK, 0, 17), s,
                  MSG_FIRST R_ORDER);
    check_invalid(add_child(N_P1, AMARK_NODE_NONE, K_MARK, 16, 17), s,
                  MSG_FIRST R_ORDER);
    check_invalid(add_child(N_P1, AMARK_NODE_NONE, K_MARK, 3, 3), s,
                  MSG_FIRST R_ORDER);
    /* With a hint, the same. */
    check_invalid(add_child(N_P1, N_HELLO, K_MARK, 6, 7), s,
                  MSG_FIRST R_ORDER);
    /* A hint that ends after the start of the range: the search does not
     * go back. */
    check_invalid(add_child(N_P1, N_END, K_MARK, 6, 6), s, MSG_FIRST R_ORDER);
    check_invalid(add_child(N_P1, N_EM, K_MARK, 0, 0), s, MSG_FIRST R_ORDER);
    /* An empty hint at the offset, after an empty sibling at the same
     * offset: without the hint the node would go before that sibling. */
    check_valid(add_child(N_P1, AMARK_NODE_NONE, K_MARK, 6, 6));
    later = g_out;
    check_valid(add_child(N_P1, AMARK_NODE_NONE, K_MARK, 6, 6));
    earlier = g_out;
    NAOS_CHECK_UINT_EQ(amark_node_next_sibling(g_doc, earlier), later);
    check_invalid(add_child(N_P1, later, K_MARK, 6, 6), s, MSG_FIRST R_ORDER);
    check_valid(add_child(N_P1, earlier, K_MARK, 6, 6));
    NAOS_CHECK_UINT_EQ(amark_node_next_sibling(g_doc, g_out), earlier);
    /* The ORDER check comes after the data check. */
    e = add_child(N_P1, AMARK_NODE_NONE, K_MARK, 5, 6);
    e.size = 1;
    check_invalid(e, s, MSG_FIRST R_STATUS);
}

NAOS_TEST(add_child_rejects_arguments_the_contract_does_not_define)
{
    size_t s = start_of(N_P1);
    edit_t e = add_child(N_P1, AMARK_NODE_NONE, K_MARK, 6, 6);

    /* Data NULL with a size, no place for the ID. */
    e.size = 2;
    check_invalid(e, s, MSG_FIRST R_STATUS);
    e = add_child(N_P1, AMARK_NODE_NONE, K_MARK, 6, 6);
    e.out = NULL;
    check_invalid(e, s, MSG_FIRST R_STATUS);
}

/* ---------------------------------------------------------------------- */
/* split_text */

NAOS_TEST(split_text_makes_two_texts)
{
    amark_node_id_t second;

    /* A text with a next sibling, the last child, the new text again. */
    check_valid(split_text(N_HELLO, 3));
    second = g_out;
    check_added(N_HELLO, AMARK_KIND_TEXT, N_P1, AMARK_NODE_NONE, second, 0,
                3);
    check_added(second, AMARK_KIND_TEXT, N_P1, N_HELLO, N_EM, 3, 6);
    NAOS_CHECK_TRUE(amark_node_is_inline(g_doc, second));
    check_valid(split_text(N_END, 15));
    check_added(g_out, AMARK_KIND_TEXT, N_P1, N_END, AMARK_NODE_NONE, 15, 17);
    check_range(N_END, 13, 15);
    NAOS_CHECK_UINT_EQ(node_at(N_P1)->last_child, g_out);
    check_valid(split_text(second, 4));
    check_added(second, AMARK_KIND_TEXT, N_P1, N_HELLO, g_out, 3, 4);
    check_added(g_out, AMARK_KIND_TEXT, N_P1, second, N_EM, 4, 6);
    /* The only child of a dialect inline. */
    check_valid(split_text(N_MARKT, off("mark") + 1));
    check_added(g_out, AMARK_KIND_TEXT, N_MARK, N_MARKT, AMARK_NODE_NONE,
                off("mark") + 1, end_of("mark"));
}

NAOS_TEST(split_text_rejects_other_kinds)
{
    check_invalid(split_text(N_EM, 7), start_of(N_EM), MSG_FIRST R_TEXT);
    check_invalid(split_text(N_P1, 3), start_of(N_P1), MSG_FIRST R_TEXT);
    check_invalid(split_text(N_ROOT, 3), 0, MSG_FIRST R_TEXT);
    check_invalid(split_text(N_MARK, off("mark") + 1), start_of(N_MARK),
                  MSG_FIRST R_TEXT);
    check_invalid(split_text(N_CODE, start_of(N_CODE) + 1), start_of(N_CODE),
                  MSG_FIRST R_TEXT);
}

NAOS_TEST(split_text_rejects_a_position_not_strictly_inside)
{
    size_t s = start_of(N_WORLD);
    size_t e = node_at(N_WORLD)->end;

    check_invalid(split_text(N_WORLD, s), s, MSG_FIRST R_TEXT);
    check_invalid(split_text(N_WORLD, e), s, MSG_FIRST R_TEXT);
    check_invalid(split_text(N_WORLD, s - 1), s, MSG_FIRST R_TEXT);
    check_invalid(split_text(N_WORLD, e + 1), s, MSG_FIRST R_TEXT);
    check_invalid(split_text(N_WORLD, CONTENT_LENGTH + 1), s,
                  MSG_FIRST R_TEXT);
    check_invalid(split_text(N_WORLD, (size_t)-1), s, MSG_FIRST R_TEXT);
    /* Just inside both ends. */
    check_valid(split_text(N_WORLD, e - 1));
    check_valid(split_text(N_WORLD, s + 1));
}

NAOS_TEST(split_text_rejects_the_middle_of_a_character)
{
    size_t a = off("caf\xc3\xa9");
    amark_node_id_t para;
    amark_node_id_t text;

    para = add(N_ROOT, AMARK_KIND_PARAGRAPH, AMARK_NODE_CLASS_AUTO,
               "caf\xc3\xa9");
    text = add(para, AMARK_KIND_TEXT, AMARK_NODE_CLASS_AUTO, "caf\xc3\xa9");
    check_tree();
    check_invalid(split_text(text, a + 4), a, MSG_FIRST R_TEXT);
    check_valid(split_text(text, a + 3));
    check_range(text, a, a + 3);
    check_added(g_out, AMARK_KIND_TEXT, para, text, AMARK_NODE_NONE, a + 3,
                a + 5);
    check_invalid(split_text(g_out, a + 4), a + 3, MSG_FIRST R_TEXT);
}

NAOS_TEST(split_text_rejects_no_place_for_the_id)
{
    edit_t e = split_text(N_HELLO, 3);

    e.out = NULL;
    check_invalid(e, start_of(N_HELLO), MSG_FIRST R_STATUS);
}

NAOS_SUITE(split_text,
    NAOS_SET_SETUP(edit),
    NAOS_SET_TEARDOWN(edit),
    NAOS_ADD_TEST(split_text_makes_two_texts),
    NAOS_ADD_TEST(split_text_rejects_other_kinds),
    NAOS_ADD_TEST(split_text_rejects_a_position_not_strictly_inside),
    NAOS_ADD_TEST(split_text_rejects_the_middle_of_a_character),
    NAOS_ADD_TEST(split_text_rejects_no_place_for_the_id)
)

/* ---------------------------------------------------------------------- */
/* Adding nodes: limits, failures, sequences */

/*
 * Runs e, which must hit a limit or a failure err: it returns err, it is
 * remembered, no diagnostic is added and the tree is as it was.
 */
static void check_failure(edit_t e, amark_error_t err)
{
    size_t diagnostics = amark_document_diagnostic_count(g_doc);

    snapshot(g_before);
    g_out = (amark_node_id_t)12345;
    NAOS_CHECK_INT_EQ(run_edit(&g_editor, &e), err);
    if (e.out != NULL) {
        NAOS_CHECK_UINT_EQ(g_out, AMARK_NODE_NONE);
    }
    snapshot(g_after);
    NAOS_CHECK_STR_EQ(g_after, g_before);
    NAOS_CHECK_UINT_EQ(amark_document_diagnostic_count(g_doc), diagnostics);
    NAOS_CHECK_INT_EQ(g_editor.failure, err);
    check_tree();
}

/* Every later edit, valid or not, returns err and changes nothing. */
static void check_failure_lasts(amark_error_t err)
{
    check_failure(add_child(N_P1, AMARK_NODE_NONE, K_MARK, 6, 6), err);
    check_failure(add_child(N_P1, AMARK_NODE_NONE, K_MARK, 5, 7), err);
    check_failure(split_text(N_HELLO, 3), err);
    check_failure(split_text(N_EM, 3), err);
    check_failure(set_kind(N_P1, K_NOTE), err);
}

NAOS_TEST(add_child_checks_the_node_limit)
{
    amark_dialect_editor_init(&g_editor, &g_run, 0, 0, N_COUNT + 1);
    check_valid(add_child(N_P1, AMARK_NODE_NONE, K_MARK, 6, 6));
    NAOS_CHECK_UINT_EQ(amark_document_node_count(g_doc), N_COUNT + 1);
    check_failure(add_child(N_P1, AMARK_NODE_NONE, K_MARK, 13, 13),
                  AMARK_ERROR_LIMIT_EXCEEDED);
    check_failure_lasts(AMARK_ERROR_LIMIT_EXCEEDED);
}

NAOS_TEST(split_text_checks_the_node_limit)
{
    amark_dialect_editor_init(&g_editor, &g_run, 0, 0, N_COUNT + 1);
    check_valid(split_text(N_HELLO, 3));
    check_failure(split_text(N_END, 15), AMARK_ERROR_LIMIT_EXCEEDED);
    check_failure_lasts(AMARK_ERROR_LIMIT_EXCEEDED);
}

NAOS_TEST(an_invalid_edit_at_the_node_limit_is_only_rejected)
{
    /* Rejected results do not count (10.12): checked before the limit. */
    amark_dialect_editor_init(&g_editor, &g_run, 0, 0, N_COUNT);
    check_invalid(add_child(N_P1, AMARK_NODE_NONE, K_MARK, 5, 7),
                  start_of(N_P1), MSG_FIRST R_ORDER);
    check_invalid(split_text(N_HELLO, 0), start_of(N_HELLO),
                  MSG_FIRST R_TEXT);
    check_valid(set_kind(N_P1, K_NOTE));
    check_failure(split_text(N_HELLO, 3), AMARK_ERROR_LIMIT_EXCEEDED);
}

NAOS_TEST(add_child_checks_the_depth_limit)
{
    /* N_MARK is at depth 3 (document, BOX, NOTE, MARK). */
    amark_dialect_editor_init(&g_editor, &g_run, 0, 4, 0);
    check_valid(add_child(N_MARK, AMARK_NODE_NONE, K_SPAN, start_of(N_MARK),
                          start_of(N_MARK)));
    check_valid(add_child(N_ROOT, AMARK_NODE_NONE, K_BOX, 17, 18));
    amark_dialect_editor_init(&g_editor, &g_run, 0, 3, 0);
    check_valid(add_child(N_NOTE, AMARK_NODE_NONE, K_SPAN, start_of(N_NOTE),
                          start_of(N_NOTE)));
    /* split_text changes no depth: N_MARKT is at depth 4. */
    check_valid(split_text(N_MARKT, off("mark") + 2));
    check_failure(add_child(N_MARK, AMARK_NODE_NONE, K_SPAN,
                            start_of(N_MARK), start_of(N_MARK)),
                  AMARK_ERROR_LIMIT_EXCEEDED);
    check_failure_lasts(AMARK_ERROR_LIMIT_EXCEEDED);
}

/*
 * Fails the k-th allocation of e on a new tree, for k = 1, 2, ... until e
 * succeeds: each failure returns NOMEM, lasts, adds no diagnostic and
 * leaves a tree that passes the check with the same shape. prepare runs
 * on each new tree first.
 */
static void check_allocation_failures(edit_t e, void (*prepare)(void))
{
    amark_error_t err = AMARK_ERROR_NOMEM;
    size_t failures = 0;
    size_t k;

    for (k = 1; k < 1000 && err == AMARK_ERROR_NOMEM; k++) {
        rebuild();
        prepare();
        NAOS_REQUIRE_UINT_LT(amark_test_dump_tree(g_doc, g_before,
                                                  SNAPSHOT_SIZE),
                             SNAPSHOT_SIZE);
        g_out = (amark_node_id_t)12345;
        g_state.fail_at = g_state.malloc_calls + k;
        err = run_edit(&g_editor, &e);
        g_state.fail_at = 0;
        if (err == AMARK_ERROR_NOMEM) {
            failures++;
            NAOS_CHECK_INT_EQ(g_editor.failure, AMARK_ERROR_NOMEM);
            NAOS_CHECK_UINT_EQ(g_out, AMARK_NODE_NONE);
            NAOS_CHECK_UINT_EQ(amark_document_diagnostic_count(g_doc), 0);
            NAOS_REQUIRE_UINT_LT(amark_test_dump_tree(g_doc, g_after,
                                                      SNAPSHOT_SIZE),
                                 SNAPSHOT_SIZE);
            NAOS_CHECK_STR_EQ(g_after, g_before);
            check_tree();
            check_failure_lasts(AMARK_ERROR_NOMEM);
        }
    }
    NAOS_CHECK_INT_EQ(err, AMARK_ERROR_NONE);
    NAOS_CHECK_UINT_GT(failures, 0);
    check_tree();
}

/* The number of nodes fill_nodes adds. */
static size_t g_fill;

/* Adds one empty MARK at the end of N_P1. */
static void add_mark(void)
{
    NAOS_REQUIRE_INT_EQ(amark_dialect_edit_add_child(&g_editor, N_P1,
                                                     AMARK_NODE_NONE, K_MARK,
                                                     rng(17, 17), NULL, 0,
                                                     &g_out),
                        AMARK_ERROR_NONE);
}

/*
 * Sets g_fill to the number of nodes that can be added to a new tree
 * before adding one allocates (the node array grows out of its arena
 * block), so that the next node added after fill_nodes allocates.
 */
static void count_fill(void)
{
    size_t calls;

    rebuild();
    g_fill = 0;
    calls = g_state.malloc_calls;
    for (;;) {
        add_mark();
        if (g_state.malloc_calls != calls) {
            break;
        }
        g_fill++;
        NAOS_REQUIRE_UINT_LT(g_fill, 100000);
    }
}

static void fill_nodes(void)
{
    size_t i;

    for (i = 0; i < g_fill; i++) {
        add_mark();
    }
}

NAOS_TEST(adding_nodes_fails_on_allocation)
{
    static char big[100000];
    edit_t e;

    memset(big, 'b', sizeof(big));
    count_fill();
    /* The node array, the copy of the data, the attribute element. */
    e = add_child(N_P1, AMARK_NODE_NONE, K_MARK, 6, 6);
    e.data = big;
    e.size = sizeof(big);
    check_allocation_failures(e, fill_nodes);
    check_allocation_failures(split_text(N_HELLO, 3), fill_nodes);
}

NAOS_TEST(edits_before_an_invalid_one_stay_and_later_ones_apply)
{
    amark_node_id_t mark;
    amark_node_id_t text;

    check_valid(add_child(N_P1, AMARK_NODE_NONE, K_MARK, 6, 6));
    mark = g_out;
    check_valid(split_text(N_HELLO, 2));
    text = g_out;
    check_invalid(add_child(N_P1, AMARK_NODE_NONE, K_MARK, 1, 3),
                  start_of(N_P1), MSG_FIRST R_ORDER);
    check_invalid(split_text(text, 2), 2, MSG_FIRST R_TEXT);
    check_valid(set_data(mark, "v", 1));
    check_valid(add_child(N_P1, text, K_SPAN, 6, 6));
    NAOS_CHECK_UINT_EQ(amark_node_next_sibling(g_doc, N_HELLO), text);
    NAOS_CHECK_UINT_EQ(amark_node_next_sibling(g_doc, text), g_out);
    NAOS_CHECK_UINT_EQ(amark_node_next_sibling(g_doc, g_out), mark);
    NAOS_CHECK_UINT_EQ(amark_node_next_sibling(g_doc, mark), N_EM);
    NAOS_CHECK_UINT_EQ(amark_document_diagnostic_count(g_doc), 2);
    NAOS_CHECK_INT_EQ(g_editor.failure, AMARK_ERROR_NONE);
    check_tree();
}

NAOS_SUITE(adding,
    NAOS_SET_SETUP(edit),
    NAOS_SET_TEARDOWN(edit),
    NAOS_ADD_TEST(add_child_checks_the_node_limit),
    NAOS_ADD_TEST(split_text_checks_the_node_limit),
    NAOS_ADD_TEST(an_invalid_edit_at_the_node_limit_is_only_rejected),
    NAOS_ADD_TEST(add_child_checks_the_depth_limit),
    NAOS_ADD_TEST(adding_nodes_fails_on_allocation),
    NAOS_ADD_TEST(edits_before_an_invalid_one_stay_and_later_ones_apply)
)

NAOS_SUITE(add_child,
    NAOS_SET_SETUP(edit),
    NAOS_SET_TEARDOWN(edit),
    NAOS_ADD_TEST(add_child_follows_the_table_of_child_classes),
    NAOS_ADD_TEST(add_child_places_the_node_between_its_siblings),
    NAOS_ADD_TEST(add_child_keeps_siblings_in_source_order),
    NAOS_ADD_TEST(add_child_gives_the_same_tree_with_or_without_a_hint),
    NAOS_ADD_TEST(add_child_copies_the_data),
    NAOS_ADD_TEST(add_child_rejects_kinds_the_extension_did_not_register),
    NAOS_ADD_TEST(add_child_rejects_a_hint_that_is_not_a_child_of_the_parent),
    NAOS_ADD_TEST(add_child_rejects_a_range_outside_the_content),
    NAOS_ADD_TEST(add_child_rejects_a_range_outside_the_parent),
    NAOS_ADD_TEST(add_child_rejects_a_range_that_overlaps_a_sibling),
    NAOS_ADD_TEST(add_child_rejects_arguments_the_contract_does_not_define)
)

/* ---------------------------------------------------------------------- */
/* Postprocessing from the parse (amark_dialect_postprocess) */

/*
 * Two extensions with postprocessing rules, over the kinds of the first
 * dialect. Each rule reads the first child of the root, a paragraph. The
 * first adds an empty MARK at its start and then returns a value the
 * contract does not define; the second adds an empty SUB at its end. The
 * third splits the first text of the paragraph and returns
 * AMARK_ERROR_NONE whatever the split returned.
 */
static amark_error_t post_first(const amark_dialect_post_t *post,
                                void *state, amark_dialect_editor_t *editor)
{
    amark_node_id_t para = amark_node_first_child(post->doc,
                                                  amark_document_root(
                                                      post->doc));
    amark_node_id_t out = AMARK_NODE_NONE;
    amark_range_t r;

    (void)state;
    /* Checks only: a rule must not leave the parse through longjmp. */
    if (!amark_node_range(post->doc, para, &r)) {
        NAOS_CHECK_TRUE(false);
        return AMARK_ERROR_NONE;
    }
    r.end = r.start;
    NAOS_CHECK_INT_EQ(amark_dialect_edit_add_child(editor, para,
                                                   AMARK_NODE_NONE, K_MARK,
                                                   r, NULL, 0, &out),
                      AMARK_ERROR_NONE);
    return AMARK_ERROR_LIMIT_EXCEEDED;
}

static amark_error_t post_second(const amark_dialect_post_t *post,
                                 void *state, amark_dialect_editor_t *editor)
{
    amark_node_id_t para = amark_node_first_child(post->doc,
                                                  amark_document_root(
                                                      post->doc));
    amark_node_id_t out = AMARK_NODE_NONE;
    amark_range_t r;

    (void)state;
    /* Checks only: a rule must not leave the parse through longjmp. */
    if (!amark_node_range(post->doc, para, &r)) {
        NAOS_CHECK_TRUE(false);
        return AMARK_ERROR_NONE;
    }
    r.start = r.end;
    NAOS_CHECK_INT_EQ(amark_dialect_edit_add_child(editor, para,
                                                   AMARK_NODE_NONE, K_SUB, r,
                                                   NULL, 0, &out),
                      AMARK_ERROR_NONE);
    return AMARK_ERROR_NONE;
}

static amark_error_t post_split(const amark_dialect_post_t *post,
                                void *state, amark_dialect_editor_t *editor)
{
    amark_node_id_t para = amark_node_first_child(post->doc,
                                                  amark_document_root(
                                                      post->doc));
    amark_node_id_t out = AMARK_NODE_NONE;
    amark_range_t r;

    (void)state;
    if (!amark_node_range(post->doc, amark_node_first_child(post->doc, para),
                          &r)) {
        NAOS_CHECK_TRUE(false);
        return AMARK_ERROR_NONE;
    }
    (void)amark_dialect_edit_split_text(editor,
                                        amark_node_first_child(post->doc,
                                                               para),
                                        r.start + 1, &out);
    return AMARK_ERROR_NONE;
}

static const amark_dialect_extension_t k_post1 = {
    "first", k_kinds1, sizeof(k_kinds1) / sizeof(k_kinds1[0]),
    NULL, 0, NULL, NULL, 0, NULL, 0, post_first, NULL, NULL};

static const amark_dialect_extension_t k_post2 = {
    "second", k_kinds2, sizeof(k_kinds2) / sizeof(k_kinds2[0]),
    NULL, 0, NULL, NULL, 0, NULL, 0, post_second, NULL, NULL};

static const amark_dialect_extension_t k_post_split = {
    "first", k_kinds1, sizeof(k_kinds1) / sizeof(k_kinds1[0]),
    NULL, 0, NULL, NULL, 0, NULL, 0, post_split, NULL, NULL};

static const amark_dialect_extension_t *const k_post_exts[] = {&k_post1,
                                                               &k_post2};
static const amark_dialect_extension_t *const k_split_exts[] = {
    &k_post_split, &k_post2};

/*
 * Rules that make one invalid edit (the root's kind cannot be changed:
 * PROTECTED) and then return: the edit's own result, AMARK_ERROR_INVALID_INPUT
 * without an invalid edit, or another value the contract does not define
 * after the invalid edit.
 */
static amark_error_t post_pass_on(const amark_dialect_post_t *post,
                                  void *state, amark_dialect_editor_t *editor)
{
    (void)state;
    return amark_dialect_edit_set_kind(editor, amark_document_root(post->doc),
                                       K_MARK);
}

static amark_error_t post_invalid_alone(const amark_dialect_post_t *post,
                                        void *state,
                                        amark_dialect_editor_t *editor)
{
    (void)post;
    (void)state;
    (void)editor;
    return AMARK_ERROR_INVALID_INPUT;
}

static amark_error_t post_other_after(const amark_dialect_post_t *post,
                                      void *state,
                                      amark_dialect_editor_t *editor)
{
    (void)state;
    (void)amark_dialect_edit_set_kind(editor, amark_document_root(post->doc),
                                      K_MARK);
    return AMARK_ERROR_LIMIT_EXCEEDED;
}

static const amark_dialect_extension_t k_post_pass_on = {
    "first", k_kinds1, sizeof(k_kinds1) / sizeof(k_kinds1[0]),
    NULL, 0, NULL, NULL, 0, NULL, 0, post_pass_on, NULL, NULL};

static const amark_dialect_extension_t k_post_invalid_alone = {
    "first", k_kinds1, sizeof(k_kinds1) / sizeof(k_kinds1[0]),
    NULL, 0, NULL, NULL, 0, NULL, 0, post_invalid_alone, NULL, NULL};

static const amark_dialect_extension_t k_post_other_after = {
    "first", k_kinds1, sizeof(k_kinds1) / sizeof(k_kinds1[0]),
    NULL, 0, NULL, NULL, 0, NULL, 0, post_other_after, NULL, NULL};

static const amark_dialect_extension_t *const k_pass_on_exts[] = {
    &k_post_pass_on, &k_post2};
static const amark_dialect_extension_t *const k_invalid_alone_exts[] = {
    &k_post_invalid_alone};
static const amark_dialect_extension_t *const k_other_after_exts[] = {
    &k_post_other_after};

static const amark_dialect_t k_pass_on_dialect = {
    sizeof(amark_dialect_t), AMARK_DIALECT_ABI, k_pass_on_exts, 2};
static const amark_dialect_t k_invalid_alone_dialect = {
    sizeof(amark_dialect_t), AMARK_DIALECT_ABI, k_invalid_alone_exts, 1};
static const amark_dialect_t k_other_after_dialect = {
    sizeof(amark_dialect_t), AMARK_DIALECT_ABI, k_other_after_exts, 1};

static const amark_dialect_t k_post_dialect = {
    sizeof(amark_dialect_t), AMARK_DIALECT_ABI, k_post_exts, 2};
static const amark_dialect_t k_split_dialect = {
    sizeof(amark_dialect_t), AMARK_DIALECT_ABI, k_split_exts, 2};

static amark_config_t g_config;
static const char *g_content; /* the content parsed into g_doc */

NAOS_SETUP(parse)
{
    amark_test_alloc_init(&g_state);
    amark_config_init(&g_config);
    g_config.allocator = amark_test_allocator(&g_state);
    g_doc = NULL;
    g_content = NULL;
}

NAOS_TEARDOWN(parse)
{
    g_state.fail_at = 0;
    amark_document_destroy(g_doc);
    g_doc = NULL;
    NAOS_CHECK_UINT_EQ(g_state.live, 0);
    NAOS_CHECK_UINT_EQ(g_state.ctx_mismatch, 0);
}

/*
 * Parses content with dialect: requires err, and on success a tree that
 * passes the check and diagnostics diagnostics; on failure, no document.
 */
static void parse_with(const amark_dialect_t *dialect, const char *content,
                       amark_error_t err, size_t diagnostics)
{
    amark_document_destroy(g_doc);
    g_doc = NULL;
    g_content = content;
    g_config.dialect = dialect;
    NAOS_REQUIRE_INT_EQ(amark_parse(content, strlen(content), &g_config,
                                    &g_doc),
                        err);
    if (err != AMARK_ERROR_NONE) {
        NAOS_CHECK_NULL(g_doc);
        return;
    }
    NAOS_REQUIRE_NOT_NULL(g_doc);
    check_tree();
    NAOS_CHECK_UINT_EQ(amark_document_diagnostic_count(g_doc), diagnostics);
}

/* Checks node: kind, block or inline, over [start, end). */
static void check_node(amark_node_id_t node, amark_kind_t kind, bool block,
                       size_t start, size_t end)
{
    amark_range_t r;

    NAOS_REQUIRE_TRUE(node != AMARK_NODE_NONE);
    NAOS_CHECK_UINT_EQ(amark_node_kind(g_doc, node), kind);
    NAOS_CHECK_TRUE(amark_node_is_block(g_doc, node) == block);
    NAOS_REQUIRE_TRUE(amark_node_range(g_doc, node, &r));
    NAOS_CHECK_UINT_EQ(r.start, start);
    NAOS_CHECK_UINT_EQ(r.end, end);
}

/* Checks that the attributes of node are the string data. */
static void check_data(amark_node_id_t node, const char *data)
{
    size_t size = 99;
    const void *bytes = amark_node_dialect_data(g_doc, node, &size);

    NAOS_REQUIRE_NOT_NULL(bytes);
    NAOS_REQUIRE_UINT_EQ(size, strlen(data));
    NAOS_CHECK_TRUE(memcmp(bytes, data, size) == 0);
}

/*
 * Checks a paragraph over [start, end) that the test dialect made a CHECK
 * in: its children are the CHECK over "[x]", the text " " and then a text
 * that starts after it, each one starting where the one before ends.
 * Returns the third child.
 */
static amark_node_id_t check_checked(amark_node_id_t para, size_t start,
                                     size_t end)
{
    amark_node_id_t check = amark_node_first_child(g_doc, para);
    amark_node_id_t space;
    amark_node_id_t rest;
    amark_node_id_t node;
    amark_range_t prev;
    amark_range_t r;

    check_node(para, AMARK_KIND_PARAGRAPH, true, start, end);
    check_node(check, AMARK_TEST_KIND_CHECK, false, start, start + 3);
    NAOS_CHECK_UINT_EQ(amark_node_first_child(g_doc, check), AMARK_NODE_NONE);
    NAOS_CHECK_UINT_EQ(amark_node_parent(g_doc, check), para);
    space = amark_node_next_sibling(g_doc, check);
    check_node(space, AMARK_KIND_TEXT, false, start + 3, start + 4);
    rest = amark_node_next_sibling(g_doc, space);
    NAOS_REQUIRE_TRUE(rest != AMARK_NODE_NONE);
    NAOS_REQUIRE_TRUE(amark_node_range(g_doc, rest, &r));
    NAOS_CHECK_UINT_EQ(r.start, start + 4);
    /* Siblings in source order, within the paragraph. */
    NAOS_REQUIRE_TRUE(amark_node_range(g_doc, check, &prev));
    for (node = space; node != AMARK_NODE_NONE;
         node = amark_node_next_sibling(g_doc, node)) {
        NAOS_REQUIRE_TRUE(amark_node_range(g_doc, node, &r));
        NAOS_CHECK_UINT_LE(prev.end, r.start);
        NAOS_CHECK_UINT_LE(r.end, end);
        prev = r;
    }
    return rest;
}

/* Checks diagnostic index of g_doc against g_content. */
static void check_parse_diagnostic(size_t index, size_t offset,
                                   const char *message)
{
    amark_diagnostic_t diag;
    size_t line = 1;
    size_t i;

    for (i = 0; i < offset; i++) {
        if (g_content[i] == '\n') {
            line++;
        }
    }
    NAOS_REQUIRE_TRUE(amark_document_diagnostic(g_doc, index, &diag));
    NAOS_CHECK_UINT_EQ(diag.offset, offset);
    NAOS_CHECK_UINT_EQ(diag.line, line);
    NAOS_CHECK_INT_EQ(diag.severity, AMARK_SEVERITY_ERROR);
    NAOS_CHECK_STR_EQ(diag.message, message);
}

#define MSG_TEST "dialect \"test\": postprocess: "

NAOS_TEST(postprocess_makes_checks_and_tasks)
{
    static const char k_in[] = "- [x] done\n"      /* 0 */
                               "- [ ] todo *em*\n" /* 11 */
                               "- plain\n"         /* 27 */
                               "\n"                /* 35 */
                               "[ ] loose\n";      /* 36 */
    amark_node_id_t root;
    amark_node_id_t list;
    amark_node_id_t item;
    amark_node_id_t rest;

    parse_with(amark_test_dialect(), k_in, AMARK_ERROR_NONE, 0);
    root = amark_document_root(g_doc);
    list = amark_node_first_child(g_doc, root);
    NAOS_CHECK_UINT_EQ(amark_node_kind(g_doc, list), AMARK_KIND_LIST);

    /* The list items become TASKs, numbered in document order. */
    item = amark_node_first_child(g_doc, list);
    check_node(item, AMARK_TEST_KIND_TASK, true, 0, 10);
    check_data(item, "1");
    rest = check_checked(amark_node_first_child(g_doc, item), 2, 10);
    check_node(rest, AMARK_KIND_TEXT, false, 6, 10);
    NAOS_CHECK_UINT_EQ(amark_node_next_sibling(g_doc, rest), AMARK_NODE_NONE);

    item = amark_node_next_sibling(g_doc, item);
    check_node(item, AMARK_TEST_KIND_TASK, true, 11, 26);
    check_data(item, "2");
    rest = check_checked(amark_node_first_child(g_doc, item), 13, 26);
    check_node(rest, AMARK_KIND_TEXT, false, 17, 22);
    check_node(amark_node_next_sibling(g_doc, rest), AMARK_KIND_EMPHASIS,
               false, 22, 26);

    /* Without the mark, the item stays. */
    item = amark_node_next_sibling(g_doc, item);
    check_node(item, AMARK_KIND_LIST_ITEM, true, 27, 34);
    NAOS_CHECK_UINT_EQ(amark_node_next_sibling(g_doc, item), AMARK_NODE_NONE);

    /* Outside a list item: the CHECK only. */
    rest = check_checked(amark_node_next_sibling(g_doc, list), 36, 45);
    check_node(rest, AMARK_KIND_TEXT, false, 40, 45);

    NAOS_REQUIRE_UINT_LT(amark_test_dump_tree(g_doc, g_after, SNAPSHOT_SIZE),
                         SNAPSHOT_SIZE);
    NAOS_CHECK_STR_EQ(g_after,
                      "(doc (ul - tight (? (p (?) (text \" \") "
                      "(text \"done\"))) (? (p (?) (text \" \") "
                      "(text \"todo \") (em (text \"em\")))) "
                      "(li (p (text \"plain\")))) "
                      "(p (?) (text \" \") (text \"loose\")))");
}

NAOS_TEST(postprocess_needs_the_mark_at_the_start_of_the_paragraph)
{
    /* The mark alone, inside the text, after an inline, in a later
     * paragraph of the item, and in a box: the CHECK where it starts a
     * paragraph, the TASK only for the first paragraph of an item. */
    static const char k_in[] = "[x]\n"            /* 0 */
                               "\n"               /* 4 */
                               "a [x] b\n"        /* 5 */
                               "\n"               /* 13 */
                               "*e* [x] c\n"      /* 14 */
                               "\n"               /* 24 */
                               "- a\n"            /* 25 */
                               "\n"               /* 29 */
                               "  [x] b\n"        /* 30 */
                               "\n"               /* 38 */
                               "@> [ ] d\n";      /* 39 */
    amark_node_id_t node;
    amark_node_id_t item;

    parse_with(amark_test_dialect(), k_in, AMARK_ERROR_NONE, 0);
    node = amark_node_first_child(g_doc, amark_document_root(g_doc));
    check_node(amark_node_first_child(g_doc, node), AMARK_KIND_TEXT, false,
               0, 3);
    node = amark_node_next_sibling(g_doc, node);
    check_node(amark_node_first_child(g_doc, node), AMARK_KIND_TEXT, false,
               5, 12);
    node = amark_node_next_sibling(g_doc, node);
    check_node(amark_node_first_child(g_doc, node), AMARK_KIND_EMPHASIS,
               false, 14, 17);
    node = amark_node_next_sibling(g_doc, node);
    item = amark_node_first_child(g_doc, node);
    check_node(item, AMARK_KIND_LIST_ITEM, true, 25, 37);
    check_checked(amark_node_next_sibling(g_doc,
                                          amark_node_first_child(g_doc, item)),
                  32, 37);
    node = amark_node_next_sibling(g_doc, node);
    check_node(node, AMARK_TEST_KIND_BOX, true, 39, 47);
    check_checked(amark_node_first_child(g_doc, node), 42, 47);
}

NAOS_TEST(postprocess_numbers_the_tasks_of_each_document_from_one)
{
    static const char k_in[] = "- [x] a\n  - [ ] b\n- [x] c\n";
    amark_node_id_t outer;
    amark_node_id_t inner;
    int i;

    /* The count is per document: the second parse starts again. */
    for (i = 0; i < 2; i++) {
        parse_with(amark_test_dialect(), k_in, AMARK_ERROR_NONE, 0);
        outer = amark_node_first_child(g_doc,
                                       amark_document_root(g_doc));
        outer = amark_node_first_child(g_doc, outer);
        check_node(outer, AMARK_TEST_KIND_TASK, true, 0, 17);
        check_data(outer, "1");
        inner = amark_node_first_child(
            g_doc, amark_node_next_sibling(g_doc,
                                           amark_node_first_child(g_doc,
                                                                  outer)));
        check_node(inner, AMARK_TEST_KIND_TASK, true, 10, 17);
        check_data(inner, "2");
        outer = amark_node_next_sibling(g_doc, outer);
        check_node(outer, AMARK_TEST_KIND_TASK, true, 18, 25);
        check_data(outer, "3");
    }
}

NAOS_TEST(an_invalid_edit_adds_a_diagnostic_and_the_rule_goes_on)
{
    static const char k_in[] = "- [x] !post-order\n" /* 0 */
                               "- [ ] b\n";          /* 18 */
    amark_node_id_t item;

    parse_with(amark_test_dialect(), k_in, AMARK_ERROR_NONE, 1);
    check_parse_diagnostic(0, 2, MSG_TEST R_ORDER);
    /* The edits before it stay; one CHECK only; later paragraphs too. */
    item = amark_node_first_child(g_doc, amark_node_first_child(
                                             g_doc,
                                             amark_document_root(g_doc)));
    check_node(item, AMARK_TEST_KIND_TASK, true, 0, 17);
    check_checked(amark_node_first_child(g_doc, item), 2, 17);
    item = amark_node_next_sibling(g_doc, item);
    check_node(item, AMARK_TEST_KIND_TASK, true, 18, 25);
    check_data(item, "2");
    check_checked(amark_node_first_child(g_doc, item), 20, 25);
}

NAOS_TEST(an_undefined_return_value_adds_a_diagnostic_and_keeps_the_edits)
{
    static const char k_in[] = "x\n"                /* 0 */
                               "\n"                 /* 2 */
                               "[x] !post-status\n" /* 3 */
                               "\n"                 /* 20 */
                               "[x] b\n";           /* 21 */
    amark_node_id_t para;

    parse_with(amark_test_dialect(), k_in, AMARK_ERROR_NONE, 1);
    /* At the start of the parsed range. */
    check_parse_diagnostic(0, 0, MSG_TEST R_STATUS);
    para = amark_node_next_sibling(
        g_doc, amark_node_first_child(g_doc, amark_document_root(g_doc)));
    check_checked(para, 3, 19);
    /* The rule returned before the last paragraph. */
    para = amark_node_next_sibling(g_doc, para);
    check_node(amark_node_first_child(g_doc, para), AMARK_KIND_TEXT, false,
               21, 26);
}

NAOS_TEST(an_undefined_return_value_is_at_the_start_of_the_parsed_range)
{
    static const char k_in[] = "skip\n[x] !post-status\nskip\n";

    g_config.start = 5;
    g_config.end = 22;
    parse_with(amark_test_dialect(), k_in, AMARK_ERROR_NONE, 1);
    check_parse_diagnostic(0, 5, MSG_TEST R_STATUS);
}

NAOS_TEST(an_undefined_return_value_goes_on_with_the_next_extension)
{
    static const char k_in[] = "ab\n";
    amark_node_id_t para;
    amark_node_id_t node;

    parse_with(&k_post_dialect, k_in, AMARK_ERROR_NONE, 1);
    check_parse_diagnostic(0, 0, MSG_FIRST R_STATUS);
    para = amark_node_first_child(g_doc, amark_document_root(g_doc));
    /* The edit of the first rule stays; the second rule ran after it. */
    node = amark_node_first_child(g_doc, para);
    check_node(node, K_MARK, false, 0, 0);
    node = amark_node_next_sibling(g_doc, node);
    check_node(node, AMARK_KIND_TEXT, false, 0, 2);
    node = amark_node_next_sibling(g_doc, node);
    check_node(node, K_SUB, false, 2, 2);
    NAOS_CHECK_UINT_EQ(amark_node_next_sibling(g_doc, node), AMARK_NODE_NONE);
}

NAOS_TEST(an_invalid_edit_passed_on_adds_one_diagnostic)
{
    amark_node_id_t para;

    /* The rule returns what its invalid edit returned: the edit's
     * diagnostic only, and the next extension's rule still runs. */
    parse_with(&k_pass_on_dialect, "ab\n", AMARK_ERROR_NONE, 1);
    check_parse_diagnostic(0, 0, MSG_FIRST R_PROTECTED);
    para = amark_node_first_child(g_doc, amark_document_root(g_doc));
    check_node(amark_node_next_sibling(g_doc,
                                       amark_node_first_child(g_doc, para)),
               K_SUB, false, 2, 2);
}

NAOS_TEST(invalid_input_without_an_invalid_edit_is_an_unknown_value)
{
    parse_with(&k_invalid_alone_dialect, "ab\n", AMARK_ERROR_NONE, 1);
    check_parse_diagnostic(0, 0, MSG_FIRST R_STATUS);
}

NAOS_TEST(another_value_after_an_invalid_edit_is_an_unknown_value)
{
    parse_with(&k_other_after_dialect, "ab\n", AMARK_ERROR_NONE, 2);
    check_parse_diagnostic(0, 0, MSG_FIRST R_PROTECTED);
    check_parse_diagnostic(1, 0, MSG_FIRST R_STATUS);
}

NAOS_TEST(a_rule_that_returns_nomem_ends_the_parse)
{
    parse_with(amark_test_dialect(), "- [x] !post-nomem\n", AMARK_ERROR_NOMEM,
               0);
    /* After edits, and before any. */
    parse_with(amark_test_dialect(), "[ ] !post-nomem\n", AMARK_ERROR_NOMEM,
               0);
}

NAOS_TEST(a_lasting_failure_ends_the_parse_with_its_error)
{
    /* doc, paragraph, text: the split of the test dialect is one too many. */
    g_config.max_node_count = 3;
    parse_with(amark_test_dialect(), "[x] a\n", AMARK_ERROR_LIMIT_EXCEEDED, 0);
    /* Even when the rule returns AMARK_ERROR_NONE after it. */
    parse_with(&k_split_dialect, "ab\n", AMARK_ERROR_LIMIT_EXCEEDED, 0);
    /* With room for the nodes, both succeed. */
    g_config.max_node_count = 6;
    parse_with(amark_test_dialect(), "[x] a\n", AMARK_ERROR_NONE, 0);
    parse_with(&k_split_dialect, "ab\n", AMARK_ERROR_NONE, 0);
}

NAOS_TEST(an_input_with_every_hook_of_the_test_dialect_parses)
{
    static const char k_in[] =
        "@> box $v$ and ~s~\n"
        "@> - [x] task in a box\n"
        "\n"
        "@note: note\n"
        "  more ~~x~~\n"
        "\n"
        "  @@@\n"
        "  fenced\n"
        "  @@@\n"
        "@line *line* $ab$\n"
        "head\n"
        "| a | *b* |\n"
        "| c | `d` |\n"
        "\n"
        "- [ ] todo $x$\n"
        "- [x] done\n";
    static const amark_kind_t k_kinds[] = {
        AMARK_TEST_KIND_BOX,  AMARK_TEST_KIND_NOTE,   AMARK_TEST_KIND_FENCE,
        AMARK_TEST_KIND_LINE, AMARK_TEST_KIND_GRID,   AMARK_TEST_KIND_ROW,
        AMARK_TEST_KIND_CELL, AMARK_TEST_KIND_VAR,    AMARK_TEST_KIND_STRIKE,
        AMARK_TEST_KIND_CHECK, AMARK_TEST_KIND_TASK};
    size_t seen[sizeof k_kinds / sizeof k_kinds[0]];
    amark_node_id_t node;
    size_t i;

    parse_with(amark_test_dialect(), k_in, AMARK_ERROR_NONE, 0);
    memset(seen, 0, sizeof seen);
    for (node = 1; node <= amark_document_node_count(g_doc); node++) {
        for (i = 0; i < sizeof k_kinds / sizeof k_kinds[0]; i++) {
            if (amark_node_kind(g_doc, node) == k_kinds[i]) {
                seen[i]++;
            }
        }
    }
    for (i = 0; i < sizeof k_kinds / sizeof k_kinds[0]; i++) {
        NAOS_CHECK_UINT_GT(seen[i], 0);
    }
    NAOS_CHECK_UINT_EQ(seen[10], 3); /* TASK */
    NAOS_CHECK_UINT_EQ(seen[9], 3);  /* CHECK */
}

NAOS_SUITE(postprocess,
    NAOS_SET_SETUP(parse),
    NAOS_SET_TEARDOWN(parse),
    NAOS_ADD_TEST(postprocess_makes_checks_and_tasks),
    NAOS_ADD_TEST(postprocess_needs_the_mark_at_the_start_of_the_paragraph),
    NAOS_ADD_TEST(postprocess_numbers_the_tasks_of_each_document_from_one),
    NAOS_ADD_TEST(an_invalid_edit_adds_a_diagnostic_and_the_rule_goes_on),
    NAOS_ADD_TEST(
        an_undefined_return_value_adds_a_diagnostic_and_keeps_the_edits),
    NAOS_ADD_TEST(
        an_undefined_return_value_is_at_the_start_of_the_parsed_range),
    NAOS_ADD_TEST(an_undefined_return_value_goes_on_with_the_next_extension),
    NAOS_ADD_TEST(an_invalid_edit_passed_on_adds_one_diagnostic),
    NAOS_ADD_TEST(
        invalid_input_without_an_invalid_edit_is_an_unknown_value),
    NAOS_ADD_TEST(another_value_after_an_invalid_edit_is_an_unknown_value),
    NAOS_ADD_TEST(a_rule_that_returns_nomem_ends_the_parse),
    NAOS_ADD_TEST(a_lasting_failure_ends_the_parse_with_its_error),
    NAOS_ADD_TEST(an_input_with_every_hook_of_the_test_dialect_parses)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(common),
    NAOS_ADD_SUITE(set_kind),
    NAOS_ADD_SUITE(set_data),
    NAOS_ADD_SUITE(shrink),
    NAOS_ADD_SUITE(add_child),
    NAOS_ADD_SUITE(split_text),
    NAOS_ADD_SUITE(adding),
    NAOS_ADD_SUITE(postprocess)
)
