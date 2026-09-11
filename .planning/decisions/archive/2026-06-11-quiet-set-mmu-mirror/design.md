# Design

## Diagnosis (from operator console trace)

10 s of identical `$ SET_MMU NUM_GATES=2 ... SYNC_FEEDBACK=0.400 ...` lines, MMU idle.
Fields frozen; only `SYNC_FEEDBACK` jitters ±0.01.

Push loop (`scripts/flare_daemon.py`):

```python
# :1008  force full resync every 10s for restart recovery
if time.time() - last_force_sync > 10.0:
    changed = True
    force_full = True        # delta = entire fields dict, ignores last_pushed_fields
# :1166  delta path (correct, quiet) — overridden by force_full above
delta = {k: v for k, v in fields.items() if last_pushed_fields.get(k) != v}
```

`force_full` sets `delta = fields` → all 34 emitted regardless of change. Moonraker echoes
every API-injected gcode to the console → wall of text.

## Three noise layers (only A is the reported spam)

| Layer | Source | In trace? | This change |
|-------|--------|-----------|-------------|
| A | 10 s blind `force_full` | yes (the spam) | **fix — silent reconcile** |
| B | published `BUF` flap at ±0.1 (no hysteresis) | no (buffer far from edge) | secondary, gated |
| C | `SYNC_FEEDBACK` analog 0.05 push (UI piston) | sub-threshold here | out of scope |

## Buffer state path (why B ≠ A)

Two type-P classifications, different centres:

- Control `buf_state_raw()` (`sync_buf.c:482`): deadband ±0.1 about **active goal**
  (`psf_goal_norm()`), then time-debounced via `buf_read_stable()` (`g_buf_hyst_ms`).
  Feeds `g_buf.state` / motor. NOT published to `SET_MMU`.
- Published `buf_status_label()` (`protocol_status.c:30`): deadband ±0.1 about
  **mechanical 0**, fixed, NO debounce. This is what reaches `BUF:` → `SET_MMU`.

So the SET_MMU-facing deadzone is a neutral-centred band with no edge hysteresis.
It is steady in the trace (buffer parked away from centre) → confirms A, not B, is the
spam. B only chatters on near-centre hover during motion.

## Mechanism — silent reconcile (layer A)

Keep the 10 s tick. Replace blind full-push with a read-then-diff:

1. Tick fires (interval unchanged — operator accepts the safety cadence).
2. GET `/printer/objects/query?mmu` → current mock field values.
3. Compare against the desired `fields` (same formatted strings `cmd_SET_MMU` stores).
4. Match → emit nothing. Mismatch (restart wiped the mock, or drift) → full `SET_MMU`.

Reads do not echo to the console, so an idle reconcile is silent. A restart still
recovers within one tick. Delta-on-change path is untouched for real state changes.

### Compare contract

`last_pushed_fields` already holds the formatted snapshot. The reconcile must diff the
**Klipper-reported** mmu object against `fields`, not against `last_pushed_fields` — that
is what catches a restart that silently reset the mock while the daemon's local snapshot
still looks current. Map the mmu object keys back to the `fields` formatting (e.g. floats
to the same `%.3f`) so equal states compare equal; otherwise every tick false-positives
and the spam returns.

## Open decisions

1. **Reconcile granularity.** Diff all mirrored fields, or a cheap subset (a sentinel
   field that a restart provably resets)? Full diff is safest; subset is cheaper but can
   miss partial drift.
2. **Read failure / mmu object absent.** Treat absent object as "restart in progress" →
   push full (fail-safe toward recovery, accepts a rare spam line).
3. **Layer B scope.** Pursue published-label debounce now, or defer until a motion-time
   trace shows ±0.1 edge flap emitting deltas? Defer unless evidence — own firmware spec
   delta when pursued.
4. **Keep `force_full` on board-online transition** (`:1014`) and first push — unchanged;
   those are genuine events, not the blind timer.

## Implementation plan / notes (2026-06-06)

### scripts/flare_daemon.py + klipper/mmu.py
- Add `_moonraker_get_mmu_status()` using the existing urllib/Moonraker pattern with a
  1 s timeout; absent/offline/non-dict `mmu` returns `None`.
- Add full-field reconcile helpers that format Moonraker `mmu` status back to daemon
  `SET_MMU` command values (`%.3f`, `%.2f`, quoted strings, list CSVs, bools).
- Compare against the Klipper-reported object, not `last_pushed_fields`, so a restart
  that resets the mock is detected even when the daemon's local snapshot is current.
- Preserve first push and board-online full pushes; make periodic tick read/compare and
  push only on mismatch or read failure.
- Expose `tc_state`, `board_feed_rate`, and `board_rev_rate` from `klipper/mmu.py`
  `get_status()` so all mirrored daemon fields have a comparable Moonraker value.
- Risk/invariant: bypass mode stores `active_gate/gate/tool=-2` inside `cmd_SET_MMU`
  while daemon command fields still carry lane values; reconcile treats those sentinels
  as equivalent only when desired `BYPASS=1`.

### scripts/test_flare_mmu_status.py
- Add offline unit coverage that applies a full `SET_MMU`, reads `MMUMock.get_status()`,
  checks daemon reconcile equality, checks float drift mismatch, and checks bypass
  sentinel equivalence.

### KLIPPER.md
- Document Mainsail/Fluidd hidden-command filter `^SET_MMU` as operator stopgap.

### Validation
- `python3 scripts/test_flare_mmu_status.py` → 29 passed, 0 failed.
- `python3 -m py_compile scripts/*.py` → pass.
