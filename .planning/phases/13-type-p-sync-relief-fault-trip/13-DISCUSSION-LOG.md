# Phase 13: Type-P Sync Relief & Fault Trip - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-09-12
**Phase:** 13-type-p-sync-relief-fault-trip
**Areas discussed:** Relief snap floor & slew, mm-trip accounting, Feed probe design, Rail-relative triggers, Knob surface & persistence, Suppression during deliberate holds, HW validation acceptance

User framing: "provide recommended solutions where my input can be skipped" and "act more like PO of this project, technical details and code bits look elvish to me". Every question carried a recommended-first option; the user took the recommendation on every question except the HW pass bar, where all four criteria were selected plus a sim-first constraint.

---

## Relief snap floor & slew

| Option | Description | Selected |
|--------|-------------|----------|
| max(est×mult, baseline_sps) | Learned per-lane baseline as floor; SYNC_MIN_RATE only if baseline 0 | ✓ |
| max(est×mult, SYNC_MIN_RATE) | Existing min-rate knob as floor | |
| Fall back to max_sps snap | Keep today's behaviour when est unknown | |

| Option | Description | Selected |
|--------|-------------|----------|
| Slew-limited at 2× SYNC_PSF_SLEW_PER_MM | Route through type-P smoothing, doubled cap | ✓ |
| Instant snap to bounded target | Keep jump, cap target | |
| Snap first tick then 2× slew | Hybrid | |

| Option | Description | Selected |
|--------|-------------|----------|
| Distance-clocked, keep 1 sps floor | Consistent with type-P path | ✓ |
| Wall-clock min step while pegged | Guarantees progress; frozen-clock overfeed risk | |

| Option | Description | Selected |
|--------|-------------|----------|
| Cap ramp-delay path by same bound | No type-P path commands raw max_sps | ✓ |
| Leave as explicit override | Documented exception | |

| Option | Description | Selected |
|--------|-------------|----------|
| Tension-only, compression deferred | SC#1 scope; re-measure | ✓ |
| Bound both sides now | Symmetric HH | |

| Option | Description | Selected |
|--------|-------------|----------|
| EV:SYNC:RELIEF_ON/OFF + existing polling | Edge events only | ✓ |
| New ST: field | Parser updates needed | |
| No new telemetry | | |

**Notes:** none.

---

## mm-trip accounting

| Option | Description | Selected |
|--------|-------------|----------|
| Raw lane feed travel while pegged | Firmware-local, no estimate | ✓ |
| Feed in excess of est demand | HH-like but circular for FLARE | |

| Option | Description | Selected |
|--------|-------------|----------|
| 32 mm (2× buffer) | Primary trip before 6 s at typical flow | ✓ |
| 16 mm | Tighter | |
| 0 = disabled | Opt-in | |

| Option | Description | Selected |
|--------|-------------|----------|
| Same as ms trip: escalation then FAULT_HOLD | Reuse block, reason in event | ✓ |
| Straight FAULT_HOLD | Distinct clog class | |

| Option | Description | Selected |
|--------|-------------|----------|
| Any BUF state change after AUTO_START | Supersedes restart-on-activation | ✓ |
| Transition OR near-neutral (HH exact) | Absolute-ish compare | |

| Option | Description | Selected |
|--------|-------------|----------|
| TM: + ARM: in extended ST: tail | Same tail as TT: | ✓ |
| Events only | | |

| Option | Description | Selected |
|--------|-------------|----------|
| No compression mm trip | Deferred | ✓ |
| Add SYNC_COMPRESSION_STOP_MM | Symmetric | |

| Option | Description | Selected |
|--------|-------------|----------|
| Reset at all tension_pin reset sites + activation | mm/ms never drift | ✓ |
| Reset only on leaving TENSION | Minimal HH | |

---

## Feed probe design

| Option | Description | Selected |
|--------|-------------|----------|
| Auto once per pinned episode, armed-gated, + PROBE: cmd | One decision point, no host round-trip | ✓ |
| Explicit PROBE: only | Host decides | |
| Auto on every pin, no arming | Boot false-trips | |

| Option | Description | Selected |
|--------|-------------|----------|
| PR:<0-3> in ST: tail + EV:SYNC:PROBE:* | Latched enum | ✓ |
| Events only | | |

