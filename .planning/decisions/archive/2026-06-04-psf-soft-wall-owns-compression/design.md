## Context

`sync_compression_recovery_active` is set TRUE on any `→ BUF_COMPRESSION`
transition inside `sync_on_transition()` (`sync.c:1916`) with **no sensor-type
gate**. It is consumed in `sync_tick()` at two sites, both ungated:

1. **Feed cap** (`sync.c:2470`): caps `target_sps` to
   `recovery_cap = extruder_est_sps - kp_window`, floored at
   `sync_compression_floor_sps()`, with a time-based collapse trim
   (`SYNC_COMPRESSION_COLLAPSE_DELAY_MS` / `_CAP_MS`).
2. **Continuous-compression collapse** (`sync.c:2582`): the held-in-COMPRESSION
   variant of the same trim.

For type-D this is the intended bang-bang drain: COMPRESSION = the only overfeed
signal, so brake to the floor and collapse until the buffer drains off the wall.

For type-P the same latch arms ~0.3 norm earlier than the soft wall and slams to
a hard floor that ignores demand (`SYNC_KP_SPS = 13811` >> `extruder_est`), so
the gradual Layer-2 compression blend never gets to act.

```
buffer pos →  goal ──────── 0.5 ──────────── 0.8 ──── 1.0 (comp rail)
                            │                  │
recovery cap (L2470):  █████│███████████████████████  arms here, slam→682 sps, latched
soft wall   (L2096):                          ░░░░░░░  proportional blend → 0
```

## Goals / Non-Goals

**Goals:**
- Type-P compression-overfeed backoff owned solely by the soft wall (Layer 2)
  and hard catch (Layer 3).
- Recovery cap + collapse remain type-D, unchanged.

**Non-Goals:**
- Removing or changing the `sync_compression_recovery_active` *flag* — it still
  gates `baseline_update_on_settle` (L1934) and the post-compression boost; those
  stay type-agnostic.
- Any change to type-D drain/collapse tuning.
- Soft-wall / hard-catch tuning (separate rig tasks 11.4/11.5).

## Decisions

### D1 — Gate the consumers, not the latch

Wrap the two feed-shaping blocks (L2470, L2582) in `BUF_SENSOR_TYPE == 0` rather
than gating the latch set at L1916. Keeping the flag live preserves its
type-agnostic side effects (settle suppression, post-comp boost) and matches the
existing pattern where shared state is set broadly but type-D-specific *shaping*
is gated at the consumer (cf. the tension-probe latch at L2504, the neutral relay
floor at L2496 — both gated at the consumer, not the producer).

*Alternative rejected*: gate the latch at L1916. Would also suppress the
post-compression boost and alter `baseline_update_on_settle` timing for type-P,
which are unrelated and currently correct.

### D2 — Soft wall is sufficient for type-P compression

Layer-2 (`psf_control_law` L2096) blends `target → 0` across
`|pos_norm| ∈ [0.8, 1.0]`; Layer-3 (L2149) faults/relief-pauses on sustained
saturation. Together they cover gradual overfeed (blend) and stuck overfeed
(saturation fault) without the latched hard floor. The recovery cap adds no
capability type-P lacks — only a steeper, earlier, latched version that fights
the PD smoothness.

*Risk*: if rig shows the soft wall alone is too soft (buffer rides deep into
compression before the blend bites), the fix is to lower `PSF_SOFT_WALL_START`
or add Kd, **not** to re-enable the type-D cap. Recorded as the rig fallback.

## Risks / Trade-offs

- **Soft wall under-brakes on aggressive overfeed** → lower `PSF_SOFT_WALL_START`
  / raise `KD_PSF`; do not re-enable the cap. Rig-verify.
- **Convention note**: the psf-analog-rig spec scenarios still read `-1 =
  compression`, but the code migrated to `+ = compression` (commit a95ccd6).
  This change is sign-agnostic (gates by sensor type, not by zone sign), so it is
  unaffected; the spec-convention cleanup is tracked separately.

## Open Questions

- Does the soft wall alone hold the buffer off the compression rail across the
  full feed-rate sweep, or is a Kd term required? Rig-measure.
