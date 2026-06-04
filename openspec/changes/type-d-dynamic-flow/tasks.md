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

- [x] 1.1 `firmware/src/sync.c` (`buf_update`, `neutral_drain_sample` /
  `→ BUF_TENSION` path ~`sync.c:1164-1178`): **Trigger 1 (velocity snap):**
  `v_norm = clamp(|arm_vel_mm_s| / SYNC_TENSION_FAST_MM_S, 0, 1)`;
  `est_sample = lerp(feed_avg*1.15, mmu_feed + drain_rate, v_norm)`;
  `alpha = lerp(EST_ALPHA_MAX, 1.0, v_norm)` (bypass the ALPHA_MAX clamp like the
  §20 attack); `EST = max(EST, blended)`.
  - 2026-06-03: Implemented velocity-scaled type-D TENSION recovery sample with
    direct alpha blend so fast crossings can reach alpha 1.0 while the
    zero-velocity endpoint remains `feed_avg * 1.15`.
  - 2026-06-03: Made the TENSION crossing blend raise-only so a buffer-limited
    velocity sample cannot lower `EST` and erase accumulated burst escalation.
- [x] 1.2 **Trigger 2 (burst escalation):** track `last_tension_ms` + `burst_n`.
  If a TENSION crossing is within `SYNC_TENSION_BURST_MS` of the prior one,
  `burst_n++` and add `SYNC_TENSION_ESC_STEP_SPS * SYNC_TENSION_ESC_RATIO^burst_n`
  to EST (clamp to a demand ceiling). Reset `burst_n` when a `BUF_NEUTRAL` dwell
  holds past `SYNC_TENSION_BURST_MS`. Geometric/aggressive is correct here
  (tension-recovery direction; overshoot → COMPRESSION is safe).
  - 2026-06-03: Added bounded geometric burst escalation, clamped to
    `GLOBAL_MAX_SPS`, reset on `sync_disable` and after held `BUF_NEUTRAL`
    dwell; `SYNC_TENSION_BURST_MS=0` disables the burst escalator.
  - 2026-06-03: Moved escalation out of the demand-sample path and onto the
    unconditional `BUF_NEUTRAL -> BUF_TENSION` crossing hook. Fast pinned
    re-touches do not satisfy the dwell/feed-average sample gates, so escalation
    must fire without `sample_valid`.
- [x] 1.3 New knobs (non-persisted, plumb like `SYNC_COMPRESSION_DRAIN_FRAC`):
  `SYNC_TENSION_FAST_MM_S` (≈2 mm/s), `SYNC_TENSION_BURST_MS` (≈300-500),
  `SYNC_TENSION_ESC_STEP_SPS`, `SYNC_TENSION_ESC_RATIO` (≈1.5-2). No
  `SETTINGS_VERSION` bump. Mind the 32-char param-name limit (§21).
  - 2026-06-03: Added `CONF_*` generation, runtime variables, load-reset-only
    settings defaults, protocol SET/GET handlers, live-write guard coverage,
    host dump entries, and config examples. `settings_t` unchanged.
- [x] 1.4 Slow single crossing (`v_norm≈0`, `burst_n=0`) MUST reproduce today's
  `feed_avg*1.15` exactly (no `7178c34` regression).
  - 2026-06-03: Slow endpoint uses `feed_avg_sps * 1.15f`; burst escalation only
    runs on repeated TENSION within the configured window.
- [x] 1.5 `BUF_SENSOR_TYPE == 0` only; type-P estimator untouched.
  - 2026-06-03: Changes are guarded by `BUF_SENSOR_TYPE == 0`; no
    `psf_control_law` or type-P estimator path edits.
- [x] 1.6 Build + tests green; OpenSpec strict.
  - 2026-06-03: Passed `ninja -C build_local`,
    `bash scripts/validate_regression.sh`, `python3 -m py_compile scripts/*.py`,
    `python3 scripts/test_*.py`, and
    `openspec validate type-d-dynamic-flow --strict`.

## 2. (Optional) velocity-scaled catchup ramp

- [ ] 2.1 Scale the `SYNC_TENSION_RAMP_DELAY` collapse by `v_norm` so a fast
  crossing reaches max feed a tick sooner. Ship only if §1 leaves the *first*
  touch clearing too slowly (catchup magnitude is already adequate).

