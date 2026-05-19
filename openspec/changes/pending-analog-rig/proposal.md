## Why

Three legacy inverted-polarity assumptions survive **only** in the type-P
(analog, `BUF_SENSOR_TYPE != 0`) path. They are inert under the type-D relay
law (relay COMPRESSION → `SYNC_MIN` stop skips them), so they cannot be
observed, reproduced, or safely fixed without an analog buffer rig. Project
policy is **never blind-fix analog**. This change exists to *track* the
deferred work so it is not re-derived each audit, and to gate any fix on
real analog hardware. Split out of `relay-buffer-control-2switch` 7.3 so
that change can archive at 24/24.

## What Changes

- No code changes now. This is a deferred-work tracker; tasks are
  hardware-blocked and remain unchecked until an analog rig exists.
- Captures the three carried-over items (verbatim from
  `relay-buffer-control-2switch` 7.3 / `audit-sync-polarity`):
  - **#6** `sync_compression_floor_sps()` (`sync.c:385-386`, applied
    `1655-1657`, gated `BUF_SENSOR_TYPE != 0`): force-raises a feed FLOOR
    while `BUF_COMPRESSION` (=full) → fights drain = inverted.
  - **#7** `compression_recovery` / collapse — same inverted bucket.
  - H2 feed-trim comment (`sync.c:1499-1517`, dead under relay override) —
    verify on the analog rig, not a relay-path inversion.

## Capabilities

### New Capabilities
- `analog-polarity-deferral`: the contract that type-P-only inverted
  assumptions are knowingly carried, inert under relay, and resolved only
  against an analog rig (never blind-fixed).

### Modified Capabilities
(none — `relay-buffer-control-2switch` already validated the relay path is
free of these; this only relocates the deferred type-P items.)

## Impact

- No firmware/scripts/config change now.
- `relay-buffer-control-2switch` 7.3 → migrated here; that change reaches
  24/24 and archives.
- Future: any fix requires an analog buffer rig + on-rig validation.
