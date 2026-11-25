# Copyright (c) The Amark Authors
# SPDX-License-Identifier: MIT

# checks.exports: the externally visible function and data symbols of the
# Amark shared library must match exported_symbols.txt exactly.
#
#   cmake -DNM=<nm> -DLIBRARY=<libamark.so> -DSYMBOLS_FILE=<list>
#         [-DPREFIX=<prefix> [-DEXPECTED_COUNT=<n>]] -P CheckExports.cmake
#
# With PREFIX (checks.exports.dialect), only the exported symbols that start
# with it are compared with the list, every name of the list must start with
# it, and the list must have EXPECTED_COUNT names when that is given. In
# both forms no exported symbol may start with "amark_test_": the dialects
# and helpers of the tests are never part of the library.
#
# Runs "nm -D --defined-only" (ELF shared libraries, i.e. Linux) and only
# reads files.

cmake_minimum_required(VERSION 3.21)

foreach(var IN ITEMS NM LIBRARY SYMBOLS_FILE)
    if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
        message(FATAL_ERROR "CheckExports.cmake: -D${var}=... is required")
    endif()
endforeach()

# Symbols the linker defines in every shared library on some toolchains; they
# are not part of Amark's interface.
set(linker_symbols _init _fini _edata _end __bss_start)

# The expected list: one name per line; "#" comments and blank lines ignored.
file(STRINGS "${SYMBOLS_FILE}" lines)
set(expected "")
foreach(line IN LISTS lines)
    string(STRIP "${line}" line)
    if(line STREQUAL "" OR line MATCHES "^#")
        continue()
    endif()
    list(APPEND expected "${line}")
endforeach()

if(DEFINED PREFIX AND NOT PREFIX STREQUAL "")
    string(LENGTH "${PREFIX}" prefix_length)
    foreach(name IN LISTS expected)
        string(SUBSTRING "${name}" 0 ${prefix_length} head)
        if(NOT head STREQUAL PREFIX)
            message(FATAL_ERROR
                "CheckExports.cmake: ${SYMBOLS_FILE} lists ${name}, which "
                "does not start with ${PREFIX}")
        endif()
    endforeach()
    if(DEFINED EXPECTED_COUNT AND NOT EXPECTED_COUNT STREQUAL "")
        list(LENGTH expected expected_count)
        if(NOT expected_count EQUAL EXPECTED_COUNT)
            message(FATAL_ERROR
                "CheckExports.cmake: ${SYMBOLS_FILE} lists ${expected_count} "
                "name(s), not ${EXPECTED_COUNT}")
        endif()
    endif()
else()
    set(prefix_length 0)
endif()

execute_process(
    COMMAND "${NM}" -D --defined-only "${LIBRARY}"
    OUTPUT_VARIABLE nm_output
    ERROR_VARIABLE nm_error
    RESULT_VARIABLE nm_result
)
if(NOT nm_result EQUAL 0)
    message(FATAL_ERROR "CheckExports.cmake: ${NM} failed (${nm_result}): ${nm_error}")
endif()

# Each line is "<address> <type> <name>[@<version>]". Function and data
# symbols have the types T, D, B, R, G, S, C, W, V, i and u (global or weak);
# A (absolute, e.g. version nodes) and N (debugging) are not.
string(REPLACE "\n" ";" nm_lines "${nm_output}")
set(actual "")
foreach(line IN LISTS nm_lines)
    if(NOT line MATCHES "^[0-9a-fA-F]* *([A-Za-z]) ([^ ]+)$")
        continue()
    endif()
    set(type "${CMAKE_MATCH_1}")
    set(name "${CMAKE_MATCH_2}")
    if(NOT type MATCHES "^[TDBRGSCWViu]$")
        continue()
    endif()
    string(REGEX REPLACE "@.*$" "" name "${name}")
    if(name IN_LIST linker_symbols)
        continue()
    endif()
    if(name MATCHES "^amark_test_")
        message(FATAL_ERROR
            "checks.exports: ${LIBRARY} exports ${name}, a name of the tests")
    endif()
    if(prefix_length GREATER 0)
        string(SUBSTRING "${name}" 0 ${prefix_length} head)
        if(NOT head STREQUAL PREFIX)
            continue()
        endif()
    endif()
    list(APPEND actual "${name}")
endforeach()
list(REMOVE_DUPLICATES actual)

set(missing "")
foreach(name IN LISTS expected)
    if(NOT name IN_LIST actual)
        list(APPEND missing "${name}")
    endif()
endforeach()
set(extra "")
foreach(name IN LISTS actual)
    if(NOT name IN_LIST expected)
        list(APPEND extra "${name}")
    endif()
endforeach()

if(missing OR extra)
    set(report "")
    foreach(name IN LISTS missing)
        string(APPEND report "  listed but not exported: ${name}\n")
    endforeach()
    foreach(name IN LISTS extra)
        string(APPEND report "  exported but not listed: ${name}\n")
    endforeach()
    message(FATAL_ERROR
        "checks.exports: the symbols of ${LIBRARY} do not match "
        "${SYMBOLS_FILE}:\n${report}"
        "Mark public functions with AMARK_API and list them; keep internal "
        "functions unmarked.")
endif()

list(LENGTH actual count)
message(STATUS "checks.exports: ${count} exported symbol(s) match the list")
