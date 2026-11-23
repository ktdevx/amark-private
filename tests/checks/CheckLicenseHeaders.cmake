# Copyright (c) The Amark Authors
# SPDX-License-Identifier: MIT

# checks.license_headers: every C, C++ and CMake source and template file in
# the repository must carry the copyright notice and the SPDX identifier in
# its first 5 lines (CONTRIBUTING.md gives the exact header).
#
# Usage:
#   cmake -DSOURCE_DIR=<repository root> [-DBINARY_DIR=<build dir>]
#         -P CheckLicenseHeaders.cmake
#
# Only files the repository owns are read. The walk skips hidden directories
# (.git, .kiro, .claude, ...), the given build directory, and any other
# directory that is a CMake build tree (it holds CMakeCache.txt) or a CMake
# work directory (CMakeFiles, _deps), so a build directory inside the source
# tree and the dependencies it fetches are never checked.

cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED SOURCE_DIR OR NOT IS_DIRECTORY "${SOURCE_DIR}")
    message(FATAL_ERROR "SOURCE_DIR must name the repository root")
endif()

set(_notice "Copyright (c) The Amark Authors")
set(_spdx "SPDX-License-Identifier: MIT")
set(_max_lines 5)

get_filename_component(_root "${SOURCE_DIR}" REALPATH)
set(_binary "")
if(DEFINED BINARY_DIR AND NOT BINARY_DIR STREQUAL "")
    get_filename_component(_binary "${BINARY_DIR}" REALPATH)
endif()

# Breadth-first walk with an explicit queue of directories.
set(_queue "${_root}")
set(_files "")
while(_queue)
    list(POP_FRONT _queue _dir)
    file(GLOB _entries LIST_DIRECTORIES true "${_dir}/*")
    foreach(_entry IN LISTS _entries)
        get_filename_component(_name "${_entry}" NAME)
        if(IS_DIRECTORY "${_entry}")
            if(IS_SYMLINK "${_entry}"
                    OR _name MATCHES "^\\."
                    OR _name STREQUAL "CMakeFiles"
                    OR _name STREQUAL "_deps"
                    OR EXISTS "${_entry}/CMakeCache.txt")
                continue()
            endif()
            if(_binary)
                get_filename_component(_real "${_entry}" REALPATH)
                if(_real STREQUAL _binary)
                    continue()
                endif()
            endif()
            list(APPEND _queue "${_entry}")
        elseif(_name STREQUAL "CMakeLists.txt"
                OR _name MATCHES "\\.(c|h|cpp|cc|cxx|hpp|hh|hxx|cmake|in)$")
            list(APPEND _files "${_entry}")
        endif()
    endforeach()
endwhile()

list(LENGTH _files _count)
if(_count EQUAL 0)
    message(FATAL_ERROR "no source files found under ${_root}")
endif()
list(SORT _files)

set(_failures "")
foreach(_file IN LISTS _files)
    # The first _max_lines lines: the text before the _max_lines-th newline,
    # or the whole (short) file.
    file(READ "${_file}" _content LIMIT 4096)
    set(_head "")
    set(_rest "${_content}")
    set(_line 0)
    while(_line LESS _max_lines)
        string(FIND "${_rest}" "\n" _pos)
        if(_pos EQUAL -1)
            string(APPEND _head "${_rest}")
            break()
        endif()
        math(EXPR _next "${_pos} + 1")
        string(SUBSTRING "${_rest}" 0 ${_next} _piece)
        string(SUBSTRING "${_rest}" ${_next} -1 _rest)
        string(APPEND _head "${_piece}")
        math(EXPR _line "${_line} + 1")
    endwhile()

    set(_missing "")
    string(FIND "${_head}" "${_notice}" _found)
    if(_found EQUAL -1)
        list(APPEND _missing "copyright notice")
    endif()
    string(FIND "${_head}" "${_spdx}" _found)
    if(_found EQUAL -1)
        list(APPEND _missing "SPDX identifier")
    endif()
    if(_missing)
        file(RELATIVE_PATH _rel "${_root}" "${_file}")
        list(JOIN _missing " and " _what)
        list(APPEND _failures "  ${_rel}: no ${_what} in the first ${_max_lines} lines")
    endif()
endforeach()

if(_failures)
    list(LENGTH _failures _bad)
    list(JOIN _failures "\n" _report)
    message(FATAL_ERROR
        "${_bad} of ${_count} files lack the license header "
        "(\"${_notice}\" and \"${_spdx}\"):\n${_report}")
endif()

message(STATUS "license headers: ${_count} files checked")
