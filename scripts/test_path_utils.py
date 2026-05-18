#!/usr/bin/env python3
"""Unit tests for path_utils.py."""

import os
import tempfile
import unittest
from path_utils import normalize_output, resolve_input, expand_input_paths, PathError


class TestPathUtils(unittest.TestCase):
    def test_normalize_output(self):
        self.assertEqual(normalize_output(""), "")
        self.assertEqual(normalize_output(None), None)
        # expanduser check
        home = os.path.expanduser("~")
        self.assertEqual(normalize_output("~/test.gcode"), os.path.join(home, "test.gcode"))
        # never globbed
        self.assertEqual(normalize_output("*.gcode"), "*.gcode")

    def test_resolve_input(self):
        with tempfile.TemporaryDirectory() as td:
            f1 = os.path.join(td, "file1.csv")
            f2 = os.path.join(td, "file2.csv")
            with open(f1, "w") as f: f.write("test")
            with open(f2, "w") as f: f.write("test")

            # Literal match
            self.assertEqual(resolve_input(f1), f1)
            
            # Glob match (single)
            self.assertEqual(resolve_input(os.path.join(td, "file1.*")), f1)

            # No match
            with self.assertRaises(PathError) as cm:
                resolve_input(os.path.join(td, "missing.*"))
            self.assertIn("no files matched", cm.exception.reason)

            # Ambiguous match
            with self.assertRaises(PathError) as cm:
                resolve_input(os.path.join(td, "*.csv"))
            self.assertIn("ambiguous match", cm.exception.reason)

            # Not a file (directory)
            with self.assertRaises(PathError) as cm:
                resolve_input(td)
            self.assertIn("not a regular file", cm.exception.reason)

            # Missing literal
            with self.assertRaises(PathError) as cm:
                resolve_input(os.path.join(td, "nonexistent"))
            self.assertIn("file not found", cm.exception.reason)

    def test_expand_input_paths(self):
        with tempfile.TemporaryDirectory() as td:
            f1 = os.path.join(td, "a.csv")
            f2 = os.path.join(td, "b.csv")
            sub = os.path.join(td, "sub")
            os.mkdir(sub)
            f3 = os.path.join(sub, "c.csv")
            
            for f in [f1, f2, f3]:
                with open(f, "w") as fh: fh.write("test")

            # Literal list
            self.assertEqual(expand_input_paths([f1, f2]), [f1, f2])
            
            # Simple glob
            self.assertEqual(expand_input_paths(os.path.join(td, "*.csv")), [f1, f2])
            
            # Recursive glob
            self.assertEqual(expand_input_paths(os.path.join(td, "**/*.csv")), [f1, f2, f3])
            
            # Mixed list
            self.assertEqual(expand_input_paths([f1, os.path.join(sub, "*.csv")]), [f1, f3])
            
            # Deduplication and sorting
            self.assertEqual(expand_input_paths([f2, f1, f2]), [f1, f2])

            # Glob no match
            with self.assertRaises(PathError) as cm:
                expand_input_paths(os.path.join(td, "*.txt"))
            self.assertIn("no files matched", cm.exception.reason)

            # Literal missing
            with self.assertRaises(PathError) as cm:
                expand_input_paths(os.path.join(td, "missing.csv"))
            self.assertIn("file not found", cm.exception.reason)

    def test_tilde_expansion(self):
        # We can't easily test real home expansion without mocking, but we can verify it calls it.
        # Just check that it doesn't crash and changes the path if it starts with ~
        home = os.path.expanduser("~")
        self.assertTrue(normalize_output("~/test").startswith(home))
        # For input, we'd need a real file. Skip deep testing of expanduser itself.

    def test_analyzer_parity(self):
        import glob
        with tempfile.TemporaryDirectory() as td:
            files = [os.path.join(td, f"run_{i}.csv") for i in range(5)]
            for f in files:
                with open(f, "w") as fh: fh.write("test")
            
            pattern = os.path.join(td, "*.csv")
            
            # Old analyzer logic:
            # if glob.has_magic(path):
            #     matches = sorted(glob.glob(path))
            old_result = sorted(glob.glob(pattern))
            
            # New helper logic:
            new_result = expand_input_paths(pattern)
            
            self.assertEqual(old_result, new_result)
            self.assertEqual(len(new_result), 5)

if __name__ == "__main__":
    unittest.main()
