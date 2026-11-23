#!/usr/bin/env python3
# Copyright (c) The Amark Authors
# SPDX-License-Identifier: MIT
"""Generate Amark's Unicode and entity tables from the committed input data.

Inputs (in --data-dir, default tools/data next to this script):
  DerivedGeneralCategory.txt, CaseFolding.txt  (Unicode Character Database)
  entities.json                                 (WHATWG HTML Standard)

Outputs (in --output-dir, default src at the repository root):
  unicode_tables.h, entity_table.h

The script uses only the Python standard library, needs Python 3.9 or later
and never touches the network. It can be imported as a module; the reading
functions below return validated, normalized data and raise InputError on any
problem in the input.

Exit codes: 0 success, 1 input error or mismatch, 2 argument error.
"""

from __future__ import annotations

import argparse
import difflib
import hashlib
import json
import os
import re
import sys
import tempfile
from dataclasses import dataclass
from typing import Dict, List, Optional, Sequence, Tuple

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
DEFAULT_DATA_DIR = os.path.join(SCRIPT_DIR, "data")
DEFAULT_OUTPUT_DIR = os.path.join(os.path.dirname(SCRIPT_DIR), "src")

GC_FILE = "DerivedGeneralCategory.txt"
CF_FILE = "CaseFolding.txt"
ENTITIES_FILE = "entities.json"

MAX_CODEPOINT = 0x10FFFF

# All two-letter General Category values defined by the UCD.
KNOWN_CATEGORIES = frozenset(
    "Lu Ll Lt Lm Lo Mn Mc Me Nd Nl No Pc Pd Ps Pe Pi Pf Po Sm Sc Sk So "
    "Zs Zl Zp Cc Cf Cs Co Cn".split()
)
PUNCTUATION_CATEGORIES = frozenset(
    "Pc Pd Ps Pe Pi Pf Po Sm Sc Sk So".split()
)
SPACE_CATEGORIES = frozenset(["Zs"])

KNOWN_FOLD_STATUSES = frozenset("CFST")

# Limits imposed by the generated table shapes (amark_entity_entry_t).
MAX_ENTITY_NAME_LENGTH = 0xFF      # name_length is uint8_t
MAX_ENTITY_NAMES_TOTAL = 0xFFFF    # name_offset is uint16_t
MAX_ENTITY_UTF8_LENGTH = 6         # utf8[6]

_UCD_HEADER_RE = re.compile(r"# ([A-Za-z]+)-([0-9]+\.[0-9]+\.[0-9]+)\.txt")
_HEX_RE = re.compile(r"[0-9A-F]{4,6}")
_GC_RANGE_RE = re.compile(r"([0-9A-F]{4,6})(?:\.\.([0-9A-F]{4,6}))?")
_ENTITY_NAME_RE = re.compile(r"&[A-Za-z0-9]+;?")
_JSON_KEY_LINE_RE = re.compile(r'\s*"([^"\\]*)"\s*:')

Range = Tuple[int, int]
FoldSimple = Tuple[int, int]
FoldFull = Tuple[int, Tuple[int, ...]]


class InputError(Exception):
    """A problem in an input file; str() is 'file:line: reason'."""

    def __init__(self, path: str, line: Optional[int], reason: str) -> None:
        self.path = path
        self.line = line
        self.reason = reason
        where = path if line is None else "%s:%d" % (path, line)
        super().__init__("%s: %s" % (where, reason))


@dataclass(frozen=True)
class Entity:
    """One entity: name without '&' and ';', value encoded as UTF-8."""

    name: bytes
    utf8: bytes


@dataclass(frozen=True)
class Inputs:
    """Everything the generated tables are built from."""

    unicode_version: str
    punctuation: List[Range]
    space: List[Range]
    fold_simple: List[FoldSimple]
    fold_full: List[FoldFull]
    entities: List[Entity]
    entities_sha256: str


def _read_bytes(path: str) -> bytes:
    try:
        with open(path, "rb") as f:
            return f.read()
    except OSError as e:
        reason = e.strerror or str(e)
        raise InputError(path, None, "cannot read file: %s" % reason) from None


def _decode_lines(path: str, data: bytes) -> List[str]:
    """Split into lines (LF, tolerating CRLF) and decode each as UTF-8."""
    lines = []
    for number, raw in enumerate(data.split(b"\n"), start=1):
        if raw.endswith(b"\r"):
            raw = raw[:-1]
        try:
            lines.append(raw.decode("utf-8"))
        except UnicodeDecodeError:
            raise InputError(path, number, "line is not valid UTF-8") from None
    return lines


