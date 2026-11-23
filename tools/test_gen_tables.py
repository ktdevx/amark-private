#!/usr/bin/env python3
# Copyright (c) The Amark Authors
# SPDX-License-Identifier: MIT
"""Self-test for tools/gen_tables.py.

Builds small synthetic inputs in a temporary directory and runs the generator
next to this file as a subprocess with --data-dir and --output-dir pointing
there, so nothing in the repository is read as input or written.

Run from any directory:  python3 -B tools/test_gen_tables.py [-v]
Exits 0 when every case passes. Needs Python 3.9 or later and only the
standard library.
"""

from __future__ import annotations

import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from typing import Dict, List, Optional

sys.dont_write_bytecode = True

GENERATOR = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "gen_tables.py")

GC_FILE = "DerivedGeneralCategory.txt"
CF_FILE = "CaseFolding.txt"
ENTITIES_FILE = "entities.json"
UNICODE_TABLES_FILE = "unicode_tables.h"
ENTITY_TABLE_FILE = "entity_table.h"
OUTPUT_FILES = (ENTITY_TABLE_FILE, UNICODE_TABLES_FILE)

VERSION = "15.1.0"

# Line numbers below are referred to by the input-error cases.
GC_LINES = [
    "# DerivedGeneralCategory-%s.txt" % VERSION,   # 1
    "# Synthetic input for the generator self-test.",
    "",
    "0020          ; Zs # SPACE",
    "00A0          ; Zs # NO-BREAK SPACE",          # 5
    "0021..0023    ; Po # EXCLAMATION MARK..NUMBER SIGN",
    "0024          ; Sc # DOLLAR SIGN",
    "0025          ; Po # PERCENT SIGN",
    "0041..005A    ; Lu # LATIN CAPITAL LETTER A..Z",
    "2028          ; Zl # LINE SEPARATOR",          # 10
    "",
]
GC_MALFORMED_LINE = 7

CF_LINES = [
    "# CaseFolding-%s.txt" % VERSION,              # 1
    "# Synthetic input for the generator self-test.",
    "",
    "0041; C; 0061; # LATIN CAPITAL LETTER A",
    "0049; C; 0069; # LATIN CAPITAL LETTER I",     # 5
    "0049; T; 0131; # LATIN CAPITAL LETTER I (Turkic)",
    "00DF; F; 0073 0073; # LATIN SMALL LETTER SHARP S",
    "0390; F; 03B9 0308 0301; # GREEK SMALL LETTER IOTA WITH ...",
    "1E9E; F; 0073 0073; # LATIN CAPITAL LETTER SHARP S",
    "1E9E; S; 1E9F; # LATIN CAPITAL LETTER SHARP S (simple)",  # 10
    "",
]
CF_F_LINE = 8
CF_MALFORMED_LINE = 5

ENTITIES_TEXT = "\n".join([
    "{",
    '  "&amp;": { "codepoints": [38], "characters": "\\u0026" },',
    '  "&amp": { "codepoints": [38], "characters": "\\u0026" },',
    '  "&nbsp;": { "codepoints": [160], "characters": "\\u00A0" },',
    '  "&legacyonly": { "codepoints": [169], "characters": "\\u00A9" },',
    '  "&NotEqualTilde;": { "codepoints": [8770, 824], '
    '"characters": "\\u2242\\u0338" }',
    "}",
    "",
])
ENTITIES_WITH_SEMICOLON = 3


def _write(path: str, data: bytes) -> None:
    with open(path, "wb") as f:
        f.write(data)


def _read(path: str) -> bytes:
    with open(path, "rb") as f:
        return f.read()


def _lines_bytes(lines: List[str]) -> bytes:
    return "\n".join(lines).encode("ascii")


class Result:
    def __init__(self, returncode: int, stdout: str, stderr: str) -> None:
        self.returncode = returncode
        self.stdout = stdout
        self.stderr = stderr

    def __str__(self) -> str:
        return "exit %d\nstdout:\n%s\nstderr:\n%s" % (
            self.returncode, self.stdout, self.stderr)


