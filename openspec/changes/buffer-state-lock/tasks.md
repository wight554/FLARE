## 1. HW Bench (gating)

- [x] 1.1 Measure MMU sustained top speed on rig. **Result:** unloaded ~100 mm/s (6000 mm/min) sustained without stall; ~117 mm/s (7000 mm/min) is the beep onset. Loaded (FL) ceiling is ~50 mm/s (3000 mm/min) — path friction (PTFE/hub/extruder) lowers torque headroom. Below the 110 mm/s ideal target, but the design.md §D6 envelope at 40 mm @ 150 mm/s still fits with the raw-edge lock-break (3.2 mm margin). Recorded in design.md §D6.
- [x] 1.2 Characterize instant-slam practical accel. **Result:** `ramp_step_rate=1000` (~3500 mm/s²) holds without stall on the rig motor (1.2 A, 16 µsteps, 22.6789511 rot_dist). `1500` (~5000 mm/s²) stalls earlier and drops the achievable top to 6000 mm/min. Picked **3500 mm/s² as the operator value**; default ships at 3500 in `gen_config.py` (`global_max_accel: 3500`). Recorded in design.md §D6.
- [x] 1.3 Confirm operator's hard upper bound for `park_speed`. **Result:** unloaded motor top = 100 mm/s; the BL catch envelope was validated for extruder retracts at 150 mm/s with the asymmetric-safety design (catch can over-drain back to tension safely, only compression slam fails). The operator's accepted `park_speed` ceiling for `BL`-guarded retracts is **150 mm/s** with the bench-validated geometry (40 mm @ 150 mm/s → 16.76 mm peak excursion vs 20 mm runway). Recorded in design.md §D6.

## 2. Firmware: Lifecycle + Catch Engine

- [x] 2.1 Repurpose `SYNC_RETRACT_ASSIST` state in `firmware/src/sync.c` to host the buffer-lock lifecycle (prime / locked / catch / settle sub-states), preserving the "learning paused, estimator preserved" contract.
- [x] 2.2 Restore an aggressive slam drive helper (analogue of the deleted `retract_assist_drive` from commit `f19f41a`) that writes `current_sps = target` directly, bypassing `SYNC_RAMP_UP_SPS`.
- [x] 2.3 Wire the sync tick to detect raw `BUF_*` departure from the armed extreme while in the locked sub-state and transition to the catch sub-state on the same tick (no hyst debounce on the break edge).
- [x] 2.4 Implement the locked-state watchdog (default 30s) that auto-releases and emits `EV:BL:TIMEOUT`.

## 3. Firmware: Prime Move

- [x] 3.1 Add a sync-owned prime drive that retracts (or feeds) the active lane until raw target state OR `BUF_MAX_TRAVEL_MM / 2` of MMU travel, whichever comes first.
- [x] 3.2 Emit `EV:BL:PRIME_BOUND` when the half-travel cap stops the prime before target raw state.
- [x] 3.3 Verify prime runs without engaging the `TASK_MOVE` fault guards (it is sync-owned, not an `MV` task).
- [x] 3.4 Fix prime motor direction convention: `forward=false` (retract) → TENSION, `forward=true` (extrude) → COMPRESSION. Revert wrong inversion from `57ab7be` (commit `57850b8`).
- [x] 3.5 Two absolute prime caps:
  - **Absolute 1** — outer safety search cap = `BUF_MAX_TRAVEL_MM` (25 mm); motor searches the full physical range before giving up with `PRIME_BOUND` (commit `5a8d2b5`).
  - **Absolute 2** — ~~post-click settle cap = `(BUF_MAX_TRAVEL_MM - switch_span) / 2` = 7.5 mm; motor continues this distance past the switch click to seat the arm firmly against the extreme (commits `7026762`, `c32db19`).~~ **REMOVED**: post-click settle distance reduced to 0. The 7.5 mm settle parked the buffer at deep-tension hard-end, over-stretching the filament and stealing catch runway; locking at the switch click is enough for lock-break edge detection.
