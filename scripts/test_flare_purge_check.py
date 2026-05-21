#!/usr/bin/env python3
"""Tests for flare_purge_check.py — synthetic traces with known verdicts.

Validates the compression-overfeed-stop criteria: a purge that reaches
COMPRESSION must show capped overfill (SYNC_RELIEVE_MM) and a short COMPRESSION
dwell (CT), not the multi-mm / multi-second grind of the old SYNC_MIN-forward
behavior.
"""
import unittest

import flare_purge_check as fpc


def status(buf, bp, est, mm, sm=1, relieve=0.0, ct=0, refill=0.0):
    return (f"OK:LN:2,BUF:{buf},MM:{mm:.1f},BP:{bp:.2f},EST:{est:.1f},"
            f"RE:0.00,AV:0.00,CT:{ct},SM:{sm},ST:1,"
            f"SYNC_RELIEVE_MM:{relieve:.1f},SYNC_REFILL_MM:{refill:.1f}")


class ParseTests(unittest.TestCase):
    def test_parse_status_line(self):
        f = fpc.parse_status_line(status("COMPRESSION", -5.1, 1071, 0,
                                         relieve=1.4, ct=400))
        self.assertEqual(f["BUF"], "COMPRESSION")
        self.assertAlmostEqual(float(f["SYNC_RELIEVE_MM"]), 1.4)
        self.assertAlmostEqual(float(f["CT"]), 400)

    def test_non_status_ignored(self):
        self.assertIsNone(fpc.parse_status_line("EV:BS:TENSION,40.1,5.00"))
        self.assertIsNone(fpc.parse_status_line("OK"))

    def test_stream_splits_events(self):
        lines = [status("NEUTRAL", 0, 100, 50),
                 "EV:SYNC:RELIEF_PAUSE",
                 status("COMPRESSION", -5, 0, 0)]
        samples, events = fpc.parse_stream(lines)
        self.assertEqual(len(samples), 2)
        self.assertEqual(len(events), 1)

    def test_state_runs(self):
        lines = [status("COMPRESSION", -5, 0, 0),
                 status("NEUTRAL", 0, 100, 50),
                 status("COMPRESSION", -5, 0, 0)]
        samples, _ = fpc.parse_stream(lines)
        self.assertEqual(fpc.state_runs(samples, "COMPRESSION"), [(0, 0), (2, 2)])


class PurgeTests(unittest.TestCase):
    def _purge(self, relieve_series, ct_series, mm_series):
        # tension surge -> neutral glide -> compression episode
        lines = [status("TENSION", 5.0, 1300, 1700) for _ in range(3)]
        lines += [status("NEUTRAL", 4.0 - k, 1345, 1680) for k in range(6)]
        for rel, ct, mm in zip(relieve_series, ct_series, mm_series):
            lines.append(status("COMPRESSION", -5.1, 200, mm, relieve=rel, ct=ct))
        return fpc.parse_stream(lines)

    def test_capped_overfill_passes(self):
        # feed drops to ~0, overfill capped at 1.5mm, short dwell -> the fix works
        rel = [0.4, 0.9, 1.4, 1.5, 1.5]
        ct = [100, 200, 300, 400, 450]
        mm = [900, 200, 0, 0, 0]
        samples, events = self._purge(rel, ct, mm)
        verdict, _ = fpc.analyze_purge(samples, events, 5.0, 12.5)
        self.assertEqual(verdict, "PASS")

    def test_overfeed_grind_fails(self):
        # old behavior: SYNC_MIN keeps feeding, ~8mm over ~5s
        rel = [2, 4, 6, 8, 10]
        ct = [1000, 2000, 3000, 4000, 4900]
        mm = [120, 100, 80, 100, 120]
        samples, events = self._purge(rel, ct, mm)
        verdict, _ = fpc.analyze_purge(samples, events, 5.0, 12.5)
        self.assertEqual(verdict, "FAIL")

    def test_no_compression_inconclusive(self):
        lines = [status("NEUTRAL", 0.0, 50, 60) for _ in range(6)]
        samples, events = fpc.parse_stream(lines)
        verdict, _ = fpc.analyze_purge(samples, events, 5.0, 12.5)
        self.assertEqual(verdict, "INCONCLUSIVE")


class RegressionTests(unittest.TestCase):
    def test_clean_run_passes(self):
        lines = []
        for _ in range(5):
            lines.append(status("TENSION", 5.0, 600, 1500))
            lines += [status("NEUTRAL", 0.5, 600, 700) for _ in range(3)]
            lines += [status("COMPRESSION", -5.0, 600, 0, relieve=0.3, ct=200)]
        samples, events = fpc.parse_stream(lines)
        verdict, _ = fpc.analyze_regression(samples, events, 5.0)
        self.assertEqual(verdict, "PASS")

    def test_starvation_event_fails(self):
        lines = [status("TENSION", 5.0, 600, 1500),
                 "EV:SYNC:cannot_refill",
                 status("NEUTRAL", 0.5, 600, 700)]
        samples, events = fpc.parse_stream(lines)
        verdict, _ = fpc.analyze_regression(samples, events, 5.0)
        self.assertEqual(verdict, "FAIL")

    def test_overpausing_relief_fails(self):
        lines = []
        for _ in range(3):
            lines += [status("COMPRESSION", -5.0, 600, 0, relieve=0.3, ct=200)]
            lines.append("EV:SYNC:RELIEF_PAUSE")
            lines += [status("NEUTRAL", 0.5, 600, 700)]
        samples, events = fpc.parse_stream(lines)
        verdict, _ = fpc.analyze_regression(samples, events, 5.0)
        self.assertEqual(verdict, "FAIL")


if __name__ == "__main__":
    unittest.main()
