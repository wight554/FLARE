# Manual HW validation session (temp — delete after the rig checks land)

Covers 12 of FLARE's 16 open hardware-validation items. Pairs with
`scripts/verify_hw_open_items.py` (log/API based — no eyeballing a UI to
decide pass/fail).

**Most of this needs no print at all.** Every item's precondition was
checked directly against `firmware/src/protocol.c`/`cutter.c`/`sync.c`:
8 items are pure bench commands to `flare_cmd`/the daemon with filament
just *loaded* (not printing), 2 need Klipper connected with a manual
extrude/retract line (still no queued print job), and only 2 (#13, #14)
genuinely benefit from a real print's motion profile — see
[Cases that need a real print](#cases-that-need-a-real-print) below.

Not covered here at all (bench-only bigger setup, or a design decision,
not a pass/fail check): #5 flash-wear-after-reboot, #7-trigger forced
watchdog stall, #12 buffer-state-lock D2 question, #16 TMC thermal margin.

## Before starting

```bash
# leave this running in a second terminal for the whole session —
# it passively classifies #4, #6, #8, #11 as they happen and logs everything
python3 scripts/verify_hw_open_items.py watch --log hw_evidence_$(date +%Y%m%d_%H%M).jsonl
```

Precondition for most of the bench items below: **filament loaded in both
lanes** (`lane_in_present` true on the active lane, `lane_out_present` clear
on the other) — you don't need to be printing, just loaded and idle.

## Bench items — no Klipper motion needed, script self-triggers everything

These four fire a firmware command (`CU`/`TC:`) themselves immediately
before the check, so there's no "catch the moment" problem — the busy
window is hundreds of ms to seconds (servo-settle alone is 500ms), which is
trivial for the script to land in but impossible to time by hand. Just run
each command; no manual coordination required.

| Item | Command | What it proves |
|---|---|---|
| #1 | `python3 scripts/verify_hw_open_items.py fire 1-cal-busy` | `CAL` during a self-triggered cut → `ER:PERSIST_BUSY`, no flash corruption |
| #2 | `python3 scripts/verify_hw_open_items.py fire 2-cp-busy` | `CP` during that same self-triggered cut → `ER:BUSY:CUTTER`, cut still completes |
| #9 | `python3 scripts/verify_hw_open_items.py fire 9-cutter-abort` | `STOP` during a self-triggered cut → `EV:CUT:ERROR:ABORTED`, blade returns to block |
| #3 | `python3 scripts/verify_hw_open_items.py fire 3-tc-busy --tc-other-lane 2 --tc-lane 1` | starts a toolchange to lane 2, immediately requests lane 1 → `ER:BUSY:TC` (adjust lane numbers to whichever pair you actually have loaded) |

## Bench items — no filament motion, no Klipper needed at all

| Item | Command | What it proves |
|---|---|---|
| #4 | `python3 scripts/flare_cmd.py BL:T` — then just **don't** send `BS`, wait ~30s | `EV:BL:TIMEOUT` fires once, not repeated (the `watch` monitor classifies this automatically) |
| #10 | `python3 scripts/verify_hw_open_items.py fire 10-bl-bare-vs-args` | bare `BL:T` no-ops (no `BREAK`/`FOLLOW`); `BL:T:20:300` self-triggers a real 20mm retract via Moonraker (`G1 E-20 F1500` + `M400`) and expects `BREAK`→`FOLLOW`→`FOLLOW_DONE` (needs `--moonraker-url` reachable and Klipper idle enough to accept the retract) |
| #11 | unplug lane-2 TMC UART/5V for ~1s, replug — printer can be fully idle | exactly one `EV:TMC:FAULT:2:COMM_FAIL`, no storm, then `EV:TMC:RESTORED:2` on replug (monitor classifies this passively) |
| #15 | `python3 scripts/verify_hw_open_items.py fire 15-command-stubs` | the 7 true no-op stubs register (no "Unknown command"), Klipper connected but printer idle is fine |

`MMU_PRINT_START`/`MMU_PRINT_END` are excluded from that command by default — they're not no-ops, they run the real `_FLARE_SYNC_TOOLHEAD` macro (synchronous `BS`) and can block Klipper's gcode queue for minutes if fired while the buffer isn't already settled (this is what caused the "Moonraker hanging" `Request 'gcode/script' pending: N seconds` symptom during initial bench testing). Only add `--include-print-sync` once `watch`'s live output shows sync idle and the last `BUF_STAB:DONE` has already landed; if it still hangs, `FIRMWARE_RESTART` clears the stuck queue.

## Bench items — need Klipper connected + one manual gcode line (no queued print)

| Item | Steps | What it proves |
|---|---|---|
| #6 | Load a short remnant (~30cm) on the active lane. In the Klipper console: `G1 E300 F300` (or repeat shorter extrudes) until it runs past the sensor. | Clean `EV:RUNOUT` → `EV:RELOAD:LOADED` escalation, no race stall (monitor classifies automatically) |
| #8 | `python3 scripts/flare_cmd.py TC:2` (or whichever lane), wait for `EV:TC:DONE`, then in the Klipper console: `G1 E20 F300` | No `FAULT:MOVE_COMPRESSION` during the toolchange; `EV:SYNC:AUTO_START` fires on that next extrusion (monitor classifies automatically) |

## After the bench session

```bash
# stop the watch terminal with Ctrl+C — it prints the final report — or re-render it:
python3 scripts/verify_hw_open_items.py report hw_evidence_YYYYMMDD_HHMM.jsonl
```

Read the report:
- **PASS** → check the box in the relevant `*-VALIDATION.md`/ROADMAP `HW:`
  line, citing the evidence timestamp from the JSONL log.
- **FAIL** → don't check the box; the report names the offending event —
  open a debugging session on that one specifically.
- **PENDING** → the condition never occurred; re-run that trigger.

Item **#7** (watchdog): after separately forcing a bench stall,
`python3 scripts/verify_hw_open_items.py fire watchdog-check` reads
`--crashlog`/`--loop-stats` back for you to confirm against ROADMAP Phase 11.

## Cases that need a real print

Everything above is a synthetic, single-shot trigger. #13 and #14 are
different in kind: they're not "did event X fire," they're "does this
*trend* look right under realistic, continuously-varying motion" — and a
few manual `G1 E...` lines in a console produce a flat, uniform demand
signal that won't reproduce the speed/direction changes, retraction
timing, and oozing-driven micro-adjustments a slicer-generated print
actually creates. These are the only two items where the bench shortcut
would give you a misleadingly clean result.

**Print setup**: any model that runs ≥20 minutes at varied speeds with a
handful of retracts/direction changes — a tall vase or a multi-perimeter
test cube both work. No runout, no toolchange forcing needed here (those
are covered above); just let it print normally with the `watch` monitor
attached the whole time:

```bash
python3 scripts/verify_hw_open_items.py watch --log hw_evidence_realprint.jsonl
```

| Item | What to capture | How to judge it |
|---|---|---|
| #13 (Phase 13 baseline) | `g_sync_current_sps` snapping to `target_sps`/`max_sps` at tension extremes during ordinary printing, and the magnitude of the known ±0.8 buffer hunting oscillation | No pass/fail — this is data collection to unblock Phase 13 planning (the `typep-feed-hunting` decision). Review the JSONL log's `sps`/event timeline after the print. |
| #14 (FlowGuard trend) | `printer.mmu.flowguard.level` moving toward `+1.0`/`-1.0` as compression/tension dwell approaches its trip threshold during real sustained printing, and returning to `0.0` promptly when sync goes inactive | `report`'s flowguard min/max/last samples should show that swing; cross-check against the Fluidd/Mainsail meter at the same timestamps if you want the visual confirmation too — but the logged samples, not the widget, are what closes Phase 14's `human_needed` items. |

If you don't want to dedicate a full print just for this, the bench items
above still cover 12/16 items cleanly — #13/#14 can wait for a print you're
already running for other reasons; just have `watch` attached when you do.
