# Phase 13 baseline capture: current snap-to-max behavior

**Status:** Prerequisite data collection, not a pass/fail check — satisfies
ROADMAP Phase 13's `**Depends on**` line ("real-print baseline capture of
current snap-to-max behaviour first, decision in `typep-feed-hunting`").

**Source:** Real Type-P rig, 2h print with ~10 tool swaps, 2026-09-12.
Captured via `scripts/verify_hw_open_items.py watch` against the live
daemon (`hw_evidence_realprint.jsonl` on the rig — not committed to this
repo; the representative excerpt below is from the session transcript).
No firmware changes were in effect during this capture — this is the
*current*, unmodified behavior Phase 13 is meant to fix.

## What the capture shows

**Repeated compression snap-to-max ("hunting").** During sustained active
sync, the buffer repeatedly snaps straight to the compression rail and
relief-pauses, then immediately restarts and repeats — e.g. one stretch
(print-relative `[414.3s]`–`[525.6s]`, ~111s) cycles through this pattern
roughly every 2.4–3.4s, dozens of times back to back:

```
EV:SYNC:AUTO_START:
EV:BS:NEUTRAL,1205.1,0.00
EV:BS:COMPRESSION,100.1,0.98
EV:BS:COMPRESSION,100.1,1.00
EV:BS:COMPRESSION,100.1,1.00
EV:SYNC:RELIEF_PAUSE:
```

This is the exact "current snap-to-max" symptom Phase 13's success
criterion #1 targets (replacing it with a bounded relief snap of
`min(max_sps, est × SYNC_PSF_RELIEF_MULT)`, default multiplier 1.33,
instead of jumping straight to `max_sps`/the compression rail).

**Oscillation magnitude.** Across the full print, `BS:` normalized
position spans roughly `-0.68` (deep tension) to `1.00` (full
compression) — consistent with the previously-documented "±0.8 hunting
oscillation." Both rails get stressed: `SYNC:TENSION_RISK_HIGH` also
fires repeatedly (e.g. `[59.7s]`, `[89.7s]`, `[119.7s]`, `[265.5s]`,
`[447.2s]`, `[508.9s]`, `[620.7s]`, `[680.7s]`, `[710.8s]`, `[740.8s]`,
`[770.8s]`, `[800.8s]`, `[830.8s]`), not just the compression side.

**Not captured in this pass:** continuous `g_sync_current_sps`/
`target_sps`/`max_sps` step-rate telemetry. The `EV:BS:` stream is buffer
*position* (raw + normalized), not the step-rate values Phase 13's
dependency line specifically names ("`sync.c` urgent-refill trace showing
`g_sync_current_sps` snapping to `target_sps`/`max_sps`"). A few one-off
`?:` polls during this session showed `SC:`/`EST:` fields but not a
continuous trace. If exact sps numbers are needed to pick the
`SYNC_PSF_RELIEF_MULT` multiplier precisely, a follow-up capture with
continuous `?:` polling (or firmware-side logging) alongside `watch`
would strengthen this — not required to start planning.

## Recommendation

Sufficient to unblock Phase 13 planning: the qualitative pattern (snap-to-
max pegging, ~2.4–3.4s hunting cadence, ±0.8-ish oscillation range, both
rails stressed) confirms the fix direction already scoped in the ROADMAP
success criteria. Precise sps-level tuning data is a refinement for the
plan/implementation stage, not a planning blocker.

## Caveat — unrelated issue found during this same print

This print also hit a `TC:UNLOADING` → `EV:UNLOAD_TIMEOUT:1` →
`EV:TC:ERROR:UNLOAD_TIMEOUT` → `EV:WARN:LOOP_LAG:22599:TC` fault, twice,
right after a tip-forming (`BL:T`) sequence. Root cause (found 2026-09-12,
host-sim reproduced): the same-day firmware commits `76b4b73`/`e0df203`
made Type-P `BL` break detection depend on an absolute normalized threshold
(`g_bl_lock_engaged = g_buf_pos <= -0.75`, prime "reached" at -0.95). On a
rig whose tension hard end reads shallower than that (this print's deepest
reading was -0.68), every prime ends `PRIME_BOUND`, the lock never counts as
engaged, no `BL:BREAK`/`FOLLOW` fires for retracts longer than the 16 mm
buffer, the buffer pins at the compression rail and the extruder skips
against the held MMU — the tip never parks, so the TC unload retracts the
full `UNLOAD_MAX_MM` (3000 mm / 30 s at 100 mm/s) with OUT still covered.
The `92e39e7` `PAUSE=1.0` removal was a red herring (sim shows the dwell
makes no difference). Fix: break/follow-gate relative to the deepest reading
actually observed at the rail (`g_bl_lock_extreme`, `BL_BREAK_DELTA_NORM`).
This is a **separate, unrelated bug** — not part of Phase 13's scope, and not
itself evidence against the snap-to-max baseline above. Tracked separately; not blocking this baseline capture.
