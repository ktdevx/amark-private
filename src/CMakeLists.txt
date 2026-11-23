# Copyright (c) The Amark Authors
# SPDX-License-Identifier: MIT

include(GenerateExportHeader)
include(GNUInstallDirs)

# The library's sources. Both amark and the test-only static library are
# built from this one list; a new module adds one line here.
set(AMARK_SOURCES
    alloc.c
    version.c
)

# Generated public headers go to <build>/include/amark/ and are not committed.
set(AMARK_GENERATED_INCLUDE_DIR "${PROJECT_BINARY_DIR}/include")

configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/version.h.in"
    "${AMARK_GENERATED_INCLUDE_DIR}/amark/version.h"
    @ONLY
)

# The library. BUILD_SHARED_LIBS selects static or shared.
add_library(amark ${AMARK_SOURCES})
add_library(Amark::amark ALIAS amark)
amark_target_defaults(amark)

set_target_properties(amark PROPERTIES
    VERSION "${PROJECT_VERSION}"
    SOVERSION 0
)

target_include_directories(amark
    PUBLIC
        "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
        "$<BUILD_INTERFACE:${AMARK_GENERATED_INCLUDE_DIR}>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

generate_export_header(amark
    EXPORT_MACRO_NAME AMARK_API
    EXPORT_FILE_NAME "${AMARK_GENERATED_INCLUDE_DIR}/amark/export.h"
)

# For a static library, AMARK_API must expand to nothing on the consumer side
# too (no dllimport), so the define is part of the target's public usage.
get_target_property(_amark_type amark TYPE)
if(_amark_type STREQUAL "STATIC_LIBRARY")
    target_compile_definitions(amark PUBLIC AMARK_STATIC_DEFINE)
endif()
unset(_amark_type)

# The test-only static library, built from the same sources as amark. Unit
# tests link it to call internal functions (declared in the headers in src/),
# whatever the type of amark. It is never installed.
if(AMARK_BUILD_TESTS)
    add_library(amark_testing STATIC ${AMARK_SOURCES})
    amark_target_defaults(amark_testing)

    target_include_directories(amark_testing
        PUBLIC
            "${PROJECT_SOURCE_DIR}/include"
            "${AMARK_GENERATED_INCLUDE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    # AMARK_API expands to nothing in the library and in the tests.
    target_compile_definitions(amark_testing PUBLIC AMARK_STATIC_DEFINE)
endif()
