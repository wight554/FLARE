## 1. Firmware rename + ingest conversion

- [ ] 1.1 `firmware/include/tune.h`: rename `CONF_BUF_HALF_TRAVEL_MM` →
  `CONF_BUF_SENSE_SPAN_MM` (default `10`), `CONF_BUF_SIZE_MM` →
  `CONF_BUF_MAX_TRAVEL_MM` (default `25`); rename backing vars/macros.
- [ ] 1.2 Add single full→half ingest conversion `half = buf_sense_span_mm / 2`
  at the value-ingest boundary; keep internal half-based representation.
  Verify it is applied exactly once (no double `/2`).
- [ ] 1.3 `firmware/src/sync.c`: update only the macro/var identifiers at
  callsites (`buf_physical_half_travel_mm`, `buf_threshold_mm`, consumers
  `:305-372`, `:427`, `:1286`); change NO formula or call graph.
- [ ] 1.4 `firmware/src/protocol.c`: SET (`:634-657`) + GET (`:793`, `:811`)
  tokens → `BUF_SENSE_SPAN` / `BUF_MAX_TRAVEL`, full-range values; remove
  `BUF_HALF_TRAVEL` / `BUF_TRAVEL` / `BUF_SIZE` (no aliases).
- [ ] 1.5 Implement clamp relationship: `buf_sense_span_mm ∈
  [2.0, buf_max_travel_mm]`, `buf_max_travel_mm ∈ [10, 1000]`; setting
  `buf_max_travel_mm` re-clamps `buf_sense_span_mm` so internal
  `half ≤ buf_max_travel_mm/2`.

## 2. Config + persistence flow

- [ ] 2.1 `config.ini` (and `config.ini.example` if present): replace
  `buf_half_travel_mm: 7.8` → `buf_sense_span_mm: 10`,
  `buf_size_mm: 22` → `buf_max_travel_mm: 25`.
- [ ] 2.2 Update `gen_config.py` / config loader key mapping; make unknown
  legacy keys a hard error (resolve design Open Question — add guard if it
  currently passes silently).
- [ ] 2.3 Update flash settings-store field mapping; bump `SETTINGS_VERSION`
  so pre-rename flash blobs are wiped to the new EMU Sync defaults.

## 3. Docs + regression

- [ ] 3.1 Update docs referencing old keys (README / AGENTS / BUILD_FLASH /
  operator tuning guide) to `buf_sense_span_mm` / `buf_max_travel_mm`.
- [ ] 3.2 `TEST_CASES.md`: add regression entry — `buf_sense_span_mm=10 ⇒
  internal half=5`, type-D relay trace unchanged vs pre-rename half=5 build.

## 4. Validation

- [ ] 4.1 `cmake --build build_local`
- [ ] 4.2 `python3 -m py_compile scripts/*.py`
- [ ] 4.3 `openspec validate align-buffer-range-vocab --strict`
- [ ] 4.4 Type-P parity reasoning: only the half-value *source* changes,
  post-ingest invariant `1.0 ≤ half ≤ buf_max_travel_mm/2` identical →
  analog (`BUF_SENSOR_TYPE != 0`) behavior unchanged for equal geometry.
- [ ] 4.5 Confirm no `buf_half_travel_mm` / `buf_size_mm` /
  `CONF_BUF_HALF_TRAVEL_MM` / `CONF_BUF_SIZE_MM` / `BUF_HALF_TRAVEL` /
  `BUF_TRAVEL` / `BUF_SIZE` identifier remains (grep firmware + scripts +
  config + docs).

## 5. Closeout

- [ ] 5.1 Commit + push to main (single milestone).
- [ ] 5.2 Hand back to `relay-buffer-control-2switch` 4.2: record the
  known-good baseline (CATCHUP=1.30/NEUTRAL=1.25) under the corrected
  `buf_sense_span_mm` default.
