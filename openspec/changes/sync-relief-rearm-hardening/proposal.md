## Why

A weakness audit of the type-D buffer-sync flow (post `compression-overfeed-stop`,
baseline + true-stop) surfaced two firmware-side fragilities in the recovery
path — the same machinery that caused the stuck/oscillation churn during the
true-stop work. They are latent (current HW tests pass) but real. This change
captures the full audit (design.md) and scopes fixes for the high/medium items.

The two high items:
1. **`RELIEF_PAUSE` re-arms only on TENSION.** `sync.c:846` is the sole
   RELIEF_PAUSE→ACTIVE exit and `sync_tick` early-returns on RELIEF_PAUSE
   (`sync.c:1271`). After a relief→reverse-relieve the buffer rests at NEUTRAL
   but sync stays paused until the buffer is drained to empty (TENSION). A
   high-flow resume right after a relief feeds nothing while the extruder drains
   the reserve → empty-wall / grind before re-arm.
2. **Estimator full-overwrite from a modeled transition.** On TENSION→COMPRESSION
   the estimator is replaced (`sync.c:801-802`) using a *modeled* travel
   (`2·threshold`, `sync.c:762`) ÷ dwell. A short/partial transition yields a
   huge `extruder_est_sps`, so the next NEUTRAL over-feeds (`est·RELAY_NEUTRAL_FRAC`)
   → compression → cycle.

## What Changes

- Re-arm `RELIEF_PAUSE` when the buffer recovers to NEUTRAL (via relieve), not
  only on TENSION — bootstrapped like the FAULT_HOLD direct-resume
  (`sync.c:1258-1262`), so resume never requires a full drain.
- Stop full-overwriting the estimator from a modeled TENSION→COMPRESSION travel;
  blend or rate-cap it.
- Medium items (steady-feed limit cycle, relieve-vs-active-extrusion gating,
  per-lane est/g_buf_pos scaling, and the NEUTRAL→COMPRESSION fast-brake gap
  surfaced by the cross-check) addressed or explicitly deferred per design.
- Low items recorded in design as notes (comment staleness, div-guard, MV
  re-anchor, expf perf, dwell-sum overflow) — not necessarily fixed here.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `sync-refactor`: the type-D relay recovery re-arms from RELIEF_PAUSE on NEUTRAL
  (not only TENSION), and the velocity estimator no longer hard-overwrites from a
  modeled transition.

## Impact

- Firmware: `firmware/src/sync.c` — RELIEF_PAUSE exit / re-arm (`846`, `1271`,
  `1731`), estimator update (`801-802`), fast-brake arm (`1112`, medium item 12),
  and (medium) `buffer_stabilize` gating and per-lane est handling. Gated to
  `BUF_SENSOR_TYPE == 0`; type-P unchanged.
- No new tunables intended.
- Validation: HW replay of relief→high-flow-resume, fast TENSION→COMPRESSION
  disturbance, and a long steady print; `scripts/flare_sync_check.py` regression.
