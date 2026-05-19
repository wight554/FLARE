## Why

`relay-confident-path-keep-or-remove` ran the missing single-regime
on-hw experiment and returned an unambiguous **REMOVE** verdict: in a
clean constant-60 mm³/s spiralize vase (the confident path's
designed-for low-flip home, confidence reached and held at RDE1% 57.5)
the confident type-D relay duty estimator drove **TENSION 42.6 %, the
buffer pegged at the +12.5 mm empty wall, COMPRESSION never** —
catastrophically worse than the `extruder_est_sps` fallback in the
identical regime (TENSION 1.0 %, BP ±5). The confident path has no
regime where it earns its keep, yet it carries firmware state, a
confidence gate, telemetry, an offline analyzer subsystem, config keys,
and tests. This change deletes it: the relay becomes fallback-only.

## What Changes

- **Firmware (`sync.c`).** NEUTRAL always uses the proven
  `extruder_est_sps · RELAY_NEUTRAL_FRAC` fallback. Delete the confident
  `v_est` NEUTRAL branch, its `[lo,hi]` clamp, the duty-estimator state
  + `v_est` blend, the confidence gate, and the
  `relay_estimator_accumulate` / `complete_phase` machinery. TENSION→
  catch-up and COMPRESSION→`SYNC_MIN` branches **unchanged**.
- **Telemetry.** Remove `RDE`/`RDCF`/`RDV` from the status string
  (`protocol.c`), `flare_cmd.py --dump`, and host parsers. `BUF`/`BP`/
  `EST`/etc. unchanged. **BREAKING (status field surface).**
- **Analyzer (`flare_analyze.py`).** Remove `relay_duty_recommendations`,
  `relay_duty_coverage`, and the `relay_estimate_lo/hi` + duty-cycle
  emit. `baseline_rate`, flow schedule, acceptance gate, and all
  non-relay output stay **byte-identical**. Retire the now-moot relay
  tests (`relay-d12`, `relay-d12-real`, `relay-cov-pass`,
  `relay-cov-warn`, `relay-d11`) and the `tests/fixtures/relay_review*`
  if unused elsewhere.
- **Config (`gen_config.py` / `config.ini`).** Remove
  `relay_estimate_lo`, `relay_estimate_hi`, `relay_confidence_cycles`,
  `relay_confidence_window_ms`, `relay_seed_warmup_ms` + their `CONF_*`
  macros. Keep `relay_catchup_frac`, `relay_neutral_frac` (fallback
  knobs), `relay_min_flip_mm` (0.0 inert knob, guard stays),
  `relay_collapse_*` (G3 stop-ramp). **BREAKING (config keys).**
- **Settings (`settings_store.c`).** Remove the deleted relay fields,
  bump `SETTINGS_VERSION` (persisted settings reset to defaults on
  flash — re-apply any custom SET).
- **Docs.** `TUNING.md`: delete the relay duty-estimator /
  confidence-gate sections, the offline relay capture/apply loop, the
  bimodal-ratchet note. Keep the fallback relay law, collapse-ramp,
  `relay_min_flip_mm` caveat. `MANUAL.md` if it cites `RDE`/the relay
  estimator.

Not touched: the archived `relay-duty-estimator-and-tuning` (immutable;
its D2/D7/D12 become dead history). The fallback relay law itself
(catch-up / COMPRESSION-stop / NEUTRAL = `extruder_est_sps·neutral_frac`),
G3 collapse-ramp, and the `relay_min_flip_mm` guard are unchanged.

## Capabilities

### New Capabilities
- `relay-fallback-only`: the post-removal type-D relay contract — NEUTRAL
  is always the `extruder_est_sps` fallback, no duty estimator /
  confidence gate / `[lo,hi]` clamp / `RDE`-`RDCF`-`RDV` telemetry; the
  analyzer emits no relay duty recommendations; non-relay analyzer
  behavior and the catch-up / COMPRESSION-stop branches are preserved.

### Modified Capabilities
- `operator-tuning-guide`: relay section reduced to the fallback law +
  collapse-ramp + `min_flip` caveat; the estimator/confidence-gate /
  offline relay-analyzer tuning content is removed.

## Impact

- Firmware: `sync.c` (relay law, estimator state, gate),
  `protocol.c` (status fields), `settings_store.c` (fields +
  `SETTINGS_VERSION`).
- Scripts: `flare_analyze.py` (relay duty machinery),
  `gen_config.py` / `config.ini` / `config.ini.example` (keys),
  `flare_cmd.py` (dump), `test_flare_analyze.py` /
  `test_gen_config.py` (retire relay tests), `tests/fixtures/`.
- Docs: `TUNING.md`, `MANUAL.md`.
- Parity guard: all non-relay analyzer output byte-identical;
  `gen_config`, firmware build, non-relay analyzer suite green.
- BREAKING: status-field surface + relay config keys (active dev,
  stale-keys-ignored policy; documented).
