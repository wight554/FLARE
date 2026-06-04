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

## Hardware follow-up 11: fill estimator helps, but taper/reject gate stalls EST (2026-06-02)

§10b improved the plant: motion became calmer, with long mid-band sweeps
(`BP -2.5 -> 0` over about a second at steady `MM~1008`) and smoother
compression-side braking (`MM 596` at `BP 4.1`, `MM 706` at `BP 2.9`). Touch
rate was down and `EST` converged from the old high latch toward `901`.

Two failures remain:

- `EST` froze around `901`, still roughly `200-250` above real demand
  (`~650`). It did not move despite later compression crossings.
- Virtual `BP` still diverged from the physical arm. The clear symptom was
  `BP:-2.0 -> BP:5.0 COMPRESSION` in one snap: the model thought the buffer was
  tension-side while the physical buffer was filling to the compression wall.

Likely cause: the `NEUTRAL -> COMPRESSION` fill window is not as steady as the
first §10b model assumed. The compression-side taper modulates applied feed
down as `BP` approaches the wall (`865 -> 685 -> 596`), and slow/near-converged
fills can trip the reject gate. This is the same measurement-window class of bug
as the contaminated COMPRESSION drain: the math is right, but the window/filter
needs to match the actual feed profile.

### §10c implementation plan

#### `firmware/src/sync.c`

- Keep tracking the whole-NEUTRAL applied-feed mean, but also track an early /
  pre-taper applied-feed mean while `pos_norm <= target_norm + deadband_norm`.
- Prefer the pre-taper mean for `NEUTRAL -> COMPRESSION` demand samples when it
  has enough samples; otherwise fall back to the whole-dwell mean.
- Relax the reject gate so slow or near-converged fills are not all dropped:
  accept `fill_sps` near `F_avg` by clamping the demand sample to `SYNC_MIN_SPS`;
  reject only true garbage (`dwell` too short, no feed samples, invalid step
  size, non-positive feed, or `fill_sps` far above `F_avg`).
- Keep Type-P untouched.

## Hardware follow-up 12: 20 s feed still sticks full; drain gate too strict (2026-06-02)

The 20 s feed log after §10c shows one successful downward fill sample, then no
fresh estimator movement:

- `EST` moved `1200 -> 1002 -> 895.9`, then froze at `895.9`.
- After the first `BUF_NEUTRAL -> BUF_COMPRESSION`, the plant cycles between a
  short `BUF_COMPRESSION` true-stop (`MM 0.1`) and long `BUF_NEUTRAL` recovery
  sweeps around `MM 640-680`.
- The physical arm still snaps back to COMPRESSION while virtual `BP` drifts as
  far as about `-2`, proving the estimator is still high versus real demand.

Root cause: once a cycle comes from `BUF_COMPRESSION -> BUF_NEUTRAL`, the next
`BUF_NEUTRAL -> BUF_COMPRESSION` fill has no known switch-to-switch travel. The
firmware correctly treats that fill as near-zero physical travel, so the fill
estimator cannot produce later corrections. The now-clean COMPRESSION drain is
the available anchor, but the current drain gate requires `300 ms`; the observed
true-stop dwells are mostly about `200 ms`, so those clean samples are skipped.

### §10d implementation plan

#### `firmware/src/sync.c`

- Lower the type-D COMPRESSION drain estimator dwell floor enough to accept the
  observed feed-zero true-stop exits while still rejecting instant bounce.
- Keep the existing feed gate (`mmu_avg_sps <= SYNC_MIN_SPS`) so this only
  accepts drained samples after the commanded feed has collapsed to zero.
- Preserve the fill estimator as the first full-span anchor and keep Type-P
  untouched.

## Hardware follow-up 13: same-side compression fill has zero known travel (2026-06-02)

The §10d rerun still froze: `EST` stayed `896.6` for the whole log while the
applied feed settled around `640-660`. The run had only two transitions:

- `NEUTRAL -> COMPRESSION` after about `0.96 s` at `MM 652-661`
- `COMPRESSION -> NEUTRAL`, then another long `NEUTRAL -> COMPRESSION` after
  about `3.8 s` with late `MM 640-654`

