## 1. P0 — freeze contract

- [x] 1.1 Lock the D1 mapping table as the single source of truth
  (TENSION/COMPRESSION/NEUTRAL, signs unchanged)
  - 2026-05-18: D1 kept as the implementation contract; analog center
    runtime value renamed to `BUF_ANALOG_NEUTRAL` to leave `BUF_NEUTRAL`
    available for the buffer state enum.

## 2. P1 — code rename (behavior-preserving)

- [x] 2.1 Rename enum `BUF_ADVANCE/TRAILING/MID →
  BUF_TENSION/COMPRESSION/NEUTRAL`; fix all compiler-flagged sites
- [x] 2.2 Rename derived identifiers: `*advance*→*tension*`,
  `*trailing*→*compression*`, state `*mid*→*neutral*` (incl.
  `mid_creep_*`, `sync_overshoot_mid_extend`, `SYNC_RELAY_MID_FRAC`,
  `sync_mid_anti_advance_*`, `PIN_BUF_ADVANCE`)
- [x] 2.3 Rename wire tokens + old-state-derived short field keys
  (`AD`,`TD`,`APX`,…) + config keys + update parsing scripts
  (`flare_cmd.py`, `gen_config.py`, analyzers/tuner) in lockstep. Legacy
  config keys left to be ignored by existing unknown-key handling (no new
  error path)
- [x] 2.4 Grep inventory: `ADVANCE|TRAILING|advance|trailing` and
  buffer-state-derived `mid` return zero matches in firmware/scripts;
  unrelated arithmetic `mid`/`midpoint` explicitly excluded (deliverable)
  - 2026-05-18: Firmware, host scripts, generated config surface, and
    status/event field keys renamed. Grep gate passed for legacy
    `ADVANCE|TRAILING|advance|trailing` plus scoped old key names in
    firmware/src, firmware/include, scripts, docs, config example, and
    live specs.

## 3. P2 — protocol/docs lockstep

- [x] 3.1 `protocol.c` emit tokens renamed; `TEST_CASES.md` status
  snapshots updated to new tokens
- [x] 3.2 Docs reworded to new vocabulary: `BEHAVIOR.md`, `HARDWARE.md`,
  `CONTEXT.md`, `MANUAL.md`, `KLIPPER.md` (no migration guide — active dev)
  - 2026-05-18: Status fields now use `TPR`, `TT`, `CT`, `CW`, `TPX`,
    `CB`, and `NC`; `BUF_ANALOG_NEUTRAL` owns the analog center tunable.

## 4. P3 — live specs

- [x] 4.1 Reword live specs: `sync-state-model`, `sync-refactor`,
  `toolchange-orchestration`, `motion-safety`, `live-tuner`
- [x] 4.2 Confirm archived changes carry no live contract reference
  (leave historical)
  - 2026-05-18: Live specs updated. Archived change directories left
    untouched as historical records and excluded from the live grep gate.

## 5. Gate + closeout

- [x] 5.1 Host `cmake --build build_local`; `py_compile scripts/*.py`
- [x] 5.2 D4 behavior snapshot identical pre/post (numeric parity, only
  token spellings differ)
- [x] 5.3 `openspec validate rename-buffer-states-tension-compression
  --strict`
  - 2026-05-18: `ninja -C build_local`, `python3 -m py_compile
    scripts/*.py`, and `openspec validate
    rename-buffer-states-tension-compression --strict` passed. Source
    snapshot review found status/config/control expressions preserved with
    renamed identifiers and tokens only. `python3 -m pytest scripts/test_*.py`
    could not run because `pytest` is not installed in this Python.
- [ ] 5.4 Commit + push to main (rename only, no logic change)
- [x] 5.5 Hand off to `audit-sync-polarity`
  - 2026-05-18: Next change remains `audit-sync-polarity`; this change
    deliberately avoided polarity/control behavior edits.
