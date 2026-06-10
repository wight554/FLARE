# Design: audit-reliability-fixes

## Context

Audit round 2 traced the control/state-machine layer end-to-end. Three high findings share a root pattern: invariants enforced in one module silently bypassed in another (sign convention flipped in sync.c but not toolchange.c; fault interlock documented in BEHAVIOR.md but absent from `sync_apply_to_active`; "never write flash mid-motion" gated on lane tasks while BL drives the motor task-less). Fixes are surgical; no normal-print control law changes.

## Goals / Non-Goals

**Goals:**

- Type-P RELOAD detects real contact and real extruder grab (H1).
- A faulted lane is never driven by background controllers; dry-spin watchdog stays armed (H2).
- A hung CPU cannot leave step PWM free-running (H3).
- Flash writes blocked during all motor motion, including BL (M1).
- Estimator state never mutated by control-law read paths (M2).
- Event stream lets a host attribute every unload outcome (M5).

**Non-Goals:**

- Fixed-point math conversion (measured: 20 ms tick uses ~3% budget; loop idles 100 µs/pass — soft-float is not a bottleneck).
- Relay / PSF control-law tuning (tracked separately: `psf-feed-quality`).
- Type-P hunting / BS no-op investigation (same).

## Decisions

**D1 — H1 contact threshold: reuse `PSF_LOAD_CONTACT_THRESHOLD_NORM` (+0.50), not a flipped home-deviation check.**
Alternatives: (a) flip to `pos > -0.85` ("off home") — rejected: −0.85 is still inside the control TENSION zone (< goal−0.1 ≈ +0.3), so FOLLOW's instant-success hole survives; (b) velocity-based contact — rejected: PSF velocity carries the L3 dt error and is noisier than position. (+0.50) is convention-correct since `cf63cc1`, hardware-validated by the FL load-contact path, and matches the spec's own contact definition (`BUF_COMPRESSION` parity). Side effect: `PSF_HOME_DEVIATION_THRESHOLD_NORM` loses its only reference → delete (L4 bundles the other dead constants).

**D2 — H1 follow success: deep tension (`pos <= -PSF_HOME_THRESHOLD_NORM`, 0.90) or toolhead sensor; keep LOAD_MAX distance fallback.**
Type-D success = TENSION *switch* = physical extreme. The type-P zone edge (pos < +0.3) is far shallower and old-tail drainage crosses it, so zone-edge success false-fires even after a real contact. Deep tension is the analog of the switch: only an extruder grab (or a genuine starve, which also means "stop feeding and finish") yanks the arm near home. Uses the existing 0.90 lock constant rather than introducing a new knob. Raw-vs-debounced: keep checking both `g_buf_pos` (continuous) and toolhead — no debounced-zone dependency left in the type-P branch.

**D3 — H2 enforcement point: gate inside `sync_apply_to_active`, not at call sites.**
One owner: `if (lane->fault != FAULT_NONE) { stop feed if running; return; }` before any drive branch. Call-site gating (sync_tick, buf_update re-arm, relief re-arm) would need 4+ copies and rot. The TC_RELOAD_FOLLOW driver already gates on `fault == FAULT_NONE` for `lane_start` but has the same else-branch hole (toolchange.c:561-567) — apply the same guard there. Non-reload RUNOUT policy: disable sync (`sync_disable(true)`) when RUNOUT fires on the active lane and reload does not trigger — an empty lane has nothing to feed; auto-start re-engages on the next real load. Alternative (leave sync armed for re-insert) rejected: that path is what produces the empty-grind loop.

**D4 — H3 watchdog: 1 s timeout, fed once per superloop pass, enabled after boot init completes.**
Longest legitimate stall = settings flash sector erase+program (~50-100 ms) + USB edge cases; 1 s gives 10× margin while still bounding a wedge to one second of grind. Enable after `settings_load()`/`settle_boot_sensors()` so a slow boot can't trip it. `watchdog_enable(1000, /*pause_on_debug=*/1)` keeps debugger sessions usable. Reset path is inherently safe: `motor_init` leaves PWM disabled and EN inactive at boot.

