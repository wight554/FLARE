# fix-settings-persistence-leaks

## Why

The runtime-settings round-trip (`settings_defaults()` → `settings_save()` →
`settings_load()`) has drifted out of symmetry. The audit of the SET/GET/persist
surface found one correctness bug and three dead-code duplications, all in
`firmware/src/settings_store.c` (plus one in `scripts/gen_config.py`):

- **`BUF_VARIANCE_BLEND_FRAC` / `BUF_VARIANCE_BLEND_REF_MM` are write-only flash
  fields.** Both are in `settings_t`, written by `settings_save()`
  (`settings_store.c:402-403`), `SET:`/`GET:`-able (`protocol.c:966-967`), and
  static-initialized to their `CONF_*` in `main.c:126-127` — but they are
  **never read in `settings_load()` and never set in `settings_defaults()`**.
  So `SET:VAR_BLEND_FRAC:0.3` → `SV:` (save) → reboot **silently reverts** to the
  compile-time default. The operator sees `GET:` confirm the value, saves, and
  loses it on the next power cycle. This violates the existing
  `project-architecture` requirement *"Runtime tunables shall follow the full
  parameter path"* (owning variable **and settings persistence** wired).

- **`settings_load()` runs the entire per-lane TMC copy loop twice** — once at
  `settings_store.c:526-541`, again at `604-618` (`TMC_ROTATION_DISTANCE[i] =
  s->tmc_rotation_distance[i]` and siblings, plus `FOLLOW_TIMEOUT_MS[i]`). The
  second pass is pure redundant work on every boot.

- **`settings_defaults()` assigns `MM_PER_STEP[0/1]` twice** (`257-258` and
  `285-286`) from the same `CONF_L*_MM_PER_STEP`.

- **`gen_config.py` `DEFAULTS` lists `buf_psf_max_comp/max_tens/neutral/goal`
  twice** (`gen_config.py:51-54` and `151-155`), identical values. Dict-literal,
  last wins — harmless, but it hides the real entries and invites a future
  edit-one-miss-the-other skew.

There is also a guard gap that let this happen: **nothing asserts that a
`settings_t` field written by `save()` is read by `load()` and seeded by
`defaults()`.** A `gen_config`-adjacent parity check would have caught the
`BUF_VARIANCE_BLEND` leak at CI time.

## What Changes

- Read `buf_variance_blend_frac` / `buf_variance_blend_ref_mm` in
  `settings_load()` (clamped to the same `SET:` bounds — frac `[0.0,0.9]`, ref
  `[0.5,5.0]`) and seed both in `settings_defaults()` from `CONF_*`, completing
  the round-trip the struct + `save()` already promise.
- Seed `RELOAD_MODE = CONF_RELOAD_MODE` in `settings_defaults()`. The new parity
  test surfaced it as a second instance of the same class: `RELOAD_MODE` is
  read in `settings_load()` but never set in `settings_defaults()`, so a runtime
  factory-reset (`settings_defaults()` without a reboot) leaves it at the last
  loaded value instead of the compiled default.
- Delete the duplicate TMC/`FOLLOW_TIMEOUT_MS` copy loop in `settings_load()`
  (keep the first, `526-541`).
- Delete the duplicate `MM_PER_STEP[0/1]` assignment in `settings_defaults()`.
- De-duplicate the `buf_psf_*` block in `gen_config.py` `DEFAULTS`.
- Add a **persistence parity test** (Python, alongside `test_gen_config.py`)
  that parses `settings_store.c` and fails if any `settings_t` field is written
  in `settings_save()` but absent from `settings_load()` or
  `settings_defaults()`.

## Non-Goals

- **No `SETTINGS_VERSION` bump.** Every change reads/writes the *existing*
  `settings_t` layout (the `buf_variance_blend_*` fields already exist and are
  already saved). Layout is byte-identical, so persisted operator
  settings (TMC currents, calibration) are **not** wiped. A bump here would be a
  regression, not a fix.
- **No behavior change** beyond making a persisted value actually persist.
- **No reclassification of any tunable.** Whether a knob *should* be persisted
  vs. live in a header is the separate `tier-config-surface` change. In
  particular the SET-able-but-unpersisted estimator/telemetry knobs
  (`EST_LOW_CF_WARN_THRESHOLD`, `EST_FALLBACK_CF_THRESHOLD`,
  `TENSION_RISK_WINDOW_MS`, `TENSION_RISK_THRESHOLD`) are **intentionally out of
  scope** — they are not `settings_t` fields, so the parity test does not flag
  them, and their keep-vs-demote decision belongs to `tier-config-surface`.
