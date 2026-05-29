# Design — fix-settings-persistence-leaks

## The defaults/save/load contract

Every durable tunable obeys a three-function round-trip in
`firmware/src/settings_store.c`:

```
settings_defaults()  RAM global ← CONF_*            (cold start / bad CRC)
settings_save()      settings_t field ← RAM global  (SV:)
settings_load()      RAM global ← settings_t field  (boot, valid CRC)
```

For a field that is written by `save()`, the invariant is:

```
field ∈ save()  ⇒  field ∈ load()  ∧  global ∈ defaults()
```

`buf_variance_blend_frac` / `_ref_mm` break it: present in the struct and in
`save()`, absent from `load()` and `defaults()`. They are written to flash and
then ignored on the way back.

## Findings (audited)

| Sev | Item | Site | Fix |
|-----|------|------|-----|
| **HIGH** | `BUF_VARIANCE_BLEND_FRAC`/`_REF_MM` saved, never loaded/defaulted | save `settings_store.c:402-403`; missing in `load()`/`defaults()` | add `load()` (clamped) + `defaults()` from `CONF_*` |
| MED | `RELOAD_MODE` loaded, never defaulted (found by parity test) | `load()` sets it; `defaults()` did not | seed `RELOAD_MODE = CONF_RELOAD_MODE` in `defaults()` |
| LOW | TMC copy loop duplicated in `load()` | `settings_store.c:526-541` **and** `604-618` | delete second loop |
| LOW | `MM_PER_STEP[0/1]` assigned twice in `defaults()` | `257-258` **and** `285-286` | delete second assignment |
| LOW | `buf_psf_*` duplicated in `DEFAULTS` | `gen_config.py:51-54` **and** `151-155` | delete one block |

## Why no version bump

`buf_variance_blend_frac` and `buf_variance_blend_ref_mm` are **already declared
in `settings_t`** and already written by `settings_save()`. Adding `load()` and
`defaults()` lines touches only code, not the on-flash byte layout. `crc32` and
field offsets are unchanged, so a unit with valid persisted settings keeps them
and simply starts honoring the previously-ignored blend fields. Bumping
`SETTINGS_VERSION` would force a wipe-to-defaults of every operator's TMC and
calibration state — the exact cost this project avoids for value-only changes
(cf. `relay-neutral-frac-detune` non-goals).

A unit that previously `SV:`'d a non-default blend value will, after this fix,
start loading it (it was in flash all along). If that surprises anyone, it is
strictly the value they explicitly set; the pre-fix behavior was the bug.

## Parity guard

A parser-based test prevents recurrence without a runtime harness:

```
parse settings_t fields                     (between "typedef struct {" / "} settings_t;")
parse "s.<field> ="  lines  → SAVED set     (settings_save body)
parse "= s-><field>" lines  → LOADED set    (settings_load body)
parse "<GLOBAL> ="          → DEFAULTED set (settings_defaults body, via known field→global map)
assert SAVED ⊆ LOADED       (no write-only fields)
```

The `defaults()` arm keys on the global-name spelling (e.g. field
`buf_variance_blend_frac` ↔ global `BUF_VARIANCE_BLEND_FRAC`), matching the
existing uppercase-macro convention. Scope is `settings_t` fields only — this is
why the SET-able-but-non-struct estimator/telemetry knobs are not flagged here;
they are `tier-config-surface`'s concern.

## Validation

- `python3 scripts/gen_config.py` regenerates `tune.h` unchanged (the `DEFAULTS`
  de-dup is value-identical).
- Firmware builds clean; `SETTINGS_VERSION` unchanged.
- New parity test fails on the current tree (proves it catches the bug), passes
  after the `load()`/`defaults()` additions.
- Manual: `SET:VAR_BLEND_FRAC:0.3` → `SV:` → reboot → `GET:VAR_BLEND_FRAC`
  returns `0.3` (was reverting to `CONF_BUF_VARIANCE_BLEND_FRAC`).
