#!/usr/bin/env python3
"""Runs every host-compiled C unit test binary in build_sim (test_*), so a new
tests/host/test_*.c target is picked up by the regression gate without a
per-binary wrapper. Each binary prints '=== All <name> tests PASSED ===' (or the
persistence suite's own banner) and exits 0 on success."""

import glob
import os
import subprocess
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD_SIM = os.path.join(REPO_ROOT, "build_sim")
EXPECTED = {"test_toolchange", "test_forensics", "test_tmc_recovery", "test_persistence"}


def _binaries():
    return sorted(
        p for p in glob.glob(os.path.join(BUILD_SIM, "test_*"))
        if os.access(p, os.X_OK) and not os.path.isdir(p) and "." not in os.path.basename(p)
    )


class TestHostUnits(unittest.TestCase):
    def test_all_expected_binaries_built(self):
        found = {os.path.basename(p) for p in _binaries()}
        if not found:
            self.skipTest(f"no test_* binaries in {BUILD_SIM} (build_sim missing)")
        self.assertTrue(EXPECTED <= found, f"missing host unit binaries: {sorted(EXPECTED - found)}")

    def test_each_binary_passes(self):
        bins = _binaries()
        if not bins:
            self.skipTest(f"no test_* binaries in {BUILD_SIM} (build_sim missing)")
        for path in bins:
            with self.subTest(binary=os.path.basename(path)):
                res = subprocess.run([path], capture_output=True, text=True, check=False)
                self.assertEqual(res.returncode, 0, f"{path} failed ({res.returncode}):\n{res.stdout}\n{res.stderr}")
                self.assertIn("passed", res.stdout.lower())


if __name__ == "__main__":
    unittest.main()
