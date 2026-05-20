## Why

The archived `relay-duty-estimator-and-tuning` change closed task 7.5
claiming a "slow/shallow/never-TENSION" steady state, but that property
was only ever validated on the **unconfident/fallback** path. On-hardware
Phase-A reprints of the 60×60 mm bimodal cube (captures r3–r6) prove the
**confident relay-estimator path — the real-print default on a flip-heavy
bimodal model — fails that bar**: it deep-TENSION-bangbangs, the buffer
rides the +12.5 mm empty wall on every fast burst (~356 ≈1 s episodes),
and the only thing that fixes it is forcing the estimator never-confident
so the demand-tracking fallback drives instead. The fix is a small
default change, but the archived change is immutable history, so it needs
its own change with the on-hw evidence recorded.

## What Changes

- **Harden the relay confidence-gate default (primary fix).** Raise
  `relay_confidence_cycles` and/or shorten `relay_confidence_window_ms`
  defaults so a flip-heavy bimodal print no longer trivially satisfies
  the gate (today: 8 cycles / 60000 ms → estimator confident ~67–71 % of
  rows). Result: such prints stay unconfident → the `extruder_est_sps`
  fallback (≈ true demand) drives NEUTRAL — exactly the archived D10
  intent ("estimator is recovery arbitrator, NOT the driver"). Decisive
  test r6 (`SET:RELAY_CONF_WINDOW_MS:1000`): `RDE` active 0 %, `BPmax`
  12.5 → 5.09 mm, buffer shallow within the ±5 mm switch span, no
  bottoming out.
- **Enable the D8 anti-chatter `relay_min_flip_mm` (residual damper).**
  Currently `0.0` (off, `sync.c:695`). Ship a non-zero default as a
  flip-frequency damper on both buffer sides, layered on top of the gate
  fix. Value tuned on-hw.
- **Expose the deep-COMPRESSION collapse-ramp constants as config.**
  `SYNC_COMPRESSION_COLLAPSE_DELAY_MS` / `_RAMP_MULT` / `_CAP_MS`
  (`sync.c:19-21`, compile-time `#define`s) become config keys so the
  deep-full stop can be softened **after** the gate fix rebalances the
  buffer toward the intended full-lean (today it is under-exercised /
  empty-biased, so tuning it now would be blind).
- **On-hardware A/B validation.** After the new defaults land, rerun a
  Phase-A A/B (one slow-only model + one fast/bimodal model) vs the
  archived §0.1 locked 4.2 baseline; record results. This is the
  hardware validation the archived 7.5 never did for the confident path.
- Record (do **not** decide) the open architectural question with the
  r3–r6 evidence: if the gate is hardened enough that the confident path
  ~never engages on real prints, is it worth keeping at all vs a
  fallback-only relay (echoes the archived D12 open question).

No change to the relay control law itself (TENSION catch-up /
COMPRESSION `SYNC_MIN` / NEUTRAL demand-track, `sync.c:1786-1820`); no
change to the analyzer (D12 fill-anchor stays as shipped). This is a
defaults + config-surface + validation change.

## Capabilities

### New Capabilities
- `relay-confidence-gate`: when the type-D relay confidence gate is
  satisfied vs not, the hardened default that keeps flip-heavy bimodal
  prints on the fallback driver, the `relay_min_flip_mm` anti-chatter
  default, and the deep-COMPRESSION collapse-ramp config surface.

### Modified Capabilities
- `operator-tuning-guide`: relay tuning section gains the hardened
  confidence-gate defaults, the `relay_min_flip_mm` knob, the new
  collapse-ramp config keys, and the bimodal-print failure-mode note.

## Impact

- Config / generated header: `config.ini`, `config.ini.example`,
  `scripts/gen_config.py` (relay_confidence_*, relay_min_flip_mm, new
  collapse-ramp keys), generated `tune.h` defines.
- Firmware: `firmware/src/sync.c:19-21` (collapse-ramp constants →
  config-driven), confidence gate `sync.c:228-231`. No control-law edit.
  `protocol.c` SET/GET surface already exposes `RELAY_CONF_CYCLES` /
  `RELAY_CONF_WINDOW_MS` (~748) — unchanged, defaults shift.
- Docs: `TUNING.md` relay section.
- Tests: `scripts/test_gen_config.py` (new/changed keys), firmware build.
- No analyzer change; archived `relay-duty-estimator-and-tuning`
  untouched (immutable). Cross-refs its D10/D12/D13.
