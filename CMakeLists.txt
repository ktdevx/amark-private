# Copyright (c) The Amark Authors
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.21)

# The project version is defined here and only here.
project(Amark VERSION 0.1.0 LANGUAGES C)

# Options. BUILD_SHARED_LIBS (CMake's standard option, default OFF) selects a
# static or shared library.
option(AMARK_BUILD_TESTS
    "Build amark's tests (fetches Naos)" ${PROJECT_IS_TOP_LEVEL})
option(AMARK_WARNINGS_AS_ERRORS
    "Treat compiler warnings as errors for amark's targets" OFF)
option(AMARK_SANITIZE
    "Build with AddressSanitizer and UndefinedBehaviorSanitizer (GCC/Clang only)" OFF)
option(AMARK_INSTALL
    "Generate install rules for amark" ${PROJECT_IS_TOP_LEVEL})

# Put executables and DLLs in one directory so that test executables can find
# the amark DLL when they run (Naos runs them right after the build).
if(PROJECT_IS_TOP_LEVEL AND NOT DEFINED CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
endif()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

# Defines amark_target_defaults() and applies the sanitizer flags to this
# directory scope, before any target (amark, tests, Naos) is defined.
include(AmarkCompileOptions)

add_subdirectory(src)
