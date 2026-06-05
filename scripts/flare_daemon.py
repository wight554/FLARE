#!/usr/bin/env python3
"""
flare_daemon.py — persistent background serial multiplexer, telemetry caching,
and EventSource (SSE) host server for FLARE.

Exposes:
  - GET /status : latest cached telemetry from RP2040 (JSON)
  - POST /cmd  : sends command to board, blocks, returns board response (OK: / ER:)
  - GET /telemetry : Server-Sent Events (SSE) 20Hz real-time telemetry stream
  - GET / : Serves the integrated HTML5/Canvas dashboard UI
"""

import argparse
import glob
import json
import math
import os
import queue
import sqlite3
import sys
import threading
import time
import urllib.request
from http.server import BaseHTTPRequestHandler, HTTPServer
from socketserver import ThreadingMixIn

# Add scripts directory to path to import serial_utils
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
try:
    import serial_utils
except ImportError:
    # Inline fallback if executed in isolated environments
    class serial_utils:
        @staticmethod
        def find_port(pref=None):
            candidates = glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*") + glob.glob("/dev/cu.usbmodem*")
            if pref in candidates:
                return pref
            return candidates[0] if candidates else None

try:
    import serial
except ImportError:
    print("flare_daemon error: 'pyserial' not installed. Run: pip install pyserial", file=sys.stderr)
    sys.exit(1)

# Global runtime state
serial_port = None
serial_lock = threading.Lock()
command_event = threading.Event()
command_reply = None
current_executing_command = None

# Thread-safe status cache
status_lock = threading.Lock()
status_cache = {
    "board_online": False,
    "active_lane": 0,
    "tc_state": "UNKNOWN",
    "g_buf_pos": 0.0,
    "buf_state": "NEUTRAL",
    "sync_feedback": 0.0,
    "sync_feedback_state": "neutral",
    "sync_drive": False,
    "sps": 0.0,
    "baseline_sps": 0.0,
    "extruder_est_sps": 0.0,
    "reserve_error_mm": 0.0,
    "in1": 0, "out1": 0,
    "in2": 0, "out2": 0,
    "toolhead": 0, "y_split": 0,
    "sync_enabled": 0,
    "reload_mode": 0,
    "buf_sensor_type": 0,
    "timestamp": 0.0
}

# Real-time EventSource (SSE) queues
sse_queues_lock = threading.Lock()
active_sse_queues = set()

# Event history
event_history = []
event_history_lock = threading.Lock()

def add_event_to_history(evt_type, evt_data):
    with event_history_lock:
        event_history.append({
            "time": time.time(),
            "type": evt_type,
            "data": evt_data
        })
        if len(event_history) > 100:
            event_history.pop(0)

# MMU usage statistics, counted from board events and persisted across restarts.
# The daemon is the single source of truth; absolute totals are pushed to the
# Klipper mmu mock via SET_MMU so MMU_STATS / num_toolchanges reflect them.
stats_lock = threading.Lock()
mmu_stats = {
    "swaps_total": 0,
    "swaps_success": 0,
    "swaps_failed": 0,
    "loads_success": 0,
    "unloads_success": 0,
    "last_error": "None",
}

# ---------------------------------------------------------------------------
# SQLite state store — sole persistent store for gate config and MMU stats.
# Default location: ~/.local/share/flare/flare.db
# Override with FLARE_DATA_DIR environment variable.
# ---------------------------------------------------------------------------
_DATA_DIR = os.environ.get("FLARE_DATA_DIR", os.path.expanduser("~/.local/share/flare"))
_DB_PATH = os.path.join(_DATA_DIR, "flare.db")
_db_lock = threading.Lock()

def db_init():
    """Create the database directory and tables on first run."""
    db_dir = os.path.dirname(_DB_PATH)
    os.makedirs(db_dir, exist_ok=True)
    with _db_lock:
        con = sqlite3.connect(_DB_PATH)
        try:
            con.executescript("""
                CREATE TABLE IF NOT EXISTS gate_config (
                    key   TEXT PRIMARY KEY,
                    value TEXT NOT NULL
                );
                CREATE TABLE IF NOT EXISTS stats (
                    key   TEXT PRIMARY KEY,
                    value TEXT NOT NULL
                );
            """)
            con.commit()
        finally:
            con.close()

def db_get(table, key, default=None):
    with _db_lock:
        con = sqlite3.connect(_DB_PATH)
        try:
            row = con.execute(f"SELECT value FROM {table} WHERE key=?", (key,)).fetchone()
        finally:
            con.close()
    if row is None:
        return default
    try:
        return json.loads(row[0])
    except Exception:
        return row[0]

def db_set(table, key, value):
    with _db_lock:
        con = sqlite3.connect(_DB_PATH)
        try:
            con.execute(
                f"INSERT OR REPLACE INTO {table} (key, value) VALUES (?, ?)",
                (key, json.dumps(value))
            )
            con.commit()
        finally:
            con.close()

def load_mmu_stats():
    try:
        with stats_lock:
            for k in mmu_stats:
                v = db_get("stats", k)
                if v is not None:
                    mmu_stats[k] = v
    except Exception as e:
        print(f"flare_daemon: failed to load stats from db: {e}", file=sys.stderr)

def save_mmu_stats():
    try:
        with stats_lock:
            snapshot = dict(mmu_stats)
        for k, v in snapshot.items():
            db_set("stats", k, v)
    except Exception as e:
        print(f"flare_daemon: failed to save stats to db: {e}", file=sys.stderr)

