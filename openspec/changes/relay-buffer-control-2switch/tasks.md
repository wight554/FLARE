## 1. Relay control core

- [x] 1.1 Add relay control fractions (current live knobs:
  `SYNC_RELAY_CATCHUP_FRAC` / `SYNC_RELAY_NEUTRAL_FRAC`)
- [x] 1.2 Override `target_sps` before the ramp/clamp, gated
  `BUF_SENSOR_TYPE == 0` (D=0): TENSION→catch-up,
  COMPRESSION→stop, NEUTRAL→hold
- [x] 1.3 Confirm ramp, `[SYNC_MIN,max]` clamp,
  fast-brake, relief logic still run after the override

## 2. Disarm FAULT_HOLD on normal switch contact

- [x] 2.1 Gate tension-dwell FAULT_HOLD to `BUF_SENSOR_TYPE != 0` (P=1)
- [x] 2.2 Gate `compression_wall_critical` FAULT_HOLD to
  `BUF_SENSOR_TYPE != 0` (P=1)
- [x] 2.3 Confirm RELIEF_PAUSE / continuous-trailing auto-stop unchanged

## 3. Validation

- [x] 3.1 `cmake --build build_local`
- [x] 3.2 `python3 -m py_compile scripts/*.py`
- [x] 3.3 `openspec validate relay-buffer-control-2switch --strict`
- [x] 3.4 Type-P analog parity reasoning: `BUF_SENSOR_TYPE != 0` (P=1)
  path unchanged
- [x] 3.5 `TEST_CASES.md`: type-D relay-law regression entry

## 4. Closeout

