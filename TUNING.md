# FLARE Tuning Guide

This guide gets a printer from "it moves filament" to "sync defaults are backed
by real print data." It assumes you can build and flash FLARE, but it does not
assume firmware knowledge.

## TL;DR: Simplest Path

Use this when the defaults already print without scary behavior and you just
want a first real tuning pass. If behavior **is** scary (repeated
`FAULT_HOLD`, `cannot_refill`/`cannot_relieve`, jams, stalls, or the buffer
stuck pinned), do **not** start here — go to
[If Behavior Is Scary](#if-behavior-is-scary-do-this-first) first.

```bash
python3 -m pip install pyserial

export FLARE_PORT=/dev/ttyACM0
export MACHINE_ID=myprinter
export KLIPPER_UDS=/home/pi/printer_data/comms/klippy.sock

mkdir -p ~/flare-runs ~/flare-state
cp ~/flare-state/buckets-${MACHINE_ID}.json \
  ~/flare-state/buckets-${MACHINE_ID}.json.$(date +%Y%m%d-%H%M%S).bak 2>/dev/null || true
```

Prepare and capture the slow profile:

```bash
python3 scripts/gcode_marker.py slow.gcode --output slow.flare.gcode --emit sidecar

python3 scripts/flare_live_tuner.py --port "$FLARE_PORT" \
  --machine-id "$MACHINE_ID" \
  --observe-daemon \
  --csv-out ~/flare-runs/slow.csv \
  --klipper-uds "$KLIPPER_UDS" \
  --sidecar slow.flare.json
```

Print `slow.flare.gcode`. Stop the tuner with Ctrl-C after the print finishes.
Then repeat for the fast profile:

```bash
python3 scripts/gcode_marker.py fast.gcode --output fast.flare.gcode --emit sidecar

python3 scripts/flare_live_tuner.py --port "$FLARE_PORT" \
  --machine-id "$MACHINE_ID" \
  --observe-daemon \
  --csv-out ~/flare-runs/fast.csv \
  --klipper-uds "$KLIPPER_UDS" \
  --sidecar fast.flare.json
```

Print `fast.flare.gcode`, stop the tuner, then emit the schedule:

```bash
python3 scripts/flare_analyze.py \
  --profile-fast ~/flare-runs/fast.csv \
  --profile-slow ~/flare-runs/slow.csv \
  --emit-flow-schedule \
  --out flow-schedule.ini
```

Review `flow-schedule.ini`, copy `flow_schedule_cap` and `[flow_schedule.v1]`
into `config.ini`, then rebuild and flash:

```bash
python3 scripts/gen_config.py
ninja -C build_local
bash scripts/flash_flare.sh
```

Check status:

```bash
python3 scripts/flare_cmd.py --port "$FLARE_PORT" "?:"
```

## What Tuning Does

FLARE sync tries to keep a little filament reserve in the buffer while the
printer pulls material at changing speeds.

- `baseline_rate` is the starting sync feed rate when no flow schedule is
  configured.
- `sync_compression_bias_frac` shifts the target slightly toward the compression side
  so the buffer keeps useful reserve.
- `[flow_schedule.v1]` lets those two values change with live estimated flow.
- `flow_schedule_cap` is the maximum number of points (`pointN` rows) the
  schedule may have. It is a fixed firmware table-size limit: valid 1 to 16,
  default 8. More points = a finer flow curve; the limit keeps the math light
  on the controller. The analyzer always reduces its output to at most this
  many points (the same inputs always reduce the same way). `flow_schedule_cap:
  1` is just the scalar `baseline_rate` / `sync_compression_bias_frac` expressed
  as a one-point schedule.

Good tuning looks boring: the buffer moves, status fields change smoothly, sync
does not pin at the tension side for long, and prints do not show repeated
runout-looking sync faults.

Bad tuning is noisy or one-sided: the buffer stays pinned, `FAULT_HOLD` appears
often, `cannot_refill` or `cannot_relieve` repeats, or different prints produce
wildly different recommendations.

## If Behavior Is Scary (Do This First)

Scary means: repeated `FAULT_HOLD`, repeated `cannot_refill` or
`cannot_relieve`, filament jams, extruder stalls, or the buffer stuck pinned to
one side. **Do not capture or tune in this state.** Tuning data from a
misbehaving setup is garbage, the analyzer will reject it (FAIL), and these
events almost always mean a *physical* problem, not a number to change.

1. **Stop and revert to the known-safe scalar config.** In `config.ini` remove
   any `[flow_schedule.v1]` block and set the shipped defaults:

   ```ini
   baseline_rate: 1600
   sync_compression_bias_frac: 0.4
   flow_schedule_cap: 8
   ```

   (These are the `config.ini.example` defaults. With no schedule block they
   act as a safe one-point schedule.) Then rebuild and flash:

   ```bash
   python3 scripts/gen_config.py
   ninja -C build_local
   bash scripts/flash_flare.sh
   ```

2. **Confirm boring behavior** with a short normal print and the status check
   from [Verify After Flash](#verify-after-flash). The buffer should move and
   settle; `FAULT_HOLD` / `cannot_*` should not repeat.

3. **If it is still scary, it is mechanical, not tuning.** Check, in order:
   filament path and spool drag, the buffer switch and arm, the cutter, and the
   extruder for a jam or under-extrusion. `cannot_refill` points to the supply
   side (cannot feed in); `cannot_relieve` points to the buffer staying
   over-full (cannot push out). Fix the hardware, then re-run step 2.

4. **Only once behavior is boring**, continue with the two-profile pass below.

## Prerequisites

Install pyserial on the machine connected to FLARE:

```bash
python3 -m pip install pyserial
```

Find the FLARE serial port:

```bash
ls /dev/ttyACM*
export FLARE_PORT=/dev/ttyACM0
python3 scripts/flare_cmd.py --port "$FLARE_PORT" "VR:" "?:"
```

Find the Klipper API socket if you use Klipper:

```bash
ps -ef | grep '[k]lippy.py'
export KLIPPER_UDS=/home/pi/printer_data/comms/klippy.sock
```

Create run/state directories and back up any existing state:

```bash
export MACHINE_ID=myprinter
mkdir -p ~/flare-runs ~/flare-state
cp ~/flare-state/buckets-${MACHINE_ID}.json \
  ~/flare-state/buckets-${MACHINE_ID}.json.$(date +%Y%m%d-%H%M%S).bak 2>/dev/null || true
```

Git/build workflow is intentionally not copied here. For branch and commit
rules, see [WORKFLOW.md](./WORKFLOW.md).

## Pick The Two Profiles

Use the same model twice:

- Slow profile: your lowest cubic-flow case that still represents real use.
- Fast profile: your highest cubic-flow case that still prints reliably.

Keep material, nozzle, buffer hardware, lane path, and model geometry the same.
Only change slicer speed/flow settings. A practical example:

- Slow: outer-wall-heavy profile, 0.20 mm layers, conservative speed.
- Fast: same model, same layer height, high infill/wall speed near your normal
  upper flow.

The analyzer uses both captures to create one deterministic result. Same input
files produce the same output.

## Capture Live Data

This is the supported and recommended path for live capture. It injects
nothing into the printed G-code and has zero print-time overhead.

Generate marked G-code and a sidecar:

```bash
python3 scripts/gcode_marker.py input.gcode --output input.flare.gcode \
  --emit sidecar
```

Expected output includes:

```text
[*] Processing file with FLARE sidecar metadata ...
[*] Done. Wrote ... sidecar segments to input.flare.json.
```

Upload and print `input.flare.gcode`. While it prints, run:

```bash
python3 scripts/flare_live_tuner.py --port "$FLARE_PORT" \
  --machine-id "$MACHINE_ID" \
  --observe-daemon \
  --csv-out ~/flare-runs/run.csv \
  --klipper-uds "$KLIPPER_UDS" \
  --sidecar input.flare.json
```

Expected signs it is working:

- `~/flare-runs/run.csv` appears and grows during the print.
- `~/flare-state/buckets-${MACHINE_ID}.json` appears or updates.
- The tuner does not send `SET:` or `SV:` in default observe mode.

Run this once for the slow profile and once for the fast profile, using
different CSV output names.

## Analyze The Two Profiles

Emit the deterministic flow schedule:

```bash
python3 scripts/flare_analyze.py \
  --profile-fast ~/flare-runs/fast.csv \
  --profile-slow ~/flare-runs/slow.csv \
  --emit-flow-schedule \
  --flow-schedule-cap 8 \
  --out flow-schedule.ini
```

`--flow-schedule-cap` is optional. Valid range is 1 to 16; default config uses
8.

Sample output file:

```ini
# flare_analyze.py emitted flow schedule
# Each point: flow_sps, baseline_sps, compression_bias_frac
flow_schedule_cap: 8

[flow_schedule.v1]
point0: 6000, 7000, 0.300
point1: 12000, 13000, 0.400
```

If there is not enough mature data, the analyzer emits one point. That is safe:
it is the scalar fallback path, just expressed as a schedule.

For a broader review patch from multiple runs and tuner state:

```bash
python3 scripts/flare_analyze.py \
  --in ~/flare-runs/run1.csv ~/flare-runs/run2.csv ~/flare-runs/run3.csv \
  --state ~/flare-state/buckets-${MACHINE_ID}.json \
  --out config.patch.ini \
  --acceptance-gate
```

## Review, Apply, Build, Flash

For a schedule, copy these from `flow-schedule.ini` into `config.ini`:

```ini
flow_schedule_cap: 8

[flow_schedule.v1]
point0: ...
point1: ...

[DEFAULT]
```

Keep `[DEFAULT]` after the schedule block if more flat config keys follow.

For a scalar fallback, use these keys instead:

```ini
baseline_rate: 1600
sync_compression_bias_frac: 0.4
```

Then regenerate, build, and flash:

```bash
python3 scripts/gen_config.py
ninja -C build_local
bash scripts/flash_flare.sh
```

After applying analyzer-reviewed values, update the state watermark:

```bash
python3 scripts/flare_analyze.py \
  --in ~/flare-runs/run1.csv ~/flare-runs/run2.csv ~/flare-runs/run3.csv \
  --out watermark.patch.ini \
  --commit-watermark \
  --state ~/flare-state/buckets-${MACHINE_ID}.json \
  --machine-id "$MACHINE_ID"
```

The watermark command uses the normal review path and therefore needs input CSVs
and an output patch path even if you only care about updating the state file.

## Baseline Recommender

`scripts/flare_baseline_recommender.py` is observe-only. It reads status lines
and prints suggestions; it never writes to firmware, never saves flash, and
does not replace the offline analyzer as the persistent authority.

Read from the live serial port until Ctrl-C:

```bash
python3 scripts/flare_baseline_recommender.py --port "$FLARE_PORT" --baud 115200
```

Replay a saved stream:

```bash
python3 scripts/flare_baseline_recommender.py --file stream.log
```

Typical output includes:

```text
Suggested baseline_sps: 1600
Suggested sync_compression_bias_frac: 0.400
```

Treat this as a hint. If it disagrees with the deterministic analyzer, prefer
the analyzer result unless you are doing a deliberate experiment.

## Verify After Flash

Run the status command:

```bash
python3 scripts/flare_cmd.py --port "$FLARE_PORT" "?:"
```

Useful fields:

- `BL`: active baseline after schedule lookup and any temporary live lift.
- `TB`: active compression bias as percent.
- `RT`: reserve target; negative means compression side.
- `BP` / `BPV`: current/effective buffer position.
- `EST`: live flow estimate.
- `SYNC_REFILL_MM`: accumulated refill effort during the current episode.
- `SYNC_RELIEVE_MM`: accumulated relieve effort during the current episode.

Events in operator terms:

- `EV:SYNC:FAULT_HOLD`: sync paused itself because it hit a hard safety
  condition. Check for jams, blocked filament, or buffer travel issues.
- `EV:SYNC:FAULT_HOLD_RECOVERY`: the pause timer expired and sync is allowed to
  try again.
- `EV:SYNC:cannot_refill`: FLARE spent a lot of effort trying to refill the
  buffer. Check drag, max sync speed, or a printer flow spike.
- `EV:SYNC:cannot_relieve`: FLARE spent a lot of effort trying to reduce excess
  buffer. Check retractions, path resistance, or too much feed pressure.

For the internal behavior behind these events, see [BEHAVIOR.md](./BEHAVIOR.md).

## Troubleshooting

### The Analyzer Says FAIL

FAIL means the recommendation is not reliable enough to apply blindly. Common
causes are too little comparable data, inconsistent runs, very high scatter, or
too little mature contributor mass.

Action: do not copy the values yet. Fix the mechanical/input issue or collect
more comparable runs, then analyze again.

### The Analyzer Says WARN

WARN means the result can still be useful, but the data says something deserves
attention: short soak, low run count, stale config, or limited contributor
coverage.

Action: review the patch, decide whether the warning is acceptable, and prefer
another capture if the warning matches something suspicious in the print.

### Different Numbers Each Run

The two-profile schedule emitter is deterministic: same input CSV files produce
the same output file. If numbers change between captures, the printer changed
or the captured runs are not comparable.

Action: use the scalar one-point fallback for a simple safe setup, or repeat
the slow/fast captures with the same model and stable hardware.

### Sparse Data Produces One Point

That is expected when the analyzer does not trust enough flow bins. A one-point
schedule is valid and keeps behavior simple while you gather more data.

## Open Questions

- `flare_baseline_recommender.py` has no `--machine-id` and no explicit
  end-of-print stop. It reads serial until Ctrl-C or a replay file until EOF.
- There is no canonical slow/fast slicer pair for every printer. Use the
  relative bracket in this guide until your hardware has known-good profiles.