def record_event_stats(evt_type, evt_data):
    """Increment usage counters from board events. TC: drives swaps; lane tasks
    drive loads/unloads. Events are reliable (firmware emits TC:DONE/TC:ERROR/
    LOADED/UNLOADED), so this never miscounts a transient poll."""
    changed = True
    with stats_lock:
        if evt_type == "TC:DONE":
            mmu_stats["swaps_total"] += 1
            mmu_stats["swaps_success"] += 1
        elif evt_type == "TC:ERROR":
            mmu_stats["swaps_total"] += 1
            mmu_stats["swaps_failed"] += 1
            mmu_stats["last_error"] = evt_data or "Unknown"
        elif evt_type == "LOADED":
            mmu_stats["loads_success"] += 1
        elif evt_type == "UNLOADED":
            mmu_stats["unloads_success"] += 1
        else:
            changed = False
    if changed:
        save_mmu_stats()

# Endpoints / Spoolman config (set from CLI args in main())
MOONRAKER_URL = "http://localhost:7125"
SPOOLMAN_URL = "http://localhost:7912"
NUM_GATES = 2

# Spoolman spool detail cache (spool_id -> (timestamp, data))
_spool_cache = {}
_spool_cache_lock = threading.Lock()
SPOOL_CACHE_TTL = 30.0

def _read_gate_map():
    """Read the gate map from SQLite."""
    def pad(key, default):
        lst = db_get("gate_config", key, [])
        if not isinstance(lst, list):
            lst = []
        out = list(lst[:NUM_GATES])
        while len(out) < NUM_GATES:
            out.append(default(len(out)) if callable(default) else default)
        return out

    return {
        "num_gates": NUM_GATES,
        "gate_material": pad("gate_material", ""),
        "gate_color": pad("gate_color", ""),
        "gate_spool_id": pad("gate_spool_id", -1),
        "gate_name": pad("gate_name", lambda i: f"Gate {i}"),
    }

def _write_gate_map_db(gate, fields):
    """Update gate map in SQLite."""
    gm = _read_gate_map()
    g = int(gate)
    if 0 <= g < NUM_GATES:
        if fields.get("material") is not None:
            gm["gate_material"][g] = str(fields["material"])
        if fields.get("color") is not None:
            gm["gate_color"][g] = str(fields["color"]).lstrip("#")[:6]
        if fields.get("spool_id") is not None:
            gm["gate_spool_id"][g] = int(fields["spool_id"])
        if fields.get("name") is not None:
            gm["gate_name"][g] = str(fields["name"])
    try:
        for key in ("gate_material", "gate_color", "gate_spool_id", "gate_name"):
            db_set("gate_config", key, gm[key])
        return True
    except Exception as e:
        print(f"flare_daemon: failed to write gate map to db: {e}", file=sys.stderr)
        return False

def _push_gate_map_to_klipper(gate, fields):
    """Push a gate-map edit to the Klipper mmu mock (MMU_GATE_MAP) so Fluidd
    stays in sync. Uses a single-quoted python-dict literal like Fluidd does."""
    inner = {k: fields[k] for k in ("material", "color", "name", "spool_id")
             if fields.get(k) is not None}
    if not inner:
        return False
    map_literal = repr({int(gate): inner})
    gcode = f'MMU_GATE_MAP MAP="{map_literal}"'
    try:
        payload = json.dumps({"script": gcode}).encode("utf-8")
        req = urllib.request.Request(
            f"{MOONRAKER_URL}/printer/gcode/script",
            data=payload, headers={"Content-Type": "application/json"}, method="POST")
        urllib.request.urlopen(req, timeout=2.0)
        return True
    except Exception:
        return False

def _spoolman_fetch_spool(spool_id):
    """Fetch spool detail: Moonraker proxy first, then direct Spoolman API."""
    spool = None
    try:
        body = json.dumps({"request_method": "GET",
                           "path": f"/v1/spool/{spool_id}"}).encode("utf-8")
        req = urllib.request.Request(
            f"{MOONRAKER_URL}/server/spoolman/proxy",
            data=body, headers={"Content-Type": "application/json"}, method="POST")
        with urllib.request.urlopen(req, timeout=1.5) as resp:
            d = json.loads(resp.read().decode("utf-8"))
        spool = d.get("result", d)
    except Exception:
        spool = None
    if not isinstance(spool, dict):
        try:
            with urllib.request.urlopen(f"{SPOOLMAN_URL}/v1/spool/{spool_id}", timeout=1.5) as resp:
                spool = json.loads(resp.read().decode("utf-8"))
        except Exception:
            return None
    if not isinstance(spool, dict):
        return None
    fil = spool.get("filament", {}) or {}
    return {
        "name": fil.get("name") or spool.get("name"),
        "material": fil.get("material"),
        "color_hex": fil.get("color_hex"),
        "remaining_weight": spool.get("remaining_weight"),
        "remaining_length": spool.get("remaining_length"),
    }

def _spoolman_get_spool(spool_id):
    now = time.time()
    with _spool_cache_lock:
        ent = _spool_cache.get(spool_id)
        if ent and now - ent[0] < SPOOL_CACHE_TTL:
            return ent[1]
    data = _spoolman_fetch_spool(spool_id)
    with _spool_cache_lock:
        _spool_cache[spool_id] = (now, data)
    return data

# --- Filament usage tracking (consumption) ---
FILAMENT_DIAMETER_MM = 1.75
DEFAULT_DENSITY_G_CM3 = 1.24  # ~PETG; only used for the offline local estimate
_usage_lock = threading.Lock()

def _usage_path():
    for p in (
        os.path.expanduser("~/printer_data/config/flare_spool_usage.json"),
        os.path.expanduser("~/flare_spool_usage.json"),
        "/tmp/flare_spool_usage.json",
    ):
        d = os.path.dirname(p)
        if os.path.isdir(d) and os.access(d, os.W_OK):
            return p
    return "/tmp/flare_spool_usage.json"

