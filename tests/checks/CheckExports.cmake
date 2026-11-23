# Copyright (c) The Amark Authors
# SPDX-License-Identifier: MIT

# checks.exports: the externally visible function and data symbols of the
# amark shared library must match exported_symbols.txt exactly.
#
#   cmake -DNM=<nm> -DLIBRARY=<libamark.so> -DSYMBOLS_FILE=<list> -P CheckExports.cmake
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
# are not part of amark's interface.
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
