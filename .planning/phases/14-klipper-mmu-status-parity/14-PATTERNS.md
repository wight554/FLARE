# Phase 14: Klipper MMU Status Parity - Pattern Map

**Mapped:** 2026-09-12
**Files analyzed:** 6 (all modified, no new files expected)
**Analogs found:** 6 / 6 (every file being touched is its own best analog — this phase extends existing code in-place rather than introducing new files/roles)

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|---|---|---|---|---|
| `klipper/mmu.py` (`get_status()`) | model / status-provider | request-response (Klipper `get_status` poll) | `klipper/mmu.py:1427-1497` (itself — extend existing dict literal) | exact |
| `klipper/mmu.py` (`cmd_SET_MMU`) | controller (gcode command handler) | CRUD (mirror daemon-pushed fields onto mock state) | `klipper/mmu.py:175-349` (itself) | exact |
| `klipper/mmu.py` (new command stubs) | controller (gcode command handler) | request-response (fire-and-forget ack) | `klipper/mmu.py:564-566` `cmd_MMU_NOOP`, registered `:168-170` | exact |
| `scripts/flare_daemon.py` (`parse_status_line`) | service / parser | streaming (serial `ST:` line, 5 Hz) | `scripts/flare_daemon.py:511-597` (itself — extend `elif` chain) | exact |
| `scripts/flare_daemon.py` (`klipper_syncer` `fields` dict + change-detection `keys` list) | service (state mirror / delta push) | event-driven (wake on change, push delta) | `scripts/flare_daemon.py:1379-1627` (itself) | exact |
| `scripts/flare_daemon.py` (`_MMU_RECONCILE_*` tables + `_format_mmu_reconcile_value`) | service (drift reconciliation) | batch (10 s periodic full-resync check) | `scripts/flare_daemon.py:1210-1327` (itself) | exact |
| `scripts/flare_daemon.py` (`_derive_action`) | utility (pure function) | transform | `scripts/flare_daemon.py:1343-1357` (itself) | exact |
| `scripts/test_flare_mmu_status.py` (new `check()` blocks) | test | request-response (offline unit assertions) | `scripts/test_flare_mmu_status.py:231-295` (existing reconcile + purge-delegation blocks) | exact |
| `klipper/mmu_sensors.py` (only if `filament_direction`/sensor-derived keys change) | model / status-provider | request-response | `klipper/mmu_sensors.py:10-51` `MockFilamentSensor.get_status()` | role-match, edit only if cascade logic touched |
| `scripts/validate_regression.py` (optional `klipper/*.py` py_compile glob) | config / build-gate | batch | `scripts/validate_regression.py:113` (`scripts/*.py` glob — extend or add sibling step) | role-match |

Since every touched file already contains the pattern to extend, there is no cross-codebase search needed beyond confirming these are the load-bearing sections — all paths below are git-tracked source (`git ls-files` confirmed for `klipper/mmu.py`, `klipper/mmu_sensors.py`, `scripts/flare_daemon.py`, `scripts/test_flare_mmu_status.py`, `scripts/validate_regression.py`, `klipper/flare_mmu.cfg`).

## Pattern Assignments

### `klipper/mmu.py` — `get_status()` dict literal (model, request-response)

**Analog:** itself, `klipper/mmu.py:1427-1497`

**Core pattern** — every value is either a plain `self.*` attribute or cheap arithmetic computed earlier in the method (`filament_position`, `bowden_progress`, `sensors_dict` built at `:1361-1425`). New keys (missing-key diff table, `flowguard` dict, `mmu_machine.happy_hare_version`) must follow this exact shape — **never** perform network/serial I/O inside `get_status()` (Pitfall 1 in RESEARCH.md):

```python
# klipper/mmu.py:1427-1497 (verbatim excerpt, tail)
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
```

For static/no-hardware keys (`espooler`, `drying_state`, `grip`, `has_bypass`, `unit`), copy this literal style: hardcoded Python value inline in the return dict, same as `'clogs_enabled': False` above — do not introduce a helper function for values that never change.