def _ucd_version(path: str, lines: List[str], expected_name: str) -> str:
    """Read the version from line 1, '# <name>-<X.Y.Z>.txt'."""
    first = lines[0] if lines else ""
    m = _UCD_HEADER_RE.fullmatch(first.rstrip())
    if m is None or m.group(1) != expected_name:
        raise InputError(
            path, 1,
            "first line is not '# %s-<X.Y.Z>.txt': %r" % (expected_name, first))
    return m.group(2)


def _data_fields(line: str) -> Optional[List[str]]:
    """Strip the comment; return the ';'-separated fields, or None if empty."""
    content = line.split("#", 1)[0].strip()
    if not content:
        return None
    return [field.strip() for field in content.split(";")]


def _parse_scalar(path: str, number: int, text: str, what: str) -> int:
    if _HEX_RE.fullmatch(text) is None:
        raise InputError(path, number, "malformed %s %r" % (what, text))
    value = int(text, 16)
    if value > MAX_CODEPOINT:
        raise InputError(
            path, number, "%s U+%04X is above U+10FFFF" % (what, value))
    if 0xD800 <= value <= 0xDFFF:
        raise InputError(
            path, number, "%s U+%04X is a surrogate" % (what, value))
    return value


def merge_ranges(ranges: Sequence[Range]) -> List[Range]:
    """Sort ranges and join the ones that overlap or are adjacent."""
    merged: List[Range] = []
    for first, last in sorted(ranges):
        if merged and first <= merged[-1][1] + 1:
            if last > merged[-1][1]:
                merged[-1] = (merged[-1][0], last)
        else:
            merged.append((first, last))
    return merged


def read_general_category(path: str) -> Tuple[str, List[Range], List[Range]]:
    """Read DerivedGeneralCategory.txt.

    Returns (unicode_version, punctuation_ranges, space_ranges); the ranges
    are inclusive (first, last) pairs, sorted, with overlapping and adjacent
    ranges joined. Punctuation is every P* and S* category; space is Zs.
    """
    lines = _decode_lines(path, _read_bytes(path))
    version = _ucd_version(path, lines, "DerivedGeneralCategory")
    punctuation: List[Range] = []
    space: List[Range] = []
    for number, line in enumerate(lines, start=1):
        fields = _data_fields(line)
        if fields is None:
            continue
        if len(fields) != 2:
            raise InputError(
                path, number, "expected '<range> ; <category>': %r" % line)
        range_text, category = fields
        m = _GC_RANGE_RE.fullmatch(range_text)
        if m is None:
            raise InputError(path, number, "malformed range %r" % range_text)
        first = int(m.group(1), 16)
        last = int(m.group(2), 16) if m.group(2) is not None else first
        if last > MAX_CODEPOINT:
            raise InputError(
                path, number, "range ends above U+10FFFF: %r" % range_text)
        if first > last:
            raise InputError(path, number, "range is reversed: %r" % range_text)
        if category not in KNOWN_CATEGORIES:
            raise InputError(
                path, number, "unknown General Category %r" % category)
        if category in PUNCTUATION_CATEGORIES:
            punctuation.append((first, last))
        elif category in SPACE_CATEGORIES:
            space.append((first, last))
    return version, merge_ranges(punctuation), merge_ranges(space)


