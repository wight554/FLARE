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
            lines.append(status("COMPRESSION", -5.0, 200, mm, relieve=rel, ct=ct))
        return fpc.parse_stream(lines)

    def test_truestop_passes(self):
        # feed ramps to 0 and holds, overfill ~0 -> the fix works
        mm = [280, 200, 120, 40] + [0] * 16
        rel = [0.0, 0.1, 0.2, 0.3] + [0.3] * 16
        ct = [i * 60 for i in range(20)]
        samples, events = self._purge(rel, ct, mm)
        verdict, _ = fpc.analyze_purge(samples, events, 5.0, 12.5)
        self.assertEqual(verdict, "PASS")

    def test_long_zero_feed_dwell_passes(self):
        # benign idle sit: feed 0 for 5 s -> PASS despite long dwell
        mm = [280, 200, 120, 40] + [0] * 40
        rel = [0.0, 0.1, 0.2, 0.3] + [0.3] * 40
        ct = [i * 110 for i in range(44)]  # dwell climbs past 4 s
        samples, events = self._purge(rel, ct, mm)
        verdict, _ = fpc.analyze_purge(samples, events, 5.0, 12.5)
        self.assertEqual(verdict, "PASS")

    def test_sustained_feed_fails(self):
        # old behavior: keeps feeding ~120 sps into a full buffer, overfill grows
        mm = [120] * 20
        rel = [i * 0.5 for i in range(20)]
        ct = [i * 250 for i in range(20)]
        samples, events = self._purge(rel, ct, mm)
        verdict, _ = fpc.analyze_purge(samples, events, 5.0, 12.5)
        self.assertEqual(verdict, "FAIL")

    def test_brief_entry_sample_passes(self):
        # Fast-stop makes COMPRESSION so brief only the entry transient is
        # sampled (1 sample, feed still high mid-ramp, overfill 0). Must NOT
        # false-fail: a 1-sample run has no steady portion to judge.
        samples, events = self._purge([0.0], [14], [1361])
        verdict, _ = fpc.analyze_purge(samples, events, 5.0, 12.5)
        self.assertEqual(verdict, "PASS")

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
