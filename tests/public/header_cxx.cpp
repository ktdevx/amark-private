/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */

/*
 * Public API test in C++: the public header compiles as C++ and
 * amark_version_string links (C linkage) and returns the header's version.
 */

#include <amark/amark.h>

#include <naos/naos.h>

NAOS_TEST(version_string)
{
    NAOS_CHECK_STR_EQ(AMARK_VERSION_STRING, amark_version_string());
}

NAOS_SUITE(header_cxx,
    NAOS_ADD_TEST(version_string)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(header_cxx)
)
