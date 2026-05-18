## 0. Prerequisite gate

- [ ] 0.1 Confirm `relay-buffer-control-2switch` task 4.2 (on-Pi A/B
  hardware baseline) has landed: a known-good slow/shallow/never-TENSION
  relay cycle is recorded. Do not start firmware behavior work until this
  baseline exists (the estimator is bounded/A-B'd against it).

## 1. Config-key migration (no behavior change)

- [ ] 1.1 Add `config.ini` keys: relay catch-up, relay NEUTRAL frac,
  `relay_estimate_lo`, `relay_estimate_hi`, estimator confidence window +
  threshold; defaults = current `#define` constants (catch-up 1.45,
  NEUTRAL 1.10).
- [ ] 1.2 Wire keys through `gen_config.py` into the generated tune header
  (mirror `baseline_rate` / `sync_compression_bias_frac` pattern).
- [ ] 1.3 Delete the legacy `SYNC_RELAY_*_FRAC` `#define`s in `sync.c`;
  consume generated values. Mark runtime-safe knobs as `SET:` params;
  keep safety bounds flash-only.
- [ ] 1.4 `gen_config.py` test + `ninja -C build_local` green; status
  snapshot identical (values unchanged → behavior byte-identical).
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
- [ ] 2.6 Unconfident/stale/boot → fall back to
  `extruder_est_sps × SYNC_RELAY_NEUTRAL_FRAC` with no feed discontinuity
  that destabilizes the cycle.
- [ ] 2.7 Estimator state volatile only — assert no flash write path is
  reachable from estimator updates.
- [ ] 2.8 Host build + captured status snapshot: with confidence gate
  unreachable, behavior == `relay-buffer-control-2switch` (rollback proof).

## 3. Telemetry + neutral_creep resolution (D5)

- [ ] 3.1 Add status/telemetry: estimate-vs-fallback flag + confidence
  value.
- [ ] 3.2 Resolve `neutral_creep` (`sync.c:395-428`): remove the dead
  compute; reuse its protocol slot for the estimator telemetry to avoid
  status-line churn (or hard-remove if no consumer reads it — decide from
  a consumer grep). No inert computed-but-unused path remains.
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
  `relay-buffer-control-2switch` task 7.2 is resolved by §3.2 (do not edit
  the other change's files here).

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
