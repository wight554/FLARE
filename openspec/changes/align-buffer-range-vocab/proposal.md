## Why

Our buffer-travel tunables use bespoke half-travel naming (`buf_half_travel_mm`,
`buf_size_mm`) while Happy Hare / EMU Sync — the ecosystem operators come from —
uses full-range *semantics* (`sync_feedback_buffer_range`,
`sync_feedback_buffer_maxrange`). The mismatch caused a real calibration error:
`buf_half_travel_mm` was left at the untuned `7.8` artifact (flagged during
`relay-buffer-control-2switch` 4.2), because operators read it as a full
switch-to-switch distance, not a half. Aligning the vocabulary removes the
half/full ambiguity and lets us ship EMU Sync defaults verbatim.

## What Changes

- **BREAKING** Rename config.ini + serial SET/GET keys to Happy Hare
  full-range semantics, keeping the terse existing `buf_` prefix:
  - `buf_half_travel_mm` → `buf_sense_span_mm` (semantics flip:
    HALF → FULL switch-to-switch distance)
  - `buf_size_mm` → `buf_max_travel_mm` (already full; rename only)
  - SET/GET tokens `BUF_HALF_TRAVEL`/`BUF_TRAVEL`/`BUF_SIZE` →
    `BUF_SENSE_SPAN` / `BUF_MAX_TRAVEL`.
    No legacy aliases (personal project, commit-to-main).
- Adopt full-range vocabulary only at the config.ini + serial boundary;
  convert full → internal half once at ingest (`range / 2`). Internal
  `sync.c` half-based geometry math is untouched.
- Set defaults per EMU Sync: `buf_sense_span_mm = 10`
  (→ internal half `5`), `buf_max_travel_mm = 25`. This also
  corrects the `7.8` half artifact.
- No relay-law, estimator, or `sync-state-model` behavior changes — vocabulary
  + defaults + ingest shim only.

## Capabilities

### New Capabilities
- `buffer-geometry-vocabulary`: buffer-travel units contract with full-range
  semantics aligned to Happy Hare / EMU Sync —
  `buf_sense_span_mm` (switch-to-switch sensing span) / `buf_max_travel_mm`
  (total mechanical travel) config + serial vocabulary, full→half ingest
  conversion, EMU Sync default values, and the clamp relationship between
  sense-span and max-travel.

### Modified Capabilities
(none — the change *complies with* the existing `persistence-contract`
requirements rather than altering them: the field rename triggers the
mandated **Settings Version Bump**, and the renamed keys still satisfy the
**Runtime Tunables Flow** `config.ini` → `gen_config.py` → `CONF_*` contract.
No requirement-level behavior changes.)

## Impact

- `firmware/include/tune.h` — `CONF_BUF_HALF_TRAVEL_MM` / `CONF_BUF_SIZE_MM`
  macros + backing var names + default values.
- `config.ini` (and `config.ini.example` if present) — keys + values.
- Config/settings loader + `gen_config.py` — key-name mapping; flash
  settings-store field mapping.
- `firmware/src/protocol.c` — SET (`:634-657`) and GET (`:793`, `:811`) key
  tokens (`BUF_SENSE_SPAN`/`BUF_MAX_TRAVEL`) + clamp wiring (`buf_sense_span_mm` clamps
  against `buf_max_travel_mm`).
- `firmware/src/sync.c` — only if macro identifiers change at callsites
  (`buf_physical_half_travel_mm` / `buf_threshold_mm` and consumers); math
  unchanged.
- Docs mentioning the old keys (README / AGENTS / BUILD_FLASH /
  operator tuning guide) + `TEST_CASES.md` regression note.
- Unblocks recording the `relay-buffer-control-2switch` 4.2 known-good
  baseline (CATCHUP=1.30/NEUTRAL=1.25) under the corrected range default.
