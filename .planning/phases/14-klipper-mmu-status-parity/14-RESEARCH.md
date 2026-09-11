# Phase 14: Klipper MMU Status Parity - Research

**Researched:** 2026-09-12
**Domain:** Klipper "extras" mock module (`klipper/mmu.py`) status-schema and command-stub parity with Happy-Hare v4, consumed by Fluidd/Mainsail/KlipperScreen `develop` builds.
**Confidence:** HIGH (all FLARE-side claims read verbatim from the working tree this session; all Happy-Hare claims read verbatim from the pinned HH clone `ef8431c4` that the prior borrow-scan session cloned into the shared scratchpad, still present this session).

## Summary

FLARE ships a hand-written Klipper "extras" module, `klipper/mmu.py`, that mimics enough of Happy-Hare's `printer.mmu` object for Fluidd/Mainsail/KlipperScreen to render an MMU panel without a real Happy-Hare install. The mock's `get_status()` (`klipper/mmu.py:1359-1497`) returns ~55 keys today. The 2026-09-11 Happy-Hare borrow scan (`.planning/research/2026-09-11-happy-hare-borrow-scan.md`) diffed this against HH `4.0.0` (`ef8431c4`, `main`) and current Fluidd/Mainsail `develop` sources and found: (a) FLARE has no `flowguard` key at all, so the FlowGuard meter widget in both UIs renders dead; (b) ~20 keys current UI mixins read are simply absent (`endless_spool_groups`, `sync_drive`, `has_bypass`, `unit`, `gate_temperature`, `encoder`, …); (c) six G-code commands the maintenance/servo dialogs send (`MMU_TEST_CONFIG`, `MMU_LED`, `MMU_GRIP`, `MMU_RELEASE`, `MMU_SERVO`, `MMU_PRINT_START`/`MMU_PRINT_END`) are unregistered, so those dialogs error; (d) `action` only ever reports `Loading`/`Unloading`/`Idle` (`scripts/flare_daemon.py:1343-1357`) where HH reports 14 distinct strings; (e) there is no test that asserts the full status schema exists before Klipper/Moonraker starts subscribing (HH added exactly this test, `test_status_fields_exist_before_ready`, in commit `60ed80f`).

This is pure host-tooling work — no firmware change is required for any of the five success criteria. The `flowguard.level` derivation can be built entirely from fields FLARE's firmware **already puts on the wire** in the `ST:` status dump (`TT:` tension-dwell ms, `CT:` compression-dwell ms, `protocol_status.c:64-80`) plus one runtime knob that is *not* gated behind `FLARE_DEV_TUNING` (`SYNC_AUTO_STOP`, default 5000 ms) and one that *is* gated (`SYNC_TENSION_STOP_MS`, default 6000 ms, `#ifdef FLARE_DEV_TUNING` at `protocol.c:571-573`) — this asymmetry is a real pitfall documented below, not a research gap.

**Primary recommendation:** Extend `klipper/mmu.py` `get_status()` with the ~20 missing keys + a computed `flowguard` dict (daemon-computed, pushed via new `SET_MMU` fields, mirrored like every other field today), extend `scripts/flare_daemon.py` `_derive_action` to the full HH action vocabulary using existing `EV:`/`TC:` events, register six no-op command stubs following the existing `cmd_MMU_NOOP` pattern (`klipper/mmu.py:564-566`), and add a `test_status_fields_exist_before_ready`-style assertion to `scripts/test_flare_mmu_status.py`.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| `flowguard` level computation (dwell-time-to-trip-budget fraction) | Host daemon (`flare_daemon.py`) | Klipper mock (`mmu.py`) mirrors the daemon's value | Firmware already exposes `TT:`/`CT:` dwell timers over `ST:`; no new firmware protocol needed. Daemon owns derived/interpreted state (it already derives `action`, `gate_status`, print state). |
| `printer.mmu` status schema (missing keys) | Klipper mock (`klipper/mmu.py`) | Host daemon supplies live values via `SET_MMU` | `get_status()` is the sole authority Moonraker/Fluidd/Mainsail read; keys with no live daemon source (e.g. `unit`, `has_bypass`) are static/derived entirely in the mock. |
| G-code command stubs (`MMU_TEST_CONFIG`, `MMU_LED`, …) | Klipper mock (`klipper/mmu.py`) | — | Pure Klipper `gcode.register_command` surface; no daemon or firmware involvement (same pattern as existing `cmd_MMU_NOOP`). |
| `action` string derivation | Host daemon (`flare_daemon.py`) | Klipper mock stores/echoes the pushed value | `_derive_action` already lives in the daemon and is pushed via `SET_MMU ACTION=...`; `mmu.py` only stores it (`cmd_SET_MMU:216,229`). |
| Status-schema regression test | Host test suite (`scripts/test_flare_mmu_status.py`) | — | Existing convention: offline logic test against a `FakePrinter`/`MMUMock`, no daemon/hardware. |

## Standard Stack

