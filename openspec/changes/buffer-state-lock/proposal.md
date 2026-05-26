## Why

Tip-forming and pre-toolchange unload currently feed the buffer through a blind,
open-loop MMU retract issued from Klipper:

```gcode
RUN_SHELL_COMMAND CMD=flare PARAMS="MV:-{mmu_tip_retract}:{park_speed*60*0.2}:I"
```

This single `:I` (ignore-buffer) move is sized (`mmu_tip_retract` ≈ 117mm) to
asynchronously cover **two** separate extruder retracts that straddle an `M400`:

| # | move | distance | speed |
|---|------|----------|-------|
| 1 | park retract (`_FLARE_TIP_FORMING`, `G0 E-{park_distance}`) | ~30mm | `park_speed` = 140mm/s |
| 2 | gear clear (`_FLARE_UNLOAD_TOOLHEAD`, `G1 E-{gear_retract}`) | ~43mm | `speed_hub_to_extruder` = 50mm/s |

Because it ignores the buffer guard, it tolerates full COMPRESSION↔TENSION
swings with no closed-loop protection. It hard-codes magic distances/feedrates
in the macro, runs blind, and a prior reactive replacement
(`SYNC_RETRACT_ASSIST`, commit `f19f41a`) was gutted because it reacted only at
the **compression switch** — far too late for the ramp to catch, so it now sits
as a no-op shell.

We want the firmware to handle the buffer naturally during printer-side
retracts, with the macro reduced to a simple "arm" call instead of an
open-loop distance/feedrate.

## What Changes

- **NEW command `BL` (Buffer Lock):** drive the active lane to push the buffer
  to a chosen extreme (tension or compression) via a bounded half-max-travel
  move, then **lock** (hold position, motor energized, zero net feed) until the
  lock is broken by a non-MMU (printer/extruder) force **or** released manually
  by `BS`. Locking pre-charges the full ~20mm of buffer runway in the direction
  opposite the expected retract.
- **NEW lock-break catch behavior:** when an external force departs the buffer
  from the locked extreme, the firmware hands off to an **aggressive slam-mirror**
  (bypass the slow PD ramp, jump straight to target SPS — the same instant
  `current_sps = target` mechanism the old `retract_assist_drive` used) so the
  MMU rides the runway instead of waiting for a switch crossing.
- **Asymmetric-safety law:** armed at deep tension, over-draining back toward
  tension is the *safe* direction (recoverable); only a compression slam is a
  grind failure. The catch therefore biases aggressive without grind risk.
- **Repurpose the `SYNC_RETRACT_ASSIST` shell** (or add `SYNC_BUFFER_LOCK`) as
  the home state for prime → lock → catch → settle, replacing the dead
  reactive-at-compression logic.
- **Remove the legacy `RA:1` / `RA:0` host commands** and the `RA` status
  field. `RA` is unused by Klipper and any external host today, so it is
  deleted outright rather than aliased — the new surface is `BL` only.
- **Klipper:** replace the blind `MV:...:I` with two `BL:T` arm calls — one
  before each extruder retract (the `G0 E-{park_distance}` park retract in
  `_FLARE_TIP_FORMING` and the `G1 E-{gear_retract}` gear clear in
  `_FLARE_UNLOAD_TOOLHEAD`) — each followed by a fixed settle pause
  (`G4 P1000`, ~1s) so the half-travel prime drives to deep tension and the
  lock energizes before the printer-side retract starts. Each retract gets a
  freshly-primed full runway. Remove the magic `mmu_tip_retract` distance.
- **Document HW limitations and the survival envelope** (min MMU speed vs move
  distance/speed) in design, with the explicit finding that the 140mm/s park
  retract is the binding wall.

## Capabilities

### New Capabilities
- `buffer-state-lock`: The `BL` command and its prime → lock → lock-break →
  catch → settle lifecycle, including the half-max-travel prime, the
  hold/lock contract, the non-MMU-force unlock trigger, the manual `BS`
  override, and the aggressive slam-mirror catch with asymmetric safety.

### Modified Capabilities
- `sync-state-model`: Add the buffer-lock lifecycle state and its transitions
  (prime, locked, catch, settle) to the explicit sync state machine; redefine
  `SYNC_RETRACT_ASSIST` from the gutted reactive-at-compression shell to the
  armed-runway model.
- `motion-safety`: Define how `BL` and its lock-break catch interact with the
  existing `MV` retract/tension and forward/compression fault guards, and the
  bounded travel limit on the prime move.

## Impact

- `firmware/src/sync.c`: implement prime/lock/catch/settle; restore an
  aggressive slam drive; wire lock-break detection to buffer state departure.
- `firmware/src/protocol.c`: add `BL` command parse + handler; add `BL` to
  status GET; keep `BS` as the manual unlock.
- `firmware/src/motion.c`: ensure the lock hold and slam catch respect (or
  intentionally bypass, when armed) the `MV` buffer fault guards.
- `klipper/flare_mmu.cfg`: replace the blind `MV:...:I` in `_FLARE_TIP_FORMING`
  with `BL` arm calls around each retract; drop `mmu_tip_retract`.
- Tuning: a new max-rate / slam constraint — the catch is only viable when the
  MMU top speed meets the survival envelope (see design); otherwise `park_speed`
  must be bounded. No config schema break, but `global_max_rate` becomes
  load-bearing for this feature.
