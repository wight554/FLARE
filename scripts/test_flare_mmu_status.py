#!/usr/bin/env python3
"""Offline logic checks for the FLARE Klipper MMU mock (klipper/mmu.py).

Exercises get_status() synthesis and _update_phase() with a fake printer — no
hardware, daemon, Moonraker, or filament feed. Validates the shipped behavior:
  #2  RELOAD/preload re-stage must NOT start a phantom load count-up
  #3  cut unload holds filament_position at 0
  #4  in-bowden load emits filament_pos=IN_BOWDEN(3) + bowden_progress (tip glide)
  bypass: omit filament_compression/tension sensors (hide piston), bowden_progress=-1

Run: python3 scripts/test_flare_mmu_status.py   (exit 0 = all pass)
"""
import os
import sys
import types

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "klipper"))
import mmu  # noqa: E402

sys.modules.setdefault("serial", types.SimpleNamespace())  # flare_daemon import guard
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import flare_daemon  # noqa: E402


class FakeReactor:
    def __init__(self):
        self.t = 1000.0

    def monotonic(self):
        return self.t


class FakeMacro:
    def __init__(self, variables):
        self.variables = variables


class FakeGcode:
    """Permissive stub: every method (respond_info, register_command,
    run_script_from_command, ...) is a silent no-op."""
    def __getattr__(self, _name):
        return lambda *a, **k: None


class FakePrinter:
    def __init__(self, varmacro):
        self._objs = {}
        self._reactor = FakeReactor()
        self._var = varmacro
        self._gcode = FakeGcode()

    def add_object(self, name, obj):
        self._objs[name] = obj

    def register_event_handler(self, *a, **k):
        pass

    def get_reactor(self):
        return self._reactor

    def lookup_object(self, name, default=None):
        if name == "gcode_macro _FLARE_VARS":
            return self._var
        if name == "gcode":
            return self._gcode
        return default          # toolhead sensor lookup -> None -> treated as clear


class FakeConfig:
    def __init__(self, printer):
        self._p = printer

    def get_printer(self):
        return self._p

    def get_name(self):
        return "mmu"


class FakeGcmd:
    """Minimal gcmd for driving cmd_SET_MMU: absent params return the default."""
    def __init__(self, params):
        self.p = params

    def get_int(self, key, default=None):
        v = self.p.get(key)
        return int(v) if v is not None else default

    def get_float(self, key, default=None):
        v = self.p.get(key)
        return float(v) if v is not None else default

    def get(self, key, default=None):
        v = self.p.get(key)
        return v if v is not None else default

    def respond_info(self, *a, **k):
        pass

    def respond_raw(self, *a, **k):
        pass

    def error(self, *a, **k):
        return Exception(*(a or ("error",)))


def new_mock():
    """Fresh MMUMock with hardware-like bowden geometry (path_len = 1925)."""
    var = FakeMacro({"bowden_length": 1808.0, "extruder_to_nozzle": 117.0})
    printer = FakePrinter(var)
    return mmu.MMUMock(FakeConfig(printer)), printer


_PASS = 0
_FAIL = 0


def check(name, cond, detail=""):
    global _PASS, _FAIL
    if cond:
        _PASS += 1
        print(f"  PASS  {name}")
    else:
        _FAIL += 1
        print(f"  FAIL  {name}   got={detail!r}")


