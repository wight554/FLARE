# Tasks — relay-neutral-frac-detune

## 1. Lower the default

- [x] 1.1 `scripts/gen_config.py` DEFAULTS: `relay_neutral_frac` `1.25 → 1.10`
  with a comment (stale gate-harden palliative; 25 % overfeed caused
  compression bang-bang; 1.10 = gentle lean).
- [x] 1.2 `config.ini.example`: update the commented `relay_neutral_frac` hint
  `1.25 → 1.10`.
- [x] 1.3 `TUNING.md`: update the `relay_neutral_frac` default in the type-D
  block `1.25 → 1.10` and note why (overfeed → quiet limit cycle).

## 2. Un-mask + correct the detector (`scripts/flare_sync_check.py`)

- [x] 2.1 `analyze_stability`: default `cycle_threshold_hz` `2.0 → 1.0`; rewrite
  docstring — type-D ringing is the relay limit cycle; reduce it by lowering
  `relay_neutral_frac` (less overfeed), not `sync_kp_rate` (inert for type-D);
  kp tuning applies to analog type-P only.
- [x] 2.2 `--stability-cycle-hz` CLI default `2.0 → 1.0` + corrected help.
- [x] 2.3 `--tune-drift-pct` CLI default `38.0 → 30.0` + corrected help.
- [x] 2.4 `analyze_drift`: rewrite docstring + FAIL message — for type-D,
  COMPRESSION-heavy drift = `relay_neutral_frac` too high, TENSION-heavy =
  catch-up too weak / `neutral_frac` too low; "Raise SYNC_KP_RATE" applies to
  analog type-P only.

## 3. Build + test

- [x] 3.1 `gen_config.py` regenerates `tune.h` with `CONF_RELAY_NEUTRAL_FRAC
  1.10f` and no errors.
- [x] 3.2 Firmware builds clean.
- [x] 3.3 `scripts/test_flare_sync_check.py` + analyzer/gen_config suites green;
  `py_compile` clean.

## 4. On-hardware validation (operator) — OPEN, blocked on rig

- [ ] 4.1 `GET:RELAY_NEUTRAL_FRAC`; `SET:RELAY_NEUTRAL_FRAC:1.10`; infill soak.
- [ ] 4.2 `flare_sync_check.py --mode stability`: peak `< 1.0` cycles/s,
  endstop `< 30 %`, TENSION ≈ 0 (no starvation). A/B 1.05–1.15 if needed.
- [ ] 4.3 If the on-hw optimum differs from 1.10, update the default to match
  and re-confirm.

## 5. Hardware-discovered relay floor fix

- [x] 5.1 `firmware/src/sync.c`: preserve the raw type-D `BUF_NEUTRAL` relay
  target as a floor after shared reserve scaling and compression-recovery
  shaping, so `RELAY_NEUTRAL_FRAC` cannot be defeated by downstream trims.
- [x] 5.2 `BEHAVIOR.md` and `TUNING.md`: document that type-D neutral relay
  output remains the minimum applied neutral feed after shared shapers.
- [x] 5.3 Validate build and OpenSpec strict validation.
  - 2026-06-02: `openspec validate relay-neutral-frac-detune --strict` passed.
  - 2026-06-02: `ninja -C build_local` passed.
- [ ] 5.4 HW: retest the reproduced 1.30 case; expected result is no post-tension
  `MM` collapse below `EST * RELAY_NEUTRAL_FRAC` while in `BUF_NEUTRAL`.
- [x] 5.5 `firmware/src/sync.c`: narrow the relay neutral floor to the
  tension-side half of `BUF_NEUTRAL` so compression-side trim can brake instead
  of repeatedly bang-banging `BUF_COMPRESSION`.
  - 2026-06-02: `openspec validate relay-neutral-frac-detune --strict` passed.
  - 2026-06-02: `ninja -C build_local` passed.

## 6. Fork A — stabilize the type-D feed (do first; type-P safe)

Root cause found 2026-06-02: the type-D ramp overshoots its target every tick
(50 Hz `MM:720↔1080` chatter), which the neutral-floor patches (§5) never
touched. See `design.md` → "Hardware follow-up 3". Fork A removes the chatter
and the overfeed lean so the buffer can rest in mid-band; the §5 floor patches
become redundant once feed can settle and may be reverted if A makes them inert.

