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

**D2 — H1 follow success: tension-zone crossing gated past the touch-settle window, or toolhead sensor; keep LOAD_MAX distance fallback.**
Rejected alternative: deep-position threshold (`pos <= -0.90` or `-0.60`) — unreachable by construction. The follow law's own TENSION response refills at `JOIN_SPS` (≈26.7 mm/s) the moment the zone edge (+0.3) is crossed, versus 2-10 mm/s extruder draw, so the arm never reaches deeper thresholds; every reload would exit via the LOAD_MAX fallback (3000 mm blind overfeed). The reason type-D works is structural, not depth: its TENSION switch is simultaneously the success trigger and the refill trigger, and the success check runs first in the tick — success *preempts* the refill on the same signal. Type-P parity = keep success at the zone-edge crossing (the law's own tension trigger) and gate out the known false-fire windows instead: (a) at-rest false fire — eliminated by the D1 approach fix (entry requires pos > +0.50); (b) follow-entry dip — the first `RELOAD_TOUCH_SETTLE_MS + RELOAD_TOUCH_BOOST_MS` (~1 s) feeds at/near `COMPRESSION_SPS` while the extruder pulls, transiently dipping below the zone edge, so success is gated on `follow_age_ms` past that window (the same window the law already special-cases). Post-settle the feed floor is `est×RELOAD_LEAN` ≥ `PRESS_SPS` (20 mm/s, compression-biased by design), so a sustained tension-edge crossing means draw persistently beat the overfeed = gear grab. Residual false positive (estimator badly low) is benign post-D1: contact already verified, filament at the extruder mouth, normal sync continues the push. Toolhead sensor accepted any time; type-D branch unchanged.

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

**D10 — H4 follow completion forks on consumer presence (`g_extruder_est_sps`), not on a fixed distance.**
Type-P hardware validation of D2 (2026-06-18) exposed the gap: D2's tension-crossing success and the compression-jam timeout both presuppose an extruder drawing the old tail. On a paused print / manual `RL:` retrigger / bench the extruder is idle, the buffer pegs `+1.0` COMPRESSION, TENSION is unreachable, and the "stuck in compression" timeout (`toolchange.c` follow check) false-fires `FOLLOW_JAM`. Physically correct read, incomplete completion model.
Rejected alternatives: (a) complete on the existing `LOAD_MAX` distance fallback — rejected: with no consumer the buffer is mechanically full, further feed just grinds in place without advancing filament to the toolhead, so a distance target is meaningless and the jam timeout fires long before `LOAD_MAX` anyway; (b) physically, with no drain there is nowhere for filament to go past the extruder mouth, so "loaded" can only mean *staged* there. The buffer is the staging signal: sustained COMPRESSION after contact = the new filament parked at the extruder, ready for grab when the print resumes. So the completion model is the mirror of the consumer case — consumer → success on TENSION (grabbed); no consumer → success on held COMPRESSION (staged). Discriminator is the estimator (`g_extruder_est_sps > RELOAD_CONSUMER_MIN_SPS`, 80 sps ≈ 0.5 mm/s draw), re-checked per tick so a mid-follow resume reverts to the tension rule. Jam detection is suppressed only while no consumer is present; the absolute follow timeout still backstops a genuinely wedged feed. Safe because FOLLOW is only entered after APPROACH verified compression contact (`> +0.5`) — an upstream jam that prevents reaching the extruder is caught by the APPROACH timeout, not here.
Companion (`RL:` resume): the manual command ran `tc_manual_reload` (fixed same-lane approach→follow), divergent from the auto path `reload_trigger` (swap + WAIT_Y). Per the intent "RL = retrigger auto from where it failed," `tc_manual_reload` now branches on physical state: active lane empty + other loaded → `reload_trigger(active)` (the swap never completed); else resume on the active lane (already-swapped fresh lane, or never ran out). The `RL:` protocol handler relaxes its `NO_FILAMENT` reject to permit the active-empty + other-present swap case (which bypasses the same-lane `OTHER_LANE_ACTIVE` guards that do not apply to a swap); `reload_trigger` owns the other-lane presence validation.

## Risks / Trade-offs

- [H1 settle-window gate may delay success on a rig whose grab happens inside the first ~1 s of follow] → toolhead sensor still completes any time; the next post-window tension touch completes one bang-bang cycle later (~hundreds of ms); LOAD_MAX fallback bounds the phase; hardware validation task gates the merge.
- [H4 no-consumer mode declares LOADED on staged compression without a real grab — `set_toolhead_filament(true)` while the filament is only parked at the extruder mouth] → that is the intended semantics for a paused/retrigger load (the lane is loaded, awaiting grab on resume); a toolhead sensor, when present, still gates real presence; on resume the normal sync grab pulls it in. Risk if `RELOAD_CONSUMER_MIN_SPS` is set so a slow real draw reads as "no consumer" → staged completion one bang-bang cycle early; benign post-contact (filament already at the extruder mouth). Hardware validation (10.3) covers paused + bench.
- [H4 `RL:` resume mis-routes the swap vs same-lane decision on a transient sensor state] → decision keys on the stable IN-present read used everywhere else; the swap path delegates to `reload_trigger`, which re-validates the other lane and aborts `NO_FILAMENT` on its own; worst case is the pre-existing same-lane behavior.
- [H2 sync-disable on non-reload RUNOUT changes long-standing (buggy) behavior hosts might accidentally rely on] → RUNOUT event still emitted; auto-start re-engages on TENSION after a real load; document in BEHAVIOR.md.
- [H3 watchdog could reset during an unforeseen legitimate stall >1 s] → only such stall found is BOOTSEL (`sleep_ms(100)` + reset — fine); flash ops measured well under budget; pause_on_debug protects bench debugging.
- [M4 caching introduces staleness if a new SET path forgets to refresh] → refresh called from the shared settings-apply chokepoints, not per-command; parity test extended to assert refresh on every geometry-affecting SET.
- [Event additions could break strict host parsers] → additive only; wire-format test covers new shapes; daemon two-part whitelist updated (lesson from prior-audit S4).

## Open Questions

- `BUF_HOME_STATE` (L4): delete field (SETTINGS_VERSION bump) or keep as reserved? Leaning delete-with-bump since round-trip parity test flags dead fields anyway.
- H2 RUNOUT policy for `RELOAD_MODE=1` + reload_trigger NO_FILAMENT abort path: reload_trigger already calls `sync_disable(true)` — confirm no double-disable side effects.
