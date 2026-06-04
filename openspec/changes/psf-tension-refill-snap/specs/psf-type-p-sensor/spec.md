## ADDED Requirements

### Requirement: Type-P Tension Refill Snap

The firmware SHALL bypass the type-P distance-based feed smoothing on the
tension/refill side so a starved buffer is refilled without the EMA ramp lag.
When `BUF_SENSOR_TYPE == 1`, the buffer is in the tension soft-wall zone
(`buf_pos_norm() < -PSF_SOFT_WALL_START`), and the control target exceeds the
current feed (`target_sps > sync_current_sps`), the applied feed SHALL be set
directly to the soft-wall target (`max_sps`). On that snap the smoothing filter
SHALL be seeded at the demand estimate (`extruder_est_sps`), so that once the
buffer leaves the wall the feed eases to the extruder rate rather than remaining
at max and overshooting into compression. Outside the tension wall, and on the
compression/neutral side, the existing distance-EMA + wall-clock-decay smoothing
is unchanged. Type-D feed application is unchanged.

#### Scenario: Fast move does not starve the buffer

- **WHEN** `BUF_SENSOR_TYPE == 1` and a fast extruder move pulls the buffer into
  the tension soft-wall zone with `target_sps > sync_current_sps`
- **THEN** `sync_current_sps` is set to the soft-wall target (`max_sps`) that tick
- **AND** the buffer is refilled without raising `SYNC:cannot_refill`

#### Scenario: Feed settles to demand on recovery

- **WHEN** the buffer climbs back out of the tension wall after a refill snap
- **THEN** the smoothing resumes from `g_psf_target_filt = extruder_est_sps`
- **AND** the feed eases to the extruder rate instead of overshooting into
  compression

#### Scenario: Compression side unaffected

- **WHEN** `BUF_SENSOR_TYPE == 1` and the buffer is on the compression/neutral side
- **THEN** the distance-EMA + wall-clock-decay smoothing applies unchanged
