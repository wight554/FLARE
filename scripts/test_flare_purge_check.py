#!/usr/bin/env python3
"""Tests for flare_purge_check.py — synthetic traces with known verdicts."""
import unittest

import flare_purge_check as fpc


def status(buf, bp, est, mm, sm=1):
    return (f"OK:LN:2,BUF:{buf},MM:{mm:.1f},BP:{bp:.2f},EST:{est:.1f},"
            f"RE:0.00,AV:0.00,SM:{sm},ST:1,CT:0")


def tension_surge():
    # EST climbs while pinned at the tension switch (+5).
    out = []
    for est in (5, 130, 280, 460, 650, 860, 1071):
        out.append(status("TENSION", 5.0, est, 1500))
    return out


def neutral_glide(est_series, bp_start=4.2, bp_end=-4.0):
    n = len(est_series)
    out = []
    for k, est in enumerate(est_series):
        bp = bp_start + (bp_end - bp_start) * k / (n - 1)
        out.append(status("NEUTRAL", bp, est, 1322))
    return out


class ParseTests(unittest.TestCase):
    def test_parse_status_line(self):
        f = fpc.parse_status_line(status("NEUTRAL", -1.5, 1071, 1322))
        self.assertEqual(f["BUF"], "NEUTRAL")
        self.assertAlmostEqual(float(f["BP"]), -1.5)
        self.assertAlmostEqual(float(f["EST"]), 1071.0)

    def test_non_status_ignored(self):
        self.assertIsNone(fpc.parse_status_line("EV:BS:TENSION,40.1,5.00"))
        self.assertIsNone(fpc.parse_status_line("OK"))

    def test_stream_splits_events(self):
        lines = [status("NEUTRAL", 0, 100, 50),
                 "EV:SYNC:RELIEF_PAUSE",
                 status("COMPRESSION", -5, 100, 50)]
        samples, events = fpc.parse_stream(lines)
        self.assertEqual(len(samples), 2)
        self.assertEqual(len(events), 1)
        self.assertIn("RELIEF_PAUSE", events[0][1])

    def test_neutral_runs_respect_sm(self):
        lines = [status("NEUTRAL", 0, 100, 50, sm=1),
                 status("NEUTRAL", 0, 100, 50, sm=0),  # sync off -> break
                 status("NEUTRAL", 0, 100, 50, sm=1)]
        samples, _ = fpc.parse_stream(lines)
        runs = fpc.neutral_runs(samples)
        self.assertEqual(runs, [(0, 0), (2, 2)])


class PurgeTests(unittest.TestCase):
    def test_frozen_est_fails(self):
        # EST stuck at 1071 across the whole glide -> the bug.
        lines = tension_surge() + neutral_glide([1071] * 8, bp_end=-4.5)
        samples, events = fpc.parse_stream(lines)
        verdict, _ = fpc.analyze_purge(samples, events, threshold=5.0, hardwall=12.5)
        self.assertEqual(verdict, "FAIL")

    def test_decaying_est_passes(self):
        # EST backs off 1071 -> 200 as the buffer slides -> corrector works.
        est = [1071, 950, 800, 640, 480, 350, 250, 200]
        lines = tension_surge() + neutral_glide(est, bp_end=-3.0)
        samples, events = fpc.parse_stream(lines)
        verdict, _ = fpc.analyze_purge(samples, events, threshold=5.0, hardwall=12.5)
        self.assertEqual(verdict, "PASS")

    def test_no_glide_inconclusive(self):
        lines = [status("NEUTRAL", 0.0, 50, 60) for _ in range(6)]
        samples, events = fpc.parse_stream(lines)
        verdict, _ = fpc.analyze_purge(samples, events, threshold=5.0, hardwall=12.5)
        self.assertEqual(verdict, "INCONCLUSIVE")


class RegressionTests(unittest.TestCase):
    def test_clean_run_passes(self):
        lines = []
        for _ in range(5):
            lines.append(status("TENSION", 5.0, 600, 1500))
            lines += [status("NEUTRAL", 0.5, 600, 700) for _ in range(4)]
        samples, events = fpc.parse_stream(lines)
        verdict, _ = fpc.analyze_regression(samples, events, threshold=5.0)
        self.assertEqual(verdict, "PASS")

    def test_starvation_event_fails(self):
        lines = [status("TENSION", 5.0, 600, 1500),
                 "EV:SYNC:TENSION_DWELL_WARN",
                 status("NEUTRAL", 0.5, 600, 700)]
        samples, events = fpc.parse_stream(lines)
        verdict, _ = fpc.analyze_regression(samples, events, threshold=5.0)
        self.assertEqual(verdict, "FAIL")

    def test_spurious_fire_fails(self):
        # Leave TENSION with high EST, then EST collapses while BP holds mid-band
        # (no slide to compression) -> corrector fired when it should be inhibited.
        lines = [status("TENSION", 5.0, 1000, 1500)]
        lines += [status("NEUTRAL", 0.5, est, 700)
                  for est in (1000, 850, 700, 550, 400, 300, 250, 200, 200)]
        samples, events = fpc.parse_stream(lines)
        verdict, _ = fpc.analyze_regression(samples, events, threshold=5.0)
        self.assertEqual(verdict, "FAIL")


if __name__ == "__main__":
    unittest.main()
