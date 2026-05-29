# Tasks — tier-config-surface

## 1. Ratify the classification

- [x] 1.1 Produce the full per-param tier table (T0/T1/T2/T3) covering every
  `gen_config.py` `DEFAULTS` key + every `SET:` handler, applying the rubric in
  `design.md`. → frozen in `design.md` "Frozen classification".
- [x] 1.2 Record an explicit ruling for each borderline param (`sync_reserve_pct`,
  `sync_overshoot_pct`, `sync_kp_rate`, ramp/tick knobs, `buf_stab_rate`,
  `pre_ramp_rate`) per the borderline table; "documented procedure ⇒ T2". →
  `design.md` "Explicit borderline rulings".
- [x] 1.3 Freeze the T3 set (~45 params, `design.md` table); this is the demotion
  scope.

## 2. Migration scaffolding (do before removing anything) — DONE

- [x] 2.1 Add a shared `DEPRECATED_KEYS` manifest in `gen_config.py` (seeded with
  the 4 SET-orphan keys; grows as the bulk batch demotes more).
- [x] 2.2 `gen_config.validate_known_keys`: a key in `DEPRECATED_KEYS` warns to
  stderr and is ignored, instead of `sys.exit(1)`.
- [x] 2.3 `flare_cmd.py --config`: skip demoted keys when dumping (removed the
  `EST_LOW_CF_THR`/`EST_FALLBACK_THR` dump entries).
- [x] 2.4 Verified: a `config.ini` with `tension_risk_threshold` warns + builds
  (exit 0); tune.h no longer emits the 4 demoted `CONF_*`.

## 3. Demote T3 to code (no value change)

> FOUNDATION LANDED: the 4 SET-orphans (`est_low_cf_warn_threshold`,
> `est_fallback_cf_threshold`, `tension_risk_window_ms`, `tension_risk_threshold`)
> are fully demoted as the end-to-end proof — they were not in `settings_t`, so
> this slice needed **no version bump**. Mechanism verified (release + dev build
> green). The remaining ~40 persisted T3 params are the BULK BATCH below.

- [x] 3.1 Unit-independent T3 defined as `#define` in `tune_internal.h`
  (~37: orphans + drift + dense cluster + singletons); motor-dependent rate T3
  (`sync_ramp_*`, `zone_bias_*`, `buf_stab`, `pre_ramp`, `ramp_step`, plus the
  tick conversion inputs `sync_tick_ms`/`ramp_tick_ms`) keep their `CONF_*`.
- [x] 3.2 Removed from `config.ini.example`, `gen_config.py` `DEFAULTS`, and the
  `tune.h` emit line. `tune.h` CONF_ count 150→114.
- [x] 3.3 Removed all **43** T3 fields from `settings_t` + `defaults()`/`save()`/
  `load()`. `settings_t` 109→~67 fields; parity 108→**67/67**; `baseline_alpha`
  (`g_baseline_alpha` subsystem) handled incl. the direct `sync.c` EWMA use.
  Caught: `sync_tick_ms`/`ramp_tick_ms` are `accel→sps` conversion inputs in
  `gen_config`, so they keep `CONF_*` (compile math must match runtime tick).
- [x] 3.4 All demoted `SET:`/`GET:` wrapped in `#ifdef FLARE_DEV_TUNING`
  (release replies `ER:SET:UNKNOWN_PARAM`; `flare_cmd --config` auto-skips any
  `UNKNOWN_PARAM` GET so the dump omits demoted keys). `live_tune_locked_param`
  list unaffected (its members `COMPRESSION_BIAS_FRAC`/`BASELINE_*` stay T2).
- [x] 3.5 `EST_LOW_CF_*` / `TENSION_RISK_*` inconsistency RESOLVED — demoted;
  release `SET:`/`GET:` now dev-only, value owned by `tune_internal.h`.

## 4. Dev escape hatch — DONE (mechanism)

- [x] 4.1 `FLARE_DEV_TUNING` CMake `option(... OFF)` + conditional
  `target_compile_definitions`; undefined in release.
- [x] 4.2 T3 `SET:`/`GET:` branches wrapped in `#ifdef FLARE_DEV_TUNING`
  (4 orphans done; bulk wraps remaining branches). Release build links clean →
  a gated key falls through to `ER:SET:UNKNOWN_PARAM`.
- [x] 4.3 Dev build (`-DFLARE_DEV_TUNING=ON`) configures + links clean; the
  gated globals have no `settings_t` field, so a dev `SET:` is inherently
  ephemeral (reverts to the `tune_internal.h` value on reboot).

## 5. Version + docs

- [x] 5.1 `SETTINGS_VERSION` 57→58 for the `settings_t` shrink (done once; the
  remaining-21 removals ride the same v58 since nothing is flashed/released yet).
- [x] 5.2 `config.ini.example`: demoted entries replaced with a consolidated
  "Internal control-loop constants (NOT user-tunable)" note pointing at
  `tune_internal.h`. (`TUNING.md` deep-pass — minor doc follow-up.)
- [x] 5.3 `MANUAL.md`: added a dev-build-only note at the `Parameters` head
  (lists the gated keys, explains `ER:SET:UNKNOWN_PARAM` + `FLARE_DEV_TUNING`);
  trimmed the demoted rows from the Smarter Sync / Sync-Feedback / Speeds /
  Safety tables (also fixed stale `RELAY_NEUTRAL_FRAC` default 1.25→1.10).

## 6. Build + regression

- [x] 6.1 `gen_config.py` regenerates a smaller `tune.h` (CONF_ 150→114); T1/T2
  macros unchanged.
- [x] 6.2 Release firmware builds + links clean; dev (`-DFLARE_DEV_TUNING`)
  builds + links clean.
- [x] 6.3 `test_gen_config.py` + `test_settings_parity.py` + full
  `validate_regression.sh` ("Static Regression Gate Passed") green; `py_compile`
  clean.
- [ ] 6.4 On-hardware: confirm control behavior byte-identical (sync soak
  before/after = same `BS`/endstop profile). OPEN — blocked on rig. Values were
  preserved (FLARE_INT_* == prior CONF_* defaults), so no behavior change expected.
