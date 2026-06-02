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

## 4. On-hardware validation (operator) — OBSOLETE, superseded by §7.7/§9.7

> 2026-06-02: HW follow-ups 5-8 proved frac alone is inert (baseline pin) — a
> frac-only soak is meaningless. Superseded by the Fork B + §9 acceptance soak
> (§7.7, §9.7). Left for history; do not run.

- [~] 4.1 ~~`SET:RELAY_NEUTRAL_FRAC:1.00`; infill soak.~~ → §7.7/§9.7.
- [~] 4.2 ~~`flare_sync_check.py --mode stability` frac A/B.~~ → §7.7/§9.7.
- [~] 4.3 ~~If on-hw optimum differs from 1.00, update default.~~ → trim
  (`SYNC_RELAY_TRIM_STEP_SPS`) now does the converging, not frac.

## 5. Hardware-discovered relay floor fix

- [x] 5.1 `firmware/src/sync.c`: preserve the raw type-D `BUF_NEUTRAL` relay
  target as a floor after shared reserve scaling and compression-recovery
  shaping, so `RELAY_NEUTRAL_FRAC` cannot be defeated by downstream trims.
- [x] 5.2 `BEHAVIOR.md` and `TUNING.md`: document that type-D neutral relay
  output remains the minimum applied neutral feed after shared shapers.
- [x] 5.3 Validate build and OpenSpec strict validation.
  - 2026-06-02: `openspec validate relay-neutral-frac-detune --strict` passed.
  - 2026-06-02: `ninja -C build_local` passed.
- [~] 5.4 OBSOLETE (superseded by §9 + §7.7). ~~HW: retest the reproduced 1.30
  case; no post-tension `MM` collapse below `EST * RELAY_NEUTRAL_FRAC`.~~ The §5
  floor was tightened to tension-side under §9.3; its behavior is now covered by
  the §7.7/§9.7 acceptance soak.
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
- [x] 6.5 HW: 10 mm/s soak + `flare_sync_check.py --mode stability`. Expect the
  50 Hz `MM` chatter gone (feed rests on target), peak `< 1.0` cycles/s, endstop
  `< 30 %`, TENSION ≈ 0. A/B `frac` 1.00 ± 0.05 if it drifts.
  - 2026-06-02: Opus rig follow-up showed 6.5 did not pass: `relay_neutral_frac`
    1.10/1.00/0.50 all pinned around `MM≈1680`, proving the baseline-derived
    anti-tension floor defeated frac. Follow-up §9 added.
- [x] 6.6 If 6.5 passes, evaluate whether the §5 neutral-floor patches are now
  inert and revert them to keep the relay law minimal (record decision in
  `design.md`).
  - 2026-06-02: Not inert. §5 floor condition was tightened in §9; no revert.

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

- [x] 7.1 `firmware/src/sync.c`: add a learned, persisted-optional type-D
  NEUTRAL feed trim, e.g. `static float g_relay_neutral_trim_sps = 0.0f;`.
  Applied only in the `BUF_SENSOR_TYPE == 0` NEUTRAL relay path:
  `neutral = clamp(EST*RELAY_NEUTRAL_FRAC + g_relay_neutral_trim_sps,
  SYNC_MIN_SPS, relay_base)` (in/after `relay_control_law`, `sync.c:1812`).
  - 2026-06-02: Added volatile `g_relay_neutral_trim_sps`, applied only in the
    type-D NEUTRAL relay path and reset when sync disables. Not persisted; no
    `SETTINGS_VERSION` bump.
