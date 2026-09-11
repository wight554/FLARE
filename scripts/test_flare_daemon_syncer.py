#!/usr/bin/env python3
"""klipper_syncer wait scheduling (daemon-klipper-mirror: a push skipped while
Klipper holds the gcode lock must be retried within a tick, not the 10 s reconcile)."""
import os
import sys
import tempfile
import unittest

os.environ.setdefault("FLARE_DATA_DIR", tempfile.mkdtemp(prefix="flare_test_syncer_"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import flare_daemon as fd  # noqa: E402


class SyncerWaitTests(unittest.TestCase):
    def test_idle_waits_for_reconcile(self):
        wait, retry = fd._syncer_wait_time(now=100.0, host_busy=False, next_idle_probe=0.0,
                                           backoff=0.0, last_force_sync=95.0, retry_at=0.0)
        self.assertAlmostEqual(wait, 5.0)
        self.assertEqual(retry, 0.0)

    def test_pending_retry_shortens_reconcile_wait(self):
        wait, retry = fd._syncer_wait_time(now=100.0, host_busy=False, next_idle_probe=0.0,
                                           backoff=0.0, last_force_sync=100.0,
                                           retry_at=100.0 + fd.KLIPPER_PUSH_RETRY_S)
        self.assertAlmostEqual(wait, fd.KLIPPER_PUSH_RETRY_S)
        self.assertGreater(retry, 0.0)

    def test_elapsed_retry_fires_immediately_and_clears(self):
        wait, retry = fd._syncer_wait_time(now=101.0, host_busy=False, next_idle_probe=0.0,
                                           backoff=0.0, last_force_sync=100.0, retry_at=100.5)
        self.assertAlmostEqual(wait, 0.1)
        self.assertEqual(retry, 0.0)

    def test_host_busy_probe_still_bounded_by_retry(self):
        wait, _ = fd._syncer_wait_time(now=100.0, host_busy=True, next_idle_probe=101.5,
                                       backoff=0.0, last_force_sync=100.0, retry_at=100.3)
        self.assertAlmostEqual(wait, 0.3)


if __name__ == "__main__":
    unittest.main()
