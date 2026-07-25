## 0. Pre-work

- [ ] 0.1 Record the rig's actual `END_PRINT` retract length + feedrate from the
  slicer end G-code into `design.md` "Open Inputs"; these seed
  `_FLARE_VARS.print_end_retract_len` / `print_end_retract_speed`. Acceptance:
  both numbers written down, and the retract classified against the ceiling
  arithmetic (below 83.33 mm/s = ordering race only; above = reserve budget in
  play, `L_max` computed).
- [ ] 0.2 `GET BUF_MAX_TRAVEL_MM` on the rig; confirm the assumed 16. Acceptance:
  value recorded in `design.md`.
- [ ] 0.3 Capture a baseline print-end event trace (`BL:*`, `BUF_STAB:*`, `BP:`)
  so post-fix comparison is against measured, not remembered, behavior.
  Acceptance: trace saved under `scratch/`, `EV:BL:TIMEOUT` presence noted.

## 1. Host completion wait (A)

- [ ] 1.1 `scripts/flare_cmd.py` `COMPLETION_EVENTS`: add
  `'BL': (['EV:BL:LOCKED', 'EV:BL:PRIME_BOUND'], ['EV:BL:TIMEOUT'])`.
  Acceptance: `flare BL:T:30:3000` blocks until the board reports locked; exits
  1 on `EV:BL:TIMEOUT`.
- [ ] 1.2 `scripts/flare_cmd.py` `COMPLETION_EVENTS`: add
  `'BS': (['EV:BUF_STAB:DONE'], ['EV:BUF_STAB:TIMEOUT', 'EV:BUF_STAB:STAGNANT_TIMEOUT'])`.
  Acceptance: `flare BS` blocks until stabilize done.
- [ ] 1.3 Verify `BS` emits `EV:BUF_STAB:DONE` on every completion path
  (`sync.c:316`, `:374`, `:392`) including the type-P no-op / already-at-goal
  case, and that `g_buffer_stabilize_emit_events` is set for host-issued `BS`.
  If any path completes silently, add the event. Acceptance: no `BS` invocation
  can hang the CLI to `--timeout`.
- [ ] 1.4 `scripts/test_wire_format.py` (or new `scripts/test_flare_cmd_wait.py`):
  unit-test the `BL` / `BS` completion-event mapping against a stubbed SSE
  stream, incl. the error-event exit path. Acceptance: tests pass under
  `python3 -m pytest scripts/`; test file is discoverable (see
  `audit-hardening-fixes` gotcha: `unittest discover` silently skips
  non-`TestCase` files).

## 2. Macros stop guessing (B, E)

- [ ] 2.1 `klipper/flare_mmu.cfg` `_FLARE_BUFFER_STABILIZE`: default `SETTLE` to
  `0` and document it as an *extra* dwell on top of the now-blocking `BS`.
  Acceptance: macro emits no `G4` unless `SETTLE > 0`.
- [ ] 2.2 `klipper/flare_mmu.cfg`: audit every `_FLARE_BUFFER_STABILIZE` call
  site (lines ~142, 202, 242, 249, 264, 347) — drop explicit `SETTLE=0`, keep an
  explicit `SETTLE=` only where mechanical settle beyond the event is justified,
  with a one-line why. Acceptance: each remaining `SETTLE=` has a comment.
- [ ] 2.3 `klipper/flare_mmu.cfg` `_FLARE_BL_RETRACT`: drop the `PAUSE` param and
  its `G4` (the `BL` shell call now blocks until locked). Update the macro
  description. Acceptance: no `PAUSE` references remain; `FLARE_UNLOAD_TOOLHEAD`
  and `_FLARE_TIP_FORMING` call sites updated.
- [ ] 2.4 `klipper/flare_mmu.cfg` `_FLARE_VARS`: add
  `variable_print_end_retract_len` and `variable_print_end_retract_speed` (mm,
  mm/s) seeded from task 0.1, plus `variable_mmu_follow_ceiling: 83.33` and
  `variable_buf_max_travel_mm` mirroring the firmware. Comment the two mirrored
  vars with `flare_cmd.py --dump` as the source of truth. Acceptance: four
  variables present + documented.
- [ ] 2.5 `klipper/flare_mmu.cfg` `_FLARE_VARS`: set `mmu_follow_rate` to
  `5000.0` (= `GLOBAL_MAX_SPS`, the real ceiling) and replace the
  "values above the firmware max are harmless" comment with the ceiling value
  and why it is the ceiling. Acceptance: configured rate equals the commanded
  rate; no silently-clamped number left in the file.
