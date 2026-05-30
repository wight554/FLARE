#!/usr/bin/env python3
"""Unit tests for flare_hil_harness.py — pure parsing / matching logic.

No hardware or daemon: events are injected directly via HilBoard._ingest, so
these run in CI. The interactive flow tests in flare_hil.py are operator-run.
"""
import threading
import time
import unittest

from flare_hil_harness import HilBoard, parse_event, event_from_sse


class ParseEventTests(unittest.TestCase):
    def test_event_lines(self):
        self.assertEqual(parse_event("EV:UNLOAD_BLOCKED"), "UNLOAD_BLOCKED")
        self.assertEqual(parse_event("EV:SYNC:RELIEF_PAUSE"), "SYNC:RELIEF_PAUSE")
        self.assertEqual(parse_event("  EV:BL:LOCKED  "), "BL:LOCKED")

    def test_non_events(self):
        self.assertIsNone(parse_event("OK:LN:2,BUF:NEUTRAL"))
        self.assertIsNone(parse_event(""))
        self.assertIsNone(parse_event(None))


class SseEventTests(unittest.TestCase):
    def test_event_dicts(self):
        self.assertEqual(event_from_sse({"event_type": "UNLOAD_BLOCKED", "event_data": ""}),
                         "UNLOAD_BLOCKED")
        self.assertEqual(event_from_sse({"event_type": "SYNC", "event_data": "RELIEF_PAUSE"}),
                         "SYNC:RELIEF_PAUSE")
        self.assertEqual(event_from_sse({"event_type": "BUF_STAB:DONE", "event_data": ""}),
                         "BUF_STAB:DONE")

    def test_non_event_frames(self):
        self.assertIsNone(event_from_sse({"buf": "NEUTRAL", "bp": 0.3}))
        self.assertIsNone(event_from_sse({}))
        self.assertIsNone(event_from_sse(None))
        self.assertIsNone(event_from_sse("not a dict"))


class WaitRefuteTests(unittest.TestCase):
    def board(self):
        return HilBoard(verbose=False)

    def test_wait_hit_and_substring(self):
        b = self.board()
        b._ingest({"event_type": "SYNC", "event_data": "RELIEF_PAUSE"})
        self.assertIsNotNone(b.wait_event("SYNC:RELIEF_PAUSE", timeout=0.3))
        self.assertIsNotNone(b.wait_event("RELIEF_PAUSE", timeout=0.3))  # substring
        ev = b.wait_event("SYNC", timeout=0.3)
        self.assertEqual(ev.data, "RELIEF_PAUSE")

    def test_wait_timeout(self):
        b = self.board()
        self.assertIsNone(b.wait_event("NOPE", timeout=0.1))

    def test_regex(self):
        b = self.board()
        b._ingest({"event_type": "BL:LOCKED", "event_data": ""})
        self.assertIsNotNone(b.wait_event(r"BL:(LOCKED|PRIME)", timeout=0.3, regex=True))

    def test_since_filters_stale(self):
        b = self.board()
        b._ingest({"event_type": "OLD", "event_data": ""})
        t0 = time.monotonic()
        self.assertIsNone(b.wait_event("OLD", timeout=0.1, since=t0))

    def test_clear_events(self):
        b = self.board()
        b._ingest({"event_type": "X", "event_data": ""})
        self.assertEqual(len(b.events()), 1)
        b.clear_events()
        self.assertEqual(len(b.events()), 0)

    def test_refute_pass_when_absent(self):
        b = self.board()
        self.assertIsNone(b.refute_event("NOPE", window=0.1))
        b.refute("NOPE", window=0.1)  # should not raise

    def test_refute_fail_when_present(self):
        b = self.board()
        threading.Timer(0.05, lambda: b._ingest({"event_type": "BLOCKED", "event_data": ""})).start()
        self.assertIsNotNone(b.refute_event("BLOCKED", window=0.5))

    def test_expect_raises_on_timeout(self):
        b = self.board()
        with self.assertRaises(AssertionError):
            b.expect("NOPE", timeout=0.1)

    def test_refute_raises_when_present(self):
        b = self.board()
        threading.Timer(0.05, lambda: b._ingest({"event_type": "BLOCKED", "event_data": ""})).start()
        with self.assertRaises(AssertionError):
            b.refute("BLOCKED", window=0.5)


class HelperTests(unittest.TestCase):
    def test_in_range(self):
        self.assertTrue(HilBoard._in_range(0.95, 0.9, None))     # >= lo
        self.assertTrue(HilBoard._in_range(-0.95, None, -0.9))   # <= hi
        self.assertTrue(HilBoard._in_range(0.3, None, None))     # no bounds
        self.assertFalse(HilBoard._in_range(0.3, 0.9, None))
        self.assertFalse(HilBoard._in_range(-0.3, None, -0.9))
        self.assertTrue(HilBoard._in_range(-0.4, -0.5, -0.3))    # within window

    def test_target_str(self):
        self.assertEqual(HilBoard._target_str(0.9, None), "(want >= +0.90)")
        self.assertEqual(HilBoard._target_str(None, -0.9), "(want <= -0.90)")
        self.assertEqual(HilBoard._target_str(-0.5, -0.3), "(want -0.50..-0.30)")
        self.assertEqual(HilBoard._target_str(None, None), "")

    def test_muted_echo_still_captures(self):
        b = HilBoard(verbose=True)
        b._mute_echo = True
        b._ingest({"event_type": "UNLOAD_BLOCKED", "event_data": ""})
        self.assertEqual(len(b.events()), 1)     # captured even with echo muted


if __name__ == "__main__":
    unittest.main()
