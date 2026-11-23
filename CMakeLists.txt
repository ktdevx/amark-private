# Copyright (c) The Amark Authors
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.21)

# The project version is defined here and only here.
project(Amark VERSION 0.1.0 LANGUAGES C)

# Options. BUILD_SHARED_LIBS (CMake's standard option, default OFF) selects a
# static or shared library.
option(AMARK_BUILD_TESTS
    "Build Amark's tests (fetches Naos)" ${PROJECT_IS_TOP_LEVEL})
option(AMARK_WARNINGS_AS_ERRORS
    "Treat compiler warnings as errors for Amark's targets" OFF)
option(AMARK_SANITIZE
    "Build with AddressSanitizer and UndefinedBehaviorSanitizer (GCC/Clang only)" OFF)
option(AMARK_INSTALL
    "Generate install rules for Amark" ${PROJECT_IS_TOP_LEVEL})

# Put executables and DLLs in one directory so that test executables can find
# the Amark DLL when they run (Naos runs them right after the build).
if(PROJECT_IS_TOP_LEVEL AND NOT DEFINED CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
endif()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

# Defines amark_target_defaults() and applies the sanitizer flags to this
# directory scope, before any target (amark, tests, Naos) is defined.
include(AmarkCompileOptions)

add_subdirectory(src)

# Tests. Naos is fetched in tests/ and only when AMARK_BUILD_TESTS is ON, so a
# build without tests needs no network.
if(AMARK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Install rules: the library, the public and generated headers, the package
# files for find_package(Amark) and LICENSE. Internal headers in src/, the
# test-only libraries, the tests and Naos are never installed.
if(AMARK_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    set(AMARK_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/Amark")

    install(TARGETS amark
        EXPORT AmarkTargets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )

    # Public headers (the committed ones and the generated export.h and
    # version.h).
    install(
        DIRECTORY
            "${PROJECT_SOURCE_DIR}/include/amark/"
            "${PROJECT_BINARY_DIR}/include/amark/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/amark"
        FILES_MATCHING PATTERN "*.h"
    )

    install(EXPORT AmarkTargets
        NAMESPACE Amark::
        DESTINATION "${AMARK_INSTALL_CMAKEDIR}"
    )

    configure_package_config_file(
        "${PROJECT_SOURCE_DIR}/cmake/AmarkConfig.cmake.in"
        "${PROJECT_BINARY_DIR}/AmarkConfig.cmake"
        INSTALL_DESTINATION "${AMARK_INSTALL_CMAKEDIR}"
    )

    # Before 1.0 a minor version may break compatibility. Change this to
    # SameMajorVersion at 1.0.
    write_basic_package_version_file(
        "${PROJECT_BINARY_DIR}/AmarkConfigVersion.cmake"
        VERSION "${PROJECT_VERSION}"
        COMPATIBILITY SameMinorVersion
    )

    install(
        FILES
            "${PROJECT_BINARY_DIR}/AmarkConfig.cmake"
            "${PROJECT_BINARY_DIR}/AmarkConfigVersion.cmake"
        DESTINATION "${AMARK_INSTALL_CMAKEDIR}"
    )

    install(FILES "${PROJECT_SOURCE_DIR}/LICENSE"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/amark"
    )
endif()
