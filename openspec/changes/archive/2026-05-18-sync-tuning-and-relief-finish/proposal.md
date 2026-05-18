## Why

The shipped `standalone-sync-relief-model` implementation (commit `86fe5b6`)
is ~65% complete: `sync_fault_hold()` is defined but never called (hard-wall
critical and advance-dwell stop still do destructive `sync_disable(true)`),
and the relief-effort counters are declared/reset but never accumulated or
emitted. The non-destructive fault contract and warn-only diagnostics are
paper-only. Separately, offline baseline tuning is non-deterministic and
single-profile: operators get different recommended baselines run to run,
which is confusing and erodes trust in the tuning workflow.

## What Changes

- Wire `sync_fault_hold()` at the two terminal jam sites per
  `standalone-sync-relief-model` design D1: hard-wall critical
  (`sync.c:1353`, currently `sync_disable(true)` / `AUTO_STOP`) and
  advance-dwell stop (`sync.c:1303`, currently `sync_disable(true)` /
  `ADV_DWELL_STOP`). Emit `FAULT_HOLD` events; the existing recovery timer
  (`sync.c:1018`) becomes reachable; estimator/drift/sigma preserved
  (non-destructive). **BREAKING** event-name change: `AUTO_STOP` /
  `ADV_DWELL_STOP` on these paths become `FAULT_HOLD`.
- Accumulate relief-effort counters in commanded-MMU mm (reuse
  `g_sync_mmu_total_mm` delta): `g_sync_refill_effort_mm` on sustained
  ADVANCE, `g_sync_relieve_effort_mm` on sustained TRAILING. Emit warn-only,
  rate-limited `SYNC cannot_refill` / `SYNC cannot_relieve` on configurable
  thresholds. Expose both counters in the `protocol.c` status line and as GET
  params. No control behavior derives from the counters.
- Add a deterministic dual-profile offline baseline workflow: run the same
  model in two profiles (fastest cubic-flow, slowest) with the existing
  tuner + marker scripts, then `scripts/flare_analyze.py` derives ONE
  baseline from both captures and writes it to memory. Hard requirement:
  identical input captures MUST produce an identical baseline (no run-to-run
  variance).
- Add `scripts/flare_baseline_recommender.py`: a host-only, stdlib+pyserial
  script that reads the tty, analyzes drift in live-tuner algo output over a
  print, and suggests a better persistent `baseline_sps`. Firmware unchanged
  for this part.

Constraints (carried from `standalone-sync-relief-model`): full-bias
invariant untouched; live baseline stays disciplined / ephemeral / up-only /
non-persistent; offline analyzer is the sole persistent authority; R8
flow-keyed schedule out of scope; no host/encoder coupling.

## Capabilities

### New Capabilities
- `deterministic-tuning-workflow`: the two-profile offline baseline procedure,
  its determinism guarantee (same inputs → same baseline), and the
  live-tuner recommendation export script contract.

### Modified Capabilities
- `motion-safety`: hard-wall critical and advance-dwell stop transition to
  `SYNC_FAULT_HOLD` (non-destructive, auto-recover) instead of destructive
  `sync_disable(true)`; the prior-change requirement becomes implemented and
  observable via the `FAULT_HOLD` event and reachable recovery.
- `live-tuner`: relief-effort counters are accumulated and exposed in status
  / GET, with warn-only `cannot_refill` / `cannot_relieve` events.
- `calibration-workflow`: offline baseline derivation is dual-profile and
  deterministic; the analyzer remains the sole persistent authority and must
  produce identical output for identical inputs.

## Impact

- Firmware: `firmware/src/sync.c` (FAULT_HOLD wiring at two sites, effort-mm
  accumulation, threshold events), `firmware/src/protocol.c` (effort counters
  in status line + GET), `config.ini.example` + `scripts/gen_config.py`
  (effort thresholds, rate-limit tunables).
- Host tools: `scripts/flare_analyze.py` (deterministic dual-profile baseline
  derivation), new `scripts/flare_baseline_recommender.py` (+ test under
  `scripts/`), workflow docs.
- Completes (does not re-open) `standalone-sync-relief-model`; that change is
  shipped at `86fe5b6` and still pending archive.
- No persistence-format change; no host/Klipper/encoder coupling added.
