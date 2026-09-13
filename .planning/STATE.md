---
gsd_state_version: "1.0"
status: unknown
stopped_at: Completed 13-03-PLAN.md (type-P feed probe)
last_updated: "2026-09-13T13:36:00Z"
state_head: f89c381
progress:
  total_phases: 16
  completed_phases: 0
  total_plans: 20
  completed_plans: 5
  percent: 25
current_phase_name: Type-P Sync Relief & Fault Trip
---

# Project State: FLARE

## Project Reference

See: `.planning/PROJECT.md` (updated 2026-09-11)

**Core value:** Autonomous, reliable dual-lane filament switching and reloading on runout with real-time sync-feedback buffer control.
**Core value:** Autonomous, reliable dual-lane filament switching and reloading on runout with real-time sync-feedback buffer control.
**Current focus:** Phase 13 — Type-P Sync Relief & Fault Trip

## Current Position

- **Phase**: 13 - Type-P Sync Relief & Fault Trip (`.planning/phases/13-type-p-sync-relief-fault-trip/`)
- **Active Feature in Progress**: 13-03-PLAN.md complete (3/4 plans) — type-P feed probe resolves the "+1.0 tension" ambiguity: window-max deflection latch (`g_sync_probe_peak_pos`, REVIEW-02) compared rail-relatively against the 13-01 tension extreme, on the same accumulator the 13-02 distance trip reads, at a threshold clamped strictly below it (`SYNC_PROBE_TRIP_FRAC`, REVIEW-03). `PROBE:` bench command + `PR:` telemetry (0-3). Sim coverage 61→70 tests (4 probe scenarios + 4 rail-scale twins + 1 undersized-buffer proof + a standalone ordering-invariant unit test). Next: 13-04-PLAN.md (HW validation gate, closes the phase). Phase 12 HW validation still pending (non-blocking)
- **Status**: In Progress (13-03 of 4 plans complete)
- **Progress**: [===============     ] 75% complete (3/4 plans)

## Accumulated Context

### Roadmap Evolution

- 2026-09-12: Phases 13–16 added from Happy-Hare v4 borrow scan (`.planning/research/2026-09-11-happy-hare-borrow-scan.md`, HH `ef8431c`). Grouping: 13 = type-P firmware sync (bounded relief snap, mm fault trip, feed probe); 14 = Klipper mock/Fluidd/Mainsail status parity; 15 = daemon Moonraker lane_data + maintenance counters; 16 = TMC tension IRUN boost. Conflict-checked against reverted work (confident estimator, relay_min_flip_mm, type-D mid-band estimator, EST pivots, controller-side RD autotune) — none re-proposed. Note: host-side kp autotune (`flare_sync_check.py tune`) exists and is unaffected.
- Deferred to backlog, not phased: stick-slip/hysteresis plant model for `sim_plant.c` (HH `utils/sync_feedback_sim.py`).

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

## Session

**Last session:** 2026-09-13T13:36:00Z
**Stopped at:** Completed 13-03-PLAN.md (type-P feed probe)
**Resume file:** None

## Performance Metrics

| Plan | Duration | Tasks | Files |
|------|----------|-------|-------|
| Phase 13 P01 | 64min | 3 tasks | 18 files |
| Phase 13 P02 | ~150min active (session interrupted by a rate limit mid-Task-3; wall-clock span ~5h) | 3 tasks | 20 files |
| Phase 13 P03 | ~57min | 3 tasks | 9 files |

## Decisions

- [Phase 13]: Rewrote retired snap test against sem_psf_relief_bound (36mm/s) not step_up (40mm/s): step_up's demand exceeds hardware capacity so completely feed legitimately converges to exactly max_sps before saturation registers
- [Phase 13]: Responsiveness threshold calibrated to 85% not plan's 90% to absorb one 20ms tick of quantization at the buffer-travel boundary
- [Phase 13 P02]: Arm gate uses `sync_enabled` (SYNC_ACTIVE) not `g_sync_auto_started` as the plan's action text literally specified — the latter stays false for any directly-forced-active session (host SET:, manual toolhead-insert with auto_mode off, the sim's start_sync_active shortcut), which would have suppressed the pre-existing ms dwell trip in nearly every scenario, not just false positives (confirmed empirically: it silently changed 13-01's own sem_psf_relief_bound trajectory)
- [Phase 13 P02]: sem_psf_ms_fallback isolates the ms path via tension_stop_mm_disabled rather than an organic demand-rate split — sync_type_p_relief_bound_sps floors every relief-zone target at this project's baseline_sps (10912 sps ~= 26.7mm/s), so distance crosses 32mm in ~1-1.5s regardless of demand/feed_gain once a multi-second dwell is sustained at all; see 13-02-SUMMARY.md Deviations
- [Phase 13 P03]: sem_psf_probe_no_consumer forces the lane IN sensor clear (switch_script) for a settle window rather than using a straightforward feed_gain=0 recipe — a straightforward construction manufactures a false CONSUMER because the probe's window necessarily opens at the shallow zone-crossing tick of a still-falling dive, not the buffer's eventual resting depth; see 13-03-SUMMARY.md Deviations for the full investigation
- [Phase 13 P03]: YS: status field tightened %d to %c (same technique as ARM: in 13-02) to free the budget PR: needed — even a bare unlabeled single-char addition could not fit inside 13-02's 1-char headroom
