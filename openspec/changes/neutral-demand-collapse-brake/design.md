## Context

Type-D standalone mode (`BUF_SENSOR_TYPE == 0`) has no analog buffer position
and no extruder encoder. `extruder_est_sps` is the only demand proxy and is
derived entirely from buffer-arm motion. The estimator is corrected in three
places in `sync_tick` (`firmware/src/sync.c`):

- TENSION rail (`s == BUF_TENSION`, after collapse delay): bleed EST **up**
  toward `sync_current_sps` — buffer is empty, extruder out-pacing feed.
- NEUTRAL `model_stalled_*` branch: only fires when the **virtual** position
  `g_buf_pos` is pinned at a rail (`<= -thr` or `>= +thr`) while the physical
  switches read NEUTRAL — a model/reality mismatch detector.
- COMPRESSION rail (`s == BUF_COMPRESSION`, after collapse delay): drag EST
  **down** toward `sync_current_sps` — buffer full, feed out-pacing extruder.

The gap: when both physical switch and virtual model agree "NEUTRAL" but the
buffer is sliding toward COMPRESSION (MMU out-feeding a collapsed extruder),
none of these fire. EST stays frozen. The NEUTRAL relay target
(`extruder_est_sps * SYNC_RELAY_NEUTRAL_FRAC`) therefore keeps commanding the
stale-high rate and drives the buffer into the wall.

Telemetry from a fast purge (`?:` poll at 100 ms):

```
phase         BP        EST(mm/min)  MMU(mm/min)  arm_vel
TENSION surge +5.00     4.7 → 1071   120 → 1802   0.64
purge ends    +4.20     1071 (froz)  1562         0.00
NEUTRAL glide +3.6→-4.1 1071 (froz)  ~1322        0.00   ← danger window
COMPRESSION   -5.42     1025 → 29    1084 → ...    -6.31  ← slam
```

EST is constant at 1071.4 across the whole glide; arm velocity is 0 (extruder
stopped). The wall predictor `CW` never trips its soft-wall threshold because
`sync_compression_wall_remaining_mm` measures distance to the far physical wall
(−12.5 mm), not to the compression switch (−5 mm) 0.9 mm away — and its
`wall_trim` is discarded in relay mode anyway.

## Goals / Non-Goals

**Goals:**

- Decay `extruder_est_sps` during the NEUTRAL band when demand has collapsed,
  so the relay NEUTRAL target backs the MMU off before the buffer reaches the
  compression switch.
- Make the corrector self-gating: it must not engage in genuine high-flow
  (where the extruder really is pulling and the buffer is not sliding to
  compression), only when the buffer is actually drifting toward the full wall.
- Arm the existing fast-brake on `NEUTRAL → COMPRESSION` so a buffer that still
  reaches the switch gets the instant stop, not a slow ramp-in from a hot feed.
- Keep type-P analog (`BUF_SENSOR_TYPE != 0`) behavior byte-identical.

**Non-Goals:**

- Changing the relay COMPRESSION law (`SYNC_MIN_SPS` → true stop). Deferred:
  prior `relay_min_flip` work caused a COMPRESSION deadlock; that path needs
  its own guarded change.
- Adding klipper→firmware extruder-velocity feedforward. Out of scope; a
  separate enhancement.
- Reworking the wall predictor distance reference or un-discarding `wall_trim`
  in relay mode. Optional polish, not required for the root fix.

## Decisions

### D1: Drift signal = sign of NEUTRAL virtual-position motion toward compression

Use the virtual position trend, not arm velocity. `g_buf_pos` integrates
`(extruder_est_sps − mmu_sps)·dt` every position tick; when EST is stale-high
and MMU is feeding, `g_buf_pos` still falls (because the commanded NEUTRAL rate
exceeds EST by the `NEUTRAL_FRAC` margin), so a **falling `g_buf_pos` in
NEUTRAL** is an available, already-computed signal that the MMU is out-feeding
whatever EST claims. Equivalent and simpler to read: `reserve_error_mm < 0`
deepening, or `g_buf_pos` below a NEUTRAL-side threshold and decreasing.

Alternative considered — arm velocity (`g_buf.arm_vel_mm_s`): only updated on
state transitions, reads 0 through the glide (see telemetry). Rejected: not a
continuous in-band signal.

### D2: Decay target = arrest-the-drift rate (≈ current MMU feed)

