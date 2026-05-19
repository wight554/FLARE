## Context

Buffer geometry is configured today by two tunables:

- `buf_half_travel_mm` (`tune.h` `CONF_BUF_HALF_TRAVEL_MM 7.8f`,
  `config.ini:119`) — **HALF** of the switch-to-switch distance.
- `buf_size_mm` (`tune.h` `CONF_BUF_SIZE_MM 22`, `config.ini:120`) —
  **FULL** maximum travel.

Internally `sync.c` is half-based: `buf_physical_half_travel_mm()`
(`sync.c:285-293`) returns `BUF_SIZE_MM*0.5` floored at
`BUF_HALF_TRAVEL_MM`; `buf_threshold_mm()` (`:295-303`) and all consumers
(`:305-372`, `:427`, `:1286`) use the half value. The relay law
(`relay-buffer-control-2switch`) is discrete-switch-state driven and does
**not** read these mm values for state decisions — only the virtual-position
estimate, predictive timing, park-target depth, and deadband scale off them.

Happy Hare / EMU Sync, the ecosystem operators come from, expresses the same
geometry as full-range values. The naming/units mismatch already produced a
miscalibration: `buf_half_travel_mm` was left at the untuned `7.8` because it
was read as a full distance (flagged in `relay-buffer-control-2switch` 4.2).

## Goals / Non-Goals

**Goals:**
- Config + serial vocabulary uses full-range semantics: `buf_sense_span_mm`
  (full switch-to-switch), `buf_max_travel_mm` (full max travel).
- Ship EMU Sync defaults verbatim: `buf_sense_span_mm = 10`, `buf_max_travel_mm = 25`.
- Internal half-based `sync.c` math stays byte-for-byte equivalent in
  behavior (only the *source* of the half value changes).
- Clean rename, no legacy key aliases (personal project, commit-to-main).

**Non-Goals:**
- No relay-law, estimator, predictive, or `sync-state-model` behavior change.
- No flash settings struct-layout version bump beyond what a field
  rename/default-change requires.
- No type-P (analog) path change; parity reasoning must hold unchanged.
- Not retuning `buf_sense_span_mm`/`buf_max_travel_mm` to this rig — `10`/`25` are the
  EMU Sync reference defaults; rig-specific calibration is separate.

## Decisions

### D1 — Adopt full-range at the boundary, convert once at ingest

`buf_sense_span_mm` is FULL switch-to-switch. Internal code wants HALF. Convert
`half = buf_sense_span_mm / 2` exactly once, where the value enters the firmware
(config ingest / SET handler), and keep the existing internal half-based
representation and call graph.

- *Why:* the relay law and virtual-position math are extensively half-based
  and already validated; reworking them to full-range is large, risky, and
  out of scope. A single ingest division isolates the vocabulary change to
  the boundary and keeps `sync.c` behavior provably unchanged.
- *Alternative (rejected):* refactor `sync.c` to full-range throughout —
  touches the relay path under active 4.2 tuning; unjustified risk for a
  vocabulary change.
- *Alternative (rejected):* keep storing half internally but expose a
  computed full-range GET only — leaves the SET/config still half; does not
  remove the ambiguity that caused the miscalibration.

### D2 — `buf_max_travel_mm` maps 1:1 to the old `buf_size_mm`

`buf_size_mm` was already FULL max travel; `buf_max_travel_mm` is a pure rename,
no unit conversion. `buf_max_travel_mm = 25` (was `22`) is a default change only.

### D3 — Names: `buf_sense_span_mm` / `buf_max_travel_mm`

Keep the terse existing `buf_` config prefix rather than the literal
`sync_feedback_buffer_range`. The *semantics* (full-range) are the Happy
Hare alignment that matters; the long literal string is not. Shorter keys
stay consistent with the rest of `config.ini` and the serial token style.
Serial tokens: `BUF_SENSE_SPAN`, `BUF_MAX_TRAVEL`.

### D4 — Clamp relationship preserved in full-range terms

Today: `buf_half_travel_mm ∈ [1.0, buf_size_mm/2]`; setting `buf_size_mm`
re-clamps half. Restated: `buf_sense_span_mm ∈ [2.0, buf_max_travel_mm]`,
`buf_max_travel_mm ∈ [10, 1000]` (old `[5,1000]` ×2 lower bound to keep
`buf_sense_span_mm ≥ 2`). Setting `buf_max_travel_mm` re-clamps `buf_sense_span_mm` so the
derived internal half never exceeds `buf_max_travel_mm/2`. Invariant after
ingest is identical to today: `1.0 ≤ half ≤ buf_max_travel_mm/2`.

### D5 — No legacy aliases

Old tokens (`BUF_HALF_TRAVEL`, `BUF_TRAVEL`, `BUF_SIZE`) and old config
keys are removed, not aliased. Single-operator personal project on
commit-to-main; a clean break is simpler than a deprecation window and
avoids the half/full ambiguity lingering behind an alias.

## Risks / Trade-offs

