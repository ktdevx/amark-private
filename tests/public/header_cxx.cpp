/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Public API test in C++: the public header compiles as C++,
 * amark_version_string links (C linkage) and returns the header's version,
 * and the allocator types can be filled in from C++.
 */

#include <amark/amark.h>

#include <naos/naos.h>

#include <stddef.h>

namespace {

int allocator_ctx;

void *allocator_malloc(void *ctx, size_t size)
{
    (void)ctx;
    (void)size;
    return NULL;
}

void allocator_free(void *ctx, void *ptr)
{
    (void)ctx;
    (void)ptr;
}

} // namespace

NAOS_TEST(version_string)
{
    NAOS_CHECK_STR_EQ(AMARK_VERSION_STRING, amark_version_string());
}

NAOS_TEST(allocator_types)
{
    amark_malloc_fn_t malloc_fn = allocator_malloc;
    amark_free_fn_t free_fn = allocator_free;
    amark_allocator_t allocator = {malloc_fn, free_fn, &allocator_ctx};

    NAOS_CHECK_PTR_EQ(&allocator_ctx, allocator.ctx);
    NAOS_CHECK_TRUE(allocator.malloc_fn == allocator_malloc);
    NAOS_CHECK_TRUE(allocator.free_fn == allocator_free);
}

NAOS_SUITE(header_cxx,
    NAOS_ADD_TEST(version_string),
    NAOS_ADD_TEST(allocator_types)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(header_cxx)
)
