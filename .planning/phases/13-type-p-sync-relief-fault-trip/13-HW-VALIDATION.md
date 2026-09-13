---
phase: "13"
slug: "type-p-sync-relief-fault-trip"
doc: "hw-validation"
# status lifecycle: draft (written before the rig is touched, Task 1) -> closed (Task 3, all
# four pass-bar items PASS with the developer's confirmation quoted) -> gap (Task 3, any item
# failed or reported NEEDS-INPUT; phase routes to /gsd-plan-phase --gaps)
status: draft
created: "2026-09-13"
rig: "type-P rig (analog/Hall buffer sensor, BUF_SENSOR_TYPE=1)"
baseline_ref: ".planning/phases/13-type-p-sync-relief-fault-trip/baseline-capture.md"
---

# Phase 13 HW Validation — Type-P Sync Relief & Fault Trip

> A rig operator should be able to execute this entire validation from this one file, without
> re-reading `13-CONTEXT.md`. D-32 ("sim first, filament last") already held: everything
> deterministic passed in `flare_sim` across plans 13-01 through 13-03 before this print is run.

---

## Pre-flight

1. **Build the dev-tuning superset** (AGENTS.md — Tier-3 `SET:`/`GET:` handlers, including both
   new knobs below, are compiled only under `-DFLARE_DEV_TUNING`):
   ```bash
   python3 scripts/gen_config.py
   cmake -S firmware -B build_local -G Ninja -DPICO_SDK_PATH=/path/to/pico-sdk -DFLARE_DEV_TUNING=ON
   ninja -C build_local
   ```
   (`scripts/validate_regression.py` already configures `build_local` with `-DFLARE_DEV_TUNING=ON`
   for every commit in this phase; this step just confirms the same superset is what gets flashed.)

2. **Flash** (`BUILD_FLASH.md` "Easy Path"):
   ```bash
   python3 scripts/flash_flare.py
   ```
   or manually via `picotool load build_local/flare_controller.uf2 -f && picotool reboot`.

3. **Power-cycle or reset the board** (a full reboot, not just a reconnect) — this is the step
   that catches an unpersisted runtime `SET:` before the rest of the capture wastes 2 hours
   measuring a configuration that will not survive the next reboot (memory: `rail-break 3000`
   needed a reflash once already).

