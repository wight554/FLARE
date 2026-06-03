# Tasks — type-d-dynamic-flow

Scope: fast-step TENSION burst only. Slow drift is already handled by
`SYNC_RESERVE_PCT 65` (verified, capture B — slow benchy rides COMPRESSION,
REGRESSION PASS). Feed floor already demoted to `SYNC_MIN_RATE 100`. The proactive
NEUTRAL soft-wall lean was dropped (reserve handles slow drift; dead-reckon gives
no gradual signal — capture B `BP` frozen then jumps). See design.md.

## 0. HW diagnosis (DONE — capture A)

- [x] 0.1 Capture A (test print 300↔1500) confirmed: `MM≈3000` (catchup fine),
  `EST` creeps ~+200 sps/touch over 4-5 touches/infill-entry
  (`667→892→1074→1194→1382`, TPX→12), first crossing `AV −2.7..−4.6 mm/s`,
  re-touches pinned `AV=0`. → Piece 2 (velocity snap + burst escalation) is the
  fix. Capture B confirmed slow drift is already handled by reserve65 → Piece 1
  dropped.

## 1. Tension-crossing EST: velocity snap + consecutive-tension escalation

- [ ] 1.1 `firmware/src/sync.c` (`buf_update`, `neutral_drain_sample` /
  `→ BUF_TENSION` path ~`sync.c:1164-1178`): **Trigger 1 (velocity snap):**
  `v_norm = clamp(|arm_vel_mm_s| / SYNC_TENSION_FAST_MM_S, 0, 1)`;
  `est_sample = lerp(feed_avg*1.15, mmu_feed + drain_rate, v_norm)`;
  `alpha = lerp(EST_ALPHA_MAX, 1.0, v_norm)` (bypass the ALPHA_MAX clamp like the
  §20 attack); `EST = max(EST, blended)`.
- [ ] 1.2 **Trigger 2 (burst escalation):** track `last_tension_ms` + `burst_n`.
  If a TENSION crossing is within `SYNC_TENSION_BURST_MS` of the prior one,
  `burst_n++` and add `SYNC_TENSION_ESC_STEP_SPS * SYNC_TENSION_ESC_RATIO^burst_n`
  to EST (clamp to a demand ceiling). Reset `burst_n` when a `BUF_NEUTRAL` dwell
  holds past `SYNC_TENSION_BURST_MS`. Geometric/aggressive is correct here
  (tension-recovery direction; overshoot → COMPRESSION is safe).
- [ ] 1.3 New knobs (non-persisted, plumb like `SYNC_COMPRESSION_DRAIN_FRAC`):
  `SYNC_TENSION_FAST_MM_S` (≈2 mm/s), `SYNC_TENSION_BURST_MS` (≈300-500),
  `SYNC_TENSION_ESC_STEP_SPS`, `SYNC_TENSION_ESC_RATIO` (≈1.5-2). No
  `SETTINGS_VERSION` bump. Mind the 32-char param-name limit (§21).
- [ ] 1.4 Slow single crossing (`v_norm≈0`, `burst_n=0`) MUST reproduce today's
  `feed_avg*1.15` exactly (no `7178c34` regression).
- [ ] 1.5 `BUF_SENSOR_TYPE == 0` only; type-P estimator untouched.
- [ ] 1.6 Build + tests green; OpenSpec strict.

## 2. (Optional) velocity-scaled catchup ramp

- [ ] 2.1 Scale the `SYNC_TENSION_RAMP_DELAY` collapse by `v_norm` so a fast
  crossing reaches max feed a tick sooner. Ship only if §1 leaves the *first*
  touch clearing too slowly (catchup magnitude is already adequate).

## 3. Docs

- [ ] 3.1 `TUNING.md` / `BEHAVIOR.md` / `MANUAL.md`: document the new tension-
  recovery knobs and that the fast-step TENSION touch is reduced to a single
  positioning touch (not eliminated — see non-goals). Note the floor stays demoted
  and reserve handles slow drift.

## 4. HW validation

- [ ] 4.1 Fast-step print (300↔1500): the burst collapses to at most **one**
  TENSION touch per step; EST correct on first touch (no re-drain). Sweep
  `SYNC_TENSION_FAST_MM_S` / `SYNC_TENSION_BURST_MS` / escalation ratio.
- [ ] 4.2 Regression: a slow single crossing does not overshoot into COMPRESSION
  (the `7178c34` gentle path still applies); slow benchy still rides the reserve.
- [ ] 4.3 Record final knob defaults; finalize gen_config defaults.
