## 0. Prerequisite gate

- [x] 0.1 Prereq gate **SATISFIED**: `relay-buffer-control-2switch`
  archived (2026-05-19, 24/24). Locked 4.2 baseline =
  **`CATCHUP=1.30` / `NEUTRAL=1.25`** + round-2 `?:` log
  (slow/shallow/never-TENSION/never-fault steady state). Scale caveat:
  pair is switch-state driven ⇒ geometry-config-independent; round-2
  BP-mm under pre-`align-buffer-range-vocab` half=7.8 scale,
  non-authoritative for the frac/bounds. Estimator is bounded/A-B'd
  against this.

## 1. Config-key migration (no behavior change)

- [x] 1.1 Add `config.ini` keys: relay catch-up, relay NEUTRAL frac,
  `relay_estimate_lo`, `relay_estimate_hi`, estimator confidence window +
  threshold, `relay_seed` (D10b cold-start seed source). Defaults =
  **the locked 4.2 baseline: catch-up `1.30`, NEUTRAL `1.25`** (NOT the
  old 1.45/1.10 `#define`s — those are the rejected round-1 pair).
- [x] 1.2 Wire keys through `gen_config.py` into the generated tune header
  (mirror `baseline_rate` / `sync_compression_bias_frac` pattern).
- [x] 1.3 Delete the legacy `SYNC_RELAY_*_FRAC` `#define`s in `sync.c`;
  consume generated values. Mark runtime-safe knobs as `SET:` params;
  keep safety bounds flash-only.
- [x] 1.4 `gen_config.py` test + `ninja -C build_local` green; status
  snapshot identical to the **locked-baseline build** (1.30/1.25 → behavior
  byte-identical to archived `relay-buffer-control-2switch` round-2).
- [x] 1.5 `config.ini.example` updated; stale `#define` references gone.

  2026-05-19 validation: `python3 scripts/gen_config.py`,
  `python3 scripts/test_gen_config.py`, `python3 -m py_compile scripts/*.py`,
  and `ninja -C build_local` pass. Runtime-safe relay fractions/confidence
  gates have `SET:`/`GET:` + `flare_cmd.py --dump` parity; estimator bounds
  and seed keys remain config/flash-only. Code no longer defines
  `SYNC_RELAY_*_FRAC`; `sync.c` consumes generated runtime globals whose
  defaults are the locked 1.30/1.25 pair.

## 2. Firmware duty-cycle estimator (D2/D3/D4)

- [x] 2.1 Add per-state filament-travel accumulators reset on each
  TENSION↔COMPRESSION flip; pair completed opposite segments into duty
  cycles. Use commanded-feed (steps/s) space, FLARE sign
  (`+1=tension/-1=compression`) — NOT Happy Hare RD space or sign.
- [x] 2.2 Compute `fh = dh/(dl+dh)`, `v_est = (1-fh)·v_low + fh·v_high`;
  fix the per-phase feed measurement as instantaneous-commanded or
  dwell-averaged and document it (must be deterministic for §4 parity).
- [x] 2.3 Confidence gate: minimum paired cycles within a recency window
  (seed from HH `autotune_cert_window=8` analogue, value config-driven).
- [x] 2.4 In the relay override (`sync.c:~1622-1634`) replace ONLY the
  `BUF_NEUTRAL` target with `clamp(v_est, lo, hi)` when confident;
  `BUF_TENSION`/`BUF_COMPRESSION` branches untouched.
- [x] 2.5 Apply the existing never-TENSION compression lean AFTER the
  estimate, BEFORE existing ramp/clamp (order: estimate → lean → clamp
  `[lo,hi]` → ramp/clamp).
- [x] 2.6 Unconfident/stale → fall back to
  `extruder_est_sps × SYNC_RELAY_NEUTRAL_FRAC` (frac = locked **1.25**)
  with no feed discontinuity. Per D10(a) this is the *normal* steady
  state in a good low-flip cycle, not an error path — do not treat
  unconfident as a fault.
- [x] 2.7 D10(b) cold-start seed: at print start / boot, seed the
  fallback feed from the offline relay baseline (`relay_seed` /
  `relay_base` / `[lo,hi]` midpoint) instead of cold `extruder_est_sps`,
  for a warmup window (exit on EST-warm or first-confident). Targets the
  deferred 4.2 round-2 startup-bangbang. No flash persistence.
