# Project State: FLARE

## Project Reference

See: `.planning/PROJECT.md` (updated 2026-09-11)

**Core value:** Autonomous, reliable dual-lane filament switching and reloading on runout with real-time sync-feedback buffer control.
**Core value:** Autonomous, reliable dual-lane filament switching and reloading on runout with real-time sync-feedback buffer control.
**Current focus:** Phase 12: Post-Phase 2–10 Regression Fixes

## Current Position

- **Phase**: 12 - Post-Phase 2–10 Regression Fixes (`.planning/phases/12-post-phase-2-10-regression-fixes/`)
- **Active Feature in Progress**: none — Phase 12 Plan 12-01 complete; HW validation pending
- **Status**: Complete (software); `HW:` items in 12-SPEC.md unchecked
- **Progress**: [====================] 100% complete

## Accumulated Context

### Active Backlog Streams & Phases
- `.planning/phases/12-post-phase-2-10-regression-fixes/`:
  - `12-SPEC.md` (done): 12 requirement groups from the 2026-09-11 review — 3 HIGH firmware (load park FAULT_BUF, cutter abort limp, bare-BL catch creep), 1 HIGH host (--dump rebuild), heartbeat lockout, STOP latch, daemon host decision.
  - `12-REVIEW.md` (done): severity-ranked findings, clean-spec list, eb0a942 migration audit.
  - `12-01-PLAN.md` (done 2026-09-11): 9 fix commits 842dbb0..8aaba7e, gate green, 261 py tests, decision D12.1 (daemon bind 0.0.0.0 + auth).
  - Next: rig-validate the four HW items in 12-01-PLAN.md, then `gsd-next`.
- `.planning/phases/11-firmware-forensics-jitter/`:
  - `11-SPEC.md` (done): Retention RAM crash logging, GET:CRASHLOG, CAL:CRASHLOG_CLEAR, and loop jitter/headroom instrumentation.
  - `11-01-PLAN.md` (done 2026-09-11): forensics ring + edge-detected transitions, `GET:CRASHLOG`/`CAL:CRASHLOG_CLEAR`/`GET:LOOP_STATS`, loop timing helpers, `test_forensics` 9/9, docs. HW watchdog check pending.
- `.planning/phases/10-tmc-heartbeat-recovery/`:
  - `10-SPEC.md` (done): CHOPCONF sentinel verification, 1000ms alternating cadence, strict idle motion lockout, 3x re-apply recovery escalation, ST: TMC telemetry, and host simulation tests.
  - `10-01-PLAN.md` (done): Firmware heartbeat tick, ST: telemetry, daemon status mirroring, and host unit tests.
- `.planning/phases/09-daemon-security/`:
  - `09-SPEC.md` (done): Bearer token authentication, loopback exemption, anti-spoofing peer resolution, token-bucket serial rate limiter, WebUI modal and CLI auto-discovery.
  - `09-01-PLAN.md` (done): Daemon security enforcement, CLI remote integration, WebUI localStorage token handling, 13 security unit tests.
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
*Last updated: 2026-09-11 after Phase 12 Plan 12-01 completion*
