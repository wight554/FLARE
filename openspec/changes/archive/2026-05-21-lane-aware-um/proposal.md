## Why

`UM:` currently unloads only the selected `active_lane`. During printing, the
active lane must keep feeding/syncing, but the standby lane may still be parked
in the preloaded state (`IN=1`, `OUT=0`). Operators need a way to unload that
standby filament without changing active lane selection and without disturbing
the live print path.

The current manual unload contract already says `UM:` never cuts. The missing
piece is lane selection: `UM:1` / `UM:2` should target a specific lane, while
bare `UM` / `UM:` should preserve the current active-lane behavior.

## What Changes

- Extend `UM` command grammar:
  - `UM` or `UM:` unloads the selected/current active lane.
  - `UM:1` unloads lane 1.
  - `UM:2` unloads lane 2.
- Keep manual unload non-cutting in all forms.
- For inactive-lane `UM:n`, preserve active print state:
  - do not change `active_lane`;
  - do not clear toolhead filament state;
  - do not disable active-lane sync;
  - do not use the shared Y-splitter sensor as evidence that the inactive
    target lane occupies the path.
- Treat inactive-lane `UM:n` as the standby-preload eject path: target lane
  must be idle, have filament at `IN`, and not be at `OUT`.
- Update command docs and host guidance so `UM:1` / `UM:2` are discoverable.

## Capabilities

### Modified Capabilities

- `manual-unload`: `UM` gains optional lane selection while preserving the
  existing no-cutter guarantee.
- `toolchange-orchestration`: manual unload remains separate from automated
  toolchange cutter phases.

## Impact

- `firmware/src/protocol.c`: parse optional `UM` lane payload, resolve either
  active lane or explicit target lane, and split active-vs-inactive side
  effects.
- `firmware/src/motion.c`: expected no algorithm change; verify existing
  `TASK_UNLOAD` to-IN behavior is sufficient for standby preloaded eject.
- `scripts/flare_cmd.py`: likely no code change because completion waits are
  verb-based (`UM:2` maps to `UM`), but verify.
- `MANUAL.md`, `BEHAVIOR.md`, `KLIPPER.md`, `README.md`: document the new
  command forms and inactive-standby constraint.
- `openspec/specs/toolchange-orchestration/spec.md`: update durable manual
  unload scenarios after implementation.
