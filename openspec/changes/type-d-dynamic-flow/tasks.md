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
- [ ] 5.7 HW: fast-step print → 1 TENSION touch per step, no burst, recovery
  fast; brief COMPRESSION pulse acceptable. Sweep `SYNC_TENSION_RECOVERY_FLOOR`
  (high enough to cover infill demand) and `SYNC_TENSION_RECOVERY_MS` (long
  enough to bridge until EST converges, short enough to stay quiet between steps).
