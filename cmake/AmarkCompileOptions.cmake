# Copyright (c) The Amark Authors
# SPDX-License-Identifier: MIT

# Common compile settings for Amark's own targets.
#
# Including this module:
#   - applies the sanitizer flags to the including directory scope when
#     AMARK_SANITIZE is ON (so every target below it, Naos included, gets them);
#   - defines amark_target_defaults(<target>).
#
# The including scope must define AMARK_WARNINGS_AS_ERRORS and AMARK_SANITIZE.

include_guard(DIRECTORY)

if(AMARK_SANITIZE)
    if(CMAKE_C_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$" AND NOT MSVC)
        set(_amark_sanitize_flags
            -fsanitize=address,undefined
            -fno-sanitize-recover=all
            -fno-omit-frame-pointer
        )
        add_compile_options(${_amark_sanitize_flags})
        add_link_options(${_amark_sanitize_flags})
        unset(_amark_sanitize_flags)
    else()
        message(FATAL_ERROR
            "AMARK_SANITIZE=ON requires GCC or Clang (AddressSanitizer and "
            "UndefinedBehaviorSanitizer with GCC/Clang flags), but the C "
            "compiler is '${CMAKE_C_COMPILER_ID}'. Configure with "
            "-DAMARK_SANITIZE=OFF or use GCC or Clang.")
    endif()
endif()

# amark_target_defaults(<target>)
#
# Applies the settings shared by every target Amark owns (the library, the
# testing library, test support and tests): C99 without extensions, hidden
# symbol visibility by default, a high warning level, and warnings as errors
# when AMARK_WARNINGS_AS_ERRORS is ON. Not applied to third-party targets.
function(amark_target_defaults target)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "amark_target_defaults: '${target}' is not a target")
    endif()

    set_target_properties("${target}" PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
        C_VISIBILITY_PRESET hidden
    )

    if(MSVC)
        target_compile_options("${target}" PRIVATE /W4 /utf-8)
        if(AMARK_WARNINGS_AS_ERRORS)
            target_compile_options("${target}" PRIVATE /WX)
        endif()
    else()
        target_compile_options("${target}" PRIVATE -Wall -Wextra -Wpedantic)
        if(AMARK_WARNINGS_AS_ERRORS)
            target_compile_options("${target}" PRIVATE -Werror)
        endif()
    endif()
endfunction()
