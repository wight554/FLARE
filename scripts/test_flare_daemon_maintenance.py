#!/usr/bin/env python3
"""Unit tests for FLARE maintenance counters and command parity.

Tests:
  - SQLite persistence across restarts with default counter auto-seeding
  - Increment hooks for EV:CUT:DONE, TC:DONE, and RELOAD events
  - Threshold warning generation and optional PAUSE escalation
  - Counter reset and deletion
  - klipper/mmu.py cmd_MMU_STATS COUNTER=... handling
"""
import json
import os
import sys
import tempfile
import threading
import types
import unittest
from http.server import BaseHTTPRequestHandler, HTTPServer

# Fake serial module for import safety
sys.modules.setdefault("serial", types.SimpleNamespace())

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import flare_daemon as fd  # noqa: E402

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "klipper"))
import mmu  # noqa: E402


class FakeKlipperServer(BaseHTTPRequestHandler):
    posted_scripts = []

    def log_message(self, format, *args):
        pass

    def do_POST(self):
        if self.path == "/printer/gcode/script":
            length = int(self.headers.get("Content-Length", 0))
            body = json.loads(self.rfile.read(length).decode("utf-8"))
            self.__class__.posted_scripts.append(body.get("script", ""))
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(b'{"result": "ok"}')
        else:
            self.send_response(404)
            self.end_headers()


class FakeGcodeCommand:
    def __init__(self, params=None):
        self.params = params or {}
        self.messages = []

    def get(self, key, default=None):
        return self.params.get(key, default)

    def get_int(self, key, default=None):
        val = self.params.get(key, default)
        return int(val) if val is not None else default

    def get_float(self, key, default=None):
        val = self.params.get(key, default)
        return float(val) if val is not None else default

    def respond_info(self, msg):
        self.messages.append(msg)


class FakePrinter:
    def __init__(self):
        self._objs = {}
        self._gcode = types.SimpleNamespace(
            register_command=lambda *a, **k: None,
            register_mux_command=lambda *a, **k: None,
            run_script_from_command=lambda *a, **k: None,
            respond_info=lambda *a, **k: None,
        )

    def add_object(self, name, obj):
        self._objs[name] = obj

    def register_event_handler(self, *a, **k):
        pass

    def lookup_object(self, name, default=None):
        if name == "gcode":
            return self._gcode
        return self._objs.get(name, default)


