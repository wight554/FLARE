## Why

`flare_daemon.py` pushes `SET_MMU` deltas at 4 Hz (`flare_daemon.py:1103`).
Because the mirror tracks continuously-varying cosmetic fields — the
`sync_feedback` float (the Fluidd buffer-piston drawing) and a synthesized
"Filament: X mm" readout in `mmu.py` `get_status` — the delta is almost never
empty during a print, so the daemon emits a `SET_MMU` nearly every tick and
overloads the Klipper gcode queue. The mm readout is also fake (FLARE has no
encoder; it is derived from config distances, now removed).

The same fixed-rate model causes a UI bug: filament checkpoints are derived from
instantaneous sensor state and pushed only every 250 ms, so a fast gate→toolhead
load transition is sampled straight to `filament_pos=10`, skipping the
intermediate Gate checkpoint. The "Gate" marker then stays unchecked even though
"Toolhead" is checked.

## What Changes

- Drop cosmetic/continuous fields from the mirror: the `sync_feedback` float
  (piston drawing) and the synthesized filament-mm readout. Keep the discrete
  **buffer state** (`sync_feedback_state`: compression/tension/neutral) — that is
  the buffer signal worth showing.
- Make the push **event-driven**: emit `SET_MMU` only when a tracked *discrete*
  UI-meaningful field changes (gate_status, filament checkpoint, buffer state,
  action, sensor flags, active_gate/tool, num_toolchanges). With the analog
  fields gone the push collapses to sparse, change-driven traffic; the periodic
  tick remains only as a full-resync safety net.
- **Latch load/unload checkpoints** so the path advances monotonically through a
  load and is not skipped by a fast sensor transition: once the Gate checkpoint
  is passed it stays set until an unload clears it; checkpoints are driven by the
  load/unload phase, not just instantaneous sensor reads.
- Remove the now-dead `bowden_length` / `extruder_to_nozzle` reads in
  `mmu.py` `get_status` (vars already removed from the cfg).

## Capabilities

### New Capabilities

- `klipper-integration`: load/unload checkpoint latch (monotonic filament_pos
  through a phase; Gate checkpoint cannot be skipped by a fast transition).

### Modified Capabilities

- `daemon-klipper-mirror`: the mirror tracks a discrete UI-meaningful field set
  (no continuous piston float, no synthetic mm) and pushes event-driven on
  discrete change rather than per 4 Hz tick.

## Impact

- `scripts/flare_daemon.py`: mirror field set + push trigger (event-driven).
- `klipper/mmu.py`: drop synthetic mm + piston float from `get_status`; checkpoint
  latch in the load/unload phase tracking.
- Specs `daemon-klipper-mirror`, `klipper-integration`.
- Validation needs the Fluidd/Mainsail MMU panel on real hardware (HW): confirm
  reduced push rate, buffer-state display, and Gate/Toolhead checkpoints latching
  correctly across load and unload.
