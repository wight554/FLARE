## 1. Inventory

- [x] 1.1 Freeze the D1 taxonomy table (Sync-Feedback Sensor; HH codes
  P=1 / D=0 / TO,CO unimplemented; law named separately) at the top of
  this change as the contract.
- [x] 1.2 Exhaustive grep inventory of the home-grown umbrella terms in
  the LIVE (non-archived) set: sensor-as-wiring-shorthand, sensor-as-"relay",
  every legacy analog alias occurrence, bare `BUF_SENSOR_TYPE` references missing the
  value contract. Record the hit list; it is the zero-survivor target.

  2026-05-19: D1 frozen in `design.md`; inventory + file plan added under
  "Apply Inventory And Plan". Pre-change protocol/status snapshot hash:
  `822388ee98ff8c2f18971c183b75b2a80493209865217d5c1be64329cd279016`.

## 2. Reword in lockstep

- [x] 2.1 Live specs: `sync-state-model`, `sync-refactor`,
  `operator-tuning-guide` — apply Sync-Feedback Sensor + P/D codes,
  separate sensor from law, retire the legacy analog alias.
- [x] 2.2 `TUNING.md`: Sync-Feedback Sensor vocabulary; document
  `BUF_SENSOR_TYPE` D=0/P=1 where sensor mode appears; sensor named
  separately from the two-level/relay law; no legacy analog alias.
- [x] 2.3 `config.ini.example`: document the D=0/P=1 contract in the same
  vocabulary; TO/CO noted as recognized-but-unimplemented.
- [x] 2.4 Source comments only in `firmware/src/sync.c` /
  `firmware/include/*` that describe the sensor — reword to Sync-Feedback
  Sensor / type P or D, replace the legacy analog alias. Do NOT rename `BUF_SENSOR_TYPE`,
  enums, or any C identifier.
- [x] 2.5 Live prose in in-progress changes that describes the sensor
  (e.g. `relay-duty-estimator-and-tuning` where it uses wiring shorthand as the
  sensor) updated for consistency; folder names left historical.

  2026-05-19: Updated live specs, `TUNING.md`, `config.ini.example`,
  `AGENTS.md`, `TEST_CASES.md`, `scripts/flare_cmd.py`, `scripts/gen_config.py`,
  sensor comments in `sync.c` / `firmware/include/*`, and live prose inside
  `relay-buffer-control-2switch` plus `relay-duty-estimator-and-tuning`.

## 3. Faithful-rollout gate

- [x] 3.1 `ninja -C build_local` green; `python3 -m py_compile scripts/*.py`.
- [x] 3.2 Captured status-line + event-token snapshot identical pre/post
  (no numeric/behavioral or `BUF_SENSOR_TYPE` value delta).
- [x] 3.3 Zero-survivor check against the §1.2 inventory on the live set
  (incl. zero stray legacy analog alias text).
- [x] 3.4 `openspec validate adopt-sync-feedback-vocab --strict`.

  2026-05-19 validation:
  - `ninja -C build_local` passed.
  - `python3 -m py_compile scripts/*.py` passed.
  - Firmware status/event token snapshot matched HEAD after removing line
    numbers: `ebcd1b76a4f73cbe71bebfae8ed66803b54eddbeb952148d62eb56a364291eb1`.
  - Zero-survivor grep passed for legacy analog alias, old sensor-as-wiring
    shorthand patterns, and stale "Analog Buffer Sensor" labels in the live
    non-archived set.
  - `openspec validate adopt-sync-feedback-vocab --strict` passed.
  - `openspec validate --all --strict` passed: 25/25.
  - `git diff --check` passed.

## 4. Closeout

- [x] 4.1 Cross-link: this change realizes
  `relay-duty-estimator-and-tuning` design D9; note D9 is now realized.

  2026-05-19: `relay-duty-estimator-and-tuning/design.md` D9 now says the
  vocabulary decision is realized by `adopt-sync-feedback-vocab`.
- [x] 4.2 Commit + push to main.
