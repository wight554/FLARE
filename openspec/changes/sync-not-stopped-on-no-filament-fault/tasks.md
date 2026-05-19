## 1. Firmware Fix

- [x] 1.1 Add `sync_disable(true)` before `return` in the NO_FILAMENT guard in `reload_trigger()` (`firmware/src/toolchange.c` ~line 89)
  - 2026-05-20: Added hard sync disable before `RELOAD:FAULT NO_FILAMENT` event.

## 2. Build & Validate

- [x] 2.1 Build firmware: `ninja -C build_local`
  - 2026-05-20: Build passed.
- [ ] 2.2 On-hardware: trigger runout with both lanes empty; verify `RELOAD:FAULT NO_FILAMENT` event emitted and `?:` shows `SM:0` without manual `ST`

  **Context 2026-05-20** (from live status):
  - Board is flashed with the fix (`sync_disable(true)` before fault).
  - Current state: `I1:1` (lane 1 has filament), `I2:0` (lane 2 empty),
    `reload_mode: 1`, `auto_mode: True`. Need both IN sensors clear.

  **How to trigger for next agent:**

  Prerequisites: filament removed from BOTH lanes (I1:0 AND I2:0 in
  `?:` output). With `reload_mode: 1`, RELOAD arms on print start.

  Monitor serial events in one terminal:
  ```bash
  python3 scripts/flare_cmd.py --port /dev/ttyACM0 --monitor
  ```
  Trigger runout by starting a print with both lanes empty — Klipper
  will call the RELOAD macro, which fires `RELOAD:START` and
  immediately faults on no filament. OR simulate directly:
  ```bash
  python3 scripts/flare_cmd.py --port /dev/ttyACM0 "RL:"
  ```
  Watch for the event line: `EV:RELOAD:FAULT NO_FILAMENT`

  After fault, check sync stopped automatically:
  ```bash
  python3 scripts/flare_cmd.py --port /dev/ttyACM0 "?:"
  # pass: SM:0  (sync disabled by fault, no manual ST: needed)
  # fail: SM:1  (bug — sync still running after no-filament fault)
  ```
  Also confirm `ST:0` (stopped) and no active motor motion on either
  lane (`L1T:IDLE`, `L2T:IDLE`).
