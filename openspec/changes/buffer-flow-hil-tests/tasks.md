## 1. Harness (daemon transport)

- [x] 1.1 `scripts/flare_hil_harness.py`: `HilBoard` over daemon HTTP — `POST /cmd` send, `/telemetry` SSE reader thread, `/status` snapshot. No direct serial.
- [x] 1.2 Pure seams `parse_event` (raw `EV:` line) and `event_from_sse` (SSE dict) for matching; `_ingest` records `Event(payload, data, t)`.
- [x] 1.3 `wait_event` / `refute_event` (time-windowed, substring + regex) and raising wrappers `expect` / `refute`.
- [x] 1.4 Helpers: `prompt` (operator), `set_sensor_type`, `safe_speeds`, `stop_motion`, context-manager close (`ST:` on exit).

## 2. Runner + flow registry

- [x] 2.1 `scripts/flare_hil.py`: `Case` registry + `@case` decorator; `--flow {sync,stab,buflock,load,unload,all}`, `--type {p,d}`, `--list`, `--daemon-url`, `--quiet`.
- [x] 2.2 Per-case `_reset` (`ST:` + `SM:0` + clear events); summary + non-zero exit on failure; `KeyboardInterrupt` → `ST:`.

## 3. Flow cases

- [x] 3.1 **unload**: P tug-of-war → `UNLOAD_BLOCKED` (D22); P healthy → refute block; D parity tension-block.
- [x] 3.2 **sync**: P compression pin → `SYNC:RELIEF_PAUSE`; P tension pin → `SYNC:FAULT_HOLD`; P `AUTO_MODE` transition → `SYNC:AUTO_START`; P resting-at-home → refute `AUTO_START` (D18 gate); P tail-assist idle at compression → `SYNC:AUTO_STOP`.
- [x] 3.3 **stab**: P loaded+off-goal → `BUF_STAB:START`/`DONE` (D23); P unloaded → refute `BUF_STAB:START` (presence gate); D parity to-neutral.
- [x] 3.4 **buflock**: P `BL:T` → `BL:PRIME`/`BL:LOCKED` (D19); P follow break → `BL:FOLLOW`/`FOLLOW_DONE` (D20); P no-motion → `BL:TIMEOUT`.
- [x] 3.5 **load**: P `FL` with filament → `LOADED`; `FL` without filament → `ER:NO_FILAMENT` guard. (Load = `FL` only; preload/autoload excluded.)

## 4. Harness unit tests (CI, no hardware)

- [x] 4.1 `scripts/test_flare_hil_harness.py`: `parse_event`, `event_from_sse` (event + non-event frames).
- [x] 4.2 `wait_event` hit/substring/regex/timeout/`since`; `clear_events`; `refute` pass/fail; `expect`/`refute` raising. Injects via `_ingest`, no network.

## 5. Operator validation on rig (blocked)

- [ ] 5.1 **BLOCKER: requires PSF rig + daemon** — run `flare_hil.py --flow all --type p`; confirm every type-P case passes against real hardware. Tune bench-dependent timeouts (load, lock).
- [ ] 5.2 **BLOCKER: requires type-D rig** — run `--type d` parity cases on type-D hardware.

## 6. Closeout

- [x] 6.1 `python3 -m py_compile`, `-m doctest`, `-m unittest test_flare_hil_harness` — all pass; `flare_hil.py --list` renders the registry.
- [ ] 6.2 Add a short "HIL buffer-flow tests" section to `TEST_CASES.md` (how to start the daemon + run a flow). (Operator-doc follow-up.)

## 7. Operator UX

- [x] 7.1 `await_buffer`: live `g_buf_pos`/zone readout from `/status` while staging; auto-proceeds at the target window, ENTER forces. Buffer-position cases use it (unload tug, sync relief/fault/auto-start/auto-stop, stab loaded, buflock prime/lock/break).
- [x] 7.2 Echo muting: `< EV` / `>` echo suppressed while a prompt, staged wait, or progress countdown is on screen (events still captured). `_mute_echo` flag honored in `_ingest`/`send`.
- [x] 7.3 Progress countdown: `expect(progress=True)` shows a one-line `\r` countdown on long waits (load `LOADED`, lock acquire/follow, auto-stop) with echo muted.
- [x] 7.4 Per-case retry/skip: on failure the runner offers `[r]etry / [s]kip / ENTER=fail`; results track pass/fail/skip; `Ctrl-C` aborts the run cleanly (`ST:`).
- [x] 7.5 Unit tests for the new pure helpers (`_in_range`, `_target_str`, muted-capture).