For daemon-mirrored keys (`sync_drive`, `flowguard`, expanded `action`), follow the `self.<attr>` pattern — the attribute must be set in `__init__` with a safe default AND updated in `cmd_SET_MMU` (see next section), exactly like `self.action`/`self.sync_feedback` today.

**`mmu_machine.happy_hare_version`** — analog is the sibling static-dict mock `MMUMachineMock` (`klipper/mmu.py:4-28`), which already returns a static `unit_0` dict from its own `get_status()`; add the new key as a sibling literal in that same dict, same style.

---

### `klipper/mmu.py` — `cmd_SET_MMU` (controller, CRUD)

**Analog:** itself, `klipper/mmu.py:175-349`

**Core pattern** — per-key `gcmd.get_int/get_float/get(key, self.current_value)` so absent keys in a delta push preserve prior state (defaults-preserved-when-absent semantics):
```python
# klipper/mmu.py:175-204 pattern (paraphrased structure, not full quote — see file for exact lines)
self.sync = gcmd.get_int('SYNC_FEEDBACK_ENABLED', self.sync_feedback_enabled)
...
# BYPASS re-adoption special case (lines 190-204): forces active_gate/gate/tool = -2
# while BYPASS is set — any new field carrying gate/tool semantics must honor this sentinel
```
New `SET_MMU` fields (`FLOWGUARD_LEVEL`, `FLOWGUARD_TRIGGER`, expanded `ACTION` vocabulary, `SYNC_DRIVE`, etc.) must be added here using the same `gcmd.get_*(KEY, self.current_value)` idiom, and must respect the `BYPASS` sentinel gate (`:190-204`) and the `is_loading`/`is_unloading` phase-tracking gate (`:225-227`) if they interact with gate/tool or phase state.

---

### `klipper/mmu.py` — new no-op command stubs (controller, request-response)

**Analog:** `klipper/mmu.py:564-566` (`cmd_MMU_NOOP`), registration at `klipper/mmu.py:168-170`

**Exact pattern to copy:**
```python
def cmd_MMU_NOOP(self, gcmd):
    """No-op: slicer compatibility stub, command accepted and ignored."""
    pass
```
Registration style (copy for each of the 9 missing commands — `MMU_TEST_CONFIG`, `MMU_LED`, `MMU_GRIP`, `MMU_RELEASE`, `MMU_SERVO`, `MMU_LED_VARS`, `MMU_SOFTWARE_VARS`, `MMU_PRINT_START`, `MMU_PRINT_END`):
```python
self.gcode.register_command('MMU_TEST_CONFIG', self.cmd_MMU_NOOP,
                             desc="FLARE: no-op stub for Fluidd/Mainsail maintenance dialog compatibility")
```
A single shared handler bound to multiple command names is proven-safe (already done for `MMU_SLICER_TOOL_MAP`/`MMU_ENDLESS_SPOOL`, `klipper/mmu.py:168-170`). Alternate stub style with a message, for reference (used elsewhere for `cmd_MMU_MOTORS_NOOP`, `klipper/mmu.py:560-562`):
```python
def cmd_MMU_MOTORS_NOOP(self, gcmd):
    """No-op: FLARE stepper drivers are firmware-managed; no host toggle."""
    gcmd.respond_raw("!! MMU motor on/off is not implemented on FLARE (drivers are firmware-managed).")
```
`MMU_PRINT_START`/`MMU_PRINT_END` should delegate to `_FLARE_SYNC_TOOLHEAD` if that macro exists in `klipper/flare_mmu.cfg` (grep before wiring — `[ASSUMED]` per RESEARCH.md A7); otherwise fall back to `cmd_MMU_NOOP`. Delegation-to-macro style already exists for `cmd_MMU_SET_PURGE`:
```python
# klipper/mmu.py:567-571
def cmd_MMU_SET_PURGE(self, gcmd):
    """Set dynamic purge length for the next toolchange."""
    purge = gcmd.get_float('PURGE', 0.0)
    self.gcode.run_script_from_command(f"_FLARE_SET_PURGE PURGE={purge}")
```

---

### `scripts/flare_daemon.py` — `parse_status_line` (service/parser, streaming)

**Analog:** itself, `scripts/flare_daemon.py:511-597`

