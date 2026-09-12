---
phase: 13
reviewers: [antigravity]
reviewed_at: 2026-09-12T19:51:58Z
plans_reviewed: [13-01-PLAN.md, 13-02-PLAN.md, 13-03-PLAN.md, 13-04-PLAN.md]
models:
  antigravity: "unknown"
model_sources:
  antigravity: "unknown"
---

# Cross-AI Plan Review — Phase 13

<!-- gsd:plan-revision-conflicts:begin -->
## Plan-Revision Conflicts
(none)
<!-- gsd:plan-revision-conflicts:end -->

## Antigravity Review

**AGENTS.md ✓ | GSD: Phase 13 (Type-P Sync Relief & Fault Trip)**

Review of implementation plans `13-01-PLAN.md` through `13-04-PLAN.md` for Phase 13. All file paths and symbol references have been verified against current source code in `/Users/Volodymyr_Zhdanov/playground/FLARE`.

---

## 13-01

### 1. Summary
Plan 13-01 serves as the phase tracer, replacing the abrupt `g_sync_current_sps = target_sps` urgent-refill snap (`firmware/src/sync.c:2020`) with a bounded, rail-relative target `min(max_sps, max(est * SYNC_PSF_RELIEF_MULT, baseline_sps))` routed through `sync_apply_type_p_smoothing` with a doubled slew cap (`SYNC_RELIEF_SLEW_MULT = 2.0f`). It establishes `SYNC_PSF_RELIEF_MULT` as a live and flash-persisted knob via the TLV settings path, mirrors the proven `g_bl_lock_extreme` rail-relative tracking (`firmware/src/sync.c:950-965`) as `g_sync_tension_extreme`, and provides comprehensive simulation coverage in `tests/host/sim_scenario.c` across rail scales 1.0, 0.7, and 0.5.

### 2. Strengths
- **Bound Invariant**: Caps commanded rate to prevent driving the stepper into mechanical stops at raw `max_sps` (15004 sps) while pinned at tension (`firmware/src/sync.c:2020`, `firmware/src/sync_analog.c:51-64`).
- **Rail-Relative Adaptation**: Eliminates absolute normalized literals (`-CONF_PSF_SOFT_WALL_START` at `firmware/src/sync.c:2013`) in favour of `g_sync_tension_extreme + SOFT_WALL_MARGIN_NORM`, preventing the regression identified in commit `51bdca8` where shallow-reading rails (`BP -0.36` to `-0.70`) bypassed soft-wall logic entirely.
- **Persistence Architecture**: Follows the non-gated TLV tag pattern (`firmware/src/settings_store.c:440,767`, `firmware/include/settings_store.h:84`) without altering frozen `settings_v63_t` or prematurely bumping `SETTINGS_VERSION`.
- **Tripwire Scenarios**: Introduces `sem_psf_relief_shallow_rail` at `type_p_rail_scale = 0.5f` (`tests/host/sim_scenario.c:581`) to automatically fail the build if the trigger ever reverts to an absolute compare.

### 3. Concerns
- **[HIGH] Target EMA Lag During Urgent Relief**: In `firmware/src/sync.c:1868-1872`, `sync_apply_type_p_smoothing()` filters `target_sps` through a distance EMA with `filter_len_mm = 25.0 mm` (`CONF_SYNC_PSF_FILTER_MM`, `scripts/gen_config.py:115`) before applying slew limiting. If `g_psf_target_filt` is seeded to `g_extruder_est_sps` on entering relief, `g_psf_target_filt` requires ~25 mm of extruded travel to approach `est * 1.33`. On a 16 mm physical buffer, this introduces substantial lag during starvation — the exact defect documented in `firmware/src/sync.c:2014-2016` ("distance-EMA below is far too slow to ramp feed before it slams the rail"). Doubling `max_step` (Step 2) will not accelerate response if Step 1 (`g_psf_target_filt`) is throttled by the 25 mm filter.
- **[MEDIUM] TLV Deserializer Clamp Omission**: In `firmware/src/settings_store.c:880`, `settings_load_tlv_tag()` copies float values via `memcpy` without range verification. Corrupt flash or invalid TLV data would load an unconstrained multiplier unless explicitly passed through `clamp_f` upon deserialization or within `settings_apply_clamps()` (`firmware/src/settings_store.c:560-600`).
- **[LOW] Long-Window Extreme Staleness**: `g_sync_tension_extreme` resets on `sync_rearm_active()`, `sync_disable()`, and `AUTO_START`. During sustained active printing spanning thousands of layers without an auto-stop/start edge, an uncalibrated negative deflection spike could permanently depress `g_sync_tension_extreme`, shifting the soft-wall entry threshold until the next toolchange.

