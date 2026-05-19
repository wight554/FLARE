## Context

Sync-Feedback Sensor type D relay mode (`BUF_SENSOR_TYPE == 0`, D=0) has
only TENSION/COMPRESSION microswitches. `relay-buffer-control-2switch`
(`sync.c:1622-1634`) ended the relaxation oscillation by overriding the
est/reserve/PI target with a two-level / hysteretic relay law:
`BUF_TENSION → relay_base × SYNC_RELAY_CATCHUP_FRAC`,
`BUF_COMPRESSION → SYNC_MIN_SPS`, `BUF_NEUTRAL → extruder_est_sps ×
SYNC_RELAY_NEUTRAL_FRAC` (**locked 4.2 baseline = 1.25**, round-2; with
`CATCHUP=1.30`), clamped `[SYNC_MIN, relay_base]`. Only the NEUTRAL branch
is a guess. Happy Hare
`mmu_sync_controller.py3` solves the identical zero-info problem for its
type-D dual-switch sensor with a duty-cycle estimator: it pairs
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
- No type-P analog (`BUF_SENSOR_TYPE != 0`, P=1) behavior change; no blind analog code
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
`extruder_est_sps × SYNC_RELAY_NEUTRAL_FRAC` path — frac = the **locked
1.25** (`relay-buffer-control-2switch` 6.x baseline). Boot/cold-start uses
the **offline-seeded** fallback variant, not cold `extruder_est_sps` (see
D10). No silent divergence: a telemetry field exposes estimate vs fallback
and confidence. Per D10 the unconfident fallback is the *normal* steady
state in a good low-flip cycle — not an error path.

### D5 — neutral_creep: honor the locked 7.2-A disposition

`relay-buffer-control-2switch` 7.2 is **already decided and committed**
(`a78d864`): **Option A — `neutral_creep` is intended-inert telemetry,
kept computing, NOT wired, NOT removed** (it is a useful long-NEUTRAL-dwell
telemetry signal for `?:` diagnosis). This change **does not reopen or
override** that decision. Therefore:

- `neutral_creep` compute (`sync.c:395-428`) and its existing protocol
  emit are **left intact** (inert telemetry, as decided).
- Estimator estimate/confidence telemetry is added as a **separate, new**
  status field — it does **not** reuse or evict the `neutral_creep` slot.

The duty estimator does subsume neutral_creep's *functional* anti-drift
intent, but 7.2-A explicitly chose to keep the inert signal regardless;
removing it here would contradict a committed disposition. *Alternative
(rejected):* delete neutral_creep + reuse its slot — directly violates the
committed 7.2-A decision. Net: minor status-line growth (one new field),
accepted.

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

### D9 — Sync-Feedback Sensor taxonomy; name the law separately from the sensor