The important correction is that the later fill is a **same-side compression
return**: after `COMPRESSION -> NEUTRAL`, the next `NEUTRAL -> COMPRESSION`
does not have known switch-to-switch travel. Treating it as a geometric fill
or drain sample either skips it or recomputes the stale plateau. But the dwell's
pre-taper applied-feed mean is exactly the useful upper-bound demand signal:
the controller found a calm feed that held the physical buffer in NEUTRAL for
seconds before the next overfeed touch.

### §10e implementation plan

#### `firmware/src/sync.c`

- Allow the estimator block to run for type-D `NEUTRAL -> COMPRESSION` even when
  the computed switch travel is near zero.
- If the `NEUTRAL -> COMPRESSION` sample has known travel, keep the existing
  `demand = F_avg - fill_rate` path.
- If the sample has no known travel, treat `F_avg` itself as an overfeed
  upper-bound demand sample and blend it into `extruder_est_sps`.
- Continue preferring the pre-taper feed average from §10c so compression-side
  braking does not poison the sample.
- Keep Type-P untouched.

## Hardware follow-up 14: real-print speed steps need reserve headroom (2026-06-02)

The 300 mm/min outer-wall / 1500 mm/min infill benchmark exposed a different
failure class from the earlier estimator latch. With `SYNC_RELAY_TRIM_STEP_SPS`
raised to `1500` and clamp to `9000`, the buffer still reached near-TENSION and
the operator observed skipping. The log shows why:

- Type-D now parks around virtual `BP:0` during long steady phases.
- Slow/compression-heavy phases can pull `EST` down (`1422 -> 673`, later
  `963 -> 794`), which is reasonable for the current flow but leaves little
  reserve when the slicer jumps to fast infill.
- The next speed-up can consume the remaining neutral travel before a switch
  event has time to teach the controller. More trim authority cannot predict an
  upcoming speed step; it only reacts after the buffer has already moved.

This is a reserve-headroom problem, not a neutral-frac or trim-step problem.
Now that §10e gives the Type-D estimator a usable demand anchor, restore a
compression-side reserve target for Type-D using the existing
`SYNC_RESERVE_PCT` knob. Type-P already uses that reserve target; Type-D had
been temporarily pinned to center during the de-pinning work so downward trim
could become live. The anti-tension floor and relay-base cap are now
demand-scaled, so the old baseline pin should not return.

### §10f implementation plan

#### `firmware/src/sync.c`

- Change `buf_target_reserve_mm()` for `BUF_SENSOR_TYPE == 0` from a hard
  `0.0f` center target to `threshold * SYNC_RESERVE_PCT / 100`.
- Keep the Type-D target clamped to the same physical threshold guard as Type-P.
- Do not change Type-P control law or estimator paths.

#### Docs / validation

- Update operator docs to explain that Type-D now parks slightly
  compression-side for print speed-step headroom.
- Validate firmware build, OpenSpec strict validation, Python compile, and the
  existing script suites.

## SYNC_RELIEF_PAUSE deadlock root cause and fix (2026-06-02)

### Root Cause
During a print pause, the buffer stays at `BUF_COMPRESSION` and eventually auto-stops, setting the state to `SYNC_RELIEF_PAUSE` (`ST:3`). In this state, `sync_tick()` returns early, so physical switch debouncing is ignored during the tick.
Negative stabilization (`buffer_stabilize_tick`) starts backing off the motor to `BUF_NEUTRAL`.
The raw switch opens and immediately sets `raw_state == BUF_NEUTRAL`, which makes `buffer_stabilize_tick` stop stabilization and clear `g_boot_stabilizing = false`.
However, because `buf_read_stable` has a debounce delay (`BUF_HYST_MS`), the physical transition is debounced and calls `buf_update(BUF_NEUTRAL)` *while* `g_boot_stabilizing` is still true (during the backward movement). This skips the re-arm recovery. Once stabilization completes and `g_boot_stabilizing` is false, no new transition occurs, leaving the buffer physically at `BUF_NEUTRAL` and stuck in `SYNC_RELIEF_PAUSE` (`ST:3`) forever. Hitting `BUF_TENSION` later does not trigger the normal `sync_tick()` auto-start because `sync_tick()` returns early.

### Fix
Instead of relying purely on the transient `buf_update()` transition event, proactively check for exit conditions at the start of `sync_tick()` when `g_sync_state == SYNC_RELIEF_PAUSE`:
If `s == BUF_TENSION` (extruder pulling filament), or `s == BUF_NEUTRAL` and a print is active (`A->task == TASK_FEED`), recover and auto-start sync immediately.