### 4. Suggestions
- When entering the relief branch, either temporarily bypass Step 1 (the 25 mm target EMA) to let `cur` slew directly toward the bounded relief target, or scale `filter_len_mm` down (e.g. `filter_len_mm / 4.0f`) while in relief so the feed rate responds promptly.
- Ensure `settings_load_tlv_tag()` clamps `g_sync_psf_relief_mult` to `[1.0f, 3.0f]` upon load, or add it to `settings_apply_clamps()` (`firmware/src/settings_store.c:560`).

### 5. Risk Assessment
**MEDIUM**. The plan is structurally rigorous and adheres to project conventions, but routing urgent relief through the 25 mm distance-EMA filter risks creating an under-responsive refill loop that could cause buffer starvation under steep acceleration.

---

## 13-02

### 1. Summary
Plan 13-02 implements the primary distance-based fault trip `SYNC_TENSION_STOP_MM` (default 32.0 mm) alongside the fallback ms timer (`firmware/src/sync.c:1638-1672`). It hooks directly into the existing `g_sync_refill_effort_mm` accumulator (`firmware/src/sync_buf.c:964`), couples trip arming to the first buffer transition within an active sync window (`g_sync_trip_armed`), suppresses trips during deliberate holds (`g_bl_sub_state`, tail assist, buffer stabilize), and exports `TM:` and `ARM:` telemetry in the extended `ST:` tail with an automated status-line budget test.

### 2. Strengths
- **Accumulator Reuse**: Reuses `g_sync_refill_effort_mm` (`firmware/src/sync_buf.c:964`) and its automatic reset in `buf_update()` (`firmware/src/sync_buf.c:833`), avoiding dual-accumulator drift and extra memory overhead.
- **Automated Line Budget Verification**: Introduces `scripts/test_status_line_budget.py` to statically verify worst-case formatted status strings against `STATUS_LINE_MAX = CMD_LINE_MAX - 8` (760 bytes, `firmware/src/protocol_status.c:16`, `firmware/include/protocol.h:5`), preventing silent string truncation.
- **Synchronized Arming**: Links the mm trip, ms dwell trip, and `CONF_PSF_WALL_SAT_MS` rail guard to `g_sync_trip_armed`, preventing false trips upon initial engagement or stall transitions.
- **Type-D Protection**: Retains the `g_buf_sensor_type != BUF_SENSOR_TYPE_D` guard (`firmware/src/sync.c:1645`), preserving microswitch relay semantics.

### 3. Concerns
- **[MEDIUM] Hold Edge-Detection Placement**: D-26 requires clearing the accumulator and arm flag on the falling edge of deliberate holds (`sync_type_p_hold_in_progress()`). If hold state checks are placed only inside `sync_check_tension_dwell_and_ramp()`, the falling edge could be missed if the buffer is not in `BUF_TENSION` at the moment the hold releases. Edge tracking must be processed in `sync_tick()` before gated sub-routines.
- **[LOW] Diagnostic Shadowing of Warn Event**: Default `SYNC_TENSION_STOP_MM` (32 mm) trips and resets the accumulator before `CONF_SYNC_CANNOT_REFILL_MM` (50.0 mm, `firmware/src/sync_buf.c:966`) can fire. While documented in `MANUAL.md`, `cannot_refill` becomes effectively dormant on Type-P systems unless the stop threshold is raised or disabled.
- **[LOW] Deserialization Range Clamping**: Similar to Plan 01, `g_sync_tension_stop_mm` should be clamped to `[0.0f, 500.0f]` in `settings_load_tlv_tag()` to prevent unvalidated values from corrupted flash sectors.

### 4. Suggestions
- Maintain a static `g_sync_prev_hold_active` in `firmware/src/sync.c` evaluated unconditionally in `sync_tick()` so that transition out of a deliberate hold reliably zeroes `g_sync_refill_effort_mm` and clears `g_sync_trip_armed` regardless of current buffer state.
- In `firmware/src/protocol_status.c:68`, verify that `TM:` explicitly formats as `0.0` whenever `!g_sync_trip_armed` to avoid reporting misleading non-zero accumulation while the trip is inactive.

### 5. Risk Assessment
**LOW**. Leveraging the existing accumulator and creating the static line-budget test makes this a safe, well-bounded change.

---

## 13-03

