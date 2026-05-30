## 1. NVM and Variables

- [x] 1.1 `settings_store.c`: remove `buf_range` (float) and `buf_invert` (bool) fields from `settings_t`; add `buf_psf_max_comp`, `buf_psf_max_tens`, `buf_psf_neutral` (replaces `buf_analog_neutral`), `buf_psf_goal` (float). Verify `sizeof(settings_t) <= 512`.
- [x] 1.2 `settings_store.c`: update `settings_defaults()` — `buf_psf_max_comp=0.0`, `buf_psf_max_tens=1.0`, `buf_psf_neutral=0.5`, `buf_psf_goal=0.3`.
- [x] 1.3 `settings_store.c`: update `settings_save()` and `settings_load()` for new fields.
- [x] 1.4 `main.c`: remove `BUF_INVERT` declaration; add `BUF_PSF_MAX_COMP`, `BUF_PSF_MAX_TENS`, `BUF_PSF_NEUTRAL` (replaces `BUF_ANALOG_NEUTRAL`), `BUF_GOAL` float declarations with defaults.
- [x] 1.5 `controller_shared.h`: update externs (remove `BUF_INVERT`, `BUF_RANGE`, `BUF_ANALOG_NEUTRAL`; add new vars).
- [x] 1.6 `tune.h`: remove `CONF_BUF_RANGE`; add `CONF_BUF_PSF_MAX_COMP=0.0f`, `CONF_BUF_PSF_MAX_TENS=1.0f`, `CONF_BUF_PSF_NEUTRAL=0.5f`, `CONF_BUF_GOAL=0.3f`.

## 2. Protocol

- [x] 2.1 `protocol.c`: remove `BUF_RANGE` and `BUF_INVERT` SET/GET handlers.
- [x] 2.2 `protocol.c`: rename `BUF_ANALOG_NEUTRAL` → `BUF_PSF_NEUTRAL` in GET/SET.
- [x] 2.3 `protocol.c`: add `BUF_PSF_MAX_COMP`, `BUF_PSF_MAX_TENS`, `BUF_GOAL` SET/GET handlers (clamp to [0,1]).
- [x] 2.4 `protocol.c`: add `CAL:PSF_COMP` handler — call `buf_analog_update()` once, store raw ADC fraction to `BUF_PSF_MAX_COMP`, call `settings_save()`.
- [x] 2.5 `protocol.c`: add `CAL:PSF_TENS` handler — same pattern for `BUF_PSF_MAX_TENS`.
- [x] 2.6 `protocol.c`: add `CAL:PSF_NEUT` handler — same pattern for `BUF_PSF_NEUTRAL`.

## 3. Analog Normalization

- [x] 3.1 `sync.c` `buf_analog_update()`: replace symmetric delta/scale formula with asymmetric HH-style mapping (D4). Remove `BUF_INVERT` reference. Auto-derive `reversed = (BUF_PSF_MAX_COMP < BUF_PSF_MAX_TENS)`.
- [x] 3.2 `sync.c` `buf_state_raw()` type-P branch: replace `BUF_THR` comparison with goal-relative zone boundaries (D3). Compute `goal_norm` from `BUF_GOAL` via same asymmetric formula; use `PSF_ZONE_DEADBAND = 0.1f`.

## 4. Unified PD Normalization

- [x] 4.1 `sync.c`: add `static float buf_pos_norm(void)` — type-D: `g_buf_pos / buf_threshold_mm()`; type-P: `g_buf_pos`. Guard division by `> 0.001f`.
- [x] 4.2 `sync.c`: add `static float buf_target_norm(void)` — type-D: `buf_target_reserve_mm() / buf_threshold_mm()`; type-P: `psf_goal_norm()` (same asymmetric formula as D3 applied to `BUF_GOAL`).
- [x] 4.3 `sync.c` `sync_tick()` PD entry: replace `bp_eff` and `effective_target` computation with `pos_norm` and `target_norm` from new helpers; replace `reserve_error_mm / threshold` division with `error_norm` directly.
- [x] 4.4 `sync.c` `sync_apply_scaling()`: delete type-P early-return branch (L446-448); confirm unified taper path handles both types correctly with normalized inputs.

