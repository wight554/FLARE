## Context

2-switch relay mode (`BUF_SENSOR_TYPE == 0`) has only TENSION/COMPRESSION
microswitches. `relay-buffer-control-2switch` (`sync.c:1622-1634`) ended the
relaxation oscillation by overriding the est/reserve/PI target with a
hysteretic relay: `BUF_TENSION → relay_base × SYNC_RELAY_CATCHUP_FRAC`,
`BUF_COMPRESSION → SYNC_MIN_SPS`, `BUF_NEUTRAL → extruder_est_sps ×
SYNC_RELAY_NEUTRAL_FRAC` (≈1.10, hand-tuned), clamped `[SYNC_MIN,
relay_base]`. Only the NEUTRAL branch is a guess. Happy Hare
`mmu_sync_controller.py3` solves the identical zero-info problem for its
dual-switch (`D`) sensor with a duty-cycle estimator: it pairs
low/high-state travel between flips into duty cycles and derives an
effective feed. FLARE has exactly the signal HH uses (the two switches) but
currently discards it in NEUTRAL.

Constraints (frozen this session): (1) the never-TENSION compression lean
stays — estimator replaces the *effective-feed guess*, not the lean policy;
(2) the deterministic offline analyzer stays the persistent authority — HH's
online flash-save is rejected; (3) FLARE polarity is `+1 = tension /
-1 = compression`, the **inverse** of HH (`+1 = compression`); every ported
formula must flip sign.

## Goals / Non-Goals

**Goals:**
- Replace the hand-tuned relay NEUTRAL feed with a runtime duty-cycle
  estimate learned from switch-flip cadence, bounded by offline-recommended
  limits, lean preserved on top, safe fallback when unconfident.
- Make relay knobs config-driven (config→flash/`SET:`, not recompile).
- Extend the deterministic offline analyzer to recommend relay
  baseline/bounds from captured flip duty; same-inputs→same-output.
- Close the relay documentation gaps (TUNING.md relay section; `TB`→`CB`).

**Non-Goals:**
- No change to TENSION→catch-up or COMPRESSION→stop branches.
- No online flash-save / no firmware persistence of the estimate.
- No analog (`BUF_SENSOR_TYPE != 0`) behavior change; no blind analog code
  (no rig; HH-modelled spec only).
- Not replacing the offline analyzer authority with online learning.

## Decisions

### D1 — Estimator scope: NEUTRAL branch only

`v_est` substitutes **only** the `BUF_NEUTRAL` relay target. TENSION and
COMPRESSION remain `relay_base × CATCHUP` and `SYNC_MIN_SPS`. Rationale:
audit + this session proved those two correct and safety-critical
(never-starve / drain-off-wall); the only hand guess is NEUTRAL. Minimal
blast radius, reviewable as "NEUTRAL feed source swap". *Alternative
(rejected):* full HH PD/EKF port — unverifiable without an analog rig,
violates constraint, huge diff.

### D2 — Duty-cycle math (sign-corrected for FLARE)

Per-state filament travel accumulated between flips. On each flip, pair the
just-completed segment with the previous opposite segment:

```
dl = travel accumulated while feeding toward COMPRESSION (NEUTRAL fill)
dh = travel accumulated in the other phase
fh = dh / (dl + dh)                       # high-phase duty fraction
v_est = (1 - fh)·v_low + fh·v_high        # duty-weighted effective feed
neutral_target = clamp(v_est, lo, hi)     # lo/hi from offline analyzer
```

`v_low`/`v_high` are the measured feed rates actually commanded in each
phase (steps/s), not HH's rotation-distance space. HH's `+1=compression`
sign is flipped: in FLARE the "seek/refill" phase is the NEUTRAL fill
toward COMPRESSION (BP negative), the "relieve" phase is drain toward
TENSION (BP positive). The estimator never runs in TENSION/COMPRESSION
(those are overridden); it only shapes the NEUTRAL dwell between flips.

### D3 — Compression lean stays on top, as a post-estimator bias

`neutral_target` from D2 is the *demand-matched* feed. The never-TENSION
lean is then applied exactly as today (the `SYNC_RELAY_NEUTRAL_FRAC` /
`SYNC_COMPRESSION_BIAS_FRAC` overfeed) so the buffer still parks between
NEUTRAL and COMPRESSION. Order: `estimate → lean → clamp [lo,hi] →
existing ramp/clamp`. The estimator removes the *guess of demand*; the lean
remains the *policy*. *Alternative (rejected):* fold the lean into the
estimator target — couples policy and measurement, regressions hard to
reason about, violates constraint 1.

### D4 — Confidence + fallback

The estimate is used only when confident: a minimum number of paired
duty cycles observed within a recency window (analogous to HH's
`autotune_cert_window` / significance gate, not its save path). Until then,
and whenever cycles go stale, NEUTRAL falls back to the existing
`extruder_est_sps × SYNC_RELAY_NEUTRAL_FRAC` path (`relay-buffer-control-
2switch` 6.x). Fallback is also the boot state. No silent divergence: a
telemetry field exposes estimate vs fallback and confidence.