## 3. Docs

- [x] 3.1 `TUNING.md` / `BEHAVIOR.md` / `MANUAL.md`: document the new tension-
  recovery knobs and that the fast-step TENSION touch is reduced to a single
  positioning touch (not eliminated — see non-goals). Note the floor stays demoted
  and reserve handles slow drift.
  - 2026-06-03: Updated operator docs for the four knobs, burst/velocity recovery
    behavior, quiet floor default, and reserve-based slow-drift protection.

## 4. HW validation

- [ ] 4.1 Fast-step print (300↔1500): the burst collapses to at most **one**
  TENSION touch per step; recovery fast, no re-drain. (See §5 — the burst
  escalation did NOT achieve this; the recovery floor does.)
- [ ] 4.2 Regression: a slow single crossing does not overshoot into COMPRESSION
  (the `7178c34` gentle path still applies); slow benchy still rides the reserve.
- [ ] 4.3 Record final knob defaults; finalize gen_config defaults.

## 5. Pivot: decaying recovery feed floor (replaces burst escalation)

HW (fix3→fix7, 2026-06-03): the §1.2 burst escalation does NOT collapse the
burst. Even after fixing the gate (ungated from `sample_valid`), widening the
window (`BURST_MS 3000`), and making the tension blend raise-only, EST still
oscillates `~780 ↔ ~1080` and TPX climbs to 12-13. Root cause: the escalation
pushes `extruder_est_sps`, but the recovery-cycle NEUTRAL-fill / COMPRESSION-drain
crossings **correctly re-estimate the low wall demand (~766)** and pull EST back
down between touches — so the re-drain (burst) persists. No EST-side fix can win:
between steps the demand genuinely is low.

Fix: a **feed-side decaying floor**, independent of EST, slammed high on the
tension touch — holds NEUTRAL feed up through the recovery so the buffer can't
re-drain, then decays (handing off to EST as it converges). Operator decision:
1 tension touch/step is acceptable; a brief COMPRESSION pulse during recovery is
acceptable if recovery is fast. Static `SYNC_MIN_RATE 1000` also worked but is
loud always; the decaying floor is loud only for ~1.5 s after each touch.

- [x] 5.1 `firmware/src/sync.c`: **remove** the §1.2 burst escalation
  (`SYNC_TENSION_ESC_STEP_SPS`, `SYNC_TENSION_ESC_RATIO`, `SYNC_TENSION_BURST_MS`,
  `g_tension_burst_n`, `g_last_tension_ms`, `type_d_tension_burst_neutral_reset`)
  — proven ineffective. Keep the velocity-snap raise-only (§1.1) as a benign
  monotonic-up EST nudge (or remove if simpler).
  - 2026-06-03: Removed escalation state, neutral reset helper, unconditional EST
    bump block, and public `_BURST/_ESC` knobs from firmware/config/protocol/dump
    docs. Kept the raise-only velocity snap.
- [x] 5.2 Add the recovery floor: on the unconditional `BUF_NEUTRAL -> BUF_TENSION`
  crossing, set `g_tension_floor_sps = SYNC_TENSION_RECOVERY_FLOOR (sps)` and
  `g_tension_floor_set_ms = now_ms`.
  - 2026-06-03: Added `g_tension_floor_sps` / `g_tension_floor_set_ms`; tension
    crossing arms the floor from `SYNC_TENSION_RECOVERY_FLOOR_SPS`.
- [x] 5.3 In the type-D `BUF_NEUTRAL` relay feed path (after `target_sps` is
  computed, before the final clamp), apply the decaying floor:
  `age = now - g_tension_floor_set_ms; if (g_tension_floor_sps>0 && age <
  SYNC_TENSION_RECOVERY_MS) { floor = g_tension_floor_sps * (1 - age/RECOVERY_MS);
  target_sps = max(target_sps, floor); }`. Feed-side only — independent of EST.
  **NEUTRAL-only**: gate on `s == BUF_NEUTRAL` so an overshoot into COMPRESSION
  stops the floor and lets the §16/§19 gated-drain stabilize the buffer (floor
  must NOT fight the COMPRESSION drain). The floor re-applies if the buffer
  returns to NEUTRAL while still inside the recovery window.
  - 2026-06-03: Added a NEUTRAL-only decaying lower-bound helper applied after
    relay/recovery shapers and before the final clamp; it never touches
    `extruder_est_sps` and expires on zero/elapsed recovery windows.
