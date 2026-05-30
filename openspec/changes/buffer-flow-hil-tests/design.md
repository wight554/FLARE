## Context

Firmware buffer flows are untested except by hand. The firmware is a Pico
cross-build (no host target) and exposes no sensor/position injection command,
so a fully deterministic host harness would require firmware changes. Events,
however, are fully observable: every flow transition emits an `EV:<type>:<data>`
line, and `flare_daemon` already centralizes serial I/O and re-exposes events
(`/telemetry` SSE, `/status`) and command send (`POST /cmd`).

## Goals / Non-Goals

**Goals**
- Repeatable, scripted coverage of all five buffer flows on real hardware.
- Reuse the daemon as the single serial owner — no port contention.
- Assert real `EV:` events (and command `OK:`/`ER:` replies) per flow.
- Unit-test the harness's pure logic so it does not regress (CI, no hardware).
- Type-P focus; type-D parity where the hardware supports it.

**Non-Goals**
- No firmware changes (no sim/inject seam — operator moves the buffer).
- Not CI for the flows themselves (they need a board + operator + daemon).
- No closed-loop physics model.
- No new daemon endpoints.

## Decisions

### Transport: daemon HTTP, not direct serial
The daemon owns the serial port. A second reader would conflict and miss the
daemon's parsing. The harness therefore: sends via `POST /cmd` (returns the
board's `OK:`/`ER:`), captures events via the `/telemetry` SSE stream on a
background thread, and reads snapshots via `/status`. This matches the existing
`flare_cmd.py` daemon path and the project rule to read daemon telemetry rather
than poll the board mid-command.

### Event matching: substring/regex on the reconstructed payload
Firmware emits `EV:<type>:<data>` (colon); the daemon comma-splits, so its
`event_type` carries the whole colon-delimited token (e.g. `SYNC:RELIEF_PAUSE`,
`BUF_STAB:DONE`, `BL:LOCKED`). The harness reconstructs `type[:data]` and
matches by substring (or regex), which is robust for every buffer event,
including types that themselves contain colons (`TC:ERROR`, `RELOAD:LOADED`,
`FAULT:MOVE_*`). Pure functions `parse_event` (raw line) and `event_from_sse`
(SSE dict) are isolated for unit testing.

### Assertion model: wait / refute, time-windowed
`wait_event(needle, timeout, since)` blocks for a matching event (optionally
only events after a snapshot time, to ignore stale ones without clearing).
`refute_event(needle, window)` asserts no match appears within a window — used
for negative cases (healthy unload must NOT `UNLOAD_BLOCKED`; D18 gate must NOT
spuriously `AUTO_START`; D23 unloaded stabilize must NOT `BUF_STAB:START`).

### Operator-assisted, not injected
Each case prompts the operator to move the buffer / stage filament, then drives
the command and asserts events. This avoids any firmware change at the cost of a
human at the bench. Safety: reduced first-motion speeds (`safe_speeds`), `ST:`
on close/abort, `SM:0` + `ST:` reset between cases.

### Scope clarifications
- **load = `FL` only.** Preload/autoload (`LO`) does not engage the buffer, so
  it is out of buffer-flow scope.
- **Auto-sync toggle (D18)** is a sync-flow concern (`SET:AUTO_MODE:1` →
  `SYNC:AUTO_START`, gated by transition + `g_buf_pos > 0.6`), with a negative
  case for the gate.
- **Type-agnostic harness → separate change.** Because the harness drives both
  D and P (`SET:BUF_SENSOR`), this is its own change, not folded into the
  type-P `psf-analog-rig`. Type-P cases are primary; type-D parity cases are
  tagged and only run under `--type d`.

## Risks / Trade-offs

- **Operator dependence** → not unattended/CI for the flows. Mitigated by
  unit-testing the harness logic and keeping prompts explicit.
- **Timeouts are bench-dependent** (bowden length, motor speed) → load/lock
  timeouts are generous and adjustable; tune per rig.
- **No event for internal steps** (relief jog, soft-wall ramp emit no `EV:`) →
  those are asserted only via their terminal events (`UNLOAD_BLOCKED`,
  `FAULT_HOLD`); finer observation would need the `BS` telemetry or a future
  firmware sim seam.
