/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/* Public API test: links Amark::amark and uses only <amark/amark.h>. */

#include <amark/amark.h>

#include <naos/naos.h>

NAOS_TEST(string_matches_header)
{
    NAOS_CHECK_STR_EQ(AMARK_VERSION_STRING, amark_version_string());
}

NAOS_SUITE(version,
    NAOS_ADD_TEST(string_matches_header)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(version)
)
