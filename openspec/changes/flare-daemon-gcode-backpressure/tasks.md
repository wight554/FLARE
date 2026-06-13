## 1. Lock-free idle probe

- [x] 1.1 Verify on rig: read `objects/query {idle_timeout}` while `MPC_CALIBRATE` runs;
  confirm `idle_timeout.state` strings for busy ("Printing") and free ("Idle"/"Ready").
  Confirm objects/query returns promptly (lock-free) during the blocking command.
- [x] 1.2 Add helper `_moonraker_get_idle_state(moonraker_url)` in `scripts/flare_daemon.py`:
  POST `objects/query {"idle_timeout": null}`, return state string or None on error.
  Pure stdlib urllib, short timeout (~1.5s).
- [x] 1.3 Add module constant for free-state set (e.g. `IDLE_FREE_STATES = {"Idle","Ready"}`)
  per 1.1 finding.

## 2. Busy-mode state machine in mirror loop

- [x] 2.1 Add `host_busy` flag + `next_idle_probe` timestamp near loop init (~L1088).
- [x] 2.2 Wrap the `SET_MMU` push (~L1340): on `urlopen` timeout/exception, classify via
  `_moonraker_get_idle_state`: state in busy → set `host_busy=True`; None (unreachable) →
  existing offline backoff; free → transient, no state change.
- [x] 2.3 At loop top: if `host_busy`, skip all gcode/script emit; throttle-poll idle state
  (respect `next_idle_probe`, cadence ~1-2s). Free → clear `host_busy`,
  `last_pushed_fields = {}` (force full resync), resume.
- [x] 2.4 Gate `_FLARE_SYNC_BOARD` emit on `not host_busy`.

## 3. Gate-map push channel

- [x] 3.1 Make `_push_gate_map_to_klipper` (~L264) respect `host_busy` (skip emit while busy)
  — pass/share the flag or guard at call site.

## 4. Validation

- [ ] 4.1 Rig repro: stop nothing, run `MPC_CALIBRATE` with patched daemon. Confirm no
  `Request 'gcode/script' pending: N seconds` accumulation in moonraker log and no Kalico
  `RecursionError` crash.
- [ ] 4.2 Confirm mirror resumes + full resync after calibration completes (mmu object
  matches desired fields within one tick of Idle).
- [ ] 4.3 Confirm offline path unchanged: kill Moonraker briefly, verify 5s backoff + full
  resync on recovery (no busy-probe misclassification).
- [x] 4.4 Run host-tool static checks / existing daemon tests; `git grep` no work project
  names in diff before commit.

## 5. Docs

- [x] 5.1 Note in operator guide: mirror telemetry freezes during long blocking commands
  (calibration), reconciles on completion — expected, not a fault.
