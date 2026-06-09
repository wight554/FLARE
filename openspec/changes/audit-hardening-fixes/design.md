# Design: audit-hardening-fixes

## Context

Audit (2026-06-10) of firmware + host scripts surfaced 13 defects across four
classes: wire-protocol drift (host and firmware disagree on event format),
unguarded actuator surfaces (daemon LAN bind, CP mid-cut, CAL flash write),
trust-without-validation (flash load, microsteps), and dead guards (parity test
broken + never executed). All fixes are small and local; none touch sync
control laws. Findings cross firmware/scripts boundary, so one change carries
both sides to keep wire-format fixes atomic (firmware emit + host parse must
land together for BL events).

## Goals / Non-Goals

**Goals:**

- Single-prefix `EV:` wire format restored end-to-end (firmware emit, daemon
  classify, flare_cmd reconstruct, tuner regex).
- Every `settings_save()` path activity-gated; every drivetrain value
  validated at SET, gen, and load entry points.
- Cutter state machine immune to host-command hijack.
- Daemon API loopback-default.
- Settings parity guard actually runs in the gate.
- Port exclusivity across all host serial opens.

**Non-Goals:**

- No control-law, estimator, or tuning changes.
- No `settings_t` layout change (no `SETTINGS_VERSION` bump).
- No auth/token system for daemon (loopback default suffices now; token is
  follow-up if remote WebUI demand appears).
- No TMC write verification (IFCNT readback) — noted in audit as optional, out
  of scope.
- Low-severity audit notes not listed in proposal cleanups (CRC padding
  portability, find_port identity probe) stay unfixed, documented in audit
  report only.

## Decisions

**D1 — Fault-event budget bypass via flag, not new writer.**
`cmd_write_line` gains a `bool critical` path (or `cmd_event_critical()`
helper): skips `cmd_event_permitted()` budget, still honors
`stdio_usb_connected()`. Callers: `FAULT:*` (motion.c), `CUT:ERROR` (cutter.c),
BL `TIMEOUT` (sync.c). Alternative — raising the global budget — rejected:
keeps droppability for chatty events, which is the budget's purpose.

**D2 — Load-path validation reuses SET-path clamps.**
`settings_load_tmc()` clamps `microsteps` to the chopconf-valid set (snap to
nearest power of two), `full_steps` to {200,400}, `gear_ratio`/`rotation_distance`
to existing `TMC_*_MIN/MAX` consts before `mm_per_step` division. Shared
helpers, not duplicated literals. Alternative — falling back to full defaults
on any invalid field — rejected: loses all user settings for one bad field.

**D3 — Power-of-2 check shared shape across layers.**
Firmware: `SET:MICROSTEPS` validates `(v & (v-1)) == 0 && v >= 1 && v <= 256`.
`gen_config.py`: same check, hard exit. Same constant set as
`tmc_setup_chopconf`'s switch — that switch stays the source of truth.

**D4 — CP guard mirrors cutter_start guard.**
`cutter_test_us` returns bool; refuses unless `CUT_IDLE`/`CUT_BOOT_PARK`;
protocol replies `ER:BUSY`. Guard lives in cutter.c (state owner), not
protocol.c, so future callers inherit it.

**D5 — Daemon bind default flip only.**
`--host` default `127.0.0.1`; startup log warns when binding non-loopback.
Breaking for remote-WebUI users: documented in proposal; mitigation = one flag.

**D6 — Parity test becomes unittest.TestCase wrapping existing logic.**
Body scan switches from `func_body("settings_load")` to union of all
`settings_load\w*` function bodies (regex over file). Keeps zero-dependency
regex approach; no compile step. Alternative — parsing with libclang — rejected
(new dependency, overkill).

**D7 — Event reconstruction format centralized in daemon.**
Daemon broadcasts `event_type`/`event_data` unchanged; flare_cmd joins with
colon. Tuner regex rewritten to `^EV:([A-Z_]+):([A-Z_]+)` + startswith fixes;
unit test feeds literal firmware lines (pins format against future drift; the
EV-format test doubles as regression for F1).

