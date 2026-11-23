# Copyright (c) The Amark Authors
# SPDX-License-Identifier: MIT

# checks.objects: the objects of the test-only static library (built from the
# same sources as amark)
#   - define no mutable static data (nm types b B C d D g G s S); read-only
#     data (r R) is allowed;
#   - reference none of abort, exit, _Exit, quick_exit, longjmp, setjmp and
#     their variants, nor __assert_fail (assert);
#   - reference malloc, calloc, realloc, aligned_alloc and free only from the
#     allocator abstraction's object (alloc.c).
#
#   cmake -DNM=<nm> -DLIBRARY=<libamark_testing.a> -P CheckObjects.cmake
#
# Sanitizer builds add static data and references, and link-time optimization
# hides local symbols from nm, so this check is not registered for them. Only
# reads files.

cmake_minimum_required(VERSION 3.21)

foreach(var IN ITEMS NM LIBRARY)
    if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
        message(FATAL_ERROR "CheckObjects.cmake: -D${var}=... is required")
    endif()
endforeach()

set(mutable_data_types "^[bBCdDgGsS]$")
set(forbidden_functions
    abort exit _exit _Exit quick_exit
    longjmp _longjmp siglongjmp __longjmp_chk
    setjmp _setjmp sigsetjmp __sigsetjmp
    __assert_fail
)
set(allocation_functions malloc calloc realloc aligned_alloc free)
set(allocator_object "alloc.c.o")

execute_process(
    COMMAND "${NM}" "${LIBRARY}"
    OUTPUT_VARIABLE nm_output
    ERROR_VARIABLE nm_error
    RESULT_VARIABLE nm_result
)
if(NOT nm_result EQUAL 0)
    message(FATAL_ERROR "CheckObjects.cmake: ${NM} failed (${nm_result}): ${nm_error}")
endif()

# For an archive, nm prints "<member>:" before each member's symbols, and each
# symbol as "<address> <type> <name>" (no address for undefined symbols,
# types U and w).
string(REPLACE "\n" ";" nm_lines "${nm_output}")
set(object "")
set(objects "")
set(violations "")
foreach(line IN LISTS nm_lines)
    if(line MATCHES "^([^ ]+):$")
        set(object "${CMAKE_MATCH_1}")
        list(APPEND objects "${object}")
        continue()
    endif()
    if(NOT line MATCHES "^[0-9a-fA-F ]* ([A-Za-z?-]) ([^ ]+)$")
        continue()
    endif()
    set(type "${CMAKE_MATCH_1}")
    set(name "${CMAKE_MATCH_2}")
    if(type MATCHES "${mutable_data_types}")
        string(APPEND violations
            "  ${object}: mutable static data '${name}' (type ${type})\n")
    elseif(type MATCHES "^[Uw]$")
        string(REGEX REPLACE "@.*$" "" name "${name}")
        if(name IN_LIST forbidden_functions)
            string(APPEND violations
                "  ${object}: references forbidden function '${name}'\n")
        elseif(name IN_LIST allocation_functions
               AND NOT object STREQUAL allocator_object)
            string(APPEND violations
                "  ${object}: references '${name}' (only ${allocator_object} may)\n")
        endif()
    endif()
endforeach()

if(NOT allocator_object IN_LIST objects)
    message(FATAL_ERROR
        "checks.objects: ${LIBRARY} has no member ${allocator_object}; "
        "nm output not understood:\n${nm_output}")
endif()

if(violations)
    message(FATAL_ERROR
        "checks.objects: violations in ${LIBRARY}:\n${violations}"
        "Internal components keep no mutable static state, report failures "
        "by return value, and allocate only through src/alloc.h.")
endif()

list(LENGTH objects count)
message(STATUS "checks.objects: ${count} object(s) passed")
