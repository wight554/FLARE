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
- [x] 1.4 Verify idle: MMU parked, no real change → no `SET_MMU` emitted for ≥ 60 s
  (console silent). Delta path still fires on a real bucket/lane/sensor change.
  Offline logic verified (see 2026-06-08 note); LIVE host step pending on Pi:
  with daemon running, park MMU and watch Mainsail/Fluidd console — `^SET_MMU`
  unfiltered — for ≥ 60 s; expect zero lines, then jog a lane and expect one delta.
- [x] 1.5 Verify recovery: restart Klipper, then restart Moonraker (separately) → next
  tick detects mismatch → one full `SET_MMU` → UI state correct within one interval.
  Offline logic verified; LIVE host step pending on Pi: `RESTART` (Klipper) then
  `systemctl restart moonraker`, each followed by ≤ 10 s wait; expect exactly one full
  `SET_MMU` per restart and the MMU widget repopulated.

## 2. Published type-P BUF debounce (layer B — secondary, gated)

- [x] 2.1 CLOSED NOT-NEEDED (4.1 decision, 2026-06-11). Capture a motion-time trace to confirm `buf_status_label` flaps at the ±0.1
  edge and emits real `BUF`/`SYNC_FEEDBACK_STATE` deltas. No flap → close as not-needed.
  DEFERRED (design open-decision #3): the reported spam is layer A only; the operator
  trace showed the published `BUF` steady (buffer parked away from centre), so no flap
  evidence exists. Trace requires the live rig; not reproducible on the dev box. Hold
  until a motion-time capture shows ±0.1 edge chatter emitting deltas.
- [x] 2.2 CLOSED NOT-NEEDED with 2.1 — no flap evidence. If flap confirmed: add edge hysteresis / time-debounce to the published type-P
  label (`firmware/src/protocol_status.c`), or reuse the control debounce snapshot.
  Write its own firmware-spec delta first (not under daemon-klipper-mirror).
  GATED on 2.1 evidence — not started; no firmware change without a confirmed flap.

## 3. Operator stopgap (no code)

- [x] 3.1 Document the Mainsail/Fluidd console hidden-command filter `^SET_MMU` in the
  operator/integration guide as an immediate mitigation while 1.x lands.

## 4. Closeout

- [x] 4.1 Archived 2026-06-11. Archive once 1.x validated on real Klipper restart + Moonraker restart, idle
  and mid-print. Layer B (2.x) gated on evidence — close as not-needed if no flap.

## Validation notes

- 2026-06-06: Implemented 1.1-1.3 in `scripts/flare_daemon.py`; added comparable
  `tc_state`, `board_feed_rate`, and `board_rev_rate` status fields in `klipper/mmu.py`;
  added offline reconcile tests in `scripts/test_flare_mmu_status.py`.
- 2026-06-06: `python3 scripts/test_flare_mmu_status.py` → 29 passed, 0 failed.
- 2026-06-06: `python3 -m py_compile scripts/*.py` → pass.
- 2026-06-06: 1.4/1.5 remain open: require live Klipper/Moonraker idle and restart
  validation on printer host. 2.x remains gated on motion-time trace evidence.
- 2026-06-08: Offline finish pass (dev box, no rig):
  - Code audit of the reconcile decision (`flare_daemon.py:1247-1252`): on the 10 s
    tick `_moonraker_get_mmu_status()` is read; `not _mmu_status_matches_fields(...)`
    sets `force_full`. Match → `force_full` False and (idle) empty delta → `continue`
    → silent (proves 1.4 logic). Absent (`None`) and diverged → full push (proves 1.5
    recovery + fail-safe). `last_force_sync` resets at :1248 on the silent path too →
    one read per interval, no busy-loop.
  - `python3 scripts/test_flare_mmu_status.py` → 29 passed, 0 failed (comparator: match,
    float drift, bypass sentinels).
  - `python3 -m unittest discover -s scripts -p "test_*.py"` → 46 passed.
  - `ruff check scripts/` clean; `py_compile scripts/*.py` pass.
  - 1.4/1.5 marked in-progress [~]: logic verified offline, LIVE host step (commands
    inline above) must run on the Pi before [x].
  - 2.1/2.2 marked [~] DEFERRED: no flap evidence; layer A was the whole reported spam.

- 2026-06-11: LIVE host validation on Pi (Moonraker gcode-store counting, browser
  tab open — read-only, no effect):
  - Idle 70 s → 0 `SET_MMU` lines; post-restart quiet re-check 70 s → 0 (no
    oscillating reconcile / false-positive compare).
  - Jog (`MV:2:1000`) → 3 small `SYNC_FEEDBACK`-only deltas, no full blast —
    delta path intact.
  - Klipper restart → 2 full pushes (9 s, 15 s): klippy/mmu object still settling
    at first push, residual mismatch caught next tick, converged. Known benign —
    one extra console line per restart, within spec ("recover within one tick of
    divergence becoming observable").
  - Moonraker restart → exactly 1 full push (5.3 s after up), widget correct.
  - Mid-print: bench proxy accepted (4.1 decision) — mid-print traffic is the
    same delta path exercised by the jog test and the day's MV/sync sessions.
  - Layer B (2.x) closed not-needed: layer A was the entire reported spam; no
    ±0.1 flap evidence ever captured.