This guarantees recovery even if a transition event is raced or skipped, while protecting end-of-print idle from spurious auto-starts.

## Soften type-D tension-fallback multiplier (2026-06-02)

### The Issue
Under slow prints with long steady NEUTRAL dwells, physical drift eventually touches the TENSION switch. Because the entry was from TENSION, there is zero switch-to-switch travel. The no-travel fallback estimated `est_sps = feed_avg_sps * 1.5f`.
If `feed_avg_sps` was already around `770 sps` (above true demand `~650`), multiplying by `1.5f` immediately yanked `EST` up to `1085 sps`. This created a positive feedback loop of aggressive overfeeding, slamming the buffer back to COMPRESSION.

### The Fix
Soften the no-travel fallback multiplier in `neutral_drain_sample` from `1.5f` to `1.15f`. This provides a gentle overfeed correction on tension hits without yanking `EST` out of convergence.

## Raise sync_ramp_accel to 500 mm/s² (2026-06-02)

### The Issue
During sudden print speed steps (e.g. infill speed jumps), the buffer leans heavily toward TENSION. The current `sync_ramp_accel` of `300 mm/s²` ramps up the MMU feed rate too slowly to keep pace with the extruder's acceleration, causing transient starvation.

### The Fix
Raise `sync_ramp_accel` to `500 mm/s²`. This increases the loop's closed-loop bandwidth and slew rate by 1.67x, allowing the MMU to match fast speed jumps immediately and prevent TENSION-side starvation.

## Lower sync_ramp_decel to 150 mm/s² (2026-06-02)

### The Issue
On exiting TENSION catch-up, the MMU feed rate drops instantly from `3000 mm/min` back to the neutral target (`~800 mm/min`) because the default `sync_ramp_decel` of `300 mm/s²` is too steep. This sudden collapse in feed rate immediately starves the buffer again, causing it to bounce right back into a TENSION starvation pin.

### The Fix
Add and lower `sync_ramp_decel` to `150 mm/s²`. This makes the deceleration ramp gentle, preventing sudden speed collapses and allowing the buffer to settle smoothly in NEUTRAL.


## Dynamic-flow TENSION drift (2026-06-02) — the trim ratchet

### Symptom

After the steady-state detune (frac → 1.00), a **constant-feed** soak is clean,
but a real print stepping demand hard (300 mm/min outer walls ↔ 1500 mm/min
infill) regresses: every COMPRESSION touch slams feed to `0`, then the buffer
drifts slowly toward TENSION "when unsure", grazing starvation. HW poll shows
`MM` sitting a few percent **below** `EST` for the whole NEUTRAL dwell, e.g. at
`EST = 814.7 mm/min`, `MM` enters NEUTRAL near `770` and only climbs back to
`~811` as the dwell ages.

### Root cause: the two-sided neutral trim ratchets negative

NEUTRAL feed = `(int)(demand_sps × relay_neutral_frac + g_relay_neutral_trim_sps)`
(`relay_control_law`, `sync.c:2034`). The trim (`sync.c:1246`) is a slow
integrator over switch crossings:

```
trim -= SYNC_RELAY_TRIM_STEP_SPS   on NEUTRAL → COMPRESSION   (STEP = 300 sps)
trim += SYNC_RELAY_TRIM_STEP_SPS   on NEUTRAL → TENSION
trim  +75 sps/s leak toward 0      after 500 ms in NEUTRAL    (LEAK_RATE_FRAC 0.25)
clamp ±SYNC_RELAY_TRIM_CLAMP_SPS   (= ±2000 sps)
```

Unit scale: `MM_PER_STEP = 0.0024437` → `1 sps = 0.1466 mm/min`. So
`EST 814 mm/min = 5552 sps`; one trim step `300 sps = 44 mm/min`; clamp
`2000 sps = 293 mm/min`; leak `75 sps/s = 11 mm/min/s`.

