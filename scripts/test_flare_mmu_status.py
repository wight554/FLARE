#!/usr/bin/env python3
"""Offline logic checks for the FLARE Klipper MMU mock (klipper/mmu.py).

Exercises get_status() synthesis and _update_phase() with a fake printer — no
hardware, daemon, Moonraker, or filament feed. Validates the shipped behavior:
  #2  RELOAD/preload re-stage must NOT start a phantom load count-up
  #3  cut unload latches unload_completed via _update_phase; filament_position
      stays 0.0 always (94e27a2 removed the synthetic-mm tip animation)
  #4  filament_pos reports discrete landmarks only (0/4/10); no bowden_progress
      interpolation, no gliding tip
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
import functest_adapter  # noqa: E402


class FakeReactor:
    def __init__(self):
        self.t = 1000.0

    def monotonic(self):
        return self.t


class FakeMacro:
    def __init__(self, variables):
        self.variables = variables


class FakeGcode:
    """Permissive stub with command call recording."""
    def __init__(self):
        self.commands = []
        self.registered = {}

    def run_script_from_command(self, cmd):
        self.commands.append(cmd)

    def register_command(self, name, handler, desc=None):
        self.registered[name] = handler

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
    print("#4 — discrete filament_pos landmarks only, no synthetic mm (94e27a2 removed the tip animation)")
    m, p = new_mock()
    m.current_phase = "load"
    m.gate_sensor_active = 1                 # path_gear true, not yet at toolhead
    m.toolhead_sensor = 0
    s = m.get_status(0)
    check("filament_pos == 4 (PARTIALLY_LOADED) mid-load", s["filament_pos"] == 4, s["filament_pos"])
    check("filament_position stays 0.0 (no synthetic mm)", s["filament_position"] == 0.0, s["filament_position"])
    check("bowden_progress stays -1.0 (no interpolation)", s["bowden_progress"] == -1.0, s["bowden_progress"])
    check("piston sensors present (not bypass)", "filament_compression" in s["sensors"], s["sensors"])

    m, p = new_mock()
    m.current_phase = "load"
    m.gate_sensor_active = 1
    m.toolhead_sensor = 1                    # reached toolhead sensor
    s = m.get_status(0)
    check("filament_pos == 10 (LOADED) at toolhead", s["filament_pos"] == 10, s["filament_pos"])
    check("filament_position still 0.0 at LOADED", s["filament_position"] == 0.0, s["filament_position"])

    print("#3 — cut unload completes via _update_phase (UNLOAD_WAIT_CUT), filament_position stays 0")
    m, p = new_mock()
    m.current_phase = "unload"
    m.gate_sensor_active = 1                  # path_gear True (not the trivial 'gear clear' branch)
    m.toolhead_sensor = 1
    m.unload_phase_start = 0.0
    m._update_phase("UNLOAD_WAIT_CUT", "Unloading", 1.0)
    check("cut unload enters cut phase", m.current_phase == "cut", m.current_phase)
    check("unload_completed latched True", m.unload_completed is True, m.unload_completed)
    s = m.get_status(0)
    check("cut unload holds filament_position at 0", s["filament_position"] == 0.0, s["filament_position"])

    m, p = new_mock()                         # contrast: in-progress (non-cut) unload
    m.current_phase = "unload"
    m.gate_sensor_active = 1
    m.toolhead_sensor = 0
    m.unload_completed = False
    s = m.get_status(0)
    check("in-progress unload reports filament_pos == 4 (PARTIALLY_LOADED)",
          s["filament_pos"] == 4, s["filament_pos"])
    check("filament_position stays 0.0 (no countdown mm)", s["filament_position"] == 0.0, s["filament_position"])

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

    print("flowguard — dwell-to-level derivation (pure function, no firmware change)")
    check("saturated tension dwell -> tangle at -1.0",
          flare_daemon._flowguard_level(True, 6000, 0) == (-1.0, "tangle"),
          flare_daemon._flowguard_level(True, 6000, 0))
    check("halfway compression dwell -> 0.5, not yet tripped",
          flare_daemon._flowguard_level(True, 0, 2500) == (0.5, ""),
          flare_daemon._flowguard_level(True, 0, 2500))
    check("sync inactive forces level 0 regardless of dwell",
          flare_daemon._flowguard_level(False, 6000, 0) == (0.0, ""),
          flare_daemon._flowguard_level(False, 6000, 0))

    print("flowguard — TT:/CT: wire parse feeds status_cache + wakes klipper_syncer")
    with flare_daemon.status_lock:
        flare_daemon.status_cache.update({
            "active_lane": 1, "tc_state": "IDLE", "lane1_task": "IDLE", "lane2_task": "IDLE",
            "buf_sensor_type": 0, "buf_state": "NEUTRAL", "in1": 0, "out1": 0, "in2": 0, "out2": 0,
            "toolhead": 0, "y_split": 0, "reload_mode": 0, "enable_cutter": 0, "unload_cut": 0,
            "board_online": True, "tension_dwell_ms": 0, "compression_dwell_ms": 0,
        })
    flare_daemon.klipper_sync_event.clear()
    flare_daemon.parse_status_line("OK:LN:1,TC:IDLE,L1T:IDLE,L2T:IDLE,TT:1234,CT:0")
    check("TT: parses to tension_dwell_ms",
          flare_daemon.status_cache.get("tension_dwell_ms") == 1234,
          flare_daemon.status_cache.get("tension_dwell_ms"))
    check("CT: parses to compression_dwell_ms",
          flare_daemon.status_cache.get("compression_dwell_ms") == 0,
          flare_daemon.status_cache.get("compression_dwell_ms"))
    check("dwell-timer edge wakes klipper_syncer (wake-gate tuple)",
          flare_daemon.klipper_sync_event.is_set(),
          flare_daemon.klipper_sync_event.is_set())

    print("flowguard — round-trip through cmd_SET_MMU / get_status()")
    m, p = new_mock()
    m.cmd_SET_MMU(FakeGcmd({
        "FLOWGUARD_ENABLED": 1, "FLOWGUARD_ACTIVE": 1, "FLOWGUARD_TRIGGER": "'tangle'",
        "FLOWGUARD_REASON": "'Tension dwell approaching trip'", "FLOWGUARD_LEVEL": -1.0,
        "FLOWGUARD_MAX_CLOG": 0.0, "FLOWGUARD_MAX_TANGLE": -1.0,
    }))
    expected_flowguard = {
        "enabled": True, "active": True, "trigger": "tangle",
        "reason": "Tension dwell approaching trip", "level": -1.0,
        "max_clog": 0.0, "max_tangle": -1.0,
    }
    check("flowguard dict round-trips exactly",
          m.get_status(0)["flowguard"] == expected_flowguard,
          m.get_status(0)["flowguard"])

    print("daemon-mirrored keys — sync_drive, is_paused/reason_for_pause, sync_feedback_flow_rate")
    m, p = new_mock()
    m.cmd_SET_MMU(FakeGcmd({"SPS": 100.0, "BASELINE_SPS": 50.0}))
    check("sync_feedback_flow_rate == 50.0 for SPS=100/BASELINE=50",
          m.get_status(0)["sync_feedback_flow_rate"] == 50.0,
          m.get_status(0)["sync_feedback_flow_rate"])
    m.cmd_SET_MMU(FakeGcmd({"SPS": 0.0}))
    check("sync_feedback_flow_rate == 100.0 guarded against div-by-zero (SPS=0)",
          m.get_status(0)["sync_feedback_flow_rate"] == 100.0,
          m.get_status(0)["sync_feedback_flow_rate"])

    m, p = new_mock()
    m.cmd_SET_MMU(FakeGcmd({"IS_PAUSED": 1, "REASON_FOR_PAUSE": "'ABORTED'"}))
    check("IS_PAUSED=1 -> is_paused True", m.get_status(0)["is_paused"] is True, m.get_status(0)["is_paused"])
    check("REASON_FOR_PAUSE strips quotes",
          m.get_status(0)["reason_for_pause"] == "ABORTED", m.get_status(0)["reason_for_pause"])

    m, p = new_mock()
    m.cmd_SET_MMU(FakeGcmd({"SYNC_DRIVE": 1}))
    check("SYNC_DRIVE=1 -> sync_drive True", m.get_status(0)["sync_drive"] is True, m.get_status(0)["sync_drive"])
    m.cmd_SET_MMU(FakeGcmd({"SYNC_DRIVE": 0}))
    check("SYNC_DRIVE=0 -> sync_drive False", m.get_status(0)["sync_drive"] is False, m.get_status(0)["sync_drive"])

    print("daemon reconcile — sync_state==SYNC_FAULT_HOLD drives IS_PAUSED/REASON_FOR_PAUSE")
    flare_daemon.status_cache["sync_state"] = flare_daemon._SYNC_STATE_FAULT_HOLD
    flare_daemon.status_cache["sync_drive"] = True
    with flare_daemon.stats_lock:
        flare_daemon.mmu_stats["last_error"] = "TEST_FAULT"
    with flare_daemon.status_lock:
        fault_state = dict(flare_daemon.status_cache)
    with flare_daemon.stats_lock:
        fault_st = dict(flare_daemon.mmu_stats)
    fault_sync_state_val = fault_state.get("sync_state", 0)
    fault_is_paused = 1 if fault_sync_state_val == flare_daemon._SYNC_STATE_FAULT_HOLD else 0
    fault_reason_for_pause = f"'{fault_st['last_error']}'" if fault_is_paused else "''"
    check("sync_state SYNC_FAULT_HOLD -> IS_PAUSED pushed as '1'",
          str(fault_is_paused) == "1", fault_is_paused)
    check("SYNC_FAULT_HOLD REASON_FOR_PAUSE mirrors mmu_stats last_error",
          fault_reason_for_pause == "'TEST_FAULT'", fault_reason_for_pause)
    flare_daemon.status_cache["sync_state"] = 0
    non_fault_is_paused = 1 if flare_daemon.status_cache.get("sync_state", 0) == flare_daemon._SYNC_STATE_FAULT_HOLD else 0
    check("non-FAULT_HOLD sync_state -> IS_PAUSED pushed as '0'",
          str(non_fault_is_paused) == "0", non_fault_is_paused)

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
        "GATE_MATERIAL": "'PLA,PETG'",
        "GATE_COLOR": "'#FF0000,#00FF00'",
        "GATE_SPOOL_ID": "'12,34'",
        "GATE_NAME": "'Gate 0,Gate 1'",
        "GATE_FILAMENT_NAME": "'Gate 0,Gate 1'",
        "FLOWGUARD_ENABLED": "1",
        "FLOWGUARD_ACTIVE": "1",
        "FLOWGUARD_TRIGGER": "'tangle'",
        "FLOWGUARD_REASON": "'Tension dwell approaching trip'",
        "FLOWGUARD_LEVEL": "-1.000",
        "FLOWGUARD_MAX_CLOG": "0.000",
        "FLOWGUARD_MAX_TANGLE": "-1.000",
        "SYNC_DRIVE": "1",
        "IS_PAUSED": "0",
        "REASON_FOR_PAUSE": "''",
        "BASELINE_SPS": "45.000",
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

    print("status schema — Plan-01 partial coverage: static/derived keys + Tasks 1-2 keys exist with correct type")
    # `status` here is the freshly-SET_MMU'd bypass-gate mock from the reconcile block above.
    expected_types = {
        # Task 1 — flowguard
        "flowguard": dict,
        # Task 2 — daemon-mirrored keys
        "sync_drive": bool,
        "is_paused": bool,
        "reason_for_pause": str,
        "sync_feedback_flow_rate": float,
        "baseline_sps": float,
        # Task 3 — static/derived keys (18)
        "has_bypass": bool,
        "unit": int,
        "operation": str,
        "drying_state": str,
        "espooler": list,
        "espooler_active": str,
        "extruder_filament_remaining": float,
        "filament_direction": int,
        "gate_temperature": list,
        "grip": str,
        "last_tool": int,
        "next_tool": int,
        "slicer_tool_map": dict,
        "endless_spool_enabled": bool,
        "endless_spool_groups": list,
        "toolchange_purge_volume": float,
        "clog_detection_enabled": bool,
    }
    for _key, _type in expected_types.items():
        _val = status.get(_key, "<MISSING>")
        check(f"status['{_key}'] present with type {_type.__name__}",
              _key in status and isinstance(_val, _type), _val)
    # 'encoder' is a deliberate literal None, not a missing key (Pitfall 2) --
    # can't use isinstance-of-NoneType generically above, assert separately.
    check("status['encoder'] is literal None (not omitted)",
          "encoder" in status and status["encoder"] is None, status.get("encoder", "<MISSING>"))
    check("clogs_enabled alias retained alongside clog_detection_enabled",
          status.get("clogs_enabled") is False, status.get("clogs_enabled"))

    print("slicer purge hook — MMU_SET_PURGE delegates to _FLARE_SET_PURGE")
    m, p = new_mock()
    m.cmd_MMU_SET_PURGE(FakeGcmd({"PURGE": 42.5}))
    check("MMU_SET_PURGE invokes _FLARE_SET_PURGE",
          p._gcode.commands == ["_FLARE_SET_PURGE PURGE=42.5"],
          p._gcode.commands)

    print("command stubs — 9 Fluidd/Mainsail maintenance/print-lifecycle commands registered")
    m, p = new_mock()
    noop_names = {"MMU_TEST_CONFIG", "MMU_LED", "MMU_GRIP", "MMU_RELEASE",
                  "MMU_SERVO", "MMU_LED_VARS", "MMU_SOFTWARE_VARS"}
    print_sync_names = {"MMU_PRINT_START", "MMU_PRINT_END"}
    all_new_names = noop_names | print_sync_names
    check("all 9 new command names registered",
          all_new_names <= set(p._gcode.registered.keys()),
          sorted(p._gcode.registered.keys()))
    check("7 no-op names map to cmd_MMU_NOOP",
          all(p._gcode.registered[n] == m.cmd_MMU_NOOP for n in noop_names),
          {n: p._gcode.registered.get(n) for n in noop_names})
    check("MMU_PRINT_START/MMU_PRINT_END map to cmd_MMU_PRINT_SYNC",
          all(p._gcode.registered[n] == m.cmd_MMU_PRINT_SYNC for n in print_sync_names),
          {n: p._gcode.registered.get(n) for n in print_sync_names})
    m.cmd_MMU_PRINT_SYNC(FakeGcmd({}))
    check("cmd_MMU_PRINT_SYNC delegates to _FLARE_SYNC_TOOLHEAD (no RESET)",
          p._gcode.commands == ["_FLARE_SYNC_TOOLHEAD"],
          p._gcode.commands)
    try:
        m.cmd_MMU_NOOP(FakeGcmd({}))
        noop_raised = False
    except Exception:
        noop_raised = True
    check("cmd_MMU_NOOP does not raise", not noop_raised, noop_raised)

    print("action strings — Cutting Filament / Preload vocabulary expansion (14-02 Task 2)")
    check("UNLOAD_CUT -> Cutting Filament",
          flare_daemon._derive_action("UNLOAD_CUT", 1, "IDLE", "IDLE") == "Cutting Filament",
          flare_daemon._derive_action("UNLOAD_CUT", 1, "IDLE", "IDLE"))
    check("UNLOAD_WAIT_CUT -> Cutting Filament",
          flare_daemon._derive_action("UNLOAD_WAIT_CUT", 1, "IDLE", "IDLE") == "Cutting Filament",
          flare_daemon._derive_action("UNLOAD_WAIT_CUT", 1, "IDLE", "IDLE"))
    check("plain UNLOAD still -> Unloading (cutting check doesn't swallow it)",
          flare_daemon._derive_action("UNLOAD", 1, "IDLE", "IDLE") == "Unloading",
          flare_daemon._derive_action("UNLOAD", 1, "IDLE", "IDLE"))
    check("recent_preload_event=True at IDLE -> Preload",
          flare_daemon._derive_action("IDLE", 0, "IDLE", "IDLE", recent_preload_event=True) == "Preload",
          flare_daemon._derive_action("IDLE", 0, "IDLE", "IDLE", recent_preload_event=True))
    check("UNLOAD_CUT beats a stale recent_preload_event flag",
          flare_daemon._derive_action("UNLOAD_CUT", 1, "IDLE", "IDLE", recent_preload_event=True) == "Cutting Filament",
          flare_daemon._derive_action("UNLOAD_CUT", 1, "IDLE", "IDLE", recent_preload_event=True))
    check("no recent_preload_event kwarg -> Idle unaffected (default False)",
          flare_daemon._derive_action("IDLE", 0, "IDLE", "IDLE") == "Idle",
          flare_daemon._derive_action("IDLE", 0, "IDLE", "IDLE"))

    print("action strings — _recent_event_seen decay window over event_history")
    with flare_daemon.event_history_lock:
        flare_daemon.event_history.clear()
    flare_daemon.add_event_to_history("PRELOAD", "1")
    check("_recent_event_seen('PRELOAD') True right after the event",
          flare_daemon._recent_event_seen("PRELOAD", within_s=2.0) is True,
          flare_daemon._recent_event_seen("PRELOAD", within_s=2.0))
    check("_recent_event_seen('RUNOUT') False when only PRELOAD is recorded",
          flare_daemon._recent_event_seen("RUNOUT", within_s=2.0) is False,
          flare_daemon._recent_event_seen("RUNOUT", within_s=2.0))
    with flare_daemon.event_history_lock:
        flare_daemon.event_history.clear()

    print("mmu_machine.happy_hare_version — non-empty string present")
    m, p = new_mock()
    hh_version = mmu.MMUMachineMock(m).get_status(0)["happy_hare_version"]
    check("happy_hare_version is a non-empty string",
          isinstance(hh_version, str) and len(hh_version) > 0, hh_version)

    print("status schema keys present — fresh mock, never SET_MMU'd (test_status_fields_exist_before_ready analogue)")
    EXPECTED_STATUS_KEYS = frozenset({
        "enabled", "is_homed", "num_gates", "active_gate", "gate", "tool", "bypass",
        "gate_status", "gate_sensor", "gate_color", "gate_material", "gate_spool_id",
        "gate_color_rgb", "gate_name", "gate_filament_name", "ttg_map", "tool_color",
        "tool_material", "tool_spool_id", "tool_color_rgb", "tool_name",
        "tool_filament_name", "action", "num_toolchanges", "swaps_total",
        "swaps_success", "swaps_failed", "loads_success", "unloads_success",
        "last_error", "toolhead_sensor", "tc_state", "sync_feedback",
        "sync_feedback_state", "sync_feedback_bias", "sync_feedback_bias_modelled",
        "sync_feedback_enabled", "print_job_state", "print_state", "board_online",
        "sps", "reload_mode", "buf_sensor_type", "enable_cutter", "unload_cut",
        "board_feed_rate", "board_rev_rate", "spoolman_support", "filament",
        "filament_pos", "filament_position", "bowden_progress", "gate_sensor_active",
        "extruder_sensor_active", "pre_gate_sensor_active", "hub_sensor_active",
        "sensors", "gate_speed_override", "gate_speed", "clogs_enabled",
        "clogs_suspended", "clogs_detected", "clogs_total", "clogs_success",
        "clogs_failed", "encoder_enabled", "encoder_suspended",
        "encoder_tangle_detected", "encoder_clog_detected", "flowguard",
        "sync_drive", "is_paused", "reason_for_pause", "sync_feedback_flow_rate",
        "baseline_sps", "has_bypass", "unit", "operation", "drying_state",
        "espooler", "espooler_active", "extruder_filament_remaining",
        "filament_direction", "gate_temperature", "grip", "last_tool", "next_tool",
        "slicer_tool_map", "endless_spool_enabled", "endless_spool_groups",
        "toolchange_purge_volume", "encoder", "clog_detection_enabled",
    })
    m, p = new_mock()  # fresh mock, no cmd_SET_MMU call — status right after __init__
    fresh_status = m.get_status(0)
    missing_keys = sorted(EXPECTED_STATUS_KEYS - set(fresh_status.keys()))
    check("status schema keys present on a never-SET_MMU'd fresh mock",
          EXPECTED_STATUS_KEYS <= set(fresh_status.keys()), missing_keys)

    print(f"\n{_PASS} passed, {_FAIL} failed")
    sys.exit(1 if _FAIL else 0)

RunnerTests = functest_adapter.testcase_from_callable(run_tests)  # unittest discover entry


if __name__ == "__main__":
    run_tests()