def _mm_to_grams(length_mm):
    r = FILAMENT_DIAMETER_MM / 2.0
    vol_cm3 = (math.pi * r * r * length_mm) / 1000.0
    return vol_cm3 * DEFAULT_DENSITY_G_CM3

_mr_spoolman_cache = {"ts": 0.0, "active": False}
_mr_spoolman_lock = threading.Lock()

def _moonraker_spoolman_active():
    """True when Moonraker's Spoolman integration is connected. If so, Moonraker
    already bills the active spool (which we set) from extruder moves, so the
    daemon must NOT also report usage or it would double-count. Cached 15 s."""
    now = time.time()
    with _mr_spoolman_lock:
        if now - _mr_spoolman_cache["ts"] < 15.0:
            return _mr_spoolman_cache["active"]
    active = False
    try:
        with urllib.request.urlopen(f"{MOONRAKER_URL}/server/spoolman/status", timeout=1.0) as resp:
            d = json.loads(resp.read().decode("utf-8"))
        active = bool(d.get("result", {}).get("spoolman_connected", False))
    except Exception:
        active = False
    with _mr_spoolman_lock:
        _mr_spoolman_cache["ts"] = now
        _mr_spoolman_cache["active"] = active
    return active

def _spoolman_use_length(spool_id, length_mm):
    """Report consumed length to Spoolman (it computes grams from its own
    filament density). Moonraker proxy first, then direct Spoolman API."""
    payload = json.dumps({"use_length": round(length_mm, 4)})
    try:
        body = json.dumps({"request_method": "POST",
                           "path": f"/v1/spool/{spool_id}/use",
                           "body": payload}).encode("utf-8")
        req = urllib.request.Request(
            f"{MOONRAKER_URL}/server/spoolman/proxy",
            data=body, headers={"Content-Type": "application/json"}, method="POST")
        with urllib.request.urlopen(req, timeout=1.5) as resp:
            resp.read()
        return True
    except Exception:
        pass
    try:
        req = urllib.request.Request(
            f"{SPOOLMAN_URL}/v1/spool/{spool_id}/use",
            data=payload.encode("utf-8"), headers={"Content-Type": "application/json"}, method="POST")
        with urllib.request.urlopen(req, timeout=1.5) as resp:
            resp.read()
        return True
    except Exception:
        return False

def _local_usage_read():
    with _usage_lock:
        try:
            with open(_usage_path()) as f:
                return json.load(f)
        except Exception:
            return {}

def _local_usage_add(gate, spool_id, length_mm):
    with _usage_lock:
        try:
            with open(_usage_path()) as f:
                data = json.load(f)
        except Exception:
            data = {}
        key = str(gate)
        ent = data.get(key, {"used_mm": 0.0, "used_g": 0.0})
        ent["used_mm"] = round(ent.get("used_mm", 0.0) + length_mm, 2)
        ent["used_g"] = round(ent.get("used_g", 0.0) + _mm_to_grams(length_mm), 3)
        ent["spool_id"] = spool_id
        data[key] = ent
        try:
            with open(_usage_path(), "w") as f:
                json.dump(data, f)
        except Exception:
            pass

def filament_usage_tracker():
    """Attribute MMU sync feed (TF delta = filament consumed by the print) to the
    loaded gate's spool: report to Spoolman if reachable, else accumulate locally.
    Runs regardless of Klipper/Moonraker so standalone setups still track usage."""
    last_total = None
    while True:
        time.sleep(1.0)
        with status_lock:
            s = dict(status_cache)
        total = s.get("total_fed_mm")
        if total is None:
            continue
        if last_total is None or total < last_total - 1.0:
            last_total = total  # init, or board reset (TF rewound)
            continue
        delta = total - last_total
        last_total = total
        if delta <= 0.05:
            continue
        # If Klipper's Moonraker is already tracking Spoolman usage (it bills the
        # active spool we set, from extruder moves), do not double-count here.
        if _moonraker_spoolman_active():
            continue
        ys, th = s.get("y_split", 0), s.get("toolhead", 0)
        loaded = -1
        if s.get("in1") and s.get("out1") and ys and th:
            loaded = 0
        elif s.get("in2") and s.get("out2") and ys and th:
            loaded = 1
        if loaded < 0:
            continue
        gm = _read_gate_map()
        sid = gm["gate_spool_id"][loaded] if loaded < len(gm["gate_spool_id"]) else -1
        recorded = False
        if isinstance(sid, int) and sid >= 0:
            recorded = _spoolman_use_length(sid, delta)
        if not recorded:
            _local_usage_add(loaded, sid, delta)

def build_gatemap_response():
    gm = _read_gate_map()
    usage = _local_usage_read()
    gates = []
    for i in range(gm["num_gates"]):
        sid = gm["gate_spool_id"][i]
        spool = _spoolman_get_spool(sid) if isinstance(sid, int) and sid >= 0 else None
        gates.append({
            "material": gm["gate_material"][i],
            "color": gm["gate_color"][i],
            "name": gm["gate_name"][i],
            "spool_id": sid,
            "spool": spool,
            "used": usage.get(str(i)),
        })
    return {"num_gates": gm["num_gates"], "gates": gates}

def apply_gatemap_edit(gate, fields):
    """Persist a gate edit: push to Klipper if running, else write to SQLite.
    Invalidate the spool cache for the affected gate so the next read refreshes."""
    pushed = _push_gate_map_to_klipper(gate, fields)
    if not pushed:
        _write_gate_map_db(gate, fields)
    if fields.get("spool_id") is not None:
        with _spool_cache_lock:
            _spool_cache.pop(int(fields["spool_id"]), None)
    return {"pushed": pushed}