- [ ] 2.6 `klipper/flare_mmu.cfg` `_FLARE_BL_RETRACT`: rename the local
  `speed_mmpm` to `speed_mm_s` (it holds mm/s — `F{speed_mmpm*60}`). No
  behavior change; call sites already pass mm/s. Acceptance: no `_mmpm` name
  remains on an mm/s value.
- [ ] 2.7 `klipper/flare_mmu.cfg` `_FLARE_BL_RETRACT`: `RESPOND` a warning when
  `SPEED > mmu_follow_ceiling` and
  `LENGTH > buf_max_travel_mm / (1 - mmu_follow_ceiling/SPEED)`, naming the
  computed `L_max` and the excess. Warn only — no clamp. Acceptance:
  `LENGTH=60 SPEED=125` warns (`L_max` 48 mm); `LENGTH=30 SPEED=125` and any
  `SPEED <= 83.33` stay silent.
- [ ] 2.8 `klipper/flare_mmu.cfg`: add `[gcode_macro _FLARE_RETRACT_GUARD_BEGIN]`
  — `LENGTH` / `SPEED` / `TIMEOUT` params defaulting to
  `print_end_retract_len` / `print_end_retract_speed` / 120 s; issues
  `BL:T:{LENGTH}:{mmu_follow_rate}:{TIMEOUT*1000}` (blocking) with no preceding
  `BS`, and runs the 2.7 budget warning. `LENGTH` is the **true** retract length
  — firmware subtracts half-travel by design (`sync.c:800`). Acceptance: arming
  returns only once locked; `LENGTH=40` yields a 32 mm follow.
- [ ] 2.9 `klipper/flare_mmu.cfg`: add `[gcode_macro _FLARE_RETRACT_GUARD_END]` —
  blocking `BS` then `_FLARE_SYNC_TOOLHEAD`. Acceptance: lock released, board
  toolhead state truthful, no `EV:BL:TIMEOUT`.
- [ ] 2.10 `klipper/flare_mmu.cfg`: confirm FLARE defines **no** macro named
  `END_PRINT`, `CANCEL_PRINT`, `PRINT_END`, or any other host-owned lifecycle
  name, so the wrapper recipe can never collide. Acceptance: grep clean; noted in
  the file header.
- [ ] 2.11 `KLIPPER.md`: document the `rename_existing` wrapper recipe with the
  Klippain `END_PRINT` + `CANCEL_PRINT` pair as the worked example — including
  that **both** need wrapping (same retract line), that `[include flare_mmu.cfg]`
  must come **after** the wrapped macros are defined, that `LENGTH` is the true
  retract length, and the reserve-budget rule
  `L_max = BUF_MAX_TRAVEL_MM / (1 - 83.33/SPEED)`. Acceptance: a reader can wrap
  an arbitrary third-party macro from the doc alone; no macro-file duplication
  (`klipper-integration` spec: KLIPPER.md scope is integration-only).

## 2b. Host-settable buffer-lock watchdog (H)

- [ ] 2b.1 `firmware/src/protocol.c` `BL` parser (`:1752`): accept an optional 4th
  field `timeout_ms`, `BL:<state>:<follow_mm>:<follow_rate>:<timeout_ms>`.
  Reject non-numeric / negative with `ER:`. Acceptance: 2-, 3-, and 4-field forms
  all parse; malformed 4th field errors rather than silently defaulting.
- [ ] 2b.2 `firmware/src/protocol.c:1625` doc block: extend the `BL` command
  comment with the new form and that omitting the field keeps
  `BL_WATCHDOG_DEFAULT_MS`. Acceptance: comment matches the parser.
- [ ] 2b.3 `firmware/src/sync.c` `sync_buffer_lock_arm`: take a `timeout_ms`
  argument; `0`/absent means `BL_WATCHDOG_DEFAULT_MS`. Store it and use it where
  `sync_buffer_lock_prime` currently sets
  `g_bl_watchdog_ms = now_ms + BL_WATCHDOG_DEFAULT_MS`. Acceptance: a 120 s arm
  does not time out at 30 s; a bare `BL:T` still does.
- [ ] 2b.4 `scripts/test_protocol_param_width.py`: extend for the 4-field `BL`
  form. Acceptance: width/parse test covers it.
- [ ] 2b.5 `MANUAL.md`: document the optional `timeout_ms` field. Acceptance:
  `grep -n 'BL:' MANUAL.md` shows the 4-field form.

## 3. Type-P prime authority (C)

- [ ] 3.1 `firmware/include/sync_internal.h`: add
  `#define BL_PRIME_PREDICT_LEAD_S 0.10f` next to `SYNC_STAB_PREDICT_LEAD_S`,
  with a comment pointing at the EMA-lag rationale. Acceptance: constant defined,
  not a runtime tunable.