The trim's premise is that crossings indicate a *standing feed bias* to null
out. That holds for a constant-feed soak. Under demand-stepped flow the
crossings are *demand transients* (the wall↔infill steps), and the cycle is
**COMPRESSION-dominated** (a 1500→300 step overfills before EST catches up). So
the trim ratchets negative — `−44 mm/min` per COMPRESSION touch, leak too slow
to recover — bottoming toward the `−293 mm/min` clamp. Negative trim makes
NEUTRAL feed < demand, so the dead-reckoned virtual position
(`buf_virtual_position_tick`, `sync.c:430`: `g_buf_pos += (feed − EST)·dt`)
integrates **downward toward TENSION**. The observed `MM ≈ 770` climbing to
`~811` is exactly the negative trim leaking back out — too late, the buffer has
already drifted.

`relay_base` clamp ruled out: `baseline_control_floor_sps ≈ BF 2400 mm/min =
16367 sps ≫ demand 5552`, never binds. `(int)` truncation ≤ 1 sps, negligible.

### Why a frac raise does not fix it

`relay_neutral_frac 1.0 → 1.1` adds `0.1 × 5552 = 555 sps = +81 mm/min`. One
COMPRESSION touch (`−44`) stays under it, but **two** touches (`−88`) exceed it
and the clamp (`−293`) buries it. In a COMPRESSION-dominated cycle the trim
dominates any modest lean. The trim must be fixed; lean on top of it is masking.

### The two complaints are one limit cycle

```
 COMP rail ┤█──┐  feed → 0 (hard true-stop) = MAX drain      ← "drops too hard"
           │   └──┐    (upper turning point dumps the whole span)
   NEUTRAL ┤      └──┐  feed = demand + (negative trim) < demand
           │         └──┐   (slow drift)
 TENS rail ┤            █   ← "drifts to TENSION when unsure" = starvation
```

Hard-`0` on COMPRESSION guarantees a full rail-to-rail drain → large, slow cycle
that always swings near TENSION. At 300 mm/min walls the extruder barely draws,
so `0`-feed pins the buffer full, then resumes from `0`, undershoots, drifts.
The hard-`0` *manufactures* the TENSION excursion.

### Fix — remove a bias, bound the amplitude (do not stack leans)

Reframe the objective as a **small asymmetric limit cycle hugging the
COMPRESSION rail** (benign, self-recalibrating) instead of unobservable mid-band
regulation. Minimal pair, attacking both turning points:

1. **One-sided trim** (`sync.c:1242-1249`). Drop the `NEUTRAL → COMPRESSION`
   down-step. Keep the `NEUTRAL → TENSION` up-step (fast anti-starvation) and
   the neutral leak. Trim becomes non-negative — it can only ever raise feed,
   never push toward TENSION. Steady overfeed is corrected by the
   `extruder_est_sps` crossing estimator (`neutral_fill_sample`, `sync.c:1145`),
   so the down-step was redundant and, under dynamic flow, harmful.

2. **Gated COMPRESSION partial-drain** (`sync.c:2499`). Replace the
   unconditional `target_sps = 0` with: while the extruder actively draws
   (estimated demand above an idle threshold), `target_sps =
   SYNC_COMPRESSION_DRAIN_FRAC × extruder_est_sps`, clamped strictly below
   demand so the buffer cannot net-fill while pinned. Keep `0` when demand ≈ 0 /
   `TASK_IDLE` — that is the purge/idle no-grind case the hard-`0` was added for
   ([[purge-grind-root-cause-macro]], [[compression-overfeed-stop]]); the two
   needs do not conflict once gated on demand. Suggested default
   `SYNC_COMPRESSION_DRAIN_FRAC ≈ 0.4` (rig-tuned).

Deferred fallbacks (only if the pair under-leans on HW), recorded not shipped:
- small `relay_neutral_frac` raise (now effective, nothing eating it), and/or
- a **time-since-crossing back-off** replacing the dead confidence probe
  (`sync.c:2388`, `uncertainty × 6 sps ≈ 0.9 mm/min` — comment claims 150
  mm/min; both the `×6` coefficient and the sqrt·0.025-scaled confidence make it
  inert). The principled version scales the lean by ticks-since-last-switch
  (intermittent-observation estimate-covariance growth), capped, reset on any
  touch.

### Control-theory framing (why this is the right shape)

- **Åström–Hägglund relay feedback / auto-tuning** — the limit cycle is itself a
  demand identifier; the crossing estimator (`arm_vel_mm_s = travel_mm /
  effective_dwell`, `sync.c:1133`) already does this. Identification is *not* the
  gap; the control objective is.
