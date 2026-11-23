# Contributing to Amark

This document gives the rules for code in this repository: the header every
source file starts with, naming, how to add a module and its tests, and what
the library code must not do. Build and test commands are in
[README.md](README.md).

## Source file header

Every C, C++ and CMake file in the repository starts with the MIT notice and
the copyright line of The Amark Authors. Do not write a year in it, so the
header never needs a yearly update.

C and C++ files (`.c`, `.h`, `.cpp`, and C templates such as
`cmake/version.h.in`):

```c
/*
 * Copyright (c) The Amark Authors
 * SPDX-License-Identifier: MIT
 */
```

CMake files (`CMakeLists.txt`, `.cmake`, and CMake templates such as
`cmake/AmarkConfig.cmake.in`) use the same two lines as `#` comments:

```cmake
# Copyright (c) The Amark Authors
# SPDX-License-Identifier: MIT
```

The `checks.license_headers` test enforces this. It walks the repository from
its root and requires both lines within the first 5 lines of every
`CMakeLists.txt` and every `.c`, `.h`, `.cpp`, `.cc`, `.cxx`, `.hpp`, `.hh`,
`.hxx`, `.cmake` and `.in` file. It skips hidden directories (`.git`,
`.github`, ...), build trees (directories holding `CMakeCache.txt`),
`CMakeFiles` and `_deps`. Notes:

- Files generated into the build directory (`amark/export.h`,
  `amark/version.h`) are not checked. Sources that are generated and
  committed (for example Unicode tables) are checked, so the generator must
  write the header.
- If you add a directory with third-party sources, add an exclusion for it to
  `tests/checks/CheckLicenseHeaders.cmake`.
- Markdown and other documentation files need no header.

## Naming

- Public functions and types start with `amark_`; macros and enumeration
  constants start with `AMARK_`.
- Internal functions used from more than one file also start with `amark_`,
  to avoid name clashes in static links. Names do not tell public from
  internal; the place of the declaration does (`include/amark/` for public,
  a header in `src/` for internal) together with `AMARK_API`. A function used
  in one file only is `static`.
- `typedef` names end in `_t`; struct and enum tags do not. Function pointer
  types follow the same rule.

  ```c
  typedef struct amark_arena { ... } amark_arena_t;
  typedef void *(*amark_malloc_fn_t)(void *ctx, size_t size);
  ```

- File names are lowercase with underscores (`line_index.c`). A module is a
  `.c` file and an internal header of the same name in `src/`.
- CMake: the project and package are `Amark` (`find_package(Amark)`,
  `AmarkConfig.cmake`), the library target and file are `amark`, and users
  link `Amark::amark`. Options and CMake functions of Amark start with
  `AMARK_` and `amark_`.
- Include public headers with angle brackets (`#include <amark/amark.h>`) and
  internal headers with quotes (`#include "arena.h"`).

## Rules for library code

The library is C99 without compiler extensions and must build with GCC,
Clang, MSVC and MinGW without warnings. In addition, code in `src/` must not
use:

- **Global or mutable static state.** No global variables and no `static`
  variables that change. Read-only tables (`static const`) are fine.
- **`abort`, `exit`, `_Exit`, `setjmp`, `longjmp`** and their variants
  (`_exit`, `quick_exit`, `sigsetjmp`, `siglongjmp`). Report failures through
  return values (`amark_error_t` in `src/error.h`).
- **`assert`.** It calls `abort`. Preconditions are the caller's
  responsibility; tests and sanitizers catch violations.
- **Recursion.** Use loops and explicit stacks, also for traversal and
  cleanup.
- **I/O**, and **`malloc`, `calloc`, `realloc`, `aligned_alloc` or `free`
  outside `src/alloc.c`.** All memory goes
  through the allocator (`amark_malloc`, `amark_malloc_array`, `amark_free`,
  or an arena). Check every allocation; on failure release what the
  operation allocated and leave its target unchanged.
- **Unchecked size arithmetic.** Compute sizes derived from input lengths
  with `amark_size_add` and `amark_size_mul`.

The `checks.objects` test enforces part of this on Linux: no mutable static
data (`nm` types `b B C d D g G s S`; read-only `r R` is allowed), no
references to `abort`, `exit`, `_Exit`, `longjmp`, `setjmp`, their variants or
`__assert_fail`, and references to `malloc`, `calloc`, `realloc`,
`aligned_alloc` and `free` from `alloc.c` only.

Public headers in `include/amark/` must compile on their own, include no
internal header, expose no struct layouts, and wrap their declarations in
`extern "C"` for C++.

## Adding a module

1. Put `src/<module>.c` and `src/<module>.h` in `src/`, both with the header
   above, and add one line, `<module>.c`, to `AMARK_SOURCES` in
   `src/CMakeLists.txt`. The library `amark` and the test-only library
   `amark_testing` are both built from that list.
2. Put the unit test in `tests/unit/test_<module>.c` and add one line to
   `tests/CMakeLists.txt`:

   ```cmake
   amark_add_unit_test(test_<module> SOURCES unit/test_<module>.c)
   ```

3. If the module adds public functions, declare them with `AMARK_API` in
   `include/amark/` (in `amark.h` or a header that `amark.h` includes), add
   each exported name to `tests/checks/exported_symbols.txt` (one per line;
   `checks.exports` compares the list with the shared library), and test them
   through the public API with `amark_add_public_test`.

