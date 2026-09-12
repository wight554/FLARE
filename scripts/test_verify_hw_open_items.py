#!/usr/bin/env python3
"""Unit tests for verify_hw_open_items.py's pure classifier helpers.

These operate on synthetic event lists shaped exactly like flare_daemon's
/status "events" field ({"time","type","data"}), so they run without any
real daemon or hardware.
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import verify_hw_open_items as v  # noqa: E402


def ev(t, evt_type, data=""):
    return {"time": t, "type": evt_type, "data": data}


class SingleOccurrenceTests(unittest.TestCase):
    def test_pending_when_absent(self):
        verdict, _ = v.classify_single_occurrence([], "BL:TIMEOUT", 0.0, burst_window_s=30)
        self.assertEqual(verdict, "pending")

    def test_pass_on_one_clean_hit(self):
        events = [ev(5.0, "BL:TIMEOUT")]
        verdict, evidence = v.classify_single_occurrence(events, "BL:TIMEOUT", 0.0, burst_window_s=30)
        self.assertEqual(verdict, "pass")
        self.assertEqual(evidence["time"], 5.0)

    def test_fail_on_storm(self):
        events = [ev(5.0, "TMC:FAULT", "2"), ev(5.5, "TMC:FAULT", "2")]
        verdict, _ = v.classify_single_occurrence(events, "TMC:FAULT", 0.0, burst_window_s=2, data_contains="2")
        self.assertEqual(verdict, "fail")

    def test_pass_when_repeats_are_well_spaced(self):
        events = [ev(5.0, "BL:TIMEOUT"), ev(50.0, "BL:TIMEOUT")]
        verdict, _ = v.classify_single_occurrence(events, "BL:TIMEOUT", 0.0, burst_window_s=30)
        self.assertEqual(verdict, "pass")

    def test_data_filter_ignores_other_lanes(self):
        events = [ev(5.0, "TMC:FAULT", "1")]
        verdict, _ = v.classify_single_occurrence(events, "TMC:FAULT", 0.0, burst_window_s=2, data_contains="2")
        self.assertEqual(verdict, "pending")


class SequenceTests(unittest.TestCase):
    def test_pending_until_both_seen(self):
        events = [ev(1.0, "RUNOUT")]
        verdict, _ = v.classify_sequence(events, ["RUNOUT", "RELOAD:LOADED"], 0.0, max_total_span_s=120)
        self.assertEqual(verdict, "pending")

    def test_pass_in_order_within_span(self):
        events = [ev(1.0, "RUNOUT"), ev(10.0, "RELOAD:LOADED")]
        verdict, matched = v.classify_sequence(events, ["RUNOUT", "RELOAD:LOADED"], 0.0, max_total_span_s=120)
        self.assertEqual(verdict, "pass")
        self.assertEqual(len(matched), 2)

    def test_fail_when_span_exceeded(self):
        events = [ev(1.0, "RUNOUT"), ev(500.0, "RELOAD:LOADED")]
        verdict, _ = v.classify_sequence(events, ["RUNOUT", "RELOAD:LOADED"], 0.0, max_total_span_s=120)
        self.assertEqual(verdict, "fail")

    def test_fail_when_forbidden_event_between(self):
        events = [ev(1.0, "RUNOUT"), ev(5.0, "FAULT:MOVE_COMPRESSION"), ev(10.0, "RELOAD:LOADED")]
        verdict, _ = v.classify_sequence(
            events, ["RUNOUT", "RELOAD:LOADED"], 0.0, max_total_span_s=120,
            forbid_types=["FAULT:MOVE_COMPRESSION"],
        )
        self.assertEqual(verdict, "fail")

    def test_pass_when_forbidden_event_is_before_window(self):
        events = [ev(-5.0, "FAULT:MOVE_COMPRESSION"), ev(1.0, "RUNOUT"), ev(10.0, "RELOAD:LOADED")]
        verdict, _ = v.classify_sequence(
            events, ["RUNOUT", "RELOAD:LOADED"], 0.0, max_total_span_s=120,
            forbid_types=["FAULT:MOVE_COMPRESSION"],
        )
        self.assertEqual(verdict, "pass")


class AbsenceThenTests(unittest.TestCase):
    def test_pass_when_nothing_forbidden_before_then(self):
        events = [ev(5.0, "BUF_STAB:DONE")]
        verdict, _ = v.classify_absence_then(events, ["BL:BREAK", "BL:FOLLOW"], "BUF_STAB:DONE", 0.0, window_s=30)
        self.assertEqual(verdict, "pass")

    def test_fail_when_forbidden_seen_before_then(self):
        events = [ev(2.0, "BL:BREAK"), ev(5.0, "BUF_STAB:DONE")]
        verdict, evidence = v.classify_absence_then(
            events, ["BL:BREAK", "BL:FOLLOW"], "BUF_STAB:DONE", 0.0, window_s=30)
        self.assertEqual(verdict, "fail")
        self.assertEqual(evidence["type"], "BL:BREAK")

    def test_pending_before_then_type_seen(self):
        events = [ev(1.0, "BL:PRIME")]
        verdict, _ = v.classify_absence_then(events, ["BL:BREAK", "BL:FOLLOW"], "BUF_STAB:DONE", 0.0, window_s=30)
        self.assertEqual(verdict, "pending")


class ReportSmokeTest(unittest.TestCase):
    def test_print_report_does_not_raise_with_empty_state(self):
        # Every WATCH_ITEMS entry defaults to "pending"; print_report must
        # render that without raising, even with no flowguard samples.
        state = dict.fromkeys(v.WATCH_ITEMS, "pending")
        v.print_report(state, [])


if __name__ == "__main__":
    unittest.main()