- **Kalman filtering with intermittent observations** (Sinopoli et al., 2004) —
  estimate covariance grows between measurements → justifies a
  time-since-crossing back-off rather than the sigma gate.
- **Constraint tightening / back-off in robust & stochastic MPC** (Mayne;
  Richards–How) — back off from the *dangerous* constraint (TENSION) by a margin
  sized to uncertainty. Formalizes "lean to the safe rail".
- **Σ-Δ / PWM relay modulation** — duty-cycling the drain depth sets the
  operating point between two discrete levels; maps onto
  `SYNC_COMPRESSION_DRAIN_FRAC`.

### Risks

- **Lean pile-up.** frac + trim + reserve + drain-`k` all act on the same
  feed-vs-demand axis; stacking is hard to reason about and can re-oscillate.
  Mitigation: ship only the *removal* (one-sided trim) + amplitude bound
  (drain-`k`); hold lean additions as deferred fallbacks.
- **Steady-soak regression.** The two-sided trim aided constant-feed
  convergence; removing the down-step shifts that onto the EST estimator. Must
  re-verify the constant-feed soak (the case that currently passes).
- **No mid-band ground truth** ([[typed-buffer-no-midband-groundtruth]]). Both
  levers are open-loop bets validated only at the next crossing; prior mid-band
  fixes failed on HW. Mitigation: every change biases toward the *benign* rail,
  so mis-tuning fails **full** (brief stall), not **starved** (defect).
- **HW-only validation.** Cycle shape is not unit-testable; needs rig soak with
  the analyzer.

### Analyzer + tuning guide

Read-only host analyzer (`scripts/`, alongside `flare_cmd.py --poll` /
`flare_sync_check.py`) over the poll stream. All metrics from existing fields
(`BP`, `BUF`, `MM`, `EST`) — no new firmware telemetry:

| Metric | From | Meaning |
|--------|------|---------|
| `tension_touches` | `BUF` → TENSION count | the hard constraint (target `0`) |
| `comp_pin_ms` | consecutive COMPRESSION w/ `MM ≈ 0` | "drops too hard" severity |
| `mean(EST − MM)` in NEUTRAL | `EST`, `MM` | underfeed / trim-ratchet signature (>0 = drifting TENSION) |
| `bp_min`, `bp_mean` | `BP` | proximity to the dangerous rail |
| `cycle_period`, amplitude | `BP` zero-crossings | cycle size |

Verdict is asymmetric: **PASS only when `tension_touches == 0`**; remediation
names `relay_neutral_frac` / COMPRESSION-drain / trim, never `sync_kp_rate` for
type-D.

`TUNING.md` tuning sequence (near-1-D search):
1. Run a representative print (e.g. 300/1500 mm/min). Baseline the analyzer.
2. Deploy one-sided trim; re-measure `tension_touches`.
3. Still > 0? Raise `relay_neutral_frac` in small steps until `tension_touches =
   0` (now effective — no negative trim to eat it).
4. Lower `SYNC_COMPRESSION_DRAIN_FRAC` until `comp_pin_ms` is comfortable (no
   grind/slam) while `tension_touches` stays `0`.
5. Guard: watch `EV:BUF,cannot_refill` / `EST_LOW_CF`.

### Settings / migration

`SYNC_COMPRESSION_DRAIN_FRAC` is a new config-backed runtime knob (SET/GET,
clamped e.g. `[0.0, 0.9]`) so the guide can sweep it live. It is not persisted
and does not bump `SETTINGS_VERSION`, because the current version-mismatch path
would reset saved TMC/calibration fields. `0.0` disables the new drain path for
legacy hard-stop A/B testing. One-sided trim and gated drain are
`BUF_SENSOR_TYPE == 0` only; type-P paths untouched.

### §15 implementation plan (2026-06-02)

Research read: this design section, the §15 task list, `buf_update()`,
`relay_neutral_trim_clamp()`, `relay_neutral_trim_leak()`, and
`relay_control_law()`. The root cause is the `BUF_NEUTRAL -> BUF_COMPRESSION`
down-step at the crossing handler; clamp/leak/reset behavior can remain as-is.

Plan:
- `firmware/src/sync.c`: remove only the COMPRESSION trim down-step and keep the
  TENSION up-step under the existing `BUF_SENSOR_TYPE == 0` crossing gate.
  Leave the EST-jump reset (`trim *= 0.25f`) and neutral leak untouched.
