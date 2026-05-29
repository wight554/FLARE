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
2026-05-29 validation: `python3 -m py_compile scripts/*.py`, `ninja -C build_local`. Commits: `88ac086` (config defaults), `4fd2efc` (gate load/unload buffer checks), `254aa98` (flare_unload_tracker.py telemetry script), `a5c6893` (BL: buffer-lock analog implementation), `ac5b0dd` (closed-loop dynamic follow inside BL_FOLLOW for type-P).





