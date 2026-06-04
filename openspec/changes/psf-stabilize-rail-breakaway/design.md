## Context

`buffer_stabilize_tick()` type-P stagnant guard (`sync.c:906`):
```c
if (BUF_SENSOR_TYPE == 1 && g_boot_stabilizing) {
    if ((int32_t)(now_ms - g_boot_stabilize_started_ms) >= 200) {
        float change = fabsf(g_buf_pos - g_boot_stabilize_start_pos);
        if (change < 0.03f) { cmd_event("BUF_STAB","STAGNANT_TIMEOUT"); boot_stabilize_stop(); return; }
    }
}
```
`g_buf_analog_saturated_since_ms` (`sync.c:144`, set at `1979`) already tracks
how long the signal has been pinned at a rail — reuse it.

Rig evidence (SSE trace): loaded buffer at `BP −1.0`, `CF:0.50`; `BS` →
`BUF_STAB:START` then `STAGNANT_TIMEOUT` with `g_buf_pos` flat at `−1.0` for the
full 200 ms. `MV:20` (20 mm) moves `−1.0 → +`. So the motor/coupling is fine;
the 2 mm fed in 200 ms is inside the rail's mechanical deadband.

## Goals / Non-Goals

**Goals:**
- Type-P stabilize drives off a saturated rail to goal.
- Preserve the fast dry-spin abort for the off-rail (uncoupled) case.
- Bounded breakaway so a jammed/uncoupled buffer still aborts (not 10 s deadline).
- Rig-tunable without reflash.

**Non-Goals:**
- Type-D stabilize (untouched).
- Changing `BUF_STAB_SPS`, the presence gate (`sync.c:813`), direction
  (`sync.c:830`), or the predict/reached logic (`sync.c:925`) — all correct.
- NVM persistence of the new knobs (live SET only; bake defaults later).

## Decisions

### D1 — Measure stagnation after desaturation, not from start

While `g_buf_analog_saturated_since_ms != 0`, skip the `change < norm` abort and
re-baseline `g_boot_stabilize_start_pos = g_buf_pos` each tick. The 200 ms /
0.03 check then judges only motion that happens *after* the buffer leaves the
rail — its actual intent (is the buffer tracking the feed?).

### D2 — Breakaway cap measured from start, baseline NOT reset

The saturated branch must abort if the buffer never desaturates (truly stuck /
uncoupled at the rail). Gate on `now_ms - g_boot_stabilize_started_ms >=
PSF_STAB_RAIL_BREAK_MS`. **`g_boot_stabilize_started_ms` is NOT reset** in the
saturated branch (only `start_pos` is) — resetting it would make the cap
unreachable, leaving only the 10 s deadline (≈100 mm fed into a jam). Default
1500 ms ≈ 15 mm at 10 mm/s: enough to clear any realistic rail deadband, short
enough that a jam doesn't grind.

*Alternative rejected*: rely on the existing 10 s `g_boot_stabilize_deadline_ms`.
Too long — 100 mm into a stuck buffer.

### D2b — Desaturated stagnant window uses its own timer

`g_boot_stabilize_started_ms` cannot also drive the off-rail stagnant window:
rail breakaway may legitimately take longer than `PSF_STAB_STAGNANT_MS`, so the
first desaturated tick would see the window already expired and compare a tiny
`-0.99 → -0.98` position delta against `PSF_STAB_STAGNANT_NORM`, false-firing
`STAGNANT_TIMEOUT`. The firmware keeps a separate
`g_stab_stagnant_since_ms` timer. It is initialized with stabilize start and
refreshed on every saturated tick along with `g_boot_stabilize_start_pos`; once
the signal desaturates, the dry-spin window is measured from that last saturated
tick. The rail cap still uses `g_boot_stabilize_started_ms`, so a never-breaks
jam remains bounded.

### D3 — Live-tunable, no NVM

`200`/`0.03f` become `PSF_STAB_STAGNANT_MS`/`PSF_STAB_STAGNANT_NORM`; add
`PSF_STAB_RAIL_BREAK_MS`. Runtime-settable via SET (mirror `SYNC_PSF_FILTER_MM`),
not persisted — tuning loop only. Bake the winning values into `tune.h` once
found.

## Risks / Trade-offs

- **Cap too low** → aborts before clearing a deep rail deadband → raise
  `PSF_STAB_RAIL_BREAK_MS`. **Too high** → grinds longer into a real jam → lower.
  Rig-tune; default 1500 ms is the starting point.
- **Saturation flag flicker** near the rail could bounce between branches — both
  branches feed in the same direction and the cap is monotonic from start, so a
  flicker cannot defeat termination.

## Open Questions

- Right `PSF_STAB_RAIL_BREAK_MS` default? Measure the actual rail breakaway
  distance on rig (how many mm of feed before `g_buf_pos` leaves `−1.0`) and set
  the cap ≥ that with margin.