## Writing tests

Tests use [Naos](https://github.com/vabelorg/naos) `v0.4.0`. Each test file
is one executable with its own `NAOS_MAIN`; there is no shared `main.c`.
Register it with one of these functions from `tests/CMakeLists.txt`:

```cmake
# Internal functions. Links amark_testing (src/ is on the include path)
# and amark_test_support (tests/support/test_alloc.h).
amark_add_unit_test(<name> SOURCES <source>... [LIBRARIES <library>...])

# The public API only. Links Amark::amark.
amark_add_public_test(<name> SOURCES <source>...)
```

Naos registers each test with CTest as `<name>.<suite>.<test>`, for example
`test_arena.arena.every_allocation_is_aligned`.

- **Clean up in teardown.** `NAOS_REQUIRE_*` leaves the test with `longjmp`,
  so code after a failed requirement does not run. Keep what a test
  allocates (an arena, a test allocator state) in static fixture variables,
  set them up in `NAOS_SETUP`, and release them in `NAOS_TEARDOWN`, then
  check `live == 0` and `ctx_mismatch == 0` there. See
  `tests/unit/test_arena.c`.
- **Inject allocation failures.** `tests/support/test_alloc.h` provides a
  counting allocator (`amark_test_alloc_init`, `amark_test_allocator`) that
  fails the `fail_at`-th call. For every operation that allocates, run it
  with `fail_at` = 1, 2, 3, ... until it succeeds, and after each attempt
  release everything and require `live == 0`. Follow
  `failure_injection_loop` in `tests/unit/test_alloc.c`.
- **Every test runs at least one check.** In Naos 0.4.0 a test that runs no
  check fails.
- **Test boundaries.** Exercise exact fits and one past them, and write to
  the whole of every allocated range before checking its contents.
- **Build Debug and Release.** GCC at `-O3` reports warnings that `-O0` does
  not, so build the tests with `-DAMARK_WARNINGS_AS_ERRORS=ON` in both. GCC
  13 may report a false `-Wmaybe-uninitialized` when a pointer that was just
  returned by `malloc` is passed to `NAOS_REQUIRE_NOT_NULL`; write
  `NAOS_REQUIRE_TRUE(p != NULL)` instead.

## Regenerating the tables

`src/unicode_tables.h` (Unicode punctuation, white space and case folding) and
`src/entity_table.h` (HTML named character references) are generated by
`tools/gen_tables.py` from the input data in `tools/data/`. Both the input
data and the generated files are committed, so building and testing the
library never runs the generator. `tools/data/README.md` records where each
input file comes from, its version and its SHA-256.

Do not edit the generated files by hand. Change the generator or the input
data and regenerate instead; the `checks.generated_tables` test and the
`generated-tables` CI job fail when the committed files differ from what the
generator makes.

What you need: Python 3.9 or later. The generator uses the standard library
only, so there is nothing to install.

Run these commands from the root of the repository:

```sh
# Write src/unicode_tables.h and src/entity_table.h (an unchanged file is
# not rewritten).
python3 tools/gen_tables.py

# Write nothing; exit with a non-zero status if a generated file is out of
# date.
python3 tools/gen_tables.py --check

# Run the generator's own tests. -B keeps Python from writing
# tools/__pycache__.
python3 -B tools/test_gen_tables.py
```

CTest runs the last two as `checks.generated_tables` and `checks.generator`
when it finds Python 3.9 or later.

To move to a new Unicode version or a newer `entities.json`:

1. Replace the files in `tools/data/` with the new ones, exactly as
   downloaded, under the same names (`DerivedGeneralCategory.txt`,
   `CaseFolding.txt`, `entities.json`). The two Unicode files must be of the
   same Unicode version; the generator stops with an error if they are not.
2. Update `tools/data/README.md`: the source URL, the version (for
   `entities.json`, the date you downloaded it), the size and the SHA-256 of
   each replaced file (`sha256sum tools/data/*.txt tools/data/entities.json`).
3. Run `python3 tools/gen_tables.py`, then `python3 tools/gen_tables.py
   --check` and `python3 -B tools/test_gen_tables.py`.
4. Build and run all tests as described in [README.md](README.md). The unit
   tests `test_unicode` and `test_entity` read `tools/data/` themselves and
   compare every entry with the library.
5. Check the year in the Unicode copyright line in [LICENSE](LICENSE)
   (`Copyright © 1991-<year> Unicode, Inc.`) against
   <https://www.unicode.org/license.txt> and update it if it changed.
6. Commit the new input data, `tools/data/README.md` and the regenerated
   files together in one commit.

## Before you send a change

Run the commands from [README.md](README.md) with
`-DAMARK_WARNINGS_AS_ERRORS=ON`, with the static and with the shared library
(`-DBUILD_SHARED_LIBS=ON`), in Debug and in Release, and once with
`-DAMARK_SANITIZE=ON`. For the sanitizer build, set both
`CMAKE_C_COMPILER` and `CMAKE_CXX_COMPILER` to the same toolchain
(`gcc`/`g++` or `clang`/`clang++`). All tests, including `checks.exports`,
`checks.objects` and `checks.license_headers`, must pass.

By contributing you agree that your contribution is licensed under the MIT
License of this repository.
