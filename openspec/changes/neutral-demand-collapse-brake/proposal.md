## Why

In type-D standalone mode the NEUTRAL relay law tracks `extruder_est_sps` on
the assumption it reflects current demand, but the estimator only self-corrects
at the rails (tension catch-up, compression bleed-down) — never in the open
NEUTRAL band. When a fast purge ends, demand collapses to zero while
`extruder_est_sps` stays frozen at the high value it learned during the
preceding tension catch-up surge. The relay keeps feeding `est × NEUTRAL_FRAC`
into a buffer the extruder is no longer draining, marching it straight into the
compression hard wall. Captured telemetry shows EST frozen at ~1071 mm/min and
MMU feeding ~1322 mm/min across the entire NEUTRAL glide (arm velocity 0) until
the buffer slams compression — enough sustained wall pressure to break a bowden
tube. This violates the existing spec promise that NEUTRAL "drifts slowly
rather than slamming a wall."

## What Changes

- Add NEUTRAL-band demand-collapse correction: while in `BUF_NEUTRAL` with the
  virtual buffer position sliding toward COMPRESSION and no recent TENSION
  refill, decay `extruder_est_sps` toward the relay-estimator rate that arrests
  the drift (current MMU feed divided by `RELAY_NEUTRAL_FRAC`, minus a small
  margin). Fills the gap left by the existing
  rail-only correctors (tension catch-up, compression bleed-down) and the
  `model_stalled_*` NEUTRAL branch that only fires when the virtual model is
  pinned at a rail.
- Arm the existing fast-brake on `NEUTRAL → COMPRESSION` transitions (currently
  only `TENSION → COMPRESSION`), gated to when the MMU feed rate is hot, so a
  buffer that reaches the compression switch from the NEUTRAL band still gets
  the instant stop instead of coasting in.
- Scope is the type-D virtual-endstop path (`BUF_SENSOR_TYPE == 0`) only.
  Type-P analog control behavior is unchanged.

Out of scope (guarded follow-on): changing the relay COMPRESSION law from
`SYNC_MIN_SPS` to a true stop — deferred due to prior `relay_min_flip` deadlock
history.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `sync-refactor`: the type-D hysteretic relay gains a NEUTRAL-band
  demand-collapse estimator corrector and arms fast-brake on
  `NEUTRAL → COMPRESSION`, so the NEUTRAL "drifts slowly rather than slamming a
  wall" promise holds when demand collapses mid-band.

## Impact

- Firmware: `firmware/src/sync.c` — estimator update block (the
  TENSION/NEUTRAL/COMPRESSION corrector chain around the `model_stalled_*`
  logic) and `sync_on_transition` fast-brake arming.
- No new config keys required for the core fix; any new margin/threshold should
  reuse existing tunables or derive from buffer geometry. New tunables, if
  introduced, flow through `config.ini` → generated `tune.h`.
- Behavior change is gated to `BUF_SENSOR_TYPE == 0`; type-P analog path stays
  byte-identical.
- Affects sync control behavior under fast purge / abrupt demand-drop; covered
  by host-side analysis of poll/event telemetry.
