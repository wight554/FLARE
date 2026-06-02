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

- [ ] 4.1 `GET:RELAY_NEUTRAL_FRAC`; `SET:RELAY_NEUTRAL_FRAC:1.00`; infill soak.
- [ ] 4.2 `flare_sync_check.py --mode stability`: peak `< 1.0` cycles/s,
  endstop `< 30 %`, TENSION ≈ 0 (no starvation). A/B 0.95–1.05 if needed.
- [ ] 4.3 If the on-hw optimum differs from 1.00, update the default to match
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
- [x] 6.3 `scripts/gen_config.py` + `config.ini.example` + `TUNING.md`: with the
  ramp able to settle, drop the documented type-D default `relay_neutral_frac`
  `1.10 → 1.00` (net fill ≈ 0; switches as guardrails, no deliberate lean).
  Keep the value `SET:`/`GET:`-tunable for A/B.
  - 2026-06-02: updated generator default, config comments, operator docs,
    OpenSpec delta/proposal/design, and regenerated ignored `tune.h`.
- [x] 6.4 Build: `gen_config.py` regenerates `tune.h` clean; `ninja -C
  build_local` passes; analyzer/gen_config test suites green.
  - 2026-06-02: `python3 scripts/gen_config.py` passed.
  - 2026-06-02: `openspec validate relay-neutral-frac-detune --strict` passed.
  - 2026-06-02: `python3 -m py_compile scripts/*.py` passed.
  - 2026-06-02: `ninja -C build_local` passed.
  - 2026-06-02: `python3 scripts/test_flare_sync_check.py` passed (33 tests).
  - 2026-06-02: `python3 scripts/test_gen_config.py` passed.
  - 2026-06-02: `python3 scripts/test_flare_analyze.py` passed.
- [ ] 6.5 HW: 10 mm/s soak + `flare_sync_check.py --mode stability`. Expect the
  50 Hz `MM` chatter gone (feed rests on target), peak `< 1.0` cycles/s, endstop
  `< 30 %`, TENSION ≈ 0. A/B `frac` 1.00 ± 0.05 if it drifts.
- [ ] 6.6 If 6.5 passes, evaluate whether the §5 neutral-floor patches are now
  inert and revert them to keep the relay law minimal (record decision in
  `design.md`).

## 7. Fork B — convergent crossing-trim (REQUIRED; the actual fix) — BUILD THIS

Decision (HW follow-ups 5-8, see `design.md`): open-loop is exhausted. Baseline
pins feed; lowering it gives longer stable holds but the dead-reckoning virtual
BP always diverges from physical (no mid-band sensor) and eventually snaps — at a
**fixed click rate**, because nothing learns from the touch. The COMPRESSION
switch *is* the only mid-band truth signal; B turns each touch into a learning
event so the click rate decays to sparse.

**Acceptance:** compression touches start, then get *sparser* over a soak and
plateau low (long quiet NEUTRAL dwell between them); MM steady (no 50 Hz chatter,
no StealthChop-threshold crossing → no wroom); TENSION ≈ 0. **FAIL** = fixed-rate
clicking (no decay) or starvation. "Zero compression / perfect mid-band" is a
**non-goal** — impossible on 2-switch dead-reckoning; rare self-correcting touch
is the target.

**Prereq — §9 first (coupled):** the baseline-derived NEUTRAL floors
(`sync_neutral_anti_tension_floor_sps` = baseline·0.70, and the `relay_base` cap)
pin feed at ~baseline and will **clamp B's downward trim right back up**, exactly
as they defeat `frac`. B cannot lower feed to demand until §9 makes those floors
demand-scaled (or tension-side-only). Do §9, then §7.

- [ ] 7.1 `firmware/src/sync.c`: add a learned, persisted-optional type-D
  NEUTRAL feed trim, e.g. `static float g_relay_neutral_trim_sps = 0.0f;`.
  Applied only in the `BUF_SENSOR_TYPE == 0` NEUTRAL relay path:
  `neutral = clamp(EST*RELAY_NEUTRAL_FRAC + g_relay_neutral_trim_sps,
  SYNC_MIN_SPS, relay_base)` (in/after `relay_control_law`, `sync.c:1812`).
- [ ] 7.2 `firmware/src/sync.c`: update the trim at each type-D state crossing
  (the transition handler near `sync.c:1027`, where `old`/`new_state` are known):
  - `new_state == BUF_COMPRESSION` → overfed → `trim -= SYNC_RELAY_TRIM_STEP_SPS`.
  - `new_state == BUF_TENSION` → starved → `trim += SYNC_RELAY_TRIM_STEP_SPS`.
  - `new_state == BUF_NEUTRAL` → no change (v1).
  Clamp `trim` to `±SYNC_RELAY_TRIM_CLAMP_SPS` (anti-windup). Step size sets the
  residual steady-state click rate — small step = slower converge, rarer plateau
  clicks. Scope strictly `BUF_SENSOR_TYPE == 0`.
- [ ] 7.3 `firmware/src/sync.c`: remove the ad-hoc, non-convergent type-D EST
  drags (`2142`, `2168`, `2189`) — the trim replaces them. **Leave type-P's EST
  feedforward (`psf_control_law:1840`) untouched**; gate the removal to
  `BUF_SENSOR_TYPE == 0` so analog type-P is byte-identical.
