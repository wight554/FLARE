# FLARE Test Cases

Practical bring-up and regression checklist for real hardware.

Use this document for repeatable validation after firmware changes, new wiring,
or tuning updates. It is intentionally operator-facing: every test has a clear
setup, command sequence, and expected result.

---

## Safety

- Start with reduced speeds for first motion on new hardware or after major firmware changes.
- Keep filament clear of the toolhead path unless the test explicitly requires a full load.
- Be ready to send `ST:` immediately if direction, sensor polarity, or lane selection looks wrong.
- Do not run RELOAD tests unattended.

Recommended temporary low-speed setup for first validation:

```bash
python3 scripts/flare_cmd.py \
  "SET:FEED_RATE:600" \
  "SET:REV_RATE:600" \
  "SET:AUTO_RATE:400" \
  "SET:JOIN_RATE:400" \
  "SET:PRESS_RATE:300"
```

---

## Preconditions

- Firmware builds successfully.
- `config.ini` matches the target hardware.
- Board flashes and enumerates over USB CDC.
- `python3 scripts/flare_cmd.py "VR:" "?:"` returns valid replies.
- IN / OUT sensors, optional Y sensor, and optional toolhead reporting path are wired as expected.

If this is a new setup, run the build and flash flow in `BUILD_FLASH.md` first.

---

## Code Analysis Regression Gate

Run this section before hardware validation when a change touches firmware,
scripts, config generation, persistence, or the serial protocol. These checks
do not prove runtime behavior, but they catch the most common integration
breaks before you flash a board.

Quick path:

```bash
python3 scripts/validate_regression.py
```

This runs the default static gate in one command. Use the detailed cases below
when you need to understand which layer failed.

### A. Generated Config Sync

#### Goal

Confirm config generation still works and the generated header matches the
current source inputs.

#### Steps

```bash
python3 scripts/gen_config.py
git diff --check
```

#### Expected Result

- `gen_config.py` completes without errors.
- No malformed generated content is introduced.
- If the change intentionally modifies compile-time defaults, the resulting
  `tune.h` update is expected and explainable.

#### Use When

- `config.ini`
- `config.ini.example`
- `scripts/gen_config.py`
- `firmware/include/tune.h` consumers

### B. Firmware Compile Regression

#### Goal

Confirm the firmware still compiles against the real Pico SDK target.

#### Steps

```bash
ninja -C build_local
```

#### Expected Result

- Build completes successfully.
- No new warnings or link failures appear in the touched area.
- Extracted modules still resolve all shared symbols and headers correctly.

#### Use When

- Any file under `firmware/`
- Any generated compile-time config change

### C. Python Helper Syntax Regression

#### Goal

Catch syntax errors in host-side helper scripts before runtime testing.

#### Steps

```bash
python3 -m py_compile scripts/*.py
```

#### Expected Result

- Command exits cleanly with no syntax errors.

#### Use When

- Any file under `scripts/`

### D. Diff Hygiene Regression

#### Goal

Catch whitespace errors, malformed patches, and broken formatting in tracked
changes.

#### Steps

```bash
git diff --check
```

#### Expected Result

- No output.

#### Use When

- Every non-trivial change before commit

### E. Settings Schema And Persistence Safety Review

#### Goal

Make sure settings layout and persistence behavior stay internally consistent.

#### Steps

1. Review the diff in `firmware/src/settings_store.c`.
2. If `settings_t` changed, confirm `SETTINGS_VERSION` was bumped.
3. If a tunable was added or removed, confirm the full path is present:
  `config.ini.example` -> `scripts/gen_config.py` -> owning runtime variable -> `settings_store.c` -> `protocol.c` -> docs.
4. If persistence commands or busy guards changed, confirm `SV:`, `LD:`, and `RS:` semantics still match `MANUAL.md`.

#### Expected Result

- `SETTINGS_VERSION` changes whenever persisted layout changes.
- No new runtime tunable exists only in one layer.
- Persistence behavior stays aligned with the documented protocol.

#### Use When

- `firmware/src/settings_store.c`
- `firmware/src/protocol.c`
- `config.ini.example`
- `scripts/gen_config.py`

#### Buffer Range Vocabulary Regression

When buffer geometry naming changes, verify `buf_switch_span_mm=10` generates a
full-range `CONF_BUF_SWITCH_SPAN_MM` of `10` and an internal half-span of `5`.
The type-D relay trace should match the pre-rename half-span-5 build for the
same physical switch spacing. Type-P analog behavior should remain unchanged
for equal geometry because only the half-span ingest source changes.

### F. Protocol And Documentation Surface Review

#### Goal

Catch command, event, or parameter drift between code and the operator docs.

#### Steps

1. Review changed `SET:` / `GET:` / command handlers in `firmware/src/protocol.c`.
2. Confirm any new or removed command, event, or parameter is reflected in:
  - `MANUAL.md`
  - `BEHAVIOR.md` if behavior changed
  - `README.md` or `KLIPPER.md` if operator workflow changed
3. If the change modifies status output fields, confirm the examples in `TEST_CASES.md` still make sense.

#### Expected Result

- No command exists only in code or only in docs.
- Parameter names and units stay consistent.
- Status snapshots remain representative of the real protocol surface.

#### Use When

- `firmware/src/protocol.c`
- Any operator-facing documentation update tied to runtime behavior

### G. Host Sync Simulation

#### Goal

Catch sync/motion/toolchange logic defects (deadlock, unreachable state, sign
error, timer scoping, unbounded saturation) on a dev machine, without a rig,
by compiling the real control sources against a host plant model and fake
actuators.

#### Steps

```bash
cmake -S tests/host -B build_sim -G Ninja
ninja -C build_sim
python3 -m unittest scripts.test_sync_sim -v
```