## 5. Control Law Extraction

- [x] 5.1 `sync.c`: extract `static int relay_control_law(buf_state_t s)` from L1712-1724 — type-D 3-zone bangbang.
- [x] 5.2 `sync.c`: add `static int psf_control_law(float error_norm)` — continuous PD: `baseline_control_floor_sps() + (int)(error_norm * kp_window)`, clamped to `[0, max_sps]`.
- [x] 5.3 `sync.c` `sync_tick()`: replace inline `if (BUF_SENSOR_TYPE == 0)` control block with dispatch to `relay_control_law()` or `psf_control_law()`.
- [x] 5.4 `sync.c`: remove `compression_floor` block at L1750 (`BUF_SENSOR_TYPE != 0 && BUF_COMPRESSION` force-raise).
- [x] 5.5 `sync.c` `BUF_SENSOR_TYPE == 0 && s == BUF_COMPRESSION → target_sps = 0` at L1732: confirm this true-stop clamp remains gated to type-D only after refactor.

## 6. Signal Publication Refactor

- [x] 6.1 `sync.c`: extract `static void buf_signal_publish(uint32_t now_ms)` from `buf_sensor_tick()` L1200-1254 — dispatches by sensor type, writes `g_buf_signal` fields once.
- [x] 6.2 `sync.c` `buf_sensor_tick()`: replace inline signal population block with call to `buf_signal_publish(now_ms)`.

## 7. Gate Estimator-Compensation to Type-D (D9)

- [x] 7.1 `sync.c`: confirm `buf_virtual_position_tick()`, residual/drift observer (L816-834), sigma/variance/confidence (L1208-1232), variance-aware blend (L1371), confidence bias shift (L1412), model-stalled detection (L1462-1505), EST_FALLBACK (L1213), estimator alpha adaptation (L799) are all gated `BUF_SENSOR_TYPE == 0`. Add gates where missing.
- [x] 7.2 `sync.c`: for type-P set confidence = 1.0 unless saturated (keep existing L1239 saturation→0.5 path).

## 8. Continuous Estimator + PD Control (Layer 1, D10-D12)

- [x] 8.1 `sync.c`: add `static float g_buf_pos_prev`; in `buf_analog_update()` compute `vel_norm = (g_buf_pos - g_buf_pos_prev) / dt_s`, update `g_buf_pos_prev` at end.
- [x] 8.2 `sync.c`: add `PSF_VEL_ALPHA` LPF on `vel_norm` → `vel_norm_f` (filtered derivative, D12).
- [x] 8.3 `sync.c`: for type-P, update `extruder_est_sps` every tick via `extruder_mm_s = mmu_mm_s + vel_norm * half_travel_mm` (reuse L788-789 formula).
- [x] 8.4 `sync.c` `psf_control_law()`: implement `target = extruder_est + Kp*error_norm (dead-zoned by PSF_CTRL_DEADBAND) + KD_PSF*vel_norm_f` (D11). Replaces the stub from task 5.2.
- [x] 8.5 `tune.h`: add `CONF_PSF_CTRL_DEADBAND`, `CONF_KD_PSF`, `CONF_PSF_VEL_ALPHA` defaults.
- [x] 8.6 `protocol.c` + `settings_store.c`: make `KD_PSF` and type-P P-gain runtime-settable + persisted (rig tuning). Re-check `sizeof(settings_t) <= 512`.

## 9. Soft Walls (Layer 2, D13)

- [x] 9.1 `sync.c`: add soft-wall blend in type-P control path — `wall = (|pos_norm| - PSF_SOFT_WALL_START)/(1 - PSF_SOFT_WALL_START)` clamped [0,1]; tension side lerp target→max_sps, compression side lerp target→0.
- [x] 9.2 `tune.h`: add `CONF_PSF_SOFT_WALL_START=0.8f`.

## 10. Hard Catch + Stop/Slowdown (Layer 3, D14)