- [x] 4.1 Commit + push to main
- [x] 4.2 On-Pi A/B: tune `SYNC_RELAY_*_FRAC` for a slow, shallow,
  never-TENSION, never-faulting cycle. **Baseline locked: `CATCHUP=1.30`,
  `NEUTRAL=1.25`** (round-2 log, see round log below). Note: old
  "never-ADVANCE" wording = new "never-TENSION" (rename; see 7.1).

  Interactive A/B loop (each round):
  - Operator sends: (a) param dump — `flare_cmd "?:"` once + the SET/config
    dump (current `SYNC_RELAY_CATCHUP_FRAC`/`NEUTRAL_FRAC`, `baseline_rate`,
    `SYNC_COMPRESSION_BIAS_FRAC`, `SYNC_MIN/MAX_SPS`); (b) a
    `flare_cmd "?:" --poll 500 > round-N.log` spanning a real print long
    enough to show several switch flips (not a 2 s window); (c) current
    frac pair + any `EV:SYNC:*` events observed.
  - Reviewer returns: cycle diagnosis (dwell, depth, TENSION pins, fault
    events) + next CATCHUP/NEUTRAL pair + expected effect + stop/continue.
  - Operator applies the pair, rebuilds (`#define` until knobs→config in
    `relay-duty-estimator-and-tuning`), flashes, reprints, recaptures.
  - Exit when: slow + shallow + never-TENSION + never-fault.

  Deliverable: the final frac pair + the `?:` poll log proving the cycle.
  This log/pair is the known-good baseline that gates
  `relay-duty-estimator-and-tuning` §0.1 (its estimator bounds against it).
  This hand-loop produces the baseline only; the deterministic offline
  `flare_analyze` path remains the persistent tuning authority.

  ### A/B round log

  - **Round 1** (2026-05-19). In: `CATCHUP=1.45`, `NEUTRAL=1.10`
    (`SYNC_MIN=692`, `RAMP_DN=553`, collapse ×3 / 250 ms). Print + stop
    poll. Diagnosis: steady-state good (NEUTRAL parks BP ~ -4.2..-5.0,
    slow shallow full-lean), but two failures — (1) TENSION spikes
    (BPN 43/45/47/50/52/54/56, 3× `TENSION_RISK_HIGH`): NEUTRAL =
    `EST*1.10` undershoots on stale/noisy EST demand steps → starve;
    (2) full-wall grind: COMPRESSION dwell TS 3.2–35.6 s, BP deepens
    -7.8 → -9.57 while held at `SYNC_MIN`, draw≈0 at print tail, slow
    `SYNC_RELIEVE_MM`. Out: **`CATCHUP=1.30`, `NEUTRAL=1.25`**.
    Expected: fewer TENSION (more demand-step margin, deeper safe
    full-lean), shallower/shorter COMPRESSION slam (gentler refill).
    Verdict: CONTINUE (never-TENSION not met). Two structural issues
    (EST-lag undershoot; COMPRESSION `SYNC_MIN` floor grinding a full
    buffer at zero draw, cf. §6.2) are not frac-fixable → defer to
    `relay-duty-estimator-and-tuning` §0 / estimator scope.

    2026-05-19 handback from `align-buffer-range-vocab`: corrected geometry
    defaults are now `buf_switch_span_mm=10` (internal half-span 5) and
    `buf_max_travel_mm=25`. Resume 4.2 with the known-good next pair
    **`CATCHUP=1.30`, `NEUTRAL=1.25`** under that corrected default.

  - **Round 2** (2026-05-19). In: `CATCHUP=1.30`, `NEUTRAL=1.25`. Full
    print + stop poll. Diagnosis: **steady-state goal MET** — from BPN 13
    on, EST locks ≈609.7, MM flat 762.1 (=EST×1.25), BP parks −4.1…−4.3,
    ~95 s+ continuous with **0 TENSION / 0 COMPRESSION / 0 fault** (one
    advisory `TENSION_RISK_HIGH`, no flip). Two boundary transients remain,
    both non-frac-fixable: (a) startup bangbang BPN 1–13 (~4 TENSION) =
    cold-EST convergence at print start; (b) end-of-print tail = COMPRESSION
    held at `SYNC_MIN` into a full buffer at zero draw, BP −7.8→−9.55,
    RELIEF_PAUSE/BUF_STAB auto-stop (expected print-end). Verdict: **STOP
    hand-loop — known-good baseline**. Both transients → handed to
    `relay-duty-estimator-and-tuning` §0 (cold-EST seeding) / estimator
    scope (COMPRESSION `SYNC_MIN` floor, cf. §6.2).

  **Deliverable (4.2 done):** frac pair **`CATCHUP=1.30` / `NEUTRAL=1.25`**
  + round-2 `?:` poll log proving the slow/shallow/never-TENSION/never-fault
  steady-state cycle. This is the known-good baseline gating
  `relay-duty-estimator-and-tuning` §0.1.

  **Scale caveat:** round-2.log was captured under the pre-fix geometry
  default (old `buf_half_travel_mm=7.8`, internal half 7.8). The frac pair
  is **switch-state driven** (§1–6 read discrete TENSION/COMPRESSION/NEUTRAL
  set by the physical switches) and therefore geometry-config-independent —
  the cycle proof holds under the corrected `buf_switch_span_mm=10`
  (half 5). Only the *virtual BP-mm readout, predictive `TENSION_RISK`
  timing, park-depth/deadband* scale with the geometry value; these are
  non-authoritative for the frac deliverable. An opportunistic re-poll
  under half=5 (next real print) is a nice-to-have to refresh the BP-mm
  reference for the estimator's mm domain — **not** a 4.2 gate.

## 5. Polarity fix (post-retest)

- [x] 5.1 Hardware showed inverted polarity (ADVANCE=empty,
  TRAILING=full per FLARE convention). Swap: ADVANCE→catch-up,
  TRAILING→back-off
- [x] 5.2 MID lean flipped to overfeed (`MID_FRAC` 0.97 → 1.05) toward
  the full/TRAILING reserve side (never starve)
- [x] 5.3 Skip legacy `trailing_floor` in relay mode (it force-raised
  feed in TRAILING, defeating back-off)
- [x] 5.4 `cmake --build build_local`; `py_compile`; `openspec validate`

## 6. Demand-tracked MID (post-retest 2)

- [x] 6.1 Hardware showed MID↔TRAILING bangbang + full-wall (-11) slam:
  MID anchored to fixed baseline (~5× real demand). Re-anchor MID to
  `extruder_est_sps * MID_FRAC` clamped `[SYNC_MIN, baseline]`
