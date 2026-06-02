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

## Hardware follow-up 5: `relay_neutral_frac` is non-functional — the baseline anti-tension floor is the true root (2026-06-02)

After Fork A (6.1-6.3) the rig still bang-bangs COMPRESSION, and a `frac` sweep
proved **`relay_neutral_frac` has no effect on the applied feed**:

| frac (`GET` confirmed) | relay law NEUTRAL = `EST·frac` | observed `MM` |
|------------------------|--------------------------------|---------------|
| 1.10 | 1857 | ~1730 |
| 1.00 | 1688 | ~1680 |
| 0.50 | **844** | **1680** (unchanged) |

frac = 0.50 should halve the feed to 844; `MM` did not move off ~1680. The relay
law output is overridden downstream. **All frac tuning (tasks §1, §4, §6.3) is
moot until that override is removed.**

### The override, identified

Rig config: `BASELINE_RATE = 2400 mm/min`, `SYNC_MAX = 3000`, threshold ⇒ the
reserve target is compression-biased to ~`+2.5 mm` (`RE` in status ≈ `BP − 2.5`).
The pin is `sync_neutral_anti_tension_floor_sps` (`sync.c:1160`):

```
assist_floor = baseline_control_floor_sps() · SYNC_NEUTRAL_ANTI_TENSION_FLOOR_FRAC
             = 2400 · 0.70  = 1680 mm/min          ← exactly the observed MM
```

It fires whenever `error_norm <= deadband_norm`, i.e. whenever the buffer sits
at or below `target + deadband`. With the target biased to `+2.5` and the buffer
riding `BP ≈ 3.2 .. 4.4`, the error straddles the deadband edge: at the low end
the floor fires and slams feed to 1680; at the high end it releases (`MM` drops
to 360/720). That edge-oscillation **is** the COMPRESSION limit cycle now.

`baseline_control_floor_sps` (`sync.c:1155`) = `max(flow_param(EST).baseline,
g_baseline_target)` — **baseline-derived, not demand-derived.**

### Why this is THE root (supersedes the frac / ramp / EST framing)

At 10 mm/s the real demand is ~300-700 mm/min (the COMPRESSION-drain rate). The
anti-tension floor pins NEUTRAL feed at **1680 mm/min ≈ 2.5-5× demand.** The
buffer physically cannot feed slow enough → mandatory overfeed → COMPRESSION →
drain → repeat, at *any* frac, with the ramp clamped, regardless of EST.

This explains the original speed sweep exactly:

```
25 mm/s  demand ≈ baseline·0.7 (1680)  → floor ≈ demand   → fine
20-15    demand < floor                → floored overfeed → bang-bang
10       demand << floor               → heavy overfeed   → bang-bang
 5       demand < SYNC_MIN             → constant SYNC_MIN ≈ demand → perfect midline
```

The floor stack (`sync_neutral_anti_tension_floor_sps`, `baseline_control_floor`,
the model-stall EST bleed `sync.c:2168`, the §5 relay floor) was all built
assuming demand ≈ baseline (a fast print). At low feed it forces gross overfeed.
EST latching ~2.8× high (follow-up 4) is real but **secondary** — even a correct
EST cannot lower the feed below the baseline·0.70 floor.

### Fix direction (reframes §6/§7)

The anti-tension refill floor must not be a fixed fraction of *baseline*. Options:

1. **Demand-scale the floor:** floor = `min(baseline·0.70, EST·k)` (or purely
   EST-derived) so at low demand the floor drops to ~demand instead of pinning
   to baseline. Keeps tension protection when demand is genuinely high.
2. **Tighten the fire condition:** fire only when *genuinely* tension-side
   (`error_norm < −deadband`, buffer actually draining toward empty), not the
   broad `<= +deadband` that fires across the compression-biased target band.
3. **Revisit the `+2.5` compression-biased reserve target** — it keeps `error`
   small so the floor fires across most of the band; a near-zero target would
   let the buffer rest mid-band.

frac, the no-overshoot ramp (6.1), and the EST-decay fix (6.2) are all real and
should stay, but they are **downstream of this floor** and cannot act until the
floor scales with demand. This is the lever, not `relay_neutral_frac`.

## Hardware follow-up 7-8: open-loop exhausted; the relay touch is the truth signal (2026-06-02)

A baseline scan (`SET:BASELINE_RATE` 2400→800→600, frac=1.00) settled it:

- **Feed tracks baseline** (MM 1680→800→600). Confirms the baseline pin.
- **Lower baseline → longer stable hold.** At 600 the buffer asymptoted to
  `BP ≈ 2.0` and held ~15 s — the longest equilibrium of any run — proving
  feed≈demand parks the buffer.