`scripts/validate_regression.py` runs this as part of the standard gate; a
simulation build break is reported as a firmware source defect, not skipped.

#### Scenario catalogue

Declared in `tests/host/sim_scenario.c`, run against both sensor types
(type-D dual-endstop and type-P analog) unless marked type-D-only:

| Scenario | Exercises |
|---|---|
| `steady` | Demand exceeds feed briefly, then converges — baseline PD behavior |
| `step_up` | Slow→fast demand transition (type-D step-skip risk) |
| `burst` | Tip-form-style demand start/stop |
| `idle_zero` | Abrupt extruder stop — frozen distance-clock overfeed risk |
| `retract` / `long_retract` | Negative demand; `long_retract`'s magnitude exceeds half travel and saturates the **compression** rail (see the corrected scenario in `specs/host-sync-simulation/spec.md` — not tension, as an earlier draft said) |
| `jam_upstream` | `feed_gain` → 0 mid-run: filament stuck upstream |
| `grind_slip` | `demand_gain` → 0 mid-run: extruder grind/slip |
| `underextrusion` | `demand_gain` → 0.5 mid-run: partial clog |
| `retract_stuck` | `retract_gain` → 0: filament fails to leave the toolhead on retract |
| `runout` | Lane OUT switch drops mid-run |
| `y_splitter_toggle` | Y-splitter switch transitions |
| `sensor_chatter` (type-D only) | Single-tick sensor flip |
| `sensor_stuck` (type-D only) | Sensor latched asserted |
| `both_switches_fault` (type-D only) | Both switches asserted → `BUF_FAULT` |
| `reload_genuine_runout_escalation` (type-P only) | audit-reliability-fixes H6: genuine tension-pinned runout escalates to RELOAD instead of looping `SYNC:FAULT_HOLD` |
| `reload_idle_consumer_staged_completion` (type-P only) | H4: RELOAD follow completes on staged compression (no consumer), no spurious `FOLLOW_JAM` |
| `reload_already_loaded_noop` (type-P only) | H5: manual `RL:` on an already-loaded lane is a no-op — `RELOAD:LOADED`, no motion restart |
| `sem_relief_pause_lifecycle` (type-P only) | sync-state-model: `SYNC_RELIEF_PAUSE` entered on compression saturation, preserves state, re-arms to `SYNC_ACTIVE` once demand resumes |
| `sem_fault_hold_standalone_recovery` (type-P only) | sync-state-model: `SYNC_FAULT_HOLD` recovers standalone (`SYNC,FAULT_HOLD_RECOVERY`) exactly `CONF_SYNC_FAULT_HOLD_RECOVERY_MS` after entry, no host command |
| `sem_bl_lock_catch` (type-D only) | buffer-state-lock: prime locks at the tension switch, holds against the buffer spring, catch engages on the same tick as an external-force lock-break |
| `sem_bl_release_via_bs` (type-D only) | buffer-state-lock: `BS` releases an active lock/catch to `SYNC_OFF` |
| `sem_bl_watchdog_timeout` (type-D only) | buffer-state-lock: an unreleased lock auto-releases (`BL,TIMEOUT`) after `BL_WATCHDOG_DEFAULT_MS` |
| `sem_cutter_large_feed_completes` | cutter-feed-timeout: a long feed (~6s) completes normally under the default 30s timeout |
| `sem_cutter_feed_timeout_jam` | cutter-feed-timeout: feed exceeding the (overridden) timeout aborts with `CUT:ERROR,FEED_TIMEOUT` |
| `sem_cutter_settle_completes` | cutter-feed-timeout: default servo-settle timing completes the open/close/reopen/done cycle |
| `sem_cutter_settle_timeout_abort` | cutter-feed-timeout: servo settle exceeding the timeout aborts with `CUT:ERROR,OPEN_TIMEOUT` |
| `sem_motion_dry_spin_probe` | motion-safety: `FAULT:DRY_SPIN` after 8s of `TASK_FEED` with both switches clear and buffer not in `BUF_TENSION` |
| `sem_relay_fallback_probe` | relay-fallback-only: type-D relay NEUTRAL/TENSION/COMPRESSION branches (`relay_control_law()`) |
| `sem_persistence_fresh_board` | persistence-contract: `settings_load()` fallback on invalid magic/CRC (real code, checked against the RAM-flash fake) |
| `sem_psf_stab_rail_breakaway` (type-P only) | psf-type-p-sensor: idle/`BS` stabilize breaks a saturated buffer off the tension rail before `PSF_STAB_RAIL_BREAK_MS`, emitting `BUF_STAB,DONE` |
| `sem_psf_stab_rail_break_timeout` (type-P only) | psf-type-p-sensor: an uncoupled/jammed lane (`feed_gain=0`) stays saturated past `PSF_STAB_RAIL_BREAK_MS`, aborting with `BUF_STAB,STAGNANT_TIMEOUT` |
| `sem_psf_unload_normal` | psf-type-p-sensor: `TASK_UNLOAD` with OUT clearing mid-retract completes (`UNLOADED,1`) for both sensor types, no guard interference |
| `sem_psf_unload_stuck` | psf-type-p-sensor: `TASK_UNLOAD` with OUT held present (extruder-gripping jam) — type-D's `UNLOAD_TENSION_BLOCK` dwell fires `UNLOAD_BLOCKED`; type-P has no position-based guard and never blocks |
| `sem_psf_no_fault_on_idle_engagement` | psf-type-p-sensor: 8s idle (sync OFF) then organic `SYNC,AUTO_START` — zero spurious `FAULT_HOLD`, both sensor types |
| `sem_sync_overfill_budget_probe` (type-D) | sync-refactor: organic engage into sustained `BUF_COMPRESSION` — `RELIEF_PAUSE` fires off the ~5s dwell timer, not a distance budget |

