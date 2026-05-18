## Context

`fix-flow-schedule-reserve-regression` restored the intended reserve floor, but hardware logs still show a second failure mode: during long MID dwell near the trailing reserve target, the controller can let commanded MMU speed fall too low while the estimator goes stale. The next flow demand then drives the buffer into `BUF_ADVANCE`, recovery overfeeds, and the loop often crosses into `BUF_TRAILING` / `SYNC_FAULT_HOLD`.

Runtime testing found one safe envelope:

- `SYNC_ADV_RAMP_MS=0`
- `SYNC_MAX_RATE=2200`
- `SYNC_MIN_RATE=100`
- `SYNC_OVERSHOOT_PCT=150`
- `SYNC_OVERSHOOT_MID_EXT=1`
- `SYNC_DN_RATE=80`

Raising `SYNC_MIN_RATE` to `500` is explicitly rejected by test data: it forced feed while pinned at trailing and still faulted. The durable fix must therefore be state-aware, not a global minimum.

## Goals / Non-Goals

**Goals:**

- Make the safer hardware-tested runtime envelope the default config and docs.
- Add MID-only anti-advance behavior so the controller does not collapse feed too far while it is still physically in `BUF_MID`.
- Preserve full braking and recovery authority in `BUF_TRAILING`.
- Keep `ADV_RISK_HIGH` as a warning that points at residual instability, not as the only mitigation.

**Non-Goals:**

- Do not change `settings_t` layout or persistence version.
- Do not add host-side auto-tuning or require the live tuner during print.
- Do not raise `SYNC_MIN_RATE` globally.
- Do not change RELOAD/toolchange state machines except for normal sync target behavior while printing.

## Decisions

1. **Default the safe runtime envelope first.**

   The runtime settings already proved less dangerous than the old defaults. Updating `config.ini`, `config.ini.example`, `scripts/gen_config.py`, and docs gives fresh builds the same baseline without requiring the operator to remember a command block.

   Alternative considered: leave defaults unchanged and document the runtime set only. Rejected because the old defaults repeatedly faulted in hardware logs and are now known-bad for this setup.

2. **Keep `SYNC_MIN_RATE` at `100`.**

   `SYNC_MIN_RATE=500` looked tempting as a feed floor, but it created prolonged `BUF_TRAILING` dwell at 500 mm/min and still ended in `FAULT_HOLD`. A global floor cannot tell safe MID assist from unsafe trailing feed.

   Alternative considered: default `SYNC_MIN_RATE=500`. Rejected by hardware data.

3. **Implement the durable behavior as a MID-only floor/assist after normal correction is computed.**

   In `sync_tick()`, after `target_sps` is computed and scaled, apply an additional floor only when:

   - sync is active,
   - state is `BUF_MID`,
   - reserve error is on the trailing/overfilled side or near the trailing reserve target,
   - estimator freshness/confidence indicates the model is likely stale,
   - active lane is feeding without fault.

   The floor should be conservative: based on current flow schedule baseline / recent known flow rather than `SYNC_MIN_RATE`. It must never force `BUF_TRAILING` feed.

   Alternative considered: make `ADVANCE` recovery more aggressive. Rejected because it treats the symptom after the buffer already hit the advance wall and can worsen the advance-to-trailing swing.

4. **Keep trailing braking stronger with `SYNC_OVERSHOOT_MID_EXT=1` and faster down ramp.**

   Hardware logs show the old down-ramp and 25% overshoot trim are too slow to catch the post-advance swing. The new defaults keep MID overshoot trim active and allow faster deceleration while retaining the existing `BUF_TRAILING` collapse and fault protections.

## Risks / Trade-offs

- **Risk: MID floor overfeeds and increases trailing dwell** -> Mitigation: gate the assist to `BUF_MID` only, do not change `BUF_TRAILING`, and keep `SYNC_MIN_RATE=100`.
- **Risk: safer defaults are hardware-specific** -> Mitigation: document these as current safe defaults and keep every value runtime-tunable.
- **Risk: stale-estimator detection is too broad** -> Mitigation: require active feed, MID state, reserve-side condition, and use hardware logs with `APX`, `AD`, `TD`, `BP`, `MM`, `EST`, `EC`, and `BUF` to validate.
- **Risk: future flow-schedule work conflicts with this floor** -> Mitigation: source the floor from the existing flow schedule / baseline helpers rather than adding a separate parallel baseline.