- [x] 2.8 Estimator + seed state volatile only — assert no flash write
  path is reachable from estimator/seed updates.
- [ ] 2.9 Host build + captured status snapshot: with confidence gate
  unreachable AND seed window elapsed, steady-state behavior ==
  archived `relay-buffer-control-2switch` round-2 (rollback proof).

  2026-05-19 validation: `ninja -C build_local` and
  `python3 -m py_compile scripts/*.py` pass. Estimator state lives in
  `sync.c` static RAM only; update paths do not call `settings_save()` or
  any flash API. Per-phase measurement is dwell-averaged commanded SPS
  weighted by commanded MMU travel. Type-D TENSION remains
  `relay_base * RELAY_CATCHUP_FRAC`; COMPRESSION remains `SYNC_MIN_SPS`;
  only NEUTRAL selects estimate vs seeded/fixed fallback.

## 3. Telemetry + neutral_creep resolution (D5)

- [x] 3.1 Add status/telemetry: estimate-vs-fallback flag + confidence
  value.
- [x] 3.2 `neutral_creep` (`sync.c:395-428`): **leave intact** per the
  committed 7.2-A disposition (intended-inert telemetry, kept computing,
  not removed — commit `a78d864`). Do NOT delete it or evict its slot.
  Estimator estimate/confidence telemetry (3.1) is a **separate new**
  status field. Add a code comment cross-linking 7.2-A so it is not
  re-flagged as dead.
- [x] 3.3 Update `protocol.c` / any host parsers in lockstep; host build +
  `py_compile` green.

  2026-05-19 validation: status appends `RDE` (estimate flag), `RDCF`
  (relay confidence percent), and `RDV` (estimate mm/min). `NC` remains
  unchanged, with a D5 code comment. Existing host status parsers are
  generic key/value readers; no parser-specific update needed.

## 4. Deterministic offline relay analyzer (D7)

- [x] 4.1 Extend capture (if needed) so switch-flip timestamps + commanded
  feed are present in the CSV for relay runs.
- [x] 4.2 `flare_analyze`: compute the same duty statistics offline;
  emit recommended `relay_base` + estimator `[lo, hi]` into the existing
  `config.ini`/flow-schedule emit. Pure function of inputs.
- [x] 4.3 Determinism test: same input CSVs → identical relay
  recommendation (byte-stable).
- [x] 4.4 Acceptance-gate parity test: existing non-relay inputs produce
  unchanged schedule + FAIL/WARN/PASS verdict.
- [x] 4.5 `python3 -m py_compile scripts/*.py`; analyzer/test suite green.

  2026-05-19 validation: `flare_live_tuner.py` CSV now records `MM` plus
  relay telemetry (`RDE`/`RDCF`/`RDV`) so zone-transition timestamps and
  commanded feed are in relay captures. `flare_analyze.py` emits
  deterministic relay recommendations (`baseline_rate` as relay base,
  `relay_estimate_lo`, `relay_estimate_hi`, `relay_seed_rate`) only when
  relay transition data exists, preserving non-relay patch shape. Added a
  byte-stability relay-duty regression and kept existing acceptance-gate /
  flow-schedule parity tests green. Also normalized historical `MID` rows to
  `NEUTRAL` and treated missing legacy `cumulative_neutral_s` as mature in
  force-mode fixtures so existing rigor tests keep exercising old captures.

## 5. Anti-chatter option (D8, secondary)

- [x] 5.1 Add optional distance-hysteresis flip guard alongside
  `BUF_HYST_MS`; config-selectable; default = existing time-based
  (behavior unchanged when unset).

  2026-05-19 validation: `relay_min_flip_mm` defaults to `0.0`, preserving
  current `BUF_HYST_MS` behavior. When set, stable type-D flips are held
  until commanded MMU travel since the last accepted flip reaches the
  configured distance.
- [x] 5.2 Capture HH relief-fraction snap as a reference note in the
  `relay-duty-estimator` spec/design only (analog, no rig — not shipped).

  2026-05-19 validation: `relay-duty-estimator` spec records the HH
  relief-fraction snap as reference-only and keeps type-P analog unchanged
  pending the deferred analog rig.

## 6. Docs (T3/T4)