- [x] 3.6 Two-phase prime: **Phase 1** drives at `SYNC_MAX_SPS` until target switch fires (or 25 mm outer cap → `PRIME_BOUND`). **Phase 2** continues exactly 7.5 mm past the switch click, then stops → `BL:LOCKED`. Distance tracked via `mm_per_s = prime_sps × MM_PER_STEP` from two independent start timestamps (commit `c32db19`).
- [x] 3.7 Run prime at `SYNC_MAX_SPS` (same ceiling as catch); two-phase distance gates handle overtravel at any speed — slow `BUF_STAB_SPS` no longer needed (commit `deaafeb`).
- [x] 3.8 Add BL_CATCH distance cap = `(max_travel - switch_span) / 2` = 7.5 mm from catch-start; emit `EV:BL:CATCH_OVERRUN` and force-release if opposite switch fails to register within that budget (commit `68fb61d`, corrected formula `7026762`).

### HW: Validate §3.5–3.8
- [x] HW:3.5a Verify `BL:T` prime reaches TENSION switch within 25 mm search budget: `BUF:TENSION` appears in poll trace before `BL:LOCKED`. **Result (2026-05-27):** `BL:PRIME` → `BL:LOCKED` → `BUF:TENSION` confirmed in telemetry; prime completed at switch click well within 25mm budget.
- [ ] HW:3.5b ~~Verify `BL:T` post-click settle: after TENSION click, motor continues ~7.5 mm more (arm visibly pushed against extreme), then `BL:LOCKED`.~~ **SUPERSEDED (§3.5):** post-click settle was removed — motor locks at switch click, no additional travel.
- [ ] HW:3.6 Verify `PRIME_BOUND` fires when TENSION sensor deliberately blocked (e.g. `BUF_INVERT=1` test): `EV:BL:PRIME_BOUND` in trace, then `BL:LOCKED` anyway.
- [ ] HW:3.8 ~~Verify `CATCH_OVERRUN` fires when compression sensor blocked: issue `BL:T`, trigger lock-break manually, confirm `EV:BL:CATCH_OVERRUN` appears before 7.5 mm budget expires.~~ **SUPERSEDED (§10):** catch state removed; lock is now passive.

## 4. Firmware: Protocol Surface

- [x] 4.1 Add `BL` command parser to `firmware/src/protocol.c` accepting `BL`, `BL:T`, `BL:C`; reject with `ER:BUSY` when sync is active or a non-idle task is running.
- [x] 4.2 Add `BL` to the status GET output (`BL:T` / `BL:C` / `BL:0`).
- [x] 4.3 Remove the legacy `RA` command parser, `RA` status field, and the `sync_retract_assist_*` host-facing wiring; replies to `RA:*` become `ER:CMD`.
- [x] 4.4 Ensure `BS` releases lock and catch immediately and returns to `SYNC_OFF` with a normal stabilization pass.

## 5. Firmware: MV Guard Interaction

- [x] 5.1 Document and enforce the spec rule that `MV` buffer fault guards (`FAULT:MOVE_TENSION` / `FAULT:MOVE_COMPRESSION`) are suppressed only while in `SYNC_RETRACT_ASSIST` catch sub-state; outside it they remain active unchanged.
- [x] 5.2 Add a sanity assert / event if a `TASK_MOVE` ever starts while the catch is active (should be unreachable by the protocol layer).

## 6. Klipper Macros

- [x] 6.1 Add `variable_use_buffer_lock: 0` default flag to `_FLARE_VARS` (or equivalent) for staged rollout.
- [x] 6.2 When the flag is `1`: in `_FLARE_TIP_FORMING`, (a) prepend a `BS` + `G4 P1000` preamble before any extruder move so the buffer starts from a stabilized baseline; (b) replace the blind `RUN_SHELL_COMMAND CMD=flare PARAMS="MV:-{mmu_tip_retract}:{park_speed*60*0.2}:I"` with the sequence `BL:T` → `G4 P1000` → `G0 E-{park_distance-dist_to_meltzone_now} F{park_speed*60}`, and `BS` after the post-park `M400`.
- [x] 6.3 When the flag is `1`: in `_FLARE_UNLOAD_TOOLHEAD`, apply the same sequence around the gear clear — `BL:T` → `G4 P1000` → `G1 E-{gear_retract} F{v.speed_hub_to_extruder*60}` → `M400` → `BS`.
- [x] 6.4 Remove the `mmu_tip_retract` variable computation once the flag is the only path. **Done (2026-05-27):** dead local removed, commit `e4af619`.

