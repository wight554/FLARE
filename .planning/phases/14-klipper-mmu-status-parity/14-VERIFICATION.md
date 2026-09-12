---
phase: 14-klipper-mmu-status-parity
verified: 2026-09-12T00:00:00Z
status: human_needed
score: 5/5 must-haves verified
covered_files:
  - .planning/REQUIREMENTS.md
  - .planning/phases/14-klipper-mmu-status-parity/14-01-PLAN.md
  - .planning/phases/14-klipper-mmu-status-parity/14-01-SUMMARY.md
  - .planning/phases/14-klipper-mmu-status-parity/14-02-PLAN.md
  - .planning/phases/14-klipper-mmu-status-parity/14-02-SUMMARY.md
  - .planning/phases/14-klipper-mmu-status-parity/14-PATTERNS.md
  - .planning/phases/14-klipper-mmu-status-parity/14-RESEARCH.md
  - .planning/phases/14-klipper-mmu-status-parity/14-VALIDATION.md
  - .planning/phases/14-klipper-mmu-status-parity/deferred-items.md
  - AGENTS.md
  - KLIPPER.md
  - klipper/mmu.py
  - scripts/flare_daemon.py
  - scripts/test_flare_mmu_status.py
  - scripts/validate_regression.py
covered_digest: "v1:sha256:58e2f4258979534223092482604f62b004d9193cc875ac919ccd78621fd7a232"
behavior_unverified: 0
overrides_applied: 0
human_verification:
  - test: "Open a live Fluidd or Mainsail MMU panel (real Moonraker + daemon connected) with sync active and drive tension/compression dwell timers toward their trip thresholds"
    expected: "The FlowGuard meter is visible in the panel and its needle/level moves toward the clog (compression) or tangle (tension) side as the corresponding dwell timer approaches its stop threshold, returning to 0 when sync is deactivated"
    why_human: "Requires a live Moonraker/Fluidd or Mainsail session against a running (or HW) rig; cannot be observed from an offline unit test of _flowguard_level()/get_status()"
  - test: "In a live Fluidd/Mainsail session, click each of the newly-stubbed MMU maintenance dialogs: MMU_LED, MMU_TEST_CONFIG, MMU_GRIP, MMU_RELEASE, MMU_SERVO (and the *_VARS dialogs)"
    expected: "Each dialog submits without the Klipper console showing an 'Unknown command' error; FLARE acknowledges (no-op) the command"
    why_human: "Requires a live Klipper/Moonraker gcode dispatch through the actual UI dialog flow, not just calling the Python handler directly in a unit test"
---

# Phase 14: Klipper MMU Status Parity Verification Report

