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

**Check your sensor type first** (`buf_sensor_type` in `config.ini`):
- `buf_sensor_type: 0` → type D (two switches) — follow **[Type-D path](#tldr-type-d-relay)**
- `buf_sensor_type: 1` → type P (analog) — follow **[Type-P path](#tldr-type-p-flow-schedule)**

```bash
python3 -m pip install pyserial

export FLARE_PORT=/dev/ttyACM0
export MACHINE_ID=myprinter
export KLIPPER_UDS=/home/pi/printer_data/comms/klippy.sock

mkdir -p ~/flare-runs ~/flare-state
cp ~/flare-state/buckets-${MACHINE_ID}.json \
  ~/flare-state/buckets-${MACHINE_ID}.json.$(date +%Y%m%d-%H%M%S).bak 2>/dev/null || true
```

### TL;DR: Type-D Relay

Type-D does not use the flow schedule or offline analyzer recommendations.
Start from the fallback relay defaults, then only adjust the retained fallback
knobs if a real print shows starvation or excess compression lean.

```bash
python3 scripts/gen_config.py
ninja -C build_local
bash scripts/flash_flare.sh
```

Check status during a real print:

```bash
python3 scripts/flare_cmd.py --port "$FLARE_PORT" "?:"
```

In a healthy run: no repeated `TENSION_RISK_HIGH`, no long tension-side pinning,
and `BP` stays off the physical wall.

### TL;DR: Type-P Flow Schedule

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

- `buf_sensor_type` selects the Sync-Feedback Sensor type: `D=0` is the
  dual two-switch sensor, `P=1` is the proportional analog sensor. Happy Hare
  types `TO` and `CO` are recognized vocabulary but are not implemented in
  FLARE.
- Type D uses the two-level / hysteretic relay control law. Type P uses the
  analog PD/EKF reserve control law. The sensor type and the control law are
  separate: keep the `D=0` / `P=1` value contract when changing sensor mode.
  Happy Hare uses the opposite sign convention for analog work
  (`+1 = compression`, `-1 = tension`); FLARE uses `+1 = tension`,
  `-1 = compression`. Any future analog port from the `audit-sync-polarity`
  D4 reference must flip every sign. The no-rig analog debt is tracked by
  `relay-buffer-control-2switch` task 7.3 / `pending-analog-rig`.
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

## Type-D Relay Fallback Tuning

For Sync-Feedback Sensor type D (`BUF_SENSOR_TYPE == 0`, D=0), FLARE uses the
two-level relay law:

- `BUF_TENSION`: refill at `baseline_rate * relay_catchup_frac`.
- `BUF_COMPRESSION`: stop completely (commands 0 SPS) to prevent overfilling.
- `BUF_NEUTRAL`: use `extruder_est_sps * relay_neutral_frac`, clamped to the
  normal `[SYNC_MIN_RATE, baseline_rate]` fallback range.

NEUTRAL is **always** driven by the extruder-speed fallback — there is no
confidence gate or duty estimator. The estimator path was removed after
on-hardware validation showed it causes the buffer to ride the physical empty
wall on bimodal (fast/slow alternating) prints: the estimator collapses under
flip-heavy traffic → NEUTRAL chronically underfeeds → 26–43 % of rows in
`BUF_TENSION`, `BP` pegged at the +12.5 mm wall. The fallback keeps `BP` off
the wall (`BPmax` ≈ 5 mm, shallow within the ±5 mm switch span) on both slow
and bimodal loads.

The knobs live in `config.ini`, not in `sync.c` defines:

```ini
relay_catchup_frac: 1.30
relay_neutral_frac: 1.10
relay_min_flip_mm: 0.0
relay_collapse_delay_ms: 250
relay_collapse_ramp_mult: 3
relay_collapse_cap_ms: 600
```

`relay_min_flip_mm` stays **`0.0` (time-only)**. A non-zero value
deadlocks the type-D relay: the flip guard accrues distance from the
gated MMU's own motion, but the COMPRESSION branch commands `SYNC_MIN`
(zero feed) and a cold start has zero feed, so the corrective flip that
would *start* motion never accumulates the distance — sync freezes. It
remains a config/`SET:` knob only for experiments that accept this
caveat; the time-based `BUF_HYST_MS` is the supported chatter guard.

`RELAY_CATCHUP_FRAC`, `RELAY_NEUTRAL_FRAC`, `RELAY_MIN_FLIP_MM`, and the three
`RELAY_COLLAPSE_{DELAY_MS,RAMP_MULT,CAP_MS}` are runtime-safe
`SET:`/`GET:` parameters (and appear in `flare_cmd.py --dump`) for field
experiments without a reflash. The collapse-ramp keys shape the
deep-COMPRESSION / print-end stop; defaults (250 / 3 / 600) already give
a graceful taper with the fallback relay law keeping the buffer off the
wall — softening them on-hw only added pre-stop chatter, so leave them
unless a specific machine shows an abrupt stop.

`relay_neutral_frac` is the type-D quiet-cycle lever. NEUTRAL feed is
`extruder_est_sps * relay_neutral_frac`, and `extruder_est_sps` already tracks
real demand, so the fraction above `1.0` is pure overfeed: it drives the buffer
into COMPRESSION, where the relay true-stops, drains, and re-feeds — the
ramp-up / stop / ramp-up limit cycle you hear. `1.10` (~10 % overfeed) is a
gentle compression lean with a long, quiet NEUTRAL dwell; `1.25` overfeeds 25 %
and bang-bangs the COMPRESSION wall. **`sync_kp_rate` and the `sync_ramp_accel`
autotune do not affect the type-D relay** — it keys only on switch state, not a
PI error. Those apply to analog type P (`BUF_SENSOR_TYPE == 1`) only.

Firmware preserves the type-D `BUF_NEUTRAL` relay target as a lower bound only
while reserve error is tension-side. If reserve is already at/above target,
shared reserve/recovery shaping may reduce `MM` below `EST * RELAY_NEUTRAL_FRAC`
to avoid repeated `BUF_COMPRESSION` bang-bang.

Tune only from real print behavior:

- Increase `relay_catchup_frac` if TENSION dwell repeats or the printer starves.
- Lower `relay_neutral_frac` if the buffer spends too much time on the
  COMPRESSION wall (the audible bang-bang).
- Raise `relay_neutral_frac` if the buffer drifts toward TENSION during steady
  demand.
- Do **not** touch `sync_kp_rate` for type D — it is inert (analog type-P only).
- Leave `relay_min_flip_mm` at `0.0` unless deliberately testing the deadlock
  caveat above.

## Type-P Buffer Lock (Tip Forming)

Tip forming uses `BL:<T|C>:<follow_mm>:<rate>` (via `_FLARE_BL_MOVE`) to hold the
buffer at an extreme and feed-follow the extruder retract. Two firmware constants
gate the type-P (analog) behavior — both compile-time in `controller_shared.h`:

- **Prime speed.** Type-P primes at `BUF_STAB_SPS`, not `SYNC_MAX_SPS`. The PSF
  reading is EMA-filtered (`BUF_ANALOG_ALPHA`) and lags, so a full-speed prime
  overshoots `PSF_HOME_THRESHOLD_NORM` (0.90) and slams the `±1.0` rail. If prime
  still bottoms hard against the rail, lower `BUF_STAB_SPS` or raise
  `BUF_ANALOG_ALPHA` (faster, less-laggy filter). Symptom of the bug: buffer pins
  at `BP:1.000,BUF:+` for the full lock duration.
- **Follow gate.** `PSF_FOLLOW_RAIL_NORM` (default `0.95`) stops the open-loop
  follow feed before it reaches the armed rail; it then drops to LOCKED and emits
  `EV:BL,FOLLOW_GATED`, re-firing only if backflow pushes the buffer off the
  extreme. Lower it (e.g. `0.90`) for more rail margin if the follow still over-feeds;
  raise it toward `1.0` to let the follow run closer to the extreme.

Type-D (switch) is bang-bang and uses neither knob — it primes at `SYNC_MAX_SPS`
and follows purely on the elapsed-distance budget.

## Type-P Sync Feed Smoothness

Type-P does **not** push the PD/feedforward target straight to the motor — the
extruder rate is estimated from the buffer arm, so the raw target is noisy. Two
distance-based stages smooth the feed (both keyed to filament mm moved, not
wall-clock), and both are **live-tunable** (no reflash):

- **`SYNC_PSF_FILTER_MM`** (default `25.0`) — target EMA length in mm. Bigger =
  smoother/slower feed response. `SET:SYNC_PSF_FILTER_MM:40` to calm a jumpy
  feed; lower toward `10` if it feels sluggish to track flow changes.
- **`SYNC_PSF_SLEW_PER_MM`** (default `1500`) — max sps change per mm of filament.
  This is the hard "how fast can the feed speed change" cap. Lower it (e.g. `800`)
  for gentler accel/decel; raise it if the feed can't keep up with fast flow steps.

Tuning order for "too aggressive": drop `SYNC_PSF_SLEW_PER_MM` first (kills the
snap), then raise `SYNC_PSF_FILTER_MM` (smooths residual jitter). Only then
touch the gains — `SYNC_KP_RATE` (proportional) and `KD_PSF` (derivative damping).
A too-high `SYNC_KP_RATE` makes the *target* large; the slew/EMA bound how fast
the motor chases it, but a smaller `SYNC_KP_RATE` reduces the magnitude to chase.
`fast_brake` (compression-slam stop) bypasses all of this — instant stop is preserved.

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
- `CB`: active compression bias as percent.
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
