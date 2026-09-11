## 1. Phase 1 — FAULT_HOLD wiring (firmware)

- [x] 1.1 Replace `sync_disable(true)` with `sync_fault_hold()` at advance-dwell stop (`firmware/src/sync.c:1303`); change `cmd_event("SYNC", "ADV_DWELL_STOP")` → `cmd_event("SYNC", "FAULT_HOLD")`; keep `extruder_est_last_update_ms` / `sync_apply_to_active()` / `return`
- [x] 1.2 Replace `sync_disable(true)` with `sync_fault_hold()` at hard-wall critical (`firmware/src/sync.c:1353`); change `cmd_event("SYNC", "AUTO_STOP")` → `cmd_event("SYNC", "FAULT_HOLD")`; keep trailing lines
- [x] 1.3 Verify recovery path (`sync.c:1018`) now reachable: FAULT_HOLD → SYNC_OFF after `CONF_SYNC_FAULT_HOLD_RECOVERY_MS` (keep shipped 5000; add `// VERIFY: retune from FAULT_HOLD/FAULT_HOLD_RECOVERY event logs`), single shared interval for both advance-dwell and hard-wall (no split), emits `FAULT_HOLD_RECOVERY`, estimator preserved through re-arm
- [x] 1.4 Confirm `sync_fault_hold()` does not reset estimator/drift/sigma (non-destructive); no full-bias / collapse-ramp code touched

## 2. Phase 1 — Effort counters (firmware)

- [x] 2.1 Add `CONF_SYNC_CANNOT_REFILL_MM = 50.0` and `CONF_SYNC_CANNOT_RELIEVE_MM = 50.0` (locked values, symmetric) to `config.ini.example` + `scripts/gen_config.py`
- [x] 2.2 In `buf_sensor_tick`, capture the per-tick `g_sync_mmu_total_mm` delta; add to `g_sync_refill_effort_mm` when buffer in ADVANCE, `g_sync_relieve_effort_mm` when in TRAILING
- [x] 2.3 Emit warn-only `SYNC cannot_refill` / `SYNC cannot_relieve` once per episode on threshold cross while still ADVANCE/TRAILING, using existing latch flags; assert no control reads the counters
- [x] 2.4 Expose `SYNC_REFILL_MM` / `SYNC_RELIEVE_MM` in `protocol.c` status line and GET param
- [x] 2.5 Local `cmake --build build_local`; run `scripts/validate_regression.sh`; behavior-parity check that SYNC_ACTIVE output is unchanged

## 3. Phase 2 — Deterministic analyzer path (host)

- [x] 3.1 Add explicit two-profile mode to `scripts/flare_analyze.py` (e.g. `--profile-fast`, `--profile-slow`, `--emit-baseline`)
- [x] 3.2 Implement deterministic reducer: stable bucket ordering, `n`-only weighting, no wall-clock recency term, fixed rounding; MUST apply the existing `BIAS_SAFE_MIN/MAX` clamps (locked decision — same guard as recency path)
- [x] 3.3 Leave existing recency-weighted config-patch path byte-unchanged; deterministic path additive and separately selected
- [x] 3.4 Add determinism test in `scripts/` (same two captures twice → byte-identical baseline; existing patch path output unchanged)

## 4. Phase 2 — Recommender script (host)

- [x] 4.1 Create `scripts/flare_baseline_recommender.py` (stdlib + pyserial only): read tty status/marker/SYNC lines, track live-tuner drift across a print
- [x] 4.2 At end-of-print, print suggested persistent `baseline_sps` + drift summary; observe-only (no `SET`/`SV` writes)
- [x] 4.3 Support replay from a recorded stream; add test asserting identical recommendation on repeated replay
- [x] 4.4 Document the fixed two-profile bracketing procedure (fast + slow capture → deterministic analyzer → config → reflash) in workflow docs; update any parser/doc referencing the renamed `AUTO_STOP`/`ADV_DWELL_STOP` events

## 5. Closeout

- [x] 5.1 `openspec validate sync-tuning-and-relief-finish --strict`
- [x] 5.2 Full `scripts/validate_regression.sh` + local build green; working tree clean on a non-main branch
