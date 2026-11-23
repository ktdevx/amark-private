# Copyright (c) The Amark Authors
# SPDX-License-Identifier: MIT

# A small project that uses Amark the way a user would. It is a standalone
# project, not part of Amark's own build.
#
#   AMARK_CONSUMER_MODE=package (default)
#       Finds an installed Amark with find_package(Amark). Pass the install
#       prefix in CMAKE_PREFIX_PATH. The prefix must be one that holds only
#       Amark (the check for internal headers looks at every file in it).
#
#   AMARK_CONSUMER_MODE=subdirectory
#       Builds Amark from source with add_subdirectory(). AMARK_SOURCE_DIR
#       points to Amark's source tree (default: this repository).
#
# Both modes link the same target, Amark::amark, and register one CTest test.

cmake_minimum_required(VERSION 3.21)

project(AmarkConsumer LANGUAGES C)

set(AMARK_CONSUMER_MODE "package" CACHE STRING
    "How to get amark: package (find_package) or subdirectory (add_subdirectory)")
set_property(CACHE AMARK_CONSUMER_MODE PROPERTY STRINGS package subdirectory)

get_filename_component(_amark_default_source_dir
    "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(AMARK_SOURCE_DIR "${_amark_default_source_dir}" CACHE PATH
    "Amark's source tree (used by the subdirectory mode and to list internal headers)")
unset(_amark_default_source_dir)

if(AMARK_CONSUMER_MODE STREQUAL "package")
    find_package(Amark 0.1 REQUIRED)
    message(STATUS "Found Amark ${Amark_VERSION} in ${Amark_DIR}")

    # Version compatibility: a much newer version must not be found.
    # find_package() sets Amark_FOUND again, so check and restore it.
    find_package(Amark 99.0 QUIET)
    if(Amark_FOUND)
        message(FATAL_ERROR
            "find_package(Amark 99.0) found Amark ${Amark_VERSION}; "
            "the package version check does not reject incompatible versions")
    endif()
    set(Amark_FOUND TRUE)

    # The install prefix must not contain Amark's internal headers (src/*.h).
    # The prefix is the parent of the installed include directory.
    get_target_property(_amark_include_dirs Amark::amark
        INTERFACE_INCLUDE_DIRECTORIES)
    list(GET _amark_include_dirs 0 _amark_include_dir)
    get_filename_component(_amark_prefix "${_amark_include_dir}" DIRECTORY)

    file(GLOB _amark_internal_headers
        LIST_DIRECTORIES false
        RELATIVE "${AMARK_SOURCE_DIR}/src"
        "${AMARK_SOURCE_DIR}/src/*.h"
    )
    if(NOT _amark_internal_headers)
        message(FATAL_ERROR
            "No internal headers found in ${AMARK_SOURCE_DIR}/src; "
            "set AMARK_SOURCE_DIR to Amark's source tree")
    endif()

    file(GLOB_RECURSE _amark_installed_files
        LIST_DIRECTORIES false
        "${_amark_prefix}/*"
    )
    set(_amark_leaked "")
    foreach(_file IN LISTS _amark_installed_files)
        get_filename_component(_name "${_file}" NAME)
        if(_name IN_LIST _amark_internal_headers)
            list(APPEND _amark_leaked "${_file}")
        endif()
    endforeach()
    if(_amark_leaked)
        list(JOIN _amark_leaked "\n  " _amark_leaked_text)
        message(FATAL_ERROR
            "Internal headers are installed in ${_amark_prefix}:\n"
            "  ${_amark_leaked_text}")
    endif()
    message(STATUS "No internal headers in ${_amark_prefix}")

    unset(_amark_include_dirs)
    unset(_amark_include_dir)
    unset(_amark_prefix)
    unset(_amark_internal_headers)
    unset(_amark_installed_files)
    unset(_amark_leaked)
    unset(_amark_leaked_text)
elseif(AMARK_CONSUMER_MODE STREQUAL "subdirectory")
    add_subdirectory("${AMARK_SOURCE_DIR}" amark)

    if(NOT TARGET Amark::amark)
        message(FATAL_ERROR "add_subdirectory(amark) did not define Amark::amark")
    endif()

    # As a subproject, Amark must not build its tests (so Naos is not
    # fetched) and must not turn warnings into errors.
    if(AMARK_BUILD_TESTS)
        message(FATAL_ERROR "AMARK_BUILD_TESTS is ON in a subproject build")
    endif()
    if(AMARK_WARNINGS_AS_ERRORS)
        message(FATAL_ERROR "AMARK_WARNINGS_AS_ERRORS is ON in a subproject build")
    endif()
    if(TARGET naos OR TARGET Naos::naos)
        message(FATAL_ERROR "A Naos target exists in a subproject build")
    endif()
else()
    message(FATAL_ERROR
        "AMARK_CONSUMER_MODE must be package or subdirectory, "
        "not \"${AMARK_CONSUMER_MODE}\"")
endif()

add_executable(consumer main.c)
set_target_properties(consumer PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_link_libraries(consumer PRIVATE Amark::amark)

# Windows has no rpath: put the Amark DLL next to the executable.
get_target_property(_amark_type Amark::amark TYPE)
if(WIN32 AND _amark_type STREQUAL "SHARED_LIBRARY")
    add_custom_command(TARGET consumer POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy
            "$<TARGET_RUNTIME_DLLS:consumer>" "$<TARGET_FILE_DIR:consumer>"
        COMMAND_EXPAND_LISTS
    )
endif()
unset(_amark_type)

enable_testing()
add_test(NAME consumer COMMAND consumer)
