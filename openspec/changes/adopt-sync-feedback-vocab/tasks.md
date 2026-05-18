## 1. Inventory

- [ ] 1.1 Freeze the D1 taxonomy table (SFS / PSF=1 / DSF=0; law named
  separately) at the top of this change as the contract.
- [ ] 1.2 Exhaustive grep inventory of the home-grown umbrella terms in
  the LIVE (non-archived) set: sensor-as-"2-switch", sensor-as-"relay",
  bare `BUF_SENSOR_TYPE` references missing the value contract. Record the
  hit list; it is the zero-survivor gate target.

## 2. Reword in lockstep

- [ ] 2.1 Live specs: `sync-state-model`, `sync-refactor`,
  `operator-tuning-guide` — apply SFS/DSF/PSF, separate sensor from law.
- [ ] 2.2 `TUNING.md`: SFS vocabulary; document `BUF_SENSOR_TYPE`
  DSF=0/PSF=1 where sensor mode appears; sensor named separately from the
  two-level/relay law.
- [ ] 2.3 `config.ini.example`: document the DSF=0/PSF=1 contract in the
  same vocabulary.
- [ ] 2.4 Source comments only in `firmware/src/sync.c` /
  `firmware/include/*` that describe the sensor — reword to SFS/DSF/PSF.
  Do NOT rename `BUF_SENSOR_TYPE`, enums, or any C identifier.
- [ ] 2.5 Live prose in in-progress changes that describes the sensor
  (e.g. `relay-duty-estimator-and-tuning` where it says "2-switch" as the
  sensor) updated for consistency; folder names left historical.

## 3. Faithful-rollout gate

- [ ] 3.1 `ninja -C build_local` green; `python3 -m py_compile scripts/*.py`.
- [ ] 3.2 Captured status-line + event-token snapshot identical pre/post
  (no numeric/behavioral or `BUF_SENSOR_TYPE` value delta).
- [ ] 3.3 Zero-survivor check against the §1.2 inventory on the live set.
- [ ] 3.4 `openspec validate adopt-sync-feedback-vocab --strict`.

## 4. Closeout

- [ ] 4.1 Cross-link: this change implements
  `relay-duty-estimator-and-tuning` design D9; note D9 is now realized.
- [ ] 4.2 Commit + push to main.
