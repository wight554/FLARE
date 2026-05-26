## 1. HW Bench (gating)

- [ ] 1.1 Measure MMU sustained top speed on rig (target ≥ 110 mm/s = 6600 mm/min) and document the figure.
- [ ] 1.2 Characterize instant-slam practical accel (0 → N sps without lost steps) and record the safe slam ceiling per lane.
- [ ] 1.3 Confirm operator's hard upper bound for `park_speed` if HW ceiling is below the worst-case envelope from `design.md` §D6.

## 2. Firmware: Lifecycle + Catch Engine

- [x] 2.1 Repurpose `SYNC_RETRACT_ASSIST` state in `firmware/src/sync.c` to host the buffer-lock lifecycle (prime / locked / catch / settle sub-states), preserving the "learning paused, estimator preserved" contract.
- [x] 2.2 Restore an aggressive slam drive helper (analogue of the deleted `retract_assist_drive` from commit `f19f41a`) that writes `current_sps = target` directly, bypassing `SYNC_RAMP_UP_SPS`.
- [x] 2.3 Wire the sync tick to detect raw `BUF_*` departure from the armed extreme while in the locked sub-state and transition to the catch sub-state on the same tick (no hyst debounce on the break edge).
- [x] 2.4 Implement the locked-state watchdog (default 30s) that auto-releases and emits `EV:BL:TIMEOUT`.

## 3. Firmware: Prime Move

- [x] 3.1 Add a sync-owned prime drive that retracts (or feeds) the active lane until raw target state OR `BUF_MAX_TRAVEL_MM / 2` of MMU travel, whichever comes first.
- [x] 3.2 Emit `EV:BL:PRIME_BOUND` when the half-travel cap stops the prime before target raw state.
- [x] 3.3 Verify prime runs without engaging the `TASK_MOVE` fault guards (it is sync-owned, not an `MV` task).

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
- [ ] 6.4 Remove the `mmu_tip_retract` variable computation once the flag is the only path.

## 7. Acceptance Validation

- [x] 7.1 Add a `scripts/flare_sync_check.py` mode (e.g. `--mode buffer-lock`) that asserts: `BL:T` → `OK`, lifecycle transitions through prime/locked/catch/settle, and no `FAULT:MOVE_*` during the catch.
- [ ] 7.2 Verify on bench: tip-forming sequence end-to-end, no buffer slam, peak excursion under the design envelope (D6).
- [ ] 7.2a Bench the rig-validated operator envelope (D6: 40 mm @ 150 mm/s extruder retract with `ramp_step_rate=1000`, MMU cap 6000 mm/min). Confirm peak buffer excursion ≤ ~17 mm (≤ 20 mm runway) and that lock-break uses the raw edge, not the hyst-debounced state. If observed excursion exceeds 18 mm, revisit `BUF_HYST_MS` handling on the break edge or tighten the retract speed/distance bound.
- [ ] 7.3 Verify on bench: unload-toolhead sequence end-to-end with the gear retract guarded by `BL`, no buffer slam.
- [ ] 7.4 Verify watchdog timeout fires when `BL:T` is armed and no break/`BS` arrives.

## 8. Cleanup + Archive

- [ ] 8.1 Remove the `:I` ignore-buffer branch from `_FLARE_TIP_FORMING` after a release window confirms no regressions.
- [ ] 8.2 Drop the `variable_use_buffer_lock` flag once `BL` is the only path.
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
- [ ] 9.10 Bench `sync_ramp_accel = 150 mm/s²` on rig: run a long sync soak with mixed flow (low + high extrusion rate, both directions). Use the automated detector: `python3 scripts/flare_sync_check.py --daemon --duration 120 --mode stability` (or `--live` if no daemon). PASS = peak BUF cycles/sec ≤ 1.0 over any 3s window; FAIL = ringing. On FAIL apply `SET:SYNC_KP_RATE:<old/k>` where `k = sync_ramp_accel / 33` (k ≈ 4.5 at default 150 → 900/4.5 ≈ 200) and re-soak until PASS.
- [ ] 9.11 If task 9.10 surfaces zone-bias overshoot in `NEUTRAL` recovery, apply `zone_bias_max ← zone_bias_max / √k` and re-soak.
- [ ] 9.12 Bench the two-extreme envelope from explore: 300 mm/min slow extrusion (5 mm/s) and 1500 mm/min fast (25 mm/s). Confirm 150 mm/s² sync slew keeps drift under ~1 mm at both ends; if drift unacceptable at the fast end, raise `sync_ramp_accel` toward 200–300 mm/s² and re-apply kp scaling.
- [ ] 9.13 Record the final per-rig tuned values (`global_max_accel`, `sync_ramp_accel`, `sync_kp_rate`, any `zone_bias_*` overrides) in a rig notes file or `BEHAVIOR.md` appendix.