Mirror the compression bleed-down. The COMPRESSION corrector pulls EST toward
`sync_current_sps`. In NEUTRAL, when the buffer is sliding to compression, the
extruder is by definition slower than the current MMU feed, so decay EST toward
`lane_motion_sps(A)` minus a small margin (symmetry with the existing
`model_stalled_tension` branch which uses `lane_motion_sps − 4`). This nulls the
drift: as EST drops, the relay NEUTRAL target drops, MMU slows to match the
(near-zero) real demand, and `g_buf_pos` stops falling — a stable fixed point.

Alternative — decay toward zero: rejected, overshoots into starvation on a brief
demand dip; arrest-the-drift is the minimal correct target.

### D3: Self-gating via the drift condition itself

No "purge detection" or mode flag. The corrector keys on the buffer actually
drifting toward compression in NEUTRAL. In genuine high-flow the extruder
matches feed, `g_buf_pos` holds mid-band, the condition is false, and EST is
left alone. This makes the fix general (any abrupt demand-drop), not
purge-specific, and impossible to false-trigger during real extrusion.

Guard: require a short dwell in NEUTRAL (reuse the existing `> 2000u` gate on
the `model_stalled` branch) and `sync_current_sps > 0` so it only acts while
actively feeding. Also require **no recent TENSION** (the buffer just being
refilled legitimately) — reuse `sync_recent_negative_until_ms` / tension-pin
timing already tracked.

### D4: fast-brake arming on NEUTRAL → COMPRESSION, gated on hot MMU

`sync_on_transition` currently sets `sync_fast_brake_until_ms` only for
`prev == BUF_TENSION && now == BUF_COMPRESSION`. Extend to fire on
`now == BUF_COMPRESSION` from NEUTRAL **when `lane_motion_sps` is above a hot
threshold** (e.g. a fraction of baseline floor, or simply `> SYNC_MIN_SPS` by a
margin). Gating on hot feed avoids arming a hard brake on a slow, benign
NEUTRAL→COMPRESSION drift where the relay stop already suffices.

Alternative — always arm: rejected, would inject a 250 ms zero-feed stop on
every gentle compression touch, fighting the normal slow relay cycle.

## Risks / Trade-offs

- [Corrector engages on a genuine slow NEUTRAL-side reserve lean and bleeds EST
  too low → mild under-feed] → Gate on a real downward drift plus dwell, and
  target arrest-the-drift (current MMU − margin), not zero, so the fixed point
  sits at true demand, not starvation.
- [EST decayed during a brief pause then real extrusion resumes → momentary
  under-feed until the next TENSION catch-up re-learns] → Acceptable: a short
  refill latency is far less harmful than a wall slam; the TENSION catch-up
  path already exists to recover, and resumed demand pulls the buffer toward
  tension which re-raises EST quickly.
- [fast-brake hot threshold mistuned → brake too eager or too late] → Derive
  from existing geometry/baseline rather than a new magic number; validate
  against the captured purge trace and a normal high-flow run.
- [Behavior divergence in type-P] → All new logic gated to
  `BUF_SENSOR_TYPE == 0`; type-P path untouched and asserted byte-identical.

## Migration Plan

Pure firmware behavior change, no settings/struct change → no
`SETTINGS_VERSION` bump. Ship in one milestone commit. Rollback = revert the
commit. Validate by replaying the purge sequence on hardware and confirming
`g_buf_pos` stabilizes in-band (no COMPRESSION slam) while a normal high-flow
print shows unchanged TENSION/NEUTRAL cycling.

## Open Questions

Resolved for implementation:

- Drift condition uses raw `g_buf_pos` delta between sync ticks plus a
  compression-side reserve error. This catches an actual in-band slide toward
  COMPRESSION and ignores stationary high-flow NEUTRAL dwell.
- Because the type-D NEUTRAL relay multiplies `extruder_est_sps` by
  `RELAY_NEUTRAL_FRAC`, the estimator target is the current MMU feed divided by
  that relay fraction, minus the same small margin used by the existing
  tension-side model-stall corrector. This is the estimator value that arrests
  the present relay-driven drift; using raw MMU feed would raise the estimator
  when `RELAY_NEUTRAL_FRAC > 1`.
- The fast-brake hot threshold is `max(baseline_control_floor_sps() / 2,
  SYNC_MIN_SPS + PRE_RAMP_SPS)`: hot purge / high-flow coasts brake, slow
  floor-speed compression touches do not.
- NEUTRAL decay reuses the rail-corrector EWMA alpha (`0.05`) to keep estimator
  behavior continuous with the existing TENSION and COMPRESSION correctors.
