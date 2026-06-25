#!/usr/bin/env python3
"""Tests for flare_daemon.record_event_stats — the MMU usage counters.

Focus: a swap (TC:DONE) must count an unload as well as the load, since the
firmware toolchange emits no standalone UNLOADED for its internal unload phase
(only the trailing LOADED). Without this, loads >> unloads (the reported bug).
"""
import os
import tempfile
import unittest

# Hermetic DB: point FLARE_DATA_DIR at a temp dir before importing the daemon
# so nothing touches the real ~/.local/share/flare/flare.db.
os.environ["FLARE_DATA_DIR"] = tempfile.mkdtemp(prefix="flare_test_stats_")

import flare_daemon as fd  # noqa: E402


class StatsCounterTests(unittest.TestCase):
    def setUp(self):
        # Reset counters and stub the SQLite write so tests stay in-memory.
        with fd.stats_lock:
            fd.mmu_stats["swaps_total"] = 0
            fd.mmu_stats["swaps_success"] = 0
            fd.mmu_stats["swaps_failed"] = 0
            fd.mmu_stats["loads_success"] = 0
            fd.mmu_stats["unloads_success"] = 0
            fd.mmu_stats["last_error"] = "None"
        self._save = fd.save_mmu_stats
        fd.save_mmu_stats = lambda: None

    def tearDown(self):
        fd.save_mmu_stats = self._save

    def test_swap_counts_load_and_unload(self):
        # Each swap: firmware emits LOADED then TC:DONE.
        n = 5
        for _ in range(n):
            fd.record_event_stats("LOADED", None)
            fd.record_event_stats("TC:DONE", None)
        self.assertEqual(fd.mmu_stats["swaps_total"], n)
        self.assertEqual(fd.mmu_stats["swaps_success"], n)
        self.assertEqual(fd.mmu_stats["loads_success"], n)
        # The fix: a swap also counts an unload, so loads/unloads stay symmetric.
        self.assertEqual(fd.mmu_stats["unloads_success"], n)
        self.assertEqual(fd.mmu_stats["loads_success"],
                         fd.mmu_stats["unloads_success"])

    def test_manual_load_and_unload_counted_standalone(self):
        fd.record_event_stats("UNLOADED", None)
        fd.record_event_stats("LOADED", None)
        self.assertEqual(fd.mmu_stats["unloads_success"], 1)
        self.assertEqual(fd.mmu_stats["loads_success"], 1)
        self.assertEqual(fd.mmu_stats["swaps_total"], 0)

    def test_failed_swap_counts_no_load_or_unload(self):
        fd.record_event_stats("TC:ERROR", "ABORTED")
        self.assertEqual(fd.mmu_stats["swaps_total"], 1)
        self.assertEqual(fd.mmu_stats["swaps_failed"], 1)
        self.assertEqual(fd.mmu_stats["swaps_success"], 0)
        self.assertEqual(fd.mmu_stats["loads_success"], 0)
        self.assertEqual(fd.mmu_stats["unloads_success"], 0)
        self.assertEqual(fd.mmu_stats["last_error"], "ABORTED")

    def test_unknown_event_ignored(self):
        fd.record_event_stats("SOMETHING_ELSE", None)
        self.assertEqual(fd.mmu_stats["swaps_total"], 0)
        self.assertEqual(fd.mmu_stats["loads_success"], 0)
        self.assertEqual(fd.mmu_stats["unloads_success"], 0)


if __name__ == "__main__":
    unittest.main()