## 7. Acceptance Validation

- [x] 7.1 Add a `scripts/flare_sync_check.py` mode (e.g. `--mode buffer-lock`) that asserts: `BL:T` → `OK`, lifecycle transitions through prime/locked/catch/settle, and no `FAULT:MOVE_*` during the catch.
- [x] 7.2 Verify on bench: tip-forming sequence end-to-end, no buffer slam, peak excursion under the design envelope (D6). **Result (2026-05-27):** validated across 17-color-swap print; tip-forming completed cleanly, no buffer slam observed.
- [x] 7.2a Bench the rig-validated operator envelope (D6: 40 mm @ 150 mm/s extruder retract with `ramp_step_rate=1000`, MMU cap 6000 mm/min). Confirm peak buffer excursion ≤ ~17 mm (≤ 20 mm runway) and that lock-break uses the raw edge, not the hyst-debounced state. If observed excursion exceeds 18 mm, revisit `BUF_HYST_MS` handling on the break edge or tighten the retract speed/distance bound. **Result (2026-05-27):** 17-swap print at bench rig settings; no excursion faults, no grind, envelope holds.
- [x] 7.3 Verify on bench: unload-toolhead sequence end-to-end with the gear retract guarded by `BL`, no buffer slam. **Result (2026-05-27):** gear-clear retract via `_FLARE_BL_MOVE` validated across 17 toolchanges; no buffer slam.
- [x] 7.4 Verify watchdog timeout fires when `BL:T` is armed and no break/`BS` arrives. **Result (2026-05-27):** `EV:BL:TIMEOUT` fired 29.86s after `BL:LOCKED` (≈30s, nominal); `BL` field returned to `0` after timeout, `BUF:TENSION` remains (buffer physically at switch — expected).
- [ ] 7.5 ~~Full BL:T lifecycle smoke on bench: `BL:T` → TENSION switch fires → 7.5 mm settle → `BL:LOCKED` event → extruder triggers lock-break → `BL:BREAK` → catch slams to COMPRESSION → `BL:CATCH_SETTLE` → sync resumes. No `PRIME_BOUND`, no `CATCH_OVERRUN`, no faults.~~ **SUPERSEDED (§10):** catch removed; BL:BREAK, CATCH_SETTLE events no longer exist.

## 8. Cleanup + Archive

- [x] 8.1 Remove the `:I` ignore-buffer branch from `_FLARE_TIP_FORMING` after a release window confirms no regressions. **Done (2026-05-27):** removed with flag retirement, commit `1c58755`.
- [x] 8.2 Drop the `variable_use_buffer_lock` flag once `BL` is the only path. **Done (2026-05-27):** flag and all conditional guards removed, commit `1c58755`.
- [x] 8.3 Update `BEHAVIOR.md` / `KLIPPER.md` to describe `BL` and the buffer-lock lifecycle; remove references to the legacy blind retract.
- [ ] 8.4 Archive this change with `openspec archive buffer-state-lock`.

## 9. Accel-Unit Refactor + Sync-Flow Tuning

Captured from explore-mode discussion. Bench validation found the catch
envelope needed both a real `GLOBAL_MAX_RATE` ceiling (was silently
clamped at 5000 mm/min) and an honest accel surface (mm/s², not the
misleading mm/min slew-per-tick).

