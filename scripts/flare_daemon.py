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
import hmac
import json
import math
import os
import queue
import secrets
import sqlite3
import sys
import threading
import time
import urllib.parse
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
    # pyserial is only needed for the live daemon's serial I/O, not for
    # importing this module (e.g. host unit tests that exercise pure logic like
    # record_event_stats). Defer the hard failure to main(), where serial is used.
    serial = None

# Global runtime state
serial_port = None
serial_lock = threading.Lock()
command_event = threading.Event()
command_reply = None
current_executing_command = None
klipper_sync_event = threading.Event()
KLIPPER_PUSH_RETRY_S = 0.5  # retry cadence for a SET_MMU push skipped while Klipper is Printing

def trigger_klipper_sync():
    """Signal klipper_syncer that a firmware event or relevant state change occurred."""
    klipper_sync_event.set()

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
    "tmc_health": "11",
    "maintenance": {"counters": {}, "warnings": []},
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

# Klipper mirror host-busy flag: True while gcode lock is held by a blocking command.
# Set by klipper_syncer thread; read by HTTP handler (apply_gatemap_edit).
_g_host_busy = False

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
                CREATE TABLE IF NOT EXISTS maintenance_counters (
                    name        TEXT PRIMARY KEY,
                    count       INTEGER NOT NULL DEFAULT 0,
                    limit_val   INTEGER,
                    warning     TEXT,
                    pause       INTEGER NOT NULL DEFAULT 0,
                    last_reset  TEXT
                );
            """)
            con.commit()
            rows = con.execute("SELECT COUNT(*) FROM maintenance_counters").fetchone()
            if rows and rows[0] == 0:
                now_iso = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
                defaults = [
                    ("cutter_cuts", 0, 1000, "Cutter blade wear limit reached. Replace blade and reset counter.", 0, now_iso),
                    ("swaps", 0, None, "", 0, now_iso),
                    ("reload_failovers", 0, None, "", 0, now_iso),
                ]
                con.executemany(
                    "INSERT INTO maintenance_counters (name, count, limit_val, warning, pause, last_reset) VALUES (?, ?, ?, ?, ?, ?)",
                    defaults
                )
                con.commit()
        finally:
            con.close()

def db_get(table, key, default=None):
    if not os.path.exists(_DB_PATH):
        return default
    with _db_lock:
        try:
            con = sqlite3.connect(_DB_PATH)
            try:
                row = con.execute(f"SELECT value FROM {table} WHERE key=?", (key,)).fetchone()
            finally:
                con.close()
        except sqlite3.OperationalError:
            return default
    if row is None:
        return default
    try:
        return json.loads(row[0])
    except Exception:
        return row[0]

def db_set(table, key, value):
    os.makedirs(os.path.dirname(_DB_PATH), exist_ok=True)
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
            # A swap unloads the outgoing lane then loads the new one. The TC
            # unload phase emits no standalone UNLOADED event (only the trailing
            # LOADED counts the load), so count the unload here to keep
            # loads/unloads symmetric -- Happy-Hare counts a swap as
            # unload(old)+load(new).
            mmu_stats["unloads_success"] += 1
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

    # Maintenance counter event hooks
    try:
        if evt_type == "EV:CUT:DONE":
            update_maintenance_counter("cutter_cuts", incr=1)
        elif evt_type == "TC:DONE":
            update_maintenance_counter("swaps", incr=1)
        elif evt_type.startswith("EV:RELOAD:") or evt_type == "EV:RELOAD":
            update_maintenance_counter("reload_failovers", incr=1)
    except Exception as e:
        print(f"flare_daemon: error updating maintenance counter for event {evt_type}: {e}", file=sys.stderr)

# Maintenance tracking
_maintenance_lock = threading.Lock()
_maintenance_paused_counters = set()

def _trigger_klipper_pause(reason):
    """Send PAUSE script to Klipper via Moonraker."""
    script = f'M118 FLARE MAINTENANCE PAUSE: {reason}\nPAUSE'
    payload = json.dumps({"script": script}).encode("utf-8")
    try:
        req = urllib.request.Request(
            f"{MOONRAKER_URL}/printer/gcode/script",
            data=payload, headers={"Content-Type": "application/json"}, method="POST")
        with urllib.request.urlopen(req, timeout=1.5) as resp:
            resp.read()
        return True
    except Exception as e:
        print(f"flare_daemon: failed to send pause to Klipper: {e}", file=sys.stderr)
        return False

def _emit_klipper_warning(msg):
    """Send M118 message to Klipper console."""
    script = f'M118 FLARE WARNING: {msg}'
    payload = json.dumps({"script": script}).encode("utf-8")
    try:
        req = urllib.request.Request(
            f"{MOONRAKER_URL}/printer/gcode/script",
            data=payload, headers={"Content-Type": "application/json"}, method="POST")
        with urllib.request.urlopen(req, timeout=1.5) as resp:
            resp.read()
        return True
    except Exception:
        return False

def get_maintenance_counters():
    """Return all maintenance counters from database."""
    if not os.path.exists(_DB_PATH):
        return {}
    with _db_lock:
        try:
            con = sqlite3.connect(_DB_PATH)
            con.row_factory = sqlite3.Row
            try:
                rows = con.execute("SELECT name, count, limit_val, warning, pause, last_reset FROM maintenance_counters").fetchall()
                out = {}
                for r in rows:
                    out[r["name"]] = {
                        "count": r["count"],
                        "limit_val": r["limit_val"],
                        "warning": r["warning"] or "",
                        "pause": bool(r["pause"]),
                        "last_reset": r["last_reset"],
                    }
                return out
            finally:
                con.close()
        except sqlite3.OperationalError:
            return {}

def _refresh_maintenance_status():
    """Update status_cache with maintenance counter summary and active warnings."""
    counters = get_maintenance_counters()
    warnings = []
    for k, v in counters.items():
        if v["limit_val"] is not None and v["limit_val"] > 0 and v["count"] >= v["limit_val"]:
            warnings.append({
                "counter": k,
                "count": v["count"],
                "limit": v["limit_val"],
                "warning": v["warning"],
            })
    with status_lock:
        status_cache["maintenance"] = {
            "counters": counters,
            "warnings": warnings,
        }

def update_maintenance_counter(name, incr=0, limit_val=None, warning=None, pause=None, reset=False, delete=False):
    """Atomically update or mutate a maintenance counter and evaluate limits."""
    os.makedirs(os.path.dirname(_DB_PATH), exist_ok=True)
    with _db_lock:
        con = sqlite3.connect(_DB_PATH)
        con.row_factory = sqlite3.Row
        try:
            if delete:
                con.execute("DELETE FROM maintenance_counters WHERE name=?", (name,))
                con.commit()
                with _maintenance_lock:
                    _maintenance_paused_counters.discard(name)
                _refresh_maintenance_status()
                return {"deleted": True, "name": name}

            row = con.execute(
                "SELECT name, count, limit_val, warning, pause, last_reset FROM maintenance_counters WHERE name=?",
                (name,)
            ).fetchone()

            now_iso = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())

            if row is None:
                cur_count = 0
                cur_limit = int(limit_val) if limit_val is not None and limit_val != "" else None
                cur_warning = warning or ""
                cur_pause = 1 if pause else 0
                cur_reset = now_iso
                con.execute(
                    "INSERT INTO maintenance_counters (name, count, limit_val, warning, pause, last_reset) VALUES (?, ?, ?, ?, ?, ?)",
                    (name, cur_count, cur_limit, cur_warning, cur_pause, cur_reset)
                )
            else:
                cur_count = row["count"]
                cur_limit = row["limit_val"]
                cur_warning = row["warning"]
                cur_pause = row["pause"]
                cur_reset = row["last_reset"]

            if reset:
                cur_count = 0
                cur_reset = now_iso
                with _maintenance_lock:
                    _maintenance_paused_counters.discard(name)

            if incr:
                cur_count += incr

            if limit_val is not None:
                cur_limit = int(limit_val) if limit_val != "" else None
            if warning is not None:
                cur_warning = str(warning)
            if pause is not None:
                cur_pause = 1 if pause else 0

            con.execute(
                "UPDATE maintenance_counters SET count=?, limit_val=?, warning=?, pause=?, last_reset=? WHERE name=?",
                (cur_count, cur_limit, cur_warning, cur_pause, cur_reset, name)
            )
            con.commit()

            result = {
                "name": name,
                "count": cur_count,
                "limit_val": cur_limit,
                "warning": cur_warning,
                "pause": bool(cur_pause),
                "last_reset": cur_reset,
            }
        finally:
            con.close()

    # Threshold checking
    if result["limit_val"] is not None and result["limit_val"] > 0:
        if result["count"] >= result["limit_val"]:
            warn_text = result["warning"] or f"Maintenance counter '{name}' exceeded limit ({result['count']}/{result['limit_val']})"
            _emit_klipper_warning(warn_text)
            if result["pause"]:
                with _maintenance_lock:
                    should_pause = name not in _maintenance_paused_counters
                    if should_pause:
                        _maintenance_paused_counters.add(name)
                if should_pause:
                    _trigger_klipper_pause(warn_text)

    _refresh_maintenance_status()
    return result

# Endpoints / Spoolman config (set from CLI args in main())
MOONRAKER_URL = "http://localhost:7125"
SPOOLMAN_URL = "http://localhost:7912"
NUM_GATES = 2

# Spoolman spool detail cache (spool_id -> (timestamp, data))
_spool_cache = {}
_spool_cache_lock = threading.Lock()
SPOOL_CACHE_TTL = 30.0

def _get_piston_scale():
    with status_lock:
        val = status_cache.get("buf_max_travel")
    if val is not None and val > 0:
        return val / 2.0
    return 15.0

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
        "gate_filament_name": pad("gate_filament_name", lambda i: f"Gate {i}"),
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
        trigger_moonraker_lane_data_sync()
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
    vendor = fil.get("vendor", {}) or {}
    extra = spool.get("extra", {}) or {}
    return {
        "id": spool.get("id"),
        "name": fil.get("name") or spool.get("name"),
        "material": fil.get("material"),
        "color_hex": fil.get("color_hex"),
        "remaining_weight": spool.get("remaining_weight"),
        "remaining_length": spool.get("remaining_length"),
        "vendor_name": vendor.get("name") if isinstance(vendor, dict) else "",
        "bed_temp": fil.get("settings_bed_temp"),
        "nozzle_temp": fil.get("settings_extruder_temp"),
        "filament_id": fil.get("id"),
        "td": extra.get("transmission_distance", 0.0) if isinstance(extra, dict) else 0.0,
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
    trigger_moonraker_lane_data_sync()
    return data

# --- Moonraker DB lane_data synchronization ---
_lane_sync_queue = queue.Queue(maxsize=10)

def _moonraker_db_post_item(namespace, key, value):
    """Write an item to Moonraker's database component."""
    url = f"{MOONRAKER_URL}/server/database/item"
    payload = json.dumps({"namespace": namespace, "key": key, "value": value}).encode("utf-8")
    req = urllib.request.Request(url, data=payload, headers={"Content-Type": "application/json"}, method="POST")
    try:
        with urllib.request.urlopen(req, timeout=2.0) as resp:
            resp.read()
        return True
    except Exception:
        return False

