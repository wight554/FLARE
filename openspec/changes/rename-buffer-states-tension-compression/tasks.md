## 1. P0 — freeze contract

- [ ] 1.1 Lock the D1 mapping table as the single source of truth
  (TENSION/COMPRESSION/NEUTRAL, signs unchanged)

## 2. P1 — code rename (behavior-preserving)

- [ ] 2.1 Rename enum `BUF_ADVANCE/TRAILING/MID →
  BUF_TENSION/COMPRESSION/NEUTRAL`; fix all compiler-flagged sites
- [ ] 2.2 Rename derived identifiers: `*advance*→*tension*`,
  `*trailing*→*compression*`, state `*mid*→*neutral*` (incl.
  `mid_creep_*`, `sync_overshoot_mid_extend`, `SYNC_RELAY_MID_FRAC`,
  `sync_mid_anti_advance_*`, `PIN_BUF_ADVANCE`)
- [ ] 2.3 Rename wire tokens + config keys + update parsing scripts
  (`flare_cmd.py`, `gen_config.py`, analyzers/tuner) in lockstep
- [ ] 2.4 Grep inventory: `ADVANCE|TRAILING|\bMID\b|advance|trailing`
  returns zero matches in firmware/scripts (deliverable)

## 3. P2 — protocol/docs lockstep

- [ ] 3.1 `protocol.c` emit tokens renamed; `TEST_CASES.md` status
  snapshots updated to new tokens
- [ ] 3.2 Docs reworded: `BEHAVIOR.md`, `HARDWARE.md`, `CONTEXT.md`,
  `MANUAL.md`, `KLIPPER.md` (+ config key migration note in `MANUAL.md`)

## 4. P3 — live specs

- [ ] 4.1 Reword live specs: `sync-state-model`, `sync-refactor`,
  `toolchange-orchestration`, `motion-safety`, `live-tuner`
- [ ] 4.2 Confirm archived changes carry no live contract reference
  (leave historical)

## 5. Gate + closeout

- [ ] 5.1 Host `cmake --build build_local`; `py_compile scripts/*.py`
- [ ] 5.2 D4 behavior snapshot identical pre/post (numeric parity, only
  token spellings differ)
- [ ] 5.3 `openspec validate rename-buffer-states-tension-compression
  --strict`
- [ ] 5.4 Commit + push to main (rename only, no logic change)
- [ ] 5.5 Hand off to `audit-sync-polarity`