**Finding (motion-safety)**: `sync.c`'s "hard-wall critical" `FAULT_HOLD` path is
provably unreachable for both sensor types (computed only under type-D,
acted on only under not-type-D) — confirmed by driving a ~63 mm/s
compression push via `idle_zero`/type-D and observing `SYNC,FAULT_HOLD`
never fires there. See `memories/repo/host-sync-sim.md`.

**Finding (relay-fallback-only)**: `relay_control_law()`'s `BUF_COMPRESSION`
branch literally `return 0`, not `SYNC_MIN` as the spec states — confirmed
via `idle_zero`/type-D converging to and holding exactly 0 for 56s+ of
sustained compression.

**Finding (persistence-contract)**: `settings_load()`'s fresh/invalid-magic
fallback calls `settings_defaults()` and returns — it never calls
`settings_save()`, so the defaulted settings are NOT written back to flash,
contrary to the spec's "Fresh Board" scenario. Confirmed via direct
flash-byte comparison in `sem_persistence_fresh_board`.

**Finding (type-d-dynamic-flow)**: the spec's "decaying recovery feed floor"
requirement names symbols (`SYNC_TENSION_RECOVERY_FLOOR`/`_MS`) that don't
exist anywhere in the firmware. The real mechanism is the AIMD-style
`sync_apply_type_d_probe_floor()` — a different design that replaced the
one the spec describes (which memory records as a rejected, FAILED+removed
pivot). See `memories/repo/host-sync-sim.md`.

**sync-refactor** (30 requirements skimmed, mostly Klipper-sidecar/analyzer/
protocol-rename scope): added `test_tension_feeds_compression_backs_off`
(`SyncRefactorTests`) confirming TENSION commands materially more feed than
COMPRESSION for both sensor types, no new C scenario needed (reuses
`steady`/`burst`).

**Finding (sync-refactor, 9th)**: "Type-D compression relief is
overfill-budgeted" implies a small (~3mm) distance-based `RELIEF_PAUSE`
trigger. The only reachable path (`sync_check_continuous_compression`) is
actually TIME-based (`CONF_SYNC_AUTO_STOP_MS`, 5000ms dwell) — confirmed
via `sem_sync_overfill_budget_probe` (organically-engaged, via
`sync_tick_auto_start_stop` staged correctly rather than the sim's
`start_sync_active` shortcut): `RELIEF_PAUSE` fires ~4780ms after
compression onset, matching the 5s constant. The overfill-budget globals
do exist but gate a different mechanism entirely (a partial-feed "drain"
rate, not RELIEF_PAUSE entry). See `memories/repo/host-sync-sim.md`.

**psf-type-p-sensor** (16 requirements, 48 scenarios): heavy overlap with
already-built type-P coverage (relief-pause recovery, `PSF_WALL_SAT_MS`
saturation entries, fault-hold no-instant-re-fault cadence — all covered
by `sem_relief_pause_lifecycle`/`sem_fault_hold_standalone_recovery`).
Net-new: "Type-P Stabilize Rail Breakaway" was untested by anything (no
prior scenario ever exercised `BS`/stabilize under type-P) — added
`sem_psf_stab_rail_breakaway`/`sem_psf_stab_rail_break_timeout` above via
a new `bs_request_at_ms` trigger calling the real `buffer_stabilize_request()`.
"Type-P Unload Uses No Position-Based Over-Tension Guard" also built, via
a new `ul_start_at_ms` trigger calling the real `lane_start(...,
TASK_UNLOAD, ...)` — see `sem_psf_unload_normal`/`sem_psf_unload_stuck`
above. "Hard Catch and Print-Stop Detection"'s `sync_fast_brake` reversible
path needed no new scenario at all: `retract`/`long_retract` under type-P
already are the spec's "Slowdown recovers"/"Real stop confirmed" cases,
just newly asserted. "Type-P Fault Timers Scoped to Active Sync"'s "Normal
extrude does not fault on engagement" is now also confirmed
(`sem_psf_no_fault_on_idle_engagement`, organically-engaged after an 8s
idle) — holds by construction, not luck: nothing in the current codebase
sets the tension-dwell timer while sync is idle. No spec/code mismatch
found in this spec — implementation matches throughout. Most of the
remaining requirements are
protocol.c-gated (calibration, `BUF_GOAL`, `BUF_RANGE`/`BUF_INVERT`),
internal-state shape (PD/dead-zone/soft-wall/filtered-derivative, not in
the CSV trace), or explicitly bench-untestable by the spec's own words
("measured against a real print"). Full requirement-by-requirement
disposition in `memories/repo/host-sync-sim.md` and
`openspec/changes/spec-derived-sim-coverage/tasks.md` task 11.

**Known gap**: the type-P RELOAD sign regression and stale-fault-timers-
while-sync-OFF are still unmodeled — those need more toolchange RELOAD
state-machine setup than the current catalogue drives. `idle_zero` covers
the frozen-distance-clock-on-abrupt-extruder-stop case; the `reload_*` and
`sem_*` scenarios above cover the RELOAD/BL-follow and sync-lifecycle
defects that were previously listed as gaps. Tracked in
`memories/repo/host-sync-sim.md`.

#### Global invariants (every tick, every scenario, no per-scenario authoring)

1. **Finiteness** — no NaN/Inf in buffer position, feed rate, or estimator output.
2. **Bounds** — commanded feed is non-negative and never exceeds `g_sync_max_sps`.
3. **Liveness** — `SYNC_RETRACT_ASSIST` / `SYNC_RELIEF_PAUSE` exit within a
   30 s simulated-time backstop; `SYNC_OFF` / `SYNC_ACTIVE` are exempt (both
   legitimately persist indefinitely).