- [x] 6.2 TRAILING (full) → `SYNC_MIN` (stop, drain off wall); remove
  `SYNC_RELAY_BACKOFF_FRAC`
- [x] 6.3 ADVANCE keeps strong fixed baseline-anchored catch-up
- [x] 6.4 `MID_FRAC` 1.05 → 1.10; `cmake`/`py_compile`/`openspec validate`

## 7. Post-rename polarity validation follow-ups (2026-05-18)

Relay polarity re-validated against tension/compression vocab (independent
read + `audit-sync-polarity` 13-site table). No inversion: `sync.c:1622-1634`
`BUF_TENSION → relay_base * SYNC_RELAY_CATCHUP_FRAC` (empty→refill),
`BUF_COMPRESSION → SYNC_MIN_SPS` (full→stop/drain),
`BUF_NEUTRAL → extruder_est_sps * SYNC_RELAY_NEUTRAL_FRAC` clamped
`[SYNC_MIN, baseline]` (gentle compression lean, never starve). Matches the
goal: park between NEUTRAL and COMPRESSION, never reach TENSION.

- [x] 7.1 Refresh `proposal.md` prose to tension/compression vocab. Old
  prose used ADVANCE/TRAILING (predating the rename); it mapped consistently
  (ADVANCE=empty=TENSION, TRAILING=full=COMPRESSION) but reading old labels
  during on-Pi tuning was the same inversion-confusion trap that caused the
  5 debug rounds. Prose-only; enums/code already correct.

  2026-05-19: `adopt-sync-feedback-vocab` refreshed `proposal.md`,
  `design.md`, and active spec prose to type-D / TENSION / COMPRESSION
  vocabulary. Historical task notes below remain as retest history.
- [x] 7.2 `neutral_creep` is telemetry-only: `g_neutral_creep_sps` computed
  in `neutral_creep_update()` (`sync.c:395-428`) but consumed only at
  `protocol.c:217` (status emit) — never added to `target_sps`. Not a
  polarity bug; relay NEUTRAL feed is unaffected today.

  **Decision: A — intended-inert telemetry. Do NOT wire.** Wiring an upward
  creep into the relay NEUTRAL feed would re-introduce the slow ratchet that
  §6 deliberately removed (demand-tracked MID fix; NEUTRAL↔COMPRESSION
  bangbang + −11 full-wall slam) and directly fights the 4.2 goal
  (slow/shallow, never-TENSION). Demand-capped at 10% so milder than the old
  5× baseline bug, but same direction = same hazard. Deleting it loses a
  free NEUTRAL-dwell telemetry signal useful for 4.2-style `?:` diagnosis.
  Keep computing; leave unwired; documented intent. Note: if ever wired
  later it is **not** `BUF_SENSOR_TYPE`-gated and would need type-D polarity
  rework first → defer that to `relay-duty-estimator-and-tuning`.

Full `sync.c` COMPRESSION/TENSION sweep (beyond audit's 13 sites): relay
path zero inverted assumptions — decode, override (`1624-1632`),
`g_buf_pos` anchor, estimator-at-crossing (`735-753`), fast-brake,
relief-pause exit, baseline Kp, AUTO_START, continuous-COMPRESSION
auto-stop all correct vocab. Historical "trailing=empty" bug fully purged
from the relay path.

- [ ] 7.3 Legacy inverted assumption survives in **type-P analog-only**
  `sync_compression_floor_sps()` (`sync.c:385-386`, applied `1655-1657`,
  gated `BUF_SENSOR_TYPE != 0`, P=1). Old `compression_floor`: force-raises a feed
  FLOOR while `BUF_COMPRESSION` (=full) → fights drain = inverted. Relay
  skips it (relay COMPRESSION → SYNC_MIN stop), so inert for current
  tuning. = `audit-sync-polarity` finding #6, classified
  `pending-analog-rig` (no analog hardware; policy = never blind-fix
  analog). Recorded here so it is not re-derived; resolve only with an
  analog rig. Same bucket: audit #7 `compression_recovery`/collapse and
  the H2 feed-trim comment (`sync.c:1499-1517`, dead under relay override)
  — verify on the analog rig, not a relay-path inversion.