- `openspec/changes/relay-neutral-frac-detune/tasks.md`: mark §15.1-§15.4
  complete after code review + validation. Leave §15.5 HW pending until real
  hardware constant-feed soak results exist.

Risk/invariant: trim must never become a negative feed bias from crossing
updates, and analog type-P (`BUF_SENSOR_TYPE == 1`) must remain untouched.

### §16 implementation plan / flag decision (2026-06-02)

Research read: `settings_store.c` version/load path, `protocol.c` SET/GET
surface, `controller_shared.h`, `gen_config.py`, `config.ini*`, `flare_cmd.py
--dump`, and `sync_tick()` around the type-D COMPRESSION true-stop. The
settings load path still calls `settings_defaults()` and returns on any
`SETTINGS_VERSION` mismatch, so bumping the version would wipe persisted
TMC/calibration values instead of preserving old fields plus defaulting the new
field.

Plan:
- Add `SYNC_COMPRESSION_DRAIN_FRAC` as a config-backed runtime global and
  `SET:`/`GET:` knob, but do not add it to `settings_t` and do not bump
  `SETTINGS_VERSION`. This makes it reboot-defaulted from `config.ini` and live
  testable without risking saved settings.
- Use the fraction itself as the A/B guard: `0.0` means legacy hard-zero
  COMPRESSION stop; `>0.0` enables the gated partial-drain path.
- In `firmware/src/sync.c`, keep the existing `fast_brake` branch first and keep
  the type-D COMPRESSION branch separate from the later `SYNC_MIN_SPS` clamp.
  Only normal active-draw COMPRESSION (`A->task == TASK_FEED` and demand above
  idle threshold) gets `frac * demand`, clamped strictly below demand; idle or
  disabled stays `0`.
- Update docs/OpenSpec to call the knob runtime-only (not persisted) until a
  non-wiping settings migration exists.

Risk/invariant: purge/idle COMPRESSION must still true-stop at `0`; analog
type-P (`BUF_SENSOR_TYPE == 1`) must remain untouched.

### §17 implementation plan (2026-06-03)

Research read: §17 tasks, analyzer/tuning-guide design, `scripts/flare_sync_check.py`,
and `scripts/test_flare_sync_check.py`. Existing checker already parses `OK:`
fields and supports log/live/daemon capture, so the lowest-risk path is adding
an `asymmetric` mode there instead of duplicating capture/parsing code.

Plan:
- `scripts/flare_sync_check.py`: add `analyze_asymmetric()` that uses only
  `BP`, `BUF`, `MM`, and `EST`. Metrics: sample/event TENSION touches,
  COMPRESSION pin total/max, NEUTRAL `mean(EST-MM)`, BP min/mean/max, and
  mean relay touch period. Verdict is asymmetric: FAIL if any TENSION touch,
  PASS otherwise; no `sync_kp_rate` remediation.
- Add `--branch-label` so A/B captures can be labeled (`hard-stop`,
  `partial-drain`, `trim-off`, etc.) without changing telemetry. Firmware branch
  guards remain runtime knobs: `SYNC_COMPRESSION_DRAIN_FRAC=0.0` disables the
  drain branch, and `SYNC_RELAY_TRIM_STEP_SPS=0` disables the trim branch.
- `scripts/test_flare_sync_check.py`: add parser/metric/verdict coverage.
- `TUNING.md`, `MANUAL.md`, `BEHAVIOR.md`, OpenSpec task notes: document the
  asymmetric analyzer and branch-test commands.

Risk/invariant: analyzer is read-only and must not require new firmware fields;
type-P control remains untouched.

### §7.5 obsolete decision (2026-06-03)

§7.5 was written for the earlier two-sided crossing trim: short
`BUF_NEUTRAL -> BUF_COMPRESSION` dwell meant severe overfeed, so the proposed v2
would weight the COMPRESSION down-step. §15 supersedes that path by making trim
one-sided: COMPRESSION touches must not subtract from trim because a negative
trim can push NEUTRAL feed below demand and drift the buffer toward TENSION.