No new external libraries. This phase touches only `klipper/mmu.py` (stdlib Klipper `gcode`/`config` API, already vendored as a Klipper "extras" module — not pip-installed) and `scripts/flare_daemon.py` (stdlib `urllib`/`json`/`threading`, already in use). **Do not add pip/npm dependencies for this phase** — the existing test/daemon/mock stack is 100% stdlib by project convention (`AGENTS.md` non-negotiable #2, `python3 -m py_compile scripts/*.py`).

## Package Legitimacy Audit

Not applicable — this phase installs no external packages (no `pip`/`npm`/`cargo` additions). `klipper/mmu.py` is a Klipper "extras" module copied into `~/klipper/klippy/extras/` by the user, not a package dependency.

## User Constraints

No `.planning/phases/14-klipper-mmu-status-parity/*-CONTEXT.md` exists yet (`ls` returned only `README.md` in the phase directory) — `/gsd-discuss-phase` has not run for this phase. There are no locked decisions, discretion areas, or deferred ideas to copy verbatim. The planner should treat every design choice below (flowguard-level formula, `mmu_machine.happy_hare_version` string, `grip`/`servo` static values, `is_paused` semantics) as open until confirmed — several are flagged `[ASSUMED]` in the Assumptions Log for exactly this reason.

## Phase Requirements

Phase requirement IDs were not pre-registered in `.planning/REQUIREMENTS.md` (grep found none matching `klipper-status-parity`). Per the phase brief, proposed IDs below for the planner to register:

| ID | Description | Research Support |
|----|-------------|------------------|
| `REQ-klipper-status-parity-flowguard-dict` | Publish `printer.mmu.flowguard` (`enabled/active/trigger/reason/level/max_clog/max_tangle`), `level` derived from firmware dwell/saturation timers, `0`/inactive whenever sync is off | §"FlowGuard derivation" below; firmware `TT:`/`CT:` fields (`protocol_status.c:64-80`), HH schema (`mmu_sync_feedback.py:73,630-633`) |
| `REQ-klipper-status-parity-missing-keys` | Add all `printer.mmu` keys current Fluidd/Mainsail `develop` read that FLARE lacks (Diff table below) | §"HH v4 schema diff table" |
| `REQ-klipper-status-parity-hh-version` | Set `mmu_machine.happy_hare_version` to a value that keeps current Fluidd/Mainsail UI branches on FLARE's actually-emulated code paths | §"mmu_machine.happy_hare_version" below; `[ASSUMED]` — needs a UI-branch check the borrow scan did not perform |
| `REQ-klipper-status-parity-command-stubs` | Register `MMU_TEST_CONFIG`, `MMU_LED`, `MMU_GRIP`, `MMU_RELEASE`, `MMU_SERVO`, `MMU_PRINT_START`, `MMU_PRINT_END` (+ `*_VARS` if the dialogs call them) as ack/no-op stubs | §"Command stub gap list" below |
| `REQ-klipper-status-parity-action-strings` | `action` reports the full HH vocabulary (`Cutting Filament`, `Preload`, `Loading`, …) derived from existing `EV:`/`TC:` events, not just Loading/Unloading/Idle | §"EV: -> action string map" below |
| `REQ-klipper-status-parity-schema-test` | A `test_status_fields_exist_before_ready`-style test asserts the full status schema before "the daemon connects" (FLARE analogue: before `board_online` first flips true) | §"Existing test conventions" below |

## Project Constraints (from CLAUDE.md / AGENTS.md)

`./CLAUDE.md` only points to `AGENTS.md`; the enforceable directives live there:

- **Build must pass before every commit** (`ninja -C build_local`) — this phase is host-only Python, so the build gate is unaffected, but do not skip it if any C touched changes (it should not for this phase).
- **Python validation**: `python3 -m py_compile scripts/*.py` before every commit touching scripts — **gap**: `klipper/*.py` is not in this glob today (`scripts/validate_regression.py:113` only globs `scripts/*.py`). The planner must add an explicit `python3 -m py_compile klipper/*.py` step (either a one-off task action or a `validate_regression.py` change) since this phase's primary edits are in `klipper/mmu.py`.
- **Commit + push after every change, automatically, without asking.**
- **Runtime tunable protocol parity** (rule 8): N/A here — this phase adds no new firmware `SET:`/`GET:` runtime tunable; it only reads existing ones (`SYNC_AUTO_STOP`, and optionally `SYNC_TENSION_STOP_MS` under dev-tuning).
- **Regression impact for new features** (rule 9): review affected flows — this phase touches `get_status()` and `SET_MMU`, both read by every other phase's status polling; verify `scripts/test_flare_mmu_status.py`'s existing 20+ assertions (bypass, RELOAD re-stage, cut-unload, proportional buffer) still pass after the schema additions.
- **No mock/stub hardware** (rule 5) is about firmware, not applicable to a Klipper "extras" mock module (which is inherently a software mock by design, per its own file header comment).
- **Self-review before commit** (rule 13): check the staged diff against `REVIEW.md`.
- **NDA — no external project names** (user global memory): none of this phase's content references any outside project; verified — all sources are FLARE, Happy-Hare (public GitHub project), Fluidd, Mainsail (all public open-source, not NDA-covered).

## Current `klipper/mmu.py` `get_status()` — every key, verbatim

`[VERIFIED: klipper/mmu.py:1427-1497]` — full literal return of `get_status()`:

```python
return {
    'enabled': self.enabled,
    'is_homed': self.is_homed,
    'num_gates': self.num_gates,
    'active_gate': self.active_gate,
    'gate': self.gate,
    'tool': self.tool,
    'bypass': self.bypass,
    'gate_status': list(self.gate_status),
    'gate_sensor': list(self.gate_sensor),
    'gate_color': list(self.gate_color),
    'gate_material': list(self.gate_material),
    'gate_spool_id': list(self.gate_spool_id),
    'gate_color_rgb': [list(x) if isinstance(x, list) else x for x in self.gate_color_rgb],
    'gate_name': list(self.gate_name),
    'gate_filament_name': list(self.gate_filament_name),
    'ttg_map': list(self.ttg_map),
    'tool_color': self.tool_color,
    'tool_material': self.tool_material,
    'tool_spool_id': self.tool_spool_id,
    'tool_color_rgb': list(self.tool_color_rgb) if isinstance(self.tool_color_rgb, list) else self.tool_color_rgb,
    'tool_name': self.tool_name,
    'tool_filament_name': self.tool_filament_name,
    'action': self.action,
    'num_toolchanges': self.swaps_total,
    'swaps_total': self.swaps_total,
    'swaps_success': self.swaps_success,
    'swaps_failed': self.swaps_failed,
    'loads_success': self.loads_success,
    'unloads_success': self.unloads_success,
    'last_error': self.last_error,
    'toolhead_sensor': self.toolhead_sensor,
    'tc_state': self.tc_state,
    'sync_feedback': self.sync_feedback,
    'sync_feedback_state': self.sync_feedback_state,
    'sync_feedback_bias': self.sync_feedback,
    'sync_feedback_bias_modelled': self.sync_feedback,
    'sync_feedback_enabled': self.sync_feedback_enabled,
    'print_job_state': self.print_job_state,
    'print_state': self.print_state,
    'board_online': self.board_online,
    'sps': self.sps,
    'reload_mode': self.reload_mode,
    'buf_sensor_type': self.buf_sensor_type,
    'enable_cutter': self.enable_cutter,
    'unload_cut': self.unload_cut,
    'board_feed_rate': self.board_feed_rate,
    'board_rev_rate': self.board_rev_rate,
    'spoolman_support': self.spoolman_support,
    'filament': self.filament,
    'filament_pos': self.filament_pos,
    'filament_position': round(filament_position, 1),
    'bowden_progress': round(bowden_progress, 1),
    'gate_sensor_active': self.gate_sensor_active,
    'extruder_sensor_active': self.extruder_sensor_active,
    'pre_gate_sensor_active': self.pre_gate_sensor_active,
    'hub_sensor_active': self.hub_sensor_active,
    'sensors': sensors_dict,
    'gate_speed_override': list(self.gate_speed_override),
    'gate_speed': self.gate_speed_override[self.active_gate] if 0 <= self.active_gate < len(self.gate_speed_override) else 100,
    'clogs_enabled': False,
    'clogs_suspended': True,
    'clogs_detected': False,
    'clogs_total': 0,
    'clogs_success': 0,
    'clogs_failed': 0,
    'encoder_enabled': False,
    'encoder_suspended': True,
    'encoder_tangle_detected': False,
    'encoder_clog_detected': False
}
```

`filament_position`/`bowden_progress` are hardcoded `0.0`/`-1.0` "checkpoint stops only" (`klipper/mmu.py:1401-1405`) — a deliberate prior decision (commit `94e27a2` per the code comment), not a gap; do not resurrect synthetic-mm interpolation.

Source of every value: **either local mock state** (set by `cmd_SET_MMU`, `klipper/mmu.py:175-349`, which the daemon calls via `RUN_SHELL_COMMAND` / gcode script push) **or computed in `get_status()` itself** from that local state (`filament`/`filament_pos`/`sensors_dict`, `klipper/mmu.py:1361-1425`). There is no direct daemon->Klipper channel other than `SET_MMU`; `klipper/mmu.py` never talks to the daemon HTTP API itself except at init (`_load_vars`, referenced around `klipper/mmu.py:1165,1264` for bowden-length/vars fallback).

`klipper/mmu_sensors.py` (`MMUSensorsMock`) is a **separate** Klipper extras module registering fake `filament_switch_sensor` objects (`mmu_pre_gate_0/1`, `mmu_gate`, `mmu_extruder`, `mmu_toolhead`, `mmu_hub`) so Fluidd/Mainsail's generic sensor tracker discovers them (`klipper/mmu_sensors.py:53-82`). Its `get_status()` (`klipper/mmu_sensors.py:10-51`) looks up the `mmu` object and re-derives `filament_detected` from the same path-cascade logic as `mmu.py`'s `get_status` — this is **duplicated logic that must stay in sync manually** if the cascade rules change; not itself a Phase-14 gap but a maintenance note for the executor.

`mmu_machine` mock (`klipper/mmu.py:4-28`, registered `klipper/mmu.py:36`): static `unit_0` dict with `has_bypass: True` already at the unit level, but **no top-level `mmu_machine.happy_hare_version`** — see below.

## `cmd_SET_MMU` — how daemon fields flow in

`[VERIFIED: klipper/mmu.py:175-349]` — every param the daemon can push, with defaults-preserved-when-absent semantics (per-key `gcmd.get_int/get_float/get(key, self.current_value)`), confirmed by the delta-push contract (`REQ-daemon-klipper-mirror-delta-set-mmu-mirr`, `.planning/REQUIREMENTS.md:372-376`). Notably: `BYPASS` re-adoption logic (`klipper/mmu.py:190-204`) forces `active_gate/gate/tool = -2` while bypass is set — any new keys that also carry gate/tool semantics (e.g. `last_tool`/`next_tool`) must respect this sentinel the same way the reconcile helper does (`_format_mmu_reconcile_value`, `scripts/flare_daemon.py:1298-1318`, lines 1305-1306: `if key in ("ACTIVE_GATE","GATE","TOOL") and desired_fields.get("BYPASS")=="1": return desired_fields.get(key)`).

`action`/`tc_state` additionally drive `_update_phase` (`klipper/mmu.py:227`) which is gated off while a synchronous wait loop (`is_loading`/`is_unloading`) owns phase tracking (`klipper/mmu.py:225`) — any new `SET_MMU` field that also needs phase-aware handling must go through the same gate, not write `self.x` unconditionally.

## `scripts/flare_daemon.py` — the `SET_MMU` builder (`klipper_syncer`)

`[VERIFIED: scripts/flare_daemon.py:1379-1627]` — `klipper_syncer(moonraker_url)` is the sole function that assembles and pushes `SET_MMU`. Key mechanics:

- **Event-driven wake**: `klipper_sync_event.wait(timeout=wait_time)` (`:1401`); `wait_time` from `_syncer_wait_time` (`:1359-1376`, unit-tested in `scripts/test_flare_daemon_syncer.py`).
- **Change detection**: only these `status_cache` keys trigger a push (`:1428-1442`): `board_online, active_lane, tc_state, buf_state, in1, out1, in2, out2, toolhead, y_split, reload_mode, enable_cutter, unload_cut` + `lane1_task, lane2_task`. **Any new field the daemon derives from `TT:`/`CT:` dwell timers must be added to this change-detection list** (or the flowguard level will only update on the periodic 10 s reconcile, not on the dwell-timer edges that actually matter).
- **Delta push contract**: `fields` dict (`:1547-1582`) is built fresh every push; only the keys whose formatted value changed since `last_pushed_fields` are emitted as `SET_MMU k=v ...` (`:1627`, confirmed by `REQ-daemon-klipper-mirror-delta-set-mmu-mirr`). New fields (flowguard, action-string expansion, endless-spool, etc.) go into this same `fields` dict.
- **Full-resync recovery**: `_moonraker_get_mmu_status` (`:1274-1283`) + `_mmu_status_matches_fields`/`_format_mmu_reconcile_value` (`:1298-1327`) reconcile the mock against desired state every 10 s (`reconcile_due`, `:1444`) and on board-online transition (`:1451-1454`). **Any new `SET_MMU` field must be added to `_MMU_RECONCILE_STATUS_KEYS` (`:1210-1249`) and, if it's a float or string, to `_MMU_RECONCILE_FLOATS`/`_MMU_RECONCILE_STRINGS` (`:1251-1272`)** or the reconcile check will silently never detect drift for that field (`_format_mmu_reconcile_value` returns `None` for unknown keys, which never equals a real `SET_MMU`-formatted string, so it would force a needless full resync every 10 s instead of correctly no-op'ing — a subtle perf/pitfall, not a hard bug).
- **`action` derivation**: `_derive_action(tc_state, active_lane, lane1_task, lane2_task)` (`:1343-1357`, full body quoted below) is called at `:1538-1540` and only ever returns `"Loading"`, `"Unloading"`, or `"Idle"`.

