# Amark

Amark is a C99 library that parses Markdown into a syntax tree that stays
faithful to the source text. It targets full conformance with
[CommonMark 0.31.2](https://spec.commonmark.org/0.31.2/), and every node
refers back to the input by byte range and line number instead of copying it,
so the original text, line endings included, can always be recovered.
Dialects such as GFM are meant to plug in without changing the core.

Design goals:

- Every input is a document: CommonMark has no syntax errors. Parsing fails
  only on invalid input encoding (when so configured), invalid settings,
  exceeded resource limits, or out-of-memory.
- Linear time in the input length, no recursion, also for pathological input.
- No global state and no I/O: the same input and settings always give the
  same tree, and separate documents can be parsed in parallel.
- Failures are local: on failure everything allocated is released and no
  partial tree is returned. All memory goes through an allocator you can
  replace.
- No runtime dependencies.

## Status

Amark is under early development (version 0.1.0) and is not usable for
parsing yet. The repository currently provides the foundation: the build,
install and package configuration, the test setup, CI, and the internal
building blocks (allocator abstraction, arena, growable array, line index,
UTF-8 validation and replacement), the parse configuration, and the syntax
tree with its read-only public API (see [Public API](#public-api)). There is no public function that
parses yet, so the tree cannot be obtained from Markdown. The API and ABI will
change without notice before 1.0, and a new minor version may be incompatible
with the previous one.

## Requirements

- A C99 compiler: GCC, Clang, MSVC or MinGW. CI builds on Linux (GCC, Clang),
  macOS (Clang) and Windows (MSVC, MinGW UCRT64).
- CMake 3.21 or newer.
- To build the tests (the default for a top-level build): Git and network
  access at configure time, because the test framework
  [Naos](https://github.com/vabelorg/naos) is fetched with `FetchContent`
  (pinned to the commit of `v0.4.0`). Configure with
  `-DAMARK_BUILD_TESTS=OFF` to build only the library, without network access.
- Optional: a C++11 compiler, to check that the public header compiles as C++
  (the `header_cxx` test is skipped without one), and `nm` (binutils) on
  Linux for the `checks.exports` and `checks.objects` tests.
- Python 3.9 or later is needed only to regenerate the tables (see
  [CONTRIBUTING.md](CONTRIBUTING.md#regenerating-the-tables)). The build
  itself never runs Python; when CTest finds Python 3.9 or later, it also
  runs the `checks.generated_tables` and `checks.generator` tests.

## Build, test and install

Run these commands from the root of the repository. They configure, build,
test and install a Release build of the static library into `build/install`:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix build/install
```

- Pass the configuration name everywhere: `-DCMAKE_BUILD_TYPE` at configure
  time for single-configuration generators (Makefiles, Ninja), and
  `--config` / `-C` for multi-configuration generators (Visual Studio,
  Xcode), which ignore `CMAKE_BUILD_TYPE`. With a single-configuration
  generator, `cmake --install --config <cfg>` installs a usable package only
  if `CMAKE_BUILD_TYPE` was set to the same configuration when configuring.
- Build before running `ctest`. Test executables that are not built yet show
  up as failing `<target>.NOT_BUILT` tests.
- Any generator works; add for example `-G Ninja` to the first command.
- Add `-DBUILD_SHARED_LIBS=ON` to the first command to build a shared library
  instead. An install prefix holds either the static or the shared library,
  not both.

CI runs the same commands with `-DAMARK_WARNINGS_AS_ERRORS=ON` added to the
first one, for both values of `BUILD_SHARED_LIBS`.

To check the installed package, build and run the small consumer project in
`tests/consumer/` against it. It finds Amark with `find_package(Amark)`,
checks that the install prefix holds no internal headers, and runs one test.
The prefix you pass must hold only Amark.

```sh
cmake -S tests/consumer -B build-consumer -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$PWD/build/install"
cmake --build build-consumer --config Release
ctest --test-dir build-consumer -C Release --output-on-failure
```

Add `-DAMARK_CONSUMER_MODE=subdirectory` (and leave out
`CMAKE_PREFIX_PATH`) to build the same project with Amark pulled in by
`add_subdirectory()` instead.

### Sanitizers

With GCC or Clang, `-DAMARK_SANITIZE=ON` builds Amark, the tests and Naos
with AddressSanitizer and UndefinedBehaviorSanitizer. Use C and C++
compilers from the same toolchain, since the C++ header test links the same
sanitizer runtime:

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DAMARK_SANITIZE=ON -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build-asan --config Debug
ctest --test-dir build-asan -C Debug --output-on-failure
```

## Using Amark from CMake

Both ways below give you the same target, `Amark::amark`. Linking it adds the
include directory and, for a static library, the `AMARK_STATIC_DEFINE`
definition; nothing else needs to be set. Include the public header as
`#include <amark/amark.h>`.

```c
#include <amark/amark.h>
#include <stdio.h>

int main(void)
{
    printf("Amark %s\n", amark_version_string());
    return 0;
}
```

### Installed package: `find_package`

```cmake
cmake_minimum_required(VERSION 3.21)
project(app LANGUAGES C)

find_package(Amark 0.1 REQUIRED)

add_executable(app main.c)
target_link_libraries(app PRIVATE Amark::amark)
```

Point CMake at the install prefix when you configure, for example
`-DCMAKE_PREFIX_PATH=/path/to/prefix`. Before 1.0, `find_package(Amark 0.1)`
accepts 0.1.x only (`SameMinorVersion`). On Windows with a shared library,
put `amark.dll` next to your executable or on `PATH`, for example by copying
`$<TARGET_RUNTIME_DLLS:app>` after the build.

### Source tree: `add_subdirectory`

```cmake
cmake_minimum_required(VERSION 3.21)
project(app LANGUAGES C)

add_subdirectory(third_party/amark)

add_executable(app main.c)
target_link_libraries(app PRIVATE Amark::amark)
```

As a subproject, Amark does not build its tests (so Naos is not fetched),
defines no install rules, and leaves warnings as warnings, unless you set the
options below before `add_subdirectory()`. `BUILD_SHARED_LIBS` of the parent
project selects a static or shared library.

## Public API

Everything public is declared by `#include <amark/amark.h>`, which also
compiles as C++. All names start with `amark_` (functions and types) or
`AMARK_` (macros and enumeration constants).

### Configuration

A parse is configured with an `amark_config_t`. Always fill it in with
`amark_config_init()` first, then change only the fields you need;
`amark_config_validate()` checks it against the length of the content and
returns `AMARK_ERROR_NONE` or `AMARK_ERROR_INVALID_CONFIG`. A configuration
that was not initialized (for example, one filled with zeros) is invalid.

```c
amark_config_t config;

amark_config_init(&config);
config.utf8_mode = AMARK_UTF8_REPLACE; /* replace invalid UTF-8 */
if (amark_config_validate(&config, content_length) != AMARK_ERROR_NONE) {
    /* the configuration is invalid */
}
```

### Reading the tree

A parse gives an `amark_document_t`, used through pointers only, and released
with `amark_document_destroy()`. Nodes are named by `amark_node_id_t` IDs
within their document: the root is `amark_document_root(doc)`, and
`AMARK_NODE_NONE` (0) means "no node". The kind of a node
(`amark_node_kind()`) is one of the `AMARK_KIND_*` values, which never change
between versions, or a dialect kind at or above `AMARK_KIND_DIALECT_MIN`; its range (`amark_node_range()`) is the bytes
`[start, end)` of the content it was parsed from. The reading functions never
change the document or allocate memory, and return a fixed value
(`AMARK_NODE_NONE`, `AMARK_KIND_NONE`, 0 or `false`) for a NULL document or
an ID that names no node.

The parent, the first child and the next sibling are enough to visit every
node in document order, without recursion or a stack:

```c
amark_node_id_t root = amark_document_root(doc);
amark_node_id_t node = root;
while (node != AMARK_NODE_NONE) {
    /* visit node */
    amark_node_id_t next = amark_node_first_child(doc, node);
    while (next == AMARK_NODE_NONE && node != root) {
        next = amark_node_next_sibling(doc, node);
        if (next == AMARK_NODE_NONE) {
            node = amark_node_parent(doc, node);
        }
    }
    node = next;
}
```

See `include/amark/tree.h` for the attributes of each kind (heading level,
list type, code block fence, link destination and so on) and for the
diagnostics a document holds.

## Options

| Option | Default | Meaning |
|---|---|---|
| `BUILD_SHARED_LIBS` | `OFF` (CMake's standard variable) | `ON` builds a shared library, `OFF` a static one. |
| `AMARK_BUILD_TESTS` | `ON` when Amark is the top-level project, else `OFF` | Build the tests and register them with CTest. Only then is Naos fetched. |
| `AMARK_WARNINGS_AS_ERRORS` | `OFF` | Treat compiler warnings as errors in Amark's own targets (`-Werror`, `/WX`). CI turns it on. |
| `AMARK_SANITIZE` | `OFF` | Build with AddressSanitizer and UndefinedBehaviorSanitizer. GCC and Clang only; configuring fails with other compilers. |
| `AMARK_INSTALL` | `ON` when Amark is the top-level project, else `OFF` | Define the install rules (library, public headers, CMake package files, `LICENSE`). |

Amark's own targets are always compiled as C99 without compiler extensions,
with `-Wall -Wextra -Wpedantic` (GCC, Clang) or `/W4` (MSVC), and with
symbols hidden except those marked `AMARK_API`.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for the source file header, naming
conventions, how to add a module and its tests, the rules the library code
follows, and how to regenerate the Unicode and entity tables.

## License

Amark is released under the MIT License; see [LICENSE](LICENSE).
Copyright (c) 2026 The Amark Authors.

Amark includes tables generated from third-party data: the Unicode Character
Database (Unicode License v3) and the named character references of the WHATWG
HTML Standard (`entities.json`; CC BY 4.0 for the copy in `tools/data/`, BSD
3-Clause for the generated table). Their notices and full license texts are
in the "Third-party data" section of [LICENSE](LICENSE), which is installed
with the library.
