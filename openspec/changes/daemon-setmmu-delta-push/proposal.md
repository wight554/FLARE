## Why

`flare_daemon.py` mirrors board state into Klipper's mmu mock via a periodic
`SET_MMU` push (4 Hz). Today it re-emits the **full ~40-field** command whenever any
watched field changes — including `buf_state` and `sync_feedback` (`>0.05`), which
churn continuously during a print. Result: the full line is spammed many times/sec
for what is usually just a piston-position tick. `cmd_SET_MMU` already keeps the
current value for any absent param, so the full payload is unnecessary.

Separately, an operator saw the gate-status dot blink "unchecked" mid-load while the
load actually succeeded — a suspected transient in the mirrored gate state. Root cause
is not provable from static reading, so this change adds flag-gated instrumentation to
capture it rather than blind-patching.

## What Changes

- **Delta push**: track the last-pushed field values; emit `SET_MMU` with only the
  fields that changed since the last push. Send a **full** `SET_MMU` on the 10 s
  force-resync, on board-online transition, and on the first push (restart recovery).
  Relies on `cmd_SET_MMU`'s absent-param-keeps-current semantics.
- **Gate-state instrumentation**: behind an env flag (`FLARE_GATE_DEBUG`), log
  `(ts, LN/active_gate, I1,O1,I2,O2, gate_status, tc_state)` whenever the gate fields
  change, so the dot-blink can be reproduced and root-caused. No behavior change when
  the flag is unset.

No change to the SET_MMU field set, formatting, or Klipper-side contract. The mock
mirror ends in the same state; only the wire volume shrinks.

## Capabilities

### New Capabilities
- `daemon-klipper-mirror`: the `flare_daemon.py` → Klipper `SET_MMU` mirror push
  contract (delta vs full pushes, absent-param semantics, force-resync recovery).

## Impact

- Touched: `scripts/flare_daemon.py` (syncer loop), tests under `scripts/test_*` if
  the push builder is unit-testable.
- Risk: a missed field in the delta would leave Klipper stale until the 10 s
  force-resync; mitigated by the periodic full push and by diffing the exact formatted
  field strings.
