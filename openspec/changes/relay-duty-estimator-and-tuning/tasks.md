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

- [ ] 1.1 Add `config.ini` keys: relay catch-up, relay NEUTRAL frac,
  `relay_estimate_lo`, `relay_estimate_hi`, estimator confidence window +
  threshold, `relay_seed` (D10b cold-start seed source). Defaults =
  **the locked 4.2 baseline: catch-up `1.30`, NEUTRAL `1.25`** (NOT the
  old 1.45/1.10 `#define`s — those are the rejected round-1 pair).
- [ ] 1.2 Wire keys through `gen_config.py` into the generated tune header
  (mirror `baseline_rate` / `sync_compression_bias_frac` pattern).
- [ ] 1.3 Delete the legacy `SYNC_RELAY_*_FRAC` `#define`s in `sync.c`;
  consume generated values. Mark runtime-safe knobs as `SET:` params;
  keep safety bounds flash-only.
- [ ] 1.4 `gen_config.py` test + `ninja -C build_local` green; status
  snapshot identical to the **locked-baseline build** (1.30/1.25 → behavior
  byte-identical to archived `relay-buffer-control-2switch` round-2).
- [ ] 1.5 `config.ini.example` updated; stale `#define` references gone.

## 2. Firmware duty-cycle estimator (D2/D3/D4)

- [ ] 2.1 Add per-state filament-travel accumulators reset on each
  TENSION↔COMPRESSION flip; pair completed opposite segments into duty
  cycles. Use commanded-feed (steps/s) space, FLARE sign
  (`+1=tension/-1=compression`) — NOT Happy Hare RD space or sign.
- [ ] 2.2 Compute `fh = dh/(dl+dh)`, `v_est = (1-fh)·v_low + fh·v_high`;
  fix the per-phase feed measurement as instantaneous-commanded or
  dwell-averaged and document it (must be deterministic for §4 parity).
- [ ] 2.3 Confidence gate: minimum paired cycles within a recency window
  (seed from HH `autotune_cert_window=8` analogue, value config-driven).
- [ ] 2.4 In the relay override (`sync.c:~1622-1634`) replace ONLY the
  `BUF_NEUTRAL` target with `clamp(v_est, lo, hi)` when confident;
  `BUF_TENSION`/`BUF_COMPRESSION` branches untouched.
- [ ] 2.5 Apply the existing never-TENSION compression lean AFTER the
  estimate, BEFORE existing ramp/clamp (order: estimate → lean → clamp
  `[lo,hi]` → ramp/clamp).
- [ ] 2.6 Unconfident/stale → fall back to
  `extruder_est_sps × SYNC_RELAY_NEUTRAL_FRAC` (frac = locked **1.25**)
  with no feed discontinuity. Per D10(a) this is the *normal* steady
  state in a good low-flip cycle, not an error path — do not treat
  unconfident as a fault.
- [ ] 2.7 D10(b) cold-start seed: at print start / boot, seed the
  fallback feed from the offline relay baseline (`relay_seed` /
  `relay_base` / `[lo,hi]` midpoint) instead of cold `extruder_est_sps`,
  for a warmup window (exit on EST-warm or first-confident). Targets the
  deferred 4.2 round-2 startup-bangbang. No flash persistence.
- [ ] 2.8 Estimator + seed state volatile only — assert no flash write
  path is reachable from estimator/seed updates.
- [ ] 2.9 Host build + captured status snapshot: with confidence gate
  unreachable AND seed window elapsed, steady-state behavior ==
  archived `relay-buffer-control-2switch` round-2 (rollback proof).

## 3. Telemetry + neutral_creep resolution (D5)

- [ ] 3.1 Add status/telemetry: estimate-vs-fallback flag + confidence
  value.
- [ ] 3.2 `neutral_creep` (`sync.c:395-428`): **leave intact** per the
  committed 7.2-A disposition (intended-inert telemetry, kept computing,
  not removed — commit `a78d864`). Do NOT delete it or evict its slot.
  Estimator estimate/confidence telemetry (3.1) is a **separate new**
  status field. Add a code comment cross-linking 7.2-A so it is not
  re-flagged as dead.
- [ ] 3.3 Update `protocol.c` / any host parsers in lockstep; host build +
  `py_compile` green.

## 4. Deterministic offline relay analyzer (D7)

- [ ] 4.1 Extend capture (if needed) so switch-flip timestamps + commanded
  feed are present in the CSV for relay runs.
- [ ] 4.2 `flare_analyze`: compute the same duty statistics offline;
  emit recommended `relay_base` + estimator `[lo, hi]` into the existing
  `config.ini`/flow-schedule emit. Pure function of inputs.
- [ ] 4.3 Determinism test: same input CSVs → identical relay
  recommendation (byte-stable).
- [ ] 4.4 Acceptance-gate parity test: existing non-relay inputs produce
  unchanged schedule + FAIL/WARN/PASS verdict.
- [ ] 4.5 `python3 -m py_compile scripts/*.py`; analyzer/test suite green.

## 5. Anti-chatter option (D8, secondary)

- [ ] 5.1 Add optional distance-hysteresis flip guard alongside
  `BUF_HYST_MS`; config-selectable; default = existing time-based
  (behavior unchanged when unset).
- [ ] 5.2 Capture HH relief-fraction snap as a reference note in the
  `relay-duty-estimator` spec/design only (analog, no rig — not shipped).

## 6. Docs (T3/T4)

- [ ] 6.1 TUNING.md: add the type-D relay-law tuning section —
  config keys, relay capture/analyze loop, runtime estimator (estimate vs
  fallback, reading confidence). State knobs are config-driven, not
  compile-time.
- [ ] 6.2 TUNING.md: fix stale status token `TB` → `CB` (verify against
  `protocol.c:197` emission).
- [ ] 6.3 Record the Happy Hare polarity-inversion landmine where the
  analog reference is cited (`+1=compression` HH vs `+1=tension` FLARE —
  flip every sign on any analog port); cross-link
  `relay-buffer-control-2switch` task 7.3.
- [ ] 6.4 Note in this change's artifacts that
  `relay-buffer-control-2switch` 7.2 was decided **A** (neutral_creep
  intended-inert telemetry, kept) and is **honored, not reopened** here
  (§3.2); 7.3 was split to `pending-analog-rig`. Do not edit the
  archived change's files.
- [ ] 6.5 Record the D10(c) accepted-limitation: the 4.2 round-2
  end-of-print COMPRESSION `SYNC_MIN`-grind is out of scope (D1 forbids
  COMPRESSION-branch edits; print-tail, draw≈0, auto-stop-handled, no
  quality impact). State it in TUNING.md / the `relay-duty-estimator`
  spec; cross-link, do not silently drop.

## 7. Validation + closeout

- [ ] 7.1 `ninja -C build_local` green; `python3 -m py_compile scripts/*.py`.
- [ ] 7.2 `openspec validate relay-duty-estimator-and-tuning --strict`.
- [ ] 7.3 Analog parity reasoning recorded: `BUF_SENSOR_TYPE != 0`
  untouched / byte-identical.
- [ ] 7.4 `TEST_CASES.md`: relay duty-estimator regression entry
  (estimate path, fallback path, bounds clamp, determinism).
- [ ] 7.5 On-Pi A/B vs the §0.1 baseline: cycle stays slow/shallow/
  never-TENSION; tune via the new config keys; record results.
- [ ] 7.6 Commit + push to main.
