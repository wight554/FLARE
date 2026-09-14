#!/usr/bin/env python3
"""Unit tests for Moonraker lane_data namespace synchronization.

Validates:
  - Payload formatting matching OrcaSlicer expectation
  - Metadata enrichment from Spoolman
  - Fallback to gate map defaults when Spoolman is unavailable
  - Orphan key cleanup for lanes >= NUM_GATES
  - Network failure resilience and non-blocking trigger queue
"""
import json
import os
import sys
import tempfile
import threading
import unittest
import urllib.parse
from http.server import BaseHTTPRequestHandler, HTTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import flare_daemon as fd  # noqa: E402


class FakeMoonrakerHandler(BaseHTTPRequestHandler):
    posted_items = []
    deleted_items = []
    existing_items = {}

    def log_message(self, format, *args):
        pass  # Suppress test noise

    def do_POST(self):
        if self.path == "/server/database/item":
            length = int(self.headers.get("Content-Length", 0))
            body = json.loads(self.rfile.read(length).decode("utf-8"))
            self.__class__.posted_items.append(body)
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps({"result": body}).encode("utf-8"))
        else:
            self.send_response(404)
            self.end_headers()

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path == "/server/database/item":
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps({
                "result": {"namespace": "lane_data", "value": self.__class__.existing_items}
            }).encode("utf-8"))
        else:
            self.send_response(404)
            self.end_headers()

    def do_DELETE(self):
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path == "/server/database/item":
            query = urllib.parse.parse_qs(parsed.query)
            key = query.get("key", [None])[0]
            if key:
                self.__class__.deleted_items.append(key)
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps({"result": "ok"}).encode("utf-8"))
        else:
            self.send_response(404)
            self.end_headers()