- [x] 5.4 New knobs (non-persisted, plumb like `SYNC_COMPRESSION_DRAIN_FRAC`):
  `SYNC_TENSION_RECOVERY_FLOOR` (mm/min, default ≈ `2400` / catchup level; `0` =
  disabled), `SYNC_TENSION_RECOVERY_MS` (default ≈ `1500`). Reset
  `g_tension_floor_sps` in `sync_disable`. Mind the 32-char param-name limit.
  - 2026-06-03: Added generator defaults/macros, non-persisted settings load
    reset, protocol SET/GET, `flare_cmd.py --dump`, config example, and manual
    docs. `settings_t` unchanged.
- [x] 5.5 `BUF_SENSOR_TYPE == 0` only; type-P untouched.
  - 2026-06-03: Floor arming and application are guarded by `BUF_SENSOR_TYPE == 0`
    and `s == BUF_NEUTRAL`; no `psf_control_law` or type-P estimator edits.
- [x] 5.6 Build + tests green; OpenSpec strict.
  - 2026-06-03: Passed `ninja -C build_local`,
    `bash scripts/validate_regression.sh`, `python3 -m py_compile scripts/*.py`,
    `python3 scripts/test_*.py`, and
    `openspec validate type-d-dynamic-flow --strict`.
- [x] 5.7 HW (2026-06-04): decaying floor swept FLOOR 1400/2000/2400 ×
  MS 1500/2500/3000/∞. **Cannot win** — decay-to-zero always returns feed to the
  low wall baseline → touch-2; no MS fits all slow-fast-slow intervals; a held
  floor (MS 60000) needs FLOOR 2000 to kill tension but then pins COMPRESSION
  (9900 ms, EST−MM −411) = loud walls. Pivoted to §6.

## 6. Pivot: AIMD probe latch (replaces decaying floor) — commit `e947354`

The decaying floor has two coupled structural failures (see §5.7 + design.md
"Pivot 2026-06-04"). Replacement: a **held feed-floor latch hunted by symmetric
AIMD, no clock**. COMPRESSION (not a timeout) is the recovery-done signal.

- [x] 6.1 `firmware/src/sync.c`: `g_tension_floor_sps` → `float`, drop
  `g_tension_floor_set_ms`. On `NEUTRAL→TENSION` snap `floor = max(floor, EST)`
  (raise-only). Remove `type_d_tension_recovery_floor_sps` decay helper.
- [x] 6.2 In `sync_tick`, per-tick AIMD on the latch: `BUF_TENSION` →
  `floor += PROBE_UP·dt`; `BUF_COMPRESSION` → `floor -= PROBE_DOWN·dt`;
  `BUF_NEUTRAL` → hold; clamp `[0, PROBE_MAX]`; apply only in NEUTRAL.
- [x] 6.3 Retire knobs `SYNC_TENSION_RECOVERY_FLOOR` / `_MS`; add
  `SYNC_TENSION_PROBE_MAX` (3000 mm/min), `_UP` (3000 mm/min/s), `_DOWN`
  (600 mm/min/s) across header/main/settings/protocol/flare_cmd/gen_config/
  config.ini(.example)/param-width test/docs. Non-persisted, no SETTINGS_VERSION
  bump. Build + param-width test green.
- [x] 6.4 HW MILESTONE (2026-06-04, default knobs, live-print captures):
  **TENSION burst COLLAPSED** — 2-3 tension events per ~60-85 s capture (was
  TPX→12 clusters). Goal met. Cost: floor parks ~250-330 sps compression-side
  (EST−MM −256..−332, BP mean +3.9..+4.3, compression rate ~0.31/s, pin ~13 %,
  max pin 800 ms = brief/draining, not a jam). PROBE_DOWN sweep 600/1200/1800:
  **no resolvable effect** once time-normalized (raw counts were over unequal
  windows — 84.5/58.4/64.5 s — so the earlier "U-shape" was an artifact). The
  compression-park is the type-D structural floor (zero-skip ⇒ compression-noisy;
  see `typed-stepskip-floor-fix`), not a DOWN tunable. Live captures are
  noise-dominated (same config 3↔11 touches/segment).