### D5 — neutral_creep resolution (closes other change's 7.2)

`neutral_creep` (computed `sync.c:395-428`, currently telemetry-only) was
the long-NEUTRAL anti-drift ramp. The duty estimator subsumes that role
(it actively tracks demand during NEUTRAL dwell). Decision: **remove** the
dead `neutral_creep` compute + its telemetry key, OR repurpose its emit
slot for estimate/confidence telemetry. Default: remove compute; reuse the
protocol slot for `duty/conf` telemetry to avoid status-line churn.
Recorded so `relay-buffer-control-2switch` 7.2 is resolved, not reopened.

### D6 — Config-key migration

`SYNC_RELAY_CATCHUP_FRAC`, `SYNC_RELAY_NEUTRAL_FRAC`, new
`relay_estimate_lo`/`relay_estimate_hi` (and confidence window/threshold)
move to `config.ini` → `gen_config.py` → generated `tune.h`, same pattern
as `baseline_rate`/`sync_compression_bias_frac`. Legacy `#define`s deleted.
Per active-dev rename policy: unknown/stale keys ignored, no migration
guide. Where a key is safe to change at runtime it is also a `SET:` param;
bounds that gate safety stay flash-only.

### D7 — Offline analyzer extension stays deterministic

`flare_analyze` gains a relay path: from captured switch-flip timestamps +
commanded feed it computes the same duty statistics offline and emits
recommended `relay_base` and estimator `[lo, hi]` into the existing
`config.ini`/flow-schedule emit. Pure function of input CSVs (same
inputs→same output); acceptance-gate parity asserted against current
analyzer outputs for non-relay inputs (no regression). This is the
*persistent authority*; the runtime estimator only adapts *within* the
offline-emitted bounds. *Alternative (rejected):* HH online autotune +
flash-save — violates constraint 2, breaks determinism guarantee in
TUNING.md.

### D8 — Anti-chatter (secondary)

Add optional distance-hysteresis flip guard (`os_min_flip_mm` style)
*alongside* time-based `BUF_HYST_MS` (config-selectable, default = current
time-based to preserve behavior). HH relief-fraction snap captured as a
reference note in the new spec only (analog, no rig — not shipped).

## Risks / Trade-offs

- [Estimator destabilizes the cycle the relay change just stabilized] →
  D1 limits it to NEUTRAL; D4 falls back to the proven `×1.10` path when
  unconfident; D7 hard-clamps to offline bounds; gated behind the other
  change's 4.2 known-good hardware baseline.
- [HH polarity sign-flip mis-ported] → D2 states the inversion explicitly;
  the FLARE-sign formula is the spec contract, not the HH source; firmware
  uses commanded-feed space, not HH RD space, removing one mapping layer.
- [Offline analyzer regression] → D7 acceptance-gate parity on existing
  non-relay inputs; relay emit is additive.
- [Config-key BREAKING for relay users] → accepted (active dev, rename
  policy: stale keys ignored); TUNING.md relay section documents new keys.
- [neutral_creep removal hides a needed behavior] → D4 fallback covers
  long-NEUTRAL anti-drift; telemetry exposes estimate/confidence so a
  regression is visible, not silent.

## Migration Plan

1. Prereq gate: `relay-buffer-control-2switch` task 4.2 on-Pi baseline
   landed (known-good slow/shallow/never-TENSION cycle).
2. D6 config keys + `gen_config.py` (no behavior change yet; values =
   current `#define` constants).
3. D2/D3/D4 firmware estimator behind D4 confidence gate (fallback =
   today's behavior until confident) → host build + status snapshot.
4. D7 analyzer extension + acceptance-gate parity tests.
5. D8 optional anti-chatter (default off / time-based).
6. D5 neutral_creep removal/repurpose.
7. TUNING.md relay section + `TB`→`CB`; HH polarity landmine note;
   cross-link `relay-buffer-control-2switch` 7.2 (resolved) / 7.3.
8. On-Pi A/B vs the 4.2 baseline; tune via new config keys.

Rollback: estimator is gated by D4 — setting confidence threshold
unreachable (or a config kill-switch) reverts to the exact
`relay-buffer-control-2switch` behavior with no firmware revert.

## Open Questions

- Exact confidence gate (cycle count + window): seed from HH
  `autotune_cert_window=8` analogue; final values are on-Pi 4.2-relative,
  resolved during step 8.
- `v_low`/`v_high` measurement: instantaneous commanded vs dwell-averaged
  per phase — pick during step 3, must be deterministic for D7 parity.
- D5 final: hard-remove `neutral_creep` vs repurpose its protocol slot —
  decide in step 6 based on whether any consumer still reads the key.