def parse_status_line(line):
    """
    Parse a raw serial status line (e.g. 'OK:LN:1,TC:IDLE,L1T:NONE,...')
    and update status_cache.
    """
    if line.startswith("OK:"):
        line = line[3:]

    parts = line.strip().split(",")
    new_data = {}
    raw_fields = {}

    for part in parts:
        if ":" not in part:
            continue
        # Split at first colon only
        key, val = part.split(":", 1)
        raw_fields[key.strip()] = val.strip()

        try:
            if key == "LN":
                val_int = int(val)
                new_data["active_lane"] = val_int
            elif key == "TC":
                new_data["tc_state"] = val
            elif key == "L1T":
                new_data["lane1_task"] = val
            elif key == "L2T":
                new_data["lane2_task"] = val
            elif key == "BP":
                bp_val = float(val)
                new_data["g_buf_pos"] = bp_val
                # Check sensor type (0 = Type-D digital, 1 = Type-P analog)
                stype = new_data.get("buf_sensor_type", status_cache.get("buf_sensor_type", 0))
                if stype == 1:
                    new_data["sync_feedback"] = max(-1.0, min(1.0, bp_val))
                else:
                    new_data["sync_feedback"] = max(-1.0, min(1.0, bp_val / 15.0))
            elif key == "BST":
                val_int = int(val)
                new_data["buf_sensor_type"] = val_int
            elif key == "BUF":
                new_data["buf_state"] = val
                new_data["sync_feedback_state"] = val.lower()
            elif key == "SM":
                new_data["sync_enabled"] = int(val)
                new_data["sync_drive"] = (int(val) == 1)
            elif key == "ST":
                new_data["sync_state"] = int(val)
            elif key == "BL":
                new_data["bl_arm"] = val
            elif key == "MM":
                new_data["sps"] = float(val)
            elif key == "BF":
                new_data["baseline_sps"] = float(val)
            elif key == "EST":
                new_data["extruder_est_sps"] = float(val)
            elif key == "RE":
                new_data["reserve_error_mm"] = float(val)
            elif key == "I1":
                new_data["in1"] = int(val)
            elif key == "O1":
                new_data["out1"] = int(val)
            elif key == "I2":
                new_data["in2"] = int(val)
            elif key == "O2":
                new_data["out2"] = int(val)
            elif key == "TH":
                new_data["toolhead"] = int(val)
            elif key == "YS":
                new_data["y_split"] = int(val)
            elif key == "RELOAD":
                new_data["reload_mode"] = int(val)
            elif key == "CU":
                new_data["enable_cutter"] = int(val)
            elif key == "UC":
                new_data["unload_cut"] = int(val)
            elif key == "TF":
                new_data["total_fed_mm"] = float(val)
            elif key == "FL_RATE":
                new_data["feed_rate_mms"] = float(val) / 60.0
            elif key == "UL_RATE":
                new_data["rev_rate_mms"] = float(val) / 60.0
        except ValueError:
            pass # ignore malformed metrics

    if new_data:
        new_data["raw_status"] = raw_fields
        new_data["board_online"] = True
        new_data["timestamp"] = time.time()
        with stats_lock:
            new_data["mmu_stats"] = dict(mmu_stats)
        with status_lock:
            status_cache.update(new_data)

        # Broadcast to all active SSE queues
        broadcast_telemetry(new_data)

def broadcast_telemetry(data):
    payload = json.dumps(data)
    with sse_queues_lock:
        for q in list(active_sse_queues):
            try:
                q.put_nowait(payload)
            except queue.Full:
                pass

def serial_reader(port_name, baud):
    global serial_port, command_reply, current_executing_command

    while True:
        print(f"flare_daemon: connecting to {port_name}...")
        try:
            with serial_lock:
                serial_port = serial.Serial(port_name, baud, timeout=1.0)
                serial_port.reset_input_buffer()

            print(f"flare_daemon: connected to {port_name} successfully")
            with status_lock:
                status_cache["board_online"] = True

            while True:
                line_bytes = serial_port.readline()
                if not line_bytes:
                    continue

                line = line_bytes.decode("utf-8", errors="ignore").strip()
                if not line:
                    continue

                # Check for asynchronous Event stream
                if line.startswith("EV:"):
                    evt_body = line[3:]
                    parts = evt_body.split(":")
                    if len(parts) > 1:
                        if parts[0] in ("TC", "CUT", "FAULT", "BL", "BUF_STAB", "SYNC") and len(parts) >= 2:
                            evt_type = f"{parts[0]}:{parts[1]}"
                            evt_data = ":".join(parts[2:])
                        else:
                            evt_type = parts[0]
                            evt_data = ":".join(parts[1:])
                    else:
                        evt_type = evt_body
                        evt_data = ""
                    print(f"flare_daemon Event: {line}")
                    add_event_to_history(evt_type, evt_data)
                    record_event_stats(evt_type, evt_data)
                    broadcast_telemetry({"event_type": evt_type, "event_data": evt_data})

                # Check for command reply
                elif line.startswith("OK:") or line.startswith("ER:") or line == "OK":
                    # If it's a status dump response (starts with OK:LN: or OK:LN=)
                    if "LN:" in line:
                        parse_status_line(line)
                        if current_executing_command and not current_executing_command.startswith("?"):
                            continue

                    command_reply = line
                    command_event.set()

                # Raw status dump line (safety fallback)
                elif "LN:" in line and "TC:" in line:
                    parse_status_line(line)

        except (serial.SerialException, OSError) as e:
            print(f"flare_daemon connection error: {e}", file=sys.stderr)
            with status_lock:
                status_cache["board_online"] = False
            broadcast_telemetry({"board_online": False})

            with serial_lock:
                if serial_port:
                    try:
                        serial_port.close()
                    except Exception:
                        pass
                    serial_port = None

            time.sleep(2.0)