- [x] 9.1 Raise `GLOBAL_MAX_RATE` outer hard-cap in `protocol.c:801` from 5000 to 12000 mm/min so operators can actually set the ceiling needed for the BL catch envelope (commit `91757b4`).
- [x] 9.2 Raise the `GLOBAL_MAX_RATE` boot and persisted-load clamps in `settings_store.c:148` and `:485` from 5000 to 12000 mm/min so `config.ini global_max_rate` is no longer silently truncated at boot (commit `e7392e2`).
- [x] 9.3 Make `MV` parser respect `GLOBAL_MAX_RATE` via `motion_clamp_rate_sps` (`protocol.c:608`); drop the redundant 50000-sps ceiling, keep the 200-sps floor as a motor stall guard (commits `3de797e`, `807f5a7`).
- [x] 9.4 Rename gen_config defaults to `global_max_accel` (mm/s²), `sync_ramp_accel` (mm/s²), `sync_ramp_decel` (mm/s²); drop the legacy `ramp_step_rate`, `sync_ramp_up_rate`, `sync_ramp_dn_rate` keys outright (no aliases — pre-release). Defaults: 3500 / 150 / 300 mm/s² (commit `c2c92ab`).
- [x] 9.5 Add `accel_to_step_sps()` helper in `scripts/gen_config.py`; derive `CONF_RAMP_STEP_SPS`, `CONF_SYNC_RAMP_UP_SPS`, `CONF_SYNC_RAMP_DN_SPS` from accel × tick / mm_per_step at config-time (commit `c2c92ab`).
- [x] 9.6 Add `SS:GLOBAL_MAX_ACCEL`, `SS:SYNC_RAMP_ACCEL`, `SS:SYNC_RAMP_DECEL` setters in `protocol.c` (mm/s² input, converts to SPS-per-tick via `MM_PER_STEP[0]` + appropriate tick); add matching `GET` handlers; remove `SS:RAMP_STEP_RATE`, `SS:SYNC_UP_RATE`, `SS:SYNC_DN_RATE` and their GETs (commit `c2c92ab`).
- [x] 9.7 Update `config.ini.example` to document the new accel knobs with mm/s² units, the BL-catch envelope context, and the sync-flow coefficient relationship (cross-ref design.md §D6a) (commit `c2c92ab`).
- [x] 9.8 Document the rig-validated operator envelope (40 mm @ 150 mm/s extruder retract, MMU cap 6000 mm/min, lane accel ~3500 mm/s², 1.2 A run current) in design.md §D6 (commits `6d16a59`, `7e4922d`).
- [x] 9.9 Document the sync-flow impact of raising `sync_ramp_accel` in coefficient form in design.md §D6a: `sync_kp_rate ← kp/k`, `zone_bias_max ← bias/√k`, everything else `k⁰` (commit `c2c92ab`).
- [ ] 9.10 Bench `sync_ramp_accel = 150 mm/s²` on rig: either run the autotune (preferred, hands-off, requires active print) or the one-shot stability check. Autotune saddle-searches `SYNC_KP_RATE` between ringing (kp too high) and drift (kp too low) and converges on the best value at the chosen accel: `python3 scripts/flare_sync_check.py --daemon --mode tune --tune-set-accel 150`. One-shot check (no SET, just verdict): `python3 scripts/flare_sync_check.py --daemon --duration 120 --mode stability`.
- [ ] 9.11 If task 9.10 surfaces zone-bias overshoot in `NEUTRAL` recovery, apply `zone_bias_max ← zone_bias_max / √k` and re-soak.
- [ ] 9.12 Bench the two-extreme envelope from explore: 300 mm/min slow extrusion (5 mm/s) and 1500 mm/min fast (25 mm/s). Confirm 150 mm/s² sync slew keeps drift under ~1 mm at both ends; if drift unacceptable at the fast end, raise `sync_ramp_accel` toward 200–300 mm/s² and re-apply kp scaling.
- [ ] 9.13 Record the final per-rig tuned values (`global_max_accel`, `sync_ramp_accel`, `sync_kp_rate`, any `zone_bias_*` overrides) in a rig notes file or `BEHAVIOR.md` appendix.