**Core pattern** — flat `elif key == "XX": new_data["snake_case_name"] = <cast>(val)` chain, one branch per wire field:
```python
# illustrative addition per RESEARCH.md §"Adding TT/CT parsing", same idiom as existing branches
elif key == "TT":
    new_data["tension_dwell_ms"] = int(val)
elif key == "CT":
    new_data["compression_dwell_ms"] = int(val)
```
Add new branches (`TT`, `CT`, optionally `TPX`) immediately before/after the existing chain's last branch (`UL_RATE`), matching the existing cast convention (`int(val)` for integer ms/count fields — see neighboring branches for `float(val)` on rate fields).

---

### `scripts/flare_daemon.py` — `klipper_syncer` `fields` dict + change-detection + reconcile (service, event-driven)

**Analog:** itself, `scripts/flare_daemon.py:1379-1627` (fields dict at `:1547-1582`, change-detection `keys` list at `:1428-1442`, reconcile tables at `:1210-1272`)

**Core pattern — three places every new mirrored field MUST be registered (Pitfall 4):**
1. `fields` dict inside `klipper_syncer` (`:1547-1582`) — the value to push via `SET_MMU`.
2. The change-detection `keys` list (`:1428-1442`) if the field's *source* `status_cache` key isn't already watched.
3. `_MMU_RECONCILE_STATUS_KEYS` / `_MMU_RECONCILE_FLOATS` / `_MMU_RECONCILE_STRINGS` (`:1210-1272`) — or `_format_mmu_reconcile_value` silently returns `None` forever for that key, forcing a full resync every 10 s (cosmetic but permanent bug).

**`_derive_action` (utility, transform) — exact current body to extend:**
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
Extend with additional `if`/`elif` branches for `"Cutting Filament"`, `"Preload"`, etc., in the same flat if-return style — no dictionary dispatch or class hierarchy, matching existing convention.

**`flowguard.level` derivation** — new pure-function pattern, illustrative (RESEARCH.md §"Recommended derivation"), same flat-return style as `_derive_action`:
```python
def _flowguard_level(sync_enabled, tension_dwell_ms, compression_dwell_ms,
                      tension_stop_ms=6000, compression_stop_ms=5000):
    if not sync_enabled or (not tension_dwell_ms and not compression_dwell_ms):
        return 0.0, ""
    if tension_dwell_ms > compression_dwell_ms:
        frac = min(1.0, tension_dwell_ms / max(1, tension_stop_ms))
        return -frac, "tangle" if frac >= 1.0 else ""
    frac = min(1.0, compression_dwell_ms / max(1, compression_stop_ms))
    return frac, "clog" if frac >= 1.0 else ""
```

---

### `scripts/test_flare_mmu_status.py` — new `check()` blocks (test, request-response)

**Analog:** itself, `scripts/test_flare_mmu_status.py:1-305` (full file already read; imports/fakes at `:1-119`, reconcile block at `:231-288`, purge-delegation block at `:290-295`)

**Imports/setup pattern** (`:1-24`):
```python
import os, sys, types
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "klipper"))
import mmu  # noqa: E402
sys.modules.setdefault("serial", types.SimpleNamespace())  # flare_daemon import guard
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import flare_daemon  # noqa: E402
import functest_adapter  # noqa: E402
```
No `unittest.TestCase` — hand-rolled `check(name, cond, detail)` + `run_tests()`, exposed to `unittest discover` via:
```python
RunnerTests = functest_adapter.testcase_from_callable(run_tests)  # unittest discover entry
```
**Reconcile-block pattern to extend** (`:231-283`, verbatim excerpt):
```python
fields = {
    "NUM_GATES": "2",
    "ACTIVE_GATE": "0",
    ...
    "GATE_FILAMENT_NAME": "'Gate 0,Gate 1'",
}
m, p = new_mock()
m.cmd_SET_MMU(FakeGcmd(fields))
status = m.get_status(0)
check("reconcile matches full field set",
      flare_daemon._mmu_status_matches_fields(status, fields), status)
```
Every new `SET_MMU`-pushable key this phase adds must be inserted into this `fields` dict (string-formatted the same way `SET_MMU` formats it) or `_mmu_status_matches_fields` coverage silently misses it.

