## Why

Type-P (PSF analog sensor) support exists in firmware but is broken: symmetric
calibration with an explicit invert flag replaced proper endpoint calibration,
the PD loop has a unit mismatch that corrupts position error, the compression
floor logic is polarity-inverted, and type-D and type-P share no PD
infrastructure despite identical math. PSF hardware now exists; these must be
fixed before any analog rig session.

Beyond the bug fixes, type-P fundamentally changes the control problem. Type-D
is an *estimator*: it reconstructs an unobservable continuous buffer position
from sparse binary switch events, which is why roughly half of `sync.c`'s
type-specific code is estimator-compensation (dead-reckoning, drift observer,
sigma/confidence, model-stall detection, bias caps to keep crossings frequent).
Type-P *measures* position directly at 50Hz, so that machinery is not ported —
it is deleted. With continuous position the controller gains a live extruder-rate
estimate every tick (vs only at crossings), enabling smooth PD+derivative
control, intentional quality biasing, predictive wall handling, and fast-move
reaction that the bang-bang type-D path cannot achieve.

## What Changes

- **BREAKING**: Remove `BUF_RANGE` and `BUF_INVERT` runtime params; replace
  with `BUF_PSF_MAX_COMP`, `BUF_PSF_MAX_TENS`, `BUF_PSF_NEUTRAL`, `BUF_GOAL`
  (all raw ADC [0,1] space, same convention as Happy Hare)
- Remove `BUF_THR` from type-P zone detection; replace with goal-relative zone
  boundaries derived from `BUF_GOAL` + fixed deadband
- Add calibration commands `CAL:PSF_COMP`, `CAL:PSF_TENS`, `CAL:PSF_NEUT`
- Replace symmetric normalization in `buf_analog_update()` with asymmetric
  HH-style mapping; polarity auto-derived from `max_comp` vs `max_tens` values
  (no explicit invert flag)
- Fix unit mismatch in PD loop: both sensor types normalized to common [-1,1]
  scale before shared PD math via `buf_pos_norm()` / `buf_target_norm()`
- Delete type-P early-return branch in `sync_apply_scaling()` — unified path
  handles both
- Extract `relay_control_law()` (type-D) and add `psf_control_law()` (type-P)
  replacing scattered `BUF_SENSOR_TYPE == 0` control branches
- Extract `buf_signal_publish()` from `buf_sensor_tick()`
- Remove inverted `compression_floor` block (L1750) for type-P
- Resolve deferred items from `pending-analog-rig`: #6 compression_floor
  removed, #7 compression_recovery timing rig-verified, H2 estimator drag
  direction rig-verified
- Add `PIN_PSF` (GP29 = ADC3) to `config.h` — already done

### Control redesign (continuous-measurement path)

- **Continuous extruder estimator**: derive `vel_norm` from per-tick position
  delta; feed `extruder_mm_s = mmu_mm_s + arm_vel` live every tick instead of
  only at crossings. For type-P, delete the estimator-compensation machinery it
  replaces (virtual position tick, drift observer, sigma/confidence, model-stall
  detection, EST_FALLBACK, estimator alpha adaptation) — all gated to type-D.
- **Layer 1 — gradual PD control**: `psf_control_law()` becomes
  `target = extruder_est + Kp*error_norm + Kd*vel_norm_filtered`, ramp-limited.
  Smooth speed that grows/shrinks with position and its rate (no bang-bang).
- **Layer 2 — soft walls**: progressive blend from PD toward safety limit across
  `|norm| ∈ [0.8, 1.0]` (raw 0.9–1.0 / 0.1–0.0). TENSION wall → blend toward
  max feed (urgent refill); COMPRESSION wall → blend toward 0 (stop overfeed).
- **Layer 3 — hard catch + print-stop detection**: derivative-triggered
  reversible `fast_brake` on rapid `vel_norm` spike; two-stage stop/slowdown
  disambiguation (brake, then classify by whether buffer drifts back toward
  tension within a confirm window → resume, or stays pinned → `relief_pause`);
  saturation-sustained → `relief_pause` (COMPRESSION) or `fault_hold` (TENSION).
- **Control dead zone**: P term dead-zoned near goal (`|error_norm| <
  PSF_CTRL_DEADBAND` → P=0) to reject ADC jitter; D term always active so fast
  moves are still caught instantly. Velocity is filtered (derivative-on-
  measurement) to avoid derivative kick.
- **Intentional biasing**: `BUF_GOAL` becomes a pure print-quality bias (no
  estimator-freshness caps), replacing type-D's `buf_target_reserve_mm` H1/H2
  bias-cap logic for the type-P path.
- **Loop-rate bump (type-P)**: raise type-P sensor/control tick above 50Hz
  (ADC read is cheap) to improve fast-move reaction; validate on rig.
- **Tip-shaping stretch goal**: validate whether continuous fast-move catching
  lets the host drop manual retract triggers during toolchange tip forming.
  Gated on loop-rate bump + rig validation of buffer travel/accel headroom.
  Stretch — measured hypothesis, not a committed deliverable.

## Capabilities

### New Capabilities
- `psf-type-p-sensor`: Complete calibrated type-P (PSF analog) sensor support:
  asymmetric endpoint calibration, auto-polarity, goal-relative zone control,
  calibration commands, continuous-measurement PD+derivative control with soft/
  hard walls and print-stop catching, control dead zone, and unified PD
  infrastructure shared with type-D.

### Modified Capabilities
- `sync-feedback`: PD control infrastructure refactored to operate on a common
  normalized scale for both type-D and type-P; control laws isolated per type;
  estimator-compensation machinery gated strictly to type-D.

## Impact

- `firmware/src/sync.c`: `buf_analog_update()`, `buf_state_raw()`,
  `sync_apply_scaling()`, `sync_tick()` PD entry and control law blocks,
  new `buf_pos_norm()`, `buf_target_norm()`, `relay_control_law()`,
  `psf_control_law()`, `buf_signal_publish()`, remove compression_floor
- `firmware/src/protocol.c`: remove BUF_RANGE/BUF_INVERT GET/SET; add new
  param GET/SET; add CAL:PSF_COMP/TENS/NEUT handlers
- `firmware/src/settings_store.c`: replace buf_range/buf_invert NVM fields
  with buf_psf_max_comp/max_tens/neutral/goal; update save/load/defaults
- `firmware/src/main.c`: variable declarations
- `firmware/include/controller_shared.h`: externs
- `firmware/include/tune.h`: defaults
- `openspec/changes/pending-analog-rig/tasks.md`: mark #6 resolved; #7 and H2
  marked pending rig validation