| Option | Description | Selected |
|--------|-------------|----------|
| NO_CONSUMER → straight to runout escalation | Skip trip budget | ✓ |
| Annotate only | Diagnostic this phase | |

| Option | Description | Selected |
|--------|-------------|----------|
| Firmware-only consumer | Daemon mirrors field | ✓ |
| Daemon interpretation too | Phase 14/15 | |

| Option | Description | Selected |
|--------|-------------|----------|
| Passive window over relief feed | No new motion state | ✓ |
| Distinct JOIN_RATE probe motion | Suspend sync | |

| Option | Description | Selected |
|--------|-------------|----------|
| Single accumulator, PROBE_MM then STOP_MM | Ordering structural | ✓ |
| Separate counters | | |

---

## Rail-relative triggers

| Option | Description | Selected |
|--------|-------------|----------|
| Per-sync-window extreme + fixed margin | g_bl_lock_extreme pattern; TENSION state before first extreme | ✓ |
| Scale -0.8 by observed rail | Absolute until first excursion | |
| BUF state only | Later entry | |

| Option | Description | Selected |
|--------|-------------|----------|
| Reuse BL_BREAK_DELTA_NORM | One validated delta | ✓ |
| New SYNC_PROBE_DELTA_NORM | | |

| Option | Description | Selected |
|--------|-------------|----------|
| Every new scenario at rail_scale 1.0 and 0.7 | bl_retract_immediate pattern | ✓ |
| Dedicated relief-fires-at-0.5 tripwire | 51bdca8 failure class | (adopted into D-25 as well) |
| Only SC#4 scenarios at 1.0 | | |

| Option | Description | Selected |
|--------|-------------|----------|
| Leave psf_control_law blend alone | Capped by bound; follow-up | ✓ |
| Make blend rail-relative | Touches 16 PSF traces | |

---

## Knob surface & persistence

| Option | Description | Selected |
|--------|-------------|----------|
| Two headline knobs live (RELIEF_MULT, TENSION_STOP_MM) | Probe distance + margin are constants | ✓ |
| All four live | | |
| Nothing live | | |

| Option | Description | Selected |
|--------|-------------|----------|
| On by default (32 mm) | ms timer remains fallback | ✓ |
| Off by default | | |

---

## Suppression during deliberate holds

| Option | Description | Selected |
|--------|-------------|----------|
| All three paused; counters reset at hold end | Zero new false jams during toolchanges | ✓ |
| Relief active, trip+probe paused | Two controllers fighting | |

| Option | Description | Selected |
|--------|-------------|----------|
| Freeze while idle, keep progress | Counter naturally doesn't grow | ✓ |
| Reset on every resume | | |

| Option | Description | Selected |
|--------|-------------|----------|
| Sensor truth wins; probe/trip skip on clear IN | Probe is for the ambiguous case | ✓ |
| Run anyway | | |

---

## HW validation acceptance

| Option | Description | Selected |
|--------|-------------|----------|
| Same 2 h / ~10-swap print, same rig, same watch | Directly comparable to baseline | ✓ |
| Short 20-min print | | |
| Two full prints | Follow-up | |

| Option | Description | Selected |
|--------|-------------|----------|
| ≤ 1 RELIEF_PAUSE / 60 s sustained sync | Headline symptom | ✓ |
| Fewer TENSION_RISK_HIGH than 13 | Tension side | ✓ |
| Zero false faults | Trip/probe must not fire on healthy print | ✓ |
| No rail hits whole print | Chosen; recorded rail-relative in D-31.4 | ✓ |

**Notes:** "But try to validate using sim whatever can be certainly validated, to avoid wasting real filament" → D-32 sim-first ordering rule.

---

## Claude's Discretion

- Placement/naming inside `sync.c`, `EV:` spelling, TLV tag numbering, exact `SOFT_WALL_MARGIN_NORM` in 0.10–0.20, sim scenario naming.
- `PROBE:` wiring via the `new-cmd` skill.
- Which existing PSF scenario traces change shape after removing the instant snap (pass/fail outcome must not change).

## Deferred Ideas

- Compression-side bounded relief.
- Compression-side mm trip (`SYNC_COMPRESSION_STOP_MM`).
- Rail-relative `psf_control_law` blend.
- Daemon-side interpretation of `PR:`.
- Continuous step-rate trace in the watch capture.
