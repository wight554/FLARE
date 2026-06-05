#!/usr/bin/env python3
"""Tests for flare_sync_check.py — synthetic traces with known verdicts.

Covers the compression-overfeed-stop criteria (purge/regression) plus the
sync-relief-rearm-hardening analyzers: rearm (D1) and estimator (D2).
"""
import os
import tempfile
import unittest

import flare_sync_check as fpc


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

    def test_daemon_raw_status_converts_to_ok_line(self):
        line = fpc.daemon_status_to_line({
            "raw_status": {
                "LN": "1",
                "BUF": "NEUTRAL",
                "BP": "0.00",
                "MM": "0.1",
                "EST": "0.1",
                "SYNC_RELIEVE_MM": "0",
            }
        })
        fields = fpc.parse_status_line(line)
        self.assertEqual(fields["BUF"], "NEUTRAL")
        self.assertEqual(fields["SYNC_RELIEVE_MM"], "0")

    def test_daemon_fallback_status_converts_to_ok_line(self):
        line = fpc.daemon_status_to_line({
            "active_lane": 1,
            "buf_state": "COMPRESSION",
            "g_buf_pos": -5.0,
            "sync_enabled": 1,
            "sps": 0.0,
            "extruder_est_sps": 120.0,
        })
        fields = fpc.parse_status_line(line)
        self.assertEqual(fields["BUF"], "COMPRESSION")
        self.assertEqual(fields["SM"], "1")

    def test_daemon_event_converts_to_ev_line(self):
        line = fpc.daemon_event_to_line({
            "type": "SYNC",
            "data": "RELIEF_PAUSE",
        })
        self.assertEqual(line, "EV:SYNC,RELIEF_PAUSE")

    def test_daemon_event_key_matches_history_identity(self):
        key = fpc.daemon_event_key({
            "time": 123.0,
            "type": "SYNC",
            "data": "AUTO_START",
        })
        self.assertEqual(key, (123.0, "SYNC", "AUTO_START"))

    def test_write_capture_log_round_trips_lines(self):
        fd, path = tempfile.mkstemp()
        os.close(fd)
        try:
            fpc.write_capture_log(path, ["OK:LN:1,BUF:NEUTRAL", "EV:SYNC,AUTO_START"])
            with open(path) as fh:
                self.assertEqual(
                    fh.read(),
                    "OK:LN:1,BUF:NEUTRAL\nEV:SYNC,AUTO_START\n",
                )
        finally:
            os.unlink(path)


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

    def test_zero_feed_idle_relief_passes(self):
        # M6-style idle relief: feed is stopped, but relief accounting grows
        # while compression settles. This is not forward overfeed.
        mm = [280, 200, 120, 40] + [0] * 40
        rel = [0.0, 1.0, 2.0, 3.0] + [6.0] * 40
        ct = [i * 110 for i in range(44)]
        samples, events = self._purge(rel, ct, mm)
        verdict, rep = fpc.analyze_purge(samples, events, 5.0, 12.5)
        self.assertEqual(verdict, "PASS")
        self.assertTrue(any("OK(idle-relief)" in line for line in rep))

    def test_zero_feed_hardwall_fails(self):
        mm = [280, 200, 120, 40] + [0] * 40
        rel = [0.0, 1.0, 2.0, 3.0] + [6.0] * 40
        ct = [i * 110 for i in range(44)]
        samples, events = self._purge(rel, ct, mm)
        samples[-1].fields["BP"] = "-12.60"
        verdict, _ = fpc.analyze_purge(samples, events, 5.0, 12.5)
        self.assertEqual(verdict, "FAIL")

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