**D8 — Exclusive opens fail fast.**
`exclusive=True` on every `serial.Serial`. Tools catch `SerialException` and
print actionable conflict message naming the daemon as likely owner.
macOS note: pyserial maps to TIOCEXCL — supported.

**D9 — TC ownership gate is one shared predicate, MV exempt.**
Single helper (`tc_busy()` = state not `TC_IDLE`/`TC_ERROR`) consulted by
`T:`, `TC:`, `RL:`, `UL:`, `UM:`(active-lane path), `LO:`, `FL:`, `FD:` →
`ER:BUSY`. `MV:` stays unguarded — existing comment documents it as the raw
recovery escape hatch. Also: `tc_start`/`tc_manual_reload` currently no-op
silently when busy while the handler replies `OK`; gate moves rejection into
the handler so every `OK` means "accepted". Alternative — making `tc_start`
return bool and branching per-handler — rejected: equivalent outcome, more
call-site churn.

**D10 — Docs fixed in same change, colon format normative.**
All 13 comma-format event references (MANUAL.md ×4, BEHAVIOR.md ×9) rewritten
to `EV:TYPE:DATA`; MANUAL events table completed (BL family, CUT:DONE/ERROR,
BUF_STAB REVERSE, SYNC subtypes); BEHAVIOR BL watchdog event renamed
`TIMEOUT`. Doc fixes ride the same change as the code so the wire format has
exactly one documented shape at merge. The tuner wire-format unit test (3.3)
is the mechanical guard; docs follow it.

**D11 — Live-delta ratchet: reset at sync-OFF, not decay.**
F10 fix = zero `g_flow_sched_live_delta[]` when sync transitions to OFF
(auto-stop or `SM:0`), keeping raise-only behavior *within* a sync session
(intentional: avoids learning idle lulls). Alternative — time-decay toward
schedule baseline — rejected: adds a tuning constant and fights the in-print
learner; session-boundary reset matches the mental model "learned flow is
per-print". Hardware judgment at apply may keep cross-print persistence if
prints prove it beneficial — then add a clamp instead.

**D12 — Motion-tracker wait loops scan, never requeue.**
Replace requeue-and-poll with: scan buffered `_messages` once for matching id,
else poll socket against a deadline. `poll()` keeps popping buffered messages
first; the bug is the producer-consumer requeue cycle in `list_objects`, fixed
at the call site.

## Risks / Trade-offs

- [BL `TIMEOUT` consumers] Hosts parsing the buggy `EV:EV:BL:*` today — none
  found in repo (daemon classified it as type `EV`, effectively discarding) →
  no migration needed; fix is strictly enabling.
- [Daemon bind flip breaks remote WebUI] → explicit `--host 0.0.0.0` restores;
  release note + startup warning.
- [exclusive=True surfaces latent double-open workflows] (tuner alongside
  daemon) → desired behavior: fail loud now instead of corrupting replies;
  tuner docs point to daemon-proxy mode.
- [Load-clamp snap changes behavior on corrupt flash] Previously hardfault or
  garbage motion; now clamped values may differ from user intent → acceptable:
  any CRC-valid-but-invalid field already means layout drift; clamps make boot
  survivable so operator can `RS`.
- [CAL gate may annoy mid-print calibration habit] CAL during motion was never
  safe (stop_all + flash stall); `ER:PERSIST_BUSY` is the correct refusal.
- [F8 gate breaks a host workflow that "recovers" by re-issuing T:/TC: mid-TC]
  → correct recovery path is `tc_abort` via existing error handling or `MV:`;
  Klipper macros audited — none issue gated commands mid-TC today.

## Migration Plan

1. Firmware fixes land first (single commit; build + unit gate).
2. Script fixes second (daemon/flare_cmd/tuner/gen_config/parity test).
3. Reflash board; restart daemon (`install_daemon.sh` service picks up new
   default — verify WebUI still reachable from the Pi itself).
4. Rollback: revert commit; no flash-layout change, settings survive both ways.

## Open Questions

- None blocking. Token auth for non-loopback daemon bind deferred until a
  remote-WebUI use case lands.