The surviving idea is not a §7 trim tweak. Dwell/uncertainty-scaled lean now
belongs to the deferred time-since-crossing back-off described in "Dynamic-flow
TENSION drift", applied as a positive safe-rail lean rather than a COMPRESSION
down-step. Keep §7.5 obsolete unless hardware proves the deferred fallback is
needed.

### §19 implementation plan (2026-06-03)

Research read: the §19 task block, this design's dynamic-flow section,
`g_sync_relieve_effort_mm` reset/accumulation sites, the §16 type-D COMPRESSION
branch in `sync_tick()`, and the non-persisted `SYNC_COMPRESSION_DRAIN_FRAC`
runtime plumbing. The regression is not task-state detection: pause captures
show `A->task == TASK_FEED` and stale `EST` throughout the pause. The reliable
signal is the already-tracked COMPRESSION relieve distance.

Plan:
- `firmware/src/sync.c`: add only
  `g_sync_relieve_effort_mm < SYNC_COMPRESSION_DRAIN_BUDGET_MM` to the existing
  Type-D active-draw partial-drain gate. Keep the existing `else target_sps = 0`
  as the over-budget hard-stop and keep the branch above the `SYNC_MIN_SPS`
  clamp.
- `SYNC_COMPRESSION_DRAIN_BUDGET_MM`: clone the non-persisted
  `SYNC_COMPRESSION_DRAIN_FRAC` surface as a config-backed runtime float:
  default `3.0`, clamp `[0.0, 25.0]`, SET/GET/live-dump support, no
  `settings_t` field, no `SETTINGS_VERSION` bump. `0.0` means immediate
  hard-stop for legacy A/B.
- `openspec/changes/relay-neutral-frac-detune/tasks.md`: mark §19.1-§19.4 after
  build/tests/OpenSpec pass; leave §19.5 HW unchecked.

Risk/invariant: applies only under `BUF_SENSOR_TYPE == 0 && s == BUF_COMPRESSION`;
analog type-P COMPRESSION/relief behavior remains byte-for-byte outside the new
global declaration/plumbing.

## Dynamic-flow TENSION skip — asymmetric EST attack (2026-06-03)

### Symptom
On the real print, slow→fast outer-wall→infill steps (300→1500 mm/min) still
drive rare TENSION touches, and the extruder **skips** as the buffer approaches
empty (defect, not just a telemetry touch). The operator cannot change slicer/
print settings, so the demand step cannot be softened upstream.

### What the soaks proved
Exhaustive HW sweeps (reserve 35–70, accel 500–700, decel 150–700, frac
1.0–1.55, catchup 1.3–1.6) map a frontier and a wall:
- **reserve** = cheap position headroom (35→55 cut tension with no compression
  cost); cliffs past ~65.
- **frac** (standing overfeed) = louder compression, does NOT fix step-tension
  (a standing lean cannot catch a step) and never reaches zero; measurement is
  noise-dominated (frac1.45 reran 3→11 tension).
- **decel** was stuck slow (150): on a 1500→300 down-step feed crawled down →
  sustained overfeed → COMPRESSION flood. `decel700` fixed the audible noise
  (`EST−MM −200→−11`, `COMP pin 9600→2200 ms`). **Keep decel ≈ 700.**
- With `accel700 + decel700` (both fast) TENSION still did not drop → **ramp
  rate is not the up-step limit.**

### Root cause of the residual skip
Type-D `extruder_est_sps` only updates at switch crossings, blended in
`blend_extruder_est_sps` at `alpha ∈ [EST_ALPHA_MIN 0.12, EST_ALPHA_MAX 0.65]`,
derived from dwell. A sharp step-up produces a *short* dwell to the corrective
crossing → *low* alpha → the high-demand sample is blended in only ~12–65 % →
feed catches the step over several crossings. The buffer drains to TENSION
(skip) before EST converges. The slow attack — worst exactly when the step is
sharpest — is the skip source. Ramp/headroom cannot fix it because feed cannot
rise until EST does.

### Fix: asymmetric EST attack (fast-up, slow-down)
In `blend_extruder_est_sps`, when the new sample is **higher** than current EST
(rising demand / step-up), blend at a fast attack alpha
`SYNC_EST_ATTACK_ALPHA` (~0.8, above `EST_ALPHA_MAX`); when lower (falling
demand), keep the existing slow clamped EMA. The first crossing after a step-up
snaps EST to the new demand → feed ramps (accel700) → catches before TENSION.
Attacks the lag directly, so no standing overfeed and no compression-noise
penalty; `decel700` keeps the down-step clean underneath.

