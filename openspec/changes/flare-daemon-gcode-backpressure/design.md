## Context

Mirror loop in `scripts/flare_daemon.py` (~L1088-1360) runs every 0.25s, builds `SET_MMU`
delta, POSTs to `{moonraker}/printer/gcode/script` with `urlopen(timeout=1.0)`. On any
exception → `backoff = time.time()+5.0`, clears caches to force full resync, retries.
`_push_gate_map_to_klipper` (L264) + `_FLARE_SYNC_BOARD` use same gcode/script channel.

Failure: blocking command (`MPC_CALIBRATE` ~15min) holds gcode lock. Client `urlopen`
times out at 1s → except → 5s backoff → retry. But Moonraker already accepted + queued the
forwarded gcode/script to klippy server-side; client timeout does not cancel it. Each retry
adds another. Klippy gcode queue piles (`pending: 60..840 seconds`), greenlet stack deepens,
`webhooks._do_query` `json.dumps` hits Python recursion limit → Kalico crash.

`idle_timeout.state` = "Printing" during any blocking gcode (incl. manual `MPC_CALIBRATE`),
returns "Idle"/"Ready" when lock free. `objects/query` does NOT take gcode lock — safe to
poll while host busy (see [[daemon-setmmu-delta-push]]: never poll mirror state mid-command,
but objects/query on idle_timeout is lock-free and authoritative for lock state).

## Goals / Non-Goals

**Goals:**
- Daemon never queues gcode/script behind a blocking command.
- Distinguish host-busy (lock held) from Moonraker-offline; right response each.
- Resume cleanly via existing full-resync; no missed-state corruption.
- Pure stdlib, no firmware change, no new deps.

**Non-Goals:**
- Fix Kalico greenlet recursion (upstream defect; report separately).
- Keep mirror live during blocking command (stale UI telemetry acceptable; not safety).
- Change delta-push / full-resync / gate-diagnostics behavior.
- Move mirror off gcode/script entirely (larger refactor; out of scope this change).

## Decisions

**D1: Detect busy via timeout, confirm via lock-free idle_timeout probe.**
On gcode/script timeout, do not assume offline. Probe `objects/query {idle_timeout}`:
- reachable + state == Printing → host-busy. Enter busy mode, suppress pushes.
- unreachable → offline. Existing 5s backoff path.
- reachable + Idle/Ready → transient; resume normally.
Why: timeout alone ambiguous (busy vs offline). idle_timeout is the authoritative,
lock-free lock-state signal. Alt rejected: gate on `print_stats.state` — stays
ready/standby during manual `MPC_CALIBRATE`, misses it.

**D2: Busy mode = full suppression of all three gcode/script emitters.**
While busy flag set, skip `SET_MMU`, `MMU_GATE_MAP`, `_FLARE_SYNC_BOARD`. All share the
lock; any one re-queues. Loop keeps running (state tracking, delta compute) but emits
nothing to gcode/script. Spoolman / DB writes (non-gcode channels) continue.
Why: partial suppression still piles requests.

**D3: Exit busy on idle.** While busy, each tick polls idle_timeout (throttled, e.g. ≥1s
between probes to avoid HTTP spam). state Idle/Ready → clear busy, force full resync
(`last_pushed_fields = {}`), resume.
Why: full resync reconciles everything missed during suppression — reuses existing
recovery path, no new reconcile logic.

**D4: Throttle probe.** Reuse a backoff-style timestamp so busy-mode polls idle_timeout
at a slow cadence (~1-2s), not the 0.25s loop rate.
Why: avoid hammering Moonraker during 15min calibration.

## Risks / Trade-offs

- [Mirror stale during blocking command] → Acceptable: telemetry-only, full resync on
  resume. Document in operator notes.
- [idle_timeout probe itself unreliable / wrong state] → Probe is read-only objects/query,
  lock-free; worst case stays suppressed one extra tick, resumes on next Idle read.
- [False busy on a one-off slow request] → D1 confirms via idle_timeout; transient timeout
  with Idle state resumes immediately, no stuck suppression.
- [Moonraker offline misclassified as busy] → D1 unreachable branch routes to existing
  offline backoff explicitly.
- [Probe HTTP adds load] → D4 throttle bounds it.

## Migration Plan

Host-tool only. Deploy: update `scripts/flare_daemon.py`, restart `flare_daemon.service`.
Rollback: revert file, restart. No firmware reflash, no state migration.

## Open Questions

- Exact idle_timeout state strings to treat as "free": confirm Kalico emits
  `"Idle"`/`"Ready"` vs only `"Idle"`. Verify on rig before finalizing constant.
- Probe cadence value (1s vs 2s) — pick during impl, low stakes.
