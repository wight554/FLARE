# Proposal: audit-hardening-fixes

## Why

Full audit (2026-06-10) of firmware (`firmware/src`, `firmware/include`) + host scripts + docs + Klipper/WebUI integration, plus a verified second-pass (Gemini gap audit), found 27 code defects and a doc-drift cluster: 3 wire-protocol bugs already live (double `EV:` prefix, dead tuner event regex, daemon `RL` completion timeout), 1 unauthenticated network actuator surface (daemon binds `0.0.0.0`), 2 dead validation guards (settings parity test broken AND never executed), toolchange ownership not enforced against host commands, plus flash-trust, cutter-hijack, and TMC shadow-desync hazards. Docs are the root cause of the tuner bug: MANUAL.md/BEHAVIOR.md document events in comma format (`EV:SYNC,FAULT_HOLD`) contradicting MANUAL.md's own normative `EV:TYPE:DATA`; the tuner was coded to the docs. All small surgical fixes; none change control laws.

## What Changes

Firmware:

- **F1** `sync.c:718,863,932`: `cmd_event("EV:BL", ...)` emits `EV:EV:BL:*` — strip redundant prefix → `cmd_event("BL", ...)`.
- **F2** `protocol.c` `CAL:PSF_*`: add `controller_activity_in_progress()` gate (`PERSIST_BUSY`) before `settings_save()` — matches SV/LD/RS; enforces "never write flash mid-motion".
- **F3** `protocol.c` `SET:MICROSTEPS`: reject non-power-of-2 (today: `tmc_setup_chopconf` returns false ignored, `mm_per_step` corrupted, bad value persisted).
- **F4** `settings_store.c` load path: clamp/validate flash-loaded `tmc_microsteps`, `tmc_full_steps`, `tmc_gear_ratio`, `tmc_rotation_distance` before `mm_per_step` division and `256/microsteps` (boot hardfault possible on layout drift).
- **F5** `cutter.c` / `protocol.c` `CP`: reject when cutter state not IDLE/BOOT_PARK (today hijacks in-flight TC cut; TC proceeds on uncut filament).
- **F6** `protocol.c` events: fault-class events (`FAULT:*`, `CUT:ERROR`, `TC:ERROR`, `RELOAD:FAULT`, BL `TIMEOUT`) bypass 8-per-100ms best-effort budget.
- **F7** `protocol_tmc.c` `TW` reg==CHOPCONF: mirror value into `tmc->chopconf` cache (stale vsense compare → wrong current scale).
- **F8** toolchange ownership not enforced against host commands: `T:n` mid-TC flips `g_active_lane` under the running state machine (`tc_tick` then reads the wrong lane's sensors and advances on false "cleared"); `TC:`/`RL:` silently no-op when TC busy yet still reply `OK`; `UL`/`UM`(active)/`LO`/`FL`/`FD` start competing lane tasks mid-TC. Gate these with `ER:BUSY` while `tc_state()` not IDLE/ERROR (`MV` stays unguarded by design for raw recovery).
- **F9** `toolchange.c:526-527`: `tc_tick_reload_follow` dereferences `lane->task_dist_mm` then NULL-checks `lane` on the next line (inverted guard); `tc_tick_reload_approach` guards NULL (396), follow does not. Latent crash if `g_active_lane` invariant ever breaks. Mirror the approach guard.
- **F10** `sync.c:586-589`: baseline learner is raise-only into `g_flow_sched_live_delta[segment]` with no clamp and no reset at sync-session boundary (resets only via `SET:BASELINE_RATE` or settings reload, sync.c:420/435); delta feeds back into `flow_param()` (475-499) so each accept compounds — within one power-on session spanning multiple prints the control baseline floor ratchets upward, never decays. Needs design call: reset deltas on sync OFF transition, or add decay/clamp.
- **F11** `sync_buf.c:892/896/910`: both signal branches stamp `g_buf_analog_last_sample_ms = now_ms` then compute `g_buf_signal.age_ms = now_ms - last_sample` in the same pass — telemetry `age_ms` is constant 0, dead field.
- Cleanups: delete dead `Y_TO_BUF_NEUTRAL` macro (`main.c:220`, references nonexistent `DIST_Y_BUF`); clamp `pos` in `RR` chained snprintf (`protocol_tmc.c:115`); `sscanf %i`→`%x` for `TW` value; `STATUS_LINE_MAX` headroom below `CMD_LINE_MAX`; ms-valued SET clamps misusing `PATH_DIST_MAX_MM` + dead `RELAY_TRIM_*_MAX_SPS` constants (`protocol.c:1049,1076-1078,1103-1107`).

Scripts:

- **S1** `flare_daemon.py`: default bind `127.0.0.1` (was `0.0.0.0`, no auth — LAN could drive motors/cutter/settings/BOOTSEL). Non-local bind requires explicit `--host`. **BREAKING** for remote WebUI setups relying on default.
- **S2** `test_settings_parity.py`: scan `settings_load_*` helper bodies (refactor broke it — fails on current tree); wrap in `unittest.TestCase` so `validate_regression.sh` discover actually executes it.
- **S3** `flare_live_tuner.py`: event matching uses comma (`EV:SYNC,FAULT_HOLD`); firmware emits colon — FAULT_HOLD/TENSION_RISK_HIGH/EST_FALLBACK reactions dead. Fix to colon + wire-format test.
- **S4** `flare_daemon.py:616`: add `RELOAD` to two-part event whitelist; `flare_cmd.py:336`: rebuild event line with colon not comma — `RL` daemon-path completion currently always times out.
- **S5** all `serial.Serial(...)` opens: `exclusive=True` (daemon, flare_cmd, live_tuner, sync_check, baseline_recommender) — Linux CDC multi-open reply theft.
- **S6** `gen_config.py`: validate `microsteps` power-of-2; hard-error on `rotation_distance <= 0` (silent `0.0125` fallback).
- **S7** `klipper_motion_tracker.py:130-143,151-153`: `poll()` drains `self._messages` before the socket; `list_objects` re-appends any id-mismatched message and loops — one unsolicited Klipper message mid-wait = deterministic infinite CPU spin (poll returns instantly forever, timeout never fires).
- **S8** `flash_flare.sh:139-148,320-343`: `find_and_mount_rp2` defined but never called; UF2 fallback raw `sudo mount /dev/sd*1` fails on automounted boards (desktop Linux `/media/*`, macOS `/Volumes/RPI-RP2`) and never scans macOS `/dev/disk*`. Fallback-only (picotool USB load is primary path).

Klipper / WebUI integration (mmu mock, daemon mirror, macros, installer):

- **K1** `klipper/mmu.py:897-906`: `FLARE_WAIT_UNLOAD` picks sensors/task via `active_gate == 0 ? lane1 : lane2` — `active_gate` of `-1`/`-2` silently watches lane 2, `unload_completed` can latch on the wrong lane's idle state → premature "Unload complete". Guard gate ∈ {0,1}, else derive from loaded gate or error.
- **K2** `klipper/mmu.py` wait loops: daemon-unreachable swallowed (`except: pass`) every poll — only exit is the full timeout with a misleading "Unload timed out"/"Toolchange timed out" error. Surface daemon-down after N consecutive failures.
- **K3** gate-map restore is single-shot: `_load_vars` pulls `/config` once at klippy init (2 s timeout); daemon SET_MMU pushes `GATE_STATUS` but never `GATE_COLOR`/`GATE_MATERIAL`/`GATE_SPOOL_ID`/`GATE_NAME` (flare_daemon.py:1216 vs mock fields); service unit orders only `After=network.target` (no klipper ordering) — klippy-before-daemon boot race leaves colors/spools/names at defaults until a manual reload. Include gate-map fields in the force-full push (or retry `_load_vars`).
- **K4** `klipper/mmu.py:1340-1341,975-977` fallback `bowden_length=1000`/`extruder_to_nozzle=117` diverge from `flare_mmu.cfg:8-9` (`1800`/`125`) — wrong synthetic filament position whenever `_FLARE_VARS` lookup fails. Align defaults.
- **K5** `flare_daemon.py:518,1092,1096,1134`: type-D piston scale hardcoded `/15.0` half-travel; firmware `BUF_MAX_TRAVEL` is runtime-tunable (tune.h default 25, reference rig 16) — piston display under/over-ranges. Derive half-travel from board config.
- **K6** `install_daemon.sh:144` never installs `flare_mmu.cfg` (echo-only reminder); extras-dir `mmu.py`/`mmu_sensors.py` copies are wiped by Klipper updates (git clean / update-manager recovery). Install or document the cfg copy explicitly + reinstall-after-Klipper-update note.
- **K7** `klipper/mmu_sensors.py:26-30,98-102` blanking model (gate ≠ active) differs from `mmu.py` `get_status` path cascade (gear-clear anchored, mmu.py:1334-1336) — sensor dots and filament widget can contradict. Unify on the path cascade.
- **K8** cosmetics: `install_daemon.sh:10` bogus `NC` color code; service unit has no documented ordering/`Wants=` rationale.

Docs:

- **D1** comma→colon event format: MANUAL.md (4 sites: 226, 227, 234, 308) + BEHAVIOR.md (9 sites) write `EV:TYPE,DATA`; normative format is `EV:TYPE:DATA` (MANUAL.md:9). Source of tuner bug S3.
- **D2** BEHAVIOR.md:193 names BL watchdog event `EV:BL,WATCHDOG`; firmware emits `EV:BL:TIMEOUT`.
- **D3** MANUAL.md events table incomplete: BL family (`PRIME`, `LOCKED`, `FOLLOW`, `FOLLOW_DONE`, `FOLLOW_GATED`, `PRIME_BOUND`, `TIMEOUT`) absent; `CUT:DONE`/`CUT:ERROR` absent; `BUF_STAB` row missing `REVERSE`; `SYNC` row missing `RELIEF_PAUSE`, `NEUTRAL_CREEP_CAP`, `cannot_refill`, `cannot_relieve`.
- **D4** daemon HTTP API (port 8088, bind address, `/cmd` `/status` `/telemetry`) undocumented; KLIPPER.md must document loopback default + explicit LAN opt-in after S1.
- **D5** KLIPPER.md:22 claims installer "creates the serial symlink `/dev/ttyACM0` proxy" — `install_daemon.sh` creates no symlink.
- **D6** CONTEXT.md:31 + MANUAL.md:374 call tuner "observe-only" unqualified — actual: observe-only by default, guarded SET writes via `--allow-bias-writes`/`--allow-baseline-writes`.
- **D7** MANUAL.md CAL rows (89-91) gain `ER:PERSIST_BUSY` note after F2; CP row (79) gains busy-rejection note after F5; `T:`/`TC:`/load rows gain TC-busy note after F8.
- **D8** MANUAL.md core `?:` status fields undocumented — Diagnostic Status Fields table (279+) covers `RT`..`RDC` only; `LN`, `TC`, `L1T`/`L2T`, `I1`/`O1`/`I2`/`O2`, `TH`, `YS`, `BUF`, `MM`, `BF`, `BP`, `SM`, `ST`, `TPR`, `CU`, `RELOAD`, `UC`, `BST`, `EST`, `RE`, `AV`, `SC` have no table.
- **D9** MANUAL.md:102 documents `CW:lane:reg:val` — firmware command is `TW` (protocol_tmc.c:57); documented command returns `ER:UNKNOWN`.
- **D10** MANUAL.md:282 says diagnostics are "appended after `SS:`" — no `SS:` marker exists in the status line (protocol_status.c).

## Capabilities

### New Capabilities

- `serial-port-ownership`: exclusive serial access rules for host tools — `exclusive=True` opens, daemon-proxy preference, single-owner invariant.

### Modified Capabilities

- `persistence-contract`: flash writes MUST be activity-gated for ALL commands (CAL included); load path MUST validate values feeding divisions before use.
- `daemon-klipper-mirror`: daemon binds loopback by default; non-local bind explicit opt-in. Event-type split table MUST cover `RELOAD`. Full-resync push MUST include gate-map fields so a klippy restart recovers colors/materials/spool ids without daemon restart ordering luck.
- `static-regression-validation`: gate MUST execute settings parity check (unittest-discoverable).
- `live-tuner`: event parsing MUST match firmware colon wire format (`EV:TYPE:DATA`).
- `project-architecture`: serial protocol — event type MUST NOT include `EV:` prefix (single-prefix invariant); fault-class events exempt from best-effort budget; toolchange ownership gating — lane-mutating commands rejected `ER:BUSY` mid-TC, no silent-no-op-with-OK replies.
- `cutter-feed-timeout`: `CP` servo test MUST be rejected while cut sequence active.

## Impact

- Firmware: `sync.c`, `protocol.c`, `protocol_tmc.c`, `cutter.c`, `settings_store.c`, `main.c`, `protocol_status.c`. No control-law change; no `SETTINGS_VERSION` bump needed (no `settings_t` layout change).
- Scripts: `flare_daemon.py`, `flare_cmd.py`, `flare_live_tuner.py`, `gen_config.py`, `test_settings_parity.py`, `serial_utils.py` consumers, `flare_sync_check.py`, `flare_baseline_recommender.py`.
- Docs: `MANUAL.md`, `BEHAVIOR.md`, `KLIPPER.md`, `CONTEXT.md`, `TEST_CASES.md` (new mid-TC injection cases).
- Host consumers of `EV:EV:BL:*` (none correct today — fix is strictly host-visible improvement).
- F8 tightens host-visible command semantics mid-TC: previously-accepted `T:`/`UL:`/`LO:`/`FL:`/`FD:` during toolchange now return `ER:BUSY` — Klipper macros must not issue them mid-TC (none do today).
- Hardware validation needed only for: CAL gate behavior, CP rejection mid-cut, TC-busy rejections, BL timeout event visibility. Rest covered by build + unit tests.
