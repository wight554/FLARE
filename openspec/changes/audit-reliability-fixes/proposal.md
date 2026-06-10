# Proposal: audit-reliability-fixes

## Why

Architecture audit round 2 (2026-06-10, control-law/state-machine/safety layer — complements `audit-hardening-fixes`, which covered protocol/integration) found 3 high-severity defects: type-P runout RELOAD emits a false `RELOAD:LOADED` without moving filament (sign-convention flip leftover, proven by git archaeology), the dry-spin safety interlock is dead (sync restarts a faulted lane and the restart disables the watchdog that faulted it), and the firmware has no hardware watchdog while step generation is free-running PWM (a CPU hang grinds filament indefinitely). Plus 5 medium and 8 low findings: flash-write-during-BL-motion gate hole, estimator-state pollution, duplicated re-arm policy, soft-float waste, event/UX gaps, and dead knobs.

## What Changes

High (safety/correctness):

- **H1** `toolchange.c:412`: type-P RELOAD approach `contacted = (g_buf_pos < PSF_HOME_DEVIATION_THRESHOLD_NORM)` (+0.85) was written under the old +1=tension/home convention; migration commit `6799171` flipped only `sync.c` (motion.c flipped separately in `cf63cc1`, toolchange.c missed; refactor `d8d3a18` moved the stale line verbatim). Under the current convention (home = −1.0) the check is true while the arm is parked at home → APPROACH instant-passes, FOLLOW's success check (`g_buf.state == BUF_TENSION`, zone = pos < goal−0.1) is also true at rest → instant false `RELOAD:LOADED` + `set_toolhead_filament(true)` → air print. Affects auto-runout RELOAD (`RELOAD_MODE=1` default) and manual `RL:`. Fix: contact = compression-side using the already-flipped `PSF_LOAD_CONTACT_THRESHOLD_NORM` (+0.50, same semantics as the FL path), and type-P follow success = deep tension (`pos <= -PSF_HOME_DEVIATION_THRESHOLD_NORM`) for parity with the type-D TENSION *switch* (a physical extreme, not the shallow zone edge) — zone-edge success would still false-fire from old-tail drainage.
- **H2** `sync.c:1110-1124` (`sync_apply_to_active`): the final else branch drives the motor directly for a lane with `task=IDLE` + `fault!=FAULT_NONE` (e.g. `FAULT_DRY_SPIN`), contradicting BEHAVIOR.md's documented interlock. The restarted motor spins with `task==TASK_IDLE`, which disables the dry-spin watchdog re-fire condition (`task != TASK_IDLE`, motion.c:538) → indefinite dry grind. Adjacent hole: RUNOUT with `RELOAD_MODE=0` leaves sync ACTIVE, which `lane_start`s the empty lane again (clearing the fault each time). Fix: faulted lane → no drive from sync; decide sync policy on non-reload RUNOUT.
- **H3** No hardware watchdog anywhere in the tree; steps are hardware PWM that free-runs through any CPU hang. Add RP2040 `watchdog_enable` + `watchdog_update` in the superloop so a wedge resets to a safe state (PWM disabled at boot init).

Medium:

- **M1** Buffer-lock (BL) PRIME/FOLLOW moves the motor with `lane->task == TASK_IDLE`, so `controller_activity_in_progress()` (protocol.c:204) accepts `SV:`/`LD:`/`RS:`/`CAL:` mid-motion → flash erase stalls the core while PWM steps (persistence-contract violation). Compounding: `sync_buffer_lock_follow` dt clamp (sync.c:926-929) maps dt>100 ms to a 1 ms *fallback* instead of saturating → distance undercount across stalls → follow overshoots its armed budget. Fix: add `sync_buffer_lock_motor_moving()` to the activity gate; clamp dt at max.
- **M2** Variance blend (sync.c:1588-1595) writes the blended position back into `g_buf_pos` each tick — compounding pull of the physical model toward its own setpoint. Default OFF but a landmine. Fix: read-path transform (local `bp_eff` style, like drift correction).
- **M3** FAULT_HOLD recovery pokes `g_buf.state`/`entered_ms` directly (sync.c:1242-1243), bypassing `buf_update` bookkeeping; RELIEF_PAUSE re-arm policy duplicated in two places (sync_buf.c:837-846 and sync.c:1271-1293); dead ternary sync.c:1254. Consolidate.
- **M4** Type-P `buf_state_raw()` re-derives `psf_goal_norm()` (2 soft-float divides) every main-loop pass (~10 kHz); `buf_threshold_mm`/`buf_physical_half_travel_mm`/`buf_target_reserve_mm` recomputed 5-10× per sync tick. Cache on settings-change/per-tick. (Explicit non-goal: fixed-point conversion — measured headroom makes it unwarranted.)
- **M5** `UNLOAD_TIMEOUT` event emitted with NULL payload (motion.c:368) — only family member without a lane id; UM/UL sequence has no terminal failure event of its own (cutter-fail → only `CUT:ERROR`; OUT-still-present reset is silent at sequence level). Add lane payload + terminal `UNLOAD_FAULT`-class event.

