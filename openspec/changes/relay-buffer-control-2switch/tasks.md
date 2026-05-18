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
  never-ADVANCE, never-faulting cycle (pending hardware)

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
