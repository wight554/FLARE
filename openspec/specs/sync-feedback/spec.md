# sync-feedback Specification

## Purpose
TBD - created by archiving change psf-soft-wall-owns-compression. Update Purpose after archive.
## Requirements
### Requirement: Compression Recovery Cap Gated to Type-D

The firmware SHALL apply the shared `sync_compression_recovery_active` feed cap
and its time-based collapse trim only when `BUF_SENSOR_TYPE == 0` (type-D). For type-P,
compression-overfeed backoff SHALL be owned solely by the soft wall (Layer 2) and
hard catch (Layer 3) in `psf_control_law` / `sync_tick`; the recovery cap SHALL
NOT reduce the type-P feed target.

The `sync_compression_recovery_active` flag itself MAY still be set on a
`→ BUF_COMPRESSION` transition for both sensor types; only the feed-cap and
collapse-trim consumers are type-D gated. Type-agnostic consumers of the flag
(baseline-settle suppression, post-compression boost) are unchanged.

#### Scenario: Type-D compression recovery caps feed

- **WHEN** `BUF_SENSOR_TYPE == 0` and `sync_compression_recovery_active` is true
- **THEN** `target_sps` is capped to `extruder_est_sps - kp_window`, floored at
  `sync_compression_floor_sps()`, with the time-based collapse trim applied
- **AND** the behavior is unchanged from before this change

#### Scenario: Type-P compression backoff owned by soft wall

- **WHEN** `BUF_SENSOR_TYPE == 1` and the buffer is overfed into the compression
  zone
- **THEN** the `sync_compression_recovery_active` feed cap does NOT reduce
  `target_sps`
- **AND** the only compression-side feed reduction is the `psf_control_law` soft
  wall blending `target → 0` across `|pos_norm| ∈ [PSF_SOFT_WALL_START, 1.0]`

#### Scenario: Type-P transient overfeed does not latch a feed floor

- **WHEN** `BUF_SENSOR_TYPE == 1` and a brief overfeed dips the buffer into the
  compression zone and then recovers
- **THEN** the feed target is not latched to `sync_compression_floor_sps()` for
  the drain back to neutral
- **AND** feed follows the continuous PD + soft-wall output