- [ ] 3.2 `firmware/src/sync.c` `sync_buffer_lock_arm`: delete the type-P
  `BUF_STAB_SPS` branch (`:786`) so type-P falls through to the existing
  `sync_clamp_max_sps(g_sync_max_sps)` expression. Acceptance: type-P prime
  commands 15004 sps; the type-D expression is textually unchanged in the diff.
- [ ] 3.3 `firmware/src/sync.c` `sync_buffer_lock_prime`: for type-P, test the
  predicted position `g_buf_pos + BL_PRIME_PREDICT_LEAD_S * g_vel_norm_f`
  against `PSF_HOME_THRESHOLD_NORM` instead of the raw position. Type-D branch
  (`buf_state_raw()`) untouched. Acceptance: prime stops without crossing the
  rail on the bench.
- [ ] 3.4 `firmware/src/sync.c:797`: keep `g_bl_prime_cap_mm = g_buf_max_travel_mm`
  (now spec-sanctioned) and update the comment at `:103` to cite the requirement
  name rather than describing it as an ad-hoc outer cap. Acceptance: comment and
  spec agree.
- [ ] 3.5 Bench HW: `BS` then `BL:T` from the compression side, watch `BP:`.
  Acceptance: prime reaches tension without `PRIME_BOUND`, no rail slam, time
  ~0.3s vs ~1.0s baseline. If it overshoots, raise `BL_PRIME_PREDICT_LEAD_S` and
  record the value; if it cannot be tuned, revert 3.2/3.3 and note the failure.

## 4. Servo catch (D)

- [ ] 4.1 `firmware/include/sync_internal.h`: add `BL_CATCH_ERR_SPAN_NORM`
  (seed `1.0f`) — buffer error, in normalized units away from the armed rail, at
  which the catch reaches `GLOBAL_MAX_SPS`. Acceptance: constant defined +
  commented.
- [ ] 4.2 `firmware/src/sync.c` `sync_buffer_lock_locked`: emit
  `cmd_event("BL", "BREAK")` on the raw-departure edge, before entering the
  catch. Acceptance: `EV:BL:BREAK` precedes `EV:BL:FOLLOW` in the trace.
- [ ] 4.3 `firmware/src/sync.c` `sync_buffer_lock_locked`: allow catch entry when
  the host supplied no follow budget — default `g_bl_follow_mm` to
  `g_buf_max_travel_mm` so a bare `BL:T` is not authority-free. Acceptance:
  `BL:T` alone catches an external retract; distance still bounded.
- [ ] 4.4 `firmware/src/sync.c` `sync_buffer_lock_follow`: recompute
  `g_bl_follow_target_sps` every tick as
  `seed + (global_max - seed) * clamp(err / BL_CATCH_ERR_SPAN_NORM, 0, 1)`
  where `err` = distance of `buf_pos` from the armed rail norm, `seed` = the
  host `follow_rate` in sps. Keep the existing `RAMP_STEP_SPS` / `RAMP_TICK_MS`
  ramp toward it (do NOT restore the instant write — pull-in stall, personal
  memory `bl-follow-instant-rate-stall`). Acceptance: escalates under a fast
  external retract, idles at the seed rate under a slow one.
- [ ] 4.5 `firmware/src/sync.c` `sync_buffer_lock_follow`: keep the
  `PSF_FOLLOW_RAIL_NORM` down-gate → `BL_LOCKED` (`FOLLOW_GATED`) ahead of the
  escalation so the catch still cannot slam the armed rail. Acceptance: gate
  fires before the rail with escalation active.
- [ ] 4.6 Distance integration must use the servo'd rate, not the seed — verify
  `g_bl_follow_traveled_mm` still integrates `g_bl_follow_mm_per_s`. Acceptance:
  `FOLLOW_DONE` fires at the requested distance under escalation.
- [ ] 4.7 Gate the escalation to `g_buf_sensor_type == BUF_SENSOR_TYPE_P` —
  escalation needs a continuous mid-band position signal type-D lacks (personal
  memory `typed-buffer-no-midband-groundtruth`). Type-D keeps the fixed seed
  rate. Acceptance: with `BUF_SENSOR_TYPE=0` the commanded follow rate sequence
  is identical to pre-change.
- [ ] 4.8 `EV:BL:BREAK` (4.2) fires for both sensor types — it is an event, not a
  motion change. Acceptance: type-D emits `BREAK` before `FOLLOW` with no rate
  or timing difference.

## 5. Spec + docs reconcile

- [ ] 5.1 `openspec/specs/buffer-state-lock/spec.md` Purpose: widen from
  "type-D buffers" to both sensor types. Acceptance: Purpose names type-P.
