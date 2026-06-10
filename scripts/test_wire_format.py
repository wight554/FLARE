#!/usr/bin/env python3
"""Wire-format unit test validating literal firmware lines against patterns/logic."""
import os
import sys
import unittest

# Ensure scripts path is in import path
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from flare_live_tuner import EVENT_RE


def daemon_split_logic(line):
    if not line.startswith("EV:"):
        return None, None
    evt_body = line[3:]
    parts = evt_body.split(":")
    if len(parts) > 1:
        if parts[0] in ("TC", "CUT", "FAULT", "BL", "BUF_STAB", "SYNC", "RELOAD", "UNLOAD") and len(parts) >= 2:
            evt_type = f"{parts[0]}:{parts[1]}"
            evt_data = ":".join(parts[2:])
        else:
            evt_type = parts[0]
            evt_data = ":".join(parts[1:])
    else:
        evt_type = evt_body
        evt_data = ""
    return evt_type, evt_data


class TestWireFormat(unittest.TestCase):
    def test_tuner_patterns(self):
        # EV:SYNC:FAULT_HOLD
        m1 = EVENT_RE.match("EV:SYNC:FAULT_HOLD")
        self.assertIsNotNone(m1)
        self.assertEqual(m1.group(1), "SYNC")
        self.assertEqual(m1.group(2), "FAULT_HOLD")

        # EV:SYNC:TENSION_RISK_HIGH
        m2 = EVENT_RE.match("EV:SYNC:TENSION_RISK_HIGH")
        self.assertIsNotNone(m2)
        self.assertEqual(m2.group(1), "SYNC")
        self.assertEqual(m2.group(2), "TENSION_RISK_HIGH")

        # EV:BUF:EST_FALLBACK
        m3 = EVENT_RE.match("EV:BUF:EST_FALLBACK")
        self.assertIsNotNone(m3)
        self.assertEqual(m3.group(1), "BUF")
        self.assertEqual(m3.group(2), "EST_FALLBACK")

        # EV:BL:TIMEOUT
        m4 = EVENT_RE.match("EV:BL:TIMEOUT")
        self.assertIsNotNone(m4)
        self.assertEqual(m4.group(1), "BL")
        self.assertEqual(m4.group(2), "TIMEOUT")

        # EV:RELOAD:LOADED:1
        m5 = EVENT_RE.match("EV:RELOAD:LOADED:1")
        self.assertIsNotNone(m5)
        self.assertEqual(m5.group(1), "RELOAD")
        self.assertEqual(m5.group(2), "LOADED")

        # EV:UNLOAD:FAULT:CUT_FAILED
        m6 = EVENT_RE.match("EV:UNLOAD:FAULT:CUT_FAILED")
        self.assertIsNotNone(m6)
        self.assertEqual(m6.group(1), "UNLOAD")
        self.assertEqual(m6.group(2), "FAULT")

    def test_daemon_split_logic(self):
        # EV:SYNC:FAULT_HOLD
        t, d = daemon_split_logic("EV:SYNC:FAULT_HOLD")
        self.assertEqual(t, "SYNC:FAULT_HOLD")
        self.assertEqual(d, "")

        # EV:SYNC:TENSION_RISK_HIGH
        t, d = daemon_split_logic("EV:SYNC:TENSION_RISK_HIGH")
        self.assertEqual(t, "SYNC:TENSION_RISK_HIGH")
        self.assertEqual(d, "")

        # EV:BUF:EST_FALLBACK
        t, d = daemon_split_logic("EV:BUF:EST_FALLBACK")
        self.assertEqual(t, "BUF")
        self.assertEqual(d, "EST_FALLBACK")

        # EV:BL:TIMEOUT
        t, d = daemon_split_logic("EV:BL:TIMEOUT")
        self.assertEqual(t, "BL:TIMEOUT")
        self.assertEqual(d, "")

        # EV:RELOAD:LOADED:1
        t, d = daemon_split_logic("EV:RELOAD:LOADED:1")
        self.assertEqual(t, "RELOAD:LOADED")
        self.assertEqual(d, "1")

        # EV:UNLOAD:FAULT:CUT_FAILED
        t, d = daemon_split_logic("EV:UNLOAD:FAULT:CUT_FAILED")
        self.assertEqual(t, "UNLOAD:FAULT")
        self.assertEqual(d, "CUT_FAILED")

        # EV:UNLOAD_TIMEOUT:1
        t, d = daemon_split_logic("EV:UNLOAD_TIMEOUT:1")
        self.assertEqual(t, "UNLOAD_TIMEOUT")
        self.assertEqual(d, "1")


if __name__ == "__main__":
    unittest.main()
