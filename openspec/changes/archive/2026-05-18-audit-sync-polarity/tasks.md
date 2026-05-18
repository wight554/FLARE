## 0. Prerequisite

- [x] 0.1 `rename-buffer-states-tension-compression` landed (vocabulary in
  place; this audit reads the corrected names)

  2026-05-18: prerequisite landed as `83989fb`; OpenSpec task closeout landed
  as `eee9a47`.

## 1. Audit pass

- [x] 1.1 Site #1 pin→state decode: verify pressed tension switch ⇒
  `BUF_TENSION` (origin of historical misnaming)

  2026-05-18: `buf_state_raw()` maps `g_buf_tension_din` to `BUF_TENSION`
  and `g_buf_compression_din` to `BUF_COMPRESSION`; analog sign follows
  `BP > BUF_THR` ⇒ `BUF_TENSION`, `BP < -BUF_THR` ⇒ `BUF_COMPRESSION`.

- [x] 1.2 Relay path: verify `8f54bff`/`1d9ebe5` read correctly in new
  vocab (TENSION→catchup, COMPRESSION→stop, NEUTRAL→demand)

  2026-05-18: relay override remains consistent in `sync_tick()`:
  `BUF_TENSION` uses baseline catch-up refill, `BUF_COMPRESSION` clamps to
  `SYNC_MIN_SPS`, and `BUF_NEUTRAL` tracks demand. No relay inversion found.

- [x] 1.3 PSF/analog path: `RT` sign, `RE`, `compression_floor`,
  `compression_recovery`/collapse, `neutral_anti_tension`,
  estimator-at-crossing, fast-brake, AUTO_START gate, `BP/BPV` sign

  2026-05-18: audited analog/PSF sign and estimator paths. `RT`, `RE`,
  fast-brake, neutral anti-tension floor, estimator crossing, AUTO_START, and
  telemetry signs are correct. `compression_floor` and
  `compression_recovery`/collapse remain `pending-analog-rig`; no blind
  behavior change.

- [x] 1.4 Fill the findings table (site → classification → evidence) in
  this change

  2026-05-18: findings table added to `design.md`, covering sites #1-#13.

## 2. Fixes

- [x] 2.1 Each relay `⚠ inverted` site: one isolated, justified commit
  citing the finding; retested on the Pi

  2026-05-18: N/A. No relay `⚠ inverted` site found. Corrected two stale
  comments that described TENSION/COMPRESSION backwards; no relay behavior
  commit or Pi retest required.

- [x] 2.2 Analog/PSF (no rig): per site, either model on Happy Hare
  `mmu_sync_controller.py3` (a) or basic-spec-only `pending-analog-rig`
  (b). No blind analog fix

  2026-05-18: selected basic-spec-only `pending-analog-rig` for
  `compression_floor` and `compression_recovery`/collapse. Added concrete
  test records to `TEST_CASES.md`.

## 3. Validation

- [x] 3.1 Relay regression on the Pi (`flare_cmd "?:" --poll 500`) per fix

  2026-05-18: N/A. Audit found no relay behavior fix to validate on Pi.
  Firmware build validation covers the comment-only source edit.

- [x] 3.2 Analog items recorded `pending-analog-rig` in `TEST_CASES.md`

  2026-05-18: added `Analog compression floor polarity` and `Analog
  compression recovery and collapse`, both status `pending-analog-rig`.

- [x] 3.3 `openspec validate audit-sync-polarity --strict`

  2026-05-18: passed. `ninja -C build_local` also passed because
  `firmware/src/sync.c` changed.

- [x] 3.4 Commit + push to main

  2026-05-18: audit commit pushed to main as `630b500`.