def run_tests():
    global _PASS, _FAIL
    print("#4 — in-bowden load animates the tip (filament_pos=3 + bowden_progress)")
    m, p = new_mock()
    m.is_loading = True
    m.current_phase = "load"
    m.load_phase_start = 0.0
    m.loading_speed = 50.0
    m.gate_sensor_active = 1
    p._reactor.t = 10.0                      # elapsed 10s * 50 mm/s = 500 mm
    s = m.get_status(0)
    check("load mm ~= 500", abs(s["filament_position"] - 500.0) < 1.0, s["filament_position"])
    check("filament_pos == 3 (IN_BOWDEN)", s["filament_pos"] == 3, s["filament_pos"])
    check("bowden_progress ~= 27.7", 25.0 < s["bowden_progress"] < 30.0, s["bowden_progress"])
    check("piston sensors present (not bypass)", "filament_compression" in s["sensors"], s["sensors"])

    m, p = new_mock()
    m.is_loading = True
    m.current_phase = "load"
    m.load_phase_start = 0.0
    m.loading_speed = 50.0
    m.gate_sensor_active = 1
    p._reactor.t = 40.0                      # 2000 mm -> clamps to path_len 1925
    s = m.get_status(0)
    check("near-end clamps to 1925", abs(s["filament_position"] - 1925.0) < 1.0, s["filament_position"])
    check("filament_pos == 10 (LOADED) at end", s["filament_pos"] == 10, s["filament_pos"])

    print("#3 — cut unload holds 0 (no 1800 clamp)")
    m, p = new_mock()
    m.current_phase = "unload"
    m.enable_cutter = 1
    m.unload_cut = 1
    m.gate_sensor_active = 1                  # path_gear True (not the trivial 'gear clear' branch)
    m.toolhead_sensor = 1                     # toolhead still set -> would otherwise clamp to 1800
    m.unload_phase_start = 0.0
    p._reactor.t = 1.0
    s = m.get_status(0)
    check("cut unload holds 0", s["filament_position"] == 0.0, s["filament_position"])
    check("unload_completed latched True", m.unload_completed is True, m.unload_completed)

    m, p = new_mock()                         # contrast: non-cut unload counts down
    m.current_phase = "unload"
    m.enable_cutter = 0
    m.unload_cut = 0
    m.gate_sensor_active = 1
    m.toolhead_sensor = 0                     # toolhead cleared -> countdown from bowden
    m.unload_phase_start = 0.0
    m.th_clear_time = 0.0
    m.loading_speed = 50.0
    p._reactor.t = 5.0                        # 1808 - 250 = 1558
    s = m.get_status(0)
    check("non-cut unload counts down (0 < mm < 1808)", 0.0 < s["filament_position"] < 1808.0, s["filament_position"])

    print("#2 — RELOAD/preload re-stage must not start a phantom load")
    m, p = new_mock()
    m.current_phase = "unload"
    m.unload_completed = True
    m.is_loading = False
    m.is_unloading = False
    m.gate_sensor_active = 1                   # reload re-stages to gate (OUT high) -> still not a load
    m._update_phase("RELOAD_APPROACH", "Loading", 100.0)
    check("RELOAD does NOT enter load", m.current_phase != "load", m.current_phase)

    m, p = new_mock()                          # contrast: genuine toolhead load enters load
    m.current_phase = "idle"
    m.gate_sensor_active = 1
    m.toolhead_sensor = 0
    m._update_phase("LOAD_START", "Loading", 100.0)
    check("LOAD_START enters load", m.current_phase == "load", m.current_phase)

    print("bypass — hide buffer piston, no bowden interpolation")
    m, p = new_mock()
    m.bypass = True
    m.toolhead_sensor = 1
    s = m.get_status(0)
    check("no filament_compression sensor", "filament_compression" not in s["sensors"], s["sensors"])
    check("no filament_tension sensor", "filament_tension" not in s["sensors"], s["sensors"])
    check("toolhead sensor still present", "toolhead" in s["sensors"], s["sensors"])
    check("bowden_progress == -1", s["bowden_progress"] == -1.0, s["bowden_progress"])
    check("filament_pos == 10 (loaded)", s["filament_pos"] == 10, s["filament_pos"])

    print("bypass persistence — daemon BYPASS push survives a Klipper restart")
    m, p = new_mock()                              # fresh mock == post-restart state
    check("fresh mock defaults bypass False", m.bypass is False, m.bypass)
    m.cmd_SET_MMU(FakeGcmd({"BYPASS": 1, "TOOLHEAD_SENSOR": 1}))
    check("daemon BYPASS=1 restores bypass", m.bypass is True, m.bypass)
    m.cmd_SET_MMU(FakeGcmd({"BYPASS": 0}))
    check("daemon BYPASS=0 clears bypass", m.bypass is False, m.bypass)
    m.bypass = True
    m.cmd_SET_MMU(FakeGcmd({}))                     # no BYPASS field -> leave as-is
    check("absent BYPASS leaves bypass unchanged", m.bypass is True, m.bypass)

    print("proportional buffer — check filament_proportional sensor mapping")
    m, p = new_mock()
    check("fresh mock defaults buf_sensor_type to 0", m.buf_sensor_type == 0, m.buf_sensor_type)
    s = m.get_status(0)
    check("buf_sensor_type 0 exposes filament_tension/compression", "filament_tension" in s["sensors"] and "filament_compression" in s["sensors"], s["sensors"])
    check("buf_sensor_type 0 does NOT expose filament_proportional", "filament_proportional" not in s["sensors"], s["sensors"])

    m.cmd_SET_MMU(FakeGcmd({"BUF_SENSOR_TYPE": 1}))
    check("cmd_SET_MMU sets buf_sensor_type to 1", m.buf_sensor_type == 1, m.buf_sensor_type)
    s = m.get_status(0)
    check("buf_sensor_type 1 exposes filament_proportional", s["sensors"].get("filament_proportional") is True, s["sensors"])
    check("buf_sensor_type 1 does NOT expose filament_tension/compression", "filament_tension" not in s["sensors"] and "filament_compression" not in s["sensors"], s["sensors"])

    print("daemon reconcile — compare Moonraker mmu status to SET_MMU formatting")
    fields = {
        "NUM_GATES": "2",
        "ACTIVE_GATE": "0",
        "GATE": "0",
        "TOOL": "0",
        "ACTION": "'Loading'",
        "TC_STATE": "'LOAD_START'",
        "GATE_STATUS": "'1,0'",
        "GATE_SENSOR": "'1,0'",
        "TOOLHEAD_SENSOR": "1",
        "SYNC_FEEDBACK": "0.400",
        "SYNC_FEEDBACK_ENABLED": "1",
        "SYNC_FEEDBACK_STATE": "'tension'",
        "PRINT_JOB_STATE": "'printing'",
        "PRINT_STATE": "'printing'",
        "BOARD_ONLINE": "1",
        "SPS": "12.345",
        "RELOAD_MODE": "1",
        "ENABLE_CUTTER": "1",
        "UNLOAD_CUT": "0",
        "BUF_SENSOR_TYPE": "1",
        "GATE_SENSOR_ACTIVE": "1",
        "EXTRUDER_SENSOR_ACTIVE": "1",
        "PRE_GATE_SENSOR_ACTIVE": "1",
        "HUB_SENSOR_ACTIVE": "1",
        "SWAPS_TOTAL": "3",
        "SWAPS_SUCCESS": "2",
        "SWAPS_FAILED": "1",
        "LOADS_SUCCESS": "4",
        "UNLOADS_SUCCESS": "5",
        "MMU_LAST_ERROR": "'None'",
        "FEED_RATE": "50.25",
        "REV_RATE": "40.50",
        "BYPASS": "0",
    }
    m, p = new_mock()
    m.cmd_SET_MMU(FakeGcmd(fields))
    status = m.get_status(0)
    check("reconcile matches full field set",
          flare_daemon._mmu_status_matches_fields(status, fields), status)
    drifted = dict(status)
    drifted["sync_feedback"] = 0.5
    check("reconcile detects float drift",
          not flare_daemon._mmu_status_matches_fields(drifted, fields), drifted["sync_feedback"])
    bypass_fields = dict(fields)
    bypass_fields.update({"BYPASS": "1", "ACTIVE_GATE": "0", "GATE": "0", "TOOL": "0"})
    m, p = new_mock()
    m.cmd_SET_MMU(FakeGcmd(bypass_fields))
    status = m.get_status(0)
    check("reconcile accepts bypass gate/tool sentinels",
          flare_daemon._mmu_status_matches_fields(status, bypass_fields),
          (status["active_gate"], status["gate"], status["tool"]))

    print(f"\n{_PASS} passed, {_FAIL} failed")
    sys.exit(1 if _FAIL else 0)

if __name__ == "__main__":
    run_tests()