- **But virtual BP diverges from physical.** During the 600 hold, status read
  `BP:+2.0` (compression-side, at target) while the operator observed the arm
  toward *tension* — model and reality disagreed. With no mid-band sensor, the
  virtual position is pure dead-reckoning on a biased `EST`; it drifts off truth
  and eventually the physical buffer hits a real switch and snaps.

**Conclusion: open-loop tuning (baseline/frac/ramp) is exhausted.** No fixed feed
holds, because there is no mid-band feedback to correct the drift. Lower baseline
only delays the snap. The clicking is *fixed-rate* because the current code never
learns from the touch (EST latched/biased; the `2142/2168/2189` nudges are
reactive and non-convergent).

### Operator acceptance bar (decisive)

Zero compression / perfect mid-band is **not** the goal and is physically
unreachable here. The bar is: **rare, self-correcting compression touches are
fine; fixed-rate constant clicking (and the wroom-wroom chopper flip) is not.**

### This reframes the COMPRESSION touch as the sensor, and makes Fork B the fix

The COMPRESSION switch is the only mid-band truth type-D gets. Each touch is a
measurement: "overfed — back off." The fix is to *learn* from it:

```
NEUTRAL→COMPRESSION crossing → feed trim −= step   (overfed)
NEUTRAL→TENSION    crossing → feed trim += step   (starved)
```

This integrates the crossing *sign* into a bounded, accumulating trim — so the
net correction converges and the touch rate **decays to sparse**, vs the current
reactive nudges that re-overfeed identically every cycle (no convergence →
constant clicking). Convergent vs non-convergent learning is the whole
difference. Steady converged feed also stops crossing the StealthChop threshold →
no wroom. See `tasks.md` §7 for the build plan.

**Coupling: §9 is a prerequisite for §7.** The baseline-derived NEUTRAL floors
(anti-tension = baseline·0.70, `relay_base` cap) clamp any downward trim back up,
exactly as they defeat `frac`. The floors must be demand-scaled / tension-side-
only (§9) before B's trim can pull feed down to demand. Build order: §9 → §7.

### Final plan of record

1. §6 (done): no-overshoot ramp + EST true-stop + frac default — keep.
2. §9: demand-scale the baseline NEUTRAL floors so feed *can* reach demand.
3. §7 (Fork B): convergent crossing-trim that *finds* demand and converges touch
   rate to sparse. This is the actual fix; build it.

Non-goal (moved to `proposal.md`): eliminating compression touches / perfect
mid-band hold.

## Hardware follow-up 9: trim alone can't win — fix EST at the root (2026-06-02)

Two Fork-B soaks settled the architecture question:

- **STEP 300:** the trim integrated `MM` down `1450 → 590` over the run (touch
  rate visibly decaying — Fork B *works*), but **overshot past demand into
  tension** and recovered slowly because tension crossings are sparse.
- **STEP 120:** the trim from the prior run **persisted** (only resets on sync
  re-arm, `sync.c:1595`), starting deeply negative; recovery was glacial
  (`MM` neutral crawled `350 → 437` at `+17 mm/min` per −5 hit) with the buffer
  **starved tension-side the whole time**.

The crossing-trim is a pure event-integrator with three structural faults: **no
leak** (ratchets unbounded), **asymmetric feedback** (compression crossings
frequent → fast down; tension crossings sparse → slow up), and **cross-session
persistence**. No `STEP` value fixes these — tuning is the wrong lever.

### Root cause, restated

Every failure traces to one thing: **`EST` is latched ~1200-1414 vs real demand
~600 (≈2.3× high)**, so the trim must swing `±~800 mm/min` just to cancel a
constant estimator error — and a swing that large, through sparse asymmetric
feedback, cannot help but overshoot and stick.

### The fix: anchor EST to the COMPRESSION drain (Fork D, `tasks.md` §10)

The buffer motion measures real demand directly, and the **COMPRESSION true-stop
is the clean anchor**: feed is `0` there, so the drain velocity *is* the demand,
with no feed term to subtract. Sample the drain on `COMPRESSION → NEUTRAL` and
correct `EST`. The existing crossing estimator (`sync.c:1010`) misses this — it
samples the *fill* (overfeed) motion at compression *entry*, which is exactly why
`EST` latches high.

With `EST ≈ demand`, `feed = EST · 1.00` matches demand directly — the same
stable regime as the 5 mm/s "perfect midline" case — the buffer holds, touches go
sparse, and the trim collapses to a small residual (now leak-bounded). The
operator already accepts the compression touch as the sensor; Fork D makes each
touch **calibrate** `EST`, not merely bound the buffer. This ends the
overshoot/starvation cycle instead of damping it.

### Plan of record (final)

