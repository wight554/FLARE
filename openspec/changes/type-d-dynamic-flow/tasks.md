# Tasks — type-d-dynamic-flow

## 0. HW diagnosis gate (confirm before/while building)

- [ ] 0.1 Capture a fast-step TENSION burst. Confirm: between touch 1 and touch 2,
  `MM` is HIGH (~3000, catchup adequate) and `EST` is LOW/creeping (the `× 1.15`
  under-correction). → confirms Piece 2 (velocity-scaled EST) is the burst fix.
- [ ] 0.2 Capture a slow-ish real print at low `SYNC_MIN_RATE` (e.g. 100). Confirm
  `BP` (`g_buf_pos`) trends toward TENSION *gradually* before a touch (→ Piece 1
  soft-wall works) vs jumps with no warning (→ dead-reckon not tracking even slow
  → fall back to a time-since-crossing ramp for Piece 1).

## 1. Proactive NEUTRAL soft-wall lean (slow-drift fix)

- [ ] 1.1 `firmware/src/sync.c`: in the type-D NEUTRAL path, compute a
  tension-side excursion `e = (-g_buf_pos) - deadband` and add a proportional
  lean `K * e` (sps) into `g_relay_neutral_trim_sps` (slew-limited toward the
  target), decaying toward 0 when `e <= 0`. Bounded by the existing trim clamp;
  only raises feed. Keep the §15 TENSION-touch step as the hard backstop.
- [ ] 1.2 New knobs `SYNC_NEUTRAL_SOFTWALL_GAIN` (sps/mm, default TBD, `0` =
  disabled) and `SYNC_NEUTRAL_SOFTWALL_DEADBAND_MM` (or reuse reserve deadband).
  Plumb non-persisted like `SYNC_COMPRESSION_DRAIN_FRAC` (SET/GET, validate list,
  gen_config, config.ini(.example), tune.h, `flare_cmd.py --dump`). No
  `SETTINGS_VERSION` bump.
- [ ] 1.3 `BUF_SENSOR_TYPE == 0` only; analog type-P feedforward byte-identical.
- [ ] 1.4 Build + tests green; OpenSpec strict.

## 2. Tension-crossing EST: velocity snap + consecutive-tension escalation

HW capture A: catchup `MM=3000` (fine); burst = EST creep ~+200 sps/touch
(`667→892→1074→1194→1382`, TPX→12), 4-5 touches/infill-entry, EST decays to ~670
between entries. First crossing has drain velocity (`AV −2.7..−4.6 mm/s`);
re-touches are pinned (`AV=0`) so velocity alone can't gate them — consecutive
touches are the signal.

- [ ] 2.1 `firmware/src/sync.c` (`buf_update`, `neutral_drain_sample` /
  `→ BUF_TENSION` path ~`sync.c:1164-1178`): **Trigger 1 (velocity snap):**
  `v_norm = clamp(|arm_vel_mm_s| / SYNC_TENSION_FAST_MM_S, 0, 1)`;
  `est_sample = lerp(feed_avg*1.15, mmu_feed + drain_rate, v_norm)`;
  `alpha = lerp(EST_ALPHA_MAX, 1.0, v_norm)` (bypass the ALPHA_MAX clamp like the
  §20 attack); `EST = max(EST, blended)`.
- [ ] 2.2 **Trigger 2 (burst escalation):** track `last_tension_ms` + `burst_n`.
  If a TENSION crossing is within `SYNC_TENSION_BURST_MS` of the prior one,
  `burst_n++` and add `SYNC_TENSION_ESC_STEP_SPS * SYNC_TENSION_ESC_RATIO^burst_n`
  to EST (clamp to a demand ceiling). Reset `burst_n` when a `BUF_NEUTRAL` dwell
  holds past `SYNC_TENSION_BURST_MS`. Geometric/aggressive is correct here
  (tension-recovery direction; overshoot → COMPRESSION is safe).
- [ ] 2.3 New knobs (non-persisted, plumb like `SYNC_COMPRESSION_DRAIN_FRAC`):
  `SYNC_TENSION_FAST_MM_S` (≈2 mm/s), `SYNC_TENSION_BURST_MS` (≈300-500),
  `SYNC_TENSION_ESC_STEP_SPS`, `SYNC_TENSION_ESC_RATIO` (≈1.5-2). No
  `SETTINGS_VERSION` bump. Mind the 32-char param-name limit (§21).
- [ ] 2.4 Slow single crossing (`v_norm≈0`, `burst_n=0`) MUST reproduce today's
  `feed_avg*1.15` exactly (no `7178c34` regression).
- [ ] 2.5 `BUF_SENSOR_TYPE == 0` only; type-P estimator untouched.
- [ ] 2.6 Build + tests green; OpenSpec strict.

## 3. (Optional) velocity-scaled catchup ramp

- [ ] 3.1 Scale the `SYNC_TENSION_RAMP_DELAY` collapse by `v_norm` so a fast
  crossing reaches max feed a tick sooner. Ship only if §2 leaves the *first*
  touch clearing too slowly (catchup magnitude is already adequate).

## 4. Demote the feed floor

- [ ] 4.1 Lower the default `SYNC_MIN_RATE` (gen_config + config.ini.example) to
  a quiet value (HW-tuned, candidate ~100-300). Keep it operator-tunable.
- [ ] 4.2 `TUNING.md`: document that slow-drift protection is the soft-wall lean
  (not the floor); `SYNC_MIN_RATE` high = optional loud zero-fast-step-skip mode.
  Update the recommended type-D config block + lever map. Update `BEHAVIOR.md` /
  `MANUAL.md` for the new knobs.
- [ ] 4.3 Build + tests green; OpenSpec strict.

## 5. HW validation

- [ ] 5.1 Slow-ish real print at the new low default: no slow TENSION drift,
  COMPRESSION clicks markedly rarer than the `SYNC_MIN_RATE 1000` config.
- [ ] 5.2 Fast-step print: at most **one** TENSION touch per step (no burst);
  EST correct on first touch (no re-drain). Sweep `SYNC_TENSION_FAST_MM_S`.
- [ ] 5.3 Regression: slow-drift overshoot that `7178c34` fixed does not return
  (gentle path still applies to slow/no-travel crossings). Sweep
  `SYNC_NEUTRAL_SOFTWALL_GAIN`.
- [ ] 5.4 Record final knob defaults; finalize gen_config defaults.
