# buffer-service-preemption

## Why

Tip-form macros can issue `BS` and then `BL:T` in quick succession. Commit
`d34af43` let `BS` preempt active sync, but `BL:T` still sees the active
stabilize drive as controller activity and returns `ER:BUSY`. That blocks buffer
lock during tip forming.

## What Changes

- `BS` becomes the explicit buffer-service preemptor: it cancels active sync,
  buffer lock, existing buffer stabilize, and standalone lane commands before
  starting a new stabilize.
- `BL:T` / `BL:C` cancel any active buffer stabilize before arming buffer lock.
- Hard activities (`TC`, cutter, manual unload) still reject with `ER:BUSY`.

## Success Criteria

- `BS` can replace active sync, BL, an existing BS stabilize, or simple lane
  motion without `ER:BUSY`.
- `BL:T` after `BS` cancels the stabilize and returns `OK`.
- Toolchange/cutter/manual unload remain protected by `ER:BUSY`.
