## 1. Relay control core

- [x] 1.1 Add `SYNC_RELAY_CATCHUP_FRAC` / `BACKOFF_FRAC` / `MID_FRAC`
- [x] 1.2 Override `target_sps` before the ramp/clamp, gated
  `BUF_SENSOR_TYPE == 0`: TRAILING→catch-up, ADVANCE→back-off, MID→hold
- [x] 1.3 Confirm ramp, `[SYNC_MIN,max]` clamp, `trailing_floor`,
  fast-brake, relief logic still run after the override

## 2. Disarm FAULT_HOLD on normal switch contact

- [x] 2.1 Gate advance-dwell FAULT_HOLD to `BUF_SENSOR_TYPE != 0`
- [x] 2.2 Gate `trailing_wall_critical` FAULT_HOLD to `BUF_SENSOR_TYPE != 0`
- [x] 2.3 Confirm RELIEF_PAUSE / continuous-trailing auto-stop unchanged

## 3. Validation

- [x] 3.1 `cmake --build build_local`
- [x] 3.2 `python3 -m py_compile scripts/*.py`
- [x] 3.3 `openspec validate relay-buffer-control-2switch --strict`
- [x] 3.4 Analog parity reasoning: `BUF_SENSOR_TYPE != 0` path unchanged
- [x] 3.5 `TEST_CASES.md`: 2-switch relay regression entry

## 4. Closeout

- [x] 4.1 Commit + push to main
- [ ] 4.2 On-Pi A/B: tune `SYNC_RELAY_*_FRAC` for a slow, shallow,
  never-TENSION, never-faulting cycle (pending hardware). Note: old
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

- [ ] 7.1 Refresh `proposal.md` prose to tension/compression vocab. It still
  uses old ADVANCE/TRAILING (predates the rename); maps consistently
  (ADVANCE=empty=TENSION, TRAILING=full=COMPRESSION) but reading old labels
  during on-Pi tuning is the same inversion-confusion trap that caused the
  5 debug rounds. Prose-only; enums/code already correct.
- [ ] 7.2 `neutral_creep` is telemetry-only: `g_neutral_creep_sps` computed
  in `neutral_creep_update()` (`sync.c:395-428`) but consumed only at
  `protocol.c:217` (status emit) — never added to `target_sps`. Decide:
  intended inert, or wire it into the NEUTRAL feed path. Not a polarity bug;
  relay NEUTRAL feed is unaffected today.

Full `sync.c` COMPRESSION/TENSION sweep (beyond audit's 13 sites): relay
path zero inverted assumptions — decode, override (`1624-1632`),
`g_buf_pos` anchor, estimator-at-crossing (`735-753`), fast-brake,
relief-pause exit, baseline Kp, AUTO_START, continuous-COMPRESSION
auto-stop all correct vocab. Historical "trailing=empty" bug fully purged
from the relay path.

- [ ] 7.3 Legacy inverted assumption survives in **analog-only**
  `sync_compression_floor_sps()` (`sync.c:385-386`, applied `1655-1657`,
  gated `BUF_SENSOR_TYPE != 0`). Old `trailing_floor`: force-raises a feed
  FLOOR while `BUF_COMPRESSION` (=full) → fights drain = inverted. Relay
  skips it (relay COMPRESSION → SYNC_MIN stop), so inert for current
  tuning. = `audit-sync-polarity` finding #6, classified
  `pending-analog-rig` (no analog hardware; policy = never blind-fix
  analog). Recorded here so it is not re-derived; resolve only with an
  analog rig. Same bucket: audit #7 `compression_recovery`/collapse and
  the H2 feed-trim comment (`sync.c:1499-1517`, dead under relay override)
  — verify on the analog rig, not a relay-path inversion.