### 1. Summary
Plan 13-03 adds the bounded firmware-local feed probe to resolve "+1.0 tension" ambiguity between a stalled/starved line and an unmoving home rail. It evaluates after `SYNC_FEED_PROBE_MM` (16 mm, derived from `g_buf_max_travel_mm`) of pinned travel on the shared accumulator, verifying whether `g_buf_pos` has moved at least `BL_BREAK_DELTA_NORM` (0.25f, `firmware/include/sync_internal.h:35`) off `g_sync_tension_extreme`. It publishes `PR:` telemetry (`0=none`, `1=running`, `2=consumer`, `3=no-consumer`), short-circuits to fault hold on `NO_CONSUMER`, exposes the `PROBE:` diagnostic serial command, and enforces SC#5 compliance.

### 2. Strengths
- **Structural Ordering**: Evaluating the probe at 16 mm and the fault trip at 32 mm on the same accumulator guarantees the probe always resolves before the distance trip can fire, eliminating race conditions.
- **Zero Additional Movement**: Operates as a passive observation window over commanded relief feed rather than executing dedicated probe moves.
- **Reusing Hardware Constants**: Reuses `BL_BREAK_DELTA_NORM` (`firmware/include/sync_internal.h:35`), leveraging the validated deflection delta from the `51bdca8` fix.
- **Strict Boundary Isolation**: Protects against architectural creep by verifying via `git diff` that `firmware/src/sync_relay.c` remains untouched, strictly enforcing SC#5.

### 3. Concerns
- **[HIGH] Point-in-Time Single-Sample Evaluation**: Evaluating the probe at the exact tick `g_sync_refill_effort_mm >= SYNC_FEED_PROBE_MM` poses false-trip risks. If an extruder consumes filament at 15 mm/s while FLARE relieves at 18 mm/s, net buffer accumulation over 16 mm of feed is only ~2.6 mm (less than `16 mm * 0.25 = 4 mm`). Furthermore, spring bounce or measurement noise at that single tick could read below `extreme + BL_BREAK_DELTA_NORM`, falsely triggering `NO_CONSUMER` and aborting a valid print.
- **[MEDIUM] Dynamic Threshold Inversion Risk**: `SYNC_FEED_PROBE_MM` is mapped to `g_buf_max_travel_mm`. If an operator configures a large buffer travel (`g_buf_max_travel_mm = 35 mm`, allowed up to 50 mm in `settings_store.c:568`) while `SYNC_TENSION_STOP_MM` is at default (32 mm), the distance trip will fire *before* the probe window evaluates, inverting the structural ordering guarantee.
- **[LOW] Protocol Bypass Table**: The plan specifies excluding `PROBE:` from `is_motion_cmd` (`firmware/src/protocol.c:1971`), which is correct since it initiates no motion. However, `cmd_execute` must ensure `PROBE:` explicitly checks that `g_buf_sensor_type == BUF_SENSOR_TYPE_P` and sync is active before arming.

### 4. Suggestions
- Latch the maximum deflection observed *across the entire 16 mm window*, rather than taking a point-in-time snapshot at 16 mm, so transient noise or print demand pulses do not cause false `NO_CONSUMER` classifications.
- Enforce in code that the probe threshold is capped relative to the trip threshold: `float probe_mm = fminf((float)g_buf_max_travel_mm, g_sync_tension_stop_mm * 0.5f);`.

### 5. Risk Assessment
**MEDIUM**. The structural logic is clean, but point-in-time threshold evaluation on a dynamic spring-loaded buffer introduces real risks of false `NO_CONSUMER` trips under high print flow.

---

## 13-04

### 1. Summary
Plan 13-04 handles physical hardware validation on the Type-P rig. It establishes an acceptance document (`13-HW-VALIDATION.md`), enforces a pre-flight reboot verification for flash persistence, gates execution behind a human-in-the-loop checkpoint (`blocking-human`) running a 2-hour, ~10-swap print matching the 2026-09-12 baseline, and records quantified outcomes in `ROADMAP.md` and `STATE.md`.

### 2. Strengths
- **Protocol Compliance**: Strictly enforces `AGENTS.md` Rule 12; no `HW:` checklist items are ticked autonomously.
- **Quantified Acceptance Bar**: Directly references the empirical baseline in `baseline-capture.md` (relief-pause frequency <= 1/60s vs dozens every 3s; `SYNC:TENSION_RISK_HIGH` < 13; zero false trips; rail-relative deflection limits).
- **Pre-Flight Persistence Check**: Validates that new knobs survive an actual reboot prior to initiating the 2-hour print run, preventing invalid captures caused by volatile `SET:` states.
- **Deterministic Separation**: Explicitly lists deterministic properties proven in `flare_sim`, focusing rig time entirely on physical mechanical validation.

