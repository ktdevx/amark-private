# Copyright (c) The Amark Authors
# SPDX-License-Identifier: MIT

# Amark's tests. Included only when AMARK_BUILD_TESTS is ON.
#
# Each test file is one executable with its own NAOS_MAIN; there is no shared
# main.c. Register a test with one line:
#
#   amark_add_unit_test(<name> SOURCES <source>... [LIBRARIES <library>...])
#       Tests of internal functions. Links amark_testing (the test-only static
#       library built from the same sources as amark, with src/ as an include
#       directory) and amark_test_support (the counting, failing allocator of
#       tests/support/).
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

# amark_test_support: the counting, failing test allocator ("test_alloc.h").
# A test-only static library, never installed.
add_library(amark_test_support STATIC support/test_alloc.c)
amark_target_defaults(amark_test_support)
target_include_directories(amark_test_support
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/support"
)
target_link_libraries(amark_test_support PUBLIC amark_testing)

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
        LIBRARIES amark_testing amark_test_support ${arg_LIBRARIES}
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

# C++ is enabled here only, and only when a C++ compiler is found, to check
# that the public headers compile as C++. The library itself stays C-only.
include(CheckLanguage)
check_language(CXX)
if(CMAKE_CXX_COMPILER)
    enable_language(CXX)
endif()

# Unit tests (tests/unit/), linked to amark_testing and amark_test_support.
amark_add_unit_test(test_alloc SOURCES unit/test_alloc.c)
amark_add_unit_test(test_arena SOURCES unit/test_arena.c)
amark_add_unit_test(test_array SOURCES unit/test_array.c)
amark_add_unit_test(test_line_index SOURCES unit/test_line_index.c)
amark_add_unit_test(test_utf8 SOURCES unit/test_utf8.c)
amark_add_unit_test(test_unicode SOURCES unit/test_unicode.c)
amark_add_unit_test(test_entity SOURCES unit/test_entity.c)

# Tests that read the input data of the generated tables (tools/data/) to
# build their expected values get its location as a C string literal.
foreach(_amark_data_test IN ITEMS test_unicode test_entity)
    target_compile_definitions("${_amark_data_test}" PRIVATE
        "AMARK_TEST_DATA_DIR=\"${PROJECT_SOURCE_DIR}/tools/data\""
    )
endforeach()
unset(_amark_data_test)

# Public API tests (tests/public/), linked to Amark::amark.
amark_add_public_test(test_version SOURCES public/test_version.c)

# amark_target_defaults sets only C properties (C99 and so on); its warning
# flags (-Wall -Wextra -Wpedantic [-Werror], /W4 [/WX]) apply to C++ too. The
# C++ standard is set here: C++11 without extensions, as Naos requires.
if(CMAKE_CXX_COMPILER)
    amark_add_public_test(header_cxx SOURCES public/header_cxx.cpp)
    set_target_properties(header_cxx PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
endif()

# Repository checks (tests/checks/), cmake -P scripts that read the built
# libraries with nm. Registered on Linux only, and only where nm is found.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(CMAKE_NM)
        set(AMARK_NM "${CMAKE_NM}")
    else()
        find_program(AMARK_NM nm)
    endif()
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND AMARK_NM)
    # The exported symbols of the shared library match the list exactly.
    get_target_property(_amark_type amark TYPE)
    if(_amark_type STREQUAL "SHARED_LIBRARY")
        add_test(NAME checks.exports
            COMMAND "${CMAKE_COMMAND}"
                "-DNM=${AMARK_NM}"
                "-DLIBRARY=$<TARGET_FILE:amark>"
                "-DSYMBOLS_FILE=${CMAKE_CURRENT_SOURCE_DIR}/checks/exported_symbols.txt"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/checks/CheckExports.cmake"
        )
    endif()
    unset(_amark_type)

    # No mutable static data, no abort/exit/longjmp, malloc/free only in
    # alloc.c. Sanitizers add static data and references, and link-time
    # optimization hides local symbols from nm, so not with either of them.
    if(NOT AMARK_SANITIZE AND NOT CMAKE_INTERPROCEDURAL_OPTIMIZATION)
        add_test(NAME checks.objects
            COMMAND "${CMAKE_COMMAND}"
                "-DNM=${AMARK_NM}"
                "-DLIBRARY=$<TARGET_FILE:amark_testing>"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/checks/CheckObjects.cmake"
        )
    endif()
endif()

# Every C, C++ and CMake source and template file in the repository carries
# the license header in its first 5 lines. Reads only files, so registered in
# every configuration with tests, whatever the compiler or OS. The build
# directory is skipped, also when it lies inside the source tree.
add_test(NAME checks.license_headers
    COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "-DBINARY_DIR=${CMAKE_BINARY_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/checks/CheckLicenseHeaders.cmake"
)

# The generated tables (src/unicode_tables.h, src/entity_table.h) match what
# tools/gen_tables.py makes from the input data in tools/data/, and the
# generator passes its own tests. Registered only where Python 3.9 or later is
# found; the build itself never runs Python. Neither writes to the source
# tree (-B: no __pycache__; the self-test writes only to a temporary
# directory).
find_package(Python3 3.9 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME checks.generated_tables
        COMMAND "${Python3_EXECUTABLE}" -B
            "${PROJECT_SOURCE_DIR}/tools/gen_tables.py" --check
    )
    add_test(NAME checks.generator
        COMMAND "${Python3_EXECUTABLE}" -B
            "${PROJECT_SOURCE_DIR}/tools/test_gen_tables.py"
    )
endif()