**Command-stub delegation test pattern** (`:290-295`, verbatim, copy for new stub tests):
```python
m, p = new_mock()
m.cmd_MMU_SET_PURGE(FakeGcmd({"PURGE": 42.5}))
check("MMU_SET_PURGE invokes _FLARE_SET_PURGE",
      p._gcode.commands == ["_FLARE_SET_PURGE PURGE=42.5"],
      p._gcode.commands)
```
For bare no-op stubs (no delegation), the analogous assertion is simply "calling `cmd_MMU_NOOP`-bound handler does not raise" — no return-value or side-effect check needed, matching the stub's own `pass` body.

**New `test_status_fields_exist_before_ready`-style block** (no existing analog in this file — new pattern per RESEARCH.md, follows the SAME `check()` idiom as everything else in the file, just against a fresh un-`SET_MMU`'d mock):
```python
m, p = new_mock()  # fresh instance, no cmd_SET_MMU call yet
s = m.get_status(0)
EXPECTED_KEYS = { ... }  # full schema incl. new keys
check("all schema keys present before first SET_MMU push",
      EXPECTED_KEYS <= set(s.keys()), sorted(EXPECTED_KEYS - set(s.keys())))
```

---

## Shared Patterns

### Three-location field registration (delta-push + reconcile)
**Source:** `scripts/flare_daemon.py:1547-1582` (`fields` dict), `:1428-1442` (change-detection `keys`), `:1210-1272` (`_MMU_RECONCILE_*` tables)
**Apply to:** every new daemon-mirrored `SET_MMU` field (`flowguard.level`/`trigger`, `sync_drive`, expanded `action`, any other daemon-computed key)
**Rule:** add the key to all three locations in the same commit or the reconcile logic silently drifts (Pitfall 4).

### `get_status()` must stay synchronous, no I/O
**Source:** `klipper/mmu.py:1427-1497` (`get_status`)
**Apply to:** all new `get_status()` keys — compute derived values in the daemon and push via `SET_MMU`; `get_status()` only reads `self.*` attributes.

### Stub-command registration
**Source:** `klipper/mmu.py:564-566` (`cmd_MMU_NOOP`) + `:168-170` (registration)
**Apply to:** all 9 missing command stubs (`klipper/mmu.py`)

### Test idiom: hand-rolled `check()`, no `TestCase`, exposed via `functest_adapter`
**Source:** `scripts/test_flare_mmu_status.py` (whole file), `scripts/functest_adapter.py:1-13`
**Apply to:** every new assertion in this phase — do not introduce a `unittest.TestCase` subclass; extend `run_tests()`.

## No Analog Found

None — this phase is a pure extension of existing files; every touched location already contains the pattern to copy from itself. If the planner decides to split action-string tests into a new file (`scripts/test_flare_daemon_action.py`, mentioned as an option in RESEARCH.md's Validation Architecture table), the analog for that new file is `scripts/test_flare_mmu_status.py`'s own `check()`/`run_tests()`/`functest_adapter` idiom (same as above) — not a new pattern.

## Metadata

**Analog search scope:** `klipper/mmu.py`, `klipper/mmu_sensors.py`, `scripts/flare_daemon.py`, `scripts/test_flare_mmu_status.py`, `scripts/functest_adapter.py`, `scripts/validate_regression.py`, `klipper/flare_mmu.cfg` — all confirmed git-tracked via `git ls-files`.
**Files scanned:** 7 (all read/grepped this session or the prior RESEARCH.md session, cross-verified with targeted `sed -n` reads for exact excerpts)
**Pattern extraction date:** 2026-09-12
**Note:** All code excerpts above were re-verified by direct `sed -n` read this session (not solely copied from RESEARCH.md) for `klipper/mmu.py:555-571` and `scripts/test_flare_mmu_status.py:1-30,225-300`; RESEARCH.md's own `[VERIFIED]` line citations were relied on for excerpts not re-read this session (`scripts/flare_daemon.py` sections, `klipper/mmu.py:1427-1497`).
