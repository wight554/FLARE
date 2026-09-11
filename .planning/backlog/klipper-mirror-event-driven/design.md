## Context

Mirror path today: firmware status (serial, polled by `flare_cmd.py`) → daemon
state → 4 Hz background thread (`flare_daemon.py:1103`) → delta `SET_MMU` →
`mmu.py` `cmd_SET_MMU` → `get_status` → Fluidd/Mainsail MMU panel. The delta
already drops unchanged fields (`daemon-klipper-mirror` "Delta SET_MMU mirror
push"), but two field families change every tick:

- `sync_feedback` (float, ~-1..+1 buffer offset) — drives the cosmetic piston
  visualizer; oscillates constantly under sync.
- synthetic "Filament: X mm" — computed in `get_status` from `bowden_length` +
  `extruder_to_nozzle` (now-removed cfg vars; falls back to 1800/125), animated
  during load/unload phases.

So nearly every 250 ms tick has a non-empty delta → constant `SET_MMU`.

`filament_pos` is derived in `get_status` instantaneously: `10` at toolhead,
`4` if past gear/in a load/unload phase, else `0`. The UI renders checkpoints
from it. A fast load reports `10` before any `4` is observed at a tick boundary
→ Gate checkpoint never lights.

## Decisions

- **Discrete tracked field set.** Mirror only fields the UI shows as state, not
  drawings: `gate_status`, `filament_pos`/checkpoints, `sync_feedback_state`
  (buffer state enum), `action`, `print_state`, sensor flags, `active_gate`,
  `tool`, `num_toolchanges`. Drop `sync_feedback` (float) and the synthetic mm.
  Keep buffer *state* (the user explicitly wants buffer state); drop the analog
  offset that only fed the piston animation.

- **Event-driven push.** Push when a tracked discrete field changes (compute on
  each firmware status/event ingest, emit on diff) instead of relying on the
  fixed tick. Keep a low-rate periodic tick purely for the existing full-resync
  recovery (`daemon-klipper-mirror` "Full resync recovery") and host-busy resume.
  Net effect: traffic ≈ number of real state transitions, not 4/s.

- **Checkpoint latch.** Track filament position as monotonic progress within a
  load/unload phase rather than instantaneous sensor derivation: entering a load
  latches Gate-passed once the gear/gate sensor has been seen (or the phase
  implies it), and holds it through to toolhead; an unload steps it back down.
  This makes the Gate checkpoint survive a fast transition and matches the
  physical sequence. Exact `filament_pos` ladder (Happy-Hare 0/1/2/3/4…10) to be
  finalized against what the Fluidd panel renders.

- **No new continuous data.** Per the user: informative yet minimal — no fancy
  drawings. Only known/UI-set discrete state.

## Risks / Open Questions

- Confirm which `filament_pos` values the Fluidd/Mainsail MMU panel maps to the
  Gate vs Toolhead checkpoint dots (drives the ladder + latch thresholds).
- Event-driven push must still satisfy full-resync and host-busy reconcile paths
  unchanged; the periodic tick stays as the resync carrier.
- Dropping `sync_feedback` float: verify no UI element other than the cosmetic
  piston consumed it; the buffer-state enum must remain for the state readout.