- [x] 6.1 TUNING.md: add the type-D relay-law tuning section —
  config keys, relay capture/analyze loop, runtime estimator (estimate vs
  fallback, reading confidence). State knobs are config-driven, not
  compile-time.
- [x] 6.2 TUNING.md: fix stale status token `TB` → `CB` (verify against
  `protocol.c:197` emission).
- [x] 6.3 Record the Happy Hare polarity-inversion landmine where the
  analog reference is cited (`+1=compression` HH vs `+1=tension` FLARE —
  flip every sign on any analog port); cross-link
  `relay-buffer-control-2switch` task 7.3.
- [x] 6.4 Note in this change's artifacts that
  `relay-buffer-control-2switch` 7.2 was decided **A** (neutral_creep
  intended-inert telemetry, kept) and is **honored, not reopened** here
  (§3.2); 7.3 was split to `pending-analog-rig`. Do not edit the
  archived change's files.
- [x] 6.5 Record the D10(c) accepted-limitation: the 4.2 round-2
  end-of-print COMPRESSION `SYNC_MIN`-grind is out of scope (D1 forbids
  COMPRESSION-branch edits; print-tail, draw≈0, auto-stop-handled, no
  quality impact). State it in TUNING.md / the `relay-duty-estimator`
  spec; cross-link, do not silently drop.

  2026-05-19 validation: `TUNING.md` now documents type-D relay config,
  capture/analyze, `RDE`/`RDCF`/`RDV`, low-flip fallback semantics, HH
  polarity inversion, and the D10(c) COMPRESSION-tail limitation. `TB` is
  corrected to `CB`. `MANUAL.md` documents relay parameters and status
  fields. The change spec preserves neutral_creep as a separate telemetry
  slot per 7.2-A and records the HH relief-snap as reference-only.

## 7. Validation + closeout

- [x] 7.1 `ninja -C build_local` green; `python3 -m py_compile scripts/*.py`.
- [x] 7.2 `openspec validate relay-duty-estimator-and-tuning --strict`.
- [x] 7.3 Analog parity reasoning recorded: `BUF_SENSOR_TYPE != 0`
  untouched / byte-identical.
- [x] 7.4 `TEST_CASES.md`: relay duty-estimator regression entry
  (estimate path, fallback path, bounds clamp, determinism).

  2026-05-19 validation: `python3 -m py_compile scripts/*.py`,
  `ninja -C build_local`, and
  `openspec validate relay-duty-estimator-and-tuning --strict` pass.
  `TEST_CASES.md` has a relay duty-estimator regression entry covering
  estimate/fallback/bounds/determinism. Type-P analog behavior remains
  code-untouched by the relay estimator path (`BUF_SENSOR_TYPE != 0` gates
  all new relay estimator and distance-hysteresis behavior).
- [ ] 7.5 On-Pi A/B vs the §0.1 baseline: cycle stays slow/shallow/
  never-TENSION; tune via the new config keys; record results.
- [x] 7.6 Commit + push to main.

  2026-05-19 validation: implementation and docs were committed and pushed
  to `main` through `fb3e7cc` (`sync: move relay knobs to config`,
  `sync: add relay duty estimator`, `tuning: analyze relay duty cycles`,
  `docs: describe relay duty tuning`). Remaining unchecked tasks require
  hardware/status capture rather than more local code changes.

## 8. Speed-step hardening (D11, review-added)

Surfaced reviewing the applied code vs a sudden major upward sustained
speed step. Items are defect-fix / sharpening of this change's own scope,
not new scope (see D11).

- [x] 8.1 Record D11 in design + the fallback-clamp spec scenarios
  (fallback clamped `[SYNC_MIN, relay_base]` only, never `[lo,hi]`;
  `[lo,hi]` gates only the confident estimator). *(this artifact pass)*
- [x] 8.2 Firmware: gate the `[lo,hi]` clamp (`sync.c:~1804-1811`) on
  `use_estimate`. Unconfident fallback **and** cold-start seed paths keep
  `extruder_est_sps × NEUTRAL_FRAC` clamped only to
  `[SYNC_MIN, relay_base]` — byte-identical to archived
  `relay-buffer-control-2switch` round-2. Estimator path `[lo,hi]`
  unchanged. Fixes the regression that contradicts §2.6 / the
  "Unconfident fallback … no behavior change" requirement.