1. §6 (done): no-overshoot ramp + EST true-stop + frac default.
2. §9 (done): demand-scale the baseline NEUTRAL floors — frac now live.
3. §10 (build): correct `EST` from the COMPRESSION drain (root fix); demote the
   §7 trim to a leak-bounded residual.

Fork B's trim stays — but as a small corrector on top of a now-correct `EST`,
not the primary actuator.

### §10 implementation plan

#### `firmware/src/sync.c`

- Repair the type-D crossing estimator so switch-travel velocity uses the same
  sign convention as the type-P estimator: `extruder = mmu - arm_velocity`.
- Give `COMPRESSION -> NEUTRAL` a threshold-distance drain sample instead of
  zero travel, and special-case it as the authoritative true-stop sample:
  commanded feed is zero, so demand is `-arm_velocity` with no MMU feed term.
- Stop direct `TENSION -> COMPRESSION` overwrites from spiking `EST`; all type-D
  crossing samples blend through the bounded EMA.
- Add a neutral dwell leak for `g_relay_neutral_trim_sps` so the §7 trim becomes
  a residual corrector instead of a stuck event-integrator. Reset or shrink the
  trim on large compression-drain `EST` corrections.
- Keep every edit gated to `BUF_SENSOR_TYPE == 0`; do not touch the analog
  type-P estimator / `psf_control_law` feedforward path.

#### Config and docs

- Lower the default `sync_relay_trim_clamp_sps` to the residual range
  (`~2000`) in `scripts/gen_config.py`, `config.ini.example`, `MANUAL.md`, and
  `TUNING.md`.
- Document that `COMPRESSION -> NEUTRAL` anchors type-D `EST` from true-stop
  drain, and that the crossing trim now leaks toward zero while neutral.

## Hardware follow-up 10: EST latch broken, but the drain window is feed-contaminated (2026-06-02)

§10 (anchor `EST` from the COMPRESSION drain) achieved its first goal: **`EST`
now moves** — the latch is gone (`949 → 886 → 972 → 1036 → … `). But it converges
the **wrong way**: monotonically *up* to ~1300 (≈2× real demand ~600), only ever
stepping at COMPRESSION exits, so the buffer keeps overfeeding and ends pinned at
the COMPRESSION wall.

Root cause: the COMPRESSION-drain window is **not feed≈0**. The true-stop ramps
down over ~100-200 ms — the rig MM read `1320 → 960 → 585 → 606 → 268 → … → 0.1`
*while* `BUF:COMPRESSION` — and the dwell is short. So the §10.1 sample picks up
(a) residual ramp-down feed (~400) it assumed was 0, plus (b) an inflated
`travel/dwell` arm velocity over the brief noisy window (~900) → demand ≈ 1300.
The "clean anchor" premise (feed = 0) is violated in practice. The first cycle
read correctly (949→886, down) before the residual built up; later cycles inflate.

### Fix: measure demand from the NEUTRAL fill, not the drain (tasks §10b)

The NEUTRAL→COMPRESSION fill is the better truth signal: it happens at a
**known, steady** commanded feed `F` (the relay NEUTRAL output, not ramping),
over a **long** window. The buffer fills toward COMPRESSION at `(F − demand)`, so
`demand = F_avg − fill_rate`, with `fill_rate = neutral_travel / neutral_dwell`
and `F_avg` a running mean of commanded feed over the NEUTRAL dwell. No
ramp-down contamination, low noise, feed known. Demote the drain sample (keep the
feed-0 true-stop itself unchanged). Sanity-bound the estimate to `[SYNC_MIN,
baseline]` and reject degenerate windows.

This keeps the Fork D principle — each touch *calibrates* `EST` — but reads the
calibration off the clean fill segment instead of the contaminated drain.

### §10b implementation plan

#### `firmware/src/sync.c`

- Track a dedicated Type-D NEUTRAL dwell feed mean from the applied
  `sync_current_sps`. Reset it on `BUF_NEUTRAL` entry and sample it while the
  stable state remains `BUF_NEUTRAL`.
- On `BUF_NEUTRAL -> BUF_COMPRESSION`, compute `fill_sps` from the known switch
  travel and the neutral dwell, then estimate demand as
  `demand_sps = neutral_feed_avg_sps - fill_sps`.
- Reject degenerate samples: no feed samples, dwell below a minimum, invalid
  lane step size, non-positive averaged feed, or `fill_sps >= F_avg` (negative
  demand). Clamp accepted demand samples to `[SYNC_MIN_SPS, baseline_control_floor_sps()]`.
- Demote `BUF_COMPRESSION -> BUF_NEUTRAL` drain sampling to a non-primary path by
  only accepting it after a minimum compression dwell and near-zero applied feed;
  subtract actual averaged feed if retained.
- Preserve type-P code paths byte-stable; all new estimator behavior is gated to
  `BUF_SENSOR_TYPE == 0`.
