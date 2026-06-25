## MODIFIED Requirements

### Requirement: Type-P Relief-Pause Auto-Recovery

The firmware SHALL re-arm type-P sync from `SYNC_RELIEF_PAUSE` when the buffer is
under genuine extruder demand, via two complementary paths:

1. **Polled predicate** (unchanged): in `sync_tick_gated_checks`, re-arm when
   `g_buf_pos < -0.6` AND (`g_sync_tension_transitioned` OR `g_vel_norm < -0.1`).
2. **Transition-driven** (new): on a debounced buffer transition into the type-P
   tension zone, the firmware SHALL re-arm immediately — mirroring the type-D
   rearm-on-transition path so recovery does not depend solely on a non-zero
   `g_vel_norm` that vanishes when the buffer is pinned at the rail.

On re-arm the controller SHALL enter `SYNC_ACTIVE`, bootstrap the feed via
`sync_bootstrap_sps()`, set `sync_auto_started`, and emit `SYNC:AUTO_START`. The
type-P re-arm SHALL NOT reseed `g_buf_pos` (it measures position directly).

The firmware SHALL NOT start idle or negative-sync buffer-stabilize while
`g_sync_state == SYNC_RELIEF_PAUSE`. Relief-pause is a sync-recovery state, not an
idle state: starting stabilize sets `g_boot_stabilizing`, which blacks out
`sync_tick` (and thus the relief re-arm) and can `buf_force_stable_state(BUF_NEUTRAL)`
the buffer — zeroing `g_buf_pos` and bypassing `sync_on_transition` — stranding
the controller. A returning fast feature SHALL be able to re-arm sync directly
from relief-pause without waiting for a stabilize to finish.

A static rest at the home tension rail (`g_buf_pos` near `-1.0`, `g_vel_norm`
near 0, no armed tension transition, no fresh tension transition) SHALL NOT
re-arm, so `SYNC_RELIEF_PAUSE` is not defeated by the buffer's resting position.

#### Scenario: Relief-pause recovers under demand

- **WHEN** `BUF_SENSOR_TYPE == 1`, `g_sync_state == SYNC_RELIEF_PAUSE`, and the
  buffer is actively falling toward tension (`g_buf_pos < -0.6` with
  `g_vel_norm < -0.1` or an armed tension transition)
- **THEN** sync re-arms to `SYNC_ACTIVE` and emits `SYNC:AUTO_START`
- **AND** `g_buf_pos` is not reseeded from `buf_target_reserve_mm()`

#### Scenario: Relief-pause recovers on tension transition while pinned

- **WHEN** `BUF_SENSOR_TYPE == 1`, `g_sync_state == SYNC_RELIEF_PAUSE`, and the
  debounced buffer state transitions into the type-P tension zone while
  `g_vel_norm` is near 0 (buffer pinned at the rail under a fast feature)
- **THEN** the transition-driven path re-arms sync to `SYNC_ACTIVE` and emits
  `SYNC:AUTO_START` without waiting for a non-zero velocity reading

#### Scenario: No stabilize starts during relief-pause

- **WHEN** `BUF_SENSOR_TYPE == 1`, `g_sync_state == SYNC_RELIEF_PAUSE`, and the
  buffer rests in compression (the condition that would otherwise start
  negative-sync stabilize)
- **THEN** no `BUF_STAB:START` is emitted and `g_boot_stabilizing` stays false
- **AND** `sync_tick` continues to run its relief re-arm check each pass

#### Scenario: Relief-pause holds at static home rest

- **WHEN** `BUF_SENSOR_TYPE == 1`, `g_sync_state == SYNC_RELIEF_PAUSE`, the buffer
  rests at the home tension rail with `g_vel_norm` near 0 and no armed or fresh
  tension transition
- **THEN** sync stays in `SYNC_RELIEF_PAUSE` and does not auto-restart

#### Scenario: Type-D relief recovery unchanged

- **WHEN** `BUF_SENSOR_TYPE == 0` and `g_sync_state == SYNC_RELIEF_PAUSE`
- **THEN** re-arm fires on `BUF_TENSION` or (`BUF_NEUTRAL` and `TASK_FEED`) as
  before, including the `g_buf_pos = buf_target_reserve_mm()` reseed
