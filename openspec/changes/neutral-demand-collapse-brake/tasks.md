## 1. NEUTRAL-band demand-collapse estimator corrector

- [x] 1.1 In `firmware/src/sync.c`, locate the estimator corrector chain in
  `sync_tick` (TENSION catch-up ~`sync.c:1411`, NEUTRAL `model_stalled_*`
  ~`sync.c:1418`, COMPRESSION bleed-down ~`sync.c:1458`) and confirm there is no
  in-band NEUTRAL corrector when the virtual model is not pinned at a rail.
- [x] 1.2 Add a NEUTRAL branch (or extend the existing `model_stalled` block)
  that detects the buffer drifting toward COMPRESSION in the open band: virtual
  position `g_buf_pos` decreasing / `reserve_error_mm` going negative, gated to
  `s == BUF_NEUTRAL`, active feed (`sync_current_sps > 0`), NEUTRAL dwell
  `> 2000u`, and no recent TENSION refill (reuse `sync_recent_negative_until_ms`
  / tension-pin timing).
- [x] 1.3 Decay `extruder_est_sps` toward the arrest-the-drift rate
  (`lane_motion_sps(A) / RELAY_NEUTRAL_FRAC` minus a small margin, mirroring the
  `model_stalled_tension` margin) using the rail-corrector EWMA alpha; set
  `extruder_est_last_update_ms = now_ms` on update.
- [x] 1.4 Gate the entire branch to `BUF_SENSOR_TYPE == 0`; verify the type-P
  analog path is untouched.

  2026-05-21 validation: implemented in `firmware/src/sync.c` with a type-D
  NEUTRAL drift tracker, compression-side reserve-error gate, active-feed and
  2 s dwell gate, recent-TENSION holdoff, and rail-corrector alpha (`0.05`).
  Resolved the relay target to `lane_motion_sps(A) / RELAY_NEUTRAL_FRAC - 4`
  so the estimator value actually arrests the type-D relay drift.

## 2. Fast-brake arming on NEUTRAL to COMPRESSION

- [x] 2.1 In `sync_on_transition` (~`sync.c:1112`) extend the fast-brake arming
  from `prev == BUF_TENSION && now == BUF_COMPRESSION` to also fire on
  `prev == BUF_NEUTRAL && now == BUF_COMPRESSION`.
- [x] 2.2 Gate the new arming to a hot MMU feed rate (derive the threshold from
  `baseline_control_floor_sps()` or a margin over `SYNC_MIN_SPS`, per design
  D4) so a slow benign drift does not arm a hard brake.
- [x] 2.3 Confirm the existing `TENSION → COMPRESSION` arming and the relief /
  continuous-compression auto-stop paths are unchanged.

  2026-05-21 validation: existing `TENSION -> COMPRESSION` arming remains
  unconditional. New `NEUTRAL -> COMPRESSION` arming is type-D-only and hot-gated
  at `max(baseline_control_floor_sps() / 2, SYNC_MIN_SPS + PRE_RAMP_SPS)`.
  Relief and continuous-compression auto-stop blocks were not changed.

## 3. Validation

- [x] 3.1 Build the firmware (per `BUILD_FLASH.md`) and confirm no warnings in
  `sync.c`.
- [ ] 3.2 Replay / re-run the fast-purge sequence on hardware; capture `?:`
  poll telemetry and confirm `g_buf_pos` (BP) stabilizes in-band with
  `extruder_est_sps` (EST) decaying during the NEUTRAL glide — no COMPRESSION
  slam, no bowden pressure.
- [ ] 3.3 Run a normal high-flow print and confirm TENSION/NEUTRAL cycling and
  EST learning are unchanged (corrector does not false-trigger).
- [x] 3.4 Confirm type-P analog mode (`BUF_SENSOR_TYPE != 0`) control output is
  byte-identical to pre-change behavior.

  2026-05-21 validation: `ninja -C build_local` passed. `openspec validate
  neutral-demand-collapse-brake --strict` passed. Type-P behavior is statically
  gated out of the new corrector and new fast-brake path (`BUF_SENSOR_TYPE == 0`
  checks); hardware replay/print validation remains pending.

## 4. Documentation

- [x] 4.1 Update `TUNING.md` / relevant docs if a new tunable (fast-brake hot
  threshold or NEUTRAL decay margin/alpha) is introduced; otherwise note the
  behavior change in the appropriate doc.

  2026-05-21 validation: no new tunable introduced. Updated `BEHAVIOR.md`,
  `TUNING.md`, and the change proposal/spec/design notes to describe the
  automatic type-D-only demand-collapse correction and hot fast-brake behavior.
