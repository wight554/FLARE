## 1. HW Bench (gating)

- [ ] 1.1 Measure MMU sustained top speed on rig (target ≥ 110 mm/s = 6600 mm/min) and document the figure.
- [ ] 1.2 Characterize instant-slam practical accel (0 → N sps without lost steps) and record the safe slam ceiling per lane.
- [ ] 1.3 Confirm operator's hard upper bound for `park_speed` if HW ceiling is below the worst-case envelope from `design.md` §D6.

## 2. Firmware: Lifecycle + Catch Engine

- [ ] 2.1 Repurpose `SYNC_RETRACT_ASSIST` state in `firmware/src/sync.c` to host the buffer-lock lifecycle (prime / locked / catch / settle sub-states), preserving the "learning paused, estimator preserved" contract.
- [ ] 2.2 Restore an aggressive slam drive helper (analogue of the deleted `retract_assist_drive` from commit `f19f41a`) that writes `current_sps = target` directly, bypassing `SYNC_RAMP_UP_SPS`.
- [ ] 2.3 Wire the sync tick to detect raw `BUF_*` departure from the armed extreme while in the locked sub-state and transition to the catch sub-state on the same tick (no hyst debounce on the break edge).
- [ ] 2.4 Implement the locked-state watchdog (default 30s) that auto-releases and emits `EV:BL:TIMEOUT`.

## 3. Firmware: Prime Move

- [ ] 3.1 Add a sync-owned prime drive that retracts (or feeds) the active lane until raw target state OR `BUF_MAX_TRAVEL_MM / 2` of MMU travel, whichever comes first.
- [ ] 3.2 Emit `EV:BL:PRIME_BOUND` when the half-travel cap stops the prime before target raw state.
- [ ] 3.3 Verify prime runs without engaging the `TASK_MOVE` fault guards (it is sync-owned, not an `MV` task).

## 4. Firmware: Protocol Surface

- [ ] 4.1 Add `BL` command parser to `firmware/src/protocol.c` accepting `BL`, `BL:T`, `BL:C`; reject with `ER:BUSY` when sync is active or a non-idle task is running.
- [ ] 4.2 Add `BL` to the status GET output (`BL:T` / `BL:C` / `BL:0`).
- [ ] 4.3 Keep the legacy `RA:1` / `RA:0` host commands as aliases for `BL:T` / `BS` per the modified `sync-state-model` spec.
- [ ] 4.4 Ensure `BS` releases lock and catch immediately and returns to `SYNC_OFF` with a normal stabilization pass.

## 5. Firmware: MV Guard Interaction

- [ ] 5.1 Document and enforce the spec rule that `MV` buffer fault guards (`FAULT:MOVE_TENSION` / `FAULT:MOVE_COMPRESSION`) are suppressed only while in `SYNC_RETRACT_ASSIST` catch sub-state; outside it they remain active unchanged.
- [ ] 5.2 Add a sanity assert / event if a `TASK_MOVE` ever starts while the catch is active (should be unreachable by the protocol layer).

## 6. Klipper Macros

- [ ] 6.1 Add `variable_use_buffer_lock: 0` default flag to `_FLARE_VARS` (or equivalent) for staged rollout.
- [ ] 6.2 When the flag is `1`: in `_FLARE_TIP_FORMING`, replace the blind `RUN_SHELL_COMMAND CMD=flare PARAMS="MV:-{mmu_tip_retract}:{park_speed*60*0.2}:I"` with `BL:T` before the park retract and `BS` after the post-park `M400`.
- [ ] 6.3 When the flag is `1`: in `_FLARE_UNLOAD_TOOLHEAD`, wrap the `G1 E-{gear_retract}` move with `BL:T` before and `BS` after, both gated by `M400` for ordering.
- [ ] 6.4 Remove the `mmu_tip_retract` variable computation once the flag is the only path.

## 7. Acceptance Validation

- [ ] 7.1 Add a `scripts/flare_sync_check.py` mode (e.g. `--mode buffer-lock`) that asserts: `BL:T` → `OK`, lifecycle transitions through prime/locked/catch/settle, and no `FAULT:MOVE_*` during the catch.
- [ ] 7.2 Verify on bench: tip-forming sequence end-to-end, no buffer slam, peak excursion under the design envelope (D6).
- [ ] 7.3 Verify on bench: unload-toolhead sequence end-to-end with the gear retract guarded by `BL`, no buffer slam.
- [ ] 7.4 Verify watchdog timeout fires when `BL:T` is armed and no break/`BS` arrives.

## 8. Cleanup + Archive

- [ ] 8.1 Remove the `:I` ignore-buffer branch from `_FLARE_TIP_FORMING` after a release window confirms no regressions.
- [ ] 8.2 Drop the `variable_use_buffer_lock` flag once `BL` is the only path.
- [ ] 8.3 Update `BEHAVIOR.md` / `KLIPPER.md` to describe `BL` and the buffer-lock lifecycle; remove references to the legacy blind retract.
- [ ] 8.4 Archive this change with `openspec archive buffer-state-lock`.