- [x] 6.1 `firmware/src/sync.c`: port the type-P no-overshoot clamp
  (`2346-2354`) to the type-D ramp (`2374`) — when stepping toward `target_sps`,
  clamp so `sync_current_sps` lands on the target instead of overshooting it
  (`±360 mm/min`/tick straddle). Scoped to `BUF_SENSOR_TYPE == 0`; leave the
  type-P distance-EMA path untouched.
  - 2026-06-02: type-D ramp now clamps each slew step to `target_sps`; `ninja -C
    build_local` passed.
- [x] 6.2 `firmware/src/sync.c`: fix the EST-decay drag — gate the neutral/
  tension/compression EST nudges (`2142`, `2168`, `2189`) to
  `BUF_SENSOR_TYPE == 0` and stop dragging `extruder_est_sps` below true demand
  during COMPRESSION true-stop, so the relay target and integrator demand term
  do not rot (`EST 1200 → 277` under constant feed in the rig log).
  - 2026-06-02: type-D bootstrap nudges are gated to `BUF_SENSOR_TYPE == 0`;
    pinned-COMPRESSION true-stop no longer drags `extruder_est_sps` toward
    `sync_current_sps`; `ninja -C build_local` passed.
- [ ] 6.3 `scripts/gen_config.py` + `config.ini.example` + `TUNING.md`: with the
  ramp able to settle, drop the documented type-D default `relay_neutral_frac`
  `1.10 → 1.00` (net fill ≈ 0; switches as guardrails, no deliberate lean).
  Keep the value `SET:`/`GET:`-tunable for A/B.
- [ ] 6.4 Build: `gen_config.py` regenerates `tune.h` clean; `ninja -C
  build_local` passes; analyzer/gen_config test suites green.
- [ ] 6.5 HW: 10 mm/s soak + `flare_sync_check.py --mode stability`. Expect the
  50 Hz `MM` chatter gone (feed rests on target), peak `< 1.0` cycles/s, endstop
  `< 30 %`, TENSION ≈ 0. A/B `frac` 1.00 ± 0.05 if it drifts.
- [ ] 6.6 If 6.5 passes, evaluate whether the §5 neutral-floor patches are now
  inert and revert them to keep the relay law minimal (record decision in
  `design.md`).

## 7. Fork B — adaptive neutral feed (only if A's residual drift unacceptable)

Gate on the §6.5 outcome: only build B if, after A, the buffer still slow-drifts
to a rail (EST DC-bias walk, no mid-band truth). B closes a slow integral loop on
switch-crossing dwell time to auto-trim the neutral feed (adaptive `frac`),
replacing the ad-hoc EST nudges. See `design.md` → "Fork B".

- [ ] 7.1 Decision gate: confirm from 6.5 that residual drift (not chatter)
  remains and is out of spec. If A alone passes, **do not build B** — close the
  change. Record the call.
- [ ] 7.2 `firmware/src/sync.c`: at each type-D state crossing, derive the
  `(feed − demand)` error from dwell time / climb rate (use `g_buf.entered_ms`)
  and accumulate a bounded neutral-feed trim. Scoped to `BUF_SENSOR_TYPE == 0`.
- [ ] 7.3 `firmware/src/sync.c`: replace the ad-hoc EST nudges (`2142/2168/2189`)
  with the §7.2 crossing-event loop for type-D; preserve a type-P path (or leave
  type-P's EST feedforward untouched) — verify no change to `psf_control_law`
  feedforward.
- [ ] 7.4 Bound + anti-windup: clamp the trim, slow gain so it follows real
  per-feature demand changes (EST tracks those at crossings) without chasing
  noise. Optionally persist; no `SETTINGS_VERSION` bump unless a new stored field
  is added.
- [ ] 7.5 Build + tests green; OpenSpec strict validation.
- [ ] 7.6 HW: long infill soak; expect the buffer to park (long NEUTRAL dwell),
  no slow rail-walk, TENSION ≈ 0, and convergence within a few crossings after a
  speed change.

## 8. Docs + spec

- [ ] 8.1 `BEHAVIOR.md`: document the type-D feed model after A — feed tracks
  demand (no overfeed lean), the ramp settles on target (no overshoot chatter),
  switches act as guardrails; and after B (if built) the neutral feed is
  dwell-time-adaptive. Note type-P is unaffected (`BUF_SENSOR_TYPE == 0` scope).
- [ ] 8.2 Update `proposal.md` "What Changes" / scope to reflect that the fix is
  ramp-overshoot + EST-decay + `frac → 1.00` (A), with B as a gated follow-up —
  superseding the original "default-only detune" framing.
