## Context

Two type-P terminal-fault paths run at the top of `sync_tick()` while
`sync_enabled`:
- Saturation: `g_buf_pos <= -0.99` for `PSF_WALL_SAT_MS` → `fault_hold`
  (tension), `>= +0.99` → `relief_pause` (compression).
- Tension dwell: `s == BUF_TENSION && sync_tension_pin_since_ms != 0` and
  `now - pin_since >= SYNC_TENSION_DWELL_STOP_MS` → `fault_hold`.

Both timers are written by code that runs regardless of sync state:
`sync_tension_pin_since_ms` in `sync_on_transition()` (`sync.c:1931`),
`g_buf_analog_saturated_since_ms` in `buf_sensor_tick()`. So they can hold values
accumulated before sync was active.

## Decisions

### D1 — Tension-dwell timer is meaningful only during active sync

The dwell-to-fault measures "how long has the buffer been pinned tension *while we
were trying to feed*." Idle dwell is irrelevant — the MMU wasn't feeding. So
restart `sync_tension_pin_since_ms` on activation:
`= (g_buf.state == BUF_TENSION) ? now_ms : 0`. If already in tension, the dwell
counts from engagement (a genuine immediate-starve still faults after the window);
if not, the next NEUTRAL→TENSION transition sets it normally.

Applied at all three type-P activation sites: normal auto-start, relief-pause
re-arm, fault-hold recovery. (The type-D relief-rearm in `sync_on_transition` is
not a type-P path and is left alone.)

*Alternative rejected*: gate the timer *set* (`sync.c:1931`) on `sync_enabled`.
Then a buffer already in tension at activation has `pin_since == 0` and never
faults even on a real starve — it loses the protection. Restarting at activation
keeps it.

### D2 — Saturation timer must reset on fault recovery

`FAULT_HOLD_RECOVERY` re-enters ACTIVE, but the buffer is typically still pinned
at the rail (the extruder pulled through the hold). `g_buf_analog_saturated_since_ms`
still holds the pre-hold timestamp, so `now - since >> PSF_WALL_SAT_MS` and the
saturation check re-faults immediately → infinite loop. Reset it to `0` on
recovery; `buf_sensor_tick()` re-arms it from `now` if the buffer is still
saturated, giving a fresh full window for the refill snap to relieve the rail.

## Risks / Trade-offs

- **A genuine immediate starve** (extruder out-pulls MMU max from the first tick)
  now faults after `SYNC_TENSION_DWELL_STOP_MS` / `PSF_WALL_SAT_MS` from
  activation rather than instantly — correct: it gives the refill snap a chance
  first, and still terminal-faults if truly unrecoverable.
- **Repeated recovery** of a true jam now cycles on the order of
  `SYNC_FAULT_HOLD_RECOVERY_MS + window` instead of thrashing — acceptable, and
  each cycle is a real refill attempt.

## Open Questions

- Residual wide hunting (`−0.84 ↔ +0.79`) and end-of-burst compression overshoot
  remain — a feed-tracking *quality* issue (snap-to-max overshoot, KD_PSF=0), not
  a fault. Tracked separately; candidates: moderate the snap target or enable a
  small Kd.
