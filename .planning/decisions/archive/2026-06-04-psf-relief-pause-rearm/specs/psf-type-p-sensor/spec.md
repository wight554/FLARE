## ADDED Requirements

### Requirement: Type-P Relief-Pause Auto-Recovery

The firmware SHALL re-arm type-P sync from `SYNC_RELIEF_PAUSE` when the buffer is
under genuine extruder demand, using the same demand discriminator as cold
auto-start (D18): `g_buf_pos < -0.6` AND (`g_sync_tension_transitioned` OR
`g_vel_norm < -0.1`). On re-arm the controller SHALL enter `SYNC_ACTIVE`,
bootstrap the feed via `sync_bootstrap_sps()`, set `sync_auto_started`, and emit
`SYNC:AUTO_START`. The type-P re-arm SHALL NOT reseed `g_buf_pos` (it measures
position directly).

A static rest at the home tension rail (`g_buf_pos` near `-1.0`, `g_vel_norm`
near 0, no armed tension transition) SHALL NOT re-arm, so `SYNC_RELIEF_PAUSE` is
not defeated by the buffer's resting position.

#### Scenario: Relief-pause recovers under demand

- **WHEN** `BUF_SENSOR_TYPE == 1`, `g_sync_state == SYNC_RELIEF_PAUSE`, and the
  buffer is actively falling toward tension (`g_buf_pos < -0.6` with
  `g_vel_norm < -0.1` or an armed tension transition)
- **THEN** sync re-arms to `SYNC_ACTIVE` and emits `SYNC:AUTO_START`
- **AND** `g_buf_pos` is not reseeded from `buf_target_reserve_mm()`

#### Scenario: Relief-pause holds at static home rest

- **WHEN** `BUF_SENSOR_TYPE == 1`, `g_sync_state == SYNC_RELIEF_PAUSE`, the buffer
  rests at the home tension rail with `g_vel_norm` near 0 and no armed tension
  transition
- **THEN** sync stays in `SYNC_RELIEF_PAUSE` and does not auto-restart

#### Scenario: Type-D relief recovery unchanged

- **WHEN** `BUF_SENSOR_TYPE == 0` and `g_sync_state == SYNC_RELIEF_PAUSE`
- **THEN** re-arm fires on `BUF_TENSION` or (`BUF_NEUTRAL` and `TASK_FEED`) as
  before, including the `g_buf_pos = buf_target_reserve_mm()` reseed