class RearmTests(unittest.TestCase):
    def test_idle_no_rearm_passes(self):
        # M1: pause then stay idle — RELIEF_PAUSE, no AUTO_START.
        lines = [status("COMPRESSION", -5.0, 0, 0),
                 "EV:SYNC:RELIEF_PAUSE",
                 status("NEUTRAL", 0.0, 0, 0)]
        samples, events = fpc.parse_stream(lines)
        verdict, _ = fpc.analyze_rearm(samples, events, idle=True)
        self.assertEqual(verdict, "PASS")

    def test_idle_spurious_rearm_fails(self):
        lines = [status("COMPRESSION", -5.0, 0, 0),
                 "EV:SYNC:RELIEF_PAUSE",
                 "EV:SYNC:AUTO_START",
                 status("NEUTRAL", 0.0, 0, 0)]
        samples, events = fpc.parse_stream(lines)
        verdict, _ = fpc.analyze_rearm(samples, events, idle=True)
        self.assertEqual(verdict, "FAIL")

    def test_resume_rearm_on_neutral_passes(self):
        # M3: pause then high-flow resume re-arms on the drain to NEUTRAL.
        lines = [status("COMPRESSION", -5.0, 0, 0),
                 "EV:SYNC:RELIEF_PAUSE",
                 "EV:SYNC:AUTO_START",
                 status("NEUTRAL", 0.0, 600, 700)]
        samples, events = fpc.parse_stream(lines)
        verdict, rep = fpc.analyze_rearm(samples, events, idle=False)
        self.assertEqual(verdict, "PASS")
        self.assertTrue(any("NEUTRAL" in line for line in rep))

    def test_resume_stuck_pause_fails(self):
        lines = [status("COMPRESSION", -5.0, 0, 0),
                 "EV:SYNC:RELIEF_PAUSE",
                 status("COMPRESSION", -5.0, 0, 0)]
        samples, events = fpc.parse_stream(lines)
        verdict, _ = fpc.analyze_rearm(samples, events, idle=False)
        self.assertEqual(verdict, "FAIL")

    def test_resume_terminal_idle_relief_fails_by_default(self):
        lines = [status("COMPRESSION", -5.0, 0, 0),
                 "EV:SYNC:RELIEF_PAUSE",
                 "EV:SYNC:AUTO_START",
                 status("NEUTRAL", 0.0, 600, 700),
                 status("COMPRESSION", -5.0, 0, 0, sm=0),
                 "EV:SYNC:RELIEF_PAUSE",
                 status("COMPRESSION", -5.0, 0, 0, sm=0),
                 status("NEUTRAL", 0.0, 0, 0, sm=0),
                 status("NEUTRAL", 0.0, 0, 0, sm=0)]
        samples, events = fpc.parse_stream(lines)
        verdict, _ = fpc.analyze_rearm(samples, events, idle=False)
        self.assertEqual(verdict, "FAIL")

    def test_resume_terminal_idle_relief_can_be_ignored(self):
        lines = [status("COMPRESSION", -5.0, 0, 0),
                 "EV:SYNC:RELIEF_PAUSE",
                 "EV:SYNC:AUTO_START",
                 status("NEUTRAL", 0.0, 600, 700),
                 status("COMPRESSION", -5.0, 0, 0, sm=0),
                 "EV:SYNC:RELIEF_PAUSE",
                 status("COMPRESSION", -5.0, 0, 0, sm=0),
                 status("NEUTRAL", 0.0, 0, 0, sm=0),
                 status("NEUTRAL", 0.0, 0, 0, sm=0)]
        samples, events = fpc.parse_stream(lines)
        verdict, rep = fpc.analyze_rearm(
            samples, events, idle=False, allow_terminal_idle_relief=True)
        self.assertEqual(verdict, "PASS")
        self.assertTrue(any("ignored terminal idle" in line for line in rep))

    def test_resume_cannot_refill_fails(self):
        lines = [status("COMPRESSION", -5.0, 0, 0),
                 "EV:SYNC:RELIEF_PAUSE",
                 "EV:SYNC:AUTO_START",
                 "EV:SYNC:cannot_refill",
                 status("TENSION", 5.0, 600, 700)]
        samples, events = fpc.parse_stream(lines)
        verdict, _ = fpc.analyze_rearm(samples, events, idle=False)
        self.assertEqual(verdict, "FAIL")

    def test_no_relief_inconclusive(self):
        lines = [status("NEUTRAL", 0.0, 600, 700) for _ in range(4)]
        samples, events = fpc.parse_stream(lines)
        verdict, _ = fpc.analyze_rearm(samples, events, idle=False)
        self.assertEqual(verdict, "INCONCLUSIVE")