**D5 — M1: gate = existing `sync_buffer_lock_motor_moving()` helper added to `controller_activity_in_progress()`.**
Catches PRIME and FOLLOW (the moving sub-states) while still allowing SV/LD/RS during BL_LOCKED (motor energized but static — flash stall is harmless to a holding stepper). dt clamp fix: `dt_s > BL_FOLLOW_DT_MAX_S → dt_s = BL_FOLLOW_DT_MAX_S` (saturate, don't collapse to 1 ms).

**D6 — M2: variance blend becomes a read-path transform.**
Compute the blend into the local effective position consumed by the control law (same pattern as `sync_apply_drift_correction`); never assign back to `g_buf_pos`. `g_buf_pos_raw_status` keeps exporting the raw model.

**D7 — M3: FAULT_HOLD recovery and RELIEF re-arm route through one helper.**
Extract `sync_rearm_active(lane, now_ms, reseed_pos)` used by the three current re-arm sites (FAULT_HOLD recovery, RELIEF tick re-arm, RELIEF transition re-arm in `buf_update`). State forcing goes through `buf_force_stable_state` instead of direct `g_buf.state` writes so tracker bookkeeping stays consistent.

**D8 — M4: cache pure-config geometry/goal helpers.**
`psf_goal_norm`, `buf_threshold_mm`, `buf_physical_half_travel_mm` become cached values recomputed by a `buf_geometry_refresh()` invoked from the existing settings apply paths (SET handlers, settings_load/defaults, BL goal-override changes). `buf_target_reserve_mm` stays live (depends on `g_extruder_est_sps`) but its constant sub-terms come from the cache. No behavior change — bitwise-identical outputs expected; verify with status-field spot checks.

**D9 — M5 events: `UNLOAD_TIMEOUT:<lane>`, plus terminal `EV:UNLOAD:FAULT:<reason>` from the manual-unload state machine.**
Reasons: `CUT_FAILED`, `OUT_BLOCKED`. Emitted via `cmd_event_critical` (fault-class, budget-exempt per prior audit F6). `ER:BUSY:<src>` suffix (L7): suffix only — wire-format tests updated to accept both; daemon/tuner regexes verified tolerant before merge.

## Risks / Trade-offs

- [H1 deep-tension success may trigger late on rigs whose extruder grab barely moves the arm] → LOAD_MAX distance fallback still completes the phase; threshold reuses the proven 0.90 lock boundary; hardware validation task gates the merge.
- [H2 sync-disable on non-reload RUNOUT changes long-standing (buggy) behavior hosts might accidentally rely on] → RUNOUT event still emitted; auto-start re-engages on TENSION after a real load; document in BEHAVIOR.md.
- [H3 watchdog could reset during an unforeseen legitimate stall >1 s] → only such stall found is BOOTSEL (`sleep_ms(100)` + reset — fine); flash ops measured well under budget; pause_on_debug protects bench debugging.
- [M4 caching introduces staleness if a new SET path forgets to refresh] → refresh called from the shared settings-apply chokepoints, not per-command; parity test extended to assert refresh on every geometry-affecting SET.
- [Event additions could break strict host parsers] → additive only; wire-format test covers new shapes; daemon two-part whitelist updated (lesson from prior-audit S4).

## Open Questions

- `BUF_HOME_STATE` (L4): delete field (SETTINGS_VERSION bump) or keep as reserved? Leaning delete-with-bump since round-trip parity test flags dead fields anyway.
- H2 RUNOUT policy for `RELOAD_MODE=1` + reload_trigger NO_FILAMENT abort path: reload_trigger already calls `sync_disable(true)` — confirm no double-disable side effects.
