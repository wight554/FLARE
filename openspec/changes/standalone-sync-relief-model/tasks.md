## 1. Phase A — Disciplined learner + warn-only counters (no control change)

- [ ] 1.1 Add config tunables for disciplined live learner: consecutive-settle count, variance reject fraction, cooldown ms, cooldown commanded-MMU mm (config.ini + generated tune.h)
- [ ] 1.2 Gate `baseline_update_on_settle` trigger (`sync.c:835`) to active normal control only: not fast-brake, not trailing-recovery, not hold/relief/fault
- [ ] 1.3 Implement multi-cycle + variance + cooldown acceptance before moving `g_baseline_sps`; keep `max()` up-only and non-persistent
- [ ] 1.4 Add commanded-MMU mm relief-effort accumulators (refill on ADVANCE, relieve on TRAILING) with reset on state change
- [ ] 1.5 Emit warn-only rate-limited `SYNC cannot_refill` / `SYNC cannot_relieve` events on effort thresholds; no control derived from counters
- [ ] 1.6 Local build `ninja -C build_local`; confirm SYNC_ACTIVE control output unchanged

## 2. Phase B — Explicit state model

- [ ] 2.1 Add `sync_state_t` enum + single state global (`main.c`/`sync.h`); derive existing `sync_enabled`/`sync_auto_started`/`g_sync_hold` from state
- [ ] 2.2 Add `sync_relief_pause()` and fault-hold entry helpers that preserve estimator/drift/sigma/integrators (do NOT call destructive `sync_disable`)
- [ ] 2.3 Reclassify callers per design D1 table: keep `sync_disable(true)` for OFF only (protocol toolchange/TS/unload, `motion.c:455` tail finish, tail-assist auto-stop `sync.c:943`)
- [ ] 2.4 Continuous-trailing terminal (`sync.c:1295`) → `SYNC_RELIEF_PAUSE`; resume to `SYNC_ACTIVE` on `BUF_ADVANCE`, reuse preserved estimator unless stale per `SYNC_EST_FRESH_MS`
- [ ] 2.5 Hard-wall critical (`sync.c:1245`) and advance-dwell stop (`sync.c:1195`) → `SYNC_FAULT_HOLD`; motor stop; auto conservative recovery after configured stable interval
- [ ] 2.6 HD command (`protocol.c:506`) → `SYNC_HOLD`: closed loop off, buffer-stabilize-to-MID retained, state preserved, learning paused; clearing HD exits cleanly
- [ ] 2.7 Suppress `mid_creep` in HOLD/RELIEF_PAUSE/FAULT_HOLD; keep in ACTIVE
- [ ] 2.8 Enforce ADVANCE-never-paused invariant in state transitions
- [ ] 2.9 Add FAULT_HOLD recovery-interval config tunable

## 3. Diagnostics + status

- [ ] 3.1 Expose current sync state in status output (`protocol.c` status line + GET param)
- [ ] 3.2 Expose relief-effort counters in diagnostics for offline analyzer consumption

## 4. Validation

- [ ] 4.1 Behavior-parity review: SYNC_ACTIVE reserve target / reserve_correction / zone_bias / soft-wall trim / collapse ramp byte-equivalent to pre-change
- [ ] 4.2 Assert no Klipper/host speed/delta/encoder coupling introduced
- [ ] 4.3 Scenario notes: overfull→RELIEF_PAUSE→ADVANCE re-arm preserves estimator; hard-wall→FAULT_HOLD→auto recovery; HD→HOLD→clear; learner does not move during abnormal states
- [ ] 4.4 Confirm live baseline still resets to config authority on reboot/settings reload (non-persistent)
- [ ] 4.5 Final `ninja -C build_local`; update BEHAVIOR.md / KLIPPER notes if state names are operator-visible
