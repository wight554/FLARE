## Why

In type-D standalone mode the relay COMPRESSION branch fed `SYNC_MIN_SPS`
(~100 sps) **forward** while the buffer was full. When the extruder stops
drawing at end of feed, that forward feed keeps pushing filament into a full
buffer, deepening the buffer past the compression switch (`BP` ~-5 → -7,
toward the physical wall) for several seconds until the blind `SYNC_AUTO_STOP_MS`
timer fires. That is the slow, pressure-building stop. Feeding `0` instead lets
the buffer settle cleanly and the extruder draw recover it.

(The separate, much larger purge grind/jam issue was a klipper macro bug — `G90`
left the extruder in absolute mode so purge `G1 E` moves retracted filament into
the buffer; fixed in the macro with `M83`, not firmware. See design.md.)

## What Changes

- Type-D (`BUF_SENSOR_TYPE == 0`) relay COMPRESSION target becomes a **true
  zero feed** instead of `SYNC_MIN_SPS`, and the `SYNC_MIN` output clamp is
  bypassed for that state so feed actually reaches 0. Two lines in
  `firmware/src/sync.c`.
- Nothing else: recovery uses the existing relieve / `SYNC_AUTO_STOP_MS` path;
  no new lifecycle, taper, overfill budget, or tunable.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `sync-refactor`: the type-D hysteretic relay COMPRESSION state commands a true
  stop (0) rather than `SYNC_MIN`, so the buffer is not over-filled at end of
  feed.

## Impact

- Firmware: `firmware/src/sync.c` — relay COMPRESSION target and the output
  clamp bypass. Gated to `BUF_SENSOR_TYPE == 0`; type-P unchanged.
- No config/tunable changes; runs on defaults (`SYNC_TENSION_RAMP_MS=0`,
  `SYNC_UP_RATE=40`, `POST_PRINT_STAB_MS=0`).
- Validated on hardware for constant feed (no jumps), fast multi-blob purge (no
  grind/jam, with the `M83` macro fix), and clean stops. Real multicolor-print
  sign-off deferred.