class EstimatorTests(unittest.TestCase):
    def test_spike_fails(self):
        # EST jumps 200 -> 1500 mm/min on TENSION->COMPRESSION = 7.5x.
        lines = [status("TENSION", 5.0, 200, 1500) for _ in range(2)]
        lines += [status("COMPRESSION", -5.0, 1500, 0) for _ in range(3)]
        samples, events = fpc.parse_stream(lines)
        verdict, _ = fpc.analyze_estimator(samples, events, factor=2.0,
                                           window=5, est_floor=100.0)
        self.assertEqual(verdict, "FAIL")

    def test_blended_passes(self):
        # Blended update: EST 300 -> 360 = 1.2x, under the 2x cap.
        lines = [status("TENSION", 5.0, 300, 1500) for _ in range(2)]
        lines += [status("COMPRESSION", -5.0, 360, 0) for _ in range(3)]
        samples, events = fpc.parse_stream(lines)
        verdict, _ = fpc.analyze_estimator(samples, events, factor=2.0,
                                           window=5, est_floor=100.0)
        self.assertEqual(verdict, "PASS")

    def test_neutral_gap_transition_passes(self):
        # Real captures often sample TENSION->NEUTRAL->COMPRESSION, not a direct
        # adjacent edge.
        lines = [status("TENSION", 5.0, 300, 1500) for _ in range(2)]
        lines += [status("NEUTRAL", 0.0, 320, 700) for _ in range(4)]
        lines += [status("COMPRESSION", -5.0, 360, 0) for _ in range(3)]
        samples, events = fpc.parse_stream(lines)
        verdict, _ = fpc.analyze_estimator(samples, events, factor=2.0,
                                           window=5, est_floor=100.0)
        self.assertEqual(verdict, "PASS")

    def test_neutral_gap_spike_fails(self):
        lines = [status("TENSION", 5.0, 200, 1500) for _ in range(2)]
        lines += [status("NEUTRAL", 0.0, 220, 700) for _ in range(4)]
        lines += [status("COMPRESSION", -5.0, 1500, 0) for _ in range(3)]
        samples, events = fpc.parse_stream(lines)
        verdict, _ = fpc.analyze_estimator(samples, events, factor=2.0,
                                           window=5, est_floor=100.0)
        self.assertEqual(verdict, "FAIL")

    def test_neutral_gap_too_long_inconclusive(self):
        lines = [status("TENSION", 5.0, 300, 1500) for _ in range(2)]
        lines += [status("NEUTRAL", 0.0, 320, 700) for _ in range(6)]
        lines += [status("COMPRESSION", -5.0, 360, 0) for _ in range(3)]
        samples, events = fpc.parse_stream(lines)
        verdict, _ = fpc.analyze_estimator(
            samples, events, factor=2.0, window=5, est_floor=100.0,
            transition_gap=3)
        self.assertEqual(verdict, "INCONCLUSIVE")

    def test_no_transition_inconclusive(self):
        lines = [status("NEUTRAL", 0.0, 300, 700) for _ in range(4)]
        samples, events = fpc.parse_stream(lines)
        verdict, _ = fpc.analyze_estimator(samples, events, factor=2.0,
                                           window=5, est_floor=100.0)
        self.assertEqual(verdict, "INCONCLUSIVE")


class AsymmetricTests(unittest.TestCase):
    def test_zero_tension_passes_and_reports_metrics(self):
        lines = [
            status("NEUTRAL", 1.0, 600, 590),
            status("NEUTRAL", 1.5, 620, 600),
            status("COMPRESSION", 5.0, 620, 0),
            status("COMPRESSION", 5.1, 620, 10),
            status("NEUTRAL", 2.0, 620, 615),
            status("COMPRESSION", 5.0, 620, 0),
        ]
        samples, events = fpc.parse_stream(lines)
        verdict, rep = fpc.analyze_asymmetric(
            samples, events, poll_ms=100, branch_label="partial-drain")
        self.assertEqual(verdict, "PASS")
        self.assertTrue(any("branch: partial-drain" in line for line in rep))
        self.assertTrue(any("TENSION touches: 0" in line for line in rep))
        self.assertTrue(any("COMPRESSION pin: total 300 ms" in line for line in rep))
        self.assertTrue(any("NEUTRAL mean(EST-MM): +11.7" in line for line in rep))
        self.assertTrue(any("relay touch period: mean 300 ms" in line for line in rep))

    def test_sampled_tension_fails(self):
        lines = [
            status("NEUTRAL", 1.0, 600, 590),
            status("TENSION", -5.0, 600, 900),
            status("NEUTRAL", 0.0, 600, 610),
        ]
        samples, events = fpc.parse_stream(lines)
        verdict, rep = fpc.analyze_asymmetric(samples, events, poll_ms=100)
        self.assertEqual(verdict, "FAIL")
        self.assertTrue(any("TENSION touches: 1" in line for line in rep))
        self.assertTrue(any("do not tune sync_kp_rate" in line for line in rep))

    def test_event_tension_fails_even_if_poll_misses_it(self):
        lines = [
            status("NEUTRAL", 1.0, 600, 590),
            "EV:BS:TENSION,0.0,-5.0",
            status("NEUTRAL", 0.0, 600, 610),
        ]
        samples, events = fpc.parse_stream(lines)
        verdict, rep = fpc.analyze_asymmetric(samples, events, poll_ms=100)
        self.assertEqual(verdict, "FAIL")
        self.assertTrue(any("0 sampled, 1 event" in line for line in rep))

    def test_empty_capture_inconclusive(self):
        verdict, _ = fpc.analyze_asymmetric([], [], poll_ms=100)
        self.assertEqual(verdict, "INCONCLUSIVE")


if __name__ == "__main__":
    unittest.main()
