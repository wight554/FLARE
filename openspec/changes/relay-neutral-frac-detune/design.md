# Design — relay-neutral-frac-detune

## The limit cycle, quantified

Type-D has only two microswitches: no analog position, no extruder feedback.
`relay_control_law` (`sync.c:1627`) is a hysteretic relay on the switch state:

| State | Feed target |
|-------|-------------|
| TENSION (empty) | `baseline × relay_catchup_frac` (fast refill) |
| NEUTRAL | `clamp(extruder_est_sps, SYNC_MIN, baseline) × relay_neutral_frac` |
| COMPRESSION (full) | `0` (true-stop, `sync.c:2097`) |

`extruder_est_sps` ≈ true demand `D` (measured each crossing). In NEUTRAL the
net buffer fill rate is `(neutral_frac − 1) · D`. With `neutral_frac = 1.25`
that is `0.25·D` — the arm climbs into COMPRESSION quickly, the relay stops to
`0`, the extruder drains it at `−D`, it re-enters NEUTRAL, and the slew-limited
feed (`SYNC_RAMP_UP/DN`) ramps back up. The audible "ramp → stop → ramp" is
this cycle; its frequency ∝ `(neutral_frac − 1)`.

The first pass lowered `neutral_frac` to `1.10`, cutting overfeed to `0.10·D`.
Hardware follow-up 3 then showed the ramp itself was overshooting its target; a
no-overshoot ramp makes `1.00` viable and preferred: NEUTRAL matches demand,
with switches acting as guardrails. If a machine *does* drift to TENSION under
steady demand, `TUNING.md` prescribes nudging `neutral_frac` slightly above
`1.00`.

## Why not other levers

- **`sync_kp_rate` / autotune** — not in the type-D path; `relay_control_law`
  ignores it. Only `psf_control_law` (analog type-P) consumes kp. Tuning it on
  type-D is a no-op (this is exactly how the prior session was misled).
- **`sync_ramp_accel/decel`** — only the slew rate between the relay's discrete
  levels. Raising it makes the motor chase the jumps harder = a tighter, more
  violent cycle (observed: accel 300/500 → unstable). 150 is validated; leave
  it.
- **COMPRESSION feed (the `0` true-stop)** — must stay `0`. Feeding `SYNC_MIN`
  into a full buffer was the bowden-overfill / purge-grind cause fixed by
  `compression-overfeed-stop`. The fix is to *reach* COMPRESSION less often
  (lower overfeed), not to soften the stop.
- **`sync_compression_bias_frac`** — a position-setpoint bias, inert for the
  type-D relay (it only sees switch state); irrelevant here.

## Why 1.00 after Fork A

`1.00` is demand match. Before Fork A, the type-D ramp could not rest on the
target, so a "gentle lean" still expressed as 50 Hz feed-command chatter. After
porting the type-P no-overshoot clamp into the type-D branch, the relay target
can settle, so deliberate NEUTRAL overfeed is no longer the default. The exact
on-hardware optimum is expected near `0.95–1.05` and is confirmed via the
runtime A/B below. `relay_neutral_frac` is `SET:`/`GET:`-tunable and clamped
`[0.5, 3.0]` (`protocol.c:978`), so this needs no reflash to validate.

## On-hardware validation (A/B, no reflash)

```
GET:RELAY_NEUTRAL_FRAC            # confirm the live value (persisted; may not be 1.25)
SET:RELAY_NEUTRAL_FRAC:1.00       # print infill; listen + watch BS compression%
SET:RELAY_NEUTRAL_FRAC:0.95       # if still cycling audibly
# if it drifts to TENSION / starves under steady demand, raise toward 1.05
```

Pass bar after the new default + threshold revert: `flare_sync_check.py
--mode stability` peak `< 1.0` cycles/s and combined endstop `< 30 %` on a
typical infill soak, with TENSION still ≈ 0 (no starvation).

## Persistence note

No `SETTINGS_VERSION` bump: this is a value-only default change and a bump
would reset every persisted setting (TMC currents, PSF/baseline calibration).
Already-flashed units therefore keep their stored `relay_neutral_frac` until
the operator `SET:`s the new value or factory-resets; fresh flashes pick up
`1.00`. The A/B step above is the migration path for existing units.

