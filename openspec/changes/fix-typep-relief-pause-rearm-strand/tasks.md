## 1. Fix #3 — no stabilize during relief-pause

- [x] 1.1 In `buffer_stabilize_controller_idle()` (`firmware/src/sync.c:177`) add
  `g_sync_state != SYNC_RELIEF_PAUSE` to the false conditions, so no stabilize
  (negative-sync or idle) can start while paused.
- [x] 1.2 Grep other `buffer_stabilize_start_internal` / `buffer_stabilize_request`
  callers to confirm the chokepoint covers them; no caller bypasses
  `buffer_stabilize_controller_idle()`.

## 2. Fix #1 — type-P rearm-on-transition

- [x] 2.1 In `firmware/src/sync_buf.c:838`, split the RELIEF_PAUSE
  rearm-on-transition by sensor type: keep type-D
  (`NEUTRAL || TENSION`) path; add type-P path that calls
  `sync_rearm_active(lane, now_ms)` when `new_state == BUF_TENSION`.
- [x] 2.2 Confirm `sync_rearm_active` reseed of `g_buf_pos` stays guarded by
  `BUF_SENSOR_TYPE_D` (`sync.c:1071`) so the type-P path does not reseed position.
- [x] 2.3 Keep the `!g_boot_stabilizing` guard intact (belt-and-suspenders with #3).

## 3. Validation

- [x] 3.1 Build dev superset: `-DFLARE_DEV_TUNING=ON` (per
  memories/repo/dev-tuning-build-blindspot) + default `build_local`; clang-tidy
  clean.
- [x] 3.2 Run host regression/parity tests; ensure type-P relief-pause scenarios
  in `psf-type-p-sensor` pass (note unittest-discover silent-skip gotcha,
  memories/repo/audit-hardening-fixes).
- [ ] 3.3 Rig: reproduce slow-after-fast edge case; confirm no `BUF_STAB:START`
  during RELIEF_PAUSE, sync re-arms (`SYNC:AUTO_START`) on fast-feature tension
  without stranding, no manual perturbation needed.
- [ ] 3.4 Rig: verify type-D relief recovery unchanged (regression check).