4. **Fault quiescence** — while `SYNC_FAULT_HOLD`, commanded feed is zero and
   event emission has ceased (except the entry tick's own announcement event).
5. **Non-oscillation** — no sync state entered more than 50 times in one run.
6. **Saturation bound** — rail saturation does not persist beyond 20 s simulated time.
7. **Event rate** — no more than 16 `cmd_event`/`cmd_event_critical` calls in one tick.

#### Expected Result

- `ninja -C build_sim` links with zero undefined symbols, `-Wall -Wextra` clean.
- Every catalogued scenario completes with no invariant violation, on both
  sensor types where applicable.
- A passing run does not check off any `HW:` task — the rig remains sole
  authority on tuning quality, control gains, and sensor noise behavior.

#### Use When

- `firmware/src/sync.c`, `sync_buf.c`, `sync_relay.c`, `sync_analog.c`
- `firmware/src/motion.c`, `toolchange.c`, `cutter.c`, `settings_store.c`
- Any change to `tests/host/**` or `scripts/test_sync_sim.py` itself

### Suggested Minimum Static Gate By Change Type

| Change Type | Minimum Checks |
|-------------|----------------|
| Firmware logic only | B, D, F, G |
| Settings or tunables | A, B, D, E, F |
| Python scripts only | C, D |
| Config generation only | A, B, D, E |
| Docs-only protocol cleanup | D, F |
| Sync/motion/toolchange logic | B, D, F, G |

Do not skip the hardware tests later for motion, sync, or RELOAD changes. This
gate is meant to fail fast on integration mistakes, not replace real-hardware
validation.

---

## Acceptance Checklist By Lane

Use this table to record whether each lane passes the minimum motion path before
you move on to sync, toolchange, or RELOAD tests.

| Check | Lane 1 | Lane 2 | Notes |
|------|--------|--------|-------|
| `T:n` selects the correct lane | ☐ | ☐ | `?:` shows `LN:1` or `LN:2` as expected |
| IN sensor changes only the matching `I*` field | ☐ | ☐ | Verify no cross-talk |
| OUT sensor changes only the matching `O*` field | ☐ | ☐ | Verify no cross-talk |
| `LO:` reaches OUT and parks cleanly | ☐ | ☐ | No timeout, no wrong direction |
| `UL:` clears OUT cleanly | ☐ | ☐ | No lingering downstream filament |
| `UM:` clears IN cleanly | ☐ | ☐ | Lane becomes physically empty |
| `FL:` reaches toolhead path correctly | ☐ | ☐ | Requires toolhead presence reporting |
| No unexpected dry-spin or timeout fault in nominal path | ☐ | ☐ | Investigate before higher-level tests |

Do not treat toolchange or RELOAD failures as meaningful until both lane columns
are green for the basic motion path.

---

## 1. Build And Serial Smoke Test

### Goal

Confirm the host tools, firmware image, USB CDC interface, and base protocol are working.

### Steps

```bash
python3 scripts/gen_config.py
cmake --build build_local
python3 scripts/flare_cmd.py "VR:" "?:"
python3 scripts/flare_cmd.py --dump --raw
```

### Expected Result

- `gen_config.py` regenerates `firmware/include/tune.h` without errors.
- Build completes successfully.
- `VR:` returns the current firmware version.
- `?:` returns a complete status line with lane, toolchange, sensor, sync, and RELOAD fields.
- `--dump --raw` returns the current runtime parameter surface without missing keys or stale names.

---

## 2. Sensor Polarity And Idle State

### Goal

Confirm each discrete sensor reports correctly before motion tests.

### Steps

1. With no filament inserted, run `python3 scripts/flare_cmd.py "?:"`.
2. Manually trigger each lane IN sensor and verify the corresponding `I1` or `I2` state changes.
3. Manually trigger each lane OUT sensor and verify `O1` or `O2`.
4. If fitted, trigger the Y-splitter sensor and verify `YS`.
5. If the host reports toolhead state, send `python3 scripts/flare_cmd.py "TS:1" "?:"` and then `python3 scripts/flare_cmd.py "TS:0" "?:"`.

### Expected Result

- Each sensor toggles the matching status field and no unrelated field changes.
- The controller remains idle during all manual sensor checks.
- Toolhead presence only changes `TH`.

---

## 3. Active Lane Selection

### Goal

Verify manual lane selection and active-lane reporting.

### Steps

```bash
python3 scripts/flare_cmd.py "T:1" "?:"
python3 scripts/flare_cmd.py "T:2" "?:"
```

### Expected Result

- `T:1` sets `LN:1`.
- `T:2` sets `LN:2`.
- `EV:ACTIVE` may be emitted if event output is enabled and the host is connected.

---

## 4. Preload And Unload Per Lane

### Goal

Validate the basic lane motion primitives without involving the toolhead.

### Steps

Run this sequence once for lane 1 and once for lane 2.

```bash
python3 scripts/flare_cmd.py "T:1" "LO:" "?:"
python3 scripts/flare_cmd.py "UL:" "?:"
python3 scripts/flare_cmd.py "UM:" "?:"
```

Repeat with `T:2`.

### Expected Result

- `LO:` drives filament to the lane OUT sensor and parks just before it.
- After preload, the selected lane is active.
- `UL:` retracts out of the downstream path and clears the OUT sensor.
- `UM:` retracts fully until the IN sensor clears.
- If a distance limit is hit, the controller emits a timeout event instead of running forever.

---

## 5. Full Load To Toolhead

### Goal

Confirm the full-load path, including toolhead sensor handoff.

### Steps

1. Insert filament into the selected lane.
2. Start a full load:

```bash
python3 scripts/flare_cmd.py "T:1" "FL:"
```

3. When filament reaches the extruder entry, have the host or test operator report toolhead presence:

```bash
python3 scripts/flare_cmd.py "TS:1" "?:"
```

4. Clear the simulated toolhead state afterward:

```bash
python3 scripts/flare_cmd.py "TS:0"
```

### Expected Result

- `FL:` runs until toolhead presence is reported.
- The firmware emits `EV:LOADED:<lane>` on success.
- If no toolhead presence arrives, motion stops at `LOAD_MAX` rather than running indefinitely.

---

## 6. Toolchange

### Goal

Verify unload, lane swap, and load sequencing in MMU mode.

### Steps

1. Preload or load both lanes so either lane can become active.
2. Disable autonomous RELOAD for this test:

```bash
python3 scripts/flare_cmd.py "SET:RELOAD_MODE:0"
```

3. Trigger a toolchange:

```bash
python3 scripts/flare_cmd.py "T:1" "TC:2"
python3 scripts/flare_cmd.py "?:"
```

### Expected Result

- The controller emits `TC:` progress events through unload, swap, and load phases.
- The previous lane retracts cleanly.
- The target lane becomes active at the end of the sequence.
- Toolchange finishes in `TC:IDLE` or reports a clear `TC:ERROR` fault state.

---

## 7. Sync Auto-Start And Auto-Stop

### Goal

Confirm the buffer-driven sync controller starts, follows demand, and stops correctly.

### Steps

1. Enable automatic flow and sync:

```bash
python3 scripts/flare_cmd.py "SET:AUTO_MODE:1" "SM:1"
```

2. Put the active lane in a loaded state where the downstream path can pull filament.
3. Pull the buffer into `TENSION` and monitor with repeated `?:` calls.
4. Let the system settle, then hold the buffer in `COMPRESSION` long enough for sync to collapse to its minimum compression-floor speed and remain there past `SYNC_AUTO_STOP`.

### Expected Result

- `BUF_TENSION` can trigger `EV:SYNC:AUTO_START`.
- Status shows sync active and `SPS` / `BL` / `BS` fields updating.
- Sustained `COMPRESSION` only triggers `EV:SYNC:AUTO_STOP` after sync has already collapsed to its minimum compression-floor speed.
- Sync does not run during toolchange or RELOAD phases.

---

## 8. RELOAD Runout Recovery

### Goal

Validate autonomous lane switching on runout using the buffer-driven RELOAD path.

### Steps

1. Prepare two lanes: one active lane near depletion, one standby lane preloaded.
2. Enable RELOAD:

```bash
python3 scripts/flare_cmd.py "SET:RELOAD_MODE:1" "SET:AUTO_MODE:1"
```

3. Cause a runout on the active lane.
4. Observe status and events during `RELOAD_WAIT_Y`, `RELOAD_APPROACH`, and `RELOAD_FOLLOW`.

### Expected Result

- The controller emits `EV:RUNOUT:<lane>` followed by `RELOAD:` progress events.
- The old lane stops once the swap path transfers ownership.
- The new lane approaches until buffer contact, then follows with bounded under-feed.
- RELOAD exits on successful pickup or fails with a visible timeout / fault instead of running forever.

### 8.2 Type-P RELOAD Runout Recovery

#### Goal
Validate autonomous lane switching on runout with type-P (analog) buffer sensor.

#### Steps
1. Set the buffer sensor to type-P:
   ```bash
   python3 scripts/flare_cmd.py "SET:BUF_SENSOR_TYPE:1"
   ```
2. Enable RELOAD:
   ```bash
   python3 scripts/flare_cmd.py "SET:RELOAD_MODE:1" "SET:AUTO_MODE:1"
   ```
3. Prepare two lanes (e.g., Lane 1 active with a short tail, Lane 2 preloaded).
4. Initiate a feed on Lane 1 and cut/runout Lane 1.
5. Watch the `BP` (buffer position) value in status.
6. During `RELOAD_APPROACH`, verify that `BP` must exceed `+0.5` (`PSF_LOAD_CONTACT_THRESHOLD_NORM`) to register contact and transition to `RELOAD_FOLLOW`. Check that it does not immediately declare success (`RELOAD:LOADED`).
7. During the first `RELOAD_TOUCH_SETTLE_MS + RELOAD_TOUCH_BOOST_MS` (~1 s) of `RELOAD_FOLLOW`, verify that even if `BP` dips into tension (below `+0.3`), the follow phase does not exit (no instant `RELOAD:LOADED`).
8. Verify that success (`RELOAD:LOADED`) is only declared when the extruder gear grabs the filament, pulling the buffer arm persistently into tension after the settle/boost window has elapsed, or when the toolhead sensor trips.

#### Expected Result
- `RELOAD_APPROACH` transitions to `RELOAD_FOLLOW` only when `BP` > +0.5.
- No instant `RELOAD:LOADED` is triggered during the settle/boost window.
- RELOAD exits successfully with `EV:RELOAD:LOADED` on a post-settle tension crossing or toolhead sensor trigger.

---

## 9. Persistence Guarding

### Goal

Confirm flash-backed settings commands are blocked during unsafe activity and allowed when idle.

### Steps

1. Start any motion, for example `FD:`.
2. While motion is active, send:

```bash
python3 scripts/flare_cmd.py "SV:" "LD:" "RS:"
```

3. Stop motion with `ST:`.
4. Repeat the same commands while idle.

### Expected Result

- During motion, each persistence command returns `ER:PERSIST_BUSY`.
- While idle, the commands succeed.
- Saved values survive a reboot when that behavior is being explicitly tested.

---

## 10. Flash And Post-Flash Smoke

### Goal

Confirm the board can re-enter BOOTSEL from firmware and return to a working serial state after reflashing.

### Steps

```bash
python3 scripts/flare_cmd.py "BOOT:"
python3 scripts/flash_flare.py
python3 scripts/flare_cmd.py "VR:" "?:"
```

### Expected Result

- `BOOT:` reboots the board into RP2040 boot mode.
- The flash script rebuilds, programs, and verifies the board.
- After reboot, the controller answers normal serial commands again.

---

## Pending Manual Hardware Validation

These cases are tracked because they require a real MMU, buffer, and printer
motion path. They are **pending-manual-hardware** until an operator records the
firmware commit, setup, commands, and observed status/events.

### pending-manual-hardware: FAULT_HOLD Entry And Auto-Recovery

#### Goal

Confirm sync enters non-destructive `FAULT_HOLD` from both hard-wall critical
and tension-dwell paths, then recovers automatically.

#### Steps

1. Start from a loaded lane with sync active and capture repeated `?:` output.
2. For the tension-dwell path, hold the buffer in `TENSION` longer than
   `SYNC_TENSION_STOP_MS`.
3. For the hard-wall path, force a compression hard-wall critical condition during
   active sync with enough net push velocity to cross the configured hard-wall
   thresholds.
4. Continue monitoring until after `CONF_SYNC_FAULT_HOLD_RECOVERY_MS`.

#### Expected Result

- Each path emits `EV:SYNC:FAULT_HOLD`.
- Status shows `ST` in the fault-hold state and motion stopped safely.
- The estimator is not destructively reset by entry into `FAULT_HOLD`.
- After the recovery interval, firmware emits `EV:SYNC:FAULT_HOLD_RECOVERY`
  and sync can re-arm without reboot.

### pending-manual-hardware: Cannot-Refill / Cannot-Relieve Effort Events

#### Goal

Confirm the warn-only effort counters emit once per episode when sustained
sync effort exceeds 50 mm.

#### Steps

1. Configure the default thresholds:
   `sync_cannot_refill_mm: 50.0` and `sync_cannot_relieve_mm: 50.0`.
2. Start sync and create a sustained `BUF_TENSION` episode where the MMU is
   trying to refill the buffer.
3. Observe `SYNC_REFILL_MM` in status until it exceeds 50.
4. Return to a neutral state, then create a sustained `BUF_COMPRESSION` episode
   where the MMU is trying to relieve/pull down excess buffer.
5. Observe `SYNC_RELIEVE_MM` until it exceeds 50.

#### Expected Result

- `EV:SYNC:cannot_refill` emits once during the TENSION episode after the
  refill effort crosses 50 mm.
- `EV:SYNC:cannot_relieve` emits once during the COMPRESSION episode after the
  relieve effort crosses 50 mm.
- Counters reset on sync state/episode changes and do not spam repeated events.
- The events are warn-only; they do not directly change the control target.

### pending-manual-hardware: Flow-Schedule Scalar Parity Sweep

#### Goal

Confirm the length-1 flow schedule preserves scalar behavior over live flow on
hardware.

#### Steps

1. Flash a scalar-only config with no `[flow_schedule.v1]` table and record a
   sync flow sweep across low, neutral, and high print demand.
2. Confirm `scripts/gen_config.py` synthesized `CONF_FLOW_SCHED_LEN 1`.
3. Capture status fields including `EST`, `BL`, `CB`, `RT`, `BP`, `SPS`, and
   any `SYNC`/`BUF` events.
4. Compare against the previous scalar build or a known-good scalar replay for
   the same hardware motion pattern.

#### Expected Result

- `BL` stays equal to the scalar baseline plus any expected ephemeral live
  learner lift.
- `CB` stays equal to the scalar compression bias at integer-milli resolution.
- Reserve target/control output is identical for milli-aligned bias configs
  and within 0.0005 absolute bias otherwise.
- No new sync, toolchange, or RELOAD fault appears during the sweep.

### pending-manual-hardware: Flow-Schedule Reserve Safety Floor

#### Goal

Confirm multi-point flow schedules cannot weaken the reserve cushion or
TENSION recovery floor at startup and low flow.

#### Steps

1. Flash a config with `[flow_schedule.v1]` containing at least one low-flow
   breakpoint whose bias is less than `COMPRESSION_BIAS_FRAC` and whose baseline is
   less than `BASELINE_RATE`.
2. Start a sync run from idle/startup and include low-flow perimeter or
   post-travel motion that clamps to that weak endpoint.
3. Capture status fields including `BUF`, `RT`, `BP`, `BL`, `CB`, `TT`, `SPS`,
   and any `SYNC`/`BUF` events.
4. Repeat with a breakpoint whose bias is greater than `COMPRESSION_BIAS_FRAC` to
   confirm deeper reserve is still honored.

#### Expected Result

- At the weak endpoint, effective reserve depth is no shallower than the scalar
  `COMPRESSION_BIAS_FRAC` cushion.
- The baseline control floor stays at or above `BASELINE_RATE`, so TENSION
  recovery gain is not sluggish.
- Startup and low-flow motion do not pin the buffer in `BUF_TENSION` for
  seconds, and no underextrusion-causing TENSION dwell is observed.
- A stronger schedule bias still deepens reserve relative to the scalar.

---

### pending-manual-hardware: Type-D Hysteretic Relay Control

#### Goal

Confirm that standalone Sync-Feedback Sensor type D (`BUF_SENSOR_TYPE=0`)
uses the two-level / hysteretic relay control law to follow a slow, shallow,
never-TENSION-leaning limit cycle with no FAULT_HOLD on normal switch
contact, during a real print.

#### Steps

1. Flash firmware with the relay change.
2. Run a real print; capture `flare_cmd.py "?:" --poll 500`.
3. Watch `BUF`, `BP`, `EV:BS:*`, `EV:SYNC:*`, cycle period/amplitude.

#### Expected Result

- Buffer cycles gently through NEUTRAL; brief COMPRESSION/TENSION switch touches,
  not multi-second pinning at ±7.8.
- No `EV:SYNC:FAULT_HOLD` from normal COMPRESSION/TENSION contact; no
  5 s pause → TENSION-slam pattern.
- Cycle leans compression (more time compression side); no underextrusion.
- Tune `relay_catchup_frac` / `RELAY_CATCHUP_FRAC` (increase if it starves)
  and `relay_neutral_frac` / `RELAY_NEUTRAL_FRAC` (lower for less compression lean, raise for more)
  until the cycle is slow and benign.
- `BUF_SENSOR_TYPE != 0` (type P analog, P=1) behavior unchanged.

---

### relay-fallback-only: Type-D Relay Regression

#### Goal

Validate fallback-only type-D relay behavior without changing the
safety-critical TENSION/COMPRESSION relay branches.

#### Steps

1. Flash firmware with `BUF_SENSOR_TYPE=0`, `relay_catchup_frac: 1.30`,
   `relay_neutral_frac: 1.25`, and `relay_min_flip_mm: 0.0`.
2. Capture `?:` status through a disturbed relay cycle with repeated
   TENSION/COMPRESSION flips.
3. Print a slow-only model from cold boot.
4. Print a fast-only model from cold boot.

#### Expected Result

- `BUF_TENSION` still commands `relay_base * RELAY_CATCHUP_FRAC`; `BUF_COMPRESSION`
  still commands `SYNC_MIN_SPS`.
- `BUF_NEUTRAL` always commands the `extruder_est_sps * RELAY_NEUTRAL_FRAC`
  fallback, clamped to `[SYNC_MIN_SPS, relay_base]`.
- `?:` status does not include `RDE`, `RDCF`, or `RDV`.
- Slow-only and fast-only cold starts bridge via bounded catch-up without
  stalling or pinning the buffer at the physical wall.

---

### pending-manual-hardware: Holdable Reserve Target (No Wall-Riding)

#### Goal

Confirm the buffer holds near a holdable reserve target with frequent
switch crossings (estimator stays fresh) instead of parking on the
compression fault wall and FAULT_HOLD-cycling, during a real print.

#### Steps

1. Flash firmware with H1/H2.
2. Run a real print; capture `flare_cmd.py "?:" --poll 500`.
3. Watch `RT`, `BP`, `EST`, `BUF`, `EV:BS:*`, `EV:SYNC:*`.

#### Expected Result

- `RT ≈ -3.9mm` (not `-6.24`); `BP` oscillates around `RT` with regular
  NEUTRAL↔COMPRESSION/TENSION crossings, not pinned at `-7.7…-7.8`.
- `EST` tracks demand and does not freeze for seconds at a hallucinated
  value.
- No repeating `FAULT_HOLD`/`FAULT_HOLD_RECOVERY` cycle; no underextrusion.
- Buffer stays compression-biased (rarely TENSION) via the H2 feed trim, not
  by parking on the wall.
- If still wall-riding, lower `SYNC_RESERVE_BIAS_POS_FRAC_CAP` and retest.

---

### pending-manual-hardware: NEUTRAL Refill And FAULT_HOLD Anti-Oscillation

#### Goal

Confirm standalone sync does not collapse into the
`NEUTRAL(deep-compression) → COMPRESSION → FAULT_HOLD → recovery → TENSION-pin →
FAULT_HOLD` oscillator when the extruder estimate is fresh but collapses
well below the learned baseline on a long same-flow print.

#### Steps

1. Flash firmware with the F1a/F1b/F2a/F2b changes.
2. Run a long, steady same-flow standalone print with Sync-Feedback Sensor
   type D (`BUF_SENSOR_TYPE=0`, D=0).
3. Capture status with `python3 scripts/flare_cmd.py "?:" --poll 500` and
   watch `BUF`, `BP`, `RT`, `EST`, `TT`, `CT`, `TPX`, `RDC`, and
   `EV:SYNC:*`.

#### Expected Result

- While `BUF:NEUTRAL` and `BP` at/below `RT`, feed holds at or above the
  baseline floor; the buffer refills toward `RT` instead of pinning at the
  `-7.80` compression wall.
- No repeating `FAULT_HOLD` / `FAULT_HOLD_RECOVERY` / `AUTO_START` cycle;
  `TENSION_RISK_HIGH` does not latch.
- After any genuine `FAULT_HOLD_RECOVERY`, `BP` resets toward `RT` (not a
  fictional TENSION) and `AUTO_START` does not slam `BUF:TENSION` with a
  large `RE`/`AV`.
- Full braking and fault-hold behavior still occur once actually in
  `BUF_COMPRESSION`.

---

## Pending Type-D Rig Session

Consolidated **type-D** hardware validation, grouped so one type-D rig session
covers all of it. These were extracted from the `audit-reliability-fixes`,
`audit-hardening-fixes`, and `fix-typep-relief-pause-rearm-strand` openspec
changes (which retain only their type-P / general HW items). Each is
**pending-manual-hardware** until an operator records the firmware commit, setup,
commands, and observed status/events.

### pending-manual-hardware: Type-D dry-spin, watchdog, BL prime (audit-reliability-fixes 10.2)

#### Goal

Confirm the H2 dry-spin interlock, H3 hardware watchdog, and BL prime ramp on a
type-D buffer.

#### Steps

1. Attempt a sync restart that would re-fire a dry spin; confirm the interlock.
2. Run `SV` and confirm the hardware watchdog does not fire during it.
3. Raise `SYNC_MAX` and observe the BL prime ramp.

#### Expected Result

- Dry-spin does not re-fire after a sync restart attempt.
- Watchdog stays silent throughout `SV`.
- BL prime ramps cleanly at the raised `SYNC_MAX`.

### pending-manual-hardware: Type-D piston full-range deflection (audit-hardening-fixes 8.3)

#### Goal

Confirm the type-D piston deflection spans the full range at the configured
`BUF_MAX_TRAVEL`.

#### Steps

1. Set the target `BUF_MAX_TRAVEL`.
2. Drive the buffer across its full travel and watch `BP` / piston in the UI.

#### Expected Result

- Piston deflection covers the full configured range, end to end, with no
  premature clamp.

### pending-manual-hardware: Type-D relief recovery regression (fix-typep-relief-pause-rearm-strand 3.4)

#### Goal

Confirm the type-P relief-pause rearm fix did not change type-D relief recovery.

#### Steps

1. On a type-D buffer, drive a relief-pause recovery scenario.
2. Compare behavior against pre-fix type-D baseline.

#### Expected Result

- Type-D relief recovery is unchanged (no regression from the type-P rearm path).

---

## Expected Status Snapshots

These are reference patterns, not byte-for-byte golden outputs. Exact rates,
buffer position, and some event timing fields will vary by setup, but the named
state fields should match the phase you are testing.

### Idle, lane 1 selected, no motion

```text
OK:LN:1,TC:IDLE,L1T:IDLE,L2T:IDLE,...,SM:0,CU:0,RELOAD:0
```

What to check:

- `LN:1`
- `TC:IDLE`
- `L1T:IDLE` and `L2T:IDLE`
- `SM:0` unless sync was intentionally enabled
- `CU:0`

### Lane 1 preloaded at OUT

```text
OK:LN:1,TC:IDLE,L1T:IDLE,L2T:IDLE,I1:1,O1:1,I2:0,O2:0,...
```

What to check:

- Active lane is the lane you preloaded
- The matching `O*` field is asserted
- The other lane does not show a false OUT trigger

### Unload in progress on lane 1

```text
OK:LN:1,TC:IDLE,L1T:UNLOAD,L2T:IDLE,...
```

What to check:

- Only the active lane has an unload task
- `TC` remains `IDLE` for plain `UL:` or `UM:` operations

### Toolchange from lane 1 to lane 2

```text
OK:LN:1,TC:LOAD_WAIT_TH,L1T:IDLE,L2T:LOAD_FULL,...
```

What to check:

- `TC` is not `IDLE` during the swap sequence
- The old lane is no longer feeding once ownership moves to lane 2
- The target lane shows a load-related task while toolchange is active

### Sync running normally

```text
OK:LN:1,TC:IDLE,L1T:FEED,L2T:IDLE,...,SM:1,BUF:TENSION,SPS:...,BL:...,BP:...
```

What to check:

- `TC:IDLE`
- Active lane task is `FEED`
- `SM:1`
- `BUF`, `SPS`, `BL`, and `BP` fields are changing sensibly as the buffer moves

### RELOAD follow active

```text
OK:LN:2,TC:RELOAD_FOLLOW,L1T:IDLE,L2T:FEED,...,SM:1,RELOAD:1
```

What to check:

- The new lane is now active
- The old lane is idle
- `TC:RELOAD_FOLLOW`
- The new lane remains in `FEED` rather than falling back to `IDLE`
- `RELOAD:1` remains set until the sequence finishes or faults out

### Persistence blocked during motion

```text
ER:PERSIST_BUSY
```

What to check:

- `SV:`, `LD:`, and `RS:` are rejected while any motion or toolchange state is active
- The same commands succeed again once the controller returns to idle

---

## Suggested Regression Minimum

For small firmware changes, run at least this subset:

1. Build And Serial Smoke Test
2. Sensor Polarity And Idle State
3. Preload And Unload Per Lane on the affected lane
4. Toolchange or RELOAD test if the change touches motion ownership, sync, or recovery
5. Persistence Guarding if the change touches settings or protocol admission rules

For major refactors, run the full list.

---

## Pending Analog Rig

These tests require Sync-Feedback Sensor type P (`BUF_SENSOR_TYPE=1`, P=1)
with a real analog buffer rig. They are documented as `pending-analog-rig`
until that hardware exists. Happy Hare types TO/CO are recognized names but
are not implemented in FLARE.

### Analog compression floor polarity

Status: `pending-analog-rig`

Goal: confirm that the analog `COMPRESSION` floor behaves as a safe low
coasting floor and does not invert the required full-buffer backoff behavior.

What to check:

- With `BUF:COMPRESSION`, commanded feed backs off toward `COMPRESSION_RATE`
  or the configured minimum sync floor.
- With `BUF:TENSION`, commanded feed increases enough to refill the buffer.
- `BPV` remains negative on the compression side and positive on the tension
  side.

### Analog compression recovery and collapse

Status: `pending-analog-rig`

Goal: confirm analog recovery caps and collapse behavior reduce feed while
the buffer stays full and do not starve refill when the arm returns to
`BUF_NEUTRAL`.

What to check:

- Sustained `BUF:COMPRESSION` tightens the speed cap and ramps down rather
  than raising feed.
- Returning to `BUF:NEUTRAL` clears compression recovery and allows normal
  refill authority.
- `AUTO_STOP` / `RELIEF_PAUSE` only fire after the configured compression
  dwell and floor conditions are met.

---

## Record Keeping

When a test fails, capture:

- firmware commit SHA
- exact commands sent
- `?:` output before and after the failure
- emitted `EV:` lines around the failure
- whether the issue reproduces on both lanes or only one lane

That information is usually enough to correlate the failure with motion,
toolchange, sync, or protocol ownership.
