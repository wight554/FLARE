## Context

NOSF is standalone RP2040 firmware. It never receives host extruder speed,
extrusion deltas, planner state, or encoder data. The sync controller
(`firmware/src/sync.c`) estimates extruder rate from zone-dwell measurements
at endstop crossings, dead-reckons a virtual buffer position between
crossings, and biases the buffer toward the full/reserve side (target between
MID and TRAILING via `buf_target_reserve_mm`).

Current jam/overfull handling is destructive. `sync_disable(true)`
(`sync.c:702`) zeroes the extruder estimator, drift observer (2.6), sigma /
confidence (2.5), reserve integral, and advance-pin window. It is called on
routine overfull events (continuous-trailing auto-stop `sync.c:1295`,
advance-dwell stop `sync.c:1195`, hard wall `sync.c:1245`) and from
`protocol.c`/`motion.c` for true OFF transitions. The live baseline learner
`baseline_update_on_settle` (`sync.c:688`, triggered `sync.c:835`) runs one
EWMA per `*→MID` settle with only `sync_enabled && dwell>500ms` gating, so it
can learn from settles immediately following abnormal control.

Tracing established two important facts:
1. The trailing path is already a graded escalation ladder: soft-wall trim
   (`sync.c:1174`) → collapse ramp/cap (`sync.c:1226,1261`) → terminal
   destructive disable (`sync.c:1295`); hard wall (`sync.c:1241`) is a
   separate fast emergency path. Only the two terminal destructive disables
   are the problem.
2. The hybrid live/offline learning split already exists structurally:
   `g_baseline_target_sps` is config-set and persisted (settings_store) =
   offline authority; `g_baseline_sps` is RAM-only, non-persistent, and via
   `baseline_control_floor_sps() = max(g_baseline_sps, g_baseline_target_sps)`
   can only ratchet the control floor up. A bad live drift self-heals on
   reboot and never poisons offline log data.

## Goals / Non-Goals

**Goals:**
- Replace destructive disable on the two terminal jam paths with explicit
  non-destructive states that preserve estimator/drift/sigma/integrators.
- Make sync lifecycle explicit and auditable (single state enum, defined
  transitions) instead of scattered booleans and early-returns.
- Discipline the live baseline learner without removing it.
- Add warn-only relief-effort diagnostics.
- Zero regression to the full-bias buffer behavior and to SYNC_ACTIVE control
  output.

**Non-Goals:**
- No Klipper/Marlin integration, host extruder speed, extrusion deltas,
  planner coupling, or encoder.
- No flow-keyed parameter schedule (deferred to dependent follow-up change
  `flow-keyed-param-schedule`).
- No persistence-format change; live baseline stays non-persistent.
- No EKF / heavy floating control for the digital dual-endstop path.
- No change to `buf_target_reserve_mm`, `reserve_correction`, `zone_bias`,
  soft-wall trim, or collapse ramp/cap.

## Decisions

### D1 — Single sync state enum, behavior-preserving mapping

States: `SYNC_OFF`, `SYNC_ACTIVE`, `SYNC_HOLD`, `SYNC_RELIEF_PAUSE`,
`SYNC_FAULT_HOLD`. Existing booleans (`sync_enabled`, `sync_auto_started`,
`g_sync_hold`, recovery flags) are derived from / driven by the state, not
replaced piecemeal. `sync_disable()` API is retained but reclassified:

| Current call site | New target |
|---|---|
| `protocol.c` toolchange/TS/unload, `motion.c:455` tail finish | `SYNC_OFF` (destructive reset retained — true off) |
| HD command (`protocol.c:506`) | `SYNC_HOLD` (non-destructive) |
| continuous-trailing auto-stop (`sync.c:1295`) | `SYNC_RELIEF_PAUSE` |
| hard-wall critical (`sync.c:1245`) | `SYNC_FAULT_HOLD` |
| advance-dwell stop (`sync.c:1195`) | `SYNC_FAULT_HOLD` (empty-side jam: cannot refill) |
| tail-assist auto-stop (`sync.c:943`) | `SYNC_OFF` (tail assist genuinely done) |

Rationale: a typed lifecycle makes the preserve-vs-reset contract explicit and
testable; piecemeal boolean edits would leave the destructive/non-destructive
distinction implicit and regression-prone. Alternative (add one
`relief_pause` bool) rejected — would not capture HOLD/FAULT distinctions or
the preservation contract cleanly.

### D2 — Keep the graded trailing ladder; swap only terminals

Soft-wall trim and collapse ramp/cap are the tuned smooth approach and stay
byte-for-byte. Only the two terminal `sync_disable(true)` calls become state
transitions. `SYNC_RELIEF_PAUSE` resumes on ADVANCE re-arm — identical to the
existing auto-stop→AUTO_START cycle, so observable timing/behavior is
unchanged except that re-arm reuses preserved estimator/drift instead of cold
bootstrap. Alternative (RELIEF_PAUSE replaces the collapse ramp) rejected:
would re-tune known-good behavior and risk full-bias regression.

### D3 — State preservation contract

