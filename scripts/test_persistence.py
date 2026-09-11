#!/usr/bin/env python3
"""Unit test runner for host flash persistence and brownout recovery tests."""

import os
import subprocess
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEST_BINARY = os.path.join(REPO_ROOT, "build_sim", "test_persistence")


class TestPersistence(unittest.TestCase):
    def test_host_persistence_suite(self):
        if not os.path.exists(TEST_BINARY):
            self.skipTest(f"test_persistence binary not found at {TEST_BINARY} (build_sim missing)")
        res = subprocess.run([TEST_BINARY], capture_output=True, text=True, check=False)
        self.assertEqual(
            res.returncode, 0, f"test_persistence failed with code {res.returncode}:\n{res.stdout}\n{res.stderr}"
        )
        self.assertIn("All persistence host unit tests PASSED", res.stdout)


if __name__ == "__main__":
    unittest.main()