def status_poller():
    """Background status poller (requests status updates at 5Hz)."""
    while True:
        time.sleep(0.2)

        # Only write status poll command if no command execution is active
        with serial_lock:
            if serial_port and serial_port.is_open:
                try:
                    # Write status request to board
                    serial_port.write(b"?: \n")
                    serial_port.flush()
                except Exception:
                    pass

# ---------------------------------------------------------------------------
# HTTP Handler & Server Mixins
# ---------------------------------------------------------------------------
class ThreadedHTTPServer(ThreadingMixIn, HTTPServer):
    """Handle requests in separate threads."""
    allow_reuse_address = True
    daemon_threads = True

class FlareHTTPHandler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        # Suppress spammy log dumps for telemetry requests
        if args and isinstance(args[0], str) and ("GET /telemetry" in args[0] or "GET /status" in args[0]):
            return
        super().log_message(format, *args)

    def do_GET(self):
        if self.path == "/status":
            with status_lock:
                snapshot = dict(status_cache)
            with event_history_lock:
                snapshot["events"] = list(event_history)
            res = json.dumps(snapshot)
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(res.encode("utf-8"))

        elif self.path == "/telemetry":
            # Server-Sent Events (SSE) Stream
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.send_header("Connection", "keep-alive")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()

            # Create a queue for this stream connection
            q = queue.Queue(maxsize=50)
            with sse_queues_lock:
                active_sse_queues.add(q)

            print(f"flare_daemon: SSE telemetry stream client connected (total active: {len(active_sse_queues)})")

            try:
                # Send current initial state immediately
                with status_lock:
                    initial_payload = json.dumps(status_cache)
                self.wfile.write(f"data: {initial_payload}\n\n".encode())
                self.wfile.flush()

                while True:
                    try:
                        # Wait for next broadcast frame
                        payload = q.get(timeout=5.0)
                        self.wfile.write(f"data: {payload}\n\n".encode())
                        self.wfile.flush()
                    except queue.Empty:
                        # Heartbeat frame to keep connection alive
                        self.wfile.write(b": keepalive\n\n")
                        self.wfile.flush()
            except Exception:
                # Client disconnected
                pass
            finally:
                with sse_queues_lock:
                    active_sse_queues.discard(q)
                print(f"flare_daemon: SSE telemetry stream client disconnected (remaining: {len(active_sse_queues)})")

        elif self.path == "/config":
            data = {}
            for key in ("gate_material", "gate_color", "gate_spool_id",
                        "gate_color_rgb", "gate_name", "gate_filament_name",
                        "ttg_map", "spoolman_support"):
                v = db_get("gate_config", key)
                if v is not None:
                    data[key] = v
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(json.dumps(data).encode("utf-8"))

        elif self.path == "/gatemap":
            res = json.dumps(build_gatemap_response())
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(res.encode("utf-8"))

        elif self.path == "/" or self.path == "/index.html":
            self.serve_static_file("index.html", "text/html")
        elif self.path == "/app.js":
            self.serve_static_file("app.js", "application/javascript")
        elif self.path == "/style.css":
            self.serve_static_file("style.css", "text/css")
        else:
            self.send_error(404, "File Not Found")

    def do_POST(self):
        if self.path == "/cmd":
            content_length = int(self.headers['Content-Length'])
            post_data = self.rfile.read(content_length)

            try:
                body = json.loads(post_data.decode("utf-8"))
                cmd_str = body.get("cmd", "").strip()
            except Exception:
                self.send_error(400, "Invalid JSON payload")
                return

            if not cmd_str:
                self.send_error(400, "Missing cmd parameter")
                return

            # Send command directly to board with lock
            response = self.execute_serial_command(cmd_str)

            if response is None:
                self.send_response(504) # Gateway Timeout
                self.send_header("Content-Type", "application/json")
                self.send_header("Access-Control-Allow-Origin", "*")
                self.end_headers()
                self.wfile.write(json.dumps({"error": "command_timeout"}).encode("utf-8"))
            else:
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Access-Control-Allow-Origin", "*")
                self.end_headers()
                self.wfile.write(json.dumps({"response": response}).encode("utf-8"))

        elif self.path == "/gatemap":
            content_length = int(self.headers.get('Content-Length', 0))
            post_data = self.rfile.read(content_length)
            try:
                body = json.loads(post_data.decode("utf-8"))
                gate = int(body.get("gate"))
            except Exception:
                self.send_error(400, "Invalid gate map payload")
                return
            fields = {k: body[k] for k in ("material", "color", "name", "spool_id") if k in body}
            result = apply_gatemap_edit(gate, fields)
            resp = build_gatemap_response()
            resp.update(result)
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(json.dumps(resp).encode("utf-8"))

        elif self.path == "/config":
            content_length = int(self.headers.get('Content-Length', 0))
            post_data = self.rfile.read(content_length)
            try:
                body = json.loads(post_data.decode("utf-8"))
            except Exception:
                self.send_error(400, "Invalid JSON payload")
                return
            for key, value in body.items():
                db_set("gate_config", key, value)
            broadcast_telemetry({"type": "gatemap_update"})
            if "bypass" in body:
                bypass_val = bool(body["bypass"])
                with status_lock:
                    status_cache["bypass"] = bypass_val
                broadcast_telemetry({"type": "bypass_update", "bypass": bypass_val})
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(json.dumps({"ok": True}).encode("utf-8"))

        else:
            self.send_error(404, "Not Found")

    def do_OPTIONS(self):
        # Support CORS pre-flight requests
        self.send_response(200)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    def serve_static_file(self, filename, content_type):
        webui_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "webui")
        filepath = os.path.join(webui_dir, filename)

        if not os.path.exists(filepath):
            # Fallback inline creation for first boot / recovery
            self.send_error(404, f"{filename} not found")
            return

        try:
            with open(filepath, "rb") as f:
                content = f.read()
            self.send_response(200)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(content)))
            self.end_headers()
            self.wfile.write(content)
        except Exception as e:
            self.send_error(500, f"Error reading file: {e}")

    def execute_serial_command(self, cmd_str):
        global command_reply, current_executing_command

        # Formatting check
        if not cmd_str.endswith("\n"):
            cmd_str += "\n"

        with serial_lock:
            if not serial_port or not serial_port.is_open:
                return "ER:BOARD_OFFLINE"

            command_event.clear()
            command_reply = None
            current_executing_command = cmd_str.strip()

            try:
                serial_port.write(cmd_str.encode("utf-8"))
                serial_port.flush()
            except Exception as e:
                current_executing_command = None
                return f"ER:WRITE_ERROR:{e}"

            # Block until event is fired (timeout 10.0s for typical moves)
            # Long commands (FL, UL, TC) execute async and return OK immediately.
            success = command_event.wait(timeout=10.0)
            current_executing_command = None

            if success:
                return command_reply
            else:
                return None