def read_case_folding(
        path: str) -> Tuple[str, List[FoldSimple], List[FoldFull]]:
    """Read CaseFolding.txt.

    Returns (unicode_version, simple, full): simple holds the status C
    mappings as (from, to), full holds the status F mappings as
    (from, (to, to[, to])); both sorted by `from`. Status S and T are
    validated and dropped.
    """
    lines = _decode_lines(path, _read_bytes(path))
    version = _ucd_version(path, lines, "CaseFolding")
    simple: Dict[int, int] = {}
    full: Dict[int, Tuple[int, ...]] = {}
    seen: Dict[Tuple[int, str], int] = {}
    for number, line in enumerate(lines, start=1):
        fields = _data_fields(line)
        if fields is None:
            continue
        if len(fields) != 4 or fields[3] != "":
            raise InputError(
                path, number,
                "expected '<code>; <status>; <mapping>;': %r" % line)
        code_text, status, mapping_text, _ = fields
        code = _parse_scalar(path, number, code_text, "code point")
        if status not in KNOWN_FOLD_STATUSES or len(status) != 1:
            raise InputError(path, number, "unknown status %r" % status)
        mapping_parts = mapping_text.split()
        if not mapping_parts:
            raise InputError(path, number, "empty mapping")
        mapping = tuple(
            _parse_scalar(path, number, part, "mapping code point")
            for part in mapping_parts)
        if 0 in mapping:
            raise InputError(path, number, "mapping contains U+0000")
        if (code, status) in seen:
            raise InputError(
                path, number,
                "U+%04X has a second status %s mapping (first on line %d)"
                % (code, status, seen[(code, status)]))
        seen[(code, status)] = number
        if status == "C":
            if len(mapping) != 1:
                raise InputError(
                    path, number,
                    "status C mapping must have 1 code point, has %d"
                    % len(mapping))
            if code in full:
                raise InputError(
                    path, number, "U+%04X has both status C and F" % code)
            simple[code] = mapping[0]
        elif status == "F":
            if not 2 <= len(mapping) <= 3:
                raise InputError(
                    path, number,
                    "status F mapping must have 2 or 3 code points, has %d"
                    % len(mapping))
            if code in simple:
                raise InputError(
                    path, number, "U+%04X has both status C and F" % code)
            full[code] = mapping
    return (version,
            sorted(simple.items()),
            sorted(full.items()))


def _json_key_lines(lines: List[str]) -> Dict[str, int]:
    """Map each top-level-looking '"key":' at the start of a line to it."""
    result: Dict[str, int] = {}
    for number, line in enumerate(lines, start=1):
        m = _JSON_KEY_LINE_RE.match(line)
        if m is not None and m.group(1) not in result:
            result[m.group(1)] = number
    return result


def _is_scalar_value(value: object) -> bool:
    return (isinstance(value, int) and not isinstance(value, bool)
            and 0 <= value <= MAX_CODEPOINT
            and not 0xD800 <= value <= 0xDFFF)


def read_entities(path: str) -> Tuple[List[Entity], str]:
    """Read entities.json.

    Returns (entities, sha256_hex). Only names ending in ';' are kept; each
    name has '&' and ';' removed and its code points encoded as UTF-8. The
    list is sorted by the bytes of the name (a prefix sorts first), the
    order the C lookup binary-searches.
    """
    data = _read_bytes(path)
    sha256 = hashlib.sha256(data).hexdigest()
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as e:
        line = data[:e.start].count(b"\n") + 1
        raise InputError(path, line, "file is not valid UTF-8") from None
    key_lines = _json_key_lines(text.split("\n"))

    def pairs_hook(pairs: List[Tuple[str, object]]) -> Dict[str, object]:
        obj: Dict[str, object] = {}
        for key, value in pairs:
            if key in obj:
                raise InputError(
                    path, key_lines.get(key), "duplicate key %r" % key)
            obj[key] = value
        return obj

    try:
        root = json.loads(text, object_pairs_hook=pairs_hook)
    except json.JSONDecodeError as e:
        raise InputError(path, e.lineno, "invalid JSON: %s" % e.msg) from None
    if not isinstance(root, dict):
        raise InputError(path, None, "top level is not a JSON object")

    entities: List[Entity] = []
    total = 0
    for key, value in root.items():
        line = key_lines.get(key)
        if _ENTITY_NAME_RE.fullmatch(key) is None:
            raise InputError(
                path, line, "malformed entity name %r" % key)
        if not key.endswith(";"):
            continue
        if not isinstance(value, dict):
            raise InputError(path, line, "%s: value is not an object" % key)
        codepoints = value.get("codepoints")
        characters = value.get("characters")
        if not isinstance(codepoints, list) or not 1 <= len(codepoints) <= 2:
            raise InputError(
                path, line,
                "%s: 'codepoints' must be a list of 1 or 2 code points" % key)
        for cp in codepoints:
            if not _is_scalar_value(cp):
                raise InputError(
                    path, line,
                    "%s: %r is not a Unicode scalar value" % (key, cp))
        expected = "".join(chr(cp) for cp in codepoints)
        if characters != expected:
            raise InputError(
                path, line,
                "%s: 'characters' does not match 'codepoints'" % key)
        name = key[1:-1].encode("ascii")
        utf8 = expected.encode("utf-8")
        if len(name) > MAX_ENTITY_NAME_LENGTH:
            raise InputError(
                path, line, "%s: name is longer than %d bytes"
                % (key, MAX_ENTITY_NAME_LENGTH))
        if len(utf8) > MAX_ENTITY_UTF8_LENGTH:
            raise InputError(
                path, line, "%s: value is longer than %d UTF-8 bytes"
                % (key, MAX_ENTITY_UTF8_LENGTH))
        total += len(name)
        if total > MAX_ENTITY_NAMES_TOTAL:
            raise InputError(
                path, line, "entity names exceed %d bytes in total"
                % MAX_ENTITY_NAMES_TOTAL)
        entities.append(Entity(name=name, utf8=utf8))
    entities.sort(key=lambda entity: entity.name)
    return entities, sha256


