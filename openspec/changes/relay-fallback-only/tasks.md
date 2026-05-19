## 1. Firmware: NEUTRAL = fallback only (R1/R2)

- [ ] 1.1 `sync.c` relay block (~1786-1820): replace NEUTRAL selection
  with the unconditional fallback
  `clamp(extruder_est_sps, SYNC_MIN, relay_base) · RELAY_NEUTRAL_FRAC`.
  Delete the `use_estimate` branch, `g_relay_estimate_sps`, the
  `[lo,hi]` clamp, and seed selection. TENSION/COMPRESSION branches
  untouched.
- [ ] 1.2 Delete estimator machinery: `relay_estimator_accumulate`,
  `relay_estimator_complete_phase`, `relay_estimator_confident`, the
  `v_est` blend (~257-258), duty/travel accumulators, pair-history,
  confidence gate (~228-231), `relay_seed_active`. Grep-confirm no
  remaining callers.
- [ ] 1.3 `ninja -C build_local` green; diff the relay block to confirm
  **only** the confident path was removed (catch-up / COMPRESSION /
  fallback expression byte-identical).

## 2. Telemetry shrink (R3, BREAKING)

- [ ] 2.1 `protocol.c`: remove `RDE`/`RDCF`/`RDV` from the status
  string and any estimate/confidence `SET:`/`GET:` + name-list
  entries. Keep `BUF`/`BP`/`EST`/`NC`/etc.
- [ ] 2.2 `flare_cmd.py`: drop the `RDE`/`RDCF`/`RDV` and removed-key
  dump entries. Host build + `py_compile` green; status parses.

## 3. Config + settings removal (R5, BREAKING)

- [ ] 3.1 Remove `relay_estimate_lo`, `relay_estimate_hi`,
  `relay_confidence_cycles`, `relay_confidence_window_ms`,
  `relay_seed_warmup_ms` from `config.ini`, `config.ini.example`,
  `gen_config.py` defaults + `CONF_*` emit. Keep
  `relay_catchup_frac`/`relay_neutral_frac`/`relay_min_flip_mm`/
  `relay_collapse_*`.
- [ ] 3.2 `settings_store.c`: remove the deleted relay fields
  (struct/apply/save/load); bump `SETTINGS_VERSION`. Document the
  persisted-settings reset.
- [ ] 3.3 `test_gen_config.py`: drop assertions for removed macros;
  `gen_config.py` + `test_gen_config.py` + `ninja` green.

## 4. Analyzer subtraction with parity (R4)

- [ ] 4.1 `flare_analyze.py`: remove `relay_duty_recommendations`,
  `relay_duty_coverage`, relay duty stats, `relay_estimate_*`/
  `relay_seed_rate` emit + the `current`/DEFAULTS keys they used.
- [ ] 4.2 Retire relay tests in `test_flare_analyze.py`
  (`relay-d12`, `relay-d12-real`, `relay-cov-pass`, `relay-cov-warn`,
  `relay-d11`) and remove `tests/fixtures/relay_review{1,2}.csv`
  after grep-confirming no other consumer.
- [ ] 4.3 **Parity gate:** non-relay analyzer output byte-identical on
  existing non-relay fixtures; full non-relay `test_flare_analyze.py`
  + `py_compile scripts/*.py` green.

## 5. Docs (R6)

- [ ] 5.1 `TUNING.md`: delete the relay duty-estimator /
  confidence-gate sections, the offline relay capture/analyze/apply
  loop, the bimodal-ratchet note. Keep fallback law + collapse-ramp +
  `relay_min_flip_mm` caveat.
- [ ] 5.2 `MANUAL.md`: remove any `RDE`/relay-estimator references.
  Grep both for dangling estimator/removed-key mentions.

## 6. Validation + closeout

- [ ] 6.1 Full host check green: `ninja -C build_local`,
  `py_compile scripts/*.py`, `test_gen_config.py`, non-relay
  `test_flare_analyze.py`; non-relay parity byte-identical.
- [ ] 6.2 On-hw smoke: flash; `GET` confirms removed keys gone +
  `RDE` absent from `?:`; sync auto-arms (`SM:1`); a vase and the cube
  run fallback-class (low TENSION, BP off the +12.5 wall). Capture +
  record.
- [ ] 6.3 `openspec validate relay-fallback-only --type change
  --strict` green. Commit + push to main. Update memory
  `relay-confident-estimator-bimodal-bangbang` (REMOVE executed).
- [ ] 6.4 Archive `relay-confident-path-keep-or-remove` and
  `relay-confidence-gate-harden` once this lands and is hw-smoked
  (their verdict/decisions are now realized).