def _moonraker_get_gate_spool_ids(moonraker_url):
    """Read the gate->spool_id mapping from the Klipper mmu object via Moonraker."""
    try:
        url = f"{moonraker_url}/printer/objects/query?mmu=gate_spool_id"
        with urllib.request.urlopen(url, timeout=1.0) as resp:
            data = json.loads(resp.read().decode("utf-8"))
        return data.get("result", {}).get("status", {}).get("mmu", {}).get("gate_spool_id", []) or []
    except Exception:
        return []

def _moonraker_set_active_spool(moonraker_url, spool_id):
    """Set Moonraker's active Spoolman spool (None clears it). Moonraker then
    bills filament consumption to this spool. No-op/ignored if Spoolman is not
    configured (404)."""
    try:
        body = json.dumps({"spool_id": spool_id}).encode("utf-8")
        req = urllib.request.Request(
            f"{moonraker_url}/server/spoolman/spool_id",
            data=body, headers={"Content-Type": "application/json"}, method="POST")
        urllib.request.urlopen(req, timeout=1.0)
        return True
    except Exception:
        return False

def _derive_action(tc_state, active_lane, lane1_task, lane2_task):
    """Map FLARE toolchange/lane-task state to a Happy Hare action string so
    Fluidd shows 'Loading: X mm' / 'Unloading: X mm' during operations."""
    ts = (tc_state or "").upper()
    if ts.startswith("LOAD") or ts == "SWAP" or ts.startswith("RELOAD"):
        return "Loading"
    if ts.startswith("UNLOAD"):
        return "Unloading"
    task = lane1_task if active_lane == 1 else (lane2_task if active_lane == 2 else "")
    task = (task or "").upper()
    if task in ("AUTOLOAD", "LOAD_FULL"):
        return "Loading"
    if task == "UNLOAD":
        return "Unloading"
    return "Idle"