def read_inputs(data_dir: str) -> Inputs:
    """Read and validate all three inputs in data_dir."""
    gc_path = os.path.join(data_dir, GC_FILE)
    cf_path = os.path.join(data_dir, CF_FILE)
    gc_version, punctuation, space = read_general_category(gc_path)
    cf_version, fold_simple, fold_full = read_case_folding(cf_path)
    if gc_version != cf_version:
        raise InputError(
            cf_path, 1,
            "Unicode version mismatch: %s is %s but %s is %s"
            % (GC_FILE, gc_version, CF_FILE, cf_version))
    entities, sha256 = read_entities(os.path.join(data_dir, ENTITIES_FILE))
    return Inputs(
        unicode_version=gc_version,
        punctuation=punctuation,
        space=space,
        fold_simple=fold_simple,
        fold_full=fold_full,
        entities=entities,
        entities_sha256=sha256,
    )


# ---------------------------------------------------------------------------
# Output

UNICODE_TABLES_FILE = "unicode_tables.h"
ENTITY_TABLE_FILE = "entity_table.h"

GENERATOR = "tools/gen_tables.py"
LINE_WIDTH = 79
ENTITY_NAME_CHARS_PER_LINE = 12
CHECK_DIFF_LINES = 40


def _header(spdx: str, source: List[str], includer: str, guard: str,
            types: List[str]) -> List[str]:
    """The comment block and include guard every generated file starts with.

    Lines 1-3 carry the notices checks.license_headers looks for.
    """
    lines = [
        "/*",
        " * Copyright (c) The Amark Authors",
        " * SPDX-License-Identifier: %s" % spdx,
        " *",
        " * Generated by %s. Do not edit." % GENERATOR,
        " *",
    ]
    lines += [" * " + line if line else " *" for line in source]
    lines += [
        " *",
        " * Included by %s only. It defines the types first:" % includer,
    ]
    lines += [" * " + line for line in types]
    lines += [
        " */",
        "",
        "#ifndef %s" % guard,
        "#define %s" % guard,
    ]
    return lines


def _wrap_items(items: Sequence[str], indent: str = "    ") -> List[str]:
    """Join initializer items with ', ' into lines of at most LINE_WIDTH."""
    lines: List[str] = []
    current = ""
    for item in items:
        piece = item + ","
        if current and len(current) + 1 + len(piece) > LINE_WIDTH:
            lines.append(current)
            current = ""
        current = piece if not current else current + " " + piece
        if not current.startswith(indent):
            current = indent + current
    if current:
        lines.append(current)
    return lines


def _hex(value: int) -> str:
    return "0x%04X" % value


def _table(ctype: str, name: str, count_macro: str,
           items: Sequence[str]) -> List[str]:
    declaration = "static const %s %s[%s] = {" % (ctype, name, count_macro)
    if len(declaration) > LINE_WIDTH:
        declaration = "static const %s\n    %s[%s] = {" % (
            ctype, name, count_macro)
    return (declaration.split("\n")
            + _wrap_items(items)
            + ["};"])