## Open follow-up (not in this change)

`flare_sync_check.py`'s `tune` mode autotunes `SYNC_KP_RATE`, which a type-D
relay ignores — the tool will happily "converge" on a meaningless knob and
report PASS, which is how the prior session was fooled. A future change should
have the tool read `BUF_SENSOR_TYPE` (via `GET:`) and refuse/redirect kp
autotune on type-D (point at `relay_neutral_frac` instead). Documented here
rather than implemented to keep this change value-only.

## Hardware follow-up: downstream clamp defeats relay neutral

2026-06-02 rig testing showed `RELAY_NEUTRAL_FRAC` is active (`EST:1200` with
`frac=1.30` produced `MM:1560`), but downstream target shaping can later reduce
type-D `BUF_NEUTRAL` output below the relay target after real `BUF_TENSION`
hits. Example: `EST:533` and `frac=1.30` imply a neutral relay target near
`693 mm/min`, yet the applied `MM` dropped to `480` or `120`, letting the
printer pull the buffer back to real `BUF_TENSION`. This changes the scope from
"default-only detune" to "preserve the relay target as a type-D neutral floor."

### firmware/src/sync.c

- Capture the raw type-D relay neutral target before shared reserve scaling and
  compression-recovery shaping.
- After those shared shapers run, re-apply that raw relay target as a lower
  bound only for `BUF_SENSOR_TYPE == 0 && BUF_NEUTRAL`.
- Keep `BUF_COMPRESSION` true-stop, fast-brake, max-rate clamping, and analog
  type-P behavior unchanged.
- Risk: a relay floor will allow more deliberate compression-side lean; validate
  that steady feeds no longer revisit `BUF_TENSION` and that compression still
  drains through the true-stop branch.

2026-06-02 follow-up testing confirmed that risk: the broad floor eliminated
the TENSION return, but kept feeding hard even after the neutral reserve was
already compression-side. That turned the type-D neutral band into repeated
COMPRESSION bang-bang. Narrow the relay floor so it only applies while the
reserve error is tension-side (`bp_eff < effective_target`). Once neutral is at
or above the reserve target, shared reserve scaling and compression-recovery
trim must be allowed to reduce feed again.

### BEHAVIOR.md

- Clarify that in type-D neutral, the relay target remains the minimum applied
  feed even after shared sync shapers. This documents the invariant exposed by
  the hardware test.

## Hardware follow-up 3: ramp overshoot is the chatter root cause (2026-06-02)

A second rig soak (10 mm/s steady feed, `flare_cmd.py --poll 100`) showed the
neutral-floor patches above did **not** stop the bang-bang: the buffer estimate
rides the compression-side half of NEUTRAL (`BP ≈ 3.2 .. 5.0`), never visits
TENSION, and slams the COMPRESSION switch repeatedly. Two structural causes,
neither addressed by the floor work:

### Cause 1 — the type-D ramp overshoots its target every tick (primary)

The type-D slew (`sync.c:2374`) has **no clamp-to-target**:

```c
else if (sync_current_sps > target_sps) sync_current_sps -= ramp_dn_sps;   // 2455 sps
else if (sync_current_sps < target_sps) sync_current_sps += SYNC_RAMP_UP_SPS; // 2455 sps
```

With `SYNC_RAMP_UP/DN_SPS = 2455` (≈ 360 mm/min) and `SYNC_TICK_MS = 20`, the
step is far larger than the residual to a neutral target of ≈ 807 mm/min
(`EST 734 × 1.10`). From 720 it jumps to 1080 (overshoot), next tick back to
720 (undershoot), forever — a **50 Hz feed-command chatter of 720↔1080 mm/min**.
This is exactly the `MM:720/1080` alternation in the poll log, present *before*
the relay law contributes anything. The 1080 bursts (≫ demand 734) are what
drive the integrator into COMPRESSION; the chatter straddling the 1000 mm/min
StealthChop threshold is the audible "wroom-wroom" (chopper-mode flip).