Adopt Happy Hare's umbrella concept **Sync-Feedback Sensor** with HH's
**canonical single-letter type codes** (HH wiki "Sync-Feedback 'Buffer'
Type Sensors"):

```
 P   Proportional   analog, ADC continuous position     FLARE BUF_SENSOR_TYPE == 1
 D   Dual           two switches, independent tension    FLARE BUF_SENSOR_TYPE == 0
                     + compression (3-state)             (this change's path)
 TO  Tension Only    single switch (buffer shortened)    not implemented in FLARE
 CO  Compression Only single switch (buffer extended)    not implemented in FLARE
```

The retired FLARE-historic analog alias is the HH **P** type. Do NOT mint
new acronyms — HH's codes are P/D/TO/CO. The *sensor* (P/D) and
the *control law* (HH "two-level" / hysteretic relay for D; PD/EKF for P)
are **named separately** — "type-D relay control" names both layers only
when the type-D sensor and relay law are explicitly distinct; the old
standalone feature label conflated
them (the same layer-mixing the tension/compression rename removed). In
this change's artifacts and the new `relay-duty-estimator` spec, the
feature is "the type-`D` two-level/relay control law + its duty-cycle
estimator", not a sensor named by its wiring. *Rationale:* cross-system legibility
with the established HH model; clean sensor-vs-law separation.
*Decision:* vocabulary decision was realized by `adopt-sync-feedback-vocab`
(**archived 2026-05-19**): Sync-Feedback Sensor + P/D/TO/CO codes,
`BUF_SENSOR_TYPE` documented as `D = 0` / `P = 1`, legacy analog alias
retired. No symbol churn here — wording in this change's prose only.

### D10 — Estimator role, cold-start seed, COMPRESSION-grind disposition

Resolves the three structural questions surfaced reviewing this plan
against the locked 4.2 round-2 baseline.

**(a) Estimator is a recovery arbitrator, not the steady-state driver.**
Round-2 proved a good cycle has *near-zero* switch flips (~0 flips / 95 s)
and that the bounded fallback `extruder_est_sps × 1.25` *is* the
known-good steady state. The duty estimator needs paired flip cycles for
confidence, so in a good cycle it stays unconfident **by design** — and
that is correct: few flips ⇒ good ⇒ proven fallback drives it. Switch
flips occur during/after a demand disturbance — exactly when the estimator
gains signal — so it engages to re-find demand during *recovery*, then
decays back to fallback as flips cease. No signal-starvation paradox:
unconfident-in-good-cycle is the intended operating point, not a failure.

**(b) Cold-start seed.** At print start `extruder_est_sps` is cold; the
bare fallback is the exact path that bangbanged in 4.2 round-2 (BPN 1–13,
~4 TENSION). Fix: for an initial warmup window the fallback feed is
**seeded from the offline analyzer's recommended relay baseline**
(`relay_base`, or the `[lo,hi]` midpoint) instead of cold
`extruder_est_sps`, until EST warms or the estimator becomes confident.
This directly attacks the deferred cold-start transient with an
offline-known-good prior. *Alternative (rejected):* preload a learned
estimate from flash — violates the no-persistence constraint (D7/§2.7).

**(c) End-of-print COMPRESSION grind — accepted limitation, out of
scope.** The 4.2 round-2 tail (COMPRESSION held `SYNC_MIN` into a full
buffer at draw≈0, BP −7.8→−9.55) would require modifying the COMPRESSION
branch, which D1 forbids (minimal blast radius; that branch is
safety-validated). It is a print-*end* artifact already caught by
RELIEF_PAUSE/BUF_STAB auto-stop with no print-quality impact. Disposition:
documented + cross-linked here, **not** widened into this change and
**not** silently dropped. A future dedicated change may revisit only if it
demonstrates real harm (it has not).

### D11 — Fallback un-clamp + calibration capture methodology

Surfaced reviewing the applied code against a sudden major *upward
sustained* speed step (e.g. feature-boundary 300→1500 mm/min).

**(a) Fallback un-clamp (defect fix).** The applied code
(`sync.c:~1804-1811`) clamps `demand_sps` to the offline
`[RELAY_ESTIMATE_LO/HI_SPS]` on **both** the confident estimator path
and the unconfident fallback, before `×NEUTRAL_FRAC`. The archived
`relay-buffer-control-2switch` round-2 build (the validated baseline)
clamped the fallback only to `[SYNC_MIN, relay_base ≈ 11000]` —
effectively unbounded for normal speeds. So a step above a
narrowly-tuned `hi` now pins the fallback low → underfeed → TENSION flip
(caught by the untouched catchup, but violates never-TENSION). This is a
regression vs round-2 and contradicts this change's own "Unconfident
fallback … no behavior change vs `relay-buffer-control-2switch`"
requirement. **Decision:** gate the `[lo,hi]` clamp on `use_estimate`;
the fallback/seed path keeps `extruder_est_sps × NEUTRAL_FRAC` clamped
only to `[SYNC_MIN, relay_base]` (round-2-identical). `[lo,hi]` governs
**only** the confidence-gated estimator — its designed role. EST itself
adapts fast (α→0.65, 2–3 transitions), so an unclamped fallback tracks a
major step quickly. *Alternative (rejected):* keep the clamp and require
wide bounds — leaves a latent regression whenever bounds are mis-tuned
and the §4 analyzer that sets them is not yet built.

**(b) Capture methodology.** §4.2 emits scalars (`relay_base` +
`[lo,hi]` = demand envelope + floor), so calibration needs two
well-sampled regimes + the transitions, not a speed sweep. Capture =
**one purpose-built model**, single print: sustains the slowest and
fastest intended demand long enough to fill the low/high buckets past the
analyzer min-sample gate, and steps between them at feature boundaries
(realistic worst case). Preferred form: a speed-banded tower (also fills
intermediate buckets cheaply). Explicitly anti-pattern: 3 prints
(slow+fast+combined — redundant) and many different models (uncontrolled,
no constant baseline — the prior 10-test trap).

**(c) Analyzer coverage verdict.** `flare_analyze` MUST report per-bucket
sample counts and emit PASS / WARN; WARN names the single deficiency
("low/high bucket under-sampled → print slower/faster/taller", "no
constant-baseline segment detected"). One print then PASSES or states
exactly the one fix — replacing the blind multi-test loop. Pure
deterministic function of the captured CSV; no new authority.

**(d) Deferred:** value-aware staleness (invalidate stale pairs / shorten
`RELAY_CONFIDENCE_WINDOW_MS` on a large demand step) — only matters once
the estimator is confidence-active in real prints; revisit at §7.5 on-Pi
A/B, not in this pass.

## Risks / Trade-offs

- [Estimator destabilizes the cycle the relay change just stabilized] →
  D1 limits it to NEUTRAL; D4 falls back to the proven `×1.25` path when
  unconfident; D7 hard-clamps to offline bounds; gated behind the locked
  4.2 round-2 baseline (`1.30/1.25`).
- [Signal starvation: good cycle has ~0 flips → estimator never confident]
  → not a defect; D10(a) makes the bounded fallback the intended
  steady-state driver; the estimator is a recovery arbitrator that only
  needs signal when flips (disturbances) actually occur.
- [Cold-start bangbang persists (deferred 4.2 transient)] → D10(b) seeds
  the cold-EST fallback from the offline relay baseline for the startup
  window instead of cold `extruder_est_sps`.
- [End-of-print COMPRESSION grind unaddressed] → D10(c) accepted
  limitation: out of D1 scope, print-tail only, auto-stop-handled, no
  quality impact; documented + cross-linked, not silently dropped.
- [HH polarity sign-flip mis-ported] → D2 states the inversion explicitly;
  the FLARE-sign formula is the spec contract, not the HH source; firmware
  uses commanded-feed space, not HH RD space, removing one mapping layer.
- [Offline analyzer regression] → D7 acceptance-gate parity on existing
  non-relay inputs; relay emit is additive.
- [Config-key BREAKING for relay users] → accepted (active dev, rename
  policy: stale keys ignored); TUNING.md relay section documents new keys.
- [neutral_creep handling contradicts committed 7.2-A] → D5 honors
  7.2-A: neutral_creep is left intact (inert telemetry, as decided);
  estimator telemetry is a separate new field, not a slot eviction.

## Migration Plan

1. Prereq gate: **satisfied** — `relay-buffer-control-2switch` archived;
   4.2 baseline `CATCHUP=1.30/NEUTRAL=1.25` + round-2 log locked (scale
   caveat: switch-driven, geometry-independent).
2. D6 config keys + `gen_config.py` (no behavior change yet; values =
   the **locked baseline** 1.30/1.25, NOT the old 1.45/1.10 #defines).
3. D2/D3/D4/D10(a,b) firmware estimator behind confidence gate; fallback =
   today's behavior, cold-start seeded from offline relay baseline →
   host build + status snapshot.
4. D7 analyzer extension + acceptance-gate parity tests.
5. D8 optional anti-chatter (default off / time-based).
6. D5: add estimator telemetry as a **new** status field;
   `neutral_creep` left intact per committed 7.2-A (no removal).
7. TUNING.md relay section + `TB`→`CB`; HH polarity landmine note;
   cross-link 7.2-A (honored), 7.3 (→ `pending-analog-rig`),
   D10(c) COMPRESSION-grind accepted-limitation note.
8. On-Pi A/B vs the locked 4.2 baseline; tune via new config keys.

Rollback: estimator is gated by D4 — setting confidence threshold
unreachable (or a config kill-switch) reverts to the exact
`relay-buffer-control-2switch` behavior with no firmware revert.

## Open Questions

- Exact confidence gate (cycle count + window): seed from HH
  `autotune_cert_window=8` analogue; final values are on-Pi
  locked-baseline-relative, resolved during step 8.
- `v_low`/`v_high` measurement: instantaneous commanded vs dwell-averaged
  per phase — pick during step 3, must be deterministic for D7 parity.
- D10(b) cold-start seed source: offline `relay_base` directly vs
  `[lo,hi]` midpoint vs a dedicated `relay_seed` config key — decide in
  step 3 (must be offline-provided / no flash persistence).
- D10(b) warmup-window exit: fixed time, EST-warm threshold, or
  first-confident — decide in step 3, on-Pi-tunable in step 8.
- (D5 resolved: 7.2-A is committed; neutral_creep stays, telemetry is a
  new field — no open question.)

## Implementation Plan (2026-05-19)

### OpenSpec artifacts

- Fix `specs/sync-refactor/spec.md` so the `neutral_creep` requirement matches
  D5 / tasks 3.2 / proposal: keep the existing intended-inert telemetry and
  add estimator telemetry separately.
- Preserve tasks history by marking completed items only after validation.

### Config and runtime parameter path

- Update `config.ini`, `config.ini.example`, and `scripts/gen_config.py` with
  relay catch-up, neutral lean, estimator bounds, confidence threshold/window,
  cold-start seed, seed warmup, and optional distance hysteresis.
- Add matching runtime globals in `main.c` / `controller_shared.h`, persist
  them in `settings_store.c`, and bump `SETTINGS_VERSION` because `settings_t`
  changes.
- Add `SET:` / `GET:` only for runtime-safe relay fractions and confidence
  threshold/window; keep estimate bounds and seed values config/flash-only.
  Update `scripts/flare_cmd.py` dump surface for runtime-safe keys.
- Risk: keep locked 4.2 defaults (`1.30` / `1.25`) and remove stale
  `SYNC_RELAY_*_FRAC` defines.

### Firmware estimator and telemetry

- In `sync.c`, add volatile type-D relay estimator state updated from stable
  TENSION/COMPRESSION transitions and commanded-feed dwell averages.
- Replace only the `BUF_NEUTRAL` relay target with estimated demand when
  confident; leave `BUF_TENSION` catch-up and `BUF_COMPRESSION` stop
  branches unchanged.
- Apply neutral lean after estimate, clamp to offline `[lo, hi]`, then use the
  existing clamp/ramp path. Fallback remains `extruder_est_sps * relay neutral`
  and cold-start can seed from offline config until EST warms or estimator
  confidence appears.
- Add status helpers / protocol fields for estimate-vs-fallback and
  confidence; leave `neutral_creep` intact with a D5 code comment.
- Risk: type-P analog path must remain behavior-identical; estimator state
  must not call flash persistence.

### Analyzer, docs, validation

- Extend `scripts/flare_analyze.py` deterministically for relay duty
  recommendations and add tests for byte-stable relay output plus non-relay
  parity.
- Update `TUNING.md`, `MANUAL.md`, `TEST_CASES.md`, and relevant specs with
  relay keys, telemetry, HH polarity inversion, and D10(c) accepted
  COMPRESSION-grind limitation.
- Validate with generated config test, `python3 -m py_compile scripts/*.py`,
  analyzer tests, firmware build, and strict OpenSpec validation before commit.