Scope: `BUF_SENSOR_TYPE == 0` only — analog type-P uses a separate per-tick
estimator (`sync.c:2004`, gated type-P) and must stay byte-identical. Risk: a
noisy crossing reading high could spike EST → feed blip → a compression touch;
bounded by `clamp_est_sps` + `type_d_sample_demand_bounds` (samples already
validated) and attack alpha < 1.0. Knob is runtime/non-persisted so the rig can
sweep `0.65→1.0`.

### Expectation
Targets the up-step skip at its source — the most promising lever left within
type-D. It will not beat the worst *instantaneous* step (the buffer must still
drain to *a* crossing first), so a hard floor may remain; if so, the only
escapes are slicer (ruled out) or type-P hardware (continuous sensor →
predictive soft-wall/velocity feedforward).

## Protocol long-parameter regression (2026-06-03)

Rig command replay showed `SET:SYNC_COMPRESSION_DRAIN_BUDGET_MM:3.0` returning
`ER:SET:ARG` and `GET:SYNC_COMPRESSION_DRAIN_BUDGET_MM` returning
`ER:GET:UNKNOWN_PARAM`, while adjacent knobs worked. The name is exactly
32 characters; `protocol.c` parses SET/GET names into `char param[32]` with
`%31[^:]`, so the token cannot include both all 32 characters and a null
terminator. The §19 knob existed in firmware but was unreachable by protocol.

Plan:
- `firmware/src/protocol.c`: raise SET/GET parameter token buffers to 64 bytes,
  use `%63[^:]`, and explicitly null-terminate copied base/GET parameters.
  Leave values and reply buffers unchanged.
- `scripts/test_protocol_param_width.py`: static regression test asserting the
  longest public SET/GET parameter name fits the protocol token buffer and scan
  width.
- `openspec/changes/relay-neutral-frac-detune/tasks.md`: mark §21 done after
  build, regression, Python, script tests, and OpenSpec strict pass.

Risk/invariant: only command parsing width changes; command semantics and type-P
paths do not change.

## RESOLUTION — the feed floor was the regression (2026-06-03)

The "asymmetric EST attack" note above expected a possible hard floor with only
type-P as escape. HW disproved the "structural wall": **a feed floor fixes it.**

Root cause of the slow→fast skip = §9 demand-scaled away the `baseline·0.70`
NEUTRAL feed floor. Without it, NEUTRAL feed falls to ~demand during the slow
outer wall, then lags the 300→1500 step; the buffer drains to TENSION before any
crossing updates the estimate → skip. Reactive levers (reserve, frac, accel,
EST-attack) can't prevent it — a 2-switch buffer has no mid-band observation, so
the drain from mid-band to tension crosses no switch. The **floor pre-empts** the
deficit: `SYNC_MIN_RATE` holds feed up through the wall so the step has no
deficit and the buffer never drains.

HW: floor sweep `800/900/950/1000` → only **1000** gives zero TENSION with margin
(BP min −1.13). `SYNC_MIN_RATE:1000` shipped as default (type-D is the default
sensor). Cost: compression-noisy on slow sections (the floor overfeeds them to
pre-empt the step) — grind-safe via §16/§19, and exactly the pre-change "noisy
but works" behavior. **There is no quiet + zero-skip on type-D**; the noise is
intrinsic to pre-empting an unobservable demand step. Quiet *and* skip-free needs
type-P (continuous sensor → predictive feedforward).

decel700 independently fixed a separate compression-noise source (slow decel
flooded compression on step-downs). EST-attack + decel are retained
(harmless-to-helpful; decel fixed real noise) but the floor is the skip fix.

Final shipped type-D defaults: `SYNC_MIN_RATE 1000 / SYNC_RESERVE_PCT 65 /
SYNC_RAMP_ACCEL 700 / SYNC_RAMP_DECEL 700 / RELAY_NEUTRAL_FRAC 1.0 /
RELAY_CATCHUP_FRAC 1.3 / SYNC_EST_ATTACK_ALPHA 0.8 /
SYNC_COMPRESSION_DRAIN_FRAC 0.4 / SYNC_COMPRESSION_DRAIN_BUDGET_MM 3.0`.
