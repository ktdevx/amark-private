/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef AMARK_DRAFT_H
#define AMARK_DRAFT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <amark/error.h>
#include <amark/tree.h>

#include "arena.h"
#include "array.h"

/*
 * The draft: the working tree of items of one inline content (a paragraph's
 * or a heading's), kept in the scratch arena. The inline parser builds it
 * and then writes it to the document as inline nodes, in one pass.
 *
 * Items are elements of an array linked by their index (the ID). ID 0
 * (AMARK_DRAFT_NONE) means "no item" and is never used; ID 1
 * (AMARK_DRAFT_ROOT) is the root, which stands for the paragraph or the
 * heading. The parser appends every item as the last child of the root;
 * emphasis, links and images are made by wrapping a part of the root's
 * children in a new item.
 *
 * The functions here never touch a document; only the emit step does. None
 * of them recurses or keeps global state.
 */
typedef uint32_t amark_draft_id_t;
#define AMARK_DRAFT_NONE ((amark_draft_id_t)0)
#define AMARK_DRAFT_ROOT ((amark_draft_id_t)1)

/* The attribute of a link or an image. */
typedef struct amark_draft_link {
    amark_link_form_t form;
    bool has_destination;
    amark_range_t destination;
    bool has_title;
    amark_range_t title;
    amark_node_id_t definition; /* reference forms only; otherwise
                                   AMARK_NODE_NONE */
} amark_draft_link_t;

typedef struct amark_draft_item {
    amark_kind_t kind;   /* an inline kind or a dialect kind;
                            AMARK_KIND_NONE for the root */
    amark_draft_id_t parent;
    amark_draft_id_t first_child;
    amark_draft_id_t last_child;
    amark_draft_id_t prev;
    amark_draft_id_t next;
    size_t start;
    size_t end;
    uint32_t attr;        /* links and images: the index in links plus 1.
                             Autolinks: the type plus 1. Otherwise 0 */
    amark_node_id_t node; /* the node written for the item; only the emit
                             step writes it */
} amark_draft_item_t;

/*
 * The members are visible in src/ but other modules use the functions only.
 */
typedef struct amark_draft {
    amark_array_t items; /* amark_draft_item_t; 0 is unused, 1 is the root */
    amark_array_t links; /* amark_draft_link_t */
} amark_draft_t;

/*
 * Initializes draft with its arrays in scratch. Allocates nothing; the draft
 * has no root until amark_draft_reset succeeds.
 */
void amark_draft_init(amark_draft_t *draft, amark_arena_t *scratch);

/*
 * Empties the draft and leaves only the root (kind AMARK_KIND_NONE, range
 * [0, 0), no children). The places of the arrays are kept, so the items and
 * links of the next content reuse them. Fails with AMARK_ERROR_NOMEM or
 * AMARK_ERROR_LIMIT_EXCEEDED only when the arrays have no place yet (the
 * first reset) and the arena cannot give one; the draft is then empty.
 */
amark_error_t amark_draft_reset(amark_draft_t *draft);

/*
 * Appends a new item of kind with the range [start, end) as the last child
 * of the root, and stores its ID in *out. On failure (AMARK_ERROR_NOMEM, or
 * AMARK_ERROR_LIMIT_EXCEEDED when the number of items has reached
 * UINT32_MAX - 1), nothing changes and *out is not written.
 */
amark_error_t amark_draft_append(amark_draft_t *draft,
                                 amark_kind_t kind, size_t start,
                                 size_t end,
                                 amark_draft_id_t *out);

/*
 * Moves the siblings from the one after after to the one before before (to
 * the last child if before is AMARK_DRAFT_NONE) into a new item of kind with
 * the range [start, end), puts the new item right after after, and stores
 * its ID in *out. The siblings between may be none; the new item then has
 * no children.
 *
 * Preconditions: after is a child of the root; before is AMARK_DRAFT_NONE or
 * a child of the root that comes after after.
 *
 * Takes time in proportion to the number of siblings moved. On failure
 * (as amark_draft_append), nothing has moved and *out is not written.
 */
amark_error_t amark_draft_wrap(amark_draft_t *draft,
                               amark_draft_id_t after,
                               amark_draft_id_t before,
                               amark_kind_t kind, size_t start,
                               size_t end,
                               amark_draft_id_t *out);

/*
 * Takes the item id (with its children) out of its parent's list of
 * children. The element stays in the array and is not reused before the
 * next amark_draft_reset. id must be an item appended or made by wrapping,
 * and not already removed.
 */
void amark_draft_remove(amark_draft_t *draft,
                        amark_draft_id_t id);

/* Sets the range of the item id (not the root) to [start, end). */
void amark_draft_set_range(amark_draft_t *draft,
                           amark_draft_id_t id,
                           size_t start, size_t end);

/*
 * Gives the item id (a link or an image) a copy of *attr. On failure
 * (AMARK_ERROR_NOMEM, or AMARK_ERROR_LIMIT_EXCEEDED when the number of links
 * cannot be represented), the item is unchanged.
 */
amark_error_t amark_draft_set_link(amark_draft_t *draft,
                                   amark_draft_id_t id,
                                   const amark_draft_link_t *attr);

/* Gives the item id (an autolink) the type type. Never fails. */
void amark_draft_set_autolink(amark_draft_t *draft,
                              amark_draft_id_t id,
                              amark_autolink_type_t type);

/*
 * Returns the item id (the root or an item appended or made by wrapping).
 * The pointer stays valid until the next item is added.
 */
const amark_draft_item_t *amark_draft_get(const amark_draft_t *draft,
                                          amark_draft_id_t id);

#endif /* AMARK_DRAFT_H */