**Type-P already solved this.** `sync.c:2346-2354` clamps the applied rate so it
never overshoots the filtered target, with the comment: *"clamped so it never
overshoots the filtered target (overshoot is what made the old ramp oscillate)."*
Type-D never received the clamp. Porting type-P's no-overshoot behavior to the
type-D ramp removes the chatter independent of the relay law — and is **zero risk
to type-P** because it only mirrors type-P's own logic into the
`BUF_SENSOR_TYPE == 0` branch.

### Cause 2 — EST decays under sustained cycling

The poll shows `EST` falling `1200 → 277` under *constant* 10 mm/s demand. Each
COMPRESSION dwell drags `extruder_est_sps` toward `sync_current_sps` (which the
true-stop pulls toward 0) via `sync.c:2189`; the tension/neutral stall nudges
(`2142`, `2168`) are the same family of ad-hoc corrections. The result corrupts
both the relay neutral target (`EST × frac`) and the integrator's demand term
(`sync.c:411`), so the model diverges from the physical buffer the longer it runs.

## Realistic plan: A (stabilize) then B (adapt)

The plant is a pure integrator (`g_buf_pos += (feed − demand)·dt`, `sync.c:413`)
with feedback only at the two switch crossings — no mid-band ground truth
([[typed-buffer-no-midband-groundtruth]]). A relay with an overfeed lean
(`frac > 1`) therefore *cannot* hold mid-band: it has no fixed point and must
limit-cycle. The 5 mm/s "perfect midline" case is the proof — there the feed
sits effectively constant ≈ demand, so the integrator nets ≈ 0 and never
switches. The fix direction is "feed = demand, switches as guardrails," not a
deliberate lean.

### Fork A — stabilize (do first, low risk, type-P safe)

1. Port the type-P no-overshoot ramp clamp to the type-D slew so feed can rest
   *on* the target instead of chattering ±360 mm/min at 50 Hz.
2. With the ramp able to settle, lower the overfeed to neutral (`frac → 1.00`)
   so net fill ≈ 0 and the buffer dwells in the quiet mid-band.
3. Fix the EST-decay drag (gate the `2142/2168/2189` nudges to
   `BUF_SENSOR_TYPE == 0`, or stop dragging EST below true demand) so the target
   does not rot under sustained operation.

Expected outcome: quiet, but **marginally** stable — with no mid-band truth, any
residual DC bias in `(feed − demand)` (EST estimation error) slowly walks the
buffer to a rail and the switch catches it. That residual slow drift is fork B's
job.

### Fork B — adapt (only if A's residual drift is unacceptable)

On a 2-switch integrator the **only** information is the *time between
crossings*: the period / climb-rate is a direct measure of `(feed − demand)`.
Close a slow integral loop on crossing events that trims the neutral feed toward
a long dwell — i.e. an **adaptive `frac`** that auto-finds the per-print value
the manual A/B (0.95/1.00/1.05) finds by hand.

- **B requires A first.** B nulls a slow-drift signal; if the 50 Hz ramp chatter
  is still present, B's error signal (climb time) is buried in it and will not
  converge. A removes the chatter and exposes the clean drift B is built to kill.
- **B is cleanup, not greenfield.** It replaces the uncoordinated, decay-causing
  EST nudges (`2142/2168/2189`) with one principled crossing-event loop.
- **Type-P safe** when scoped to the `BUF_SENSOR_TYPE == 0` crossing path. Type-P
  has continuous analog position and its own PD loop (`psf_control_law`); it does
  not need (and must not run) the dwell-time trim. The only leak point is the
  shared EST nudges — keep a type-P path or gate the replacement by sensor type.

A is the floor (quiet); B is the ceiling (self-correcting). They are
complementary, not either/or.

### Type-P regression map (applies to both forks)

```
SAFE  (separate branches — type-P untouched):
  relay_control_law (sync.c:1812)   BUF_SENSOR_TYPE == 0 only
  type-D ramp (sync.c:2374)         clamp ported IS type-P's own logic → 0 change to P

EXPOSED (shared code — must gate by sensor type or type-P shifts):
  EST nudges (sync.c:2139-2193)     type-P uses EST as feedforward (psf_control_law:1840)
  reserve integral / compression-recovery shaping
```
