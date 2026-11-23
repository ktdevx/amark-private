# Copyright (c) The Amark Authors
# SPDX-License-Identifier: MIT

# amark's tests. Included only when AMARK_BUILD_TESTS is ON.
#
# Each test file is one executable with its own NAOS_MAIN; there is no shared
# main.c. Register a test with one line:
#
#   amark_add_unit_test(<name> SOURCES <source>... [LIBRARIES <library>...])
#       Tests of internal functions. Links amark_testing (the test-only static
#       library built from the same sources as amark, with src/ as an include
#       directory).
#
#   amark_add_public_test(<name> SOURCES <source>...)
#       Tests of the public API only. Links Amark::amark.
#
# Naos registers every test with CTest as <name>.<suite>.<test>, after the
# executable is built.

include(FetchContent)
include(CheckCSourceCompiles)

# Naos is pinned by commit hash, not by tag name, so the fetched content
# cannot change.
FetchContent_Declare(Naos
    GIT_REPOSITORY https://github.com/vabelorg/naos.git
    GIT_TAG 09fde7172c4b63d416c32e98499cb0c0306d3dea  # v0.4.0
)
FetchContent_MakeAvailable(Naos)

# A static Naos uses the C math functions (fabs, isnan, isinf, signbit), and
# its CMake target does not link the math library. Link "m" into the tests
# only where those do not link without it but do link with it (some Unix
# systems). Elsewhere (MSVC, macOS, glibc with the calls inlined) nothing is
# added.
set(AMARK_TEST_LINK_LIBM OFF)
set(_amark_libm_probe [[
#include <math.h>
int main(void)
{
    volatile double x = -1.5;
    return (fabs(x) > 1.0 && !isnan(x) && !isinf(x) && signbit(x)) ? 0 : 1;
}
]])
check_c_source_compiles("${_amark_libm_probe}" AMARK_MATH_WITHOUT_LIBM)
if(NOT AMARK_MATH_WITHOUT_LIBM)
    set(CMAKE_REQUIRED_LIBRARIES m)
    check_c_source_compiles("${_amark_libm_probe}" AMARK_MATH_WITH_LIBM)
    unset(CMAKE_REQUIRED_LIBRARIES)
    if(AMARK_MATH_WITH_LIBM)
        set(AMARK_TEST_LINK_LIBM ON)
    endif()
endif()
unset(_amark_libm_probe)

# Settings shared by every test executable.
function(_amark_test_target_settings target)
    amark_target_defaults("${target}")
    if(AMARK_TEST_LINK_LIBM)
        target_link_libraries("${target}" PRIVATE m)
    endif()
endfunction()

# amark_add_unit_test(<name> SOURCES <source>... [LIBRARIES <library>...])
function(amark_add_unit_test name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;LIBRARIES")
    if(arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "amark_add_unit_test: unknown arguments: ${arg_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT arg_SOURCES)
        message(FATAL_ERROR "amark_add_unit_test: SOURCES is required")
    endif()

    naos_add_executable("${name}"
        SOURCES ${arg_SOURCES}
        LIBRARIES amark_testing ${arg_LIBRARIES}
    )
    _amark_test_target_settings("${name}")
endfunction()

# amark_add_public_test(<name> SOURCES <source>...)
function(amark_add_public_test name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES")
    if(arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "amark_add_public_test: unknown arguments: ${arg_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT arg_SOURCES)
        message(FATAL_ERROR "amark_add_public_test: SOURCES is required")
    endif()

    naos_add_executable("${name}"
        SOURCES ${arg_SOURCES}
        LIBRARIES Amark::amark
    )
    _amark_test_target_settings("${name}")
endfunction()

# Unit tests (tests/unit/), linked to amark_testing.

# Public API tests (tests/public/), linked to Amark::amark.