class GeneratorTestCase(unittest.TestCase):
    """Fresh data and output directories for every test."""

    def setUp(self) -> None:
        self.tmp = tempfile.mkdtemp(prefix="amark-gen-tables-test-")
        self.addCleanup(shutil.rmtree, self.tmp)
        self.data_dir = os.path.join(self.tmp, "data")
        self.output_dir = os.path.join(self.tmp, "out")
        os.mkdir(self.data_dir)
        os.mkdir(self.output_dir)
        self.write_inputs()

    def write_inputs(self, gc: Optional[List[str]] = None,
                     cf: Optional[List[str]] = None,
                     entities: Optional[str] = None) -> None:
        _write(os.path.join(self.data_dir, GC_FILE),
               _lines_bytes(GC_LINES if gc is None else gc))
        _write(os.path.join(self.data_dir, CF_FILE),
               _lines_bytes(CF_LINES if cf is None else cf))
        _write(os.path.join(self.data_dir, ENTITIES_FILE),
               (ENTITIES_TEXT if entities is None else entities)
               .encode("utf-8"))

    def run_generator(self, *args: str, output_dir: Optional[str] = None,
                      cwd: Optional[str] = None) -> Result:
        command = [sys.executable, "-B", GENERATOR,
                   "--data-dir", self.data_dir,
                   "--output-dir", output_dir or self.output_dir]
        command += list(args)
        proc = subprocess.run(
            command, cwd=cwd or self.tmp,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        return Result(proc.returncode,
                      proc.stdout.decode("utf-8", "replace"),
                      proc.stderr.decode("utf-8", "replace"))

    def generate(self, output_dir: Optional[str] = None) -> Dict[str, bytes]:
        result = self.run_generator(output_dir=output_dir)
        self.assertEqual(result.returncode, 0, str(result))
        directory = output_dir or self.output_dir
        return {name: _read(os.path.join(directory, name))
                for name in OUTPUT_FILES}

    def snapshot(self) -> Dict[str, object]:
        """Names, contents and mtimes of everything in the output dir."""
        state: Dict[str, object] = {}
        for name in sorted(os.listdir(self.output_dir)):
            path = os.path.join(self.output_dir, name)
            state[name] = (_read(path), os.stat(path).st_mtime_ns)
        return state

    def age_outputs(self) -> None:
        """Move every output's mtime into the past so a rewrite shows."""
        for name in os.listdir(self.output_dir):
            os.utime(os.path.join(self.output_dir, name),
                     ns=(1000000000 * 10 ** 9, 1000000000 * 10 ** 9))


class ReproducibilityTest(GeneratorTestCase):
    """1.2: same input, same bytes; LF only; ASCII only."""

    def test_two_runs_are_byte_identical(self) -> None:
        other = os.path.join(self.tmp, "out2")
        os.mkdir(other)
        first = self.generate()
        second = self.generate(output_dir=other)
        self.assertEqual(first, second)

    def test_independent_of_working_directory(self) -> None:
        first = self.generate()
        other = os.path.join(self.tmp, "out2")
        os.mkdir(other)
        result = self.run_generator(output_dir=other, cwd=self.data_dir)
        self.assertEqual(result.returncode, 0, str(result))
        for name in OUTPUT_FILES:
            self.assertEqual(_read(os.path.join(other, name)), first[name])

    def test_output_is_ascii_without_cr(self) -> None:
        for name, data in self.generate().items():
            with self.subTest(file=name):
                self.assertNotIn(b"\r", data)
                self.assertTrue(all(b < 0x80 for b in data),
                                "%s has a non-ASCII byte" % name)
                self.assertTrue(data.endswith(b"\n"))
                self.assertFalse(data.endswith(b"\n\n"))

    def test_success_reports_written_files_and_leaves_nothing_else(
            self) -> None:
        result = self.run_generator()
        self.assertEqual(result.returncode, 0, str(result))
        for name in OUTPUT_FILES:
            self.assertIn(name, result.stdout)
        self.assertEqual(sorted(os.listdir(self.output_dir)),
                         sorted(OUTPUT_FILES))

    def test_rerun_leaves_unchanged_outputs_untouched(self) -> None:
        self.generate()
        self.age_outputs()
        before = self.snapshot()
        result = self.run_generator()
        self.assertEqual(result.returncode, 0, str(result))
        self.assertEqual(self.snapshot(), before)


class InputErrorTest(GeneratorTestCase):
    """1.7, 2.5: exit 1, name the file (and line), keep existing outputs."""

    SENTINEL = b"/* placed before the run; must survive it */\n"

    def setUp(self) -> None:
        super().setUp()
        for name in OUTPUT_FILES:
            _write(os.path.join(self.output_dir, name), self.SENTINEL)
        self.age_outputs()
        self.before = self.snapshot()

    def assert_input_error(self, file_name: str,
                           line: Optional[int] = None) -> Result:
        result = self.run_generator()
        self.assertEqual(result.returncode, 1, str(result))
        self.assertIn(file_name, result.stderr)
        if line is not None:
            self.assertIn("%s:%d:" % (file_name, line), result.stderr)
        self.assertNotIn("Traceback", result.stderr)
        self.assertEqual(self.snapshot(), self.before)
        return result

    def test_missing_file(self) -> None:
        for name in (GC_FILE, CF_FILE, ENTITIES_FILE):
            with self.subTest(file=name):
                self.write_inputs()
                os.remove(os.path.join(self.data_dir, name))
                self.assert_input_error(name)

    def test_malformed_general_category_line(self) -> None:
        gc = list(GC_LINES)
        gc[GC_MALFORMED_LINE - 1] = "0024 Sc # missing separator"
        self.write_inputs(gc=gc)
        self.assert_input_error(GC_FILE, GC_MALFORMED_LINE)

    def test_malformed_case_folding_line(self) -> None:
        cf = list(CF_LINES)
        cf[CF_MALFORMED_LINE - 1] = "0049; C 0069; # missing separator"
        self.write_inputs(cf=cf)
        self.assert_input_error(CF_FILE, CF_MALFORMED_LINE)

    def test_malformed_entities(self) -> None:
        self.write_inputs(entities=ENTITIES_TEXT.replace(
            '"&nbsp;"', '"&nb-sp;"'))
        self.assert_input_error(ENTITIES_FILE, 4)

    def test_unicode_version_mismatch(self) -> None:
        cf = list(CF_LINES)
        cf[0] = "# CaseFolding-15.0.0.txt"
        self.write_inputs(cf=cf)
        result = self.assert_input_error(CF_FILE)
        self.assertIn(VERSION, result.stderr)
        self.assertIn("15.0.0", result.stderr)

    def test_full_folding_with_four_targets(self) -> None:
        cf = list(CF_LINES)
        cf[CF_F_LINE - 1] = "0390; F; 03B9 0308 0301 0301; # four targets"
        self.write_inputs(cf=cf)
        self.assert_input_error(CF_FILE, CF_F_LINE)

    def test_argument_error_exits_2(self) -> None:
        result = self.run_generator("--no-such-option")
        self.assertEqual(result.returncode, 2, str(result))
        self.assertEqual(self.snapshot(), self.before)


class CheckTest(GeneratorTestCase):
    """3.2, 3.4: --check compares without writing."""

    def setUp(self) -> None:
        super().setUp()
        self.generate()
        self.age_outputs()

    def test_passes_right_after_generation(self) -> None:
        before = self.snapshot()
        result = self.run_generator("--check")
        self.assertEqual(result.returncode, 0, str(result))
        self.assertEqual(self.snapshot(), before)

    def test_one_changed_byte_fails_and_names_the_file(self) -> None:
        for changed in OUTPUT_FILES:
            with self.subTest(file=changed):
                self.generate()
                path = os.path.join(self.output_dir, changed)
                data = bytearray(_read(path))
                middle = len(data) // 2
                data[middle] = ord("x") if data[middle] != ord("x") \
                    else ord("y")
                _write(path, bytes(data))
                self.age_outputs()
                before = self.snapshot()
                result = self.run_generator("--check")
                self.assertEqual(result.returncode, 1, str(result))
                self.assertIn(changed, result.stderr)
                unchanged = [n for n in OUTPUT_FILES if n != changed][0]
                self.assertNotIn(unchanged, result.stderr)
                self.assertEqual(self.snapshot(), before)

    def test_missing_output_fails_and_names_the_file(self) -> None:
        os.remove(os.path.join(self.output_dir, ENTITY_TABLE_FILE))
        before = self.snapshot()
        result = self.run_generator("--check")
        self.assertEqual(result.returncode, 1, str(result))
        self.assertIn(ENTITY_TABLE_FILE, result.stderr)
        self.assertEqual(self.snapshot(), before)


class ContentTest(GeneratorTestCase):
    """1.4, 1.5, 1.6, 6.4, 9.3: what the outputs say and leave out."""

    def setUp(self) -> None:
        super().setUp()
        self.outputs = {name: data.decode("ascii")
                        for name, data in self.generate().items()}

    def header(self, name: str) -> str:
        """The leading comment block, up to and including ' */'."""
        text = self.outputs[name]
        self.assertTrue(text.startswith("/*\n"), name)
        end = text.index("\n */\n")
        return text[:end]

    def test_first_five_lines_carry_the_notices(self) -> None:
        for name in OUTPUT_FILES:
            with self.subTest(file=name):
                first5 = "\n".join(self.outputs[name].split("\n")[:5])
                self.assertIn("Copyright (c) The Amark Authors", first5)
                self.assertIn("SPDX-License-Identifier: MIT", first5)

    def test_unicode_header(self) -> None:
        header = self.header(UNICODE_TABLES_FILE)
        self.assertIn("SPDX-License-Identifier: MIT AND Unicode-3.0", header)
        self.assertIn("Generated by tools/gen_tables.py. Do not edit.",
                      header)
        self.assertIn("DerivedGeneralCategory-%s.txt" % VERSION, header)
        self.assertIn("CaseFolding-%s.txt" % VERSION, header)
        self.assertIn("Unicode Character Database", header)
        self.assertIn("Unicode, Inc.", header)
        self.assertIn("Unicode License v3", header)

    def test_entity_header(self) -> None:
        header = self.header(ENTITY_TABLE_FILE)
        sha256 = hashlib.sha256(ENTITIES_TEXT.encode("utf-8")).hexdigest()
        self.assertIn("SPDX-License-Identifier: MIT AND BSD-3-Clause", header)
        self.assertIn("Generated by tools/gen_tables.py. Do not edit.",
                      header)
        self.assertIn("entities.json", header)
        self.assertIn(sha256, header)
        self.assertIn("HTML Standard", header)
        self.assertIn("WHATWG", header)
        self.assertIn("BSD 3-Clause License", header)

    def test_counts_follow_the_input(self) -> None:
        unicode_text = self.outputs[UNICODE_TABLES_FILE]
        # 0021..0023, 0024, 0025 join into one range; Zs is 0020 and 00A0.
        self.assertIn("#define AMARK_UNICODE_PUNCTUATION_COUNT 1\n",
                      unicode_text)
        self.assertIn("{0x0021, 0x0025}", unicode_text)
        self.assertIn("#define AMARK_UNICODE_SPACE_COUNT 2\n", unicode_text)
        self.assertIn("#define AMARK_UNICODE_FOLD_SIMPLE_COUNT 2\n",
                      unicode_text)
        self.assertIn("#define AMARK_UNICODE_FOLD_FULL_COUNT 3\n",
                      unicode_text)
        self.assertIn("{0x0390, {0x03B9, 0x0308, 0x0301}}", unicode_text)
        self.assertIn("{0x1E9E, {0x0073, 0x0073, 0x0000}}", unicode_text)
        self.assertIn("#define AMARK_ENTITY_COUNT %d\n"
                      % ENTITIES_WITH_SEMICOLON,
                      self.outputs[ENTITY_TABLE_FILE])

    def test_other_categories_are_left_out(self) -> None:
        unicode_text = self.outputs[UNICODE_TABLES_FILE]
        self.assertNotIn("0x0041, 0x005A", unicode_text)   # Lu
        self.assertNotIn("0x2028", unicode_text)            # Zl

    def test_status_s_and_t_are_left_out(self) -> None:
        unicode_text = self.outputs[UNICODE_TABLES_FILE]
        self.assertNotIn("0x1E9F", unicode_text)            # S target
        self.assertNotIn("0x0131", unicode_text)            # T target
        self.assertIn("{0x0049, 0x0069}", unicode_text)     # C kept

    def test_names_without_semicolon_are_left_out(self) -> None:
        entity_text = self.outputs[ENTITY_TABLE_FILE]
        self.assertNotIn("legacyonly", entity_text)
        self.assertNotIn("'l', 'e', 'g'", entity_text)
        self.assertNotIn("0xC2, 0xA9", entity_text)         # U+00A9
        self.assertIn("/* amp */", entity_text)
        self.assertIn("#define AMARK_ENTITY_NAMES_LENGTH %d\n"
                      % len("NotEqualTildeampnbsp"), entity_text)


if __name__ == "__main__":
    unittest.main()