- [x] 7.2 `firmware/src/sync.c`: update the trim at each type-D state crossing
  (the transition handler near `sync.c:1027`, where `old`/`new_state` are known):
  - `new_state == BUF_COMPRESSION` → overfed → `trim -= SYNC_RELAY_TRIM_STEP_SPS`.
  - `new_state == BUF_TENSION` → starved → `trim += SYNC_RELAY_TRIM_STEP_SPS`.
  - `new_state == BUF_NEUTRAL` → no change (v1).
  Clamp `trim` to `±SYNC_RELAY_TRIM_CLAMP_SPS` (anti-windup). Step size sets the
  residual steady-state click rate — small step = slower converge, rarer plateau
  clicks. Scope strictly `BUF_SENSOR_TYPE == 0`.
  - 2026-06-02: `BUF_NEUTRAL -> BUF_COMPRESSION` subtracts the step;
    `BUF_NEUTRAL -> BUF_TENSION` adds it; trim clamps to configured bounds.
- [x] 7.3 `firmware/src/sync.c`: remove the ad-hoc, non-convergent type-D EST
  drags (`2142`, `2168`, `2189`) — the trim replaces them. **Leave type-P's EST
  feedforward (`psf_control_law:1840`) untouched**; gate the removal to
  `BUF_SENSOR_TYPE == 0` so analog type-P is byte-identical.
  - 2026-06-02: Removed the type-D pinned-wall/model-stall EST nudge block;
    type-P estimator and `psf_control_law` were not changed.
- [x] 7.4 `scripts/gen_config.py` + `config.ini.example` + `TUNING.md`: add
  `SYNC_RELAY_TRIM_STEP_SPS` and `SYNC_RELAY_TRIM_CLAMP_SPS` as `SET:`/`GET:`
  tunables (mirror the `relay_neutral_frac` plumbing in `protocol.c`). Document
  the convergence intuition. No `SETTINGS_VERSION` bump unless the learned trim
  is persisted (a new stored field) — if persisted, bump and note it.
  - 2026-06-02: Added defaults/config comments, runtime globals, `SET:`/`GET:`,
    `flare_cmd.py --dump`, `MANUAL.md`, `TUNING.md`, `BEHAVIOR.md`, and OpenSpec
    requirements. Learned trim is volatile, so no settings field/version bump.
- [ ] 7.5 Optional v2 (only if v1 converges too slowly / clicks too long):
  weight the step by dwell time — a *short* NEUTRAL→COMPRESSION dwell = large
  overfeed = bigger step; long dwell = small step. Use `g_buf.entered_ms`.
- [x] 7.6 Build + tests green; OpenSpec strict validation.
  - 2026-06-02: `python3 scripts/gen_config.py` passed.
  - 2026-06-02: `ninja -C build_local` passed.
  - 2026-06-02: `openspec validate relay-neutral-frac-detune --strict` passed.
  - 2026-06-02: `python3 -m py_compile scripts/*.py` passed.
  - 2026-06-02: `python3 scripts/test_flare_sync_check.py` passed (33 tests).
  - 2026-06-02: `python3 scripts/test_gen_config.py` passed.
  - 2026-06-02: `python3 scripts/test_flare_analyze.py` passed.
  - 2026-06-02: Code review (explore-mode audit, Opus) — §7 + §9 verified against
    spec, all correct:
    - §7.1 trim `g_relay_neutral_trim_sps` applied `EST*frac + trim` clamped
      `[SYNC_MIN, relay_base]` (`sync.c:120, 1853`).
    - §7.2 crossing update gated `old==NEUTRAL && new∈{TENSION,COMPRESSION}`
      (`sync.c:1062-1070`) — NEUTRAL entry does NOT pump trim; COMP −step / TENS
      +step; clamped ±CLAMP.
    - §7.3 nudges `2142/2168/2189` removed; type-P EST is self-sufficient via its
      own continuous estimator (`sync.c:1822`), so wholesale removal does not
      starve type-P feedforward (psf_control_law untouched).
    - §9.2 anti-tension floor now `min(baseline·0.70, EST·1.05, neutral_target)`
      (`sync.c:1187-1195`); §9.3 fires only `error_norm < −deadband`
      (`1185`/`2177`); §9.4 type-D `buf_target_reserve_mm → 0.0` (`350-352`);
      §9.5 relay_base is upper cap, does not block downward trim. **The baseline
      feed-pin is eliminated; frac + trim are live.**
    - `ninja -C build_local` clean.
    - Caveats (non-blocking, verify on HW): (a) trim resets to 0 on sync
      (re)start (`sync.c:1595`) — not persisted → brief click burst at each feed
      start until re-converge; (b) `CONF_SYNC_RELAY_TRIM_CLAMP_SPS=12000`
      (~1760 mm/min) is wide → mild converge overshoot risk, tune down if needed
      (step 300 sps ≈ 44 mm/min/crossing is sane); (c) run one type-P sanity
      soak before shipping since the nudge removal was not `BUF_SENSOR_TYPE`-gated
      (low risk — type-P EST path independent).
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
  - 2026-06-02: updated for built crossing trim: switch touches adjust volatile
    type-D neutral feed; type-P remains out of scope.
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

