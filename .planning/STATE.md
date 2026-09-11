# Project State: FLARE

## Project Reference

See: `.planning/PROJECT.md` (updated 2026-09-11)

**Core value:** Autonomous, reliable dual-lane filament switching and reloading on runout with real-time sync-feedback buffer control.
**Core value:** Autonomous, reliable dual-lane filament switching and reloading on runout with real-time sync-feedback buffer control.
**Current focus:** Phase 8: Settings TLV / Delta Schema Migration

## Current Position

- **Phase**: 8 - Settings TLV / Delta Schema Migration (`.planning/phases/08-settings-tlv-migration/`)
- **Active Feature in Progress**: Phase 8 - Plan 08-01
- **Status**: Complete (Plan 08-01 complete and validated)
- **Progress**: [====================] 100% complete

## Accumulated Context

### Active Backlog Streams & Phases
- `.planning/phases/08-settings-tlv-migration/`:
  - `08-SPEC.md` (done): Packed Tag-Length-Value (TLV) flash encoding, 1024B buffer, v63 lazy migration, unknown tag pruning, bounds safety.
  - `08-01-PLAN.md` (done): Schema definitions, TLV serializer/deserializer, v63 fallback, parity test updates, host simulation tests.
- `.planning/phases/07-flash-pingpong-atomic-persistence/`:
  - `07-SPEC.md` (done): Dual 4 KB ping-pong sectors (0x1FE000 and 0x1FF000), monotonic sequence number `seq`, SETTINGS_VERSION 63, signed sequence arbitration, and CRC32 verification.
  - `07-01-PLAN.md` (done): Firmware save/load implementation, readback CRC verification, host unit test suite (`test_persistence.c`), and parity/regression integration.
- `.planning/phases/06-advanced-toolchange-reload/`:
  - `06-SPEC.md` (done): Cutter stall/watchdog interlocks, firmware bypass mode (`BYPASS=1`), Klipper pause escalation, and toolhead load retry/park contract.
  - `06-01-PLAN.md` (done): Cutter PWM de-energize on abort/fail, toolhead load retries (`tc_ts_retries`) and parking (`tc_ts_park_mm`), firmware bypass interlocks, and Klipper pause actuation.
- `.planning/phases/05-automated-calibration-live-tuning/`:
  - `05-01-PLAN.md` (done): Sensor calibration wizard (`flare_calibrate.py`), live tuner PSF proportional gain adjustment, and trace analyzer step-rate vs displacement charting (`flare_analyze.py --chart`)
- `.planning/phases/04-host-sync-sim-coverage/04-01-PLAN.md` (done): Multi-lane runout failover edge cases, Type-P PSF control law audit/tests, determinism & stress lag margin gates
- `.planning/phases/03-klipper-event-mirror-and-slicer-purge/03-01-PLAN.md` (done): Event-driven daemon mirror & dynamic OrcaSlicer purge hook
- `.planning/phases/02-buffer-retract-catch-hardening/`:
  - `02-01-PLAN.md` (done): CLI event completion (`BL`/`BS`) & Klipper macro cleanup
  - `02-02-PLAN.md` (done): Firmware fast prime & proportional rate servo catch
  - `02-03-PLAN.md` (done): Retract guard macros & host sim / bench validation
- `.planning/phases/01-hardware-validation-and-audit-closeout/01-01-PLAN.md`

---
*Last updated: 2026-09-11 after Phase 5 Plan 05-01 completion*