- [ ] 5.2 Apply the `buffer-state-lock` delta (prime cap rename, type-P prime,
  servo catch, `EV:BL:BREAK`). Acceptance: `openspec validate` passes.
- [ ] 5.3 Apply the `sync-state-model` delta: catch rate clause reconciled to
  `GLOBAL_MAX_SPS`, ramped. Acceptance: no clause conflicts with
  `buffer-state-lock`.
- [ ] 5.4 Apply the `psf-type-p-sensor`, `klipper-integration`,
  `klipper-mmu-config` deltas. Acceptance: `openspec validate --strict` clean.
- [ ] 5.5 `MANUAL.md`: document `BL` event contract (`PRIME`, `PRIME_BOUND`,
  `LOCKED`, `BREAK`, `FOLLOW`, `FOLLOW_GATED`, `FOLLOW_DONE`, `TIMEOUT`) and that
  the host CLI blocks on it. Acceptance: `grep -n 'EV:BL' MANUAL.md` shows the
  full set.
- [ ] 5.6 `TEST_CASES.md`: add print-end retract catch case + bare-`BL:T` catch
  case. Acceptance: both present with pass criteria.

## 6. Build + validation

- [ ] 6.1 `./build_local` green. Also build the dev superset
  (`-DFLARE_DEV_TUNING=ON`) — `FLARE_DEV_TUNING` blocks are invisible to the
  default build and clang-tidy (personal memory `dev-tuning-build-blindspot`).
  Acceptance: both configs compile clean.
- [ ] 6.2 `./build_clang` / clang-tidy gate clean on both configs.
  Acceptance: no new diagnostics.
- [ ] 6.3 `python3 -m pytest scripts/` green, incl. the new CLI wait tests.
  Acceptance: all pass, count recorded.
- [ ] 6.4 HW: rerun the task 0.3 trace with the new end G-code. Acceptance:
  no `EV:BL:TIMEOUT`; buffer does not reach the compression stop during
  `END_PRINT`; `BP:` at print end within the tension half. Compare *rates*, not
  raw counts (personal memory `typed-tension-recovery-floor`).
- [ ] 6.5 Type-D preservation, desk-check only (no type-D rig in scope): walk the
  staged diff of `sync_buffer_lock_arm` / `_prime` / `_locked` / `_follow` and
  confirm every changed line is either inside a `BUF_SENSOR_TYPE_P` branch or
  event-only. Acceptance: reviewer can name each type-D-reachable changed line
  and why it is inert; list them in the readiness note.
- [ ] 6.6 HW: count `FOLLOW_GATED` → `BREAK` → `FOLLOW` cycles during a sustained
  retract and record the `BP:` swing (design predicts ~0.4 mm on the 16 mm rig).
  Acceptance: amplitude bounded and not growing; if the cycle is audibly harsh,
  log it as the trigger for the continuous-law follow-on rather than widening
  scope here.
- [ ] 6.7 Cross-check the mirrored vars against the firmware:
  `flare_cmd.py --dump` -> `global_max_sps` and `buf_max_travel_mm` must match
  `_FLARE_VARS.mmu_follow_ceiling` (mm/s) and `buf_max_travel_mm`. Acceptance:
  both agree; mismatch documented if the rig differs from defaults.
- [ ] 6.8 HW: verify the reserve-budget warning fires on a deliberately
  over-budget retract and stays silent on the tip-form park (125 mm/s, ~25-30 mm,
  inside the 48 mm budget). Acceptance: one true positive, no false positive on
  the normal toolchange path.
- [ ] 6.9 HW: wrap the rig's real Klippain `END_PRINT` and `CANCEL_PRINT` per the
  2.11 recipe with `LENGTH=40 SPEED=35`. Acceptance: buffer stays off the
  compression stop through the whole 40 mm retract; `BP:` lands near NEUTRAL at
  `_FLARE_RETRACT_GUARD_END`; no `EV:BL:TIMEOUT` on either macro, including a
  deliberate mid-print cancel.
- [ ] 6.10 Full multi-colour print. Acceptance: no buffer fault, no spurious
  `BL`/`BS` events, tip quality unchanged.

## 7. Readiness

- [ ] 7.1 Write `memories/repo/` observation file for this change (root cause,
  the spec/code divergence, what the servo catch changed). Acceptance: file
  present, no source snippets or secrets.
- [ ] 7.2 Set a real `## Purpose` on every touched spec before archive
  (personal memory `openspec-archive-purpose-gotcha`: archive stamps a
  placeholder Purpose and trips the `spec-readability` tripwire).
  Acceptance: no placeholder Purpose in any touched spec.
- [ ] 7.3 `openspec validate --strict` + `REVIEW.md` staged-diff self-review.
  Acceptance: both clean.
