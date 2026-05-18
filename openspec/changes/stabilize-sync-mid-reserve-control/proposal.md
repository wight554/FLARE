## Why

Hardware testing after `fix-flow-schedule-reserve-regression` shows the reserve floor now stays active, but the sync loop still collapses into repeated `ADVANCE -> TRAILING -> FAULT_HOLD` cycles on long same-flow prints. Runtime tuning found a safer operating envelope, but the firmware still needs a MID-range control guard so it does not wait at the trailing edge until the next advance event.

## What Changes

- Make the best-known safe runtime sync settings the project defaults:
  - `sync_max_rate: 2200`
  - `sync_ramp_dn_rate: 80`
  - `sync_overshoot_pct: 150`
  - `sync_advance_ramp_delay_ms: 0`
  - `sync_overshoot_mid_extend: 1`
- Preserve `sync_min_rate: 100`; raising it to `500` caused trailing dwell/faults in hardware logs.
- Add firmware behavior so MID reserve control does not collapse feed too far while the buffer is near the trailing reserve target and the estimator is stale.
- Keep full braking available in `BUF_TRAILING`; the anti-advance assist applies only while still in `BUF_MID`.
- Keep `ADV_RISK_HIGH` as a warning/diagnostic, not the primary control path.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `sync-refactor`: Require sync active control to preserve standalone stability by using safe defaults and MID-only anti-advance reserve behavior without weakening trailing-side recovery.

## Impact

- Firmware sync controller: `firmware/src/sync.c`
- Runtime/default configuration: `config.ini`, `config.ini.example`, `scripts/gen_config.py`, generated `firmware/include/tune.h`
- Serial/runtime documentation: `MANUAL.md`, `BEHAVIOR.md`
- OpenSpec sync contract: `openspec/specs/sync-refactor/spec.md`
- Validation: build firmware, compile Python scripts, validate OpenSpec, then hardware A/B with `scripts/flare_cmd.py "?:" --poll 500`
