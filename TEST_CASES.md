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
bash scripts/validate_regression.sh
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

### Suggested Minimum Static Gate By Change Type

| Change Type | Minimum Checks |
|-------------|----------------|
| Firmware logic only | B, D, F |
| Settings or tunables | A, B, D, E, F |
| Python scripts only | C, D |
| Config generation only | A, B, D, E |
| Docs-only protocol cleanup | D, F |

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
bash scripts/flash_flare.sh
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

### relay-duty-estimator: Type-D Estimate/Fallback Regression

#### Goal

Validate the relay duty estimator without changing the safety-critical
TENSION/COMPRESSION relay branches.

#### Steps

1. Flash firmware with `BUF_SENSOR_TYPE=0`, `relay_catchup_frac: 1.30`,
   `relay_neutral_frac: 1.25`, and analyzer-provided `relay_estimate_lo`,
   `relay_estimate_hi`.
2. Capture `?:` status or tuner CSV through a disturbed relay cycle with
   repeated TENSION/COMPRESSION flips.
3. Run `scripts/flare_analyze.py` twice on the same CSV and compare output.
4. Repeat a quiet low-flip run with the confidence gate unreachable or stale.
5. Print a slow-only model from cold boot.
6. Print a fast-only model from cold boot.

#### Expected Result

- `BUF_TENSION` still commands `relay_base * RELAY_CATCHUP_FRAC`; `BUF_COMPRESSION`
  still commands `SYNC_MIN_SPS`.
- `RDE:1` appears only after enough recent paired cycles; `RDCF` rises with
  paired transitions and decays/stales back to fallback.
- `RDV` remains within `relay_estimate_lo` / `relay_estimate_hi`.
- Quiet steady state can stay `RDE:0`; this is not a fault.
- Same CSV input produces byte-identical relay analyzer recommendations.
- **D13 Startup Asymmetry**: A slow-only print from cold boot does not slam the COMPRESSION wall at startup (`seed ≈ lo`, not baseline).
- **D13 Startup Asymmetry**: A fast-only print from cold boot bridges the cold under-feed via bounded catch-up in ≤2 cycles without stalling.

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
