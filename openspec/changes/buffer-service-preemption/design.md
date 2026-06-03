# Design — buffer-service-preemption

## Findings

- `d34af43` moved `BS` active-sync cancellation ahead of the activity gate, but
  kept `g_boot_stabilizing` as a hard busy condition. A later `BL:T` therefore
  rejects while BS is still driving the buffer.
- `BL` also checks `controller_activity_in_progress()` before disabling active
  sync, so SYNC-owned lane feed can still look busy depending on timing.
- `boot_stabilize_stop()` already stops the stabilize motor cleanly but is local
  to `sync.c`.

## Plan

### firmware/src/sync.c + firmware/include/sync.h
- Add `buffer_stabilize_cancel()` wrapper around the existing stop helper.
- Keep cancellation scoped to buffer stabilize only; no state-machine reset.

### firmware/src/protocol.c
- Split hard activity (`TC`, cutter, manual unload) from cancellable activity.
- In `BS`, reject hard activity, then cancel active sync/BL, active buffer
  stabilize, and standalone lane commands before requesting fresh stabilize.
- In `BL`, parse arguments first, reject hard activity, then cancel active buffer
  stabilize and active sync before arming BL.
- Risk: do not cancel TC/cutter/manual unload; do not leave motors enabled from
  canceled BS.

### MANUAL.md + BEHAVIOR.md
- Document BS/BL buffer-service preemption semantics.

## Validation

- Build firmware.
- Run regression, Python syntax/tests, and OpenSpec strict validation.
