# One-print HW validation session (temp — delete after the rig checks land)

Covers 12 of FLARE's 16 open hardware-validation items in one Type-P print +
one Fluidd/Mainsail session. Pairs with `scripts/verify_hw_open_items.py`
(log/API based — no eyeballing a UI to decide pass/fail).

Not covered here (bench-only or a design decision, not a print check):
#5 flash-wear-after-reboot, #7-trigger forced watchdog stall, #12
buffer-state-lock D2 question, #16 TMC thermal margin.

## Slicer setup

Use any model that gives you **3+ toolchanges**, **1+ full runout** near the
end, and **plenty of retract/prime cycles** in between — a multi-color test
cube or a tall multi-material vase works. Concretely:

- **Filament**: load lane 1 with a short remnant (~2–3m) so it runs out
  naturally in the back half of the print (item #6). Keep lane 2 full.
- **Layers**: tall enough (≥60 min print) that there's time to fire the
  manual triggers below between automatic events — a 100mm vase at 0.2mm/60mm/s
  is plenty.
- **Toolchanges**: slice with a multi-material/multi-color setting that
  forces at least 3 toolchanges (T0→T1→T0…) so item #8 has more than one
  chance to land.
- **`TC_TS_PARK_MM`**: leave at whatever the rig is currently configured
  with (default `0`) — item #8 checks the *current* config, not a change.
- **Retracts**: normal slicer retract settings are fine; item #10/#11 are
  fired manually on top of whatever retracts the gcode already does.

## Before starting

```bash
# terminal 1 — start the passive monitor, leave it running for the whole print
python3 scripts/verify_hw_open_items.py watch --log hw_evidence_$(date +%Y%m%d_%H%M).jsonl
```

Have a second terminal ready for the `fire` commands below, and the printer's
Fluidd/Mainsail tab open (only for item #14's visual cross-check, not as the
source of truth — the monitor's flowguard samples are).

## During the print — fire these when the described condition is true

| When | Command | Covers |
|---|---|---|
| Right as any motion starts (e.g. a toolchange begins) | `python3 scripts/verify_hw_open_items.py fire 1-cal-busy` | #1 |
| Mid-cut (watch the console/EV:CUT:FEEDING) | `python3 scripts/verify_hw_open_items.py fire 2-cp-busy` | #2 |
| Right after a toolchange starts | `python3 scripts/verify_hw_open_items.py fire 3-tc-busy` | #3 |
| Mid-cut, a *different* cut than #2 | `python3 scripts/verify_hw_open_items.py fire 9-cutter-abort` | #9 |
| On any retract | `python3 scripts/verify_hw_open_items.py fire 10-bl-bare-vs-args` | #10 |
| Once, briefly (~1s) | Unplug lane-2 TMC UART/5V, replug | #11 (monitor catches this passively) |
| Anytime, once | `python3 scripts/verify_hw_open_items.py fire 15-command-stubs` | #15 |

Items **#4, #6, #8, #13, #14** need no manual trigger — the monitor
classifies them from whatever the print naturally does (buffer lock/timeout,
the scripted runout, each toolchange, and the whole session's `sps`/flowguard
telemetry). Just let the print run.

## After the print

```bash
# stop terminal 1 with Ctrl+C — it prints the final report — or re-render it:
python3 scripts/verify_hw_open_items.py report hw_evidence_YYYYMMDD_HHMM.jsonl
```

Read the report:
- **PASS** items → check the box in the relevant `*-VALIDATION.md`/ROADMAP
  `HW:` line, citing the evidence timestamp from the JSONL log.
- **FAIL** items → don't check the box; the report names the offending event
  — open a debugging session on that one specifically.
- **PENDING** items → the condition never occurred this print; re-run that
  trigger (or the whole session) rather than marking it done.
- **#14 flowguard samples** → cross-check against what the Fluidd/Mainsail
  meter showed at the same moments, but the logged values are the record of
  truth for closing out Phase 14's two `human_needed` items.

Item **#7** (watchdog): after separately forcing a bench stall,
`python3 scripts/verify_hw_open_items.py fire watchdog-check` reads
`--crashlog`/`--loop-stats` back for you to confirm against the criteria in
ROADMAP Phase 11.
