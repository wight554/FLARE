## Context

Type-D standalone (`BUF_SENSOR_TYPE == 0`) drives the active lane as a two-level
relay (`firmware/src/sync.c`): TENSION = catch-up, NEUTRAL = demand-tracking,
COMPRESSION = back off. The COMPRESSION branch fed `SYNC_MIN_SPS` forward, and
the output `clamp_i(target, SYNC_MIN, max)` re-floored any lower value. So even
"backed off," the MMU pushed ~100 sps into a full buffer.

At end of feed (extruder stops) this deepened `BP` ~-5 → -7 (past the switch
toward the −12.5 mm wall) over ~5 s until the blind `SYNC_AUTO_STOP_MS` timer —
the slow, pressure-building stop.

## Goals / Non-Goals

**Goals:** clean, fast stop — feed actually stops when the buffer is full so it
isn't over-filled. Type-P unchanged.

**Non-Goals:** anything else. No mid-band estimator, no approach-taper, no
relief-pause lifecycle, no overfill-budget, no new tunable.

## Decisions

### D1: COMPRESSION relay target = 0, bypass the SYNC_MIN clamp

Set the type-D COMPRESSION target to `0`, and add `else if (BUF_SENSOR_TYPE == 0
&& s == BUF_COMPRESSION) target_sps = 0;` before the `clamp_i(..., SYNC_MIN, ...)`
so the clamp doesn't re-floor it. The existing ramp-down brings `sync_current`
to 0; baseline `lane_stop` + relieve handle recovery. Two lines, no new state.

Why nothing else: the elaborate variants (below) all regressed something.

## Risks / Trade-offs

- [A COMPRESSION touch during active printing now feeds 0 instead of 100 sps] →
  Benign: the extruder is drawing the buffer off the wall anyway; 0 drains it
  faster and cannot starve (the buffer was full). Constant-feed HW test showed
  no regression.
- [Deadlock from zero feed] → The flip out of COMPRESSION keys on the physical
  NEUTRAL crossing (extruder draw), and `relay_min_flip_mm` defaults to 0
  (time-based). No deadlock.

## Migration Plan

Pure firmware, no settings change. Defaults unchanged. Rollback = revert.

## Alternatives considered (all tried on hardware and reverted)

The original framing assumed FLARE was over-feeding into the wall during fast
purges. A long iteration chased that with firmware:

- **NEUTRAL demand-collapse estimator** — impossible in 2-switch type-D (no
  mid-band ground truth; see `typed-buffer-no-midband-groundtruth`). Reverted.
- **NEUTRAL approach-taper** — caused a constant-feed TENSION-jump limit cycle.
  Reverted.
- **Relief-pause lifecycle** (lane-release / OFF-after-relieve / resume-on-
  NEUTRAL) + **overfill-budget relief** (`relay_compression_relief_mm`) — caused
  the buffer to get stuck, then oscillate, then grind between blobs. Reverted.
- **Re-enabling `SYNC_TENSION_RAMP`** — blasts SYNC_MAX on tension → over-fill
  slam. Reverted.

Root cause of the purge chaos turned out to be a **klipper macro bug**: `G90`
sets the extruder to ABSOLUTE mode in Klipper, so the purge `G1 E{amount}` lines
(written as relative amounts) became position/retract moves that shoved filament
backward into the buffer → compression → toolhead pressure → extruder grind.
Fixed in the macro with `M83`. After that, baseline FLARE was good for purge and
constant feed; the only real firmware need was this 1-line stop.

## Open Questions

- Real multicolor-print sign-off (dynamic flow + toolchanges + duration) is
  deferred; low regression risk for a 1-line COMPRESSION 100→0 change.
