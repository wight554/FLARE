## Why

`flare_daemon.py` mirror loop POSTs `SET_MMU` deltas to Moonraker `/printer/gcode/script`
every 0.25s. gcode/script takes Klipper gcode lock. While long blocking command holds lock
(e.g. `MPC_CALIBRATE`, ~15min), client times out at 1.0s but Moonraker queues every
forwarded request server-side regardless. Requests pile in klippy gcode queue
(`Request 'gcode/script' pending: 60..840 seconds`) until Kalico crashes:
`RecursionError: maximum recursion depth exceeded` in `webhooks._do_query` `json.dumps`.
Root = Kalico greenlet defect, but daemon traffic is trigger. Fix our side so daemon never
queues telemetry behind blocking command.

## What Changes

- Add host-busy backpressure to mirror push loop. On gcode/script timeout, treat as
  "gcode lock busy": stop emitting all gcode/script pushes (`SET_MMU`, `MMU_GATE_MAP`,
  `_FLARE_SYNC_BOARD`).
- While busy, poll lock-free `objects/query {idle_timeout}` until
  `idle_timeout.state` returns Idle/Ready, then resume pushes.
- Distinguish busy (lock held, host alive) from offline (Moonraker down). Offline keeps
  existing 5s backoff. Busy = suppress + probe, no blind retry that re-queues.
- `MMU_GATE_MAP` (`_push_gate_map_to_klipper`) + `_FLARE_SYNC_BOARD` gated by same busy
  flag; they share the lock.
- No new deps; pure stdlib. No firmware change.

## Capabilities

### New Capabilities
<!-- none -->

### Modified Capabilities
- `daemon-klipper-mirror`: add requirement — mirror push SHALL apply host-busy
  backpressure (suppress gcode/script pushes while gcode lock busy; resume on idle via
  lock-free probe) so daemon never piles requests behind blocking command. Existing delta /
  full-resync / diagnostics requirements unchanged.

## Impact

- `scripts/flare_daemon.py`: mirror loop (~L1088-1360), `_push_gate_map_to_klipper` (~L264).
- New lock-free probe helper for `idle_timeout` via `objects/query`.
- Behavior: during blocking host command, mirror freezes (acceptable — UI telemetry stale,
  not safety-critical) then reconciles via existing full-resync path on resume.
- Related: [[daemon-setmmu-delta-push]] (gcode-lock starves mirror), `klipper-integration`.
