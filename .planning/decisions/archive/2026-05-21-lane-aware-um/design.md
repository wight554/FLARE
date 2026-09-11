## Context

Current `UM` path lives in `firmware/src/protocol.c` and resolves the target
through `get_active_lane_and_clear_error()`. That helper only looks at
`active_lane`, so a user cannot unload a preloaded standby lane without first
changing selection via `T:n`.

Current active-lane `UM` side effects:

- clears retract assist;
- sets sync state to `SYNC_OFF`;
- calls `sync_disable(false)`;
- calls `set_toolhead_filament(false)`;
- if target `OUT` or shared Y-splitter is present, runs a non-cut OUT-clear leg
  before continuing to `IN` clear;
- otherwise runs `TASK_UNLOAD` with `unload_to_in = true`.

Those side effects are correct for unloading the active print path, but wrong
for unloading a standby preloaded lane while printing. The active lane may be
feeding and syncing, and the toolhead filament latch belongs to the active print
path, not the standby lane.

The shared Y-splitter sensor is the most important ambiguity. During printing,
Y can be occupied by the active lane. If `UM:2` targets an inactive preloaded
lane (`IN=1`, `OUT=0`) while lane 1 is printing, using `on_al(&g_y_split)` would
falsely treat lane 2 as path-occupied and run the extra clear leg. Inactive
standby eject should ignore shared Y and rely on the target lane's own `OUT`
state.

## Desired Command Contract

| Command | Target | Side effects |
|---|---|---|
| `UM` | selected/current active lane | current active-lane behavior |
| `UM:` | selected/current active lane | current active-lane behavior |
| `UM:1` | lane 1 | explicit-lane behavior |
| `UM:2` | lane 2 | explicit-lane behavior |

Explicit-lane behavior depends on whether the target equals `active_lane`.

### Explicit target is active lane

Same as current `UM:`:

- active path is being unloaded;
- sync/toolhead state may be cleared;
- shared Y check may be used as today;
- no cutter phase is started.

### Explicit target is inactive lane

Intended for unloading a standby preloaded lane while printing:

- target lane must be idle;
- target lane must have `IN=1`;
- target lane must have `OUT=0`;
- command starts `TASK_UNLOAD` to `IN` clear;
- command does not change `active_lane`;
- command does not clear `toolhead_has_filament`;
- command does not disable active-lane sync;
- command does not inspect shared Y-splitter occupancy;
- no cutter phase is started.

If target inactive lane has `OUT=1`, reject rather than guessing. At that point
the lane is not simply preloaded, and pulling it while another lane prints risks
shared-path collision.

## Regression Impact

### Preload

Inactive `UM:n` should be the inverse of standby preload: preloaded lane at
`IN=1`, `OUT=0` retracts until `IN=0`. It must not affect the other lane's
autopreload state or selected active lane.

### Load / Full Load

No `TASK_LOAD_FULL` behavior changes expected. Verify explicit inactive `UM:n`
is rejected if the target lane is not idle.

### Unload

Active-lane `UM` and `UM:n` where `n == active_lane` must keep current behavior,
including non-cut clear leg and final `EV:UNLOADED:<lane>`.

Inactive-lane `UM:n` should emit the same unload completion event for the target
lane. Host completion waiting already keys by command verb in `flare_cmd.py`.

### Toolchange / Cutter

Manual unload must remain separate from `TC` cutter phases. No `cutter_start()`
call should be reachable from any `UM` form.

`TC` state should not be active during inactive `UM:n` unless the command is
explicitly rejected as busy. This avoids mixing manual standby eject with
toolchange-owned state.

### Sync / RELOAD

Inactive standby eject while printing must preserve active-lane sync state.
Active-lane `UM` continues to disable sync because it unloads the live path.

During RELOAD/toolchange states, explicit inactive `UM:n` should be rejected as
busy unless there is already a proven safe idle path. The simple rule keeps
state ownership clear.

### Persistence / Protocol / Docs

No settings or persistence changes. Protocol grammar and docs change only.

## Implementation Plan

2026-05-21 implementation note: `lane-aware-um` initially had proposal,
design, and tasks but no OpenSpec spec delta. Add a
`toolchange-orchestration` delta before code so the durable lane-aware manual
unload contract is validateable during implementation.

### `firmware/src/protocol.c`

- Add a small helper to resolve optional lane payload for `UM`.
- Keep empty payload mapped to active lane.
- For payload `1` or `2`, resolve `lane_ptr(payload)` without mutating
  `active_lane`.
- Reject other non-empty payload with `ER:ARG`.
- Split `UM` handling:
  - active target: current behavior;
  - inactive target: require target idle, `IN=1`, `OUT=0`; start
    `start_manual_unload_to_in()` only; skip sync/toolhead side effects and
    shared Y check.
- Risk: `manual_unload_active()` remains global, so while inactive unload is in
  progress other commands should still return `ER:BUSY` except allowed
  status/stop commands.

### `firmware/src/motion.c`

- Avoid changes unless implementation reveals `TASK_UNLOAD` to-IN cannot handle
  standby preloaded eject cleanly.
- Risk to watch: unload timeout or jam events should still include enough lane
  context for host diagnosis.

### `scripts/flare_cmd.py`

- Verify no code change needed for `UM:1` / `UM:2` because completion events are
  keyed by verb before `:`.
- If docs/examples are embedded in the script header, update them only if needed.

### Docs and Specs

- Update `MANUAL.md` command reference to list `UM[:lane]`.
- Update `BEHAVIOR.md` unload section with active vs inactive lane behavior.
- Update `KLIPPER.md` / README examples if they mention `UM` command forms.
- Update `openspec/specs/toolchange-orchestration/spec.md` with lane-aware
  manual unload scenarios after implementation.

## Validation Plan

- `ninja -C build_local`.
- `python3 -m py_compile scripts/*.py` if any script is touched.
- Static checks:
  - no `cutter_start()` path from `UM`;
  - inactive `UM:n` does not call `sync_disable()` or
    `set_toolhead_filament(false)`;
  - active `UM` behavior remains unchanged.
- Hardware/protocol checks when a board is available:
  - active lane 1, lane 2 preloaded: `UM:2` returns `OK`, emits
    `EV:UNLOADED:2`, and `?:` still reports `LN:1`;
  - active lane 1 printing/sync active: `UM:2` does not clear toolhead/sync
    state;
  - `UM` and `UM:` still unload active lane as before;
  - `UM:3` returns `ER:ARG`;
  - `UM:2` with lane 2 at `OUT=1` while inactive returns an error and does not
    move.
