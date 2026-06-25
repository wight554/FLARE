# psf-relief-pause-rearm (archived 2026-06-04)

- Type-P +1.0 tension rail is ambiguous (home vs starvation); resolve via (mode × filament_present), not tension value alone.
- Sync safety stays firmware-local: NO host-pause actuator — Happy-Hare `cmd_MMU_PAUSE` is a stub; firmware relief/fault path must not depend on host pausing.
- Spec: `psf-type-p-sensor` (relief pause/rearm scenarios). Rig-validated at archive.

## Slow-after-fast strand follow-up (fix-typep-relief-pause-rearm-strand, archived 2026-06-25)

- Strand bug: type-P stuck in `SYNC_RELIEF_PAUSE` after slow-following-fast; the polled velocity/flag rearm predicate becomes unsatisfiable once the buffer is pinned at the tension rail (`g_vel_norm≈0`).
- Fix #1: type-P transition-driven rearm from RELIEF_PAUSE (`sync_buf.c:838`) — on debounced cross into BUF_TENSION call `sync_rearm_active`; type-D path unchanged. Type-P rearm must NOT reseed `g_buf_pos` (reseed stays guarded `BUF_SENSOR_TYPE_D`, `sync.c:1071`).
- Fix #3: do not start idle/negative-sync stabilize while `g_sync_state == SYNC_RELIEF_PAUSE` (`buffer_stabilize_controller_idle`) — stabilize sets `g_boot_stabilizing` which blacks out the rearm and can `buf_force_stable_state(NEUTRAL)` strand it.
- HW VALIDATED ~1000 swaps (2026-06-25): no `BUF_STAB:START` during RELIEF_PAUSE, sync re-arms (`SYNC:AUTO_START`) without manual perturbation.