- **Stale `config.ini` / saved flash settings from before the rename load
  with defaults or wrong values** → On rename, bump the settings version so
  pre-rename flash blobs are rejected and EMU Sync defaults applied; ensure
  `gen_config.py` errors (not silently skips) on the unknown old keys so a
  stale `config.ini` is caught at build, not at runtime.
- **Off-by-2× error in the ingest conversion** (forgetting `/2`, or
  applying it twice) → Single conversion point (D1); add a regression note
  in `TEST_CASES.md` asserting `buf_sense_span_mm=10 ⇒ internal half=5` and the
  type-D relay trace is unchanged vs the pre-rename half=5 build.
- **GET now reports full-range while operators muscle-memory expect half**
  → Acceptable and intended; this *is* the alignment. Operator tuning-guide
  doc updated in the same change.
- **Type-P (analog) parity** → analog path also consumes
  `buf_physical_half_travel_mm()`/`buf_threshold_mm()`; since only the
  half-value *source* changes and the post-ingest invariant is identical,
  type-P behavior is unchanged. Captured as an explicit validation task.

## Migration Plan

1. Rename macros/vars/keys + add ingest `/2` conversion + EMU Sync defaults.
2. Bump settings version so pre-rename flash is not misread.
3. Update `config.ini` (+ `.example` if present), `gen_config.py` mapping,
   docs, `TEST_CASES.md`.
4. Rollback = revert commit (single milestone, commit-to-main).

## Apply Plan (2026-05-19)

### Source Inventory

- `scripts/gen_config.py`: defaults still expose `buf_half_travel_mm=7.8`
  and `buf_size_mm=22`; generated macros are `CONF_BUF_HALF_TRAVEL_MM` and
  `CONF_BUF_SIZE_MM`. The parser currently accepts unknown flat keys by
  merging `raw` into defaults, so stale pre-rename config keys would be
  silently accepted unless an explicit guard is added.
- `firmware/src/main.c` / `firmware/include/controller_shared.h`: runtime
  globals are `BUF_HALF_TRAVEL_MM` and `BUF_SIZE_MM`.
- `firmware/src/settings_store.c`: `SETTINGS_VERSION=50`; `settings_t`
  persists `buf_half_travel_mm` and `buf_size_mm`; defaults/load/save clamp
  the half value against total travel.
- `firmware/src/protocol.c`: SET accepts `BUF_HALF_TRAVEL`/`BUF_TRAVEL` and
  `BUF_SIZE`; GET emits the same legacy tokens.
- `firmware/src/sync.c` and `firmware/src/motion.c`: geometry consumers use
  the internal half value and total max travel. Math should stay unchanged
  after identifier rename.
- Docs with old keys/tokens: `MANUAL.md`, `BEHAVIOR.md`, `KLIPPER.md`,
  `config.ini.example`, `scripts/flare_cmd.py`, plus local ignored
  `config.ini` for build input.

### Edit Plan

- Boundary naming:
  - Config key `buf_sense_span_mm` is full range; generated macro
    `CONF_BUF_SENSE_SPAN_MM` remains full range.
  - Runtime variable `BUF_SENSE_SPAN_HALF_MM` stores the internal half value
    used by existing geometry math.
  - Config key/runtime max `buf_max_travel_mm` / `BUF_MAX_TRAVEL_MM` map
    one-to-one to old total travel.
- Ingest conversion:
  - Add a small full-range clamp helper at each ingest boundary
    (`settings_defaults`, `settings_load`, protocol SET): clamp full
    `buf_sense_span_mm` to `[2.0, buf_max_travel_mm]`, then assign
    `BUF_SENSE_SPAN_HALF_MM = full / 2.0f`.
  - Save/GET report full range as `BUF_SENSE_SPAN_HALF_MM * 2.0f`; no extra
    ingest conversion there.
- Persistence:
  - Rename settings fields to `buf_sense_span_mm` and `buf_max_travel_mm`.
  - Bump `SETTINGS_VERSION` from 50 to 51 so old flash blobs reset to EMU
    Sync defaults.
- Config/scripts:
  - Update defaults and `CONF_*` emission in `gen_config.py`.
  - Add unknown-key validation, allowing known defaults, per-lane `_l1/_l2`
    overrides, and `flow_schedule.v1` `point*` rows. Old keys become hard
    errors.
  - Update tracked `config.ini.example`, script dump mappings, and local
    ignored `config.ini` so build generation works.
- Validation:
  - Regenerate ignored `firmware/include/tune.h` from local `config.ini`.
  - Build, py_compile, OpenSpec validate, all-spec validate, and grep for
    legacy identifiers in tracked live surfaces (excluding this change's own
    proposal/design/spec/tasks where old terms are the required history).

## Open Questions

- Resolved during apply: `gen_config.py` used to pass unknown flat keys through.
  This change adds explicit key validation, with allowances for known defaults,
  per-lane `_l1`/`_l2` overrides, and `flow_schedule.v1` `point*` rows. Stale
  pre-rename geometry keys now fail generation as unknown config keys.