def _moonraker_db_delete_item(namespace, key):
    """Delete an item from Moonraker's database component."""
    url = f"{MOONRAKER_URL}/server/database/item?namespace={urllib.parse.quote(namespace)}&key={urllib.parse.quote(key)}"
    req = urllib.request.Request(url, method="DELETE")
    try:
        with urllib.request.urlopen(req, timeout=2.0) as resp:
            resp.read()
        return True
    except Exception:
        return False

def _moonraker_db_get_namespace(namespace):
    """Retrieve all items in a Moonraker database namespace."""
    url = f"{MOONRAKER_URL}/server/database/item?namespace={urllib.parse.quote(namespace)}"
    try:
        with urllib.request.urlopen(url, timeout=2.0) as resp:
            data = json.loads(resp.read().decode("utf-8"))
        res = data.get("result", {})
        if isinstance(res, dict):
            val = res.get("value", {})
            if isinstance(val, dict):
                return val
        return {}
    except Exception:
        return {}

def build_moonraker_lane_payload(gate_idx):
    """Format lane_data entry for Moonraker DB in the shape OrcaSlicer expects."""
    gm = _read_gate_map()
    g = int(gate_idx)
    num_gates = gm.get("num_gates", NUM_GATES)
    if not (0 <= g < num_gates):
        return None

    sid = gm["gate_spool_id"][g] if g < len(gm["gate_spool_id"]) else -1
    spool = _spoolman_get_spool(sid) if isinstance(sid, int) and sid >= 0 else None

    default_name = gm["gate_name"][g] if g < len(gm["gate_name"]) else f"Gate {g}"
    default_mat = gm["gate_material"][g] if g < len(gm["gate_material"]) else ""
    default_color = gm["gate_color"][g] if g < len(gm["gate_color"]) else ""

    if spool and isinstance(spool, dict):
        vendor_name = spool.get("vendor_name") or ""
        name = spool.get("name") or default_name
        material = spool.get("material") or default_mat
        color = (spool.get("color_hex") or default_color).lstrip("#")[:6].upper()
        bed_temp = int(spool.get("bed_temp") or 0)
        nozzle_temp = int(spool.get("nozzle_temp") or 0)
        td = float(spool.get("td") or 0.0)
        spool_id = int(spool.get("id") or sid)
        filament_id = int(spool.get("filament_id") if spool.get("filament_id") is not None else -1)
    else:
        vendor_name = ""
        name = default_name
        material = default_mat
        color = default_color.lstrip("#")[:6].upper()
        bed_temp = 0
        nozzle_temp = 0
        td = 0.0
        spool_id = int(sid) if isinstance(sid, int) else -1
        filament_id = -1

    return {
        "vendor_name": vendor_name,
        "name": name,
        "color": color,
        "material": material,
        "bed_temp": bed_temp,
        "nozzle_temp": nozzle_temp,
        "scan_time": time.time(),
        "td": td,
        "lane": g,
        "spool_id": spool_id,
        "filament_id": filament_id,
    }