def klipper_syncer(moonraker_url):
    """Background thread to push status updates to Moonraker at 4Hz."""
    last_sync = {}
    backoff = 0.0
    last_force_sync = 0.0
    was_online = False
    last_loaded_gate = None
    last_active_spool = object()  # sentinel distinct from None / any spool id
    last_pushed_fields = {}  # KEY -> last-pushed formatted value, for delta SET_MMU
    last_gate_dbg = None  # FLARE_GATE_DEBUG: last logged gate-input tuple
    gate_debug = bool(os.environ.get("FLARE_GATE_DEBUG"))

    while True:
        time.sleep(0.25)

        # Check backoff timer
        if backoff > time.time():
            continue

        # Get copy of current cache
        with status_lock:
            state = dict(status_cache)

        # Detect changes in values of interest (excluding high-frequency float noise)
        keys = [
            "board_online", "active_lane", "tc_state",
            "buf_state", "in1", "out1", "in2", "out2",
            "toolhead", "y_split", "reload_mode"
        ]

        changed = False
        for k in keys:
            if state.get(k) != last_sync.get(k):
                changed = True
                break
        # Lane-task changes drive the action label
        for k in ("lane1_task", "lane2_task"):
            if state.get(k) != last_sync.get(k):
                changed = True

        # Check if sync_feedback changed significantly to update Mainsail/Fluidd piston
        stype = state.get("buf_sensor_type", 0)
        g_buf_pos = state.get("g_buf_pos", 0.0)
        sync_feedback = max(-1.0, min(1.0, g_buf_pos)) if stype == 1 else max(-1.0, min(1.0, g_buf_pos / 15.0))

        last_stype = last_sync.get("buf_sensor_type", 0)
        last_g_buf_pos = last_sync.get("g_buf_pos", 0.0)
        last_sync_feedback = max(-1.0, min(1.0, last_g_buf_pos)) if last_stype == 1 else max(-1.0, min(1.0, last_g_buf_pos / 15.0))

        if abs(sync_feedback - last_sync_feedback) > 0.05:
            changed = True

        # Force full sync every 10 seconds to recover if Klipper/Moonraker restarted.
        # full = send every SET_MMU field (not just the delta) for restart recovery.
        force_full = False
        if time.time() - last_force_sync > 10.0:
            changed = True
            force_full = True

        board_online = state.get("board_online", False)
        trigger_board_sync = False
        if board_online and not was_online:
            trigger_board_sync = True
            changed = True
            force_full = True

        if not last_pushed_fields:
            force_full = True  # first push after start / recovery

        if not changed:
            continue

        lines = []

        # Add SET_MMU command to update Klipper native mmu object variables (Mainsail/Fluidd)
        active_lane = state.get("active_lane", 0)
        active_gate = active_lane - 1  # 0-indexed: L1 -> 0, L2 -> 1, none -> -1
        out1 = state.get("out1", 0)
        out2 = state.get("out2", 0)
        in1 = state.get("in1", 0)
        in2 = state.get("in2", 0)
        toolhead = state.get("toolhead", 0)
        g_buf_pos = state.get("g_buf_pos", 0.0)
        stype = state.get("buf_sensor_type", 0)
        if stype == 1:
            sync_feedback = max(-1.0, min(1.0, g_buf_pos))
        else:
            sync_feedback = max(-1.0, min(1.0, g_buf_pos / 15.0))
        sync_feedback_enabled = 1
        buf_state = state.get("buf_state", "NEUTRAL").lower()
        if buf_state in ["+", "tension"]:
            buf_state = "tension"
        elif buf_state in ["-", "compression", "compressed"]:
            buf_state = "compressed"
        else:
            buf_state = "neutral"
        tc_state = state.get("tc_state", "UNKNOWN")
        board_online = 1 if state.get("board_online", False) else 0
        sps = state.get("sps", 0.0)
        reload_mode = state.get("reload_mode", 0)
        enable_cutter = state.get("enable_cutter", 0)
        unload_cut = state.get("unload_cut", 0)

        # Map print job state and print state
        if tc_state in ["FOLLOW", "APPROACH"]:
            print_job_state = "printing"
            print_state = "printing"
        elif tc_state == "IDLE":
            print_job_state = "standby"
            print_state = "ready"
        else:
            print_job_state = "standby"
            print_state = "ready"

        y_split = state.get("y_split", 0)
        if in1 and out1 and y_split and toolhead:
            gate_status_1 = 2
        elif in1:
            gate_status_1 = 1
        else:
            gate_status_1 = 0

        if in2 and out2 and y_split and toolhead:
            gate_status_2 = 2
        elif in2:
            gate_status_2 = 1
        else:
            gate_status_2 = 0

        # Determine if any gate is actually fully loaded to the toolhead
        loaded_gate = -1
        if gate_status_1 == 2:
            loaded_gate = 0
        elif gate_status_2 == 2:
            loaded_gate = 1

        # Align klipper_tool and klipper_gate to active_gate so that UI highlights the selected card
        # and displays its spool details correctly. Bypass is persisted by the
        # daemon and re-asserted via the BYPASS field below so it survives a
        # Klipper restart; the mmu mock forces the -2 sentinels from that flag.
        klipper_tool = active_gate
        klipper_gate = active_gate
        # Physical sensor states for active gate and combiner
        gate_sensor_active = out1 if active_gate == 0 else (out2 if active_gate == 1 else 0)
        extruder_sensor_active = y_split
        pre_gate_sensor_active = in1 if active_gate == 0 else (in2 if active_gate == 1 else 0)
        hub_sensor_active = y_split

        with stats_lock:
            st = dict(mmu_stats)

        action = _derive_action(tc_state, active_lane,
                                state.get("lane1_task", "IDLE"),
                                state.get("lane2_task", "IDLE"))

        feed_rate = state.get("feed_rate_mms", 50.0)
        rev_rate = state.get("rev_rate_mms", 50.0)
        bypass = bool(state.get("bypass", False))

        # All SET_MMU mirror fields as formatted strings, in a stable order. cmd_SET_MMU
        # keeps the current value for any absent param, so we can push only the fields
        # that changed (delta) and still leave the mock in the same state.
        fields = {
            "NUM_GATES": "2",
            "ACTIVE_GATE": str(active_gate),
            "GATE": str(klipper_gate),
            "TOOL": str(klipper_tool),
            "ACTION": f"'{action}'",
            "TC_STATE": f"'{tc_state}'",
            "GATE_STATUS": f"'{gate_status_1},{gate_status_2}'",
            "GATE_SENSOR": f"'{in1},{in2}'",
            "TOOLHEAD_SENSOR": str(toolhead),
            "SYNC_FEEDBACK": f"{sync_feedback:.3f}",
            "SYNC_FEEDBACK_ENABLED": str(sync_feedback_enabled),
            "SYNC_FEEDBACK_STATE": f"'{buf_state}'",
            "PRINT_JOB_STATE": f"'{print_job_state}'",
            "PRINT_STATE": f"'{print_state}'",
            "BOARD_ONLINE": str(board_online),
            "SPS": f"{sps:.3f}",
            "RELOAD_MODE": str(reload_mode),
            "ENABLE_CUTTER": str(enable_cutter),
            "UNLOAD_CUT": str(unload_cut),
            "BUF_SENSOR_TYPE": str(stype),
            "GATE_SENSOR_ACTIVE": str(gate_sensor_active),
            "EXTRUDER_SENSOR_ACTIVE": str(extruder_sensor_active),
            "PRE_GATE_SENSOR_ACTIVE": str(pre_gate_sensor_active),
            "HUB_SENSOR_ACTIVE": str(hub_sensor_active),
            "SWAPS_TOTAL": str(st["swaps_total"]),
            "SWAPS_SUCCESS": str(st["swaps_success"]),
            "SWAPS_FAILED": str(st["swaps_failed"]),
            "LOADS_SUCCESS": str(st["loads_success"]),
            "UNLOADS_SUCCESS": str(st["unloads_success"]),
            "MMU_LAST_ERROR": f"'{st['last_error']}'",
            "FEED_RATE": f"{feed_rate:.2f}",
            "REV_RATE": f"{rev_rate:.2f}",
            "BYPASS": str(1 if bypass else 0),
        }

        # FLARE_GATE_DEBUG: log gate-relevant inputs when they change, to root-cause
        # the gate-status dot blink. No-op unless the env flag is set.
        if gate_debug:
            gate_tuple = (active_gate, in1, out1, in2, out2,
                          gate_status_1, gate_status_2, tc_state)
            if gate_tuple != last_gate_dbg:
                last_gate_dbg = gate_tuple
                print(f"[gate-dbg {time.time():.3f}] active_gate={active_gate} "
                      f"in1={in1} out1={out1} in2={in2} out2={out2} "
                      f"gate_status={gate_status_1},{gate_status_2} tc={tc_state}",
                      file=sys.stderr, flush=True)

        if force_full:
            delta = fields
        else:
            delta = {k: v for k, v in fields.items() if last_pushed_fields.get(k) != v}

        if not delta and not trigger_board_sync:
            continue

        if delta:
            lines.append("SET_MMU " + " ".join(f"{k}={v}" for k, v in delta.items()))

        if trigger_board_sync:
            lines.append("_FLARE_SYNC_BOARD")

        if not lines:
            continue

        gcode_script = "\n".join(lines)
        payload = json.dumps({"script": gcode_script}).encode("utf-8")

        # Make request to Moonraker
        try:
            req = urllib.request.Request(
                f"{moonraker_url}/printer/gcode/script",
                data=payload,
                headers={"Content-Type": "application/json"},
                method="POST"
            )
            with urllib.request.urlopen(req, timeout=1.0) as resp:
                if resp.status == 200:
                    last_sync = state
                    # Record the full current field snapshot so the next push only
                    # diffs against what Klipper now actually holds.
                    last_pushed_fields = dict(fields)
                    last_force_sync = time.time()
                    was_online = board_online
        except Exception:
            # Moonraker offline, backoff for 5.0 seconds
            last_sync = {}  # Clear cache to force push on recovery
            last_pushed_fields = {}  # force a full SET_MMU on recovery
            backoff = time.time() + 5.0

        # Spoolman: mirror the loaded gate's spool as Moonraker's active spool so
        # consumption is billed to the correct spool on toolchange (like Happy
        # Hare). Pushed only when the loaded gate changes; Moonraker does the
        # actual usage tracking. Harmlessly ignored if Spoolman is not configured.
        if loaded_gate != last_loaded_gate:
            last_loaded_gate = loaded_gate
            desired_spool = None
            if loaded_gate >= 0:
                spool_ids = _moonraker_get_gate_spool_ids(moonraker_url)
                if loaded_gate < len(spool_ids):
                    sid = spool_ids[loaded_gate]
                    if isinstance(sid, int) and sid >= 0:
                        desired_spool = sid
            if desired_spool != last_active_spool:
                if _moonraker_set_active_spool(moonraker_url, desired_spool):
                    last_active_spool = desired_spool