- [x] 9.1 Diagnostic confirm (no code): at the rig, lower `BASELINE_RATE` (e.g.
  `SET:BASELINE_RATE:800`) and re-poll at 10 mm/s. Expect `MM` floor to drop to
  ~`new_baseline × 0.70`. Confirms the anti-tension floor is the pin. Restore
  baseline after.
  - 2026-06-02: Opus rig baseline scan 2400→800→600 confirmed feed tracked the
    baseline-derived floor; user explicitly reported notes from that run.
- [x] 9.2 `firmware/src/sync.c` `sync_neutral_anti_tension_floor_sps`: make the
  floor demand-scaled, not a fixed fraction of baseline — e.g.
  `min(baseline·0.70, extruder_est_sps·k)` or EST-derived — so at low demand the
  floor falls to ~demand instead of pinning to baseline·0.70. Preserve tension
  protection when demand is genuinely high. Scope to `BUF_SENSOR_TYPE == 0`;
  verify type-P (analog) path unaffected.
  - 2026-06-02: Type-D anti-tension floor is `min(baseline·0.70, EST·1.05,
    neutral_target)`; type-P keeps the old baseline floor path.
- [x] 9.3 `firmware/src/sync.c`: tighten the fire condition so the floor engages
  only when genuinely tension-side (`error_norm < −deadband_norm`), not the
  broad `<= +deadband` that fires across the compression-biased target band.
  - 2026-06-02: Type-D floor now fires only at `error_norm < -deadband_norm`;
    §5 relay floor uses the same stricter tension-side condition.
- [x] 9.4 Re-evaluate the `+2.5` compression-biased reserve target
  (`buf_target_reserve_mm`) for type-D: a near-zero target lets the buffer rest
  mid-band instead of riding the compression edge where the floor keeps firing.
  - 2026-06-02: Type-D reserve target now returns 0.0 mm; type-P target/bias path
    remains unchanged.
- [x] 9.5 Audit the rest of the baseline-derived floor stack for the same
  low-demand defect: `baseline_control_floor_sps` use in `relay_control_law`
  (`relay_base` clamp), the model-stall EST bleed (`sync.c:2168`, bleeds EST up
  to `baseline_floor`), the §5 relay floor. Demand-scale or gate each.
  - 2026-06-02: `relay_base` remains only a max cap; §5 relay floor now
    tension-side-only; model-stall EST bleed remains for §7 removal.
- [x] 9.6 Build + tests green; OpenSpec strict validation.
  - 2026-06-02: `ninja -C build_local` passed.
  - 2026-06-02: `openspec validate relay-neutral-frac-detune --strict` passed.
  - 2026-06-02: `python3 -m py_compile scripts/*.py` passed.
  - 2026-06-02: `python3 scripts/test_flare_sync_check.py` passed (33 tests).
  - 2026-06-02: `python3 scripts/test_gen_config.py` passed.
  - 2026-06-02: `python3 scripts/test_flare_analyze.py` passed.