- [x] 8.3 `flare_analyze`: add a coverage verdict — per-bucket sample
  counts + PASS/WARN naming the single deficiency (low/high bucket
  under-sampled → print slower/faster/taller; no constant-baseline
  segment detected). Pure deterministic function of the captured CSV;
  no new authority; existing acceptance-gate parity preserved.
- [x] 8.4 TUNING.md §6.1: prescribe the single purpose-built calibration
  model (sustained slow+fast + feature-boundary steps; speed-banded
  tower preferred) and the anti-patterns (no 3 prints
  slow+fast+combined; no "many different models" — the
  no-constant-baseline trap).
- [x] 8.5 Rollback proof: with confidence unreachable + seed elapsed +
  `[lo,hi]` set absurdly narrow, fallback NEUTRAL == archived round-2
  (the narrow bounds no longer underfeed the fallback);
  `ninja -C build_local` + `py_compile` +
  `openspec validate relay-duty-estimator-and-tuning --strict` green.
- [x] 8.6 Commit + push to main.

  2026-05-19 validation: `sync.c` relay NEUTRAL block now gates
  `[RELAY_ESTIMATE_LO_SPS, RELAY_ESTIMATE_HI_SPS]` clamp on `use_estimate`
  only; fallback/seed path is clamped to `[SYNC_MIN_SPS, relay_base]`
  (D11(a) fix). `flare_analyze.py` gains `relay_duty_coverage()` that
  splits paired duty-cycle estimates into low/high buckets at the median,
  reports per-bucket counts, and emits PASS/WARN with named deficiency;
  result is embedded as a comment block in the review patch when relay
  recommendations are present. `TUNING.md` now prescribes the purpose-built
  single-model capture (speed-banded tower preferred) and names the
  anti-patterns (3-print split, many-model approach). Three new tests
  (`relay-cov-pass`, `relay-cov-warn`, `relay-d11`) added; all 27 tests
  pass. `ninja -C build_local` green.

## 9. Blended-estimate ratchet fix (D12, on-Pi confirmed)

On-Pi 60×60 cube, 2-round bimodal capture: `relay_estimate_hi`
collapsed `1600→1062→612`, `relay_seed_rate` `1600→143→137` with no
demand change, while `baseline_rate`/`fill_p90` correctly held ~2193.
COMPRESSION rows crushed `940→290` — duty blend pinned to `v_low` under
the intended never-COMPRESSION lean. Analyzer-only defect fix (firmware
D2 math unchanged); see D12.

- [x] 9.1 Record D12 in design (mechanism, on-Pi evidence, fill-anchor
  decision, runtime corollary) + Risks + Open Questions entries.
  *(this artifact pass)*
- [x] 9.2 `flare_analyze.relay_duty_recommendations`: re-source
  `relay_estimate_hi` from `p90(fill_rates)·margin` and
  `relay_seed_rate` from fill-phase p50 (or EST p50); wrap both in a
  `max(current, …)` monotone floor (mirror `baseline_rate:738`). Keep
  `relay_estimate_lo` blend/`p10`-sourced (did not collapse on-Pi).
  Resolve the D12 open: `hi` margin factor; `seed` source; whether
  `relay_seed_rate` survives as a distinct scalar or folds into
  `relay_base`. Must stay deterministic (D7 parity).
- [x] 9.3 Regression-replay the two on-Pi CSVs (review1/review2):
  assert post-fix `hi`/`seed` do **not** ratchet round-over-round and
  land within margin of `fill_p90`/EST p50 (~2000–2200), not the
  collapsed 612/137. Add as a deterministic analyzer test fixture
  (sanitized rows) alongside `relay-d11`.
- [x] 9.4 Acceptance-gate parity: existing non-relay analyzer outputs
  byte-identical; `relay-cov-*`/`relay-d11` still green;
  `python3 -m py_compile scripts/*.py`; full analyzer test suite.
- [x] 9.5 TUNING.md: note the bimodal-model ratchet failure mode and
  that `hi`/`seed` are now fill-anchored (calibration no longer needs
  COMPRESSION dwell — consistent with the D10 lean, resolves the D11(b)
  capture tension for never-COMPRESSION steady state).
- [x] 9.6 `openspec validate relay-duty-estimator-and-tuning --strict`
  green; commit + push to main.