def build_unicode_tables(inputs: Inputs) -> bytes:
    """Build the contents of unicode_tables.h."""
    version = inputs.unicode_version
    source = [
        "Source: DerivedGeneralCategory-%s.txt, CaseFolding-%s.txt"
        % (version, version),
        "(Unicode Character Database, Copyright (c) Unicode, Inc., licensed "
        "under",
        "the Unicode License v3; see LICENSE).",
    ]
    types = [
        "amark_unicode_range_t, amark_unicode_fold_simple_t and",
        "amark_unicode_fold_full_t.",
    ]
    out = _header("MIT AND Unicode-3.0", source, "unicode.c",
                  "AMARK_UNICODE_TABLES_H", types)
    out += [
        "",
        "#define AMARK_UNICODE_PUNCTUATION_COUNT %d" % len(inputs.punctuation),
        "#define AMARK_UNICODE_SPACE_COUNT %d" % len(inputs.space),
        "#define AMARK_UNICODE_FOLD_SIMPLE_COUNT %d" % len(inputs.fold_simple),
        "#define AMARK_UNICODE_FOLD_FULL_COUNT %d" % len(inputs.fold_full),
        "",
        "/* General Category P* and S*: inclusive ranges, sorted by first, "
        "neither",
        "   overlapping nor adjacent. */",
    ]
    out += _table(
        "amark_unicode_range_t", "amark_unicode_punctuation",
        "AMARK_UNICODE_PUNCTUATION_COUNT",
        ["{%s, %s}" % (_hex(a), _hex(b)) for a, b in inputs.punctuation])
    out += [
        "",
        "/* General Category Zs: inclusive ranges, sorted by first, neither",
        "   overlapping nor adjacent. */",
    ]
    out += _table(
        "amark_unicode_range_t", "amark_unicode_space",
        "AMARK_UNICODE_SPACE_COUNT",
        ["{%s, %s}" % (_hex(a), _hex(b)) for a, b in inputs.space])
    out += [
        "",
        "/* CaseFolding.txt status C: {from, to}, sorted by from. */",
    ]
    out += _table(
        "amark_unicode_fold_simple_t", "amark_unicode_fold_simple_table",
        "AMARK_UNICODE_FOLD_SIMPLE_COUNT",
        ["{%s, %s}" % (_hex(a), _hex(b)) for a, b in inputs.fold_simple])
    out += [
        "",
        "/* CaseFolding.txt status F: {from, {to, to, to}}, sorted by from; "
        "to[2]",
        "   is 0 when the mapping has 2 code points. */",
    ]
    out += _table(
        "amark_unicode_fold_full_t", "amark_unicode_fold_full_table",
        "AMARK_UNICODE_FOLD_FULL_COUNT",
        ["{%s, {%s}}" % (_hex(a), ", ".join(
            _hex(c) for c in tuple(b) + (0,) * (3 - len(b))))
         for a, b in inputs.fold_full])
    out += ["", "#endif /* AMARK_UNICODE_TABLES_H */"]
    return _encode(out)


def build_entity_table(inputs: Inputs) -> bytes:
    """Build the contents of entity_table.h."""
    source = [
        "Source: entities.json",
        "(sha256 %s)," % inputs.entities_sha256,
        "HTML Standard, Copyright (c) WHATWG (Apple, Google, Mozilla, "
        "Microsoft),",
        "BSD 3-Clause License; see LICENSE.",
    ]
    types = ["amark_entity_entry_t."]
    out = _header("MIT AND BSD-3-Clause", source, "entity.c",
                  "AMARK_ENTITY_TABLE_H", types)
    names = b"".join(entity.name for entity in inputs.entities)
    out += [
        "",
        "#define AMARK_ENTITY_COUNT %d" % len(inputs.entities),
        "#define AMARK_ENTITY_NAMES_LENGTH %d" % len(names),
        "",
        "/* The names of all entries, without '&' and ';', in entry order. "
        "There is",
        "   no separator and no terminating NUL; an entry finds its name by",
        "   name_offset and name_length. Written as characters, not a string",
        "   literal, to stay within the C99 limit on the length of a "
        "literal. */",
        "static const char amark_entity_names[AMARK_ENTITY_NAMES_LENGTH] = {",
    ]
    for start in range(0, len(names), ENTITY_NAME_CHARS_PER_LINE):
        chunk = names[start:start + ENTITY_NAME_CHARS_PER_LINE]
        out.append("    " + " ".join("'%s'," % chr(c) for c in chunk))
    out += [
        "};",
        "",
        "/* {name_offset, name_length, utf8_length, {utf8}}, sorted by the "
        "bytes of",
        "   the name (a prefix first); unused utf8 bytes are 0. */",
        "static const amark_entity_entry_t",
        "    amark_entity_entries[AMARK_ENTITY_COUNT] = {",
    ]
    offset = 0
    for entity in inputs.entities:
        utf8 = entity.utf8 + b"\0" * (MAX_ENTITY_UTF8_LENGTH - len(entity.utf8))
        out.append("    {%d, %d, %d, {%s}}, /* %s */" % (
            offset, len(entity.name), len(entity.utf8),
            ", ".join("0x%02X" % b for b in utf8),
            entity.name.decode("ascii")))
        offset += len(entity.name)
    out += ["};", "", "#endif /* AMARK_ENTITY_TABLE_H */"]
    return _encode(out)


