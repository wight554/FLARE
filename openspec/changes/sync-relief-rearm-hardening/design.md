## Context

Weakness audit of the type-D buffer-sync control flow in `firmware/src/sync.c`
(state = baseline + the `compression-overfeed-stop` true-stop). Type-D has only
two microswitches (TENSION/COMPRESSION, NEUTRAL between), no analog position, no
extruder feed-forward; `g_buf_pos` is dead-reckoned and only re-anchored at
switch crossings; `extruder_est_sps` is learned only at crossings. This document
is the record of the audit; the proposal scopes which findings to fix.

## Recovery state machine (the central fragility)

```
end of feed (extruder idle)
        │
COMPRESSION, feed=0 ──5s SYNC_AUTO_STOP_MS──► RELIEF_PAUSE   (sync.c:1731-1737)
        │                                          │ sync OFF, lane stopped
        │                       NEG_SYNC reverse-relieve (buffer_stabilize_tick)
        │                                          ▼
        │                        buf_force_stable_state(NEUTRAL)   (sync.c:708)
        │                                          │
        │                  buf_update(NEUTRAL) → sync.c:846 checks TENSION ONLY
        ▼                                          │
sits NEUTRAL, sync = RELIEF_PAUSE ◄───────────────┘ (NEUTRAL does not clear it)
        │
sync_tick: RELIEF_PAUSE → return (sync.c:1271)   ← auto-start cannot re-arm
        │
ONLY exit: buffer physically reaches TENSION → sync.c:846 → ACTIVE
```

## Findings (ranked)

| # | sev | finding | file:line | trigger | fix locus |
|---|-----|---------|-----------|---------|-----------|
| 1 | high | RELIEF_PAUSE re-arms only on TENSION; after relief→relieve→NEUTRAL sync stays paused until a full drain to empty; high-flow resume starves before re-arm | sync.c:846, 1271, 1731 | relief then high-flow resume / mid-print pause | firmware |
| 2 | high | Estimator full-overwrite on TENSION→COMPRESSION from a *modeled* travel (2·threshold ÷ dwell); short/partial transition → huge est → next NEUTRAL over-feeds → cycle | sync.c:762, 787-788, 801-802 | any fast/partial TENSION→COMPRESSION | firmware |
| 3 | med | Steady-feed relay limit cycle: NEUTRAL = est·RELAY_NEUTRAL_FRAC clamped to baseline_floor; if > true demand the buffer creeps to COMPRESSION every ~5s → RELIEF_PAUSE → reverse-relieve → re-arm, repeatedly | sync.c:1678-1683, 1731 | baseline_floor / NEUTRAL_FRAC > sustained demand | config/firmware |
| 4 | med | NEG_SYNC reverse-relieve gated on controller_idle = FLARE lane idle only (no extruder-motion knowledge) → can reverse-pull against a slow active extrusion | sync.c:611-614, 677-681 | slow-flow + over-feed → compression 5s mid-print | firmware |
| 5 | med | extruder_est_sps and g_buf_pos are global, not per-lane; a toolchange carries est (sps) + reserve across lanes, mis-scaled if lanes differ in MM_PER_STEP | sync.c:124, 788, 852 | per-lane different rotation_distance | firmware (rescale on swap) / N/A if identical |
| 6 | low | buf_read_stable flip-guard comment stale ("COMPRESSION commands SYNC_MIN" — now 0); logic OK, only active if relay_min_flip_mm>0 (default 0) | sync.c:552-563 | relay_min_flip_mm>0 | comment + config caveat |
| 7 | low | est_sps = …/MM_PER_STEP[idx] no runtime div-by-zero guard (relies on gen_config mandatory rotation_distance) | sync.c:788 | rotation_distance=0 misconfig | firmware guard / config |
| 8 | low | MV (TASK_MOVE) not tracked by buf_virtual_position_tick → g_buf_pos stale across an MV until next crossing re-anchors | sync.c:346-349 | MV feed-forward / retract | firmware |
| 9 | low | est update gated prev_dwell > BUF_HYST_MS; marginal (~BUF_HYST) transitions skip the update → staleness | sync.c:779 | rapid near-switch flapping | firmware |
| 10 | low | expf() (software float, no FPU) at every NEUTRAL↔wall crossing for the drift observer | sync.c:822 | frequent crossings | perf |
| 11 | low | mixes passed now_ms and global g_now_ms (fast-brake check) → minor timing skew | sync.c:1114 | — | tidy |

## Checked, NOT issues
- Mid-band demand estimation correctly absent (no ground truth between crossings).
- Type-D both-switches → feed 0 + `BS:FAULT`, never FAULT_HOLD (`sync.c:1314`); FAULT_HOLD recovery is type-P only.
- COMPRESSION→0 does not deadlock: flip-out exempt from the travel guard (`sync.c:560`) + relay_min_flip_mm defaults 0.

## Decisions (proposed, for the scoped fixes)

- **D1 (item 1):** add a RELIEF_PAUSE→ACTIVE exit when the buffer reaches NEUTRAL
  via the relieve service, reseeding `g_buf_pos`/`sync_current` like the
  FAULT_HOLD recovery (`sync.c:1258-1262`) so resume needs no full drain.
- **D2 (item 2):** replace the hard overwrite at `sync.c:801-802` with a blended
  / rate-capped update so a single modeled transition can't spike `est`.
- **D3 (items 3-4):** evaluate gating the reverse-relieve on a longer idle and/or
  tightening the NEUTRAL hold; may be config-only. Decide during apply.
- **D5 (item 5):** rescale `est` on active-lane change if `MM_PER_STEP` differs;
  no-op for identical lanes.

## Risks / Trade-offs

- [Re-arming on NEUTRAL could resume into a still-leaning buffer] → reseed to the
  reserve target + bootstrap sps (proven safe by the FAULT_HOLD recovery).
- [Blended estimator reacts slower to a genuine fast demand jump] → the relay
  catch-up at TENSION already covers refill regardless of est; the blend only
  slows the NEUTRAL feed-forward, which is the safe direction.

## Open Questions

- Whether items 3-4 need firmware or are acceptable as config guidance.
- Real-print sign-off of the recovery changes once implemented.