def sync_moonraker_lane_data():
    """Push lane_data for all active gates to Moonraker DB and prune orphans."""
    gm = _read_gate_map()
    num_gates = gm.get("num_gates", NUM_GATES)
    for g in range(num_gates):
        payload = build_moonraker_lane_payload(g)
        if payload:
            _moonraker_db_post_item("lane_data", f"lane{g}", payload)

    # Cleanup orphaned lanes
    existing = _moonraker_db_get_namespace("lane_data")
    if isinstance(existing, dict):
        for k in list(existing.keys()):
            if k.startswith("lane"):
                try:
                    idx = int(k[4:])
                    if idx >= num_gates:
                        _moonraker_db_delete_item("lane_data", k)
                except ValueError:
                    pass

def trigger_moonraker_lane_data_sync():
    """Queue a non-blocking lane_data synchronization."""
    try:
        _lane_sync_queue.put_nowait(True)
    except (queue.Full, Exception):
        pass

def _lane_sync_worker():
    while True:
        try:
            _lane_sync_queue.get()
            time.sleep(0.05)
            while not _lane_sync_queue.empty():
                try:
                    _lane_sync_queue.get_nowait()
                except queue.Empty:
                    break
            sync_moonraker_lane_data()
        except Exception as e:
            print(f"flare_daemon: lane_data sync worker error: {e}", file=sys.stderr)

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
    pushed = False if _g_host_busy else _push_gate_map_to_klipper(gate, fields)
    if not pushed:
        _write_gate_map_db(gate, fields)
    if fields.get("spool_id") is not None:
        with _spool_cache_lock:
            _spool_cache.pop(int(fields["spool_id"]), None)
    trigger_moonraker_lane_data_sync()
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
                    new_data["sync_feedback"] = max(-1.0, min(1.0, bp_val / _get_piston_scale()))
            elif key == "BST":
                val_int = int(val)
                new_data["buf_sensor_type"] = val_int
            elif key == "TMC":
                new_data["tmc_health"] = val
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
            elif key == "TT":
                new_data["tension_dwell_ms"] = int(val)
            elif key == "CT":
                new_data["compression_dwell_ms"] = int(val)
            elif key == "TB":
                new_data["tension_boost"] = bool(int(val))
        except ValueError:
            pass # ignore malformed metrics

    if new_data:
        new_data["raw_status"] = raw_fields
        new_data["board_online"] = True
        new_data["timestamp"] = time.time()
        with stats_lock:
            new_data["mmu_stats"] = dict(mmu_stats)
        with status_lock:
            keys_to_check = (
                "active_lane", "tc_state", "lane1_task", "lane2_task",
                "buf_sensor_type", "buf_state", "in1", "out1", "in2", "out2",
                "toolhead", "y_split", "reload_mode", "enable_cutter", "unload_cut",
                "tension_dwell_ms", "compression_dwell_ms", "sync_state", "sync_enabled"
            )
            field_changed = any(
                k in new_data and new_data[k] != status_cache.get(k)
                for k in keys_to_check
            ) or (not status_cache.get("board_online"))
            status_cache.update(new_data)

        if field_changed:
            trigger_klipper_sync()

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
                serial_port = serial.Serial(port_name, baud, timeout=1.0, exclusive=True)
                serial_port.reset_input_buffer()

            print(f"flare_daemon: connected to {port_name} successfully")
            with status_lock:
                status_cache["board_online"] = True
            trigger_klipper_sync()

            # Query BUF_MAX_TRAVEL immediately on connection
            try:
                serial_port.write(b"GET:BUF_MAX_TRAVEL\n")
                serial_port.flush()
            except Exception:
                pass

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
                        if parts[0] in ("TC", "CUT", "FAULT", "BL", "BUF_STAB", "SYNC", "RELOAD", "UNLOAD") and len(parts) >= 2:
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
                    trigger_klipper_sync()

                # Check for command reply
                elif line.startswith("OK:") or line.startswith("ER:") or line == "OK":
                    if "BUF_MAX_TRAVEL:" in line:
                        try:
                            parts = line.split(":")
                            val = float(parts[-1])
                            with status_lock:
                                status_cache["buf_max_travel"] = val
                            print(f"flare_daemon: cached buf_max_travel = {val} mm")
                        except Exception:
                            pass
                        # fall through to signal command_event so /cmd callers don't time out

                    # If it's a status dump response (starts with OK:LN: or OK:LN=)
                    if "LN:" in line:
                        parse_status_line(line)
                        if current_executing_command and not current_executing_command.startswith("?"):
                            continue

                    if current_executing_command and current_executing_command.strip().upper() == "GET:CRASHLOG":
                        if command_reply is None:
                            command_reply = line
                        else:
                            command_reply += "\n" + line
                        if line == "OK:CRASH:END" or line == "OK:NO_CRASH" or line.startswith("ER:"):
                            command_event.set()
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
            trigger_klipper_sync()

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

# CORS configuration
CORS_ALLOW_ALL = False
ALLOWED_CORS_ORIGINS = set()


def is_cors_origin_allowed(origin: str, request_host: str = None) -> bool:
    """Validate whether an HTTP Origin header is permitted to access API endpoints."""
    if not origin:
        return False
    if CORS_ALLOW_ALL:
        return True
    try:
        parsed = urllib.parse.urlparse(origin)
        hostname = (parsed.hostname or "").lower()
    except Exception:
        return False
    if not hostname:
        return False
    # Always allow local loopback
    if hostname in ("127.0.0.1", "localhost", "::1") or hostname.startswith("127."):
        return True
    # Allow same-origin matching request Host header
    if request_host:
        req_host_clean = request_host.split(":")[0].lower()
        if parsed.netloc.lower() == request_host.lower() or hostname == req_host_clean:
            return True
    # Explicit allowed origins
    origin_lower = origin.lower()
    if origin_lower in ALLOWED_CORS_ORIGINS or (parsed.netloc and parsed.netloc.lower() in ALLOWED_CORS_ORIGINS):
        return True
    for pattern in ALLOWED_CORS_ORIGINS:
        if pattern.endswith("*") and origin_lower.startswith(pattern[:-1].lower()):
            return True
    return False


# ---------------------------------------------------------------------------
# Remote Authentication & Rate Limiting
# ---------------------------------------------------------------------------
AUTH_TOKEN = None
AUTH_REQUIRED = False
TRUST_PROXY = False