| State | Closed loop | Motor | Estimator/drift/sigma/integral | Learning | Resume |
|---|---|---|---|---|---|
| OFF | off | per caller | reset (destructive) | off | external command / AUTO_START |
| ACTIVE | on | sync output | live + updated | gated on | — |
| HOLD | off | buffer-stabilize→MID | **preserved** | off | host clears HD |
| RELIEF_PAUSE | off (assist 0/floor) | stop/floor | **preserved** | off | buffer reaches ADVANCE → ACTIVE |
| FAULT_HOLD | off | stopped | **preserved** | off | auto, conservative, after stable interval; no host |

HOLD stabilizes to MID (not reserve target) because HD is only sent when
paused/idle — extruder not pulling, so MID is safe and there is no full-bias
concern. RELIEF_PAUSE/FAULT_HOLD never drain by design (assist 0/floor while
buffer is already overfull); the full-bias band is reached again only via
normal SYNC_ACTIVE control after re-arm.

### D4 — Disciplined live baseline learner

Keep `g_baseline_sps` live, ephemeral, up-only (`max()`), non-persistent.
Discipline `baseline_update_on_settle` trigger:
- Only when state == `SYNC_ACTIVE` (not HOLD/RELIEF/FAULT, not fast-brake, not
  trailing-recovery active).
- Require N consecutive comparable settle observations before moving (multi-
  cycle), not one.
- Reject if recent settle variance exceeds a relative threshold (outlier /
  low-confidence reject).
- Cooldown: minimum elapsed time AND minimum commanded-MMU distance since last
  update.
Offline analyzer remains the only persistent authority
(`g_baseline_target_sps` via config/`BASELINE_SPS`). Concrete N / variance /
cooldown constants are tuning parameters chosen from existing bucket data
during implementation; they are config tunables, not hardcoded magic.

### D5 — Warn-only FlowGuard-style effort counters

Accumulate effort in commanded-MMU mm (exact, locally known):
- Sustained ADVANCE: `refill_effort_mm`; on threshold while still ADVANCE →
  emit `SYNC cannot_refill` event (generalizes the existing `g_adv_pin_ts`
  window into a distance metric).
- Sustained TRAILING: `relief_effort_mm`; on threshold while still TRAILING →
  emit `SYNC cannot_relieve`.
Diagnostics only in this change — no control behavior derives from them yet
(keeps regression surface zero; future changes may act on them).

### D6 — Out-of-scope flow-keyed schedule, dependency direction

The root cause of regime-dependent offline recommendations (scalar
baseline/bias differs per print speed mix) is acknowledged but addressed by
the follow-up change `flow-keyed-param-schedule`, which **depends on** this
change (built after it) to avoid touching the same baseline/bias path twice
concurrently. Interim: scalar + disciplined live (D4) absorbs in-regime
variation; the scalar path is preserved as the future schedule's degenerate
single-point fallback.

## Risks / Trade-offs

- [State model touches every `sync_disable` caller — large blast radius] →
  Explicit caller→state mapping table (D1); retain `sync_disable` for OFF only
  with byte-identical reset; behavior-parity check on SYNC_ACTIVE output.
- [Preserved estimator goes stale during long pause] → Keep existing
  `SYNC_EST_FRESH_MS` aging; re-arm path falls back to `sync_bootstrap_sps`
  when stale, same as today.
- [RELIEF_PAUSE could starve extruder if it never re-arms] → Resume trigger is
  ADVANCE (buffer emptied), identical to current cycle; ADVANCE is never
  paused; under-extrusion direction always wins.
- [Disciplined learner could stop adapting on legitimately mixed prints] →
  Live tier is bounded, up-only, ephemeral; worst case degrades to config
  baseline (current floor behavior), never below; offline authority unchanged.
- [Effort counters add per-tick math on RP2040] → Integer mm accumulators,
  no float-heavy work; warn-only, rate-limited events.
- [Hidden coupling between HD path and buffer-stabilize] → HD only sent
  paused/idle (confirmed); SYNC_HOLD reuses existing `buffer_stabilize`
  NEG_SYNC→MID logic unchanged.

## Migration Plan

Phased within one change, safety-first ordering so each phase is independently
shippable and revertable:
1. Phase A (lowest risk): D4 disciplined learner gate + D5 warn-only counters.
   No control-path behavior change.
2. Phase B (core): D1 state enum + D2/D3 reclassify the two terminal disables
   to RELIEF_PAUSE/FAULT_HOLD and HD→HOLD; preserve-vs-reset contract.
3. Validate locally (`ninja -C build_local`) with scenario notes; assert no
   Klipper/host coupling introduced; behavior-parity review of SYNC_ACTIVE.

Rollback: revert per phase; `sync_disable` API retained so reverting Phase B
restores prior destructive behavior without touching Phase A.

## Open Questions

- Exact disciplined-learner constants (N consecutive settles, variance
  fraction, cooldown ms / mm) — derive from existing bucket/run data during
  implementation; expose as config tunables.
- FAULT_HOLD "stable interval" duration for auto conservative recovery —
  choose from observed hard-wall recovery timing; conservative default.
- Whether advance-dwell stop (`sync.c:1195`) FAULT_HOLD recovery should differ
  from hard-wall recovery (empty-side vs full-side); default: same
  conservative auto path unless data shows otherwise.