```python
# scripts/flare_daemon.py:1343-1357 (verbatim)
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
```

`record_event_stats(evt_type, evt_data)` (`scripts/flare_daemon.py:200-226`) currently only recognizes `TC:DONE`, `TC:ERROR`, `LOADED`, `UNLOADED` for the usage counters (`mmu_stats`) — it is **not** the place `action` is derived (that's purely from `status_cache` state in `_derive_action`), but it is the natural place to add a `cutter_cuts` maintenance counter later (out of scope for Phase 14 — see Roadmap note below).

## `ST:` fields available for `flowguard.level` — no firmware change needed

`[VERIFIED: firmware/src/protocol_status.c:38-84]` — full format-string + args:

```c
// firmware/src/protocol_status.c:43-60 (first half)
"LN:%d,TC:%s,L1T:%s,L2T:%s,"
"I1:%d,O1:%d,I2:%d,O2:%d,"
"TH:%d,YS:%d,BUF:%s,MM:%.1f,BF:%.1f,BP:%.2f,SM:%d,BL:%s,ST:%d,TPR:%d,CU:%d,RELOAD:%d,UC:%d,"
"BST:%d,BY:%d,TMC:%d%d,"
"EST:%.1f,RE:%.2f,AV:%.2f,SC:%.1f",
...

// firmware/src/protocol_status.c:63-80 (second half, only appended if room)
uint32_t now_ms = g_now_ms;
uint32_t ad_ms = sync_tension_dwell_ms(now_ms);
uint32_t td_ms = (g_buf.state == BUF_COMPRESSION && g_buf.entered_ms > 0)
                     ? (now_ms - g_buf.entered_ms)
                     : 0;
snprintf(b + blen, sizeof(b) - (size_t)blen,
         ",RT:%.2f,TT:%u,CT:%u,SK:%u,CF:%.2f,ES:%.2f"
         ",TPX:%d,CB:%d,BPV:%d,MK:%u:%s"
         ",SYNC_REFILL_MM:%d,SYNC_RELIEVE_MM:%d,TF:%.1f,FL_RATE:%.1f,UL_RATE:%.1f",
         (double)sync_reserve_target_mm(), (unsigned)ad_ms, (unsigned)td_ms,
         ...
```

So every `?:` status poll (5 Hz, `status_poller`, `scripts/flare_daemon.py:735-748`) already carries `TT:<ms>` (continuous tension-pin dwell, `sync_tension_dwell_ms()`) and `CT:<ms>` (continuous compression dwell, only nonzero while `g_buf.state==BUF_COMPRESSION`). **`scripts/flare_daemon.py`'s `parse_status_line` does not currently parse `TT`/`CT`/`ST`(sync_state, already parsed)/`TPX` into `status_cache`** — `[VERIFIED: scripts/flare_daemon.py:511-597]`, the `if/elif key ==` chain there stops at `UL_RATE` and has no `TT`/`CT`/`TPX` branch. **This is the one required addition on the parse side** — add `elif key == "TT": new_data["tension_dwell_ms"] = int(val)` / `elif key == "CT": new_data["compression_dwell_ms"] = int(val)` (and optionally `TPX`) to `parse_status_line` before any flowguard-level math is possible.

**Trip-threshold knobs to normalize against** — asymmetric availability, a real pitfall:
- Tension side: `SYNC_TENSION_STOP_MS` (`sync_tension_dwell_stop_ms`, MANUAL.md default **6000**, `[VERIFIED: MANUAL.md:225]` — `| \`SYNC_TENSION_STOP_MS\` | \`sync_tension_dwell_stop_ms\` | Hard stop if continuously pinned at tension endstop for this many ms. 0 = disable. | 6000 |`) is only exposed via `GET:` **inside `#ifdef FLARE_DEV_TUNING`** (`[VERIFIED: firmware/src/protocol.c:571-573]` — the `GET` branch for this param sits between `#ifdef FLARE_DEV_TUNING` at line 571 preceded by an unrelated `#endif` at 568 closing a *different* guarded block, i.e. this branch itself starts a fresh `#ifdef FLARE_DEV_TUNING` at 571). A default (non-dev-tuning) firmware build will `ER:` on `GET:SYNC_TENSION_STOP_MS`.
- Compression side: `SYNC_AUTO_STOP` (`g_sync_auto_stop_ms`, default **5000**, `[VERIFIED: firmware/include/tune.h:79]` `#define CONF_SYNC_AUTO_STOP_MS 5000`, and `[VERIFIED: firmware/src/protocol.c:569-570]` `else if (!strcmp(param, "SYNC_AUTO_STOP")) snprintf(out, out_len, "SYNC_AUTO_STOP:%d", g_sync_auto_stop_ms);` — this `GET:` branch is **not** inside any `#ifdef`, so it works on every build) is the closest compression-side trip constant, though it gates a `RELIEF_PAUSE` transition (`sync_check_continuous_compression`, `firmware/src/sync.c:1887-1909`), not a `FAULT_HOLD` — semantically the nearest analogue to HH's `"clog"` trip but not an exact match. `[ASSUMED]` that `SYNC_AUTO_STOP` is the right normalizer for the compression side of `flowguard.level` — flag for user confirmation; the alternative is hardcoding both constants from MANUAL.md defaults and never issuing a `GET:` at all (simpler, always works, but drifts silently if a user retunes `SYNC_TENSION_STOP_MS`/`SYNC_AUTO_STOP` at runtime via dev-tuning `SET:`).

**Recommended derivation (host/daemon-side, no firmware change)**:
```python
# scripts/flare_daemon.py — new helper, illustrative only (not yet in the codebase)
def _flowguard_level(sync_enabled, tension_dwell_ms, compression_dwell_ms,
                      tension_stop_ms=6000, compression_stop_ms=5000):
    if not sync_enabled or (not tension_dwell_ms and not compression_dwell_ms):
        return 0.0, ""            # inactive -> 0, satisfies success criterion #1
    if tension_dwell_ms > compression_dwell_ms:
        frac = min(1.0, tension_dwell_ms / max(1, tension_stop_ms))
        return -frac, "tangle" if frac >= 1.0 else ""     # negative = tension side, matches HH sign convention
    frac = min(1.0, compression_dwell_ms / max(1, compression_stop_ms))
    return frac, "clog" if frac >= 1.0 else ""             # positive = compression side
```
Sign convention (`positive = compression/"clog"`, `negative = tension/"tangle"`) matches HH exactly — see next section.

## FlowGuard — exact HH v4 dict shape, semantics, and range

`[VERIFIED: HH ef8431c4 extras/mmu/unit/mmu_sync_controller.py:73]` — the dict's field set and defaults, read verbatim from the pinned clone this session:

```python
self.flowguard_status = {'trigger': '', 'reason': '', 'level': 0.0, 'max_clog': 0.0, 'max_tangle': 0.0, 'active': False, 'enabled': False}
```

`[VERIFIED: HH ef8431c4 extras/mmu/unit/mmu_sync_controller.py:807-905]` — `update_flowguard(d_ext, sensor_reading)` computes a signed "relief effort" and accumulates it separately per extreme:
- While pegged at the **compression** extreme: accumulates `_relief_comp_mm`; `level = min(1.0, |_relief_comp_mm| / flowguard_relief_mm)` (**positive**, `[0,1]`); trips `trigger="clog"` when the ratio reaches `1.0`. `max_clog` is the running high-water mark of this positive value.
- While pegged at the **tension** extreme: accumulates `_relief_tens_mm`; `level = max(-1.0, -|_relief_tens_mm| / flowguard_relief_mm)` (**negative**, `[-1,0]`); trips `trigger="tangle"` at `-1.0`. `max_tangle` is the running low-water mark (most negative).
- Leaving either extreme resets both sides' accumulators to `0.0` (`:895-905`).
- **Arming** (`:825-841`): `_armed` starts `False`; only becomes `True` after motion is observed **and** either the coarse extreme-state changed or (for proportional sensors) the reading is near-neutral. Until armed, `status()` reports the last known level, `active=False` — this is exactly the anti-false-trip-at-boot mechanism FLARE's success criterion #1 ("0 whenever sync is inactive") should mirror: gate the whole dict on `sync_enabled`, not just `level`.
- `status()` return (`[VERIFIED: HH ef8431c4 extras/mmu/unit/mmu_sync_controller.py:906-921]`):
```python
def status(self):
    s = {
        "active": self._armed,
        "level": self._level,
        "max_clog": self._max_clog,
        "max_tangle": self._max_tangle,
        "trigger": self._trigger,
        "reason": self._reason,
    }
```
`enabled` is merged in by the caller (`mmu_sync_feedback.py:630-633`, `[VERIFIED]`): `self.flowguard_status['enabled'] = bool(self.p.flowguard_enabled)`.

**Proposed FLARE `flowguard` dict** (daemon-computed, mirrored via new `SET_MMU FLOWGUARD_LEVEL=...`/`FLOWGUARD_TRIGGER=...` fields into `klipper/mmu.py`, assembled into a dict inside `get_status()` the same way `sensors_dict` is assembled today, `klipper/mmu.py:1407-1425`):

| HH key | Type | FLARE source | Notes |
|---|---|---|---|
| `enabled` | bool | `sync_feedback_enabled` (already mirrored) | Reuse existing field |
| `active` | bool | `sync_drive`/`sync_enabled` (`SM:` field, already parsed) | 0/False whenever sync inactive per success criterion 1 |
| `trigger` | str (`''`\|`'clog'`\|`'tangle'`) | new daemon computation above | Empty string default, not `None` — matches HH default |
| `reason` | str | new daemon computation, human string | Optional; HH always includes it even though the phase brief's field list omits it — include for parity since Fluidd's meter may read it |
| `level` | float `[-1,1]` | new daemon computation from `TT:`/`CT:` | Sign convention: `+`=compression/clog, `-`=tension/tangle |
| `max_clog` | float `[0,1]` | daemon-side high-water mark, reset on... | `[ASSUMED]` reset policy (HH resets on leaving the extreme's accumulator, not the high-water mark itself — HH's `max_clog`/`max_tangle` are lifetime-until-explicit-reset; confirm reset trigger with user) |
| `max_tangle` | float `[-1,0]` | daemon-side low-water mark | Same as above |

## HH v4 `printer.mmu` schema diff table

`[VERIFIED: HH ef8431c4 extras/mmu/mmu_controller.py:631-676]` (core status dict) `+ :846-867 mmu_gate_maps.py` (merged via `status.update(self.gate_maps.get_status(eventtime))`) `+ :370-379 mmu_sync_feedback.py` (merged via `status.update(self.mmu_unit().sync_feedback.get_status(eventtime))`). Only listing keys the borrow-scan's Fluidd/Mainsail grep (`.planning/research/2026-09-11-happy-hare-borrow-scan.md:244-252`) confirmed the UIs actually read, cross-referenced against `[VERIFIED]` HH source:

| HH key | Type | FLARE has? | Proposed FLARE source/value |
|---|---|---|---|
| `action` | str | Partial (3 values) | Expand `_derive_action` — see next section |
| `bowden_progress` | float | Yes (`-1.0` fixed) | Keep as-is (deliberate, `klipper/mmu.py:1401-1405`) |
| `clog_detection_enabled` | bool | Yes (`clogs_enabled: False`, different key name!) | **Rename risk** — FLARE currently emits `clogs_enabled` (plural), HH/UI reads `clog_detection_enabled` (singular, no `s`). `[VERIFIED: klipper/mmu.py:1487]` `'clogs_enabled': False,` vs `[VERIFIED: HH mmu_controller.py:672]` `'clog_detection_enabled': False,`. Add the correctly-named key; decide whether to keep or drop the legacy `clogs_enabled` alias (backward-compat for any custom macros a user wrote against it) |
| `drying_state` | str | No | Static `''` (`DRYING_STATE_NONE`, `[VERIFIED: HH ef8431c4 extras/mmu/mmu_constants.py:337]` `DRYING_STATE_NONE = ''`) — FLARE has no heater/dryer integration |
| `enabled` | bool | Yes | No change |
| `endless_spool_enabled` | bool | No | Map to `reload_mode == 1` (FLARE's RELOAD mode is the firmware analogue per borrow-scan §"Already borrowed") — `[ASSUMED]`, confirm with user whether RELOAD-mode-on should read as "endless spool enabled" for UI purposes |
| `endless_spool_groups` | list[int] | No | Static `[0, 1]` (each gate its own group; FLARE's 2-lane RELOAD is not a Klipper-driven endless-spool remap) — `[ASSUMED]` default, needs confirmation |
| `espooler` | list | No | Static `[ESPOOLER_NONE]*num_gates` = `['', '']` — FLARE has no eSpooler hardware |
| `espooler_active` | str | No | Static `''` |
| `extruder_filament_remaining` | float | No | `[ASSUMED]` — no Spoolman "remaining" wiring found in this phase's scope; likely static `0.0` or omit if the UI tolerates absence (`60ed80f`/`4d343c9` commits say Moonraker needs the key *present*, value can be a safe default) |
| `filament` | str | Yes | No change |
| `filament_direction` | int/str | No | HH: extrude(+1)/retract(-1)/none(0) direction of last filament move. `[ASSUMED]` — FLARE mock has no move-direction tracking; propose static `0` or derive from `current_phase` (`load`->1, `unload`->-1, else 0) |
| `filament_pos` | int | Yes | No change |
| `filament_position` | float | Yes (`0.0` fixed) | Keep as-is (deliberate) |
| `flowguard` | dict | No | See FlowGuard section above |
| `gate` | int | Yes | No change |
| `gate_color` | list | Yes | No change |
| `gate_filament_name` | list | Yes | No change |
| `gate_material` | list | Yes | No change |
| `gate_speed_override` | list | Yes | No change |
| `gate_spool_id` | list | Yes | No change |
| `gate_status` | list | Yes | No change |
| `gate_temperature` | list[float] | No | `[VERIFIED: HH ef8431c4 extras/mmu/mmu_gate_maps.py:859]` `'gate_temperature': self.gate_temperature,` — static `[0,0]` (0 = "use extruder default" per HH's own fallback logic, `mmu_controller.py:2734`) |
| `grip` | str | No (selector-only concept) | `[ASSUMED]` — FLARE is `VirtualSelector` (`mmu_machine` mock already declares `'selector_type': 'VirtualSelector'`, `klipper/mmu.py:17`), no physical gripper. Static `"Gripped"` (gear always engaged in FLARE's design) is the closest true statement, but confirm with user before locking in — a wrong static value could mislead the maintenance dialog |
| `has_bypass` | bool | No (top-level; only nested in `mmu_machine.unit_0`) | Static `True` — matches existing `mmu_machine` mock's `unit_0.has_bypass` |
| `is_homed` | bool | Yes | No change |
| `is_paused` | bool | No | `[ASSUMED]` — FLARE's `cmd_MMU_PAUSE` (`klipper/mmu.py:542-548`) dispatches `STOP` to firmware but never sets any `self.is_paused`-like attribute; no field currently tracks pause state. Needs a design decision: track `self.is_paused` set by `cmd_MMU_PAUSE`/cleared by `cmd_MMU_UNLOCK`/`cmd_MMU_RESET`, or leave permanently `False`. Flag as open question |
| `last_tool` / `next_tool` | int | No | Derive from `active_gate` (FLARE 1:1 gate==tool); `[ASSUMED]` since FLARE's toolchange is synchronous (no "next" queued ahead of the current swap) — `next_tool` may always equal `active_gate` post-swap, i.e. effectively redundant. Confirm with user |
| `num_toolchanges` | int | Yes (`swaps_total` alias) | No change |
| `operation` | str | No | `[ASSUMED]` static `''` — HH's `saved_toolhead_operation` supports pause/resume macro replay, a feature FLARE does not implement |
| `print_state` | str | Yes (different vocabulary: `standby`/`ready`/`printing`) | HH's `print_state` comes from `psm.print_state` (`initialized/ready/started/printing/complete/cancelled/error/pause_locked/paused/standby`, `[VERIFIED: .planning/research/2026-09-11-happy-hare-borrow-scan.md:267-268]`, itself citing HH `mmu_print_state_machine.py`). FLARE currently only emits `printing`/`ready` (`[VERIFIED: scripts/flare_daemon.py:1491-1499]`). Full 9-state mapping is borrow-scan rec, tagged LATER — **out of scope for Phase 14** unless the planner decides current UIs require more states than `ready/printing` to avoid a dead "paused" indicator |
| `reason_for_pause` | str | No | Map to `last_error` while `is_paused`, else `''` — `[ASSUMED]`, depends on the `is_paused` design decision above |
| `slicer_tool_map` | dict | No | `[VERIFIED: HH ef8431c4 extras/mmu/mmu_gate_maps.py:865]` `'slicer_tool_map': self.slicer_tool_map,` — static `{}` acceptable; FLARE has `scripts/gcode_marker.py` but no live slicer-tool-map ingestion into the mock |
| `sync_drive` | bool | No | Map directly from `sync_enabled` (`SM:` parsed field, already in `status_cache`) |
| `sync_feedback_flow_rate` | float | No | Borrow-scan formula (`.planning/research/2026-09-11-happy-hare-borrow-scan.md:228-230`): `100 * min(1, baseline_sps/current_sps)` using existing `sps`/`baseline_sps` fields (`BF:` already parsed as `baseline_sps`, `[VERIFIED: scripts/flare_daemon.py:566-567]`) |
| `tool` | int | Yes | No change |
| `toolchange_purge_volume` | float | No | Static `0.0` acceptable; FLARE's dynamic purge (`MMU_SET_PURGE`, `klipper/mmu.py:568-571`) already exists as a separate mechanism — could surface the last-set purge value here for consistency; `[ASSUMED]` low priority |
| `ttg_map` | list | Yes | No change |
| `unit` | int | No | Static `0` — FLARE is single-unit |
| `mmu_machine.happy_hare_version` | str | No | See dedicated section below |

## `mmu_machine.happy_hare_version`

`[VERIFIED: HH ef8431c4 extras/mmu_machine.py:105]` — `self.machine_status["happy_hare_version"] = self.happy_hare_version`, sourced from `[VERIFIED: HH ef8431c4 extras/mmu/mmu_constants.py:20]` `VERSION = "4.0.0"`. FLARE's `MMUMachineMock.get_status()` (`klipper/mmu.py:8-28`) has no `happy_hare_version` key at all. The borrow-scan flags this as introduced in commit `ee6bb5c` "for easy UI behavior switching" and recommends: *"set it to a v3 string FLARE actually emulates, or UIs may take v4 code paths"* (`.planning/research/2026-09-11-happy-hare-borrow-scan.md:241-243`). **This specific recommendation is `[ASSUMED]`** — neither this session nor the borrow-scan session actually traced which Fluidd/Mainsail `develop` code branches key off this version string (the borrow-scan's own citation list for this line does not include a UI-side file). The planner should treat "which version string" as an open question requiring either (a) a targeted grep of Fluidd/Mainsail `mixins/mmu.ts` for `happy_hare_version` comparisons, or (b) a `checkpoint:human-verify` after shipping with a chosen string, watching for any version-gated UI element (e.g., a v4-only button) rendering incorrectly.

## EV: events -> HH action strings

`[VERIFIED: MANUAL.md:353-378]` — full `EV:` catalog (grep `^| .EV.\|^## Events` region, quoted verbatim, the authoritative field/event reference per `AGENTS.md`'s `[lookup]` convention):

```
| Event | Data | Description |
|-------|------|-------------|
| `AUTO_LOAD` | `lane` | Automatic full-load was started because the controller was empty when filament was inserted. |
| `PRELOAD` | `lane` | Automatic preload-to-OUT was started on filament insertion. |
| `RUNOUT` | `lane` | Filament runout detected on specified lane. |
| `LOADED` | `lane` | Filament successfully reached the toolhead/gears. |
| `UNLOADED`| `lane` | Filament successfully retracted past the OUT or IN sensor. |
| `LOAD_TIMEOUT` | `lane` | A load task hit its configured distance limit before completion. |
| `UNLOAD_TIMEOUT` | `lane` | An unload task hit its configured distance limit before completion. |
| `UNLOAD:FAULT` | `CUT_FAILED\|OUT_BLOCKED` | Manual-unload terminal fault event. |
| `MOVE_DONE` | `lane` | Exact move completed. |
| `ACTIVE` | `lane\|NONE`| Reported when the active lane changes. |
| `FAULT:DRY_SPIN`| `lane` | Motor spinning > 8s without filament (`IN` clear). |
| `SYNC` | `AUTO_START\|AUTO_STOP\|FAULT_HOLD\|FAULT_HOLD_RECOVERY\|TENSION_DWELL_WARN\|TENSION_RISK_HIGH\|RELIEF_PAUSE\|NEUTRAL_CREEP_CAP\|cannot_refill\|cannot_relieve` | ... |
| `BUF` | `DRIFT_RESET` | ... |
| `BUF` | `EST_LOW_CF\|EST_FALLBACK` | ... |
| `BUF_STAB` | `START\|DONE\|TIMEOUT\|STAGNANT_TIMEOUT\|REVERSE` | ... |
| `BL` | `PRIME\|LOCKED\|BREAK\|FOLLOW\|FOLLOW_DONE\|FOLLOW_GATED\|PRIME_BOUND\|TIMEOUT` | ... |
| `BS` | Mode-specific snapshot | ... |
| `TC:*` | Phase-specific | `TC:UNLOADING`, `TC:CUTTING`, `TC:SWAPPING`, `TC:LOADING`, `TC:LOAD_RETRY_RETRACT`, `TC:TS_PARKED`, `TC:DONE`, `TC:ERROR:<reason>` |
| `RELOAD:*` | Phase-specific | `RELOAD:SWITCHING`, `RELOAD:JOINING`, `RELOAD:LOADED`, `RELOAD:FAULT` |
| `CUT` | `FEEDING\|DONE\|ERROR` | Cutter execution events. |
| `FLASH` | `WEAR_WARNING` | ... |
| `TMC:RESTORED` / `TMC:FAULT` | ... | ... |
| `CRASH:DETECTED` | `WATCHDOG` | ... |
| `WARN:LOOP_LAG` | `<us>:<module>` | ... |
```

`[VERIFIED: HH ef8431c4 extras/mmu/mmu_constants.py:124-137]` — HH's canonical `ACTION_*` vocabulary:
```python
ACTION_IDLE = 0
ACTION_LOADING = 1
ACTION_LOADING_EXTRUDER = 2
ACTION_UNLOADING = 3
ACTION_UNLOADING_EXTRUDER = 4
ACTION_FORMING_TIP = 5
ACTION_HEATING = 6
ACTION_CHECKING = 7
ACTION_HOMING = 8
ACTION_SELECTING = 9
ACTION_CUTTING_TIP = 10         # Cutting at toolhead e.g.  _MMU_CUT_TIP macro
ACTION_CUTTING_FILAMENT = 11    # Cutting at MMU e.g. EREC cutting macro
ACTION_PURGING = 12             # Non slicer purging e.g. when running blobifier
ACTION_PRELOAD = 13             # Preloading filament into a gate (own action so it can consume a pending spool_id)
```
And the exact string map (`[VERIFIED: HH ef8431c4 extras/mmu/mmu_controller.py:1220-1239]`):
```python
def _get_action_string(self, action=None):
    if action is None:
        action = self.action
    return ("Idle" if action == ACTION_IDLE else
            "Loading" if action == ACTION_LOADING else
            "Unloading" if action == ACTION_UNLOADING else
            "Loading Ext" if action == ACTION_LOADING_EXTRUDER else
            "Exiting Ext" if action == ACTION_UNLOADING_EXTRUDER else
            "Forming Tip" if action == ACTION_FORMING_TIP else
            "Cutting Tip" if action == ACTION_CUTTING_TIP else
            "Heating" if action == ACTION_HEATING else
            "Checking" if action == ACTION_CHECKING else
            "Homing" if action == ACTION_HOMING else
            "Selecting" if action == ACTION_SELECTING else
            "Cutting Filament" if action == ACTION_CUTTING_FILAMENT else
            "Purging" if action == ACTION_PURGING else
            "Preload" if action == ACTION_PRELOAD else
            "Unknown")
```

**Proposed FLARE `EV:`/`TC:` -> HH action string map** (extends `_derive_action`, `scripts/flare_daemon.py:1343-1357`):

| FLARE trigger | HH action string | Confidence |
|---|---|---|
| `TC:CUTTING` event, or `tc_state` containing `CUT` | `"Cutting Filament"` (MMU-side cutter, matches HH's `ACTION_CUTTING_FILAMENT` doc comment "Cutting at MMU") | `[ASSUMED]` mapping choice — FLARE has one cutter (MMU-side only), so `"Cutting Tip"` (toolhead-side, HH-only concept) never applies |
| `tc_state` startswith `LOAD` / `SWAP` / `RELOAD` (existing) | `"Loading"` | Existing, unchanged |
| `tc_state` startswith `UNLOAD` (existing) | `"Unloading"` | Existing, unchanged |
| `EV:PRELOAD` active / `lane*_task == "PRELOAD"` | `"Preload"` | `[ASSUMED]` — no current `lane*_task` value named `PRELOAD` confirmed; verify against `firmware/src/toolchange.c` task-name table before implementing (not read this session — out of budget; flag for executor to grep `task_name(` in `firmware/src/*.c`) |
| `tc_state == "TC:TS_PARKED"` or a toolhead-wait phase | `"Homing"` or `"Checking"` | `[ASSUMED]` — weakest mapping in this table; FLARE has no explicit homing-equivalent phase since it is a `VirtualSelector`. Consider omitting rather than guessing wrong |
| default / `IDLE` | `"Idle"` | Existing, unchanged |

Given the mapping-confidence gaps above, the planner should treat the **exact** `EV:`/`tc_state`->action table as a design decision for `/gsd-discuss-phase`, not something this research locks in — the firmware-side vocabulary (`TC:*` phase names, `lane*_task` values) needs one more grep pass (`task_name(` in `firmware/src/toolchange.c`/`firmware/src/motion.c`) that this session did not have budget for. At minimum, `"Cutting Filament"`, `"Preload"`, and the existing three (`Loading`/`Unloading`/`Idle`) are well-grounded; `"Forming Tip"`, `"Homing"`, `"Checking"`, `"Selecting"`, `"Heating"` either have no FLARE-firmware equivalent (no heater, no physical selector) or need the additional grep.

## Command stub gap list

`[VERIFIED: klipper/mmu.py:109-172]` — every `register_command` call in `MMUMock.__init__`, verbatim command names: `SET_MMU, MMU_STATS, MMU_PRELOAD, MMU_UNLOAD, MMU_LOAD, MMU_CHANGE_TOOL, MMU_EJECT, MMU_RECOVER, MMU_CHECK_GATE, MMU_CHECK_GATES, MMU_GATE_MAP, MMU_TTG_MAP, MMU_SPOOLMAN, MMU_SELECT, MMU_SELECT_BYPASS, FLARE_WAIT_TC, FLARE_WAIT_UNLOAD, FLARE_START_UNLOAD, MMU_STATUS, MMU_HOME, MMU_UNLOCK, MMU_PAUSE, MMU_RESET, MMU_SYNC_GEAR_MOTOR, MMU_MOTORS_ON, MMU_MOTORS_OFF, MMU_SLICER_TOOL_MAP, MMU_ENDLESS_SPOOL, MMU_SET_PURGE`.

**Confirmed absent** (per borrow-scan §6, `.planning/research/2026-09-11-happy-hare-borrow-scan.md:254-258`, cross-checked against the list above): `MMU_TEST_CONFIG`, `MMU_LED`, `MMU_GRIP`, `MMU_RELEASE`, `MMU_SERVO`, `MMU_LED_VARS`, `MMU_SOFTWARE_VARS`, `MMU_PRINT_START`, `MMU_PRINT_END`.

**Existing stub pattern to copy** (`[VERIFIED: klipper/mmu.py:564-566]`):
```python
def cmd_MMU_NOOP(self, gcmd):
    """No-op: slicer compatibility stub, command accepted and ignored."""
    pass
```
registered at `klipper/mmu.py:168-170` for `MMU_SLICER_TOOL_MAP`/`MMU_ENDLESS_SPOOL`. This exact pattern (bare `pass`, or `gcmd.respond_info("FLARE: ... not implemented.")` per the `cmd_MMU_RESET`/`cmd_MMU_UNLOCK`/`cmd_MMU_HOME` variants at `klipper/mmu.py:534-552`) is what the planner should use for all nine missing commands — register each with its own `register_command` call (Klipper requires distinct method references per command in some cases, but a single shared `cmd_MMU_NOOP` bound to multiple command names is exactly what FLARE already does for two commands, so this is a proven-safe pattern to extend to nine).

`MMU_PRINT_START`/`MMU_PRINT_END` per the borrow-scan should stub to `_FLARE_SYNC_TOOLHEAD` rather than a bare no-op (`.planning/research/2026-09-11-happy-hare-borrow-scan.md:257-258`) — **this session did not verify `_FLARE_SYNC_TOOLHEAD` exists** in `klipper/flare_mmu.cfg` macros; the planner/executor must grep `flare_mmu.cfg` for this macro name before wiring the stub to it (if absent, fall back to bare no-op).

## Existing test conventions

`[VERIFIED: scripts/test_flare_mmu_status.py:1-305]`, full file read. Pattern:
- No `unittest.TestCase` class at module scope for the main suite; uses a hand-rolled `run_tests()` + `check(name, cond, detail)` assertion helper, then exposes it to `unittest discover` via `RunnerTests = functest_adapter.testcase_from_callable(run_tests)` (`:300`) — `[VERIFIED: scripts/functest_adapter.py:1-13]`, the documented reason is that `unittest discover` silently skips modules with no `TestCase` (the "Audit hardening fixes" gotcha noted in project memory).
- Imports `klipper/mmu.py` by `sys.path.insert(0, ...klipper)` then `import mmu` (`:19-20`); stubs the `serial` module (`sys.modules.setdefault("serial", types.SimpleNamespace())`) before importing `flare_daemon` so the daemon's `import serial` doesn't fail without pyserial installed (`:22-24`).
- `FakePrinter`/`FakeConfig`/`FakeGcode`/`FakeGcmd`/`FakeReactor`/`FakeMacro` (`:28-113`) are the full fake-Klipper surface — no real Klipper installation needed to test `mmu.py`.
- `new_mock()` (`:115-119`) builds a fresh `mmu.MMUMock` with hardware-like bowden geometry variables.
- The **daemon reconcile** test block (`:231-288`) is the closest existing precedent to a schema test: it builds a `fields` dict matching every `SET_MMU`-pushable key, pushes it via `cmd_SET_MMU`, then calls `flare_daemon._mmu_status_matches_fields(status, fields)` to assert daemon<->mock agreement. **This must be extended** with every new key this phase adds, or the reconcile logic (`_MMU_RECONCILE_STATUS_KEYS` etc.) will silently drift from what `get_status()` actually returns.

**Recommended `test_status_fields_exist_before_ready`-style addition**: a new `check`-block asserting `set(EXPECTED_KEYS) <= set(m.get_status(0).keys())` against a **fresh, never-`cmd_SET_MMU`'d** `MMUMock` instance (i.e. right after `new_mock()`, before any board data has ever arrived) — this is the FLARE analogue of HH's "before ready" (HH's version asserts the schema is present before Moonraker's first subscription callback; FLARE's analogue is "before the daemon's first `SET_MMU` push", i.e. immediately after `__init__`/`_load_vars`). `[VERIFIED: HH ef8431c4 test/README.md referenced in borrow-scan]` — this session did not re-read HH's actual test file (`test/test_mmu_bootup.py`) verbatim; the borrow-scan's citation (`60ed80f`, `.planning/research/2026-09-11-happy-hare-borrow-scan.md:298-300`) is the source for this recommendation, tag `[CITED: borrow-scan note, itself citing HH ef8431c4 test/test_mmu_bootup.py]`.

Other daemon/klipper-adjacent test files found (`ls scripts/test_*.py | xargs grep -l "mmu\|klipper\|daemon"`): `test_flare_daemon_cors.py`, `test_flare_daemon_security.py`, `test_flare_daemon_stats.py`, `test_flare_daemon_syncer.py` (only `_syncer_wait_time`, not `SET_MMU` field building), `test_flare_live_tuner.py`, `test_flare_mmu_status.py` (the primary one for this phase), `test_flare_sync_check.py`, `test_install_daemon.py`, `test_klipper_motion_tracker.py`, `test_sync_sim.py`, `test_wire_format.py`.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Python stdlib `unittest`, discovery-based (no pytest/pip dependency) |
| Config file | none — driven by `scripts/validate_regression.py`, `[VERIFIED: scripts/validate_regression.py:1-140]`, full file read |
| Quick run command | `python3 -m unittest scripts.test_flare_mmu_status -v` |
| Full suite command | `python3 -m unittest discover -s scripts -p 'test_*.py'` |

### Phase Requirements -> Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| `REQ-klipper-status-parity-flowguard-dict` | `flowguard` dict present with correct types, `level`/`active` = 0/False when `sync_feedback_enabled` is False | unit | `python3 -m unittest scripts.test_flare_mmu_status -v` | ✅ extend existing file |
| `REQ-klipper-status-parity-missing-keys` | Every diff-table key present with correct type after a representative `cmd_SET_MMU` push | unit | same | ✅ extend existing file |
| `REQ-klipper-status-parity-hh-version` | `mmu_machine.get_status()['unit_0']`... or top-level `happy_hare_version` present | unit | same | ✅ extend existing file (new `MMUMachineMock` assertion) |
| `REQ-klipper-status-parity-command-stubs` | Each new command calls its handler without raising, `p._gcode` interactions recorded as expected (mirrors the existing `MMU_SET_PURGE` delegation test, `:290-295`) | unit | same | ✅ extend existing file |
| `REQ-klipper-status-parity-action-strings` | `_derive_action`-equivalent (or its daemon-side successor) returns each new HH string for its trigger condition | unit | `python3 -m unittest scripts.test_flare_daemon_syncer -v` or a new `scripts/test_flare_daemon_action.py` | ❌ Wave 0 — new file, or extend `test_flare_mmu_status.py`'s daemon-reconcile block |
| `REQ-klipper-status-parity-schema-test` | Full expected-key-set assertion on a never-`SET_MMU`'d fresh mock | unit | same | ❌ Wave 0 — new `check()` block |

### Sampling Rate
- **Per task commit:** `python3 -m unittest scripts.test_flare_mmu_status -v` (fast, ~1s, no build/hardware)
- **Per wave merge:** `python3 -m unittest discover -s scripts -p 'test_*.py'` (full suite, includes `test_sync_sim` — build required, per `validate_regression.py` step 4/7)
- **Phase gate:** `python3 scripts/validate_regression.py` full run before `/gsd-verify-work` — **note**: this also runs the firmware build + host sync sim even though Phase 14 makes no firmware changes; that's expected/required by the existing gate, not a new cost this phase introduces.

### Wave 0 Gaps
- [ ] `python3 -m py_compile klipper/*.py` step — **not currently part of `validate_regression.py` or any documented gate** (`[VERIFIED]` absence: grep for `klipper/*.py` and `klipper.*py_compile` in `scripts/validate_regression.py` and `AGENTS.md` both returned no match). The planner should add either a one-off task action (`python3 -m py_compile klipper/*.py`) to every plan that touches `klipper/mmu.py`, or a small `validate_regression.py` change extending the glob — the latter is out of this phase's stated scope but worth flagging as a `checkpoint` recommendation.
- [ ] A schema-assertion test block in `scripts/test_flare_mmu_status.py` (or a new file) for `REQ-klipper-status-parity-schema-test`.
- [ ] An action-string test for the expanded `_derive_action` vocabulary.
- Framework install: none — `unittest` is stdlib, already in use.

## Common Pitfalls

### Pitfall 1: `get_status()` must stay cheap and JSON-serializable
**What goes wrong:** Klipper's `get_status()` is called at Moonraker's subscription rate (4+ Hz typically) for every subscribed client. Any blocking I/O (HTTP call, file read, `GET:`-over-serial round-trip) inside `get_status()` would stall the whole Klipper reactor thread.
**Why it happens:** The temptation to "just query the daemon live" for the flowguard level instead of relying on the already-mirrored `SET_MMU` push.
**How to avoid:** Compute `flowguard.level` in the **daemon** (which already has cheap in-memory access to `status_cache`'s `TT:`/`CT:`-derived fields) and push it via `SET_MMU`, exactly like every other derived field (`action`, `gate_status`, print state) already works. `get_status()` in `klipper/mmu.py` must only ever read `self.*` attributes and do cheap arithmetic — never touch the network or serial port.
**Warning signs:** Any new code in `klipper/mmu.py` importing `urllib`/`socket`/`serial`, or calling a daemon HTTP endpoint from inside `get_status()`.

### Pitfall 2: `None` vs. missing key — `encoder: None` is deliberate
**What goes wrong:** "Cleaning up" `encoder: None` to omit the key entirely, or to `False`/`{}` instead.
**Why it happens:** `None` looks like an oversight to someone unfamiliar with the HH schema.
**How to avoid:** `[VERIFIED: HH ef8431c4 extras/mmu/mmu_controller.py:661]` `'encoder': None,` is HH's own literal default (FLARE has no encoder hardware, matching HH's no-encoder-unit case, `[VERIFIED: HH ef8431c4 extras/mmu/unit/mmu_sync_feedback.py:373]` the base `status` dict template also defaults `'encoder'` implicitly absent unless `has_encoder()`). Commit `60ed80f` ("Keep MMU status fields stable during startup", cited in borrow-scan `:237-238`) exists specifically because Moonraker needs stable key *presence* even when the value is `None` — an omitted key breaks subscription stability differently than a `None` value does. FLARE should add `'encoder': None` as a literal key, not omit it.

### Pitfall 3: `FLARE_DEV_TUNING` gates half the flowguard trip-threshold surface
**What goes wrong:** Code that unconditionally does `GET:SYNC_TENSION_STOP_MS` at daemon startup to read the live tension trip threshold will get `ER:` on any default (non-dev-tuning) firmware build, and if the response isn't handled, could hang the serial reader waiting for a reply that never matches the expected format.
**Why it happens:** This is the same class of bug as the "Dev-tuning build blindspot" project memory (`FLARE_DEV_TUNING #ifdef` blocks invisible to default build) — except here it's the *daemon* being surprised by firmware build configuration, not a linter.
**How to avoid:** Either (a) hardcode the MANUAL.md-documented defaults (6000/5000 ms) and accept drift if a dev-tuning user retunes at runtime, or (b) issue the `GET:` opportunistically, treat any `ER:` response as "use the hardcoded default," and never block on it. Do not treat a missing/error response as fatal.
**Warning signs:** New serial command-response handling code in `flare_daemon.py` that assumes every `GET:` succeeds.

### Pitfall 4: Reconcile-table drift silently defeats delta-push
**What goes wrong:** Adding a new field to the `fields` dict in `klipper_syncer` (`scripts/flare_daemon.py:1547-1582`) without also adding it to `_MMU_RECONCILE_STATUS_KEYS`/`_MMU_RECONCILE_FLOATS`/`_MMU_RECONCILE_STRINGS` (`:1210-1272`) makes `_format_mmu_reconcile_value` return `None` for that key forever, which never equals the desired formatted string, which means `_mmu_status_matches_fields` never returns `True`, which forces a full `SET_MMU` resync **every 10 seconds, forever** (not a correctness bug — the mock still ends up correct — but a silent, permanent 10 s-cadence extra gcode push that never stabilizes, wasting gcode-lock cycles).
**Why it happens:** The delta-push/reconcile machinery has three places a new field must be registered (`fields` dict, change-detection `keys` list if the source changed, reconcile tables) and it's easy to update only one.
**How to avoid:** Grep all three locations (`fields = {`, `_MMU_RECONCILE_STATUS_KEYS`, the `keys = [...]` change-detection list at `:1428-1432`) whenever adding a new `SET_MMU` field; the existing test's reconcile block (`test_flare_mmu_status.py:231-288`) should be extended to catch this by construction (it will fail if `_mmu_status_matches_fields` ever returns `False` for a fully-formed `fields` dict).
**Warning signs:** A field that's correctly mirrored but the daemon logs (or `FLARE_GATE_DEBUG`) show `SET_MMU` firing every ~10 s indefinitely even with the printer idle.

### Pitfall 5: `MMUSensorsMock` duplicates the sensor-cascade logic
**What goes wrong:** Changing the filament-path-sensor cascade rules in `mmu.py`'s `get_status()` (`:1361-1378`) without also updating the **separately duplicated** copy in `klipper/mmu_sensors.py`'s `MockFilamentSensor.get_status()` (`:10-51`) leaves Fluidd's/Mainsail's generic sensor-tracker widget showing stale/inconsistent state relative to the MMU panel's own sensor dots.
**Why it happens:** Two independent Klipper "extras" objects (`mmu` and `mmu_sensors <name>`) each read `mmu`'s raw attributes and re-derive the same cascade logic — not a shared function.
**How to avoid:** This phase's success criteria don't require touching the cascade logic itself, but if any new key (e.g. `filament_direction`) is derived from sensor state, check whether `mmu_sensors.py` needs the equivalent update for consistency, even though it wasn't explicitly listed in the phase brief.
**Warning signs:** Fluidd's sensor tracker widget (separate from the MMU panel) disagreeing with the MMU panel's own sensor display.

## Code Examples

### Adding TT/CT parsing (illustrative, not yet in codebase)
```python
# scripts/flare_daemon.py — inside parse_status_line's elif chain (after "UL_RATE", ~line 595)
elif key == "TT":
    new_data["tension_dwell_ms"] = int(val)
elif key == "CT":
    new_data["compression_dwell_ms"] = int(val)
```

### Registering a new no-op command stub (exact existing pattern to replicate)
```python
# klipper/mmu.py — inside MMUMock.__init__, alongside the existing register_command calls (~line 172)
self.gcode.register_command('MMU_TEST_CONFIG', self.cmd_MMU_NOOP,
                             desc="FLARE: no-op stub for Fluidd/Mainsail maintenance dialog compatibility")
self.gcode.register_command('MMU_LED', self.cmd_MMU_NOOP, desc="FLARE: no-op stub")
self.gcode.register_command('MMU_GRIP', self.cmd_MMU_NOOP, desc="FLARE: no-op stub")
self.gcode.register_command('MMU_RELEASE', self.cmd_MMU_NOOP, desc="FLARE: no-op stub")
self.gcode.register_command('MMU_SERVO', self.cmd_MMU_NOOP, desc="FLARE: no-op stub")
self.gcode.register_command('MMU_LED_VARS', self.cmd_MMU_NOOP, desc="FLARE: no-op stub")
self.gcode.register_command('MMU_SOFTWARE_VARS', self.cmd_MMU_NOOP, desc="FLARE: no-op stub")
```
(`cmd_MMU_NOOP` already exists at `klipper/mmu.py:564-566` — reusing it for these seven new registrations is consistent with the existing `MMU_SLICER_TOOL_MAP`/`MMU_ENDLESS_SPOOL` precedent at `:168-170`.)

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|---------------|--------|
| HH's `printer.mmu` status assembled once, no stability guarantee | Keys always present from first query (`None`/default, not absent) | Commit `60ed80f`, cited 2026-08-17 in borrow-scan | Moonraker subscriptions establish cleanly before init completes; FLARE's mock already does this implicitly (all keys are instance attributes set in `__init__`, so they're always present) — no FLARE change needed for *this* specific concern, only for the *missing* keys themselves |
| `action` limited to load/unload | 14-value `ACTION_*` enum | HH v4 (ongoing since well before `ef8431c4`) | FLARE's 3-value `_derive_action` is the gap this phase closes |
| No `flowguard` concept | Distance-based FlowGuard replacing encoder-only clog detection | HH v4 sync-controller rewrite (pre-`ef8431c4`, "already borrowed" section notes the rd_filter port predates this scan) | FLARE has dwell-time-based fault detection (`SYNC_TENSION_STOP_MS`/`SYNC_AUTO_STOP`) that is the firmware-local analogue; this phase surfaces it to the UI for the first time |

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | `SYNC_AUTO_STOP` (5000 ms) is the correct compression-side normalizer for `flowguard.level`, paired with `SYNC_TENSION_STOP_MS` (6000 ms) on the tension side | FlowGuard derivation | Meter shows misleading fill percentage; low severity (cosmetic), not a safety issue since the underlying firmware trip logic is unchanged |
| A2 | `mmu_machine.happy_hare_version` should be a "v3-shaped" string, per borrow-scan's untraced recommendation | `mmu_machine.happy_hare_version` section | Wrong UI code branch taken by Fluidd/Mainsail for version-gated features; needs a targeted UI-source check before locking in |
| A3 | `grip` should report static `"Gripped"` (FLARE gear always engaged, no physical selector) | HH v4 schema diff table | Maintenance dialog shows a "Gripped"/"Released" toggle that does nothing meaningful; low risk (cosmetic) but could confuse a user trying to physically release filament |
| A4 | `endless_spool_enabled`/`endless_spool_groups` should map from `reload_mode`/static `[0,1]` | HH v4 schema diff table | UI's endless-spool group editor may allow edits that have no firmware effect, or may hide a feature FLARE partially supports via RELOAD |
| A5 | `is_paused` needs a new tracked attribute set by `cmd_MMU_PAUSE`/cleared by `cmd_MMU_UNLOCK`/`cmd_MMU_RESET` | HH v4 schema diff table | If left `False` always, Fluidd's pause/resume UI state never reflects reality; if implemented wrong, could show "paused" when the printer isn't, confusing the operator |
| A6 | `EV:PRELOAD`/`tc_state`-based mapping to `"Preload"`/`"Homing"`/`"Checking"` action strings — the exact FLARE trigger conditions were not verified against `task_name(` in firmware source this session | EV: -> action string map | Wrong or missing action string shown transiently during a toolchange phase; low risk (cosmetic, self-corrects on next state change) |
| A7 | `_FLARE_SYNC_TOOLHEAD` macro exists for `MMU_PRINT_START`/`MMU_PRINT_END` stub delegation | Command stub gap list | If the macro doesn't exist, wiring the stub to it would error; planner must grep `klipper/flare_mmu.cfg` before implementing, fall back to bare no-op if absent |
| A8 | `max_clog`/`max_tangle` reset policy (lifetime high-water mark vs. per-print-job reset) | FlowGuard dict proposal | If reset policy diverges from user expectation, the meter could show a stale "worst-ever" value that never clears, or clear too eagerly and hide a real trend |

## Open Questions

1. **Exact `EV:`/`tc_state` -> HH action-string mapping for `"Preload"`, `"Homing"`, `"Checking"`, `"Selecting"`**
   - What we know: HH's full 14-value vocabulary (verified above) and FLARE's `TC:*`/`RELOAD:*`/`EV:` event catalog (verified above).
   - What's unclear: Which FLARE `lane*_task`/`tc_state` values correspond to `"Preload"` vs the existing `PRELOAD` event's semantics already used for `AUTO_LOAD`; whether `"Homing"`/`"Selecting"`/`"Checking"` have any FLARE equivalent at all (FLARE is a `VirtualSelector`, no physical homing/selecting step).
   - Recommendation: Grep `task_name(` in `firmware/src/toolchange.c`/`firmware/src/motion.c` before finalizing the map; consider shipping only the well-grounded subset (`Loading`, `Unloading`, `Idle`, `Cutting Filament`, `Preload`) for Phase 14 and deferring the rest.

2. **`is_paused`/`reason_for_pause` semantics**
   - What we know: `cmd_MMU_PAUSE` dispatches `STOP` but tracks no pause attribute; `last_error` already exists.
   - What's unclear: Whether Fluidd/Mainsail's pause-UI actually needs a real `is_paused` toggle for Phase 14's stated success criteria, or whether a permanently-`False` stub is sufficient to stop dialogs from erroring (the phase brief only requires the *key to exist with correct type*, not necessarily correct semantics).
   - Recommendation: Confirm with `/gsd-discuss-phase` whether "exists with correct type" (minimum bar, static `False`/`''`) or "reflects real pause state" (requires new state tracking) is the actual bar for success criterion #2.

3. **`mmu_machine.happy_hare_version` exact string**
   - What we know: HH v4 = `"4.0.0"`; borrow-scan recommends "a v3 string" without specifying which.
   - What's unclear: Which Fluidd/Mainsail `develop` code paths, if any, actually branch on this value today (untraced by either research session).
   - Recommendation: A quick targeted grep (`happy_hare_version` in Fluidd/Mainsail `mixins/mmu.ts`) before committing to a specific string, or ship without the key changed and monitor for UI regressions via `checkpoint:human-verify`.

## Sources

### Primary (HIGH confidence — read verbatim this session)
- `klipper/mmu.py` (full `get_status`, `cmd_SET_MMU`, `__init__`, all `register_command` calls, existing stub patterns) — FLARE working tree, commit `f404bba` (per gitStatus at session start)
- `klipper/mmu_sensors.py` (full file, via CodeGraph `codegraph explore`)
- `scripts/flare_daemon.py` (`parse_status_line`, `record_event_stats`, `klipper_syncer`, `_derive_action`, `_MMU_RECONCILE_*`, `status_poller`)
- `scripts/test_flare_mmu_status.py` (full file — test conventions, existing schema/reconcile coverage)
- `scripts/functest_adapter.py` (unittest-discover adaptation pattern)
- `scripts/validate_regression.py` (full file — Validation Architecture gate)
- `firmware/src/protocol_status.c` (full file — `ST:` field format string, `TT:`/`CT:` dwell fields)
- `firmware/src/protocol.c` (`GET:` param handling, `FLARE_DEV_TUNING` gating around `SYNC_TENSION_STOP_MS`)
- `firmware/src/sync.c` (`sync_check_tension_dwell_and_ramp`, `sync_check_continuous_compression`, `sync_tick_type_p_rail_guard`)
- `firmware/include/tune.h` (`CONF_SYNC_AUTO_STOP_MS`, `CONF_PSF_WALL_SAT_MS` defaults)
- `MANUAL.md` (`EV:` event catalog §"Events (`EV:`)", `SYNC_TENSION_STOP_MS`/`TT`/`CT`/`TPX` parameter table rows — grepped, not read wholesale, per `AGENTS.md` convention)
- `AGENTS.md`, `.planning/STATE.md`, `.planning/REQUIREMENTS.md` (grepped sections)
- `.planning/research/2026-09-11-happy-hare-borrow-scan.md` — full file, PRIMARY INPUT per task brief
- Happy-Hare clone `ef8431c4` (scratchpad, still present this session): `extras/mmu/unit/mmu_sync_controller.py`, `extras/mmu/unit/mmu_sync_feedback.py`, `extras/mmu/mmu_controller.py`, `extras/mmu/mmu_gate_maps.py`, `extras/mmu/mmu_constants.py`, `extras/mmu_machine.py`, `extras/mmu/unit/selectors/{mmu_servo_selector,mmu_linear_servo_selector,mmu_rotary_selector}.py` — all read verbatim via `Read`/`grep` this session

### Secondary (MEDIUM confidence)
- Fluidd/Mainsail `develop` key lists — not re-fetched this session; relied on the borrow-scan's prior verbatim citations (`.planning/research/2026-09-11-happy-hare-borrow-scan.md:244-252`), which the borrow-scan session fetched directly from `develop` raw sources on 2026-09-11. Tagged `[CITED: borrow-scan note]` throughout rather than `[VERIFIED]` since this session did not independently re-fetch those files.

### Tertiary (LOW confidence)
- None used — where evidence was insufficient (EV:->action full mapping, `happy_hare_version` UI-branch behavior, `_FLARE_SYNC_TOOLHEAD` macro existence), findings are tagged `[ASSUMED]` in the Assumptions Log rather than presented as researched fact.

## Metadata

**Confidence breakdown:**
- Current-state inventory (what `mmu.py`/`flare_daemon.py`/firmware already emit): HIGH — every claim read verbatim from the working tree this session.
- HH v4 target schema: HIGH — every claim read verbatim from the pinned `ef8431c4` clone this session.
- Proposed FLARE-side derivation/mapping decisions (flowguard formula, action-string map, `grip`/`is_paused`/`happy_hare_version` values): LOW-MEDIUM, explicitly flagged `[ASSUMED]` — these are design choices, not facts, and belong in `/gsd-discuss-phase` before the planner locks them in.

**Research date:** 2026-09-12
**Valid until:** 30 days (stable domain — HH clone is pinned to a SHA, FLARE firmware/daemon behavior only changes via this repo's own commits; re-verify if `ef8431c4`'s scratchpad clone is garbage-collected or if Fluidd/Mainsail `develop` moves and the borrow-scan's citations go stale)