class TestMoonrakerLaneData(unittest.TestCase):
    def setUp(self):
        self.tmp_dir = tempfile.TemporaryDirectory()
        fd._DB_PATH = os.path.join(self.tmp_dir.name, "test_flare.db")
        fd.db_init()
        FakeMoonrakerHandler.posted_items = []
        FakeMoonrakerHandler.deleted_items = []
        FakeMoonrakerHandler.existing_items = {}

    def tearDown(self):
        self.tmp_dir.cleanup()

    def test_build_moonraker_lane_payload_fallback(self):
        """Test default payload generation when no Spoolman spool is assigned."""
        fd._spool_cache.clear()
        payload = fd.build_moonraker_lane_payload(0)
        self.assertIsNotNone(payload)
        self.assertEqual(payload["lane"], 0)
        self.assertEqual(payload["name"], "Gate 0")
        self.assertEqual(payload["vendor_name"], "")
        self.assertEqual(payload["bed_temp"], 0)
        self.assertEqual(payload["nozzle_temp"], 0)
        self.assertEqual(payload["filament_id"], -1)
        self.assertIn("scan_time", payload)

    def test_build_moonraker_lane_payload_with_spoolman(self):
        """Test payload enrichment when Spoolman metadata is available in cache."""
        import time
        now = time.time()
        mock_spool = {
            "id": 42,
            "name": "Prusament Galaxy Black",
            "material": "PLA",
            "color_hex": "000000",
            "vendor_name": "Prusa Research",
            "bed_temp": 60,
            "nozzle_temp": 215,
            "filament_id": 101,
            "td": 1.25,
        }
        with fd._spool_cache_lock:
            fd._spool_cache[42] = (now, mock_spool)

        # Set gate 0 to spool_id 42
        fd._write_gate_map_db(0, {"spool_id": 42})

        payload = fd.build_moonraker_lane_payload(0)
        self.assertIsNotNone(payload)
        self.assertEqual(payload["lane"], 0)
        self.assertEqual(payload["spool_id"], 42)
        self.assertEqual(payload["vendor_name"], "Prusa Research")
        self.assertEqual(payload["name"], "Prusament Galaxy Black")
        self.assertEqual(payload["material"], "PLA")
        self.assertEqual(payload["color"], "000000")
        self.assertEqual(payload["bed_temp"], 60)
        self.assertEqual(payload["nozzle_temp"], 215)
        self.assertEqual(payload["filament_id"], 101)
        self.assertEqual(payload["td"], 1.25)

    def test_sync_moonraker_lane_data_pushes_and_cleans_orphans(self):
        """Test full sync pushes active gates and deletes orphan keys."""
        server = HTTPServer(("127.0.0.1", 0), FakeMoonrakerHandler)
        port = server.server_address[1]
        server_thread = threading.Thread(target=server.serve_forever, daemon=True)
        server_thread.start()

        orig_mr_url = fd.MOONRAKER_URL
        orig_num_gates = fd.NUM_GATES
        try:
            fd.MOONRAKER_URL = f"http://127.0.0.1:{port}"
            fd.NUM_GATES = 2

            # Seed existing items in fake Moonraker DB including orphan lane2
            FakeMoonrakerHandler.existing_items = {
                "lane0": {"name": "Old 0"},
                "lane1": {"name": "Old 1"},
                "lane2": {"name": "Stale 2"},
            }

            fd.sync_moonraker_lane_data()

            # Verify POST called for lane0 and lane1
            posted_keys = [item["key"] for item in FakeMoonrakerHandler.posted_items]
            self.assertIn("lane0", posted_keys)
            self.assertIn("lane1", posted_keys)
            self.assertNotIn("lane2", posted_keys)

            # Verify DELETE called for orphan lane2
            self.assertIn("lane2", FakeMoonrakerHandler.deleted_items)
            self.assertNotIn("lane0", FakeMoonrakerHandler.deleted_items)
            self.assertNotIn("lane1", FakeMoonrakerHandler.deleted_items)

        finally:
            fd.MOONRAKER_URL = orig_mr_url
            fd.NUM_GATES = orig_num_gates
            server.shutdown()
            server.server_close()

    def test_sync_reports_success_and_failure(self):
        """sync_moonraker_lane_data returns True only when every POST lands.

        Rig 2026-09-14 (E1): the boot-time sync's POSTs failed once (Moonraker
        not ready), the return was never checked, and nothing re-triggered ->
        `lane_data` stayed 404 for the daemon's whole life. The worker now
        keys its retry off this return, so it must be truthful."""
        orig_mr_url = fd.MOONRAKER_URL
        orig_num_gates = fd.NUM_GATES
        try:
            fd.NUM_GATES = 2
            # Unreachable Moonraker: every POST fails -> False.
            fd.MOONRAKER_URL = "http://127.0.0.1:59999"
            self.assertFalse(fd.sync_moonraker_lane_data(),
                             "sync must report failure when POSTs cannot land")

            # Reachable Moonraker: every POST succeeds -> True.
            server = HTTPServer(("127.0.0.1", 0), FakeMoonrakerHandler)
            port = server.server_address[1]
            threading.Thread(target=server.serve_forever, daemon=True).start()
            FakeMoonrakerHandler.existing_items = {}
            FakeMoonrakerHandler.posted_items = []
            try:
                fd.MOONRAKER_URL = f"http://127.0.0.1:{port}"
                self.assertTrue(fd.sync_moonraker_lane_data(),
                                "sync must report success when every POST lands")
            finally:
                server.shutdown()
                server.server_close()
        finally:
            fd.MOONRAKER_URL = orig_mr_url
            fd.NUM_GATES = orig_num_gates

    def test_sync_moonraker_network_error_resilience(self):
        """Test that offline Moonraker endpoint does not throw or crash sync."""
        orig_mr_url = fd.MOONRAKER_URL
        try:
            # Use an unreachable port
            fd.MOONRAKER_URL = "http://127.0.0.1:59999"
            # Should catch exceptions internally and complete safely
            fd.sync_moonraker_lane_data()
        finally:
            fd.MOONRAKER_URL = orig_mr_url

    def test_trigger_moonraker_lane_data_sync_queues(self):
        """Test trigger_moonraker_lane_data_sync adds item to queue."""
        while not fd._lane_sync_queue.empty():
            fd._lane_sync_queue.get_nowait()

        fd.trigger_moonraker_lane_data_sync()
        self.assertFalse(fd._lane_sync_queue.empty())
        item = fd._lane_sync_queue.get_nowait()
        self.assertTrue(item)

    def test_sync_does_not_requeue_itself_on_spool_cache_miss(self):
        """Regression: the sync worker builds a payload per gate, and each
        build does a Spoolman lookup. A cache miss inside that lookup used to
        queue ANOTHER sync, so every sync re-entered itself (bounded only by
        SPOOL_CACHE_TTL). Now the sync path opts out of the notify, while an
        ordinary cold lookup — e.g. from the status endpoint — still queues
        one so fresh spool metadata reaches Moonraker."""
        server = HTTPServer(("127.0.0.1", 0), FakeMoonrakerHandler)
        port = server.server_address[1]
        threading.Thread(target=server.serve_forever, daemon=True).start()

        orig_mr_url = fd.MOONRAKER_URL
        orig_num_gates = fd.NUM_GATES
        orig_fetch = fd._spoolman_fetch_spool
        fetches = []
        try:
            fd.MOONRAKER_URL = f"http://127.0.0.1:{port}"
            fd.NUM_GATES = 1
            FakeMoonrakerHandler.existing_items = {}
            FakeMoonrakerHandler.posted_items = []

            # Cold cache + a fetch stub that records every miss.
            with fd._spool_cache_lock:
                fd._spool_cache.clear()
            fd._spoolman_fetch_spool = lambda sid: (fetches.append(sid) or {"id": sid, "name": "S"})
            fd._write_gate_map_db(0, {"spool_id": 7})  # queues one sync itself

            while not fd._lane_sync_queue.empty():
                fd._lane_sync_queue.get_nowait()

            fd.sync_moonraker_lane_data()

            self.assertEqual(fetches, [7], "sync should have fetched the cold spool once")
            self.assertTrue(fd._lane_sync_queue.empty(),
                            "sync_moonraker_lane_data must not queue another sync")

            # The non-sync path keeps its notify: a cold lookup queues exactly one.
            with fd._spool_cache_lock:
                fd._spool_cache.clear()
            fd._spoolman_get_spool(7)
            self.assertFalse(fd._lane_sync_queue.empty(),
                             "an ordinary cache miss must still queue a sync")
            fd._lane_sync_queue.get_nowait()
            self.assertTrue(fd._lane_sync_queue.empty())
        finally:
            fd.MOONRAKER_URL = orig_mr_url
            fd.NUM_GATES = orig_num_gates
            fd._spoolman_fetch_spool = orig_fetch
            with fd._spool_cache_lock:
                fd._spool_cache.clear()
            server.shutdown()


if __name__ == "__main__":
    unittest.main()