- [ ] 7.4 `scripts/gen_config.py` + `config.ini.example` + `TUNING.md`: add
  `SYNC_RELAY_TRIM_STEP_SPS` and `SYNC_RELAY_TRIM_CLAMP_SPS` as `SET:`/`GET:`
  tunables (mirror the `relay_neutral_frac` plumbing in `protocol.c`). Document
  the convergence intuition. No `SETTINGS_VERSION` bump unless the learned trim
  is persisted (a new stored field) — if persisted, bump and note it.
- [ ] 7.5 Optional v2 (only if v1 converges too slowly / clicks too long):
  weight the step by dwell time — a *short* NEUTRAL→COMPRESSION dwell = large
  overfeed = bigger step; long dwell = small step. Use `g_buf.entered_ms`.
- [ ] 7.6 Build + tests green; OpenSpec strict validation.
- [ ] 7.7 HW: 10 mm/s soak. Confirm acceptance above — touch rate decays to
  sparse, feed steady (no wroom), TENSION ≈ 0. Then a feature-speed change
  (10→25 mm/s) should re-converge within a few crossings. Restore
  `BASELINE_RATE:2400` first.

## 8. Docs + spec

- [x] 8.1 `BEHAVIOR.md`: document the type-D feed model after A — feed tracks
  demand (no overfeed lean), the ramp settles on target (no overshoot chatter),
  switches act as guardrails; and after B (if built) the neutral feed is
  dwell-time-adaptive. Note type-P is unaffected (`BUF_SENSOR_TYPE == 0` scope).
  - 2026-06-02: documented demand-match default and no-overshoot type-D ramp;
    B remains gated and not documented as built.
- [x] 8.2 Update `proposal.md` "What Changes" / scope to reflect that the fix is
  ramp-overshoot + EST-decay + `frac → 1.00` (A), with B as a gated follow-up —
  superseding the original "default-only detune" framing.
  - 2026-06-02: proposal and OpenSpec tuning delta now frame Fork A as
    no-overshoot ramp + EST true-stop fix + `relay_neutral_frac` `1.00`.

## 9. Fork C — demand-scale the baseline anti-tension floor (NEW ROOT, do before §7)

HW follow-up 5 (see `design.md`): a `frac` sweep proved `relay_neutral_frac` is
**non-functional** — frac 1.10/1.00/0.50 all produce `MM ≈ 1680`. The pin is
`sync_neutral_anti_tension_floor_sps` = `BASELINE_RATE(2400) × 0.70 = 1680`, a
baseline-derived NEUTRAL refill floor that fires whenever the buffer is at/below
the compression-biased reserve target. At 10 mm/s (demand ~300-700) this floor
is 2.5-5× demand ⇒ mandatory overfeed ⇒ COMPRESSION bang-bang, at any frac.
This supersedes the "frac is the lever" framing; §6.3 frac change is inert until
this floor scales with demand.

- [ ] 9.1 Diagnostic confirm (no code): at the rig, lower `BASELINE_RATE` (e.g.
  `SET:BASELINE_RATE:800`) and re-poll at 10 mm/s. Expect `MM` floor to drop to
  ~`new_baseline × 0.70`. Confirms the anti-tension floor is the pin. Restore
  baseline after.
- [ ] 9.2 `firmware/src/sync.c` `sync_neutral_anti_tension_floor_sps`: make the
  floor demand-scaled, not a fixed fraction of baseline — e.g.
  `min(baseline·0.70, extruder_est_sps·k)` or EST-derived — so at low demand the
  floor falls to ~demand instead of pinning to baseline·0.70. Preserve tension
  protection when demand is genuinely high. Scope to `BUF_SENSOR_TYPE == 0`;
  verify type-P (analog) path unaffected.
- [ ] 9.3 `firmware/src/sync.c`: tighten the fire condition so the floor engages
  only when genuinely tension-side (`error_norm < −deadband_norm`), not the
  broad `<= +deadband` that fires across the compression-biased target band.
- [ ] 9.4 Re-evaluate the `+2.5` compression-biased reserve target
  (`buf_target_reserve_mm`) for type-D: a near-zero target lets the buffer rest
  mid-band instead of riding the compression edge where the floor keeps firing.
- [ ] 9.5 Audit the rest of the baseline-derived floor stack for the same
  low-demand defect: `baseline_control_floor_sps` use in `relay_control_law`
  (`relay_base` clamp), the model-stall EST bleed (`sync.c:2168`, bleeds EST up
  to `baseline_floor`), the §5 relay floor. Demand-scale or gate each.
- [ ] 9.6 Build + tests green; OpenSpec strict validation.
- [ ] 9.7 HW: 10 mm/s soak. Expect `MM` now tracks real demand (not 1680), frac
  becomes functional again, COMPRESSION cycle gone or greatly reduced. THEN
  re-run the §6.3 frac A/B and the §7.1 Fork-B gate with frac actually live.

> Note: §6.1 (no-overshoot ramp) and §6.2 (EST true-stop) stay — both correct —
> but are downstream of this floor and could not act while the floor pinned feed.
> §7 (Fork B / adaptive frac) is gated behind §9: frac must be functional first.