4. **Confirm both new knobs persisted, and read the pre-existing dwell threshold, with `GET:`
   after the reboot — not merely after a `SET:`:**
   ```bash
   python3 scripts/flare_cmd.py "GET:SYNC_PSF_RELIEF_MULT"
   python3 scripts/flare_cmd.py "GET:SYNC_TENSION_STOP_MM"
   python3 scripts/flare_cmd.py "GET:SYNC_TENSION_STOP_MS"
   ```
   Expected replies (config.ini defaults, unless the rig's `config.ini` overrides them —
   record whichever value actually comes back):
   - `OK:SYNC_PSF_RELIEF_MULT:1.33` (13-01, `SYNC_PSF_RELIEF_MULT`, clamped 1.0-3.0)
   - `OK:SYNC_TENSION_STOP_MM:32.0` (13-02, `SYNC_TENSION_STOP_MM`, clamped 0-500, 0=disable)
   - `OK:SYNC_TENSION_STOP_MS:6000` (pre-existing `sync_tension_dwell_stop_ms`, unchanged by this
     phase — recorded so the mm-vs-ms trip split observed in the capture can be interpreted
     afterwards: item 3 below expects the mm trip, not the ms fallback, to be the one that
     stays silent, since 13-02-SUMMARY.md's investigation found the ms path structurally
     unreachable ahead of mm at this project's tuning defaults)
   If any value differs from what was `SET:` before the reboot, STOP — the capture would
   measure a configuration the board does not actually run. Re-flash and retry pre-flight.

---

## Capture procedure

1. **Start the event-log capture** before the print, run for its full duration:
   ```bash
   python3 scripts/verify_hw_open_items.py watch --log phase13_hw_watch.jsonl --interval 1.0
   ```
   (`watch_p` in `verify_hw_open_items.py`'s `build_parser`; `--log` defaults to
   `hw_verify_evidence.jsonl` — pass an explicit Phase-13-specific path so it is never
   confused with an unrelated open-items capture. `Ctrl+C` to stop at the end of the print.)

2. **Concurrently, start the position capture** — the watch log above records events only;
   pass-bar item 4 is a buffer-*position* question the event log cannot answer:
   ```bash
   python3 scripts/flare_sync_check.py --daemon --csv phase13_hw_positions.csv --duration 7500
   ```
   (`--daemon` captures live through `flare_daemon`'s `/status`; `--duration` in seconds, set a
   little past the expected ~2h/7200s print length so the capture doesn't cut off early; `--csv`
   writes `CSV_FIELDS` including `BP` — buffer position — and `SM` — `sync_enabled`, `"1"` while
   ordinary print sync is active, `"0"` during a deliberate `BL:`/toolchange rail operation.)

3. **Run the print** — the same job as the 2026-09-12 baseline: roughly 2 hours, roughly 10
   tool swaps, same rig (D-30). Do not alter feed rates, filament, or slicer settings from the
   baseline run; a comparison against a changed print is not a comparison against the baseline.

4. **Stop both captures** when the print completes (or aborts — record that too).

5. **Neither capture file is committed to this repository.** Only the derived numbers (the
   parser's own output, pasted into the Results table below) are recorded here.

---

## Pass bar (D-31 — all four required)

| # | Item | Baseline comparator | Countable signal |
|---|------|----------------------|-------------------|
| 1 | Relief-pause rate at most 1 per 60 s during sustained sync | Baseline: dozens, roughly every 2.4-3.4s (`baseline-capture.md`) | Count of `SYNC:RELIEF_PAUSE` events divided by the sustained-sync span the log covers, reported per 60 s |
| 2 | Fewer `SYNC:TENSION_RISK_HIGH` events than the baseline | Baseline: 13 in 2 h (`baseline-capture.md`) | Total count of `SYNC:TENSION_RISK_HIGH` events over the print |
| 3 | Zero false stops on a print that completes normally | Target: 0 of each | Counts of `SYNC:FAULT_HOLD`, `SYNC:TENSION_STOP:MM`, `SYNC:TENSION_STOP:MS`, and `SYNC:PROBE:NO_CONSUMER` events |
| 4 | No rail hits during print sync, outside deliberate BL:/toolchange operations | The extremes actually observed in *this* capture's `BP` samples — never a literal position threshold | Minimum and maximum `BP` observed while `SM == "1"` (ordinary print sync), and whether any such sample falls within `BL_BREAK_DELTA_NORM` of either extreme |

**Why item 4 is expressed relative to the observed extremes, never as a literal position
threshold (0.75/-0.99/0.8 forbidden here):** this project already learned this lesson the hard
way. The same-day firmware commits `76b4b73`/`e0df203` gated the type-P buffer-lock engage
detection on a fixed absolute threshold (`g_bl_lock_engaged = g_buf_pos <= -0.75`), and a real
rig's tension hard end read shallower than that (`-0.68` observed, later confirmed as shallow as
`-0.36` cold and `-0.99`/`-1.00` hot) — so the lock never counted as engaged, `BL:BREAK`/`FOLLOW`
never fired, and the buffer pinned at the compression rail while the extruder skipped against
the held MMU (the `UNLOAD_TIMEOUT` fault documented in `baseline-capture.md`). The fix
(`51bdca8`) was to gate relative to the deepest reading actually observed at the rail
(`g_bl_lock_extreme`, `BL_BREAK_DELTA_NORM`), not a hardcoded constant. Item 4 in this sheet
follows the identical discipline: it is derived from the extremes this capture itself observes,
never from an assumed number like `0.8` (the previously-documented "hunting oscillation" span)
or `-0.99`/`-0.75` (the two constants involved in the `51bdca8` regression).

**Derive all four numbers with a single invocation — not a hand-assembled grep/awk pipeline —
of the committed, unit-tested parser:**
```bash
python3 scripts/verify_phase13_hw_log.py \
  --events phase13_hw_watch.jsonl \
  --csv phase13_hw_positions.csv
```
It prints one `PASS`/`FAIL`/`NEEDS-INPUT` line per item above plus an `OVERALL:` verdict line,
and exits non-zero unless all four items `PASS`. A partial result — including any item the
script reports `NEEDS-INPUT` because an input capture was missing — is a **fail for that item**,
not a judgement call. Paste the tool's output verbatim into the Results table below; do not
re-derive any number by hand.

---

## What sim already proved (D-32 — do not re-litigate on the rig)

Rig time is spent only on what `flare_sim` cannot reach — real spring dynamics and real
extruder grip. The following are already deterministic and machine-checked, and this print is
not expected to re-discover them:

- **The bound invariant** (13-01): type-P urgent refill never snaps directly to `max_sps`/the
  compression rail while starved; the relief target is bounded by
  `min(max_sps, max(demand x SYNC_PSF_RELIEF_MULT, baseline_sps))`. Proven across rail scales
  1.0/0.7/0.5 by the `TypePReliefBoundTests` suite and four `flare_sim` scenarios.
- **Trip ordering and arming** (13-02): `SYNC_TENSION_STOP_MM` is evaluated before
  `SYNC_TENSION_STOP_MS`; both trips arm only after the first observed buffer-state transition
  in the active-sync window (`g_sync_trip_armed`), and neither fires during a deliberate rail
  hold. Covered by `sem_psf_mm_trip`, `sem_psf_ms_fallback`, and the arm-gate scenarios.
- **Hold suppression**: the trips (and the probe) are suppressed while `BL:`, tail-assist,
  buffer-stabilize, or a RELOAD follow is in progress — proven by dedicated scenarios, not left
  to the rig to discover empirically.
- **Probe outcomes** (13-03): `PROBE:CONSUMER` vs `PROBE:NO_CONSUMER` resolved via the
  window-max deflection latch compared rail-relatively against the tracked tension extreme;
  proven by `sem_psf_probe_consumer`/`sem_psf_probe_no_consumer` and their rail-scale twins.
- **Shallow-rail twins**: all of the above re-run at `type_p_rail_scale` 0.7 and 0.5 to confirm
  rail-relative behavior holds when the physical rail reads shallower than the default scale —
  the exact axis the `51bdca8` regression broke.

If the rig print surfaces something none of the above anticipated (an unexpected event string,
an audible near-slip, a swap behaving differently than the baseline), record it under Results
notes even if all four pass-bar items still pass — Task 3 carries it forward as a durable
observation, not a silent pass.

---

## Results

*Filled in only from the Task 2 checkpoint's reported numbers — see `<precondition>` on Task 3
of `13-04-PLAN.md`. Never inferred, estimated, or filled from a sim result.*

| # | Item | Observed | Baseline | Verdict | Notes |
|---|------|----------|----------|---------|-------|
| 1 | Relief-pause rate (/60s) | | dozens (~every 2.4-3.4s) | | |
| 2 | `SYNC:TENSION_RISK_HIGH` count | | 13 | | |
| 3 | False stops (fault_hold/stop_mm/stop_ms/no_consumer) | | 0/0/0/0 | | |
| 4 | Rail hits during print sync | | 0 | | |

**Parser invocation used for these results:**
```
(not yet run — filled in by Task 3 alongside the Results table above)
```

**`HW:` acceptance:** ⬜ not yet confirmed by the developer (AGENTS.md rule 12 — never checked
without explicit user confirmation and real rig results).