## 10. Catch Removal (post-bench reversal)

Bench testing revealed the reactive catch was solving a non-problem. The
operator confirmed the legacy `MV:-{mmu_tip_retract}:{park_speed*60*0.2}:I`
flow always reached the COMPRESSION endstop during long unloads — and
that was **accepted behavior**, not a failure mode. Buffer mechanically
absorbs extruder retract; filament bunches in buffer; no motor stall
because the MMU is idle (not fighting taut filament). The catch added
stall risk (motor commanded to retract against still-taut filament at
lock-break) without preventing any real failure.

**Decision: drop the catch entirely. Keep prime + passive locked + watchdog.**

### What was removed
- `BL_CATCH` sub-state in `firmware/src/sync.c`.
- All `g_bl_catch_*` statics (start_ms, mm_per_s, cap_mm, observed_non_target, target_sps, current_sps).
- Lock-break handler in `BL_LOCKED` (raw transition no longer triggers catch).
- Catch tick body (ramped slam, terminator priority chain, OVERRUN cap).
- Events: `BL:BREAK`, `BL:CATCH_DONE`, `BL:CATCH_OVERWHELM`, `EV:BL:CATCH_OVERRUN`, `EV:BL:CATCH_TIMEOUT`, `BL:MV_DURING_CATCH`.
- `sync_buffer_lock_catch_active()` function + `sync.h` declaration.
- `MV_DURING_CATCH` defensive guard in `protocol.c` MV handler.

### What remains
- `BL_PRIME` → drains buffer to switch click, stops at click.
- `BL_LOCKED` → motor energized at zero rate (holding torque); buffer free to migrate via external force. Only watchdog can break the lock from firmware.
- `BS` → releases lock; subsequent stabilize handles cleanup.

### Tasks superseded
- 2.2 (slam helper) — slam direct-write helper deleted with catch path.
- 2.3 (lock-break detection) — no longer wired; lock is passive.
- 3.8 (catch distance cap) — removed with catch.
- HW:3.8 (CATCH_OVERRUN bench check) — N/A.
- 5.1, 5.2 (MV guards during catch / sanity event) — N/A, no catch state to suppress.
- 7.5 (full catch lifecycle smoke) — rewrite as prime-only smoke.

### New tasks
- [x] 10.1 Bench: `BL:T` → extruder retract (tip-forming length, ≤20 mm) → buffer ends in TENSION zone, no slam. **Result:** passive lock works; tip-form completes cleanly with passive BL.
- [x] 10.2 Bench: `BL:T` → extruder long retract (50-150 mm park unload) → buffer reaches COMPRESSION endstop. **Result:** without MMU follow-on, extruder TMC skipped against full buffer → filament tip kept oozing → tip bulge. Drove the design of section 11 (BL:T follow-on retract).
- [x] 10.3 Update design.md §D5 (asymmetric safety) and §D6 (envelope) to reflect that catch is gone; buffer endstop on retract is accepted only with BL:T follow-on mass-balancing the long retract.
- [x] 10.4 Mark `_FLARE_TIP_FORMING` / `_FLARE_UNLOAD_TOOLHEAD` macros: remove any "expect BL:BREAK / CATCH_DONE" log assertions if present.

## 11. BL:T Follow-On Concurrent Retract

Bench (task 10.2) showed passive BL:T is not enough for long extruder
retracts: buffer fills to mechanical endstop, extruder TMC skips, tip
melt continues to ooze → filament tip bulges. Adds an event-triggered
concurrent MMU retract armed at BL arm time:

  `BL:T:<follow_mm>:<follow_rate_mmpm>` / `BL:C:<follow_mm>:<follow_rate_mmpm>`

Lifecycle: PRIME → LOCKED → (on first raw transition off armed extreme)
FOLLOW → LOCKED. Mass balance: buffer fill = (extruder_rate − mmu_rate)
× T. Empty extra args = passive lock (back-compat).