def _encode(lines: List[str]) -> bytes:
    """Join with LF, end with exactly one LF, and encode as ASCII."""
    return ("\n".join(lines) + "\n").encode("ascii")


def build_outputs(inputs: Inputs) -> Dict[str, bytes]:
    """Map each generated file name to its contents."""
    return {
        UNICODE_TABLES_FILE: build_unicode_tables(inputs),
        ENTITY_TABLE_FILE: build_entity_table(inputs),
    }


def _file_mode() -> int:
    umask = os.umask(0)
    os.umask(umask)
    return 0o666 & ~umask


def write_outputs(output_dir: str, outputs: Dict[str, bytes]) -> List[str]:
    """Write every output through a temporary file in output_dir and rename it
    into place. Files whose contents already match are left untouched.
    Returns the paths written."""
    written: List[str] = []
    mode = _file_mode()
    for name in sorted(outputs):
        path = os.path.join(output_dir, name)
        data = outputs[name]
        if _read_existing(path) == data:
            continue
        fd, tmp = tempfile.mkstemp(
            dir=output_dir, prefix="." + name + ".", suffix=".tmp")
        try:
            with os.fdopen(fd, "wb") as f:
                f.write(data)
            os.chmod(tmp, mode)
            os.replace(tmp, path)
        except BaseException:
            try:
                os.remove(tmp)
            except OSError:
                pass
            raise
        written.append(path)
    return written


def _read_existing(path: str) -> Optional[bytes]:
    try:
        with open(path, "rb") as f:
            return f.read()
    except OSError:
        return None


def check_outputs(output_dir: str, outputs: Dict[str, bytes]) -> List[str]:
    """Compare outputs with the files in output_dir without writing.

    Returns one report per file that is missing or differs: its path, the
    first CHECK_DIFF_LINES lines of a unified diff, and how to fix it.
    """
    reports: List[str] = []
    for name in sorted(outputs):
        path = os.path.join(output_dir, name)
        expected = outputs[name]
        actual = _read_existing(path)
        if actual == expected:
            continue
        if actual is None:
            lines = ["%s: missing or unreadable" % path]
        else:
            lines = ["%s: out of date" % path]
            diff = list(difflib.unified_diff(
                actual.decode("utf-8", "replace").splitlines(),
                expected.decode("ascii").splitlines(),
                fromfile="a/" + name + " (current)",
                tofile="b/" + name + " (generated)",
                lineterm=""))
            lines += diff[:CHECK_DIFF_LINES]
            if len(diff) > CHECK_DIFF_LINES:
                lines.append("... (%d more diff lines)"
                             % (len(diff) - CHECK_DIFF_LINES))
        lines.append("To fix, run: python3 %s" % GENERATOR)
        reports.append("\n".join(lines))
    return reports


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate src/unicode_tables.h and src/entity_table.h "
                    "from the input data in tools/data.")
    parser.add_argument(
        "--check", action="store_true",
        help="do not write; fail if the generated files are out of date")
    parser.add_argument(
        "--data-dir", default=DEFAULT_DATA_DIR, metavar="DIR",
        help="directory holding the input data (default: tools/data)")
    parser.add_argument(
        "--output-dir", default=DEFAULT_OUTPUT_DIR, metavar="DIR",
        help="directory for the generated files (default: src)")
    return parser.parse_args(argv)


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    try:
        outputs = build_outputs(read_inputs(args.data_dir))
    except InputError as e:
        print("gen_tables.py: %s" % e, file=sys.stderr)
        return 1
    if args.check:
        reports = check_outputs(args.output_dir, outputs)
        for report in reports:
            print(report, file=sys.stderr)
        return 1 if reports else 0
    try:
        written = write_outputs(args.output_dir, outputs)
    except OSError as e:
        print("gen_tables.py: cannot write %s: %s"
              % (e.filename or args.output_dir, e.strerror or e),
              file=sys.stderr)
        return 1
    for path in written:
        print(path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