# ---------------------------------------------------------------------------
# Main Execution
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(description="FLARE persistent host proxy daemon")
    parser.add_argument("--port", help="Serial port connection path (e.g. /dev/ttyACM0)")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate (default: 115200)")
    parser.add_argument("--host", default="0.0.0.0", help="HTTP server bind host (default: 0.0.0.0)")
    parser.add_argument("--api-port", type=int, default=8088, help="HTTP/SSE API server port (default: 8088)")
    parser.add_argument("--no-klipper", action="store_true", help="Bypass Moonraker/Klipper telemetry synchronization")
    parser.add_argument("--moonraker-url", default="http://localhost:7125", help="Moonraker base URL (default: http://localhost:7125)")
    parser.add_argument("--spoolman-url", default="http://localhost:7912", help="Spoolman base URL for direct API fallback (default: http://localhost:7912)")
    args = parser.parse_args()

    global MOONRAKER_URL, SPOOLMAN_URL
    MOONRAKER_URL = args.moonraker_url
    SPOOLMAN_URL = args.spoolman_url

    # 1. Resolve preferred serial port candidate
    port_name = serial_utils.find_port(args.port)
    if not port_name:
        print("flare_daemon error: no serial devices found matching candidate patterns", file=sys.stderr)
        sys.exit(1)

    print(f"flare_daemon: resolved active port candidate -> {port_name}")

    # 1.5 Initialise SQLite state store and restore persisted MMU usage statistics
    db_init()
    load_mmu_stats()

    # 2. Launch persistent background serial worker thread
    reader_t = threading.Thread(target=serial_reader, args=(port_name, args.baud), daemon=True)
    reader_t.start()

    # 3. Launch background status poller thread
    poller_t = threading.Thread(target=status_poller, daemon=True)
    poller_t.start()

    # 3.1 Launch filament-usage tracker (Spoolman or local; runs without Klipper)
    usage_t = threading.Thread(target=filament_usage_tracker, daemon=True)
    usage_t.start()

    # 3.5 Launch Klipper telemetry syncer if enabled
    if not args.no_klipper:
        print(f"flare_daemon: Klipper telemetry syncer enabled targeting {args.moonraker_url}")
        syncer_t = threading.Thread(target=klipper_syncer, args=(args.moonraker_url,), daemon=True)
        syncer_t.start()

    # 4. Start HTTP & SSE proxy web server
    try:
        server = ThreadedHTTPServer((args.host, args.api_port), FlareHTTPHandler)
        print(f"flare_daemon: HTTP and SSE server running on http://{args.host}:{args.api_port}")
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nflare_daemon: shutting down...")
    except Exception as e:
        print(f"flare_daemon server error: {e}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
