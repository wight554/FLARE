## 0. Prerequisite

- [ ] 0.1 `rename-buffer-states-tension-compression` landed (vocabulary in
  place; this audit reads the corrected names)

## 1. Audit pass

- [ ] 1.1 Site #1 pin→state decode: verify pressed tension switch ⇒
  `BUF_TENSION` (origin of historical misnaming)
- [ ] 1.2 Relay path: verify `8f54bff`/`1d9ebe5` read correctly in new
  vocab (TENSION→catchup, COMPRESSION→stop, NEUTRAL→demand)
- [ ] 1.3 PSF/analog path: `RT` sign, `RE`, `compression_floor`,
  `compression_recovery`/collapse, `neutral_anti_tension`,
  estimator-at-crossing, fast-brake, AUTO_START gate, `BP/BPV` sign
- [ ] 1.4 Fill the findings table (site → classification → evidence) in
  this change

## 2. Fixes

- [ ] 2.1 Each `⚠ inverted` site: one isolated, justified commit citing
  the finding
- [ ] 2.2 `❓ needs-hardware`: document + gate on analog rig (no blind fix)

## 3. Validation

- [ ] 3.1 Relay regression on the Pi (`flare_cmd "?:" --poll 500`) per fix
- [ ] 3.2 Analog items recorded `pending-analog-rig` in `TEST_CASES.md`
- [ ] 3.3 `openspec validate audit-sync-polarity --strict`
- [ ] 3.4 Commit + push to main