- [ ] 9.7 HW: 10 mm/s soak. Expect `MM` now tracks real demand (not 1680), frac
  becomes functional again, COMPRESSION cycle gone or greatly reduced. THEN
  re-run the §6.3 frac A/B and the §7.1 Fork-B gate with frac actually live.

> Note: §6.1 (no-overshoot ramp) and §6.2 (EST true-stop) stay — both correct —
> but are downstream of this floor and could not act while the floor pinned feed.
> §7 (Fork B / adaptive frac) is gated behind §9: frac must be functional first.

## 10. Fork D — correct type-D EST from the COMPRESSION-drain (ROOT FIX) — BUILD THIS

HW §7.7 (300-step) and the STEP-120 retest proved the crossing-trim alone cannot
win: it is a pure event-integrator with **no leak, asymmetric crossing rates
(fast down / sparse up), and it persists across sessions**, so it overshoots
deep negative and recovers glacially while the buffer starves. Every failure
this session traces to the same root: **`EST` is latched ~1200-1414 vs real
demand ~600 (≈2.3× high)**, forcing the trim to swing ±800 to cancel it.

Fix the root: the buffer motion measures real demand directly. In particular the
**COMPRESSION true-stop** is a clean anchor — feed is 0, so the buffer drains at
*exactly* real demand with no feed term to subtract. Sample it and correct `EST`.
With `EST ≈ demand`, `feed = EST · 1.00` matches demand (the 5 mm/s "perfect
midline" regime), the buffer holds, touches go sparse, and the trim collapses to
a small residual. The operator already accepts the touch as the sensor — this
makes each touch *calibrate*, not just bound.

- [x] 10.1 `firmware/src/sync.c` crossing handler (~`sync.c:1010-1033`): on
  `COMPRESSION → NEUTRAL`, the dwell just spent at the true-stop had commanded
  feed ≈ 0 (`relay_control_law` COMPRESSION = 0). Compute the drain velocity from
  the compression dwell (`travel / dwell`, using `g_buf.entered_ms` and the known
  buffer travel) and treat it as a **direct demand sample** `demand_sps ≈
  drain_sps` (no `mmu_avg` term — feed was 0). Blend into `extruder_est_sps`
  (alpha by dwell length / confidence). This is the ground-truth anchor the
  current estimator misses because it samples the *fill* (overfeed) motion at
  compression *entry*, not the *drain* at exit.
  - 2026-06-02: `COMPRESSION -> NEUTRAL` now uses a threshold-distance drain
    sample with feed forced to 0 and blends it into `extruder_est_sps` using
    dwell-scaled alpha.
- [x] 10.2 `firmware/src/sync.c`: verify/repair the existing crossing estimator
  (`1010`) for the other transitions — it must not re-bias `EST` back up from the
  overfeed fill motion. Prefer the feed-known transitions: COMPRESSION exit
  (feed 0 → demand = drain) and steady NEUTRAL traverse (demand = feed − net
  fill rate) over the TENSION→COMPRESSION direct-set (`1027-1028`), which samples
  a transient catch-up. Keep it `BUF_SENSOR_TYPE == 0`; type-P estimator
  (`sync.c:1822`) untouched.
  - 2026-06-02: Type-D crossing math now uses `extruder = mmu - arm_velocity`,
    matching the type-P sign convention, and the direct
    `TENSION -> COMPRESSION` estimator overwrite is removed. Type-P estimator
    code was not changed.
- [x] 10.3 `firmware/src/sync.c`: demote the §7 trim to a **small residual**
  corrector now that `EST` carries the demand: lower `CONF_SYNC_RELAY_TRIM_CLAMP_SPS`
  (e.g. → ~2000) and add a **leak toward 0** while parked in NEUTRAL, so it
  self-centers as `EST` converges and can no longer ratchet into a stuck corner
  or persist a large bias across sessions. Optionally zero the trim on a large
  `EST` correction.
  - 2026-06-02: Default trim clamp lowered to `2000`; trim leaks toward zero
    during type-D NEUTRAL dwell and shrinks on large compression-drain `EST`
    corrections. Docs/config/OpenSpec updated.
- [x] 10.4 Build + tests green; OpenSpec strict validation.
  - 2026-06-02: `python3 scripts/gen_config.py` passed.
  - 2026-06-02: `ninja -C build_local` passed.
  - 2026-06-02: `openspec validate relay-neutral-frac-detune --strict` passed.
  - 2026-06-02: `python3 -m py_compile scripts/*.py` passed.
  - 2026-06-02: `python3 scripts/test_flare_sync_check.py` passed (33 tests).
  - 2026-06-02: `python3 scripts/test_gen_config.py` passed.
  - 2026-06-02: `python3 scripts/test_flare_analyze.py` passed.
- [ ] 10.5 HW: 10 mm/s soak, `BASELINE_RATE:2400`, `frac 1.00`. Expect: `EST`
  converges toward the real ~600 (not stuck at 1200/1414); `MM` settles near
  demand without diving to tension or slamming compression; touches sparse and
  self-correcting; **TENSION ≈ 0, no glacial starved recovery**. Then a speed
  step (10→25 mm/s) should re-converge `EST` within a few touches.
- [ ] 10.6 Persistence check: confirm a fresh sync arm starts sane (trim 0, `EST`
  re-learned from the first touches) — no inherited bias from a prior session.

## 10b. Fork D correction — measure demand from the NEUTRAL fill, not the COMPRESSION drain

HW (§10.5, see `design.md` follow-up 10) confirmed §10.1 broke the EST latch —
`EST` now *moves* — but it over-estimates ~2× and walks **up** to ~1300 instead
of down to real demand (~600), so the buffer keeps overfeeding and ends pinned
COMPRESSION. Root cause: the COMPRESSION-drain window is **not** feed≈0. The
true-stop ramps down over ~100-200 ms (rig MM showed `1320→960→585→606→…→0.1`
*while* `BUF:COMPRESSION`), and the dwell is short, so the sample picks up
residual ramp-down feed **plus** an inflated `travel/dwell` arm velocity ≈
`400 + 900 = ~1300`. The "feed=0 clean anchor" assumption is violated.

Switch the demand measurement to the **NEUTRAL fill at known steady feed** — a
long, feed-known, low-noise window:

- [x] 10b.1 `firmware/src/sync.c`: on `NEUTRAL → COMPRESSION` (type-D), the
  buffer just filled from its NEUTRAL entry to the COMPRESSION wall at a **known,
  steady** commanded feed `F` (the relay NEUTRAL output, not ramping). Compute
  the fill rate from the NEUTRAL dwell: `fill_sps = neutral_travel / neutral_dwell`
  (use `g_buf.entered_ms` and the threshold travel). Then
  `demand_sps = F_avg − fill_sps`, where `F_avg` is the average *commanded* feed
  over that NEUTRAL dwell (track a running mean of `sync_current_sps` while in
  NEUTRAL). Blend into `extruder_est_sps` (dwell-scaled alpha). Feed is steady and
  known here, so unlike the drain there is no ramp-down contamination.
  - 2026-06-02: Added a dedicated type-D NEUTRAL feed accumulator over actual
    applied `sync_current_sps`, reset on `BUF_NEUTRAL` entry. `NEUTRAL ->
    COMPRESSION` now estimates demand as averaged feed minus measured fill rate
    and blends by dwell length.
- [x] 10b.2 `firmware/src/sync.c`: remove / demote the §10.1 COMPRESSION-drain
  sample — it is too short and feed-contaminated to estimate demand. Keep the
  COMPRESSION true-stop itself (feed 0) unchanged; only stop *measuring demand*
  from it. If a drain sample is retained at all, gate it to the post-ramp-down
  portion (`sync_current_sps ≈ 0`) with a minimum dwell, and subtract actual
  `lane_motion` — but 10b.1 is the primary estimator.
  - 2026-06-02: Drain samples are no longer primary; they are accepted only after
    minimum compression dwell and near-zero averaged feed, with the actual feed
    term included.
- [x] 10b.3 Sanity-bound the estimate: clamp `demand_sps` to `[SYNC_MIN,
  baseline]` and ignore samples from degenerate windows (dwell below a floor, or
  `fill_sps` ≥ `F_avg` which would imply negative demand). Prevents a single
  noisy crossing from yanking `EST`.
  - 2026-06-02: Rejects no-sample, short-dwell, invalid step-size,
    non-positive-feed, and `fill_sps >= F_avg` windows. Accepted samples clamp
    to `[SYNC_MIN_SPS, baseline_control_floor_sps()]`.
- [x] 10b.4 Build + tests green; OpenSpec strict validation.
  - 2026-06-02: `ninja -C build_local` passed.
  - 2026-06-02: `openspec validate relay-neutral-frac-detune --strict` passed.
  - 2026-06-02: `python3 -m py_compile scripts/*.py` passed.
  - 2026-06-02: `python3 scripts/test_flare_sync_check.py` passed (33 tests).
  - 2026-06-02: `python3 scripts/test_gen_config.py` passed.
  - 2026-06-02: `python3 scripts/test_flare_analyze.py` passed.
- [ ] 10b.5 HW: 10 mm/s soak, `BASELINE_RATE:2400`, `frac 1.00`, fresh sync arm.
  Expect `EST` to converge **down** toward real demand (~600-700, not ~1300),
  `MM` settle near demand, touches sparse + self-correcting, TENSION ≈ 0, no
  COMPRESSION-stuck tail. Then a 10→25 mm/s step should re-converge `EST` up
  within a few fills.
  - 2026-06-02: Opus rig verdict: motion calmer, long mid-band sweeps, smoother
    compression-side braking, and touch rate down. Not accepted: `EST` stalled at
    `901` (about 200-250 high), virtual `BP` still diverged from physical, and
    the run ended with impossible `BP:-2.0 -> BP:5.0 COMPRESSION` snaps / stuck
    full tail. Follow-up §10c added.

## 10c. Fork D refinement — pre-taper feed mean + relaxed fill gate

HW §10b.5 showed the fill estimator works but stops short: compression-side
taper modulates `MM` during the measured fill window, and slow/near-converged
fills can be rejected as degenerate. Keep `F_avg` as actual applied feed, but
prefer the early flat part of the dwell and relax the near-zero fill gate.

- [x] 10c.1 `firmware/src/sync.c`: track a second type-D NEUTRAL feed mean for
  the pre-taper region (`pos_norm <= target_norm + deadband_norm`) while also
  keeping the whole-dwell mean. Prefer the pre-taper mean for
  `NEUTRAL -> COMPRESSION` samples when enough samples exist; fall back to the
  whole-dwell mean otherwise.
  - 2026-06-02: Added pre-taper feed accumulator over actual `sync_current_sps`,
    sampled before compression-side taper begins.
- [x] 10c.2 `firmware/src/sync.c`: relax the §10b.3 reject gate so converged
  slow fills remain usable. Reject truly bad samples (short dwell, no feed,
  invalid step size, non-positive feed, or fill rate far above feed), but allow
  near-zero / slightly overrun fills to clamp to `SYNC_MIN_SPS`.
  - 2026-06-02: `fill_sps <= F_avg * 1.25` is accepted and bounded; far-overrun
    windows still reject.
- [x] 10c.3 Docs/spec/OpenSpec sync; build + tests green.
  - 2026-06-02: `BEHAVIOR.md`, `TUNING.md`, OpenSpec design, spec, and tasks
    updated.
  - 2026-06-02: `ninja -C build_local` passed.
  - 2026-06-02: `openspec validate relay-neutral-frac-detune --strict` passed.
  - 2026-06-02: `python3 -m py_compile scripts/*.py` passed.
  - 2026-06-02: `python3 scripts/test_flare_sync_check.py` passed (33 tests).
  - 2026-06-02: `python3 scripts/test_gen_config.py` passed.
  - 2026-06-02: `python3 scripts/test_flare_analyze.py` passed.
- [ ] 10c.4 HW: rerun 10 mm/s soak, `BASELINE_RATE:2400`, `frac 1.00`, fresh
  sync arm. Expect `EST` to continue below 901 toward real demand, no impossible
  virtual `BP` divergence snaps, and no stuck-full tail.
  - 2026-06-02: 20 s feed log after §10c: `EST` moved `1200 -> 1002 -> 895.9`,
    then froze. Later cycles show short feed-zero `BUF_COMPRESSION` dwells
    (`MM 0.1`) and long `BUF_NEUTRAL` sweeps around `MM 640-680`, but virtual
    `BP` still drifts to about `-2` before physical COMPRESSION snaps. Follow-up
    §10d added; do not accept yet.

## 10d. Fork D refinement — accept short feed-zero drain exits

After §10c, the fill estimator updates once but cannot keep correcting after
cycles that originate from `COMPRESSION -> NEUTRAL`: the next compression-side
fill has no known switch-to-switch travel. The COMPRESSION true-stop is now
clean (`MM 0.1`), but the drain fallback requires `300 ms` while the rig exits
COMPRESSION in about `200 ms`.

- [x] 10d.1 `firmware/src/sync.c`: lower the type-D COMPRESSION drain estimator
  dwell floor enough to accept short feed-zero exits while still rejecting
  instant bounce.
  - 2026-06-02: Set `SYNC_DRAIN_EST_MIN_DWELL_MS` from `300` to `150`; the
    existing near-zero feed gate remains in place.
- [x] 10d.2 Docs/spec/OpenSpec sync; build + tests green.
  - 2026-06-02: `BEHAVIOR.md`, `TUNING.md`, OpenSpec design, spec, and tasks
    updated.
  - 2026-06-02: `ninja -C build_local` passed.
  - 2026-06-02: `openspec validate relay-neutral-frac-detune --strict` passed.
  - 2026-06-02: `python3 -m py_compile scripts/*.py` passed.
  - 2026-06-02: `python3 scripts/test_flare_sync_check.py` passed (33 tests).
  - 2026-06-02: `python3 scripts/test_gen_config.py` passed.
  - 2026-06-02: `python3 scripts/test_flare_analyze.py` passed.
- [ ] 10d.3 HW: rerun 20 s / 10 mm/s feed. Expect `EST` to keep correcting
  below `895.9` toward the observed steady `MM 640-680`, virtual `BP` no longer
  drifting tension-side before physical COMPRESSION snaps, and COMPRESSION
  touches becoming sparse/self-correcting instead of a stuck-full tail.
  - 2026-06-02: Rerun not accepted: `EST` stayed flat at `896.6`; `MM` stayed
    calm near `650`, but virtual `BP` still snapped from about `-1.3/-1.6` to
    `5.0 COMPRESSION`. Follow-up §10e added.

## 10e. Fork D refinement — same-side fill uses applied-feed sample

After §10d, the plant shows the useful signal is the long same-side NEUTRAL dwell
itself: applied feed is calm near `640-660`, but the next
`NEUTRAL -> COMPRESSION` touch has no known switch-to-switch travel because the
prior NEUTRAL entry came from COMPRESSION. Use the pre-taper applied feed as an
upper-bound demand sample instead of skipping the touch.

- [x] 10e.1 `firmware/src/sync.c`: let type-D `NEUTRAL -> COMPRESSION`
  estimator handling run even when computed travel is near zero.
  - 2026-06-02: Estimator block now admits zero-travel neutral fill samples
    while continuing to ignore other zero-travel transitions.
- [x] 10e.2 `firmware/src/sync.c`: for known-travel fills, keep
  `demand = F_avg - fill_rate`; for same-side / no-known-travel fills, blend
  `F_avg` itself as an upper-bound demand sample.
  - 2026-06-02: Reuses the §10c pre-taper feed average when available, so late
    neutral dwell feed around real demand is preferred over compression-side
    braking.
- [x] 10e.3 Docs/spec/OpenSpec sync; build + tests green.
  - 2026-06-02: `BEHAVIOR.md`, `TUNING.md`, OpenSpec design, spec, and tasks
    updated.
  - 2026-06-02: `ninja -C build_local` passed.
  - 2026-06-02: `openspec validate relay-neutral-frac-detune --strict` passed.
  - 2026-06-02: `python3 -m py_compile scripts/*.py` passed.
  - 2026-06-02: `python3 scripts/test_flare_sync_check.py` passed (33 tests).
  - 2026-06-02: `python3 scripts/test_gen_config.py` passed.
  - 2026-06-02: `python3 scripts/test_flare_analyze.py` passed.
- [ ] 10e.4 HW: rerun 20 s / 10 mm/s feed. Expect the second
  `NEUTRAL -> COMPRESSION` touch to pull `EST` below `896` toward `640-660`,
  with virtual `BP` no longer drifting deeply tension-side before compression
  snaps.
  - 2026-06-02: Estimator accepted as directionally useful, but the real-print
    benchmark exposed a separate speed-step reserve failure: after slow phases,
    the centered Type-D target left too little headroom for 300→1500 mm/min
    jumps. Follow-up §10f added.

## 10f. Type-D reserve headroom for real-print speed steps

The real-print benchmark (300 mm/min outer walls, 1500 mm/min everything else)
showed that a centered Type-D neutral target leaves too little physical reserve:
after slow/compression-heavy phases lower `EST`, the next speed-up can pull the
buffer near TENSION before the reactive trim has time to help.

- [x] 10f.1 `firmware/src/sync.c`: restore a compression-side Type-D reserve
  target using existing `SYNC_RESERVE_PCT` instead of hard-centering Type-D at
  `0.0 mm`. Keep Type-P behavior unchanged.
- [x] 10f.2 Docs/spec/OpenSpec sync: document that Type-D now parks slightly
  compression-side for speed-step headroom, while switch touches remain the
  calibration truth.
  - 2026-06-02: Updated `BEHAVIOR.md`, `TUNING.md`, `design.md`, and the
    OpenSpec operator-tuning delta.
- [x] 10f.3 Build + tests green; OpenSpec strict validation.
  - 2026-06-02: `ninja -C build_local` passed.
  - 2026-06-02: `openspec validate relay-neutral-frac-detune --strict` passed.
  - 2026-06-02: `python3 -m py_compile scripts/*.py` passed.
  - 2026-06-02: `python3 scripts/test_flare_sync_check.py` passed (33 tests).
  - 2026-06-02: `python3 scripts/test_gen_config.py` passed.
  - 2026-06-02: `python3 scripts/test_flare_analyze.py` passed.
- [ ] 10f.4 HW: rerun the real-print benchmark. Expect no TENSION or near-
  TENSION skipping on 300→1500 mm/min speed-ups. Rare compression touches are
  acceptable if they self-correct and do not form a fast fixed-rate click cycle.

## 11. Fix SYNC_RELIEF_PAUSE deadlock

- [x] 11.1 `firmware/src/sync.c` `sync_tick()`: check and auto-start sync from `SYNC_RELIEF_PAUSE` if the debounced buffer state `s` is `BUF_TENSION`, or `BUF_NEUTRAL` while the feed task is active (`A->task == TASK_FEED`), resolving the negative-stabilization race deadlock.
  - 2026-06-02: Added proactive recovery block to `sync_tick()` that bypasses the early return and re-arms sync if the debounced state is ready and active print is verified.
- [x] 11.2 Build + tests green; OpenSpec strict validation.
  - 2026-06-02: `ninja -C build_local` and `bash scripts/validate_regression.sh` passed successfully.

