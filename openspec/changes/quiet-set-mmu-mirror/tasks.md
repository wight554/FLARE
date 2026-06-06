## 1. Silent reconcile (layer A — primary)

- [x] 1.1 Add a Moonraker read helper: GET `/printer/objects/query?mmu`, return the mmu
  object field map (or `None` on failure / absent object). Reuse the existing urllib
  pattern; 1 s timeout; no console side effect.
- [x] 1.2 Build the reconcile compare: map mmu-object values to the same formatting
  `fields` uses (floats `%.3f` / `%.2f`, quoted strings) so equal states compare equal.
  Unit-test the mapping against `cmd_SET_MMU` formatting (`scripts/test_flare_mmu_status.py`).
- [x] 1.3 Replace the blind `force_full` timer (`flare_daemon.py:1008`): on the 10 s tick,
  read + compare; set `force_full` only on mismatch or read-failure (absent object →
  fail-safe full push). Keep first-push and board-online-transition full pushes.
- [ ] 1.4 Verify idle: MMU parked, no real change → no `SET_MMU` emitted for ≥ 60 s
  (console silent). Delta path still fires on a real bucket/lane/sensor change.
- [ ] 1.5 Verify recovery: restart Klipper, then restart Moonraker (separately) → next
  tick detects mismatch → one full `SET_MMU` → UI state correct within one interval.

## 2. Published type-P BUF debounce (layer B — secondary, gated)

- [ ] 2.1 Capture a motion-time trace to confirm `buf_status_label` flaps at the ±0.1
  edge and emits real `BUF`/`SYNC_FEEDBACK_STATE` deltas. No flap → close as not-needed.
- [ ] 2.2 If flap confirmed: add edge hysteresis / time-debounce to the published type-P
  label (`firmware/src/protocol_status.c`), or reuse the control debounce snapshot.
  Write its own firmware-spec delta first (not under daemon-klipper-mirror).

## 3. Operator stopgap (no code)

- [x] 3.1 Document the Mainsail/Fluidd console hidden-command filter `^SET_MMU` in the
  operator/integration guide as an immediate mitigation while 1.x lands.

## 4. Closeout

- [ ] 4.1 Archive once 1.x validated on real Klipper restart + Moonraker restart, idle
  and mid-print. Layer B (2.x) gated on evidence — close as not-needed if no flap.

## Validation notes

- 2026-06-06: Implemented 1.1-1.3 in `scripts/flare_daemon.py`; added comparable
  `tc_state`, `board_feed_rate`, and `board_rev_rate` status fields in `klipper/mmu.py`;
  added offline reconcile tests in `scripts/test_flare_mmu_status.py`.
- 2026-06-06: `python3 scripts/test_flare_mmu_status.py` → 29 passed, 0 failed.
- 2026-06-06: `python3 -m py_compile scripts/*.py` → pass.
- 2026-06-06: 1.4/1.5 remain open: require live Klipper/Moonraker idle and restart
  validation on printer host. 2.x remains gated on motion-time trace evidence.