### Shipped
- [x] 11.1 Firmware: BL_FOLLOW sub-state + statics (`g_bl_follow_mm`,
      `g_bl_follow_rate_mmpm`, `g_bl_follow_start_ms`,
      `g_bl_follow_mm_per_s`); FOLLOW arm in LOCKED tick on raw
      transition; FOLLOW tick tracks traveled mm and returns to LOCKED
      on completion. Commit `bf0215a`.
- [x] 11.2 Firmware: extended `sync_buffer_lock_arm` signature with
      `follow_mm` + `follow_rate_mmpm`; `sync_buffer_lock_motor_moving`
      now returns true during FOLLOW for autopreload edge suppression.
      Commit `bf0215a`.
- [x] 11.3 Protocol: BL parser accepts `BL:[T|C]:<mm>:<rate>`;
      `sscanf("%c:%f:%f")`; rejects lone follow_mm without rate via
      `ER:ARG`. Commit `bf0215a`.
- [x] 11.4 Klipper: `_FLARE_TIP_FORMING` + `FLARE_UNLOAD_TOOLHEAD` wire
      `BL:T:<tip_retract_dist|gear_retract>:{v.mmu_follow_rate}`; new
      `_FLARE_VARS.variable_mmu_follow_rate` knob (default 3000
      mm/min). Commit `eb75200`.
- [x] 11.5 Klipper: drop redundant 1-s `G4 P` waits around BL:T / BS —
      catch-state safety margin no longer relevant; the dwell caused
      hotend ooze + tip bulge. Commit `7bfcf9a`.

### Per-rig bench result + tuning (operator)
- [x] 11.6 Reference rig: `global_max_rate=5000`, `global_max_accel=2500`,
      `variable_mmu_follow_rate=2000`. Tip relatively good; no skipping
      under FOLLOW; buffer cycles through TENSION/COMPRESSION cleanly.

### Open
- [x] 11.7 Persist per-rig tuned values (`global_max_rate`,
      `global_max_accel`, `mmu_follow_rate`) in `config.ini` and/or
      bump `gen_config.py` + `flare_mmu.cfg` defaults if the rig
      values are general-purpose.
- [x] 11.8 Firmware speed cap for FOLLOW: clamp the parsed
      `follow_rate_mmpm`-derived SPS through `sync_clamp_max_sps`
      (loaded ceiling = `SYNC_MAX_SPS`) instead of the looser
      `motion_clamp_rate_sps` (global ceiling). Free MMU motion tops
      out at GLOBAL_MAX, but loaded BL FOLLOW stalls well below.
      `sync_clamp_max_sps` already represents the validated loaded
      ceiling and is used by PRIME; reuse here. Bench-verify the cap
      eliminates the need to hand-tune `mmu_follow_rate` down.
- [x] 11.9 Auto-subtract a "land near NEUTRAL" margin from follow_mm
      so the FOLLOW move finishes with the buffer comfortably away
      from the armed extreme. After mass-balance during the move the
      buffer drifts back toward the armed end as MMU keeps draining
      past the extruder finish point; without a subtract the
      buffer-end position is at the switch click, one step away from
      the mechanical hard end. Subtract `BUF_MAX_TRAVEL_MM / 2`
      (operator-stated value — leaves buffer near NEUTRAL center,
      not at the TENSION switch click). Edge case: if `follow_mm <
      BUF_MAX_TRAVEL_MM / 2` the result goes ≤ 0 → disable the
      follow-on entirely (passive lock only; macro probably doesn't
      need a FOLLOW for such small moves anyway).
- [x] 11.10 BS must always force a buffer stabilize. Symptom: after
      `BL:T:<dist>:<rate>` + extruder retract + BS, the filament
      sometimes parks at the TENSION switch and stays there
      indefinitely — sync never re-engages because the BL auto-start
      suppression flag is set on release and only clears when raw
      physically departs TENSION. Fix: BS now calls
      `sync_retract_assist_set(false)` +
      `sync_bl_clear_autostart_suppress()` + the regular
      `buffer_stabilize_request()` (commit pending after this task
      write).