### 3. Concerns
- **[LOW] Log Processing Volume**: A 2-hour continuous capture via `scripts/verify_hw_open_items.py watch` produces substantial log data; providing exact, automated grep/awk parsing recipes in `13-HW-VALIDATION.md` is necessary to prevent human calculation errors.

### 4. Suggestions
- Provide a companion verification script (e.g. `scripts/verify_phase13_hw_log.py`) that consumes the capture log and automatically evaluates the four pass-bar metrics against the baseline.

### 5. Risk Assessment
**LOW**. Fully human-gated, clearly scoped, and methodologically sound.

---

## Cross-Plan Architectural Coherence & Edge Cases

1. **Relief Response vs. Filter Lag (13-01 & 13-03)**:
   If urgent relief feed in 13-01 is dampened by the 25 mm target EMA (`sync_apply_type_p_smoothing`), the buffer will struggle to advance by 4 mm (`BL_BREAK_DELTA_NORM`) during the 16 mm probe window in 13-03 while printing at medium-to-high volumetric speeds. Resolving the EMA lag in 13-01 directly protects the probe in 13-03 from false `NO_CONSUMER` trips.
2. **Threshold Relationship Invariant (13-02 & 13-03)**:
   The structural assumption `PROBE_MM < SYNC_TENSION_STOP_MM < CONF_SYNC_CANNOT_REFILL_MM` holds at default values (16 mm < 32 mm < 50 mm), but runtime changes to `g_buf_max_travel_mm` or `SYNC_TENSION_STOP_MM` can invert this hierarchy. Clamping `PROBE_MM` dynamically preserves this invariant.
3. **TLV Serialization Sequence (13-01 & 13-02)**:
   Plan 13-01 allocates tag 65 (`TAG_SYNC_PSF_RELIEF_MULT`) and Plan 13-02 allocates tag 66 (`TAG_SYNC_TENSION_STOP_MM`) following `TAG_FLASH_ERASE_COUNT = 64` (`firmware/include/settings_store.h:84`). Both plans correctly mandate re-checking the tag enum to prevent collisions.

---

## Overall Assessment

The implementation plans for Phase 13 are of high quality, displaying strong familiarity with the FLARE codebase, prior regression fixes (`51bdca8`), and simulation harnesses. Addressing the **target EMA lag in 13-01** and implementing a **window-based maximum deflection check for the probe in 13-03** will ensure robust real-world performance on physical hardware.

---

## Consensus Summary

Only one reviewer lane (Antigravity) was available for this run — Claude was skipped for independence (this session runs inside Claude Code), and gemini/codex/coderabbit/opencode/qwen/cursor/kimi-code/ollama/lm_studio/llama_cpp were not detected on this host. The findings below reflect a single grounded, source-verified review rather than cross-model consensus.

### Agreed Strengths
(single-reviewer run — no cross-reviewer agreement to report)

### Agreed Concerns
(single-reviewer run — no cross-reviewer agreement to report)

### Divergent Views
(single-reviewer run — no divergence to report)

### Single-Reviewer Findings Worth Prioritizing
- **[HIGH] 13-01 — Target EMA lag during urgent relief**: the 25 mm distance-EMA in `sync_apply_type_p_smoothing()` (`firmware/src/sync.c:1868-1872`) may throttle the bounded relief target's ramp-up on a 16 mm buffer, undermining the responsiveness the relief snap is meant to provide. Suggested fix: bypass or shrink the filter window while in the relief branch.
- **[HIGH] 13-03 — Point-in-time probe evaluation risk**: evaluating `NO_CONSUMER` at a single tick when the accumulator crosses `SYNC_FEED_PROBE_MM` is vulnerable to noise/spring-bounce false positives and slow-consumer false positives. Suggested fix: latch max deflection over the whole probe window rather than sampling once at the boundary.
- **[MEDIUM] 13-03 — Threshold ordering can invert**: `SYNC_FEED_PROBE_MM` scales with `g_buf_max_travel_mm`, which can exceed `SYNC_TENSION_STOP_MM` at non-default configs, breaking the assumed `PROBE_MM < STOP_MM < CANNOT_REFILL_MM` ordering. Suggested fix: clamp `PROBE_MM` to a fraction of `SYNC_TENSION_STOP_MM` at runtime.
- **[MEDIUM] 13-02 — Hold falling-edge detection placement**: clearing the trip accumulator/arm flag on hold release must be evaluated unconditionally in `sync_tick()`, not only inside a gated sub-routine, or the edge can be missed.
- **[MEDIUM] 13-01/13-02 — TLV deserializer clamp omission**: new float knobs (`SYNC_PSF_RELIEF_MULT`, `SYNC_TENSION_STOP_MM`) load via raw `memcpy` with no range clamp on corrupt/invalid flash data.