**Phase Goal:** Keep the `klipper/mmu.py` Happy-Hare facade and daemon mirror rendering correctly in current Fluidd/Mainsail/KlipperScreen builds (verified against their `develop` sources)
**Verified:** 2026-09-12
**Status:** human_needed
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths (ROADMAP Success Criteria)

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | `printer.mmu.flowguard` dict published, `level` derived from dwell timers, 0 when sync inactive | ✓ VERIFIED | `_flowguard_level()` (`scripts/flare_daemon.py:1387-1411`) returns `(0.0, "")` unconditionally when `sync_active` is `False` (line 1399-1400), matching REQ text verbatim in its own docstring ("Sync inactive always forces (0.0, "") regardless of dwell state (Success Criterion 1)"). `level` moves toward `+1.0` via `compression_frac` and toward `-1.0` via `tension_frac`, both computed from `tension_dwell_ms`/`compression_dwell_ms` against stop thresholds. Dict shape (`enabled/active/trigger/reason/level/max_clog/max_tangle`) is built at `klipper/mmu.py:1536-1544` and exercised by 34+ passing unit-test assertions in `scripts/test_flare_mmu_status.py`. Verified independently by running `python3 -m unittest scripts.test_flare_mmu_status -v` myself (88 passed, 0 failed, exit 0) — not taken from SUMMARY claims. |
| 2 | All listed `printer.mmu` status keys present with correct types + `happy_hare_version` | ✓ VERIFIED | Every key named in the ROADMAP criterion (`endless_spool_groups`, `sync_feedback_flow_rate`, `sync_drive`, `reason_for_pause`, `operation`, `last_tool`/`next_tool`, `is_paused`, `has_bypass`, `unit`, `filament_direction`, `gate_temperature`, `slicer_tool_map`, `espooler`, `drying_state`, `encoder: None`, …) is present in `klipper/mmu.py`'s `MMUMock.get_status()` (confirmed via grep at lines 1545-1601) and `mmu_machine.happy_hare_version` is set at `klipper/mmu.py:21` (`'4.0.0'`) inside `MMUMachineMock.get_status()`. The 93-key schema-presence test (Truth 5 below) asserts all of these exist with the correct type on a fresh mock. `endless_spool_groups`/`endless_spool_enabled` are `[ASSUMED]`-documented mappings to RELOAD per plan intent, not fabricated. |
| 3 | `MMU_TEST_CONFIG`, `MMU_LED`, `MMU_GRIP`/`MMU_RELEASE`/`MMU_SERVO`, `MMU_PRINT_START`/`MMU_PRINT_END`, `*_VARS` dialogs register as ack/no-op | ✓ VERIFIED | All 9 command names are registered in `MMUMock.__init__` (`klipper/mmu.py:202-218`): 7 bound to `cmd_MMU_NOOP` (pure `pass`, `klipper/mmu.py:621-623`), `MMU_PRINT_START`/`MMU_PRINT_END` bound to `cmd_MMU_PRINT_SYNC` which delegates to the pre-existing `_FLARE_SYNC_TOOLHEAD` macro with no `RESET` param (`klipper/mmu.py:630-634`, matching the macro's own CANCEL_PRINT-only contract). Confirmed via `grep` and by the passing "command stubs — 9 Fluidd/Mainsail maintenance/print-lifecycle commands registered" test block. |
| 4 | `action` reports full HH vocabulary (Cutting Filament, Preload, Loading, Unloading, …) from `EV:` events | ✓ VERIFIED | `_derive_action()` (`scripts/flare_daemon.py:1437-1464`) returns `"Cutting Filament"` for `UNLOAD_CUT`/`UNLOAD_WAIT_CUT` (checked before the generic `UNLOAD`-prefix branch, so it doesn't get swallowed), `"Preload"` as a fallback-from-Idle when a recent `EV:PRELOAD` was seen via `_recent_event_seen()` (bounded reverse-scan over `event_history`, decay window `_ACTION_PRELOAD_DECAY_S=2.0`), plus the pre-existing `Loading`/`Unloading`/`Idle`. Wiring confirmed end-to-end: `klipper_syncer` computes `recent_preload` and passes it into `_derive_action(...)` (`scripts/flare_daemon.py:1650-1654`), whose result flows into the `fields["ACTION"]` SET_MMU push (line 1697) that lands in the mock's `action` status key. `"Forming Tip"/"Heating"/"Selecting"/"Checking"/"Homing"/"Purging"` are explicitly and honestly documented as having no live FLARE signal (no physical selector/heater/encoder) rather than fabricated — this is a legitimate, disclosed scope boundary, not a gap. |
| 5 | A `test_status_fields_exist_before_ready`-style schema test exists and passes | ✓ VERIFIED | `scripts/test_flare_mmu_status.py:509-539` builds a fresh `MMUMock` via `new_mock()` with **no** `cmd_SET_MMU` call, calls `get_status(0)`, and asserts a 93-key `EXPECTED_STATUS_KEYS` frozenset is a subset of the returned keys, reporting `sorted(EXPECTED_KEYS - set(status.keys()))` as failure detail. I ran this test myself: `python3 -m unittest scripts.test_flare_mmu_status -v` → `88 passed, 0 failed`, exit code 0, including `PASS status schema keys present on a never-SET_MMU'd fresh mock`. |

**Score:** 5/5 truths verified (0 present-but-behavior-unverified)

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `klipper/mmu.py` | FlowGuard dict, 23 new/renamed keys, `happy_hare_version`, 9 command stubs, `cmd_MMU_PRINT_SYNC` | ✓ VERIFIED | 1605 lines; all keys/handlers present and wired (grep-confirmed at cited line numbers) |
| `scripts/flare_daemon.py` | `_flowguard_level`, `_recent_event_seen`, `_derive_action` extension, reconcile-table nested-tuple support, `fields` dict push | ✓ VERIFIED | 1928 lines; pure functions unit-tested, `fields` dict at :1692-1738 pushes every new SET_MMU param |
| `scripts/test_flare_mmu_status.py` | 93-key schema test, 34+15 new checks across both plans | ✓ VERIFIED | 548 lines; 88 checks pass (re-run by me, not trusted from SUMMARY) |
| `scripts/validate_regression.py` | Python-syntax glob widened to include `klipper/*.py` | ✓ VERIFIED | `py_files` union confirmed at line 115 |
| `.planning/REQUIREMENTS.md` | 6 `REQ-klipper-status-parity-*` entries | ✓ VERIFIED | All 6 present at lines 631-672 with full descriptions matching ROADMAP criteria |
| `.planning/ROADMAP.md` | Phase 14 `Requirements:` line updated from `TBD` to the 6 REQ IDs | ✓ VERIFIED | Confirmed via `git log` — commit `8093ffa` "docs(14): register phase 14 REQ IDs in ROADMAP.md"; line 188 in current ROADMAP.md lists all 6 IDs |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| Firmware `TT:`/`CT:` dwell telemetry | `printer.mmu.flowguard.level` | `_flowguard_level()` → `klipper_syncer` fields dict → `cmd_SET_MMU` → `get_status()['flowguard']` | ✓ WIRED | Full chain read and confirmed in code (lines 1660-1738 daemon side; 255-264, 1536-1544 mmu.py side) |
| `EV:PRELOAD` event | `action = "Preload"` | `add_event_to_history` → `event_history` → `_recent_event_seen` → `_derive_action(recent_preload_event=True)` | ✓ WIRED | `klipper_syncer` call site at line 1650-1654 passes the computed flag into `_derive_action` |
| Fluidd/Mainsail dialog gcode | ack/no-op | `register_command(...cmd_MMU_NOOP)` / `cmd_MMU_PRINT_SYNC` → `_FLARE_SYNC_TOOLHEAD` | ✓ WIRED | Registrations at lines 202-218; handlers at 621-634 |
| `tc_state`/lane task | `action` string | `_derive_action` → `fields["ACTION"]` → `cmd_SET_MMU` → `get_status()['action']` | ✓ WIRED | Same push mechanism as FlowGuard; reconcile table also supports read-back (`_MMU_RECONCILE_STRINGS` includes `ACTION`) |

### Behavioral Spot-Checks / Test Runs (run by verifier, not trusted from SUMMARY)

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| Phase-specific test module passes | `python3 -m unittest scripts.test_flare_mmu_status -v` | `88 passed, 0 failed`, exit 0 | ✓ PASS |
| Full script test suite (regression check) | `python3 -m unittest discover -s scripts -p 'test_*.py'` | `Ran 261 tests in 2.277s` / `OK`, exit 0 | ✓ PASS — note: this environment has `build_sim/flare_sim` present, so the `test_sync_sim.py` gap logged in `deferred-items.md` (missing binary) does not reproduce here; full suite is green with zero errors, a stronger result than the SUMMARY's own "10 errors, pre-existing gap" claim |
| Python syntax check (scripts + klipper mock) | `python3 -m py_compile scripts/*.py klipper/*.py` | Clean, exit 0 | ✓ PASS |
| `validate_regression.py` full gate | not run to completion | `PICO_SDK_PATH` unset in this environment | ? SKIP — pre-existing, unrelated environment gap per `deferred-items.md`; this phase makes no firmware changes, so this does not block Phase 14's own scope |

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|-------------|--------------|--------|----------|
| REQ-klipper-status-parity-flowguard-dict | 14-01 | FlowGuard dict from TT:/CT: dwell timers | ✓ SATISFIED | Truth 1 above |
| REQ-klipper-status-parity-missing-keys | 14-01 | All FLARE-sourced `printer.mmu` keys with correct types | ✓ SATISFIED | Truth 2 above |
| REQ-klipper-status-parity-hh-version | 14-02 | `happy_hare_version` exposed | ✓ SATISFIED | Truth 2 above; verification path (checked live Fluidd/Mainsail `develop` sources, found no version-gated branch) documented in `klipper/mmu.py:11-20` comment and 14-02-SUMMARY.md |
| REQ-klipper-status-parity-command-stubs | 14-02 | 9 dialog commands ack/no-op | ✓ SATISFIED | Truth 3 above |
| REQ-klipper-status-parity-action-strings | 14-02 | Expanded `action` vocabulary | ✓ SATISFIED | Truth 4 above |
| REQ-klipper-status-parity-schema-test | 14-02 | Schema-presence test | ✓ SATISFIED | Truth 5 above |

No orphaned requirements found — `.planning/REQUIREMENTS.md` maps exactly these 6 IDs to Phase 14, all 6 are claimed across the two plans' frontmatter and satisfied.

### Anti-Patterns Found

None. Scanned `klipper/mmu.py`, `scripts/flare_daemon.py`, `scripts/test_flare_mmu_status.py`, `scripts/validate_regression.py` for `TBD|FIXME|XXX|TODO|HACK|PLACEHOLDER` and placeholder-language patterns (`placeholder`, `coming soon`, `not yet implemented`, `not available`) — zero matches in any phase-modified file.

### Human Verification Required

1. **FlowGuard meter renders and moves in a live Fluidd/Mainsail MMU panel**
   - **Test:** With a live Moonraker + Fluidd/Mainsail session and the daemon connected, drive tension/compression dwell timers toward their trip thresholds during active sync.
   - **Expected:** The FlowGuard meter is visible and its level moves toward clog/tangle sides accordingly, returning to 0 when sync deactivates.
   - **Why human:** Requires a live browser/Moonraker rendering session; not observable from an offline unit test of the underlying pure function. (Already flagged as HW-only in `14-VALIDATION.md`'s "Manual-Only Verifications" table by the plan authors themselves — not a phase gap.)
   - **CONFIRMED 2026-09-12 (data side)** — real 2h Type-P print, 975 `SET_MMU FLOWGUARD_LEVEL=…` pushes captured from Moonraker `server/gcode_store` (the same value `printer.mmu.flowguard.level` exposes). Compression side ramps 0.756 → 0.996 in 40 ms steps (`t=95.6–96.8 s`, again at `t≈43 s`), `FLOWGUARD_MAX_CLOG` tracks the high-water mark, tension side reaches −0.494. The next push after 0.996 is `SYNC_DRIVE=0 FLOWGUARD_ACTIVE=0 FLOWGUARD_LEVEL=0.000` (`t=96.9 s`, ≈100 ms later; same at `t=230.2 s`), and re-arm restarts from 0.000. The meter rendering itself (widget visible in Fluidd/Mainsail) is a browser eyeball check — user has observed the UI change; not re-logged.

2. **Maintenance dialogs do not raise "Unknown command" in a live UI session**
   - **Test:** Click MMU_LED, MMU_TEST_CONFIG, MMU_GRIP, MMU_RELEASE, MMU_SERVO (and `*_VARS`) dialogs in a live Fluidd/Mainsail session.
   - **Expected:** Each submits cleanly; Klipper console shows FLARE's ack, no "Unknown command" error.
   - **Why human:** Requires live gcode dispatch through the real UI dialog flow and a running Klipper instance; a unit test calling the Python handler directly proves the handler doesn't raise, but not that Klipper's command-registration path resolves correctly end-to-end in a live session.
   - **CONFIRMED 2026-09-12 (7/9)** — `verify_hw_open_items.py fire 15-command-stubs` on rig dispatched all 7 no-op stubs through the real Moonraker `/printer/gcode/script` -> Klipper path (functionally equivalent to the UI dialog flow's command dispatch, though not literally mouse-clicked in Fluidd/Mainsail): all 7 registered cleanly, no "Unknown command". `MMU_PRINT_START`/`MMU_PRINT_END` remain unconfirmed — deliberately excluded by default (they run the real `_FLARE_SYNC_TOOLHEAD` macro, not a no-op; see `MANUAL_TEST_PLAN.md`), need `--include-print-sync` with sync idle and buffer already settled.

### Gaps Summary

No gaps found. All five ROADMAP Success Criteria for Phase 14 are supported by code that I read and tested directly (not merely asserted by SUMMARY.md), and the corresponding commits (`c243446..8093ffa`) are present in the current `main` history. The two open items are HW-only UI-rendering checks that the plan's own `14-VALIDATION.md` correctly scoped as manual-only from the start — they are not implementation gaps, they are inherent limits of what an offline host-tooling phase can self-verify. The two pre-existing environment gaps (missing `flare_sim` binary, missing `PICO_SDK_PATH`) noted in `deferred-items.md` are confirmed unrelated to this phase's scope; in this verification environment the `flare_sim` binary happens to already exist, so the full `unittest discover` suite actually runs clean (261 tests, `OK`) — a stronger result than what either SUMMARY reported for their own worktrees.

---

*Verified: 2026-09-12*
*Verifier: Claude (gsd-verifier)*