RATE_LIMIT_BUCKETS = {}
RATE_LIMIT_LOCK = threading.Lock()
RATE_LIMIT_BURST = 20.0
RATE_LIMIT_RATE = 10.0  # requests per second replenishment


class TokenBucket:
    """Thread-safe per-client token bucket rate limiter."""

    def __init__(self, rate: float = 10.0, capacity: float = 20.0):
        self.rate = float(rate)
        self.capacity = float(capacity)
        self.tokens = float(capacity)
        self.last_update = time.monotonic()
        self.lock = threading.Lock()

    def consume(self, cost: float = 1.0) -> bool:
        with self.lock:
            now = time.monotonic()
            elapsed = max(0.0, now - self.last_update)
            self.last_update = now
            self.tokens = min(self.capacity, self.tokens + elapsed * self.rate)
            if self.tokens >= cost:
                self.tokens -= cost
                return True
            return False


def is_loopback(ip: str) -> bool:
    """Check if an IP string corresponds to loopback (127.0.0.1, ::1, localhost)."""
    if not ip:
        return False
    ip_clean = ip.strip().lower()
    if ip_clean in ("127.0.0.1", "::1", "localhost", "::ffff:127.0.0.1"):
        return True
    if ip_clean.startswith("127."):
        return True
    return False


def get_client_ip(handler: BaseHTTPRequestHandler) -> str:
    """Resolve client IP, honoring X-Forwarded-For only when TRUST_PROXY is enabled."""
    if TRUST_PROXY:
        forwarded = handler.headers.get("X-Forwarded-For")
        if forwarded:
            parts = [p.strip() for p in forwarded.split(",") if p.strip()]
            if parts:
                return parts[0]
    if handler.client_address and len(handler.client_address) > 0:
        return str(handler.client_address[0])
    return "127.0.0.1"


def check_rate_limit(ip: str) -> bool:
    """Check rate limit for client IP. Returns True if permitted, False if throttled."""
    if is_loopback(ip):
        return True
    now = time.monotonic()
    with RATE_LIMIT_LOCK:
        if len(RATE_LIMIT_BUCKETS) > 500:
            stale = [k for k, b in RATE_LIMIT_BUCKETS.items() if (now - b.last_update) > 120.0]
            for k in stale:
                del RATE_LIMIT_BUCKETS[k]
        bucket = RATE_LIMIT_BUCKETS.get(ip)
        if bucket is None:
            bucket = TokenBucket(rate=RATE_LIMIT_RATE, capacity=RATE_LIMIT_BURST)
            RATE_LIMIT_BUCKETS[ip] = bucket
        return bucket.consume()


def get_default_token_path() -> str:
    return os.path.expanduser("~/.flare/auth.token")