- [x] 6.5 A/B DECISION (2026-06-04, slow print, near-equal duration 184 vs
  198 s): on the cleaner slow stimulus DOWN 1200 beats 600 clearly — comp/s
  0.33→0.22 (−33 %), pin% 1.2→0.35 (−70 %), max pin 800→500 ms, BP mean
  +4.24→+3.88, both tension/s 0 and EST−MM ~+1 (latch barely engaged on slow =
  regression PASS, no overfeed). The earlier fast-step "flat DOWN" was because
  frequent tension re-snaps outrun the leak; slow sections are where leak-rate
  shows. No fast-case regression (burst stays collapsed). **Baked
  `SYNC_TENSION_PROBE_DOWN` default 600 → 1200.** Audibly indistinguishable but
  measurably quieter.
- [x] 6.6 COMPRESSION back-off clamp at EST (2026-06-04): operator saw an
  occasional **compression→tension lean** (not a double-click) — the floor
  ramped DOWN past demand during a long compression dwell, left COMPRESSION
  under-fed, then drained into TENSION. Fix: clamp the per-tick down-ramp at the
  live `extruder_est_sps` (`floor = max(floor - DOWN·dt, EST)`) so back-off eases
  toward demand but never below it — overshoot stays on the safe (compression)
  side, a genuine demand drop still leaks down as the drain crossing re-lowers
  EST. Dissolves the DOWN-vs-lean seesaw so DOWN 1200 (wall-quiet) is kept.
  **REVERTED (2e64ba2):** HW poll showed `EST` pinned at ~2400 (ceiling)
  through slow features — the frozen-EST type-D limit — so clamping
  `floor >= EST` pinned the floor high: slow features overfed into long
  COMPRESSION pins, fast features bang-banged at 2400. The clamp assumed EST
  tracks demand; between crossings it does not. Restored plain leaky AIMD
  back-off. The rare compression→tension lean is unsolved and needs floor+EST
  telemetry to catch, not a blind EST bound.
- [x] 6.8 NEUTRAL uncertainty creep (2026-06-04): operator note — the AIMD
  held in NEUTRAL, but NEUTRAL is the *uncertain* state (buffer may drift to
  either rail; only a click resolves it). Poll showed the floor leaking to
  `feed == demand` exactly → metastable → slow noise drift into a TENSION click
  (the bad rail). Added a gentle NEUTRAL up-creep `floor += PROBE_NEUTRAL·dt`
  toward the safe COMPRESSION rail, so uncertainty always resolves into a
  compression click (drains) not a tension starve; dwell-based, needs no
  dead-reckon (distinct from the dropped position lean) and guarantees a rail
  (unlike a fixed `RELAY_NEUTRAL_FRAC` offset). New knob
  `SYNC_TENSION_PROBE_NEUTRAL` (default 300 mm/min/s, gentle; 0 = old hold).
  Build + param-width green. HW watch pending — ship behind knob, observe poll
  before baking; risk = over-compression if too high.
  - HW (2026-06-04): creep WORKS — poll shows NEUTRAL feed actively climbing to
    a COMPRESSION click; **tension now only on structural step-ups** (floor sits
    at demand, EST snaps low→high only when demand actually steps), metastable
    slow-drift-to-tension gone. Swept 300→150: 150 overshoots less (high-demand
    floor ~1789 vs ~2083 over demand ~810, −22 %) → shorter compression drains,
    no regression. **Baked default 300 → 150.** DOWN 2400 vs 1200 was ~no-op
    (kept 1200). Remaining compression-dwell = type-D structural ceiling
    (zero-skip ⇒ compression-noisy; type-P is the only quiet+touch-free path).
    Final A/B = fixed sliced print, not square-wave eyeballing.
- [ ] 6.7 OPEN (surfaced 2026-06-04): `EST` does not decay on sustained
  slow/compression (poll showed it pinned at the 2400 ceiling through slow
  features). This force-feeds slow features and is an estimator/reserve issue,
  NOT a recovery-floor problem — the floor cannot fix a demand estimate that
  will not come down. Investigate with floor+EST telemetry before any further
  floor edits.