- [x] 10.1 `sync.c`: detect rapid `|vel_norm|` toward compression > `PSF_JUMP_NORM_PER_S` → engage `sync_fast_brake_until_ms` (reversible).
- [x] 10.2 `sync.c`: stop/slowdown state machine — after brake, within `PSF_STOP_CONFIRM_MS`: vel_norm positive → resume PD; still pinned compression → `sync_relief_pause()`.
- [x] 10.3 `sync.c`: saturation-sustained — `pos_norm <= -0.99` for `PSF_WALL_SAT_MS` → `sync_relief_pause()`; `pos_norm >= +0.99` for `PSF_WALL_SAT_MS` → `sync_fault_hold()`.
- [x] 10.4 `tune.h`: add `CONF_PSF_JUMP_NORM_PER_S`, `CONF_PSF_STOP_CONFIRM_MS`, `CONF_PSF_WALL_SAT_MS` defaults.
- [x] 10.5 `sync.c`: confirm type-D `compression_wall_critical` (L1679) stays gated to `BUF_SRC_VIRTUAL_ENDSTOP` — untouched.

## 11. Rig Verification (hardware-blocked)

- [ ] 11.1 **BLOCKER: requires PSF rig** — Measure ADC jitter floor; size `PSF_CTRL_DEADBAND` and `KD_PSF` against it. Confirm no speed hunting at goal.
- [ ] 11.2 **BLOCKER: requires PSF rig** — Confirm Kp/Kd signs against physical buffer/gear orientation.
- [ ] 11.3 **BLOCKER: requires PSF rig** — Verify Layer 1 PD: smooth speed tracking, no oscillation; gradual response to slow drift.
- [ ] 11.4 **BLOCKER: requires PSF rig** — Verify Layer 2 soft walls: progressive blend at 0.1/0.9, no cliff.
- [ ] 11.5 **BLOCKER: requires PSF rig** — Verify Layer 3: rapid jump → reversible brake; slowdown recovers; real stop → relief_pause; saturation → relief/fault.
- [ ] 11.6 **BLOCKER: requires PSF rig** — `compression_recovery` (carried item #7): confirm trigger correct OR confirm superseded/removed for type-P by Layer 3.
- [ ] 11.7 **BLOCKER: requires PSF rig** — estimator drag-down L1498-1504 (carried item H2): confirm removed for type-P under D9, or correct if still present.
- [ ] 11.8 **BLOCKER: requires PSF rig** — Regression: type-D relay path unchanged (re-run relay steady-state check).
- [x] 11.9 **Rig**: Auto-sync transition gating (Type-P only). Prevent spurious auto-sync when homed or booted at tension. Only trigger when transitioning to tension, and require `g_buf_pos > 0.6f` for Type-P analog sensor. Keep Type-D untouched.
- [x] 11.10 **Rig**: Gate manual load/unload buffer checks in motion.c to Type-D only, and implement high-frequency `scripts/flare_unload_tracker.py` diagnostic telemetry tracker.
- [x] 11.11 **Rig**: Implement buffer-lock (`BL:`) physical extreme targeting and highly sensitive lock-break detection for Type-P analog configuration.
- [x] 11.12 **Rig**: Implement closed-loop dynamic analog follow inside `BL_FOLLOW` for Type-P to keep the buffer neutral during fast printer retracts.
- [x] 11.13 **Rig**: Implement highly sensitive contact detection (`g_buf_pos < 0.85f`) in `TC_RELOAD_APPROACH` for Type-P to detect tail contact instantly.
- [x] 11.14 Add `PSF_TENSION_PIN_NORM` (`0.90f`, rig-tune) + `PSF_UNLOAD_RELIEF_ARM_MS` (`300u`) to `controller_shared.h` — sibling of `PSF_HOME_THRESHOLD_NORM`, reachable from `motion.c` (which does not include `tune.h`). Velocity-slam threshold stays `CONF_PSF_JUMP_NORM_PER_S` in `tune.h`, single-sourced via the `sync_buf_tension_slam()` accessor. (D22)
- [x] 11.15 `motion.c` `TASK_UNLOAD`: type-P relief jog — extended the recover branch (L297) via `buf_recover_due`; pinned `g_buf_pos >= PSF_TENSION_PIN_NORM` for `PSF_UNLOAD_RELIEF_ARM_MS` (or a velocity slam) && `!unload_buf_recover_done` → one-shot forward jog reusing the existing latch jog-then-reverse mechanic (bounded by `BUF_SWITCH_SPAN_HALF_MM` at `BUF_STAB_SPS`, distance via `dist_at_out_mm`). (D22 tier C)
- [x] 11.16 `motion.c` `UNLOAD_TENSION_BLOCK` (L341): `buf_overtensioned` substitutes `g_buf_pos >= PSF_TENSION_PIN_NORM` for the `g_buf.state == BUF_TENSION` test on type-P; reuses `buf_tension_since_ms` + `UNLOAD_TENSION_BLOCK_MS`; double-load exception kept. (D22 tier D)
- [x] 11.17 Tier A folded into tier C: `sync_buf_tension_slam()` (new `sync.c`/`sync.h` accessor = `g_vel_norm > CONF_PSF_JUMP_NORM_PER_S`) short-circuits the relief dwell, so a slam triggers the stop-then-jog within one control tick — reversible via the bounded jog-then-resume. No separate brake state machine. (D22 tier A)
- [ ] 11.18 **BLOCKER: requires PSF rig** — measure healthy-unload buffer floor; set `PSF_TENSION_PIN_NORM` above it. Verify tug-of-war → `UNLOAD_BLOCKED`, healthy + relax-to-home → no false block, TC-unload parity, velocity-slam sign + threshold.
- [x] 11.19 Gate A needs no `PSF_STAB_DEADBAND`: `buf_state_raw()` already reports `BUF_NEUTRAL` within `PSF_ZONE_DEADBAND` (0.1) of goal (D3), which is the stabilize stop tolerance. Folded. (D23 Gate A)
- [x] 11.20 `sync.c` `buffer_stabilize_start_internal` (L735): replaced the type-P hard-return with a board-local presence gate (`lane_out_present`/`lane_in_present` on `pick_boot_stabilize_lane()`); present → fall through to the shared goal-relative path; absent → no-op (tension=home, no dry-spin). Direction (`forward = buf_state==BUF_TENSION`) and motor-start reuse the existing path. Type-D untouched. (D23 Gate A)
- [x] 11.21 Stabilize tick (L816–862) already keys on `buf_state_raw()==BUF_NEUTRAL` for `BUF_STAB DONE` and on the goal-relative zones for the overshoot REVERSE, so type-P drives to goal and stops correctly with no change. Confirmed. (D23 Gate A)
- [x] 11.22 Gate B (klipper-agnostic) already provided: continuous feedforward (D10, L1663), soft-wall tension→max refill (D13, L1732), and terminal `fault_hold` on sustained tension at the home rail (type-P tension-dwell L2078 + saturation L1775). No host-pause dependency. Velocity pre-catch NOT added blind (overlaps the soft wall) — deferred to rig (11.23) to decide if it adds value. (D23 Gate B)
- [ ] 11.23 **BLOCKER: requires PSF rig** — tune Gate B so normal print flow never reaches home (feedforward + soft-wall-start + pre-catch + goal bias) and the terminal fault fires only on genuine jam, not transient fast moves. Verify Gate A: loaded → stabilize-to-goal, unloaded → no-op. Confirm `g_vel_norm` tension sign.
- [x] 11.24 **Rig**: Fix uninitialized stable state variables on boot causing spurious auto-sync. Implement `sync_init(void)` and call it at boot. Restore Type-P boot stabilization even when in home state, with a highly responsive 200ms stagnant timeout to stop immediately if blocked/homed.
- [x] 11.25 **Rig**: Disable boot buffer stabilization entirely as requested by user ("no buffer stab on boot").



## 12. Loop-Rate Bump (stretch, D16)

- [ ] 12.1 **rig** — Measure whether 50Hz is the fast-move bottleneck or motor accel is. If loop-bound, add decoupled `PSF_TICK_MS` (target 100-200Hz) for type-P sensor/control; keep telemetry at 50Hz.

## 13. Tip-Shaping Validation (stretch, D17)

- [ ] 13.1 **rig** — Define acceptance test: extruder tip-forming moves absorbed by buffer without saturation, MMU gear follows live. Measure travel/accel headroom.
- [ ] 13.2 **rig** — If acceptance met, document host-side follow-up to drop manual retract triggers (separate change, host-side). If not met, record limits and keep retracts.

## 14. Build and Closeout

- [x] 14.1 `cmake --build build_local` — confirm clean build, no warnings for modified files.
- [x] 14.2 Confirm carried items resolved: #6 (compression_floor) removed in group 5; #7 and H2 resolved or superseded per tasks 11.6/11.7. (Supersedes the former `pending-analog-rig` tracker, now merged here.)
- [x] 14.3 `openspec validate psf-analog-rig --strict` — passes.
- [x] 14.4 Commit milestone(s) — split firmware foundation (groups 1-7) from control redesign (groups 8-10) into separate commits per AGENTS.md one-milestone-per-commit.

2026-05-27 validation: `python3 -m py_compile scripts/*.py`, `python3 scripts/gen_config.py`, `git diff --check`, `ninja -C build_local`. Commit `4f47251`.
2026-05-29 validation: `python3 -m py_compile scripts/*.py`, `ninja -C build_local`. Commits: `88ac086` (config defaults), `4fd2efc` (gate load/unload buffer checks), `254aa98` (flare_unload_tracker.py telemetry script), `a5c6893` (BL: buffer-lock analog implementation), `e2686f4` (closed-loop dynamic follow inside BL_FOLLOW for type-P), `b51f6db` (sensitive RELOAD contact detection for type-P), `551c41a` (PSF home and deviation named constants).
2026-05-30 validation: D22 type-P unload over-tension guard (11.14–11.17) — `controller_shared.h` consts, `sync_buf_tension_slam()` accessor (`sync.c`/`sync.h`), `motion.c` `TASK_UNLOAD` relief jog + block extended to type-P. `ninja -C build_local` clean, no warnings, links. 11.18 rig-blocked. (uncommitted)
2026-05-30 RIG FIX (first PSF rig session): type-P self-faulted to `SYNC_FAULT_HOLD` at the home tension rail. Root cause: the type-P Layer-3 saturation catch (`sync.c` ~L1761) ran on every `sync_tick` *before* the `!sync_enabled` guard, so the resting home rail (`g_buf_pos`≈+1.0) tripped `sync_fault_hold()` at idle and broke a manual `UL` (`ST:4`, ~0.1mm). Fix A: gate that block to `sync_enabled` (over-tension is a fault only while actively syncing a loaded buffer). Fix B: arm the unload over-tension guard only after the buffer leaves the rail once (`g_buf_pos < PSF_HOME_DEVIATION_THRESHOLD_NORM`) via new lane flag `unload_buf_left_rail`, so a mid-tube/unloaded retract that never leaves home can't false-`UNLOAD_BLOCKED`. `ninja` clean.
2026-05-30 RIG FIX 2: Spurious auto-sync triggered on boot for Type-P analog configuration. Root cause: at boot, the buffer tracker was initialized to BUF_NEUTRAL, and the analog sensor reading was not updated during the 25 ms debouncing settling loop. Consequently, on the first sensor tick, a transition from BUF_NEUTRAL to BUF_TENSION was spuriously detected. This tripped the g_sync_tension_transitioned flag, which caused auto-sync to trigger automatically at boot when filament was physically present. Once in active sync, it fell into an infinite loop of saturation faulting (ST:4) and auto-recovering right back into sync, ignoring user ST commands. Fix: call buf_analog_update() in the 25 ms debounce loop if BUF_SENSOR_TYPE == 1, then initialize g_buf.state to the actual buf_state_raw() on boot. Ninja clean. Commit `af5e6e4`.
2026-05-30 RIG FIX 3: Spurious auto-sync and motor run leak after manual unload (UL) completed/blocked. Root cause 1: when TASK_UNLOAD retracted filament and pulled the buffer to tension, sync_on_transition() transitioned BUF_NEUTRAL → BUF_TENSION and armed g_sync_tension_transitioned = true. Once the unload ended or was blocked (task became TASK_IDLE), sync_tick() immediately auto-started sync since the buffer was still physically at tension, causing a feed forward at 2400 sps. Root cause 2: when the filament reached the toolhead, Klipper sent TS:1, which called sync_set_state(SYNC_OFF) but bypassed sync_disable(false), leaving the active lane in TASK_FEED and the motor running forward indefinitely. Fix 1: gate g_sync_tension_transitioned to only arm when the lane is actively in TASK_IDLE or TASK_FEED. Fix 2: change TS command handler to use sync_disable(false) to cleanly halt the motor and reset the lane task. Ninja clean. Commit `868c297`.
2026-05-30 RIG FIX 4: Spurious auto-sync after a blocked unload when BUF_HOME_STATE != 1. Root cause: the is_tension_active check bypassed the transition flag gate (g_sync_tension_transitioned) and evaluated to true whenever the buffer position exceeded 0.6f if BUF_HOME_STATE was set to a non-tension state (e.g. 0/neutral). When a manual unload blocked and aborted, leaving the buffer physically pinned at tension, this bypass immediately auto-started sync. Fix: always require g_sync_tension_transitioned to be true for Type-P auto-sync, regardless of BUF_HOME_STATE. Ninja clean. Commit `8688ee5`.
2026-05-30 RIG FIX 5: Buffer slammed to compression during manual load (FL) / auto-load for Type-P. Root cause: manual load and auto-load contact detection in TASK_LOAD_FULL required the buffer to go all the way to BUF_COMPRESSION (g_buf_pos < -0.50), causing the filament to slam hard into the extruder gears and compress the buffer completely before registering the load as complete. Fix: implement fast contact detection in TASK_LOAD_FULL for Type-P using the highly sensitive PSF_HOME_DEVIATION_THRESHOLD_NORM (0.85f) contact boundary, triggering the load as complete the instant the buffer departs the home tension rail by 15%. Ninja clean. Commit `243e953`.
2026-05-30 RIG FIX 6: Manual unload (UL command) blocked immediately when buffer changed from compression to tension, with no relief jog occurring. Root cause: start_manual_unload_lane() initialized A->unload_buf_recover_done to true. This completely disabled the one-shot buffer tension relief jog during manual unloads, causing manual retracts to hit the over-tension block and fail immediately if they encountered resistance. Fix: initialize A->unload_buf_recover_done to false in start_manual_unload_lane, allowing manual unloads to benefit from the same relief jog used in toolchange unloads. Ninja clean. Commit `7703448`.
2026-05-30 RIG FIX 8: Buffer started feeding fast at boot or failed to neutralize properly. Root cause 1: `g_buf_stable_state` was initialized to BUF_NEUTRAL (0) on boot, which was different from the physical `buf_state_raw()` state (e.g. BUF_TENSION). As a result, the first stable tick processed a fake transition from BUF_NEUTRAL to BUF_TENSION, arming `g_sync_tension_transitioned = true` and auto-starting sync prematurely. Root cause 2: `buffer_stabilize_start_internal` unconditionally gated out boot stabilization if the buffer was in `BUF_HOME_STATE` (tension), preventing it from attempting any short jog to verify homing/slack. Fix 1: implement `sync_init()` to force-initialize all stable and pending states to `buf_state_raw()` on boot, preventing fake boot transitions. Fix 2: remove the `BUF_HOME_STATE` check to restore boot stabilization from the home state. Fix 3: tighten the stagnant check in `buffer_stabilize_tick` to 200 ms and a < 0.03f change threshold to stop immediately if homed/blocked. Ninja clean.
2026-05-30 RIG FIX 9: Boot stabilization was too intrusive or unwanted at boot. Fix: disabled `boot_stabilize_start` call inside `main.c` completely so no buffer stabilization runs at boot ("no buffer stab on boot"). Verified manual unload logic executed initial retract, 12.5mm forward relief jog, and 12.5mm retract back to tension perfectly before physically stalling against the closed extruder. Ninja clean.