class TestFlareDaemonMaintenance(unittest.TestCase):
    def setUp(self):
        self.tmp_dir = tempfile.TemporaryDirectory()
        fd._DB_PATH = os.path.join(self.tmp_dir.name, "test_flare.db")
        fd._maintenance_paused_counters.clear()
        fd.db_init()
        FakeKlipperServer.posted_scripts = []

    def tearDown(self):
        self.tmp_dir.cleanup()

    def test_default_counters_seeded_and_persisted(self):
        """Verify default counters exist on initial db_init and survive reload."""
        counters = fd.get_maintenance_counters()
        self.assertIn("cutter_cuts", counters)
        self.assertIn("swaps", counters)
        self.assertIn("reload_failovers", counters)

        self.assertEqual(counters["cutter_cuts"]["count"], 0)
        self.assertEqual(counters["cutter_cuts"]["limit_val"], 1000)
        self.assertFalse(counters["cutter_cuts"]["pause"])

        # Update counter
        fd.update_maintenance_counter("cutter_cuts", incr=5)
        self.assertEqual(fd.get_maintenance_counters()["cutter_cuts"]["count"], 5)

        # Re-run db_init; data must survive
        fd.db_init()
        self.assertEqual(fd.get_maintenance_counters()["cutter_cuts"]["count"], 5)

    def test_event_hooks_increment_counters(self):
        """Verify board events trigger maintenance increments."""
        initial_cuts = fd.get_maintenance_counters()["cutter_cuts"]["count"]
        initial_swaps = fd.get_maintenance_counters()["swaps"]["count"]
        initial_reload = fd.get_maintenance_counters()["reload_failovers"]["count"]

        fd.record_event_stats("EV:CUT:DONE", "")
        self.assertEqual(fd.get_maintenance_counters()["cutter_cuts"]["count"], initial_cuts + 1)

        fd.record_event_stats("TC:DONE", "LANE=1")
        self.assertEqual(fd.get_maintenance_counters()["swaps"]["count"], initial_swaps + 1)

        fd.record_event_stats("EV:RELOAD:APPROACH", "")
        self.assertEqual(fd.get_maintenance_counters()["reload_failovers"]["count"], initial_reload + 1)

    def test_threshold_limit_warning_and_pause_escalation(self):
        """Verify warning emitted and Klipper PAUSE triggered on limit exceeding."""
        server = HTTPServer(("127.0.0.1", 0), FakeKlipperServer)
        port = server.server_address[1]
        server_thread = threading.Thread(target=server.serve_forever, daemon=True)
        server_thread.start()

        orig_mr = fd.MOONRAKER_URL
        try:
            fd.MOONRAKER_URL = f"http://127.0.0.1:{port}"

            # Configure counter with limit 3 and pause=1
            fd.update_maintenance_counter("blade_test", limit_val=3, warning="Change blade now!", pause=1)

            # Increment to 2: below limit, no pause
            fd.update_maintenance_counter("blade_test", incr=2)
            self.assertEqual(len(FakeKlipperServer.posted_scripts), 0)

            # Increment to 3: reaches limit -> triggers warning & PAUSE
            fd.update_maintenance_counter("blade_test", incr=1)
            self.assertTrue(any("PAUSE" in s for s in FakeKlipperServer.posted_scripts))
            self.assertTrue(any("Change blade now!" in s for s in FakeKlipperServer.posted_scripts))

            # Status cache should list warning
            status = fd.status_cache.get("maintenance", {})
            warnings = status.get("warnings", [])
            self.assertTrue(any(w["counter"] == "blade_test" for w in warnings))

            # Reset clears warning and pause lock
            fd.update_maintenance_counter("blade_test", reset=True)
            self.assertEqual(fd.get_maintenance_counters()["blade_test"]["count"], 0)
            status_after = fd.status_cache.get("maintenance", {})
            self.assertEqual(len(status_after.get("warnings", [])), 0)

        finally:
            fd.MOONRAKER_URL = orig_mr
            server.shutdown()
            server.server_close()

    def test_klipper_mmu_stats_command_parity(self):
        """Verify Klipper mock MMU_STATS command handles COUNTER= syntax."""
        config = types.SimpleNamespace(
            get_printer=lambda: FakePrinter(),
            get_name=lambda: "mmu",
            getint=lambda name, default=0: default,
            getfloat=lambda name, default=0.0: default,
            get=lambda name, default="": default,
            getboolean=lambda name, default=False: default,
        )
        mock = mmu.MMUMock(config)

        # 1. Update counter via command
        gcmd_update = FakeGcodeCommand({
            "COUNTER": "blade_custom",
            "LIMIT": "500",
            "WARNING": "Blade worn",
            "PAUSE": "1",
            "INCR": "10",
        })
        mock.cmd_MMU_STATS(gcmd_update)
        self.assertTrue(any("updated" in m for m in gcmd_update.messages))

        # 2. Query counter via command
        gcmd_query = FakeGcodeCommand({"COUNTER": "blade_custom"})
        mock.cmd_MMU_STATS(gcmd_query)
        self.assertTrue(any("Maintenance Counter: blade_custom" in m for m in gcmd_query.messages))
        self.assertTrue(any("Count:      10" in m for m in gcmd_query.messages))

        # 3. Reset counter via command
        gcmd_reset = FakeGcodeCommand({"COUNTER": "blade_custom", "RESET": "1"})
        mock.cmd_MMU_STATS(gcmd_reset)
        self.assertTrue(any("reset to 0" in m for m in gcmd_reset.messages))

        # 4. Bare MMU_STATS listing counters
        gcmd_bare = FakeGcodeCommand({})
        mock.cmd_MMU_STATS(gcmd_bare)
        self.assertTrue(any("Maintenance Counters" in m for m in gcmd_bare.messages))


if __name__ == "__main__":
    unittest.main()