def init_auth_token(host: str, cli_token: str = None) -> str:
    """
    Initialize auth token based on CLI argument, environment, or ~/.flare/auth.token.
    If bound to non-loopback and no token configured, auto-generates 32-char hex token.
    """
    global AUTH_TOKEN, AUTH_REQUIRED
    # Behind a reverse proxy the socket peer is always the proxy (loopback), so
    # the bind host says nothing about who is calling: require auth whenever
    # X-Forwarded-For is trusted (12-SPEC §9.2).
    is_local_only = is_loopback(host) and not TRUST_PROXY

    token = cli_token or os.environ.get("FLARE_AUTH_TOKEN")
    token_path = get_default_token_path()
    if not token and os.path.exists(token_path):
        try:
            with open(token_path, encoding="utf-8") as f:
                token = f.read().strip()
        except Exception as e:
            print(f"flare_daemon warning: failed reading {token_path}: {e}", file=sys.stderr)

    if not is_local_only:
        AUTH_REQUIRED = True
        if not token:
            token = secrets.token_hex(16)
            try:
                os.makedirs(os.path.dirname(token_path), mode=0o700, exist_ok=True)
                fd = os.open(token_path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
                with open(fd, "w", encoding="utf-8") as f:
                    f.write(token + "\n")
                print(f"flare_daemon: auto-generated remote auth token in {token_path}")
            except Exception as e:
                print(f"flare_daemon warning: failed saving auth token to {token_path}: {e}", file=sys.stderr)
    else:
        AUTH_REQUIRED = False

    AUTH_TOKEN = token
    return AUTH_TOKEN


def is_request_authenticated(handler: BaseHTTPRequestHandler) -> bool:
    """Check whether caller is permitted to mutate state."""
    client_ip = get_client_ip(handler)
    if is_loopback(client_ip):
        return True
    if not AUTH_REQUIRED:
        return True
    if not AUTH_TOKEN:
        return False

    auth_hdr = handler.headers.get("Authorization", "")
    if auth_hdr.startswith("Bearer "):
        bearer_token = auth_hdr[7:].strip()
        return hmac.compare_digest(bearer_token, AUTH_TOKEN)
    return False


class FlareHTTPHandler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        # Suppress spammy log dumps for telemetry requests
        if args and isinstance(args[0], str) and ("GET /telemetry" in args[0] or "GET /status" in args[0]):
            return
        super().log_message(format, *args)

    def send_cors_headers(self):
        """Send Access-Control-Allow-Origin header if the request origin is allowed."""
        origin = self.headers.get("Origin")
        if origin and is_cors_origin_allowed(origin, self.headers.get("Host")):
            self.send_header("Access-Control-Allow-Origin", origin)
            self.send_header("Vary", "Origin")

    def do_GET(self):
        origin = self.headers.get("Origin")
        if origin and not is_cors_origin_allowed(origin, self.headers.get("Host")):
            self.send_error(403, "CORS origin forbidden")
            return

        if self.path == "/status":
            with status_lock:
                snapshot = dict(status_cache)
            with event_history_lock:
                snapshot["events"] = list(event_history)
            res = json.dumps(snapshot)
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_cors_headers()
            self.end_headers()
            self.wfile.write(res.encode("utf-8"))

        elif self.path == "/telemetry":
            # Server-Sent Events (SSE) Stream
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.send_header("Connection", "keep-alive")
            self.send_cors_headers()
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
            self.send_cors_headers()
            self.end_headers()
            self.wfile.write(json.dumps(data).encode("utf-8"))

        elif self.path == "/gatemap":
            res = json.dumps(build_gatemap_response())
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_cors_headers()
            self.end_headers()
            self.wfile.write(res.encode("utf-8"))

        elif self.path == "/maintenance":
            counters = get_maintenance_counters()
            res = json.dumps({"counters": counters})
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_cors_headers()
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
        origin = self.headers.get("Origin")
        if origin and not is_cors_origin_allowed(origin, self.headers.get("Host")):
            self.send_error(403, "CORS origin forbidden")
            return

        try:
            content_length = int(self.headers.get('Content-Length', 0))
        except (ValueError, TypeError):
            content_length = 0
        post_data = self.rfile.read(content_length) if content_length > 0 else b""

        client_ip = get_client_ip(self)
        if not is_request_authenticated(self):
            self.send_response(401)
            self.send_header("Content-Type", "application/json")
            self.send_header("WWW-Authenticate", 'Bearer realm="FLARE Daemon"')
            self.send_cors_headers()
            self.end_headers()
            self.wfile.write(json.dumps({"error": "unauthorized", "message": "Valid Bearer token required for remote mutations"}).encode("utf-8"))
            return

        if self.path == "/cmd":
            if not check_rate_limit(client_ip):
                self.send_response(429)
                self.send_header("Content-Type", "application/json")
                self.send_header("Retry-After", "1")
                self.send_cors_headers()
                self.end_headers()
                self.wfile.write(json.dumps({"error": "rate_limit_exceeded", "message": "Command rate limit exceeded"}).encode("utf-8"))
                return

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
                self.send_cors_headers()
                self.end_headers()
                self.wfile.write(json.dumps({"error": "command_timeout"}).encode("utf-8"))
            else:
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_cors_headers()
                self.end_headers()
                self.wfile.write(json.dumps({"response": response}).encode("utf-8"))

        elif self.path == "/gatemap":
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
            self.send_cors_headers()
            self.end_headers()
            self.wfile.write(json.dumps(resp).encode("utf-8"))

        elif self.path == "/config":
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
            trigger_klipper_sync()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_cors_headers()
            self.end_headers()
            self.wfile.write(json.dumps({"ok": True}).encode("utf-8"))

        elif self.path == "/maintenance":
            try:
                body = json.loads(post_data.decode("utf-8")) if post_data else {}
            except Exception:
                self.send_error(400, "Invalid JSON payload")
                return
            action = body.get("action", "update")
            name = body.get("name")
            if not name:
                self.send_error(400, "Missing 'name' in maintenance request")
                return
            if action == "reset":
                res = update_maintenance_counter(name, reset=True)
            elif action == "delete":
                res = update_maintenance_counter(name, delete=True)
            else:
                res = update_maintenance_counter(
                    name,
                    incr=int(body.get("incr", 0)),
                    limit_val=body.get("limit_val"),
                    warning=body.get("warning"),
                    pause=body.get("pause")
                )
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_cors_headers()
            self.end_headers()
            self.wfile.write(json.dumps({"result": "ok", "counter": res}).encode("utf-8"))

        else:
            self.send_error(404, "Not Found")

    def do_OPTIONS(self):
        # Support CORS pre-flight requests
        origin = self.headers.get("Origin")
        if origin and not is_cors_origin_allowed(origin, self.headers.get("Host")):
            self.send_error(403, "CORS origin forbidden")
            return
        self.send_response(200)
        self.send_cors_headers()
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type, Authorization")
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

_MMU_RECONCILE_STATUS_KEYS = {
    "NUM_GATES": "num_gates",
    "ACTIVE_GATE": "active_gate",
    "GATE": "gate",
    "TOOL": "tool",
    "ACTION": "action",
    "TC_STATE": "tc_state",
    "GATE_STATUS": "gate_status",
    "GATE_SENSOR": "gate_sensor",
    "TOOLHEAD_SENSOR": "toolhead_sensor",
    "SYNC_FEEDBACK": "sync_feedback",
    "SYNC_FEEDBACK_ENABLED": "sync_feedback_enabled",
    "SYNC_FEEDBACK_STATE": "sync_feedback_state",
    "PRINT_JOB_STATE": "print_job_state",
    "PRINT_STATE": "print_state",
    "BOARD_ONLINE": "board_online",
    "SPS": "sps",
    "RELOAD_MODE": "reload_mode",
    "ENABLE_CUTTER": "enable_cutter",
    "UNLOAD_CUT": "unload_cut",
    "BUF_SENSOR_TYPE": "buf_sensor_type",
    "GATE_SENSOR_ACTIVE": "gate_sensor_active",
    "EXTRUDER_SENSOR_ACTIVE": "extruder_sensor_active",
    "PRE_GATE_SENSOR_ACTIVE": "pre_gate_sensor_active",
    "HUB_SENSOR_ACTIVE": "hub_sensor_active",
    "SWAPS_TOTAL": "swaps_total",
    "SWAPS_SUCCESS": "swaps_success",
    "SWAPS_FAILED": "swaps_failed",
    "LOADS_SUCCESS": "loads_success",
    "UNLOADS_SUCCESS": "unloads_success",
    "MMU_LAST_ERROR": "last_error",
    "FEED_RATE": "board_feed_rate",
    "REV_RATE": "board_rev_rate",
    "BYPASS": "bypass",
    "GATE_MATERIAL": "gate_material",
    "GATE_COLOR": "gate_color",
    "GATE_SPOOL_ID": "gate_spool_id",
    "GATE_NAME": "gate_name",
    "GATE_FILAMENT_NAME": "gate_filament_name",
    # FlowGuard sub-dict: 2-tuples resolve against get_status()['flowguard'][...]
    # rather than a top-level scalar key (see _format_mmu_reconcile_value).
    "FLOWGUARD_ENABLED": ("flowguard", "enabled"),
    "FLOWGUARD_ACTIVE": ("flowguard", "active"),
    "FLOWGUARD_TRIGGER": ("flowguard", "trigger"),
    "FLOWGUARD_REASON": ("flowguard", "reason"),
    "FLOWGUARD_LEVEL": ("flowguard", "level"),
    "FLOWGUARD_MAX_CLOG": ("flowguard", "max_clog"),
    "FLOWGUARD_MAX_TANGLE": ("flowguard", "max_tangle"),
    "SYNC_DRIVE": "sync_drive",
    "IS_PAUSED": "is_paused",
    "REASON_FOR_PAUSE": "reason_for_pause",
    "BASELINE_SPS": "baseline_sps",
}

_MMU_RECONCILE_FLOATS = {
    "SYNC_FEEDBACK": 3,
    "SPS": 3,
    "FEED_RATE": 2,
    "REV_RATE": 2,
    "FLOWGUARD_LEVEL": 3,
    "FLOWGUARD_MAX_CLOG": 3,
    "FLOWGUARD_MAX_TANGLE": 3,
    "BASELINE_SPS": 3,
}

_MMU_RECONCILE_STRINGS = {
    "ACTION",
    "TC_STATE",
    "GATE_STATUS",
    "GATE_SENSOR",
    "SYNC_FEEDBACK_STATE",
    "PRINT_JOB_STATE",
    "PRINT_STATE",
    "MMU_LAST_ERROR",
    "GATE_MATERIAL",
    "GATE_COLOR",
    "GATE_SPOOL_ID",
    "GATE_NAME",
    "GATE_FILAMENT_NAME",
    "FLOWGUARD_TRIGGER",
    "FLOWGUARD_REASON",
    "REASON_FOR_PAUSE",
}

def _moonraker_get_mmu_status(moonraker_url):
    """Read Klipper's mmu object through Moonraker. Returns None if absent/offline."""
    try:
        url = f"{moonraker_url}/printer/objects/query?mmu"
        with urllib.request.urlopen(url, timeout=1.0) as resp:
            data = json.loads(resp.read().decode("utf-8"))
        mmu_status = data.get("result", {}).get("status", {}).get("mmu")
        return mmu_status if isinstance(mmu_status, dict) else None
    except Exception:
        return None

# idle_timeout.state values that mean the gcode lock is free.
IDLE_FREE_STATES = {"Idle", "Ready"}

def _moonraker_get_idle_state(moonraker_url):
    """Read idle_timeout.state via lock-free objects/query. Returns state string or None on error."""
    try:
        url = f"{moonraker_url}/printer/objects/query?idle_timeout"
        with urllib.request.urlopen(url, timeout=1.5) as resp:
            data = json.loads(resp.read().decode("utf-8"))
        return data.get("result", {}).get("status", {}).get("idle_timeout", {}).get("state")
    except Exception:
        return None

def _format_mmu_reconcile_value(key, status, desired_fields):
    """Return the SET_MMU-formatted value represented by a Moonraker mmu status."""
    status_key = _MMU_RECONCILE_STATUS_KEYS.get(key)
    if status_key is None:
        return None

    if isinstance(status_key, tuple):
        # Nested sub-dict (e.g. flowguard): resolve both levels, or bail if
        # either is missing/not a dict rather than raising.
        outer_key, inner_key = status_key
        nested = status.get(outer_key)
        if not isinstance(nested, dict) or inner_key not in nested:
            return None
        value = nested[inner_key]
    else:
        if status_key not in status:
            return None
        value = status[status_key]
        if key in ("ACTIVE_GATE", "GATE", "TOOL") and desired_fields.get("BYPASS") == "1":
            return desired_fields.get(key)  # cmd_SET_MMU stores -2 while command carries lane fields.
        elif key == "BYPASS":
            value = 1 if bool(value) else 0

    if key in _MMU_RECONCILE_FLOATS:
        return f"{float(value):.{_MMU_RECONCILE_FLOATS[key]}f}"
    if key in _MMU_RECONCILE_STRINGS:
        if isinstance(value, (list, tuple)):
            value = ",".join(str(x) for x in value)
        return f"'{str(value)}'"
    if isinstance(value, bool):
        return str(1 if value else 0)
    return str(value)

def _mmu_status_matches_fields(mmu_status, fields):
    """True when the Klipper mmu mock already stores the desired mirror fields."""
    if not isinstance(mmu_status, dict):
        return False
    for key, desired in fields.items():
        if _format_mmu_reconcile_value(key, mmu_status, fields) != desired:
            return False
    return True

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

# MANUAL.md documented default for SYNC_TENSION_STOP_MS. The live GET: is
# FLARE_DEV_TUNING-gated (RESEARCH.md Pitfall 3), so hardcode the default
# rather than query firmware for it.
_FLOWGUARD_TENSION_STOP_MS = 6000
# tune.h CONF_SYNC_AUTO_STOP_MS default.
_FLOWGUARD_COMPRESSION_STOP_MS = 5000


def _flowguard_level(sync_active, tension_dwell_ms, compression_dwell_ms,
                      tension_stop_ms=_FLOWGUARD_TENSION_STOP_MS,
                      compression_stop_ms=_FLOWGUARD_COMPRESSION_STOP_MS):
    """Derive a Happy-Hare-shaped FlowGuard (level, trigger) from FLARE's
    existing TT:/CT: dwell-timer telemetry -- no firmware change needed.

    Sign convention matches HH's mmu_sync_controller.py:807-905: positive =
    compression/"clog" side, negative = tension/"tangle" side. `trigger` is ""
    until the corresponding dwell timer actually saturates its stop threshold
    (i.e. the fault is imminent, not merely non-zero). Sync inactive always
    forces (0.0, "") regardless of dwell state (Success Criterion 1).
    """
    if not sync_active:
        return 0.0, ""

    tension_frac = min(1.0, tension_dwell_ms / tension_stop_ms) if tension_stop_ms > 0 else 0.0
    compression_frac = min(1.0, compression_dwell_ms / compression_stop_ms) if compression_stop_ms > 0 else 0.0

    if tension_frac >= compression_frac:
        level = -tension_frac
        trigger = "tangle" if tension_frac >= 1.0 else ""
    else:
        level = compression_frac
        trigger = "clog" if compression_frac >= 1.0 else ""
    return level, trigger


# firmware/include/sync.h sync_state_t: 5th enum member (0-indexed 4) = SYNC_FAULT_HOLD.
_SYNC_STATE_FAULT_HOLD = 4

# Decay window for a recently-seen EV:PRELOAD event to still surface as the
# "Preload" action string (14-RESEARCH.md "EV: events -> HH action strings").
_ACTION_PRELOAD_DECAY_S = 2.0


def _recent_event_seen(event_type, within_s=2.0):
    """True if an event_history entry of event_type landed within the last
    within_s seconds. event_history is chronological (oldest first) and capped
    at 100 entries, so scanning in reverse and breaking on the first
    too-old entry keeps this bounded and cheap (T-14-05)."""
    now = time.time()
    with event_history_lock:
        for entry in reversed(event_history):
            if now - entry["time"] > within_s:
                break
            if entry["type"] == event_type:
                return True
    return False


def _derive_action(tc_state, active_lane, lane1_task, lane2_task, recent_preload_event=False):
    """Map FLARE toolchange/lane-task state to a Happy Hare action string so
    Fluidd shows 'Loading: X mm' / 'Unloading: X mm' during operations.

    Grounded subset only (14-RESEARCH.md "EV: events -> HH action strings",
    Open Question 1, RESOLVED): "Forming Tip", "Heating", "Selecting",
    "Checking", "Homing", and "Purging" have no live FLARE daemon signal --
    no physical selector/heater/encoder, and tip-forming/purging happen
    entirely inside Klipper macros the daemon never observes -- so they
    intentionally fall through to the default "Idle" branch below rather
    than fabricate a trigger condition for them.
    """
    ts = (tc_state or "").upper()
    if ts in ("UNLOAD_CUT", "UNLOAD_WAIT_CUT"):
        return "Cutting Filament"
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
    if recent_preload_event:
        return "Preload"
    return "Idle"

def _syncer_wait_time(now, host_busy, next_idle_probe, backoff, last_force_sync, retry_at):
    """Sleep budget for one klipper_syncer pass, and the (possibly cleared) retry deadline.

    Priority: host-busy probe throttle, offline backoff, else the 10 s reconcile —
    but a pending push retry (set when Klipper was Printing) always shortens the
    wait so the mirror recovers within ~KLIPPER_PUSH_RETRY_S, not 10 s."""
    if host_busy:
        wait_time = max(0.1, next_idle_probe - now)
    elif backoff > now:
        wait_time = max(0.1, backoff - now)
    else:
        wait_time = max(0.1, (last_force_sync + 10.0) - now)
    if retry_at > now:
        wait_time = min(wait_time, max(0.1, retry_at - now))
    elif retry_at:
        retry_at = 0.0
        wait_time = 0.1
    return wait_time, retry_at


def klipper_syncer(moonraker_url):
    """Background thread to push status updates to Moonraker event-driven."""
    global _g_host_busy
    last_sync = {}
    backoff = 0.0
    last_force_sync = 0.0
    was_online = False
    last_loaded_gate = None
    last_active_spool = object()  # sentinel distinct from None / any spool id
    last_pushed_fields = {}  # KEY -> last-pushed formatted value, for delta SET_MMU
    flowguard_max_clog = 0.0  # FlowGuard high-water mark, tension/compression sides
    flowguard_max_tangle = 0.0
    last_gate_dbg = None  # FLARE_GATE_DEBUG: last logged gate-input tuple
    gate_debug = bool(os.environ.get("FLARE_GATE_DEBUG"))
    host_busy = False   # gcode lock held by blocking command (e.g. MPC_CALIBRATE)
    next_idle_probe = 0.0  # throttle: earliest time to probe idle_timeout again
    retry_at = 0.0  # a push skipped while Printing is retried at this time, not the 10 s reconcile

    while True:
        # Event-driven wait: wake on firmware event, state change, or periodic reconcile (10s)
        now = time.time()
        wait_time, retry_at = _syncer_wait_time(now, host_busy, next_idle_probe, backoff,
                                                last_force_sync, retry_at)

        klipper_sync_event.wait(timeout=wait_time)
        klipper_sync_event.clear()

        # Check backoff timer
        if backoff > time.time():
            continue

        # Short debounce dwell (50ms) to coalesce rapid event bursts into a single delta push
        time.sleep(0.05)

        # Busy mode: gcode lock held — suppress all gcode/script emitters and
        # throttle-poll idle_timeout until lock is free before resuming.
        if host_busy:
            if time.time() >= next_idle_probe:
                next_idle_probe = time.time() + 1.5
                idle_state = _moonraker_get_idle_state(moonraker_url)
                if idle_state is not None and idle_state in IDLE_FREE_STATES:
                    host_busy = False
                    _g_host_busy = False
                    last_pushed_fields = {}  # force full resync on resume
            continue

        # Get copy of current cache
        with status_lock:
            state = dict(status_cache)

        # Detect changes in values of interest (excluding high-frequency float noise)
        keys = [
            "board_online", "active_lane", "tc_state",
            "buf_state", "in1", "out1", "in2", "out2",
            "toolhead", "y_split", "reload_mode", "enable_cutter", "unload_cut",
            "tension_dwell_ms", "compression_dwell_ms", "sync_state", "sync_enabled"
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

        reconcile_due = time.time() - last_force_sync > 10.0

        # Full sync = send every SET_MMU field (not just the delta).
        force_full = False

        board_online = state.get("board_online", False)
        trigger_board_sync = False
        if board_online and not was_online:
            trigger_board_sync = True
            changed = True
            force_full = True

        if not last_pushed_fields:
            force_full = True  # first push after start / recovery

        if not changed and not reconcile_due:
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
        stype = state.get("buf_sensor_type", 0)
        sync_feedback_enabled = 1
        buf_state = state.get("buf_state", "NEUTRAL").lower()
        if buf_state in ["+", "tension"]:
            buf_state = "tension"
            cosmetic_piston = 1.0
        elif buf_state in ["-", "compression", "compressed"]:
            buf_state = "compressed"
            cosmetic_piston = -1.0
        else:
            buf_state = "neutral"
            cosmetic_piston = 0.0
        tc_state = state.get("tc_state", "UNKNOWN")
        board_online = 1 if state.get("board_online", False) else 0
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
        if in1 and out1 and y_split and toolhead and active_gate == 0:
            gate_status_1 = 2
        elif in1:
            gate_status_1 = 1
        else:
            gate_status_1 = 0

        if in2 and out2 and y_split and toolhead and active_gate == 1:
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

        # "Preload" is a fallback-from-Idle signal only: a live tc_state/task
        # branch above always wins over a stale recent-PRELOAD flag.
        recent_preload = _recent_event_seen("PRELOAD", within_s=_ACTION_PRELOAD_DECAY_S)
        action = _derive_action(tc_state, active_lane,
                                state.get("lane1_task", "IDLE"),
                                state.get("lane2_task", "IDLE"),
                                recent_preload_event=recent_preload)

        bypass = bool(state.get("bypass", False))

        # FlowGuard: derive HH-shaped fault-approach telemetry from the
        # dwell-timer fields already parsed off TT:/CT: -- no firmware change.
        sync_active = bool(state.get("sync_drive", False))
        tension_dwell_ms = state.get("tension_dwell_ms", 0)
        compression_dwell_ms = state.get("compression_dwell_ms", 0)
        flowguard_level, flowguard_trigger = _flowguard_level(
            sync_active, tension_dwell_ms, compression_dwell_ms)
        flowguard_active = sync_active and flowguard_level != 0.0
        flowguard_enabled = sync_feedback_enabled
        if flowguard_trigger == "tangle":
            flowguard_reason = "Tension dwell approaching trip"
        elif flowguard_trigger == "clog":
            flowguard_reason = "Compression dwell approaching trip"
        else:
            flowguard_reason = ""
        if not sync_active:
            flowguard_max_clog = 0.0
            flowguard_max_tangle = 0.0
        elif flowguard_level > 0:
            flowguard_max_clog = max(flowguard_max_clog, flowguard_level)
        elif flowguard_level < 0:
            flowguard_max_tangle = min(flowguard_max_tangle, flowguard_level)

        # Daemon-mirrored keys derived from existing status_cache fault/sync
        # signals -- no new firmware state (RESEARCH.md Open Question 2).
        sync_state_val = state.get("sync_state", 0)
        is_paused = 1 if sync_state_val == _SYNC_STATE_FAULT_HOLD else 0
        reason_for_pause = f"'{st['last_error']}'" if is_paused else "''"
        sync_drive_val = 1 if state.get("sync_drive", False) else 0
        baseline_sps = state.get("baseline_sps", 0.0)

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
            # Latched/dampened cosmetic buffer piston position (-1.0 to 1.0):
            # Monotonically reflects discrete buffer state (neutral: 0.0, tension: 1.0,
            # compression: -1.0) to prevent Fluidd/Mainsail animation thrash while still
            # driving the UI piston graphic.
            "SYNC_FEEDBACK": f"{cosmetic_piston:.3f}",
            "SYNC_FEEDBACK_ENABLED": str(sync_feedback_enabled),
            "SYNC_FEEDBACK_STATE": f"'{buf_state}'",
            "PRINT_JOB_STATE": f"'{print_job_state}'",
            "PRINT_STATE": f"'{print_state}'",
            "BOARD_ONLINE": str(board_online),
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
            "BYPASS": str(1 if bypass else 0),
            "FLOWGUARD_ENABLED": str(1 if flowguard_enabled else 0),
            "FLOWGUARD_ACTIVE": str(1 if flowguard_active else 0),
            "FLOWGUARD_TRIGGER": f"'{flowguard_trigger}'",
            "FLOWGUARD_REASON": f"'{flowguard_reason}'",
            "FLOWGUARD_LEVEL": f"{flowguard_level:.3f}",
            "FLOWGUARD_MAX_CLOG": f"{flowguard_max_clog:.3f}",
            "FLOWGUARD_MAX_TANGLE": f"{flowguard_max_tangle:.3f}",
            "SYNC_DRIVE": str(sync_drive_val),
            "IS_PAUSED": str(is_paused),
            "REASON_FOR_PAUSE": reason_for_pause,
            "BASELINE_SPS": f"{baseline_sps:.3f}",
        }

        # Periodic restart recovery is a silent reconcile: read Klipper's mmu
        # object and only emit a full SET_MMU when the mock diverged or vanished.
        if reconcile_due:
            last_force_sync = time.time()
            mmu_status = _moonraker_get_mmu_status(moonraker_url)
            check_fields = dict(fields)
            gm = _read_gate_map()
            check_fields["GATE_MATERIAL"] = f"'{','.join(gm['gate_material'])}'"
            check_fields["GATE_COLOR"] = f"'{','.join(gm['gate_color'])}'"
            check_fields["GATE_SPOOL_ID"] = f"'{','.join(str(x) for x in gm['gate_spool_id'])}'"
            check_fields["GATE_NAME"] = f"'{','.join(gm['gate_name'])}'"
            check_fields["GATE_FILAMENT_NAME"] = f"'{','.join(gm['gate_filament_name'])}'"
            if not _mmu_status_matches_fields(mmu_status, check_fields):
                changed = True
                force_full = True

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
            delta = dict(fields)
            gm = _read_gate_map()
            delta["GATE_MATERIAL"] = f"'{','.join(gm['gate_material'])}'"
            delta["GATE_COLOR"] = f"'{','.join(gm['gate_color'])}'"
            delta["GATE_SPOOL_ID"] = f"'{','.join(str(x) for x in gm['gate_spool_id'])}'"
            delta["GATE_NAME"] = f"'{','.join(gm['gate_name'])}'"
            delta["GATE_FILAMENT_NAME"] = f"'{','.join(gm['gate_filament_name'])}'"
        else:
            delta = {k: v for k, v in fields.items() if last_pushed_fields.get(k) != v}

        if not delta and not trigger_board_sync:
            continue

        if delta:
            lines.append("SET_MMU " + " ".join(f"{k}={v}" for k, v in delta.items()))

        if trigger_board_sync and not host_busy:
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
            idle_state = _moonraker_get_idle_state(moonraker_url)
            if idle_state is None:
                # Moonraker unreachable — offline backoff
                last_sync = {}
                last_pushed_fields = {}
                backoff = time.time() + 5.0
            elif idle_state == "Printing":
                # Gcode lock held by a TC or print macro — skip this push and retry
                # shortly (daemon-klipper-mirror: recover within one tick, not the
                # 10 s reconcile — _FLARE_CHANGE_LANE reads printer.mmu.gate_status at
                # render time right after the macro releases the lock). Do NOT enter
                # host_busy: printing is normal and gate_status must keep flowing.
                retry_at = time.time() + KLIPPER_PUSH_RETRY_S
            elif idle_state not in IDLE_FREE_STATES:
                # Gcode lock busy during non-print (e.g. MPC_CALIBRATE) — suppress
                host_busy = True
                _g_host_busy = True
                next_idle_probe = time.time() + 1.5
            # else: transient timeout with idle host — resume normally next tick

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
    if serial is None:
        print("flare_daemon error: 'pyserial' not installed. Run: pip install pyserial", file=sys.stderr)
        sys.exit(1)
    parser = argparse.ArgumentParser(description="FLARE persistent host proxy daemon")
    parser.add_argument("--port", help="Serial port connection path (e.g. /dev/ttyACM0)")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate (default: 115200)")
    parser.add_argument("--host", default="0.0.0.0", help="HTTP server bind host (default: 0.0.0.0 - LAN dashboard reachable, remote mutations need the Bearer token; pass 127.0.0.1 for loopback only)")
    parser.add_argument("--api-port", type=int, default=8088, help="HTTP/SSE API server port (default: 8088)")
    parser.add_argument("--cors-origins", default="", help="Allowed CORS origins (default: loopback/same-host only; pass '*' or comma-separated origins)")
    parser.add_argument("--no-klipper", action="store_true", help="Bypass Moonraker/Klipper telemetry synchronization")
    parser.add_argument("--moonraker-url", default="http://localhost:7125", help="Moonraker base URL (default: http://localhost:7125)")
    parser.add_argument("--spoolman-url", default="http://localhost:7912", help="Spoolman base URL for direct API fallback (default: http://localhost:7912)")
    parser.add_argument("--auth-token", help="Bearer authentication token for remote mutating endpoints (auto-generated if omitted on non-loopback)")
    parser.add_argument("--trust-proxy", action="store_true", help="Trust X-Forwarded-For header when behind reverse proxy (default: evaluate direct peer IP only)")
    args = parser.parse_args()

    global MOONRAKER_URL, SPOOLMAN_URL, CORS_ALLOW_ALL, ALLOWED_CORS_ORIGINS, TRUST_PROXY
    MOONRAKER_URL = args.moonraker_url
    SPOOLMAN_URL = args.spoolman_url
    TRUST_PROXY = args.trust_proxy
    if args.cors_origins == "*":
        CORS_ALLOW_ALL = True
    elif args.cors_origins:
        ALLOWED_CORS_ORIGINS = {o.strip().lower() for o in args.cors_origins.split(",") if o.strip()}

    init_auth_token(args.host, args.auth_token)
    if AUTH_REQUIRED and AUTH_TOKEN:
        print("flare_daemon: remote access authentication enabled (Bearer token enforced for /cmd, /config, /gatemap)")
    elif not AUTH_REQUIRED:
        print("flare_daemon: loopback-only mode; authentication enforcement disabled")

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
        lane_worker_t = threading.Thread(target=_lane_sync_worker, daemon=True)
        lane_worker_t.start()
        trigger_moonraker_lane_data_sync()

    # 4. Start HTTP & SSE proxy web server
    if not is_loopback(args.host) and not AUTH_TOKEN:
        print("WARNING: HTTP server bound to non-loopback without active auth token!", file=sys.stderr)
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