Low (cleanups):

- **L1** BL prime applies SYNC_MAX with no ramp (sync.c:812; ~12% margin to the known ~2500 mm/min pull-in stall; a raised SYNC_MAX_RATE stalls silently and the time-based travel estimate then locks at a wrong position). Ramp like FOLLOW.
- **L2** Dead prime phase-2 machinery: `g_bl_prime_post_cap_mm` always 0 (sync.c:785) with comments still describing (max-span)/2 settle.
- **L3** `buf_analog_update` velocity uses nominal `g_sync_tick_ms` instead of actual elapsed (sync_buf.c:415-418) — up to ~25% vel error feeding D-term/jump-brake/stabilize-predict.
- **L4** Dead knob `BUF_HOME_STATE` (persisted + SET/GET, never read); dead constants `PSF_TENSION_PIN_NORM`, `PSF_UNLOAD_RELIEF_ARM_MS` (controller_shared.h:26-28).
- **L5** `RELOAD_WAIT_Y` with `RELOAD_Y_TIMEOUT_MS=0` + tail not yet cleared → `age > 0` instant `RELOAD_Y_TIMEOUT` (toolchange.c:394); timeout=0 should disable the Y gate, not insta-fail.
- **L6** CONTEXT.md says `SETTINGS_VERSION` 47; actual 59 (settings_store.c:24).
- **L7** `ER:BUSY` covers 5+ distinct causes; suffix the source (`ER:BUSY:TC` etc.).
- **L8** `g_buf_signal.age_ms` still computes ~0 every publish (stamped same pass, sync_buf.c:380/932 vs :909) — prior-audit F11 didn't land observably; dead branch toolchange.c:431.

## Capabilities

### New Capabilities

(none — all changes harden existing capabilities)

### Modified Capabilities

- `toolchange-orchestration`: type-P RELOAD contact MUST be compression-side (parity with `BUF_COMPRESSION`); type-P follow success MUST require deep tension or toolhead sensor, not the shallow zone edge; `RELOAD_Y_TIMEOUT_MS=0` MUST disable the Y gate without insta-failing tail-clear waits.
- `motion-safety`: sync MUST NOT drive a faulted lane; dry-spin protection MUST remain armed across sync restarts; firmware MUST run a hardware watchdog so a hung loop cannot leave step PWM free-running.
- `project-architecture`: activity gate for persistence MUST cover buffer-lock motor motion (task-less drive); unload-family terminal events MUST carry the lane id; a host-initiated unload sequence MUST emit a terminal success-or-failure event.

## Impact

- Firmware: `toolchange.c`, `sync.c`, `sync_buf.c`, `motion.c`, `protocol.c`, `controller_shared.h`, `main.c` (watchdog init). No `settings_t` layout change planned except `BUF_HOME_STATE` removal decision (if removed → `SETTINGS_VERSION` bump).
- Control laws: H1/H2 change RELOAD + fault-path behavior only; normal-print sync laws (relay, PSF) untouched.
- Host visibility: new/changed events (`UNLOAD_TIMEOUT:<lane>`, terminal unload failure event, optional `ER:BUSY:<src>` suffix — host regexes tolerate suffixes per wire-format tests, verify).
- Docs: BEHAVIOR.md (RELOAD type-P contact, dry-spin interlock wording), CONTEXT.md (SETTINGS_VERSION), MANUAL.md (events), TEST_CASES.md (type-P RELOAD hardware case).
- Hardware validation needed: H1 on a type-P rig (runout RELOAD + `RL:`), H2 dry-spin re-fire, L1 prime ramp at raised SYNC_MAX. H3/M1 verifiable by bench + build.
