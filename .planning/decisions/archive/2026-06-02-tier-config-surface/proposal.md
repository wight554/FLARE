# tier-config-surface

## Why

The config surface is flat: ~150 `CONF_*` macros (`tune.h`), ~148 `config.ini`
keys (`gen_config.py` `DEFAULTS`), ~85 persisted `settings_t` fields, ~120
`SET:`/`GET:` handlers — and **every** tunable is treated as if it deserves the
full path (config default + flash persist + SET + GET + docs). It does not.

Three kinds of parameter are conflated:

1. **Hardware facts** — motor `full_steps`/`gear_ratio`/`rotation_distance`,
   TMC `driver_*`, `run_current`, `dir_invert`. Wrong = broken, not suboptimal.
2. **This-build geometry + operator taste** — sensor `dist_*`, `buf_*` travel,
   `baseline_rate`, `sync_compression_bias_frac`, the flow schedule, relay
   `catchup`/`neutral` fracs, speeds. Per-machine or per-print; genuinely owned
   by the operator. The live tuner exists for exactly these.
3. **Internal control-loop constants** — EWMA time constants and filter alphas
   (`baseline_alpha`, `buf_drift_ewma_tau_ms`, `est_alpha_min/max`,
   `buf_analog_alpha`, `psf_vel_alpha`), estimator-confidence thresholds, the
   variance-blend, the drift observer, the relay-collapse law, neutral-creep,
   zone-bias, PSF control internals, debounce/tick windows. ~40 knobs that were
   set **once** during firmware development ("rig-validated 2026-05-28" in the
   comments), have **no documented operator tuning procedure**, and are tied to
   the algorithm, not the hardware or the print.

Kind 3 pays the full plumbing tax for nothing. Each one is ~10 edit sites and,
when it lives in `settings_t`, every tweak bumps `SETTINGS_VERSION` (now **57**)
and wipes every operator's persisted TMC/calibration on update. The flat surface
is also where consistency has *already* broken: the SET-able-but-unpersisted
`EST_LOW_CF_WARN_THRESHOLD` / `EST_FALLBACK_CF_THRESHOLD` /
`TENSION_RISK_WINDOW_MS` / `TENSION_RISK_THRESHOLD` (`protocol.c:975-989`, no
`settings_t` field, no `defaults()` seed) are kind-3 knobs that leaked into the
runtime surface and now reset silently on reboot. The mess clusters exactly
where kind-3 doesn't belong.

Evidence the runtime surface is unnecessary for kind 3: **`flare_live_tuner.py`
writes only `COMPRESSION_BIAS_FRAC` and `BASELINE_SPS`** (both kind-2, both
already in `live_tune_locked_param`, `protocol.c:96-113`). No automated tool
sweeps a kind-3 knob; the only consumer of their `SET:` handlers is ad-hoc
manual bench experimentation.

## What Changes

- **Define an explicit tier model** (`config-surface-tiers` capability) with a
  decision rubric: T0 board constant → T1 hardware limit → T2 durable
  (geometry + subjective) → T3 internal constant.
- **Demote T3 to code.** Internal control-loop constants become
  `static const` / `#define` in the owning module (`sync.c`, the estimator, the
  drift observer, PSF), keyed to their current `CONF_*` value (no behavior
  change). They leave `config.ini.example`, `gen_config.py` `DEFAULTS`,
  `settings_t`, `defaults()`/`save()`/`load()`, and the release `SET:`/`GET:`
  surface.
- **Add an optional `FLARE_DEV_TUNING` build flag** that re-exposes the demoted
  knobs as **ephemeral** `SET:` (runtime poke, no persist, no `config.ini`).
  Off in release builds; the handlers compile out entirely.
- **Graceful migration for demoted keys.** `gen_config.py` warns-and-ignores a
  demoted key in an existing `config.ini` (instead of the current hard
  `unknown config key` exit), and `flare_cmd.py --config` stops emitting demoted
  keys, so a dumped config still rebuilds. Both read one shared demoted-key
  manifest.
- **One `SETTINGS_VERSION` bump** for the `settings_t` shrink — paid once. After
  this, iterating a kind-3 constant is a one-line recompile with **zero** version
  bumps and zero operator-settings wipes.

## Non-Goals

- **No control-law or behavior change.** Each demoted constant keeps its exact
  current value. This is a relocation of *where* a number lives, not a retune.
  (Retuning any specific knob is a separate value-only change, cf.
  `relay-neutral-frac-detune`.)
- **No change to T0/T1/T2 plumbing.** Hardware, geometry, and operator-taste
  knobs keep config.ini + persist + SET + GET + round-trip. The live tuner's two
  knobs are untouched.
- **Not a dependency of, and does not block, `fix-settings-persistence-leaks`.**
  That change fixes the `settings_t` round-trip under the current model; this one
  shrinks the model. They compose: leaks fixed first, then surface trimmed.
- **No new GUI/tuning workflow.** Operator-facing tooling already targets T2.
